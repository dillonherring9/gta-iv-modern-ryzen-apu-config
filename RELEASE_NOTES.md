# Version 4 — Modern APU Configuration

> **Version 4 is a new, complete cumulative release. Version 3 remains unchanged at its existing tag and release page.**

## What Version 4 is

Version 4 is a reversible configuration installer for **GTA IV Complete Edition with Gillian’s Complete Edition Drag-and-Drop Archive already installed in the game folder**. It does not ship GTA IV, Gillian’s archive, FusionFix, DXVK/GPLAsync binaries, shaders, saves, launchers, or third-party mod content. It provides a documented configuration layer, backups, rollback, audit evidence, and a self-maintenance package.[1]

The Version 4 design adopts the useful findings from a separately supplied audit: explain the setup in plain language; expose a safe general modern-APU starting point; treat FusionFix’s in-game settings as the normal graphics adjustment surface; and preserve a narrowly scoped Radeon 660M/GPLAsync path for people who can identify their exact renderer. The audit source report and adoption decision are preserved in the complete package and source archive.

## Complete installer behavior

Version 4 retains the full seven-file Complete Edition archive contract. It validates the GTA IV game root and an existing `vulkan.dll`, refuses to run while `GTAIV.exe` is open, creates a timestamped backup, sets `GTAIV.exe` to run as administrator, and restores both the files and the prior per-user compatibility value during uninstall.

| Managed location | Version 4 file |
|---|---|
| Game root | Selected `dxvk.conf` |
| `pc\` | `stream.ini` |
| `plugins\` | `ConsoleSelectMenuIV.ini` |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` |
| `plugins\` | `LibertyCityPlates.txt` |

The installer keeps every critical safeguard that Version 3 established. It still avoids the brittle exact `GTAIV.EFLC.FusionFix.asi` filename check; does not redistribute renderer binaries; does not write `AudioMap.ini`, a static `preload.list`, or an unvalidated GPLAsync overlay; and does not alter an existing `vulkan.dll`.

## Two explicit renderer-configuration choices

| Choice | Intended user | What is installed |
|---|---|---|
| **General modern APU** — default | A modern AMD/Intel integrated GPU where the archive renderer is unknown, stock DXVK, DXVK-Sarek, or another non-GPLAsync build. | A stock/upstream-compatible `dxvk.conf` with no fork-only settings. |
| **Radeon 660M / GPLAsync `v3.0-1`** — advanced | The Ryzen 5 PRO 6650U/Radeon 660M reference context where `GTAIV_d3d9.log` already confirms the separate GPLAsync `v3.0-1` artifact. | The same baseline plus `dxvk.enableAsync = true`. It excludes `dxvk.gplAsyncCache`, which GPLAsync removed in v2.7-1, and does not apply the old unverified async-thread key.[2] |

The General choice is recommended when uncertain. The advanced choice does **not** detect hardware or install GPLAsync; it simply writes a compatible, minimal configuration after a user knowingly confirms the pre-existing renderer.

## Menu-first graphics guidance

FusionFix documents that most settings are available through its in-game settings, with `.ini`/`.cfg` files available for advanced or outside-menu changes.[3] Version 4 keeps the complete archive configuration files but no longer presents a fixed `.ini` dump as the only current adjustment surface. Its README gives a clear starting point: Vulkan only when the archive route works; one 60 FPS FusionFix limiter; SMAA; high-resolution shadows, ambient occlusion, sun shafts, and volumetric fog off initially; conservative reflection MSAA; and cache-warmed testing before judging traffic/rain/shadow changes.

## Honest dependency boundary

Gillian’s Complete Edition archive is still the required baseline. The guide says the site is in minimal-maintenance mode and future updates may be sparse, so Version 4 documents the dependency rather than implying permanent upstream support.[1] It retains full sources, credits, validation procedures, original Version 3 documents, current Version 4 audit-adoption reasoning, and a successor-maintenance path.

[1] [Gillian’s Complete Edition Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[2] [GPLAsync `v3.0-1` README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)

[3] [FusionFix configuration guidance](https://gillian-guide.github.io/essential-modding/fusionfix/)
