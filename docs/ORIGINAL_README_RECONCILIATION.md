# Original README Preservation and Current Version 3 Reconciliation

The original maintenance-oriented README is deliberately being restored as the public base document. Its value is that it tells a careful user how to think: establish one renderer path, keep working copies before changing binaries, use the 32-bit asset for a 32-bit game, inspect `GTAIV_d3d9.log`, warm caches before judging a result, test both episodes, and retain a rollback path. Those principles remain valid.

However, it was written for an earlier nine-file GPLAsync-centered installer and must not silently redefine the current Version 3 contract. This record makes the difference visible. The original prose remains preserved word-for-word; the current Version 3 annotations below and beside it take precedence wherever they differ.

| Original-guide topic | Preserved value | Current Version 3 treatment |
|---|---|---|
| Renderer profile | Documents a successful GPLAsync-style DXVK route and explains controlled binary replacement. | The current installer writes the upstream-compatible stock `dxvk.conf` and assumes the pre-existing Drag-and-Drop Archive Vulkan route. GPLAsync remains a retained advanced alternative/reference, not an automatically installed renderer profile. |
| DXVK update procedure | The original five-step `x32\d3d9.dll` to `vulkan.dll` replacement sequence is preserved exactly because GTA IV is 32-bit and FusionFix’s Vulkan route uses `vulkan.dll`. | This is a **manual maintainer experiment**, not a routine installer step or guaranteed upgrade. Back up the archive’s working `vulkan.dll`, change only one binary, test the log and repeat runs, and restore the known-good file if it fails. Gillian’s archive remains a curated/version-locked environment. |
| Automatic installer | Explains an older installer name, Version 3.0.0 wording, nine managed files, and the former backup path. | The present executable is `GTAIV_After_Dark_Complete_Configuration_v3.exe`; it installs seven managed configuration files, creates a timestamped complete-configuration backup, sets `RUNASADMIN` on every installation, and restores the exact prior compatibility value on uninstall. |
| AudioMap | The original guide lists `AudioMap.ini` among the old configuration files. | AudioMap is no longer a current project file, payload file, manual-install target, or installer target. The audit found it was not a documented Drag-and-Drop Archive component. Its old appearance remains visible only as historical evidence. |
| Static `preload.list` | The original guide correctly warns that the file requires matching shader files. | The list remains retained for reference, but must not be blindly copied into the current archive. Modern FusionFix shader metadata must match the actual installed shader layout; the current installer leaves it untouched. |
| FusionFix and loader updates | The original guide teaches controlled upgrades and one loader path. | Do not use it as permission to update FusionFix inside the archive casually. Gillian specifically advises against enabling FusionFix update checks in the archive. The former exact FusionFix ASI filename check was removed because it was brittle, not because any loader/binary arrangement is interchangeable. |
| Third-party assets | The original guide preserves names, credits, and linked original sources. | Those credits and sources remain public and embedded. Version 3 still redistributes none of their game/mod binaries. |
| Reference APU | The original Ryzen 5 PRO 6650U/Radeon 660M narrative is the project’s real reference context. | It remains a reference setup, not a benchmark promise, a requirement, or an AMD-only limitation. |

## Current source boundaries

FusionFix’s official project supports GTA IV Complete Edition and documents its selectable Vulkan path.[1] Gillian’s current Drag-and-Drop Archive is a complete ready-to-play single-player package that must be extracted into the game root; it describes Complete Edition as the stable/polished route and instructs users not to enable FusionFix update checks inside the archive.[2]

> **Reading rule:** The original guide teaches the maintenance method. The current Version 3 annotation identifies what the current installer does automatically. If a maintainer changes DXVK, an ASI loader, shader metadata, or texture/content layout manually, that maintainer owns the controlled-test and rollback process described in the original guide.

## References

[1] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
