# Contributing

Describe the observed problem, expected behavior and a repeatable way to reach
it. Include GTA IV CE version, platform and relevant configuration. Remove
personal information and credentials from logs before sharing them.

Build in a separate directory using [BUILDING.md](BUILDING.md). For script
changes, preserve explicit opcodes, native IDs, string bytes and the `.data`
section unless the change requires otherwise. Keep member names and archive
order stable. The current archive also carries 4,200 preserved network-script
and resource allocations: use the hash-guarded release baseline and retain them
when rebuilding the 96 covered gameplay scripts. Do not replace a mixed release
archive with a script-only projection.

CE has specific supported stack classes, and opcode zero is not an executable
no-op. Check actual native signatures and VM behavior when adapting episode
scripts. `PushS` is signed 16-bit: keep intentional negative values signed and
use full-width `Push` for positive global addresses above 32767. Never widen
the assembler's range just to accept an invalid operand. Check global addresses
as executed, because an unsigned spelling can underflow the global table and
corrupt unrelated player state.

For club changes, check crowd cleanup and panic behavior, repeated entry, player
control and camera restoration, payments and Pole Position asset completion.
Model loading and in-game appearance require the matching release assets;
source assembly alone does not test those paths.

Preserve the shared model-number order when changing definitions. Peds and
objects use one list across the loaded IDE files: adding peds ahead of objects
can change models stored in existing saves. Append new definitions after all
existing models; do not remove or reorder older definitions. Run
`python3 -I tools/check_model_order.py` against the source snapshot and use
`--root` to check an extracted release, including its radio-data identity.
Model-list positions are not a universal save-record offset. See
[config/README.md](config/README.md) for the baseline and checker scope.

Keep the current `GAME.DAT16` station list and `stationslimit.txt` together.
Changing the station list broke existing saves in two development attempts,
including the reverted checkpoint-241 retry. A parser or fresh-game test does
not verify existing-save compatibility. Checkpoint 245 was tested with an
existing save for hotel-pickup appearance and all Pole Position dancers.

For native startup changes, preserve the VCCore initialization/hook order and
legacy-module guard. Avoid profile-API and CRT file-lock dependencies inside DLL
initialization; retain the shared startup helpers and deferred paths unless a
replacement has been validated. Keep module-specific configuration contracts.
Report what was actually checked: exact file comparison, an isolated fixture,
a live process capture or observed gameplay. A build alone does not establish
runtime behavior.

Submit source changes and relevant documentation together. Keep game assets,
vendor binaries, compiled outputs, saves and personal settings out of source
commits. Preserve component notices and credit upstream work. If a source or
credit is missing, provide its original link or readme so the mapping can be
corrected. This export does not introduce a blanket project license.
