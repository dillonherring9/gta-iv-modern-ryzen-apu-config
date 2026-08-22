# Start Here: GTA IV Complete Edition on a Modern PC

> **You do not need to understand modding vocabulary to use this package.** Follow the steps in order, do not add unrelated GTA IV fixes at the same time, and use the built-in rollback if you later decide not to keep this configuration.

This project exists because the original PC release has a deserved reputation for behaving poorly on newer computers. That reputation does **not** mean that GTA IV Complete Edition is unplayable today. The current FusionFix project explicitly supports Complete Edition and adds fixes and modern-system options; Gillian’s current Drag-and-Drop Archive is a maintained, ready-to-play single-player package built around that route.[1] [2]

Version 3 does not claim that every computer will run every scene perfectly. It gives a carefully bounded, reversible configuration on top of a working Complete Edition Drag-and-Drop Archive installation. The evidence, credits, original configuration sources, alternative renderer material, historical reasoning, and uninstaller path all remain available after installation.

## Before you begin

The installer is for **Windows** and a legitimate copy of **GTA IV Complete Edition (1.2.0.59)**. It is not a GTA IV download, a game crack, a multiplayer tool, a replacement for Gillian’s archive, or a generic fix for every GTA IV mod setup. FusionFix’s developers identify Complete Edition as the fully supported game version; its legacy support is different and is outside this installer’s scope.[1]

| You need | In ordinary words | How to recognize it |
|---|---|---|
| A legitimate Complete Edition installation | The current Steam or Rockstar Games Launcher version of GTA IV, including Episodes from Liberty City. | Your game folder contains `GTAIV.exe`. |
| A clean base for the archive | A game folder that is not a mixture of old random fixes, renderers, loaders, and previous modpacks. | If unsure, use your launcher’s file verification/reinstall option or make a fresh separate installation before applying the archive. Gillian specifically requires a clean copy for this archive.[2] |
| Gillian’s **Complete Edition** Drag-and-Drop Archive | The large ready-to-play archive that supplies FusionFix, its intended Vulkan route, and the compatible game-content/plugin layout. | You have extracted the archive into the same folder as `GTAIV.exe`. |
| Version 3 from this project | The small configuration installer from this release. | Its filename is `GTAIV_After_Dark_Complete_Configuration_v3.exe`. |
| Normal Windows write access | Permission for the game and installer to update their own configuration files. | The installer will configure GTAIV.exe to run as administrator for this specific setup, then remember how it was set before. |

> **Do not use this installer on a downgraded copy, a Linux/Proton installation, a multiplayer setup, or a game folder assembled from unrelated guides.** The Drag-and-Drop Archive and this configuration both assume the Windows Complete Edition route. That narrow boundary is deliberate: it avoids pretending that one set of settings can safely cover every historical GTA IV setup.

## Why it is reasonable to try this today

The strongest reason is not an unsupported claim that an old game suddenly became easy to run. It is that the modern setup has an actively maintained compatibility layer and a documented installation route. FusionFix describes its purpose as fixing GTA IV Complete Edition issues and adding features for modern systems, including an in-game choice between DirectX 9 and Vulkan.[1] Gillian’s archive is a current, deliberately assembled Complete Edition package; its author calls Complete Edition the more stable and polished option for a first vanilla-faithful playthrough.[2]

The project’s reference context is a **Ryzen 5 PRO 6650U with Radeon 660M integrated graphics and 16 GB dual-channel memory**. That makes this package especially relevant to recent Ryzen laptop and mini-PC APU owners who assume they need a dedicated graphics card before attempting GTA IV. It is a tested configuration context, **not** a hardware requirement, benchmark, or promise of a locked frame rate. Laptop power limits, cooling, RAM configuration, display resolution, driver version, background software, and the number of visual/vehicle additions can all change results.

| A realistic expectation | What it means in practice |
|---|---|
| **A modern, supported game path exists.** | Complete Edition with FusionFix and the archive is a real present-day route, not an abandoned Windows XP-only recipe.[1] [2] |
| **Integrated Radeon hardware is a reasonable class to try.** | This project supplies a cautious shared-memory APU configuration reference. Start at a sensible resolution and judge repeat play, not a single benchmark scene. |
| **The first drive may not represent normal play.** | Shader/pipeline caches can build after a new driver, renderer, resolution, setting, weather effect, or content change. Repeat the same route after restart before deciding that the setup failed. |
| **Visual completeness and maximum speed are different goals.** | Gillian explicitly says the full archive prioritizes a vanilla-faithful experience and is not its best-performance option. Do not interpret that as an installer defect.[2] |
| **You can go back.** | Version 3 backs up the seven configuration files it manages and preserves the previous Windows compatibility setting. Its uninstaller offers to restore that latest backup. |

## The complete setup, one step at a time

### Step 1 — Make sure you have the right game folder

Install **GTA IV Complete Edition** through Steam or the Rockstar Games Launcher. Open the game’s installation folder. You are in the correct place only when you can see a file named `GTAIV.exe`.

If you already tried several old GTA IV guides, do not keep piling files on top. First use your launcher’s verification/reinstall function or use a clean separate game copy. The Drag-and-Drop Archive says it must be installed on a clean copy, and it does not support adding arbitrary extra mods to its curated layout.[2]

### Step 2 — Put Gillian’s complete archive into that exact folder

Download **the Complete Edition / 1.2.0.59 archive** from Gillian’s Drag-and-Drop Archive page. Extract the archive’s contents into the folder that contains `GTAIV.exe`; do not create an extra nested folder such as `GTA IV\1.2 archive\...` and do not place the archive beside the game folder. Gillian’s documented installation step is exactly to extract the archive into the game folder.[2]

When extraction is complete, do not update FusionFix from inside the game. The archive is version-locked as a coordinated package, and its own instructions say not to enable FusionFix update checks.[2]

### Step 3 — Run the archive’s setup utility

Inside the same game folder, open `GTAIVSetupUtility.exe`. This is **not** the game; it is the archive’s setup helper.

| Click in the setup utility | What to do | Why it matters |
|---|---|---|
| **Open…** | Select the exact folder containing `GTAIV.exe`. | It tells the helper where your GTA IV installation is. |
| **Reinstall DXVK** | Click it and wait for it to complete. | It restores the archive’s intended Vulkan/DXVK files. |
| **Setup launch options** | Click it, then accept the confirmation. | It places the appropriate launcher text on the clipboard. |

The archive documentation calls this utility a failsafe for installing DXVK and setting launch options. It is not the game itself and it does not need you to understand renderer files.[2]

### Step 4 — Paste the launch options where you launch the game

After selecting **Setup launch options**, the archive copies text to Windows’ clipboard. Paste that text into **one** of the following places; choose the one you actually use to start GTA IV.

| If you use | Where to paste the copied text |
|---|---|
| **Steam** | Right-click GTA IV in your Library, choose **Properties**, then paste it into **Launch Options**. |
| **Rockstar Games Launcher** | Open the GTA IV page in your library, open **Settings**, then paste it into **Launcher arguments**. |
| **A desktop shortcut** | Right-click the shortcut, choose **Properties**, and paste the copied text at the end of the **Target** box. Do not remove the existing quoted game path. |

These are the archive’s own documented launcher paths.[2] Do not add old command lines from YouTube videos or a second set of renderer flags at the same time.

### Step 5 — Launch once and choose the archive’s graphics route

Start GTA IV through Steam, Rockstar Games Launcher, or `PlayGTAIV.exe`, as the archive instructs. In the game’s **Graphics** settings, select **Vulkan** for **Graphics API** when the archive’s Vulkan route works, then allow the game to restart if asked. FusionFix documents this as a selectable DirectX 9/Vulkan setting that requires a restart.[1]

If selecting Vulkan immediately crashes your game, follow Gillian’s recovery instruction: close the game, delete `d3d9.cfg` from the game folder, and do not keep forcing that setting while you investigate the archive/setup utility/driver path.[2] Do not add another `d3d9.dll`, `dxgi.dll`, ASI loader, frame limiter, or random “fix pack” as a reaction. Extra wrappers make the source of a problem harder to identify.

### Step 6 — Close GTA IV completely

Close GTA IV, Steam or the Rockstar Games Launcher, mod managers, and any tool that could be holding the game folder open. This lets the Version 3 installer back up and replace configuration files safely.

### Step 7 — Run the Version 3 configuration installer

Run `GTAIV_After_Dark_Complete_Configuration_v3.exe`. When it asks for a folder, choose the same folder containing `GTAIV.exe`. You do not need to browse into `plugins`, `pc`, or a Windows system folder.

The installer checks the selected game root, checks that the archive’s `vulkan.dll` is present, and refuses to run while `GTAIV.exe` is open. It intentionally does **not** demand one exact FusionFix plugin filename because a valid archive can use a different file layout or loader.

| What the installer does | What that means for you |
|---|---|
| Creates a timestamped backup | Your prior copies of the seven files this installer manages are saved before anything is replaced. |
| Installs the full archive-specific configuration set | It writes `dxvk.conf`, `pc\stream.ini`, Console Select Menu, both FusionFix files, Xbox Rain Droplets, and LibertyCityPlates settings. |
| Enables **Run as administrator** for `GTAIV.exe` | In this setup, GTA IV needs it to create required configuration files. The installer saves your prior compatibility value first. |
| Copies a complete reference package into the game folder | `GTAIV_After_Dark_Complete_Package` contains the sources, credits, plain-language guide, validation material, history, and retained reference alternatives. |
| Shows the backup location at the end | Keep this location available. It is how the uninstaller can restore the preceding configuration state. |

### Step 8 — Start playing normally

Launch the game the same way you did in Step 5. Do **not** run `GTAIV.exe` directly to bypass the launcher options; Windows will apply the administrator setting when it starts the executable through the normal game launch process.

For the first calm test, load or begin a game, drive through a normal daytime area, enter and leave one interior, then try a night/rain scene. Restart and repeat the same drive once. Look for crashes, missing textures, invisible cars, broken license plates, repeated long stutters, or uneven pacing—not just the frame-rate number in an empty area.

## First-week settings advice

The archive recommends `Traffic Density` 100 so its custom population work is fully visible.[2] On the reference APU class, start there if you want the intended archive experience, but lower it to 80 and then 70 if dense traffic is the specific source of poor frame pacing. Change **one thing**, test it, then decide. Do not begin by lowering every graphics setting or installing performance scripts.

Use FusionFix’s in-game FPS limiter as the sole limiter while testing. Do not stack Radeon Chill, RTSS, DXVK caps, or several launch tools over it. One active limiter makes troubleshooting understandable.

## If something goes wrong

| Symptom | First safe action | Do not do this first |
|---|---|---|
| The Version 3 installer says `GTAIV.exe` is missing | Go back and select the folder that visibly contains `GTAIV.exe`. | Do not select `plugins`, `pc`, `Documents`, or the archive download folder. |
| The installer says `vulkan.dll` is missing | Repeat Steps 2–4 with Gillian’s archive setup utility and correct launch options. | Do not download an unrelated `vulkan.dll` from a DLL site. |
| The installer says GTA IV is running | Close the game, launcher, mod manager, and file-copy tools, then run it again. | Do not force-copy configuration files over an open game. |
| The game fails when Vulkan is selected | Delete `d3d9.cfg` as Gillian instructs, then revisit the archive setup/driver route. | Do not add another renderer wrapper or random DXVK build. |
| Rain or dense traffic is the only slow part | Repeat the same scene after a restart; then reduce traffic density first for traffic-heavy scenes. | Do not immediately replace shaders, preload files, or the entire archive. |
| You see missing plates, a resource error, or unusual graphics after manual changes | Undo your most recent extra mod/configuration change. The archive’s shader metadata and this package’s retained preload reference must match the actual installed layout. | Do not copy the legacy static `preload.list` blindly. |
| You no longer want this configuration | Use **Installed apps** or the installer’s uninstaller, choose **Yes** when asked to restore the latest backup. | Do not delete your saves or the full game folder as the first rollback step. |

## What Version 3 changes—and what it leaves alone

The installer manages only seven configuration files and one **per-user** Windows compatibility entry for `GTAIV.exe`. It does not overwrite GTA IV archives, saves, launcher accounts, FusionFix binaries, DXVK binaries, the archive’s shaders, or unrelated mods. It also does not add the old AudioMap configuration; the full audit found that file was an unsupported carryover, not a documented part of Gillian’s archive.

The complete package intentionally retains the GPLAsync renderer alternative and a legacy static shader-preload reference for research/history. They are **not** automatically applied, because choosing a renderer or forcing shader metadata that does not match the real archive layout can create more problems than it solves.

## References

[1] [ThirteenAG, GTAIV.EFLC.FusionFix — official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[3] [Gillian’s GTA IV Modding Guide — FusionFix](https://gillian-guide.github.io/essential-modding/fusionfix/)
