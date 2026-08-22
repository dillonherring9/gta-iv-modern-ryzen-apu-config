# Version 3 Components and Compatibility

Version 3 is a configuration package for a legitimate **GTA IV Complete Edition** installation. It does not include GTA IV, Rockstar launcher files, saves, FusionFix/DXVK binaries, shader files, or third-party mod assets. The package is intentionally divided into a small default profile and optional component settings.

## Default configuration scope

| File | Default installation status | Purpose |
|---|---|---|
| `GTAIV.EFLC.FusionFix.cfg` | Installed after FusionFix prerequisite check | FusionFix visible graphics, API, and frame-limit preferences |
| `GTAIV.EFLC.FusionFix.ini` | Installed after FusionFix prerequisite check | Advanced FusionFix shadow, limiter, vehicle-budget, and Project2DFX settings |
| `stream.ini` | Installed after game-root and FusionFix prerequisite checks | Streaming configuration supplied with the reference package |
| Per-user `GTAIV.exe` `RUNASADMIN` entry | Installed and managed on every package installation | Required so GTA IV can create its configuration files in the documented installation layout; the prior value is preserved for uninstall rollback |
| `dxvk-stock.conf` | Manual selection only | Upstream-compatible DXVK baseline; copy as `dxvk.conf` only when the FusionFix Vulkan route and a compatible DXVK build are active |

## Optional configuration scope

| Component file | Required installed component | Default behavior |
|---|---|---|
| `AudioMap.ini` | The matching DualSense Audio Mapper plugin and its referenced audio files | Not installed unless selected and the plugin exists |
| `ConsoleSelectMenuIV.ini` | Console Select Menu IV plugin | Not installed unless selected and the plugin exists |
| `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets plugin | Not installed unless selected and the plugin exists |
| `LibertyCityPlates.txt` | LibertyCityPlates plugin plus its required shader/content installation | Not installed unless selected and the plugin exists |
| `preload.list` | A legacy static shader setup whose exact shader files have been verified | Never installed by default; retain only as a legacy reference |
| `dxvk-gplasync-v2.6.2-1.conf` | Exact GPLAsync v2.6.2-1 artifact named and hashed in the file | Manual selection only; never installed automatically |

## Renderer boundary

Verify the relevant FusionFix and GPLAsync archives against [`DEPENDENCY_LOCKS.md`](DEPENDENCY_LOCKS.md) before using this package. The installer can detect expected filenames, but it cannot prove third-party archive hashes at runtime.

The stock profile has only upstream-compatible DXVK keys. The GPLAsync profile carries fork-specific async keys and is valid only with the exact renderer artifact described at the top of that file. Do not use a renderer update as a casual optimization change. Record the old artifact, replace one component, restart GTA IV, and collect a cold/warm comparison under the [validation protocol](VALIDATION_PROTOCOL.md).

FusionFix’s current upstream project fully supports Complete Edition; legacy executable builds require its separately maintained Legacy Addon.[^1] The project’s current preload handling also makes manually overwriting a static list unnecessary for the normal 5.0.1 route, while maintainers still require preload metadata to be valid when a legacy/static setup relies on it.[^2] [^3]

## Installer safety boundary

The installer verifies the game root and the FusionFix plugin before it writes default configuration. It does not install or repair third-party binaries, confirm a game build from file metadata, override shader metadata, or prove performance on the target system. It **does** set the per-user `GTAIV.exe` `RUNASADMIN` compatibility entry on every installation because the documented layout otherwise prevents GTA IV from creating required configuration files. The installer records the previous value and restores it on uninstall. It is a reversible configuration writer, not a complete modpack installer.

## References

[^1]: [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
[^2]: [FusionFix issue #1431 — preload-list behavior](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)
[^3]: [FusionFix issue #1347 — invalid resource and LibertyCityPlates context](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)
