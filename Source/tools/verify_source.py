#!/usr/bin/env python3
"""Verify the original source export against its SHA256SUMS.txt, without dependencies."""
import hashlib
from pathlib import Path, PurePosixPath
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    problems = []
    expected = set()
    for line in (ROOT / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            problems.append("Invalid checksum line")
            continue
        digest, name = match.groups()
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or name in expected:
            problems.append("Invalid or duplicate path: " + name)
            continue
        expected.add(name)
        target = ROOT.joinpath(*relative.parts)
        if not target.is_file() or any(p.is_symlink() for p in (target, *target.parents)):
            problems.append("Missing file or symlink: " + name)
            continue
        with target.open("rb") as stream:
            actual = hashlib.file_digest(stream, "sha256").hexdigest()
        if actual != digest:
            problems.append("Changed: " + name)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    print("PASS: " + str(len(expected)) + " source files match the export checksums")
    return 0


if __name__ == "__main__":
    sys.exit(main())
