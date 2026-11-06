# 🌴 Vice City Actual Next Gen CE — selective 8.0 integration

**Grand Theft Auto: Vice City, rebuilt inside GTA IV: The Complete Edition 1.2.0.59.** This mod combines the Complete
Edition conversion with AVCNG 7.0.1 and selected compatible 8.0 content, alongside project fixes and features. FusionFix
5.1.2, DLSS-IV and native DualSense support are included. The mod does not require ScriptHook .NET or a .NET runtime.
You need your own copy of the game. The mod does not replace `GTAIV.exe` or Rockstar base archives.

> [!IMPORTANT]
> This release builds on the work of many mod authors. All credit goes to them; see [Credits](#-credits).

> [!NOTE]
> **Adult content:** the Pole Position strip club's dancers are nude, and so is the stripper in the club's purchase
> cutscene. Nudity appears only inside the club.

## ❓ What is this?

Play the story of 2002's **GTA Vice City**: Tommy Vercetti, the full city, its missions, characters, cars,
radio and 1980s Miami. It runs on **GTA IV's engine**, with GTA IV's lighting, physics, Euphoria ragdolls and driving.

It builds on three fan projects, with selected additions from AVCNG 8.0:

1. **Vice City NextGen Edition** by Revolution Team converted Vice City's map, missions and characters into GTA IV.
2. **Vice City for GTA IV: The Complete Edition** by Kn4ckers made that conversion run on GTA IV: The Complete Edition.
3. **GTA Vice City Actual Next-Gen Edition 7.0.1** (**AVCNG**, also called VCANGE) by GTATrilogyGames and contributors
   added a big modpack on top: reworked missions and scripts, restored weapons and aircraft, extra vehicles and map
   areas, new sounds, HD textures and map edits.

The package is a single overlay for Complete Edition. The selected 8.0 work preserves the project's existing assets
and mission choices; it is not a wholesale copy or a claim that every 8.0 difference has been ported.

The build includes fixes for missing resources, weapon animations, collision and sound configuration, plus native
scripts and plugins for the HUD, music and gameplay features described below.

## 🆚 How this differs from the original AVCNG 8.0

The original **GTA Vice City Actual Next-Gen Edition 8.0** is the work of GTATrilogyGames and contributors. Its main
package is designed around an existing Vice City NextGen installation and provides separate optional components. This
release is our **Complete Edition adaptation**, combining our existing 7.0.1-based CE build with selected 8.0 material;
it is not a wholesale copy of 8.0, and we have not evaluated every 8.0 option or script difference.

| | Original AVCNG 8.0 main package | This Complete Edition port |
|---|---|---|
| **Starting point** | Its instructions call for a Vice City NextGen installation, cleanup and then the main files. Some options are separate. | One consolidated overlay for a clean, owned GTA IV: The Complete Edition 1.2.0.59 install. No downgraded executable is supplied. |
| **Scripts** | Includes IV-SDK .NET and managed components such as ViceTweaks and MoveImprove. | The included features use native scripts and plugins; no .NET runtime or ScriptHook .NET is required. Our implementations are not a claim to reproduce every 8.0 tweak. |
| **Content** | Includes its own 8.0 assets, edits and mission changes. | Selects compatible 8.0 additions while retaining this project's HD assets, working repairs and native systems. Examples include wounded cutscene outfits, mall furniture adjustments, mainland neon corrections and Gator Keys lights. |
| **Missions and languages** | Has its own mission-script and language setup. | Includes our restored dialogue and gameplay changes, with English, French, German, Italian, Spanish, Russian and Japanese text files. The optional Gonzalez escape scene is not included, preserving the balcony route. |

The original and this port are related projects, not interchangeable file sets. Do not install this on top of the
original AVCNG or mix individual files from the two. Additional features such as our population, controller support and
native HUD are described below; individual features still need their own playtesting.

## 🛠️ A clean starting point for modding

This release is designed to be a consolidated, approachable base for making your own changes. Install it on a clean game,
play it for a while, and see what you actually want to change before adding more mods. Keep a backup of your working setup,
read each mod's requirements and add changes one at a time so you can spot conflicts.

**You are free to try a compatible ScriptHook .NET installation or other .NET mods separately if you want.** This release
does not need or include them for its own features. We have not tested optional .NET hooks or arbitrary third-party mods
with this build; version, loader, script and resource conflicts are possible. Back up your setup first and check that each
add-on supports GTA IV: The Complete Edition 1.2.0.59. Avoid adding multiple loaders or overwriting included files without
understanding what the replacement does.

## ✨ Features

### 🌴 Vice City

- Vice City NextGen (Revolution Team) through Kn4ckers' Complete Edition release, with AVCNG 7.0.1's reworked scripts and
  missions, restored weapons and aircraft, extra vehicles, extra map areas (Everglades, Gator Keys and more), fonts and
  loading screens, plus selected compatible assets and improvements from AVCNG 8.0.
- Classic Vice City activities as native scripts:
  - **35 rampages 💀** at their 2002 places, with briefings, countdowns and kill counters. You are given the rampage
    weapon. Vehicle rampages count cars destroyed; weapon attribution in some cases still needs further testing.
  - **Hidden packages** at their 2002 places, including inside the police station and the mall, with the 2002 rewards
    (chainsaw, Python, flamethrower, minigun and more).
  - **Business revenue:** a gold **$** at every business you own fills up each day; walk in to collect it.
  - **Shop hold-ups** at 12 stores, **fast-food counters** that heal you, **Leaf Links security**, the Downtown shooting
    range, **insane stunt bonuses** and wheelie/stoppie bonuses.
- **2002-style pickups:** a clean adrenaline pill, the rampage skull, **save cassettes** at every save point, and
  **green / red house icons** at properties that are for sale or still locked. Health is GTA IV's own green first-aid kit.
- **2002-inspired population and traffic:** areas use people, gangs and traffic based on 2002 Vice City, by day and by
  night: Cubans in Little Havana, Haitians in Little Haiti, bikers downtown, soldiers at the army base, workers at the
  airport and docks, and golfers in Caddies at Leaf Links. Street traffic is made of 2002 cars, matched to each area's
  2002 mix of cheap and expensive cars, and the boats out on the water are the 2002 leisure and work boats.
- **Parked vehicles:** parked cars stand at their 2002 spots with their 2002 models, the 2002 car
  spawns are back, and vehicles with no 2002 home are parked somewhere that makes sense (the Biplane at the airport, the
  Armadillo on a downtown roof pad, Hotring cars beside the stadium, the RC Baron by the RC race van). Mission and reward
  vehicles still come with their missions.
- **Completion counted out of 154, like 2002:** rampages, the shooting range and shop hold-ups count towards it.
- **A Vice City stats screen:** rampages passed, stores knocked off, properties and assets owned, revenue collected,
  people saved, fires put out, pizzas delivered, hidden packages found, answered incoming calls and your best paramedic
  and firefighter levels. Twelve unique tracked shop robberies display full credit of 15; they still award the existing
  one-time completion point.
- **Contextual tutorials** explain wanted levels, hospital and police station, bribes, motorcycle balance, hotwiring and
  resisting disarm, with control-aware prompts.
- **Club crowds react:** fire a gun or hit someone inside the Malibu or Pole Position and the crowd stops dancing; most
  run from you and some cower.
- **Walk into Ken Rosenberg's office** in free roam as well as in missions: the Washington Beach building has its
  corridors, stairwell, collision and HD office.
- **AVCNG's map edits:** the overhauled Vercetti mansion inside and out on Starfish Island, Ocean View Hotel room 317,
  the Malibu Club interior, the V-Rock building, Phil's place and the junkyard, the airport access road, the Little
  Havana coast road, the Leaf Links walls and hedges, and 1980s street litter, plants and phones.
- **Selected 8.0 map work:** adjusted mall furniture heights, corrected some mainland neon texture references and added
  Gator Keys nighttime accent lights. The pause map has transparent surrounding water and mipmapped map tiles.
- **The airport seaplane dock is back:** walk down its stairs onto the deck, or pull up to it by boat.
- **Parachute:** you carry one in any helicopter or plane. Jump, press **Sprint** to open, steer with the movement keys
  or stick, press **Enter Vehicle** to cut away. It closes by itself when you land. Real parachute animations from
  *The Ballad of Gay Tony*, and Tommy wears a GTA V-style parachute backpack while he carries one.
- **Amphibious aircraft:** the Skimmer floats, idles and takes off again from the sea, and the Sea Sparrow lands on
  water. Nobody drowns sitting in them.
- **Rhino:** its cannon fires explosive shells.
- **Phnom Penh '86 and Supply & Demand:** the helicopters and the boat have twice the health, and they only blow up when
  their health meter runs out.
- Story-based wanted limits and a six-star army response.
- Pole Position strip club with its own dancers (club only), and Independence FM as a tenth radio station.

### 🚗 Real-world physics

- **Every vehicle has its own handling**, tuned to the real vehicle it's based on: what the community says each Vice City
  car is modelled on, checked against the car's own model. Weight, power and acceleration, top speed, gears, front-,
  rear- or four-wheel drive, brakes, tyre grip, ABS where the real car had it, and a centre of gravity placed by the real
  car's front/rear weight split. A few examples: Infernus = Lamborghini Countach, Cheetah = Ferrari Testarossa,
  Turismo = Ferrari F40, Banshee = Corvette C4, Stinger = Ferrari Daytona Spyder, Comet = Porsche 911, Sentinel = BMW 535i,
  Sentinel XS = BMW M3, Deluxo = DeLorean, Bullet = Ford GT, Patriot = Humvee.
- Supercars are tuned toward their real counterparts; 1950s cruisers, vans and trucks use slower, heavier settings.
  GTA IV donor vehicles retain their suspension geometry. Measured in-game performance varies with game physics.
- **Boats** are tuned toward real-world counterparts, and the **RC Bandit** uses handling for a large RC buggy.
- **People weigh what real people do:** about 56–66 kg for women and 76–81 kg for men, more for big builds and for
  uniformed police, firefighters and guards. **Tommy weighs 92 kg (203 lb).**
- **Street furniture has real weights:** lamp posts, signs, bins, benches, hydrants, mailboxes, newspaper boxes, the beach
  ball and shop windows (by their glass). Cars no longer stop dead against a rubbish bag.
- **Odd-job time limits fit the real vehicles:** Paramedic, Firefighter, Vigilante, Pizza Boy, Taxi and the Bloodring get
  the extra time their slower vehicles need.

### 🎮 DualSense

- **PlayStation 5 DualSense support is built in.** A native virtual controller is included, without DS4Windows, ViGEm
  or Steam Input. USB and Bluetooth behavior depends on the operating system and controller setup.
- **Adaptive triggers** for driving and shooting: throttle and brake resistance, ABS stutter, redline buzz, road-surface
  texture and weapon trigger feel.
- **HD haptics matched to Vice City:** vehicle and weapon mappings select engine, firing and reload sound effects.
  Impacts, explosions, rough roads, gear changes and nearby traffic have haptic effects too. Individual mappings and
  hardware configurations have not all been tested; game sound remains on the PC or TV.
- **The controller speaker plays only what matters:**
  - a **police siren** while you're wanted, louder the closer the police are and silent when they're far away;
  - a **heartbeat** when you're nearly dead (20% health or less), faster as your health drops. It plays on your
    PC/TV and on the controller at the same moment, and stops when you heal.
  Your game sound stays on your PC or TV.
- **The light bar shows your health:** green at full health, yellow at half, red near death. While you're wanted it
  flashes red and blue like police lights.
- Supports **Bluetooth and USB**; connection, haptics and speaker behavior depend on the system and audio setup.
- **Controller speaker volume** right in the game: **Settings → Audio**, just under Music Volume, 0–100% in 5% steps.
- Player LEDs, touchpad gestures, and optional gyro aiming and motion steering.

### 🖥️ Classic HUD

- The **classic Vice City HUD**, rebuilt as a native game script: clock, rolling eight-digit money counter, health and
  armour with icons that flash when you're hit, the weapon icon with reserve and clip ammo, and six wanted stars that
  flash and grey out like the original.
- The classic pink Vice City radar ring.
- It hides itself in cutscenes, fades, the Pay 'n' Spray, when you're wasted or busted, and with the game's HUD option.
  **F7 → World → Hide HUD** and **F7 → Gameplay Tweaks → No HUD in Cinematic Camera** hide it too.

### 🎬 Cutscenes and health

- **Cutscenes show your outfit:** for the standard outfits, Tommy's high-detail cutscene model wears the clothes you have
  on. Custom outfits use his default cutscene look. Supported outfits also have matching wounded cutscene models.
- **Full health** when a cutscene starts and ends.
- **Low-health recovery:** when your health bar is below 25% and nothing has hurt you for 3 seconds, it refills 1% per
  second up to 25%. Any hit pauses it, so it never makes you invincible.

### 🔊 Sound and music

- **AVCNG sound:** AVCNG's weapon, horn, tyre, footstep, ambience and engine sounds, death and start tunes, and the
  police scanner's Vice City car names.
- **Proper sounds for 23 vehicles and 13 weapons** that would otherwise use generic ones, including the Fairmont VCPD,
  the VCN Maverick, the Rio, the Coast Guard boat, the Speeder, the Sea Sparrow, the Infernus, the Pizza Boy, the Tec-9,
  the minigun, the sniper rifle, the M16 and the AUG.
- **2002 mission dialogue:** 62 original lines added across missions at selected moments, with subtitles in seven
  languages. They span 13 missions: the French courier taunting you in **Mall Shootout**, Lance calling for help in **Guardian Angels**, the soldiers and the
  tank crew in **Sir, Yes Sir!**, the Colonel and his helicopter runs in **All Hands On Deck!**, Candy and Mercedes in
  **Recruitment Drive**, the builders and guards who turn on you in **Demolition Man**, plus **Supply & Demand**,
  **Gun Runner**, **Cap the Collector**, **Keep Your Friends Close**, **Four Iron**, **Back Alley Brawl** and **Waste
  the Wife**. The script checks for an existing conversation before playing a line.
- **The Job and Cortez:** a damaged-car crew conversation and a police-station weapon warning use original English
  recordings with subtitles. An optional restored cut Cortez call can arrive after Diaz; it is not represented as an
  active retail 2002 mission event.
- **Original 2002 voices and sounds restored:** Tommy orders at the food stands and the staff answer (Burger Shot, Cheezy
  Crust, Shaft and Bandit each have their own lines), the Leaf Links guards speak when you walk in armed, the Hotring
  announcer and crowd call the race, the Pole Position dancers talk during a private dance, and the Cherry Popper
  purchase scene ends with its original sound.
- **Wanted, death and flying music:** chase music from 3 stars, music when you die or are nearly dead, and music while
  you fly. The music pauses with the menu.

### 🗣️ Languages

- Play in **English, French, German, Italian, Spanish, Russian or Japanese**: pick the language in
  **Settings → Display → Language**.
- The included text covers missions, titles, activities, statistics, graphics options and restored dialogue subtitles.
  Some newly written translations, especially Russian and Japanese, have not been reviewed by native speakers; display
  and individual language combinations need further gameplay testing. Vehicle, radio and brand names may remain English.
- The newly restored dialogue uses English audio with translated subtitles. The inherited Russian speech, cutscene and
  radio archives remain included; whether and how they are selected in game has not been verified.
- **日本語:** Japanese text has its own Japanese font. The pause menu tabs use Latin letters because
  the Vice City menu font has no Japanese characters.

### 🎨 Look

- Proper HD Era Vehicles with every vehicle texture at 1024 pixels and crisp badges and logos.
- Photoreal Tommy, cutscene and story characters, pedestrians, props, palms, grass, sand, water and city surfaces.
- **Street lamps where the original 2002 Vice City had them**, using the original lamp designs. The lamps are breakable and light up at night.
- Solid palm and tree trunks: you crash into them instead of driving through. Bushes, hedges and ferns stay passable.
- Distant palms and buildings use models matched to their close-up counterparts.
- HD map textures, IaCF_VC and CRTP interiors, 1980s Miami pedestrian clothing and crowds for every district by time of day.
- Turquoise Miami ocean and pools, Mint water, a Miami sky and a 1980s colour grade.
- Project Thunder lightning and storms ⚡, Xbox rain droplets, and DLSS / FSR upscaling through DLSS-IV.

### 🔫 Gameplay

- Realistic Handling & Physics as the base for the real-world tuning above, with realistic weight and balance for
  helicopters, planes and RC toys as well.
- The 1978 Ford Fairmont as the VCPD police car.
- Restored weapons and animations; 20 guns with real-world damage, fire rate, magazines and range; rifles pierce armour.
- Realistic Euphoria ragdolls for pedestrians, GTA IV's default ragdoll for Tommy, and Army/MMA melee moves.
- Native **F7 Vice City Menu** with Gameplay Tweaks (save anywhere outside missions, skip radio track, holster and more).

### ⚙️ Performance and stability

- **Mjolnir IV:** adjusts CPU affinity and busy-wait behavior and turns off the engine's automatic quality limiter.
  Performance depends on hardware and settings.
- **Cutscene memory flush:** the plugin clears ambient cars, pedestrians and objects before a cutscene and pauses traffic
  until it ends. Mission characters and the car you're sitting in stay. A car you
  left parked doesn't, so steal another one.
- Some large cutscene models are split into smaller memory pages to reduce hangs in affected scenes.
- **DLSS / FSR** through DLSS-IV provide upscaling options. Visual quality and frame rate depend on hardware and settings.
- Selected files and dependencies have received static checks. This is not a gameplay or hardware compatibility guarantee.

## 📋 Requirements

- Your own copy of **GTA IV: The Complete Edition 1.2.0.59**. No downgrade.
- At least 12 GB of free disk space for the mod files; allow additional space for backups.
- Windows: the **Microsoft Visual C++ 2015–2022 Redistributable (x86)** (most PCs already have it).
- Linux / Steam Deck: Proton.
- Optional: a **DualSense** (PS5) controller. Any other controller, keyboard and mouse work as usual.

## 📦 Installation

1. **Prepare the game.** Close it and start with a clean GTA IV: The Complete Edition 1.2.0.59 installation. Launch
   the unmodified game once, then quit. Remove conflicting mods through their own documented uninstall instructions;
   do not delete saves. Back up any game-folder files that the supplied file list says the overlay would replace.
2. **Download every release part.** Get all `AVCNGCE8.zip.001`, `.002` and later numbered parts from the GitHub
   release assets and put them together in one folder. They are pieces of one archive, not separate mods. The GitHub
   automatically generated “Source code” downloads are not the playable release.
3. **Extract once.** Install 7-Zip, open `AVCNGCE8.zip.001` and extract it to a new empty folder. Keep all numbered
   parts beside it. On Linux, run `7z x AVCNGCE8.zip.001 -oAVCNGCE8-extracted`. Windows File Explorer alone may
   not open the split archive. Extraction produces a `release` folder.
4. **Copy the mod.** Open that `release` folder and copy its contents—including `update`, `plugins`, `DLSSIV` and the
   loose files—into the game directory beside `GTAIV.exe`. Keep the folder structure intact. This is a mod overlay,
   not a standalone copy of the game.
5. **Start playing.** Choose **New Game**. Make a spare save, quit, relaunch and load it. Older saves and saves from
   other mod configurations have not been certified compatible; back up your own saves before testing.

You can check the downloaded parts against `PARTS-SHA256SUMS.txt`. The extracted `SHA256SUMS.txt` lists the individual
mod files; on Linux, run `sha256sum -c SHA256SUMS.txt` inside the extracted `release` folder.

On Linux / Steam Deck, if the loader does not start, set the Steam launch option
`WINEDLLOVERRIDES="dinput8=n,b" %command%` (in Lutris: DLL override `dinput8` = `n,b`). The build was developed and
played on Linux with Proton. Windows and Steam Deck setups may behave differently and are not comprehensively tested.

**Graphics:** For the included DLSS-IV setup, select **Graphics API: Vulkan** and **Anti-aliasing: Off** in the Display
settings. DLSS and FSR support depends on the GPU, driver and operating system. NVIDIA under Proton may require NVAPI
(`PROTON_ENABLE_NVAPI=1`).

### 🎮 DualSense setup

- **Windows:** connect the controller and start the game.
- **Linux / Steam Deck:** turn **Steam Input off** for the game (Steam: game *Properties* → *Controller* → *Disable
  Steam Input*). If you launch from Steam's Game Mode, also add `SDL_GAMECONTROLLER_IGNORE_DEVICES=` (empty) to the
  launch options or the game's environment variables (Lutris: *System options* → *Environment variables*), so Steam
  doesn't hide the controller from the game.
- **Bluetooth:** pair and connect the controller **before** you start the game. If the game hangs while starting or the
  light bar stays dark, turn the controller off, reconnect it, and start the game again.
- **KDE Plasma:** if the touchpad moves the camera like a mouse, open *System Settings* → *Touchpad*, select
  **DualSense Wireless Controller Touchpad** and untick **Device enabled**.
- For USB audio, use the controller's own sound device; on Linux, select its **Analog Surround 4.0** profile where
  available. Bluetooth speaker and haptic support is included but varies by system.

## 🎮 Controls

| Input | Action |
|---|---|
| **F7** / **LB + D-pad right** | Vice City Menu (D-pad up also opens it) |
| Arrows / numpad / D-pad · Enter / A · Backspace / B | Move · select · back in the menu |
| **Ctrl+F7** | Project Thunder menu |
| **Alt+V** | First-person camera |
| **N** in a vehicle · **Tab** on foot | Next radio track · holster weapon |
| **Sprint** while falling from an aircraft · **Enter Vehicle** | Open the parachute · cut away |
| Aim with fists, bat, knife, Pool Cue or Katana, then **Z / V / C / X / G** | Army/MMA moves (**F5** switches style) |

A DualSense uses the standard controller layout (Cross = A, Circle = B, Square = X, Triangle = Y).

## ⚙️ Settings

- `plugins/GTA_IV_DualSense.ini` (created on first start): haptic and trigger strength, which effects are on, light bar
  brightness and its colour in menus, player LEDs, touchpad gestures, gyro aiming and motion steering.
- `plugins/VCScriptHook.ini`:
  - `[ControllerAudio]`: controller speaker on/off and volume (the volume is also in Settings → Audio), Bluetooth audio
    on/off, haptic strength; which kinds of effects also play on the speaker (`SpeakerEngine`, `SpeakerRoad`,
    `SpeakerImpacts`, `SpeakerWeapons`, `SpeakerExplosions`, `SpeakerAmbient`, `SpeakerOther`, all 0 = haptics only;
    `SpeakerFilter=0` puts every effect on the speaker); the police siren (`Siren`, `SirenVolume`, `SirenRange` in metres)
    and the heartbeat (`Heartbeat`, `HeartbeatBelowPercent`, `HeartbeatControllerVolume`, `HeartbeatTvVolume`).
  - `[Lightbar]`: `HealthColours` (health colours) and `PoliceFlash` (red/blue flash while wanted), 1 = on, 0 = off.
- `plugins/AudioMap.ini`: which sound each vehicle and weapon plays on the controller. Editing it is safe.
- `plugins/VCMusic.ini`: `[VC2002Sounds]` turns the restored 2002 voices on or off and sets their volume. Turn each kind
  of music on or off, set the volume and fades, and choose how many stars start the chase music. Add your own `.mp3`,
  `.wav` or `.ogg` files to `plugins/VCMusic/Wanted`, `Death` or `Flying` (up to 16 each).
- `plugins/VCMemory.ini`: the cutscene flush (on/off, clear radius, model budgets, traffic pause) and `[Outfits]`
  (`Enabled=1` makes cutscenes follow your outfit). The same plugin adds Japanese to the language menu.
- The plugins write logs next to themselves (`VCMusic.log`, `VCMemory.log`, `VCScriptHook.log`, `mjolnir_cpu_fix.log`).

## 💾 Saves

Save at a safehouse or with F7 → Gameplay Tweaks → Save Game Now (not during missions). Saves are in
`Documents\Rockstar Games\GTA IV\Profiles\` (on Proton, inside the game's Wine prefix).

## 📝 Notes and testing

- The live build has received general successful playtest reports. Individual missions, new map features, translated
  subtitles, every controller configuration, and save/reload combinations have not all been separately verified.
- An intermittent startup failure with a running process but no game window has occurred. Its cause is unknown; it has
  also appeared with different changes removed. If it occurs, preserve the game and Proton logs for diagnosis.
- AVCNG sound configuration is merged selectively; the Vice City NextGen radio list is retained. The parachute has no
  sounds. Previously existing Russian archives remain included, but their in-game activation is unverified.
- Start a New Game for the rampage, revenue and stats counters to start from zero and for the 2002 parked cars. A save
  keeps the parked cars it was made with. Newly authored translations are not all native-speaker reviewed.
- The Classic HUD's positions follow the screen's aspect ratio. Real-world handling makes some older and utility cars slow.

## 🗑️ Uninstalling

Restore the backups of any files that the mod overwrote and remove the mod-added files listed in `SHA256SUMS.txt`.
If you do not have those backups, reinstall or verify a clean copy of the game and remove any remaining mod files. Back up
your saves and personal files first. Saves are in `Documents\Rockstar Games\GTA IV\Profiles\` (inside the Wine prefix
on Proton); do not delete that folder as a troubleshooting step.

## 🙏 Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce)
- **GTATrilogyGames** and contributors: [GTA Vice City Actual Next-Gen Edition (AVCNG / VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack), including its sounds and its Various Map Edits
- Authors of the map edits collected in AVCNG: the **Starfish Mansion Overhaul Project** and **Vercetti Mansion
  Overhaul**, the **V-Rock building** interior, the **Ocean View Hotel** rework and **Interiors and Cutscene Fixes (IaCF_VC)**
- **DrAnusTart**: Access Rosenberg's Office and HD Overhaul
- **CyanFire**: Vice City NextGen Edition Classic HUD (rebuilt here as a native script)
- **A.R Scorpion** (cutscene outfit models) and **xdd5110** (the original cutscene outfit concept): VCNG Cutscene Patch
- **NodixTech**: [GTA IV DualSense](https://www.nexusmods.com/gta4/mods/1136), adapted here for Vice City
- **GeorgLegato**: [LinuxAudio4Dualsense5](https://github.com/GeorgLegato/LinuxAudio4Dualsense5) (AGPL-3.0), the DualSense
  Bluetooth speaker and haptics protocol
- **Xiph.Org Foundation**: [Opus](https://opus-codec.org) audio codec (BSD licence)
- **llamacomma**: [Mjolnir IV](https://github.com/llamacomma/Mjolnir_IV) (MIT licence)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **ItsClonkAndre**: Project Thunder (GPL-3.0), as Project Thunder CE; VWantedMusic, DeathMusicIV and VFlyingMusic; the
  IV-SDK .NET native list
- **SC-CL** authors: the GTA IV native name table
- **Un4seen Developments**: BASS audio library
- **DLSS-IV** author
- **AIO All Classic Language Patch v2.0** author, shared by **Contremaitre**: French, German, Italian and Spanish text,
  fonts and translated images
- **Arcenciel1991**: the Japanese translation and Japanese font (GTA VC Actual NextGen 日本語化 mod)
- **1978 Ford Fairmont VCPD:** **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000**
- Author of the 2009 GTA IV **parachute mod** (`para.asi`): canopy model and the parachute behaviour
- Author of **"Parachute port from GTA 5 to GTA 4"**: the parachute backpack
- **Rockstar Games** (*The Ballad of Gay Tony*): the parachute animations
- **metalwars** and **Kevin1230san**: the nude female models used for the Pole Position dancers
- **CGP**: Enhanced Fighting And Euphoria (rebuilt natively)
- **LibertyTweaks** authors (rebuilt natively); **Zolika1351**: ZMenu (inspiration for the F7 menu)
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (Shturmovik2), **VCNG HD Pedestrians**,
  the **VCNE HD map pack**, **RestoredWeapons**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters** and **TreesMod**
- **Sergeanur**: [SCFIX-Miami](https://github.com/Sergeanur/SCFIX-Miami), the 2002 Vice City script reference for the
  rampage, hidden package, population, parked-car and mission-dialogue data
- **GTABase** and the GTA community wikis: which real vehicle each Vice City car is based on
- **Rockstar Games**: Grand Theft Auto: Vice City (including the original voices, mission dialogue and sounds restored
  here) and Grand Theft Auto IV (including the police siren used on the controller speaker)

## ⚖️ Disclaimer

Unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or Sony Interactive Entertainment. It
contains only modified and added files; you need your own legal copy of GTA IV: The Complete Edition. Please support
the original mod authors.
