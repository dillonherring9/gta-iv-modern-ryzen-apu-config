# Current AVCNGCE game scripts

These 93 editable `.scasm` files rebuild the current AVCNGCE `script.img`.
They preserve the installed SCO bytecode, including later mission fixes. They
are assembly source, not the original high level source of the port. Earlier
recovered C variants are not included because they cannot rebuild the current
missions wholesale.

`members.txt` preserves archive member order, the IMG3 resource header and each
member's resource type. `manifest.json` records source, SCO and allocated-byte
hashes against the packaged release. The exported `members.txt` corrects two
stale resource types from the old authoring manifest: `mob_phone.sco` and
`bribestar_wanted.sco` are type `1`, as in the current installed/release image.
No `.scasm` bytes were changed for this export.

Use Python 3.11 or newer, `openssl` in PATH, and `fourfury==0.13.3`. From the
root of this source export, create an isolated environment and install:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python tools/build_scripts.py --help
python tools/build_scripts.py --exe "/path/to/GTAIV.exe" --out "../AVCNGCE-build/scripts" --compare "/path/to/update/common/data/cdimages/script.img"
```

On Windows, activate `.venv\Scripts\activate` and ensure `openssl.exe` is
available in PATH. The assembler reads the encryption key from your own game
executable; no executable or key is shipped in this source export.

The build writes `script.img` and `build-report.json` into the requested external
directory. It rejects output inside this export or the game directory and does
not install anything. `--compare` is optional; it requires the full image,
all 93 SCO members, their order, resource types, offsets and allocated padding
to match. A mismatch exits with status 1 and leaves the new output for review.

For development, edit a `.scasm` file and rebuild without `--compare`. Jumps,
calls and switch destinations use labels so the assembler can relocate them.
Preserve explicit short opcodes, float bit patterns, string padding and the
verbatim `.data` section when byte identity matters. Native function names
come from the bundled SC-CL `natives32.h`; unknown or ambiguous functions retain
their numeric IDs. The complete third party license notice remains in that header.

The snapshot reproduces the release image exactly; this is a source and file
identity check. Test any edited missions in the game before distributing them.
