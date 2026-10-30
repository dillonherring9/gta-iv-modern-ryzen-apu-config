# GTA Vice City Actual Next Gen 7.0.1 — Final Port for GTA IV: The Complete Edition

**A port of Vice City Actual Next Gen Edition (VCANGE) 7.0.1 to GTA IV: The Complete Edition (1.2.0.59).**
VCANGE 7.0.1 is built for the old 1.0.8.0 game; this port runs it natively on the Complete Edition.
For Windows, Linux and Steam Deck (Proton). FusionFix 5.1.2 and DLSS-IV are included and already set up.
No .NET script mods and no ScriptHook: everything runs through FusionFix's `update` overlay, native plugins and native
game scripts. `GTAIV.exe` and the original Rockstar data archives are not replaced.

> [!IMPORTANT]
> I did not create the original mods. I ported, fixed and packaged them to run on the Complete Edition. All credit goes to
> the authors listed under [Credits](#credits).

> [!NOTE]
> **Adult content:** the Pole Position strip club's dancers are nude, and so is the stripper in the club's purchase
> cutscene. Nudity appears only inside the club.

## What's in it

### Vice City

- Vice City NextGen (Revolution Team) through Kn4ckers' Complete Edition release, with VCANGE 7.0.1's reworked scripts and
  missions, restored weapons and aircraft, extra vehicles, extra map areas (Everglades, Gator Keys and more), HUD, fonts
  and loading screens.
- Classic Vice City activities as native scripts: **all 35 rampages** with a skull pickup, **insane stunt bonuses**,
  **shop hold-ups** at 12 stores, **fast-food counters** that heal you, **Leaf Links security**, the Downtown shooting
  range and wheelie/stoppie bonuses.
- **Parachute:** you carry one in any helicopter or plane. Jump, press **Sprint** to open, steer with the movement keys
  or stick, press **Enter Vehicle** to cut away. It closes by itself when you land. With the
  [Parachute Backpack Patch](#parachute-backpack-patch), Tommy wears the real TBoGT parachute backpack.
- Story-based wanted limits and a six-star army response.
- Pole Position strip club with its own dancers (club only).
- Independence FM as a tenth radio station.

### Look

- Proper HD Era Vehicles with every vehicle texture at 1024 pixels and crisp badges and logos.
- Photoreal Tommy, cutscene and story characters, pedestrians, props, palms, grass, sand, water and city surfaces.
- HD map textures, IaCF_VC and CRTP interiors, and outfit-aware cutscenes.
- 1980s Miami pedestrian clothing and 1980s Miami crowds for every district by time of day.
- Turquoise Miami ocean and pools, Mint water, a Miami sky and a 1980s colour grade.
- Project Thunder lightning and storms, Xbox rain droplets, and DLSS / FSR upscaling through DLSS-IV.

### Gameplay

- Realistic Handling & Physics, with realistic weight and balance for cars, bikes, helicopters, planes, boats and RC toys.
- Restored weapons and animations; 20 guns with real-world damage, fire rate, magazines and range; rifles pierce armour.
- Realistic Euphoria ragdolls for pedestrians, GTA IV's default ragdoll for Tommy, and Army/MMA melee moves.
- Native **F7 Vice City Menu** with Gameplay Tweaks (save anywhere outside missions, skip radio track, holster and more).
- Full health when a cutscene starts and ends.

## Requirements

- Your own legal copy of **GTA IV: The Complete Edition 1.2.0.59**.
- About 9 GB free for the extracted files, plus space for the download.
- Linux / Steam Deck: Proton.

## Installation

1. Close the game. Start from an unmodded Complete Edition and launch it once.
2. Move any existing `update`, `plugins` or `DLSSIV` folders out of the game folder.
3. Download **all** archive parts into one folder and open the `.001` part with 7-Zip.
4. Copy **everything inside the extracted `Release` folder** next to `GTAIV.exe`.
5. Start the game and choose **New Game**. Your Documents folder, saves and `User Music` stay as they are.

Linux / Steam Deck: if the loader doesn't start, use the Steam launch option
`WINEDLLOVERRIDES="dinput8=n,b" %command%` (in Lutris: DLL override `dinput8` = `n,b`).

**Graphics:** check **Graphics API: Vulkan** and **Anti-aliasing: Off** in Display settings. DLSS-IV uses DLSS on NVIDIA
RTX cards and FSR on others; NVIDIA under Proton needs NVAPI (`PROTON_ENABLE_NVAPI=1`).

## Parachute Backpack Patch

A small optional add-on (`Vice City Actual Next Gen - Parachute Backpack Patch.7z`, about 3 MB). With the parachute
equipped, Tommy wears the red ProLaps backpack from *The Ballad of Gay Tony*, fitted to his back, instead of the loose pack
model that sat sideways. Everything else about the parachute works as before, and existing saves keep working.

To install, close the game and copy the `update` folder from inside the patch folder next to `GTAIV.exe`, choosing
**Replace / Overwrite**. The patch's `Restore Final Release Backpack` folder undoes it the same way. It changes only
`update/common/data/cdimages/script.img`.

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
`DLSS-IV.asi`, `DLSS-IV.cfg`, `DLSS-IV README.txt`, `vulkan.dll`, `update.txt` and this README from the game folder.

## Notes

- Tested in game on Linux/Proton, including New Game and save → quit → relaunch → load. Windows and Steam Deck weren't
  tested separately.
- The original Vice City NextGen audio configuration is used, because VCANGE's breaks save loading.
- The parachute has no sounds. Rampages count target kills without checking the weapon or drive-by condition.

## Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce)
- **GTATrilogyGames** and contributors: [Vice City Actual Next-Gen Edition (VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **ItsClonkAndre**: Project Thunder (GPL-3.0), as Project Thunder CE
- **DLSS-IV** author
- **1978 Ford Fairmont VCPD:** **Driver [GCU Modding Team]**, **DMN [GTA-Unlimited Team]**, **BubLik [GCU Modding Team]** and **deepcover0000**
- Author of the 2009 GTA IV **parachute mod** (`para.asi`): canopy and pack models and the parachute behaviour
- **Rockstar Games** (*The Ballad of Gay Tony*): the parachute backpack used by the Parachute Backpack Patch
- **metalwars** and **Kevin1230san**: the nude female models used for the Pole Position dancers
- **CGP**: Enhanced Fighting And Euphoria (rebuilt natively)
- **LibertyTweaks** authors (rebuilt natively); **Zolika1351**: ZMenu (inspiration for the F7 menu)
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (Shturmovik2), **VCNG HD Pedestrians**,
  the **VCNE HD map pack**, **RestoredWeapons**, **IaCF_VC**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters** and **TreesMod**
- **Rockstar Games**: Grand Theft Auto: Vice City and Grand Theft Auto IV

## Disclaimer

Unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or the authors of the included mods.
It contains only modified and added files; you need your own legal copy of GTA IV: The Complete Edition. Please support
the original mod authors and respect their redistribution terms.
