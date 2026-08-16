# Getting GTA IV to Run Smooth and Look Next Gen on PC on Modern Ryzen 5 PRO APUs

## And Possibly Other Hardware with Similar Specifications

> **Community configuration guide — not an original mod**  
> This is a community-oriented configuration package for **GTA IV Complete Edition** on modern Ryzen 5 PRO integrated-graphics systems. I did not create FusionFix, DXVK, the Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Xbox Rain Droplets, Project2DFX, restored vegetation, vehicle packs, traffic/popcycle files, or any other third-party project referenced here. Please preserve the original authors, licenses, and download information for every mod used.

This guide describes a balanced way to make GTA IV look substantially more modern while improving frame pacing on hardware similar to an **AMD Ryzen 5 PRO 6650U with Radeon 660M graphics and 16 GB of dual-channel memory**. It is also suitable as a starting point for other recent AMD APUs with comparable integrated graphics and shared-memory bandwidth.

The objective is not to promise a constant 60 FPS in every situation. GTA IV can remain CPU-limited by traffic, world streaming, vehicle modifications, scripts, and weather even when the GPU has headroom. The objective is a stable and repeatable baseline: modern visual fixes, sensible shader loading, fewer expensive frame-time spikes, and a reversible installation.

## Version 2 Tested Update

Version 2 is the **same complete Version 1 package**, rebuilt with only the three matching configuration files replaced: `tuned/dxvk.conf`, `tuned/GTAIV.EFLC.FusionFix.cfg`, and `tuned/GTAIV.EFLC.FusionFix.ini`. Every other file, backup, companion configuration, validation record, and document remains in place.

The Version 2 trio was supplied and tested together by the package publisher on the reference Ryzen 5 PRO 6650U / Radeon 660M system. The publisher reports smooth high-setting, high-resolution play without the earlier repeated skipping. This is a test result on the reference setup—not a performance guarantee on every driver, resolution, thermal profile, or mod stack.

---

## Table of Contents

- [Version 2 Tested Update](#version-2-tested-update)
- [Target Hardware](#target-hardware)
- [What This Package Does](#what-this-package-does)
- [Package Contents](#package-contents)
- [Recommended Installation Order](#recommended-installation-order)
- [Graphics and Performance Profile](#graphics-and-performance-profile)
- [DXVK Configuration](#dxvk-configuration)
- [Shader Preloading](#shader-preloading)
- [Testing the Configuration](#testing-the-configuration)
- [Troubleshooting](#troubleshooting)
- [Rollback](#rollback)
- [Credits and Attribution](#credits-and-attribution)
- [Disclaimer](#disclaimer)
- [References](#references)

---

## Target Hardware

The reference system for this configuration is an **AMD Ryzen 5 PRO 6650U** with integrated **Radeon 660M** graphics and **16 GB of dual-channel system memory**.

The Radeon 660M is an RDNA 2 integrated GPU. Unlike a discrete graphics card with a large dedicated VRAM pool, it shares system memory with the CPU. Dual-channel memory improves the available bandwidth, but GTA IV still benefits from controlling shader compilation, shadows, rain particles, full-screen post-processing, and traffic-related CPU load.

| Component | Reference system | Configuration implication |
|---|---|---|
| Processor | Ryzen 5 PRO 6650U | World streaming, traffic, scripts, and vehicle packs may remain CPU-limited. |
| Graphics | Radeon 660M integrated GPU | Shadow filters, rain particles, shader compilation, and post-processing need careful balancing. |
| Memory | 16 GB dual-channel | A strong configuration for an integrated GPU, but memory is shared with Windows and the game. |
| Renderer | FusionFix with DXVK/Vulkan when stable | Use one renderer path and avoid stacking wrappers. |
| Frame target | 60 FPS where the scene allows it | Evaluate frame-time consistency, not only the highest FPS number. |

Results vary with display resolution, refresh rate, cooling, power limits, AMD driver version, Windows configuration, and the number of installed visual and vehicle mods.

---

## What This Package Does

Version 2 keeps the Version 1 package intact but replaces the three matching configuration files with a **tested high-visual DXVK and FusionFix profile**. It retains FusionFix as the single intended gameplay limiter while restoring the visual settings that the publisher was able to use smoothly on the reference system.

| Area | Configuration choice | Reason |
|---|---|---|
| Graphics fixes | FusionFix | Provides modern fixes and additional graphics controls for the supported Complete Edition build. |
| Renderer | DXVK/Vulkan when stable | Translates Direct3D 9 rendering to Vulkan through the active DXVK wrapper. |
| Frame pacing | One FusionFix 60 FPS limiter | Avoids conflicts between FusionFix, DXVK, Radeon Chill, RTSS, and other limiters. |
| Anti-aliasing | SMAA | Provides useful edge smoothing without relying on heavier multi-sample options. |
| Shadows | Shadow Filter 5 with `ExtraDynamicShadows=1` retained | Restores the higher-quality shadow filter while keeping the previously compatible one-level dynamic-shadow setting. |
| Post-processing | Sun shafts, ambient occlusion, and volumetric fog enabled | Restores the high-visual FusionFix presentation tested by the publisher. |
| Rain | Reduced static and moving droplet counts | Prevents weather scenes from combining too many particle and lighting costs. |
| Shader loading | Complete preload list | Helps prevent missing-resource errors when the matching shader files are installed. |
| Shared-memory behavior | Tested DXVK memory, pipeline, frame-latency, fullscreen-exclusive, and async settings | Keeps the publisher-tested renderer profile while retaining an integrated-APU-aware memory hint. |

---

## Package Contents

| Path | Purpose |
|---|---|
| `tuned/GTAIV.EFLC.FusionFix.cfg` | Main FusionFix graphics and frame-pacing settings. |
| `tuned/GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix shadow, streaming, and limiter settings. |
| `tuned/dxvk.conf` | DXVK translation-layer settings for shader compilation, latency, memory reporting, and presentation. |
| `tuned/GTAIV.XboxRainDroplets.ini` | Rain particle-density settings. |
| `tuned/preload.list` | Shader preload list containing the base entries and the required mod shader names. |
| `tuned/AudioMap.ini` | Supplied custom audio mapping. |
| `tuned/ConsoleSelectMenuIV.ini` | Supplied console-style menu configuration. |
| `tuned/LibertyCityPlates.txt` | Supplied license-plate and vehicle-effect configuration. |
| `originals/` | Backups of the supplied configuration files. |
| `docs/` | Hardware, configuration, and preload-list documentation. |
| `validation_report.txt` | File-difference, duplicate-entry, and integrity checks. |
| `PACKAGE_SHA256SUMS.txt` | Package checksums for maintainers. |

The package does not include GTA IV game files, Rockstar files, proprietary binaries, user saves, or launcher/account data.

---

## Recommended Installation Order

### 1. Begin with a legitimate working installation

Use **GTA IV Complete Edition** and confirm that the unmodified game launches. FusionFix’s guide identifies the Complete Edition as build **1.2.0.59**.[^1]

### 2. Install the foundation fixes

Install the current official FusionFix release and the dependencies required by the Drag-and-Drop Archive. Install Fusion Overloader if the archive or the chosen mod documentation requires it. Follow the current instructions from each project rather than mixing DLLs from unrelated guides.

### 3. Install the archive and optional enhancements

Install the Drag-and-Drop Archive and compatible visual, traffic, vehicle, vegetation, Project2DFX, and LibertyCityPlates components. it's an all in one package. https://gillian-guide.github.io/drag-and-drop-archive/ follow the guides instructions for it. Test the game after the foundation layer and again after groups of optional mods.

### 4. Create a backup

Close GTA IV and its launcher completely. Back up the game folder or at minimum the current `plugins`, `update`, shader, and configuration files. The package includes an `originals/` folder, but a second backup inside the live game folder is recommended.

### 5. Copy the tuned configuration files

Copy each file from `tuned/` over the corresponding file in the same location where the existing installation already keeps that file. Do not invent a new path. Depending on the setup utility and FusionFix build, configuration files may be beside the plugin or in the game root.

### 6. Keep one loader and one renderer path

Do not add a second ASI loader, `d3d9.dll`, `dxgi.dll`, DXVK wrapper, shader proxy, or arbitrary `update`-folder priority layer. FusionFix and the intended Fusion Overloader structure should remain the active loader and file-precedence path.

---

## Graphics and Performance Profile

The following values describe the Version 2 high-visual profile. These three configuration files are intended to be used as one matched set:

| File | Original value | Profile value | Purpose |
|---|---:|---:|---|
| `GTAIV.EFLC.FusionFix.cfg` | `SunShafts=1` | `SunShafts=1` | Keeps the high-visual sun-shaft effect enabled. |
| `GTAIV.EFLC.FusionFix.cfg` | `AmbientOcclusion=1` | `AmbientOcclusion=1` | Keeps ambient occlusion enabled. |
| `GTAIV.EFLC.FusionFix.cfg` | `ShadowFilter=5` | `ShadowFilter=5` | Keeps the higher-quality shadow filter enabled. |
| `GTAIV.EFLC.FusionFix.cfg` | `VolumetricFog=1` | `VolumetricFog=1` | Keeps volumetric fog enabled. |
| `GTAIV.EFLC.FusionFix.cfg` | `DepthOfField=7` | `DepthOfField=10` | Uses the supplied Version 2 depth-of-field value. |
| `GTAIV.EFLC.FusionFix.cfg` | `MotionBlur=1` | `MotionBlur=4` | Uses the supplied Version 2 motion-blur value. |
| `GTAIV.EFLC.FusionFix.ini` | `ExtraDynamicShadows=2` | `ExtraDynamicShadows=1` | Retains vegetation shadows while reducing heavier secondary shadow work. |
| `GTAIV.EFLC.FusionFix.ini` | `FpsLimit=-2` | `FpsLimit=60` | Makes the custom-mode fallback explicit. |
| `GTAIV.XboxRainDroplets.ini` | `MaxDrops=1500` | `MaxDrops=900` | Reduces static rain-particle work. |
| `GTAIV.XboxRainDroplets.ini` | `MaxMovingDrops=3000` | `MaxMovingDrops=1800` | Reduces moving rain-particle work. |

The Version 2 trio keeps FusionFix’s 60 FPS preset, accurate limiter mode, SMAA, PC+ tree settings, tone mapping, bloom, distant lights, console vehicle reflections, extended limits, the supplied vehicle budget, and the Version 1 rain settings. It restores the listed high-visual effects because they were reported as stable in the reference-system test.

> **Do not stack frame limiters.** During initial testing, disable Radeon Chill, RTSS, external DXVK caps, and other per-game limiters. A single limiter is easier to diagnose and usually produces more predictable frame pacing.

---

## DXVK Configuration

`dxvk.conf` controls the DXVK translation layer beneath FusionFix. DXVK translates GTA IV’s Direct3D 9 rendering calls into Vulkan, so the file can influence shader compilation, pipeline caching, frame latency, memory reporting, and presentation behavior. It matters only when the matching DXVK/Vulkan wrapper is active; it does not affect native Direct3D 9.

| Setting | Value | Role in this profile |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | Supports shader-cache behavior on GPLAsync/GPLALL-style forks. |
| `d3d9.maxAvailableMemory` | `4096` | Reports a 4 GiB memory-budget hint; it is not a hard VRAM reservation. |
| `d3d9.maxFrameLatency` | `1` | Requests a one-frame queue for lower latency. Test for stutter. |
| `dxvk.numCompilerThreads` | `4` | Uses four shader compiler threads as a conservative six-core mobile-CPU setting. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Lets DXVK and the Vulkan driver choose the compatible pipeline-library path. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Avoids forcing device-local constant-buffer placement on shared-memory graphics. |
| `d3d9.presentInterval` | `-1` | Does not override the game’s presentation interval. |
| `d3d9.maxFrameRate` | `0` | Disables DXVK’s own limiter so FusionFix remains the single 60 FPS limiter. |
| `dxvk.allowFse` | `true` | Enables the supplied fullscreen-exclusive behavior where the active DXVK build and display path support it. |
| `dxvk.enableAsync` | `true` | Enables the supplied async behavior on DXVK builds that support this build-specific setting. |
| `dxvk.numAsyncThreads` | `4` | Uses the supplied four-thread async setting on DXVK builds that support it. |

> **Compatibility warning:** `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads` are associated with GPLAsync/GPLALL-style or other async-capable DXVK variants rather than the current standard upstream option set. They are preserved exactly because the package publisher tested this route. Users of stock upstream DXVK should keep the supported settings and remove only these build-specific lines if their own DXVK log reports them as unknown. `dxvk.allowFse` can affect Alt+Tab or GDI-window behavior on some systems; set it to `false` first if that specific issue appears. The standard upstream DXVK options are documented in the official configuration reference.[^2]

If shader compilation causes first-run stutter, allow the cache to warm up before judging performance. If the CPU becomes saturated during compilation, test `dxvk.numCompilerThreads=2`. Do not add a second frame limiter through DXVK, Radeon Chill, RTSS, or another external tool until this single-limiter profile has been tested.

---

## Shader Preloading

`preload.list` is shader/resource preload metadata, not a general mod load order. The tuned list preserves the supplied base sequence and includes the following FusionFix/LibertyCityPlates-related shader names:

```text
gta_trees_extended.fx
gta_vehicle_licenseplate.fx
gta_vehicle_licenseplate_ext.fx
gta_vehicle_track2.fx
gta_vehicle_track.fx
```

These names are useful only when the matching shader files are installed in the correct GTA IV shader directory. Adding a filename to the list does not create the shader. FusionFix issue documentation connects missing entries for these shaders with invalid-resource crashes and incorrect LibertyCityPlates installations.[^3]

If a later FusionFix release dynamically registers the same shaders, follow the current upstream instructions and do not maintain duplicate or conflicting preload files.[^4]

---

## Testing the Configuration

Test one change group at a time. Start the game, load a save, enter and exit an interior, drive through dense daytime traffic, drive at night in rain, trigger a vehicle using custom audio, and reload once.

Check for missing or invisible vehicles, black or corrupted materials, missing license-plate effects, audio dropouts, lighting corruption after interior transitions, crashes during loading, and uneven frame pacing despite a nominal 60 FPS reading.

If dense traffic remains heavy, lower the in-game traffic-density slider from `100` to `80` before reducing texture quality. If necessary, test `70`. If rain remains the only problem, lower `MaxMovingDrops` from `1800` to `1400` before reducing `MaxDrops` further. If shadows remain the bottleneck, set `ExtraDynamicShadows=0` as a second-stage fallback.

---

## Troubleshooting

| Symptom | First action |
|---|---|
| Game fails to start | Verify the game build, FusionFix installation, and active renderer DLL chain. Do not add another wrapper. |
| Invalid resource or shader error | Restore `preload.list`, then verify every referenced shader file exists. |
| Startup crash with a GPLAsync build | Comment out `dxvk.gplAsyncCache=True` and test again. |
| Rain causes stutter | Lower `MaxMovingDrops` first. |
| Shadows cause large frame-time dips | Set `ExtraDynamicShadows=0`. |
| Vehicle audio breaks | Keep the vehicle budget unchanged and test the vehicle/audio pack combination. |
| Traffic scenes remain slow | Lower traffic density before reducing texture quality. |
| Vulkan is unstable | Use the supported FusionFix fallback path and record the driver/version combination. |

---

## Rollback

1. Close GTA IV and its launcher.
2. Restore the files from `originals/` to their original locations.
3. If the issue is shader-related, restore `preload.list` first.
4. If the issue continues, restore `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `dxvk.conf`, `GTAIV.XboxRainDroplets.ini`, and `preload.list`.
5. Do not delete saves or modify Rockstar game archives as a first troubleshooting step.

---

## Credits and Attribution

This is a community configuration guide. It is **not an original mod**, and the person publishing this README should not be credited as the creator of the projects below.

| Project or resource | Credit |
|---|---|
| **FusionFix** | ThirteenAG and FusionFix contributors. See the [official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix). |
| **DXVK** | doitsujin and DXVK contributors. See the [official repository](https://github.com/doitsujin/dxvk). |
| **DXVK GPLAsync** | Ph42oN and the GPLAsync contributors when that fork is used. See the [project page](https://gitlab.com/Ph42oN/dxvk-gplasync). |
| **Gillian’s GTA IV Modding Guide** | Gillian and the guide’s contributors. See the [official guide](https://gillian-guide.github.io/). |
| **Drag-and-Drop Archive** | The authors and maintainers credited by Gillian’s archive documentation. |
| **LibertyCityPlates** | The LibertyCityPlates authors and contributors. Use the mod’s own distribution page for authoritative credits and licensing. |
| **Fusion Overloader** | Its original authors and contributors. Follow its official documentation for current precedence rules. |
| **Project2DFX, restored vegetation, vehicle packs, traffic/popcycle files, and other enhancements** | Credit the respective authors on their original mod pages. Do not redistribute their files without permission. |
| **AMD Ryzen PRO and Radeon** | AMD owns the platform names, specifications, drivers, and related trademarks. |
| **GTA IV** | Rockstar Games and its licensors. This is an unofficial community configuration guide. |

Preserve every original `LICENSE`, `README`, author notice, and download-page reference when distributing a complete installation.

---

## Disclaimer

This configuration is provided as-is for testing on similar hardware. It is not affiliated with Rockstar Games, AMD, ThirteenAG, FusionFix, Gillian, DXVK, LibertyCityPlates, or any other mod author. Performance varies with game version, drivers, cooling, power limits, resolution, memory speed, and installed content.

Always keep a backup. A setting that improves one system may reduce stability on another. When reporting a problem, include the GTA IV build, FusionFix version, DXVK variant, Radeon driver version, resolution, memory configuration, installed visual and vehicle mods, and the exact symptom.

---

## References

[^1]: [Gillian’s FusionFix guide](https://gillian-guide.github.io/essential-modding/fusionfix/) — Complete Edition compatibility, installation, configuration, and build context.
[^2]: [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf) — upstream option names, supported values, and behavior.
[^3]: [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347) — LibertyCityPlates/FusionFix shader preload requirements and invalid-resource troubleshooting.
[^4]: [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431) — preload.list correctness and dynamic shader-registration behavior.
[^5]: [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) — primary project documentation and configuration definitions.
[^6]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) — archive installation and compatibility context.
[^7]: [Gillian’s mod-loading guide](https://gillian-guide.github.io/extras/modloading/) — Fusion Overloader precedence and update-folder guidance.
[^8]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync) — GPL/async cache behavior and fork-specific compatibility context.
