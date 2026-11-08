# VCANGCE source

Developer sources for **Vice City Actual Next Gen CE** (**VCANGCE**), targeting
GTA IV Complete Edition **1.2.0.59** and aligned with release checkpoint **221**.
Players can find the playable package through the repository's main README
and release downloads.

This folder contains all **96 current game-script assembly sources**, five
maintained release-native modules, the two project DualSense binary patches,
portable build tools and the Opus source subset used by the controller loader.
It excludes game executables, compiled plugins and binary game assets. Some
scripts were reconstructed from current bytecode; the export does not contain
every original author's high-level source.

The current scripts include the native parachute launcher, player and AI
controllers, the automatic wearable-aircraft adapter, and the movement
compatibility adaptation. Entering a full-size plane or helicopter provides
the pack automatically; RC aircraft are excluded, and the pack does not occupy
a weapon slot. The CE stack and opcode corrections and native startup-lock
repairs are included. Existing game features are retained in the release.

| Start here | Purpose |
|---|---|
| [BUILDING.md](BUILDING.md) | Dependencies and commands |
| [SOURCE-STATUS.md](SOURCE-STATUS.md) | Reproduction results and source gaps |
| [PROVENANCE.json](PROVENANCE.json) / [VALIDATION.json](VALIDATION.json) | Source origins and verification evidence |
| [scripts/](scripts/) | Current SCO assembly and archive manifest |
| [native/](native/) | Native modules and DualSense patches |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Changes and useful verification |
| [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) | Component notices and licensing scope |
| [CREDITS.md](CREDITS.md) | Current VCANGCE release credits |

The build tools create separate outputs. They do not install the mod or run
the game. This source export rebuilds the covered scripts and modules; it does
not assemble the full playable release or rebuild its map, model, texture and
audio assets. Read the source status for vendor-source gaps.

Run `python3 tools/verify_source.py` to check the snapshot's file set and
checksums before editing it.
