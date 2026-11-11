#!/usr/bin/env python3
"""Build all 96 VCANGCE scripts, preserving the release's mixed IMG resources."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "scripts"
EXPECTED_COUNT = 96


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_members() -> tuple[int, list[tuple[str, int]]]:
    lines = [line.split() for line in (SOURCES / "members.txt").read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    if len(lines) != EXPECTED_COUNT + 1 or lines[0][0] != "res" or len(lines[0]) != 2:
        raise ValueError("members.txt must contain its res header and exactly 96 members")
    reserved = int(lines[0][1], 0)
    members = []
    for fields in lines[1:]:
        if len(fields) != 2 or not re.fullmatch(r"[A-Za-z0-9_]+\.sco", fields[0]):
            raise ValueError("invalid members.txt row")
        kind = int(fields[1])
        if kind not in (0, 1):
            raise ValueError("invalid canonical script resource type")
        members.append((fields[0], kind))
    if len({name.casefold() for name, _ in members}) != EXPECTED_COUNT:
        raise ValueError("duplicate script names in members.txt")
    return reserved, members


def validate_sources() -> list[dict]:
    _, members = source_members()
    expected = {Path(name).stem + ".scasm" for name, _ in members}
    actual = {path.name for path in SOURCES.glob("*.scasm")}
    if actual != expected:
        raise ValueError("canonical source coverage differs from members.txt")
    return [dict(path="scripts/" + name, sha256=sha((SOURCES / name).read_bytes()))
            for name in sorted(expected)]


def inspect_image(data: bytes, *, canonical_only: bool = False) -> dict:
    """Inspect plain IMG3 allocations; resource flags are not SCO byte lengths."""
    reserved, expected = source_members()
    wanted = {name.casefold(): kind for name, kind in expected}
    if len(data) < 20:
        raise ValueError("truncated IMG header")
    magic, version, count, table_size, width, resource = struct.unpack_from("<4I2H", data)
    if (magic, version, width, resource) != (0xA94E2A52, 3, 16, reserved):
        raise ValueError("expected the release's plain IMG3 header")
    table_end = 20 + table_size
    if table_size < count * 16 or table_end > len(data):
        raise ValueError("invalid IMG3 directory bounds")
    raw_names = data[20 + count * 16:table_end].split(b"\0")
    if len(raw_names) != count + 1 or raw_names[-1]:
        raise ValueError("invalid IMG3 name table")
    rows, ranges, seen, canonical, other_sco = [], [], set(), set(), []
    for index, raw_name in enumerate(raw_names[:-1]):
        name = raw_name.decode("ascii")
        folded = name.casefold()
        if not name or "/" in name or "\\" in name or folded in seen:
            raise ValueError("invalid or duplicate IMG member name")
        seen.add(folded)
        size_or_flags, kind, sector, blocks, padding_or_flags = struct.unpack_from("<3I2H", data, 20 + index * 16)
        start, allocated = sector * 2048, blocks * 2048
        if start < table_end or start + allocated > len(data):
            raise ValueError("invalid IMG allocation bounds")
        ranges.append((start, start + allocated))
        is_canonical = folded in wanted
        is_sco = folded.endswith(".sco")
        if is_canonical:
            canonical.add(folded)
            if wanted[folded] != kind:
                raise ValueError("canonical SCO resource type differs from members.txt")
        elif is_sco:
            other_sco.append(name)
        if canonical_only and not is_canonical:
            continue
        row = dict(name=name, resource_type=kind, offset=start, allocated_bytes=allocated,
                   directory_size_or_flags=size_or_flags, directory_padding_or_flags=padding_or_flags,
                   allocation_sha256=sha(data[start:start + allocated]))
        if is_sco:
            if allocated - size_or_flags != padding_or_flags or size_or_flags < 24:
                raise ValueError("invalid SCO allocation size/padding")
            if struct.unpack_from("<I", data, start)[0] not in (0x0e726373, 0x0d524353):
                raise ValueError("invalid SCO magic")
            row.update(size=size_or_flags, padding_bytes=padding_or_flags,
                       sha256=sha(data[start:start + size_or_flags]))
        rows.append(row)
    if canonical != set(wanted):
        raise ValueError("image does not contain all 96 canonical scripts")
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError("overlapping IMG allocations")
    return dict(bytes=len(data), sha256=sha(data), resource_header=resource,
                container_members=count, canonical_scripts=EXPECTED_COUNT,
                additional_scripts=other_sco, other_resources=count - EXPECTED_COUNT - len(other_sco),
                members=rows)


def preserved_resources(before: dict, after: dict) -> dict:
    canonical = {name.casefold() for name, _ in source_members()[1]}
    a = [r for r in before["members"] if r["name"].casefold() not in canonical]
    b = [r for r in after["members"] if r["name"].casefold() not in canonical]
    fields = ("name", "resource_type", "allocated_bytes", "directory_size_or_flags",
              "directory_padding_or_flags", "allocation_sha256")
    exact = [[r[k] for k in fields] for r in a] == [[r[k] for k in fields] for r in b]
    return dict(result="PASS" if exact else "FAIL", complete_allocations=len(a),
                additional_scripts=len(before["additional_scripts"]), other_resources=before["other_resources"],
                exact_order_types_flags_and_allocations=exact,
                scope="Every noncanonical member is preserved, including all 21 network SCOs and 4,179 other resources. Sector offsets may relocate if an edited canonical SCO changes allocation size.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, required=True, help="your owned GTA IV CE GTAIV.exe (read only)")
    ap.add_argument("--resources-from", type=Path, required=True, help="checkpoint245 release script.img, read only; supplies all noncanonical resources")
    ap.add_argument("--out", type=Path, required=True, help="output directory outside the source export and game")
    ap.add_argument("--compare", type=Path, help="optional reference script.img; require complete image identity")
    args = ap.parse_args()
    exe, out = args.exe.expanduser().resolve(), args.out.expanduser().resolve()
    base_path = args.resources_from.expanduser().resolve()
    compare_path = args.compare.expanduser().resolve() if args.compare else None
    if not exe.is_file() or not base_path.is_file():
        ap.error("--exe and --resources-from must identify existing files")
    if out.is_relative_to(ROOT) or out.is_relative_to(exe.parent):
        ap.error("--out must be outside both the source export and game directory")
    if out / "script.img" in {exe, base_path, compare_path}:
        ap.error("the output must not overwrite any input")
    try:
        source_rows = validate_sources()
        snapshot = json.loads((SOURCES / "manifest.json").read_text())
        baseline = base_path.read_bytes()
        baseline_info = inspect_image(baseline)
        if baseline_info["sha256"] != snapshot["image"]["sha256"]:
            raise ValueError("--resources-from must be the matching checkpoint245 release script.img")
        if not shutil.which("openssl"):
            raise ValueError("openssl is required in PATH")
        import fourfury
        if fourfury.__version__ != "0.13.3":
            raise ValueError("fourfury 0.13.3 is required")
        sys.path.insert(0, str(ROOT / "tools/scoasm"))
        import build_img
        import scoasm
        image = build_img.build(SOURCES, scoasm.game_key(exe), base_path)
        result = inspect_image(image)
        result["source_files"] = source_rows
        result["matches_snapshot_image"] = result["sha256"] == snapshot["image"]["sha256"]
        result["matches_snapshot_sources"] = source_rows == snapshot["source_files"]
        result["preserved_resources"] = preserved_resources(baseline_info, result)
        passed = result["preserved_resources"]["result"] == "PASS"
        if compare_path:
            reference = baseline if compare_path == base_path else compare_path.read_bytes()
            reference_info = baseline_info if compare_path == base_path else inspect_image(reference)
            result["comparison"] = dict(reference_sha256=reference_info["sha256"],
                exact_image=image == reference, exact_members=result["members"] == reference_info["members"],
                resource_header=result["resource_header"] == reference_info["resource_header"])
            passed = passed and all(result["comparison"][k] for k in ("exact_image", "exact_members", "resource_header"))
        out.mkdir(parents=True, exist_ok=True)
        if any((out / name).is_symlink() for name in ("script.img", "build-report.json")):
            raise ValueError("output files may not be symbolic links")
        (out / "script.img").write_bytes(image)
        (out / "build-report.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"Built 96 scripts in {result['container_members']}-member mixed IMG: {out / 'script.img'}")
        print(f"SHA256 {result['sha256']}")
        print("PASS: all 4,200 noncanonical resource allocations preserved" if passed else "FAIL: inspect build-report.json")
        if compare_path:
            print("PASS: complete reference image and member allocations are identical" if passed else "FAIL: output differs from the reference")
        return 0 if passed else 1
    except subprocess.CalledProcessError:
        # OpenSSL's command contains the transient key; never print that exception.
        print("error: script encryption failed", file=sys.stderr)
        return 2
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    except (OSError, KeyError, IndexError, struct.error, ImportError) as exc:
        print(f"error: build could not complete ({type(exc).__name__})", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
