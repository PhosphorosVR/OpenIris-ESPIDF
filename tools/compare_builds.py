"""
Reference build comparison
==========================

Proves that a change which is meant to be compiled out (feature symbols off)
leaves the firmware image of other boards untouched.

Two images count as identical when every byte matches except
  - the ELF SHA-256 in the app description (image offset 0xb0, 32 bytes),
  - the image checksum byte and the appended image SHA-256.
Those three are derived from the ELF file, which also covers debug info and
line numbers and therefore changes with any edit to a shared source file.

Reference builds use their own sdkconfig and build directory, so neither the
checked-in sdkconfig nor build/ is touched. Build time, date and version are
pinned (APP_REPRODUCIBLE_BUILD, APP_PROJECT_VER="reference").

Usage (inside an ESP-IDF shell, from the repository root):
    python tools/compare_builds.py build --board project_babble --out <dir>
    python tools/compare_builds.py compare <baseline dir> <new dir>
    python tools/compare_builds.py selftest <dir>

Exit code 0 means identical (compare) or passed (selftest).
"""

import argparse
import bisect
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import switchBoardType as sbt  # noqa: E402

REFERENCE_OVERRIDES = [
    "CONFIG_APP_REPRODUCIBLE_BUILD=y",
    "CONFIG_APP_PROJECT_VER_FROM_CONFIG=y",
    'CONFIG_APP_PROJECT_VER="reference"',
]

# Copied next to app.bin/.elf/.map, paths relative to the build directory
EXTRA_ARTIFACTS = {
    "bootloader.bin": "bootloader/bootloader.bin",
    "partition-table.bin": "partition_table/partition-table.bin",
    "sdkconfig.h": "config/sdkconfig.h",
}

IMAGE_MAGIC = 0xE9
APP_DESC_OFFSET = 0x20
APP_DESC_MAGIC = 0xABCD5432
ELF_SHA_OFFSET = 0xB0
ELF_SHA_LEN = 32
MAX_REPORTED_RANGES = 25


# ---------------------------------------------------------------- build

def _config_key(line: str) -> Optional[str]:
    line = line.strip()
    if line.startswith("CONFIG_") and "=" in line:
        return line.split("=", 1)[0]
    if line.startswith("# CONFIG_") and line.endswith(" is not set"):
        return line[2:-len(" is not set")]
    return None


def merged_defaults(board_key: str) -> List[str]:
    """base_defaults + board file + reference overrides, last assignment wins."""
    ordered: Dict[str, str] = {}
    sources = [sbt.get_base_config_path(), sbt.get_board_config_path(board_key)]
    lines: List[str] = []
    for path in sources:
        with open(path, "r") as f:
            lines.extend(f.read().splitlines())
    lines.extend(REFERENCE_OVERRIDES)
    for line in lines:
        key = _config_key(line)
        if key is None:
            continue
        ordered.pop(key, None)
        ordered[key] = line.strip()
    return list(ordered.values())


def _run(cmd: List[str], cwd: str) -> str:
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def cmd_build(args) -> int:
    board = sbt.normalize_board_name(args.board)
    if not board:
        print(f"Unknown board '{args.board}', see tools/switchBoardType.py --list")
        return 2
    out = os.path.abspath(args.out)
    if os.path.exists(out) and os.listdir(out):
        print(f"{out} exists and is not empty, refusing to overwrite a reference build")
        return 2
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("IDF_PATH not set, run this from an ESP-IDF shell")
        return 2

    defaults = merged_defaults(board)
    target = next((l.split("=", 1)[1].strip('"') for l in defaults if l.startswith("CONFIG_IDF_TARGET=")), "")
    uvc_present = os.path.isdir(os.path.join(sbt.get_root_path(), "components", "usb_device_uvc"))
    if (target == "esp32s3") != uvc_present:
        # switchBoardType moves platform components around; reference builds never do.
        print(f"Target {target} does not match the current component layout, use an {target} checkout")
        return 2

    root = sbt.get_root_path()
    os.makedirs(out, exist_ok=True)
    defaults_path = os.path.join(out, "sdkconfig.defaults")
    with open(defaults_path, "w", newline="\n") as f:
        f.write("\n".join(defaults) + "\n")
    build_dir = os.path.join(out, "build")

    cmd = [
        sys.executable, os.path.join(idf_path, "tools", "idf.py"),
        "-C", root, "-B", build_dir,
        "-D", f"SDKCONFIG={os.path.join(out, 'sdkconfig')}",
        "-D", f"SDKCONFIG_DEFAULTS={defaults_path}",
        "build",
    ]
    print("Running:", " ".join(cmd), flush=True)
    with open(os.path.join(out, "build.log"), "w") as log:
        result = subprocess.run(cmd, cwd=root, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print(f"Build failed, see {os.path.join(out, 'build.log')}")
        return result.returncode

    info = collect(out, build_dir, board, target)
    if not args.keep_build:
        shutil.rmtree(build_dir)
    print(json.dumps(info, indent=2))
    return 0


def collect(out: str, build_dir: str, board: str, target: str) -> dict:
    """Copy the artifacts next to the build directory and record where they came from."""
    root = sbt.get_root_path()
    idf_path = os.environ["IDF_PATH"]
    with open(os.path.join(build_dir, "project_description.json")) as f:
        desc = json.load(f)
    # app_bin/app_elf are relative to the build directory
    app_elf = os.path.join(build_dir, desc["app_elf"])
    shutil.copy2(os.path.join(build_dir, desc["app_bin"]), os.path.join(out, "app.bin"))
    shutil.copy2(app_elf, os.path.join(out, "app.elf"))
    shutil.copy2(os.path.splitext(app_elf)[0] + ".map", os.path.join(out, "app.map"))
    for name, rel in EXTRA_ARTIFACTS.items():
        shutil.copy2(os.path.join(build_dir, rel), os.path.join(out, name))

    info = {
        "board": board,
        "target": target,
        "commit": _run(["git", "rev-parse", "HEAD"], root),
        "dirty": _run(["git", "status", "--porcelain", "--untracked-files=no"], root) != "",
        "idf_version": _run(["git", "describe", "--tags", "--always"], idf_path),
        "compiler": _run([desc["c_compiler"], "--version"], root).splitlines()[0],
        "app_sha256": hashlib.sha256(open(os.path.join(out, "app.bin"), "rb").read()).hexdigest(),
    }
    with open(os.path.join(out, "build_info.json"), "w") as f:
        json.dump(info, f, indent=2)
    return info


# ---------------------------------------------------------------- image

@dataclass
class Segment:
    load_addr: int
    file_offset: int  # of the data, not the segment header
    length: int


@dataclass
class Image:
    data: bytes
    segments: List[Segment]
    checksum_offset: int
    hash_offset: Optional[int]

    def masked_ranges(self) -> List[Tuple[int, int, str]]:
        ranges = [
            (ELF_SHA_OFFSET, ELF_SHA_OFFSET + ELF_SHA_LEN, "ELF SHA-256"),
            (self.checksum_offset, self.checksum_offset + 1, "image checksum"),
        ]
        if self.hash_offset is not None:
            ranges.append((self.hash_offset, self.hash_offset + 32, "image SHA-256"))
        return ranges

    def locate(self, offset: int) -> Tuple[Optional[int], Optional[int]]:
        """File offset -> (segment index, load address)."""
        for i, seg in enumerate(self.segments):
            if seg.file_offset <= offset < seg.file_offset + seg.length:
                return i, seg.load_addr + (offset - seg.file_offset)
        return None, None


def parse_image(path: str) -> Image:
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 24 or data[0] != IMAGE_MAGIC:
        raise ValueError(f"{path}: not an ESP app image")
    seg_count = data[1]
    hash_appended = data[23] == 1
    pos = 24
    segments = []
    for _ in range(seg_count):
        load_addr, length = struct.unpack_from("<II", data, pos)
        segments.append(Segment(load_addr, pos + 8, length))
        pos += 8 + length
    checksum_offset = pos + (15 - pos % 16)
    hash_offset = checksum_offset + 1 if hash_appended else None
    expected_len = checksum_offset + 1 + (32 if hash_appended else 0)
    if len(data) != expected_len:
        raise ValueError(f"{path}: size {len(data)} does not match segment table ({expected_len})")
    if struct.unpack_from("<I", data, APP_DESC_OFFSET)[0] != APP_DESC_MAGIC:
        raise ValueError(f"{path}: app description not at 0x{APP_DESC_OFFSET:x}")
    return Image(data, segments, checksum_offset, hash_offset)


class Symbols:
    def __init__(self, elf_path: str):
        self.addrs: List[int] = []
        self.entries: List[Tuple[int, int, str]] = []
        try:
            from elftools.elf.elffile import ELFFile
        except ImportError:
            return
        if not os.path.isfile(elf_path):
            return
        with open(elf_path, "rb") as f:
            elf = ELFFile(f)
            symtab = elf.get_section_by_name(".symtab")
            if symtab is None:
                return
            for sym in symtab.iter_symbols():
                if sym["st_info"]["type"] in ("STT_FUNC", "STT_OBJECT") and sym["st_value"]:
                    self.entries.append((sym["st_value"], sym["st_size"], sym.name))
        self.entries.sort()
        self.addrs = [e[0] for e in self.entries]

    def lookup(self, addr: Optional[int]) -> str:
        if addr is None or not self.addrs:
            return ""
        i = bisect.bisect_right(self.addrs, addr) - 1
        if i < 0:
            return ""
        start, size, name = self.entries[i]
        if addr < start + max(size, 1):
            return f"{name}+0x{addr - start:x}"
        return f"after {name}"


def _diff_ranges(a: bytes, b: bytes, masked: List[Tuple[int, int, str]]) -> List[Tuple[int, int]]:
    skip = bytearray(len(a))
    for start, end, _ in masked:
        skip[start:end] = b"\x01" * (end - start)
    ranges = []
    i, n = 0, len(a)
    while i < n:
        if a[i] != b[i] and not skip[i]:
            j = i
            while j < n and a[j] != b[j] and not skip[j]:
                j += 1
            ranges.append((i, j))
            i = j
        else:
            i += 1
    return ranges


def compare_images(path_a: str, path_b: str, elf_b: Optional[str], quiet: bool = False) -> bool:
    img_a, img_b = parse_image(path_a), parse_image(path_b)
    log = (lambda *a: None) if quiet else print

    layout_a = [(s.load_addr, s.length) for s in img_a.segments]
    layout_b = [(s.load_addr, s.length) for s in img_b.segments]
    if layout_a != layout_b or len(img_a.data) != len(img_b.data):
        log("DIFFERENT: segment layout")
        for i in range(max(len(layout_a), len(layout_b))):
            sa = layout_a[i] if i < len(layout_a) else None
            sb = layout_b[i] if i < len(layout_b) else None
            mark = "" if sa == sb else "  <--"
            fmt = lambda s: "-" if s is None else f"0x{s[0]:08x} len 0x{s[1]:x}"
            log(f"  seg {i}: {fmt(sa):28} {fmt(sb):28}{mark}")
        return False

    ranges = _diff_ranges(img_a.data, img_b.data, img_b.masked_ranges())
    if not ranges:
        masked = [m for m in img_b.masked_ranges() if img_a.data[m[0]:m[1]] != img_b.data[m[0]:m[1]]]
        log("IDENTICAL" + (" (masked: " + ", ".join(m[2] for m in masked) + ")" if masked else ""))
        return True

    symbols = Symbols(elf_b) if elf_b else None
    total = sum(e - s for s, e in ranges)
    log(f"DIFFERENT: {total} byte(s) in {len(ranges)} range(s)")
    for start, end in ranges[:MAX_REPORTED_RANGES]:
        seg, addr = img_b.locate(start)
        where = f"seg {seg} addr 0x{addr:08x}" if addr is not None else "header/padding"
        sym = symbols.lookup(addr) if symbols else ""
        log(f"  file 0x{start:06x} len {end - start:5}  {where}  {sym}")
    if len(ranges) > MAX_REPORTED_RANGES:
        log(f"  ... {len(ranges) - MAX_REPORTED_RANGES} more")
    return False


def _compare_raw(name: str, path_a: str, path_b: str) -> bool:
    if not (os.path.isfile(path_a) and os.path.isfile(path_b)):
        print(f"{name}: missing")
        return False
    same = open(path_a, "rb").read() == open(path_b, "rb").read()
    print(f"{name}: {'IDENTICAL' if same else 'DIFFERENT'}")
    return same


def cmd_compare(args) -> int:
    print("app.bin: ", end="")
    ok = compare_images(os.path.join(args.a, "app.bin"), os.path.join(args.b, "app.bin"), os.path.join(args.b, "app.elf"))
    for name in ("bootloader.bin", "partition-table.bin", "sdkconfig.h"):
        ok = _compare_raw(name, os.path.join(args.a, name), os.path.join(args.b, name)) and ok
    return 0 if ok else 1


def cmd_selftest(args) -> int:
    """Checks the comparison itself: masked bytes are ignored, anything else is found."""
    path = os.path.join(args.dir, "app.bin")
    img = parse_image(path)
    scratch = os.path.join(args.dir, "selftest.bin")
    ok = True

    def mutated(offset: int) -> bytes:
        data = bytearray(img.data)
        data[offset] ^= 0xFF
        return bytes(data)

    def check(label: str, data: bytes, expect_equal: bool):
        nonlocal ok
        with open(scratch, "wb") as f:
            f.write(data)
        equal = compare_images(path, scratch, None, quiet=True)
        passed = equal == expect_equal
        ok = ok and passed
        print(f"{'PASS' if passed else 'FAIL'}  {label}")

    try:
        check("identical copy", img.data, True)
        for start, end, label in img.masked_ranges():
            check(f"change in {label} is masked", mutated(start), True)
            check(f"change in last byte of {label} is masked", mutated(end - 1), True)
        check("change right after ELF SHA-256 is found", mutated(ELF_SHA_OFFSET + ELF_SHA_LEN), False)
        check("change right before ELF SHA-256 is found", mutated(ELF_SHA_OFFSET - 1), False)
        for i, seg in enumerate(img.segments):
            if seg.length:
                check(f"change in segment {i} is found", mutated(seg.file_offset + seg.length // 2), False)
        check("change in image header is found", mutated(4), False)
    finally:
        if os.path.exists(scratch):
            os.remove(scratch)
    print("selftest", "passed" if ok else "FAILED")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="build one board as reference into an empty directory")
    b.add_argument("--board", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--keep-build", action="store_true", help="keep the build directory")
    b.set_defaults(func=cmd_build)

    c = sub.add_parser("compare", help="compare two reference build directories")
    c.add_argument("a")
    c.add_argument("b")
    c.set_defaults(func=cmd_compare)

    s = sub.add_parser("selftest", help="check the comparison against mutated copies of one image")
    s.add_argument("dir")
    s.set_defaults(func=cmd_selftest)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
