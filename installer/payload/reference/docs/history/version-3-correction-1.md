# Version 3.0.2 — Corrective Core Configuration Release

Version 3.0.2 corrects the package boundaries, release traceability, and installer behavior identified after Version 3.0.0. It is not a claim of a newly benchmarked performance result. The underlying FusionFix profile remains a reference configuration for the documented Ryzen 5 PRO 6650U / Radeon 660M context; users must follow the included validation protocol before treating it as a result for their own system.

## Corrected release contract

| Area | Version 3.0.2 correction |
|---|---|
| Source traceability | This release is generated from its immutable `v3.0.2` Git tag. Its source archive includes `SOURCE_COMMIT.txt`. Historical tags and releases are not rewritten. |
| Asset integrity | `SHA256SUMS.txt` covers every published asset, including the installer executable, source archive, and both renderer configuration files. |
| Build disclosure | The repository now records the Windows/NSIS release contract in [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md) and automates the tagged build in GitHub Actions. |
| Default installer scope | The installer now backs up and writes only `stream.ini` plus the two FusionFix profiles. It requires a confirmed game root and installed FusionFix, refuses to proceed while GTA IV is running, and does not set `RUNASADMIN`. |
| Renderer clarity | The default `dxvk-stock.conf` contains upstream-compatible settings only. The GPLAsync configuration is a separate `dxvk-gplasync-v2.6.2-1.conf` profile tied to a named, SHA-256-locked renderer artifact. |
| Shader metadata | The installer no longer overwrites `preload.list`. The legacy file is retained only as reference because current FusionFix shader handling and any installed shader set must determine valid preload metadata. |
| Optional mod settings | AudioMap, Console Select Menu, Xbox Rain Droplets, and LibertyCityPlates configuration are no longer installed automatically. They remain manual optional components with dependency requirements. |
| Evidence boundary | “Log-validated” and equivalent performance language has been removed from the release claim. [`docs/VALIDATION_PROTOCOL.md`](docs/VALIDATION_PROTOCOL.md) defines the evidence gate for future testing claims. |

## Upgrade guidance

Version 3.0.2 does not delete Version 2 or Version 3.0.0 backups or uninstall entries. If a prior installer is installed, remove installers in **reverse installation order** after making a complete game-folder backup. The Version 3.0.2 installer uses its own backup root and its uninstaller restores only files it managed.

Do not let this release replace a working `dxvk.conf` automatically. Select either the stock DXVK baseline or the exact GPLAsync profile manually after checking [`docs/COMPONENTS_AND_COMPATIBILITY.md`](docs/COMPONENTS_AND_COMPATIBILITY.md). Keep one renderer route, one limiter, and one changed variable at a time.

## Known boundaries

The installer cannot prove the game build, diagnose third-party mod precedence, repair a broken FusionFix/DXVK installation, confirm optional content, or establish performance on a new computer. It is a reversible core-configuration layer, not a complete modpack installer.

## Credits and sources

Version 3.0.2 contains no Rockstar, FusionFix, DXVK, GPLAsync, or third-party mod binaries. Credit remains with the original projects and their contributors.

- [FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
- [DXVK](https://github.com/doitsujin/dxvk)
- [DXVK GPLAsync v2.6.2-1 release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v2.6.2-1)
- [Gillian’s GTA IV Modding Guide](https://gillian-guide.github.io/)
