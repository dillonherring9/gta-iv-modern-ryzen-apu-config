# 🌴 AVCNG CE 7.0.1: GTA Vice City Actual Next Gen for GTA IV: The Complete Edition

**Release: AVCNGCE701**

**Grand Theft Auto: Vice City, rebuilt inside GTA IV, running natively on GTA IV: The Complete Edition (1.2.0.59).**
Windows, Linux and Steam Deck (Proton). FusionFix 5.1.2, DLSS-IV and full DualSense support come included and set up.
Nothing else to install: no .NET, no ScriptHook .NET, no ViGEm, no downgraded game. `GTAIV.exe` and Rockstar's original
data archives are never touched.

> [!IMPORTANT]
> This release builds on the work of many mod authors. All credit goes to them; see [Credits](#-credits).

> [!NOTE]
> **Adult content:** the Pole Position strip club's dancers are nude, and so is the stripper in the club's purchase
> cutscene. Nudity appears only inside the club.

## ❓ What is this?

You play the whole story of 2002's **GTA Vice City**: Tommy Vercetti, the full city, its missions, characters, cars,
radio and 1980s Miami. It runs on **GTA IV's engine**, with GTA IV's lighting, physics, Euphoria ragdolls and driving.

It is built from three fan projects, and you don't need to download any of them. Everything is in this package:

1. **Vice City NextGen Edition** by Revolution Team converted Vice City's map, missions and characters into GTA IV.
2. **Vice City for GTA IV: The Complete Edition** by Kn4ckers made that conversion run on GTA IV: The Complete Edition.
3. **GTA Vice City Actual Next-Gen Edition 7.0.1** (**AVCNG**, also called VCANGE) by GTATrilogyGames and contributors
   added a big modpack on top: reworked missions and scripts, restored weapons and aircraft, extra vehicles and map
   areas, new sounds, HD textures and map edits.

**AVCNG CE 7.0.1** is AVCNG 7.0.1 rebuilt so that it runs on the current Complete Edition, on Windows, Linux and Steam
Deck. Its crashes are fixed, and it adds 2002 Vice City features, real-world physics, a classic HUD and full DualSense
support.

## 🔁 How it differs from the original AVCNG 7.0.1

The original AVCNG 7.0.1 is made for Windows. It replaces your game's `GTAIV.exe` with the 2008 version 1.0.8.0 plus
Games for Windows Live's `xlive.dll`, and many of its features run through .NET script hooks. On Linux (Proton) it did
not work for us. AVCNG CE leaves your game as it is and runs everything natively:

| Original AVCNG 7.0.1 | AVCNG CE 7.0.1 (this release) |
|---|---|
| Replaces `GTAIV.exe` with the 2008 1.0.8.0 executable and adds `xlive.dll` | Your own GTA IV: The Complete Edition 1.2.0.59; `GTAIV.exe` and Rockstar's archives stay untouched |
| .NET script hooks (IV-SDK .NET, ScriptHookDotNet) running .NET scripts for the wanted, death and flying music, traffic, Classic HUD and tweaks | No .NET at all: those features are rebuilt as native game scripts and native plugins |
| ScriptHook, ZolikaPatch, IVTweaker, XLivelessAddon, V-Handling and the ZMenu plugin | FusionFix 5.1.2 and a native **F7 Vice City Menu** |
| A Windows clean-up batch file and a list of optional packs to pick from | Copy one folder into a clean game; the options are already chosen and merged |
| Made for Windows; did not run for us on Linux | Windows, Linux and Steam Deck (Proton); developed and tested on Linux |

**Problems in the original that are fixed here:**

- **Loading a save crashed.** Two props were registered twice in the weapons collision archive, and AVCNG's sound
  configuration changes the save format. Both are fixed; AVCNG's sounds are still here, merged into the original Vice
  City sound setup.
- **"Riot":** you could lose control after the intro cutscene, because the script waited for a sound bank that doesn't
  exist.
- **Damaged textures and models:** APC, Infernus, tear gas, AUG and P90. The broken AUG and tear-gas models are replaced
  with working GTA IV ones.
- **Missing weapon animations** (AK-47, Tec-9, Uzi, minigun, flamethrower, AUG, M16, AA-12, sawn-off, grenade launcher)
  that AVCNG expected from an optional pack.
- **Missing pieces its scripts use:** bribe-star pickups, the adrenaline pickup animation, two Ocean Beach neon signs and
  the alternate-outfit cutscenes.
- **Data typos:** vehicle colour names (Armadillo, Grancher), a traffic-group name (Schafter) and the sniper animation set.
- **Silent missions:** 62 lines of 2002 mission dialogue that the conversion never played are back (see *Sound and music*).

Everything else that AVCNG CE adds is listed below.

## ✨ Features

### 🌴 Vice City

- Vice City NextGen (Revolution Team) through Kn4ckers' Complete Edition release, with AVCNG 7.0.1's reworked scripts and
  missions, restored weapons and aircraft, extra vehicles, extra map areas (Everglades, Gator Keys and more), fonts and
  loading screens.
- Classic Vice City activities as native scripts:
  - **All 35 rampages 💀** at their 2002 places, each with a briefing, a countdown and a kill counter. You're handed the
    rampage weapon and it's named on screen. On foot, only kills made with it count; vehicle rampages count every car
    you destroy.
  - **Hidden packages** at their 2002 places, including inside the police station and the mall, with the 2002 rewards
    (chainsaw, Python, flamethrower, minigun and more).
  - **Business revenue:** a gold **$** at every business you own fills up each day; walk in to collect it.
  - **Shop hold-ups** at 12 stores, **fast-food counters** that heal you, **Leaf Links security**, the Downtown shooting
    range, **insane stunt bonuses** and wheelie/stoppie bonuses.
- **2002-style pickups:** a clean adrenaline pill, the rampage skull, **save cassettes** at every save point, and
  **green / red house icons** at properties that are for sale or still locked. Health is GTA IV's own green first-aid kit.
- **2002 population and traffic:** every area has the people, gangs and traffic it had in 2002 Vice City, by day and by
  night: Cubans in Little Havana, Haitians in Little Haiti, bikers downtown, soldiers at the army base, workers at the
  airport and docks, and golfers in Caddies at Leaf Links. Street traffic is made of 2002 cars, matched to each area's
  2002 mix of cheap and expensive cars, and the boats out on the water are the 2002 leisure and work boats.
- **Every vehicle has a place to find it:** parked cars stand at their 2002 spots with their 2002 models, the 2002 car
  spawns are back, and vehicles with no 2002 home are parked somewhere that makes sense (the Biplane at the airport, the
  Armadillo on a downtown roof pad, Hotring cars beside the stadium, the RC Baron by the RC race van). Mission and reward
  vehicles still come with their missions.
- **Completion counted out of 154, like 2002:** rampages, the shooting range and shop hold-ups count towards it.
- **A Vice City stats screen:** rampages passed, stores knocked off, properties and assets owned, revenue collected,
  people saved, fires put out, pizzas delivered, hidden packages found and your best paramedic and firefighter levels.
- **Club crowds react:** fire a gun or hit someone inside the Malibu or Pole Position and the crowd stops dancing; most
  run from you and some cower.
- **Walk into Ken Rosenberg's office** in free roam as well as in missions: the Washington Beach building has its
  corridors, stairwell, collision and HD office.
- **AVCNG's map edits:** the overhauled Vercetti mansion inside and out on Starfish Island, Ocean View Hotel room 317,
  the Malibu Club interior, the V-Rock building, Phil's place and the junkyard, the airport access road, the Little
  Havana coast road, the Leaf Links walls and hedges, and 1980s street litter, plants and phones.
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
- Supercars are as fast as the real ones; 1950s cruisers, vans and trucks feel heavy and slow, with period tyres and
  brakes. The cars that come from GTA IV keep their own suspension geometry, so they stay planted over bumps and jumps.
- **Boats** reach their real counterparts' top speeds, and the **RC Bandit** drives like a large RC buggy.
- **People weigh what real people do:** about 56–66 kg for women and 76–81 kg for men, more for big builds and for
  uniformed police, firefighters and guards. **Tommy weighs 92 kg (203 lb).**
- **Street furniture has real weights:** lamp posts, signs, bins, benches, hydrants, mailboxes, newspaper boxes, the beach
  ball and shop windows (by their glass). Cars no longer stop dead against a rubbish bag.
- **Odd-job time limits fit the real vehicles:** Paramedic, Firefighter, Vigilante, Pizza Boy, Taxi and the Bloodring get
  the extra time their slower vehicles need.

### 🎮 DualSense

- **Full PlayStation 5 DualSense support, built in.** Connect it over USB or Bluetooth and play: it works as a normal
  game controller with no DS4Windows, ViGEm or Steam Input needed.
- **Adaptive triggers** for driving and shooting: throttle and brake resistance, ABS stutter, redline buzz, road-surface
  texture and weapon trigger feel.
- **HD haptics matched to Vice City:** every vehicle follows the engine sound it really uses in Vice City, and every gun
  follows its Vice City firing and reload sounds. Impacts, explosions, rough roads, gear changes and nearby traffic are
  felt too. All of these are felt through the controller's HD haptics, not heard from its speaker.
- **The controller speaker plays only what matters:**
  - a **police siren** while you're wanted, louder the closer the police are and silent when they're far away;
  - a **heartbeat** when you're nearly dead (20% health or less), faster as your health drops. It plays on your
    PC/TV and on the controller at the same moment, and stops when you heal.
  Your game sound stays on your PC or TV.
- **The light bar shows your health:** green at full health, yellow at half, red near death. While you're wanted it
  flashes red and blue like police lights.
- Works **over Bluetooth and USB**, with nothing extra to install on your computer.
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
  on. Custom outfits use his default cutscene look.
- **Full health** when a cutscene starts and ends.
- **Low-health recovery:** when your health bar is below 25% and nothing has hurt you for 3 seconds, it refills 1% per
  second up to 25%. Any hit pauses it, so it never makes you invincible.

### 🔊 Sound and music

- **AVCNG sound:** AVCNG's weapon, horn, tyre, footstep, ambience and engine sounds, death and start tunes, and the
  police scanner's Vice City car names.
- **Proper sounds for 23 vehicles and 13 weapons** that would otherwise use generic ones, including the Fairmont VCPD,
  the VCN Maverick, the Rio, the Coast Guard boat, the Speeder, the Sea Sparrow, the Infernus, the Pizza Boy, the Tec-9,
  the minigun, the sniper rifle, the M16 and the AUG.
- **2002 mission dialogue:** 62 lines that 2002 Vice City's missions played but the conversion never did, now spoken at
  the same moments as in 2002, with subtitles where 2002 showed them, in all seven languages. Across 13 missions: the
  French courier taunting you in **Mall Shootout**, Lance calling for help in **Guardian Angels**, the soldiers and the
  tank crew in **Sir, Yes Sir!**, the Colonel and his helicopter runs in **All Hands On Deck!**, Candy and Mercedes in
  **Recruitment Drive**, the builders and guards who turn on you in **Demolition Man**, plus **Supply & Demand**,
  **Gun Runner**, **Cap the Collector**, **Keep Your Friends Close**, **Four Iron**, **Back Alley Brawl** and **Waste
  the Wife**. A line never talks over a conversation that's already playing.
- **Original 2002 voices and sounds restored:** Tommy orders at the food stands and the staff answer (Burger Shot, Cheezy
  Crust, Shaft and Bandit each have their own lines), the Leaf Links guards speak when you walk in armed, the Hotring
  announcer and crowd call the race, the Pole Position dancers talk during a private dance, and the Cherry Popper
  purchase scene ends with its original sound.
- **Wanted, death and flying music:** chase music from 3 stars, music when you die or are nearly dead, and music while
  you fly. The music pauses with the menu.

### 🗣️ Languages

- Play in **English, French, German, Italian, Spanish, Russian or Japanese**: pick the language in
  **Settings → Display → Language**.
- Everything on screen is translated: missions, mission titles, passed/failed banners, intro images, the Vice City
  activities, the stats screen, race messages, the restored mission subtitles and the graphics options. Vehicle, radio
  station and brand names stay in English, as in the original game.
- **日本語:** the full Japanese translation with its own Japanese font. The pause menu tabs use Latin letters because
  the Vice City menu font has no Japanese characters.

### 🎨 Look

- Proper HD Era Vehicles with every vehicle texture at 1024 pixels and crisp badges and logos.
- Photoreal Tommy, cutscene and story characters, pedestrians, props, palms, grass, sand, water and city surfaces.
- **Street lamps where the original 2002 Vice City had them**, using the original lamp designs. Every lamp is breakable,
  lights up at night and stands firmly on the sidewalk.
- Solid palm and tree trunks: you crash into them instead of driving through. Bushes, hedges and ferns stay passable.
- Distant palms and buildings match their close-up models, so they don't change shape or pop in.
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

- **Mjolnir IV:** unpins the game's main thread from CPU core 0 so the work spreads across all your cores, lets a
  busy-wait loop yield, and turns off the engine's automatic quality limiter. Helpful on modern 6–16 core CPUs and APUs.
- **Cutscene memory flush:** every cutscene starts with a clean slate. Before it loads, every ambient car, ped and object
  is cleared, and traffic stays paused until it ends. Mission characters and the car you're sitting in stay. A car you
  left parked doesn't, so steal another one.
- Large cutscene models are split into smaller memory pages, so every cutscene loads instead of hanging on the loading
  screen.
- **DLSS / FSR** through DLSS-IV for a sharper picture at a higher frame rate.
- Every archive, script, map placement, model, collision, texture reference and translation in this release is
  checked for consistency before it ships.

## 📋 Requirements

- Your own legal copy of **GTA IV: The Complete Edition 1.2.0.59** (the current Steam / Rockstar version). No downgrade.
- About 10 GB free for the extracted files, plus space for the download.
- Windows: the **Microsoft Visual C++ 2015–2022 Redistributable (x86)** (most PCs already have it).
- Linux / Steam Deck: Proton.
- Optional: a **DualSense** (PS5) controller. Any other controller, keyboard and mouse work as usual.

## 🧼 Clean install: please read this first

> [!IMPORTANT]
> **Install this on a clean game. It is the most important step.** Most crashes, endless "Loading Game" screens,
> missing sounds and saves that won't load come from leftover files of other mods.

**What a clean install is:** an untouched GTA IV: The Complete Edition 1.2.0.59 with no mod files in the game folder.
That means none of these:

- `update`, `plugins`, `scripts`, `IVSDKDotNet` or `DLSSIV` folders
- loose `.asi`, `.dll` or `.ini` files you or another mod added, such as `dinput8.dll`, `d3d9.dll`, `vulkan.dll`,
  `xlive.dll`, ScriptHook, ZolikaPatch or other ASI loaders
- files from another Vice City port, **including the original AVCNG** and its downgraded `GTAIV.exe`

**Why it matters:** FusionFix loads every file in the `update` folder over the game's own data. A single leftover file
from another mod replaces the matching file in this release. The game then mixes files that don't belong together and
can crash or hang. Leftover plugins and script hooks can also clash with this release's native plugins.

**How to get one:**

1. Uninstall the game (Steam: right-click the game → *Manage* → *Uninstall*).
2. Open the game's install folder (Steam: *Manage* → *Browse local files* before uninstalling, usually
   `steamapps/common/Grand Theft Auto IV`) and delete whatever is left in it. *Verify integrity of game files* alone
   is **not** enough: it repairs Rockstar's files but leaves mod files in place.
3. Reinstall the game, start it once without mods to reach the main menu, then quit.

### 🧽 A clean copy of the mod, too

- **Download every part fresh** and check it against `SHA256SUMS.txt`. A broken or incomplete part gives you broken files.
- **Extract into a new, empty folder.**
- **Copy everything** inside the `AVCNGCE701` folder into the game folder. Don't skip, rename or swap files, and
  don't add files from other mods or packs on top.
- To reinstall, delete everything the mod installed first (see *Uninstalling*), or do the clean game install again.

Your saves are not in the game folder (they're in `Documents\Rockstar Games\GTA IV\Profiles\`), so a clean install
doesn't touch them. Start a **New Game**: saves made with other mods, including the original AVCNG, may not load.

## 📦 Installation

1. Close the game. Make sure you have a **clean install** (see above) that you have launched once.
2. Download **all** parts (`AVCNGCE701.zip.001`, `.002`, …) into one folder and open `AVCNGCE701.zip.001`
   with 7-Zip (Windows) or run `7z x AVCNGCE701.zip.001` (Linux). Windows Explorer can't open split archives.
3. Copy **everything inside the extracted `AVCNGCE701` folder** next to `GTAIV.exe`.
4. Start the game and choose **New Game**. Your Documents folder, saves and `User Music` stay as they are.

Linux / Steam Deck: if the loader doesn't start, use the Steam launch option
`WINEDLLOVERRIDES="dinput8=n,b" %command%` (in Lutris: DLL override `dinput8` = `n,b`).

**Graphics:** set **Graphics API: Vulkan** and **Anti-aliasing: Off** in Display settings. DLSS-IV uses DLSS on NVIDIA
RTX cards and FSR on others; NVIDIA under Proton needs NVAPI (`PROTON_ENABLE_NVAPI=1`).

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
- The controller speaker and HD haptics work over Bluetooth out of the box. Over a USB cable they use the controller's
  own sound device; on Linux, pick its **Analog Surround 4.0** profile in your sound settings.

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

## 🗑️ Uninstalling

Remove the `update`, `plugins` and `DLSSIV` folders and the loose files `dinput8.dll`, `d3d9.dll`, `d3d9.cfg`,
`DLSS-IV.asi`, `DLSS-IV.cfg`, `DLSS-IV README.txt`, `vulkan.dll`, `update.txt` and this README from the game folder.

## 📝 Notes

- Developed and tested on Linux with Proton.
- AVCNG's sound configuration is merged in, except its radio station list, which would break save loading. The radio is
  the Vice City NextGen set.
- The parachute has no sounds.
- Start a **New Game** for the rampage, revenue and stats counters to start from zero, and for the 2002 parked cars
  (a save keeps the parked cars it was made with).
- Translations cover the on-screen text; voices and radio stay in English. The Russian and Japanese subtitles of the
  restored 2002 mission lines are new translations; corrections are welcome.
- Real-world handling means slow cars really are slow: a 1950s cruiser, a golf cart or a forklift won't keep up with traffic.
- The Classic HUD's positions are built into its script; they follow your screen's aspect ratio.

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
