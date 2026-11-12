#!/usr/bin/env python3
"""Build VCANGCE's current project-native modules without reading or installing a game.

Requires Python 3.9+ and Zig 0.13.0. All intermediate and compiled files stay
under --out; sources and bundled dependencies are read only.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MODULES = ("vccore", "scripthook")
LEGACY_MODULES = ("vcmusic", "vcmemory", "vcsh_loader", "vcaudioheap")
# Object order equals the maintained project build, so outputs match the release bytes.
CORE_MODULES = (("vcaudioheap", "vcaudioheap.c"), ("vccutscene", "vccutscene.c"),
                ("vcapc", "vcapc.c"), ("vcloading", "vcloading.c"),
                ("vcmusic", "vcmusic.c"), ("vcsh_loader", "vcsh_loader.c"))


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("Command failed: " + subprocess.list2cmdline(command) +
                           "\n" + result.stdout + result.stderr)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize_pe_timestamp(path):
    data = bytearray(path.read_bytes())
    if data[:2] != b"MZ":
        raise ValueError("Expected a PE image: " + str(path))
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE signature: " + str(path))
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x14c:
        raise ValueError("Expected an x86 PE image: " + str(path))
    struct.pack_into("<I", data, pe + 8, 0)
    path.write_bytes(data)


def opus_sources(directory):
    def variable(filename, name):
        text = (directory / filename).read_text().replace("\\\n", " ")
        match = re.search(r"^" + name + r"\s*=\s*(.*)$", text, re.M)
        if not match:
            raise ValueError("Missing Opus source list: " + name)
        return match.group(1).split()
    return (variable("celt_sources.mk", "CELT_SOURCES") +
            variable("silk_sources.mk", "SILK_SOURCES") +
            variable("silk_sources.mk", "SILK_SOURCES_FLOAT") +
            variable("opus_sources.mk", "OPUS_SOURCES") +
            variable("opus_sources.mk", "OPUS_SOURCES_FLOAT"))


def build_opus(zig, output, jobs):
    source = ROOT / "dependencies/opus-1.6"
    work = output / "opus"
    work.mkdir(parents=True, exist_ok=True)
    flags = ["-target", "x86-windows-gnu", "-O2", "-DOPUS_BUILD", "-DUSE_ALLOCA",
             "-DHAVE_LRINTF", "-DHAVE_LRINT", "-DNDEBUG"]
    for folder in ("include", "celt", "silk", "silk/float", "."):
        flags.append("-I" + str(source / folder))
    flags.append("-w")
    sources = opus_sources(source)
    objects = [work / (name.replace("/", "_") + ".o") for name in sources]
    commands = [[zig, "cc", *flags, "-c", str(source / name), "-o", str(obj)]
                for name, obj in zip(sources, objects)]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        list(pool.map(run, commands))
    library = work / "libopus.a"
    library.unlink(missing_ok=True)
    run([zig, "ar", "rcs", str(library), *map(str, objects)])
    return library, {"version": "1.6", "source_count": len(sources),
                     "sha256": sha256(library), "bytes": library.stat().st_size}


def build_core(base, output, library):
    """Keep the six component modules in separate translation units."""
    work = output / "vccore-objects"
    work.mkdir(parents=True, exist_ok=True)
    objects = []
    for module, filename in CORE_MODULES:
        source = ROOT / "native" / module
        obj = work / (module + ".o")
        run([*base, "-fno-common", "-DDllMain=" + module + "_DllMain",
             "-I" + str(source), "-I" + str(ROOT / "dependencies/opus-1.6/include"),
             "-c", "-o", str(obj), str(source / filename)])
        objects.append(obj)
    source = ROOT / "native/vccore"
    obj = work / "vccore.o"
    run([*base, "-fno-common", "-I" + str(source), "-c", "-o", str(obj),
         str(source / "vccore.c")])
    objects.append(obj)
    target = output / "VCCore.asi"
    run([base[0], "cc", "-target", "x86-windows-gnu", "-O2", "-shared", "-s",
         "-o", str(target), *map(str, objects), str(library), "-lpsapi", "-lsetupapi",
         "-lhid", "-lole32", "-lwinmm"])
    return target


def build(args):
    compiler = shutil.which(args.zig)
    if not compiler:
        raise ValueError("Zig was not found: " + args.zig)
    version = subprocess.check_output([compiler, "version"], text=True).strip()
    if version != "0.13.0":
        raise ValueError("Use Zig 0.13.0; found " + version)
    output = Path(args.out).expanduser().resolve()
    if output == ROOT or ROOT in output.parents and output.parts[len(ROOT.parts)] in (
            "native", "dependencies", "tools", "scripts", "docs"):
        raise ValueError("Choose a build output directory outside the source directories")
    output.mkdir(parents=True, exist_ok=True)
    chosen = MODULES if args.module == "all" else (args.module,)
    library = None
    report = {"compiler_version": version, "target": "x86-windows-gnu",
              "pe_coff_timestamp": 0, "modules": {}}
    if any(module in chosen for module in ("vcsh_loader", "vccore")):
        library, report["opus"] = build_opus(compiler, output, args.jobs)
    base = [compiler, "cc", "-target", "x86-windows-gnu", "-O2",
            "-Wall", "-Wextra", "-Wno-misleading-indentation"]
    for module in chosen:
        source = ROOT / "native" / module
        extra = []
        if module == "vccore":
            target = build_core(base, output, library)
            name = target.name
        elif module == "vcmusic":
            name, inputs, flags = "VCMusic.asi", [source / "vcmusic.c"], []
        elif module == "vcmemory":
            name, inputs = "VCMemory.asi", [source / "vcmemory.c"]
            flags, extra = [], ["-lpsapi"]
        elif module == "vcsh_loader":
            name, inputs = "VCScriptHook.asi", [source / "vcsh_loader.c"]
            flags = ["-I" + str(ROOT / "dependencies/opus-1.6/include")]
            extra = [str(library), "-lsetupapi", "-lhid", "-lole32", "-lwinmm"]
        elif module == "scripthook":
            name, inputs = "ScriptHook.dll", [source / "vcsh.c", source / "vcsh.def"]
            flags = []
        else:
            name, inputs = "VCAudioHeap.asi", [source / "vcaudioheap.c"]
            flags = []
        if module != "vccore":
            target = output / name
            command = [*base, *flags, "-shared", "-s", "-o", str(target),
                       *map(str, inputs), *extra]
            run(command)
        normalize_pe_timestamp(target)
        for suffix in (".lib", ".pdb"):
            target.with_suffix(suffix).unlink(missing_ok=True)
        report["modules"][module] = {"file": name, "bytes": target.stat().st_size,
                                     "sha256": sha256(target)}
        print(name + " " + sha256(target), flush=True)
    if args.fixtures:
        report["fixtures"] = []
        for module in ("vcaudioheap", "vccore"):
            fixture = output / (module + "-fixture.exe")
            fixture_flags = ["-Wno-unused-function"] if module == "vccore" else []
            run([*base, "-s", "-Wall", "-Wextra", "-Werror", *fixture_flags,
                 str(ROOT / "native" / module / "fixture.c"), "-o", str(fixture)])
            normalize_pe_timestamp(fixture)
            report["fixtures"].append({"file": fixture.name, "sha256": sha256(fixture),
                                       "status": "Built; run explicitly under Windows or Wine."})
    (output / "native-build.json").write_text(json.dumps(report, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", default="zig", help="Zig 0.13.0 executable")
    parser.add_argument("--out", default=str(ROOT / "build/native"), help="Build output directory")
    parser.add_argument("--module", choices=("all", *MODULES, *LEGACY_MODULES), default="all",
                        help="Default: current VCCore + ScriptHook ABI DLL. Legacy IDs build historical standalone ASIs.")
    parser.add_argument("--jobs", type=int, default=4, help="Parallel Opus compiler jobs")
    parser.add_argument("--fixtures", action="store_true",
                        help="Also build isolated audio-heap and VCCore dispatcher fixtures")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    try:
        build(args)
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
