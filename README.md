# Vice City Actual Next Gen CE — Build 264 (Now with 64-Bit Rendering)

**Play Grand Theft Auto: Vice City inside Grand Theft Auto IV.** Tommy Vercetti's story and the whole neon-lit 1986 city, rebuilt with GTA IV's lighting, physics and Euphoria animation, HD artwork, extra islands, the classic side activities and restored dialogue. It comes as one download that you copy into your own copy of **GTA IV: The Complete Edition**.

**Complete mod package** · for **GTA IV: The Complete Edition 1.2.0.59** · download **Build264.zip** from [Releases](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases/latest)

- [What this is](#what-this-is)
- [What makes this version different](#what-makes-this-version-different)
- [64-bit rendering, explained](#64-bit-rendering-explained)
- [What you need](#what-you-need)
- [Installation, step by step](#installation-step-by-step)
- [Controls](#controls)
- [What's in the game](#whats-in-the-game)
- [Settings](#settings)
- [Saves](#saves)
- [Troubleshooting](#troubleshooting)
- [Uninstalling](#uninstalling)
- [Source code](#source-code)
- [Credits](#credits)

> **Adult content:** this is a mature GTA game. The Pole Position club dancers, and the stripper in the club's purchase cutscene, are nude.

## What this is

**Grand Theft Auto: Vice City** is Rockstar's 2002 game. It's set in a city based on 1980s Miami, and you play Tommy Vercetti, who builds a criminal empire after a drug deal goes wrong.

**Grand Theft Auto IV** is Rockstar's 2008 game. It has a much newer engine, with realistic lighting, weighty vehicle physics and **Euphoria**, which makes characters stumble, fall and react to hits instead of playing canned animations. **The Complete Edition** (often shortened to **CE**) is the version sold today on Steam and the Rockstar Games Launcher. It includes GTA IV and its *Episodes from Liberty City*. This package targets version **1.2.0.59**.

A **total conversion** replaces a game's world and story with another one. This mod swaps GTA IV's Liberty City for Vice City and its missions, characters, vehicles, radio and sound, all running in GTA IV's engine.

**Vice City Actual Next Gen CE** (short name **VCANGCE**) is a complete, ready-to-play package of that conversion. It combines:

- the Vice City conversion and the large community modpack built on it, with HD vehicles, characters and textures, extra areas such as the Everglades and Gator Keys, extra vehicles and upgraded sound;
- work by other modders, such as FusionFix (fixes for GTA IV itself) and DLSS-IV (modern upscaling), all included and set up;
- this project's own work: the classic Vice City activities rebuilt as native game scripts, restored dialogue, tuned handling, native PlayStation 5 DualSense support, a wearable parachute and many fixes.

You download one archive, extract it and copy its contents into your GTA IV folder. You don't need to find, install or combine any other mod. This is an unofficial fan project. You need your own copy of GTA IV: The Complete Edition, and the base GTA IV executable and complete base game are not included.

## What makes this version different

Several Vice City projects for GTA IV exist, and their names are confusingly similar. Here is how they relate, from the oldest to this one:

| Project | What it is |
|---|---|
| **Vice City NextGen Edition** by Revolution Team | The original conversion that brought Vice City's city, missions and characters into GTA IV. Every project below builds on it. |
| **Vice City for GTA IV: The Complete Edition** by Kn4ckers | An adaptation of that conversion for the Complete Edition. |
| **GTA Vice City Actual NextGen Edition** (AVCNG, also written VCANGE) by GTATrilogyGames and contributors | A large community modpack on top of the conversion: HD assets, extra map areas, vehicles, sounds and many smaller mods. It's built around the older 2008 version of GTA IV (1.0.8.0, with Games for Windows Live) and uses .NET script mods. |
| **Vice City Actual Next Gen CE** (this project) | AVCNG's 7.0.1 content plus selected 8.0 additions, brought to the current Complete Edition and extended with the native features and fixes in this README. |

What you get with this version:

- **It runs on the GTA IV you already own.** It's built for the current Complete Edition, 1.2.0.59. You don't downgrade the game, swap its executable or install Games for Windows Live.
- **Everything is native.** AVCNG's .NET script features (gameplay tweaks, weather and music) and its menu for the old game version are rebuilt as native game scripts and plugins. ScriptHook .NET and the .NET runtime aren't needed.
- **Vice City's side content is back.** The rampages, hidden packages, business income, shop hold-ups, food counters, insane stunt bonuses, completion percentage and the full Vice City statistics page are rebuilt for GTA IV.
- **The city feels like 1986 Vice City.** Each district has the pedestrians, traffic and parked cars of the 2002 game. Tommy can walk into Ken Rosenberg's office, and the original-style street lamps light the streets at night.
- **More of the original dialogue.** 62 original mission lines that were missing from the conversion are restored across 13 missions, with subtitles in seven languages.
- **Vehicles handle like what they are.** Each car is tuned toward the real vehicle its model represents, so a Countach-style Infernus and a Humvee-style Patriot drive very differently.
- **Native DualSense support:** adaptive triggers, haptics, sound from the controller speaker and a light bar that tracks health and police. DS4Windows and Steam Input aren't needed.
- **One package, set up and checked together.** Every file is listed with a checksum, and updates keep your existing saves working (see [Saves](#saves)).
- **Setup instructions for Windows, Linux and Steam Deck** are included below.

## 64-bit rendering, explained

**VCRender moves the graphics work into a separate 64-bit process.** GTA IV still supplies the gameplay, missions, physics and its Direct3D 9 commands. The included host renders those commands through **DXVK and Vulkan**, then displays the finished picture in the game window. You launch GTA IV normally; the host starts automatically.

The benefit is memory headroom: the graphics driver and its GPU mappings no longer take space inside the 32-bit game process. This lets the package provide a larger texture budget while keeping address-space safety limits. **GTA IV itself remains 32-bit**; this is not a replacement 64-bit GTAIV.exe and it does not give the engine unlimited memory.

The renderer also includes:

- **Paired 64-bit upscaling.** The included DLSS-IV client supplies the scene, depth and motion data; its 64-bit helper performs the upscale before the HUD. **FSR 3.1 is the path used and verified with this build.** DLSS-IV is the component's name; an NVIDIA RTX card is not required for FSR. NVIDIA DLSS requires RTX hardware and has not been verified with VCRender.
- **GPU-side synchronization** for the upscaler, with a fallback and a watchdog if the helper stops responding.
- **One frame ahead by default,** keeping the game's rendering queue short to reduce added input delay.
- **Space for large texture dictionaries.** Blocks above 8 MiB, and blocks the original physical-resource heap cannot fit, can use a separate pool. It is capped at 512 MiB and preserves at least 600 MiB of free game address space. This addresses the fragmented-heap failure that repeatedly unloaded roads and buildings.
- **Correct allocation failures and streaming diagnostics,** so the game receives real graphics errors and the logs can identify a resource that fails to load.

The installed build has run with the pool and FSR active, including a display reset and clean exit. These features improve memory handling; frame rate still depends on your CPU, GPU, resolution and graphics settings.

**Which Graphics API should I select?** Leave **Vulkan** selected and **Anti-aliasing Off**. VCRender already uses the included DXVK/Vulkan host. With the host running, that API menu setting does not switch it to a different renderer; it selects the fallback if the host cannot start. No separate DXVK installation is needed.

## What you need

- **GTA IV: The Complete Edition, version 1.2.0.59**, from Steam or the Rockstar Games Launcher. Let it update fully. To check the version on Windows, right-click `GTAIV.exe`, choose **Properties → Details** and look for **File version 1.2.0.59**.
- **Disk space:** allow about **9 GB** for the download and **10 GB** for the extracted mod. Keep about **30 GB** free while installing, in addition to the base game and your backups. Joining split parts creates another copy of the download, so allow extra space if you use that method.
- **A 64-bit operating system and a recent graphics driver compatible with DXVK 3.x.** A Vulkan 1.3-capable GPU is required, with the extensions used by the bundled DXVK. See [DXVK driver support](https://github.com/doitsujin/dxvk/wiki/Driver-support); Vulkan 1.3 support alone is not a compatibility guarantee.
- **7-Zip** to extract the download: [7-zip.org](https://www.7-zip.org/) on Windows. On Linux and Steam Deck you can use the commands in step 3 instead.
- **Windows:** install both the [Microsoft Visual C++ Redistributable **x86 and x64**](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/). The game-side plugins need x86; the 64-bit upscaler dependencies need x64.
- **Linux and Steam Deck:** a current Proton version and graphics drivers that support the included DXVK 3.x. Proton provides compatibility for both the 32-bit game and its 64-bit host; use a 64-bit/WoW64 prefix if configuring Wine manually.
- **Optional:** a PlayStation 5 DualSense controller. Keyboard and mouse, Xbox and other controllers work with GTA IV's normal controls.

## Installation, step by step

### 1. Prepare GTA IV

1. Install **GTA IV: The Complete Edition** and let it finish updating.
2. Start the game once, wait for the main menu, then quit. This finishes the game's first-time setup.
3. **Start from a clean game folder.** It shouldn't contain any other mods. If you've modded GTA IV before, uninstall it, delete any leftover game folder and reinstall. Verifying the game files in Steam restores changed files but doesn't remove files that mods added.
4. **Back up your saves.** They're in `Documents\Rockstar Games\GTA IV\Profiles\`. With Proton, this folder is inside the game's Proton prefix.

### 2. Download

1. Open the [latest release](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases/latest) and scroll down to **Assets**.
2. Download **all five parts**, `Build264.zip.001` to `Build264.zip.005`, into the **same folder**. Don't rename them. Together they form one ZIP file; none of them works alone.
3. Don't download **Source code (zip)** or **Source code (tar.gz)**. GitHub adds those automatically, and they don't contain the game files.

If someone gave you the single full **Build264.zip** instead, use that file and skip the parts.

### 3. Extract

**Windows:** install 7-Zip. Right-click `Build264.zip.001` and choose **7-Zip → Extract files…** (on Windows 11, choose **Show more options** first). Choose a new empty folder, for example `C:\VCANGCE`, and click **OK**. 7-Zip reads all five parts by itself; only open `.001`. Windows' built-in ZIP tool can't open split archives.

**Linux or Steam Deck:** open a terminal in the download folder. On Steam Deck, use **Konsole** in Desktop Mode. If 7-Zip is installed:

```sh
7z x Build264.zip.001 -oVCANGCE
```

If your installed command is `7zz`, use that instead of `7z`. Without 7-Zip, join the parts into the full ZIP and extract that with an archive tool such as Ark:

```sh
cat Build264.zip.00[1-5] > Build264.zip
```

Either way, you get a folder named **`release`**. It contains `update`, `plugins`, `DLSSIV`, several loose files, `README.md` and `CREDITS.md`.

### 4. Find your GTA IV folder

You need the folder that contains **`GTAIV.exe`**.

- **Steam:** in your Library, right-click **Grand Theft Auto IV: The Complete Edition** and choose **Manage → Browse local files**. If you see a folder named `GTAIV`, open it.
- **Rockstar Games Launcher:** usually `C:\Program Files\Rockstar Games\Grand Theft Auto IV`.

### 5. Copy the mod in

1. Open the extracted **`release`** folder.
2. Select **everything inside it** and copy it.
3. Paste it into the folder that contains `GTAIV.exe`. Choose **Replace** (or **Overwrite**) for every file when asked.

When you're done, `GTAIV.exe`, the `update` folder and the `plugins` folder should be side by side. Don't put the `release` folder itself into the game folder.

### 6. Linux and Steam Deck only: set the launch option

In Steam, right-click the game, choose **Properties → General**, and under **Launch Options** enter:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

This tells Proton to load the mod's loader. In Lutris, add the DLL override `dinput8` with the value `n,b` instead.

### 7. Play

1. Start the game normally.
2. In **Settings → Display**, leave **Graphics API** on **Vulkan** and set **Anti-aliasing** to **Off**. The supplied renderer and upscaler start automatically. Use your monitor's normal resolution; the upscaler can render the scene internally at a lower resolution.
3. Choose **New Game**, or load your save. A new game starts the activities, statistics and parked cars from their intended beginning.

**Optional, checking the download:** `PARTS-SHA256SUMS.txt` lists the checksum of each part, and `Build264.zip.sha256` lists the checksum of the full ZIP. Inside `release`, `SHA256SUMS.txt` lists every mod file. On Linux, run `sha256sum -c SHA256SUMS.txt` in that folder.

### If this folder already contains mods

A clean GTA IV CE folder is the supported starting point. Keep your saves separately, remove the old mod installation completely, and install this full package. Merely copying files over an unknown mod stack can leave conflicting plugins behind.

For an existing VCANGCE installation, the former standalone `plugins/VCAudioHeap.asi`, `plugins/VCMemory.asi`, `plugins/VCMusic.asi` and `plugins/VCScriptHook.asi` have been combined into `VCCore.asi`. Remove those four old files if present; `VCCore.asi` disables itself while any remain. `plugins/VCMemory.ini` is obsolete. Keep the new package's `ScriptHook.dll`, `VCScriptHook.ini`, `VCMusic.ini` and `VCCutscene.ini`. Use the complete matching VCRender folder and plugin from this package, rather than mixing renderer files from another installation.

### DualSense setup

- Connect the controller before starting the game: by USB, or paired and connected over Bluetooth.
- **Linux and Steam Deck:** turn off Steam Input for this game in **Properties → Controller**. In Steam's Game Mode, also put `SDL_GAMECONTROLLER_IGNORE_DEVICES=` (with nothing after the `=`) in front of the launch option, so Steam doesn't hide the controller from the game:

  ```
  SDL_GAMECONTROLLER_IGNORE_DEVICES= WINEDLLOVERRIDES="dinput8=n,b" %command%
  ```

  In Lutris, add it under **System options → Environment variables** instead.
- **KDE Plasma:** if the touchpad moves the mouse while you play, disable **DualSense Wireless Controller Touchpad** in **System Settings → Touchpad**.
- **USB audio on Linux:** pick the controller's sound device, which may appear as an **Analog Surround 4.0** profile, so the speaker and haptics work.

## Controls

GTA IV's normal controls apply. The mod adds these:

| Input | Action |
|---|---|
| **F7**, or **LB + D-pad right** | Open the Vice City Menu (D-pad up also opens it) |
| Arrow keys, numpad or D-pad | Move through the menu |
| **Enter** / **A** · **Backspace** / **B** | Select · go back |
| **Ctrl+F7** | Project Thunder weather menu |
| **Alt+V** | First-person camera |
| **N** in a vehicle | Next radio track |
| **Tab** on foot | Holster your weapon |
| Get into any full-size plane or helicopter | You automatically get a parachute pack and can still use guns |
| **Sprint** while falling with the pack | Open the parachute |
| Movement controls / the air-brake buttons shown on screen | Steer / slow down while parachuting |
| **Enter Vehicle** while parachuting | Cut the parachute away |
| Aim with fists, a bat, knife, pool cue or katana, then **Z / V / C / X / G** | Army and MMA fighting moves (**F5** changes style) |

On a DualSense, Cross is A, Circle is B, Square is X and Triangle is Y.

## What's in the game

### Story, activities and progression

The full Vice City story runs in GTA IV's engine. The Everglades and Gator Keys are added alongside the original city.

- **35 rampages** at their 2002 locations. Each gives you a weapon, a briefing, a countdown and a kill counter, and vehicle rampages count vehicles destroyed.
- **Hidden packages** at their original spots, including the mall and the police station, with the classic rewards such as the chainsaw, Python, flamethrower and minigun.
- **Business income:** businesses you own earn money every day. Walk into their gold **$** pickup to collect it.
- **Pizza Boy** deliveries thrown from the scooter: the customer walks over and picks up the box, and only a pizza that reaches them counts.
- **Shop hold-ups** at 12 stores, food counters that heal you, Leaf Links golf-course security, the Downtown shooting range, insane stunt bonuses and wheelie and stoppie bonuses.
- **Completion out of 154.** Rampages, the shooting range and shop hold-ups count toward it. Robbing all 12 tracked shops shows **15** stores on the statistics page and gives the one-time completion point.
- **The Vice City statistics page:** rampages, shops, properties, assets, money collected, people saved, fires put out, pizzas delivered, hidden packages, answered phone calls and your best paramedic and firefighter levels. People saved and fires put out count each rescue and each fire, not completed levels.
- **Tutorials when you need them,** for wanted levels, hospital and police-station recovery, bribes, motorcycle balance, hotwiring and resisting disarm. The prompts show your own controls.
- **Pole Position:** 12 occupied poles, two floor dancers, varied routines and private lap dances with GTA IV animations and cameras. Buying the club, paying dancers and the club's income work as in Vice City.
- **Clubs react to violence.** Shooting or fighting in the Malibu or Pole Position stops the dancers, and the crowd flees or cowers.
- **Story-based wanted limits,** and the army arrives at six stars.

Pickups keep Vice City's familiar shapes: the adrenaline pill, rampage skull, business money, save cassettes and green and red property icons. Health uses GTA IV's green first-aid kit.

### The city

- **Districts populated as in the 2002 game:** Cubans in Little Havana, Haitians in Little Haiti, bikers Downtown, soldiers at the army base, workers at the airport and docks, and golfers in caddies at Leaf Links. Day and night have their own mixes.
- **Classic traffic and boats,** with cheap and expensive cars in the right districts. Parked cars use the original locations and models. Added vehicles have sensible spots too: the Biplane at the airport, the Armadillo on a Downtown rooftop pad, Hotring cars by the stadium and the RC Baron beside the RC race van. Mission and reward vehicles appear where their missions put them.
- **Ken Rosenberg's office** in Washington Beach can be entered in free roam, with its corridors, stairwell and HD interior.
- **Upgraded locations:** the Vercetti mansion inside and out, Ocean View Hotel room 317, the Malibu Club, the V-Rock building, Phil's place, the junkyard, the airport access road, the Little Havana coast road, Leaf Links walls and hedges, and 1980s street details.
- **More from the 8.0 modpack:** furnished mall interiors, corrected mainland neon, night accent lights in Gator Keys and a cleaner pause-menu map.
- **Reworked beaches and ground:** new dry sand with fine grain detail on the Atlantic beaches and Gator Keys, mapped seamlessly across the main beach, so it looks continuous from the ground and from the air. Mismatched grass, coastal paths and airport paving are corrected.
- **An airport seaplane dock** you can reach by the stairs or by boat.
- **Original-style street lamps,** placed from the 2002 map, that break when hit and light the streets at night. Palm and tree trunks are solid, while bushes, hedges and ferns can be walked through. Distant palms and buildings match their close-up versions.

### Vehicles, physics and weapons

- **Every vehicle has its own handling,** tuned toward the real car its model represents: weight, power, acceleration, top speed, gears, drive layout, brakes, grip, ABS where fitting, and front-to-rear balance. Examples include the Infernus (Lamborghini Countach), Cheetah (Ferrari Testarossa), Turismo (Ferrari F40), Banshee (Corvette C4), Comet (Porsche 911), Sentinel (BMW 535i), Deluxo (DeLorean) and Patriot (Humvee).
- **Heavy and old vehicles feel heavy and old.** Cruisers, vans and trucks are slower and weightier than supercars.
- **Boats, helicopters, planes and RC vehicles** have their own weight and handling. The RC Bandit drives like a large RC buggy.
- **People and objects have realistic weights.** Tommy weighs 92 kg (203 lb). Street furniture, bins, bags, benches, signs, hydrants and glass have matching weights.
- **More time for odd jobs.** Paramedic, Firefighter, Vigilante, Pizza Boy, Taxi and Bloodring timers allow for the more realistic handling.
- **Seaplanes work on water.** The Skimmer floats, idles and takes off from water, and the Sea Sparrow can land on it. You can't drown while seated in either.
- **The Rhino is indestructible.** Nothing damages it, its tyres can't burst and it doesn't dent. That includes the army's tanks.
- **Rhino tank shells explode.** In **Phnom Penh '86** the helicopters, and in **Supply & Demand** the boat, have double health and explode when their health runs out.
- **A wearable parachute.** Get into any full-size plane or helicopter, in any seat, and Tommy puts on a parachute pack. He can still hold and fire guns while wearing it. Jump, press **Sprint** to open the canopy, steer with the movement controls and press **Enter Vehicle** to cut away. The canopy, pack, animations, landing and sounds come from *The Ballad of Gay Tony*.
- **The 1978 Ford Fairmont police car,** restored weapons and animations, and 20 guns tuned for damage, fire rate, magazine size and range. Rifles pierce armour.
- **Motorcycle helmets.** Tommy puts on a helmet when he gets on a motorcycle and takes it off when he gets off.
- **Euphoria for everyone,** with GTA IV's normal player ragdoll and Army and MMA melee moves.

### HUD, menu and cutscenes

- **The classic Vice City HUD,** rebuilt as a native script: clock, rolling eight-digit money, health and armour that flash when hit, weapon icon, reserve and clip ammo, six wanted stars and the pink radar ring. It adapts to your screen's aspect ratio.
- **The HUD hides when it should:** in cutscenes, fades, the Pay 'n' Spray and the wasted and busted screens, and when you turn it off in the game settings. The F7 menu adds **Hide HUD** and **No HUD in Cinematic Camera**.
- **The F7 Vice City Menu** lets you save outside missions, skip radio tracks, holster weapons and change gameplay options.
- **Your outfit carries into cutscenes.** Standard outfits switch to matching high-detail Tommy models in cutscenes, including the wounded versions. That includes the blue Soiree suit. Custom outfits use the default cutscene look.
- **Health between fights:** cutscenes start and end with full health. Below 25% health, three seconds without taking damage starts a recovery of 1% per second, up to 25%. Any hit pauses it.

### Sound, music and dialogue

- **Upgraded sounds** for weapons, horns, footsteps, ambience, engines and the wasted and start tunes, and Vice City vehicle names on the police scanner. Tyre squeal on roads comes from mmtsrn1's **BeamNG Sound Mod V2**.
- **Dedicated sounds for 23 vehicles and 13 weapons** that would otherwise share generic ones, including the Fairmont police car, VCN Maverick, Rio, Coast Guard boat, Speeder, Sea Sparrow, Infernus, Pizza Boy, Tec-9, minigun, sniper rifle, M16 and AUG.
- **62 restored mission lines in 13 missions,** with subtitles in seven languages: Mall Shootout, Guardian Angels, Sir, Yes Sir!, All Hands On Deck!, Recruitment Drive, Demolition Man, Supply & Demand, Gun Runner, Cap the Collector, Keep Your Friends Close, Four Iron, Back Alley Brawl and Waste the Wife. A line waits if a conversation is already playing.
- **The Job:** the crew talks when the getaway car takes damage, and the police station warns you about carrying a weapon, with consequences if you ignore it.
- **A restored phone call from Cortez** can arrive after the Diaz missions. It's cut content from the original game, with the original recordings and translated subtitles.
- **Original activity voices:** Tommy and the food-counter staff, the Leaf Links guards, the Hotring announcer and crowd, the Pole Position dancers and the end of the Cherry Popper purchase scene.
- **Chase and death music.** Chase music starts at three wanted stars, and death music plays when you die or your health is at 10% or below. Music pauses with the pause menu. The Vice City radio stations are included, plus **Independence FM** as a tenth station.

### DualSense controller

Native PlayStation 5 DualSense support, built into the game. DS4Windows, ViGEm and Steam Input aren't needed. It works over USB and Bluetooth.

- **Adaptive triggers:** throttle and brake resistance, ABS stutter, a redline buzz, road texture and a trigger feel for each weapon.
- **Haptics** matched to Vice City's vehicle and weapon sounds: engines, firing and reloading, impacts, explosions, rough ground, gear changes and passing traffic.
- **Controller speaker:** a police siren while you're wanted, louder as police get closer. Below 20% health a heartbeat plays, faster as health falls; it also plays through your speakers and stops when you heal. Everything else plays through your PC or TV as usual.
- **Light bar:** green through yellow to red as you lose health, flashing red and blue while you're wanted.
- **Speaker volume** in **Settings → Audio**, just below Music Volume, from 0 to 100% in 5% steps.
- **Player LEDs and touchpad gestures,** plus optional gyro aiming and motion steering.

### Graphics

- **HD vehicles** from Proper HD Era Vehicles, with detailed textures, badges and logos.
- **HD characters and world:** Tommy, cutscene characters, pedestrians in 1980s clothing, props, vegetation, roads and buildings, and improved interiors.
- **Atmosphere:** turquoise ocean and pools, Mint ocean water, a Miami sky and colour grading, Project Thunder lightning and storms, and Xbox-style rain droplets on the camera.
- **Distant lights** at night: streetlights, traffic and lamp glows across Vice City. Turn them on in **Settings → Display → Distant Lights**.
- **FSR 3.1 upscaling through the paired DLSS-IV client and 64-bit helper,** alongside FusionFix's graphics fixes and options. See [64-bit rendering](#64-bit-rendering-explained) for the distinction between the component name and verified backend.
- **Smoother running:** Mjolnir IV tunes how the game uses your CPU. GTA IV's own streaming and graphics safeguards manage memory, including around cutscenes, so nothing is cleared away when a cutscene starts. Loading screens skip a redundant wait, a fix from FusionFix's development version.

### Languages

Choose **English, French, German, Italian, Spanish, Russian or Japanese** in **Settings → Display → Language**. Missions, activities, statistics, graphics options and the subtitles of the restored dialogue are translated. Japanese has its own font; the pause-menu tabs stay in Latin letters because the Vice City menu font has no Japanese characters. The restored dialogue is spoken in English with translated subtitles. Vehicle, radio and brand names stay in English.

## Settings

Play the mod as supplied first. If you add other mods later, back up a working copy and add one at a time. The included features don't need ScriptHook .NET.

| File in the game folder | What you can change |
|---|---|
| `plugins/VCRender.ini` | The renderer, borderless/fullscreen handling, frame queue, extra texture budget, bounded physical-resource pool, diagnostics and whether the paired upscaler loads. |
| `DLSS-IV.cfg` | Upscaling quality, custom render scale and sharpening. These settings belong to the paired DLSS-IV client; see the notes below. |
| `plugins/VCMusic.ini` | Music and restored sounds: chase, death and flying music on or off, their volume and fade times, the wanted level that starts the chase music, and the volume of the restored voices. |
| `plugins/GTA_IV_DualSense.ini` (created on first start) | Trigger and haptic strength, effects, menu light-bar colour and brightness, player LEDs, touchpad gestures, gyro aiming and motion steering. |
| `plugins/VCScriptHook.ini` | `[ControllerAudio]`: speaker, Bluetooth audio, haptic strength, siren and heartbeat volume, and which effects reach the speaker. `[Lightbar]`: health colours and police flashing. |
| `plugins/AudioMap.ini` | Which controller sounds and haptics each vehicle and weapon uses. |
| `plugins/VCCutscene.ini` | `[Outfits]`: whether Tommy's current outfit carries into cutscenes. |

**Upscaling:** `quality=1` in `DLSS-IV.cfg` is the supplied Quality preset, rendering at about two thirds of output width and height. `custom=0` keeps that preset; `sharpness=0` adds no extra sharpening. Close the game before editing and restart afterward. To request the documented FSR 3.1 path explicitly, set `upscaler=1` and `fsr_version=3`. The supplied automatic selection settings are retained. The configuration also describes upstream features. NVIDIA DLSS has not been verified with VCRender, and FSR 4 and frame generation are not advertised here. **HDR output is unavailable through VCRender's current display path; leave `hdr=0`.**

**Native-resolution rendering:** in `plugins/VCRender.ini`, set `[Upscaler] Enabled=0` to keep VCRender but turn off its paired upscaler. Higher internal resolution increases GPU work. Restore `Enabled=1` to enable the upscaler again.

**Renderer queue and memory:** `MaxFramesAhead=1` is the supplied low-latency queue setting. Leave the texture budget and physical-pool limits at their defaults unless diagnosing a specific problem. Raising them cannot remove the 32-bit engine's address-space limit. Keep `plugins/global.ini`: it prevents the original root DLSS-IV plugin from loading a second time while VCRender loads its paired client.

**Turning the flying music back on:** open `plugins/VCMusic.ini` in a text editor. In the `[Flying]` section, change `Enabled=0` to `Enabled=1` and save.

**Your own music:** put `.mp3`, `.wav` or `.ogg` files in `plugins/VCMusic/Wanted`, `plugins/VCMusic/Death` or `plugins/VCMusic/Flying`, up to 16 per folder.

**Controller speaker categories:** in `VCScriptHook.ini`, `SpeakerEngine`, `SpeakerRoad`, `SpeakerImpacts`, `SpeakerWeapons`, `SpeakerExplosions`, `SpeakerAmbient` and `SpeakerOther` choose which effects also play from the controller speaker. At the supplied value of `0` they stay haptics only. The siren and heartbeat have their own settings, and `SpeakerFilter=0` sends every effect to the speaker.

## Saves

Save at a safehouse, or anywhere outside a mission with **F7 → Gameplay Tweaks → Save Game Now**. Saves are stored in `Documents\Rockstar Games\GTA IV\Profiles\`; with Proton, that folder is inside the game's Proton prefix. A save keeps the parked cars it was created with.

**Existing VCANGCE saves are retained.** Back them up before installing. The established radio-station order and pickup-model numbering are preserved, so the renderer changes do not require a new game. A save also retains its existing parked-car population. Saves from unrelated Vice City conversions or unmodded GTA IV are not interchangeable with this package.

A new game is available if you want the city, activities and statistics initialized from the beginning. Keep your backup outside the game folder before reinstalling or changing mods.

## Troubleshooting

- **7-Zip says a volume is missing or the archive is corrupt.** Confirm that `Build264.zip.001` through `.005` are together, with their exact original names. Check `PARTS-SHA256SUMS.txt` and download a failing part again. Extract only from `.001`.
- **The game starts as normal GTA IV in Liberty City.** Check that `GTAIV.exe`, `dinput8.dll`, `update` and `plugins` are together. On Proton, use the launch option in step 6. Do not leave the files inside an extra `release` folder.
- **The game closes or cannot start.** Start with a clean CE 1.2.0.59 installation, the complete matching package and current GPU drivers. On Windows, install both the x86 and x64 Visual C++ Redistributables. Check for the four old standalone plugins listed above. A 32-bit operating system cannot run the host.
- **No upscaling or an unexpectedly soft picture.** Keep Anti-aliasing Off, restart after changing settings, and check `DLSS-IV.cfg` quality/custom scale. FSR 3.1 is the verified VCRender path; the component's DLSS-IV name does not require an NVIDIA card. For an explicit FSR request, use `upscaler=1` and `fsr_version=3`. Do not install a second DLSS-IV or replace the paired files.
- **I changed DirectX/Vulkan and the picture stayed the same.** That is expected while VCRender's host is running. Leave Vulkan selected; the menu setting controls the fallback rather than the active host.
- **Loading artwork disappears at a particular resolution.** Apply/reset the display setting and try another supported resolution or your monitor's native resolution. The artwork layout can depend on the resolution.
- **Missing roads or buildings after prolonged travel.** Keep the renderer's default memory limits, copy the logs before restarting, and report the location and route. The new allocation pool targets a known fragmented-heap cause; other streaming failures need their own log evidence.
- **The DualSense is not detected.** Connect it before starting. On Linux/Steam Deck, turn off Steam Input and add `SDL_GAMECONTROLLER_IGNORE_DEVICES=` as described in [DualSense setup](#dualsense-setup).
- **Reporting a problem:** open a [GitHub issue](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/issues) with your OS, GPU/driver, resolution, mission/location and actions before the problem. Copy logs before relaunching: `plugins/VCRender.log`, `plugins/VCRender/logs/`, `plugins/VCRender/VCBridge-upscale-helper.log`, `DLSS-IV.log` and relevant plugin logs. A new launch can replace them. Include a screenshot if it helps show the issue.

## Uninstalling

The simplest way is to uninstall GTA IV, delete what's left of its folder and reinstall it. To remove only the mod, delete every file listed in `SHA256SUMS.txt` from the game folder, then use **Verify integrity of game files** in Steam (or the Rockstar Games Launcher equivalent) to restore the original game files the mod replaced. Keep your `Profiles` saves folder; it isn't part of the mod.

## Source code

The repository's [Source folder](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/tree/main/Source) contains the previously published **build 254** source snapshot, including gameplay scripts and native plugin work. **Build 264 adds VCRender work that is not yet part of that public snapshot.** The automatic GitHub “Source code” downloads are source archives, not this complete playable package. Download the five `Build264.zip` parts for the game files.

Textures, models and sound are distributed through the release package with their creator credits and bundled notices.

## About this project

Vice City Actual Next Gen CE is an unofficial fan project. It isn't affiliated with or endorsed by Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment. You need your own copy of GTA IV: The Complete Edition. Please support the original creators listed below.

## Credits

Vice City Actual Next Gen CE brings together the work of many creators. The original games, the Vice City conversion and the community assets belong to their respective authors. This project contributes the Complete Edition integration, the native adaptations, and the fixes and additions described in the README.

### The original games

- **Rockstar Games and the original development teams** — Grand Theft Auto: Vice City, Grand Theft Auto IV and The Ballad of Gay Tony; the original city, characters, missions, voices, music, animations and game technology.

### The Vice City conversion and the modpack it builds on

- **Revolution Team** — GTA Vice City NextGen Edition, the foundation that brings Vice City to GTA IV's RAGE engine.
- **Kn4ckers** — [Vice City for GTA IV: The Complete Edition](https://github.com/knackers4/vce), the Complete Edition base conversion used by this project.
- **GTATrilogyGames and the VCNE / DayL modding communities** — [GTA Vice City Actual NextGen Edition](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack), the 7.0.1 foundation and selected 8.0 material, including collected map, vehicle, weapon, sound and presentation work, and this release’s parachute and blue Soiree suit source assets.

### Engine fixes, mod loader and graphics

- **ThirteenAG and FusionFix contributors** — [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix), the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) and [Xbox Rain Droplets](https://github.com/ThirteenAG/XboxRainDroplets). The loading-screen improvement from FusionFix's development version ([commit ed3ace4](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/commit/ed3ace47581c171f20d4557caaedfd7c89750ff7), GPL-3.0) is adapted into `VCCore.asi`.
- **ChunkLeChuck** — [DLSS-IV](https://github.com/ChunkLeChuck/DLSSIV). Its bundled components retain their own notices, including AMD FidelityFX, NVIDIA DLSS, Intel XeSS, DXVK and Khronos/Vulkan components; see `DLSSIV/licenses`.
- **llamacomma** — [Mjolnir IV](https://github.com/llamacomma/Mjolnir_IV), adapted for this build (MIT).

### Weather, music and sound

- **ItsClonkAndre** — [Project Thunder](https://github.com/ClonkAndre/ProjectThunderIV), adapted as Project Thunder CE (GPL-3.0); VWantedMusic, DeathMusicIV, VFlyingMusic, and the IV-SDK .NET native reference used during development.
- **mmtsrn1** — [New Tire Skid Sound / BeamNG Sound Mod V2](https://www.beamng.com/threads/new-tire-skid-sound-updated.9056/), adapted into the road-skid sounds and matching controller audio.
- **Un4seen Developments** — [BASS](https://www.un4seen.com/) audio library.

### Controller support

- **NodixTech** — [GTA IV DualSense](https://www.nexusmods.com/gta4/mods/1136), adapted here for Vice City and native controller support.
- **GeorgLegato** — [LinuxAudio4Dualsense5](https://github.com/GeorgLegato/LinuxAudio4Dualsense5), the Bluetooth speaker/haptics protocol (AGPL-3.0).
- **Xiph.Org Foundation and Opus contributors** — [Opus](https://opus-codec.org/) audio codec (BSD).

### Map, buildings and interiors

- **DrAnusTart / Dr-AnusTart** — [Vercetti Estate Overhaul](https://www.nexusmods.com/gta4/mods/644), [Ocean View Hotel Overhaul](https://www.nexusmods.com/gta4/mods/585), [V-Rock Interior](https://www.nexusmods.com/gta4/mods/599), and Access Rosenberg's Office and HD Overhaul.
- **Fizzora** — Interiors and Cutscenes Fixes (IaCF_VC), as credited by the upstream package; **Ivan992** — Gator Keys.
- The creators of the inherited **Various Map Edits, Starfish Mansion Overhaul Project, Realistic Handling & Physics (Shturmovik2), VCNE HD map pack, RestoredWeapons, Cutscene Recognition, Mint Ocean Waters and TreesMod**. Some author identities were not recoverable from the preserved packages; their work is still acknowledged by its published mod name.

### Vehicles

- **MrOort** — [Proper HD Vehicles for GTA Vice City NextGen / Proper HD Era Vehicles – Lore Friendly](https://www.nexusmods.com/gta4/mods/1012). The pack credits **Myst, BigDudeManzo, Mr.Lorenzo, MeehmetEmin07, Excelsior, Budilov_Play, Saharov Channel, Svetlakov, Spartan112, AIGE, R&E, Peerless, RE, Vsoreny, YSS323, Dr_CHANI, Claude_Lib, DK22Pac, Thundersmacker, MGgames100, cromchen, Nitroflasche, K.E.Y (shaxtar), Milcin, Tonebee / twattyballs2011, Algonquin Hood, Ixl_Vanquish_MK, ThatOneHick, SimpleUnknown, KevinDV, deepcover0000, M4k3, G.C.T., direct_UA, Alex_SS, Ross [BloggerRoss], _CP_, dirprof, Diporrison's Lair, AXL163, Carface / Bigfoot2003, RoadRunner71, Intensified / Compeast, universetwisters, AlexandreRR, Milcin7, Rockstar Games and Ubisoft Reflections**. This carries forward the source pack's contributor credits; individual upstream variants may have been superseded in this CE build.
- **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000** — the 1978 Ford Fairmont VCPD model, conversion, updates and police livery.

### Characters, clothing and cutscenes

- **R0NALDMCDONLD / RONALDMCDONLD** — [Vice City Next Gen Edition HD Pedestrians](https://www.nexusmods.com/gta4/mods/539); its page also credits **Riot-MH** for the US Army pedestrian.
- **Diya / _BasicallyDiya** and **XCLSIOR** — [Cutscene Retexture Project](https://libertycity.net/files/gta-4/222231-cutscene-retexture-project-vice-city.html), including character, prop, clothing and environment textures and the Ken Rosenberg/Mercedes retextures.
- **Lukakion** — Improved Tommy’s Clothes, credited by the upstream 8.0 package. The blue Soiree suit and its clean/wounded cutscene forms are adapted from that package’s player and Cutscene Retexture Project assets.
- **A.R Scorpion / A.R.Scorpion** — VCNG Cutscene Patch outfit models; **xdd5110** — the original cutscene-outfit model-loading concept and upstream script work.
- **metalwars** and **Kevin1230san** — the Pole Position nude dancer models, including the source models used for the added dancer variations.
- **BrynnaDaRosa** — **Playboy Bunny Niko Bellic**, the source of the black bunny ears, bow-tie collar and cuffs adapted for two Pole Position dancers. The original author’s page is [Brynna on Patreon](https://www.patreon.com/Brynna).

### Gameplay systems, HUD and menus

- **CyanFire** — [Vice City Nextgen Edition – Classic HUD](https://www.nexusmods.com/gta4/mods/667), reimplemented here as a native game script.
- **Rockstar Games / The Ballad of Gay Tony** — the original parachute scripts, models, animations and sounds used by the native parachute integration supplied through 8.0 and adapted for CE here.
- **LotsofEds / ServalEd** — [MoveImprove](https://github.com/LotsofEds/MoveImproveIV-SDK), whose parachute animation/ragdoll compatibility predicate is translated into the existing native movement script.
- **CGP** — Enhanced Fighting And Euphoria, with features rebuilt natively here.
- **catsmackaroo, ItsClonkAndre and LibertyTweaks contributors** — [LibertyTweaks](https://github.com/catsmackaroo/LibertyTweaks), with selected features reimplemented natively; **Zolika1351** — ZMenu, a reference for the F7 menu.
- **Sergeanur** — [SCFIX-Miami](https://github.com/Sergeanur/SCFIX-Miami), an original Vice City script reference for activities, population, parked cars and restored dialogue.

### Languages

- **Arcenciel1991** — [GTA VC Actual NextGen Japanese Translation](https://www.nexusmods.com/grandtheftautovicecity/mods/695), including its Japanese font.
- **AIO All Classic Language Patch v2.0 creators**, shared by **Contremaitre** — French, German, Italian and Spanish text, fonts and translated images. Contremaitre is credited as the known source/distributor, rather than assigned sole translation authorship.

### Technical references and development tools

- **gutbash** — [revd](https://github.com/gutbash/revd), technical reference for the physical audio heap location and 192 MiB capacity precedent. The included audio-heap plugin is independent project code; revd’s engine-slot relocation and config writer are not included.
- **Alexander Blade / scocl** and **SC-CL contributors** — compiler/header/native-reference work used for native script development; **FourFury** — RAGE-format tooling used during inspection and authoring. These development credits do not mean the tools are player requirements.
- **GTABase, GTAMods Wiki and the wider GTA modding community** — vehicle and format references, documentation and shared research.

### Earlier prototypes

Earlier project prototypes used **Nixolas1**’s 2009 GTA IV parachute behavior reference, **Quechus13**’s canopy models, and **gongel**’s [GTA V backpack port](https://www.nexusmods.com/gta4/mods/888). Those controller, canopy and backpack implementations have been replaced by the native 8.0-derived integration in this release; this acknowledgment preserves their development contribution.

### VCRender, 64-bit rendering and paired upscaling

- **Vice City Actual Next Gen CE project contributors** — VCRender's 32-bit client and 64-bit renderer, the paired DLSS-IV integration, Vulkan interop, GPU-side synchronization, texture-budget safety, streaming diagnostics and physical-resource allocation work. This is the project's renderer; GTA IV itself remains a 32-bit game.
- **Philip Rebohle, Joshua Ashton and DXVK contributors** — [DXVK](https://github.com/doitsujin/dxvk), the Direct3D-to-Vulkan implementation used by the 64-bit host. The bundled x64 Direct3D 9 and DXGI libraries are unmodified DXVK v3.1.1-97-ga78a467cadb22be-pcos from **Proton-CachyOS**; see `plugins/VCRender/LICENSE-DXVK.txt`.
- **Chunk / ChunkLeChuck** — the paired [DLSS-IV](https://github.com/ChunkLeChuck/DLSSIV) client and helper used by VCRender's upscaling path (Apache-2.0), with project integration changes. Its original notice is preserved at `plugins/VCRender/NOTICE-DLSS-IV.txt` and its licence at `plugins/VCRender/LICENSE-DLSS-IV.txt`.
- **Advanced Micro Devices, Inc. and GPUOpen contributors** — [AMD FidelityFX Super Resolution](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), including the FSR 3.1.5 upscaler and RCAS sharpening components. AMD notices remain in `DLSSIV/licenses` and the paired DLSS-IV notice.
- **Jean-Laurent ROUZIES** — [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) transport/protocol headers (MIT), with portions derived from **NIGos**' dlss5-dx11-bridge (MIT); the original notices are retained. These protocol names describe technical dependencies, not a claim that this package includes DLSS 5.
- **The Khronos Group, LunarG and Valve** — Vulkan headers and related interfaces; the original header notices are retained. **Tatsuyuki Ishi** — [LatencyFleX](https://github.com/ishitatsuyuki/LatencyFleX), acknowledged in the paired DLSS-IV notice (Apache-2.0).
- **gutbash** — [b-bridge](https://github.com/gutbash/b-bridge), an earlier out-of-process rendering research/reference used during this project's development. VCRender's core is project code; its Vulkan/upscaler integration carries forward the project's own earlier bridge work and the separately credited transport dependencies above.

### Corrections

If a name or contribution is missing or incorrect, please open a GitHub issue with the original mod link or readme so the credit can be corrected. This is an unofficial fan project; these credits do not imply endorsement by the original authors.
