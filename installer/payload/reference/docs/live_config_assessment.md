# Version 3 Complete Package and Core Profile Assessment

Version 3 embeds the complete retained package: the safe core profile, both renderer profiles, optional component settings, legacy preload reference, documentation, sources, credits, and history. This document assesses the **core profile applied automatically** by the installer. The other embedded files remain available in `GTAIV_After_Dark_Complete_Package` because their correct deployment depends on external plugins, exact renderer artifacts, or verified shader layouts.

It replaces earlier assessment language that referred to an intermediate CHSS configuration and a log-validated GPLAsync path. The package no longer presents either statement as an active release claim.

## Actual core profile

| Area | Shipped setting | Assessment |
|---|---|---|
| Renderer selection | `GraphicsAPI=1`; installer requires FusionFix and `vulkan.dll` | The core profile is for a pre-existing, working FusionFix Vulkan route. The installer does not install or update the renderer. |
| Frame pacing | FusionFix preset 60 with accurate sleep-yield limiter; DXVK cap is not installed automatically | One intended limiter is the correct default diagnostic boundary. |
| Anti-aliasing | SMAA | A practical image-quality/performance choice for the reference iGPU context. |
| Shadows | **Soft** `ShadowFilter=4`, `ExtraDynamicShadows=1`, `HighResolutionShadows=0` | The shipped final configuration is Soft, not CHSS. It preserves a restrained secondary-shadow tier. |
| Post-processing | Ambient occlusion, volumetric fog, and sun shafts disabled | These are intentionally excluded from the core profile to reduce avoidable GPU pressure. |
| Reflections | Console car reflections enabled; Reflection MSAA disabled | Reflection MSAA remains outside the core performance profile. |
| Streaming and limits | `stream.ini` plus VehicleBudget/ExtendedLimits values in FusionFix | These are mod-stack-sensitive choices and require episode-level validation. |

## Evidence boundary

The repository does not currently include a complete `GTAIV_d3d9.log`, renderer artifact hash, fixed route telemetry, or cold/warm frame-time captures from the stated Radeon 660M machine. Therefore, this document does not assert a measured frame-rate outcome or call any renderer configuration “log-validated.”

The reference hardware remains useful context: Ryzen 5 PRO 6650U, Radeon 660M integrated graphics, and 16 GB dual-channel memory. It is not a compatibility guarantee. Shared memory, driver behavior, laptop power limits, thermals, resolution, traffic, installed content, and renderer build can materially change the outcome.

## Required validation

Use [`VALIDATION_PROTOCOL.md`](VALIDATION_PROTOCOL.md) before publishing a result. At minimum, record the exact FusionFix and renderer artifacts, active configuration hashes, a redacted renderer log, a fixed route, one cold run, two warm runs, and separate GTA IV/TLAD/TBoGT results where all episodes are installed.

## Operational rule

Keep one renderer route, one frame limiter, and one changed variable. A cold cache is not a tuning conclusion. When a renderer, driver, graphics setting, resolution, or shader/content path changes, restart GTA IV when required and compare warm passes before altering another variable.
