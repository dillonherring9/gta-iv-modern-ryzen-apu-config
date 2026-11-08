#!/usr/bin/env python3
"""Apply the two preserved project patches to NodixTech's DualSense 1.4 binary.

The original vendor binary is not included. Requires pefile==2024.8.26.
Input, intermediate and final hashes are fixed; the input file is never changed.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = "ba15d63140a05359b762abe1bdde09bda7f6204bec33b2614e19d2602e72a8b4"
FINAL = "03bae1ada2e9b9503233f10810118c4bb37d9ee152371abbf38f5130999240eb"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Original GTA_IV_DualSense.asi 1.4")
    parser.add_argument("--output", required=True, type=Path, help="New patched binary")
    args = parser.parse_args()
    source, output = args.input.expanduser().resolve(), args.output.expanduser().resolve()
    try:
        if source == output:
            raise ValueError("Input and output must be different paths")
        if output.exists():
            raise ValueError("Output already exists; choose a new output path")
        if hashlib.sha256(source.read_bytes()).hexdigest() != ORIGINAL:
            raise ValueError("Input is not the supported original DualSense 1.4 binary")
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="avcngce-dualsense-", dir=output.parent) as folder:
            first, final = Path(folder) / "165.asi", Path(folder) / "final.asi"
            for script, before, after in (("patch_165.py", source, first),
                                          ("patch_166.py", first, final)):
                subprocess.run([sys.executable, "-I", str(ROOT / "native/dualsense-patches" / script),
                                str(before), str(after)], check=True)
            if hashlib.sha256(final.read_bytes()).hexdigest() != FINAL:
                raise ValueError("Patched binary did not match the release identity")
            final.replace(output)
        print("SHA256 " + FINAL)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
