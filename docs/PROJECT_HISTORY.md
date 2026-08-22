# GTA IV, After Dark — Project History and Cumulative Design

## The project in one sentence

GTA IV, After Dark is a **reversible GTA IV Complete Edition configuration package** for a documented Ryzen APU reference environment. It packages configuration, installation logic, validation standards, dependency boundaries, sources, credits, and historical reasoning; it does not redistribute GTA IV or third-party binaries and assets.

> Every public package is cumulative. A newer version retains the full prior configuration, the complete documentation, the credit and source record, the evidence boundary, and the rollback story. A release is never a delta-only installer.

## Version 1 — Reference baseline

Version 1 established the project’s reference context: Ryzen 5 PRO 6650U, Radeon 660M, and 16 GB dual-channel memory. It introduced the balanced 60 FPS-oriented FusionFix profile, the APU cache-warm-up rule, the single-limiter rule, the original renderer configuration rationale, and the project’s third-party credit boundary.

The central lesson remains current: shader and pipeline caches must build. A fresh route, new effect, weather condition, driver update, renderer change, resolution change, or major content change can introduce temporary work. Restart when a change requires it, compare warm passes over the same route, and do not represent a first-run impression as a durable result.

## Version 2 — Complete package and installer era

Version 2 made the package operationally complete. It aligned the README, tuning files, documentation, and package contents; introduced the clean game-folder chooser; added timestamped backups and Windows uninstall registration; and made the configuration bundle auditable rather than a loose list of tweaks.

The installer behavior established here remains part of Version 3: it resolves the actual `GTAIV.exe` game root, refuses unknown folder layouts, avoids stale Rockstar registry paths, backs up managed files, and offers rollback. Version 2 also made clear that a complete package does not redistribute Rockstar, FusionFix, DXVK, or third-party mod binaries.

## Version 3 — Complete cumulative configuration

Version 3 combines the retained knowledge from Versions 1 and 2 with the current safe installation contract. It contains the core FusionFix and streaming profile, both renderer configuration routes, all retained optional component settings, the legacy shader-preload reference, dependency locks, cache and validation guidance, sources, credits, build instructions, and every earlier release note.

The default installation is intentionally conservative. It applies the three core configuration files and the required per-user `RUNASADMIN` compatibility setting for `GTAIV.exe`, because the documented installation layout otherwise prevents GTA IV from creating configuration files at launch. The installer records the prior compatibility value and restores it on uninstall.

The same executable embeds the entire retained package. Renderer profiles, Console Select Menu, Xbox Rain Droplets, LibertyCityPlates, and the legacy preload reference are included in the installed `GTAIV_After_Dark_Complete_Package` directory. They are not overwritten automatically because their correct use depends on external plugins, an exact renderer build, or a verified shader layout. This is a complete package with safe deployment boundaries, not a partial installer.

## Renderer and optional-content boundary

The stock DXVK profile contains upstream-compatible keys and remains manually selected. The GPLAsync profile is retained only for the exact documented GPLAsync artifact. Do not mix renderer routes or frame limiters, and change one variable at a time.

Optional component files remain part of the complete package because they are part of the project’s retained configuration knowledge. Their presence does not mean their required third-party plugin, shader, audio, or content files have been verified. Read [Components and Compatibility](COMPONENTS_AND_COMPATIBILITY.md) and [Dependency Locks](DEPENDENCY_LOCKS.md) before applying them.

## Evidence, credits, and sources

Version 3 makes no universal performance promise. The [Validation Protocol](VALIDATION_PROTOCOL.md) defines what must be recorded before describing a profile as tested or log-validated. Credits remain with Rockstar Games, ThirteenAG and FusionFix contributors, DXVK contributors, GPLAsync contributors, Gillian’s GTA IV Modding Guide, LibertyCityPlates, Project2DFX, Xbox Rain Droplets, Console Select Menu, and all other original authors.

The detailed source material has been retained under [`docs/history/`](history/). Those records are historical context, not separate patch downloads.

## Historical records retained

| Record | Why it is retained |
|---|---|
| `version-1-baseline.md` | Original hardware scope, cache guidance, and credit boundary |
| `version-2-package.md` | Complete configuration-package contract |
| `version-2-installer.md` | Folder-selection, backup, and rollback design |
| `version-3-original.md` | Original renderer-path, installer, and migration reasoning |
| `version-3-corrections.md` | Renderer separation, tag integrity, workflow, and administrator-compatibility corrections |

## Sources

- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
- [DXVK](https://github.com/doitsujin/dxvk)
- [DXVK GPLAsync v2.6.2-1](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v2.6.2-1)
- [Gillian’s GTA IV Modding Guide](https://gillian-guide.github.io/)
