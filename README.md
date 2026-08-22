# GTA IV, After Dark — Modern PC Configuration

## A clear setup and maintenance guide for GTA IV Complete Edition

> **This is a configuration guide, not a new modpack and not a promise of perfect performance.** It helps you use GTA IV Complete Edition with Gillian’s complete Drag-and-Drop Archive on a modern Windows PC. It is based on a Ryzen 5 PRO 6650U, Radeon 660M, and 16 GB dual-channel-memory reference system, but it can also be useful on other modern hardware.

GTA IV has a reputation for being difficult on newer PCs. A workable modern path exists: FusionFix supports Complete Edition and provides modern-system options, while Gillian’s Drag-and-Drop Archive provides a ready-to-play Complete Edition base.[1] [2] This project adds a small, reversible configuration layer on top of that base.

The project does **not** include GTA IV, the Drag-and-Drop Archive, FusionFix, DXVK binaries, shaders, saved games, launcher files, or third-party mod files. It does include configuration files, a backup/rollback installer, documentation, sources, credits, history, validation guidance, and a complete self-maintenance reference.

## Choose the right reading path

| If you are… | Read this |
|---|---|
| New to GTA IV modding | Start with **Quick setup** below. Every action is written in plain language. |
| Unsure what a technical word means | Read **Words you will see** before changing anything. |
| Trying to fix a problem | Read **Test, troubleshoot, and roll back**. |
| Updating DXVK, an ASI loader, or high-resolution textures yourself | Read **Advanced maintenance** and then the full reference. |
| Taking over the project or making a fork | Read [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md) and [`CONTRIBUTING.md`](CONTRIBUTING.md). |
| Looking for every original instruction, source, credit, historical detail, and older maintenance explanation | Read [`docs/COMPLETE_MAINTENANCE_REFERENCE.md`](docs/COMPLETE_MAINTENANCE_REFERENCE.md). It is the preserved full reference; this simpler page does not delete that information. |
| Unsure whether the archive installed stock DXVK, GPLAsync, or another async-capable renderer | Read [`docs/ARCHIVE_RENDERER_IDENTIFICATION.md`](docs/ARCHIVE_RENDERER_IDENTIFICATION.md) **before** reinstalling or replacing `vulkan.dll`. |

## First: know the one folder that matters

The **game folder** is the folder that contains `GTAIV.exe`. It may look like `C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV\GTAIV.exe` or a Rockstar Games Launcher folder. Do not select the `plugins` folder, the `pc` folder, Documents, a download folder, or an extra folder created while extracting an archive.

| Word | Simple meaning | What you should do |
|---|---|---|
| **`.ini` / `.cfg` file** | A small text file that stores settings. | Let Version 3 install its managed settings files. Do not edit one without knowing why. |
| **DLL** | A support file used by Windows programs. | Never download an individual DLL from a random DLL website. Use the original project’s release or the archive setup utility. |
| **DXVK** | A component that lets GTA IV use Vulkan instead of its original DirectX 9 graphics path. | Use Gillian’s setup utility first. Treat manual DXVK changes as advanced maintenance. |
| **Vulkan** | The newer graphics system selected in the game when the archive route works. | Select it only after the archive setup is complete. |
| **FusionFix** | A community project that fixes GTA IV Complete Edition and adds useful settings. | Gillian’s archive supplies its intended version. Do not casually update it inside the curated archive. |
| **ASI loader** | A helper that loads some GTA IV plugins. | Keep one intended loader; do not add another one because a video says to. |
| **Shader/cache** | Graphics work that the game and driver learn and save while you play. | Expect some stutter after major changes. Restart and repeat the same route before judging performance. |

## Quick setup: do these steps in order

> **Do not mix this with several other complete GTA IV guides.** A clean setup is easier to fix and easier to hand to another person later.

| Step | Do this | You know it worked when |
|---|---|---|
| **1. Install the correct game** | Install a legitimate **GTA IV Complete Edition** copy through Steam or Rockstar Games Launcher. If you previously mixed several GTA IV guides, make a clean copy first. | You can open the game folder and see `GTAIV.exe`. |
| **2. Install Gillian’s base archive** | Download Gillian’s **Complete Edition / 1.2.0.59 Drag-and-Drop Archive**. Extract the files inside the archive directly into the game folder. Do not leave them inside a new extra folder. | Archive files now sit beside `GTAIV.exe`. |
| **3. Use the archive setup helper** | Open `GTAIVSetupUtility.exe` in the game folder. Select **Open…**, select the game folder, then select **Reinstall DXVK** and **Setup launch options**. | The helper completes and copies launcher text to the Windows clipboard. |
| **4. Paste the launch options** | Steam: right-click GTA IV in Library → **Properties** → **Launch Options**. Rockstar Launcher: paste into **Launcher arguments**. Shortcut: right-click → **Properties** → add the text at the end of **Target** without deleting the existing path. | Your normal launch method keeps the copied text. |
| **5. Start the game once** | Launch GTA IV normally. In Graphics settings, choose **Vulkan** only when the archive route works, then restart if asked. | You reach the menu or load a save. |
| **6. If Vulkan crashes** | Close GTA IV. Delete `d3d9.cfg` in the game folder. Then repeat the archive-helper step and check your graphics driver. | Do not add random DLLs or a second renderer wrapper. |
| **7. Close everything** | Close GTA IV, Steam or Rockstar Games Launcher, mod managers, and file-copy tools. | GTA IV is no longer running. |
| **8. Run Version 3** | Run `GTAIV_After_Dark_Complete_Configuration_v3.exe` and choose the folder containing `GTAIV.exe`. | The installer finishes and tells you where it stored the backup. |
| **9. Test calmly** | Drive in daylight, enter and leave an interior, then try night/rain. Restart the game and repeat the same drive. | Judge the second, cache-warmed drive—not only the first minutes after a change. |

## What Version 3 does for you

Version 3 is a **configuration installer for a game folder where the complete Drag-and-Drop Archive already exists**. It checks that the selected folder contains `GTAIV.exe`, checks for `vulkan.dll`, and stops if GTA IV is open. It does not demand one exact FusionFix `.asi` filename because valid archive layouts can differ. The `vulkan.dll` check confirms a Vulkan route exists; it does **not** prove which DXVK family or version the archive selected. Use [`docs/ARCHIVE_RENDERER_IDENTIFICATION.md`](docs/ARCHIVE_RENDERER_IDENTIFICATION.md) to identify it safely.

Before it changes anything, it makes a timestamped backup. It sets **Run this program as an administrator** for `GTAIV.exe`, because this target setup needs the game to create required configuration files. When you uninstall, it can restore the backed-up files and the exact Windows compatibility setting that was present before installation.

| Location | Version 3 installs | Why it matters |
|---|---|---|
| Game folder | `dxvk.conf` | The stock DXVK baseline for the archive’s Vulkan route. |
| `pc\` | `stream.ini` | The streaming profile. |
| `plugins\` | `ConsoleSelectMenuIV.ini` | Console Select Menu setting. |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` | Main FusionFix settings. |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix settings. |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets settings. |
| `plugins\` | `LibertyCityPlates.txt` | LibertyCityPlates settings. |

The installer does **not** add AudioMap. The audit found it was an old carryover, not a documented part of Gillian’s archive. It also does not automatically install a static `preload.list` or the GPLAsync renderer alternative, because both depend on an exact renderer/shader setup and can cause problems when blindly copied.

## What to expect from your computer

The reference system is a Ryzen 5 PRO 6650U, Radeon 660M integrated graphics, and 16 GB dual-channel memory. It is a useful tested starting point, not a minimum requirement or a benchmark promise. GTA IV can still be limited by traffic, world streaming, scripts, weather, resolution, laptop power limits, cooling, driver quality, memory bandwidth, and extra mods.

| Hardware situation | Safe first expectation |
|---|---|
| Recent AMD or Intel integrated graphics | Start at a sensible resolution, use the normal setup, let caches warm, and change one setting at a time. |
| AMD or NVIDIA discrete GPU | The normal installer still applies. Choose the intended GPU for `GTAIV.exe` in Windows, then test the main game and both episodes. |
| Intel Arc discrete GPU | The installer can still apply, but the graphics driver and DXVK/FusionFix path decide the result. Test GTA IV, TLAD, and TBoGT separately. |
| Intel UHD 770 only | The installer can still apply, but the Radeon 660M visual result is not guaranteed. Start lower and give caches time before reducing heavy settings. |
| No working Vulkan-capable driver | Do not force a Vulkan DLL onto the system. Use the documented native Direct3D 9 fallback path instead. |

For the reference APU, the tested starting point uses a FusionFix 60 FPS target, SMAA, reduced rain, high-resolution shadows off, ambient occlusion/sun shafts/volumetric fog off, and one frame limiter. If dense traffic is slow after the cache is warm, lower Traffic Density from `100` to `80` before cutting texture quality. If rain is slow, try `MaxMovingDrops=1400`. If shadows cause large frame-time dips, try `ExtraDynamicShadows=0`.

## Rules that prevent most broken setups

| Keep doing this | Do not do this |
|---|---|
| Use one renderer path, one ASI loader, and one active frame limiter. | Stack `d3d9.dll`, `dxgi.dll`, multiple DXVK wrappers, shader proxies, extra ASI loaders, Radeon Chill, RTSS, and several limiter tools. |
| Back up a working file before testing a replacement. | Replace DXVK, FusionFix, an ASI loader, shader metadata, and content files all at once. |
| Restart GTA IV when a graphics/renderer setting requires it, then repeat the same route. | Decide a setting is bad from one cold first drive. |
| Follow Gillian’s archive instructions and keep the archive’s intended component versions together. | Enable FusionFix’s update check or casually update it inside the curated archive. |
| Use the original authors’ pages for third-party mods and support. | Treat this repository as permission to redistribute their binaries or content. |

## Advanced maintenance: only after the basic setup works

The full step-by-step DXVK, Ultimate ASI Loader, high-resolution-texture, shader, and manual-install instructions are preserved in [`docs/COMPLETE_MAINTENANCE_REFERENCE.md`](docs/COMPLETE_MAINTENANCE_REFERENCE.md). Before changing any renderer file, first use [`docs/ARCHIVE_RENDERER_IDENTIFICATION.md`](docs/ARCHIVE_RENDERER_IDENTIFICATION.md) to identify what the archive currently installed. The short rules below are here so you know what matters most.

### Update DXVK for FusionFix

This is an experiment, not a required upgrade or an automatic performance improvement. Save the working `vulkan.dll`, change **only DXVK**, restart GTA IV, inspect `GTAIV_d3d9.log`, replay a route after caches warm, and restore the saved DLL if the game stops working.

1. Download DXVK only from the [official DXVK releases](https://github.com/doitsujin/dxvk/releases).
2. Open the downloaded archive’s **`x32`** folder. GTA IV is 32-bit; do not use the `x64` folder.
3. Copy `x32\d3d9.dll` to a temporary place and rename the copy to `vulkan.dll`.
4. Replace the game folder’s existing `vulkan.dll` with that renamed copy. Do **not** replace `d3d9.dll` for FusionFix’s Vulkan route.
5. Launch once, confirm the Vulkan/DXVK backend in `GTAIV_d3d9.log`, then repeat a route after the cache warms. Restore the saved `vulkan.dll` if the game fails.

### Update Ultimate ASI Loader for FusionFix

This is also an advanced, one-change-at-a-time test. GTA IV is 32-bit, so use the **x86** `dinput8.dll`, not the Win64 file.

1. Download only from the [official Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases).
2. Save the old game-folder `dinput8.dll`, then replace it with the new x86 `dinput8.dll`.
3. Launch GTA IV and confirm FusionFix and ScriptHook start normally.
4. Keep one intended ASI loader. Do not add a second proxy unless the loader’s own documentation specifically requires it.

### Optional high-resolution vanilla textures

HQ Vanilla Textures City Revitalization is optional. It is not part of Version 3. If you choose it, back up original files, use the project’s tested **loose-file** layout instead of placing it in an `update` folder, and test GTA IV, TLAD, and TBoGT separately. On the reference APU, lower output resolution one step and restart if the larger textures leave too little headroom.[12]

## Test, troubleshoot, and roll back

### Test the city, not only the menu

Load a save or start a new game. Move between an interior and the street, drive through dense daytime traffic, return at night in rain, use a vehicle with custom audio, reload once, and test both episodes if installed. Look for launch crashes, loading crashes, missing vehicles, black/corrupted materials, missing plate effects, audio dropouts, lighting changes after interiors, or uneven pacing.

| Problem | First safe action |
|---|---|
| Game does not start | Check the game build, FusionFix installation, and active renderer-DLL chain. Do not add another wrapper. |
| First-drive stutter | Restart if you changed graphics/driver/renderer settings, then repeat the same route so caches can build. |
| Stutter remains after repeat runs | Undo the last setting change, restart, test the previous working configuration, then check thermals, traffic density, and renderer compatibility. |
| Invalid resource or shader error | Restore the matching shader metadata/reference, then verify every named shader actually exists. Do not copy a random preload list. |
| GPLAsync-style startup crash | Comment out `dxvk.gplAsyncCache=True` and test again only if you deliberately installed that GPLAsync-style DXVK build. |
| Rain stutter after warm-up | Lower `MaxMovingDrops` first. |
| Shadow frame-time dips | Test `ExtraDynamicShadows=0`. |
| Vehicle audio breaks | Keep the vehicle budget unchanged and retest the audio/vehicle combination. |
| Traffic stays slow | Lower Traffic Density before lowering texture quality. |
| Vulkan is unstable | Use FusionFix’s documented fallback path and record the driver/version. |
| EFLC fails while GTA IV works | Recheck loose-file versus `update`-folder changes, then remove the latest change and test each episode. |

### Roll back safely

If you used Version 3, close GTA IV and its launcher. Open Windows **Installed apps**, find the Version 3 installer, and choose uninstall. Choose **Yes** when it asks to restore the newest backup. This restores the managed configuration files and the previous `GTAIV.exe` compatibility setting.

For a manual advanced change, restore the file you saved before the change. For shader-related trouble, restore the prior shader metadata first. Then restore `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `dxvk.conf`, `GTAIV.XboxRainDroplets.ini`, and `stream.ini` as needed. Do not delete saves or change Rockstar game archives as the first recovery step.

## Help the next person instead of creating a mystery setup

If you test this on other hardware, report the CPU/APU, GPU, RAM amount and channel configuration, Windows version, driver version, GTA IV build, archive/FusionFix/DXVK version, resolution, refresh rate, extra mods, exact settings changed, whether you restarted, cold result, warm repeat result, and results for GTA IV, TLAD, and TBoGT. A detailed report is more useful than a bare FPS screenshot.

The current owner does not promise permanent support. This project is prepared for a careful successor. Read [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md) before proposing a takeover or fork, and use [`CONTRIBUTING.md`](CONTRIBUTING.md) for reports and improvements. Users may move to a better-maintained successor, but they should back up first and use one complete configuration ecosystem at a time.

## Credits and project boundary

This is an unofficial community configuration project. Credit belongs to the people who made the underlying tools. Please preserve their credits, licenses, readmes, and source links when sharing or maintaining this work.

| Project or resource | Credit and boundary |
|---|---|
| **FusionFix** | ThirteenAG and FusionFix contributors. [Official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix). |
| **DXVK** | doitsujin and DXVK contributors. [Official repository](https://github.com/doitsujin/dxvk). |
| **DXVK GPLAsync** | Ph42oN and GPLAsync contributors when that fork is used. [Project page](https://gitlab.com/Ph42oN/dxvk-gplasync). |
| **Gillian’s guide and archive** | Gillian and the guide/archive contributors. [Official guide](https://gillian-guide.github.io/). |
| **Other improvements** | The original authors of LibertyCityPlates, Xbox Rain Droplets, Fusion Overloader, Project2DFX, vegetation, vehicle, traffic/popcycle, and other projects. Use their own pages for support, updates, licenses, and permissions. |
| **AMD Ryzen PRO and Radeon** | AMD owns the platform names, drivers, specifications, and trademarks. The reference hardware does not make this AMD-only. |
| **GTA IV** | Rockstar Games and its licensors. This project contains no game content. |

Keep a backup. A change that works well in one part of the city may be poor on another computer because drivers, thermals, power limits, resolution, memory bandwidth, and installed content differ.

## References

[1] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[3] [Gillian’s GTA IV Modding Guide — FusionFix](https://gillian-guide.github.io/essential-modding/fusionfix/)

[4] [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)

[5] [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)

[6] [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)

[7] [Gillian’s mod-loading guide](https://gillian-guide.github.io/extras/modloading/)

[8] [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync)

[9] [Intel Core i9-12900K specifications](https://www.intel.com/content/www/us/en/products/sku/134599/intel-core-i912900k-processor-30m-cache-up-to-5-20-ghz/specifications.html)

[10] [FusionFix issue #1079](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1079)

[11] [Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)

[12] [HQ Vanilla Textures City Revitalization](https://www.nexusmods.com/gta4/mods/781)
