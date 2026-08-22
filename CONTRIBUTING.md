# Contributing and Maintaining This Project

Thank you for considering a contribution. This repository is designed to outlive a single maintainer, but it must not become a pile of untraceable GTA IV tweaks. Every accepted change needs a stated scope, source evidence, a controlled test, a rollback path, and complete release documentation.

> **Start with the handoff guide:** [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md) explains the project boundary, successor-maintenance model, release rules, and user transition path. This file is the practical contribution checklist.

## What belongs here

A contribution belongs here when it improves the documented Windows GTA IV Complete Edition path built on Gillian’s pre-existing Drag-and-Drop Archive. It may improve beginner instructions, clarify a current source, repair an installer/backup/rollback defect, document a reproducible compatibility finding, or adjust a configuration with evidence and repeatable testing.

A contribution does **not** belong here merely because it is a popular mod, a video recommendation, a one-off frame-rate screenshot, an arbitrary DLL, or a collection of unrelated fixes. Do not upload GTA IV files, FusionFix/DXVK binaries, shaders, the Drag-and-Drop Archive, copyrighted music, or other third-party mod content unless the rights holder clearly permits it and the project owner deliberately accepts that scope.

## Before you change a file

Read the current README, [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md), [`docs/COMPLETE_INSTALL_CONTRACT.md`](docs/COMPLETE_INSTALL_CONTRACT.md), [`docs/ORIGINAL_README_RECONCILIATION.md`](docs/ORIGINAL_README_RECONCILIATION.md), and [`docs/MAINTAINER_HANDOFF.md`](docs/MAINTAINER_HANDOFF.md). The original README teaches controlled self-maintenance; the current contract states what Version 3 actually installs automatically.

| Check | Why it matters |
|---|---|
| Identify the exact GTA IV build and archive version/date. | This project targets the Windows Complete Edition archive route; a result from another setup may not transfer. |
| Save the current file/version before testing. | A controlled test is reversible. |
| Change one configuration, renderer, loader, or shader-metadata variable at a time. | Combining changes prevents anyone from identifying what helped or harmed. |
| Restart GTA IV when a graphics/renderer change requires it, then repeat the same route after caches warm. | A first run can be dominated by cache construction rather than steady behavior. |
| Test the main game and both EFLC episodes if the change can affect shared files. | A base-game-only result is incomplete for Complete Edition. |
| Keep the source URL, version/date, log evidence, and exact settings. | Future maintainers need to reproduce the decision instead of trusting memory. |

## A useful issue or pull request

A report should make it possible for another person to reproduce the situation. Include the following information in complete sentences, even if the result is negative.

```text
GTA IV build:
Drag-and-Drop Archive version/date:
Windows version:
CPU / integrated or discrete GPU:
RAM amount and channel configuration:
GPU driver version:
Resolution and refresh rate:
FusionFix version:
DXVK version or variant:
Extra mods/content installed:
Exact files/settings changed:
Exact symptom and when it occurs:
Did the game restart after the change?:
Cold-run result:
Repeat warm-run result:
Main game result:
TLAD result:
TBoGT result:
Rollback result:
Relevant log/error text:
Source links for the proposed change:
```

A pull request should identify all changed files, explain the reason for every functional change, distinguish an observation from a general recommendation, show the test/rollback result, and update all affected documentation. Preserve sources, credits, history, and verification records. Do not bury a configuration change inside a documentation-only description.

## Changes that need extra caution

| Change | Required additional care |
|---|---|
| DXVK or renderer file | Keep the previous `vulkan.dll`; use the correct 32-bit asset; inspect `GTAIV_d3d9.log`; test a warm route; document restoration. |
| FusionFix or ASI loader | Do not treat this as routine within Gillian’s curated archive. Preserve the one-loader rule; do not reintroduce the removed brittle exact-ASI-filename installer check. |
| Shader metadata or `preload.list` | Verify every referenced shader exists in the real target layout. Do not turn the retained legacy reference into an automatic installer copy without a new, reproducible compatibility record. |
| New configuration file | Prove it belongs to the documented archive environment; define its target path, backup behavior, uninstall behavior, source/credit, and test result. |
| Performance tuning | Do not promise a frame rate. Include resolution, scene, cache state, power/thermal context, and any content that changes streaming or GPU pressure. |
| Removal | Preserve the historical explanation. AudioMap is the model: it was removed from active payload because evidence did not support it, while its history remains visible. |

## Local validation before requesting publication

Run the checks appropriate to the change before asking a maintainer to publish a release.

```text
# From the repository root

git diff --check
(cd installer/payload && sha256sum -c ../INSTALLER_PAYLOAD_SHA256SUMS.txt)
makensis -DPRODUCT_VERSION=3 installer/GTAIV_V3_Automatic_Installer.nsi
```

After source/payload files change, synchronize any embedded `installer/payload/reference/` copies and regenerate `installer/INSTALLER_PAYLOAD_SHA256SUMS.txt` before running the manifest check. The installed reference package must not tell a different story from the public repository.

## Release discipline

Version 3 is the current cumulative release. Corrections that remain inside its defined Complete Edition/archive contract must replace the existing `v3` assets in place; do not create serial patch releases or partial installers. The release workflow builds the installer, complete package archive, complete source archive, and `SHA256SUMS.txt` from the tagged commit.

Before calling a release complete, download its public assets and verify the checksum manifest, `SOURCE_COMMIT.txt`, required documentation, seven-file installer contract, AudioMap exclusion, and current release notes. See [`BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md) and [`installer/BUILDING.md`](installer/BUILDING.md) for the detailed complete-release contract.

## Respectful continuity

Contributors may maintain a fork or develop a successor project if they disclose the relationship, preserve third-party credits, avoid false compatibility claims, and give users a clean migration/rollback path. Users are never required to remain on this project if another route becomes better maintained. The only unacceptable outcome is a confusing mixture of competing configurations in the same GTA IV folder.
