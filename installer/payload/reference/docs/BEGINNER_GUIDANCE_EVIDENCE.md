# Beginner guidance evidence and writing boundary

This working record defines the factual boundary for the Version 3 beginner-facing explanation. It will be retained in the published reference package so readers can distinguish supported facts from configuration-specific expectations.

## What can be stated plainly

| Statement | Evidence boundary | Source |
|---|---|---|
| GTA IV Complete Edition remains a supported target for FusionFix. | FusionFix’s official repository says that the project addresses issues in Grand Theft Auto IV: The Complete Edition and adds features for modern systems. | [FusionFix repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) |
| A person does not need to manually assemble every individual fix to begin. | Gillian’s Drag-and-Drop Archive describes itself as a complete ready-to-play single-player modpack intended to be extracted into the directory containing `GTAIV.exe`. | [Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) |
| The archive’s Complete Edition route is the appropriate project baseline. | Gillian describes Complete Edition 1.2.0.59 as the more stable/polished choice and the recommended first-time vanilla-faithful route; FusionFix identifies Complete Edition as its fully supported version. | [Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) [FusionFix repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) |
| Vulkan is an in-game FusionFix Graphics API choice and requires restart when changed. | FusionFix documents its selectable DirectX 9/Vulkan graphics API option. Gillian’s archive instruction says to select Vulkan after its intended setup, but to recover by deleting `d3d9.cfg` and leaving that setting alone if Vulkan crashes. | [FusionFix repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) [Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) |
| The package must not promise universal frame rate or flawless operation. | Gillian explicitly warns that the archive is not the best-performance option and can reduce performance in favor of a vanilla-faithful experience. Performance depends on hardware, drivers, resolution, settings, caches, and other mods. | [Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) |
| The project’s administrator flag is an intentional local reliability workaround, not a general property of every GTA IV installation. | This package’s field-tested target layout needs `GTAIV.exe` to create configuration files successfully. The Version 3 installer records the user’s existing compatibility state, adds the per-user `RUNASADMIN` setting, and restores the prior state on uninstall. | Local installer contract: `docs/COMPLETE_INSTALL_CONTRACT.md` |

## What the beginner guide must explain

A first-time user needs an ordinary Windows folder, a legitimate GTA IV Complete Edition installation, the complete Drag-and-Drop Archive, and the Version 3 installer. They do **not** need to know what a renderer, an ASI, a shader, a loader, or an `.ini` file is before beginning.

The guide must lead with the practical claim: this is a maintained Windows configuration path for a real current Complete Edition setup, including modern integrated Radeon systems, not a claim that any computer will run the game at a particular frame rate. It must separately identify the project’s reference Ryzen 5 PRO 6650U/Radeon 660M test context as a reference configuration rather than a hardware requirement or performance guarantee.

The correct beginner sequence is: make a clean copy; put Gillian’s complete archive into the game folder; run its setup utility as directed; put its launch options in the chosen launcher/shortcut; launch once and select the archive’s Graphics API choice when it works; close the game; run this package’s installer against the folder containing `GTAIV.exe`; then make a calm first test. Each step needs a clear success check and a safe response to a mismatch.

## Safety and scope boundary

This project does not provide the game, mod binaries, bypass DRM, make multiplayer supported, or turn every old save/mod combination into a supported configuration. It avoids updating FusionFix inside Gillian’s version-locked archive, avoids adding random extra mods, and preserves an uninstaller path. Troubleshooting begins with verifying the folder and prerequisites, not layering more tools on top.

## References

[1] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[2] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[3] [Gillian’s GTA IV Modding Guide — FusionFix](https://gillian-guide.github.io/essential-modding/fusionfix/)
