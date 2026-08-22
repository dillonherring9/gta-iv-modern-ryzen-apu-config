# Contribution summary

Explain the problem first, then state the smallest change made. Link the issue or authoritative upstream source where applicable.

## Target and scope

- [ ] This targets Windows GTA IV Complete Edition with Gillian’s pre-existing Drag-and-Drop Archive.
- [ ] I identified whether this is an installer/configuration issue, an archive/setup issue, a third-party mod conflict, a renderer/driver issue, or documentation-only work.
- [ ] I did not add GTA IV files, FusionFix/DXVK binaries, shaders, or other third-party content without clear rights and explicit project-scope approval.
- [ ] I did not reintroduce AudioMap or make a static `preload.list` automatic without new source evidence and a tested shader-layout contract.

## Evidence and testing

Describe the test machine, game/archive version, GPU/driver, resolution, changes made, and exact route/scene used. State what was tested and what was not tested.

- [ ] I saved the prior file/configuration and tested one meaningful variable at a time.
- [ ] I restarted GTA IV where appropriate and compared a repeat cache-warmed route rather than relying only on a cold first run.
- [ ] I tested main GTA IV, TLAD, and TBoGT where the change can affect shared files.
- [ ] I verified a rollback path.
- [ ] I did not turn an untested observation into a universal performance promise.

## Contract and documentation

- [ ] I checked [`docs/COMPLETE_INSTALL_CONTRACT.md`](docs/COMPLETE_INSTALL_CONTRACT.md) and did not change the seven-file installer contract without fully documenting the reason.
- [ ] I updated user documentation, sources/credits, dependency context, history, and embedded `installer/payload/reference/` copies where needed.
- [ ] I regenerated `installer/INSTALLER_PAYLOAD_SHA256SUMS.txt` after payload changes.
- [ ] I kept the installer backup/rollback behavior and GTAIV.exe administrator compatibility behavior intact, or documented a tested replacement.

## Release verification

- [ ] `git diff --check` passed.
- [ ] The payload SHA-256 manifest passed.
- [ ] The Version 3 installer compiled successfully.
- [ ] If this is released, the existing `v3` assets will be replaced in place—not published as a patch-number release or a partial installer.

## Rollback instructions

Write the exact safe reversal procedure for this change. If none is needed, explain why.
