# Manual installation — both editions

## Prerequisites (unchanged from upstream)
1. Legitimate GTA IV Complete Edition (Steam or Rockstar Games Launcher).
2. Gillian's Complete Edition Drag-and-Drop Archive extracted into the folder
   that contains `GTAIV.exe`.
3. `GTAIVSetupUtility.exe` (from the archive) run once — Reinstall DXVK,
   Setup launch options — so a working Vulkan route already exists.
4. GTA IV launched at least once successfully before you touch this profile.

## Choose your edition
- Radeon 660M, or you specifically installed DXVK-GPLAsync → `radeon-660m-edition/`
- Any other modern integrated GPU (other AMD APUs, Intel Iris Xe/Arc, Intel UHD),
  or you're not sure which DXVK build you have → `general-apu-edition/`

If you're not sure which renderer you actually have installed, check
`GTAIV_d3d9.log` in the game folder after launching once — it names the active
DXVK build. Don't guess and swap `vulkan.dll` blindly.

## Steps
1. **Close everything.** GTA IV, Steam/Rockstar Launcher, any mod manager.
2. **Back up first.** Copy at minimum `dxvk.conf`, `plugins\GTAIV.EFLC.FusionFix.ini`,
   and `plugins\GTAIV.EFLC.FusionFix.cfg` from your game folder to a safe location
   with today's date in the folder name. (The scripted installer in `installer/`
   does this automatically with a timestamp — see `installer/BUILD_INSTRUCTIONS.md`.)
3. **Copy the renderer config.** Copy your chosen edition's `dxvk.conf` into the
   GTA IV root folder (next to `GTAIV.exe`), replacing the existing one.
4. **Apply the FusionFix settings.** Open GTA IV, open the FusionFix in-game
   menu, and set the values listed in your edition's `SETTINGS.md`. This is
   intentionally a menu-driven step, not a file copy — see the audit report for
   why. If you also want the handful of verified `.ini`-only keys, add them to
   `plugins\GTAIV.EFLC.FusionFix.ini` by hand, matching the exact key names
   already present in your installed copy of that file.
5. **Restart GTA IV fully** — not just close the menu — so renderer/graphics
   changes actually take effect.
6. **Test properly, not from the first drive.** Load a save, drive daytime
   traffic, enter/exit an interior, then try night + rain. Restart once and
   repeat the same route before judging anything. Shader/pipeline caches need
   that second pass to be representative.
7. **If something's worse, not better:** restore the backup from step 2 before
   trying another change. Change one variable at a time.

## Rolling back
Restore the `dxvk.conf` and FusionFix files from your step-2 backup, then
restart GTA IV. If you used the scripted installer instead, run
`installer/uninstall.ps1` and choose to restore the latest backup.
