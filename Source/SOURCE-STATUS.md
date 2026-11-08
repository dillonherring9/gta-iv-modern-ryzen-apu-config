# Source status

Snapshot: **7 November 2026**. This export covers the maintained code needed
for the current AVCNGCE release through checkpoint 215.

| Component | Verified result |
|---|---|
| 93 SCO scripts | Fresh assembly reproduces every member and the entire current installed/release `script.img`, including archive metadata and padding |
| `VCMusic.asi`, `VCMemory.asi`, `VCScriptHook.asi`, `ScriptHook.dll` | Fresh Zig 0.13.0 builds match the release after setting the PE COFF timestamp to zero in both files |
| `VCAudioHeap.asi` | Fresh build also matches the raw release bytes |
| `GTA_IV_DualSense.asi` | Included patch chain reproduces the release hash from the supported original 1.4 binary; full vendor source is not included |

The script image SHA256 is
`5da9c42a6b82039aaccac0d468c4d11f2bc672dbf10b1e8bdeaca6de8110ef71`.
The exported member manifest restores two stale resource-type fields to the
current image values; all 93 `.scasm` files retain their maintained bytes.
[scripts/manifest.json](scripts/manifest.json) records the source and member
hashes. Native builds write `native-build.json` with their output identities.

This is a curated source export, not a complete release build system.
FusionFix and its D3D9 proxy have retained matching-version reference source,
but exact binary reproduction was not established here. The retained DLSS-IV
checkout is newer than the installed binary. Exact local source/build mappings
were not established for Project Thunder CE, Mjolnir IV or Xbox Rain Droplets.
Those vendor components and game assets are outside this export. The optional
local `VCPhysicsCheck` diagnostic is deliberately excluded. Historical recovered
C script variants are also excluded because bulk rebuilding them would lose
later mission changes.

On 7 November the project owner reported that “everything works.” This is a
positive general gameplay report. The export checks establish file identities
and reproducible builds; they do not provide comprehensive mission, language,
platform or save/load coverage. The historically intermittent no-window
startup issue has no established permanent cure.
