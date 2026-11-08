#!/usr/bin/env python3
"""Let GTA_IV_DualSense.asi run its HD-haptic audio engine over Bluetooth.

Input: step 165's build (sha 65bc890f...). The mod has three Bluetooth gates (BT flag 0x10061867):
  0x10013c74 jne  engine start gate: on BT the XAudio2 engine is never started (shut down instead)
  0x10013d22 jne  "HD haptics active" flag 0x1005f3c2 (gates every haptic sound/voice)
  0x10019499 jne  per-frame HD mode (valid flag0 0x1c, no motor rumble) needs connected && engine running
Each 2-byte jne -> nop nop. Everything else unchanged: if no DualSense audio endpoint exists, the
engine does not start (0x10010780 finds no "dualsense"/"wireless controller" render endpoint), so
the mod keeps using motor rumble as before.
"""
import hashlib, sys
from pathlib import Path
src, dst = Path(sys.argv[1]), Path(sys.argv[2])
data = bytearray(src.read_bytes())
assert hashlib.sha256(data).hexdigest() == "65bc890f6ebf1ea3a6baddcf7f8f2bb9b781ae0a713a4636d7478c93b2c7c049", "input is not step 165's build"
for off, old in ((0x13074, b"\x75\x73"), (0x13122, b"\x75\x04"), (0x18899, b"\x75\x0d")):
    assert data[off:off+2] == old, hex(off)
    data[off:off+2] = b"\x90\x90"
tmp = dst.with_suffix(".tmp"); tmp.write_bytes(data); tmp.replace(dst)
print(dst, hashlib.sha256(data).hexdigest())
