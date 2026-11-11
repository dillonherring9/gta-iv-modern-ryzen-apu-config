# VCANGCE source

Developer sources for **Vice City Actual Next Gen CE** (**VCANGCE**), targeting
GTA IV Complete Edition **1.2.0.59** and aligned with release checkpoint **245**.
Players can find the playable package through the repository's main README
and release downloads. The packaged release's note about an earlier source
snapshot describes its packaging date; this later source publication aligns
the covered code and included text configuration with checkpoint 245.

This folder contains all **96 current game-script assembly sources**, the five
maintained native module sources and the dispatcher that combines four of them
into **VCCore.asi**, the two project DualSense binary patches, portable build
tools and the Opus source subset used by the controller loader. Default native
builds produce **VCCore.asi** and **ScriptHook.dll**. Game executables, compiled
plugins and binary game assets are supplied separately. Some scripts were
reconstructed from current bytecode; the export does not contain every original
author's high-level source.

The [configuration snapshot](config/README.md) includes the current text loaders,
model definitions and station-limit setting, with a read-only model-order
checker. Checkpoint 245 keeps the original 5,740 model definitions in order and
appends the eight new dancer peds after every existing model. Pedestrians and
objects share this numbering. The attempted checkpoint-241 radio-list change
was undone after it broke existing saves; the current station limit remains 10.

The scripts include the native parachute controllers, automatic wearable-aircraft
adapter, movement compatibility changes and the expanded Pole Position routines.
The club uses twelve pole/table dancer positions and two floor dancers offering
GTA IV-style lap dances, while retaining Vice City's payment and business
progression. The native dispatcher preserves the existing audio-heap, memory,
music and controller functionality and their configuration filenames. Earlier
signed-global, ragdoll-recovery and startup-lock repairs remain included.

| Start here | Purpose |
|---|---|
| [BUILDING.md](BUILDING.md) | Dependencies and commands |
| [SOURCE-STATUS.md](SOURCE-STATUS.md) | Reproduction results and source gaps |
| [PROVENANCE.json](PROVENANCE.json) / [VALIDATION.json](VALIDATION.json) | Source origins and verification evidence |
| [scripts/](scripts/) | Current SCO assembly and mixed-archive manifest |
| [native/](native/) | Native modules, VCCore dispatcher and DualSense patches |
| [config/](config/) | Current text configuration, original model-order baseline and save-safety checks |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Changes and useful verification |
| [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) | Component notices and licensing scope |
| [CREDITS.md](CREDITS.md) | Checkpoint-245 release credits |

The current `script.img` also holds preserved network scripts and game resources.
To rebuild it, supply the checkpoint-245 archive with `--resources-from`; the
builder replaces the 96 covered scripts and preserves all other allocations.
See [BUILDING.md](BUILDING.md) for the complete command.

The build tools create separate outputs and do not install the mod or run the
game. This curated export rebuilds the covered code. It does not assemble the
full playable release or regenerate its binary radio, language, map, model,
texture, collision and audio integrations. The included text configuration is
an exact authoring snapshot, rather than a complete asset build pipeline.
Read the source status for vendor-source gaps and current gameplay evidence.

Run `python3 tools/verify_source.py` to check the published source checksums
before editing it.
