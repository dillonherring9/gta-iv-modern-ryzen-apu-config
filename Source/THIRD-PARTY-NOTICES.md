# Third party notices

This export preserves component notices and introduces no blanket license.
Existing notices apply to their respective material; files without an explicit
license receive no new license here. Publication does not transfer ownership
of the original games, conversion, native API references or community work.
See the checkpoint-254 release [CREDITS.md](CREDITS.md) for attribution.

| Material | Retained notice or provenance |
|---|---|
| Opus 1.6 source subset | [COPYING](dependencies/opus-1.6/COPYING), [patent/licensing notes](dependencies/opus-1.6/LICENSE_PLEASE_READ.txt) and original file notices; BSD terms |
| Controller protocol implementation | Cites GeorgLegato's LinuxAudio4Dualsense5 0.6 (AGPL-3.0); preserved [AGPL text](native/vcsh_loader/AGPL-3.0-reference.txt) |
| SC-CL native declarations | Complete notice retained in [natives32.h](tools/scoasm/natives32.h), including its free-of-charge redistribution condition; credits Rocko Tompkins and Nathan James |
| Generated ScriptHook native table | SC-CL / IV-SDK declaration provenance retained; no new ownership or license claimed over upstream names |
| Current SCO assembly | Maintained/reconstructed bytecode source, including the TBoGT parachute integration supplied through upstream 8.0; original game and community authors retain their rights |
| Movement compatibility adaptation | Credits LotsofEds / ServalEd's MoveImprove reference; the native predicate adaptation is in the current movement assembly, not a bundled full upstream .NET project |
| VCCore dispatcher | Project consolidation code; linked modules retain their existing notices and provenance |
| Loading-screen handoff (`native/vcloading`) | C adaptation of FusionFix `loadingdelays.ixx` commit ed3ace47581c by ThirteenAG and Fusion contributors; GPL-3.0-or-later, with the [upstream license](native/vcloading/upstream/LICENSE) and both upstream source versions retained |
| Mjolnir patch recipe | Project patch script only; llamacomma's MIT-licensed Mjolnir IV binary is not included |
| FourFury 0.13.3 | External MIT-licensed build dependency; installed separately |
| DualSense patches | Project patch scripts only; original NodixTech vendor ASI and full source are not included |
| Text configuration snapshot | Retained game/conversion loader and IDE text with project integration changes; original game and community authors retain their rights; see [config/README.md](config/README.md) and release credits |

Zig, Python, OpenSSL and optional pefile are separate development dependencies
under their own terms. BASS is loaded by the music component at runtime and is
not included in this source folder. Release models, textures, collision, audio,
translations and other game resources, including the current parachute, blue
Soiree and dancer assets, are also outside the source export. Selected text
configuration is included; the binary resources it references remain separate.
The script builder
reads preserved resources from a user-supplied release archive; it does not
embed them here. Other release component licenses remain with their respective
distributions.
