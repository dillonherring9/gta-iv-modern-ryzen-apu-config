# Third party notices

This export preserves component notices and introduces no blanket license.
Existing notices apply to their respective material; files without an explicit
license receive no new license here. Publication does not transfer ownership
of the original games, conversion, native API references or community work.
See the unchanged release [CREDITS.md](CREDITS.md) for project attribution.

| Material | Retained notice or provenance |
|---|---|
| Opus 1.6 source subset | [COPYING](dependencies/opus-1.6/COPYING), [patent/licensing notes](dependencies/opus-1.6/LICENSE_PLEASE_READ.txt) and original file notices; BSD terms |
| Controller protocol implementation | Cites GeorgLegato's LinuxAudio4Dualsense5 0.6 (AGPL-3.0); preserved [AGPL text](native/vcsh_loader/AGPL-3.0-reference.txt) |
| SC-CL native declarations | Complete notice retained in [natives32.h](tools/scoasm/natives32.h), including its free-of-charge redistribution condition; credits Rocko Tompkins and Nathan James |
| Generated ScriptHook native table | SC-CL / IV-SDK declaration provenance retained; no new ownership or license claimed over upstream names |
| FourFury 0.13.3 | External MIT-licensed build dependency; installed separately |
| DualSense patches | Project patch scripts only; original NodixTech vendor ASI and full source are not included |

Zig, Python, OpenSSL and optional pefile are separate development dependencies
under their own terms. BASS is loaded by the music module at runtime and is
not included in this source folder. Other release component licenses remain
with their respective distributions.
