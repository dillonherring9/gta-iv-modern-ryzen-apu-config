# GTA IV Version 2 Automatic Installer — Build and Test Notes

This directory contains the auditable **NSIS source** for `GTAIV_V2_Automatic_Installer_v2.0.1.exe`, its nine-file configuration payload, and `INSTALLER_PAYLOAD_SHA256SUMS.txt` for payload verification. It does **not** include GTA IV, launcher files, saved games, executable replacements, DXVK binaries, FusionFix binaries, or other third-party mod assets.

## What Version 2.0.1 fixes

Version 2.0.1 aligns the embedded nine-file payload with the current tracked `tuned/` profile. It also fixes the packaging gap by storing the complete installer source in this repository and fixes the documented Windows removal path by registering the installer in **Installed apps**. The uninstaller removes that registration when it finishes.

| Item | Version 2.0.1 behavior |
|---|---|
| Game-folder check | Installation proceeds only when the selected folder directly contains `GTAIV.EXE`. |
| Managed files | The installer writes exactly nine configuration files to the destinations documented in the main README. |
| Backup | Existing managed files are copied to a timestamped folder under `GTAIV_V2_Installer_Backups` before replacement. |
| Compatibility setting | The current user's prior `GTAIV.EXE` compatibility value is recorded; the installer sets `RUNASADMIN`, and a rollback restores the recorded value. |
| Installed apps | The installer registers itself in Windows **Settings → Apps → Installed apps** with an uninstall command. |
| Uninstall | Choosing **Yes** restores the latest installer backup; choosing **No** only removes the uninstaller registration and executable. |

## Prerequisites

Build on Windows with **NSIS 3.09 or newer** installed. The `makensis` compiler must be available from Command Prompt or called by its full path. Keep the directory layout intact because the script uses relative `payload\` paths.

## Verify the payload

From this directory, compare the SHA-256 hash of every file under `payload\` with `INSTALLER_PAYLOAD_SHA256SUMS.txt` before compiling. The manifest paths are relative to the `payload` directory.

```powershell
Set-Location .\payload
Get-FileHash -Algorithm SHA256 .\root\dxvk.conf
Get-FileHash -Algorithm SHA256 .\pc\stream.ini
Get-FileHash -Algorithm SHA256 .\plugins\AudioMap.ini
Get-FileHash -Algorithm SHA256 .\plugins\ConsoleSelectMenuIV.ini
Get-FileHash -Algorithm SHA256 .\plugins\GTAIV.EFLC.FusionFix.cfg
Get-FileHash -Algorithm SHA256 .\plugins\GTAIV.EFLC.FusionFix.ini
Get-FileHash -Algorithm SHA256 .\plugins\GTAIV.XboxRainDroplets.ini
Get-FileHash -Algorithm SHA256 .\plugins\LibertyCityPlates.txt
Get-FileHash -Algorithm SHA256 .\common\shaders\preload.list
```

## Build

Open Command Prompt in this `installer` directory and run:

```text
makensis GTAIV_V2_Automatic_Installer.nsi
```

A successful compilation writes:

```text
build\GTAIV_V2_Automatic_Installer_v2.0.1.exe
```

## Safe Windows test checklist

Use a disposable copy of a legitimate GTA IV installation or back up the real game folder first. Close GTA IV and the Rockstar Games Launcher before the test.

| Step | Expected result |
|---|---|
| Select a folder without `GTAIV.EXE` | Installation is blocked with a clear folder-selection message. |
| Select the real game folder and install | Nine files are written, a timestamped backup is created, and the installer appears in Installed apps. |
| Check `GTAIV_V2_Installer_Backups` | `LAST_BACKUP.ini`, `backup_manifest.txt`, and prior versions of files that existed before installation are present. |
| Start the uninstaller and choose **Yes** | The latest backup is restored and the former compatibility value is restored. |
| Check Installed apps again | The installer entry is removed. |

> **Do not test this installer against an unbacked-up modded game folder.** The installer protects its nine managed files, but it cannot diagnose or reverse unrelated mod, launcher, driver, or renderer changes.

## Attribution and distribution

This source contains only the installer script, documentation, checksum manifest, and configuration payload supplied for this package. Preserve the original credits, licenses, and distribution terms for GTA IV, FusionFix, DXVK, Gillian’s resources, and every third-party mod used with the configuration.
