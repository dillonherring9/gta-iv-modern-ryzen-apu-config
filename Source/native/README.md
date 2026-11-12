# Native project code

These maintained authoring files reproduce the two project-native modules in
VCANGCE at checkpoint 254: `VCCore.asi` and `ScriptHook.dll`. The portable
builder writes fresh outputs without reading or changing an installed game.
Use Python 3.11+ and Zig 0.13.0 from the source folder root:

```sh
python3 -I tools/build_native.py --zig /path/to/zig --out ../VCANGCE-build/native
```

The default builds both current modules. Use `--module vccore` or
`--module scripthook` to build one. Opus 1.6 is compiled from the bundled source
for VCCore; no Windows SDK download or game files are needed.

| Directory | Current role | Authoring checkpoint | Dependencies |
|---|---|---|---|
| `vccore` | `VCCore.asi` dispatcher, linking the six components below | 252 | Bundled startup logging helper |
| `vcaudioheap` | Embedded physical audio heap fix | 220 | Windows APIs; isolated guard fixture |
| `vccutscene` | Embedded cutscene outfit selection and language-menu entry | 248 | Bundled outfit and scene headers |
| `vcapc` | Embedded APC/Rhino invulnerability | 252 | 14 byte-guarded CE vehicle natives |
| `vcloading` | Embedded FusionFix ed3ace loading-screen flush backport (GPL-3.0) | 251 | Exact CE code guards; upstream source and license in `upstream/` |
| `vcmemory` | Historical standalone memory module, replaced by `vccutscene` | 220 | Windows PSAPI; bundled outfit and scene headers |
| `vcmusic` | Embedded music bridge | 220 | Loads the playable release's BASS library dynamically |
| `vcsh_loader` | Embedded controller and ScriptHook worker | 220 | Bundled Opus 1.6; Windows HID, SetupAPI, OLE and multimedia APIs |
| `scripthook` | Separate `ScriptHook.dll` native ABI | 219 | Bundled export definition and generated native-name table |

VCCore compiles each component separately with a renamed `DllMain` and
`-fno-common`, links them in the order listed in `tools/build_native.py`, then
initializes AudioHeap, VCCutscene, Music and ScriptHook, followed by the
loading handoff and VCAPC. VCAPC therefore chains the game-loop call outside
the three existing frame hooks. The audio heap, music and controller loader
sources and headers are unchanged from 220. VCCutscene keeps VCMemory's outfit
and language support without its world clearing, model-budget and density
overrides. It reads `VCCutscene.ini`; the other INI, log, music asset and ABI DLL
names are unchanged. The loading handoff patches only two render-thread calls
inside SetLoadingRenderFunction, after exact-byte checks. VCAPC checks code in
the runtime-decrypted region only on the first frame. If any
legacy standalone ASI is present on disk or already loaded, VCCore records the
conflict and runs none of its embedded handlers. A partial old installation
therefore needs all four old ASIs removed to activate VCCore. The playable
release supplies that consolidated layout; this builder does not install it.

The builder retains explicit `--module vcaudioheap`, `vcmemory`, `vcmusic` and
`vcsh_loader` targets to reproduce the historical standalone ASIs; `vcmemory`
includes the cutscene clearing that the current build removed. They are
excluded from the default build and are not current release outputs. Do not
combine their outputs with VCCore. Vendor ASIs retain their separate upstream
contracts. The optional, read-only `VCPhysicsCheck` developer logger is excluded
from the playable release and this public source export.

The startup repairs retain immutable settings snapshots and bounded direct
Win32 logging. The ScriptHook ABI shim defers its initial file log until its
first frame; the controller worker still loads that shim outside DLL
initialization. Existing deferred BASS loading is retained. These changes
address captured loader, profile and CRT stream lock cycles. They do not
establish permanent startup reliability across every platform.

A fresh build from this export, using Zig 0.13.0 and freshly compiled bundled
Opus, reproduced the current VCCore and ScriptHook release bytes exactly.
PE COFF timestamps are normalized to zero. `native-build.json` records each
output's SHA256. Prior checkpoint-235 compiled dispatcher and synthetic x86
startup/hook fixtures remain separate evidence from gameplay. Source publication
does not launch the game or certify gameplay, device operation or save loading.

`--fixtures` additionally compiles `vcaudioheap-fixture.exe` and
`vccore-fixture.exe`; run them explicitly under Windows or Wine. The former
checks the physical audio heap guards; the latter checks 23 dispatcher cases,
including all legacy-file subsets, loaded names, invalid paths and the attach
and reverse-detach order of all six components. Neither fixture calls the game. Compilation alone does not run them.

`scripthook/natives_table.h` is the exact generated table used by the maintained
shim; it originated from the retained SC-CL / IV-SDK native declaration
reference. Regeneration is not needed to compile. This package does not claim
ownership of the native API names or relicense that upstream reference.

The controller source cites LinuxAudio4Dualsense5 0.6 (GeorgLegato, AGPL-3.0) for
Bluetooth speaker and haptics protocol work. Its preserved upstream license is
`vcsh_loader/AGPL-3.0-reference.txt`. Opus keeps its BSD and patent notices in
`../dependencies/opus-1.6/`. These notices preserve provenance and do not assign
a new blanket license to every project file.

`dualsense-patches/` holds the two original hash-guarded binary patch scripts,
not NodixTech's full vendor source or binary. After installing
`tools/requirements-patches.txt`, supply the original GTA IV DualSense 1.4 ASI:

```sh
python3 -I tools/patch_dualsense.py --input /path/to/original/GTA_IV_DualSense.asi --output ../VCANGCE-build/GTA_IV_DualSense.asi
```

The wrapper refuses an existing output, preserves the input, applies patches
165 then 166, and verifies the release hash. It does not install a host audio
service or alter game settings.

`mjolnir-patch/patch_mjolnir.py` reproduces the shipped `Mjolnir_IV.asi` from
llamacomma's official RELEASE_1.3 binary (MIT), which is not included. It
requires the exact original SHA256 and changes five bytes: it skips the block
that switches off GTA IV's native graphics-quality budget check, and keeps both
CPU fixes. Standard-library Python only:

```sh
python3 -I native/mjolnir-patch/patch_mjolnir.py /path/to/original/Mjolnir_IV.asi ../VCANGCE-build/Mjolnir_IV.asi
```
