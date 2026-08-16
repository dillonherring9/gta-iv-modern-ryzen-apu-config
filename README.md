# Getting GTA IV to Run Smooth and Look Next Gen on Modern APUs

## A Tested Ryzen 5 PRO 6650U / Radeon 660M Configuration — and a Community Compatibility Project

> **An honest community configuration package, not an original-mod claim.**
> I put this together around the way I actually play GTA IV: a legitimate GTA IV Complete Edition install, a modern mod stack, a Ryzen 5 PRO 6650U with Radeon 660M integrated graphics, and **16 GB of dual-channel memory**. My goal was never to sell a miracle fix or promise the same numbers for every system. I wanted a cleaner, more consistent experience: modern visual fixes, better frame pacing, fewer ugly stalls, and a setup I could roll back when something did not feel right.

This repository is built from my tested configuration path and the work of the modding community. I did **not** create FusionFix, DXVK, Gillian’s Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Xbox Rain Droplets, Project2DFX, restored vegetation, vehicle packs, traffic/popcycle files, or any other third-party project discussed here. Their original authors deserve the credit. This package is my organized configuration, documentation, and installer layer for using those projects together responsibly.

---

## Table of Contents

- [My Reference Setup and Result](#my-reference-setup-and-result)
- [Help Me Map APU Compatibility](#help-me-map-apu-compatibility)
- [Intel iGPU Note: `forceIntelGraphics=true`](#intel-igpu-note-forceintelgraphicstrue)
- [What This Package Is](#what-this-package-is)
- [How I Put It Together](#how-i-put-it-together)
- [What You Need to Install Separately](#what-you-need-to-install-separately)
- [Package Contents](#package-contents)
- [Recommended Installation Path](#recommended-installation-path)
- [Automatic Installer](#automatic-installer)
- [Version 2 Graphics and Performance Profile](#version-2-graphics-and-performance-profile)
- [DXVK Configuration Notes](#dxvk-configuration-notes)
- [Shader Preloading](#shader-preloading)
- [EFLC and Loose-File Compatibility Note](#eflc-and-loose-file-compatibility-note)
- [Testing, Troubleshooting, and Rollback](#testing-troubleshooting-and-rollback)
- [Credits, Boundaries, and Sources](#credits-boundaries-and-sources)

---

## My Reference Setup and Result

I built and tested this Version 2 profile around the following system. It is the baseline for the claims in this README, not a requirement and not a guarantee that every APU will behave the same way.

| Component | My reference setup | Why it matters here |
|---|---|---|
| Processor | AMD Ryzen 5 PRO 6650U | GTA IV still leans heavily on CPU work for streaming, traffic, scripts, weather, and complex mod stacks. |
| Graphics | Integrated Radeon 660M, RDNA 2 | It uses shared system memory, so shadows, post-processing, rain, shader compilation, and display resolution need balance. |
| Memory | 16 GB dual-channel system memory | Dual-channel bandwidth matters to an integrated GPU, but Windows and the game still share the same memory pool. |
| Game | GTA IV Complete Edition, build 1.2.0.59 | This is the Complete Edition build context identified in Gillian’s FusionFix guide.[^1] |
| Renderer path | FusionFix with one active DXVK/Vulkan path when stable | Stacking wrappers and loaders makes crashes and stutter harder to diagnose. |
| Frame target | 60 FPS where the scene allows it | I judge this setup by frame-time consistency and actual playability, not only the highest FPS counter. |

On my reference setup, I tested the three Version 2 configuration files together and found that they made high settings and a higher resolution feel much smoother, without the repeated skipping I was trying to get rid of. Traffic, weather, shader warm-up, drivers, thermals, resolution, power limits, and extra mods can still change the result. This is a **tested starting point**, not a universal performance promise.

---

## Help Me Map APU Compatibility

My Ryzen/Radeon setup is the reference system because it is the hardware I can speak about honestly. I do **not** want the title or branding to make people think this is locked to AMD if the evidence starts showing that it works well on Intel, other AMD APUs, or similar integrated-graphics systems too.

If this works for you on another APU or iGPU, please open a GitHub issue or leave a release comment with the details below. I am especially interested in reports from **Intel Core Ultra/Arc iGPU, Intel Iris Xe, other Radeon 600M/700M/800M-class APUs, and comparable mobile systems**. Clear reports—good or bad—will help me turn this from one person’s reference build into something more useful for the wider GTA IV community.

> If I receive enough consistent, useful reports across Intel and other non-reference systems, I may move the project toward more vendor-neutral **modern APU** branding. Until then, I will keep the Ryzen 5 PRO 6650U / Radeon 660M label visible so nobody mistakes my tested baseline for a claim of universal support.

### Copy/paste compatibility report

```text
APU / CPU:
Integrated GPU or discrete GPU:
RAM amount and channel configuration:
Windows version:
Graphics-driver version:
GTA IV build:
FusionFix version:
DXVK version or variant:
Resolution and refresh rate:
Extra mods installed:
What felt good:
What did not work:
DLC test result (The Lost and Damned / The Ballad of Gay Tony):
Anything you changed from this package:
```

A simple, honest report is enough. Tell me where it was smooth, where it stuttered, what crashed, and what you had to change. That is how we build something real—one clean test at a time.

---

## Intel iGPU Note: `forceIntelGraphics=true`

If you are testing on an **Intel integrated GPU**, set `forceIntelGraphics=true` **where that option is actually available in your FusionFix build, setup utility, or matching configuration template**. Apply it only in the location documented or exposed by the version you have installed, restart the game, and test GTA IV plus both EFLC episodes before deciding that the change helped.

> **This is conditional, not universal.** The supplied Version 2 configuration files do not currently expose this setting, so do not add an unknown line blindly or paste it into `dxvk.conf`. If your installed FusionFix build does not provide or recognize `forceIntelGraphics`, leave it absent, keep the rest of this profile unchanged, and include that detail in your compatibility report. Do not change the setting on AMD or other non-Intel systems unless the specific tool you are using documents a reason to do so.

When you report back, include whether `forceIntelGraphics=true` was available, where you set it, the FusionFix version, and whether it changed launch behavior, GPU selection, stability, or frame pacing. That is the kind of detail that can turn a guess into useful cross-APU support.

---

## What This Package Is

Version 2 preserves the complete Version 1 package and replaces the matched Version 2 configuration trio:

| File | What Version 2 changes |
|---|---|
| `tuned/dxvk.conf` | Uses the tested DXVK configuration for the active DXVK/Vulkan path. |
| `tuned/GTAIV.EFLC.FusionFix.cfg` | Uses the tested high-visual FusionFix graphics and frame-pacing profile. |
| `tuned/GTAIV.EFLC.FusionFix.ini` | Uses the matching advanced FusionFix profile, including the tested limiter and shadow balance. |
| `tuned/stream.ini` | Provides the supplied streaming configuration for manual placement in `pc\stream.ini`. |

The package is meant to give you a controlled, repeatable configuration layer. It does **not** replace the game, the launcher, saves, game archives, FusionFix binaries, DXVK binaries, or any separately distributed mod assets.

---

## How I Put It Together

I supplied the reference hardware context, the live configuration files, the Version 2 replacements, the tests I was able to run, and the decisions about how the package should feel. I used **Manus, an AI assistant**, as a research, organization, documentation, packaging, and validation partner. It helped me catalogue the configuration layer, compare the Version 1 and Version 2 files, research the documented behavior of the renderer and mods, organize the installation/rollback instructions, create the optional Windows installer, and verify archive and checksum integrity.

That assistance did **not** make the original mods, test every hardware combination, own the results on a different system, or replace the judgment of the people who built the underlying tools. I made the final choices around my setup, and I want feedback from real players before widening the compatibility claims.

| I provided and directed | Manus helped organize and validate | Supplied or installed separately |
|---|---|---|
| Reference-system details, target behavior, tested Version 2 files, desired paths, and final review direction | Research synthesis, file comparison, README structure, installer logic, checksum generation, packaging, and rollback documentation | GTA IV, FusionFix, DXVK/DXVK variants, Gillian’s archive, Fusion Overloader, visual mods, vehicles, traffic/popcycle content, and all third-party binaries/assets |

---

## What You Need to Install Separately

This repository gives you configurations and an installer—not a complete pre-modded game. Start with a legitimate GTA IV Complete Edition installation and make sure the unmodified game launches before you begin. Then follow the current instructions from every project you choose to use.

| Component or resource | Where it belongs in the process |
|---|---|
| [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) | Install and configure the current official release before applying this profile. |
| [DXVK](https://github.com/doitsujin/dxvk) or a compatible tested variant | Use only one intended Direct3D-to-Vulkan wrapper path. Keep the version and documentation in mind. |
| [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) | Follow the archive’s own current instructions. It is an all-in-one resource, but it still has its own setup rules. |
| Fusion Overloader | Install only when required by the archive or the mod documentation you are following. Respect its precedence rules. |
| LibertyCityPlates, Xbox Rain Droplets, Project2DFX, vegetation, vehicle, traffic, and audio mods | Obtain these from their original sources, preserve their credits, and test them in small groups. |

Do not add a second ASI loader, `d3d9.dll`, `dxgi.dll`, DXVK wrapper, shader proxy, or random `update`-folder priority layer on top of the path you intend to use. One loader path and one renderer path are easier to troubleshoot than a stack of overlapping fixes.[^7]

---

## Package Contents

The refreshed Version 2 **Release** and **Source** ZIPs both include the tuned files below. The Source ZIP also includes originals, assessment documents, the validation report, and a package checksum manifest.

| Package path | Live-game destination for manual installation | Purpose |
|---|---|---|
| `tuned/dxvk.conf` | `\dxvk.conf` | Tested Version 2 DXVK renderer profile. |
| `tuned/stream.ini` | `\pc\stream.ini` | Supplied streaming configuration. |
| `tuned/AudioMap.ini` | `\plugins\AudioMap.ini` | Supplied custom audio mapping. |
| `tuned/ConsoleSelectMenuIV.ini` | `\plugins\ConsoleSelectMenuIV.ini` | Supplied console-style menu configuration. |
| `tuned/GTAIV.EFLC.FusionFix.cfg` | `\plugins\GTAIV.EFLC.FusionFix.cfg` | Main FusionFix graphics and frame-pacing profile. |
| `tuned/GTAIV.EFLC.FusionFix.ini` | `\plugins\GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix shadow, streaming, and limiter settings. |
| `tuned/GTAIV.XboxRainDroplets.ini` | `\plugins\GTAIV.XboxRainDroplets.ini` | Reduced rain-droplet density profile. |
| `tuned/LibertyCityPlates.txt` | `\plugins\LibertyCityPlates.txt` | Supplied license-plate and vehicle-effect configuration. |
| `tuned/preload.list` | `\common\shaders\preload.list` | Shader preload metadata for the matching shader setup. |

> **Important:** `preload.list` is metadata, not a general mod load order. It only helps when every shader it lists is actually installed in the expected shader directory.[^3] [^4]

---

## Recommended Installation Path

### 1. Start clean and make a backup

Confirm that your legitimate GTA IV Complete Edition install starts before adding anything. Close GTA IV, Rockstar Games Launcher, and any mod manager. Back up the game folder, or at minimum the existing `plugins`, `pc`, `common\shaders`, `update`, and renderer/configuration files.

### 2. Build the foundation first

Install FusionFix and the dependencies required by the Drag-and-Drop Archive. If your planned stack uses Fusion Overloader, install it only as directed by the relevant documentation. Test the game after the foundation layer before adding visual packs, vehicles, traffic, vegetation, or other extras.

### 3. Choose manual installation or the automatic installer

For **manual installation**, copy the nine `tuned/` files to the exact live-game destinations in the Package Contents table. `stream.ini` belongs in `pc\stream.ini`; it is not a plugin file.

For the guided route, use the Version 2 Automatic Installer described below. It applies the same nine files to the correct destinations after you choose the GTA IV folder containing `GTAIV.EXE`.

### 4. Keep the configuration readable

Use one primary frame limiter. For this profile, FusionFix is intended to be the limiter. During early testing, disable Radeon Chill, RTSS, extra DXVK caps, or other external caps so you can understand which change affects frame pacing.

---

## Automatic Installer

The **GTA IV Version 2 Automatic Installer** is optional. It deploys the same tested Version 2 configuration and `stream.ini` included in the refreshed ZIPs, but it puts them in the right places for you. It does **not** include GTA IV, launcher files, save files, executable replacements, FusionFix/DXVK binaries, or third-party mod assets.

1. Close GTA IV, the Rockstar Games Launcher, and any mod tool that may hold a configuration file open.
2. Run `GTAIV_V2_Automatic_Installer_v2.0.0.exe` and approve the Windows administrator prompt.
3. Select the GTA IV folder that directly contains `GTAIV.EXE`, for example `C:\Program Files (x86)\Rockstar Games\GTA IV`.
4. The installer validates `GTAIV.EXE`, creates a timestamped backup, writes the nine files shown above, and applies the current user’s **Run as administrator** compatibility flag to `GTAIV.EXE`.

Before overwriting a file, the installer stores the previous copy beneath:

```text
<GTA IV folder>\GTAIV_V2_Installer_Backups\YYYY-MM-DD_HH-MM-SS\
```

Its uninstaller can restore the latest installer backup and the compatibility setting that existed before installation. Use **Installed apps** or `GTAIV_V2_Automatic_Installer_Uninstall.exe` in the game folder, then choose **Yes** when it offers to restore the latest backup. If a target file did not exist before installation, the accepted rollback removes the installer-created file.

---

## Version 2 Graphics and Performance Profile

These are the choices I kept together as the Version 2 profile. The three Version 2 files are a matched set; do not mix them with an unrelated DXVK or FusionFix profile unless you are ready to test the change yourself.

| Area | Profile choice | Why I kept it |
|---|---|---|
| Frame pacing | FusionFix 60 FPS limit with accurate limiter mode | Keeps the limiter decision in one place. |
| Anti-aliasing | SMAA | Useful edge smoothing without relying on a heavier multi-sample path. |
| Shadows | Shadow Filter 5 with `ExtraDynamicShadows=1` | Keeps a richer image while avoiding the heavier second dynamic-shadow level. |
| Post-processing | Sun shafts, ambient occlusion, and volumetric fog enabled | Part of the high-visual look I tested on the reference system. |
| Rain | `MaxDrops=900`, `MaxMovingDrops=1800` | Pulls back weather-particle work before cutting the whole look. |
| Shader loading | Complete supplied preload list | Helps avoid missing-resource errors when every matching shader is installed. |
| Shared-memory approach | DXVK memory, pipeline, frame-latency, fullscreen, and async settings preserved as tested | Keeps the supplied renderer route while acknowledging the limits of a shared-memory APU. |

If dense traffic remains heavy, lower the in-game traffic-density slider from `100` to `80` before lowering texture quality. If rain is the problem, try `MaxMovingDrops=1400` first. If shadows are still the bottleneck, try `ExtraDynamicShadows=0` as a second-stage fallback.

---

## DXVK Configuration Notes

`dxvk.conf` only matters when a matching DXVK/Vulkan wrapper is actually active. DXVK translates GTA IV’s Direct3D 9 calls to Vulkan, so it can affect shader compilation, pipeline caching, latency, memory reporting, and presentation behavior. It does nothing when the game is running on native Direct3D 9.

| Setting | Value in this profile | Practical role |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | Preserves shader-cache behavior for GPLAsync/GPLALL-style forks. |
| `d3d9.maxAvailableMemory` | `4096` | Provides a 4 GiB memory-budget hint; it is not a hard VRAM reservation. |
| `d3d9.maxFrameLatency` | `1` | Requests a one-frame queue for lower latency; test it if stutter changes. |
| `dxvk.numCompilerThreads` | `4` | A conservative compiler-thread count for the six-core mobile reference CPU. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Lets the driver and DXVK select a compatible pipeline-library route. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Avoids forcing device-local placement on shared-memory graphics. |
| `d3d9.maxFrameRate` | `0` | Leaves DXVK’s own cap off so FusionFix remains the intended limiter. |
| `dxvk.allowFse` | `true` | Preserves the supplied fullscreen-exclusive behavior when the active DXVK build supports it. |
| `dxvk.enableAsync` / `dxvk.numAsyncThreads` | `true` / `4` | Preserves the supplied async-capable configuration. |

> **Compatibility warning:** `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads` are associated with GPLAsync/GPLALL-style or other async-capable variants, not the current standard upstream DXVK option set. I preserved them because they were part of the path I tested. If stock upstream DXVK reports them as unknown, keep supported lines and remove only those build-specific lines. The official DXVK configuration reference is the authority for upstream options.[^2] If Alt+Tab or window behavior breaks, test `dxvk.allowFse=false` first.

Let shader caches warm up before judging a fresh run. If CPU use during compilation is the problem, test `dxvk.numCompilerThreads=2`. Keep external frame caps disabled until you know the base profile is behaving correctly.

---

## Shader Preloading

The supplied `preload.list` includes the base entries and the following FusionFix/LibertyCityPlates-related shader names:

```text
gta_trees_extended.fx
gta_vehicle_licenseplate.fx
gta_vehicle_licenseplate_ext.fx
gta_vehicle_track2.fx
gta_vehicle_track.fx
```

Adding a filename to `preload.list` does **not** create the shader. The matching files must exist in the correct GTA IV shader directory. FusionFix issue documentation connects missing entries for these shaders with invalid-resource crashes and incorrect LibertyCityPlates installations.[^3]

If a later FusionFix build dynamically registers the same shaders, follow the current upstream guidance instead of maintaining a duplicate or conflicting preload file.[^4]

---

## EFLC and Loose-File Compatibility Note

I found an important difference between the base story and the EFLC episodes while working through this setup. If you choose to add the replacement linked below, my tested approach is to install it as **loose-file replacements**, not as an `update`-folder mod:

- [Linked replacement resource on Nexus Mods](https://www.nexusmods.com/gta4/mods/781)

In my experience, installing that content in the `update` folder could leave the base GTA IV story working while The Lost and Damned or The Ballad of Gay Tony crashed during launch. Replacing the corresponding loose files kept EFLC working and allowed the changes to appear in the episodes too.

> This is an experience-based compatibility note, not a claim that every version of that mod or every installation will behave identically. Back up first, follow the mod author’s instructions where they differ, and test GTA IV, The Lost and Damned, and The Ballad of Gay Tony separately after each major change.

---

## Testing, Troubleshooting, and Rollback

### Test the setup in real gameplay

Do not judge the profile from one loading screen. Start a save, move between interiors and the street, drive through dense daytime traffic, drive at night in rain, use vehicles with custom audio, reload once, and test both EFLC episodes if you have them installed.

Look for missing or invisible vehicles, black/corrupted materials, missing plate effects, audio dropouts, lighting changes after interiors, launch crashes, loading crashes, or uneven frame pacing even when an FPS counter looks fine.

### Troubleshooting table

| Symptom | First move |
|---|---|
| Game fails to start | Verify the game build, FusionFix installation, and active renderer DLL chain. Do not add another wrapper. |
| Invalid resource or shader error | Restore `preload.list`, then verify every listed shader exists. |
| Startup crash with a GPLAsync-style build | Comment out `dxvk.gplAsyncCache=True` and test again. |
| Rain stutter | Lower `MaxMovingDrops` first. |
| Large shadow-related frame-time dips | Test `ExtraDynamicShadows=0`. |
| Vehicle audio breaks | Keep the vehicle budget unchanged and retest the audio/vehicle combination. |
| Traffic remains slow | Lower traffic density before cutting texture quality. |
| Vulkan is unstable | Use the documented FusionFix fallback path and record the driver/version combination in your report. |
| EFLC crashes while the base story works | Review the loose-file versus `update`-folder note above, then test each episode with recent changes removed. |

### Rollback

If you used the automatic installer, close GTA IV and the launcher, then run `GTAIV_V2_Automatic_Installer_Uninstall.exe` or use **Installed apps**. Choose **Yes** to restore the latest installer backup. That returns the prior copies of the nine managed files and restores the preceding `GTAIV.EXE` compatibility setting.

For a manual install, restore the files from `originals/` to their former locations. If the problem looks shader-related, restore `preload.list` first. Then restore `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `dxvk.conf`, `GTAIV.XboxRainDroplets.ini`, and `stream.ini` as needed. Do not delete saves or modify Rockstar game archives as a first troubleshooting step.

---

## Credits, Boundaries, and Sources

This is my community configuration package and documentation path. It is **not** an official Rockstar, AMD, FusionFix, DXVK, Gillian, LibertyCityPlates, or mod-author release. I will keep credit where it belongs and ask everyone who uses or redistributes this material to do the same.

| Project or resource | Credit and boundary |
|---|---|
| **FusionFix** | ThirteenAG and FusionFix contributors. Use the [official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix). |
| **DXVK** | doitsujin and DXVK contributors. Use the [official repository](https://github.com/doitsujin/dxvk). |
| **DXVK GPLAsync** | Ph42oN and GPLAsync contributors when that fork is used. See the [project page](https://gitlab.com/Ph42oN/dxvk-gplasync). |
| **Gillian’s GTA IV Modding Guide and Drag-and-Drop Archive** | Gillian and the guide/archive contributors. Follow the [official guide](https://gillian-guide.github.io/) and archive documentation. |
| **LibertyCityPlates, Xbox Rain Droplets, Fusion Overloader, Project2DFX, vegetation, vehicles, traffic/popcycle, and other enhancements** | Their original authors and contributors. Use the original pages for licenses, credits, updates, and support. Do not redistribute their files without permission. |
| **AMD Ryzen PRO and Radeon** | AMD owns the platform names, drivers, specifications, and trademarks. My reference hardware does not make this an AMD-only project. |
| **GTA IV** | Rockstar Games and its licensors. This is an unofficial configuration project and includes no game content. |

The configuration is provided as-is. Always keep a backup. A change that feels perfect on one system may reduce stability on another because driver version, thermal behavior, power limits, display resolution, memory bandwidth, and installed content all matter.

## References

[^1]: [Gillian’s FusionFix guide](https://gillian-guide.github.io/essential-modding/fusionfix/) — Complete Edition compatibility, installation, configuration, and build context.
[^2]: [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf) — upstream option names, supported values, and behavior.
[^3]: [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347) — LibertyCityPlates/FusionFix shader preload requirements and invalid-resource troubleshooting.
[^4]: [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431) — preload-list correctness and dynamic shader-registration behavior.
[^5]: [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) — primary project documentation and configuration definitions.
[^6]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) — archive installation and compatibility context.
[^7]: [Gillian’s mod-loading guide](https://gillian-guide.github.io/extras/modloading/) — Fusion Overloader precedence and update-folder guidance.
[^8]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync) — GPL/async cache behavior and fork-specific compatibility context.
