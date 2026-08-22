# GTA IV, After Dark — Complete Install Contract Audit

## Scope that was audited

The audit covered every tracked source artifact: the complete NSIS installer, all installer payload files and mirrored reference copies, both renderer profiles, all component configurations, the streaming profile, the legacy preload list, active documentation, historical release records, the release workflow, the payload manifest, the public `v1`/`v2`/`v3` tags and releases, and the current local uncommitted prerequisite-check edits.

The project’s intended audience is not a clean unmodded GTA IV installation. It is a **GTA IV Complete Edition installation with Gillian’s Drag-and-Drop Archive already extracted into the game root**, including the archive’s plugin and content layout. Gillian describes that archive as a complete ready-to-play modpack and instructs users to extract it into the directory containing `GTAIV.exe`; the Complete Edition archive includes FusionFix, LibertyCityPlates, Xbox Rain Droplets, and Console Select Menu for the Complete Edition route.[1]

## Cumulative requirements retained

The final installer must remain a single complete package. It must preserve all configuration knowledge, source links, credits, release rationale, dependency records, history, integrity data, backups, rollback behavior, and the required per-user `GTAIV.exe` `RUNASADMIN` compatibility setting. It must not add a new public release; corrected assets replace the existing Version 3 assets in place.

The installer must no longer reject a valid Drag-and-Drop Archive installation because `plugins\GTAIV.EFLC.FusionFix.asi` is not present at that exact path. FusionFix remains part of the required archive environment, but the filename check is brittle because valid layouts/loaders can differ. The game-root check, `vulkan.dll` check, running-game check, timestamped backup, administrator compatibility backup, installer registration, and rollback remain required.

## Configuration disposition

| Artifact | Audit finding | Full-installer treatment |
|---|---|---|
| `GTAIV.EFLC.FusionFix.cfg` | Central project profile | Install to `plugins\` and back up/restore. |
| `GTAIV.EFLC.FusionFix.ini` | Central advanced profile | Install to `plugins\` and back up/restore. |
| `stream.ini` | Central streaming profile | Install to `pc\` and back up/restore. |
| `ConsoleSelectMenuIV.ini` | Complete Edition Drag-and-Drop Archive component | Install to `plugins\` and back up/restore. |
| `GTAIV.XboxRainDroplets.ini` | Complete Edition Drag-and-Drop Archive component | Install to `plugins\` and back up/restore. |
| `LibertyCityPlates.txt` | Complete Edition Drag-and-Drop Archive component; needed for the intended plate behavior | Install to `plugins\` and back up/restore. |
| `dxvk-stock.conf` | Current upstream-compatible DXVK baseline | Install as root `dxvk.conf` and back up/restore. The installer expects the archive’s Vulkan route, but does not redistribute binaries. |
| `dxvk-gplasync-v2.6.2-1.conf` | Version-locked alternative renderer profile, not a general Drag-and-Drop Archive baseline | Preserve in the complete reference package only. It is mutually exclusive with stock DXVK and must not overwrite the archive’s standard DXVK route automatically. |
| `preload.list` | Legacy static shader metadata; not a plugin setting | Preserve in the complete reference package only. Current FusionFix 5.0.1 can derive normal shader metadata from the installed shader set; overwriting it can cause LibertyCityPlates resource errors when the actual shader layout differs.[2] [3] |
| `AudioMap.ini` | DualSense Audio Mapper configuration, introduced by the old Version 2 installer but not identified as a Drag-and-Drop Archive component in the project evidence or current official archive documentation | Remove from active payload, reference package, documentation, credits, manifest, and installer contract. Retain its historical mention only inside immutable historical release records. |

## Resulting install behavior

The corrected Version 3 installer will write **seven** managed configuration files to a pre-existing Drag-and-Drop Archive game directory: root `dxvk.conf`; `pc\stream.ini`; and `plugins\ConsoleSelectMenuIV.ini`, `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `GTAIV.XboxRainDroplets.ini`, and `LibertyCityPlates.txt`.

Before replacing those files, it will create timestamped backups of all seven. Its uninstaller will restore or remove those same seven files according to the latest backup, then restore the exact prior `GTAIV.exe` compatibility value. It will not write an arbitrary static `preload.list`, GPLAsync overlay, or AudioMap configuration.

The complete package archive and embedded reference directory will still contain every retained source, credit, rationale, dependency lock, validation record, renderer alternative, legacy shader reference, and historical release note. Removing AudioMap from the active project does not remove the historical evidence that it once appeared in an older installer.

## References

[1]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[2]: [FusionFix preload-list behavior discussion](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)

[3]: [FusionFix LibertyCityPlates shader-resource issue](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)
