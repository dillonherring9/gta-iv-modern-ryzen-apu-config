# GTA IV After Dark — Version 4 Modern APU Configuration

> **A complete, reversible configuration installer for GTA IV Complete Edition after Gillian’s Complete Edition Drag-and-Drop Archive is already in the game folder.** Version 4 helps a new player choose a sensible graphics starting point, preserves the complete archive-specific configuration set, and leaves a clear path to maintain or replace the setup later.

This project is **not a new modpack**, does not include GTA IV or third-party mod binaries, and does not promise a fixed FPS number. It is a configuration, backup, rollback, documentation, and handoff layer built around the current Complete Edition archive route.[1] [2]

## Read this first

GTA IV can work on modern Windows PCs, including capable integrated graphics, but it has to be approached as an older game with a modern compatibility stack. The archive gives you the game/mod foundation; FusionFix modernizes the Complete Edition experience and exposes most graphics choices in its in-game settings; DXVK can use Vulkan where that route already works.[1] [2]

> **One folder, one renderer, one loader, one frame limiter.** Do not combine several “complete” GTA IV guides, renderer wrappers, ASI loaders, or limiter tools at the same time.

| If you are… | Read this |
|---|---|
| New to GTA IV modding | **Quick setup** below, from top to bottom. |
| Unsure which installer choice is right | **Choose your Version 4 profile**. |
| Adjusting graphics after installation | **Set your FusionFix menu starting point**. |
| Using Radeon 660M and deliberately installed GPLAsync `v3.0-1` | [`docs/GPLASYNC_V3_0_1_MIGRATION.md`](docs/GPLASYNC_V3_0_1_MIGRATION.md) before choosing the advanced installer option. |
| Updating a renderer, loader, or textures manually | [`docs/COMPLETE_MAINTENANCE_REFERENCE.md`](docs/COMPLETE_MAINTENANCE_REFERENCE.md), then test one change at a time. |
| Taking over or forking the project | [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md), [`CONTRIBUTING.md`](CONTRIBUTING.md), and [`docs/V4_AUDIT_ADOPTION.md`](docs/V4_AUDIT_ADOPTION.md). |
| Looking for the preserved Version 3 public documents | [`docs/history/README_VERSION_3_FINAL.md`](docs/history/README_VERSION_3_FINAL.md) and [`docs/history/RELEASE_NOTES_VERSION_3_FINAL.md`](docs/history/RELEASE_NOTES_VERSION_3_FINAL.md). The `v3` tag/release is unchanged. |

## The one folder that matters

The **game folder** is the folder that contains `GTAIV.exe`. It might look like:

```text
C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto IV
```

Do not choose `plugins`, `pc`, Documents, Downloads, or an extra folder created while extracting an archive. The installer checks for `GTAIV.exe` and will stop safely if you select the wrong place.

| Word | Simple meaning |
|---|---|
| **FusionFix** | The Complete Edition fix/feature project that adds modern options, including a Graphics API selection and in-game graphics controls. |
| **DXVK / Vulkan** | The optional modern graphics route that translates GTA IV’s original Direct3D 9 calls to Vulkan. |
| **`vulkan.dll`** | The renderer file used by FusionFix’s Vulkan route. Version 4 checks that it exists but never downloads, replaces, or guesses which renderer it is. |
| **Cache warm-up** | First runs after a renderer/driver/shader change can stutter while work is compiled. Judge the second and later repeat runs, not the first minute. |
| **Rollback** | Restoring the files from the installer’s timestamped backup if a result is worse. |

## Quick setup

| Step | Do this | Success looks like |
|---:|---|---|
| **1** | Install a legitimate **GTA IV Complete Edition** copy through Steam or Rockstar Games Launcher. | You can open the folder containing `GTAIV.exe`. |
| **2** | Extract Gillian’s **Complete Edition / 1.2.0.59 Drag-and-Drop Archive** directly into that game folder. Start from a clean game copy; do not install it on a downgraded copy.[2] | Archive files sit beside `GTAIV.exe`, not inside an extra folder. |
| **3** | Launch the game once. If the archive Vulkan route is not working, use the included `GTAIVSetupUtility.exe`: select the game folder, then use **Reinstall DXVK** and **Setup launch options**. It is a recovery/failsafe tool, not something a working install must run again just to satisfy this guide.[2] | GTA IV reaches the menu or a save. |
| **4** | In Graphics settings, choose **Vulkan** only when that route works. If Vulkan crashes, close the game, delete `d3d9.cfg` from the game folder, and do not add random DLLs. | The game launches normally through your usual launcher. |
| **5** | Close GTA IV and anything holding the game files open. Run `GTAIV_After_Dark_Complete_Configuration_v4.exe`, then choose the folder containing `GTAIV.exe`. | The installer accepts the folder and offers a configuration choice. |
| **6** | Choose the **General modern APU** profile unless you have independently verified GPLAsync `v3.0-1` in `GTAIV_d3d9.log`. | The installer creates a timestamped backup before it changes files. |
| **7** | Let the installer finish. It sets `GTAIV.exe` to run as administrator because the archive route needs the game to create configuration files. | The final page gives the backup and reference-package locations. |
| **8** | Apply the menu starting point below, restart GTA IV fully, then test day traffic, an interior, and night/rain twice. | Judge the second, cache-warmed route. |

## Choose your Version 4 profile

Both choices install the same seven complete-archive configuration files, preserve the same backups/rollback, and set/restore the same administrator compatibility value. They differ **only** in the root `dxvk.conf` that the installer writes.

| Installer choice | Choose it when | What Version 4 writes |
|---|---|---|
| **General modern APU** — default | You use a modern AMD/Intel integrated GPU and have not confirmed the exact renderer, or the archive uses stock DXVK, DXVK-Sarek, or another non-GPLAsync route. | An upstream-compatible DXVK baseline with no fork-only settings. |
| **Radeon 660M / GPLAsync `v3.0-1`** — advanced | Your hardware is the Ryzen 5 PRO 6650U/Radeon 660M reference context **and** `GTAIV_d3d9.log` confirms the separate GPLAsync `v3.0-1` renderer already installed as `vulkan.dll`. | The same baseline plus `dxvk.enableAsync = true`. It deliberately omits the removed `dxvk.gplAsyncCache` key and older undocumented async-thread key. |

> **Do not choose Radeon 660M / GPLAsync merely because you have AMD graphics.** It is not automatic hardware detection and does not install GPLAsync. If uncertain, choose **General modern APU**. You can later restore the backup and make a controlled renderer/profile experiment.

## What Version 4 installs

Version 4 manages the full documented Complete Edition archive configuration set. Before replacing anything, it backs up all seven files. Its uninstaller can restore those files and the exact prior per-user Windows compatibility value.

| Location | Managed file |
|---|---|
| Game folder | `dxvk.conf` — selected General or advanced Radeon profile |
| `pc\` | `stream.ini` |
| `plugins\` | `ConsoleSelectMenuIV.ini` |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` |
| `plugins\` | `LibertyCityPlates.txt` |

The installer validates `GTAIV.exe`, requires an existing `vulkan.dll`, refuses to run while `GTAIV.exe` is open, does **not** require one brittle FusionFix `.asi` filename, and never installs `AudioMap.ini`, a static `preload.list`, a renderer binary, or unverified GPLAsync settings.

## Set your FusionFix menu starting point

FusionFix states that most options are available in its in-game settings, while the `.ini` and `.cfg` files remain useful for settings outside the menu or advanced maintenance.[1] Version 4 preserves the archive configuration files and then gives you a **menu-first starting point** rather than inventing an undocumented complete `.ini` profile.

Open the FusionFix in-game settings using the control shown by **your installed FusionFix build**. Do not assume a video’s keybind is universal. Apply the following as a first test, then fully restart GTA IV.

| Area | General modern APU starting point | Radeon 660M reference starting point |
|---|---|---|
| Graphics API | Vulkan only when the archive route already works. | Same. |
| Frame limiter | 60 FPS in FusionFix; leave DXVK’s limiter off. | Same. |
| Anti-aliasing | SMAA. | SMAA. |
| High-resolution shadows | Off. | Off. |
| Ambient occlusion, sun shafts, volumetric fog | Off first; restore only after stable warm-cache testing. | Off first. |
| Shadow filter | Soft; test visual preference after stability. | Soft as the reference starting point. |
| Extra dynamic shadows | Start conservatively; turn down first if warm frame times spike. | Start at the menu’s lower/vegetation-focused option; do not chase the heaviest option without margin. |
| Reflection MSAA quality | Off/low as a conservative first test. | Off/low. |
| Traffic density | Keep the archive’s intended setting first. If dense traffic still stutters after warm runs, reduce from `100` to `80` before cutting texture quality. | Same. |
| Rain droplets | Reduce before texture quality if rain is the bottleneck. | Lower moving droplets from the archive setting before other image-quality cuts. |

Use exactly **one** frame limiter. Do not stack FusionFix’s limiter, DXVK’s limiter, Radeon Chill, RTSS, and another external limiter together.

## Test, recover, and maintain

After an install or a graphics change, drive the same route in dense daylight traffic, enter and leave an interior, then test night/rain. Restart once and repeat. If something is worse, restore the latest Version 4 backup before changing another setting.

| Symptom | First safe response |
|---|---|
| Vulkan crashes | Close GTA IV, delete `d3d9.cfg`, and return to the archive’s documented Vulkan setup. Do not add a second wrapper. |
| First-drive stutter | Restart and repeat the same route. Do not judge a cold shader/cache pass as permanent. |
| Warm-run traffic stutter | Reduce Traffic Density from `100` to `80` before lowering texture quality. |
| Warm-run rain stutter | Reduce rain/moving-droplet settings before changing several other options. |
| Radeon GPLAsync choice shows unsupported keys | Stop copying old profiles. Read [`docs/GPLASYNC_V3_0_1_MIGRATION.md`](docs/GPLASYNC_V3_0_1_MIGRATION.md), save the log, and restore the General backup if needed. |
| You want to switch profiles | Uninstall Version 4 and restore its backup, then reinstall and make one different choice. |

## Important boundaries and continuity

Gillian’s guide remains the required archive baseline, but its site is in minimal-maintenance mode and says future archive updates may be sparse. Version 4 records that dependency honestly; it does not claim to replace Gillian, FusionFix, DXVK, or their support channels.[2]

This project welcomes a careful successor and supports safe migration to a better-maintained route later. It preserves its sources, audit adoption record, current renderer boundaries, old Version 3 documents, and all retained history in the installer reference package and complete source archive. See [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md).

Version 3 remains available unchanged as the previous complete release: [Version 3 release](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases/tag/v3).

## References

[1] [FusionFix configuration guidance](https://gillian-guide.github.io/essential-modding/fusionfix/)

[2] [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
