"""
Camera recovery check (CAMERA_RECOVERY_ENABLE firmware)
=======================================================

Repeats the hardware checks of AP3/AP4 (docs/CAM_CE_RESET_Analyse.md, 17.7):
recovery while the UVC streams run, recovery after the host closed the camera,
automatic recovery from a provoked outage, and the camera status.

The port is the device's CDC port in UVC mode (or the setup-mode port). Connecting
does not reset the board. Stream watching needs OpenCV and reads every UVC camera the
host sees (OpenCV has no device names, so gaps are reported per camera index; stop
other apps that use the cameras first).

Usage:
    uv run tools/camera_recovery_check.py status --port COM767
    uv run --with opencv-python tools/camera_recovery_check.py recover --port COM767 --count 20 --watch
    uv run --with opencv-python tools/camera_recovery_check.py after-close --port COM767 --count 5
    uv run --with opencv-python tools/camera_recovery_check.py watch --seconds 20
    uv run --with opencv-python tools/camera_recovery_check.py fault --port COM767 --kind hold_reset
        (fault needs a build with CONFIG_CAMERA_TEST_HOOKS=y)

Exit code 0: everything passed.
"""

import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from camera_power_bench import connect_quietly, result_of  # noqa: E402

COOLDOWN_S = 5.3  # the firmware refuses restarts within 5 s of the last one
RECOVER_TIMEOUT_S = 40
STALL_MS = 500  # a frame interval above this counts as standstill (normal: ~33 ms)


def import_cv2():
    try:
        import cv2
    except ImportError:
        sys.exit("OpenCV is missing: run with  uv run --with opencv-python tools/camera_recovery_check.py ...")
    return cv2


class StreamWatcher:
    """Reads every UVC camera in its own thread and records frame times."""

    def __init__(self, max_index: int = 4):
        self.cv2 = import_cv2()
        self.max_index = max_index
        self.stop_event = threading.Event()
        self.stamps: dict = {}
        self.threads = []
        self.t0 = 0.0

    def _read(self, idx: int):
        cap = self.cv2.VideoCapture(idx, self.cv2.CAP_DSHOW if os.name == "nt" else self.cv2.CAP_ANY)
        if not cap.isOpened():
            return
        stamps = self.stamps.setdefault(idx, [])
        while not self.stop_event.is_set():
            ok, frame = cap.read()
            if ok and frame is not None:
                stamps.append(time.time() - self.t0)
        cap.release()

    def start(self, settle_s: float = 3.0):
        self.t0 = time.time()
        self.threads = [threading.Thread(target=self._read, args=(i,), daemon=True) for i in range(self.max_index)]
        for t in self.threads:
            t.start()
        time.sleep(settle_s)

    def stop(self) -> dict:
        self.stop_event.set()
        for t in self.threads:
            t.join(timeout=5)
        return self.stamps

    @staticmethod
    def report(stamps: dict, gap_ms: float) -> dict:
        out = {}
        for idx, s in sorted(stamps.items()):
            if len(s) < 2:
                out[idx] = {"frames": len(s)}
                continue
            intervals = [(s[i] - s[i - 1]) * 1000 for i in range(1, len(s))]
            gaps = [round(x) for x in intervals if x > gap_ms]
            out[idx] = {
                "frames": len(s),
                "fps": round(len(s) / (s[-1] - s[0]), 1),
                "gaps_over_ms": gap_ms,
                "gaps": len(gaps),
                "longest_gap_ms": max(gaps) if gaps else 0,
                # During an outage DirectShow repeats the last frame about once a second,
                # so an outage shows up as a run of ~1 s intervals rather than one long gap.
                "stalled_ms": round(sum(x for x in intervals if x > STALL_MS)),
            }
        return out


def status(device) -> dict:
    result = result_of(device.send_command("get_camera_status"))
    if result.get("status") != "success":
        sys.exit(f"get_camera_status failed: {result}")
    return result["data"]


def recover(device, level: str) -> dict:
    response = device.send_command("recover_camera", {"level": level}, timeout=RECOVER_TIMEOUT_S)
    result = result_of(response)
    data = result.get("data") if isinstance(result.get("data"), dict) else {"error": result.get("data")}
    return data


def summary_line(data: dict) -> str:
    outcome = data.get("result") or data.get("error")
    if data.get("failed_step"):
        outcome = f"failed at {data['failed_step']}"
    rail = data.get("rail", {}).get("verdict", "-")
    return f"{outcome:24} {data.get('performed', '-'):12} rail {rail:13} {data.get('duration_ms', '-')} ms"


def cmd_status(args) -> int:
    device = connect_quietly(args.port, False)
    if device is None:
        return 2
    try:
        print(json.dumps(status(device), indent=2))
    finally:
        device.disconnect()
    return 0


def cmd_watch(args) -> int:
    watcher = StreamWatcher()
    watcher.start(settle_s=0)
    time.sleep(args.seconds)
    print(json.dumps(StreamWatcher.report(watcher.stop(), args.gap_ms), indent=2))
    return 0


def run_recoveries(device, count: int, level: str, before_each=None) -> tuple:
    outcomes: dict = {}
    durations = []
    heap = []
    first = status(device)["heap_internal"]
    heap.append(first["largest_block"])
    for i in range(1, count + 1):
        if before_each:
            before_each()
        data = recover(device, level)
        print(f"#{i:3}  {summary_line(data)}", flush=True)
        key = data.get("result") or data.get("error") or "unknown"
        if data.get("failed_step"):
            key = f"failed:{data['failed_step']}"
        outcomes[key] = outcomes.get(key, 0) + 1
        if key == "recovered":
            durations.append(data.get("duration_ms", 0))
        if i % 10 == 0 or i == count:
            heap.append(status(device)["heap_internal"]["largest_block"])
        if i < count:
            time.sleep(COOLDOWN_S)
    return outcomes, durations, heap


def cmd_recover(args) -> int:
    device = connect_quietly(args.port, False)
    if device is None:
        return 2
    watcher = StreamWatcher() if args.watch else None
    try:
        if watcher:
            watcher.start()
        outcomes, durations, heap = run_recoveries(device, args.count, args.level)
        final = status(device)
    finally:
        stamps = watcher.stop() if watcher else None
        device.disconnect()

    report = {
        "outcomes": outcomes,
        "duration_ms": [min(durations), max(durations)] if durations else None,
        "heap_largest_block": heap,
        "camera_state": final["state"],
        "gate": final.get("gate"),
    }
    if stamps is not None:
        # each recovery pauses one stream for 0.7-2.5 s; anything longer is suspicious
        report["streams"] = StreamWatcher.report(stamps, args.gap_ms)
    print(json.dumps(report, indent=2))
    ok = outcomes.get("recovered", 0) == args.count and final["state"] == "running"
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_after_close(args) -> int:
    """The case that once failed with drain_timeout: host opens and closes the camera,
    then a restart has to take back the frame of the unfinished transfer."""
    cv2 = import_cv2()
    device = connect_quietly(args.port, False)
    if device is None:
        return 2

    def open_and_close():
        for idx in range(4):
            cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY)
            if cap.isOpened():
                end = time.time() + 2
                while time.time() < end:
                    cap.read()
            cap.release()
        time.sleep(1)

    try:
        outcomes, durations, heap = run_recoveries(device, args.count, "auto", before_each=open_and_close)
        final = status(device)
    finally:
        device.disconnect()
    print(json.dumps({"outcomes": outcomes, "gate": final.get("gate"), "heap_largest_block": heap}, indent=2))
    ok = outcomes.get("recovered", 0) == args.count
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_fault(args) -> int:
    """Provoke an outage and wait for the automatic recovery (frame_timeout)."""
    device = connect_quietly(args.port, False)
    if device is None:
        return 2
    watcher = StreamWatcher()
    try:
        before = status(device)["recovery"]["by_trigger"]["frame_timeout"]
        watcher.start()
        result = result_of(device.send_command("camera_test_fault", {"kind": args.kind}))
        if result.get("status") != "success":
            print(f"camera_test_fault refused: {result.get('data')} (needs CONFIG_CAMERA_TEST_HOOKS=y)")
            return 2
        # driver timeout 4-8 s, then about 1.2 s restart
        deadline = time.time() + args.wait_s
        after = before
        while time.time() < deadline:
            time.sleep(1)
            after = status(device)["recovery"]["by_trigger"]["frame_timeout"]
            if after["successes"] > before["successes"]:
                break
        time.sleep(3)  # let the stream settle again
        final = status(device)
    finally:
        stamps = watcher.stop()
        device.disconnect()

    last = final["recovery"]["last"][0] if final["recovery"]["last"] else None
    print(json.dumps({"frame_timeout_before": before, "frame_timeout_after": after, "last": last,
                      "streams": StreamWatcher.report(stamps, 1500)}, indent=2))
    ok = after["successes"] > before["successes"] and final["state"] == "running"
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("status", help="print get_camera_status")
    s.add_argument("--port", required=True)
    s.set_defaults(func=cmd_status)

    w = sub.add_parser("watch", help="read all UVC cameras and report fps and gaps")
    w.add_argument("--seconds", type=float, default=20)
    w.add_argument("--gap-ms", type=float, default=300)
    w.set_defaults(func=cmd_watch)

    r = sub.add_parser("recover", help="repeated recover_camera, optionally while watching the streams")
    r.add_argument("--port", required=True)
    r.add_argument("--count", type=int, default=10)
    r.add_argument("--level", default="auto", choices=["auto", "reinit", "reset", "power_cycle"])
    r.add_argument("--watch", action="store_true", help="keep all UVC streams open and report gaps")
    r.add_argument("--gap-ms", type=float, default=3000)
    r.set_defaults(func=cmd_recover)

    a = sub.add_parser("after-close", help="open and close the cameras on the host, then recover")
    a.add_argument("--port", required=True)
    a.add_argument("--count", type=int, default=5)
    a.set_defaults(func=cmd_after_close)

    f = sub.add_parser("fault", help="provoke an outage (test hooks) and wait for the automatic recovery")
    f.add_argument("--port", required=True)
    f.add_argument("--kind", default="hold_reset", choices=["hold_reset", "sensor_standby"])
    f.add_argument("--wait-s", type=float, default=30)
    f.set_defaults(func=cmd_fault)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
