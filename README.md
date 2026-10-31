# GTA Vice City Actual Next Gen 7.0.1 for GTA IV: The Complete Edition

![Game](https://img.shields.io/badge/GTA%20IV-Complete%20Edition%201.2.0.59-orange)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20Steam%20Deck-blue)
![Languages](https://img.shields.io/badge/languages-EN%20%7C%20FR%20%7C%20DE%20%7C%20IT%20%7C%20ES-green)

A port of **Vice City Actual Next Gen Edition (VCANGE) 7.0.1** to **GTA IV: The Complete Edition (1.2.0.59)**.

VCANGE 7.0.1 is built for the old 1.0.8.0 version of GTA IV. This port runs it natively on the Complete Edition, with
FusionFix 5.1.2 and DLSS-IV included and already set up. It uses no .NET script mods and no ScriptHook: everything runs
through FusionFix's `update` overlay, native plugins and native game scripts. `GTAIV.exe` and the original Rockstar data
archives are not replaced.

> [!IMPORTANT]
> I did not create the original mods. I ported, fixed and packaged them to run on the Complete Edition. All credit goes to
> the authors listed under [Credits](#credits).

> [!NOTE]
> **Adult content:** the Pole Position strip club's dancers are nude, and so is the stripper in the club's purchase
> cutscene. Nudity appears only inside the club.

## Download

Get the latest version from the **[Releases](../../releases/latest)** page. The game files are split into several
`TrickOrTreat.zip.00x` parts; download all of them.

## Features

### Vice City

- Vice City NextGen (Revolution Team) through Kn4ckers' Complete Edition release, with VCANGE 7.0.1's reworked scripts and
  missions, restored weapons and aircraft, extra vehicles, extra map areas (Everglades, Gator Keys and more), HUD, fonts
  and loading screens.
- Classic Vice City activities as native scripts: all 35 rampages with a skull pickup, insane stunt bonuses, shop
  hold-ups at 12 stores, fast-food counters that heal you, Leaf Links security, the Downtown shooting range and
  wheelie/stoppie bonuses.
- Parachute in any helicopter or plane, with the red ProLaps backpack from *The Ballad of Gay Tony*.
- Story-based wanted limits and a six-star army response.
- Pole Position strip club with its own dancers (club only).
- Independence FM as a tenth radio station.

### Languages

- English, French, German, Italian and Spanish (Settings → Display → Language).
- Translated text, mission titles, passed/failed banners and intro images; the extra Vice City activities are
  translated too. Voices and radio stay in English.

### Visuals

- Proper HD Era Vehicles with 1024-pixel vehicle textures and crisp badges and logos.
- Photoreal Tommy, cutscene and story characters, pedestrians, props, palms, grass, sand, water and city surfaces.
- Street lamps at the original 2002 Vice City positions with the original designs: breakable, lit at night and set
  firmly on the sidewalks.
- Solid palm and tree trunks; bushes, hedges and ferns stay passable.
- Distant palms and buildings match their close-up models, so they don't change shape or pop in.
- HD map textures, IaCF_VC and CRTP interiors, and cutscenes that follow Tommy's alternate outfit.
- 1980s Miami pedestrian clothing and crowds for every district by time of day.
- Turquoise Miami ocean and pools, Mint water, a Miami sky and a 1980s colour grade.
- Project Thunder lightning and storms, Xbox rain droplets, and DLSS / FSR upscaling through DLSS-IV.

### Gameplay

- Realistic Handling & Physics, with realistic weight and balance for cars, bikes, helicopters, planes, boats and RC toys.
- 1978 Ford Fairmont as the VCPD police car.
- Restored weapons and animations; 20 guns with real-world damage, fire rate, magazines and range; rifles pierce armour.
- Euphoria ragdolls for pedestrians, GTA IV's default ragdoll for Tommy, and Army/MMA melee moves.
- Native F7 Vice City Menu with Gameplay Tweaks (save anywhere outside missions, skip radio track, holster and more).
- Full health when a cutscene starts and ends.

## Requirements

- Your own legal copy of **GTA IV: The Complete Edition 1.2.0.59** (Steam or Rockstar Games Launcher).
- About 9 GB of free space for the extracted files, plus space for the download.
- [7-Zip](https://www.7-zip.org/) (Windows) or `7z` / `p7zip` (Linux) to extract the split archive.
- Linux / Steam Deck: Proton.

## Installation

1. Close the game. Start from an unmodded Complete Edition and launch it once.
2. Move any existing `update`, `plugins` or `DLSSIV` folders out of the game folder.
3. Download **all** `TrickOrTreat.zip.00x` parts into one folder and check them against `SHA256SUMS.txt`.
4. Open `TrickOrTreat.zip.001` with 7-Zip (Windows) or run `7z x TrickOrTreat.zip.001` (Linux). Windows Explorer can't
   open split archives.
5. Copy **everything inside the extracted `TrickOrTreat` folder** next to `GTAIV.exe`.
6. Start the game and choose **New Game**. Your saves and `User Music` stay as they are.

### Linux / Steam Deck

If the loader doesn't start, set the Steam launch option:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

In Lutris, add the DLL override `dinput8` = `n,b`. NVIDIA under Proton needs NVAPI (`PROTON_ENABLE_NVAPI=1`) for DLSS.

### Recommended settings

- Display → **Graphics API: Vulkan**
- Display → **Anti-aliasing: Off** (DLSS-IV handles anti-aliasing: DLSS on NVIDIA RTX cards, FSR on others)

## Controls

| Input | Action |
|---|---|
| **F7** / **LB + D-pad right** | Vice City Menu (D-pad up also opens it) |
| Arrows / numpad / D-pad · Enter / A · Backspace / B | Move · select · back in the menu |
| **Ctrl+F7** | Project Thunder menu |
| **Alt+V** | First-person camera |
| **N** in a vehicle · **Tab** on foot | Next radio track · holster weapon |
| **Sprint** while falling from an aircraft · **Enter Vehicle** | Open the parachute · cut away |
| Aim with fists, bat, knife, Pool Cue or Katana, then **Z / V / C / X / G** | Army/MMA moves (**F5** switches style) |

## Saves

Save at a safehouse or with F7 → Gameplay Tweaks → Save Game Now (not during missions). Saves are in
`Documents\Rockstar Games\GTA IV\Profiles\` (on Proton, inside the game's Wine prefix).

## Uninstalling

Remove the `update`, `plugins` and `DLSSIV` folders and the loose files `dinput8.dll`, `d3d9.dll`, `d3d9.cfg`,
`DLSS-IV.asi`, `DLSS-IV.cfg`, `DLSS-IV README.txt`, `vulkan.dll`, `update.txt` and `README.md` from the game folder.

## Troubleshooting

| Problem | Fix |
|---|---|
| Plain GTA IV starts instead of Vice City | The files aren't next to `GTAIV.exe`, or the `dinput8` override is missing on Linux. |
| "Cannot open file as archive" | Download every `.00x` part into the same folder and open `.001` with 7-Zip. |
| Crash or odd behaviour after installing over other mods | Start again from an unmodded Complete Edition (Installation step 1–2). |
| No DLSS on NVIDIA under Proton | Add `PROTON_ENABLE_NVAPI=1` to the launch options. |
| Text still in English after switching language | Restart the game after changing Settings → Display → Language. |

## Known issues

- Tested on Linux/Proton (New Game and save → quit → relaunch → load). Windows and Steam Deck weren't tested separately.
- The original Vice City NextGen audio configuration is used, because VCANGE's breaks save loading.
- The parachute has no sounds.
- Rampages count target kills without checking the weapon or drive-by condition.

### Why cutscenes don't show the clothes you're wearing

In cutscenes Tommy wears his default outfit, or his one alternate outfit, no matter what you have on in the game. This
won't be changed, for a technical reason:

- Cutscenes don't use the in-game Tommy. They use a separate high-detail cutscene model of him, which only exists in
  those two outfits.
- Every cutscene animation was made for that cutscene model's skeleton, which is the original Vice City-style rig with
  its own facial bones. The in-game Tommy uses GTA IV's skeleton. Apart from the root bone, the two skeletons share no
  bones.
- If cutscenes used the in-game Tommy (the way stock GTA IV does for Niko), the animations would have nothing to move,
  and Tommy would stand frozen while everyone else acts out the scene.

Making it work would mean converting about 100 cutscene animations to GTA IV's skeleton, probably losing Tommy's facial
animation along the way, or rebuilding every outfit for the cutscene skeleton by hand. Either way it's a huge job with
an uncertain result, so the detailed cutscene Tommy stays as he is.

## Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce)
- **GTATrilogyGames** and contributors: [Vice City Actual Next-Gen Edition (VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **ItsClonkAndre**: Project Thunder (GPL-3.0), as Project Thunder CE
- **DLSS-IV** author
- **AIO All Classic Language Patch v2.0** author, shared by **Contremaitre**: French, German, Italian and Spanish text,
  fonts and translated images
- **1978 Ford Fairmont VCPD:** **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000**
- Author of the 2009 GTA IV **parachute mod** (`para.asi`): canopy and pack models and the parachute behaviour
- **Rockstar Games** (*The Ballad of Gay Tony*): the parachute backpack
- **metalwars** and **Kevin1230san**: the nude female models used for the Pole Position dancers
- **CGP**: Enhanced Fighting And Euphoria (rebuilt natively)
- **LibertyTweaks** authors (rebuilt natively); **Zolika1351**: ZMenu (inspiration for the F7 menu)
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (Shturmovik2), **VCNG HD Pedestrians**,
  the **VCNE HD map pack**, **RestoredWeapons**, **IaCF_VC**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters** and **TreesMod**
- **Rockstar Games**: Grand Theft Auto: Vice City and Grand Theft Auto IV

## Disclaimer

Unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or the authors of the included mods.
It contains only modified and added files; you need your own legal copy of GTA IV: The Complete Edition. Included
third-party components keep their own licenses (for example, Project Thunder is GPL-3.0). Please support the original
mod authors and respect their redistribution terms.
