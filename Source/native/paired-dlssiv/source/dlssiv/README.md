# DLSS-IV

DLSS-IV adds DLSS, DLAA, FSR and XeSS to Grand Theft Auto IV and Episodes from Liberty City. It reads real motion
vectors out of the game's engine, and it also adds HDR output and frame generation.

This repository is the source code. It has no releases. To play the mod, download it from the Nexus page, which is
also the place for questions and bug reports:

https://www.nexusmods.com/games/gta4/mods/1430

## How it is put together

- `DLSS-IV.asi` (32-bit, `source/`) runs inside the game. It reads the engine's matrices and depth, draws the motion
  vectors, and hands the frame to the helper through DXVK's Vulkan interop.
- `dlssiv-helper.exe` (64-bit, `host/`) runs the upscalers: NVIDIA DLSS through NGX, AMD FSR through the FidelityFX
  API, and Intel XeSS. They are 64-bit only, which is why the helper is a separate program.
- `dlssiv-fg.dll` (64-bit, `ffx-own/`) is the frame generation library. It is built from AMD's FidelityFX SDK source in
  `external/ffx`, with this project's own changes.

The game needs FusionFix with its Graphics API option set to Vulkan, and DXVK. The mod's download from Nexus has
everything else a player needs.

## What you need to build

- Windows 10 or 11.
- Visual Studio 2022, or its Build Tools, with the C++ x86 and x64 tools and a Windows SDK. The build scripts find it
  with `vswhere`. To use a different install, set `DLSSIV_VSROOT` to its folder.
- Windows PowerShell 5.1, which comes with Windows.
- Python 3 on `PATH`. `build.ps1` runs `tools/check-cfg.py`, and the checks and test harnesses in `tools/` are Python.
  Without Python, `build.ps1` skips that check and says so.
- git, optionally. The build id in the log and the date in the binaries' headers come from the current commit. Without
  git the id is `unknown-source`.

## Get the NVIDIA and Intel files

The NVIDIA DLSS SDK and Intel XeSS licences do not allow their files in this repository, so two scripts fetch them:

```
.\tools\fetch-ngx.ps1
.\tools\fetch-xess.ps1
```

`fetch-ngx.ps1` downloads six headers, the import library and NVIDIA's licence from
https://github.com/NVIDIA/DLSS. `fetch-xess.ps1` downloads Intel's XeSS SDK 3.0.2 zip (about 77 MB) from
https://github.com/intel/xess and takes the headers and the licence out of it. Both check every file against a
recorded hash and refuse anything else. If you already have the files, pass `-FromLocal` with a folder (for XeSS, the
zip itself works too) and nothing is downloaded. Git ignores everything the two scripts write.

## Build

In PowerShell, from the repository's root folder:

```
.\build.ps1 -Ship
```

This builds both programs the way a release ships them: `DLSS-IV.asi` in the root folder and
`host\dlssiv-helper.exe`. The other builds:

| Command | What it builds |
|---|---|
| `.\build.ps1` | the development `.asi`, with the extra logging |
| `.\build.ps1 -Release` | the release `.asi` without frame generation |
| `.\build-host.ps1` | the development helper |
| `.\build-host.ps1 -Release -Fg` | the release helper with frame generation (`-Ship` runs this one) |
| `.\build-ffx-own.ps1 -Target dlssiv-fg` | `dlssiv-fg.dll`, into `obj\dlssiv-fg` |

The `.asi` and the helper are a pair. They check each other's protocol version when they connect, so always use two
files built from the same source.

The release helper loads only a `dlssiv-fg.dll` whose size and SHA-256 match `tools/ffx-own/RECORD-dlssiv-fg-cut.txt`.
A build of the library matches that record byte for byte only with the compiler, linker and Windows SDK the record
names.

The compiled shader headers next to each `.hlsl` in `source/shaders` are committed. After you change a shader, run
`.\tools\compile-shaders.ps1` (it uses `fxc` from the Windows SDK). `build.ps1` stops when a shader is newer than its
header.

## Try a build in the game

Install the mod from Nexus first. Then copy your `DLSS-IV.asi` over the one in the game folder, and your
`dlssiv-helper.exe` over the one in its `DLSSIV` folder. `build.ps1 -Deploy <folder>` and
`build-host.ps1 -Deploy <folder>` do the copy, with the environment variable `GTAIV_ROOT` set to the folder that holds
your game folder.

## Checks and tests

`tools/` holds the checks and test harnesses for changes. Each one explains itself at the top of its file. Some to
start with:

```
python tools\check-source-bytes.py
python tools\check-cfg.py
python tools\check-fg-fence.py
.\tools\compile-shaders.ps1 -Verify
python tools\prove-ffx-own.py
```

Some harnesses need the graphics card. Harnesses that stage AMD's FidelityFX DLLs or Intel's `libxess.dll` read them
from `external/ffx` and `external/xess`: put AMD's files from the FidelityFX SDK 2.3.0 release there yourself, and run
`.\tools\fetch-xess.ps1 -Runtime` for Intel's.

## Licence

DLSS-IV is licensed under the Apache License 2.0: see `LICENSE`. `NOTICE` lists the third-party work in this repository
and its licences. AMD's FidelityFX SDK code, the Khronos Vulkan headers, the DLSS5-Feeder files and DXVK's interface
declarations keep their own licences, which are next to them.
