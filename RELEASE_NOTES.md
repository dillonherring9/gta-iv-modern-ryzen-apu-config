# Version 3 — Complete Drag-and-Drop Archive Configuration

> **Version 3 is one complete, cumulative configuration installer for a GTA IV Complete Edition installation that already has Gillian’s Drag-and-Drop Archive extracted into the game root.** It is not a patch, a partial update, or a modpack redistribution.

## What Version 3 installs

The installer applies the complete documented configuration set for that archive environment. Before replacing anything, it creates a timestamped backup of every managed file and stores the prior per-user `GTAIV.exe` compatibility value.

| Installed location | Version 3 file | Purpose |
|---|---|---|
| Game root | `dxvk.conf` | Upstream-compatible DXVK baseline; FusionFix remains the sole intended frame limiter. |
| `pc\` | `stream.ini` | Streaming allocation profile. |
| `plugins\` | `ConsoleSelectMenuIV.ini` | Console Select Menu setting for the Complete Edition archive route. |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` | Main FusionFix profile. |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix profile. |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets setting. |
| `plugins\` | `LibertyCityPlates.txt` | LibertyCityPlates setting, including the intended plate behavior. |

The installer sets **Run this program as an administrator** for `GTAIV.exe`, because this documented installation layout otherwise prevents GTA IV from creating its required configuration files. Its uninstaller restores the latest backup of all seven managed files and the exact prior compatibility value.

## Prerequisite boundary

Install the complete Drag-and-Drop Archive into the folder containing `GTAIV.exe` first, then use its intended Vulkan route. The installer verifies the game root, `vulkan.dll`, and a closed `GTAIV.exe` process. It deliberately does **not** reject the archive based on the absence of one exact FusionFix ASI filename, since a valid archive can use a different loader or file layout.

Version 3 does not redistribute GTA IV, the Drag-and-Drop Archive, FusionFix, DXVK binaries, shaders, or third-party content. It writes the configuration that belongs to that pre-existing archive setup.

## Retained but not overwritten

The executable and complete package archive preserve the GPLAsync renderer alternative, the legacy shader-preload reference, dependency locks, validation protocol, credits, sources, and all historical reasoning. The GPLAsync file is mutually exclusive with the stock DXVK baseline. The legacy `preload.list` remains a reference only: current FusionFix shader metadata depends on the actual installed shader layout, and blindly replacing it can cause resource errors with LibertyCityPlates.

`AudioMap.ini` is intentionally absent. The audit found it was a DualSense Audio Mapper carryover from an older installer, not a documented Drag-and-Drop Archive component. Historical release records retain that fact for auditability, but Version 3 neither installs nor bundles its active configuration.

## Complete package and evidence

The installer copies the full documentation, sources, credits, renderer alternative, preload reference, and historical records to `GTAIV_After_Dark_Complete_Package` in the game root. The public Version 3 release also includes a complete package archive, complete source archive, and SHA-256 manifest.

Cache warm-up remains normal on a shared-memory APU. Change one variable at a time, restart when required, and compare cold and repeat warm passes according to [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md). The project does not make universal performance claims.
