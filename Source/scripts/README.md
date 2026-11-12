# Current VCANGCE game scripts

These 96 editable `.scasm` files are the current checkpoint254 gameplay
sources used by `VCANGCE.Zip`. They preserve the installed SCO bytecode,
including mission, parachute, Pole Position dancer, lap dance and pizza changes.
They are assembly source, not the original high level source of the port.
Earlier recovered C variants cannot rebuild the current missions wholesale.

The checkpoint254 `script.img` is a mixed resource container: 96 current
gameplay SCOs, 21 unchanged network SCOs and 4,179 other resources, 4,296
members in total. A new build **requires the matching release's script.img**
as its read-only resource baseline. The builder replaces only the 96 named
scripts and preserves all 4,200 other complete allocations, directory types,
flags and member order. Building the old scripts-only layout would discard
assets required by the consolidated build; both supplied builders refuse
that operation for this snapshot.

`members.txt` preserves the canonical script order, IMG3 resource header and
each canonical member's resource type. `manifest.json` records source hashes,
the full mixed release image and every member's allocation hash, plus the
96-script canonical projection used for focused VM audits. Non-SCO directory
fields can encode resource flags; they are not treated as ordinary byte
lengths. All 96 sources are byte-for-byte copies of the maintained current
sources. No gameplay code was changed for this export.

Use Python 3.11 or newer, `openssl` in PATH, and `fourfury==0.13.3`. From the
root of this source export:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python tools/build_scripts.py --help
python tools/build_scripts.py --exe "/path/to/GTAIV.exe" --resources-from "/path/to/release/update/common/data/cdimages/script.img" --out "../VCANGCE-build/scripts" --compare "/path/to/release/update/common/data/cdimages/script.img"
```

The resource baseline must have checkpoint254 (unchanged since 252) SHA256
`ddb8dc6f3d1944f92fd92d96a36839464a6885ce26e003e8a6092ffd74517d3a`.
On Windows, activate `.venv\Scripts\activate` and ensure `openssl.exe` is
available in PATH. The assembler reads the encryption key from your own game
executable; no executable, game asset or key is shipped in this source export.

The build writes the complete mixed `script.img` and `build-report.json` to
the requested external directory. It rejects output inside this export or
the game directory and does not install anything. `--compare` is optional;
it requires the complete image, all 4,296 members, order, types, flags,
offsets and allocated padding to match. A mismatch exits with status 1 and
leaves the new output for review. An unchanged fresh rebuild is byte-identical
to the packaged 859,693,056-byte mixed IMG.

For development, edit a `.scasm` file and rebuild without `--compare`, while
still supplying the matching release baseline with `--resources-from`.
All noncanonical allocations remain exact; their sector offsets can move if
an edited SCO grows or shrinks. Jumps, calls and switches use labels so the
assembler can relocate them. Preserve explicit short opcodes, float bit
patterns, string padding and the verbatim `.data` section when byte identity
matters. Native names come from the bundled SC-CL `natives32.h`; unknown or
ambiguous functions retain numeric IDs. The header retains its complete
third party license notice.

The existing parachute uses `vc8_parachute`, `parachutelauncher`,
`parachute_player` and `parachute_ai`. Full-size aircraft equip a wearable
pack in any seat while ordinary gun use remains available; `rcbaron` and
`tourmav` (this build's RC helicopter) are excluded. Mission/player guards
and temporary weapon blocking during actual parachute flight remain. The
adapter removes a leftover weapon-41 token; the launcher/player do not grant
it. The four parachute scripts use full-width `Push` for 443 positive global
loads fixed at checkpoint224, preserving seven intentional negative short
constants. `vc_euphoria` protects all 30 installed parachute state names.

Pole Position's `ambstreams` now owns the added dancers, floor dances, lap
dance sessions, billing, statistics and asset progression. It reuses the two
floor dancers rather than spawning a second private-session dancer. The
previous `STRIP` private-session implementation was removed. The two bunny
accessory models depend on corrected private skeleton graphs in the release
assets; rebuilding scripts does not regenerate those assets.

Three CE constraints matter when editing these scripts:

- Script startup requires an exact supported stack class. The launcher uses
  1,024 words; a 2,048-word request is unsupported in CE 1.2.0.59.
- Opcode zero is named `Nop` by this lossless assembler, but the CE VM treats
  it as invalid when executed. Existing unreachable padding remains exact;
  do not add reachable `Nop` instructions.
- `PushS` sign-extends its 16-bit operand, range -32768 to 32767. Use full-width
  `Push` for positive globals above 32767. `PushS 60000` executes as -5536 and
  can address memory before global storage. The assembler rejects overflow.
  Preserve real negative constants because changing them changes logic.

`tools/check_script_contracts.py` checks signed operand boundaries and direct
adjacent integer-immediate global indices in these 96 canonical scripts.
It accepts the full mixed IMG and deliberately excludes network scripts and
non-SCO resources. It reports computed indices separately; it does not prove
control flow, native behavior or gameplay. `tools/scoasm/project_scripts.py`
can write a read-only canonical projection for other focused script tools.
The projection must never replace the complete runtime IMG.

The fresh build and global-range checks are file and bytecode evidence.
Earlier owner gameplay reports confirmed windshield recovery, wearable pack
with guns, deployment/landing, club dancers and lap dances through228.
Checkpoint 252's `pizza` script throws the box from the scooter with the
native `throw_grenade` clip, applies native velocity toward the customer and
counts only a box that settles near them; `range_bootstrap` no longer
relaunches the inert `vc_cutmem` script. The current build retains
consolidation and excludes the save-breaking241 radio change. The owner confirmed existing-save loading, the correct hotel
shirt pickup and all Pole Position dancers after the245 model-order correction.
Source publication launches no game and does not certify every mission,
language, platform or long-term cold-start path. Test edits in the game
before distributing them.
