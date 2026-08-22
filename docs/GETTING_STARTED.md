# Start Here: Version 4 on a Modern PC

> **You do not need prior GTA IV modding knowledge.** Follow these steps in order, add nothing unrelated while testing, and keep the installer backup until you know the result is stable.

Version 4 is for **Windows GTA IV Complete Edition (1.2.0.59)** after Gillian’s Complete Edition Drag-and-Drop Archive is already installed in the folder containing `GTAIV.exe`. It is not a GTA IV download, crack, multiplayer tool, Linux/Proton guide, generic old-mod bundle, or replacement for Gillian’s archive.[1] [2]

The separate Version 3 start-here guide is preserved at [`history/GETTING_STARTED_VERSION_3_FINAL.md`](history/GETTING_STARTED_VERSION_3_FINAL.md). The public Version 3 release remains unchanged.

## What you need

| You need | In ordinary words | How to recognize it |
|---|---|---|
| Legitimate Complete Edition | The current Steam or Rockstar Games Launcher GTA IV with Episodes from Liberty City. | The game folder contains `GTAIV.exe`. |
| A clean archive base | A game folder that is not a mixture of old renderers, loaders, guides, and modpacks. | Start from a clean copy or launcher-verified install before extracting the archive. |
| Gillian’s **Complete Edition** archive | The ready-to-play 1.2.0.59 archive that provides the intended FusionFix/content layout. | Its files are extracted directly into the game folder. |
| Version 4 installer | `GTAIV_After_Dark_Complete_Configuration_v4.exe`. | The release package also contains its SHA-256 manifest and source archive. |
| Normal Windows write access | Permission for the game and installer to update configuration files. | Version 4 sets `GTAIV.exe` to run as administrator and records the previous setting. |

## Why trying GTA IV today is reasonable

FusionFix supports GTA IV Complete Edition and provides modern settings, including a selectable DirectX 9/Vulkan graphics route. Gillian’s archive is a documented Complete Edition route, though its guide is in minimal-maintenance mode and says future updates may be sparse.[1] [2]

The project’s Radeon 660M context is a **reference**, not a promise. A Ryzen 5 PRO 6650U/Radeon 660M with 16 GB dual-channel memory shows that a recent APU is worth trying, but resolution, driver version, laptop power limits, cooling, RAM configuration, background software, and added content can change results. Judge repeat, cache-warmed play—not an empty-scene FPS screenshot.

## Install it in order

### Step 1 — Locate the game folder

Open the GTA IV installation folder through Steam or Rockstar Games Launcher. You are in the correct place only when you can see `GTAIV.exe`. Do not select `plugins`, `pc`, Documents, Downloads, or an extra archive folder.

### Step 2 — Extract Gillian’s Complete Edition archive

Download the **Complete Edition / 1.2.0.59** Drag-and-Drop Archive and extract its contents directly into the folder containing `GTAIV.exe`. Do not install it on a downgraded copy and do not combine it with unrelated complete guides. Gillian says the archive must be installed on a clean copy.[2]

Do not enable FusionFix’s update checker inside the archive. The archive is a coordinated set of versions, and Version 4 does not authorize arbitrary component updates.

### Step 3 — Get the archive Vulkan route working

Launch GTA IV once through Steam, Rockstar Games Launcher, or `PlayGTAIV.exe`. In Graphics settings, choose **Vulkan** only when that route works. If Vulkan crashes, close the game, delete `d3d9.cfg` from the game folder, and do not stack another graphics wrapper on top.[2]

If the archive Vulkan route or launcher arguments need repair, use the included `GTAIVSetupUtility.exe`:

| Setup Utility action | What to do |
|---|---|
| **Open…** | Select the game folder containing `GTAIV.exe`. |
| **Reinstall DXVK** | Use when the archive Vulkan route needs repair. It is a documented failsafe, not a mandatory repeat step for an already working installation. |
| **Setup launch options** | Use when your launcher arguments need repair. Paste the copied text into Steam Launch Options, Rockstar Launcher arguments, or the end of a shortcut Target field. |

### Step 4 — Close GTA IV, then run Version 4

Close GTA IV and any mod manager or tool holding the game folder open. Run `GTAIV_After_Dark_Complete_Configuration_v4.exe` and choose the folder containing `GTAIV.exe`.

The installer checks that game root, requires the archive’s existing `vulkan.dll`, and refuses to run while `GTAIV.exe` is open. It intentionally does **not** demand one exact FusionFix plugin filename because valid archive layouts can differ.

### Step 5 — Choose the renderer configuration

| Installer choice | Pick it when | Do not pick it when |
|---|---|---|
| **General modern APU** — recommended | You are unsure which renderer you have, use stock DXVK/DXVK-Sarek, or use any modern integrated GPU without a confirmed GPLAsync renderer. | Never a problem when uncertain; it is the safe default. |
| **Radeon 660M / GPLAsync `v3.0-1`** — advanced | `GTAIV_d3d9.log` already confirms the separate GPLAsync `v3.0-1` renderer, and you deliberately want that experiment. | You merely have AMD graphics, only saw `vulkan.dll`, or copied an old GPLAsync config. |

Version 4 never installs or replaces `vulkan.dll`. The advanced option writes only `dxvk.enableAsync = true`; it does not use the removed `dxvk.gplAsyncCache` key or an old undocumented async-thread setting.

### Step 6 — Understand the backup and administrator step

Before replacing any file, Version 4 creates a timestamped backup of all seven managed files. It also preserves the current per-user Windows compatibility value, adds `RUNASADMIN` for `GTAIV.exe`, and records both for uninstall/rollback. This is required because this setup needs GTA IV to create its configuration files.

| Version 4 manages | Location |
|---|---|
| Selected `dxvk.conf` | Game folder |
| `stream.ini` | `pc\` |
| Console Select Menu, FusionFix `.cfg`/`.ini`, Xbox Rain Droplets, LibertyCityPlates | `plugins\` |

It does **not** install AudioMap, a static `preload.list`, a DLL, or a second renderer.

### Step 7 — Apply the FusionFix menu starting point

FusionFix exposes most graphics choices in its in-game settings. Use the control shown by your installed FusionFix build; do not assume a random video’s keybind is correct. Apply these conservative starting choices, then **fully restart GTA IV**.

| Setting | Start here |
|---|---|
| Graphics API | Vulkan only when the archive route works. |
| Frame limiter | 60 FPS in FusionFix. Leave DXVK’s limiter off. |
| Anti-aliasing | SMAA. |
| High-resolution shadows | Off. |
| Ambient occlusion, sun shafts, volumetric fog | Off at first. |
| Shadow filter | Soft. |
| Reflection MSAA | Off/low. |
| Traffic density | Start with the archive’s intended `100`; reduce to `80` only after warm testing identifies dense traffic as the problem. |
| Rain droplets | Lower these before lowering texture quality if rain is the specific bottleneck. |

Use **one** frame limiter. Do not stack the FusionFix limiter, DXVK cap, Radeon Chill, RTSS, and other external caps.

### Step 8 — Test properly

Load a save or start a game. Drive a daytime traffic route, enter/exit an interior, then test night/rain. Restart GTA IV and repeat the same route before judging it. First runs after renderer, driver, weather, resolution, or graphics changes can build caches.

## If something goes wrong

| Symptom | First safe action |
|---|---|
| Installer says `GTAIV.exe` is missing | Select the folder that visibly contains `GTAIV.exe`. |
| Installer says `vulkan.dll` is missing | Repair the archive Vulkan route with Gillian’s documented tools; never download a random DLL from a DLL site. |
| GTA IV is running | Close the game and tools that hold its files, then retry. |
| Vulkan fails | Close GTA IV, delete `d3d9.cfg`, then repair the archive route instead of adding another wrapper. |
| Dense traffic is slow after a warm repeat route | Lower Traffic Density from `100` to `80`, then retest before cutting texture quality. |
| Rain is slow after a warm repeat route | Reduce rain/moving-droplet settings first. |
| GPLAsync profile shows unsupported keys | Read [`GPLASYNC_V3_0_1_MIGRATION.md`](GPLASYNC_V3_0_1_MIGRATION.md), save `GTAIV_d3d9.log`, and restore the General backup if needed. |
| You want to undo Version 4 | Use Installed apps or the Version 4 uninstaller and choose to restore the latest Version 4 backup. |

## Keep the evidence

The installer places a complete reference package beside the game. It contains the source/credit record, current and historical documentation, renderer boundaries, audit adoption decision, validation protocol, handoff guide, and the readable material from the user-supplied audit. Do not delete it until you have finished testing.

## References

[1] [FusionFix upstream project](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
