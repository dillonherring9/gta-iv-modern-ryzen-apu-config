# GTA IV, After Dark — Version 3

## Complete cumulative configuration for GTA IV Complete Edition

> **Version 3 is the complete recommended package.** It contains the current core configuration, every retained optional configuration, the renderer choices, the installer, rollback logic, documentation, sources, credits, and the project’s full decision history. It is not a patch over an earlier download.

GTA IV, After Dark is a reversible configuration package for a documented **Ryzen 5 PRO 6650U / Radeon 660M / 16 GB dual-channel** reference environment. It is not a complete modpack and does not redistribute GTA IV, FusionFix, DXVK, GPLAsync, third-party plugins, shader files, or content assets. Those remain the work of their original authors.

## Start here

| Read this | Purpose |
|---|---|
| [`RELEASE_NOTES.md`](RELEASE_NOTES.md) | What the complete Version 3 installer contains and applies |
| [`docs/PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md) | Version 1 through Version 3 reasoning, credits, and retained decisions |
| [`docs/COMPONENTS_AND_COMPATIBILITY.md`](docs/COMPONENTS_AND_COMPATIBILITY.md) | Core configuration, renderer choices, and optional component requirements |
| [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md) | Exact FusionFix and GPLAsync artifact boundaries |
| [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md) | Evidence required before calling a profile tested or log-validated |
| [`installer/BUILDING.md`](installer/BUILDING.md) | Complete installer payload, build, and Windows smoke-test contract |

## What the complete installer does

The Version 3 executable includes the entire current configuration package. It verifies the actual `GTAIV.exe` game root, compatible FusionFix/Vulkan prerequisites, and a closed game process; makes a timestamped backup; installs the safe core configuration; applies the required `GTAIV.exe` administrator compatibility setting; and places the entire reference package beside the game for offline access.

| Applied safely by default | Included in the installer and reference bundle |
|---|---|
| `pc\stream.ini` | Stock DXVK baseline and exact GPLAsync overlay |
| `plugins\GTAIV.EFLC.FusionFix.cfg` | AudioMap, Console Select Menu, Xbox Rain Droplets, and LibertyCityPlates settings |
| `plugins\GTAIV.EFLC.FusionFix.ini` | Legacy preload reference, all docs, sources, credits, and project history |
| Per-user `GTAIV.exe` `RUNASADMIN` setting | Complete tuned-file archive and optional-component guidance |

The optional files are not absent or forgotten: they are included in the complete package but not overwritten automatically. Their correct use depends on a separate plugin, a locked renderer artifact, or a verified shader layout. Read the compatibility guide before applying them manually.

## Required administrator compatibility

The installer sets **Run this program as an administrator** for `GTAIV.exe` on every Version 3 installation. This is required for the documented installation layout because GTA IV can otherwise fail to create its configuration files at launch. The installer saves the prior per-user compatibility value and restores it on uninstall.

## Installation order

1. Start from a legitimate GTA IV Complete Edition installation that launches normally.
2. Install and launch the compatible FusionFix/Vulkan route described in [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md).
3. Close GTA IV, the Rockstar Games Launcher, mod managers, and other tools that may hold files open.
4. Run the complete Version 3 installer and select the actual folder containing `GTAIV.exe`.
5. Review `GTAIV_After_Dark_Complete_Package` in the game folder before choosing an optional renderer or component setting.
6. Change one variable at a time and validate cold and warm passes under the [validation protocol](docs/VALIDATION_PROTOCOL.md).

## Renderer rule

Use one renderer route and one frame limiter. The stock DXVK profile contains upstream-compatible keys. The GPLAsync profile is tied to its exact documented artifact and must not be applied to upstream DXVK or another renderer version. Do not diagnose performance while stacking a second wrapper, limiter, ASI loader, Radeon Chill, RTSS, or an `update`-folder change.

## Cache reality

On a shared-memory APU, shader and pipeline caches must build. New effects, weather, resolution changes, driver changes, renderer changes, and major content changes can cause temporary cold-route work. Restart when required, repeat the same demanding route, and judge warm passes—not the first minutes of a new run.

## Rollback

The installer stores timestamped backups under:

```text
<GTA IV folder>\GTAIV_After_Dark_Configuration_Backups\YYYY-MM-DD_HH-MM-SS\
```

Use Windows **Installed apps** or `GTAIV_After_Dark_Configuration_Uninstall.exe` to restore the latest core configuration and the exact prior `GTAIV.exe` compatibility value. The embedded reference package is intentionally retained for documentation, credits, auditability, and later recovery.

## Credits and sources

This is an unofficial community configuration project. Credit remains with Rockstar Games; ThirteenAG and FusionFix contributors; DXVK contributors; GPLAsync contributors; Gillian’s GTA IV Modding Guide; LibertyCityPlates; Project2DFX; Xbox Rain Droplets; AudioMap; Console Select Menu; and every other original author. Consult [`docs/PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md) for the full retained attribution and historical record.
