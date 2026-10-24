# Vice City Actual Next Gen — Release 5

**GTA Vice City NextGen + VCANGE 7.0.1, ported to GTA IV: The Complete Edition (1.2.0.59).**
Windows, Linux and Steam Deck (Proton). No .NET script hooks. FusionFix 5.1.2 and DLSS-IV are included and already set up.
Copy one folder next to `GTAIV.exe` and start a New Game.

Everything installs into GTA IV's `update` and `plugins` folders through FusionFix's overloader. No original game files are
replaced, so the launcher has nothing to "repair".

> [!IMPORTANT]
> I did not create the original mod assets or code. I ported, fixed and packaged them to run natively on the
> Complete Edition with FusionFix. All credit for the mods goes to the authors listed under [Credits](#credits).

## What's new in Release 5

- **Photoreal environment:** 130 textures for palm trees and plants, grass, beach sand and water (pools, lake bed, seabed).
  Each one was checked at full size and repeated across a surface, so the seams don't show.
- **Photoreal city surfaces:** 216 map textures. These are the most-used surfaces (concrete, pavement, the main road,
  aluminium and steel, lamp and telegraph poles, mansion wood, crates, roof tiles, soil and planters) and walls, floors,
  trims and building fronts that were stored as blurry upscales. Text, logos, shadows, glass and anything that came out
  blocky were left as they were.
- **Real detail on flat surfaces:** 106 more textures keep their original image and colours exactly, with only fine
  surface detail added on top. Pastel plaster walls get real stucco grain, and cutscene and story clothing gets fabric weave.
- **Miami pools:** clear turquoise pool water with light caustics, at the Diaz mansion pool and fountain and the mall fountain.
- **Miami ocean:** the sea colour is turquoise all day and in every weather, and slightly clearer in fair weather.
- **Ocean Beach glitch fixed:** north of the Ocean Beach Ammu-Nation, one building drew huge flat lavender see-through
  polygons and slowed the game until it crashed. The cause was a broken see-through dirt layer wrapped around the building
  (from the base mod); it is no longer drawn.
- **AUG (Ruger slot) fixed:** VCANGE's AUG model is broken, and the hidden AUG pickup behind that Ocean Beach building
  caused the same lavender glitch. The weapon now uses GTA IV's M4 model and keeps its VC-style HUD icon.
- **Tear gas fixed:** VCANGE's tear gas model is broken too: it had no skeleton and claimed to be 8 m wide. It now uses
  GTA IV's grenade model with the VC-style tear gas icon. Gameplay is unchanged.
- **VolumetricLights removed:** its light beams weren't noticeable in play, so the plugin is no longer included.
  FusionFix's own light volumes stay on.

## What was new in Release 4

- **Photoreal characters, second pass:** Tommy, every cutscene character, story characters and ambient pedestrians,
  each texture checked by hand (teeth, eyes and lettering kept as the originals).
- **Photoreal cutscene props and sets:** 133 textures in 49 prop models (furniture, cars, weapons, food, buildings).
  Results that added banding, blotches or changed text were left out.
- **Cutscene fix:** stretched lines from hands and arms are gone. An earlier texture writer had damaged the geometry of 18
  cutscene models, and a bone-ID clash affected Tommy's alternate-outfit cutscenes.
- **F7 Vice City Menu:** a native trainer that replaces VCANGE's ZMenu, which only runs on 1.0.7/1.0.8. It has player, weapons,
  a 148-vehicle spawner, vehicle options, VC teleports, world, Tommy's clothes, bodyguards, objects, animations, a
  first-person camera and more.
- **Gameplay Tweaks** (in the F7 menu): the LibertyTweaks features rebuilt natively. Save anywhere (blocked during missions),
  skip radio track, holster weapon, lose stars when unseen, armoured cops from 3 stars, engines die on fire, gun dot and more.
- **Euphoria & fighting:** natively rebuilt from *Enhanced Fighting And Euphoria*. Seatbelt, get up from a fall, stumble and
  knock-down ragdolls, crash injuries, and Army/MMA melee moves.
- **Project Thunder CE:** lightning, thunder and storm blackouts.
- **Swaying palm trees** from VCANGE's TreesMod.
- **VCANGE bugs fixed**, from its issue tracker. Each one was confirmed in the game's files first:
  - The taxi was slow and handled differently from the police car with the same body (#26).
  - "Saves made" in the stats always showed 0 (#19).
  - Sunshine Autos didn't accept the Cheetah on the import list (#6).
  - Chopper Checkpoints could never start: no Sparrow ever appeared at the start points (#4).
  - Saving at the Malibu Club loaded you under the map (#5).
- **Map and data fixes from a full audit:** a Starfish Island seabed piece rendered as a submarine, and a beach texture
  dictionary was missing textures.

## Features

- Vice City NextGen (Revolution Team), through Kn4ckers' Complete Edition release
- VCANGE 7.0.1 content: reworked scripts and missions, restored weapons and aircraft, extra vehicles, map areas
  (Everglades, Gator Keys and more), HUD, fonts, loading screens and fixes
- Proper HD Era Vehicles – Lore Friendly (with its VCANGE handling tweaks) and Realistic Handling & Physics
- Restored weapons with their animations
- HD map textures (visually reviewed; replacements that changed the art or the 1986 look were left out)
- Interior and cutscene-prop improvements (IaCF_VC, CRTP) and outfit-aware cutscenes (Cutscene Recognition)
- VCNG HD Pedestrians with photoreal textures; photoreal Tommy, cutscene characters and props
- Photoreal palm trees, grass, sand, water and city surfaces
- Clear "Mint" turquoise water, turquoise Miami ocean and pools, and a 1980s Miami weather and lighting palette
- GTA V-style health regeneration (native script; refills to half the bar while standing still)
- Native F7 Vice City Menu with Gameplay Tweaks
- Native Euphoria ragdolls and Army/MMA melee moves
- Repeatable Downtown shooting range and wheelie/stoppie cash bonuses
- Project Thunder lightning and Xbox rain droplets
- DLSS / FSR upscaling through DLSS-IV, on NVIDIA, AMD and Intel cards
- FusionFix 5.1.2 included; nothing else to download

## Controls added by this build

| Key (controller) | Action |
|---|---|
| **F7** (D-pad up, or hold LB + D-pad right) | Open/close the Vice City Menu. Arrows/numpad to move, Enter to select, Backspace/numpad 0 (B) to go back |
| **Ctrl+F7** | Project Thunder menu (force a storm, bolts, a blackout) |
| **N** (in a vehicle) | Skip to the next radio track |
| **Tab** (on foot) | Holster / draw your weapon |
| **F1** (in a car) | Seatbelt on/off |
| **Space** (after a fall) | Get up |
| Hold aim with fists, bat, knife or cue, then **Z / V / C / X / G** | Melee moves. **F5** switches between Army and MMA style |

The N and Tab tweaks can be switched off in the F7 menu under Gameplay Tweaks.

## What is different from VCANGE

VCANGE replaces your game with the 2008 `GTAIV.exe` 1.0.8.0 (Games for Windows Live) and relies on .NET script mods.
This build keeps the Complete Edition. The important .NET features were rebuilt as native game scripts instead.

| VCANGE component | In this build |
|---|---|
| `update` folder content | Included in full, plus the fixes listed here |
| Downgrade to GTAIV.exe 1.0.8.0 + xlive | Not used; stays on Complete Edition 1.2.0.59 |
| ZMenu | Replaced by the native F7 Vice City Menu |
| LibertyTweaks, HPAPRegen, Enhanced Fighting And Euphoria (.NET) | Rebuilt as native scripts (Gameplay Tweaks, health regen, Euphoria & fighting) |
| ProjectThunderIV (.NET) | Replaced by Project Thunder CE (native plugin) |
| Other .NET scripts (WeapFuncs, MoveImprove, wanted/flying/death music, …) | Not included (they don't run under Proton) |
| Plugins built for 1.0.7/1.0.8 only (IVTweaker, ScriptHook, XLivelessAddon, V-Handling) | Not included. FusionFix covers the limit increases |
| ZolikaPatch | Not included (no confirmed Complete Edition support). Pop-up headlights on three cars stay fixed |
| VCANGE's own sound set | **Not included** (see Known limitations) |

### Fixes applied while porting (Releases 1–3)

- Save-loading crash: two props (`amb_paperbag05`, `amb_comb`) were registered by two collision dictionaries in the
  RestoredWeapons archive; the duplicates were removed.
- Save-loading crash: VCANGE's audio configuration breaks loading saves, so the original Vice City audio set is used.
- "Riot" mission: the player could lose control after the intro cutscene (the script waited for a sound bank that does
  not exist); fixed.
- Damaged textures repaired: APC and Infernus (vehicles), tear gas, AUG and P90 (weapons). In Release 5 the broken AUG and
  tear gas models were replaced with working GTA IV models (see What's new in Release 5).
- Missing weapon animation sets added (AK-47, Tec-9, Uzi, minigun, flamethrower, AUG, M16, AA-12, sawn-off, grenade
  launcher); VCANGE expected them from an optional pack.
- Missing assets that VCANGE's scripts use: bribe-star pickups, the adrenaline pickup animation, two Ocean Beach neon signs,
  alternate-outfit cutscenes.
- Data fixes: vehicle colour names (Armadillo, Grancher), a traffic-group typo (Schafter), the sniper animation set.

## Requirements

- A legal copy of **GTA IV: The Complete Edition**, updated to **1.2.0.59** (Steam or Rockstar Games Launcher)
- About **9 GB** of free space
- Linux / Steam Deck: Proton (tested with Proton-CachyOS through Lutris/umu). Windows: nothing extra

## Installation

> [!WARNING]
> Follow these steps exactly. Skipping one will most likely make the game crash on startup.

### Step 1: Clean base
1. Start with a **fresh, unmodded** installation of GTA IV: The Complete Edition, and start the game once.
2. If your game folder already has `plugins` or `update` folders (from FusionFix or another mod), delete them.

### Step 2: Copy the build into the game
1. Download **all** the `.7z.00x` parts of Release 5 into one folder and extract the first one (`.001`) with 7-Zip.
   You get a folder called **Vice City Actual Next Gen**.
2. Copy **everything inside** that folder into your GTA IV folder, next to `GTAIV.exe`. Replace files when asked.
3. **Linux / Steam Deck only:** if the mod doesn't load, add this to the game's Steam launch options:
   `WINEDLLOVERRIDES="dinput8=n,b" %command%`
   (In Lutris: *Runner options → DLL overrides →* `dinput8` = `n,b`.)

### Step 3: Clear the old configuration (mandatory for new installs)
Delete your old GTA IV documents folder so settings from a previous setup don't carry over.

> [!CAUTION]
> This folder also holds your **save games** (in `Profiles`). Copy it somewhere safe first if you want to keep them.

- **Windows:** delete `Documents\Rockstar Games\GTA IV`.
- **Linux / Steam Deck (Proton):** delete the `GTA IV` folder in the game's prefix:
  - Steam: `~/.steam/steam/steamapps/compatdata/12210/pfx/drive_c/users/steamuser/Documents/Rockstar Games/GTA IV/`
  - Lutris or other launchers: `<your prefix>/drive_c/users/steamuser/Documents/Rockstar Games/GTA IV/`

### First launch
- Start the game and choose **New Game**.
- For DLSS / FSR, open the game's **Display** settings and set **Graphics API: Vulkan** and **Anti-aliasing: Off**.
  DLSS-IV then chooses automatically: DLSS on NVIDIA RTX cards, FSR on AMD, Intel and older NVIDIA cards (FSR 4 on Radeon
  RX 7000/9000, FSR 3.1 elsewhere).
- Linux with an NVIDIA card: DLSS needs NVAPI enabled in Proton (`PROTON_ENABLE_NVAPI=1`, or the DLSS option in Lutris).
  AMD and Intel cards need nothing extra.

## Updating from Release 4

Delete the `update` and `plugins` folders from your game folder, then do **Step 2** above. You don't need Step 3.
Release 4 saves keep working (Release 5 changes textures, the water colour, one map placement and two weapon models, not the
save format). Coming from Release 3: the same steps work, and your saves should load too.

## Uninstalling

Delete everything you copied from the **Vice City Actual Next Gen** folder: the `update`, `plugins` and `DLSSIV` folders and
the loose files next to `GTAIV.exe` (`dinput8.dll`, `d3d9.dll`, `DLSS-IV.asi`, `DLSS-IV.cfg`, `DLSS-IV README.txt`, `vulkan.dll`,
`update.txt`, `README.md`). The original game files were never changed.

## Saves

- Saves made with VCANGE itself (on 1.0.8.0), or with VCANGE's sound set, are not compatible. Start a New Game.
- The F7 menu's "Save Game Now" refuses to save during a mission, because a mid-mission save can break mission progress.
  Use it in free roam.

## Known limitations

- **VCANGE's Vice City sounds** (original sirens, tyre skids, weapon sounds) are not included. Every variant tried still
  crashed when loading a save, so the original Vice City NextGen audio is used.
- The shooting range and stunt bonuses are lightly tested.
- Some reports on VCANGE's tracker could not be confirmed in this build's files and are not changed. Please report them if
  you hit them, with the mission and what you were doing:
  - Crashes in Vigilante and in Gun Runner (grenades on the trucks).
  - Paramedic level 10.
  - The V.I.P. waiting at the airport.
  - Martha's Mug Shot.
  - Police sitting on air in No Escape.
  - FPS drops on the new islands and around the Diaz mansion.
- The Taxi side mission shows its "100 fares" completion message only when you leave the taxi (the reward itself arrives on time).
- A desk prop in the bank interior is missing (it is missing from the base mod as well).
- The airport seaplane dock was disabled by VCANGE and stays disabled.
- The AUG looks like GTA IV's M4 and the tear gas like GTA IV's grenade when held or thrown, because VCANGE's own models are
  broken. Their HUD icons are still the Vice City ones.
- Ocean Beach lost the dirt streaks on one building north of the Ammu-Nation (the layer that caused the glitch).
- Some IaCF interior models reference a few detail textures that exist nowhere; those surfaces look slightly flatter.

## Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce) (legal release, used as the base)
- **GTATrilogyGames** and contributors: [Vice City Actual Next-Gen Edition (VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **ItsClonkAndre**: Project Thunder (GPL-3.0), here as the Complete Edition port Project Thunder CE
- **CGP**: Enhanced Fighting And Euphoria (rebuilt natively here)
- **LibertyTweaks** authors (features rebuilt natively); **Zolika1351**: ZMenu (the inspiration for the F7 menu's layout)
- Authors of **Proper HD Era Vehicles – Lore Friendly**, **Realistic Handling & Physics** (Shturmovik2), **VCNG HD Pedestrians**,
  the **VCNE HD map pack**, **RestoredWeapons**, **IaCF_VC**, **CRTP**, **Cutscene Recognition**, **Mint Ocean Waters**,
  **TreesMod** and **DLSS-IV**

## Disclaimer

This is an unofficial fan project, not affiliated with Rockstar Games, Take-Two Interactive or the authors of the included
mods. It contains only modified and added files; you need your own legal copy of GTA IV: The Complete Edition. Please
support the original mod authors and respect their redistribution terms.
