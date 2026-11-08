# AVCNGCE source

Developer sources for **Advanced Vice City Next Gen CE**, targeting GTA IV
Complete Edition **1.2.0.59** and aligned with the AVCNGCE release through
project checkpoint **215**. Players can find the playable package through the
repository's main README and release downloads.

This folder contains 93 editable game-script assembly sources, five maintained
native modules, the two project DualSense binary patches, portable build tools
and the Opus source subset needed to compile the controller loader. It contains
no proprietary game executables, compiled plugins, models, textures or audio.
Some sources were reconstructed from current bytecode; this is not a complete
collection of every original author's high level source.

| Start here | Purpose |
|---|---|
| [BUILDING.md](BUILDING.md) | Dependencies and commands |
| [SOURCE-STATUS.md](SOURCE-STATUS.md) | Reproduction results and source gaps |
| [PROVENANCE.json](PROVENANCE.json) / [VALIDATION.json](VALIDATION.json) | Machine-readable source origins and verification evidence |
| [scripts/](scripts/) | Current SCO assembly and archive manifest |
| [native/](native/) | Native modules and DualSense patches |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Changes and useful verification |
| [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) | Component notices and licensing scope |
| [CREDITS.md](CREDITS.md) | Unmodified AVCNGCE release credits |

The build tools create separate outputs. They do not install the mod or run
the game. Read the source status before treating this folder as a rebuild of
the entire release.

Run `python3 tools/verify_source.py` to check this source snapshot's file set
and checksums before editing it.
