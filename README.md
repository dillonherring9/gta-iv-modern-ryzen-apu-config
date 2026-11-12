# Vice City Actual Next Gen CE

**Play Grand Theft Auto: Vice City inside Grand Theft Auto IV.** Tommy Vercetti's story and the whole neon-lit 1986 city, rebuilt with GTA IV's lighting, physics and Euphoria animation, HD artwork, extra islands, the classic side activities and restored dialogue. It comes as one download that you copy into your own copy of **GTA IV: The Complete Edition**.

**Build 254** · for **GTA IV: The Complete Edition 1.2.0.59** · download **VCANGCE.Zip** from [Releases](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases/latest)

- [What this is](#what-this-is)
- [What makes this version different](#what-makes-this-version-different)
- [New in build 254](#new-in-build-254)
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

**Grand Theft Auto IV** is Rockstar's 2008 game. It has a much newer engine, with realistic lighting, weighty vehicle physics and **Euphoria**, which makes characters stumble, fall and react to hits instead of playing canned animations. **The Complete Edition** (often shortened to **CE**) is the version sold today on Steam and the Rockstar Games Launcher. It includes GTA IV and its *Episodes from Liberty City*. Its current version is **1.2.0.59**.

A **total conversion** replaces a game's world and story with another one. This mod swaps GTA IV's Liberty City for Vice City and its missions, characters, vehicles, radio and sound, all running in GTA IV's engine.

**Vice City Actual Next Gen CE** (short name **VCANGCE**) is a complete, ready-to-play package of that conversion. It combines:

- the Vice City conversion and the large community modpack built on it, with HD vehicles, characters and textures, extra areas such as the Everglades and Gator Keys, extra vehicles and upgraded sound;
- work by other modders, such as FusionFix (fixes for GTA IV itself) and DLSS-IV (modern upscaling), all included and set up;
- this project's own work: the classic Vice City activities rebuilt as native game scripts, restored dialogue, tuned handling, native PlayStation 5 DualSense support, a wearable parachute and many fixes.

You download one archive, extract it and copy its contents into your GTA IV folder. You don't need to find, install or combine any other mod. This is an unofficial fan project. You need your own copy of GTA IV: The Complete Edition, and no Rockstar game files are included.

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
- **Windows, Linux and Steam Deck.** Setup steps for each are below.

## New in build 254

Everything new since build 247:

- **The Rhino is indestructible.** Gunfire, explosions, fire, crashes and melee no longer damage the tank. Its tyres can't burst and its body doesn't dent. This applies to every Rhino, including the army's tanks at six wanted stars.
- **Pizza Boy throws the pizzas.** Tommy throws each pizza from the scooter with a real throwing animation. The box flies to the customer, who walks over and picks it up. A pizza that lands in the water, keeps rolling or lands too far away doesn't count, and the customer waits for your next throw.
- **Cutscenes leave the world alone.** Earlier builds cleared nearby traffic, pedestrians and loose objects when a cutscene started, and could remove a car you'd parked nearby. GTA IV's own streaming now manages memory around cutscenes, so nothing is cleared away when one starts. Your outfit still carries into cutscenes.
- **Less waiting behind loading screens.** While a loading screen is already showing, the game no longer stops for extra screen refreshes on every frame behind it. This fix comes from FusionFix's development version, ahead of its next release.
- **GTA IV's graphics safeguard is back on.** Mjolnir IV still tunes how the game uses your CPU, but it no longer switches off the game's own check that keeps graphics settings within its memory budget.
- **No more trees growing out of the pavement.** 134 palms that stood straight on sidewalks, roads and concrete with no planter or soil around them are gone, including the row in front of the Ocean View Hotel. Potted palms, and trees in grass, sand, soil or planters, stay.
- **Distant lights work in Vice City.** Turn on **Settings → Display → Distant Lights** for streetlights, traffic and lamp glows across the city at night. The road-lights data from the Vice City conversion carried Liberty City's index, so it couldn't draw Vice City's roads; it's rebuilt for Vice City's streets.
- **Your saves carry over.** Saves from builds 247, 245 and 224 keep working.

Earlier updates:

- **Build 247:** the music that played whenever you flew a helicopter or plane is off, so in the air you hear the radio and the aircraft. It was easy to mistake for the near-death music. See [Settings](#settings) to turn it back on.
- **Build 245:** all 12 Pole Position poles are occupied, and two floor dancers offer GTA IV-style private lap dances, with Vice City's payment, dialogue and club-ownership progression kept. Two dancers wear black bunny accessories. It also corrected textures for pedestrians, the player and map objects, fixed Washington Beach decals, vehicle colours, engine sounds and pedestrian voices, and combined four of the project's plugins into one, `VCCore.asi`.

## What you need

- **GTA IV: The Complete Edition, version 1.2.0.59**, from Steam or the Rockstar Games Launcher. Let it update fully. To check the version on Windows, right-click `GTAIV.exe`, choose **Properties → Details** and look for **File version 1.2.0.59**.
- **Disk space:** the download is about **8.6 GB** and the extracted mod about **9.9 GB**. Keep about **30 GB** free while installing, on top of the game itself and your backups.
- **7-Zip** to extract the download: [7-zip.org](https://www.7-zip.org/) on Windows. On Linux and Steam Deck you can use the commands in step 3 instead.
- **Windows:** the [Microsoft Visual C++ Redistributable **x86**](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/). Many PCs already have it. Install the x86 one even on 64-bit Windows.
- **Linux and Steam Deck:** Proton, Steam's built-in compatibility layer for Windows games.
- **Optional:** a PlayStation 5 DualSense controller. Keyboard and mouse, Xbox and other controllers work with GTA IV's normal controls.

## Installation, step by step

### 1. Prepare GTA IV

1. Install **GTA IV: The Complete Edition** and let it finish updating.
2. Start the game once, wait for the main menu, then quit. This finishes the game's first-time setup.
3. **Start from a clean game folder.** It shouldn't contain any other mods. If you've modded GTA IV before, uninstall it, delete any leftover game folder and reinstall. Verifying the game files in Steam restores changed files but doesn't remove files that mods added.
4. **Back up your saves.** They're in `Documents\Rockstar Games\GTA IV\Profiles\`. With Proton, this folder is inside the game's Proton prefix.

### 2. Download

1. Open the [latest release](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases/latest) and scroll down to **Assets**.
2. Download **all five parts**, `VCANGCE.Zip.001` to `VCANGCE.Zip.005`, into the **same folder**. Don't rename them. Together they form one ZIP file; none of them works alone.
3. Don't download **Source code (zip)** or **Source code (tar.gz)**. GitHub adds those automatically, and they don't contain the game files.

If someone gave you the single full **VCANGCE.Zip** instead, use that file and skip the parts.

### 3. Extract

**Windows:** install 7-Zip. Right-click `VCANGCE.Zip.001` and choose **7-Zip → Extract files…** (on Windows 11, choose **Show more options** first). Choose a new empty folder, for example `C:\VCANGCE`, and click **OK**. 7-Zip reads all five parts by itself; only open `.001`. Windows' built-in ZIP tool can't open split archives.

**Linux or Steam Deck:** open a terminal in the download folder. On Steam Deck, use **Konsole** in Desktop Mode. If 7-Zip is installed:

```sh
7z x VCANGCE.Zip.001 -oVCANGCE
```

Without 7-Zip, join the parts into the full ZIP and extract that with any archive tool, such as Ark:

```sh
cat VCANGCE.Zip.00[1-5] > VCANGCE.Zip
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
2. In **Settings → Display**, set **Graphics API** to **Vulkan** and **Anti-aliasing** to **Off**. The included DLSS-IV upscaler needs both. Which of DLSS or FSR you can choose depends on your graphics card.
3. Choose **New Game**, or load your save. A new game starts the activities, statistics and parked cars from their intended beginning.

**Optional, checking the download:** `PARTS-SHA256SUMS.txt` lists the checksum of each part, and `VCANGCE.Zip.sha256` lists the checksum of the full ZIP. Inside `release`, `SHA256SUMS.txt` lists every mod file. On Linux, run `sha256sum -c SHA256SUMS.txt` in that folder.

### Updating from an earlier build

- **From build 247 or 245:** copy the new `release` contents over your installation as in step 5. You can then delete `plugins/VCMemory.ini`; it's no longer used, and the cutscene outfit setting is now in `plugins/VCCutscene.ini`.
- **From build 224 or earlier:** before you copy, delete `plugins/VCAudioHeap.asi`, `plugins/VCMemory.asi`, `plugins/VCMusic.asi` and `plugins/VCScriptHook.asi`. They're now combined into `plugins/VCCore.asi`, which switches itself off while any of the old files is present. Copying doesn't delete them, so remove them by hand, along with the unused `plugins/VCMemory.ini`. Keep the other `.ini` files and `ScriptHook.dll` from the new package.

Your saves keep working either way.

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
- **DLSS and FSR upscaling** through DLSS-IV, with FusionFix's fixes and options for GTA IV.
- **Smoother running:** Mjolnir IV tunes how the game uses your CPU. GTA IV's own streaming and graphics safeguards manage memory, including around cutscenes, so nothing is cleared away when a cutscene starts. Loading screens skip a redundant wait, a fix from FusionFix's development version.

### Languages

Choose **English, French, German, Italian, Spanish, Russian or Japanese** in **Settings → Display → Language**. Missions, activities, statistics, graphics options and the subtitles of the restored dialogue are translated. Japanese has its own font; the pause-menu tabs stay in Latin letters because the Vice City menu font has no Japanese characters. The restored dialogue is spoken in English with translated subtitles. Vehicle, radio and brand names stay in English.

## Settings

Play the mod as supplied first. If you add other mods later, back up a working copy and add one at a time. The included features don't need ScriptHook .NET.

| File in the game folder | What you can change |
|---|---|
| `plugins/VCMusic.ini` | Music and restored sounds: chase, death and flying music on or off, their volume and fade times, the wanted level that starts the chase music, and the volume of the restored voices. |
| `plugins/GTA_IV_DualSense.ini` (created on first start) | Trigger and haptic strength, effects, menu light-bar colour and brightness, player LEDs, touchpad gestures, gyro aiming and motion steering. |
| `plugins/VCScriptHook.ini` | `[ControllerAudio]`: speaker, Bluetooth audio, haptic strength, siren and heartbeat volume, and which effects reach the speaker. `[Lightbar]`: health colours and police flashing. |
| `plugins/AudioMap.ini` | Which controller sounds and haptics each vehicle and weapon uses. |
| `plugins/VCCutscene.ini` | `[Outfits]`: whether Tommy's current outfit carries into cutscenes. |

**Turning the flying music back on:** open `plugins/VCMusic.ini` in a text editor. In the `[Flying]` section, change `Enabled=0` to `Enabled=1` and save.

**Your own music:** put `.mp3`, `.wav` or `.ogg` files in `plugins/VCMusic/Wanted`, `plugins/VCMusic/Death` or `plugins/VCMusic/Flying`, up to 16 per folder.

**Controller speaker categories:** in `VCScriptHook.ini`, `SpeakerEngine`, `SpeakerRoad`, `SpeakerImpacts`, `SpeakerWeapons`, `SpeakerExplosions`, `SpeakerAmbient` and `SpeakerOther` choose which effects also play from the controller speaker. At the supplied value of `0` they stay haptics only. The siren and heartbeat have their own settings, and `SpeakerFilter=0` sends every effect to the speaker.

## Saves

Save at a safehouse, or anywhere outside a mission with **F7 → Gameplay Tweaks → Save Game Now**. Saves are stored in `Documents\Rockstar Games\GTA IV\Profiles\`; with Proton, that folder is inside the game's Proton prefix. A save keeps the parked cars it was created with.

**Updating keeps your saves.** Saves from builds 247, 245 and 224 load and play normally in build 254, so you don't need to start a new game. Back up your saves before any update anyway.

Why this works: a GTA IV save remembers two things from the mod.

- **The radio station list.** If an update changed the stations, older saves would stop loading.
- **The model number of every pickup a script placed** (clothes, health, armour, hidden packages and so on). A save remembers these pickups by their position in the game's model list, not by name. If an update inserted or removed models ahead of them, the pickups in older saves would look like the wrong object, though they could still be collected.

This build keeps both exactly as in builds 224, 245 and 247. Models that were added later, such as the extra Pole Position dancers, come after all the others. That's also why two changes tried during development were left out: a nine-station radio update and a clean-up of unused model definitions. Both would have affected existing saves.

## Troubleshooting

- **The game starts as normal GTA IV, in Liberty City.** The mod didn't load. Check that `GTAIV.exe`, `dinput8.dll`, `update` and `plugins` are in the same folder. On Linux and Steam Deck, set the launch option from step 6.
- **The game closes or won't start.** Make sure the game folder had no other mods and that, on Windows, the **x86** Visual C++ Redistributable is installed. If you updated from build 224 or older, delete the four old `.asi` files listed under [Updating from an earlier build](#updating-from-an-earlier-build).
- **DLSS or FSR options are missing.** Set **Graphics API** to **Vulkan** and **Anti-aliasing** to **Off**, then restart the game. With an NVIDIA card on Proton, add `PROTON_ENABLE_NVAPI=1` in front of the launch option.
- **The DualSense isn't detected.** Connect it before starting the game. On Linux and Steam Deck, turn off Steam Input for the game and add `SDL_GAMECONTROLLER_IGNORE_DEVICES=` as described in [DualSense setup](#dualsense-setup).
- **Reporting a problem:** open an issue on the [GitHub page](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/issues). Say what system you play on, which mission or place you were in and what you did just before. Attach the `.log` files from the `plugins` folder, copied before you start the game again, because each launch replaces them.

## Uninstalling

The simplest way is to uninstall GTA IV, delete what's left of its folder and reinstall it. To remove only the mod, delete every file listed in `SHA256SUMS.txt` from the game folder, then use **Verify integrity of game files** in Steam (or the Rockstar Games Launcher equivalent) to restore the original game files the mod replaced. Keep your `Profiles` saves folder; it isn't part of the mod.

## Source code

The repository's [Source folder](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/tree/main/Source) holds the code for this build: all 96 gameplay scripts, the source of the `VCCore.asi` and `ScriptHook.dll` plugins with their build tools, the Mjolnir patch, the loader and model-definition files, and the model-order checker that keeps saves compatible. Building from it reproduces this build's script archive, `VCCore.asi`, `ScriptHook.dll` and patched `Mjolnir_IV.asi` exactly. Textures, models, sound and other game assets are only in the release download.

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

### Corrections

If a name or contribution is missing or incorrect, please open a GitHub issue with the original mod link or readme so the credit can be corrected. This is an unofficial fan project; these credits do not imply endorsement by the original authors.
