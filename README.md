# Vice City NextGen — Linux/Proton-friendly build for GTA IV: The Complete Edition

A community build of **GTA Vice City NextGen** for the current Steam/Rockstar release of
**GTA IV: The Complete Edition (1.2.0.59)**. Everything installs into GTA IV's `update` folder
through FusionFix's overloader, so no original game files are replaced, and the Rockstar Games
Launcher has nothing to "repair".

It brings together the content of **Vice City Actual Next-Gen Edition (VCANGE) 7.0.1** and several
HD packs on top of Kn4ckers' legal Complete Edition release of Vice City NextGen — **without any
.NET script hooks** (ScriptHookDotNet / IVSDK .NET), so it runs on Linux through Proton as well as
on Windows.

> I did not make these mods. This is a port and integration of other people's work, assembled and
> debugged with AI assistance. All credit goes to the original authors listed below.

## Features

- Vice City NextGen (Revolution Team), via Kn4ckers' Complete Edition release
- VCANGE 7.0.1 content: reworked scripts and missions, restored weapons and aircraft, extra vehicles,
  map areas (Everglades and more), HUD, fonts, loading screens and fixes
- Proper HD Era Vehicles – Lore Friendly, with its VCANGE handling tweaks
- Realistic Handling & Physics
- Restored weapons with their animations
- HD map textures (visually reviewed — replacements that changed the art or the 1986 look were left out)
- Interior and cutscene-prop improvements (IaCF_VC, CRTP) and outfit-aware cutscenes (Cutscene Recognition)
- VCNG HD Pedestrians with photoreal textures, and photoreal Tommy/cutscene characters
- Clear "Mint" turquoise water and a 1980s Miami weather/lighting palette
- GTA V-style health regeneration (native script — refills to half the bar while standing still)
- Repeatable Downtown shooting range and wheelie/stoppie cash bonuses (native scripts, experimental)
- Volumetric light beams (VolumetricLights plugin)
- Optional DLSS / FSR upscaling through DLSS-IV — works on NVIDIA, AMD and Intel cards

## VCANGE 8.0

VCANGE 8.0 is not out yet — I'm waiting for it too. GTATrilogyGames has been showing it as a work in
progress: a total overhaul with more content than 7.0.1, which this build is based on. Please don't
ask me or the VCANGE team for 8.0 builds, leaks or release dates; follow their channels instead.

VCANGE is made by **GTATrilogyGames** with the **VCNE modding community** and **DayL's modding
community**. They spent months restoring missions, weapons, vehicles, map areas and countless
details, and this build would not exist without them. Please support them directly:

- GitHub (official releases): <https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack>
- YouTube (showcases and 8.0 progress): <https://www.youtube.com/@GTATrilogyGames>
- GTAForums: <https://gtaforums.com/topic/1002512-grand-theft-auto-vice-city-actual-nextgen-edition/>

Linux and Steam Deck players: if you enjoy this build, leave the VCANGE team a friendly comment saying
you play through Proton. I hope 8.0, or a later release, runs on the Complete Edition without .NET
script hooks so it works on Linux out of the box — a wish, not a request; it's their project and their
call.

## What is different from VCANGE

VCANGE replaces your game with the 2008 `GTAIV.exe` 1.0.8.0 (Games for Windows Live) and relies on
.NET script mods. This build keeps the Complete Edition and leaves those out.

| VCANGE component | In this build |
|---|---|
| `update` folder content | Included in full (plus the fixes below) |
| Downgrade to GTAIV.exe 1.0.8.0 + xlive | Not used — stays on Complete Edition 1.2.0.59 |
| IVSDK .NET / ScriptHookDotNet scripts (LibertyTweaks, WeapFuncs, MoveImprove, wanted/flying music, …) | Not included (do not run under Proton). Health regen was re-made as a native script |
| Plugins built for 1.0.7/1.0.8 only (IVTweaker, ZMenu, ScriptHook, XLivelessAddon, V-Handling) | Not included |
| ZolikaPatch | Not included (no confirmed Complete Edition support; FusionFix covers most of it). Pop-up headlights on three cars therefore stay fixed |
| VCANGE's own sound set | **Not included** — see Known limitations |

### Fixes applied while porting

- Save loading crash: two props (`amb_paperbag05`, `amb_comb`) were registered by two collision
  dictionaries in the RestoredWeapons archive; the duplicates were removed.
- Save loading crash: VCANGE's audio configuration breaks loading saves — the original Vice City
  audio set is used.
- "Riot" mission: the player could lose control after the intro cutscene (the script waited for a
  sound bank that does not exist); fixed.
- Damaged textures repaired: APC and Infernus (vehicles), tear gas, AUG and P90 (weapons).
- Missing weapon animation sets added (AK-47, Tec-9, Uzi, minigun, flamethrower, AUG, M16, AA-12,
  sawn-off, grenade launcher) — VCANGE expected them from an optional pack.
- Missing assets VCANGE's scripts use: bribe-star pickups, the adrenaline pickup animation, two
  Ocean Beach neon signs, alternate outfit cutscenes.
- Data fixes: vehicle colour names (Armadillo, Grancher), traffic-group typo (Schafter), sniper
  animation set.

## Requirements

- A legal copy of **GTA IV: The Complete Edition**, updated to **1.2.0.59** (Steam or Rockstar Games Launcher)
- **FusionFix 5.1.2 or newer** — <https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases>
- Linux: Proton (tested with Proton-CachyOS through Lutris/umu). Windows: nothing extra
- Optional: **DLSS-IV** for DLSS/FSR upscaling (download it from its own page; not bundled here)

## Installation

1. Install GTA IV: The Complete Edition and start it once.
2. Install FusionFix (copy its files into the GTA IV folder).
3. **Linux only:** make sure the game loads FusionFix's `dinput8.dll` instead of Wine's.
   In Steam launch options: `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
   In Lutris: *Runner options → DLL overrides → `dinput8` = `n,b`*.
4. Download every part of this release and extract it into the GTA IV folder, merging folders:
   - `update/` — everything for Vice City
   - `plugins/VolumetricLights.asi` and `VolumetricLights.ini`
5. Set these in `plugins/GTAIV.EFLC.FusionFix.ini`:
   ```ini
   UnlockFramerateDuringLoadscreens = 0
   VehicleBudget = 146552832
   ExtendedLimits = 1
   EpisodicVehicles = 1
   ```
6. Start the game and choose **New Game**.

### Optional: DLSS-IV (any graphics card)

1. Install DLSS-IV following its readme (it brings its own `vulkan.dll` and `DLSSIV` folder).
2. In the game's graphics menu, set FusionFix's **Graphics API to Vulkan** and **Anti-aliasing to Off**.
3. In `DLSS-IV.cfg` leave `upscaler=-1` (automatic): DLSS on NVIDIA RTX cards, FSR on everything else
   (FSR 4 on Radeon RX 7000/9000, FSR 3.1 on other cards).
4. Linux with an NVIDIA card: DLSS needs NVAPI enabled in Proton
   (`PROTON_ENABLE_NVAPI=1`, or the DLSS option in Lutris). AMD and Intel cards use FSR and need nothing extra.

## Uninstalling

Delete the Vice City files you added to `update/` and the two VolumetricLights files from `plugins/`.
The original game files were never changed.

## Saves

Saves are stored by the game as usual. Saves made with VCANGE's own sound set (or with VCANGE on
1.0.8.0) are not compatible with this build — start a New Game.

## Known limitations

- **VCANGE's Vice City sounds** (original sirens, tyre skids, weapon sounds) are not included. Every
  variant tried — trimming slots, removing duplicated sound banks, removing episode banks — still
  crashed when loading a save. The original Vice City NextGen audio is used instead.
- The shooting range and stunt bonuses are new, lightly tested scripts.
- A desk prop in the bank interior is missing (it is missing from the base mod as well).
- The airport seaplane dock was disabled by VCANGE and stays disabled.
- Some interior models from IaCF reference a few detail textures that do not exist anywhere; affected
  surfaces look slightly flatter.

## Credits

- **Revolution Team** — GTA Vice City NextGen Edition
- **Kn4ckers** — Vice City for GTAIV: The Complete Edition (legal release, used as the base):
  <https://github.com/knackers4/vce>
- **GTATrilogyGames** and the contributors credited in VCANGE — Vice City Actual Next-Gen Edition 7.0.1
- Authors of **Proper HD Era Vehicles – Lore Friendly** and the creators it credits
- **Shturmovik2** — Realistic Handling & Physics
- Authors of **VCNG HD Pedestrians**, the **VCNE HD map pack**, **RestoredWeapons**, **IaCF_VC**, **CRTP**,
  **Cutscene Recognition**, **OG-LikeBehaviour** (weapon animation sets only) and **Various Map Edits**
  (bribe-star and pickup animation files only)
- **Mint Ocean Waters** mod authors
- Author of **VolumetricLights**
- **ThirteenAG** and contributors — FusionFix
- Author of **DLSS-IV** (not bundled)

## Disclaimer

This is an unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or the
authors of the included mods. It contains only modified/added files — you need your own legal copy
of GTA IV: The Complete Edition. Please support the original mod authors and respect their
redistribution terms.
