# GTA IV, After Dark — Version 3.0.3 Core Configuration

## A reversible FusionFix profile for a modern Ryzen APU reference setup

> **Version 3.0.3 is a configuration package, not a complete GTA IV modpack or a universal performance fix.** It is organized around a Ryzen 5 PRO 6650U, Radeon 660M integrated graphics, and 16 GB of dual-channel memory, but it does not promise the same behavior on another laptop, driver, renderer build, or mod list.

The package supplies a narrow FusionFix/streaming configuration layer, optional renderer profiles, documentation, and an auditable installer. It does **not** redistribute GTA IV, launcher files, saves, FusionFix or DXVK binaries, shader files, plugin binaries, texture assets, vehicle packs, traffic files, or any other third-party mod content.

## Start here

| Read this | When it matters |
|---|---|
| [`docs/COMPONENTS_AND_COMPATIBILITY.md`](docs/COMPONENTS_AND_COMPATIBILITY.md) | Before choosing the default installer, stock DXVK configuration, GPLAsync overlay, or an optional plugin setting |
| [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md) | Before calling a configuration “tested,” comparing performance, or reporting an issue |
| [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md) | Before downloading FusionFix or selecting the exact GPLAsync overlay |
| [`installer/BUILDING.md`](installer/BUILDING.md) | Before rebuilding or Windows-testing the installer |
| [`RELEASE_NOTES_v3.0.3.md`](RELEASE_NOTES_v3.0.3.md) | To understand the corrective release scope and upgrade boundary |

## What Version 3.0.3 actually changes

The default installer is deliberately small. It first confirms a GTA IV game root and the installed FusionFix plugin, refuses to proceed while `GTAIV.exe` is running, backs up pre-existing managed files, and writes only the following three files:

| Repository file | Game destination | Scope |
|---|---|---|
| `tuned/stream.ini` | `pc\stream.ini` | Core streaming configuration |
| `tuned/GTAIV.EFLC.FusionFix.cfg` | `plugins\GTAIV.EFLC.FusionFix.cfg` | Main FusionFix graphics, API, and limiter preferences |
| `tuned/GTAIV.EFLC.FusionFix.ini` | `plugins\GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix shadows, limiter, vehicle budget, and Project2DFX settings |

It does **not** automatically install a DXVK configuration, a shader preload list, AudioMap, Console Select Menu, Xbox Rain Droplets, LibertyCityPlates configuration, third-party DLLs, an ASI loader, or a Windows compatibility flag. Those are separate dependency decisions, not safe defaults.

## The reference profile

The default FusionFix files target a restrained 60 FPS-oriented visual balance. They use the FusionFix Vulkan route only when a compatible renderer is already installed, select a 60 FPS FusionFix preset and accurate limiter, use SMAA, keep high-resolution shadows, AO, volumetric fog, sun shafts, and reflection MSAA disabled, use Soft shadows with reduced extra dynamic shadows, and reduce rain density in the optional rain-plugin file. These are **reference choices**, not a benchmark guarantee.

The profile makes no claim that a first route through Liberty City represents final frame pacing. A new driver, renderer, resolution, graphics setting, weather path, shader, texture, or major mod can create cold-cache work. Restart GTA IV after changes that require it, repeat a fixed demanding route, and compare warm passes under the [validation protocol](docs/VALIDATION_PROTOCOL.md).

## Renderer choices

### Stock DXVK baseline

`tuned/dxvk-stock.conf` contains only upstream-compatible DXVK keys. Use it only with an already working FusionFix Vulkan path and a compatible DXVK build. Back up the active `dxvk.conf`, copy this file into the GTA IV root as `dxvk.conf`, restart GTA IV, and test a cold pass followed by warm passes. It leaves DXVK’s frame-rate cap off so FusionFix remains the only intended limiter.

### Optional GPLAsync v2.6.2-1 overlay

`tuned/renderers/dxvk-gplasync-v2.6.2-1.conf` is **not** a generic DXVK update profile. It is tied to the exact GPLAsync v2.6.2-1 artifact named and SHA-256-locked at the top of that file. It is relevant only when that renderer is deliberately installed through the FusionFix Vulkan route using the game’s 32-bit DLL path. If the renderer log reports unsupported keys or startup becomes unstable, restore `dxvk-stock.conf` before altering any other setting.

> Keep **one renderer route and one frame limiter**. Do not layer an extra DXVK wrapper, an ENB/ReShade proxy, a second ASI loader, Radeon Chill, RTSS, or another cap while diagnosing the base profile.

## Optional plugin settings

The following files remain available for manual, dependency-aware use, but the installer never writes them by default:

| Optional file | Requirement |
|---|---|
| `tuned/components/AudioMap.ini` | Matching Audio Mapper plugin and every referenced audio file |
| `tuned/components/ConsoleSelectMenuIV.ini` | Console Select Menu IV plugin |
| `tuned/components/GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets plugin |
| `tuned/components/LibertyCityPlates.txt` | LibertyCityPlates plus its required shader/content installation |
| `tuned/legacy/preload.list` | A verified legacy/static shader layout only; not a performance shortcut or a general load order |

Current FusionFix guidance treats shader preload metadata as a correctness issue when a setup relies on it, while its current shader handling can generate the normal list from installed shader content. Do not overwrite a user’s `preload.list` unless every named shader and its location have been verified.[^1] [^2]

## Safe installation order

1. Begin with a legitimate GTA IV Complete Edition installation that launches without this package.
2. Verify the exact FusionFix v5.0.1 archive from [`docs/DEPENDENCY_LOCKS.md`](docs/DEPENDENCY_LOCKS.md), install it, and launch the game once. The core installer requires `plugins\GTAIV.EFLC.FusionFix.asi`.
3. Install the renderer only when you intend to use FusionFix’s Vulkan route. Keep the renderer version and artifact hash in the test record.
4. Close GTA IV, the Rockstar Games Launcher, mod managers, and tools that may lock the managed files.
5. Run the Version 3.0.3 installer or manually copy only the three core files above. Preserve the installer backup.
6. Choose a renderer profile manually, if needed. Do not mix the stock DXVK baseline and GPLAsync-specific overlay.
7. Add optional plugin settings only after their dependencies and shader/content layout are known-good.
8. Validate GTA IV, TLAD, and TBoGT separately where installed. Record cold and warm results before changing another quality layer.

## Rollback

The Version 3.0.3 installer stores backups under:

```text
<GTA IV folder>\GTAIV_Core_Configuration_Installer_Backups\YYYY-MM-DD_HH-MM-SS\
```

Use Windows **Installed apps** or `GTAIV_Core_Configuration_Installer_Uninstall.exe`, then choose **Yes** to restore the latest core-configuration backup. The uninstaller restores only the files Version 3.0.3 managed. It does not alter renderer binaries, optional plugin files, game executables, shader layouts, or earlier installer backups.

If an older project installer is installed too, make a complete game-folder backup and remove installers in reverse installation order. Do not delete saves or game archives as a first troubleshooting step.

## Release integrity

Each corrective release is generated from an immutable Git tag. The source archive carries `SOURCE_COMMIT.txt`; `SHA256SUMS.txt` covers every published asset, including the installer executable. The reproducibility contract and Windows toolchain are recorded in [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md).

The project previously shipped release assets whose source state was not represented by the public Version 3.0.0 tag. Version 3.0.3 corrects that process without moving historical tags or rewriting past releases.

## Credits and boundaries

This is an unofficial community configuration project. FusionFix belongs to ThirteenAG and its contributors; DXVK belongs to doitsujin and its contributors; GPLAsync belongs to its contributors; Gillian’s guide, LibertyCityPlates, Xbox Rain Droplets, AudioMap, Console Select Menu, and all other optional content belong to their original authors. Respect each project’s license, distribution terms, and current installation guidance.

## References

[^1]: [FusionFix issue #1431 — preload-list behavior](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)
[^2]: [FusionFix issue #1347 — invalid-resource and LibertyCityPlates context](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)
[^3]: [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
[^4]: [DXVK official configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)
[^5]: [DXVK GPLAsync v2.6.2-1 release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v2.6.2-1)
