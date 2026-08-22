# GTA IV Radeon 660M Reference Context — Version 3.0.2

> **Status:** This is a hardware-context document, not an independently reproduced benchmark report. Version 3.0.2 does not publish frame-time captures or a retained renderer log for the stated reference machine.

## Reference context

| Component | Reference context | What it means |
|---|---|---|
| Processor | AMD Ryzen 5 PRO 6650U | GTA IV, traffic, streaming, scripts, and mod content can compete with rendering for CPU time and APU package power. |
| Graphics | Radeon 660M integrated RDNA 2 graphics | Rendering uses shared system memory and memory bandwidth rather than dedicated GDDR. |
| Memory | 16 GB dual-channel system memory | Dual-channel memory is preferable for an iGPU, but Windows, GTA IV, caches, and the GPU still share one pool. |
| Game target | GTA IV Complete Edition | The project configuration targets the modern Complete Edition/FusionFix path rather than a downgraded executable. |
| Intended pacing | FusionFix 60 FPS profile | The target is a constrained frame-pacing preference, not a promise that every scene or mod list remains at 60 FPS. |

## Reasonable priorities for this class of system

The final core profile favors a limited number of settings with clear boundaries: one frame limiter, reduced shadow cost, no high-resolution shadows, no AO/fog/sun shafts, SMAA, no reflection MSAA, and optional—not automatic—rain, shader, plate, audio, and renderer changes. This is a rational starting position for shared-memory graphics, but it must be measured on the actual driver and cooling envelope.

Resolution, traffic, view distance, detail distance, weather effects, reflections, large loose textures, vehicle budgets, distant-light content, and added plugins may all change sustained frame times. A warm-cache result from a single route is not transferable until another controlled system repeats it.

## Renderer boundary

The core FusionFix profile selects the Vulkan route only after a compatible `vulkan.dll` renderer is already present. The repository supplies two manual configuration choices:

- `tuned/dxvk-stock.conf` for upstream-compatible DXVK settings.
- `tuned/renderers/dxvk-gplasync-v2.6.2-1.conf` only for the exact GPLAsync artifact named and hashed in that file.

The latter is not a generic “better DXVK” setting. See [`dxvk_configuration.md`](dxvk_configuration.md) and [`COMPONENTS_AND_COMPATIBILITY.md`](COMPONENTS_AND_COMPATIBILITY.md).

## Evidence required before a performance conclusion

A valid result must include exact driver and renderer versions, configuration hashes, redacted `GTAIV_d3d9.log` lines, a fixed route, a cold pass, at least two warm passes, and separate GTA IV/TLAD/TBoGT testing where applicable. Follow [`VALIDATION_PROTOCOL.md`](VALIDATION_PROTOCOL.md) before adding benchmark claims, compatibility tables, or hardware recommendations.

## Sources

- [AMD Ryzen 5 PRO 6650U support page](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-6000-series/amd-ryzen-5-pro-6650u.html)
- [FusionFix official repository](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
- [DXVK official repository](https://github.com/doitsujin/dxvk)
- [Gillian’s GTA IV optimization guide](https://gillian-guide.github.io/optimization/)
