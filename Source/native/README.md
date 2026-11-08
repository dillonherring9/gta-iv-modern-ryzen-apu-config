# Native project code

These are the exact maintained authoring files for five modules in AVCNGCE.
The portable builder writes fresh outputs and never reads or changes a game.
Use Python 3.9+ and Zig 0.13.0:

```sh
python3 -I tools/build_native.py --zig /path/to/zig --out ../AVCNGCE-build/native
```

Run this from the source folder root (the directory containing `BUILDING.md`).
`--module vcaudioheap` builds one module;
the other module IDs are `vcmusic`, `vcmemory`, `vcsh_loader`, and `scripthook`.
`--fixtures` also builds the audio-heap guard fixture, which can be run explicitly
under Windows or Wine. Compilation does not run a game or establish gameplay behavior.

| Directory | Output | Authoring checkpoint | Dependencies |
|---|---|---|---|
| `vcmusic` | `VCMusic.asi` | 205 | Windows runtime; loads the release's BASS library dynamically |
| `vcmemory` | `VCMemory.asi` | 192 | Windows PSAPI; bundled outfit and scene headers |
| `vcsh_loader` | `VCScriptHook.asi` | 189 | Bundled Opus 1.6 source; Windows HID, SetupAPI, OLE and multimedia APIs |
| `scripthook` | `ScriptHook.dll` | 161 | Bundled export definition and generated native-name table |
| `vcaudioheap` | `VCAudioHeap.asi` | 215 | Windows APIs; optional isolated fixture |

PE COFF timestamps are normalized to zero in new outputs. Earlier distributed
modules can have nonzero build timestamps, so raw SHA256 comparison alone is
insufficient for those modules. The generated `native-build.json` records the
new output hashes. All five modules were freshly compiled from this package
and matched the release byte for byte after timestamp normalization;
`VCAudioHeap.asi` also matched its raw release hash. This check did not install
or replace any release files.

`scripthook/natives_table.h` is the exact generated table used by the maintained
shim; it originated from the retained SC-CL / IV-SDK native declaration reference.
Regeneration is not needed to compile. This package does not claim ownership of
the native API names or relicense that upstream reference.

The controller source cites LinuxAudio4Dualsense5 0.6 (GeorgLegato, AGPL-3.0) for
Bluetooth speaker and haptics protocol work. Its preserved upstream license is
`vcsh_loader/AGPL-3.0-reference.txt`. Opus keeps its BSD and patent notices in
`../dependencies/opus-1.6/`. These notices are provenance, and do not assign a
new blanket license to every project file.

`dualsense-patches/` holds the two original hash-guarded binary patch scripts,
not NodixTech's full vendor source or binary. After installing
`tools/requirements-patches.txt`, supply the original GTA IV DualSense 1.4 ASI:

```sh
python3 -I tools/patch_dualsense.py --input /path/to/original/GTA_IV_DualSense.asi --output ../AVCNGCE-build/GTA_IV_DualSense.asi
```

The wrapper refuses an existing output, preserves the input, applies 165 then
166, and verifies the final release hash. It does not install a host audio
service or alter game settings.
