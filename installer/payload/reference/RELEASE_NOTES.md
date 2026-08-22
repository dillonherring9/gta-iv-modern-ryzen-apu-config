# Version 3 — Complete Cumulative Configuration

> **Version 3 is the complete, beginner-ready configuration installer for GTA IV Complete Edition after Gillian’s Drag-and-Drop Archive is installed.** It is the current recommended release. It is not a small update, a delta installer, or a redistribution of GTA IV or third-party mod binaries.

GTA IV has a reputation for being difficult to run on modern PCs. A documented current route now exists: FusionFix supports GTA IV Complete Edition and its modern-system features, while Gillian’s complete Drag-and-Drop Archive provides a curated, ready-to-play Windows Complete Edition layout.[1] [2] Version 3 supplies the audited configuration, backup, rollback, credits, sources, history, and plain-language explanation on top of that layout.

## Read this before downloading

This package is suitable for people who have a legitimate **Windows GTA IV Complete Edition (1.2.0.59)** installation and want a structured way to try the curated archive on a modern computer, including recent Ryzen APU/integrated Radeon systems. Its reference context is a Ryzen 5 PRO 6650U with Radeon 660M graphics and 16 GB dual-channel memory. That is a useful test context, **not** a promise of a fixed frame rate or universal performance result.

The full archive itself prioritizes a vanilla-faithful experience and community fixes rather than maximum possible performance; hardware, power limits, cooling, memory configuration, resolution, drivers, and extra content still matter.[2] The release therefore provides a stable path and an undo path, rather than a misleading “works for everyone” claim.

## The beginner route

You do not need prior GTA IV modding knowledge. The complete click-by-click guide is [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md). In short, install GTA IV Complete Edition cleanly; extract Gillian’s **Complete Edition** archive into the folder containing `GTAIV.exe`; run `GTAIVSetupUtility.exe` and use **Reinstall DXVK** plus **Setup launch options**; paste those launch options into your normal launcher; launch once and use the archive’s Vulkan option when it works; close the game; then run this Version 3 installer against the same `GTAIV.exe` folder.[2]

If Vulkan crashes immediately after it is selected, Gillian’s documented recovery step is to close the game, delete `d3d9.cfg`, and revisit the archive setup/driver path. Do not layer unrelated DLL wrappers, loaders, frame limiters, or old “fix packs” over the archive in response.[2]

## What Version 3 installs

Before it changes anything, the installer creates a timestamped backup of every managed file and stores the existing per-user Windows compatibility value for `GTAIV.exe`.

| Installed location | Version 3 file | Purpose |
|---|---|---|
| Game root | `dxvk.conf` | Upstream-compatible DXVK baseline for the archive’s Vulkan route. |
| `pc\` | `stream.ini` | Streaming configuration profile. |
| `plugins\` | `ConsoleSelectMenuIV.ini` | Console Select Menu setting for the Complete Edition archive route. |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` | Main FusionFix profile. |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix profile. |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets setting. |
| `plugins\` | `LibertyCityPlates.txt` | LibertyCityPlates setting. |

The installer verifies the selected GTA IV game root, requires the archive’s `vulkan.dll`, and refuses to run while `GTAIV.exe` is active. `vulkan.dll` confirms that the archive’s Vulkan route exists; it does not identify whether Gillian’s hardware-aware Setup Utility selected stock DXVK, GPLAsync, or another compatible renderer. Read [`docs/ARCHIVE_RENDERER_IDENTIFICATION.md`](docs/ARCHIVE_RENDERER_IDENTIFICATION.md) before reinstalling or replacing a working renderer. The installer deliberately does **not** reject an otherwise valid archive solely because `plugins\GTAIV.EFLC.FusionFix.asi` is absent: loader and plugin-file layouts can differ.

The installer also sets **Run this program as an administrator** for `GTAIV.exe`. This is required for the documented target layout because the game otherwise cannot create required configuration files. On uninstall, Version 3 offers to restore the latest backup of all seven files and restores the exact prior compatibility value.

## Complete package, retained knowledge, and boundaries

The executable copies a complete reference package into `GTAIV_After_Dark_Complete_Package` in the game folder. It retains all sources, credits, history, validation material, component documentation, renderer-alternative information, and legacy shader-preload reasoning. The public release likewise includes the complete installer, complete package archive, source archive, and SHA-256 checksum manifest.

The GPLAsync alternative and legacy `preload.list` remain preserved as reference material. They are not installed automatically because the renderer choice and static shader metadata must match the exact installed archive/shader layout; blindly overwriting them can introduce instability or LibertyCityPlates resource failures. The retained GPLAsync 2.6.2-1 reference is not a claim that every archive installation uses GPLAsync.

`AudioMap.ini` is intentionally absent from Version 3’s active documentation, payload, and installer. The full audit identified it as a historical DualSense Audio Mapper carryover rather than a documented Drag-and-Drop Archive component. Its historical mention is retained in the archived records for transparency, but the configuration itself is not reintroduced.

## Support boundary and safe first actions

| Situation | Correct first action |
|---|---|
| You cannot find `GTAIV.exe` | Select the actual game folder, not `plugins`, `pc`, Documents, or the archive download location. |
| The installer cannot find `vulkan.dll` | Revisit the archive extraction and setup utility. Do not download random standalone DLL files. |
| The installer says the game is running | Close GTA IV, Steam/Rockstar Launcher, mod managers, and any tool holding the folder open. |
| You see first-run stutter | Restart and repeat the same route before judging the setup; caches can build after renderer/driver/settings/content changes. |
| Dense traffic is the only problem | Test a lower in-game Traffic Density before changing multiple graphics options. |
| You want to undo Version 3 | Use the uninstaller and choose **Yes** to restore the latest backup. Do not delete saves or the whole game folder first. |

This installer does not support downgraded/legacy copies, Linux/Proton, multiplayer, arbitrary old mod combinations, or post-install “update everything” experimentation. It does not provide the game, bypass launchers/DRM, or guarantee a benchmark number. It provides a tidy, auditable Windows Complete Edition configuration path.

## Community continuity and handoff

This project is prepared for responsible maintenance by someone other than its current owner; it does not depend on a promise of indefinite personal support. A potential maintainer should read [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md), follow [`CONTRIBUTING.md`](CONTRIBUTING.md), preserve the complete package/rollback/evidence model, and replace the existing `v3` assets in place only after a reproducible validation.

Users are free to choose a better maintained successor, archive, or community route when one becomes available. They should first back up the game folder, restore/uninstall Version 3 if it manages the same files, and follow one complete configuration ecosystem at a time rather than mixing two competing setups.

## References

[1] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[3] [Gillian’s GTA IV Modding Guide — FusionFix](https://gillian-guide.github.io/essential-modding/fusionfix/)
