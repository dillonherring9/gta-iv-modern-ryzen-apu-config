# Version 4 Complete Install Contract

## Purpose and scope

Version 4 is a **complete, cumulative configuration release** for a GTA IV Complete Edition game root where Gillian’s Complete Edition Drag-and-Drop Archive is already extracted. It is not a game installer, a modpack replacement, a FusionFix/DXVK binary distributor, or a performance guarantee.

The separate Version 3 contract is preserved unchanged at [`history/V3_COMPLETE_INSTALL_CONTRACT.md`](history/V3_COMPLETE_INSTALL_CONTRACT.md), and the public `v3` tag/release is not changed by Version 4 work.

## Required pre-existing environment

The user must already have:

1. A legitimate GTA IV Complete Edition installation.
2. Gillian’s Complete Edition Drag-and-Drop Archive extracted into the folder containing `GTAIV.exe`.
3. A working archive Vulkan route, evidenced by `vulkan.dll` in the game root. The archive Setup Utility is a recovery/failsafe route when setup needs repair; it is not a reason to reinstall DXVK on an already working installation.[^1]
4. GTA IV closed before Version 4 writes files.

Version 4 must validate `GTAIV.exe`, retain the `vulkan.dll` check, and retain the running-process check. It must not require a single exact FusionFix `.asi` file path because valid archive layouts can differ.

## Full configuration disposition

| Artifact | Version 4 treatment | Backup/rollback |
|---|---|---|
| `dxvk.conf` | Install the **General modern APU** baseline by default, or the explicitly selected Radeon 660M/GPLAsync `v3.0-1` profile. Version 4 never writes a renderer DLL. | Back up and restore. |
| `pc\stream.ini` | Install the established archive streaming configuration. | Back up and restore. |
| `plugins\ConsoleSelectMenuIV.ini` | Install the Complete Edition archive component configuration. | Back up and restore. |
| `plugins\GTAIV.EFLC.FusionFix.cfg` | Install the established advanced archive profile. FusionFix’s in-game menu remains the normal user-facing adjustment surface. | Back up and restore. |
| `plugins\GTAIV.EFLC.FusionFix.ini` | Install the established advanced archive profile. | Back up and restore. |
| `plugins\GTAIV.XboxRainDroplets.ini` | Install the archive component configuration. | Back up and restore. |
| `plugins\LibertyCityPlates.txt` | Install the archive component configuration. | Back up and restore. |
| Historical GPLAsync 2.6.2 profile | Retain in reference material only; never apply automatically. | Not a managed target. |
| GPLAsync `v3.0-1` profile | Offer only as an explicit advanced `dxvk.conf` selection after local renderer verification; install no DLL and omit removed/undocumented legacy keys. | The selected root `dxvk.conf` is backed up/restored. |
| `preload.list` | Retain as reference only because static shader metadata must match the installed layout. | Never install automatically. |
| `AudioMap.ini` | Exclude from active payload and installation. Retain historical mentions only. | Never install. |

## Profile-choice rule

The installer must show two choices after the game folder is validated:

| Choice | Default status | Required condition |
|---|---|---|
| General modern APU | **Default / recommended** | Use for unknown, stock DXVK, DXVK-Sarek, or another non-GPLAsync renderer. |
| Radeon 660M / GPLAsync `v3.0-1` | Advanced opt-in | `GTAIV_d3d9.log` must already identify the separate GPLAsync `v3.0-1` renderer. The user explicitly confirms the warning. |

The advanced profile may contain `dxvk.enableAsync = true`. It must not copy `dxvk.gplAsyncCache`, which GPLAsync removed in v2.7-1, or the old `dxvk.numAsyncThreads` value unless a future artifact-specific evidence record validates it.[^2]

## Administrator and uninstall rule

Every Version 4 installation writes the per-user `RUNASADMIN` compatibility value for `GTAIV.exe`, preserving any prior value. The Version 4 uninstaller restores the latest Version 4 backup for all seven managed files and restores that exact prior compatibility value.

Version 4 uses its own backup root, uninstall registration key, uninstaller executable, and installed reference-package directory. It must not overwrite Version 3’s backup, uninstaller, or release assets.

## Complete knowledge package rule

The embedded reference directory and public complete package archive must include the current README, Version 4 release notes, this contract, the audit adoption decision, copied readable audit-source material with its hash/provenance note, all current configuration sources, the historical Version 3 documents/contract, credits, dependency locks, validation protocol, GPLAsync migration guide, preload reasoning, handoff materials, and source history.

## References

[^1]: [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[^2]: [GPLAsync `v3.0-1` README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)
