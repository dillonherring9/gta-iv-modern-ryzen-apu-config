#!/usr/bin/env python3
"""Keep Mjolnir's CPU fixes and leave GTA IV's native quality safeguard active.

Offline, exact-input patch. Never edits a running process or installs its output.
Skip the entire unconditional Fix 7 block, including its misleading log messages.
Neutralize only the HIGHLOW relocation on the replaced PUSH immediate: relative
JMP displacements must not receive the DLL load-base delta.
"""
from pathlib import Path
import argparse, hashlib, json, struct

INPUT_SHA256 = 'c909dfab44e385aa00257ef969525204b451298f595ce0b0a10d8c38edab50d7'
PATCH_RVA = 0x12db
TARGET_RVA = 0x1378
RELOC_RVA = PATCH_RVA + 1
EXPECTED = bytes.fromhex('68 24 33 00 10')
REPLACEMENT = b'\xe9' + struct.pack('<i', TARGET_RVA - PATCH_RVA - 5)

def digest(b): return hashlib.sha256(b).hexdigest()

def patch(raw):
    if digest(raw) != INPUT_SHA256:
        raise ValueError('unsupported Mjolnir input SHA256')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[:2] != b'MZ' or raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('invalid PE')
    machine, sections = struct.unpack_from('<HH', raw, pe+4)
    opt_size = struct.unpack_from('<H', raw, pe+20)[0]
    opt = pe+24
    if machine != 0x14c or struct.unpack_from('<H', raw, opt)[0] != 0x10b:
        raise ValueError('not PE32/i386')
    if struct.unpack_from('<I', raw, opt+28)[0] != 0x10000000:
        raise ValueError('unexpected preferred image base')
    section_rows=[]
    for i in range(sections):
        off=opt+opt_size+i*40
        virtual_size,rva,size,ptr=struct.unpack_from('<IIII',raw,off+8)
        section_rows.append((rva,virtual_size,size,ptr))
    def file_offset(rva):
        for start,virtual_size,size,ptr in section_rows:
            if start <= rva < start+size:
                return ptr+rva-start
        raise ValueError(f'RVA {rva:x} is outside section raw data')
    at=file_offset(PATCH_RVA)
    if raw[at:at+5] != EXPECTED:
        raise ValueError('unexpected Fix 7 entry instruction')
    # Verify surrounding CPU fix exit and common diagnostic continuation.
    if raw[file_offset(0x12ce):file_offset(0x12db)] != bytes.fromhex('68 e4 32 00 10 e8 48 01 00 00 83 c4 04'):
        raise ValueError('unexpected CPU-fix predecessor')
    if raw[file_offset(TARGET_RVA):file_offset(TARGET_RVA)+13] != bytes.fromhex('68 e8 3c 00 10 e8 9e 00 00 00 83 c4 04'):
        raise ValueError('unexpected diagnostic continuation')
    reloc_start,reloc_size=struct.unpack_from('<II',raw,opt+96+5*8)
    cursor=file_offset(reloc_start);end=cursor+reloc_size
    matching=[];blocks=[]
    while cursor < end:
        page,size=struct.unpack_from('<II',raw,cursor)
        if size < 8 or size % 2 or cursor+size > end:
            raise ValueError('invalid relocation block')
        blocks.append((page,size))
        for off in range(cursor+8,cursor+size,2):
            value=struct.unpack_from('<H',raw,off)[0]
            target=page+(value & 0xfff)
            if target==RELOC_RVA:
                matching.append((off,value))
        cursor+=size
    if len(matching)!=1 or matching[0][1] >> 12 != 3:
        raise ValueError('expected exactly one HIGHLOW on replaced immediate')
    reloc_offset,reloc_value=matching[0]
    output=bytearray(raw)
    output[at:at+5]=REPLACEMENT
    struct.pack_into('<H',output,reloc_offset,reloc_value & 0xfff)
    changes=[i for i,(a,b) in enumerate(zip(raw,output)) if a!=b]
    allowed=set(range(at,at+5)) | {reloc_offset,reloc_offset+1}
    if not changes or set(changes)-allowed or len(output)!=len(raw):
        raise ValueError('unexpected mutation scope')
    info=dict(input_sha256=INPUT_SHA256,output_sha256=digest(output),bytes=len(raw),
              entry_rva=PATCH_RVA,target_rva=TARGET_RVA,patch_file_offset=at,
              original=EXPECTED.hex(),replacement=REPLACEMENT.hex(),
              relocation_rva=RELOC_RVA,relocation_file_offset=reloc_offset,
              original_relocation=reloc_value,replacement_relocation=reloc_value & 0xfff,
              changed_file_offsets=changes,relocation_blocks=blocks,
              explanation='Skip only quality-adjuster disabling block; preserve native flag, CPU patches, and diagnostic worker.')
    return bytes(output),info

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('input',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--evidence',type=Path)
    args=p.parse_args()
    if args.input.resolve()==args.output.resolve():
        p.error('input and output must differ')
    out,info=patch(args.input.read_bytes())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(out)
    if args.evidence:
        args.evidence.parent.mkdir(parents=True,exist_ok=True)
        args.evidence.write_text(json.dumps(info,indent=2)+'\n')
    print('PASS',info['output_sha256'],len(info['changed_file_offsets']),'changed bytes')

if __name__=='__main__':main()
