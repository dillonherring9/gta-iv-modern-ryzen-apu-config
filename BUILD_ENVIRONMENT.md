# Build Environment Contract

Version 3.0.2 releases are built from an immutable Git tag by the repository’s Windows release workflow. This document describes the expected environment; each generated release also includes a `SOURCE_COMMIT.txt` file containing the exact commit used to build it.

| Field | Release contract |
|---|---|
| Operating system | GitHub Actions `windows-latest` runner |
| Installer compiler | NSIS 3.09 or newer, installed through Chocolatey |
| Source revision | Exact commit resolved by the release tag; copied to `SOURCE_COMMIT.txt` |
| Installer command | `makensis installer\GTAIV_V3_Automatic_Installer.nsi` |
| Installer output | `installer\build\GTAIV_Core_Configuration_Installer_v<version>.exe` |
| Asset integrity | SHA-256 manifest generated after all assets are built |
| Source archive | Generated from the tagged checkout, excluding Git metadata and generated build directories |

## Reproducibility boundary

The source archive, source commit, build command, and all-assets SHA-256 manifest are required for every release. NSIS self-extracting archives may not be byte-identical when built with a different compiler stub, operating system, or compression environment. A byte mismatch alone is not proof that a published artifact is invalid; it is a signal to compare the source commit, compiler version, manifest, and embedded payload before trusting a rebuild.

## Release verification

A release is complete only when the GitHub release, tag, `SOURCE_COMMIT.txt`, source archive, installer executable, selected renderer profiles, and SHA-256 manifest all identify the same version. Do not create a release from a branch tip and apply a tag later. Do not move or retag historical releases; publish a corrective version instead.
