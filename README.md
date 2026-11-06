# 🌴 Vice City Actual Next Gen CE — Release8CDBC

**Vice City on GTA IV: The Complete Edition 1.2.0.59, with AVCNG 7.0.1 as its foundation, selected AVCNG 8.0 content, and the native gameplay features described here.** This is the complete mod overlay: install it on a clean copy of GTA IV CE and start playing. Everything needed from the Vice City conversion is included; you do not need to install another Vice City modpack first.

FusionFix 5.1.2, DLSS-IV, a classic Vice City HUD and native DualSense support are included. The supplied gameplay features use native game scripts and plugins and require no ScriptHook .NET or .NET runtime. You must own GTA IV: The Complete Edition; `GTAIV.exe` and Rockstar's base archives are not supplied.

> [!IMPORTANT]
> This release brings together the work of many mod authors. Please support them and see the [credits](#-credits).

> [!NOTE]
> **Adult content:** the Pole Position dancers and the stripper in its purchase cutscene are nude. This content appears inside the club.

## 🆚 Where this fits with AVCNG 7.0.1 and 8.0

This is a Complete Edition adaptation of **GTA Vice City Actual Next-Gen Edition** (AVCNG / VCANGE), built on **Revolution Team's Vice City NextGen Edition** and **Kn4ckers' Complete Edition conversion**. It keeps the 7.0.1-based CE build's missions, HD assets, extra vehicles, map areas and sound, with selected compatible material from 8.0 and the features below.

| Area | Included in this CE build |
|---|---|
| **Game version** | GTA IV: The Complete Edition **1.2.0.59**, without a downgrade. |
| **8.0 content** | Wounded cutscene outfit models, adjusted mall furniture, mainland neon corrections, Gator Keys accent lights, and pause-map transparency and mipmaps. |
| **Gameplay and scripts** | Native classic HUD, activities, population, statistics, contextual tutorials, restored dialogue and controller integration. These have their own implementations and do not reproduce every 8.0 tweak. |
| **Mission choices** | Includes the restored bank dialogue, station weapon warning and cut post-Diaz Cortez call described below. Treacherous Swine retains the balcony route; the optional Gonzalez escape scene is not included. |
| **Languages** | English, French, German, Italian, Spanish, Russian and Japanese text, with English recordings for the newly restored dialogue. |
| **Installation** | One complete overlay for a clean CE game. No earlier release of this port, AVCNG 7.0.1 or AVCNG 8.0 installation is required. |

The 8.0 integration is selective. Readers familiar with the original 8.0 should use this README as the description of what this build includes; every 8.0 option and script difference has not been ported or evaluated. Its files should be kept together as one build.

## ✨ The game you get

### 🌴 City, activities and progression

Play Tommy Vercetti's Vice City story with GTA IV's lighting, vehicle physics and Euphoria. AVCNG's extra areas, including the Everglades and Gator Keys, sit alongside the original city and its missions.

- **35 classic rampages** at their 2002 locations, with a supplied weapon, briefing, countdown and kill counter. Vehicle rampages count vehicles destroyed. Strict weapon attribution in some rampages still needs further testing.
- **Hidden packages** at their original locations, including the mall and police station, with the classic weapon rewards such as the chainsaw, Python, flamethrower and minigun.
- **Business revenue:** owned businesses accumulate money each day. Walk into their gold **$** pickup to collect it.
- **Shop hold-ups** at 12 tracked stores, healing food counters, Leaf Links security, the Downtown shooting range, insane stunt bonuses and wheelie/stoppie bonuses.
- **Completion out of 154**, with rampages, shooting-range completion and shop hold-ups contributing to the total. Completing all 12 tracked shop robberies gives full store credit: the statistics display becomes **15**, and the existing one-time completion point is awarded once.
- **Vice City statistics:** rampages, shops, properties, assets, collected revenue, people saved, fires put out, pizzas delivered, hidden packages, answered incoming calls and best paramedic/firefighter levels. People saved and fires put out count individual rescues and fires rather than completed levels. Missed or declined incoming calls do not add to the answered-call count.
- **Contextual tutorials** for wanted levels, hospital and police-station recovery, bribes, motorcycle balance, hotwiring and resisting disarm, with prompts that follow your controls.
- **Club crowds react** to gunfire and attacks inside the Malibu and Pole Position: dancers stop, and the crowd flees or cowers.
- **Story-based wanted limits** and an army response at six stars.

The pickups use Vice City's familiar shapes: the adrenaline pill, rampage skull, business money, save cassettes and green/red property icons. Health uses GTA IV's green first-aid kit. Pickup texture packing is repaired while keeping the original icon artwork.

### 🏙️ Map and population

- **District populations based on 2002 Vice City:** Cubans in Little Havana, Haitians in Little Haiti, bikers Downtown, soldiers at the army base, workers at the airport and docks, and golfers in Caddies at Leaf Links. Day and night use their own population mixes.
- **Classic traffic and boats**, with each district's mix of cheap and expensive vehicles. Parked cars use the original locations and models. Added vehicles also have suitable homes: the Biplane at the airport, Armadillo on a Downtown roof pad, Hotring cars by the stadium and RC Baron beside the RC race van. Mission and reward vehicles retain their mission spawns.
- **Enter Ken Rosenberg's office in free roam:** the Washington Beach building includes its corridors, stairwell, collision and HD office.
- **AVCNG map edits:** the Vercetti mansion inside and out, Ocean View Hotel room 317, Malibu Club, V-Rock building, Phil's place, the junkyard, airport access road, Little Havana coast road, Leaf Links walls and hedges, and 1980s street details.
- **Selected 8.0 map work:** mall furniture placement, corrected mainland neon texture references and nighttime accent lights in Gator Keys. The pause map has transparent surrounding water and mipmapped tiles; it covers the same geography.
- **Airport seaplane dock:** reach the deck from the stairs or approach by boat.
- **Original-style street lamps**, placed from the 2002 map, with breakable models and nighttime lighting. Palm and tree trunks have solid collision; bushes, hedges and ferns remain passable. Distant palms and buildings match their nearby versions.

The world uses three consolidated map archives for the east, west and shared areas, with the map content preserved. This reduces the number of separate map files; no frame-rate, loading-time or memory improvement is claimed from that change.

### 🚗 Vehicles, physics and weapons

- **Individual vehicle handling** is tuned toward the real vehicles the Vice City models represent: mass, power, acceleration, top speed, gears, drivetrain, brakes, grip, ABS where appropriate and front/rear weight balance. Examples include the Infernus/Countach, Cheetah/Testarossa, Turismo/F40, Banshee/Corvette C4, Comet/Porsche 911, Sentinel/BMW 535i, Deluxo/DeLorean and Patriot/Humvee.
- **Heavy and older vehicles behave accordingly.** Cruisers, vans and trucks are slower and heavier than supercars. GTA IV donor vehicles retain their suspension geometry; measured performance depends on the game physics.
- **Boats, helicopters, planes and RC vehicles** have their own weight and handling settings. The RC Bandit is tuned as a large RC buggy.
- **Pedestrian and object masses** reflect their size and equipment. Tommy is 92 kg (203 lb). Street furniture, bins, bags, benches, signs, hydrants and glass have revised masses.
- **Odd-job timers** for Paramedic, Firefighter, Vigilante, Pizza Boy, Taxi and Bloodring allow extra time for the slower vehicle handling.
- **Amphibious aircraft:** the Skimmer can float, idle and take off from water, and the Sea Sparrow can land on it. Occupants are protected from drowning while seated in them.
- **Rhino cannon** fires explosive shells. The helicopters in **Phnom Penh '86** and the boat in **Supply & Demand** have doubled health and explode when their health meter is exhausted.
- **Parachute:** carried in helicopters and planes. Jump and press **Sprint** to open, steer with movement controls, and press **Enter Vehicle** to cut away. It closes on landing. It uses *The Ballad of Gay Tony* animations and a GTA V-style backpack.
- **1978 Ford Fairmont VCPD car**, restored weapons and animations, and 20 guns tuned for damage, fire rate, magazine size and range. Rifles pierce armour.
- **Pedestrian Euphoria**, GTA IV's default player ragdoll and Army/MMA melee moves.

### 🖥️ HUD, controls and cutscenes

- **Classic Vice City HUD**, rebuilt as a native script: clock, rolling eight-digit money, health and armour with damage flashes, weapon icon, reserve/clip ammunition, six wanted stars and the pink radar ring.
- **HUD visibility follows the game:** it hides for cutscenes, fades, Pay 'n' Spray, wasted/busted states and the HUD setting. The F7 menu's **Hide HUD** and **No HUD in Cinematic Camera** options also apply.
- **Native F7 Vice City Menu**, including saving outside missions, radio-track skipping, weapon holstering and gameplay options.
- **Outfits carry into cutscenes:** supported standard outfits use matching high-detail Tommy models, including wounded versions. Custom outfits use the default cutscene appearance.
- **Full health at the start and end of cutscenes.** Below 25% health, going three seconds without damage starts recovery at 1% per second, stopping at 25%. Taking a hit interrupts recovery.

### 🔊 Sound, music and dialogue

- **AVCNG sound** for weapons, horns, tyres, footsteps, ambience, engines, death/start tunes and Vice City vehicle names on the police scanner.
- **Specific sound assignments for 23 vehicles and 13 weapons** that otherwise use generic sounds, including the Fairmont VCPD, VCN Maverick, Rio, Coast Guard boat, Speeder, Sea Sparrow, Infernus, Pizza Boy, Tec-9, minigun, sniper rifle, M16 and AUG.
- **62 restored original mission lines across 13 missions**, with subtitles in seven languages. These include Mall Shootout, Guardian Angels, Sir, Yes Sir!, All Hands On Deck!, Recruitment Drive, Demolition Man, Supply & Demand, Gun Runner, Cap the Collector, Keep Your Friends Close, Four Iron, Back Alley Brawl and Waste the Wife. The dialogue system checks for an existing conversation before playing a line.
- **The Job:** original crew dialogue can play when the getaway car is damaged. The police-station setup has the original warning about carrying a weapon and an escalation if you ignore it.
- **Post-Diaz Cortez call:** restored cut dialogue can arrive off mission after Diaz. It uses the original recordings and translated subtitles; this is restored cut content rather than a claim that the call ran in the retail 2002 game.
- **Original activity voices and sounds:** Tommy and food-counter staff, Leaf Links guards, Hotring announcer/crowd, Pole Position private-dance dialogue and the Cherry Popper purchase-scene ending.
- **Wanted, death, low-health and flying music**, with chase music beginning at three stars by default. Music pauses with the menu. **Independence FM** is available as a tenth station.

### 🎮 DualSense

Native PS5 DualSense support includes a virtual controller, adaptive triggers, haptics, speaker effects and lighting. DS4Windows, ViGEm and Steam Input are not needed for the included integration. USB and Bluetooth behavior depends on the system and controller/audio setup.

- **Adaptive triggers:** throttle and brake resistance, ABS stutter, redline buzz, road texture and weapon trigger feel.
- **Haptics:** Vice City vehicle and weapon sound mappings, with effects for engines, firing/reloading, impacts, explosions, rough surfaces, gear changes and nearby traffic.
- **Controller speaker:** a police siren while wanted, stronger as police approach, and a heartbeat at 20% health or below that speeds up as health falls. The heartbeat also plays on the PC/TV and stops on healing. Main game audio remains on the PC/TV.
- **Light bar:** green through yellow to red as health falls; flashing red/blue while wanted.
- **Speaker volume in Settings → Audio**, directly below Music Volume, from 0–100% in 5% steps.
- **Player LEDs and touchpad gestures**, plus optional gyro aiming and motion steering.

Individual sound mappings, Bluetooth/audio configurations and hardware combinations have not all been tested.

### 🎨 Graphics and presentation

Proper HD Era Vehicles supplies detailed vehicle textures, badges and logos. The package also includes HD Tommy, cutscene characters, pedestrians, props, vegetation, city surfaces, map textures, IaCF_VC/CRTP interiors and 1980s pedestrian clothing.

The presentation includes turquoise ocean and pools, Mint water, Miami sky and colour grading, Project Thunder lightning and storms, Xbox rain droplets and DLSS/FSR options through DLSS-IV.

**Mjolnir IV** adjusts CPU affinity and busy-wait behavior and disables the engine's automatic quality limiter. Cutscene memory management clears ambient cars, pedestrians and objects and pauses traffic around cutscenes, while preserving mission characters and the vehicle you occupy. An unoccupied car you parked may be removed. Some large cutscene models use smaller memory pages to reduce hangs in affected scenes. Performance depends on your hardware and settings.

### 🗣️ Languages

Choose **English, French, German, Italian, Spanish, Russian or Japanese** in **Settings → Display → Language**. Text includes missions, activities, statistics, graphics options and restored-dialogue subtitles. Japanese has its own font; pause-menu tabs use Latin letters because the Vice City menu font lacks Japanese characters.

Newly restored dialogue plays in English with translated subtitles. The inherited Russian speech, cutscene and radio archives remain present; their in-game selection has not been verified. Some new translations, especially Russian and Japanese, have not received native-speaker review. Vehicle, radio and brand names may remain English.

## 📋 Requirements

- An owned **GTA IV: The Complete Edition 1.2.0.59** installation.
- At least **12 GB free for the extracted mod**, plus space for the downloaded parts and any backups.
- **Windows:** Microsoft Visual C++ 2015–2022 Redistributable **x86**.
- **Linux / Steam Deck:** Proton.
- Optional **DualSense** controller. Keyboard/mouse and other controllers can use the normal game controls.

## 📦 Install Release8CDBC

1. **Prepare a clean GTA IV CE game.** Close it. Use version 1.2.0.59, launch the unmodified game once and quit. Back up saves and any game files the overlay will replace. If coming from AVCNG 7.0.1, AVCNG 8.0 or another modded setup, first restore a clean CE game using that setup's uninstall instructions or a separate clean installation. Do not layer this over it; leftover scripts, plugins and old map archives can conflict.
2. **Download all numbered parts** from this GitHub release: `Release8CDBC.zip.001`, `.002` and every later part. Keep them together in one folder. They form one archive. GitHub's automatically generated **Source code** ZIP/TAR downloads are not the game mod.
3. **Extract the first part with 7-Zip** into a new empty folder. Open `Release8CDBC.zip.001`; 7-Zip reads the remaining parts automatically. Windows File Explorer alone may not handle this split archive. On Linux: `7z x Release8CDBC.zip.001 -oRelease8CDBC-extracted`.
4. **Open the extracted `release` folder.** Copy everything inside it—`update`, `plugins`, `DLSSIV` and the loose files—into the GTA IV directory **beside `GTAIV.exe`**, preserving the folder structure. Copy the contents of `release`, rather than putting that folder itself inside the game.
5. **Start a New Game.** This gives the activities, statistics and parked-car layout their intended starting state. Make a spare save, quit, relaunch and load it before investing time in a playthrough. Back up older saves before trying them; saves from other configurations are not certified compatible.

To verify downloads, compare the numbered parts with **`PARTS-SHA256SUMS.txt`**. The extracted **`SHA256SUMS.txt`** verifies the mod's individual files; on Linux, run `sha256sum -c SHA256SUMS.txt` inside the extracted `release` folder.

### Linux / Steam Deck and graphics

If the mod loader does not start, set Steam's launch option to `WINEDLLOVERRIDES="dinput8=n,b" %command%`. In Lutris, use the DLL override `dinput8` = `n,b`.

For the supplied DLSS-IV setup, choose **Graphics API: Vulkan** and **Anti-aliasing: Off** in Display settings. DLSS and FSR availability depends on your GPU, driver and operating system. NVIDIA under Proton may require `PROTON_ENABLE_NVAPI=1`. The build was developed and played on Linux with Proton; Windows and Steam Deck configurations have not been comprehensively tested.

### DualSense setup

- **Connect before starting.** On Windows, connect the controller and launch the game. For Bluetooth, pair and connect it before launch.
- **Linux / Steam Deck:** disable Steam Input for this game in Steam **Properties → Controller**. In Steam Game Mode, set `SDL_GAMECONTROLLER_IGNORE_DEVICES=` to an empty value in the game's environment so Steam does not hide the controller. Lutris exposes this under **System options → Environment variables**.
- **KDE Plasma:** if the controller touchpad also moves the mouse/camera, disable **DualSense Wireless Controller Touchpad** under **System Settings → Touchpad**.
- **USB audio:** use the controller's sound device; Linux may expose an **Analog Surround 4.0** profile. Speaker and haptic behavior, particularly over Bluetooth, depends on the audio setup.
- If startup hangs or the light bar stays dark, reconnect the controller before another launch. Preserve current logs first if you are reporting a failure.

## 🎮 Controls

| Input | Action |
|---|---|
| **F7** / **LB + D-pad right** | Open Vice City Menu; D-pad up also opens it |
| Arrows / numpad / D-pad | Navigate menu |
| Enter / A · Backspace / B | Select · back |
| **Ctrl+F7** | Project Thunder menu |
| **Alt+V** | First-person camera |
| **N** in a vehicle · **Tab** on foot | Next radio track · holster weapon |
| **Sprint** while falling from an aircraft | Open parachute |
| **Enter Vehicle** while parachuting | Cut away |
| Aim with fists, bat, knife, Pool Cue or Katana, then **Z / V / C / X / G** | Army/MMA moves; **F5** changes style |

DualSense uses the normal controller layout: Cross = A, Circle = B, Square = X and Triangle = Y.

## ⚙️ Settings and making your own changes

Play the supplied build before adding other mods, keep a working backup and add one change at a time. Compatible ScriptHook .NET or other .NET mods can be tried separately; the included features do not require them, and optional .NET hooks and arbitrary third-party mods have not been tested with this build. Check CE 1.2.0.59 support and loader/resource conflicts before replacing files.

| File | Settings |
|---|---|
| `plugins/GTA_IV_DualSense.ini` (created on first start) | Trigger/haptic strength, effects, menu light-bar colour/brightness, player LEDs, touchpad gestures, gyro aiming and motion steering. |
| `plugins/VCScriptHook.ini` | `[ControllerAudio]` speaker, Bluetooth audio, haptic strength, siren/heartbeat volumes and which effect categories reach the speaker. `[Lightbar]` health colours and police flashing. Speaker volume is also available in the in-game Audio menu. |
| `plugins/AudioMap.ini` | Controller sound mappings for vehicles and weapons. |
| `plugins/VCMusic.ini` | Restored voices/sounds, their volume, music categories, volumes/fades and chase-music wanted threshold. |
| `plugins/VCMemory.ini` | Cutscene clearing radius/budgets, traffic pause and `[Outfits]` cutscene outfit matching. The plugin also adds Japanese to the language menu. |

In `VCScriptHook.ini`, `SpeakerEngine`, `SpeakerRoad`, `SpeakerImpacts`, `SpeakerWeapons`, `SpeakerExplosions`, `SpeakerAmbient` and `SpeakerOther` control speaker categories. Their supplied values of `0` keep those effects as haptics; the siren and heartbeat have separate controls. `SpeakerFilter=0` sends every effect to the speaker.

You can add your own `.mp3`, `.wav` or `.ogg` music to `plugins/VCMusic/Wanted`, `Death` and `Flying`, up to 16 tracks per folder. Plugins write diagnostic logs beside their files.

## 💾 Saves and practical notes

Save at a safehouse or use **F7 → Gameplay Tweaks → Save Game Now** outside missions. Saves live in `Documents\Rockstar Games\GTA IV\Profiles\`; on Proton this is inside the game's Wine prefix. A save retains the parked vehicles it was created with.

The current build has been reported working well in general play. Static checks also cover the installed files, scripts, resource references and texture-page packing. These do not establish that every mission branch, translation, controller configuration or save/reload combination has been tested, or that the packaged release has been installed on another machine.

An intermittent failure leaving `GTAIV.exe` running without a game window has occurred, and its cause remains undiagnosed. If reporting it, keep the game, plugin and Proton logs before restarting Wine. The map consolidation does not claim to fix this issue.

The Vice City NextGen radio list is retained alongside the selected AVCNG sound configuration. The parachute has no sound effects. The classic HUD adjusts its positions for the screen aspect ratio.

## 🗑️ Uninstalling

Restore backed-up files that the overlay replaced, then remove its added files using `SHA256SUMS.txt` as the file list. If no backups are available, restore or reinstall a clean game and remove any remaining mod files. Back up saves and personal files first; do not delete the Profiles folder to troubleshoot the mod.

## 🙏 Credits

- **Revolution Team**: GTA Vice City NextGen Edition.
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce).
- **GTATrilogyGames** and contributors: [GTA Vice City Actual Next-Gen Edition (AVCNG / VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack), including its sounds and Various Map Edits.
- Authors of the map edits collected in AVCNG: **Starfish Mansion Overhaul Project**, **Vercetti Mansion Overhaul**, the **V-Rock building** interior, **Ocean View Hotel** rework and **Interiors and Cutscene Fixes (IaCF_VC)**.
- **DrAnusTart**: Access Rosenberg's Office and HD Overhaul.
- **CyanFire**: Vice City NextGen Edition Classic HUD, rebuilt here as a native script.
- **A.R Scorpion** (outfit models) and **xdd5110** (original concept): VCNG Cutscene Patch.
- **NodixTech**: [GTA IV DualSense](https://www.nexusmods.com/gta4/mods/1136), adapted here for Vice City.
- **GeorgLegato**: [LinuxAudio4Dualsense5](https://github.com/GeorgLegato/LinuxAudio4Dualsense5), AGPL-3.0, for the Bluetooth speaker/haptics protocol.
- **Xiph.Org Foundation**: [Opus](https://opus-codec.org), BSD licence.
- **llamacomma**: [Mjolnir IV](https://github.com/llamacomma/Mjolnir_IV), MIT licence.
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets.
- **ItsClonkAndre**: Project Thunder (GPL-3.0), adapted as Project Thunder CE; VWantedMusic, DeathMusicIV, VFlyingMusic and the IV-SDK .NET native list.
- **SC-CL** authors: GTA IV native name table.
- **Un4seen Developments**: BASS audio library.
- **DLSS-IV** author.
- **AIO All Classic Language Patch v2.0** author, shared by **Contremaitre**: French, German, Italian and Spanish text, fonts and translated images.
- **Arcenciel1991**: Japanese translation and font, GTA VC Actual NextGen 日本語化 mod.
- **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000**: 1978 Ford Fairmont VCPD.
- Author of the 2009 GTA IV **parachute mod** (`para.asi`): canopy and parachute behavior.
- Author of **Parachute port from GTA 5 to GTA 4**: parachute backpack.
- **Rockstar Games / The Ballad of Gay Tony**: parachute animations.
- **metalwars** and **Kevin1230san**: Pole Position nude dancer models.
- **CGP**: Enhanced Fighting And Euphoria, rebuilt natively.
- **LibertyTweaks** authors, rebuilt natively, and **Zolika1351** for ZMenu, inspiration for the F7 menu.
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (**Shturmovik2**), **VCNG HD Pedestrians**, **VCNE HD map pack**, **RestoredWeapons**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters** and **TreesMod**.
- **Sergeanur**: [SCFIX-Miami](https://github.com/Sergeanur/SCFIX-Miami), the original Vice City script reference for activities, population, parked cars and restored dialogue.
- **GTABase** and GTA community wikis: vehicle counterpart references.
- **Rockstar Games**: Grand Theft Auto: Vice City and Grand Theft Auto IV, including original voices, mission dialogue, activity sounds and the police siren used on the controller.

Unofficial fan project, unaffiliated with Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment. You need your own legal copy of GTA IV: The Complete Edition. Please credit and support the original authors.
