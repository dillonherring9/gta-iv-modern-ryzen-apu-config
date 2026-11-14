#!/usr/bin/env python3
"""Cross-build the Build 269 VCRender client/host with Zig 0.15.2; never install."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
COMMON = ['-std=c++17', '-O2', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra',
          '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-Wno-unused-function',
          '-Wno-cast-function-type', '-fno-strict-aliasing']
CLIENT = ['vcr_channel.cpp', 'vcr_present.cpp', 'vcr_util.cpp', 'vcr_resources.cpp',
          'vcr_device.cpp', 'vcr_window.cpp', 'vcr_d3d9.cpp', 'vcr_dxgi.cpp', 'vcr_budget.cpp',
          'vcr_interop.cpp', 'vcr_streamdiag.cpp', 'vcr_physpool.cpp']
EXPECTED = {'VCRender.asi': '9c5bfa2b4c95d3cea118fe86c80a7656f1065e3408d086d66ed50fbd38951a6a',
            'VCRenderHost.exe': '2857db28c21730f957237230c270916a033905bd38dfaf544aec9b99988d7d3b'}

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    parser.add_argument('--output', type=Path, default=ROOT / 'build')
    parser.add_argument('--check-release', action='store_true', help='Fail unless both binaries match VCANGCE-269.zip')
    parser.add_argument('--tests', action='store_true', help='Also compile allocator/diagnostic stand-in executables')
    args = parser.parse_args()
    zig = shutil.which(args.zig)
    if not zig:
        parser.error('Zig not found. Install Zig 0.15.2 and pass --zig or set ZIG.')
    version = subprocess.check_output([zig, 'version'], text=True).strip()
    if version != '0.15.2':
        parser.error('Expected Zig 0.15.2; found ' + version)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    def run(command):
        command = list(map(str, command)); commands.append(command)
        subprocess.run(command, check=True)
    objects = []
    for name in CLIENT:
        obj = output / (Path(name).stem + '.x86.o')
        run([zig, 'c++', '-target', 'x86-windows-gnu', *COMMON,
             '-I', ROOT / 'src/third_party/vulkan', '-c', ROOT / 'src/client' / name, '-o', obj])
        objects.append(obj)
    client = output / 'VCRender.asi'
    run([zig, 'c++', '-target', 'x86-windows-gnu', '-shared', '-s', '-o', client,
         *objects, '-luser32', '-lgdi32', '-luuid'])
    host = output / 'VCRenderHost.exe'
    run([zig, 'c++', '-target', 'x86_64-windows-gnu', *COMMON, '-municode',
         '-Wl,--subsystem,windows', '-s', '-I', ROOT / 'src/third_party/vulkan',
         ROOT / 'src/server/vcr_host.cpp', ROOT / 'src/server/vcr_host_vk.cpp',
         ROOT / 'src/upscale/vc_upscale_server.cpp', '-o', host,
         '-luser32', '-lshell32', '-luuid', '-lpsapi'])
    for path in (client, host):
        data = bytearray(path.read_bytes()); pe = struct.unpack_from('<I', data, 60)[0]
        assert data[pe:pe + 4] == b'PE\0\0'
        struct.pack_into('<I', data, pe + 8, 0); path.write_bytes(data)
    records = {path.name: {'bytes': path.stat().st_size, 'sha256': sha(path),
                          'release_sha256': EXPECTED[path.name], 'matches_release': sha(path) == EXPECTED[path.name]}
               for path in (client, host)}
    if args.tests:
        for name in ('physpool_test', 'streamdiag_test'):
            run([zig, 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                 '-fno-exceptions', '-fno-rtti', '-o', output / (name + '.exe'),
                 ROOT / 'tests' / (name + '.cpp')])
    (output / 'build.json').write_text(json.dumps({'compiler': 'Zig ' + version,
        'artifacts': records, 'commands': commands,
        'sources': {str(p.relative_to(ROOT)): sha(p) for p in sorted((ROOT / 'src').rglob('*')) if p.is_file()}}, indent=2) + '\n')
    print(json.dumps(records, indent=2))
    if args.check_release and not all(row['matches_release'] for row in records.values()):
        raise SystemExit('Build does not reproduce the release; inspect build.json.')

if __name__ == '__main__':
    main()
