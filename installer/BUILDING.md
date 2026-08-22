# GTA IV, After Dark — Complete Installer Build and Release Guide

The installer is a **complete cumulative package**, not a delta update. Every Version 3 executable embeds the current core configuration, both renderer profiles, all retained optional component settings, the legacy preload reference, the full documentation tree, sources, credits, dependency locks, validation protocol, and project history.

A complete installer does not automatically overwrite every embedded file. The safe core profile is installed after prerequisite checks. Renderer, plugin-dependent, and shader-layout-dependent files remain embedded in the reference bundle because their correct deployment depends on the user’s installed third-party components.

## Embedded payload contract

| Payload area | Installed automatically | Included in complete package |
|---|---|---|
| `pc/stream.ini` | Yes, to `pc\stream.ini` | Yes |
| FusionFix `.cfg` and `.ini` | Yes, to `plugins\` after FusionFix/Vulkan checks | Yes |
| Per-user `GTAIV.exe` `RUNASADMIN` setting | Yes; prior value is saved and restored on uninstall | Documented in the reference bundle |
| Stock DXVK profile | No; selected manually after verifying a compatible DXVK route | Yes |
| GPLAsync profile | No; selected manually only with the exact locked GPLAsync artifact | Yes |
| AudioMap, Console Select Menu, Xbox Rain Droplets, LibertyCityPlates settings | No; require their matching plugins/content | Yes |
| Legacy `preload.list` | No; requires a verified static shader layout | Yes |
| Documentation, sources, credits, and historical reasoning | Copied to `GTAIV_After_Dark_Complete_Package` | Yes |

## Safety behavior

The installer requires a folder containing `GTAIV.exe` or a documented nested layout, requires `plugins\GTAIV.EFLC.FusionFix.asi` and `vulkan.dll`, and aborts while GTA IV is running. It creates a timestamped backup of pre-existing core files, records the prior per-user compatibility value, installs the three safe core files, and sets **Run this program as an administrator** for `GTAIV.exe` because the documented layout otherwise blocks GTA IV from creating configuration files.

On uninstall, the installer restores the latest core-file backup and the exact prior compatibility value. It intentionally retains `GTAIV_After_Dark_Complete_Package` so the complete configuration, documentation, credits, and historical record remain available for audit and recovery.

## Local build

Build on Windows with **NSIS 3.09 or newer**. Preserve the `installer/payload/` layout because the script embeds every file beneath it.

```text
cd installer
makensis /DPRODUCT_VERSION=3 GTAIV_V3_Automatic_Installer.nsi
```

The compiler writes:

```text
build\GTAIV_After_Dark_Complete_Configuration_v3.exe
```

Before compiling, generate and verify the manifest for **every** embedded payload file rather than checking only the core files:

```powershell
Set-Location .\payload
Get-ChildItem -Recurse -File | Get-FileHash -Algorithm SHA256
```

Compare the results with [`INSTALLER_PAYLOAD_SHA256SUMS.txt`](INSTALLER_PAYLOAD_SHA256SUMS.txt).

## Whole-major release process

The authoritative route is the Windows workflow at [`.github/workflows/release.yml`](../.github/workflows/release.yml). It triggers only on `v1`, `v2`, or `v3` and builds from the tag’s exact source revision. Each public major release publishes:

1. The complete installer executable.
2. The complete package archive, including all docs, history, tuning files, and payload manifest.
3. The complete auditable source archive.
4. `SHA256SUMS.txt` covering every released asset.

The workflow writes `SOURCE_COMMIT.txt` into both archives. See [`BUILD_ENVIRONMENT.md`](../BUILD_ENVIRONMENT.md) for the complete reproducibility contract.

## Windows smoke-test checklist

Use a disposable GTA IV Complete Edition installation or a complete backup. Test the actual release artifact, not only the source tree.

| Step | Expected result |
|---|---|
| Select a wrong folder | Installer stops before changing files. |
| Select a valid root without FusionFix/Vulkan prerequisite | Installer stops before changing files. |
| Start `GTAIV.exe` and invoke installer | Installer stops before changing files. |
| Install on a valid root | Core files and `RUNASADMIN` are applied; a timestamped backup records the prior files and compatibility value. |
| Inspect `GTAIV_After_Dark_Complete_Package` | Every retained renderer profile, optional component file, preload reference, document, source, credit, and history record is present. |
| Verify optional files | They are present in the reference bundle but no optional plugin-dependent file has been silently overwritten in the game root. |
| Uninstall and choose restore | Latest core backup and exact prior compatibility value are restored; reference bundle remains available. |
| Verify release assets | Installer, complete package archive, source archive, and SHA-256 manifest share the tagged `SOURCE_COMMIT.txt`. |
