# GTA IV, After Dark — Version 3

## Complete configuration for Gillian’s Drag-and-Drop Archive

> **Version 3 is one complete, cumulative configuration installer for GTA IV Complete Edition with Gillian’s Drag-and-Drop Archive already extracted into the game folder.** It retains the project’s configurations, sources, credits, historical reasoning, integrity records, backup path, and rollback behavior in one package.

This repository does not redistribute GTA IV, Gillian’s archive, FusionFix, DXVK binaries, third-party plugins, shaders, or content. It supplies the configuration layer for a pre-existing archive installation.

## Installation

1. Start with a legitimate GTA IV Complete Edition installation and extract the complete Drag-and-Drop Archive into the folder containing `GTAIV.exe`.[^1]
2. Use the archive’s intended Vulkan route and close GTA IV, the launcher, mod managers, and other tools that may hold files open.
3. Run the Version 3 installer and select the actual folder containing `GTAIV.exe`.
4. The installer verifies the game root, `vulkan.dll`, and closed game process; makes a timestamped backup; installs all seven managed configuration files; and sets **Run this program as an administrator** for `GTAIV.exe`.
5. Launch the game through its normal launcher path, set Graphics API to Vulkan where required by the archive, and test one changed variable at a time.

The installer deliberately does not block on a single expected FusionFix ASI filename. FusionFix remains part of the archive requirement, but valid archive loaders and layouts can differ.

## Managed configuration files

| Target path | Installed file |
|---|---|
| `dxvk.conf` | Stock DXVK baseline |
| `pc\stream.ini` | Streaming profile |
| `plugins\ConsoleSelectMenuIV.ini` | Console Select Menu setting |
| `plugins\GTAIV.EFLC.FusionFix.cfg` | Main FusionFix profile |
| `plugins\GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix profile |
| `plugins\GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets setting |
| `plugins\LibertyCityPlates.txt` | LibertyCityPlates setting |

Before installing, the executable backs up all seven targets. On uninstall, it restores or removes those same targets according to the latest backup and restores the exact prior `GTAIV.exe` compatibility value. The reference package remains in the game directory for auditability and recovery.

## Included reference material

`GTAIV_After_Dark_Complete_Package` is copied beside the game. It retains the complete source and credit context, dependency locks, validation protocol, full project history, GPLAsync renderer alternative, and legacy preload reference. These retained items are not forgotten options; they are intentionally not overwritten because they require a distinct renderer artifact or exact shader layout.

`AudioMap.ini` is not part of Version 3. The audit identified it as an older DualSense Audio Mapper carryover rather than a documented Drag-and-Drop Archive component. Its historical presence remains recorded under [`docs/history/`](docs/history/), but it is not installed or included in the active payload.

## Renderer and preload boundary

The installer writes the upstream-compatible stock `dxvk.conf`. The GPLAsync configuration remains included as a version-locked alternative only; do not combine it with stock DXVK or substitute it for a different renderer artifact. The legacy preload list also remains reference-only, because current FusionFix shader metadata must match the actual installed shader layout, particularly with LibertyCityPlates.

## Cache and validation rule

On a shared-memory APU, shader and pipeline caches must build. A fresh route, new weather, new effect, renderer change, driver update, resolution change, or major content change can create temporary stutter. Restart when required, repeat the same demanding route, and judge warm passes through [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md), not a first-run impression.

## Documentation

| Document | Purpose |
|---|---|
| [`RELEASE_NOTES.md`](RELEASE_NOTES.md) | Complete Version 3 installation and rollback contract |
| [`docs/COMPONENTS_AND_COMPATIBILITY.md`](docs/COMPONENTS_AND_COMPATIBILITY.md) | Archive component, renderer, and shader boundaries |
| [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md) | Renderer and FusionFix evidence boundaries |
| [`docs/PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md) | Retained Version 1 through Version 3 reasoning and credits |
| [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md) | Evidence requirements for performance claims |
| [`installer/BUILDING.md`](installer/BUILDING.md) | Complete installer payload and release verification |

[^1]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
