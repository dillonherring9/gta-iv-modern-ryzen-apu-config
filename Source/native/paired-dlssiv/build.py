#!/usr/bin/env python3
"""Build the paired Build 264 DLSS-IV client/helper; requires separately acquired SDKs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'source/dlssiv'
EXPECTED = {'dlssiv-client.dll': 'b71acebec3935229f23baee2bfa897b9624c4024cb6460c4abaf31a78782f552',
            'dlssiv-helper-bridge.exe': 'a3f500ff812dff919018512fba7967d7b233d3b7dce6287406129e17f4b48d7f'}

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    p.add_argument('--lld', default=os.environ.get('LLD_LINK', 'lld-link'))
    p.add_argument('--sdk', type=Path, default=os.environ.get('XWIN_SDK'))
    p.add_argument('--ngx', type=Path, default=SOURCE / 'external/ngx')
    p.add_argument('--xess', type=Path, default=SOURCE / 'external/xess')
    p.add_argument('--output', type=Path, default=ROOT / 'build')
    p.add_argument('--check-release', action='store_true')
    a = p.parse_args()
    zig, lld = shutil.which(a.zig), shutil.which(a.lld)
    if not zig or not lld or a.sdk is None:
        p.error('Provide Zig 0.15.2, lld-link 22.1.8 and --sdk /path/to/xwin/splat (see README.md).')
    version = subprocess.check_output([zig, 'version'], text=True).strip()
    if version != '0.15.2':
        p.error('Expected Zig 0.15.2; found ' + version)
    sdk, ngx, xess, out = a.sdk.resolve(), a.ngx.resolve(), a.xess.resolve(), a.output.resolve()
    required = [sdk / 'crt/include', sdk / 'sdk/include/ucrt', sdk / 'sdk/include/um', sdk / 'sdk/include/shared',
                ngx / 'include/nvsdk_ngx.h', ngx / 'lib/nvsdk_ngx_s.lib', xess / 'inc/xess/xess.h']
    for path in required:
        if not path.exists():
            p.error('Missing dependency: ' + str(path))
    out.mkdir(parents=True, exist_ok=True)
    commands = []
    def run(command):
        command = list(map(str, command)); commands.append(command)
        subprocess.run(command, check=True)
    inc = []
    for path in (sdk / 'crt/include', sdk / 'sdk/include/ucrt', sdk / 'sdk/include/um', sdk / 'sdk/include/shared'):
        inc += ['-isystem', path]
    common = [zig, 'c++', '-nostdlib', '-ffreestanding', '-std=c++17', '-O2',
              '-fms-extensions', '-fms-compatibility', *inc]
    flags = ['-fasync-exceptions', '-DDLSSIV_DEV=0', '-DDLSSIV_FG=1',
             '-DIVT_BUILD_ID="60f29688-vcbridge1"', '-I' + str(SOURCE / 'external/vulkan'), '-I' + str(ROOT / 'src')]
    obj32 = out / 'source__dlssiv__source__DLSS-IV.cpp.obj'
    run([*common, '-target', 'x86-windows-msvc', '-DWIN32', '-D_WINDOWS', *flags, '-msse4.1',
         '-c', SOURCE / 'source/DLSS-IV.cpp', '-o', obj32])
    obj64 = out / 'source__dlssiv__host__dlssiv-helper.cpp.obj'
    run([*common, '-target', 'x86_64-windows-msvc', '-DWIN32', '-DUNICODE', *flags,
         '-I' + str(ngx / 'include'), '-I' + str(SOURCE / 'external/ffx'), '-I' + str(xess / 'inc'),
         '-DDLSSIV_FG_OWN_SIZE=822272ull',
         '-DDLSSIV_FG_OWN_SHA="96764ff6b33ff7f88fbb0c41693b431cb34bf3e106c2449327aecf411f1ff03f"',
         '-c', SOURCE / 'host/dlssiv-helper.cpp', '-o', obj64])
    def link(arch, objects, name, extra, subsystem):
        cmd = [lld, '/machine:' + arch, '/subsystem:' + subsystem, '/timestamp:0']
        if name.endswith('.dll'):
            cmd += ['/dll']
        cmd += objects
        target = 'x86' if arch == 'x86' else 'x86_64'
        for path in (sdk / ('crt/lib/' + target), sdk / ('sdk/lib/um/' + target), sdk / ('sdk/lib/ucrt/' + target)):
            cmd += ['/libpath:' + str(path)]
        cmd += [name + '.lib' for name in ('libcmt', 'libvcruntime', 'libucrt', 'oldnames', 'kernel32',
                'user32', 'gdi32', 'ole32', 'advapi32', 'version', 'shell32', 'dbghelp', 'uuid', 'psapi', 'comctl32', *extra)]
        cmd += ['/out:' + str(out / name)]
        run(cmd)
    link('x86', [obj32], 'dlssiv-client.dll', ('d3d9',), 'windows')
    link('x64', [obj64, ngx / 'lib/nvsdk_ngx_s.lib'], 'dlssiv-helper-bridge.exe', ('d3d12', 'dxgi'), 'console')
    artifacts = {name: {'bytes': (out / name).stat().st_size, 'sha256': sha(out / name),
                       'release_sha256': expected, 'matches_release': sha(out / name) == expected}
                 for name, expected in EXPECTED.items()}
    result = {'compiler': 'Zig ' + version, 'linker': subprocess.check_output([lld, '--version'], text=True).strip(),
              'artifacts': artifacts, 'commands': commands,
              'sources': {str(f.relative_to(ROOT)): sha(f) for f in sorted((ROOT / 'source').rglob('*')) if f.is_file()},
              'external_ngx_library_sha256': sha(ngx / 'lib/nvsdk_ngx_s.lib')}
    (out / 'build.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(artifacts, indent=2))
    if a.check_release and not all(r['matches_release'] for r in artifacts.values()):
        raise SystemExit('Build does not reproduce the release; inspect build.json and SDK versions.')

if __name__ == '__main__':
    main()
