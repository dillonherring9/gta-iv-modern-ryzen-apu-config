# Source status

Code snapshot: **13 November 2026**. This export covers maintained code for
**Vice City Actual Next Gen CE** through checkpoint **269**, matching the
locally prepared `VCANGCE-269.zip` release's covered scripts, native components and
included text configuration.

## Covered code

- All **96** current SCO scripts are editable assembly, including the imported
  parachute controllers, wearable-aircraft adapter, movement compatibility
  changes and expanded Pole Position behavior. The later club changes preserve
  Vice City's asset progression while adding pole/table dancers, two floor
  dancers and GTA IV-style private lap dances. Their required models, accessories
  and other binary assets belong to the playable release.
- The maintained native component sources are included. **VCCore.asi** links six
  of them behind the dispatcher: audio heap, VCCutscene (cutscene outfits and the
  language menu), VCAPC (indestructible APC/Rhino), the FusionFix ed3ace
  loading-screen backport, music and the controller loader. **ScriptHook.dll**
  remains separate for its ABI. The checkpoint-220 memory module, whose world
  clearing and model-budget/density overrides checkpoint 248 removed, is kept
  only as a historical standalone build target. Existing settings and startup
  repairs are retained; the unsuccessful checkpoint-223 diagnostic guard is absent.
- The Mjolnir IV quality-check patch recipe is included; the vendor binary is not.
- The two hash-guarded DualSense patches are included. The original vendor
  binary and its full source are not included.
- The [configuration snapshot](config/README.md) contains the current text
  loaders, IDE model definitions and radio station limit, together with a
  frozen posted-release model-order baseline and a read-only checker. It
  records the save-safe model layout installed by checkpoint 245, unchanged
  through checkpoint 269.

The checkpoint-269 mixed `script.img` (unchanged since 252) SHA256 is
`ddb8dc6f3d1944f92fd92d96a36839464a6885ce26e003e8a6092ffd74517d3a`.
Its **4,296** members comprise **96** covered gameplay scripts, **21** unchanged
network scripts and **4,179** other resources. The latter **4,200** members are
preserved from a user-supplied, hash-guarded release archive during a build;
their bytes are not embedded in this source export.
[scripts/manifest.json](scripts/manifest.json) records the complete image and
canonical script projection. Native builds write `native-build.json` with
output identities. The current VCCore release SHA256 is
`906fe70328aad3afc50787bf9c595bfdfcec4cb407b2f91599e1ce9b4cf95985`.

## Retained legacy reproduction results (11 November 2026)

| Component | Retained exact-build result |
|---|---|
| All 96 gameplay SCOs and mixed `script.img` | Fresh assembly plus the supplied release resource baseline reproduces the complete 859,693,056-byte archive exactly, including all 4,200 preserved allocations |
| `VCCore.asi` and `ScriptHook.dll` | A fresh Zig 0.13.0 build exactly matches the current release bytes; the bundled Opus subset is freshly compiled across 137 translation units |
| Text configuration | Snapshot identities match checkpoint 269 (unchanged since 245); the checker preserves the original 5,740 model definitions in order and verifies eight appended dancer peds |

The 96-member canonical script projection SHA256 is
`a3902018583d569e56574b105bb91ac96d7e728e8fd3b04f5249d9ccee86d59e`.
The current ScriptHook SHA256 is
`5071926931d9f82285cb0e7fade442fd9cf7743877109201980f08b6926bf1b9`.
[VALIDATION.json](VALIDATION.json) records publication-specific build results,
archive comparisons, input identities and their scope.

The retained checkpoint-245 baseline passed audit pass 14: **38 commands, zero
failures and zero flags**. Earlier pass-10 source comparisons and 117 SCO
roundtrips, and an isolated rollback of changes 241 through 235, remain
historical evidence for the unchanged code and consolidation work. The
checkpoint-241 radio change was subsequently undone. Maintained-project audits
are separate from the publication-specific checks recorded in the validation
file. Builds, parser checks and isolated fixtures do not establish gameplay
success.

The unchanged DualSense two-patch chain has retained verification against the
supported original 1.4 binary. Full vendor source is not included. Earlier
checkpoint-224 executable-backed verification detected the signed-global
negative control, and covered 23 compiled aircraft fixtures and 34 movement
predicate cases.

## Current behavior and evidence

Checkpoint 252 (11 November 2026) adds the indestructible APC/Rhino, the
pizza throw and delivery, VCCutscene in place of the cutscene memory clearing,
the FusionFix ed3ace loading-screen backport and the Mjolnir quality-check
patch. The 11 November fresh builds reproduce its VCCore, ScriptHook and
`script.img` exactly; both compiled fixtures pass under Wine. The owner ran
build 252: every VCCore module logged active, all 14 VCAPC vehicle natives
resolved, and Mjolnir logged GTA IV's own quality check as active.

Checkpoints 253 and 254 change only binary map data in the release, so this export's code, scripts and
configuration are unchanged. Step 253 moves 134 trees that stood on bare sidewalk, road or concrete (no planter
or soil) below the map, changing only their placement heights. Step 254 rebuilds the block index of Vice City's
`distantlights.dat`, which had carried Liberty City's tables, so FusionFix's Distant Lights draw Vice City's roads.

The parachute is automatically wearable in full-size planes and helicopters,
excludes the two RC aircraft, and lets the player keep a gun without selecting
a parachute weapon. The retained correction changes 443 positive global-address
loads to full-width `Push`: CE sign-extends the 16-bit `PushS` operand, so the old
`PushS 60000` accessed global index -5536 and overwrote Tommy's animation buffers.
Seven intentional negative constants retain their operand bytes. The movement
predicate covers all 30 protected parachute animation names, including landing
and the installed `free_fall_deccelerate` spelling. Native startup logging and
settings reads were revised after live thread captures established loader-lock
cycles.

The owner confirmed that the previously crashing high-speed windshield-ejection
and rolling test stays running normally, and confirmed automatic aircraft pack,
gun use while wearing it, deployment and landing. A read-only monitored impact
session retained valid player animation-channel headers during play. Earlier
reports covered save loading, normal play and steering. Pole Position received
positive club-entry and general-play reports after its accessory correction.

The checkpoint-241 radio station-list retry broke the owner's existing saves
and was undone. The current release retains the previous station list and
station limit of **10**. The two appended car-sound definitions from checkpoint
233 remain; the save-breaking radio-list import is absent.

Checkpoint 245 restores the posted checkpoint-224 model order and appends the
eight new dancer definitions after all existing models. Peds and objects share
one model-number list. Inserting new peds ahead of the objects, and later
removing definitions, made saved pickups draw unrelated models even though
collecting them still worked. The owner tested checkpoint 245 with an existing
save: the Ocean View hotel pickup again displays the folded shirt, and all
Pole Position dancers remain present. The release supports continuing existing
saves and does not require a new game for these changes.

The owner confirmed on 10 November 2026 that all crashes reported during
development are resolved in play: the no-window startup, post-save,
windshield-ejection/ragdoll and Riot failures. Riot was completed and the next
mission started. Road tyre sounds, Skimmer water takeoff, motorcycle helmets,
the blue Soiree suit in cutscenes and the new ground textures, viewed from the
ground and the air, are also confirmed in play testing. Later texture, voice
and consolidation changes have positive general-play reports alongside their
static/build evidence. The remaining untested parts are strict weapon
attribution in some rampages, and Windows and Steam Deck. These gameplay
observations come from the owner's Linux/Proton reports; this source review
and the automated build checks do not launch the game.

## Source scope

This is a curated code export. The 96 assembly sources preserve the current
compiled scripts; they do not provide all original C or .NET projects.
Historical recovered C variants are excluded because bulk rebuilding them can
discard later fixes.

The checkpoint-269 release also includes archive/map-definition consolidation,
language-table merging, retained radio/audio data, texture/voice repairs, new
club models/accessories, parachute and blue Soiree assets, ground mapping and
other community content. The included text configuration records the current
loader and model-definition state. Those binary integrations and their full
asset-authoring pipeline are not included or regenerated by these builders.
The mixed-image builder preserves the release's resource allocations rather
than rebuilding them. Current credits retain the upstream authors and development references;
this does not imply that their full original projects are present here.

FusionFix and its D3D9 proxy have retained matching-version reference source
outside this export; exact binary reproduction was not established here.
The paired VCRender DLSS-IV client/helper are covered by the new renderer
export and its dedicated verification. The separately retained root DLSS-IV.asi
is disabled by global.ini while VCRender is active; this export makes no new
exact-source claim for that older inactive binary. Exact local source/build
mappings were not established for Project Thunder CE, Mjolnir IV or Xbox Rain Droplets. Those vendor components remain outside this export;
the project's exact-input Mjolnir patch is in `native/mjolnir-patch/`.
The optional local `VCPhysicsCheck` diagnostic is also excluded.

## Renderer additions and evidence through Build 269

The VCRender client/host and paired upscaler sources are now included. VCRender
redirects D3D9 work from the 32-bit game to an external 64-bit DXVK host; the
paired FSR path also runs externally. The current client includes accurate
backend resource-creation errors, streaming-load diagnostics and the physical
resource pool for large texture/vertex blocks. The existing world/scripts and
VCCore/ScriptHook binaries remain unchanged from the build-254 export.

The installed build-264 game session on 13 November recorded an active pool,
3,259 loads, zero failed loads, eight large allocations, two fallback allocations
and ten frees. It does not establish a successful repeat of the exact prolonged
drive or helicopter-jump failure scenario. The renderer work through 261 has
the owner's general positive gameplay report. Source publication launches no game.

Earlier native/script reproduction results above were obtained for the unchanged
252/254 sources on 11 November; they are retained historical evidence, not new builds for this publication. New renderer export builds and their exact release comparisons
are recorded separately in `renderer_verification` in VALIDATION.json.

## Current renderer verification and gameplay scope (Build 269)

Fresh cross-builds from this exported renderer source reproduce the exact
Build 269 client and host. Paired DLSS-IV sources and outputs remain byte-identical
to the separately reproduced Build 264 pair. The current renderer carries
protocol 4; mixed older/newer client-host pairs are refused. Reset preserves
implicit swapchain identity; held ordinary D3D9 surfaces reject Reset with
balanced ownership, while Ex retains usable old surfaces. Transient presenter
mapping failures retry, captures pin their reading slot, and failed FSR shutdown
propagates quarantine and rejects Reset while pinning host cleanup until exit.
The step-268 bounded one-time startup-window handoff remains byte-identical.

Retained installed-269 fixtures: renderer 77 and resource-error 88 differential
checks plus 35 retained-swapchain assertions match configured direct DXVK;
FAST/BRIDGE 42 each, proxy 14, startup 38 and borderless 58 pass. Reset contract
60 has the same 16 reviewed reference/runtime differences and no new differences.
Targeted ownership, mapping recovery, helper-death shutdown/Reset/release,
capture interleaving and protocol-mismatch controls pass. These fixture results
do not establish recovery from an arbitrary GPU or driver hang.

The owner confirmed startup and menu/gameplay in an actual nested Gamescope
session on build 268. That is retained evidence for 268; no actual-game test of
269 or full Steam Gaming Mode confirmation is claimed. The older build-264
streaming session above remains dated evidence, and the original prolonged-drive
or helicopter-jump reproduction still lacks a distinct successful retest.
