"""GTA_IV_DualSense.asi (Nexus 1136 v1.4, sha256 ba15d631…) -> step 165 build. Data/immediate changes only:
1. Bluetooth markers (mod 0x10019DF9/0x10019E0C/0x10019E1F: three `push <str>; find in lowercased HID path`):
   ':'          Proton / Wine (Bazzite, SteamOS, any Linux): BT paths carry the pad's MAC 'xx:xx:..'; USB paths, '\\?\' and GUIDs have no ':'
   '00001124'   Windows: {00001124-0000-1000-8000-00805f9b34fb} = Bluetooth HID service in the BT HID path
   '&0002054c'  Windows: BT vendor-source form VID&0002054C (USB is VID_054C)
   New strings live in .rdata file slack (VirtualSize raised to SizeOfRawData); the pushes keep their base relocations.
2. Weapon table ids 8,24,26,27,28,29,30 -> 32,33,34,35,21,90,91 (Vice City, step 164).
Both PS5 pads already match (VID 054c, PID 0ce6 DualSense / 0df2 DualSense Edge; &col02/&col03 skipped)."""
import sys, struct, hashlib, pefile
from pathlib import Path
src, dst = Path(sys.argv[1]), Path(sys.argv[2])
raw = bytearray(src.read_bytes()); orig = bytes(raw)
assert hashlib.sha256(raw).hexdigest() == 'ba15d63140a05359b762abe1bdde09bda7f6204bec33b2614e19d2602e72a8b4'
pe = pefile.PE(data=bytes(raw)); base = pe.OPTIONAL_HEADER.ImageBase
off = lambda va: pe.get_offset_from_rva(va - base)
rd = [s for s in pe.sections if s.Name.startswith(b'.rdata')][0]
assert rd.Misc_VirtualSize == 0x1149A and rd.SizeOfRawData == 0x11600
cave_rva = (rd.VirtualAddress + rd.Misc_VirtualSize + 15) & ~15
markers = [b':', b'00001124', b'&0002054c']
pushes = {0x10019DF9: 0x10056288, 0x10019E0C: 0x10056290, 0x10019E1F: 0x10056298}   # push site -> old string
relocs = {e.rva for b_ in pe.DIRECTORY_ENTRY_BASERELOC for e in b_.entries if e.type == 3}
p = cave_rva
for (site, old), m in zip(pushes.items(), markers):
    o = off(site); assert raw[o] == 0x68 and struct.unpack_from('<I', raw, o + 1)[0] == old
    assert (site + 1 - base) in relocs, hex(site)
    co = pe.get_offset_from_rva(p); assert all(x == 0 for x in raw[co:co + len(m) + 1])
    raw[co:co + len(m) + 1] = m + b'\0'; struct.pack_into('<I', raw, o + 1, base + p); p += (len(m) + 1 + 3) & ~3
assert p <= rd.VirtualAddress + rd.SizeOfRawData
# section header VirtualSize
hdr = rd.get_file_offset(); struct.pack_into('<I', raw, hdr + 8, rd.SizeOfRawData)
t = off(0x10057C38); ids = [struct.unpack_from('<I', raw, t + 12 * i)[0] for i in range(19)]
assert ids == [7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 24, 26, 27, 28, 29, 30, 31]
for i, x in enumerate(ids):
    r = {8: 32, 24: 33, 26: 34, 27: 35, 28: 21, 29: 90, 30: 91}.get(x)
    if r: struct.pack_into('<I', raw, t + 12 * i, r)
# 3. Caps-based Bluetooth flag (all OSes): at 0x1001A0C1 the mod stores HIDP_CAPS.OutputReportByteLength (66 A3 F0 1C 06 10 =
#    mov [0x10061CF0],ax) right after HidP_GetCaps succeeded. A stub in .text slack does that store, then sets the mod's BT flag
#    byte 0x10061867 = (OutputReportByteLength >= 78): BT DualSense = 547 (VERIFIED on the user's pad under Proton), USB = 48.
#    Path markers above stay as the fallback when caps are unavailable.
tx = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
assert tx.Misc_VirtualSize == 0x4974C and tx.SizeOfRawData == 0x49800
stub_rva = (tx.VirtualAddress + tx.Misc_VirtualSize + 15) & ~15; S = base + stub_rva
hook = 0x1001A0C1; ho = off(hook); assert bytes(raw[ho:ho + 6]) == bytes.fromhex('66A3F01C0610')
assert (hook + 2 - base) in relocs
stub = bytearray(bytes.fromhex('66A3F01C0610') + bytes.fromhex('663D4E00') + bytes.fromhex('0F930567180610') + b'\xE9\0\0\0\0')
struct.pack_into('<i', stub, len(stub) - 4, (hook + 6) - (S + len(stub)))
so = pe.get_offset_from_rva(stub_rva); assert all(x == 0 for x in raw[so:so + len(stub)])
raw[so:so + len(stub)] = stub
raw[ho:ho + 6] = b'\xE9' + struct.pack('<i', S - (hook + 5)) + b'\x90'
struct.pack_into('<I', raw, tx.get_file_offset() + 8, tx.SizeOfRawData)
# relocations: neutralise the old entry at hook+2 (now inside our jmp), add entries for the stub's two absolute addresses
rl = [s for s in pe.sections if s.Name.startswith(b'.reloc')][0]; dd = pe.OPTIONAL_HEADER.DATA_DIRECTORY[5]
killed = 0
for blk in pe.DIRECTORY_ENTRY_BASERELOC:
    for e in blk.entries:
        if e.type == 3 and e.rva == hook + 2 - base:
            eo = e.struct.get_file_offset()
            w = struct.unpack_from('<H', raw, eo)[0]; assert w >> 12 == 3; struct.pack_into('<H', raw, eo, w & 0x0FFF); killed += 1
assert killed == 1
page = stub_rva & ~0xFFF; ents = [(3 << 12) | ((stub_rva + 2) - page), (3 << 12) | ((stub_rva + 13) - page)]
block = struct.pack('<II', page, 8 + 2 * len(ents) + (2 if len(ents) % 2 else 0)) + b''.join(struct.pack('<H', x) for x in ents) + (b'\0\0' if len(ents) % 2 else b'')
bo = pe.get_offset_from_rva(dd.VirtualAddress + dd.Size); assert dd.VirtualAddress + dd.Size + len(block) <= rl.VirtualAddress + rl.SizeOfRawData
assert all(x == 0 for x in raw[bo:bo + len(block)])
raw[bo:bo + len(block)] = block
struct.pack_into('<I', raw, dd.get_file_offset() + 4, dd.Size + len(block))
struct.pack_into('<I', raw, rl.get_file_offset() + 8, max(rl.Misc_VirtualSize, dd.Size + len(block)))
pe2 = pefile.PE(data=bytes(raw)); pe2.OPTIONAL_HEADER.CheckSum = pe2.generate_checksum(); raw = pe2.write()
dst.write_bytes(raw)
print('changed bytes', sum(a != b for a, b in zip(raw, orig)), hashlib.sha256(raw).hexdigest())
