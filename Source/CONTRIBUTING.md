# Contributing

Describe the observed problem, expected behavior and a repeatable way to reach
it. Include GTA IV CE version, platform and relevant configuration. Remove
personal information and credentials from logs before sharing them.

Build in a separate directory using [BUILDING.md](BUILDING.md). For script
changes, preserve explicit opcodes, native IDs, string bytes and the `.data`
section unless the change requires otherwise. Keep member names and archive
order stable. CE has specific supported stack classes, and opcode zero is not
an executable no-op. Check the actual native signatures and VM behavior when
adapting episode scripts.

`PushS` is signed 16-bit in CE. Keep intentional negative values signed and
use full-width `Push` for positive global addresses above 32767; never widen
the assembler's range just to accept them. Check global-address operands as
executed, because an apparently positive unsigned spelling can underflow the
global table and corrupt unrelated player state. The maintained parachute
correction includes these checks and preserves seven real negative constants.

For native startup changes, avoid profile-API and CRT file-lock dependencies
inside DLL initialization; preserve the shared startup helpers and deferred
initialization paths unless their replacement has been validated. Report what
was actually checked: exact file comparison, an isolated fixture, a live
process capture or observed gameplay. A build alone does not establish runtime
behavior.

Submit source changes and relevant documentation together. Keep game assets,
vendor binaries, compiled outputs, saves and personal settings out of source
commits. Preserve component notices and credit upstream work. If a source or
credit is missing, provide its original link or readme so the mapping can be
corrected. This export does not introduce a blanket project license.
