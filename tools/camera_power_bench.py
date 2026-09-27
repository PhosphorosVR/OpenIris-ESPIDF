"""
Camera power bench (CAM_CE / CAM_RESET)
=======================================

Sends get_camera_status and a series of camera_power_cycle commands over the
USB serial port, prints one summary line per cycle and stores every raw
response as one JSON line (--out). Firmware needs CONFIG_CAMERA_POWER_CONTROL.

The device must be in setup mode: switch_mode setup, restart, then any command
within the startup delay. camera_power_cycle is refused while UVC streams.

Usage:
    uv run tools/camera_power_bench.py --port COM5 --status
    uv run tools/camera_power_bench.py --port COM5 --cycles 50 --trace --out proof_eye_l.jsonl
    uv run tools/camera_power_bench.py --port COM5 --off-ms 5000     # DMM on 1V5_Cx during the 5 s
    uv run tools/camera_power_bench.py --port COM5 --force           # board without the lines
"""

import argparse
import json
import os
import sys
import time

import serial

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from openiris_device import OpenIrisDevice  # noqa: E402

# Reinit plus first frame can take several seconds on top of the off time.
CYCLE_TIMEOUT_MARGIN_S = 30


def connect_quietly(port: str, debug: bool) -> OpenIrisDevice | None:
    """Open with DTR/RTS already low. OpenIrisDevice.connect() lowers them only after
    opening, and on USB-Serial-JTAG that edge resets the chip (rst:0x15), which would
    wipe the state and the reset reason this bench is meant to observe."""
    device = OpenIrisDevice(port, debug, debug)
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 1
    connection.write_timeout = 1
    connection.dtr = False
    connection.rts = False
    try:
        connection.open()
    except serial.SerialException as e:
        print(f"Failed to open {port}: {e}")
        return None
    device.connection = connection
    device.connected = True
    return device


def result_of(response: dict) -> dict:
    try:
        return response["results"][0]["result"]
    except (KeyError, IndexError, TypeError):
        return {"status": "error", "data": response}


def summarize(index: int, result: dict) -> str:
    data = result.get("data")
    if not isinstance(data, dict) or "rail" not in data:
        return f"#{index:3}  {result.get('status')}: {json.dumps(data)}"
    rail = data["rail"]
    steps = data.get("steps", {})
    rise = f"{rail['rise_us'] / 1000:.1f} ms" if rail.get("rise_us", -1) >= 0 else "-"
    return (
        f"#{index:3}  {data.get('result'):13} rail {rail['verdict']:13} "
        f"control {rail['control_mv']} end {rail['end_mv']} off {rail['off_ms']} ms rise {rise}  "
        f"reinit {steps.get('reinit')} frame {data.get('first_frame_ms')} ms  "
        f"pid {data.get('pid_before')}->{data.get('pid_after')}  {data.get('duration_ms')} ms"
        + (f"  FAILED at {data['failed_step']}" if "failed_step" in data else "")
    )


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", required=True)
    p.add_argument("--status", action="store_true", help="only get_camera_status")
    p.add_argument("--cycles", type=int, default=1)
    p.add_argument("--off-ms", type=int, default=500)
    p.add_argument("--trace", action="store_true", help="record fall and rise of the camera rail")
    p.add_argument("--force", action="store_true", help="run the sequence even if the probe found no lines")
    p.add_argument("--pause-s", type=float, default=1.0, help="pause between cycles")
    p.add_argument("--out", help="append every response as a JSON line")
    p.add_argument("--debug", action="store_true")
    args = p.parse_args()

    out = open(args.out, "a", encoding="utf-8") if args.out else None

    def record(kind: str, response: dict):
        if out:
            out.write(json.dumps({"time": time.time(), "port": args.port, "kind": kind, "response": response}) + "\n")
            out.flush()

    device = connect_quietly(args.port, args.debug)
    if device is None:
        return 2
    try:
        status = device.send_command("get_camera_status")
        record("status", status)
        print(json.dumps(result_of(status), indent=2))
        if args.status:
            return 0 if result_of(status).get("status") == "success" else 1

        params = {"off_ms": args.off_ms, "trace": args.trace, "force": args.force}
        failures = 0
        done = 0
        verdicts: dict = {}
        for i in range(1, args.cycles + 1):
            done = i
            response = device.send_command("camera_power_cycle", params, timeout=args.off_ms // 1000 + CYCLE_TIMEOUT_MARGIN_S)
            record("power_cycle", response)
            result = result_of(response)
            print(summarize(i, result), flush=True)
            data = result.get("data") if isinstance(result.get("data"), dict) else {}
            verdict = data.get("rail", {}).get("verdict", "none")
            verdicts[verdict] = verdicts.get(verdict, 0) + 1
            if result.get("status") != "success":
                failures += 1
                if data.get("error") in ("busy", "not_supported", "Command timeout"):
                    # refused, or the device stopped answering: more commands only pile up
                    break
            if i < args.cycles:
                time.sleep(args.pause_s)
        print(f"\n{done} cycle(s), {failures} failed, rail verdicts: {verdicts}")
        return 0 if failures == 0 else 1
    finally:
        device.disconnect()
        if out:
            out.close()


if __name__ == "__main__":
    sys.exit(main())
