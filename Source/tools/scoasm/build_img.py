#!/usr/bin/env python3
"""Assemble every script listed in <srcdir>/members.txt and write script.img (IMG v3, plain TOC, 2048-byte blocks),
the layout the project uses for update/common/data/cdimages/script.img.
usage: build_img.py <srcdir> <GTAIV.exe> <out script.img> [--check <reference script.img>]"""
import sys, struct, hashlib
from pathlib import Path
HERE = Path(__file__).resolve().parent; sys.path.insert(0, str(HERE)); import scoasm
def build(srcdir, key):
    lines = [l.split() for l in (srcdir / 'members.txt').read_text().splitlines() if l.strip() and not l.startswith('#')]
    res = int(lines[0][1], 0); members = [(l[0], int(l[1]) if len(l) > 1 else 1) for l in lines[1:]]
    rows = []
    for name, rtype in members:
        data = scoasm.assemble((srcdir / (Path(name).stem + '.scasm')).read_text(), key)
        alloc = (len(data) + 2047) // 2048 * 2048; rows.append((name, data + bytes(alloc - len(data)), len(data), alloc, rtype))
    names = b''.join(r[0].encode() + b'\0' for r in rows); n = len(rows); ts = n * 16 + len(names)
    start = (20 + ts + 2047) // 2048 * 2048; pos = start; table = bytearray()
    for name, blob, size, alloc, rtype in rows:
        table += struct.pack('<IIIHH', size, rtype, pos // 2048, alloc // 2048, alloc - size); pos += len(blob)
    out = bytearray(struct.pack('<IIIIHH', 0xA94E2A52, 3, n, ts, 16, res)) + table + names
    out += bytes(start - len(out))
    for r in rows: out += r[1]
    return bytes(out)
if __name__ == '__main__':
    src, exe, dst = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    img = build(src, scoasm.game_key(exe)); dst.write_bytes(img); print(dst, len(img), hashlib.sha256(img).hexdigest())
    if '--check' in sys.argv:
        ref = Path(sys.argv[sys.argv.index('--check') + 1]).read_bytes()
        print('PASS: identical to reference' if ref == img else 'FAIL: differs from reference'); sys.exit(0 if ref == img else 1)
