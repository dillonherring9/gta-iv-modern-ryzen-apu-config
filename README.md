# Vice City Actual Next Gen — Release 7

**GTA Vice City NextGen + VCANGE 7.0.1, ported to GTA IV: The Complete Edition (1.2.0.59).**
For Windows, Linux and Steam Deck (Proton); documented local gameplay testing is on Linux/Proton.
No .NET script hooks. FusionFix 5.1.2 and DLSS-IV are included and already set up.
Copy everything inside the extracted R7 folder next to `GTAIV.exe` and start a New Game.

The conversion uses FusionFix's `update` overlay, native plugins and the included loader/upscaler files.
It does not replace `GTAIV.exe` or the original Rockstar data archives.

> [!IMPORTANT]
> I did not create the original mod assets or code. I ported, fixed and packaged them to run natively on the
> Complete Edition with FusionFix. All credit for the mods goes to the authors listed under [Credits](#credits).

## What's new in Release 7

Release 7 focuses on vehicle physics, the Fairmont police cruiser, clearer mission text, a tidier runtime layout and corrected
health regeneration. It carries forward the photoreal textures, native F7 menu and Complete Edition port from Release 6.

**User-tested** means a result was explicitly reported in the current rebuild record. **Verified in files** means resource,
dependency or script checks passed; it does not mean the feature has been tested in game.

### User-tested improvements

- **RC helicopter and Sparrow corrections:** the RC helicopter's physical settings were matched to its toy model, and the
  Sparrow received an isolated centre-of-gravity correction. Both fixes received positive gameplay feedback. Completion of
  every affected mission and side activity has not been separately documented.
- **Better vehicle handling:** the user reported that cars handle dramatically better after the vehicle physics batch.
  Repairs include the DF8/Baller's centre of gravity, Bobber and Hellfury wheel/rider physics transforms, and wheel-physics
  positions in the Emperor, Infernus and Patriot. Individual coverage of every repaired model was not specified.
- **1978 Ford Fairmont VCPD:** the Fairmont replaces the **Beater Police Cruiser** (`policew`) slot, with its author's
  handling, matching wheel settings and four-channel colours. The regular police cruiser keeps its existing model and
  handling. The installed Fairmont received positive in-game feedback.
- **Cleaner project layout:** 324 authoring and reference files were preserved outside the runtime overlay, leaving active
  assets intact. Relaunch, New Game and save → quit → relaunch → load passed for that cleanup checkpoint. Sources and
  reference material are maintained separately from the files used to play.

### Included changes awaiting specific gameplay confirmation

- **Corrected health regeneration:** normal recovery still starts after five seconds without damage and gradually restores
  health up to half the bar. Health or armour damage restarts the delay; armour is not regenerated. Full health is restored
  **once when a cutscene loads/starts and once when it ends/unloads**. The repeated full-heal loop and the extra two-second
  healing window have been removed. This addresses the script behavior associated with prolonged apparent mission immunity;
  actual mission and cutscene behavior still needs an in-game test.
- **Additional vehicle data and resource repairs:** corrected reversed traction values for Sentinel and Sultan and repaired
  vehicle archive resource flags and compressed-stream lengths. These file checks preserve the installed model payloads;
  they do not certify every vehicle's behavior.
- **Clearer Demolition Man warning:** the RC helicopter now has its own out-of-range message instead of displaying a mission
  activation prompt. Ordinary activation prompts keep their existing wording.
- **Completed text entries:** added two labels present in the copied text source but missing from compiled text, and supplied
  the missing English “Japanese” caption in language settings. The two source-only labels have unverified visible triggers;
  the menu-caption repair does not add a new translation. Supplemental text lookup order was also corrected.
- **Blue Vice City menu artwork:** the selected title picture is installed in both main-menu background dictionaries, with
  fitted 16:9, 4:3 and 21:9 versions. Existing buttons and icons are retained. Appearance in game, 16:10 behavior and displays
  wider than 21:9 still need checking.
- **Consolidated runtime archives:** total IMG archives reduced from **57 to 24**. Thirty-eight map archives became seven
  regional packs; weapon and Fairmont resources were consolidated into their main archives. Seven identical resource copies
  were removed while all unique moved resource payloads were preserved. Supplemental text catalogs were reduced from sixteen
  to seven, retaining the same effective text values. These changes reduce package and registration counts; FPS, startup and
  frame-time gains have not been measured.

### Current test coverage

Save/load success, broad driving improvement, the RC/Sparrow fixes and the Fairmont have recorded positive user results.
The last explicitly confirmed combined relaunch, New Game and save → quit → relaunch → load test was **before the later
archive consolidation and health-script correction**. Those final changes passed file checks; the health script also passed
thirteen mocked bytecode scenarios. Their combined gameplay test remains pending.

A scoped dialogue/audio audit found no verified missing dependency among the inspected mission conversation groups,
cutscene captions and requested audio banks. This is an audit result, not a complete mission playthrough or a new voice pack.

## What was new in Release 6

The following sections retain the earlier release history. Historical gameplay reports are not a new certification of the
current Release 7 checkpoint; use the current test coverage above for the recovered build.

### Improvements introduced in Release 6

- **Photoreal texture overhaul across the build:** brings together our photoreal passes for city surfaces, palms and
  plants, grass, sand, water, Tommy, cutscene and story characters, pedestrians, clothing and props. Text, logos and
  important original details are preserved, and original textures remain where replacements failed review.
- **Final city texture pass:** 298 of 301 selected map textures upgraded across 34 map archives, covering common
  surfaces, building details and more blurry 256-pixel textures. Twenty-eight results use the original layout with fine
  generated detail added; three were left original after review. These counts describe this pass, not a unique total
  across all releases.
- **Independence FM included:** GTA IV's station is now the tenth station alongside Vice City's nine, with its IV logo
  and the correct name in the radio settings. The separate Release 5 add-on is no longer needed.
- **Native health regeneration included:** recovery after five seconds without damage, up to half the bar, including
  while walking, running or driving. Release 7 replaces the older cutscene healing behavior with one refill at each boundary,
  as described above. Armour is not regenerated. The separate Release 5 health patch is no longer needed.
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

### Earlier fixes with feature tests still pending

These changes were listed as file-verified in Release 6. Their specific feature and mission results have not been fully
documented in the recovered gameplay record. Current Release 7 startup and save/load coverage is described above.

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
- Native health regeneration after five seconds without damage, while moving or driving, up to half the bar; one full-health refill at cutscene entry and one at exit
- Independence FM as the tenth radio station, with its correct name and logo
- Miami sky with seamless clouds, more stars and the revised 1980s colour grade
- Native F7 Vice City Menu with Gameplay Tweaks
- Native Euphoria ragdolls and Army/MMA melee moves
- Story-based wanted limits and a native six-star army response (included; gameplay confirmation pending)
- Cutscene memory helper and the Jury Fury loading repair carried forward from earlier releases
- Corrected RC helicopter/Sparrow physics, targeted car and motorcycle physics repairs, and the 1978 Ford Fairmont VCPD
- Blue Vice City menu artwork, corrected mission/help text and consolidated runtime archives
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
The earlier release record reports successful normal-station playback; personal music playback under Linux/Proton remains
unconfirmed.

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
- Free space for the extracted conversion, downloaded archive parts and backups, in addition to the installed GTA IV game.
  Use the final Release 7 download listing for its packaged and extracted sizes.
- On Linux / Steam Deck: Proton. Local gameplay testing was performed with Proton-CachyOS through Lutris/umu.
  Separate Windows and Steam Deck testing is not documented in the current test record.

## Installation

1. Close the game. Start from an unmodded Complete Edition installation and launch it once before adding the conversion.
2. If the game already has modded `update`, `plugins` or `DLSSIV` folders, move them somewhere safe outside the game folder
   before installing. Back up existing loader/upscaler files if another setup already uses them.
3. Extract the Release 7 download with 7-Zip. If it is supplied as numbered parts, download **all parts** into the same
   folder and open the first `.001` file; the remaining parts are read automatically.
4. Copy **everything inside the extracted R7 folder** into the GTA IV folder containing `GTAIV.exe`. The copied `update`
   and `plugins` folders must sit directly beside the executable.
5. **Keep your GTA IV Documents folder, `Profiles` saves and `User Music`. No Documents-folder deletion is required.**
6. Launch the game and choose **New Game** for a fresh installation.

On Linux / Steam Deck, if the loader does not activate, use this Steam launch option:

```text
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

In Lutris, add the equivalent DLL override: `dinput8` = `n,b`.

### Graphics and upscaling

- The build includes `d3d9.cfg` with Vulkan selected. In Display settings, verify **Graphics API: Vulkan** and set
  **Anti-aliasing: Off** for DLSS-IV.
- The bundled DLSS-IV configuration selects its upscaler automatically: DLSS on supported NVIDIA RTX hardware, with FSR
  for other supported GPUs. The chosen FSR implementation depends on hardware and the bundled DLSS-IV version.
- NVIDIA under Proton needs NVAPI available; the existing setup uses `PROTON_ENABLE_NVAPI=1` or the launcher's DLSS/NVAPI option.
- The bundled configuration has HDR disabled.

## Updating from Release 6 or earlier

1. Close the game and back up your saves and personal mod settings.
2. Move the existing build's `update`, `plugins` and `DLSSIV` folders somewhere safe outside the game folder. Release 7
   consolidates archives and text catalogs, so an older overlay must be removed before copying the new one.
3. Copy the complete R7 contents beside `GTAIV.exe`, replacing the previous build's loader/upscaler files when prompted.
4. Keep your Documents folder, saves and User Music. No save or configuration reset is required by the new layout or
   health-script correction. Avoid restoring entire old config or archive sets over the new build.
5. Test New Game using a spare slot, then save → quit → relaunch → load. Check the features below during the same test pass.

Release 7 carries forward the R5 add-ons and R6 fixes. **Do not apply the old R5 Install or Uninstall folders over R7:**
they contain older files, including a whole `script.img`, and would overwrite later fixes. Do not add the former separate
Fairmont archive, map packs or supplemental text files back over the consolidated layout.

Earlier checkpoints have successful save/load reports. Compatibility of every existing R5/R6 save with the final combined
R7 checkpoint remains unconfirmed. Keep a backup and test your existing save separately. If moving from VCANGE's incompatible
audio configuration, use a New Game for that transition; keep the old save files.

### One combined gameplay check

- **Startup and saves:** relaunch, check New Game, then save → quit → relaunch → load. Keep your desired save in another slot.
- **Health:** take damage below half health, avoid damage for five seconds and confirm gradual recovery stops halfway.
  Take another hit during the wait to check the delay restarts. Check a cutscene entry and exit refill, then take damage
  during normal mission play to confirm full health is not repeatedly restored.
- **Vehicles and weapons:** try the Fairmont and normal cars, an affected motorcycle, the Sparrow/RC helicopter and a custom
  weapon. Detailed Demolition Man flight, bomb handling and mission completion still need confirmation.
- **World and UI:** drive through beach, mainland, airport and bridge areas, enter an interior and check night props.
  Check the blue menu picture, readable buttons, the RC range warning and the English language-option caption.

This checks the changed systems in one pass; it does not certify every mission or resource.

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
**Verified in files** means file and dependency checks passed; it is not an in-game test. The current Release 7 archive
consolidation and health correction still await their combined gameplay test. A fully tested playthrough is not claimed.

- FPS, load-time and frame-time improvement from the project cleanup and archive consolidation are **unmeasured**.
- Windows and Steam Deck have no separately documented current test pass; local gameplay testing is on Linux/Proton.
- Older per-feature results for the story-based wanted limits, six-star army response, Malibu teleport, vehicle textures,
  mission messages, shooting range and wheelie/stoppie bonuses remain incomplete in the recovered test record.
- RC/Sparrow success does not establish completion of every RC mission or checkpoint activity. Broad driving improvement
  does not establish that every vehicle physics issue is resolved; several ambiguous model observations remain unpatched.
- The new main-menu picture and language/RC text changes still need specific display checks. The two added source-only
  labels have no verified visible triggers. Menu behavior at 16:10 or wider than 21:9 is not certified.
- VCANGE's extra sound configuration previously broke save loading and remains excluded. The working original VCNG audio
  configuration is retained. Personal Independence FM music playback under Linux/Proton remains unconfirmed.
- Unconfirmed reports carried forward from the VCANGE tracker include Vigilante crashes, Gun Runner truck/grenade crashes,
  Paramedic level 10, The V.I.P. airport wait, Martha's Mug Shot, police sitting on air in No Escape, and performance on the
  additional islands or around Diaz's mansion. These are reports to reproduce, not confirmed failures of Release 7.
- The Taxi side mission's “100 fares” message appears when leaving the taxi; its reward arrives at the intended point,
  according to the earlier release record.
- A bank desk prop is absent from the base conversion. VCANGE's disabled airport seaplane dock remains disabled.
- The AUG and tear gas retain their gameplay slots and HUD icons but use replacement GTA IV models.
- The repaired Ocean Beach building no longer has the removed dirt-streak layer.
- Some IaCF models reference unavailable detail textures, leaving those surfaces less detailed.
- Higher-resolution cutscene assets remain installed. The earlier Jury Fury repair and memory helper do not establish that
  every cutscene is free from loading or memory issues. The recovered record does not provide a new complete cutscene test.

## Credits

- **Revolution Team**: GTA Vice City NextGen Edition
- **Kn4ckers**: [Vice City for GTAIV: The Complete Edition](https://github.com/knackers4/vce) (Complete Edition base release)
- **GTATrilogyGames** and contributors: [Vice City Actual Next-Gen Edition (VCANGE)](https://github.com/GTATrilogyGames/ViceCityActualNextGenEditionModpack)
- **ThirteenAG** and contributors: [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) and Xbox Rain Droplets
- **1978 Ford Fairmont VCPD:** **Driver [GCU Modding Team]** (original model), **DMN [GTA-Unlimited Team]** (GTA IV conversion),
  **BubLik [GCU Modding Team]** (updates) and **deepcover0000** (VCPD livery)
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
