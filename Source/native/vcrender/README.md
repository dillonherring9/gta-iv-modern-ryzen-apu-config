# VCRender — Build 269 source

This is the exact renderer source used by **VCANGCE-269.zip**: a 32-bit Direct3D 9 client (`VCRender.asi`) and a separate 64-bit DXVK host (`VCRenderHost.exe`). The game itself remains 32-bit. This tree includes the resource-creation error propagation, streaming diagnostics and bounded physical-resource allocator used by the release.

## Build

Install **Python 3** and the official **Zig 0.15.2** distribution. Zig includes the Clang/MinGW headers and libraries needed here; this renderer build does not need a separate Windows SDK. Linux x86_64 is the verified build host.

The official Zig Linux archive is [zig-x86_64-linux-0.15.2.tar.xz](https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz), SHA256 `02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239`. Do not substitute the separate Zig 0.13 toolchain used by the older gameplay plugins.

From this directory:

```sh
python3 -I build.py --zig /path/to/zig-0.15.2/zig --check-release --tests
```

Outputs go to `build/`, or the directory passed with `--output`. `ZIG` is also accepted as an environment variable. The script never installs or launches the game. It preserves the original compiler flags and PE timestamp normalization; only tool discovery/output selection and verification/reporting have been made portable.

Expected release artifacts:

| File | Bytes | SHA256 |
|---|---:|---|
| VCRender.asi | 423424 | 9c5bfa2b4c95d3cea118fe86c80a7656f1065e3408d086d66ed50fbd38951a6a |
| VCRenderHost.exe | 374272 | 2857db28c21730f957237230c270916a033905bd38dfaf544aec9b99988d7d3b |

`build/build.json` records the exact source hashes, commands, output hashes and release comparisons. `--check-release` exits unsuccessfully if a binary differs. Byte identity is checked for this pinned toolchain; other compiler versions/platforms are not assumed identical.

## Tests

`--tests` also builds the 32-bit allocator and diagnostic stand-ins. Run them with Wine/Proton in an isolated prefix, or on a suitable Windows test system, from the build output directory:

```text
physpool_test.exe VCRender.asi physpool-results.txt
streamdiag_test.exe VCRender.asi streamdiag-results.txt
```

These use synthetic engine-layout fixtures; they do not modify a game. Other retained `.cpp` tests cover D3D9 differentials, FSR interop, presentation, budgets and latency. Graphics tests require a real compatible DXVK/Vulkan device, an appropriate presentation environment and the matching runtime dependency set. The source snapshot is not a claim of successful playthrough coverage.

## Runtime dependency set

Use the matching package files together: `plugins/VCRender.asi`, `plugins/VCRender.ini`, `plugins/global.ini`, and `plugins/VCRender/{VCRenderHost.exe,d3d9.dll,dxgi.dll,dxvk.conf,dlssiv-client.dll,dlssiv-helper-bridge.exe}`. The host DLLs are unmodified DXVK **v3.1.1-97-ga78a467cadb22be-pcos** from Proton-CachyOS **cachyos-11.0-20261015-slr**; `LICENSE-DXVK.txt` records their provenance. This source export does not build or redistribute those vendor binaries.

The paired client/helper source and its separate build requirements are in [`../paired-dlssiv`](../paired-dlssiv/README.md). The release also supplies the `DLSSIV` vendor runtime directory. DXVK, AMD/NVIDIA/Intel runtime DLLs and the game executable are dependencies, not outputs of this renderer build. Do not replace only one member of a client/host pair; the renderer uses protocol 4.

## Notices and scope

The Vulkan headers are vendored at **Khronos Vulkan-Headers vulkan-sdk-1.4.357.0**, retaining their MIT/Apache notices in `src/third_party/vulkan`. The upscaler service retains DLSS-IV's Apache-2.0 notice and the MIT DLSS5-Feeder/derived protocol notices under `src/upscale`. Those notices are independent of the project's own renderer code; no blanket licence over unrelated third-party work is implied.

`defaults/` holds the release renderer settings. The separate source snapshot's `config/game` contains the complete exported public configuration. Texture/model/audio assets are distributed with the playable package rather than this source tree.

## Changes through 269

The source includes window lifecycle and retained-swapchain repairs, borderless
activation handling and the bounded one-time main-window startup handoff.
Build 269 fixes implicit-surface Reset ownership, retries transient frame mappings,
pins capture slots and propagates FSR quarantine into shutdown/Reset/destruction.
Restart the game after a quarantined helper failure. Renderer protocol 4 rejects
mixed client/host generations. Source publication does not launch a game.

[tests/REVIEW-TESTS.md](tests/REVIEW-TESTS.md) identifies the additional review
fixture sources and their dependencies. Their retained results are described in
SOURCE-STATUS.md; exporting a fixture is separate from rerunning it.
