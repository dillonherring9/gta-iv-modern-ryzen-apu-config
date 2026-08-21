# DXVK Configuration — The Signal Behind the Streetlights

The supplied `dxvk.conf` is the renderer side of the Ryzen 5 PRO 6650U / Radeon 660M profile. Version 3 does not present it as a universal magic file. It records the settings that the actual GTA IV runtime log accepted on the intended **DXVK GPLAsync v2.6.2-style path**.

DXVK translates GTA IV’s Direct3D 9 calls into Vulkan. When the correct DXVK/Vulkan DLL is active, `dxvk.conf` can influence shader and pipeline work, CPU-to-GPU latency, frame pacing, memory reporting, and whether DXVK adds another FPS cap. FusionFix handles GTA IV fixes and features; this file governs the translation layer beneath it. A configuration file beside a DLL is read only by that DLL’s DXVK build. It does not change native Direct3D 9 and cannot wake up a missing Vulkan wrapper.

> **Keep the path clean:** one renderer, one ASI-loader route, and one intentional frame limiter. A second wrapper or cap can turn a clean log into a long night of guessing.

## The Version 3 profile

| Setting | Version 3 value | Why it is here |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | The runtime log accepted this GPLAsync cache behavior. It is build-specific, not a stock-upstream guarantee. |
| `dxvk.enableAsync` | `true` | Restored because the active GPLAsync v2.6.2-style runtime reported it as accepted. |
| `dxvk.numAsyncThreads` | `4` | Restored to the working async-worker balance reported in the log. |
| `dxvk.numCompilerThreads` | `4` | Keeps four compiler workers on a six-core mobile APU, leaving room for GTA IV streaming, traffic, scripts, and Windows. |
| `d3d9.maxAvailableMemory` | `4096` | A 4 GiB compatibility hint for the 32-bit D3D9 application, not a reserved VRAM pool. |
| `d3d9.maxFrameLatency` | `1` | Requests a one-frame queue for lower latency; test it only if repeatable pacing problems remain. |
| `d3d9.maxFrameRate` | `0` | Leaves DXVK’s explicit cap off so FusionFix owns the intended 60 FPS target. |
| `d3d9.presentInterval` | `-1` | Leaves the presentation interval to the game rather than layering a second VSync policy. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | The Radeon 660M log confirmed Graphics Pipeline Library support; Auto lets DXVK and the driver choose the compatible path. |
| `dxvk.allowFse` | `true` | The log confirmed full-screen-exclusive support on the active Radeon route. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Avoids forcing device-local placement on an integrated GPU that shares system memory. |

## Cache reality on a shared-memory APU

Shader and pipeline caches have to build. The first pass through new traffic, weather, lighting, a higher-resolution loose asset, or a changed render path can hitch while the renderer learns the work. A changed graphics setting, resolution, DXVK option, driver, renderer, or major mod can make earlier cache entries irrelevant. Restart GTA IV when the change calls for it, replay the same demanding route, then judge the second or third pass.

The tested high-resolution replacement remains a **loose-file** mod. Keep it loose. The `update`-folder alternative can leave the base story alive while making EFLC crash at launch. If the loose assets pressure the Radeon 660M too hard, lower output resolution one sensible step before raising DXVK thread counts.

## Compatibility boundary

Version 3 targets the renderer behavior shown in the log: a DXVK GPLAsync v2.6.2-style build that accepts `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads`. Those three values are **not** a universal stock DXVK profile. If a standard upstream DXVK build reports them as unknown, retain the standard settings and remove only those build-specific lines. Do not add another renderer DLL to silence the message.

The profile intentionally stays at four compiler and four async workers. Raising both at once does not make GTA IV a modern multi-threaded game; it can instead pull CPU time away from traffic, scripts, streaming, and Windows during cache work. Treat a different count as a one-variable experiment after the profile has warmed and only if repeatable evidence says the CPU is clear.

## A clean test route

Start with this file unchanged. Confirm `GTAIV_d3d9.log` reports the intended DXVK build, the expected effective configuration, and the intended Radeon device. Then test a save load, dense daytime traffic, a rainy night route, an interior transition, a vehicle with custom audio, and both EFLC episodes where installed.

| Symptom | First move |
|---|---|
| Stutter only on the first route, then smoother repeat runs | Let the cache warm. Do not tune against a first-run hitch. |
| Stutter after a graphics, resolution, renderer, driver, or DXVK change | Restart GTA IV, then replay the same demanding route until the cache settles. |
| Immediate crash with the GPLAsync path | Comment out `dxvk.gplAsyncCache`, then `dxvk.enableAsync` / `dxvk.numAsyncThreads` only if the first step does not resolve it. |
| Rough pacing with `maxFrameLatency=1` | Restore `d3d9.maxFrameLatency=0` before changing FusionFix’s limiter. |
| Loose high-resolution assets push the system too hard | Reduce output resolution before trimming texture quality; then test rain and EFLC again. |

## References

[1]: [DXVK upstream configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf).

[2]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync).

[3]: [Gillian’s GTA IV optimization guide](https://gillian-guide.github.io/optimization/).
