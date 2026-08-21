# GTA IV Version 3 Automatic Installer — Build and Test Notes

This directory is the complete auditable source for `GTAIV_V3_Automatic_Installer_v3.0.0.exe`. It carries the nine-file configuration payload, the payload checksum manifest, and the instructions needed to rebuild the installer. It does **not** include GTA IV, Rockstar launcher files, saves, DXVK/FusionFix binaries, executable replacements, or third-party mod assets.

## Version 3: what changed after the city went quiet

Version 3 returns to the runtime log rather than guessing. The user-tested Radeon 660M session reported **DXVK GPLAsync v2.6.2**, Graphics Pipeline Library support, full-screen-exclusive support, and an active `4 / 4` compiler-and-async worker balance. The package now preserves that working renderer profile instead of shipping a reduced file that had lost accepted settings.

| Area | Version 3 behavior |
|---|---|
| DXVK profile | Restores the logged working `dxvk.enableAsync`, `dxvk.numAsyncThreads=4`, `dxvk.gplAsyncCache=True`, and `dxvk.allowFse=true` values alongside the existing four compiler threads. |
| Folder selection | Starts blank and never imports a stale Rockstar registry path. The player selects the real game folder once. |
| Validation | Checks direct and common nested layouts only after **Install** is clicked; it does not trap the player with a disabled button. |
| Version 2 upgrade | Once the Version 3 uninstaller is written, the installer removes the obsolete Version 2 Installed-apps entry and uninstaller. Version 3 backs up the files it replaces before it writes anything. |
| Loose replacement mod | The installer never moves or repackages loose mod files. Keep the tested loose-file route; do not move that replacement into an `update` folder. |
| Rollback | Version 3 restores the latest Version 3 backup and the prior compatibility value when the user chooses **Yes** during uninstall. |

> **After-hours rule:** one renderer, one limiter, one change at a time. Restart GTA IV after renderer or graphics-path changes, then judge the second or third pass—not the first cold lap.

## Prerequisites

Build on Windows with **NSIS 3.09 or newer**. The `makensis` compiler must be available from Command Prompt or called by its full path. Preserve this directory layout because the script uses relative `payload\` paths.

## Verify the payload

From the `installer` directory, enter `payload\` and compare each hash against `INSTALLER_PAYLOAD_SHA256SUMS.txt` before compiling:

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
makensis GTAIV_V3_Automatic_Installer.nsi
```

A successful compilation writes:

```text
build\GTAIV_V3_Automatic_Installer_v3.0.0.exe
```

## Safe Windows test checklist

Use a disposable GTA IV installation or make a complete backup first. Close GTA IV, Rockstar Games Launcher, mod managers, and any tool that may hold game files open.

| Step | Expected result |
|---|---|
| Open the installer | The destination field is blank. |
| Browse to the game folder | Select the folder that contains `GTAIV.EXE`; no duplicate Rockstar path should appear. |
| Click **Install** | The installer verifies the folder after the click and shows the full path if it needs clarification. |
| Check `GTAIV_V3_Installer_Backups` | A timestamped backup contains copies of any managed files that existed before installation. |
| Check `dxvk.conf` | The nine-file payload is present, including the logged GPLAsync `4 / 4` thread profile. |
| Check Installed apps | The Version 3 entry is present; the obsolete Version 2 entry is removed after a successful upgrade. |
| Run the Version 3 uninstaller and choose **Yes** | The latest backup and prior `GTAIV.EXE` compatibility value are restored. |

> **Do not add extra wrappers, ASI loaders, or frame limiters while testing.** The installer protects its own nine files; it cannot untangle unrelated DLL chains, mod precedence rules, driver changes, or loose-file replacements.

## Attribution and distribution

This source contains only the installer script, documentation, checksum manifest, and project configuration payload. Preserve the licenses, credits, and distribution terms for GTA IV, FusionFix, DXVK, Gillian’s resources, and every third-party mod used with this configuration.
