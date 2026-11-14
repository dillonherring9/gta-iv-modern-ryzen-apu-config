#!/usr/bin/env python3
"""Acquire/check the pinned NVIDIA and Intel build SDK inputs under their own licences.

This downloads only SDK headers, the NVIDIA import library, and their licences.
It does not download game assets or upscaler runtime DLLs. Files are git-ignored.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent
PINS = json.loads((ROOT / 'SDK-PINS.json').read_text())

def digest(data):
    return hashlib.sha256(data).hexdigest()

def download(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'VCANGCE-source-sdk-fetch/264'})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true', help='Verify existing files; do not download/write')
    p.add_argument('--ngx-ref', default=PINS['ngx']['default_ref'], help='NVIDIA ref to fetch; every file must still match its hash pin')
    p.add_argument('--ngx', type=Path, default=ROOT / 'source/dlssiv/external/ngx')
    p.add_argument('--xess', type=Path, default=ROOT / 'source/dlssiv/external/xess')
    a = p.parse_args()
    def write_checked(path, data, expected, normalized=False):
        actual = digest(data.replace(b'\r\n', b'\n') if normalized else data)
        if actual != expected:
            raise SystemExit('Hash mismatch for ' + str(path) + ': ' + actual)
        if not a.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    for row in PINS['ngx']['files']:
        path = a.ngx / row['destination']
        data = path.read_bytes() if path.exists() else (None if a.check else download(PINS['ngx']['base'].format(ref=a.ngx_ref) + row['upstream']))
        if data is None:
            raise SystemExit('Missing SDK input: ' + str(path))
        write_checked(path, data, row['normalized_sha256'], True)
    row = PINS['ngx']['library']; path = a.ngx / row['destination']
    data = path.read_bytes() if path.exists() else (None if a.check else download(PINS['ngx']['base'].format(ref=a.ngx_ref) + row['upstream']))
    if data is None or len(data) != row['bytes']:
        raise SystemExit('Missing/wrong-sized NGX import library')
    write_checked(path, data, row['sha256'])
    archive = None
    for row in PINS['xess']['files']:
        path = a.xess / row['destination']
        if path.exists():
            data = path.read_bytes()
        elif a.check:
            raise SystemExit('Missing SDK input: ' + str(path))
        else:
            if archive is None:
                package = download(PINS['xess']['url'])
                if len(package) != PINS['xess']['bytes'] or digest(package) != PINS['xess']['sha256']:
                    raise SystemExit('Intel SDK ZIP does not match its pin')
                archive = zipfile.ZipFile(io.BytesIO(package))
            candidates = [n for n in archive.namelist() if n == row['upstream'] or n.endswith('/' + row['upstream'])]
            if len(candidates) != 1:
                raise SystemExit('Ambiguous/missing Intel SDK file: ' + row['upstream'])
            data = archive.read(candidates[0])
        write_checked(path, data, row['normalized_sha256'], True)
    print('PASS: all pinned NVIDIA/Intel SDK inputs and licence texts verified')

if __name__ == '__main__':
    main()
