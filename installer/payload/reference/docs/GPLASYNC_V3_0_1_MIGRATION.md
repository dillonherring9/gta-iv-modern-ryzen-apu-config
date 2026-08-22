# GPLAsync `v3.0-1` Migration Note

> **This is an advanced maintenance note, not a default Version 4 installer requirement.** The default Version 4 General modern APU profile keeps an upstream-compatible `dxvk.conf` and does not install or replace a renderer DLL. Version 4 offers its Radeon 660M option only after the user confirms the separate Ph42oN GPLAsync `v3.0-1` build in `GTAIV_d3d9.log`.

The published tag is named **`v3.0-1`**. It updates the fork to upstream DXVK 3.0; it is sometimes informally called “3.0.1,” but it is not the old Version 3 project release and it is not the same renderer as `dxvk-gplasync-v2.6.2-1`.[1]

## The important difference from the retained 2.6.2 reference

The existing `tuned/renderers/dxvk-gplasync-v2.6.2-1.conf` is preserved as a **historical, version-locked reference**. It belongs only to its named 2.6.2-1 artifact. It contains `dxvk.gplAsyncCache = True`, but the GPLAsync project removed that setting in `v2.7-1`; it must **not** be copied into `v3.0-1`.[2]

| Setting from the old 2.6.2 reference | Safe `v3.0-1` treatment | Why |
|---|---|---|
| `dxvk.gplAsyncCache = True` | **Remove it.** | GPLAsync states that this cache option was removed in `v2.7-1`. |
| `dxvk.enableAsync = true` | Keep it or set it explicitly. | The `v3.0-1` patch reads this option and enables asynchronous pipeline behavior. |
| `dxvk.numAsyncThreads = 4` | Do not copy it automatically. Add it only if `GTAIV_d3d9.log` confirms the exact build accepts it. | The current GPLAsync `v3.0-1` README documents `dxvk.enableAsync`, not this old thread-count key. |
| `dxvk.numCompilerThreads = 4` | Keep as a conservative starting point, then test. | This is a current DXVK compiler-thread option; it is not a GPLAsync identity marker. |
| `d3d9.numBackBuffers` | Treat as a presentation experiment only after the log confirms it is accepted. | Back-buffer count is not proof of renderer identity and is not a universal performance increase. |

## Safe minimum `dxvk.conf` for GPLAsync `v3.0-1`

Start from the Version 4 Radeon 660M/GPLAsync baseline or from the General profile only after preserving a backup. Add only the following GPLAsync-specific line after recording a backup of the working `dxvk.conf`, `vulkan.dll`, and `GTAIV_d3d9.log`:

```ini
dxvk.enableAsync = true
```

Do not add `dxvk.gplAsyncCache`. Keep `dxvk.enableGraphicsPipelineLibrary = Auto` unless a controlled test and logs provide a reason to change it. The GPLAsync project notes that Graphics Pipeline Library can use more VRAM; it does not justify changing it blindly on a shared-memory APU.[2]

## Pick one synchronization owner

Do not let DXVK, GTA IV/FusionFix, and AMD Adrenalin all try to control VSync at once. Choose exactly one of the two paths below.

| Your goal | `d3d9.presentInterval` | GTA IV / FusionFix VSync | AMD Adrenalin Enhanced Sync | Back-buffer starting point |
|---|---:|---|---|---:|
| Use **AMD Enhanced Sync** for the Vulkan game | `0` | Off | On in the `GTAIV.exe` game profile | `2`; test `3` only if the log accepts the setting and repeat-run pacing improves. |
| Use traditional DXVK-controlled VSync | `1` | Off | Off | `3` is Gillian’s documented GTA IV starting point while using VSync. |

A `d3d9.presentInterval` value of `0` disables VSync in DXVK; a positive value enables it; and `-1` leaves the application’s own policy unchanged.[3] AMD documents Enhanced Sync as supporting Vulkan games, but it is a low-latency synchronization feature rather than a guarantee of traditional VSync behavior at every frame rate.[4]

For a Radeon 660M shared-memory APU, do **not** raise `d3d9.numBackBuffers` above `3` as a casual tweak. More queued images consume shared graphics memory and can increase input latency. Change only `2` versus `3`, restart GTA IV, repeat the exact same demanding route after caches warm, and keep the value with the clearly better repeat-run result.

## Verify after every renderer change

1. Start GTA IV once, close it normally, then open `GTAIV_d3d9.log` in the game folder.
2. Confirm the renderer version and check for any `Unknown option`, `Unsupported`, or configuration-error line.
3. Hash `vulkan.dll` in PowerShell before and after a renderer change:

   ```powershell
   Get-FileHash -Algorithm SHA256 .\vulkan.dll
   ```

4. Test GTA IV, TLAD, and TBoGT where installed: one cold pass, then at least two warm passes on the same route.
5. Restore the saved `vulkan.dll` and `dxvk.conf` if startup, visual output, sustained frame time, or input response is worse.

> **Do not call GPLAsync `v3.0-1` a universally validated Version 4 renderer profile.** Version 4 offers its minimal configuration only as an explicit advanced choice; the renderer remains a separately maintained experiment until a complete evidence directory records the exact artifact hash, driver, log, cold/warm results, and rollback result.

## References

[1] [GPLAsync `v3.0-1` release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v3.0-1)

[2] [GPLAsync `v3.0-1` README](https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/v3.0-1/README.md)

[3] [DXVK configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)

[4] [AMD Enhanced Sync documentation](https://www.amd.com/en/products/software/adrenalin/software-enhancedsync.html)
