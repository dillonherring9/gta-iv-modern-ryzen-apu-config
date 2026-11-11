# Building

Run these commands from the **source folder root**, the directory containing
this file. This also applies when the folder is nested under `Source/` in a
larger repository. Keep outputs in a separate directory such as
`../VCANGCE-build/`.

Before editing, run `python3 tools/verify_source.py` to verify the published
source snapshot. [PROVENANCE.json](PROVENANCE.json) records origins and export
adjustments; [VALIDATION.json](VALIDATION.json) records publication checks and
release comparisons.

Use Python **3.11+**. Native builds require **Zig 0.13.0** and target 32-bit
Windows. The included Opus subset is built automatically for `VCCore.asi`.
No downloaded Windows SDK or original game files are needed for native builds.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -I tools/build_native.py --zig /path/to/zig --out ../VCANGCE-build/native
```

On Windows, activate `.venv\Scripts\activate` instead. The default outputs are
**VCCore.asi** and **ScriptHook.dll**. `--module vccore` or `--module scripthook`
builds one of them. Optional legacy module IDs are `vcaudioheap`, `vcmemory`,
`vcmusic` and `vcsh_loader`; they build the earlier standalone ASIs from the
retained module sources. The consolidated release uses VCCore. Its dispatcher
refuses to initialize if any legacy ASI is present, so remove those standalone
outputs when using the consolidated layout. Settings still use
`VCMemory.ini`, `VCMusic.ini`, `VCScriptHook.ini` and the other existing
module-specific filenames; there is no replacement `VCCore.ini`. Logs also
retain their module-specific filenames, including `VCAudioHeap.log`.

`--fixtures` additionally compiles the isolated audio-heap guard and VCCore
dispatcher fixtures; running them under Windows or Wine is a separate action. See [native/README.md](native/README.md)
for module dependencies, initialization behavior and notices. The retained
startup helpers and deferred ScriptHook startup logger remain part of the build.

## Configuration and model order

The [configuration snapshot](config/README.md) contains the current text
loaders, IDE definitions and `stationslimit.txt`. Check it with the standard
library only; no game installation or save files are needed:

```sh
python3 -I tools/check_model_order.py
```

To check an extracted checkpoint-245 release or an installed game instead:

```sh
python3 -I tools/check_model_order.py --root "/path/to/release" --report ../VCANGCE-build/model-order.json
```

The second command also checks that release's `GAME.DAT16` identity. This binary
radio file is supplied by the playable release, rather than embedded here. The
report must be new and outside both the source folder and the checked root.

The checker expands the actual IDE load order and checks the original 5,740
definitions followed by the eight new dancer peds. Peds share the model list
with objects; `hier` records are excluded and an MLO contributes its header.
Inserting, deleting or reordering definitions ahead of an existing model can
make saved pickups display another model. Append new definitions after the
entire existing load order. The checkpoint-241 radio-list retry was undone
after it broke saves; preserve the current station list and limit of 10.
These checks establish configuration identities and order, not gameplay.

## Scripts

Script builds need `openssl` in PATH and the pinned **FourFury 0.13.3** from
`requirements.txt`. Supply your own GTA IV CE executable to read its encryption
key and the **checkpoint-245** release archive as a read-only resource baseline:

```sh
python tools/build_scripts.py --exe "/path/to/GTAIV.exe" --resources-from "/path/to/release/update/common/data/cdimages/script.img" --out ../VCANGCE-build/scripts --compare "/path/to/release/update/common/data/cdimages/script.img"
```

The baseline is hash-guarded to the current release; an older script-only IMG
cannot supply its resources. It contains **4,296 members**: the **96** covered
gameplay scripts, **21** preserved network scripts and **4,179** other game
resources. The builder assembles all 96 sources and retains the other **4,200**
complete allocations, including their metadata and padding. It writes
`script.img` and `build-report.json` outside this export and the game directory.
With unedited sources, `--compare` checks the complete archive byte for byte.
For edited scripts, omit `--compare` and review the report; the resource baseline
is still required. See [scripts/README.md](scripts/README.md) for details.

The assembler models `PushS` as signed 16-bit and rejects out-of-range operands.
Use full-width `Push` for positive global addresses above 32767. For example,
`PushS 60000` is invalid source; encoding those bits would execute as -5536 in CE
and could corrupt memory through `GetGlobalPs`. See [scripts/README.md](scripts/README.md)
for the other CE constraints.

Check the built archive's signed operands and direct global addresses with:

```sh
python tools/check_script_contracts.py --exe "/path/to/GTAIV.exe" --image ../VCANGCE-build/scripts/script.img --report ../VCANGCE-build/script-contracts.json
```

The checker selects the 96 covered scripts from either a mixed archive or their
96-member projection. It does not audit the preserved network scripts or game
resources. The report must be a new file outside the export and game. An optional
`--negative-control "/path/to/old/script.img"` verifies that an older broken
archive is detected and intentional negative short constants are preserved.
The check covers adjacent integer-immediate `GetGlobalPs` operands against the
declared global storage; computed indices, control flow and gameplay require
separate review. Retained checkpoint-224 verification detected all 443 invalid
addresses in the checkpoint-221 negative control.

The optional DualSense patch path needs **pefile 2024.8.26** and the supported
original NodixTech DualSense 1.4 ASI, which is not included:

```sh
python -m pip install -r tools/requirements-patches.txt
python -I tools/patch_dualsense.py --input "/path/to/original/GTA_IV_DualSense.asi" --output ../VCANGCE-build/GTA_IV_DualSense.asi
```

The wrapper applies the two hash-guarded patches in order and verifies the
final release hash. It preserves the input and refuses an existing output.
Builds do not copy runtime libraries, settings or content into a game. The
release supplies the models, textures, collision, language tables and audio
needed by the current scripts; these builders do not regenerate that content.
