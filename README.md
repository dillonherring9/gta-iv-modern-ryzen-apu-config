# Vice City Actual Next Gen CE

**Vice City Actual Next Gen CE** brings Tommy Vercetti’s story and the city from **Grand Theft Auto: Vice City** into **Grand Theft Auto IV: The Complete Edition**, with community visual upgrades, extra areas and vehicles, restored activities and dialogue, and the fixes and native features developed for this project.

Vice City is Rockstar’s 2002 game set in a fictional city based on Miami in 1986. This is an unofficial PC modification: it uses GTA IV’s engine, lighting, physics and Euphoria to play that city and story. **CE** means **The Complete Edition** of GTA IV. You need your own copy of **GTA IV: The Complete Edition, version 1.2.0.59**. The base game executable and Rockstar’s base archives are not included.

**This is the complete mod package.** It includes the Vice City conversion and the selected community additions described here. You do not need another Vice City conversion, modpack, previous release or separate patch first. FusionFix 5.1.2, DLSS-IV and the native plugins used by this build are included. The supplied gameplay features require no ScriptHook .NET or .NET runtime.

**Download:** open [Releases](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/releases) and expand **Assets**. Download every numbered `VCANGCE.Zip` part and follow [Installation](#installation). If your download host supplies the single **VCANGCE.Zip**, use that instead. GitHub’s automatic **Source code** downloads are not the playable mod.

**Content note:** this is an adult GTA game; the Pole Position dancers and the stripper in its purchase cutscene are nude.

## New in this release

- **Expanded Pole Position:** all 12 dance-pole positions are occupied, with eight additional pole/table dancers and two floor dancers. The floor dancers offer GTA IV-style private lap dances, with original Vice City dialogue, payment and business progression retained. Two dancers wear adapted black bunny ears, collars and cuffs.
- **Existing saves keep working:** the radio data and every model number that saves depend on are unchanged from the previous release, and the new models are listed after everything else. Two development changes that broke saves were left out; see [Saves](#saves) for how and why.
- **Asset and sound repairs:** corrected missing pedestrian/player and map/prop texture references, Washington Beach decal height, selected vehicle colours and engine-sound assignments, and dead civilian/dancer voice references.
- **Simpler file layout:** game resources, map definitions and language tables are consolidated, and four project gameplay plugins are combined into **VCCore.asi** while retaining their existing settings files. These changes preserve the retained features; no measured performance improvement is claimed.

## How this differs from the other Vice City NextGen projects

Several separate projects contribute to this release. Their similar names can be confusing:

| Project | What it contributes and how it relates to this release |
|---|---|
| **Vice City NextGen Edition — Revolution Team** | The underlying Vice City conversion for GTA IV: the city, story and conversion work on which these later projects build. |
| **Vice City for GTAIV: The Complete Edition — Kn4ckers** | The Complete Edition adaptation used as a foundation here. This release targets CE **1.2.0.59**, without downgrading GTA IV. |
| **GTA Vice City Actual NextGen Edition — GTATrilogyGames and contributors** | The upstream community modpack, also called **AVCNG / VCANGE**. Our foundation is its **7.0.1** content, with selected compatible additions from **8.0**. |
| **Vice City Actual Next Gen CE — this project** | This separately maintained Complete Edition build, abbreviated **VCANGCE**, with the native gameplay systems, controller integration and fixes described below. It is an unofficial adaptation of the credited upstream work. |

This build keeps the inherited missions, HD assets, extra vehicles, map areas and sounds. Selected 8.0 material includes the native parachute system and its assets/sounds, Tommy’s blue Soiree suit in gameplay and clean/wounded cutscenes, other wounded cutscene outfits, mall furniture placement, mainland neon corrections, Gator Keys accent lights and pause-map transparency/mipmaps. It does **not** include every 8.0 option or script change. For example, Treacherous Swine retains its balcony route; the optional Gonzalez escape scene is not included. Install this package as one complete build rather than combining files from the different projects.

## Work made here and work from the community

The original game, conversion, HD artwork, many models, map improvements, translations, sounds and several plugin foundations come from Rockstar and the community authors credited at the bottom. We do not claim those assets as our own.

Work developed or adapted **in this project** includes:

- **Native gameplay scripts:** the classic HUD and F7 menu, rampages, shop hold-ups, food counters, business revenue, completion/statistics tracking, contextual tutorials and activity behavior. Community HUD/mod designs and original Vice City scripts informed this work; those authors and references are credited below.
- **City and vehicle corrections:** original-game population and parked-car placement, handling and mass tuning, object and tree collision work, amphibious aircraft protection, mission vehicle-health behavior and adjusted activity timers.
- **Dialogue and sound integration:** a native mission-voice bridge, restored dialogue and subtitles, activity sounds, vehicle/weapon sound assignments and native music behavior. The recordings and source sounds remain their original creators’ work.
- **Controller integration:** Vice City-specific DualSense mappings, native game integration, controller audio, lighting and Bluetooth support, built around credited community implementations and protocol work.
- **CE fixes and asset integration:** cutscene outfit matching, selected upstream 8.0 outfits, parachute and map/UI additions, repaired pickup texture packing, missing texture references, consolidated game-resource archives, map definitions and language tables. Four project gameplay plugins now share VCCore.asi while keeping their existing configuration files. No measured performance gain is claimed from consolidation.
- **Ground-surface improvements:** new dry beach sand and grain detail, continuous mapping across the main beach, matching nearby and distant sand textures, and corrections to confirmed grass, coastal path and clean airport paving mismatches. Wet sand, tracks and distinct surface types keep their own appearance.
- **Startup, save-load and ragdoll repairs:** corrected the parachute’s CE script startup and execution faults, repaired its audio-bank directory, and removed the captured native-plugin startup lock conflicts. A further parachute-script memory correction fixes the diagnosed corruption behind Tommy’s windshield-ejection and ragdoll-recovery crashes. The reported high-speed crash test now stays running normally, and the wearable pack, gun use, deployment and landing still work.
- **Additional included fixes:** BeamNG-derived road tyre squeals adapted for game and controller playback; easier Skimmer takeoff from water; restoration of the player prop assets needed for motorcycle helmets; and a guarded native audio-memory capacity increase addressing the allocation failure found during Riot.

The tyre sounds, Skimmer water takeoff, motorcycle helmets and Riot audio-memory correction are confirmed in play testing on Linux with Proton. Riot was completed and the next mission started. See [Testing and known issues](#testing-and-known-issues).

## The game you get

### City, activities and progression

Play Tommy Vercetti's Vice City story with GTA IV's lighting, vehicle physics and Euphoria. AVCNG's extra areas, including the Everglades and Gator Keys, sit alongside the original city and its missions.

- **35 classic rampages** at their 2002 locations, with a supplied weapon, briefing, countdown and kill counter. Vehicle rampages count vehicles destroyed. Strict weapon attribution in some rampages still needs further testing.
- **Hidden packages** at their original locations, including the mall and police station, with the classic weapon rewards such as the chainsaw, Python, flamethrower and minigun.
- **Business revenue:** owned businesses accumulate money each day. Walk into their gold **$** pickup to collect it.
- **Shop hold-ups** at 12 tracked stores, healing food counters, Leaf Links security, the Downtown shooting range, insane stunt bonuses and wheelie/stoppie bonuses.
- **Completion out of 154**, with rampages, shooting-range completion and shop hold-ups contributing to the total. Completing all 12 tracked shop robberies gives full store credit: the statistics display becomes **15**, and the existing one-time completion point is awarded once.
- **Vice City statistics:** rampages, shops, properties, assets, collected revenue, people saved, fires put out, pizzas delivered, hidden packages, answered incoming calls and best paramedic/firefighter levels. People saved and fires put out count individual rescues and fires rather than completed levels. Missed or declined incoming calls do not add to the answered-call count.
- **Contextual tutorials** for wanted levels, hospital and police-station recovery, bribes, motorcycle balance, hotwiring and resisting disarm, with prompts that follow your controls.
- **Expanded Pole Position:** 12 occupied pole positions and two floor dancers, varied routines, and private lap dances using GTA IV animations and cameras. The club keeps Vice City’s purchase, payment, asset-completion and revenue progression. Two dancers have black bunny accessories.
- **Club crowds react** to gunfire and attacks inside the Malibu and Pole Position: dancers stop, and the crowd flees or cowers.
- **Story-based wanted limits** and an army response at six stars.

The pickups use Vice City's familiar shapes: the adrenaline pill, rampage skull, business money, save cassettes and green/red property icons. Health uses GTA IV's green first-aid kit. Pickup texture packing is repaired while keeping the original icon artwork.

### Map and population

- **District populations based on 2002 Vice City:** Cubans in Little Havana, Haitians in Little Haiti, bikers Downtown, soldiers at the army base, workers at the airport and docks, and golfers in Caddies at Leaf Links. Day and night use their own population mixes.
- **Classic traffic and boats**, with each district's mix of cheap and expensive vehicles. Parked cars use the original locations and models. Added vehicles also have suitable homes: the Biplane at the airport, Armadillo on a Downtown roof pad, Hotring cars by the stadium and RC Baron beside the RC race van. Mission and reward vehicles retain their mission spawns.
- **Enter Ken Rosenberg's office in free roam:** the Washington Beach building includes its corridors, stairwell, collision and HD office.
- **AVCNG map edits:** the Vercetti mansion inside and out, Ocean View Hotel room 317, Malibu Club, V-Rock building, Phil's place, the junkyard, airport access road, Little Havana coast road, Leaf Links walls and hedges, and 1980s street details.
- **Selected 8.0 map work:** mall furniture placement, corrected mainland neon texture references and nighttime accent lights in Gator Keys. The pause map has transparent surrounding water and mipmapped tiles; it covers the same geography.
- **Reworked beaches and ground:** dry Atlantic and Gator Keys sand has new colour and grain detail. Main-beach texture mapping is aligned across adjoining ground pieces, with matching nearby and distant sand textures to reduce visible breaks from the air. Confirmed grass, coastal path and clean airport paving mismatches are also corrected. The new ground textures are confirmed in play from the ground and while flying.
- **Airport seaplane dock:** reach the deck from the stairs or approach by boat.
- **Original-style street lamps**, placed from the 2002 map, with breakable models and nighttime lighting. Palm and tree trunks have solid collision; bushes, hedges and ferns remain passable. Distant palms and buildings match their nearby versions.

The mod uses consolidated game-resource archives and map-definition files, with the active map content retained. No frame-rate, loading-time or memory improvement is claimed from consolidation.

### Vehicles, physics and weapons

- **Individual vehicle handling** is tuned toward the real vehicles the Vice City models represent: mass, power, acceleration, top speed, gears, drivetrain, brakes, grip, ABS where appropriate and front/rear weight balance. Examples include the Infernus/Countach, Cheetah/Testarossa, Turismo/F40, Banshee/Corvette C4, Comet/Porsche 911, Sentinel/BMW 535i, Deluxo/DeLorean and Patriot/Humvee.
- **Heavy and older vehicles behave accordingly.** Cruisers, vans and trucks are slower and heavier than supercars. GTA IV donor vehicles retain their suspension geometry; measured performance depends on the game physics.
- **Boats, helicopters, planes and RC vehicles** have their own weight and handling settings. The RC Bandit is tuned as a large RC buggy.
- **Pedestrian and object masses** reflect their size and equipment. Tommy is 92 kg (203 lb). Street furniture, bins, bags, benches, signs, hydrants and glass have revised masses.
- **Odd-job timers** for Paramedic, Firefighter, Vigilante, Pizza Boy, Taxi and Bloodring allow extra time for the slower vehicle handling.
- **Amphibious aircraft:** the Skimmer can float and idle on water, with stronger thrust/lift settings to ease water takeoff; the Sea Sparrow can land on water. Occupants are protected from drowning while seated in them. Skimmer water takeoff is confirmed in play testing.
- **Rhino cannon** fires explosive shells. The helicopters in **Phnom Penh '86** and the boat in **Supply & Demand** have doubled health and explode when their health meter is exhausted.
- **Automatic wearable parachute:** entering any full-size helicopter or plane gives Tommy the pack immediately, in any seat. RC aircraft are excluded. The pack stays separate from weapon selection: Tommy can wear it while holding and using a gun. Jump and press **Sprint** to deploy, steer with movement controls, and press **Enter Vehicle** to cut away. Follow the on-screen air-brake prompts for your controller layout. The imported native system includes its canopy, pack, animations, landing behavior and sound effects. Pack-and-gun operation, deployment and landing passed the latest user gameplay test; steering was confirmed in earlier play testing.
- **1978 Ford Fairmont VCPD car**, restored weapons and animations, and 20 guns tuned for damage, fire rate, magazine size and range. Rifles pierce armour.
- **Motorcycle helmet assets restored:** the original player props are supplied for the engine’s automatic helmet behavior. Appearance on Tommy and automatic helmet behavior are confirmed in play testing.
- **Pedestrian Euphoria**, GTA IV's default player ragdoll and Army/MMA melee moves. Tommy’s normal impact, rolling and recovery behavior is retained; the corrected parachute scripts no longer overwrite his animation state. The previously crashing windshield-ejection test now passes.

### HUD, controls and cutscenes

- **Classic Vice City HUD**, rebuilt as a native script: clock, rolling eight-digit money, health and armour with damage flashes, weapon icon, reserve/clip ammunition, six wanted stars and the pink radar ring.
- **HUD visibility follows the game:** it hides for cutscenes, fades, Pay 'n' Spray, wasted/busted states and the HUD setting. The F7 menu's **Hide HUD** and **No HUD in Cinematic Camera** options also apply.
- **Native F7 Vice City Menu**, including saving outside missions, radio-track skipping, weapon holstering and gameplay options.
- **Outfits carry into cutscenes:** supported standard outfits use matching high-detail Tommy models, including wounded versions. The blue Soiree suit uses selected 8.0 gameplay and clean/wounded cutscene models. Custom outfits use the default cutscene appearance.
- **Full health at the start and end of cutscenes.** Below 25% health, going three seconds without damage starts recovery at 1% per second, stopping at 25%. Taking a hit interrupts recovery.

### Sound, music and dialogue

- **Community sound upgrades** for weapons, horns, footsteps, ambience, engines, death/start tunes and Vice City vehicle names on the police scanner. Road tyre skids use excerpts from **mmtsrn1’s BeamNG Sound Mod V2**, resampled and looped here for game and controller playback; the road tyre sounds are confirmed in play testing.
- **Specific sound assignments for 23 vehicles and 13 weapons** that otherwise use generic sounds, including the Fairmont VCPD, VCN Maverick, Rio, Coast Guard boat, Speeder, Sea Sparrow, Infernus, Pizza Boy, Tec-9, minigun, sniper rifle, M16 and AUG.
- **62 restored original mission lines across 13 missions**, with subtitles in seven languages. These include Mall Shootout, Guardian Angels, Sir, Yes Sir!, All Hands On Deck!, Recruitment Drive, Demolition Man, Supply & Demand, Gun Runner, Cap the Collector, Keep Your Friends Close, Four Iron, Back Alley Brawl and Waste the Wife. The dialogue system checks for an existing conversation before playing a line.
- **The Job:** original crew dialogue can play when the getaway car is damaged. The police-station setup has the original warning about carrying a weapon and an escalation if you ignore it.
- **Post-Diaz Cortez call:** restored cut dialogue can arrive off mission after Diaz. It uses the original recordings and translated subtitles; this is restored cut content rather than a claim that the call ran in the retail 2002 game.
- **Original activity voices and sounds:** Tommy and food-counter staff, Leaf Links guards, Hotring announcer/crowd, Pole Position private-dance dialogue and the Cherry Popper purchase-scene ending.
- **Wanted, death, low-health and flying music**, with chase music beginning at three stars by default. Music pauses with the menu. **Independence FM** is available as a tenth station.

### DualSense

Native PS5 DualSense support includes a virtual controller, adaptive triggers, haptics, speaker effects and lighting. DS4Windows, ViGEm and Steam Input are not needed for the included integration. USB and Bluetooth behavior depends on the system and controller/audio setup.

- **Adaptive triggers:** throttle and brake resistance, ABS stutter, redline buzz, road texture and weapon trigger feel.
- **Haptics:** Vice City vehicle and weapon sound mappings, with effects for engines, firing/reloading, impacts, explosions, rough surfaces, gear changes and nearby traffic.
- **Controller speaker:** a police siren while wanted, stronger as police approach, and a heartbeat at 20% health or below that speeds up as health falls. The heartbeat also plays on the PC/TV and stops on healing. Main game audio remains on the PC/TV.
- **Light bar:** green through yellow to red as health falls; flashing red/blue while wanted.
- **Speaker volume in Settings → Audio**, directly below Music Volume, from 0–100% in 5% steps.
- **Player LEDs and touchpad gestures**, plus optional gyro aiming and motion steering.

Individual sound mappings, Bluetooth/audio configurations and hardware combinations have not all been tested.

### Graphics and presentation

Proper HD Era Vehicles supplies detailed vehicle textures, badges and logos. The package also includes HD Tommy, cutscene characters, pedestrians, props, vegetation, city surfaces, map textures, IaCF_VC/CRTP interiors and 1980s pedestrian clothing.

The presentation includes turquoise ocean and pools, Mint water, Miami sky and colour grading, Project Thunder lightning and storms, Xbox rain droplets and DLSS/FSR options through DLSS-IV.

**Mjolnir IV** adjusts CPU affinity and busy-wait behavior and disables the engine's automatic quality limiter. Cutscene memory management clears ambient cars, pedestrians and objects and pauses traffic around cutscenes, while preserving mission characters and the vehicle you occupy. An unoccupied car you parked may be removed. Some large cutscene models use smaller memory pages to reduce hangs in affected scenes. Performance depends on your hardware and settings.

### Languages

Choose **English, French, German, Italian, Spanish, Russian or Japanese** in **Settings → Display → Language**. Text includes missions, activities, statistics, graphics options and restored-dialogue subtitles. Japanese has its own font; pause-menu tabs use Latin letters because the Vice City menu font lacks Japanese characters.

Newly restored dialogue plays in English with translated subtitles. The inherited Russian speech, cutscene and radio archives remain present; their in-game selection has not been verified. Some new translations, especially Russian and Japanese, have not received native-speaker review. Vehicle, radio and brand names may remain English.

## Requirements

- An owned **GTA IV: The Complete Edition 1.2.0.59** installation.
- Allow **30 GB of free working space** for the download, reconstructed ZIP and extracted mod together, in addition to the installed base game and any backups. Direct extraction from the numbered parts avoids keeping another full ZIP copy.
- **Windows:** [Microsoft Visual C++ v14 Redistributable **x86**](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) (the runtime compatible with Visual C++ 2015–2022).
- **Linux / Steam Deck:** Proton.
- Optional **DualSense** controller. Keyboard/mouse and other controllers can use the normal game controls.

## Installation

1. **Install and check the base game.** Install your owned **GTA IV: The Complete Edition 1.2.0.59**, launch the unmodified game once through your normal launcher, then quit. Close the game before copying files. This package does not install GTA IV itself.
2. **Use a clean game folder.** Back up your saves and any files being replaced. A clean folder has no other Vice City conversion, extra scripts or third-party plugins already added. If your game is already modded, restore a clean copy or use a separate clean installation first; verifying the game alone may leave added mod files behind. If you reuse a previous mod folder, remove `plugins/VCAudioHeap.asi`, `plugins/VCMemory.asi`, `plugins/VCMusic.asi` and `plugins/VCScriptHook.asi` before launching this release. Their replacement, `plugins/VCCore.asi`, disables all of its embedded components if any old standalone file remains. Copying new files over an old build does not remove those old files. The existing INI filenames and the separate `ScriptHook.dll` are still used; keep those supplied in the new package.
3. **Download every numbered part from Assets:** `VCANGCE.Zip.001`, `VCANGCE.Zip.002` and every later numbered part shown. Save all of them in the **same folder**, keeping their names unchanged. They are pieces of one ZIP, not separate patches. This guide is available in the [repository README](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/blob/main/README.md) and inside the extracted package. Do not use **Code → Download ZIP** or the automatic **Source code** ZIP/TAR files.
4. **Extract once with [7-Zip](https://www.7-zip.org/).** Open `VCANGCE.Zip.001` in 7-Zip and extract to a new empty folder; it reads the other parts automatically. Do not extract each part separately. Windows File Explorer alone may not handle the split archive. On Linux, with 7-Zip installed: `7z x VCANGCE.Zip.001 -oVCANGCE-extracted`. If you were given the single full `VCANGCE.Zip` instead, extract that file directly; you do not also need the numbered parts.
5. **Find your GTA IV game folder.** This is the folder that contains **`GTAIV.exe`**. In Steam, right-click the game → **Manage → Browse local files**; in other launchers, use their installed-files/open-folder option. If the opened folder contains a `GTAIV` subfolder, open it to find the executable.
6. **Copy the mod into that folder.** Open the extracted **`release`** folder and copy **everything inside it** (`update`, `plugins`, `DLSSIV` and the loose files) into the folder containing `GTAIV.exe`. Merge folders and replace files when prompted. The result should have `GTAIV.exe`, `update` and `plugins` side by side. Do not place the `release` folder itself inside the game directory.
7. **Launch GTA IV normally and choose New Game, or load your existing save** (back it up first; see [Saves](#saves)). On Proton, see the loader setting below if the conversion does not load. A new game gives activities, statistics and parked cars their intended starting state.

To verify downloads, compare the numbered parts with **`PARTS-SHA256SUMS.txt`**. The single full ZIP has its own **`VCANGCE.Zip.sha256`** file. The extracted **`SHA256SUMS.txt`** verifies the mod's individual files; on Linux, run `sha256sum -c SHA256SUMS.txt` inside the extracted `release` folder.

### Linux / Steam Deck and graphics

If the mod loader does not start, set Steam's launch option to `WINEDLLOVERRIDES="dinput8=n,b" %command%`. In Lutris, use the DLL override `dinput8` = `n,b`.

For the supplied DLSS-IV setup, choose **Graphics API: Vulkan** and **Anti-aliasing: Off** in Display settings. DLSS and FSR availability depends on your GPU, driver and operating system. NVIDIA under Proton may require `PROTON_ENABLE_NVAPI=1`. The build was developed and played on Linux with Proton; Windows and Steam Deck configurations have not been comprehensively tested.

### DualSense setup

- **Connect before starting.** On Windows, connect the controller and launch the game. For Bluetooth, pair and connect it before launch.
- **Linux / Steam Deck:** disable Steam Input for this game in Steam **Properties → Controller**. In Steam Game Mode, set `SDL_GAMECONTROLLER_IGNORE_DEVICES=` to an empty value in the game's environment so Steam does not hide the controller. Lutris exposes this under **System options → Environment variables**.
- **KDE Plasma:** if the controller touchpad also moves the mouse/camera, disable **DualSense Wireless Controller Touchpad** under **System Settings → Touchpad**.
- **USB audio:** use the controller's sound device; Linux may expose an **Analog Surround 4.0** profile. Speaker and haptic behavior, particularly over Bluetooth, depends on the audio setup.
- If the light bar or controller effects do not initialize, check the controller connection and preserve current logs before another launch if you are reporting a failure.

## Controls

| Input | Action |
|---|---|
| **F7** / **LB + D-pad right** | Open Vice City Menu; D-pad up also opens it |
| Arrows / numpad / D-pad | Navigate menu |
| Enter / A · Backspace / B | Select · back |
| **Ctrl+F7** | Project Thunder menu |
| **Alt+V** | First-person camera |
| **N** in a vehicle · **Tab** on foot | Next radio track · holster weapon |
| Enter a full-size plane or helicopter | Automatically receive the wearable parachute; guns remain available |
| **Sprint** while falling with the pack | Open parachute |
| Movement controls / displayed air-brake controls | Steer / brake while parachuting |
| **Enter Vehicle** while parachuting | Cut away |
| Aim with fists, bat, knife, Pool Cue or Katana, then **Z / V / C / X / G** | Army/MMA moves; **F5** changes style |

DualSense uses the normal controller layout: Cross = A, Circle = B, Square = X and Triangle = Y.

## Settings and making your own changes

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

## Saves

Save at a safehouse or use **F7 → Gameplay Tweaks → Save Game Now** outside missions. Saves live in `Documents\Rockstar Games\GTA IV\Profiles\`; on Proton this is inside the game's Wine prefix. A save retains the parked vehicles it was created with.

### Updating with an existing save

**You can keep playing your existing save; a new game is not required.** This includes saves started with the previous release. Back up your saves before updating, as with any mod update.

**Why this matters.** A GTA IV save records two things from the mod that an update can break:

- **The radio station list.** Changing the stations changes the layout of the save, and older saves then fail to load.
- **The model number of every pickup placed by a script** (clothes, health, armour, hidden packages and similar). A save remembers each of these pickups by the model's position in the game's model list, not by its name. If an update inserts, removes or reorders model definitions ahead of a pickup's model, that pickup in older saves shows the wrong object, for example a clothes pickup drawn as a see-through sheet or a gate, even though collecting it still works.

**What this release does.** Both are kept exactly as in the previous release. The radio data is unchanged; two car engine-sound entries are only added after it. Every existing model keeps its number: the new models in this release, the extra Pole Position dancers, are listed after everything else.

**What was left out for this reason.** Two changes were tried during development and removed before this release: a nine-station radio update, which stopped existing saves from loading, and a cleanup of unused model definitions, which made pickups in existing saves show the wrong model. A new game was unaffected by either.

**Tested.** A long-running save carried over from earlier builds loads, plays and shows its pickups correctly, including the clothes pickup at the Ocean View hotel, and all the Pole Position dancers are present. The model order is also checked definition by definition against the previous release. Saves from releases older than the previous one were not checked individually.

## Testing and known issues

The retained gameplay features have positive general-play reports, including Pole Position after its accessory correction. Earlier save-loading and normal-play tests passed. After the parachute-script correction, the same high-speed windshield-ejection and rolling test that crashed stayed running normally; the user also confirmed the automatic aircraft pack, gun use while wearing it, deployment and landing. Steering was confirmed in earlier play testing. All the crashes reported during development (no-window startup, post-save, windshield-ejection/ragdoll and Riot) are confirmed resolved in play testing. Road tyre sounds, Skimmer water takeoff, motorcycle helmets, the blue Soiree suit in cutscenes and the new ground textures, seen from the ground and the air, are also confirmed in play testing, as are the latest asset, voice and consolidation changes in general play. The only parts not yet tested are strict weapon attribution in some rampages, and Windows and Steam Deck.

- **Startup and save loading:** the immediate post-save crash and the two no-window startup lock conflicts are corrected and confirmed resolved in play testing. A long-running save carried over from earlier builds loads, plays and shows its pickups correctly in this one. If a no-window failure ever appears on your system, preserve the current game/plugin/Proton logs before restarting Wine, and close a remaining `GTAIV.exe` process before trying again.
- **Ground appearance:** the new sand and ground corrections pass texture and mapping checks and are confirmed in play from the ground and while flying. Mixed distant hotel and Downtown textures retain their existing artwork.
- **Riot audio-memory correction:** a crash log identified a failed physical audio allocation followed by a null write. The audio-heap component now included in `VCCore.asi` increases the shared audio heap to 192 MiB before audio initialization. Build and guard checks pass, and the Riot crash is confirmed resolved in play testing: the mission was completed and the next one started. It writes its result to `plugins/VCAudioHeap.log`.
- **Latest changes:** Pole Position has positive club-entry and general gameplay reports after its accessory correction. The later resource, voice and consolidation changes pass static/build checks and are confirmed in general play.
- **Platform coverage:** development and play reports are primarily from Linux with Proton. Windows and Steam Deck have not been tested. The included Windows instructions are installation guidance.

When reporting a problem, include your operating system, game version, mission/location, steps to reproduce, added mods and relevant logs. Keep your save backed up.

The Vice City NextGen radio list is retained alongside the selected AVCNG sound configuration. The parachute includes its imported sound bank and events. The classic HUD adjusts its positions for the screen aspect ratio.

## Uninstalling

Restore backed-up files that the overlay replaced, then remove its added files using `SHA256SUMS.txt` as the file list. If no backups are available, restore or reinstall a clean game and remove any remaining mod files. Back up saves and personal files first; do not delete the Profiles folder to troubleshoot the mod.

## Developer sources

The repository’s [Source directory](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/tree/main/Source) has been updated to match this release (checkpoint 245, commit [`4ab3fe9`](https://github.com/dillonherring9/gta-iv-modern-ryzen-apu-config/commit/4ab3fe95249927585e19be3848674fb750b0fe7c)). It holds the code and text configuration: all 96 gameplay scripts, the VCCore and ScriptHook plugin sources with their builders, and the loader and model-definition files together with the model-order checker that keeps existing saves compatible. Fresh builds from it reproduce this release’s script archive, VCCore.asi and ScriptHook.dll exactly. Textures, models, audio and other binary assets are not in Source, so players need the release assets described above.

Release package: **VCANGCE.Zip**, prepared 2026-11-10 UTC, maintained build checkpoint 245.

## Credits

Vice City Actual Next Gen CE brings together the work of many creators. The original games, the Vice City conversion and the community assets belong to their respective authors. Our contribution is the Complete Edition integration, native adaptations, fixes and additions described in this release.

- **Rockstar Games and the original development teams** — Grand Theft Auto: Vice City, Grand Theft Auto IV and The Ballad of Gay Tony; the original city, characters, missions, voices, music, animations and game technology.
- **Revolution Team** — GTA Vice City NextGen Edition, the foundation that brings Vice City to GTA IV's RAGE engine.
- **Kn4ckers** — [Vice City for GTA IV: The Complete Edition](https://github.com/knackers4/vce), the Complete Edition base conversion used by this project.
- **GTATrilogyGames and the VCNE / DayL modding communities** — [GTA Vice City Actual NextGen Edition](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack), the 7.0.1 foundation and selected 8.0 material, including collected map, vehicle, weapon, sound and presentation work, and this release’s parachute and blue Soiree suit source assets.
- **ThirteenAG and FusionFix contributors** — [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix), the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) and [Xbox Rain Droplets](https://github.com/ThirteenAG/XboxRainDroplets).
- **ChunkLeChuck** — [DLSS-IV](https://github.com/ChunkLeChuck/DLSSIV). Its bundled components retain their own notices, including AMD FidelityFX, NVIDIA DLSS, Intel XeSS, DXVK and Khronos/Vulkan components; see `DLSSIV/licenses`.
- **llamacomma** — [Mjolnir IV](https://github.com/llamacomma/Mjolnir_IV), adapted for this build (MIT).
- **NodixTech** — [GTA IV DualSense](https://www.nexusmods.com/gta4/mods/1136), adapted here for Vice City and native controller support.
- **GeorgLegato** — [LinuxAudio4Dualsense5](https://github.com/GeorgLegato/LinuxAudio4Dualsense5), the Bluetooth speaker/haptics protocol (AGPL-3.0).
- **Xiph.Org Foundation and Opus contributors** — [Opus](https://opus-codec.org/) audio codec (BSD).
- **Un4seen Developments** — [BASS](https://www.un4seen.com/) audio library.
- **ItsClonkAndre** — [Project Thunder](https://github.com/ClonkAndre/ProjectThunderIV), adapted as Project Thunder CE (GPL-3.0); VWantedMusic, DeathMusicIV, VFlyingMusic, and the IV-SDK .NET native reference used during development.
- **DrAnusTart / Dr-AnusTart** — [Vercetti Estate Overhaul](https://www.nexusmods.com/gta4/mods/644), [Ocean View Hotel Overhaul](https://www.nexusmods.com/gta4/mods/585), [V-Rock Interior](https://www.nexusmods.com/gta4/mods/599), and Access Rosenberg's Office and HD Overhaul.
- **CyanFire** — [Vice City Nextgen Edition – Classic HUD](https://www.nexusmods.com/gta4/mods/667), reimplemented here as a native game script.
- **A.R Scorpion / A.R.Scorpion** — VCNG Cutscene Patch outfit models; **xdd5110** — the original cutscene-outfit model-loading concept and upstream script work.
- **MrOort** — [Proper HD Vehicles for GTA Vice City NextGen / Proper HD Era Vehicles – Lore Friendly](https://www.nexusmods.com/gta4/mods/1012). The pack credits **Myst, BigDudeManzo, Mr.Lorenzo, MeehmetEmin07, Excelsior, Budilov_Play, Saharov Channel, Svetlakov, Spartan112, AIGE, R&E, Peerless, RE, Vsoreny, YSS323, Dr_CHANI, Claude_Lib, DK22Pac, Thundersmacker, MGgames100, cromchen, Nitroflasche, K.E.Y (shaxtar), Milcin, Tonebee / twattyballs2011, Algonquin Hood, Ixl_Vanquish_MK, ThatOneHick, SimpleUnknown, KevinDV, deepcover0000, M4k3, G.C.T., direct_UA, Alex_SS, Ross [BloggerRoss], _CP_, dirprof, Diporrison's Lair, AXL163, Carface / Bigfoot2003, RoadRunner71, Intensified / Compeast, universetwisters, AlexandreRR, Milcin7, Rockstar Games and Ubisoft Reflections**. This carries forward the source pack's contributor credits; individual upstream variants may have been superseded in this CE build.
- **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000** — the 1978 Ford Fairmont VCPD model, conversion, updates and police livery.
- **R0NALDMCDONLD / RONALDMCDONLD** — [Vice City Next Gen Edition HD Pedestrians](https://www.nexusmods.com/gta4/mods/539); its page also credits **Riot-MH** for the US Army pedestrian.
- **Diya / _BasicallyDiya** and **XCLSIOR** — [Cutscene Retexture Project](https://libertycity.net/files/gta-4/222231-cutscene-retexture-project-vice-city.html), including character, prop, clothing and environment textures and the Ken Rosenberg/Mercedes retextures.
- **Lukakion** — Improved Tommy’s Clothes, credited by the upstream 8.0 package. The blue Soiree suit and its clean/wounded cutscene forms are adapted from that package’s player and Cutscene Retexture Project assets.
- **Rockstar Games / The Ballad of Gay Tony** — the original parachute scripts, models, animations and sounds used by the native parachute integration supplied through 8.0 and adapted for CE here.
- **LotsofEds / ServalEd** — [MoveImprove](https://github.com/LotsofEds/MoveImproveIV-SDK), whose parachute animation/ragdoll compatibility predicate is translated into the existing native movement script.
- **mmtsrn1** — [New Tire Skid Sound / BeamNG Sound Mod V2](https://www.beamng.com/threads/new-tire-skid-sound-updated.9056/), adapted into the road-skid sounds and matching controller audio.
- **metalwars** and **Kevin1230san** — the Pole Position nude dancer models, including the source models used for the added dancer variations.
- **BrynnaDaRosa** — **Playboy Bunny Niko Bellic**, the source of the black bunny ears, bow-tie collar and cuffs adapted for two Pole Position dancers. The original author’s page is [Brynna on Patreon](https://www.patreon.com/Brynna).
- **CGP** — Enhanced Fighting And Euphoria, with features rebuilt natively here.
- **catsmackaroo, ItsClonkAndre and LibertyTweaks contributors** — [LibertyTweaks](https://github.com/catsmackaroo/LibertyTweaks), with selected features reimplemented natively; **Zolika1351** — ZMenu, a reference for the F7 menu.
- **Arcenciel1991** — [GTA VC Actual NextGen Japanese Translation](https://www.nexusmods.com/grandtheftautovicecity/mods/695), including its Japanese font.
- **AIO All Classic Language Patch v2.0 creators**, shared by **Contremaitre** — French, German, Italian and Spanish text, fonts and translated images. Contremaitre is credited as the known source/distributor, rather than assigned sole translation authorship.
- **Fizzora** — Interiors and Cutscenes Fixes (IaCF_VC), as credited by the upstream package; **Ivan992** — Gator Keys.
- The creators of the inherited **Various Map Edits, Starfish Mansion Overhaul Project, Realistic Handling & Physics (Shturmovik2), VCNE HD map pack, RestoredWeapons, Cutscene Recognition, Mint Ocean Waters and TreesMod**. Some author identities were not recoverable from the preserved packages; their work is still acknowledged by its published mod name.
- **gutbash** — [revd](https://github.com/gutbash/revd), technical reference for the physical audio heap location and 192 MiB capacity precedent. The included audio-heap plugin is independent project code; revd’s engine-slot relocation and config writer are not included.
- **Sergeanur** — [SCFIX-Miami](https://github.com/Sergeanur/SCFIX-Miami), an original Vice City script reference for activities, population, parked cars and restored dialogue.
- **Alexander Blade / scocl** and **SC-CL contributors** — compiler/header/native-reference work used for native script development; **FourFury** — RAGE-format tooling used during inspection and authoring. These development credits do not mean the tools are player requirements.
- **GTABase, GTAMods Wiki and the wider GTA modding community** — vehicle and format references, documentation and shared research.

Earlier project prototypes used **Nixolas1**’s 2009 GTA IV parachute behavior reference, **Quechus13**’s canopy models, and **gongel**’s [GTA V backpack port](https://www.nexusmods.com/gta4/mods/888). Those controller, canopy and backpack implementations have been replaced by the native 8.0-derived integration in this release; this acknowledgment preserves their development contribution.

If a name or contribution is missing or incorrect, please open a GitHub issue with the original mod link or readme so the credit can be corrected. This is an unofficial fan project; these credits do not imply endorsement by the original authors.
