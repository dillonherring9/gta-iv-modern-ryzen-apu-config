#!/usr/bin/env python3
"""Assemble every script listed in <srcdir>/members.txt and write script.img (IMG v3, plain TOC, 2048-byte blocks),
the layout the project uses for update/common/data/cdimages/script.img.
usage: build_img.py <srcdir> <GTAIV.exe> <out script.img> [--check <reference script.img>]
                    [--resources-from <mixed script.img>]

The mixed mode replaces only members explicitly listed in members.txt and
preserves every other complete allocation, including the network scripts.
For current sources the baseline must be supplied: omitting it is refused when
the maintained source map identifies a mixed runtime container.
"""
import sys, struct, hashlib
from pathlib import Path
HERE = Path(__file__).resolve().parent; sys.path.insert(0, str(HERE)); import scoasm
def build(srcdir, key, base_image=None):
    if base_image is None:
        # Current project sources may not silently produce an IMG that drops the
        # resources merged with scripts. Historical/portable standalone sources keep
        # their prior behavior.
        portable_manifest = srcdir / 'manifest.json'
        if portable_manifest.is_file():
            import json
            if json.loads(portable_manifest.read_text()).get('mixed_container'):
                raise ValueError('Mixed runtime IMG requires --resources-from; refusing resource loss')
        mapping = srcdir.parent.parent / 'manifests/runtime-source-map.json'
        if mapping.is_file():
            import json
            doc = json.loads(mapping.read_text())
            if doc.get('vm_scripts', {}).get('mixed_container'):
                raise ValueError('Mixed runtime IMG requires --resources-from; refusing resource loss')
    lines = [l.split() for l in (srcdir / 'members.txt').read_text().splitlines() if l.strip() and not l.startswith('#')]
    res = int(lines[0][1], 0); members = [(l[0], int(l[1]) if len(l) > 1 else 1) for l in lines[1:]]
    rows = []
    for name, rtype in members:
        data = scoasm.assemble((srcdir / (Path(name).stem + '.scasm')).read_text(), key)
        alloc = (len(data) + 2047) // 2048 * 2048; rows.append((name, data + bytes(alloc - len(data)), len(data), alloc, rtype))
    if base_image is not None:
        return merge_existing(Path(base_image), rows, res)
    names = b''.join(r[0].encode() + b'\0' for r in rows); n = len(rows); ts = n * 16 + len(names)
    start = (20 + ts + 2047) // 2048 * 2048; pos = start; table = bytearray()
    for name, blob, size, alloc, rtype in rows:
        table += struct.pack('<IIIHH', size, rtype, pos // 2048, alloc // 2048, alloc - size); pos += len(blob)
    out = bytearray(struct.pack('<IIIIHH', 0xA94E2A52, 3, n, ts, 16, res)) + table + names
    out += bytes(start - len(out))
    for r in rows: out += r[1]
    return bytes(out)


def merge_existing(path, replacements, reserved):
    """Read raw IMG3, replacing named SCOs without interpreting other resources."""
    raw = path.read_bytes()
    magic, version, count, size, width, res = struct.unpack_from('<4I2H', raw)
    if (magic, version, width, res) != (0xA94E2A52, 3, 16, reserved):
        raise ValueError('Mixed baseline IMG header/reserved differs from sources')
    if size < count * 16 or 20 + size > len(raw):
        raise ValueError('Invalid mixed IMG directory bounds')
    updates = {r[0].casefold(): r for r in replacements}
    if len(updates) != len(replacements):
        raise ValueError('Duplicate source member names')
    rows, seen, cursor, unchanged = [], set(), 20 + count * 16, True
    ranges = []
    for index in range(count):
        end = raw.find(b'\0', cursor, 20 + size)
        if end < cursor:
            raise ValueError('Unterminated IMG name')
        name = raw[cursor:end]; cursor = end + 1
        folded = name.decode('utf-8', 'surrogateescape').casefold()
        if folded in seen:
            raise ValueError('Duplicate IMG member')
        seen.add(folded)
        fields = list(struct.unpack_from('<3I2H', raw, 20 + index * 16))
        offset, alloc = fields[2] * 2048, fields[3] * 2048
        if offset < 20 + size or offset + alloc > len(raw):
            raise ValueError('Invalid IMG allocation bounds')
        ranges.append((offset, offset + alloc))
        blob = raw[offset:offset + alloc]
        update = updates.pop(folded, None)
        if update is not None:
            _, candidate, logical, new_alloc, kind = update
            if not folded.endswith('.sco') or fields[1] != kind:
                raise ValueError('Source target is not the expected SCO type')
            # Preserve byte-exact baseline when every assembled script matches;
            # this includes original directory/sector padding and network SCOs.
            if fields[0] != logical or blob[:logical] != candidate[:logical]:
                unchanged = False
                blob = candidate
                fields[0], fields[1], fields[3], fields[4] = logical, kind, new_alloc // 2048, new_alloc - logical
        rows.append((name, fields, blob))
    if updates or cursor != 20 + size:
        raise ValueError('Missing source members or malformed IMG name table')
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('Overlapping IMG allocations')
    if unchanged:
        return raw
    names = b''.join(name + b'\0' for name, _, _ in rows)
    table_size = count * 16 + len(names)
    start = (20 + table_size + 2047) // 2048 * 2048
    table, pos = bytearray(), start
    for name, fields, blob in rows:
        fields[2] = pos // 2048
        table += struct.pack('<3I2H', *fields)
        pos += len(blob)
    out = bytearray(struct.pack('<4I2H', magic, version, count, table_size, width, res)) + table + names
    out += bytes(start - len(out))
    for _, _, blob in rows:
        out += blob
    return bytes(out)
if __name__ == '__main__':
    if len(sys.argv) < 4 or sys.argv[1] in ('-h', '--help'):
        raise SystemExit(__doc__ or 'see tools/README.md')
    src, exe, dst = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    base = Path(sys.argv[sys.argv.index('--resources-from') + 1]) if '--resources-from' in sys.argv else None
    img = build(src, scoasm.game_key(exe), base); dst.write_bytes(img); print(dst, len(img), hashlib.sha256(img).hexdigest())
    if '--check' in sys.argv:
        ref = Path(sys.argv[sys.argv.index('--check') + 1]).read_bytes()
        print('PASS: identical to reference' if ref == img else 'FAIL: differs from reference'); sys.exit(0 if ref == img else 1)
