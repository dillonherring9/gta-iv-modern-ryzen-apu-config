# Version 3 Complete Installer Build Guide

Version 3 is a complete configuration installer for **GTA IV Complete Edition with Gillian’s Drag-and-Drop Archive already installed**. It writes the audited seven-file configuration set, backs up all seven before overwrite, applies the required `GTAIV.exe` administrator compatibility setting, and retains complete documentation/history beside the game.

## Managed payload

| Destination | Payload file |
|---|---|
| Game root | `root\dxvk.conf` |
| `pc\` | `stream.ini` |
| `plugins\` | `ConsoleSelectMenuIV.ini` |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` |
| `plugins\` | `LibertyCityPlates.txt` |

The installer does **not** manage `AudioMap.ini`; it is not part of the documented archive contract. The GPLAsync profile and legacy preload list remain in the reference bundle only, because each requires a distinct renderer artifact or verified shader layout.

## Safety and rollback

The installer requires a confirmed `GTAIV.exe` root, `vulkan.dll`, and a closed game process. It does not require one exact FusionFix ASI filename. It records the old values of all seven managed files and the prior per-user compatibility flags under `GTAIV_After_Dark_Configuration_Backups`. Uninstall restores or removes all seven managed files according to that backup and restores the exact prior compatibility value.

## Local build and validation

Build with NSIS 3.09 or newer:

```text
cd installer
makensis /DPRODUCT_VERSION=3 GTAIV_V3_Automatic_Installer.nsi
```

The output is `build\GTAIV_After_Dark_Complete_Configuration_v3.exe`. Before publishing, verify every file under `payload/` against `INSTALLER_PAYLOAD_SHA256SUMS.txt`; compile the installer; and confirm the seven managed targets appear in backup, install, and uninstall paths.

## In-place Version 3 replacement

Version 3 corrections replace the existing Version 3 release assets under the same `v3` tag. Do not create a patch-number release. Rebuild the installer, complete package archive, source archive, and `SHA256SUMS.txt` from the corrected source; delete the four existing Version 3 assets; upload the four replacement assets to the same release; and preserve the Version 3 title and release URL.
