# GTA IV, After Dark — Version 3

## A practical modern-PC path for GTA IV Complete Edition

> **GTA IV is still playable on a modern Windows PC.** This project gives newcomers a clear, reversible configuration path for **GTA IV Complete Edition** after Gillian’s complete Drag-and-Drop Archive has been installed. It is designed for people—including recent Ryzen APU owners—who have heard that the PC version is impossible to run well and want a sensible way to try it without assembling a pile of conflicting fixes.

The reason this is worth trying is concrete: the current [FusionFix project](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) supports GTA IV Complete Edition and supplies modern-system fixes plus a selectable Vulkan route, while Gillian’s current Drag-and-Drop Archive is a documented, ready-to-play Complete Edition package.[1] [2] Version 3 adds a carefully audited configuration layer, backups, rollback, plain-language guidance, sources, credits, and historical reasoning. It is **one complete cumulative package**, not a patch or a modpack redistribution.

This package’s reference context is a **Ryzen 5 PRO 6650U / Radeon 660M integrated GPU with 16 GB dual-channel memory**. That is useful proof that recent shared-memory AMD graphics are a realistic class of hardware to try; it is **not** a promise of identical results, a universal 60 FPS claim, or a requirement to own that exact computer. Cooling, laptop power mode, memory bandwidth, resolution, driver version, and added content all matter. Gillian also makes clear that the full archive prioritizes a vanilla-faithful experience rather than the maximum possible performance.[2]

## Start here: the whole setup in plain English

You do **not** need to understand “DXVK,” “ASI,” “shader,” or “renderer” before starting. The detailed guide, with screenshots-free click-by-click explanations and recovery steps, is [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md).

| Step | Do this | You know it worked when |
|---|---|---|
| **1. Install the base game** | Install a legitimate **GTA IV Complete Edition** through Steam or Rockstar Games Launcher. Begin with a clean copy, not a mixture of old GTA IV guides. | You can find `GTAIV.exe` in the game’s installation folder. |
| **2. Install Gillian’s archive** | Download the **Complete Edition / 1.2.0.59** Drag-and-Drop Archive and extract its contents directly into the same folder as `GTAIV.exe`.[2] | The game folder now contains the archive’s files, including its intended Vulkan route. |
| **3. Run the archive helper** | Open `GTAIVSetupUtility.exe`, select the same game folder, choose **Reinstall DXVK**, then **Setup launch options**. | The helper finishes and copies launch-option text to your clipboard. |
| **4. Paste those launch options** | Paste them into Steam’s **Launch Options**, Rockstar Launcher’s **Launcher arguments**, or the end of your shortcut’s **Target**, depending on how you launch the game.[2] | Your chosen launcher/shortcut retains the copied text. |
| **5. Launch once** | Start GTA IV normally and select **Vulkan** under Graphics API when the archive’s route works; restart if prompted.[1] | The game launches and reaches its menu or a save. |
| **6. Close everything** | Close GTA IV, its launcher, mod managers, and file-copy tools. | `GTAIV.exe` is no longer running. |
| **7. Run Version 3** | Start `GTAIV_After_Dark_Complete_Configuration_v3.exe` and select the folder that contains `GTAIV.exe`. | The installer shows a backup location and completes without a prerequisite warning. |
| **8. Test calmly** | Launch normally again, drive a repeatable route, restart once, and repeat it. | You judge repeat play and frame pacing—not only the first few minutes or an empty area. |

> **If the archive’s Vulkan option crashes:** close the game, delete `d3d9.cfg` in the game folder, and revisit the archive setup/driver path as Gillian instructs.[2] Do not respond by adding another renderer wrapper, random DLL download, second ASI loader, or several frame limiters.

## What Version 3 actually installs

The installer is for a pre-existing **Windows Complete Edition Drag-and-Drop Archive** installation. It checks that you chose a GTA IV game root, verifies the archive’s `vulkan.dll`, and refuses to change files while GTA IV is running. It does not require one brittle, exact FusionFix plugin filename because valid archive layouts can differ.

| Installed location | Version 3 file | Why it is there |
|---|---|---|
| Game root | `dxvk.conf` | The upstream-compatible DXVK baseline for the archive’s Vulkan route. |
| `pc\` | `stream.ini` | The archived streaming configuration profile. |
| `plugins\` | `ConsoleSelectMenuIV.ini` | Complete Edition Console Select Menu setting. |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` | Main FusionFix graphics/frame-pacing profile. |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix profile. |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets setting. |
| `plugins\` | `LibertyCityPlates.txt` | LibertyCityPlates setting. |

Before changing those seven files, the installer creates a timestamped backup. It also sets **Run this program as an administrator** for `GTAIV.exe`, because this documented configuration needs the game to create its required configuration files. The uninstaller offers to restore the most recent backup of all seven files and restores the exact Windows compatibility setting that existed before installation.

## Honest limits: what this project can and cannot do

| This project **can** do | This project **cannot** promise or do |
|---|---|
| Give a reproducible Windows Complete Edition configuration path built on current FusionFix/Drag-and-Drop Archive documentation. | Guarantee a specific frame rate, eliminate every stutter, or make every laptop setting equivalent. |
| Provide an APU-conscious reference profile and a way to test changes one at a time. | Make a low-power/cooling-limited system, single-channel memory system, heavy visual mod stack, or unsupported game version behave like the reference context. |
| Back up and restore the exact seven managed configuration files and the prior GTAIV.exe compatibility value. | Undo unrelated mods, game-file changes, driver changes, save corruption, or launcher/account problems. |
| Preserve the project’s evidence, credits, source files, history, and reference alternatives in `GTAIV_After_Dark_Complete_Package`. | Redistribute GTA IV, the archive, FusionFix/DXVK binaries, shaders, launcher authentication, or third-party mods. |
| Keep the archive’s configuration coherent. | Support arbitrary extra mods, legacy/downgraded copies, Linux/Proton, or multiplayer with this installer. |

The package deliberately does **not** install the old `AudioMap.ini`. The full audit found it was an unsupported DualSense Audio Mapper carryover rather than a documented Drag-and-Drop Archive component. It remains mentioned only in historical records for transparency.

The GPLAsync renderer alternative and a legacy shader-preload reference remain in the complete reference package, with their sources and reasoning. They are not automatically overwritten, because they require a distinct renderer artifact or a shader layout that exactly matches the files in the user’s archive. That restraint is a reliability feature, not missing content.

## First-time troubleshooting and rollback

| If you see this | Begin here |
|---|---|
| Installer cannot find `GTAIV.exe` | Select the game folder itself—not `plugins`, `pc`, `Documents`, the archive download, or a parent folder that merely contains the installation. |
| Installer says `vulkan.dll` is missing | Repeat the archive installation and `GTAIVSetupUtility.exe` steps. Never download a standalone DLL from an untrusted DLL site. |
| Installer says GTA IV is running | Close the game, Steam/Rockstar Launcher, mod managers, and tools holding the folder open; then run it again. |
| Dense traffic is slow | Repeat the same route after a restart, then lower in-game Traffic Density from 100 to 80 before changing many other settings. |
| Rain is slow | Repeat a rainy route after a restart; change one variable at a time rather than replacing renderer/shader files. |
| You want to return to the prior state | Use the installer entry in **Installed apps** (or run the uninstaller), then choose **Yes** to restore the latest complete-configuration backup. Do not delete saves or the game folder first. |

## Complete documentation and evidence

| Document | Purpose |
|---|---|
| [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md) | The full beginner setup, success checks, recovery guidance, realistic modern-APU expectations, and rollback path. |
| [`RELEASE_NOTES.md`](RELEASE_NOTES.md) | What the current Version 3 release does, its prerequisite boundary, and its verification status. |
| [`docs/COMPLETE_INSTALL_CONTRACT.md`](docs/COMPLETE_INSTALL_CONTRACT.md) | The audited seven-file installer contract and AudioMap removal rationale. |
| [`docs/COMPONENTS_AND_COMPATIBILITY.md`](docs/COMPONENTS_AND_COMPATIBILITY.md) | Renderer, plugin, component, and shader-layout boundaries. |
| [`docs/BEGINNER_GUIDANCE_EVIDENCE.md`](docs/BEGINNER_GUIDANCE_EVIDENCE.md) | The source-backed claims used in the beginner explanation. |
| [`docs/PROJECT_HISTORY.md`](docs/PROJECT_HISTORY.md) | Retained versions, decisions, credits, and correction history. |
| [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md) | A disciplined method for evaluating repeatability and performance claims. |

## References

[1] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[3] [Gillian’s GTA IV Modding Guide — FusionFix](https://gillian-guide.github.io/essential-modding/fusionfix/)
