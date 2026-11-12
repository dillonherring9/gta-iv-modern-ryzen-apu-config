#!/usr/bin/env python3
"""scoasm — byte-exact GTA IV (PC, CE 1.2.0.59) SCO script disassembler / assembler.

  scoasm.py disasm <file.sco> [-o file.scasm]          SCO -> readable assembly source
  scoasm.py asm    <file.scasm> [-o file.sco] --exe GTAIV.exe
  scoasm.py verify <file.scasm> <original.sco> --exe GTAIV.exe   assembles and compares SHA-256

Exactness rules (so a source rebuilds the identical file): every opcode is written by its own name (short forms
Push_N / PushS / Push stay distinct), PushF keeps its float bits (`0x...` when the decimal form would not round-trip),
PushString keeps every byte including padding NULs, data after the code (statics, globals) is kept verbatim.
PushS is a signed16 immediate; larger positive values require full-width Push.
Jump/Call/Switch targets become labels, so code can be edited and the assembler relocates everything.
Encrypted scripts ('scr\\x0e') are re-encrypted with the AES key read from the user's own GTAIV.exe
(fourfury.crypto.extract_aes_key); no key is stored in this tool. Natives are written by SC-CL natives32.h name
(table ids, not joaat of the name) when the name is unique, otherwise as 0xID.
"""
import argparse, hashlib, re, struct, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
OPS = ['Nop', 'Add', 'Sub', 'Mult', 'Div', 'Mod', 'Not', 'Neg', 'CmpEQ', 'CmpNE', 'CmpGT', 'CmpGE', 'CmpLT', 'CmpLE',
       'fAdd', 'fSub', 'fMult', 'fDiv', 'fMod', 'fNeg', 'fCmpEQ', 'fCmpNE', 'fCmpGT', 'fCmpGE', 'fCmpLT', 'fCmpLE',
       'vAdd', 'vSub', 'vMult', 'vDiv', 'vNeg', 'And', 'Or', 'Xor', 'Jump', 'JumpFalse', 'JumpTrue', 'ItoF', 'FtoI',
       'FtoV', 'PushS', 'Push', 'PushF', 'Dup', 'Drop', 'CallNative', 'Call', 'Function', 'Return', 'pGet', 'pSet',
       'pPeekSet', 'ToStack', 'FromStack'] + [f'GetLocalPv{i}' for i in range(8)] + [
       'GetLocalPs', 'GetStaticPs', 'GetGlobalPs', 'GetArrayPs', 'Switch', 'PushString', 'PushStringNull', 'StrCopy',
       'ItoS', 'StrAdd', 'StrAddi', 'Catch', 'Throw', 'MemCopy', 'GetXProtect', 'SetXProtect', 'RefXProtect', 'Exit'] + [
       f'Push_Neg{n}' for n in range(16, 0, -1)] + [f'Push_{k - 96}' for k in range(96, 256)]
assert len(OPS) == 256 and OPS[80] == 'Push_Neg16' and OPS[96] == 'Push_0' and OPS[255] == 'Push_159'
OPC = {n: i for i, n in enumerate(OPS)}
JUMPS = ('Jump', 'JumpFalse', 'JumpTrue', 'Call')
BYTE_OPS = ('StrCopy', 'ItoS', 'StrAdd', 'StrAddi')
MAGIC_ENC, MAGIC_PLAIN = 0x0E726373, 0x0D524353   # 'scr\x0e' encrypted, 'SCR\r' plain

def natives():
    src = (HERE / 'natives32.h').read_text(errors='replace')
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    pairs = re.findall(r'_native32\((0x[0-9a-fA-F]+)\)\s+[^;\n(]*?\b([A-Za-z_]\w*)\s*\(', src)
    by_id, by_name = {}, {}
    for h, n in pairs:
        by_id.setdefault(int(h, 16), n); by_name.setdefault(n, set()).add(int(h, 16))
    unique = {n: next(iter(s)) for n, s in by_name.items() if len(s) == 1}
    return by_id, unique

def aes(data, key, mode):
    n = len(data) // 16 * 16; v = data[:n]
    for _ in range(16):
        if n: v = subprocess.run(['openssl', 'enc', '-aes-256-ecb', mode, '-nopad', '-K', key.hex()], input=v, capture_output=True, check=True).stdout
    return v + data[n:]

def game_key(exe):
    from fourfury.crypto import extract_aes_key  # pip install fourfury==0.13.3
    return extract_aes_key(Path(exe))

def decode(code):
    pc, out = 0, []
    while pc < len(code):
        name = OPS[code[pc]]; size, val = 1, None
        if name in JUMPS or name == 'Push': size, val = 5, struct.unpack_from('<i', code, pc + 1)[0]
        elif name == 'PushF': size, val = 5, struct.unpack_from('<I', code, pc + 1)[0]
        elif name == 'PushS': size, val = 3, struct.unpack_from('<h', code, pc + 1)[0]
        elif name == 'CallNative': size, val = 7, (code[pc + 1], code[pc + 2], struct.unpack_from('<I', code, pc + 3)[0])
        elif name == 'Function': size, val = 4, (code[pc + 1], struct.unpack_from('<H', code, pc + 2)[0])
        elif name == 'Return': size, val = 3, (code[pc + 1], code[pc + 2])
        elif name == 'PushString': size = 2 + code[pc + 1]; val = bytes(code[pc + 2:pc + size])
        elif name == 'Switch': size = 2 + code[pc + 1] * 8; val = [struct.unpack_from('<iI', code, pc + 2 + i * 8) for i in range(code[pc + 1])]
        elif name in BYTE_OPS: size, val = 2, code[pc + 1]
        if pc + size > len(code): raise ValueError(f'instruction at {pc:x} exceeds code')
        out.append((pc, name, val)); pc += size
    return out

def esc(b):
    s = ''
    for c in b:
        ch = chr(c)
        if ch == '"' or ch == '\\': s += '\\' + ch
        elif 32 <= c < 127: s += ch
        elif c == 0: s += '\\0'
        else: s += f'\\x{c:02x}'
    return '"' + s + '"'

def unesc(s):
    out, i = bytearray(), 0
    while i < len(s):
        c = s[i]
        if c == '\\':
            n = s[i + 1]
            if n == '0': out.append(0); i += 2
            elif n == 'x': out.append(int(s[i + 2:i + 4], 16)); i += 4
            else: out.append(ord(n)); i += 2
        else: out += c.encode('latin1'); i += 1
    return bytes(out)

def fmt_float(bits):
    f = struct.unpack('<f', struct.pack('<I', bits))[0]
    r = repr(f)
    if f == f and abs(f) != float('inf') and struct.unpack('<I', struct.pack('<f', float(r)))[0] == bits: return r
    return f'0x{bits:08X}'

def disassemble(raw, key, name):
    magic, cs, st, gl, fl, sig = struct.unpack_from('<6I', raw)
    if magic not in (MAGIC_ENC, MAGIC_PLAIN): raise ValueError(f'{name}: not a GTA IV SCO')
    code = raw[24:24 + cs]
    if magic == MAGIC_ENC: code = aes(code, key, '-d')
    tail = raw[24 + cs:]
    by_id, unique = natives(); ins = decode(code)
    valid = {p for p, _, _ in ins}; targets = set()
    for p, n, v in ins:
        if n in JUMPS and v in valid: targets.add(v)
        if n == 'Switch': targets.update(t for _, t in v if t in valid)
    lab = lambda t: f'@L_{t:06x}' if t in valid else str(t)
    L = [f'; {name} - GTA IV SCO assembly source (scoasm). Rebuild: scoasm.py asm {Path(name).stem}.scasm --exe GTAIV.exe',
         f'; original sha256 {hashlib.sha256(raw).hexdigest()}',
         f'.script {name}', f'.encrypted {1 if magic == MAGIC_ENC else 0}',
         f'.statics {st}', f'.globals {gl}', f'.flags 0x{fl:08X}', f'.signature 0x{sig:08X}', '', '.code']
    for p, n, v in ins:
        if n == 'Function': L.append('')
        if p in targets: L.append(f'L_{p:06x}:')
        if v is None: arg = ''
        elif n in JUMPS: arg = lab(v)
        elif n == 'PushF': arg = fmt_float(v)
        elif n == 'CallNative':
            a, r, i = v; nm = by_id.get(i)
            arg = (f'{nm} ' if nm and unique.get(nm) == i else f'0x{i:08X} ') + f'{a} {r}'
        elif n == 'Function': arg = f'{v[0]} {v[1]}'
        elif n == 'Return': arg = f'{v[0]} {v[1]}'
        elif n == 'PushString': arg = esc(v)
        elif n == 'Switch': arg = ', '.join(f'{c}={lab(t)}' for c, t in v)
        else: arg = str(v)
        L.append(f'    {n} {arg}'.rstrip())
    L += ['', '.data   ; statics + globals and any trailing bytes, verbatim']
    for i in range(0, len(tail), 32): L.append('    ' + tail[i:i + 32].hex())
    return '\n'.join(L) + '\n'

def assemble(text, key):
    hdr, code_lines, data, sec = {}, [], bytearray(), None
    by_id, unique = natives()
    for line in text.splitlines():
        s = line.split(';', 1)[0].strip() if '"' not in line else _strip_comment(line)
        if not s: continue
        if s.startswith('.'):
            k, _, v = s.partition(' ')
            if k == '.code': sec = 'code'
            elif k == '.data': sec = 'data'
            else: hdr[k[1:]] = v.strip()
            continue
        if sec == 'code': code_lines.append(s)
        elif sec == 'data': data += bytes.fromhex(s)
    # pass 1: sizes and labels
    items, labels, pc = [], {}, 0
    for s in code_lines:
        if s.endswith(':') and ' ' not in s: labels[s[:-1]] = pc; continue
        op, _, arg = s.partition(' '); arg = arg.strip()
        if op not in OPC: raise ValueError(f'unknown opcode {op!r}')
        if op in JUMPS or op in ('Push', 'PushF'): size = 5
        elif op == 'PushS': size = 3
        elif op == 'CallNative': size = 7
        elif op == 'Function': size = 4
        elif op == 'Return': size = 3
        elif op == 'PushString': size = 2 + len(unesc(arg[1:-1]))
        elif op == 'Switch': size = 2 + 8 * (arg.count('=') if arg else 0)
        elif op in BYTE_OPS: size = 2
        else: size = 1
        items.append((op, arg)); pc += size
    tgt = lambda a: labels[a[1:]] if a.startswith('@') else int(a, 0)
    out = bytearray()
    for op, arg in items:
        out.append(OPC[op])
        if op in JUMPS: out += struct.pack('<i', tgt(arg))
        elif op == 'Push': out += struct.pack('<i', int(arg, 0))
        elif op == 'PushF': out += struct.pack('<I', int(arg, 16)) if arg.lower().startswith('0x') else struct.pack('<f', float(arg))
        elif op == 'PushS':
            value = int(arg, 0)
            if not -32768 <= value <= 32767:
                raise ValueError(f'PushS is signed16 in CE; {value} requires full-width Push')
            out += struct.pack('<h', value)
        elif op == 'CallNative':
            nm, a, r = arg.split(); i = int(nm, 16) if nm.lower().startswith('0x') else unique[nm]
            out += bytes([int(a), int(r)]) + struct.pack('<I', i)
        elif op == 'Function': a, l = arg.split(); out += bytes([int(a)]) + struct.pack('<H', int(l))
        elif op == 'Return': a, b = arg.split(); out += bytes([int(a), int(b)])
        elif op == 'PushString': b = unesc(arg[1:-1]); out += bytes([len(b)]) + b
        elif op == 'Switch':
            cases = [c.strip().split('=') for c in arg.split(',')] if arg else []
            out.append(len(cases))
            for c, t in cases: out += struct.pack('<iI', int(c, 0), tgt(t))
        elif op in BYTE_OPS: out.append(int(arg, 0))
    enc = hdr.get('encrypted', '1') == '1'
    code = aes(bytes(out), key, '-e') if enc else bytes(out)
    head = struct.pack('<6I', MAGIC_ENC if enc else MAGIC_PLAIN, len(out), int(hdr['statics']), int(hdr['globals']), int(hdr['flags'], 16), int(hdr['signature'], 16))
    return head + code + bytes(data)

def _strip_comment(line):
    q = False; i = 0
    while i < len(line):
        c = line[i]
        if c == '\\' and q: i += 2; continue
        if c == '"': q = not q
        elif c == ';' and not q: return line[:i].strip()
        i += 1
    return line.strip()

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['disasm', 'asm', 'verify']); ap.add_argument('src'); ap.add_argument('orig', nargs='?')
    ap.add_argument('-o'); ap.add_argument('--exe', required=True)
    a = ap.parse_args(); key = game_key(a.exe)
    if a.cmd == 'disasm':
        t = disassemble(Path(a.src).read_bytes(), key, Path(a.src).name)
        Path(a.o or Path(a.src).with_suffix('.scasm')).write_text(t)
    elif a.cmd == 'asm':
        Path(a.o or Path(a.src).with_suffix('.sco')).write_bytes(assemble(Path(a.src).read_text(), key))
    else:
        b = assemble(Path(a.src).read_text(), key); o = Path(a.orig).read_bytes()
        ok = b == o; print(('PASS' if ok else 'FAIL') + f' {a.src}: {hashlib.sha256(b).hexdigest()}'); sys.exit(0 if ok else 1)
