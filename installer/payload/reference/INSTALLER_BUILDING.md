# Version 4 Complete Installer Build Guide

Version 4 is a complete configuration installer for **GTA IV Complete Edition with Gillian’s Complete Edition Drag-and-Drop Archive already installed**. It preserves Version 3’s full seven-file safety contract while adding an explicit renderer-configuration choice. It does not modify the historical Version 3 installer source, tag, or release assets.

## Managed payload

Both Version 4 choices back up, install, and restore the same seven documented archive configuration files.

| Destination | Payload file |
|---|---|
| Game root | Selected `root\general\dxvk.conf` or `root\radeon660m\dxvk.conf`, installed as `dxvk.conf` |
| `pc\` | `stream.ini` |
| `plugins\` | `ConsoleSelectMenuIV.ini` |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` |
| `plugins\` | `LibertyCityPlates.txt` |

The default **General modern APU** profile contains only upstream-compatible DXVK settings. The advanced **Radeon 660M / GPLAsync `v3.0-1`** profile is an explicit user choice for a system where `GTAIV_d3d9.log` already identifies that separate renderer. It adds `dxvk.enableAsync = true` only; it does not install `vulkan.dll`, `dxvk.gplAsyncCache`, or the old undocumented async-thread setting.

The installer does **not** manage `AudioMap.ini`; it is not part of the documented archive contract. The historical GPLAsync 2.6.2 profile and legacy preload list remain in the reference bundle only, because each requires an exact renderer artifact or verified shader layout.

## Safety and rollback

The installer requires a confirmed `GTAIV.exe` root, an existing `vulkan.dll`, and a closed GTA IV process. It does not require one exact FusionFix ASI filename. It backs up all seven files and the prior per-user `GTAIV.exe` compatibility value under `GTAIV_After_Dark_Configuration_V4_Backups`.

The installer sets `RUNASADMIN` for `GTAIV.exe` on every Version 4 installation. The Version 4 uninstaller restores or removes the same seven files according to its latest backup, restores the exact prior compatibility value, and uses its own V4 uninstaller/registry/reference-package paths. It does not overwrite Version 3’s backup or uninstall registration.

## Local build and validation

Build with NSIS 3.09 or newer:

```text
cd installer
makensis /DPRODUCT_VERSION=4 GTAIV_V4_Automatic_Installer.nsi
```

The output is `build\GTAIV_After_Dark_Complete_Configuration_v4.exe`. Before publication, regenerate and verify `INSTALLER_PAYLOAD_SHA256SUMS.txt`, compile the installer, and confirm all of the following:

1. The two profile source files exist under `payload\root\general\` and `payload\root\radeon660m\`.
2. The installer presents General as the default and warns before the advanced Radeon/GPLAsync choice.
3. Both choices write exactly one root `dxvk.conf` and the six other managed archive configuration files.
4. Backup, install, and uninstall cover all seven files.
5. `RUNASADMIN`, game-root, `vulkan.dll`, and closed-process safeguards remain present.
6. The exact `GTAIV.EFLC.FusionFix.asi` filename check remains absent.
7. No active payload file is named `AudioMap.ini`.
8. The embedded reference bundle contains the V4 contract, audit-adoption decision, audit-source provenance/readmes, GPLAsync migration guide, current documentation, and preserved V3 history.

## Separate Version 4 publication

Version 4 is released under the separate `v4` tag. The existing `v3` tag and Version 3 release assets are protected historical material and must not be changed during Version 4 work.

After the initial Version 4 release, a correction that remains inside the V4 contract replaces the four existing `v4` assets in place: installer, complete package archive, source archive, and `SHA256SUMS.txt`. Do not create patch-number releases. Download the public assets and verify both `SHA256SUMS.txt` and `SOURCE_COMMIT.txt` before calling a Version 4 publication complete.
