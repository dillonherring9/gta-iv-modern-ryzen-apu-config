# GTA IV, After Dark — Modern Ryzen APU Configuration

## A Ryzen 5 PRO 6650U / Radeon 660M Profile for a Smoother Night in Liberty City

> **This is a community configuration package, not an original-mod claim.** I built it around the way I actually run GTA IV Complete Edition: a legitimate install, a modern mod stack, a Ryzen 5 PRO 6650U with Radeon 660M integrated graphics, and **16 GB of dual-channel memory**. The point is not to sell a miracle. The point is a cleaner ride through the city: stronger frame pacing, modern fixes, fewer ugly pauses, and a path back out if the night goes wrong.

This repository is my organized configuration, documentation, and installer layer. I did **not** create FusionFix, DXVK, Gillian’s Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Xbox Rain Droplets, Project2DFX, restored vegetation, vehicle packs, traffic/popcycle files, or any other third-party project named here. Their authors built the foundation and deserve the credit.

---

## Read this first if you know nothing about GTA IV modding

You are not expected to know what an `.ini` file, a DLL, a renderer, a shader, a loader, Vulkan, DXVK, or an APU is. This page explains the complete maintenance method because a careful beginner deserves to understand how to keep the game working, not merely press an installer button and hope.

**The only folder that matters at the beginning is the game folder.** It is the folder that visibly contains the file named `GTAIV.exe`. Do not select the `plugins` folder, the `pc` folder, your Documents folder, the archive download, or a folder with a similar name. Every instruction below that says “game root,” “game folder,” or “GTA IV root” means this exact `GTAIV.exe` folder.

| Word you may see | Plain meaning | What a first-time user should do |
|---|---|---|
| **`.ini` / `.cfg` file** | A small text file that stores settings. | Let Version 3 place its seven managed settings files. Do not edit one unless a guide tells you exactly why. |
| **DLL** | A Windows support file used by a program. | Do not download individual DLLs from random DLL websites. Use the archive/setup helper or the original author’s release page. |
| **DXVK** | A component that helps the game use Vulkan instead of its original DirectX 9 graphics path. | Use Gillian’s setup helper first. Only use the later DXVK-upgrade section after you have a working game and a saved copy of the old file. |
| **Vulkan** | The modern graphics system selected inside FusionFix/GTA IV when its setup is working. | Select it in the game only after the archive has been set up. If it crashes, follow the `d3d9.cfg` recovery step in the table below. |
| **FusionFix** | A community fix project that repairs GTA IV Complete Edition issues and adds options. | The Drag-and-Drop Archive already supplies the intended version. Do not update it casually inside this curated archive. |
| **ASI loader** | A helper that lets GTA IV load certain community plugins. | Keep one intended loader. Do not add another one because a video says it is required. |
| **Shader/cache** | Graphics work the game/driver learns and stores while you play. | Expect some first-time stutter after important changes. Restart, repeat the route, and judge the repeat drive. |

### The normal first-time route in current Version 3

| Step | Do exactly this | What success looks like |
|---|---|---|
| **1. Start clean** | Install a legitimate **GTA IV Complete Edition** copy through Steam or Rockstar Games Launcher. If you previously followed several GTA IV videos/guides, make a clean game copy first rather than mixing them together. | You can open the game folder and see `GTAIV.exe`. |
| **2. Install the foundation package** | Download Gillian’s **Complete Edition / 1.2.0.59 Drag-and-Drop Archive**. Extract the archive’s contents directly into the folder that contains `GTAIV.exe`. “Extract” means opening the downloaded archive with 7-Zip/WinRAR and copying the files inside it into the game folder—not leaving them inside an extra subfolder. | The archive’s files are beside `GTAIV.exe`. |
| **3. Run the archive helper** | In that same folder, open `GTAIVSetupUtility.exe`. Select **Open…**, choose the folder containing `GTAIV.exe`, then choose **Reinstall DXVK** and **Setup launch options**. DXVK is the archive’s graphics-translation component; you do not need to edit it at this stage. | The helper finishes and copies launch-option text to the Windows clipboard. |
| **4. Paste the copied launch options** | If you use Steam, right-click GTA IV in your Library, select **Properties**, and paste the text into **Launch Options**. If you use Rockstar Games Launcher, paste it into **Launcher arguments**. If you use a shortcut, right-click it, choose **Properties**, and paste the text at the end of **Target** without removing the existing game path. | Your normal launch method retains the text. |
| **5. Launch once** | Launch GTA IV normally. In the game’s Graphics menu, select **Vulkan** for Graphics API only when the archive’s route works, then restart if the game requests it. | The game reaches its menu or a save. If it crashes when you select Vulkan, close it, delete `d3d9.cfg` from the game folder, and return to Step 3. |
| **6. Close the game and launchers** | Close GTA IV, Steam or Rockstar Games Launcher, mod managers, and any file-copy program. | GTA IV is no longer running. |
| **7. Run this project’s installer** | Run `GTAIV_After_Dark_Complete_Configuration_v3.exe`. When it asks where GTA IV is installed, choose the folder containing `GTAIV.exe`. | The installer backs up your prior configuration and tells you where that backup is stored. |
| **8. Test instead of guessing** | Launch normally, drive through a daytime area, enter/leave one interior, then try night/rain. Restart the game and repeat the same route once before deciding whether it is good or bad. | You are judging a repeated, cache-warmed drive instead of the first minutes after a renderer/settings change. |

### What this installer does today

The current installer assumes that Gillian’s complete archive is already in the game folder. It checks that you chose the right folder, checks for the archive’s `vulkan.dll`, and refuses to alter files while GTA IV is open. It applies the following seven archive-specific configuration files: root `dxvk.conf`; `pc\stream.ini`; and `plugins\ConsoleSelectMenuIV.ini`, `GTAIV.EFLC.FusionFix.cfg`, `GTAIV.EFLC.FusionFix.ini`, `GTAIV.XboxRainDroplets.ini`, and `LibertyCityPlates.txt`.

Before changing them, it creates a timestamped backup. It also enables **Run this program as an administrator** for `GTAIV.exe`, because this target layout needs the game to create required configuration files. The uninstaller offers to restore the latest backup and restores the exact Windows compatibility setting that existed first. The installer deliberately does **not** require one exact FusionFix `.asi` filename because valid archive loader/file layouts can differ.

### Read the original guide below, but use these current Version 3 rules when they differ

Everything after this note is the preserved original maintenance guide. It is intentionally kept because it explains the logic behind a clean renderer path, cache testing, controlled DXVK/ASI-loader upgrades, troubleshooting, rollback, credits, and sources. Its words were written for an older **nine-file GPLAsync-style profile**, so the table below prevents a beginner from accidentally applying an old instruction that no longer belongs to current Version 3.

| Original-guide wording you will see below | Current Version 3 rule |
|---|---|
| “Nine managed files,” old installer filenames, or the old Version 3.0.0 backup/uninstaller names | Use `GTAIV_After_Dark_Complete_Configuration_v3.exe`. It manages **seven** files and its own timestamped complete-configuration backup. |
| `AudioMap.ini` as a package or manual-install file | Do **not** copy or recreate it. The audit found it was not a documented Drag-and-Drop Archive component. |
| A static `preload.list` manual-install destination | Keep it as reference only. Do not overwrite the archive’s shader metadata unless you fully understand and verify the matching shader layout. |
| GPLAsync values as the automatically installed renderer profile | Current Version 3 installs the upstream-compatible stock `dxvk.conf`; GPLAsync remains a preserved advanced alternative, not an automatic choice. |
| Update FusionFix inside the archive | Do not casually update FusionFix in Gillian’s curated archive and do not enable its update check. |
| The five-step DXVK upgrade that replaces `vulkan.dll` | Keep and use it only as an **advanced, controlled maintenance procedure**: save the currently working file, change only one binary, inspect `GTAIV_d3d9.log`, replay a route after caches build, and restore the old file if the game fails. It is not a required first-time step or a guaranteed performance upgrade. |

For the complete comparison, including why these rules exist, read [`docs/ORIGINAL_README_RECONCILIATION.md`](docs/ORIGINAL_README_RECONCILIATION.md). For a shorter first-time walkthrough, read [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md).

### This project is meant to be handed on responsibly

The current owner does not claim permanent support. If this project helps you and you have the patience to test, document, and preserve rollback behavior, the repository is prepared for a careful successor. Read [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md) before proposing a takeover or fork, and use [`CONTRIBUTING.md`](CONTRIBUTING.md) for reports and improvements. Users may move to a better-maintained successor project when one exists; they should make that move cleanly, with backups, rather than mixing two complete configurations in one game folder.

---

## Original maintenance guide, preserved for self-maintenance

> **Preservation note:** The original guide below is intentionally retained word-for-word so that users can learn the full logic, follow the historic maintenance procedures, retain the author’s thanks and sources, and understand what earlier versions attempted. The current Version 3 rules above take priority only where the original guide refers to old installer file counts, AudioMap, static preload installation, the former GPLAsync default, or retired installer names.


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
| [Version 3 Graphics and Performance Profile](#version-3-graphics-and-performance-profile) | The log-validated settings that stay together and the dials that move first. |
| [DXVK and Cache Reality](#dxvk-and-cache-reality) | Renderer behavior, compiler settings, caches, and restarts. |
| [Shader Preloading](#shader-preloading) | What `preload.list` is—and what it cannot do. |
| [Testing, Troubleshooting, and Rollback](#testing-troubleshooting-and-rollback) | How to isolate trouble and return to a known-good state. |
| [Credits, Boundaries, and Sources](#credits-boundaries-and-sources) | Attribution and technical references. |

---

## My Reference Setup and Result

I tested Version 3 around the system below. It is the baseline behind the words in this README, not a promise that every laptop, driver, or mod list will move the same way.

| Component | Reference setup | Why it matters |
|---|---|---|
| Processor | AMD Ryzen 5 PRO 6650U | GTA IV still spends real CPU time on traffic, streaming, scripts, weather, and a heavy mod stack. |
| Graphics | Radeon 660M integrated RDNA 2 graphics | The GPU shares system memory, so resolution, shadows, rain, post-processing, and shader compilation must stay in balance. |
| Memory | 16 GB dual-channel system memory | Dual-channel bandwidth matters on an iGPU, but Windows and the game still drink from the same pool. |
| Game | GTA IV Complete Edition, build 1.2.0.59 | This is the Complete Edition context used in Gillian’s FusionFix guidance.[^1] |
| Renderer path | FusionFix with one active DXVK/Vulkan path when stable | Stacked wrappers make crashes and stutter harder to read. |
| Frame target | 60 FPS where the scene allows it | I care about consistent frame times and real play, not a loud counter that hides a rough ride. |

On this setup, the Version 3 profile made high settings and a higher resolution feel much smoother once the cache had warmed. Traffic, weather, shader warm-up, drivers, thermals, power limits, resolution, memory speed, and extra mods can still change the result. This is a **tested starting point**, not a universal performance guarantee.

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

The Version 3 installer is **CPU-agnostic**: it validates the folder holding `GTAIV.exe`, backs up the nine managed files, and writes the same profile to the same destinations. A strong Intel CPU can run this profile. The real question is which GPU is rendering GTA IV, which driver it is using, whether DXVK/FusionFix is stable, which resolution is active, and what extra mods are in the room.

Do not add `forceIntelGraphics=true` just because it sounds like a solution. Keep Version 3 unchanged for the first run, choose the intended GPU for `GTAIV.exe` in Windows, restart after renderer changes, let caches build, and test the base story plus both EFLC episodes before deciding an Intel-specific change helped.

| Intel graphics setup | First expectation |
|---|---|
| NVIDIA or AMD discrete GPU installed | A strong fit. Use the normal installer, force the discrete GPU for `GTAIV.exe` in Windows, and test normally. |
| Intel Arc discrete GPU installed | The installer still works, but driver quality and the DXVK/FusionFix route decide the outcome. Test GTA IV, TLAD, and TBoGT separately. |
| Only Intel UHD 770 iGPU | The installer still works, but the Radeon 660M high-visual result is not guaranteed. Start at a sensible lower resolution and give caches time before reducing the heaviest settings. |

`dxvk.numCompilerThreads=4` is a conservative choice for the six-core Ryzen reference system. It is safe on a Core i9-12900K, but it is not trying to use every logical thread. Leave it at `4` for the first test. Real frame pacing after the cache settles matters more than an aggressive thread count.[^9]

---

## What This Package Is

Version 3 carries the complete Version 1 and Version 2 work forward, then returns to the thing that mattered most: the log from the machine actually driving through Liberty City. The package restores the DXVK GPLAsync values that were confirmed active and healthy on the Ryzen 5 PRO 6650U / Radeon 660M reference run.

| File | What Version 3 carries into the night |
|---|---|
| `tuned/dxvk.conf` | The log-validated DXVK GPLAsync profile: four compiler threads, four async workers, GPL cache behavior, full-screen-exclusive support, and a single-limiter path. |
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

### Optional upgrades: keep the late-night signal current

The Version 3 profile is built around the renderer and loader path that already works on the reference system. If you decide to update either binary, do it as a controlled replacement: save the current file first, change one component, restart GTA IV, and confirm the new `GTAIV_d3d9.log` before changing anything else.

#### Upgrade DXVK for FusionFix

FusionFix’s Vulkan route uses a **32-bit DXVK build renamed to `vulkan.dll`** in the GTA IV root. Do not leave the downloaded file as `d3d9.dll` beside the game when you are using FusionFix’s `API=1` Vulkan route; replace the existing `vulkan.dll` instead.[^10]

1. Download the current Windows release from the [official DXVK releases](https://github.com/doitsujin/dxvk/releases). Keep the existing working `vulkan.dll` somewhere safe first; a newer DXVK build is an experiment, not an automatic performance win.
2. Extract the archive and open its **`x32`** folder. GTA IV is a 32-bit process, so do not use the `x64` folder.
3. Copy the `x32\d3d9.dll` file to a temporary location and rename that copy to **`vulkan.dll`**.
4. Replace the existing `GTA IV\vulkan.dll` with the renamed file. Do not rename or replace `d3d9.dll` for this FusionFix path.
5. Launch GTA IV once, confirm the Vulkan/DXVK backend in `GTAIV_d3d9.log`, and replay the same route after the shader and pipeline cache has warmed. If the game stops launching, restore the backed-up `vulkan.dll` before testing another variable.

#### Upgrade Ultimate ASI Loader for FusionFix

Ultimate ASI Loader is the proxy that loads `.asi` plugins. GTA IV is 32-bit, so the correct replacement is the **x86** `dinput8.dll`, not the Win64 build.[^11]

1. Download the current release from the [official Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases).
2. Extract the archive and select the **x86** `dinput8.dll` asset.
3. Back up the existing `GTA IV\dinput8.dll`, then copy the new x86 `dinput8.dll` into the GTA IV root and replace the old one.
4. Launch the game and confirm that FusionFix and ScriptHook initialize normally. Keep one intended ASI loader only; do not add a second proxy under another DLL name unless the loader’s own documentation requires it.

> **The quiet rule:** update DXVK or the loader, not both on the same night. If the log changes, you want one reason for it.

#### Optional high-resolution vanilla textures

The [HQ Vanilla Textures City Revitalization](https://www.nexusmods.com/gta4/mods/781) project replaces selected city textures with AI-enhanced versions of the original vanilla assets. It is not a generic texture dump: the author describes choosing the enhancement method and target resolution per material and asset, with some textures at 1x, others at 2x, and others at 4x. That care is why the result can look sharper without every file being enlarged blindly.[^12]

The asset is optional, and its install path matters. While shaping this setup, I found one important difference between the base story and EFLC: the tested route is **loose-file replacement**, not an `update`-folder installation. The `update`-folder version could leave the base game running while **The Lost and Damned** or **The Ballad of Gay Tony** crashed at launch. Replacing the corresponding loose files kept EFLC alive and let the texture change appear in both episodes.

1. Download the resource from Nexus Mods and follow its own file list; do not treat it as part of the nine-file Version 3 configuration package.
2. Back up each original before replacing it, then use the mod’s **loose-file** layout in the GTA IV root. Do not move the replacement into an `update` folder for this tested EFLC route.
3. Launch the base game, The Lost and Damned, and The Ballad of Gay Tony separately. A base-story launch is not enough to call the installation healthy.
4. If the higher-resolution assets push the Radeon 660M too hard, lower output resolution one sensible step, restart GTA IV, and let the shader and pipeline caches rebuild before judging the tradeoff. A clean image with headroom is worth more than maximum pixels with no room to breathe.

### Package contents

| Package path | Manual destination | Purpose |
|---|---|---|
| `tuned/dxvk.conf` | `\dxvk.conf` | Version 3 log-validated DXVK GPLAsync renderer profile. |
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

The **GTA IV Version 3 Automatic Installer** is the clean route through the city. It places the same nine-file Version 3 configuration and `stream.ini` found in `tuned/`; it does **not** contain GTA IV, launcher files, saves, executable replacements, FusionFix/DXVK binaries, or third-party assets.

> **Version 3 is built from the working night-drive log: choose your GTA IV folder once, then let the installer handle the profile, backup, and return path.**

1. Close GTA IV, the Rockstar Games Launcher, mod managers, and every tool holding these files open.
2. Run `GTAIV_V3_Automatic_Installer_v3.0.0.exe` and approve the Windows administrator prompt.
3. The destination field starts blank. Click **Browse** and select the GTA IV game folder once, such as `C:\Program Files (x86)\Rockstar Games\GTA IV` when that folder contains `GTAIV.exe`.
4. After you click **Install**, the installer checks the selected folder and common nested GTA IV layouts. If it cannot locate the executable, it shows the full selected path and lets you return to the chooser instead of trapping you with a disabled button.
5. The installer creates a timestamped Version 3 backup, writes the nine managed files, restores the log-validated DXVK GPLAsync profile, applies the current user’s **Run as administrator** compatibility flag to `GTAIV.exe` when present, and registers its uninstaller in Windows **Installed apps**.
6. A successful Version 3 upgrade removes the obsolete Version 2 Installed-apps entry and uninstaller only after the Version 3 uninstaller exists. The old Version 2 backup is left untouched; the new Version 3 backup captures every managed file it replaces.

Backups land here before a file is overwritten:

```text
<GTA IV folder>\GTAIV_V3_Installer_Backups\YYYY-MM-DD_HH-MM-SS\
```

For a rollback, close GTA IV and the launcher, then use **Settings → Apps → Installed apps** or run `GTAIV_V3_Automatic_Installer_Uninstall.exe`. Choose **Yes** to restore the latest Version 3 backup and the prior compatibility setting. The auditable source, payload hashes, and build notes are in [`installer/`](installer/).

---

## Version 3 Graphics and Performance Profile

Version 3 is a matched after-hours set: a clean 60 FPS target, a restrained iGPU load, and a renderer profile recovered from the session that was already running right. Do not blend it into an unrelated DXVK or FusionFix profile unless you are ready to restart, replay the same demanding route, and let the new cache settle.

| Area | Profile choice | Why it stays |
|---|---|---|
| Frame pacing | FusionFix 60 FPS target with accurate sleep-yield limiter | Keeps the limiter decision in one place and leaves DXVK’s own cap off. |
| Shadows | Shadow Filter 4 with `ExtraDynamicShadows=1`; high-resolution shadows off | Preserves city depth without opening the most expensive shadow path. |
| Post-processing | Ambient occlusion, sun shafts, and volumetric fog off in the tracked profile | Saves shared iGPU headroom for the road, traffic, and stability. |
| Rain | `MaxDrops=900`, `MaxMovingDrops=1800` | Holds back weather-particle work before cutting the after-dark look entirely. |
| Shader loading | Complete supplied preload list | Avoids missing-resource errors when every matching shader is present. |
| Shared-memory approach | Log-validated DXVK GPLAsync memory, pipeline, latency, fullscreen, compiler, and async-worker settings | Keeps the actual working renderer route while respecting a shared-memory APU. |

If dense traffic still feels heavy after the cache is warm, lower the in-game traffic-density slider from `100` to `80` before touching texture quality. If rain is the issue, try `MaxMovingDrops=1400` first. If shadows are still taking their tax, try `ExtraDynamicShadows=0` as the second-stage fallback.

---

## DXVK and Cache Reality

`dxvk.conf` only matters when a matching DXVK/Vulkan wrapper is active. DXVK translates GTA IV’s Direct3D 9 calls to Vulkan, so it can influence shader compilation, pipeline caching, latency, memory reporting, and presentation. It does nothing when the game is running on native Direct3D 9. **Version 3 restores the GPLAsync values that `GTAIV_d3d9.log` reported as active on the tested Radeon 660M session; it does not pretend those fork-specific keys belong to every DXVK build.**

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

## Hardware and renderer compatibility

The package is not universally incompatible with hardware, but several settings have real boundaries. The important distinction is whether a setting **cannot work on a given path** or merely asks that hardware to do more work. Version 3 was validated on a Ryzen 5 PRO 6650U with Radeon 660M graphics, 16 GB dual-channel memory, Complete Edition 1.2.0.59, FusionFix 5.0.1, and the DXVK GPLAsync-style renderer path shown in the live log. Treat that as the tested center point, not a promise that every PC behaves identically.

| Area | Hard compatibility boundary | Performance-sensitive, not automatically incompatible |
|---|---|---|
| Game build | The tracked FusionFix profiles target GTA IV Complete Edition 1.2.0.59. Do not assume they are drop-in compatible with a downgraded or different executable. | Resolution, traffic, view distance, reflections, and water scale with the scene and the hardware. |
| Graphics API | `GraphicsAPI=1` and `d3d9.cfg` `API=1` select the Vulkan route. It requires a working Vulkan-capable driver and the matching DXVK/FusionFix path. If Vulkan will not launch or remains unstable, use the documented native D3D9 fallback instead.[^1] [^5] | Vulkan may be faster and smoother on one driver while native D3D9 is more stable on another. This is a renderer choice, not a universal GPU guarantee. |
| DXVK build | `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads` are build-specific. They belong with the GPLAsync-style path recorded in the log; stock upstream DXVK may report them as unknown or ignore them.[^2] | `dxvk.numCompilerThreads=4` and `d3d9.maxFrameLatency=1` affect CPU work and pacing. Lower compiler threads or test another latency value only after warm-cache runs. |
| Vulkan capability | A GPU/driver without a working Vulkan implementation cannot use this DXVK profile. Do not force a Vulkan DLL onto that system. | Pipeline-library support can remain `Auto`; the driver decides whether the feature is useful. |
| Integrated/shared-memory graphics | `d3d9.deviceLocalConstantBuffers=False` is intentional for the Radeon 660M shared-memory path. Do not treat `d3d9.maxAvailableMemory=4096` as reserved VRAM; it is only a budget hint. | 4K-style textures, high-resolution shadows, AO, volumetric fog, sun shafts, rain density, and heavy reflections consume more bandwidth and shared memory. Lower them before changing the renderer. |
| Content and preload files | `preload.list` requires every named shader to exist in the expected directory. Missing resources can cause invalid-resource or loading failures; the list is not a general mod loader.[^3] [^4] | The supplied audio map, console selector, plates, and rain files are content/visual choices, not GPU-vendor requirements. |
| Loader and file precedence | Use one intended ASI loader, one renderer wrapper, and one compatible loose/update-file route. Stacked `dinput8.dll`, DXVK wrappers, or conflicting precedence layers are installation conflicts, not hardware limitations.[^7] | Vehicle budgets, extended limits, high-resolution loose textures, and large streaming profiles may increase memory pressure or expose mod-specific audio/content issues without being universally incompatible. |

For the reference Radeon 660M, keep the shipped `4` compiler threads, `4` async workers, SMAA, 60 FPS FusionFix limiter, high-resolution shadows off, AO/fog/sun shafts off, and reduced rain as the first baseline. On a discrete GPU with more dedicated VRAM, the post-processing, reflection, shadow, and texture ceilings may move upward, but the GPLAsync keys still depend on the renderer build rather than the GPU brand.

If a system fails before reaching the menu, first isolate the **hard path**: restore the last working Vulkan/DXVK DLL, remove a conflicting wrapper or loader, verify the game build, and test native D3D9. If it launches but stutters or loses frame time, treat it as a **pressure problem**: let caches warm, then reduce one visual or streaming load at a time. Keep external frame caps disabled until the base profile is stable. If Alt+Tab or window behavior breaks on the active DXVK build, test `dxvk.allowFse=false` first.

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

If you used the Version 3 installer, close GTA IV and the launcher, then run `GTAIV_V3_Automatic_Installer_Uninstall.exe` or use **Installed apps**. Choose **Yes** to restore the newest Version 3 installer backup. That returns the prior copies of the nine managed files and restores the previous `GTAIV.exe` compatibility setting.

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
[^10]: [FusionFix issue #1079](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1079) — FusionFix’s Vulkan/DXVK placement context and its `vulkan.dll` naming path.
[^11]: [Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) — official releases, x86/x64 proxy assets, and installation naming guidance.
[^12]: [HQ Vanilla Textures City Revitalization](https://www.nexusmods.com/gta4/mods/781) — original vanilla texture replacements, selective AI enhancement, and asset-resolution context.
