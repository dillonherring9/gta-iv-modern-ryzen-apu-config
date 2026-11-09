# Building

Run these commands from the **source folder root**, the directory containing
this file. This also applies when the folder is nested under `Source/` in a
larger repository. Keep outputs in a separate directory such as
`../VCANGCE-build/`.

Before editing, run `python3 tools/verify_source.py` to verify the published
source snapshot. [PROVENANCE.json](PROVENANCE.json) records origins and export
adjustments; [VALIDATION.json](VALIDATION.json) records the release comparisons.

Use Python **3.11+**. Native builds require **Zig 0.13.0** and target 32-bit
Windows. The included Opus subset is built automatically for `VCScriptHook.asi`.
No downloaded Windows SDK or original game files are needed for native builds.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -I tools/build_native.py --zig /path/to/zig --out ../VCANGCE-build/native
```

On Windows, activate `.venv\Scripts\activate` instead. `--module vcaudioheap`
builds one native module; other IDs are `vcmusic`, `vcmemory`, `vcsh_loader`
and `scripthook`. `--fixtures` additionally compiles the isolated audio-heap
fixture; running it under Windows or Wine is a separate action. See
[native/README.md](native/README.md) for module dependencies. The current
sources include shared startup logging/settings helpers and the deferred
ScriptHook startup logger.

Script builds need `openssl` in PATH and the pinned
**FourFury 0.13.3** from `requirements.txt`. Supply your own GTA IV CE
executable to read its encryption key. To reproduce the current archive:

```sh
python tools/build_scripts.py --exe "/path/to/GTAIV.exe" --out ../VCANGCE-build/scripts --compare "/path/to/update/common/data/cdimages/script.img"
```

The tool writes `script.img` and `build-report.json`, requires all **96**
sources and rejects output inside this export or the game directory. For
edited scripts, omit `--compare` and review the resulting report. Details are
in [scripts/README.md](scripts/README.md). This builds the script archive;
parachute/suit models, textures, collision and audio are supplied by the
playable release and are outside this source builder.

The checkpoint-224 assembler models `PushS` as signed 16-bit and rejects
out-of-range operands. Use full-width `Push` for positive global addresses
above 32767. For example, `PushS 60000` is invalid source; encoding those bits
would execute as -5536 in CE and could corrupt memory through `GetGlobalPs`.
See [scripts/README.md](scripts/README.md) for the other CE constraints.

Check the built archive's signed operands and direct global addresses with:

```sh
python tools/check_script_contracts.py --exe "/path/to/GTAIV.exe" --image ../VCANGCE-build/scripts/script.img --report ../VCANGCE-build/script-contracts.json
```

The report must be a new file outside the export and game. An optional
`--negative-control "/path/to/old/script.img"` verifies that an older broken
archive is detected and intentional negative short constants are preserved.
This checks adjacent integer-immediate `GetGlobalPs` operands against the
archive's declared global storage; computed indices, control flow and gameplay
require separate review. The export was checked against the checkpoint-221
negative control, detecting all 443 invalid addresses.

The optional DualSense patch path needs **pefile 2024.8.26** and the supported
original NodixTech DualSense 1.4 ASI, which is not included:

```sh
python -m pip install -r tools/requirements-patches.txt
python -I tools/patch_dualsense.py --input "/path/to/original/GTA_IV_DualSense.asi" --output ../VCANGCE-build/GTA_IV_DualSense.asi
```

The wrapper applies the two hash-guarded patches in order and verifies the
final release hash. It preserves the input and refuses an existing output.
Builds do not copy runtime libraries, settings or content into a game.
