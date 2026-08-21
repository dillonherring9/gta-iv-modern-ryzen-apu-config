# GTA IV, After Dark — Modern Ryzen APU Configuration

## A Ryzen 5 PRO 6650U / Radeon 660M Profile for a Smoother Night in Liberty City

> **This is a community configuration package, not an original-mod claim.** I built it around the way I actually run GTA IV Complete Edition: a legitimate install, a modern mod stack, a Ryzen 5 PRO 6650U with Radeon 660M integrated graphics, and **16 GB of dual-channel memory**. The point is not to sell a miracle. The point is a cleaner ride through the city: stronger frame pacing, modern fixes, fewer ugly pauses, and a path back out if the night goes wrong.

This repository is my organized configuration, documentation, and installer layer. I did **not** create FusionFix, DXVK, Gillian’s Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Xbox Rain Droplets, Project2DFX, restored vegetation, vehicle packs, traffic/popcycle files, or any other third-party project named here. Their authors built the foundation and deserve the credit.

## **READ THIS BEFORE YOU JUDGE THE PERFORMANCE — CACHES HAVE TO BUILD**

> **If you are playing on an APU, expect stutter while shader and pipeline caches build. Those caches are important. The first drive through a new area, new weather, a new time of night, or a new effect can hitch while the renderer learns the work. Let it build before you decide the profile is broken.**
>
> **Changing in-game graphics settings, resolution, the renderer path, DXVK options, driver versions, or major visual mods can trigger cache rebuilding. Some changes require a full restart of GTA IV; if you change them and keep playing without restarting, the game may stutter until you do. There is no switch, tweak, or shortcut that lets a shared-memory APU skip cache building.**

> **The clean rule:** make one change, restart the whole game when the setting calls for it, then repeat the same demanding route until the cache has had time to settle. Do not judge the first lap like it is the final record.

---

## Table of Contents

| Section | What it covers |
|---|---|
| [My Reference Setup and Result](#my-reference-setup-and-result) | The hardware and the standard behind this profile. |
| [Help Me Map APU Compatibility](#help-me-map-apu-compatibility) | How to send a useful report from another iGPU system. |
| [Installing Without Creating a Mess](#installing-without-creating-a-mess) | The clean install path, manual route, and installer behavior. |
| [Version 2 Graphics and Performance Profile](#version-2-graphics-and-performance-profile) | The settings that stay together and the dials that move first. |
| [DXVK and Cache Reality](#dxvk-and-cache-reality) | Renderer behavior, compiler settings, caches, and restarts. |
| [Shader Preloading](#shader-preloading) | What `preload.list` is—and what it cannot do. |
| [Testing, Troubleshooting, and Rollback](#testing-troubleshooting-and-rollback) | How to isolate trouble and return to a known-good state. |
| [Credits, Boundaries, and Sources](#credits-boundaries-and-sources) | Attribution and technical references. |

---

## My Reference Setup and Result

I tested Version 2 around the system below. It is the baseline behind the words in this README, not a promise that every laptop, driver, or mod list will move the same way.

| Component | Reference setup | Why it matters |
|---|---|---|
| Processor | AMD Ryzen 5 PRO 6650U | GTA IV still spends real CPU time on traffic, streaming, scripts, weather, and a heavy mod stack. |
| Graphics | Radeon 660M integrated RDNA 2 graphics | The GPU shares system memory, so resolution, shadows, rain, post-processing, and shader compilation must stay in balance. |
| Memory | 16 GB dual-channel system memory | Dual-channel bandwidth matters on an iGPU, but Windows and the game still drink from the same pool. |
| Game | GTA IV Complete Edition, build 1.2.0.59 | This is the Complete Edition context used in Gillian’s FusionFix guidance.[^1] |
| Renderer path | FusionFix with one active DXVK/Vulkan path when stable | Stacked wrappers make crashes and stutter harder to read. |
| Frame target | 60 FPS where the scene allows it | I care about consistent frame times and real play, not a loud counter that hides a rough ride. |

On this setup, the three Version 2 files together made high settings and a higher resolution feel much smoother once the cache had warmed. Traffic, weather, shader warm-up, drivers, thermals, power limits, resolution, memory speed, and extra mods can still change the result. This is a **tested starting point**, not a universal performance guarantee.

### A note for the drive

> **Optional, but it is the route I recommend for the full Liberty City experience:** support the music by purchasing the complete The Weeknd catalog through legitimate stores or artist-supporting platforms, load the music you own into GTA IV User Music, and let **Independence FM** score every mission. It is the right kind of after-dark—wet streets, long red lights, and nowhere you need to be except inside the city.

---

## Help Me Map APU Compatibility

The Ryzen/Radeon system is the one I can speak on honestly. I do not want the name to trap this project on AMD if real reports show it runs well on Intel, other Radeon APUs, or similar integrated graphics. If you test it elsewhere, leave a GitHub issue or release comment with the details below. Clean reports—good or bad—turn one person’s setup into something the whole community can use.

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
Did caches settle after repeat runs?:
Which settings changed before the test?:
Did you restart GTA IV after those changes?:
What felt good:
Where did it stutter or crash?:
DLC test result (The Lost and Damned / The Ballad of Gay Tony):
```

> **Bring the details, not just the feeling.** Tell me where it was smooth, what stuttered, whether it happened only while caches were fresh, what changed, and whether you restarted. That is how we separate a bad profile from a cold cache.

### Intel systems

The Version 2 installer is **CPU-agnostic**: it validates the folder holding `GTAIV.EXE`, backs up the nine managed files, and writes the same profile to the same destinations. A strong Intel CPU can run this profile. The real question is which GPU is rendering GTA IV, which driver it is using, whether DXVK/FusionFix is stable, which resolution is active, and what extra mods are in the room.

Do not add `forceIntelGraphics=true` just because it sounds like a solution. Keep Version 2 unchanged for the first run, choose the intended GPU for `GTAIV.EXE` in Windows, restart after renderer changes, let caches build, and test the base story plus both EFLC episodes before deciding an Intel-specific change helped.

| Intel graphics setup | First expectation |
|---|---|
| NVIDIA or AMD discrete GPU installed | A strong fit. Use the normal installer, force the discrete GPU for `GTAIV.EXE` in Windows, and test normally. |
| Intel Arc discrete GPU installed | The installer still works, but driver quality and the DXVK/FusionFix route decide the outcome. Test GTA IV, TLAD, and TBoGT separately. |
| Only Intel UHD 770 iGPU | The installer still works, but the Radeon 660M high-visual result is not guaranteed. Start at a sensible lower resolution and give caches time before reducing the heaviest settings. |

`dxvk.numCompilerThreads=4` is a conservative choice for the six-core Ryzen reference system. It is safe on a Core i9-12900K, but it is not trying to use every logical thread. Leave it at `4` for the first test. Real frame pacing after the cache settles matters more than an aggressive thread count.[^9]

---

## What This Package Is

Version 2 preserves the complete Version 1 package and replaces the matched Version 2 configuration trio.

| File | What Version 2 changes |
|---|---|
| `tuned/dxvk.conf` | The tested DXVK configuration for the active DXVK/Vulkan route. |
| `tuned/GTAIV.EFLC.FusionFix.cfg` | The high-visual FusionFix graphics and frame-pacing profile. |
| `tuned/GTAIV.EFLC.FusionFix.ini` | The matching advanced FusionFix limiter, shadow, and streaming profile. |
| `tuned/stream.ini` | The supplied streaming configuration for manual placement in `pc\stream.ini`. |

It is a controlled, repeatable configuration layer. It does **not** replace the game, launcher, saves, game archives, FusionFix binaries, DXVK binaries, or separately distributed mod assets.

I supplied the reference hardware, active configuration files, target behavior, testing direction, and final choices. **Manus AI** helped with research synthesis, document organization, file comparison, installer packaging, and validation. It did not make the underlying mods, test every hardware combination, or replace the judgment of their authors.

---

## Installing Without Creating a Mess

Start with a legitimate GTA IV Complete Edition install that launches before you touch anything. FusionFix, DXVK, Gillian’s archive, Fusion Overloader, and every visual or content mod remain separate projects with their own current instructions.

| Component or resource | Place in the process |
|---|---|
| [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) | Install and configure the current official release before this profile. |
| [DXVK](https://github.com/doitsujin/dxvk) or a compatible tested variant | Use **one** intended Direct3D-to-Vulkan route. Keep its version and documentation in sight. |
| [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) | Follow the archive’s own current instructions; it has its own setup rules. |
| Fusion Overloader | Install only where the archive or mod documentation tells you to. Respect precedence rules. |
| Visual, vehicle, traffic, vegetation, rain, and audio mods | Take them from the original authors and test them in small groups. |

> **Do not stack a second ASI loader, `d3d9.dll`, `dxgi.dll`, DXVK wrapper, shader proxy, or random `update`-folder priority layer on top of the path you mean to use.** One loader and one renderer are easier to diagnose than a beautiful-looking pile of conflicting fixes.[^7]

### Package contents

| Package path | Manual destination | Purpose |
|---|---|---|
| `tuned/dxvk.conf` | `\dxvk.conf` | Version 2 DXVK renderer profile. |
| `tuned/stream.ini` | `\pc\stream.ini` | Supplied streaming configuration. |
| `tuned/AudioMap.ini` | `\plugins\AudioMap.ini` | Custom audio mapping. |
| `tuned/ConsoleSelectMenuIV.ini` | `\plugins\ConsoleSelectMenuIV.ini` | Console-style menu configuration. |
| `tuned/GTAIV.EFLC.FusionFix.cfg` | `\plugins\GTAIV.EFLC.FusionFix.cfg` | Main graphics and frame-pacing profile. |
| `tuned/GTAIV.EFLC.FusionFix.ini` | `\plugins\GTAIV.EFLC.FusionFix.ini` | Advanced shadow, streaming, and limiter profile. |
| `tuned/GTAIV.XboxRainDroplets.ini` | `\plugins\GTAIV.XboxRainDroplets.ini` | Reduced rain-droplet density profile. |
| `tuned/LibertyCityPlates.txt` | `\plugins\LibertyCityPlates.txt` | License-plate and vehicle-effect configuration. |
| `tuned/preload.list` | `\common\shaders\preload.list` | Shader preload metadata for the matching shader setup. |

> **Important:** `preload.list` is metadata, not a general mod load order. It only belongs here when every named shader is installed in the expected directory.[^3] [^4]

### Recommended path

1. **Back up before the lights go out.** Close GTA IV, Rockstar Games Launcher, and any mod manager. Back up the whole game folder or, at minimum, `plugins`, `pc`, `common\shaders`, `update`, and renderer/configuration files.
2. **Build the foundation first.** Install FusionFix and the dependencies required by the archive. Test the base layer before visual packs, vehicles, traffic, vegetation, or other extras.
3. **Use the manual route or the installer.** For manual installation, copy the nine `tuned/` files to the destinations above. `stream.ini` belongs in `pc\stream.ini`, not in `plugins`.
4. **Keep the profile readable.** FusionFix is the intended limiter. During early testing, turn off Radeon Chill, RTSS, extra DXVK caps, and other external caps. You need to know which change is moving the frame time.

### Automatic installer

The **GTA IV Version 2 Automatic Installer** is optional. It installs the same tested Version 2 configuration and `stream.ini` found in the ZIPs, but it places them for you. It does **not** contain GTA IV, launcher files, saves, executable replacements, FusionFix/DXVK binaries, or third-party assets.

1. Close GTA IV, the Rockstar Games Launcher, and every tool holding these files open.
2. Run `GTAIV_V2_Automatic_Installer_v2.0.0.exe` and approve the Windows administrator prompt.
3. Select the GTA IV folder that directly contains `GTAIV.EXE`, such as `C:\Program Files (x86)\Rockstar Games\GTA IV`.
4. The installer validates `GTAIV.EXE`, creates a timestamped backup, writes the nine managed files, and applies the current user’s **Run as administrator** compatibility flag to `GTAIV.EXE`.

Backups land here before a file is overwritten:

```text
<GTA IV folder>\GTAIV_V2_Installer_Backups\YYYY-MM-DD_HH-MM-SS\
```

The uninstaller can restore the latest installer backup and the previous compatibility setting. Use **Installed apps** or `GTAIV_V2_Automatic_Installer_Uninstall.exe`, then choose **Yes** when it offers the latest backup.

---

## Version 2 Graphics and Performance Profile

These settings are a matched set. Do not blend them into an unrelated DXVK or FusionFix profile unless you are prepared to test the change, restart when necessary, and let the resulting cache settle.

| Area | Profile choice | Why it stays |
|---|---|---|
| Frame pacing | FusionFix 60 FPS limit with accurate limiter mode | Keeps the limiter decision in one place. |
| Anti-aliasing | SMAA | Smooths edges without opening a heavier multi-sample path. |
| Shadows | Shadow Filter 5 with `ExtraDynamicShadows=1` | Keeps depth in the image while avoiding the heavier second dynamic-shadow level. |
| Post-processing | Sun shafts, ambient occlusion, and volumetric fog enabled | Part of the high-visual look tested on the reference system. |
| Rain | `MaxDrops=900`, `MaxMovingDrops=1800` | Pulls back weather-particle work before cutting the entire look. |
| Shader loading | Complete supplied preload list | Avoids missing-resource errors when every matching shader is present. |
| Shared-memory approach | Tested DXVK memory, pipeline, latency, fullscreen, and async settings | Keeps the tested renderer route while respecting the limits of a shared-memory APU. |

If dense traffic still feels heavy after the cache is warm, lower the in-game traffic-density slider from `100` to `80` before touching texture quality. If rain is the issue, try `MaxMovingDrops=1400` first. If shadows are still taking their tax, try `ExtraDynamicShadows=0` as the second-stage fallback.

---

## DXVK and Cache Reality

`dxvk.conf` only matters when a matching DXVK/Vulkan wrapper is active. DXVK translates GTA IV’s Direct3D 9 calls to Vulkan, so it can influence shader compilation, pipeline caching, latency, memory reporting, and presentation. It does nothing when the game is running on native Direct3D 9.

| Setting | Value in this profile | Practical role |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | Preserves shader-cache behavior for GPLAsync/GPLALL-style forks. |
| `d3d9.maxAvailableMemory` | `4096` | A 4 GiB memory-budget hint, not a reserved VRAM pool. |
| `d3d9.maxFrameLatency` | `1` | Requests a one-frame queue for lower latency; test it if stutter changes. |
| `dxvk.numCompilerThreads` | `4` | A conservative compiler-thread count for the six-core mobile reference CPU. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Lets DXVK and the driver pick a compatible pipeline-library route. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Avoids forcing device-local placement on shared-memory graphics. |
| `d3d9.maxFrameRate` | `0` | Leaves DXVK’s cap off so FusionFix remains the limiter. |
| `dxvk.allowFse` | `true` | Keeps the supplied fullscreen-exclusive behavior where the active DXVK build supports it. |
| `dxvk.enableAsync` / `dxvk.numAsyncThreads` | `true` / `4` | Keeps the supplied async-capable configuration. |

> **Cache truth for APUs:** shader and pipeline caches must build. A cache miss can look and feel like a stutter, especially in a fresh location or after a settings change. This is normal during the build phase; it is not something an APU can be tuned out of existence. Change one variable, restart GTA IV when the renderer or graphics setting needs it, then replay the same route until the cache stabilizes.

> **Compatibility warning:** `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads` belong to GPLAsync/GPLALL-style or other async-capable variants, not the standard upstream DXVK option set. If stock upstream DXVK reports them as unknown, keep supported lines and remove only the build-specific lines.[^2]

If CPU load during cache construction is the problem, test `dxvk.numCompilerThreads=2`. Keep external frame caps disabled until you know the base profile is stable. If Alt+Tab or window behavior breaks, test `dxvk.allowFse=false` first.

---

## Shader Preloading

The supplied `preload.list` contains the base entries and these FusionFix/LibertyCityPlates-related names:

```text
gta_trees_extended.fx
gta_vehicle_licenseplate.fx
gta_vehicle_licenseplate_ext.fx
gta_vehicle_track2.fx
gta_vehicle_track.fx
```

Adding a filename to `preload.list` does **not** create a shader or erase a cache hitch. The matching files must exist in the correct GTA IV shader directory. FusionFix issue documentation connects missing entries with invalid-resource crashes and incorrect LibertyCityPlates installations.[^3]

If a future FusionFix release dynamically registers the same shaders, follow the upstream method instead of keeping a duplicate or conflicting list.[^4]

---

## EFLC and Loose-File Compatibility Note

I found one difference between the base story and EFLC while shaping this setup. If you add the [linked replacement resource on Nexus Mods](https://www.nexusmods.com/gta4/mods/781), my tested route is **loose-file replacement**, not an `update`-folder mod.

In my testing, the `update`-folder version could leave the base game running while The Lost and Damned or The Ballad of Gay Tony crashed at launch. Replacing the corresponding loose files kept EFLC alive and let the change show up in both episodes.

The replacement assets are much higher resolution than the originals. If they push the profile too hard, reduce output resolution one sensible step, restart, and give the changed graphics path time to rebuild its cache before calling the tradeoff. A clean, stable image with headroom is worth more than maximum pixels with no room to breathe.

---

## Testing, Troubleshooting, and Rollback

### Test it in the city, not just at the loading screen

Start a save, move between interiors and streets, drive through dense daytime traffic, return at night in rain, use a vehicle with custom audio, reload once, and test both EFLC episodes if they are installed. A smooth cache-warmed loop is the only honest verdict.

Look for missing vehicles, black or corrupted materials, missing plate effects, audio dropouts, lighting changes after interiors, launch crashes, loading crashes, or uneven frame pacing even when the FPS counter is trying to look impressive.

| Symptom | First move |
|---|---|
| Game fails to start | Verify the game build, FusionFix installation, and active renderer-DLL chain. Do not add another wrapper. |
| Fresh-route or first-run stutter | Replay the same route after the cache builds. If you recently changed graphics, renderer, driver, or DXVK options, restart GTA IV first. |
| Stutter never settles after repeat runs | Return the last settings change, restart, and test the prior known-good profile. Then check thermals, traffic density, and renderer compatibility. |
| Invalid resource or shader error | Restore `preload.list`, then verify every named shader exists. |
| Startup crash with a GPLAsync-style build | Comment out `dxvk.gplAsyncCache=True` and test again. |
| Rain stutter after cache warm-up | Lower `MaxMovingDrops` first. |
| Large shadow-related frame-time dips | Test `ExtraDynamicShadows=0`. |
| Vehicle audio breaks | Keep the vehicle budget unchanged and retest the audio/vehicle combination. |
| Traffic remains slow | Lower traffic density before cutting texture quality. |
| Vulkan is unstable | Use FusionFix’s documented fallback path and record the driver/version in your report. |
| EFLC crashes while the base story works | Revisit the loose-file versus `update`-folder note, then test each episode with the latest change removed. |

### Rollback

If you used the installer, close GTA IV and the launcher, then run `GTAIV_V2_Automatic_Installer_Uninstall.exe` or use **Installed apps**. Choose **Yes** to restore the newest installer backup. That returns the prior copies of the nine managed files and restores the previous `GTAIV.EXE` compatibility setting.

For manual installation, restore files from `originals/` to their prior locations. If it feels shader-related, restore `preload.list` first. Then restore `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `dxvk.conf`, `GTAIV.XboxRainDroplets.ini`, and `stream.ini` as needed. Do not delete saves or alter Rockstar game archives as the first move.

---

## Credits, Boundaries, and Sources

This is my community configuration package. It is **not** an official Rockstar, AMD, FusionFix, DXVK, Gillian, LibertyCityPlates, or mod-author release. Credit belongs to the people who made the tools; please keep it there when you use or share this material.

| Project or resource | Credit and boundary |
|---|---|
| **FusionFix** | ThirteenAG and FusionFix contributors. Use the [official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix). |
| **DXVK** | doitsujin and DXVK contributors. Use the [official repository](https://github.com/doitsujin/dxvk). |
| **DXVK GPLAsync** | Ph42oN and GPLAsync contributors when that fork is active. See the [project page](https://gitlab.com/Ph42oN/dxvk-gplasync). |
| **Gillian’s GTA IV Modding Guide and Drag-and-Drop Archive** | Gillian and the guide/archive contributors. Follow the [official guide](https://gillian-guide.github.io/). |
| **Other enhancements** | The original authors of LibertyCityPlates, Xbox Rain Droplets, Fusion Overloader, Project2DFX, vegetation, vehicle, traffic/popcycle, and other projects. Use their original pages for licenses, updates, and support. |
| **AMD Ryzen PRO and Radeon** | AMD owns the platform names, drivers, specifications, and trademarks. My reference hardware does not make this an AMD-only project. |
| **GTA IV** | Rockstar Games and its licensors. This is an unofficial configuration project and contains no game content. |

The configuration is provided as-is. Keep a backup. A change that feels perfect in one city may turn rough on another system because drivers, thermals, power limits, resolution, memory bandwidth, and installed content all matter.

## References

[^1]: [Gillian’s FusionFix guide](https://gillian-guide.github.io/essential-modding/fusionfix/) — Complete Edition compatibility, installation, configuration, and build context.
[^2]: [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf) — upstream option names, supported values, and behavior.
[^3]: [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347) — LibertyCityPlates/FusionFix shader preload requirements and invalid-resource troubleshooting.
[^4]: [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431) — preload-list correctness and dynamic shader-registration behavior.
[^5]: [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) — primary project documentation and configuration definitions.
[^6]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) — archive installation and compatibility context.
[^7]: [Gillian’s mod-loading guide](https://gillian-guide.github.io/extras/modloading/) — Fusion Overloader precedence and update-folder guidance.
[^8]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync) — GPL/async cache behavior and fork-specific compatibility context.
[^9]: [Intel Core i9-12900K specifications](https://www.intel.com/content/www/us/en/products/sku/134599/intel-core-i912900k-processor-30m-cache-up-to-5-20-ghz/specifications.html) — 16 total cores, 8 Performance cores, 8 Efficient cores, 24 threads, and Intel UHD Graphics 770.
