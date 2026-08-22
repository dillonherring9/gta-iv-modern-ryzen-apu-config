# Version 4 Primary-Source Findings

This record preserves the key external findings used to evaluate the user-supplied audit and design Version 4. It does not replace the fuller adoption decision in [`V4_AUDIT_ADOPTION.md`](V4_AUDIT_ADOPTION.md).

| Finding | Source-backed conclusion | Version 4 consequence |
|---|---|---|
| Complete Edition archive | Gillian describes the Drag-and-Drop Archive as a ready-to-play single-player modpack. The Complete Edition route is 1.2.0.59; the archive must be extracted into the game folder; a clean copy is required; the current page says it was updated July 18, 2026. | Version 4 retains the Complete Edition/archive prerequisite and never tries to become a replacement modpack. |
| Setup Utility | Gillian says `GTAIVSetupUtility.exe` is a **failsafe** method to install DXVK and launch options, not mandatory for archive functionality. | Version 4 requires a working `vulkan.dll` route before it changes configuration, but does not tell a working archive installation to reinstall DXVK as a ritual. |
| Archive maintenance status | Gillian’s pages state the site has been in minimal-maintenance mode from January 2026 and future modpack updates may be sparse. | Version 4 explains this dependency honestly and preserves handoff/migration material. |
| FusionFix controls | FusionFix documents new in-game menu options, including Graphics API, FPS limiter, anti-aliasing, fog, sun shafts, ambient occlusion, shadow filter, and more. Gillian’s FusionFix page says most options are available in the in-game settings, while `.ini`/`.cfg` editing remains available for outside-menu settings. | Version 4 keeps the complete archive configuration files but presents menu-first graphics guidance rather than fabricating a broad new static `.ini` profile. |
| FusionFix release | The current upstream latest release record is `v5.0.1`, published 2026-05-12. | Version 4 does not replace the archive’s FusionFix binary or tell users to update it independently. |
| GPLAsync `v3.0-1` | The published tag is `v3.0-1`, based on DXVK 3.0. Its README says async is enabled with `DXVK_ASYNC=1` or `dxvk.enableAsync=true`; it says `dxvk.gplAsyncCache` was removed in `v2.7-1`. | Version 4’s advanced Radeon configuration uses `dxvk.enableAsync = true` only and excludes the removed cache key and older unverified async-thread key. |
| AMD Enhanced Sync | AMD documents Enhanced Sync as supporting Vulkan games. DXVK documents `d3d9.presentInterval = 0` as disabling DXVK VSync, positive values as enabling it, and `-1` as no override. | The current advanced migration guide documents this only as an explicit one-owner synchronization experiment, not an automatic installer setting. |

## Primary sources

1. [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
2. [Gillian’s FusionFix guide](https://gillian-guide.github.io/essential-modding/fusionfix/)
3. [FusionFix upstream repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
4. [FusionFix latest-release API record](https://api.github.com/repos/ThirteenAG/GTAIV.EFLC.FusionFix/releases/latest)
5. [GPLAsync v3.0-1 release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v3.0-1)
6. [GPLAsync v3.0-1 README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)
7. [DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)
8. [AMD Enhanced Sync documentation](https://www.amd.com/en/products/software/adrenalin/software-enhancedsync.html)
