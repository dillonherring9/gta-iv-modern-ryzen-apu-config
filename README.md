Vice City Actual Next Gen — Release 6

**GTA Vice City NextGen + VCANGE 7.0.1, ported to GTA IV: The Complete Edition (1.2.0.59).**
Windows, Linux and Steam Deck (Proton). No .NET script hooks. FusionFix 5.1.2 and DLSS-IV are included and already set up.
Copy everything inside the extracted R6 folder next to `GTAIV.exe` and start a New Game.

The conversion uses FusionFix's `update` overlay, native plugins and the included loader/upscaler files.
It does not replace `GTAIV.exe` or the original Rockstar data archives.

> [!IMPORTANT]
> I did not create the original mod assets or code. I ported, fixed and packaged them to run natively on the
> Complete Edition with FusionFix. All credit for the mods goes to the authors listed under [Credits](#credits).

## What's new in Release 6

### User-confirmed improvements

- **Photoreal texture overhaul across the build:** brings together our photoreal passes for city surfaces, palms and
  plants, grass, sand, water, Tommy, cutscene and story characters, pedestrians, clothing and props. Text, logos and
  important original details are preserved, and original textures remain where replacements failed review.
- **Final city texture pass:** 298 of 301 selected map textures upgraded across 34 map archives, covering common
  surfaces, building details and more blurry 256-pixel textures. Twenty-eight results use the original layout with fine
  generated detail added; three were left original after review. These counts describe this pass, not a unique total
  across all releases.
- **Independence FM included:** GTA IV's station is now the tenth station alongside Vice City's nine, with its IV logo
  and the correct name in the radio settings. The separate Release 5 add-on is no longer needed.
- **Revised health regeneration included:** after five seconds without damage, health regenerates up to half the bar,
  including while walking, running or driving. Cutscenes refill health to full and keep it full for two seconds afterward.
  Armour is unchanged. The separate Release 5 health patch is no longer needed.
- **Miami sky and 1980s colour grade:** seamless cloud noise, a denser starfield, and warmer pink/peach dawns and sunsets.
  Fair-weather lighting has a more vivid Miami palette; the later colour-grade pass leaves rain, fog and storms unchanged.
- **Vehicle and menu corrections:** repaired handling records, invalid paint choices and RC Baron colour grouping;
  restored Contender and Washington names; corrected F7 vehicle labels, previous-spawn deletion, per-car door locks and
  failed model-load cleanup. Vehicle material links and the Dinghy's shared texture link were also repaired.
- **Pool Cue fighting:** the cue now receives the enhanced melee moves. Katana support is retained.
- **Jury Fury loading hang fixed:** Ken's cutscene face texture was reduced from 4096 to 2048 pixels to reduce its memory
  allocation. The user confirmed this resolved the hang before that cutscene.
- **Cutscene memory helper:** reduces pedestrian/vehicle model budgets during cutscene loading and playback, then restores
  them afterward. Its current cleanup targets distant ambient entities outside a protected 150-metre area around the
  player. The user reported success in cutscenes played so far; this is not a guarantee that every cutscene has been tested.

### Included fixes awaiting gameplay confirmation

These changes are verified in the R6 files. Their specific feature/mission tests and the latest combined New Game → save →
quit → relaunch → load test remain pending.

- **Story-based wanted limits:** outside missions, the cap is four stars initially, five after Guardian Angels, and six
  after Rub Out. Trespass checks no longer block the progression logic or reduce an existing wanted level. Missions keep
  control of their own temporary limits.
- **Six-star army response:** a native script adds a Rhino, a Barracks and soldiers during suitable outdoor free-roam
  pursuits. Enemy tanks ram rather than fire their cannon. Spawning, pursuit and cleanup still need an in-game test.
- **F7 Malibu Club teleport:** the destination now uses Malibu's entrance area instead of the Links View Apartment area.
  Arrival on foot and in a car still needs checking.
- **Failed animation/model requests:** the F7 animation player and the Sparrow helper release requests when loading times out.
- **Missing vehicle textures and bus repair:** 24 missing texture entries were added across 11 vehicle dictionaries, using
  original sources where available and documented substitutes elsewhere. The dictionaries were then rebuilt with corrected
  memory-page layouts to address the bus texture noise introduced by the first repair. Vehicle appearance needs checking.
- **Mission animations and messages:** corrected Cortez/Love Fist animation references and a map-blip label; supplied nine
  missing Sunshine Autos race messages; restored the original film-studio tax and Kickstart wreck messages; corrected
  classic vehicle names and matching F7 labels. These mission/UI paths still need testing.

## What was new in Release 5

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
- Native health regeneration after five seconds without damage, while moving or driving, up to half the bar; full health during cutscenes
- Independence FM as the tenth radio station, with its correct name and logo
- Miami sky with seamless clouds, more stars and the revised 1980s colour grade
- Native F7 Vice City Menu with Gameplay Tweaks
- Native Euphoria ragdolls and Army/MMA melee moves
- Story-based wanted limits and a native six-star army response (included; gameplay confirmation pending)
- Cutscene memory helper and the user-confirmed Jury Fury loading repair
- Repeatable Downtown shooting range and wheelie/stoppie cash bonuses
- Project Thunder lightning and Xbox rain droplets
- DLSS / FSR upscaling through DLSS-IV, on NVIDIA, AMD and Intel cards
- FusionFix 5.1.2 included

## Controls added by this build

| Key / controller input | Action |
|---|---|
| **F7** / **LB + D-pad right** | Toggle the Vice City Menu. **D-pad up** also opens it when closed. |
| Arrows / numpad / D-pad | Navigate the menu. |
| Enter / numpad 5 / A | Select. |
| Backspace / numpad 0 / B | Go back. |
| **Ctrl+F7** | Open the Project Thunder menu. |
| **Alt+V** | Toggle the first-person camera. |
| **N**, in a vehicle | Skip to the next radio track. |
| **Tab**, on foot | Holster / draw your weapon. |
| **F1**, in a car | Toggle the seatbelt. |
| **Space**, after a fall | Get up. |
| Hold aim with fists, bat, knife, Pool Cue or Katana, then **Z / V / C / X / G** | Enhanced melee moves. **F5** switches Army/MMA style. |

The N and Tab tweaks can be disabled under F7 → Gameplay Tweaks. Additional trainer shortcut keys are off by default.
When using Project Thunder to force weather, return to normal weather before saving, as directed by its included configuration.

## Independence FM and your own music

Independence FM is already enabled; its station audio comes from your own GTA IV installation.

For personal tracks, put MP3s in `Documents\Rockstar Games\GTA IV\User Music`, scan them from the pause menu's Audio
settings, and enable Independence FM Mode. Under Proton, use the equivalent folder inside the game's Wine prefix.
The normal station has been user-tested; playback of personal music under Linux/Proton remains unconfirmed.

## What is different from VCANGE

VCANGE replaces your game with the 2008 `GTAIV.exe` 1.0.8.0 (Games for Windows Live) and relies on .NET script mods.
This build keeps the Complete Edition. The important .NET features were rebuilt as native game scripts instead.

| VCANGE component | Complete Edition build |
|---|---|
| Main conversion content | Adapted for the FusionFix overlay, with repaired dependencies and the changes listed above. |
| Downgraded `GTAIV.exe` 1.0.8.0 and xlive | Uses Complete Edition 1.2.0.59. |
| ZMenu | Replaced by the native F7 Vice City Menu. |
| Selected LibertyTweaks, HPAPRegen and Enhanced Fighting And Euphoria features | Rebuilt as native game scripts. |
| ProjectThunderIV .NET version | Replaced by the native Project Thunder CE plugin. |
| Other .NET scripts | Excluded from this build. |
| Old-version plugins such as IVTweaker, ScriptHook, XLivelessAddon and V-Handling | Excluded; the build uses FusionFix's available fixes and limits. |
| ZolikaPatch | Excluded. |
| VCANGE audio configuration | Replaced by the working original Vice City NextGen audio configuration, with Independence FM added. |
| VolumetricLights plugin | Removed in Release 5; FusionFix's own light volumes remain enabled. |

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

- Your own legal copy of **GTA IV: The Complete Edition 1.2.0.59**.
- About **10 GB** for the extracted build, in addition to GTA IV and the downloaded archive parts. The extracted build occupies approximately 8.84 GiB.
- On Linux / Steam Deck: Proton. Local gameplay testing was performed with Proton-CachyOS through Lutris/umu.
  Separate Windows and Steam Deck testing is not documented in the current test record.

## Installation

1. Close the game. Start from an unmodded Complete Edition installation and launch it once before adding the conversion.
2. If the game already has modded `update`, `plugins` or `DLSSIV` folders, move them somewhere safe outside the game folder
   before installing. Back up existing loader/upscaler files if another setup already uses them.
3. Download **all five parts**, `R6.7z.001` through `R6.7z.005`, into the same folder. Open `.001` with 7-Zip and extract it;
   the other parts are read automatically.
4. Copy **everything inside the extracted R6 folder** into the GTA IV folder containing `GTAIV.exe`. The copied `update`
   and `plugins` folders must sit directly beside the executable.
5. **Keep your GTA IV Documents folder, `Profiles` saves and `User Music`. No Documents-folder deletion is required.**
6. Launch the game and choose **New Game** for a fresh installation.

On Linux / Steam Deck, if the loader does not activate, use this Steam launch option:

```text
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

In Lutris, add the equivalent DLL override: `dinput8` = `n,b`.

### Graphics and upscaling

- R6 includes `d3d9.cfg` with Vulkan selected. In Display settings, verify **Graphics API: Vulkan** and set
  **Anti-aliasing: Off** for DLSS-IV.
- The bundled DLSS-IV configuration selects its upscaler automatically: DLSS on supported NVIDIA RTX hardware, with FSR
  for other supported GPUs. The chosen FSR implementation depends on hardware and the bundled DLSS-IV version.
- NVIDIA under Proton needs NVAPI available; the existing setup uses `PROTON_ENABLE_NVAPI=1` or the launcher's DLSS/NVAPI option.
- The bundled configuration has HDR disabled.

## Updating from Release 5

1. Close the game and back up your saves and any personal mod settings.
2. Move the existing build's `update`, `plugins` and `DLSSIV` folders out of the game folder.
3. Copy the complete R6 contents beside `GTAIV.exe`, replacing the previous build's loader/upscaler files when prompted.
4. Keep your Documents folder and saves. Test New Game, then save → quit → relaunch → load, plus the features you use.

R6 incorporates both R5 add-ons. **Do not apply their old Install or Uninstall folders over R6:** they contain older files,
including a whole `script.img`, and would overwrite later fixes.

Compatibility of existing R5 saves with the latest combined R6 batch has not yet been confirmed. Keep your saves and a
backup, then test your own save. VCANGE's incompatible audio configuration previously caused save-loading failures;
start a New Game when moving from that configuration.

## Uninstalling

Close the game, then remove this build's `update`, `plugins` and `DLSSIV` folders and its loose files beside `GTAIV.exe`:
`dinput8.dll`, `d3d9.dll`, `d3d9.cfg`, `DLSS-IV.asi`, `DLSS-IV.cfg`, `vulkan.dll` and `update.txt`.
Remove the build's README if you copied it there. Restore any files you backed up from a previous mod setup.

Keep the GTA IV Documents folder and its saves. The conversion does not replace the original Rockstar executable or data archives.

## Saves

Use F7 → Gameplay Tweaks → Save Game Now in free roam. It refuses to save while a mission is active.

Save locations:

- **Windows:** `Documents\Rockstar Games\GTA IV\Profiles\`.
- **Steam / Proton:** `<Steam library>/steamapps/compatdata/12210/pfx/drive_c/users/steamuser/Documents/Rockstar Games/GTA IV/Profiles/`.
- **Lutris / other Wine prefixes:** `<prefix>/drive_c/users/steamuser/Documents/Rockstar Games/GTA IV/Profiles/`.

Back up `Profiles` before changing builds. Preserve `User Music` separately if you use custom radio tracks.

## Known limitations

**User-tested** means the result is recorded in the current rebuild log, not that every mission has been completed.
**Verified in files** means installation and static checks passed; it is not an in-game test.
The pending items above correspond to steps 60–65 and 70–73. The current cutscene helper has only the reported
“success so far” coverage. R6 should not be described as a fully tested playthrough.

- VCANGE's extra sound configuration repeatedly broke save loading and remains excluded. The working original VCNG audio
  configuration is retained.
- The shooting range and wheelie/stoppie bonuses have not yet received a documented user test.
- Unconfirmed VCANGE tracker reports remain open: Vigilante crashes, Gun Runner truck/grenade crashes, Paramedic level 10,
  The V.I.P. airport wait, Martha's Mug Shot, police sitting on air in No Escape, and performance on the additional islands
  or around Diaz's mansion. These are reports to reproduce, not confirmed failures of R6.
- The Taxi side mission's “100 fares” message appears when leaving the taxi; its reward arrives at the intended point.
- A bank desk prop is absent from the base conversion. VCANGE's disabled airport seaplane dock remains disabled.
- The AUG and tear gas retain their gameplay slots and HUD icons but use the replacement IV models described above.
- The repaired Ocean Beach building no longer has the removed dirt-streak layer.
- Some IaCF models reference unavailable detail textures, leaving those surfaces less detailed.
- Higher-resolution cutscene assets remain installed. The Jury Fury repair and memory helper do not establish that every
  other cutscene is free from loading or memory issues.

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
