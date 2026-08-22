# Check Dependency Freshness Before You Change Anything

> **A newer version is not automatically a better version for an already working GTA IV Drag-and-Drop Archive.** This page helps you identify what is installed, compare it with Version 4’s documented context, and avoid turning a version check into an uncontrolled update.

Version 4 is a configuration layer for Gillian’s Complete Edition archive, not a program that safely updates every dependency. It intentionally does **not** download or replace FusionFix, DXVK/GPLAsync, Ultimate ASI Loader, archive files, shaders, or `vulkan.dll`.

## First rule: identify, then decide

Before changing a dependency, save a copy of the game folder’s current `dxvk.conf`, `vulkan.dll`, and `GTAIV_d3d9.log`. Start GTA IV once, close it normally, and then read `GTAIV_d3d9.log` in the game folder. That log is the first source for the renderer actually being used.

Use this checklist after a new Version 4 installation, before an advanced renderer experiment, when troubleshooting a new problem, or before adopting a successor project. Do **not** run it every day and do **not** update a working archive simply because a website shows a newer number.

| Component | Version 4 documented context, August 22, 2026 | How to identify your installed state | Safe action when different |
|---|---|---|---|
| GTA IV | Windows **Complete Edition 1.2.0.59**. | Use the launcher/game properties and confirm the archive is the Complete Edition route. | Do not use Version 4 on a downgraded/legacy game copy. |
| Gillian’s Drag-and-Drop Archive | Complete Edition archive, documented as updated July 18, 2026. Gillian says the site is in minimal-maintenance mode and future updates may be sparse.[^1] | Keep the archive download date/checksum and consult its current archive page. | Start from a clean archive install if you intentionally update. Do not layer a new archive on top of a mixed old mod folder. |
| FusionFix | The current archive record names **FusionFix 5.0.1**. Most settings are available in the in-game menu; the archive advises against enabling FusionFix update checks.[^1] [^2] | Read the installed FusionFix files/release notes and record the archive date. | Do not click an automatic update inside the curated archive. Treat a manual FusionFix change as a separate controlled experiment or use a clean archive update. |
| DXVK / renderer route | The General profile accepts an existing upstream-compatible archive route; it does not require one DXVK version. Upstream DXVK 3.0.2 is a reference snapshot, not an instruction to replace `vulkan.dll`.[^3] | Read `GTAIV_d3d9.log` and hash `vulkan.dll` with PowerShell: `Get-FileHash -Algorithm SHA256 .\vulkan.dll`. | Keep a working renderer unless you have a reason to test another one. Never download a random DLL or stack another wrapper. |
| GPLAsync | The advanced Version 4 profile is only for the separate **GPLAsync `v3.0-1`** artifact after log confirmation. | `GTAIV_d3d9.log`, the exact artifact source, and the `vulkan.dll` hash. | Use [`GPLASYNC_V3_0_1_MIGRATION.md`](GPLASYNC_V3_0_1_MIGRATION.md). Do not copy the old `gplAsyncCache` key; it was removed in v2.7-1.[^4] |
| Ultimate ASI Loader | One intended **x86** loader path, not a version requirement for Version 4. | Inspect the archive’s loader files and keep a record of the archive version/date. | Do not add a second loader to “make sure” a plugin works. Treat a loader update as a separate reversible test. |

## What Version 4 can and cannot tell you

The Version 4 installer can prove that you selected a folder containing `GTAIV.exe`, that an existing `vulkan.dll` is present, and that GTA IV is closed before it writes the seven managed configuration files. It cannot truthfully infer a renderer’s safety from the filename alone, prove that every third-party binary is current, or safely update a curated archive without risking component mismatch.

> **If the game works, the version check is a record—not an instruction to update.** Keep the record with your backup. If the game does not work, make one controlled change, restart, test a repeat cache-warmed route, and restore the old files if the result is worse.

## If you find a mismatch

| You discover | Do this first | Do not do this |
|---|---|---|
| A newer archive release | Read its changelog and update instructions, back up the entire game folder, then use a clean archive update path. | Do not drop selected new `.asi` or `update` files into an old mixed install. |
| A newer FusionFix release | Keep the archive’s bundled build unless you deliberately test an update or rebuild from a compatible archive revision. | Do not enable automatic FusionFix updates inside the archive. |
| A newer upstream DXVK release | Record your working `vulkan.dll` hash and log first. Only test the correct x86 `d3d9.dll` renamed as `vulkan.dll` in a controlled rollback experiment. | Do not assume a newer DXVK release contains GPLAsync or copy fork-only settings into it. |
| GPLAsync differs from `v3.0-1` | Treat it as a different renderer artifact with its own settings/log evidence. | Do not reuse Version 4’s advanced profile without reviewing that artifact’s documentation. |
| You cannot identify the renderer | Leave the General modern APU profile selected and ask for help with the log, renderer hash, archive date, GPU/driver, and exact symptom. | Do not reinstall DXVK or replace DLLs merely to make the version table look current. |

## Sources to re-check

The following primary pages are the correct places to compare new upstream information. Record the date you checked them in an issue, test note, or handoff record.

1. [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
2. [FusionFix upstream releases](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases)
3. [FusionFix configuration guidance](https://gillian-guide.github.io/essential-modding/fusionfix/)
4. [DXVK upstream releases](https://github.com/doitsujin/dxvk/releases)
5. [GPLAsync releases](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases)
6. [Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)

## References

[^1]: [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[^2]: [FusionFix configuration guidance](https://gillian-guide.github.io/essential-modding/fusionfix/)

[^3]: [DXVK `v3.0.2` release](https://github.com/doitsujin/dxvk/releases/tag/v3.0.2)

[^4]: [GPLAsync `v3.0-1` README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)
