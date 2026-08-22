# Build Environment and Complete-Release Contract

Public releases use whole major version tags only: `v1`, `v2`, and `v3`. A public release is a complete cumulative package, never a delta installer or a patch-only asset.

| Field | Complete-release contract |
|---|---|
| Operating system | GitHub Actions `windows-latest` runner |
| Installer compiler | NSIS 3.09 or newer, installed through Chocolatey |
| Release trigger | Whole-major tag: `v1`, `v2`, or `v3`. The current `v3` tag may be deliberately moved to a fully reviewed correction commit so the existing Version 3 assets are replaced in place rather than creating patch-release clutter. |
| Source revision | Exact commit resolved by the release tag; copied to every archive as `SOURCE_COMMIT.txt` |
| Installer command | `makensis /DPRODUCT_VERSION=<major> installer\GTAIV_V3_Automatic_Installer.nsi` |
| Installer output | `installer\build\GTAIV_After_Dark_Complete_Configuration_v<major>.exe` |
| Complete package archive | README, current release note, full `docs/` tree including history, full `tuned/` tree, installer payload manifest, and `SOURCE_COMMIT.txt` |
| Complete source archive | Entire tagged repository source, release workflow, installer source and payload, docs, tuned files, and `SOURCE_COMMIT.txt`; generated build directories are excluded |
| Asset integrity | One SHA-256 manifest generated after all public assets are assembled |

## Cumulative installer rule

The installer payload must include the current core configuration, every retained optional component configuration, every retained renderer profile, the legacy shader-preload reference, documentation, sources, credits, and historical reasoning. It may apply only the safe core configuration automatically where optional files require external plugins, an exact renderer, or a verified shader layout. Those optional files must still be embedded in the executable and preserved in the installed reference bundle.

## Reproducibility boundary

The complete package archive, source archive, source commit, build command, and all-assets SHA-256 manifest are required for every release. NSIS self-extracting archives may not be byte-identical across compiler stubs, operating systems, or compression environments. A byte mismatch alone is not proof that a published artifact is invalid; compare the source commit, compiler version, manifest, and embedded payload before trusting a rebuild.

## Release verification

A major release is complete only when its tag, installer, complete package archive, source archive, and SHA-256 manifest identify the same source commit. Build from the reviewed commit carried by the whole-major tag; when correcting Version 3, move only the existing `v3` tag after validation so the workflow replaces its four named assets in place. Do not ship a release with a subset-only installer, and do not remove historical rationale, credits, sources, or retained configuration files from a later major package.

## Successor-maintainer rule

A successor must keep the public release surface simple: `v1` and `v2` are historical records; `v3` is the complete current release. Before moving `v3`, run the current payload-manifest check and installer build, update every embedded reference copy, then download the published assets and verify `SHA256SUMS.txt` and `SOURCE_COMMIT.txt`. See [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md) and [`CONTRIBUTING.md`](CONTRIBUTING.md) for the evidence and testing standards that accompany this release mechanism.
