# 🌴 GTA Vice City Actual Next Gen 7.0.1 for GTA IV: The Complete Edition

**A port of Vice City Actual Next Gen Edition (VCANGE) 7.0.1 to GTA IV: The Complete Edition (1.2.0.59).**
VCANGE 7.0.1 was built for the old 1.0.8.0 game. This port runs it natively on the Complete Edition, on Windows, Linux
and Steam Deck (Proton). FusionFix 5.1.2 and DLSS-IV are included and already set up.
There are no .NET script mods and no ScriptHook. Everything runs through FusionFix's `update` overlay, native plugins and
native game scripts. `GTAIV.exe` and the original Rockstar data archives are not touched.

> [!IMPORTANT]
> I did not create the original mods. I ported, fixed and packaged them to run on the Complete Edition. All credit goes to
> the authors listed under [Credits](#-credits).

> [!NOTE]
> **Adult content:** the Pole Position strip club's dancers are nude, and so is the stripper in the club's purchase
> cutscene. Nudity appears only inside the club.

## ✨ Features

### 🌴 Vice City

- Vice City NextGen (Revolution Team) through Kn4ckers' Complete Edition release, with VCANGE 7.0.1's reworked scripts and
  missions, restored weapons and aircraft, extra vehicles, extra map areas (Everglades, Gator Keys and more), fonts and
  loading screens.
- Classic Vice City activities as native scripts: **all 35 rampages** with a skull pickup 💀, **insane stunt bonuses**,
  **shop hold-ups** at 12 stores, **fast-food counters** that heal you, **Leaf Links security**, the Downtown shooting
  range and wheelie/stoppie bonuses.
- **Walk into Ken Rosenberg's office** in free roam as well as in missions: the Washington Beach building has its
  corridors, stairwell, collision and HD office.
- **VCANGE's map edits:** the overhauled Vercetti mansion inside and out on Starfish Island, Ocean View Hotel room 317,
  the Malibu Club interior, the V-Rock building, Phil's place and the junkyard, the airport access road, the Little
  Havana coast road, the Leaf Links walls and hedges, and 1980s street litter, plants and phones.
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
- **Cutscene memory flush:** every cutscene starts with a clean slate. Before it loads, every ambient car, ped and object
  is cleared, and traffic stays paused until it ends. Mission characters and the car you're sitting in stay. A car you
  left parked doesn't, so steal another one.
- Large cutscene models are split into smaller memory pages, so cutscenes load instead of hanging on the loading screen.

### 🔊 Sound and music

- **AVCNG sound:** Vice City Actual Next-Gen Edition's weapon, horn, tyre, footstep, ambience and engine sounds, death
  and start tunes, and the police scanner's Vice City car names.
- **Proper sounds for 23 vehicles and 13 weapons** that would otherwise use generic ones, including the Fairmont VCPD,
  the VCN Maverick, the Rio, the Coast Guard boat, the Speeder, the Sea Sparrow, the Infernus, the Pizza Boy, the Tec-9,
  the minigun, the sniper rifle, the M16 and the AUG.
- **Wanted, death and flying music:** chase music from 3 stars, music when you die or are nearly dead, and music while
  you fly. The music pauses with the menu.

### 🗣️ Languages

- Play in **English, French, German, Italian or Spanish**: pick the language in **Settings → Display → Language**.
- The text, mission titles, passed/failed banners and intro images are translated, and so are the extra Vice City
  activities. Vehicle, radio station and brand names stay in English.

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

- Realistic Handling & Physics, with realistic weight and balance for cars, bikes, helicopters, planes, boats and RC toys.
  The cars that come from GTA IV keep their own suspension geometry, so sports cars such as the Turismo, Banshee, Comet,
  Coquette and Super GT stay planted over bumps and jumps instead of bouncing.
- The 1978 Ford Fairmont as the VCPD police car.
- Restored weapons and animations; 20 guns with real-world damage, fire rate, magazines and range; rifles pierce armour.
- Realistic Euphoria ragdolls for pedestrians, GTA IV's default ragdoll for Tommy, and Army/MMA melee moves.
- Native **F7 Vice City Menu** with Gameplay Tweaks (save anywhere outside missions, skip radio track, holster and more).

### ⚙️ Performance

- **Mjolnir IV:** unpins the game's main thread from CPU core 0 so the work spreads across all your cores, lets a
  busy-wait loop yield, and turns off the engine's automatic quality limiter. Helpful on modern 6–16 core CPUs and APUs.

## 📋 Requirements

- Your own legal copy of **GTA IV: The Complete Edition 1.2.0.59**.
- About 9.5 GB free for the extracted files, plus space for the download.
- Windows: the **Microsoft Visual C++ 2015–2022 Redistributable (x86)** (most PCs already have it).
- Linux / Steam Deck: Proton.

## 🧼 Clean install: please read this first

> [!IMPORTANT]
> **Install this on a clean game. It is the most important step.** Most crashes, endless "Loading Game" screens,
> missing sounds and saves that won't load come from leftover files of other mods.

**What a clean install is:** an untouched GTA IV: The Complete Edition 1.2.0.59 with no mod files in the game folder.
That means none of these:

- `update`, `plugins`, `scripts` or `DLSSIV` folders
- loose `.asi`, `.dll` or `.ini` files you or another mod added, such as `dinput8.dll`, `d3d9.dll`, `vulkan.dll`,
  `xlive.dll`, ScriptHook, ZolikaPatch or other ASI loaders
- files from another Vice City port

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
- **Copy everything** inside the `AVCNGPortDone` folder into the game folder. Don't skip, rename or swap files, and don't
  add files from other mods or packs on top.
- To reinstall, delete everything the mod installed first (see *Uninstalling*), or do the clean game install again.

Your saves are not in the game folder (they're in `Documents\Rockstar Games\GTA IV\Profiles\`), so a clean install
doesn't touch them. Start a **New Game**: saves made with other mods may not load.

## 📦 Installation

1. Close the game. Make sure you have a **clean install** (see above) that you have launched once.
2. Download **all** parts (`AVCNGPortDone.zip.001`, `.002`, …) into one folder and open `AVCNGPortDone.zip.001` with
   7-Zip (Windows) or run `7z x AVCNGPortDone.zip.001` (Linux). Windows Explorer can't open split archives.
3. Copy **everything inside the extracted `AVCNGPortDone` folder** next to `GTAIV.exe`.
4. Start the game and choose **New Game**. Your Documents folder, saves and `User Music` stay as they are.

Linux / Steam Deck: if the loader doesn't start, use the Steam launch option
`WINEDLLOVERRIDES="dinput8=n,b" %command%` (in Lutris: DLL override `dinput8` = `n,b`).

**Graphics:** check **Graphics API: Vulkan** and **Anti-aliasing: Off** in Display settings. DLSS-IV uses DLSS on NVIDIA
RTX cards and FSR on others; NVIDIA under Proton needs NVAPI (`PROTON_ENABLE_NVAPI=1`).

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

## ⚙️ Settings

- `plugins/VCMusic.ini`: turn each kind of music on or off, set the volume and fades, and choose how many stars start the
  chase music. Add your own `.mp3`, `.wav` or `.ogg` files to `plugins/VCMusic/Wanted`, `Death` or `Flying` (up to 16
  each).
- `plugins/VCMemory.ini`: the cutscene flush (on/off, clear radius, model budgets, traffic pause) and `[Outfits]`
  (`Enabled=1` makes cutscenes follow your outfit).
- The plugins write logs next to themselves (`VCMusic.log`, `VCMemory.log`, `mjolnir_cpu_fix.log`).

## 💾 Saves

Save at a safehouse or with F7 → Gameplay Tweaks → Save Game Now (not during missions). Saves are in
`Documents\Rockstar Games\GTA IV\Profiles\` (on Proton, inside the game's Wine prefix).

## 🗑️ Uninstalling

Remove the `update`, `plugins` and `DLSSIV` folders and the loose files `dinput8.dll`, `d3d9.dll`, `d3d9.cfg`,
`DLSS-IV.asi`, `DLSS-IV.cfg`, `DLSS-IV README.txt`, `vulkan.dll`, `update.txt` and this README from the game folder.

## 📝 Notes

- Developed and played on Linux/Proton. Windows and Steam Deck weren't tested separately.
- VCANGE's sound configuration is merged in, except its radio station list, which would break save loading. The radio is
  the Vice City NextGen set.
- The parachute has no sounds. Rampages count target kills without checking the weapon or drive-by condition.
- Translations cover the text only; voices and radio stay in English.
- The Classic HUD's positions are built into its script; they follow your screen's aspect ratio.

## 🙏 Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce)
- **GTATrilogyGames** and contributors: [Vice City Actual Next-Gen Edition (VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack), including its sounds and its Various Map Edits
- Authors of the map edits collected in VCANGE: the **Starfish Mansion Overhaul Project** and **Vercetti Mansion
  Overhaul**, the **V-Rock building** interior, the **Ocean View Hotel** rework and **Interiors and Cutscene Fixes (IaCF_VC)**
- **DrAnusTart**: Access Rosenberg's Office and HD Overhaul
- **CyanFire**: Vice City NextGen Edition Classic HUD (rebuilt here as a native script)
- **A.R Scorpion** (cutscene outfit models) and **xdd5110** (the original cutscene outfit concept): VCNG Cutscene Patch
- **llamacomma**: [Mjolnir IV](https://github.com/llamacomma/Mjolnir_IV) (MIT licence)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **ItsClonkAndre**: Project Thunder (GPL-3.0), as Project Thunder CE; VWantedMusic, DeathMusicIV and VFlyingMusic; the
  IV-SDK .NET native list
- **Un4seen Developments**: BASS audio library
- **DLSS-IV** author
- **AIO All Classic Language Patch v2.0** author, shared by **Contremaitre**: French, German, Italian and Spanish text,
  fonts and translated images
- **1978 Ford Fairmont VCPD:** **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000**
- Author of the 2009 GTA IV **parachute mod** (`para.asi`): canopy model and the parachute behaviour
- Author of **"Parachute port from GTA 5 to GTA 4"**: the parachute backpack
- **Rockstar Games** (*The Ballad of Gay Tony*): the parachute animations
- **metalwars** and **Kevin1230san**: the nude female models used for the Pole Position dancers
- **CGP**: Enhanced Fighting And Euphoria (rebuilt natively)
- **LibertyTweaks** authors (rebuilt natively); **Zolika1351**: ZMenu (inspiration for the F7 menu)
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (Shturmovik2), **VCNG HD Pedestrians**,
  the **VCNE HD map pack**, **RestoredWeapons**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters** and **TreesMod**
- **Rockstar Games**: Grand Theft Auto: Vice City and Grand Theft Auto IV

## ⚖️ Disclaimer

Unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or the authors of the included mods.
It contains only modified and added files; you need your own legal copy of GTA IV: The Complete Edition. Please support
the original mod authors and respect their redistribution terms.
