# DXVK Configuration — Version 3.0.2

Version 3.0.2 separates renderer configuration by compatibility contract. It does not bundle a DXVK binary and does not claim that DXVK improves every GTA IV installation. DXVK may improve a CPU-limited D3D9 path, may not help a GPU-limited path, and must be tested against the actual driver and mod stack.[^1]

## Stock DXVK baseline

`tuned/dxvk-stock.conf` is the default manual renderer profile. It uses only keys documented by upstream DXVK and contains no GPLAsync-specific async settings.

| Setting | Value | Boundary |
|---|---:|---|
| `d3d9.maxFrameLatency` | `1` | Requests stricter frame latency; test `0` if repeatable warm-cache pacing becomes worse. |
| `d3d9.maxFrameRate` | `0` | Leaves DXVK’s cap off so FusionFix is the only intended limiter. |
| `d3d9.presentInterval` | `-1` | Does not override the game’s VSync policy. |
| `d3d9.maxAvailableMemory` | `4096` | D3D9 memory-reporting/tracking value in MiB, not reserved dedicated VRAM. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Lets DXVK and the driver select a supported pipeline-library path. |
| `dxvk.allowFse` | `true` | Requests full-screen-exclusive capability where the active build and display path support it. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Keeps device-local constant-buffer streaming disabled for the shared-memory reference context. |

Upstream DXVK documents these semantics in its configuration reference.[^2]

## GPLAsync overlay

`tuned/renderers/dxvk-gplasync-v2.6.2-1.conf` is an **optional, version-locked overlay**. It includes `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads`, which are not a generic stock-DXVK configuration contract.

The file names the required `dxvk-gplasync-v2.6.2-1.tar.gz` artifact and its SHA-256. Use it only after verifying that exact archive, copying the 32-bit `d3d9.dll` through FusionFix’s documented Vulkan route, and recording the result in the validation evidence directory. If a log reports unsupported keys or the game becomes unstable, restore the stock file before changing any other variable.[^3]

## Change protocol

1. Back up the active `dxvk.conf` and renderer DLL before changing anything.
2. Change one renderer component or configuration file only.
3. Restart GTA IV after renderer/graphics changes.
4. Record the renderer artifact hash and effective `GTAIV_d3d9.log` lines.
5. Compare a cold pass with at least two warm passes on the same route.
6. Test GTA IV, TLAD, and TBoGT separately where installed.

Do not use a cold shader/pipeline pass as proof of a persistent performance problem. Do not stack a second wrapper, proxy, limiter, or ASI loader to hide an unsupported-key message.

## References

[^1]: [Gillian’s GTA IV optimization guide](https://gillian-guide.github.io/optimization/)
[^2]: [DXVK upstream configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)
[^3]: [DXVK GPLAsync v2.6.2-1 release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v2.6.2-1)
