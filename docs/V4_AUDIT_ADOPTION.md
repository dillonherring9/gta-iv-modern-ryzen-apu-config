# Version 4 Audit Adoption Decision

## Purpose

Version 4 is a **separate, complete cumulative release** based on a full review of the user-supplied `gta-iv-after-dark-audit.zip` archive. It does not move the existing `v3` tag, alter the Version 3 release assets, or pretend that a menu guide alone replaces a complete installer.

The supplied archive was extracted and read as **reference material only**. Its PowerShell and NSIS files were not executed. The attached archive SHA-256 is:

```text
5694423c8bdc25657660419d409a5015130e8bf11c8390c337e9070f3697aa30
```

Its original report and beginner-oriented settings material are retained in the Version 4 reference package for credit and traceability. This decision document explains how their recommendations were adopted or constrained.

## Evidence-based adoption table

| Audit recommendation | Version 4 decision | Reason |
|---|---|---|
| Explain the project as a reversible configuration layer on top of a pre-existing Complete Edition Drag-and-Drop Archive. | **Adopt.** | It accurately describes the project boundary and avoids redistribution claims. |
| Give newcomers separate, readable general-APU and Radeon 660M paths. | **Adopt with guardrails.** | A general stock-DXVK profile is appropriate for a broad modern-iGPU starting point. The 660M path is an explicitly advanced GPLAsync `v3.0-1` choice, not an automatic hardware detection claim. |
| Use FusionFix’s in-game settings as the primary user-facing control surface. | **Adopt.** | FusionFix documents that most options are available in the in-game settings, while its files remain useful for advanced/out-of-menu configuration.[^1] |
| Stop installing the full seven-file configuration set because the menu is newer. | **Reject.** | The complete Drag-and-Drop Archive contract still requires backup/restore of the seven documented managed files. Version 4 installs those files and provides menu guidance as the readable, current adjustment layer. |
| Replace the native installer with a PowerShell wrapper. | **Reject.** | The audit prototype only manages four files, warns instead of enforcing the Vulkan prerequisite, uses a broad `ExecutionPolicy Bypass` wrapper, and does not preserve the full installer contract. Version 4 extends the native NSIS pattern instead. |
| Treat `GTAIVSetupUtility.exe` and **Reinstall DXVK** as mandatory. | **Correct.** | Gillian describes the Setup Utility as a failsafe path rather than a mandatory archive prerequisite. Version 4 requires a working archive Vulkan route before it writes configuration, but does not tell a working installation to reinstall DXVK merely to satisfy a checklist.[^2] |
| General APU profile uses upstream-compatible DXVK keys only. | **Adopt.** | It is the safest automatic renderer profile when the exact archive renderer has not been verified. |
| Radeon 660M profile uses GPLAsync `v3.0-1`. | **Adopt as opt-in only.** | The user must select it knowingly and independently confirm the exact GPLAsync artifact in `GTAIV_d3d9.log`. The package never distributes or replaces `vulkan.dll`. |
| Copy `dxvk.gplAsyncCache` into the new GPLAsync profile. | **Reject.** | GPLAsync removed that key in `v2.7-1`; it is retained only inside the historical 2.6.2 reference profile.[^3] |
| Copy `dxvk.numAsyncThreads = 4` into the new GPLAsync profile. | **Do not install automatically.** | The current GPLAsync `v3.0-1` README documents `dxvk.enableAsync`, not that older thread-count key. It remains a local, log-gated experiment only. |
| State that 60 FPS is guaranteed on a Radeon 660M. | **Reject.** | The project must not turn one reference system into a universal performance promise. Version 4 uses a 60 FPS target as a starting point and requires warm-cache testing. |
| Hide Gillian’s minimal-maintenance status. | **Reject.** | Version 4 states it clearly and provides a maintenance/handoff path, while respecting the archive as the required baseline.[^2] |

## Version 4 installer contract

Version 4 remains a configuration installer for a **GTA IV Complete Edition game root where Gillian’s Complete Edition Drag-and-Drop Archive has already been extracted**. It does not provide the game, archive, FusionFix, DXVK/GPLAsync binaries, shader files, or other third-party mod content.

It will preserve the Version 3 safety contract: validate the game root and `vulkan.dll`; refuse to write while `GTAIV.exe` is running; back up and restore the seven managed configuration files; set and restore the per-user `RUNASADMIN` compatibility value for `GTAIV.exe`; retain all sources/credits/history/reference material; exclude AudioMap; and avoid the brittle exact FusionFix `.asi` filename check.

Version 4 adds an explicit profile choice:

| Profile | Installer behavior | When to choose it |
|---|---|---|
| **General modern APU** | Installs the upstream-compatible stock DXVK baseline plus the six non-renderer archive configurations. | Recommended for a modern integrated GPU when the installed renderer is unknown, stock DXVK, DXVK-Sarek, or another non-GPLAsync path. |
| **Radeon 660M / GPLAsync `v3.0-1`** | Installs the same six non-renderer archive configurations plus a minimal, no-legacy-cache GPLAsync overlay. | Choose only after `GTAIV_d3d9.log` confirms the separate GPLAsync `v3.0-1` artifact. It never installs a renderer DLL. |

The general profile remains the default. The Radeon profile includes `dxvk.enableAsync = true`, but not `dxvk.gplAsyncCache` or undocumented old thread-count settings. Every user receives the full menu-first guide, renderer identification guide, GPLAsync migration guide, audit adoption record, and retained historical material.

## References

[^1]: [FusionFix configuration guide](https://gillian-guide.github.io/essential-modding/fusionfix/)

[^2]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[^3]: [GPLAsync `v3.0-1` README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)
