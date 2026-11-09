#!/usr/bin/env python3
"""Check signed PushS and direct-immediate globals in a 96-script VCANGCE IMG.

Requires the same fourfury/openssl dependencies as build_scripts.py. This is a
focused bytecode audit, not a proof of computed indices or gameplay behavior.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools/scoasm"))
import build_scripts
import scoasm


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_signed_boundaries() -> dict:
    source = (".script contract.sco\n.encrypted 0\n.statics 0\n.globals 0\n"
              ".flags 0x0\n.signature 0x0\n.code\n{} {}\n.data\n")
    signed = [-32768, -180, -120, -20, -1, 0, 32767]
    overflow = [-32769, 32768, 60000, 60240, 65535]
    full = [32768, 60000, 60240, 60341, 65535]
    for value in signed:
        expected = bytes([scoasm.OPC["PushS"]]) + struct.pack("<h", value)
        raw = scoasm.assemble(source.format("PushS", value), b"")
        if raw[24:] != expected or scoasm.decode(expected) != [(0, "PushS", value)]:
            raise ValueError("signed PushS boundary roundtrip failed")
    for value in overflow:
        try:
            scoasm.assemble(source.format("PushS", value), b"")
        except ValueError:
            pass
        else:
            raise ValueError("assembler accepted an out-of-range PushS operand")
    for value in full:
        raw = scoasm.assemble(source.format("Push", value), b"")
        if scoasm.decode(raw[24:]) != [(0, "Push", value)]:
            raise ValueError("full-width Push boundary roundtrip failed")
    return dict(result="PASS", signed_roundtrips=signed,
                rejected_short_operands=overflow, full_width_roundtrips=full)


def immediate(name: str, value: object) -> int | None:
    if name in ("Push", "PushS"):
        return value if isinstance(value, int) else None
    if name.startswith("Push_Neg"):
        return -int(name[8:])
    if name.startswith("Push_"):
        return int(name[5:])
    return None


def audit_image(data: bytes, key: bytes) -> dict:
    image = build_scripts.inspect_image(data)
    decoded, declarations = [], []
    for member in image["members"]:
        name, start, size = member["name"], member["offset"], member["size"]
        raw = data[start:start + size]
        if len(raw) < 24:
            raise ValueError("truncated SCO header")
        magic, code_size, statics, globals_, flags, signature = struct.unpack_from("<6I", raw)
        if magic not in (scoasm.MAGIC_ENC, scoasm.MAGIC_PLAIN):
            raise ValueError("unsupported SCO magic")
        if 24 + code_size + 4 * (statics + globals_) > len(raw):
            raise ValueError("SCO code/data bounds exceed the member")
        if globals_:
            declarations.append(dict(member=name, words=globals_))
        code = raw[24:24 + code_size]
        if magic == scoasm.MAGIC_ENC:
            code = scoasm.aes(code, key, "-d")
        decoded.append((name, code, scoasm.decode(code)))
    # This archive has one shared global-storage declaration, in startup.sco.
    # Refuse ambiguous layouts rather than guessing a bound from the sources.
    if len(declarations) != 1:
        raise ValueError("expected exactly one nonzero SCO global-storage declaration")
    bound = declarations[0]["words"]
    bad, constants = [], []
    direct = unresolved = 0
    for name, code, instructions in decoded:
        for index, (pc, operation, value) in enumerate(instructions):
            following = instructions[index + 1][1] if index + 1 < len(instructions) else None
            if operation == "PushS" and value < 0 and following != "GetGlobalPs":
                constants.append(dict(member=name, pc=pc, value=value,
                                      operand_bytes=code[pc + 1:pc + 3].hex()))
            if operation != "GetGlobalPs":
                continue
            previous = instructions[index - 1] if index else None
            address = immediate(previous[1], previous[2]) if previous else None
            if address is None:
                unresolved += 1
                continue
            direct += 1
            if not 0 <= address < bound:
                bad.append(dict(member=name, pc=pc, index=address,
                                immediate_opcode=previous[1]))
    return dict(sha256=image["sha256"], bytes=len(data), script_count=len(decoded),
                global_storage=declarations[0], direct_immediate_global_refs=direct,
                computed_or_other_global_refs=unresolved,
                invalid_immediate_global_refs=bad,
                negative_short_constants=constants)


def constant_signature(audit: dict) -> Counter:
    return Counter((row["member"], row["value"], row["operand_bytes"])
                   for row in audit["negative_short_constants"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True, help="your owned GTAIV.exe, read only")
    parser.add_argument("--image", type=Path, required=True, help="current 96-member script.img, read only")
    parser.add_argument("--negative-control", type=Path, help="optional older image expected to fail the global-range audit")
    parser.add_argument("--report", type=Path, help="new JSON file outside this source export and the game")
    args = parser.parse_args()
    exe, image = args.exe.expanduser().resolve(), args.image.expanduser().resolve()
    control = args.negative_control.expanduser().resolve() if args.negative_control else None
    report = args.report.expanduser().resolve() if args.report else None
    inputs = {exe, image} | ({control} if control else set())
    if report and (report.is_relative_to(ROOT) or report.is_relative_to(exe.parent)
                   or report in inputs or report.exists() or args.report.is_symlink()):
        parser.error("--report must be a new file outside the source export and game, distinct from inputs")
    try:
        boundaries = check_signed_boundaries()
        if not shutil.which("openssl"):
            raise ValueError("openssl is required in PATH")
        import fourfury
        if fourfury.__version__ != "0.13.3":
            raise ValueError("fourfury 0.13.3 is required")
        key = scoasm.game_key(exe)
        current = audit_image(image.read_bytes(), key)
        passed = not current["invalid_immediate_global_refs"]
        result = dict(schema_version=1, result="PASS" if passed else "FAIL",
                      signed_operands=boundaries, current_image=current,
                      scope="Direct adjacent integer-immediate GetGlobalPs operands only; computed indices, control flow, native behavior and gameplay are not proved. Global bound comes from the encrypted archive's sole nonzero SCO global-storage header. No key or local input paths are recorded.")
        if control:
            old = audit_image(control.read_bytes(), key)
            preserved = constant_signature(current) == constant_signature(old)
            detected = bool(old["invalid_immediate_global_refs"])
            same_storage = old["global_storage"] == current["global_storage"]
            passed = passed and detected and preserved and same_storage
            result.update(negative_control=old,
                          negative_control_detected=detected,
                          negative_constant_operands_preserved=preserved,
                          same_global_storage_declaration=same_storage,
                          result="PASS" if passed else "FAIL")
        serialized = json.dumps(result, indent=2) + "\n"
        if report:
            report.parent.mkdir(parents=True, exist_ok=True)
            with report.open("x") as stream:
                stream.write(serialized)
            print(f"{result['result']}: {current['script_count']} scripts; "
                  f"{len(current['invalid_immediate_global_refs'])} invalid direct globals; "
                  f"{len(current['negative_short_constants'])} negative PushS constants")
            if control:
                print(f"Negative control: {len(old['invalid_immediate_global_refs'])} invalid direct globals; "
                      f"negative constant operands preserved: {preserved}")
        else:
            print(serialized, end="")
        return 0 if passed else 1
    except subprocess.CalledProcessError:
        # OpenSSL's command contains the transient key; never print that exception.
        print("error: script decryption failed", file=sys.stderr)
        return 2
    except (OSError, ValueError, KeyError, IndexError, struct.error, ImportError) as error:
        print(f"error: audit could not complete ({type(error).__name__})", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
