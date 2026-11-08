# Contributing

Describe the observed problem, the expected behavior and a repeatable way to
reach it. Include the game version, platform, relevant configuration and logs
with personal information removed.

Build in a separate directory using [BUILDING.md](BUILDING.md). For script
changes, preserve explicit opcodes, native IDs, string bytes and the `.data`
section unless the change requires otherwise. Keep member names and archive
order stable. Rebuild the affected code and report what was actually checked:
file comparison, an isolated fixture or observed gameplay.

Submit source changes and relevant documentation together. Keep game assets,
vendor binaries, compiled outputs, saves and personal settings out of source
commits. Preserve component notices and credit upstream work. If a source or
credit is missing, provide its original link or readme so the mapping can be
corrected. This export does not introduce a blanket project license.
