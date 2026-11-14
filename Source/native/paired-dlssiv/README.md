# Paired DLSS-IV client/helper — Builds 264–269

This is the project-modified **DLSS-IV 1.1 / build ID `60f29688-vcbridge1`** source used by VCRender: `dlssiv-client.dll` (x86) and `dlssiv-helper-bridge.exe` (x64). It is distinct from the original root `DLSS-IV.asi`, which VCRender's loader configuration suppresses. Keep this pair matched.

The paired source supplies scene/guide processing and the 64-bit upscaler helper. VCRender's own client/host calls the service. The original `source/dlssiv/README.md` and `NOTICE` describe upstream capabilities and dependencies; they are retained attribution/reference material, not a support claim for every backend through VCRender. FSR 3.1.5 is the gameplay-verified path; the NVIDIA DLSS implementation is present but not verified on RTX through VCRender. The current VCRender presenter does not output HDR.

## Files included and separately acquired inputs

- Included: the exact project-modified `source/` and `host/` code, shader bytecode headers used by that code, AMD FidelityFX API headers/notices, Vulkan headers/notices, DXVK interface notices, and the project `source/bridge/src/vc_upscale_wire.h` ABI header. This ABI header is the only retained bridge-tree dependency; the NVIDIA bridge implementation is not needed to build this pair.
- Separately acquire under their own terms: Microsoft Windows SDK/CRT, NVIDIA NGX headers/static import library, and Intel XeSS headers. Vendor runtime DLLs and game assets are not source outputs.
- `SDK-PINS.json`, `fetch-sdks.py`, and the original `tools/fetch-ngx.ps1` / `fetch-xess.ps1` retain hash-pinned acquisition instructions. SDK content is deliberately excluded and git-ignored. Fetches refuse changed upstream files rather than silently changing build inputs.

## Verified build toolchain

The release was built on Linux x86_64 with:

- **Zig 0.15.2**, Clang 20.1.2, using the `x86-windows-msvc` / `x86_64-windows-msvc` targets.
- **LLD 22.1.8** (`lld-link`), `/timestamp:0`.
- Microsoft SDK/CRT laid out by **xwin 0.10.0**. The acquisition manifest is Visual Studio 2022 **17.14.44+37827.9**; relevant CRT header package **14.44.35220**, desktop CRT libraries **14.44.35226**. Microsoft components remain under Microsoft's terms and are not redistributed here.
- The exact NGX and XeSS pins in `SDK-PINS.json`; AMD FidelityFX API **v2.3.0** headers already included. Generated shader headers are retained, so rebuilding the shader compiler pipeline is not required for these two binaries.

Obtain Zig from the [official 0.15.2 download](https://ziglang.org/download/0.15.2/), LLD from [LLVM](https://github.com/llvm/llvm-project/releases/tag/llvmorg-22.1.8) or a matching system package, and xwin from [xwin 0.10.0](https://github.com/Jake-Shadle/xwin/releases/tag/0.10.0). Use xwin's SDK/CRT acquisition instructions for both x86 and x86_64 desktop libraries; pass the resulting splat directory to `--sdk`. A newer Microsoft SDK can build compatible code while producing different binary bytes, so retain/check the toolchain fingerprint rather than claiming automatic reproducibility with the latest SDK.

## Build

First acquire the pinned NVIDIA/Intel inputs (Python standard library only; original PowerShell fetchers are also available):

```sh
python3 -I fetch-sdks.py
python3 -I fetch-sdks.py --check
```

NVIDIA's `main` is not an immutable pin. The individual hashes are the pins. If a file changes upstream, use `--ngx-ref` with a retained matching revision; the script never relaxes the hash checks. The Intel SDK ZIP is pinned by size and SHA256 before extraction.

Then build:

```sh
python3 -I build.py --zig /path/to/zig-0.15.2/zig \
  --lld /path/to/lld-link --sdk /path/to/xwin/splat --check-release
```

`ZIG`, `LLD_LINK`, and `XWIN_SDK` may supply the paths. `--ngx` and `--xess` may point at separately acquired input directories outside this checkout. If your LLD package uses private shared libraries, configure its normal library search path before running it. The build has no hardcoded project directories and never installs or launches a game.

Outputs default to `build/`; use `--output` to keep all artifacts elsewhere. Expected identities:

| Output | Bytes | SHA256 |
|---|---:|---|
| dlssiv-client.dll | 1191936 | b71acebec3935229f23baee2bfa897b9624c4024cb6460c4abaf31a78782f552 |
| dlssiv-helper-bridge.exe | 332288 | a3f500ff812dff919018512fba7967d7b233d3b7dce6287406129e17f4b48d7f |

The portable builder keeps the original compile/link order, build ID, static CRT options and frame-generation-library identity constants. `build/build.json` records fresh output hashes and whether they match the release. This builds the client/helper, not the separate AMD/NVIDIA/Intel runtime libraries, the optional frame-generation library or the playable package. Merely finding a backend in the helper source does not establish that it works in game through VCRender.

## Licences

The original Apache-2.0 `LICENSE` and `NOTICE` are preserved in `source/dlssiv`. Its transport directories retain the MIT DLSS5-Feeder/NIGos notices; AMD, DXVK, Khronos, LatencyFleX and other referenced notices remain with their respective source dependencies. NVIDIA/Intel SDK files fetched for compilation retain their own licence texts. Do not add downloaded SDK content or build outputs to the public source snapshot.

The Build 269 release retains the exact paired implementation and binaries from
Build 264. Its fresh Build 264 reproduction is retained evidence; Build 269
publication checks source and runtime parity without relabeling it as a new build.
