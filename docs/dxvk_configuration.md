# DXVK Configuration — Keep the Renderer Honest

The supplied `dxvk.conf` ships in the Source and Release packages because it describes the intended DXVK side of the Ryzen 5 PRO 6650U / Radeon 660M profile. It is preserved as tested, but it is an **optional renderer configuration**, never a universal requirement.

## What this file can—and cannot—do

DXVK translates GTA IV’s Direct3D 9 calls into Vulkan. When the correct DXVK/Vulkan DLL is active, its configuration can change shader and pipeline compilation, CPU-to-GPU latency, frame pacing, memory reporting, and whether DXVK adds another FPS cap. FusionFix handles GTA IV fixes and features; `dxvk.conf` controls the translation layer beneath it.

A configuration file beside a DLL is read only by that DLL’s DXVK build. It does not change native Direct3D 9, and it cannot wake up a missing Vulkan wrapper. Keep one renderer path active at a time.

## Supplied settings

| Setting | Supplied value | Meaning in this profile |
|---|---:|---|
| `dxvk.gplAsyncCache` | `True` | Fork-specific GPL/async state-cache behavior. It is not in the current standard upstream DXVK reference; use it only with a DXVK-GPLAsync/GPLALL build that documents it. |
| `d3d9.maxAvailableMemory` | `4096` | Reports 4 GiB to the D3D9 application. It is a compatibility hint, not a hard VRAM reservation. |
| `d3d9.maxFrameLatency` | `1` | Requests one queued frame. It can reduce latency, but it can reveal rough pacing if the game or driver cannot hold the target. |
| `dxvk.numCompilerThreads` | `4` | Uses four shader compiler threads. This can shorten first-run compilation, but it may compete with GTA IV streaming and simulation on a mobile Ryzen CPU. |
| `dxvk.enableGraphicsPipelineLibrary` | `Auto` | Lets the driver and DXVK choose a compatible Graphics Pipeline Library route. |
| `d3d9.deviceLocalConstantBuffers` | `False` | Avoids forcing a device-local memory policy on integrated graphics. |
| `d3d9.presentInterval` | `-1` | Leaves presentation interval to the game instead of creating a second VSync policy. |
| `d3d9.maxFrameRate` | `0` | Turns off DXVK’s own limiter so FusionFix remains the single intended 60 FPS limiter. |

The standard meanings of the upstream options are documented in DXVK’s configuration reference.[^1]

## **Cache reality for an APU**

> **Shader and pipeline caches have to build. On a shared-memory APU, a cold cache can hitch when you enter a new area, trigger a new effect, change weather, or use a new renderer path. This is expected during warm-up, and there is no setting that makes the cache-building phase disappear.**
>
> **Changing in-game graphics settings, resolution, DXVK options, drivers, renderer selection, or significant mods can make old cache entries irrelevant. Some changes require a full GTA IV restart. If you leave the game open after a setting change that needs a restart, it can stutter until you relaunch it.**

The right method is restrained: make one change, restart when required, replay the same demanding route, then judge the second or third pass. Do not tune against a first-run hitch by changing five other settings.

## Compatibility warning

`dxvk.gplAsyncCache` belongs to GPLAsync/GPLALL-style builds rather than the standard upstream DXVK configuration. Ordinary upstream DXVK can ignore that line. A compatible GPLAsync fork may improve cache behavior, but it can also create driver-specific crashes. Keep a rollback copy and use the fork’s release notes.[^2]

The profile intentionally uses `d3d9.presentInterval=-1` and `d3d9.maxFrameRate=0` so DXVK does not compete with FusionFix’s 60 FPS limiter. Do not add Radeon Chill, RTSS, another DXVK cap, or a second frame limiter while validating the single-limiter profile.

## A clean testing sequence

Start with the file unchanged and verify that the game loads the intended DXVK build by checking the DXVK log next to the active DLL. Test a save load, dense traffic, rain at night, an interior transition, and a vehicle with custom audio.

| Symptom | First move |
|---|---|
| Stutter only on the first run, then smoother repeat runs | Let the shader/pipeline cache warm up. This is consistent with compilation work. |
| Stutter after graphics, resolution, renderer, driver, or DXVK changes | Restart GTA IV, then replay a demanding route until the new cache has settled. |
| Immediate crash | Comment out only `dxvk.gplAsyncCache` or return it to the fork’s documented default. |
| Rough pacing with `maxFrameLatency=1` | Restore `d3d9.maxFrameLatency=0` before changing FusionFix’s limiter. |
| CPU contention during compilation | Test `dxvk.numCompilerThreads=2`. If the CPU is genuinely clear, test `6`—do not increase blindly. |

## References

[^1]: [Official DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf) — upstream option names, supported values, and behavior.
[^2]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync) — fork-specific GPL/async cache behavior and compatibility context.
