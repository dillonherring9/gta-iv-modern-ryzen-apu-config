# DXVK Configuration Notes

The supplied `dxvk.conf` is included in both the Source and Release packages because it describes the intended DXVK-side behavior for the Ryzen 5 PRO 6650U/Radeon 660M profile. It is preserved unchanged in the current package and should be treated as an **optional renderer configuration**, not as a universal requirement.

## Why it matters

DXVK translates Direct3D 9 calls into Vulkan. The configuration can affect shader/pipeline compilation, CPU-to-GPU latency, frame pacing, memory reporting, and whether DXVK itself applies an additional FPS cap. These settings sit below FusionFix: FusionFix controls GTA IV’s fixes and in-game features, while `dxvk.conf` controls the translation layer when a DXVK/Vulkan DLL is actually active.

A configuration file next to a DLL is only read by that DLL’s DXVK build. It does not change native Direct3D 9, and it cannot make a missing Vulkan wrapper work. Use one renderer path at a time.

## Supplied settings

| Setting | Supplied value | Meaning and practical effect |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | Fork-specific GPL/async state-cache behavior. This is not a standard option in the current upstream DXVK reference and should be used only with a DXVK-GPLAsync/GPLALL build that documents it. |
| `d3d9.maxAvailableMemory` | `4096` | Reports 4 GiB of available memory to the D3D9 application. This is a compatibility hint, not a hard VRAM reservation. |
| `d3d9.maxFrameLatency` | `1` | Requests a maximum frame latency of one frame. It can reduce queued frames but may expose stutter if the game or driver cannot sustain the target. |
| `dxvk.numCompilerThreads` | `4` | Forces four shader compiler threads. This can reduce first-run compilation time on a six-core Ryzen mobile CPU, but may compete with GTA IV’s streaming and simulation threads. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Leaves Vulkan Graphics Pipeline Library support to DXVK and the driver. `Auto` is the safest starting point. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Disables device-local constant-buffer placement. This is a conservative compatibility choice and avoids forcing a memory placement policy on an integrated GPU. |
| `d3d9.presentInterval` | `-1` | Does not override the game’s presentation interval. This prevents DXVK from creating a second VSync policy. |
| `d3d9.maxFrameRate` | `0` | Disables DXVK’s own FPS limiter. FusionFix remains the single intended 60 FPS limiter. |

The upstream DXVK configuration reference confirms the standard meanings of `d3d9.maxFrameLatency`, `dxvk.numCompilerThreads`, `dxvk.enableGraphicsPipelineLibrary`, `d3d9.maxAvailableMemory`, `d3d9.deviceLocalConstantBuffers`, `d3d9.presentInterval`, and `d3d9.maxFrameRate`.[^1]

## Important compatibility warning

`dxvk.gplAsyncCache` belongs to the GPLAsync/GPLALL family rather than the standard upstream DXVK configuration documented above. If the installed wrapper is ordinary upstream DXVK, it may ignore that line. If it is a GPLAsync fork, the line may improve shader-cache behavior but can also cause driver-specific crashes; use the fork’s release notes and keep a rollback copy.[^2]

The profile intentionally keeps `d3d9.presentInterval=-1` and `d3d9.maxFrameRate=0`. This prevents DXVK from competing with FusionFix’s 60 FPS limiter. Do not enable Radeon Chill, RTSS, another DXVK cap, or a second frame limiter until the single-limiter profile has been tested.

## Recommended testing sequence

Start with the file unchanged and verify that the game actually loads the intended DXVK build. Check the DXVK log beside the active DLL. Test a save load, dense traffic, rain at night, an interior transition, and a vehicle using custom audio.

If the game stutters during the first run but becomes smoother after repeated loads, this is consistent with shader/pipeline cache warm-up. If the game crashes immediately, first disable only `dxvk.gplAsyncCache` by commenting it out or set it to the fork’s documented default. If frame pacing feels worse, restore `d3d9.maxFrameLatency=0` before changing the FusionFix limiter. If CPU contention is visible during compilation, test `dxvk.numCompilerThreads=2`; if compilation is smooth and the CPU has headroom, test `6` rather than increasing it blindly.

## References

[^1]: [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf) — upstream option names, supported values, and behavior.
[^2]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync) — fork-specific GPL/async cache behavior and compatibility context.
