#!/usr/bin/env python3
"""Read-only checkpoint-245 model-order and radio-identity check (stdlib only).

Default input: the authoring snapshot in ../config/game. --root accepts an
extracted release or installed game. Reads default.dat, then gta.dat; resolves
common:/ and platform:/ with update/ precedence, case-insensitively.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys

SOURCE = Path(__file__).resolve().parents[1]
MODEL_SECTIONS = {"objs", "tobj", "anim", "tanm", "peds", "cars", "weap", "mlo", "lodm"}
OTHER_SECTIONS = {"hier", "2dfx", "txdp", "amat", "agrps", "path", "tree"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sequence_digest(sequence):
    """Hash UTF-8 section<TAB>lowercase-name<LF> rows, including duplicates."""
    return digest("".join(f"{section}\t{name}\n" for section, name in sequence).encode("utf-8"))


class Reader:
    def __init__(self, root):
        self.root = Path(root).resolve()
        if not self.root.is_dir():
            raise ValueError(f"Input root is not a directory: {self.root}")
        self.cache = {}

    def read(self, relative):
        relative = relative.replace("\\", "/").lower()
        parts = PurePosixPath(relative).parts
        if not parts or relative.startswith("/") or any(part in (".", "..") for part in parts):
            raise ValueError(f"Unsafe input path: {relative}")
        if relative in self.cache:
            return self.cache[relative]
        current = self.root
        for part in parts:
            if not current.is_dir():
                self.cache[relative] = None
                return None
            matches = [p for p in current.iterdir() if p.name.lower() == part]
            if len(matches) > 1:
                raise ValueError(f"Ambiguous case-insensitive path: {relative}")
            if not matches:
                self.cache[relative] = None
                return None
            current = matches[0]
        data = current.read_bytes() if current.is_file() else None
        self.cache[relative] = data
        return data

    def resolve(self, virtual):
        virtual = virtual.strip().replace("\\", "/")
        match = re.fullmatch(r"(common|platform):/(.+)", virtual, re.I)
        if not match:
            raise ValueError(f"Unsupported virtual path: {virtual}")
        relative = ("common/" if match[1].lower() == "common" else "pc/") + match[2]
        for candidate in ("update/" + relative, relative):
            data = self.read(candidate)
            if data is not None:
                return candidate.lower(), data.decode("latin1")
        raise ValueError(f"Missing loaded file: {virtual}")


def definitions(text):
    """Shared model list: peds count; hier does not; MLO headers count once."""
    section, mlo_header = None, False
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        low = line.lower()
        if "," not in line:
            if low == "end":
                section = None
            elif low in MODEL_SECTIONS | OTHER_SECTIONS:
                section, mlo_header = low, low == "mlo"
            elif low == "mloend":
                mlo_header = True
            elif low.startswith("mlo") or low == "roomend":
                mlo_header = False
            continue
        if section not in MODEL_SECTIONS:
            continue
        if section == "mlo":
            if not mlo_header:
                continue
            mlo_header = False
        name = line.split(",", 1)[0].strip().lower()
        if not name:
            raise ValueError("Empty model name")
        yield section, name


def models(reader):
    sequence, loaded = [], []
    for virtual in ("common:/data/default.dat", "common:/data/gta.dat"):
        dat_path, text = reader.resolve(virtual)
        for raw in text.splitlines():
            line = raw.split("#", 1)[0].strip()
            match = re.fullmatch(r"IDE\s+(.+)", line, re.I)
            if match:
                ide_path, ide = reader.resolve(match[1])
                entries = list(definitions(ide))
                loaded.append({"dat": dat_path, "path": ide_path, "models": len(entries)})
                sequence.extend(entries)
    return sequence, loaded


def check(root, check_game):
    manifest = json.loads((SOURCE / "config/manifest.json").read_text("utf-8"))
    baseline = json.loads((SOURCE / "config/model-order-224.json").read_text("utf-8"))
    reader = Reader(root)
    sequence, loaded = models(reader)
    prefix = [tuple(row) for row in baseline["sequence"]]
    tail = [tuple(row) for row in manifest["appended_models"]]
    expected = prefix + tail
    errors = []
    if sequence_digest(prefix) != baseline["sequence_sha256"]:
        errors.append("Frozen baseline sequence digest mismatch")
    if len(prefix) != baseline["count"]:
        errors.append("Frozen baseline model count mismatch")
    if len(sequence) != manifest["expected_model_count"]:
        errors.append("Checkpoint model count mismatch")
    if sequence_digest(sequence) != manifest["expected_sequence_sha256"]:
        errors.append("Checkpoint sequence digest mismatch")
    if loaded != manifest["loaded_ides"]:
        errors.append("IDE load list differs from checkpoint 245")
    mismatch = next((i for i, (a, b) in enumerate(zip(sequence, expected)) if a != b), None)
    if sequence != expected:
        errors.append("Model definition sequence differs from posted 224 prefix plus eight appended dancers")
    identities = []
    for row in manifest["files"]:
        data = reader.read(row["path"])
        actual = None if data is None else digest(data)
        valid = actual == row["sha256"] and data is not None and len(data) == row["bytes"]
        identities.append({"path": row["path"], "sha256": actual, "matches": valid})
        if not valid:
            errors.append(f"Snapshot identity differs: {row['path']}")
    critical = {}
    for name, wanted in baseline["critical_model_indices_zero_based"].items():
        positions = [i for i, (_, model) in enumerate(sequence) if model == name]
        critical[name] = {"expected": wanted, "actual": positions}
        if positions != wanted:
            errors.append(f"Critical model index differs: {name}")
    radio = manifest["radio_game_dat16"]
    raw = reader.read(radio["path"])
    radio_result = {"path": radio["path"], "expected_sha256": radio["sha256"],
                    "actual_sha256": None if raw is None else digest(raw),
                    "checked": check_game}
    if check_game and (raw is None or digest(raw) != radio["sha256"] or len(raw) != radio["bytes"]):
        errors.append("GAME.DAT16 is missing or differs from checkpoint 245")
    return {"schema": 1, "status": "PASS" if not errors else "FAIL", "root": str(reader.root),
            "model_count": len(sequence), "posted_224_prefix_count": len(prefix),
            "prefix_matches": sequence[:len(prefix)] == prefix,
            "appended_models_match": sequence[len(prefix):] == tail,
            "sequence_sha256": sequence_digest(sequence), "loaded_ides": loaded,
            "critical_model_indices_zero_based": critical,
            "first_mismatch": None if mismatch is None else {"index_zero_based": mismatch,
                "expected": expected[mismatch], "actual": sequence[mismatch]},
            "file_identities": identities, "radio_game_dat16": radio_result,
            "errors": errors,
            "scope": "Read-only configuration/identity check; no save parsing or gameplay certification. "
                     "Indices are zero-based list positions, not an asserted universal save-record offset."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, help="Installed game or extracted release; also requires matching GAME.DAT16")
    parser.add_argument("--report", type=Path, help="Write a new JSON report outside this Source folder and input root")
    args = parser.parse_args()
    root = (args.root or SOURCE / "config/game").resolve()
    if args.report:
        report = args.report.resolve()
        if report == SOURCE or SOURCE in report.parents or report == root or root in report.parents:
            parser.error("--report must be outside the Source folder and input root")
        if report.exists():
            parser.error("--report already exists")
    try:
        result = check(root, check_game=args.root is not None)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        result = {"schema": 1, "status": "FAIL", "errors": [str(exc)]}
    output = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open("x", encoding="utf-8") as stream:
            stream.write(output)
    print(output, end="")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
