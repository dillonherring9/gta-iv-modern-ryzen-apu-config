#!/usr/bin/env python3
"""Build the 93 AVCNGCE scripts into an external directory; never install them."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "scripts"
EXPECTED_COUNT = 93


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def inspect_image(data: bytes) -> dict:
    """Read this project's plain IMG3 layout, including allocated padding bytes."""
    if len(data) < 20:
        raise ValueError("truncated IMG header")
    magic, version, count, table_size, item_size, resource = struct.unpack_from("<IIIIHH", data)
    if (magic, version, item_size, count) != (0xA94E2A52, 3, 16, EXPECTED_COUNT):
        raise ValueError("expected the plain IMG3 archive with exactly 93 entries")
    table_end = 20 + table_size
    if table_size < count * 16 or table_end > len(data):
        raise ValueError("invalid IMG3 directory bounds")
    names = data[20 + count * 16:table_end].split(b"\0")
    if len(names) != count + 1 or names[-1]:
        raise ValueError("invalid IMG3 name table")
    rows, intervals = [], []
    for index, raw_name in enumerate(names[:-1]):
        name = raw_name.decode("ascii")
        if not re.fullmatch(r"[A-Za-z0-9_]+\.sco", name):
            raise ValueError(f"invalid script member name: {name!r}")
        size, kind, sector, blocks, padding = struct.unpack_from("<IIIHH", data, 20 + index * 16)
        start, allocated = sector * 2048, blocks * 2048
        if start < table_end or start + allocated > len(data) or allocated - size != padding:
            raise ValueError(f"invalid allocation for {name}")
        intervals.append((start, start + allocated))
        rows.append(dict(name=name, resource_type=kind, size=size, offset=start,
                         allocated_bytes=allocated, padding_bytes=padding,
                         sha256=sha(data[start:start + size]),
                         allocation_sha256=sha(data[start:start + allocated])))
    if len({row["name"] for row in rows}) != count:
        raise ValueError("duplicate archive member names")
    ordered = sorted(intervals)
    if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("overlapping archive allocations")
    return dict(bytes=len(data), sha256=sha(data), resource_header=resource, members=rows)


def validate_sources() -> list[dict]:
    lines = [line.split() for line in (SOURCES / "members.txt").read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if len(lines) != EXPECTED_COUNT + 1 or lines[0][0] != "res" or len(lines[0]) != 2:
        raise ValueError("members.txt must contain its res header and exactly 93 members")
    int(lines[0][1], 0)
    names = []
    for fields in lines[1:]:
        if len(fields) != 2 or not re.fullmatch(r"[A-Za-z0-9_]+\.sco", fields[0]):
            raise ValueError(f"invalid members.txt row: {fields!r}")
        if int(fields[1]) not in (0, 1):
            raise ValueError(f"invalid script resource type: {fields!r}")
        names.append(fields[0])
    if len(set(names)) != EXPECTED_COUNT:
        raise ValueError("duplicate script names in members.txt")
    expected = {Path(name).stem + ".scasm" for name in names}
    actual = {path.name for path in SOURCES.glob("*.scasm")}
    if actual != expected:
        raise ValueError(f"source coverage mismatch: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")
    return [dict(path="scripts/" + name, sha256=sha((SOURCES / name).read_bytes()))
            for name in sorted(expected)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, required=True, help="your owned GTA IV CE GTAIV.exe (read only)")
    ap.add_argument("--out", type=Path, required=True, help="output directory outside this source export and the game")
    ap.add_argument("--compare", type=Path, help="optional reference script.img; require exact bytes and member metadata")
    args = ap.parse_args()
    exe, out = args.exe.expanduser().resolve(), args.out.expanduser().resolve()
    if not exe.is_file():
        ap.error("--exe must identify an existing GTAIV.exe")
    if out.is_relative_to(ROOT) or out.is_relative_to(exe.parent):
        ap.error("--out must be outside both the source export and the game directory")
    compare_path = args.compare.expanduser().resolve() if args.compare else None
    if compare_path and out / "script.img" == compare_path:
        ap.error("the output must not overwrite the comparison image")
    try:
        source_rows = validate_sources()
        reference = compare_path.read_bytes() if compare_path else None
        reference_info = inspect_image(reference) if reference is not None else None
        if not shutil.which("openssl"):
            raise ValueError("openssl is required in PATH")
        try:
            import fourfury
        except ImportError as exc:
            raise ValueError("install requirements.txt in this Python environment") from exc
        if fourfury.__version__ != "0.13.3":
            raise ValueError(f"expected fourfury 0.13.3, found {fourfury.__version__}")
        sys.path.insert(0, str(ROOT / "tools/scoasm"))
        import build_img
        import scoasm
        image = build_img.build(SOURCES, scoasm.game_key(exe))
        result = inspect_image(image)
        snapshot = json.loads((SOURCES / "manifest.json").read_text())
        result["source_files"] = source_rows
        result["matches_snapshot_image"] = result["sha256"] == snapshot["image"]["sha256"]
        result["matches_snapshot_sources"] = source_rows == snapshot["source_files"]
        passed = True
        if reference_info is not None:
            result["comparison"] = dict(
                reference_sha256=reference_info["sha256"],
                exact_image=image == reference,
                exact_members=result["members"] == reference_info["members"],
                resource_header=result["resource_header"] == reference_info["resource_header"])
            passed = all(result["comparison"][key] for key in ("exact_image", "exact_members", "resource_header"))
        out.mkdir(parents=True, exist_ok=True)
        if any((out / name).is_symlink() for name in ("script.img", "build-report.json")):
            raise ValueError("output files may not be symbolic links")
        (out / "script.img").write_bytes(image)
        (out / "build-report.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"Built 93 scripts: {out / 'script.img'}")
        print(f"SHA256 {result['sha256']}")
        if reference_info is not None:
            print("PASS: reference image, 93 members and allocations are identical" if passed
                  else "FAIL: output differs from the comparison image; inspect build-report.json")
        return 0 if passed else 1
    except (OSError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
