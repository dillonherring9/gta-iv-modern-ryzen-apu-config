#!/usr/bin/env python3
"""Project exactly the named canonical SCOs from a mixed plain IMG3.

This is a read-only view for focused VM/global audits. It does not rebuild the
mixed runtime image or alter any source. Every selected complete allocation is
copied, and all unselected resources/network SCOs are counted explicitly.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct


def project(image, manifest):
    image, manifest = Path(image), Path(manifest)
    lines = [line.split() for line in manifest.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith('#')]
    assert lines[0][0] == 'res'
    reserved = int(lines[0][1], 0)
    names = [line[0] for line in lines[1:]]
    assert len(names) == len({n.casefold() for n in names})
    assert all(n.lower().endswith('.sco') for n in names)
    with image.open('rb') as stream:
        head = stream.read(20)
        magic, version, count, size, width, res = struct.unpack('<4I2H', head)
        assert (magic, version, width, res) == (0xA94E2A52, 3, 16, reserved)
        assert count * 16 <= size <= image.stat().st_size - 20
        table = stream.read(size)
        cursor, found, all_sco, seen = count * 16, {}, [], set()
        wanted = {n.casefold() for n in names}
        ranges = []
        for index in range(count):
            end = table.index(b'\0', cursor)
            rawname = table[cursor:end]; cursor = end + 1
            name = rawname.decode('utf-8', 'surrogateescape')
            assert name.casefold() not in seen
            seen.add(name.casefold())
            fields = list(struct.unpack_from('<3I2H', table, index * 16))
            off, alloc = fields[2] * 2048, fields[3] * 2048
            assert off >= 20 + size and off + alloc <= image.stat().st_size
            ranges.append((off, off + alloc))
            if name.lower().endswith('.sco'):
                all_sco.append(name)
            if name.casefold() in wanted:
                stream.seek(off)
                blob = stream.read(alloc)
                assert len(blob) == alloc and fields[0] <= alloc
                assert fields[1] in (0, 1) and fields[4] == alloc - fields[0]
                assert struct.unpack_from('<I', blob)[0] in (0x0e726373, 0x0d524353)
                found[name.casefold()] = (rawname, fields, blob)
    assert cursor == len(table) and set(found) == wanted
    ranges.sort()
    assert all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:]))
    namebytes = b''.join(found[name.casefold()][0] + b'\0' for name in names)
    ts = len(names) * 16 + len(namebytes)
    start = (20 + ts + 2047) // 2048 * 2048
    directory, position = bytearray(), start
    for name in names:
        _, fields, blob = found[name.casefold()]
        fields[2] = position // 2048
        directory += struct.pack('<3I2H', *fields)
        position += len(blob)
    out = bytearray(struct.pack('<4I2H', magic, version, len(names), ts, width, res)) + directory + namebytes
    out += bytes(start - len(out))
    for name in names:
        out += found[name.casefold()][2]
    return bytes(out), dict(container_members=count, canonical_scripts=len(names),
        other_sco=[n for n in all_sco if n.casefold() not in wanted],
        other_resources=count - len(all_sco),
        canonical_projection_sha256=hashlib.sha256(out).hexdigest())


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image', type=Path, required=True)
    ap.add_argument('--members', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    data, report = project(args.image, args.members)
    with args.out.open('xb') as stream:
        stream.write(data)
    print(json.dumps(report))
