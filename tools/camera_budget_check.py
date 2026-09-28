"""
Camera recovery budget check (test firmware with CONFIG_CAMERA_TEST_HOOKS=y)
============================================================================

Checks the recovery budget on hardware (docs/ABNAHME_KAMERA_POWER.md, section 4).
Outages are provoked with camera_test_fault hold_reset; the device detects them as
frame_timeout and restarts the camera by itself.

  held     Automatic recoveries spaced over 30 s: every one has to hold (frames for
           30 s), none may be refused, nothing gets suspended.
  suspend  Three recoveries that do not hold (a new fault a few seconds after each one)
           suspend the automatic triggers; resume_in_s counts down and the suspension
           lifts by itself after 5 min; a run of 30 s holds again.
           With CONFIG_CAMERA_RECOVERY_ESP_RESTART=y additionally: the first suspension
           restarts the ESP, a second one right after that boot does not, and after a
           held run the next suspension restarts it again.

All UVC cameras the host sees are kept open (OpenCV), so the device has a consumer;
they are reopened after the ESP restarted. Stop other apps that use the cameras first.
The port is the device's CDC port in UVC mode; connecting does not reset the board.

Usage:
    uv run --with opencv-python tools/camera_budget_check.py held --port COM767 --count 11
    uv run --with opencv-python tools/camera_budget_check.py suspend --port COM767

Exit code 0: everything passed. Takes about 45 s per held recovery and 7 min (12 min with
the ESP restart option) for suspend.
"""

import argparse
import json
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from camera_power_bench import result_of  # noqa: E402
from openiris_device import OpenIrisDevice  # noqa: E402

HOLD_S = 30  # firmware: a restart holds after 30 s of frames
RESUME_S = 300  # firmware: quiet time that lifts a suspension
RESTART_DELAY_S = 12  # firmware: ESP restart after one log flush interval + 2 s
KEYS = ("suspended", "resume_in_s", "unheld_in_row", "unheld_limit", "held", "not_held", "suspensions", "resumes",
        "restarted_by_recovery", "esp_restart_armed")

T0 = time.time()
LOG_FILE = None


def log(msg: str, **fields):
    line = f"{time.strftime('%H:%M:%S')} +{time.time() - T0:5.0f}s  {msg}" + (f"  {json.dumps(fields)}" if fields else "")
    print(line, flush=True)
    if LOG_FILE:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(line + "\n")


def import_cv2():
    try:
        import cv2
    except ImportError:
        sys.exit("OpenCV is missing: run with  uv run --with opencv-python tools/camera_budget_check.py ...")
    return cv2


class Device:
    """Command connection that survives the ESP restarting."""

    def __init__(self, port: str):
        self.port = port
        self.device = None

    def present(self) -> bool:
        from serial.tools import list_ports

        return any(p.device == self.port for p in list_ports.comports())

    def close(self):
        if self.device:
            try:
                self.device.disconnect()
            except Exception:
                pass
        self.device = None

    def command(self, name: str, data: dict | None = None) -> dict | None:
        for _ in range(2):
            if self.device is None:
                if not self.present():
                    return None
                device = OpenIrisDevice(self.port, False, False)
                device.connect()
                if not device.is_connected():
                    return None
                self.device = device
            response = self.device.send_command(name, data, timeout=10)
            if "results" in response:
                return result_of(response)
            self.close()
        return None

    def status(self) -> dict | None:
        result = self.command("get_camera_status")
        return result["data"] if result and result.get("status") == "success" else None


class Streams:
    """Keeps every UVC camera open and reading; reopens a camera after read failures."""

    def __init__(self, max_index: int = 4):
        self.cv2 = import_cv2()
        self.max_index = max_index
        self.stop_event = None
        self.threads = []

    def _read(self, idx: int, stop_event: threading.Event):
        cv2 = self.cv2
        while not stop_event.is_set():
            cap = cv2.VideoCapture(idx, cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY)
            if not cap.isOpened():
                cap.release()
                stop_event.wait(2)
                continue
            failures = 0
            while not stop_event.is_set() and failures <= 20:
                ok, _ = cap.read()
                failures = 0 if ok else failures + 1
                if not ok:
                    time.sleep(0.05)
            cap.release()

    def start(self):
        self.stop_event = threading.Event()
        self.threads = [threading.Thread(target=self._read, args=(i, self.stop_event), daemon=True) for i in range(self.max_index)]
        for thread in self.threads:
            thread.start()

    def stop(self):
        if self.stop_event:
            self.stop_event.set()
            for thread in self.threads:
                thread.join(timeout=10)

    def reopen(self):
        # after a USB re-enumeration the camera indices can shift
        self.stop()
        time.sleep(1)
        self.start()


def brief(st: dict) -> dict:
    r = st["recovery"]
    out = {k: r[k] for k in KEYS if k in r}
    ft = r["by_trigger"]["frame_timeout"]
    out.update(frame_timeout=f"{ft['successes']}/{ft['attempts']}", refused=r["refused"], state=st["state"],
               uptime_s=st["uptime_s"], reset=st["reset_reason"], heap_block=st["heap_internal"]["largest_block"])
    return out


def wait_for(device: Device, condition, timeout_s: float, interval_s: float = 1.0) -> dict | None:
    end = time.time() + timeout_s
    while time.time() < end:
        st = device.status()
        if st and condition(st):
            return st
        time.sleep(interval_s)
    return None


def recoveries(st: dict) -> int:
    return st["recovery"]["by_trigger"]["frame_timeout"]["successes"]


def fault(device: Device) -> bool:
    result = device.command("camera_test_fault", {"kind": "hold_reset"})
    if not result or result.get("status") != "success":
        log("camera_test_fault refused (needs CONFIG_CAMERA_TEST_HOOKS=y)", result=result)
        return False
    return True


def automatic_recovery(device: Device, before: dict) -> dict | None:
    """Provoke an outage and wait for the automatic recovery (driver timeout 4-8 s)."""
    if not fault(device):
        return None
    return wait_for(device, lambda s: recoveries(s) > recoveries(before), 45)


def cmd_held(device: Device, count: int) -> bool:
    start = device.status()
    log("held: start", **brief(start))
    first = None
    passed = True
    for i in range(1, count + 1):
        before = device.status()
        if automatic_recovery(device, before) is None:
            log(f"#{i}: no automatic recovery within 45 s")
            return False
        first = first or time.time()
        time.sleep(HOLD_S + 5)
        st = device.status()
        r = st["recovery"]
        ok = (r["held"] == before["recovery"]["held"] + 1 and r["unheld_in_row"] == 0 and not r["suspended"]
              and r["refused"] == start["recovery"]["refused"])
        passed = passed and ok
        log(f"#{i}: {'held' if ok else 'NOT AS EXPECTED'}", last_held=r["last"][0].get("held"), **brief(st))
    log(f"held: {count} automatic recoveries in {(time.time() - first) / 60:.1f} min, all held: {passed}")
    return passed


def unheld_series(device: Device, label: str) -> dict | None:
    """One recovery, then three that do not hold (a new fault shortly after each one).
    Returns the status right after the suspension."""
    st = device.status()
    log(f"{label}: start", **brief(st))
    st = automatic_recovery(device, st)
    if st is None:
        log(f"{label}: no automatic recovery")
        return None
    for k in (1, 2, 3):
        time.sleep(3)
        before = st
        if not fault(device):
            return None
        if k < 3:
            st = wait_for(device, lambda s: s["recovery"]["not_held"] > before["recovery"]["not_held"]
                          and recoveries(s) > recoveries(before), 45)
            if st is None:
                log(f"{label}: no 'not held' / recovery {k}")
                return None
            log(f"{label}: not held {k}, recovered again", **brief(st))
        else:
            st = wait_for(device, lambda s: s["recovery"]["suspended"], 45, interval_s=0.5)
            if st is None:
                log(f"{label}: not suspended after the third")
                return None
            log(f"{label}: suspended", **brief(st))
    return st


def check_resume(device: Device, st: dict, label: str, expect_restart_armed: bool | None) -> bool:
    """Suspended without an ESP restart: resume_in_s counts down, the suspension lifts by
    itself, and a run of 30 s holds (which also re-arms the ESP restart)."""
    t_suspended = time.time()
    r = st["recovery"]
    first = r.get("resume_in_s")
    shown = isinstance(first, int) and RESUME_S - 10 <= first <= RESUME_S and r.get("unheld_in_row") == r.get("unheld_limit")
    log(f"{label}: resume_in_s {first} at the suspension, unheld {r.get('unheld_in_row')}/{r.get('unheld_limit')}: "
        f"{'ok' if shown else 'NOT AS EXPECTED'}")
    uptime = st["uptime_s"]
    time.sleep(30)
    st = device.status()
    later = st["recovery"].get("resume_in_s") if st else None
    counting = isinstance(later, int) and isinstance(first, int) and 25 <= first - later <= 35
    no_restart = st is not None and st["uptime_s"] >= uptime + 25 and st["recovery"]["suspended"]
    log(f"{label}: 30 s later resume_in_s {later}, counting down: {counting}, no ESP restart: {no_restart}",
        **(brief(st) if st else {}))
    st = wait_for(device, lambda s: s["recovery"]["resumes"] > r["resumes"], RESUME_S + 120, interval_s=5)
    if st is None:
        log(f"{label}: no resume within {RESUME_S + 120} s")
        return False
    log(f"{label}: resumed {time.time() - t_suspended:.0f} s after the suspension (5 min + wait for the next "
        "trigger + poll interval 5 s)", **brief(st))
    cleared = st["recovery"]["resume_in_s"] is None and not st["recovery"]["suspended"]
    held = wait_for(device, lambda s: s["recovery"]["unheld_in_row"] == 0 and s["recovery"]["held"] > st["recovery"]["held"]
                    and (expect_restart_armed is None or s["recovery"]["esp_restart_armed"] == expect_restart_armed),
                    HOLD_S + 60)
    log(f"{label}: resumed run held{' and re-armed the ESP restart' if expect_restart_armed else ''}: {held is not None}",
        **(brief(held) if held else {}))
    return shown and counting and no_restart and cleared and held is not None


def wait_for_restart(device: Device, streams: Streams, label: str) -> dict | None:
    start = time.time()
    device.close()
    while device.present() and time.time() - start < RESTART_DELAY_S + 30:
        time.sleep(0.5)
    if device.present():
        log(f"{label}: the ESP did not restart")
        return None
    log(f"{label}: ESP restarted {time.time() - start:.1f} s after the suspension (delay {RESTART_DELAY_S} s)")
    while not device.present() and time.time() - start < RESTART_DELAY_S + 90:
        time.sleep(0.5)
    time.sleep(2)
    streams.reopen()
    st = wait_for(device, lambda s: s["state"] == "running", 30)
    if st:
        log(f"{label}: back", **brief(st))
    return st


def cmd_suspend(device: Device, streams: Streams) -> bool:
    st = device.status()
    with_restart = "esp_restart_armed" in st["recovery"]
    if not with_restart:
        st = unheld_series(device, "suspend")
        return st is not None and check_resume(device, st, "suspend", None)

    # A: armed, the suspension restarts the ESP
    st = unheld_series(device, "A")
    if st is None:
        return False
    armed = st["recovery"]["esp_restart_armed"]
    st = wait_for_restart(device, streams, "A")
    a_ok = armed and st is not None and st["recovery"]["restarted_by_recovery"] and not st["recovery"]["esp_restart_armed"]
    log(f"A: {'ok' if a_ok else 'NOT AS EXPECTED'}")
    if st is None:
        return False

    # B: right after that boot, no held run in between: no restart, resumes by itself, re-armed
    time.sleep(5)  # the host reopens the stream
    st = unheld_series(device, "B")
    b_ok = st is not None and check_resume(device, st, "B", True)
    log(f"B: {'ok' if b_ok else 'NOT AS EXPECTED'}")
    if st is None:
        return False

    # C: re-armed, the next suspension restarts the ESP again
    time.sleep(3)
    st = unheld_series(device, "C")
    if st is None:
        return False
    st = wait_for_restart(device, streams, "C")
    c_ok = st is not None and st["recovery"]["restarted_by_recovery"] and not st["recovery"]["esp_restart_armed"]
    log(f"C: {'ok' if c_ok else 'NOT AS EXPECTED'}")
    return a_ok and b_ok and c_ok


def main() -> int:
    global LOG_FILE
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("test", choices=["held", "suspend"])
    p.add_argument("--port", required=True)
    p.add_argument("--count", type=int, default=11, help="held: number of recoveries (default 11, one more than the old rate limit)")
    p.add_argument("--log", help="also append the lines to this file")
    args = p.parse_args()
    LOG_FILE = args.log
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    device = Device(args.port)
    streams = Streams()
    streams.start()
    time.sleep(4)
    try:
        if device.status() is None:
            log(f"no get_camera_status on {args.port}")
            return 2
        passed = cmd_held(device, args.count) if args.test == "held" else cmd_suspend(device, streams)
    finally:
        streams.stop()
        device.close()
    log(f"{args.test}: {'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
