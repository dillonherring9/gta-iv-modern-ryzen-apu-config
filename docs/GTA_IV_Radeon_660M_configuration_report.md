# GTA IV Complete Edition on Ryzen 5 PRO 6650U / Radeon 660M

**Prepared by Manus AI**  
**Scope:** GTA IV: Complete Edition, the installed Gillian drag-and-drop archive, FusionFix 5.0.1, and a Ryzen 5 PRO 6650U laptop with Radeon 660M integrated graphics and **16 GB dual-channel system memory**.

## Executive assessment

The Radeon 660M is fully capable of running the **modernized GTA IV: Complete Edition** path, but it is an integrated RDNA 2 GPU operating within a 15–28 W APU package and using shared system memory. It is therefore better suited to a **well-balanced 60 fps profile** than to an all-options-maxed profile with every archive enhancement and every FusionFix post-processing effect enabled. GTA IV itself is a legacy Direct3D 9-era, 32-bit x86 game design whose CPU simulation, streaming, visibility, draw-call behavior, frame pacing, and shader setup can matter as much as raw GPU capability.

The installed archive is designed for a vanilla-faithful enhanced experience rather than highest possible performance. Its own guide explicitly warns that performance will noticeably decrease, packages FusionFix 5.0.1 and other improvements, and directs the user to the Vulkan/DXVK path when it is stable.[11] The right approach is to preserve that package as one tested unit, enable **FusionFix’s Vulkan route** if stable, use **one 60 fps limiter**, retain high texture quality, and control the compounded costs of reflection resolution, shadows, ambient occlusion, volumetric fog, distant lights, long visibility distances, and high traffic.

> **Recommendation:** Start with the **Balanced 60** profile below. Do not install a separate FusionFix, DXVK, ASI loader, ENB, ReShade, or unrelated graphics mod over the archive. First establish stable frame pacing; only then add visual effects one at a time.

## 1. Release context, hardware era, and PC architecture

The table separates the original PC port from the **Complete Edition** re-release. These are frequently conflated, yet they represent very different Windows and hardware baselines.

| Item | Original GTA IV PC port | GTA IV: Complete Edition |
|---|---|---|
| PC release period | Contemporary reporting described a 2 December 2008 retail date.[3] | Rockstar released the combined Complete Edition through Rockstar Games Launcher and Steam on **24 March 2020**.[1] |
| Included content | GTA IV PC release. | GTA IV plus *Episodes from Liberty City*; the update removed the former Games for Windows Live requirement for Steam owners.[1] |
| Supported Windows baseline at release/current listing | Windows XP SP3 or Windows Vista SP1.[3] | Rockstar’s current requirements list **Windows 10 64-bit**.[2] |
| CPU baseline | Minimum Core 2 Duo 1.8 GHz or Athlon X2 64 2.4 GHz; recommended Core 2 Quad 2.4 GHz or Phenom X3 2.1 GHz.[3] | Minimum Core 2 Quad Q6600 or Phenom 9850-class quad core.[2] |
| Graphics baseline | Minimum 256 MB GeForce 7900 or Radeon X1900; recommended 512 MB GeForce 8600 or Radeon HD 3870.[3] | Rockstar lists a 512 MB GeForce 8600 or Radeon HD 3870-class card.[2] |
| Rendering generation | Legacy Direct3D 9-era PC rendering, implemented by a 32-bit x86 game process. | Still a legacy D3D9-style game path, with FusionFix able to offer a Vulkan route through DXVK.[8] [9] |

The original target hardware was vastly older than this APU. That does **not** make every setting free: GTA IV’s engine is particularly sensitive to scene complexity, CPU-side submission/streaming, and frame-time consistency. Moreover, Rockstar’s current support page cautions that some integrated or mobile graphics devices may be incompatible; that is a general support disclaimer, not a statement that the 660M cannot run the game.[2]

## 2. How GTA IV’s graphics are processed

Rockstar has not publicly published the full proprietary RAGE rendering source, so the following describes the verified Direct3D 9 execution model that GTA IV uses, the publicly documented AMD execution model, and FusionFix/DXVK’s documented API path. It should not be read as a claim that every internal RAGE subroutine is known.

| Stage | Native Direct3D 9 route | FusionFix Vulkan route | Importance on this laptop |
|---|---|---|---|
| CPU and engine work | The game performs simulation, streaming, visibility decisions, draw-call preparation, resource state changes, and Direct3D 9 API submission. | The same game logic and D3D9 submission occur. | High traffic, long view/detail distances, and modded assets can make this CPU- or streaming-limited even with a capable GPU. |
| API translation/driver work | The AMD Windows D3D9 driver receives legacy D3D9 calls and maps them to the modern driver/hardware stack. | DXVK translates D3D9 work into Vulkan resources, pipelines, and command submission. DXVK is a Vulkan-based layer for D3D8/9/10/11; FusionFix exposes DirectX 9 or Vulkan in its Graphics API option.[8] [9] | Vulkan may reduce legacy driver overhead or improve pacing, but is not guaranteed to be better on every driver/build. Retain it only after a stability test. |
| Geometry and raster work | Vertex shaders transform model vertices; primitives are assembled and rasterized. | The equivalent GPU work is represented through Vulkan pipelines/commands. | Long draw distance, more vehicles, restored vegetation, and high-detail archives increase geometry/raster workload. |
| Pixel shading and effects | Pixel shaders texture and shade fragments; fog, stencil, blending, depth and render-target operations follow. Microsoft documents these D3D9 stages, including vertex-to-pixel interpolation and post-pixel fog/stencil/blending operations.[4] | The Vulkan pipeline runs the translated shader work on the same GPU. | Reflections, high-quality water, anti-aliasing, shadows, ambient occlusion, volumetric fog, sun shafts, and distant lights raise render-target, texture, fill-rate, and bandwidth cost. |
| Presentation | The completed frame is presented through Windows’ display path. | DXVK manages Vulkan presentation; FusionFix supplies its own FPS limiter and windowing options.[9] | A stable 60 fps cap is preferable to an unstable 70–100 fps rate because GTA IV retains timing-sensitive behavior at high frame rates.[12] |

Microsoft’s Direct3D 9 documentation explains the key programmable relationship: vertex shaders produce clip-space position and optional texture/color data; rasterization interpolates data; pixel shaders operate on individual pixels; and fog, stencil, and render-target blending occur after the pixel shader.[4] That is the conceptual graphics pipeline GTA IV presents to its driver.

## 3. How the Radeon 660M executes that work

AMD identifies the Ryzen 5 PRO 6650U as a **Zen 3+ / Rembrandt** APU with six CPU cores, twelve threads, a 2.9 GHz base clock, up to 4.5 GHz boost, and a 15–28 W configurable TDP. Its Radeon 660M has **six graphics cores, 384 graphics shaders, and up to 1.9 GHz graphics frequency**.[5] It is an **RDNA 2** generation integrated GPU; AMD describes RDNA 2 as providing enhanced compute units, a revised visual pipeline, first-generation ray tracing accelerators, and Infinity Cache technology in the broader architecture family.[6]

| Radeon 660M execution element | What happens | GTA IV implication |
|---|---|---|
| Command processor and scheduling | AMD documents the command processor as the CPU/GPU interface that receives and distributes work. It dispatches work to the GPU’s shader resources.[7] | High draw-call/state-change games can expose CPU/API overhead. This is the principal reason the DXVK/FusionFix Vulkan option is worth testing. |
| Shader resources | AMD documents a compute unit as the fundamental parallel execution block. RDNA uses 32-thread wave execution, running many threads in lockstep while switching to other work when one group waits on memory.[7] | GTA IV’s translated vertex/pixel shader work, plus FusionFix’s SMAA, fog, AO, shadows, and post effects, runs across the 660M’s shader resources. |
| Textures, caches, and system memory | Caches serve shader instructions/data, while misses and streaming must use memory. An integrated GPU does not have its own discrete GDDR VRAM pool; it shares the laptop’s RAM and memory bandwidth with the CPU. | The user’s **16 GB dual-channel** memory is a material strength, but it is not equivalent to 16 GB of discrete VRAM. Texture packs and multiple full-resolution render targets can still cause contention. |
| Power/thermals | CPU and GPU share the mobile APU power/thermal envelope. The 6650U is configurable from 15 to 28 W.[5] | Heavy traffic can boost CPU demand while high reflections/AO/fog boost GPU demand. On battery or under thermal restriction, the system may lower clocks and cause inconsistency. |
| Memory-channel effect | AMD supports dual-channel DDR5-4800 or LPDDR5-6400 configurations.[5] Their theoretical dual-channel peak is approximately **76 GB/s** and **102 GB/s**, respectively, before efficiency losses. | Dual channel substantially improves the 660M’s available bandwidth. The actual installed memory speed remains unverified because the remote desktop inventory produced no output. |

The practical conclusion is that the 660M can comfortably satisfy the original 2008-era specification, but the archive turns GTA IV into a more modern and bandwidth-sensitive workload. Effects that add full-screen passes or extra shadow/reflection targets should be treated as optional quality layers rather than default requirements.

## 4. Verified inputs and verification limitation

| Input | Status | Consequence for the plan |
|---|---|---|
| Ryzen 5 PRO 6650U / Radeon 660M | Provided by the user and matched to AMD’s product specification.[5] | Uses the 660M-specific integrated-GPU profile below. |
| 16 GB system memory | Provided by the user. | Adequate capacity for Windows, the game, and the archive, provided background software remains reasonable. |
| Dual-channel memory | Confirmed by the user. | Avoids the most damaging iGPU memory-bandwidth bottleneck. |
| Gillian drag-and-drop archive | Project instruction says installed; archive documentation reviewed.[11] | Keep its included FusionFix 5.0.1/DXVK ecosystem intact; do not layer duplicates. |
| Exact Windows version, driver build, display resolution/refresh, memory speed, live game-file tree, and current config | **Not yet verified.** Multiple read-only desktop inventory commands produced no output and were stopped; no system or game file was changed. | The display-dependent resolution recommendation and exact driver advice remain conditional. |

The user has granted system-access authorization. The limitation is technical connectivity, not a permission issue. Once the connected desktop responds, the first read-only checks should be the Windows build, Radeon driver version, memory speed, active resolution/refresh rate, power plan, and the actual FusionFix/DXVK/launcher files. The report is deliberately conservative until those are confirmed.

## 5. Recommended loading order and installation discipline

The archive is already an integrated package. It should **not** be treated like a modular mod list that benefits from manually shuffling every file. FusionFix itself supplies file-overload and IMG-loading facilities, while the guide warns against adding unlisted mods and against enabling automatic FusionFix updates.[9] [11]

| Order | Required action | Reason |
|---|---|---|
| 1. Base game | Use a clean GTA IV: Complete Edition **1.2.0.59** installation as the archive’s foundation. | The archive explicitly requires a clean, non-downgraded copy.[11] |
| 2. Archive payload | Retain the archive’s own installed files in the directory containing `GTAIV.exe`. | The archive is designed as one ready-to-play payload; manual piecemeal rearrangement creates unsupported conflicts.[11] |
| 3. Loader layer | Keep **one** ASI/proxy loader path supplied by the archive. Do not add a second `dinput8.dll`, `d3d9.dll`, `dxgi.dll`, or another injector. | Duplicate proxy/loader DLLs are a common cause of a game that fails to start or behaves unpredictably. FusionFix itself is installed at game root.[9] |
| 4. FusionFix version | Keep the bundled **FusionFix 5.0.1**. Do not update it separately while validating performance. | The archive is known to bundle 5.0.1; the guide says not to enable FusionFix update checks.[11] [10] |
| 5. DXVK/API layer | Use only the archive/setup-utility DXVK path for Vulkan testing. Do not overlay a standalone DXVK release. | The guide’s Setup Utility is its designed DXVK/launch-option recovery route.[11] |
| 6. Update/content layer | Leave the archive’s own update/asset ordering unchanged. Enable archive-specific optional content only by its documented method. | For example, the archive documents the optional Potential Grim image rename separately; it is not a performance prerequisite.[11] |
| 7. Launch layer | Launch from Steam, Rockstar Games Launcher, or `PlayGTAIV.exe`, as the archive directs. | This preserves the expected launcher and DLC behavior.[11] |

The most important “load order” decision is therefore **exclusion**: no competing renderer wrapper, no duplicated DXVK, no separate FusionFix release, no standalone script hook unless the archive specifically includes it, and no visual overhaul layered on top until the baseline has been measured.

## 6. Balanced 60 profile — recommended starting configuration

This profile is designed to keep the archive’s visual character while respecting an iGPU’s shared bandwidth and the 6650U’s shared package power. It is a starting point, not a claimed benchmark result.

### 6.1 Windows and launcher setup

Use AC power for play and select Windows’ best-performance power policy for the session. Keep background browser tabs, video capture, game launchers, and overlays to a minimum while validating. AMD recommends the laptop manufacturer’s validated graphics driver where available; those drivers can include platform-specific compatibility and power tuning.[5]

If using the **Vulkan/DXVK** route, set the game’s Graphics API to **Vulkan**, restart when prompted, and use FusionFix’s windowing options: enable **Windowed** and **Windowed Borderless**. Gillian’s guide makes the same borderless recommendation for FusionFix.[12] If Vulkan causes a crash, follow the archive’s recovery instruction—delete `d3d9.cfg` and do not reselect the API—then operate with native DirectX 9 while troubleshooting.[11]

For Complete Edition with FusionFix/DXVK, retain only these minimal launcher arguments if the archive’s setup path has not already provided them:

```text
-norestrictions -nomemrestrict
```

Do **not** use `-managed` with DXVK. Do **not** add an invented/oversized `-availablevidmem` value: Gillian notes that the override is unnecessary with FusionFix 4.0.0 or later, and FusionFix lists fixes for VRAM detection.[9] [12] If falling back to native DirectX 9, the archive/setup utility’s own established arguments are safer than hand-building a new list.

Use a **single limiter**. Set the in-game VSync option **Off** when using DXVK and set FusionFix’s FPS Limiter to **60**. Do not simultaneously add a DXVK `d3d9.maxFrameRate` cap, Radeon Chill cap, RTSS cap, and a FusionFix cap. Gillian recommends 60 or 30 fps to avoid timing-related issues; a 60 fps FusionFix cap meets that aim without double-limiting.[12]

### 6.2 In-game core graphics settings

| Setting | Balanced 60 starting value | Why |
|---|---|---|
| Video mode | Native resolution **if 1920×1080 or lower**. If the internal laptop display is above 1080p, begin at 1920×1080. | Limits pixel and render-target cost while preserving a sharp image. Actual display resolution is pending verification. |
| Aspect ratio | Auto | Matches the active display mode. |
| Texture Quality | High | 16 GB dual-channel system RAM makes high textures reasonable; do not confuse this with unlimited dedicated VRAM. |
| Reflection Resolution | High | Very High adds a meaningful bandwidth/render-target cost; raise only after testing. |
| Water Quality | High | Preserves quality without starting at the most demanding water-reflection mode. |
| Shadow Quality | High | Avoids the higher cost of Very High while preserving more dynamic quality than low settings. |
| Night Shadows | Medium | Extra local shadow maps are expensive; the guide also warns high levels can create artifacts.[12] |
| Extra Night Shadows | Off | FusionFix describes it as an extremely broken original PC feature and does not recommend it.[9] |
| Texture Filter Quality | Anisotropic 16× | High visual value and normally low cost on modern GPUs. |
| View Distance | 35 | A controlled starting point that reduces CPU, geometry, streaming, and bandwidth pressure compared with extreme values. |
| Detail Distance | 25 | Keeps city detail convincing without compounding visibility cost. |
| Vehicle Density | 100 with the archive’s custom popcycle; reduce to 80 then 70 if traffic/streaming causes stutter. | The archive specifically says 100 to use its custom popcycle, but traffic is the first CPU/streaming dial for an APU.[11] |
| Anti-aliasing | SMAA | A comparatively light post-process option; avoid expensive multisampling initially. |
| FPS Limiter | 60 | Stable timing should take priority over a higher unstable rate.[12] |

### 6.3 FusionFix visual settings

| FusionFix option | Balanced 60 starting value | Rationale |
|---|---|---|
| Graphics API | Vulkan, if stable | The guide recommends Vulkan when possible; revert to D3D9 if it crashes.[11] [12] |
| Volumetric Fog | Off initially | The guide identifies it as performance-heavy.[12] |
| Sun Shafts | Off initially | A discretionary full-screen visual effect. |
| Extended Sunlight Reach | Off initially | Potentially performance-heavy; enable only after baseline testing.[12] |
| Tone Mapping | On | Improves highlight handling with low expected incremental cost.[12] |
| Ambient Occlusion | Off initially | FusionFix 5.0.1 native AO improves grounding but adds several depth/post-processing passes.[10] |
| Shadow Filter | Soft | Avoids the sharper/possibly heavier CHSS path; FusionFix notes CHSS only takes effect with Very High shadows.[9] |
| Distant Lights | Default initially | Project2DFX-style distant lights can increase visible scene complexity; test later. |
| Definition | On | Recommended by the guide for resolutions above 1280×720 to avoid the PC blur behavior.[12] |
| Console Gamma | On or preference | Visual preference; FusionFix documents it as restoring Xbox 360-style contrast.[9] |
| Console Auto Exposure | Preference | Visual choice; no need to enable during baseline testing. |
| Depth of Field | Low or cutscenes only | Preserves cinematics without making normal gameplay unnecessarily soft. |
| Motion Blur | Preference, default Off | Image preference; keeping it off makes performance/clarity evaluation easier. |
| Tree Lighting / Tree Alpha | PC+ | Sensible visual balance. Use Console only if the archive’s console vegetation assets are retained. |
| Reflection MSAA | Off | FusionFix warns that this experimental option can behave differently or break by API, GPU vendor driver, or DXVK version.[10] |

## 7. Controlled uplift and fallback profiles

Do not enable several quality features in one change. Test each change in the same demanding scenario—fast driving through dense traffic, rain/night lighting, and a bridge or high-visibility district—before retaining it.

| Profile | Apply when | Changes from Balanced 60 |
|---|---|---|
| Quality 60 | The Balanced 60 profile is consistently stable with low fan/thermal concern. | Raise Reflection Resolution to Very High; enable **one** of Ambient Occlusion, Volumetric Fog, or Project2DFX distant lights; test for 15–30 minutes. Keep shadows at High before considering Very High. |
| Visual showcase 30 | Stable visual fidelity is preferred to response/pacing at 60 fps. | Set FusionFix limiter to 30; raise shadows and reflections cautiously; enable AO or fog, but still avoid Extra Night Shadows and Reflection MSAA. |
| Performance 60 | There are recurring drops, stutter, or sustained heat/fan throttling. | Use 1600×900 or 1280×720 if needed; reflection/water Medium; shadows Medium/High; view 25; detail 15; traffic 70; all optional FusionFix post-processing Off. |
| Native D3D9 fallback | Vulkan crashes, visual corruption persists, or the Vulkan route is less stable on the actual driver. | Select DirectX 9, restore the archive’s normal launch configuration, turn in-game VSync On, retain the FusionFix 60 cap, and continue with Balanced settings. |

## 8. Validation order and fault isolation

Run the built-in GTA IV benchmark only as a comparative first check; a real driving session is more valuable. Enable FusionFix’s FPS counter during diagnosis. A stable result is a consistent frame-time experience, no texture eviction/pop-in spike, no crash on reload, and no sustained clock/thermal collapse—not merely a high average FPS.

| Test order | Action | Interpretation / next action |
|---|---|---|
| 1 | Launch the unmodified installed archive with Balanced 60 settings. Begin a new save if practical. | Confirms the archive’s intended baseline. |
| 2 | Test Vulkan, 60 fps limiter, borderless mode, and a dense daytime drive. | If stable, keep this rendering path. If it crashes, use the archive’s D3D9 recovery instruction and stop altering graphics API settings. |
| 3 | Test rain/night, several minutes of fast driving, and loading a save. | If stutter occurs during traffic/streaming, reduce Vehicle Density, View Distance, then Detail Distance—in that order. |
| 4 | Add one visual feature. | If sustained FPS drops or frame-time spikes appear, disable that same feature before changing unrelated settings. |
| 5 | Test after a cold launch and after 30 minutes on AC. | A late performance decline points more to thermal/power contention than a simple setting mismatch. |
| 6 | Only after a stable profile is established, consider archive-supported optional content. | Preserve a copy of the known-good config first. |

## 9. What I have and have not changed

I performed **research and read-only inspection attempts only**. I did **not** edit GTA IV, FusionFix, DXVK, launcher arguments, Windows settings, drivers, registry values, or power settings. The desktop shell connection did not return output for even a minimal read-only command, so I could not verify the current installed FusionFix config or automatically tailor the report to the actual display/driver state.

The user has stated that Git is installed. That is useful for maintaining a versioned copy of a confirmed working configuration, but it is not needed for normal play and should not be used to alter the game installation until the correct files are visible and a restore point/backup is made.

## References

[1]: https://www.rockstargames.com/newswire/article/75o94113117o24/GTAIV-Complete-Edition-Now-Available-on-the-Rockstar-Games-Launcher-an "Rockstar Games — GTA IV: Complete Edition now available"
[2]: https://support.rockstargames.com/articles/7BOteXZsOSvgaEsyzz743i/grand-theft-auto-iv-pc-system-requirements "Rockstar Support — Grand Theft Auto IV PC system requirements"
[3]: https://www.wired.com/2008/10/gta-iv-pc-delay/ "WIRED — GTA IV PC Delayed, System Requirements Revealed"
[4]: https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9 "Microsoft Learn — Writing HLSL Shaders in Direct3D 9"
[5]: https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-6000-series/amd-ryzen-5-pro-6650u.html "AMD — Ryzen 5 PRO 6650U specifications and drivers"
[6]: https://www.amd.com/en/technologies/rdna.html "AMD — RDNA architecture"
[7]: https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html "AMD ROCm — GPU hardware implementation"
[8]: https://github.com/doitsujin/dxvk "DXVK — Vulkan-based translation layer"
[9]: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix "FusionFix — GTA IV Complete Edition documentation"
[10]: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases "FusionFix 5.0.1 release notes"
[11]: https://gillian-guide.github.io/drag-and-drop-archive/ "Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive"
[12]: https://gillian-guide.github.io/additional-setup/ "Gillian’s GTA IV Modding Guide — Additional Setup and optimal graphics settings"
[13]: https://gillian-guide.github.io/essential-modding/fusionfix/ "Gillian’s GTA IV Modding Guide — FusionFix"
