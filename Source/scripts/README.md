# Current VCANGCE game scripts

These 96 editable `.scasm` files rebuild the checkpoint-224 VCANGCE
`script.img`. They preserve the installed SCO bytecode, including later
mission fixes. They are assembly source, not the original high level source
of the port. Earlier recovered C variants are not included because they
cannot rebuild the current missions wholesale.

`members.txt` preserves archive member order, the IMG3 resource header and
each member's resource type directly from the packaged image. `manifest.json`
records source, SCO and allocated-byte hashes, offsets and padding lengths
against that release. All 96 `.scasm` files match the maintained current
sources byte-for-byte; no script source was edited for this export.

The current parachute uses `vc8_parachute`, `parachutelauncher`,
`parachute_player` and `parachute_ai`; the previous `vc_native_chute` is no
longer part of the release. Entering a full-size plane or helicopter grants
the wearable pack in any seat, without selecting a parachute weapon or
preventing normal gun use while wearing it. The model exclusions are
`rcbaron` and `tourmav` (the latter is this build's RC helicopter). Existing
mission/player guards remain, as does temporary weapon blocking during
actual parachute flight. The adapter removes a leftover weapon-41 token;
the launcher/player do not grant it.

The four parachute scripts use full-width `Push` for 443 positive global
addresses previously encoded with signed `PushS`. Seven intentional negative
constants retain their exact operand bytes. `vc_euphoria` protects all 30
installed parachute states, including `land`, `land_safe`, `land_fast`,
`chute_off` and the actual `free_fall_deccelerate` name. These five scripts
contain the checkpoint-224 changes; the other 91 script allocations remain
byte-identical to checkpoint 221.

Use Python 3.11 or newer, `openssl` in PATH, and `fourfury==0.13.3`. From the
root of this source export, create an isolated environment and install:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python tools/build_scripts.py --help
python tools/build_scripts.py --exe "/path/to/GTAIV.exe" --out "../VCANGCE-build/scripts" --compare "/path/to/update/common/data/cdimages/script.img"
```

On Windows, activate `.venv\Scripts\activate` and ensure `openssl.exe` is
available in PATH. The assembler reads the encryption key from your own game
executable; no executable or key is shipped in this source export.

The build writes `script.img` and `build-report.json` into the requested
external directory. It rejects output inside this export or the game
directory and does not install anything. `--compare` is optional; it requires
the full image, all 96 SCO members, their order, resource types, offsets and
allocated padding to match. A mismatch exits with status 1 and leaves the
new output for review.

For development, edit a `.scasm` file and rebuild without `--compare`. Jumps,
calls and switch destinations use labels so the assembler can relocate
them. Preserve explicit short opcodes, float bit patterns, string padding
and the verbatim `.data` section when byte identity matters. Native function
names come from the bundled SC-CL `natives32.h`; unknown or ambiguous
functions retain their numeric IDs. The complete third party license
notice remains in that header.

Three CE-specific constraints matter when modifying the parachute scripts:

- Script startup requires an exact supported stack class. The launcher uses
  1,024 words; a 2,048-word request is unsupported in CE 1.2.0.59 and caused
  the diagnosed save-load crash. A larger numeric request does not select
  the next available class.
- Opcode zero is labeled `Nop` by this lossless assembler, but the CE VM
  treats it as an invalid instruction when executed. Two reachable imported
  zero instructions were removed at checkpoint 221. Existing unreachable
  padding remains byte-exact; do not add reachable `Nop` instructions.
- `PushS` sign-extends its 16-bit operand, with range -32768 to 32767. Use
  full-width `Push` for positive global addresses above 32767. The old bits
  for `PushS 60000` execute as -5536; `GetGlobalPs` then addresses memory
  before global storage. This corrupted Tommy's animation buffers and caused
  Euphoria recovery crashes even in car-only sessions. The assembler rejects
  overflow instead of silently accepting an unsigned operand. Preserve real
  negative constants; replacing every negative immediate would change logic.

The snapshot reproduces the release image exactly; this is a source and
file identity check. After checkpoint 224 the user confirmed windshield-crash
recovery, automatic wearable pack use with a gun, deployment and landing.
Earlier reports also confirmed save loading and steering. Exporting these
sources performs no new gameplay test. Test edited missions in the game
before distributing them.
