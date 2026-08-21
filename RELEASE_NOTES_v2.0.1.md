# Version 2.0.1 — Installer Discoverability Test Build

Version 2.0.1 is a **test release for the automatic installer only**. This build aligns the installer’s nine embedded configuration files with the current tracked `tuned/` profile, repairs the Windows uninstall experience, and makes the installer source reproducible from the repository.

| Area | Version 2.0.1 change |
|---|---|
| Windows Installed apps | The installer now creates an entry in **Settings → Apps → Installed apps**, matching the rollback path documented in the README. |
| Uninstall cleanup | After the uninstaller completes, it removes its Installed apps entry and the uninstaller executable. |
| Source availability | The complete NSIS source, payload, checksum manifest, build guide, and test checklist are tracked in [`installer/`](installer/). |
| Documentation | The README now separates the executable installer, manual-release ZIP, and auditable installer source, so each download has a clear purpose. |
| Configuration payload | The installer now matches the current nine-file `tuned/` profile exactly, including the current DXVK and FusionFix settings. |

## What to test on Windows

Use a disposable GTA IV installation or back up the game folder before testing. Close GTA IV and the Rockstar Games Launcher, run `GTAIV_V2_Automatic_Installer_v2.0.1.exe` as administrator, and select the directory that directly contains `GTAIV.EXE`.

> **The most important test is the rollback path.** Confirm that the installer appears in Installed apps after installation, that a timestamped backup exists, and that choosing **Yes** during uninstall restores the latest installer backup and removes the Installed apps entry.

The full test checklist, build process, payload-check instructions, and scope boundaries are in [`installer/BUILDING.md`](installer/BUILDING.md).

## Boundaries

This test build contains only the installer and the existing community configuration payload. It does not redistribute GTA IV, launcher files, saves, FusionFix/DXVK binaries, or third-party mod assets. Keep a backup, test one change at a time, and follow original authors’ distribution terms.
