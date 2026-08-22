# Version 3 — Complete Cumulative Configuration

> **Version 3 is the complete recommended GTA IV, After Dark configuration package.** It is not a delta installer, a patch file, or a replacement for the history that came before it.

## What Version 3 contains

The Version 3 installer carries the complete current package: the core FusionFix and streaming profile; stock DXVK and GPLAsync renderer profiles; AudioMap, Console Select Menu, Xbox Rain Droplets, and LibertyCityPlates settings; the legacy preload reference; dependency locks; validation protocol; build instructions; source links; credits; and the complete Version 1 through Version 3 decision history.

The installer safely applies the core configuration by default and stores the entire reference package at `GTAIV_After_Dark_Complete_Package` inside the game folder. This makes the release self-contained without blindly overwriting settings whose correct use depends on optional third-party plugins, exact renderer builds, or a verified shader layout.

| Applied automatically | Included for intentional manual use |
|---|---|
| `pc\stream.ini` | `dxvk-stock.conf` |
| `plugins\GTAIV.EFLC.FusionFix.cfg` | `dxvk-gplasync-v2.6.2-1.conf` |
| `plugins\GTAIV.EFLC.FusionFix.ini` | AudioMap, Console Select Menu, Xbox Rain Droplets, and LibertyCityPlates settings |
| Per-user `GTAIV.exe` `RUNASADMIN` compatibility setting | Legacy `preload.list` reference and every guide, source, credit, and historical record |

## Required administrator compatibility

Version 3 sets **Run this program as an administrator** for `GTAIV.exe` because the documented installation layout otherwise prevents GTA IV from creating its required configuration files. The installer records the prior per-user compatibility value before changing it. On uninstall, it restores that exact prior value, or removes the entry if the installer created it from an empty state.

## Before installation

Use a legitimate GTA IV Complete Edition installation. Install and launch a compatible FusionFix/Vulkan route first, close GTA IV and related tools, then select the actual folder containing `GTAIV.exe`. The installer verifies the game root, FusionFix plugin, Vulkan route, and running-game state before applying the core configuration.

Version 3 does not redistribute GTA IV, Rockstar launcher files, saves, FusionFix, DXVK, GPLAsync, shader files, or third-party mod assets. Verify third-party prerequisites through [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md) before choosing an optional renderer or component file.

## Why the package is structured this way

A complete package is not the same as an unsafe universal overwrite. The retained optional files are part of the project’s full configuration knowledge, but their dependencies vary. Version 3 includes them, documents them, and preserves them in the embedded reference bundle; it applies only the configuration that the installer can validate safely.

Cache warm-up remains normal on a shared-memory APU. Test one change at a time, restart when needed, repeat the same demanding route, and compare warm passes under [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md). Do not call an unrecorded first-run impression a universal performance result.

## History and credits

The reasoning from every prior iteration is retained in [`docs/PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md) and [`docs/history/`](docs/history/). The project remains an unofficial community configuration package. Credit remains with Rockstar Games; ThirteenAG and FusionFix contributors; DXVK contributors; GPLAsync contributors; Gillian’s GTA IV Modding Guide; LibertyCityPlates; Project2DFX; Xbox Rain Droplets; AudioMap; Console Select Menu; and all other original authors.
