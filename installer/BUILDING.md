# GTA IV Version 3.0.2 Core Configuration Installer — Build and Release Guide

Version 3.0.2 is a **narrow, reversible FusionFix configuration installer**. It does not install GTA IV, renderer binaries, ASI loaders, shader files, optional mod plugins, saved games, or game archives. It writes only three core configuration files after positive game-root and FusionFix checks.

## Managed installer payload

| Payload file | Destination | Why it is in the automatic scope |
|---|---|---|
| `payload/pc/stream.ini` | `pc\stream.ini` | Core streaming configuration used with the reference package |
| `payload/plugins/GTAIV.EFLC.FusionFix.cfg` | `plugins\GTAIV.EFLC.FusionFix.cfg` | Main FusionFix graphics and frame-limit profile |
| `payload/plugins/GTAIV.EFLC.FusionFix.ini` | `plugins\GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix shadow, limiter, vehicle-budget, and distant-light profile |

The installer does **not** automatically install `dxvk.conf`, `preload.list`, AudioMap, ConsoleSelectMenu, Xbox Rain Droplets, or LibertyCityPlates configuration. Those files are optional and require their separate dependency contracts; see [`docs/COMPONENTS_AND_COMPATIBILITY.md`](../docs/COMPONENTS_AND_COMPATIBILITY.md).

## Installer safety behavior

The installer requires a folder containing `GTAIV.exe` or one of the documented nested game-folder layouts. It also requires `plugins\GTAIV.EFLC.FusionFix.asi` and aborts when `GTAIV.exe` is running. It creates a timestamped backup of pre-existing managed files, installs the three core files, and provides an uninstaller that restores the latest Version 3.0.2 backup on request.

It deliberately does not set a Windows **Run as administrator** flag on the game executable, cannot infer the player’s complete mod stack, and cannot validate performance. Close the Rockstar Games Launcher, GTA IV, mod managers, and tools that may hold files before installation.

## Local build

Build on Windows with **NSIS 3.09 or newer**. Preserve the installer directory layout because the script uses relative `payload\` paths.

```text
cd installer
makensis GTAIV_V3_Automatic_Installer.nsi
```

The compiler writes:

```text
build\GTAIV_Core_Configuration_Installer_v3.0.2.exe
```

Before compiling, verify the exact embedded files from the `installer/payload` directory:

```powershell
Set-Location .\payload
Get-FileHash -Algorithm SHA256 .\pc\stream.ini
Get-FileHash -Algorithm SHA256 .\plugins\GTAIV.EFLC.FusionFix.cfg
Get-FileHash -Algorithm SHA256 .\plugins\GTAIV.EFLC.FusionFix.ini
```

Compare these hashes with [`INSTALLER_PAYLOAD_SHA256SUMS.txt`](INSTALLER_PAYLOAD_SHA256SUMS.txt).

## Release process

The authoritative release route is the Windows GitHub Actions workflow at [`.github/workflows/release.yml`](../.github/workflows/release.yml). It is triggered only by a `v*` tag and performs the following work from the tagged commit:

1. Builds the NSIS installer on Windows.
2. Generates `SOURCE_COMMIT.txt` from the checked-out commit.
3. Creates a source archive containing the source, docs, payload, renderer profiles, and build metadata.
4. Creates a SHA-256 manifest covering **every published asset**, including the installer EXE.
5. Publishes the tagged release with those generated assets.

The workflow records its toolchain expectations in [`BUILD_ENVIRONMENT.md`](../BUILD_ENVIRONMENT.md). A byte-identical EXE rebuild is preferred but not assumed across different NSIS stubs or Windows build environments. The complete manifest and source commit are the minimum release-integrity contract.

## Windows smoke test checklist

Use a disposable GTA IV Complete Edition installation or a complete backup. The test must verify the actual release artifact, not only the source tree.

| Step | Expected result |
|---|---|
| Select a wrong folder | Installer stops before any file is changed. |
| Select a valid root without FusionFix | Installer stops before any file is changed. |
| Start `GTAIV.exe` and invoke installer | Installer stops before any file is changed. |
| Install with a valid FusionFix root | Timestamped backup contains any pre-existing core files; only the three core files are changed. |
| Inspect game root | No `dxvk.conf`, static `preload.list`, optional plugin configuration, third-party binary, or executable compatibility flag is added by the installer. |
| Uninstall and choose restore | Latest Version 3.0.2 backup restores each managed file, or installer-created files are removed. |
| Run the validation protocol | The game, all applicable episodes, and the selected renderer pass the evidence gates in [`docs/VALIDATION_PROTOCOL.md`](../docs/VALIDATION_PROTOCOL.md). |
