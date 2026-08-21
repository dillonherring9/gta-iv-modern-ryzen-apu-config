# GTA IV Radeon 660M Configuration Report

> **Scope:** GTA IV Complete Edition, Gillian’s Drag-and-Drop Archive, FusionFix 5.0.1, a Ryzen 5 PRO 6650U laptop with Radeon 660M integrated graphics, and **16 GB dual-channel system memory**.

## Executive read

The Radeon 660M can carry a modernized GTA IV path, but it is still an RDNA 2 integrated GPU living inside a 15–28 W APU package and sharing system memory with the CPU. That means this machine looks better in a balanced 60 FPS profile than in an all-settings-maxed stack with every archive enhancement and every FusionFix effect turned loose at once.

GTA IV is a legacy D3D9-era, 32-bit game. Its traffic, streaming, simulation, visibility, draw calls, frame pacing, and shader behavior can matter as much as raw GPU power. The archive is built for an enhanced, vanilla-faithful experience, not the highest possible frame rate. Keep it as one tested unit, use the FusionFix Vulkan route only when stable, run one 60 FPS limiter, keep textures high, and control the cost of reflections, shadows, fog, AO, long view distances, and traffic.[11]

> **APU cache rule:** shaders and pipelines must build. A fresh route, a changed renderer, a new graphics setting, a driver update, or a new visual mod can produce cache-building stutter. Repeat the same route after the change and restart GTA IV when the setting requires it. **There is no configuration trick that lets a shared-memory APU avoid this work.**

| Recommendation | Reason |
|---|---|
| Begin with **Balanced 60** | Establish a cache-warmed, repeatable baseline before chasing visual uplift. |
| Keep one renderer and loader path | A second DXVK, FusionFix, ASI loader, ENB, ReShade, or random proxy makes every stutter and crash harder to isolate. |
| Change one quality layer at a time | A controlled scene and a repeat run show the truth; several changes at once only create noise. |
| Restart when the game or setting asks for it | Continuing without the restart can leave the changed path stuttering until it resets. |

## 1. The hardware and the game

| Item | What it means for this profile |
|---|---|
| GTA IV Complete Edition | Rockstar re-released the combined Complete Edition on 24 March 2020, but its rendering behavior still comes from an older PC design.[1] |
| Game workload | Traffic, long draw distances, modded assets, streaming, and frame pacing can become CPU- or bandwidth-sensitive long before an FPS counter tells the whole story. |
| Ryzen 5 PRO 6650U | Six Zen 3+ cores, twelve threads, and a configurable 15–28 W package provide solid CPU headroom, but CPU and GPU share power and thermal space.[5] |
| Radeon 660M | Six RDNA 2 graphics cores / 384 shaders, sharing laptop memory instead of carrying its own discrete GDDR pool.[5] |
| 16 GB dual-channel memory | A real strength for an iGPU. It improves bandwidth, but Windows, GTA IV, mods, caches, and the GPU still share the same reservoir. |

The original game was built for hardware far older than the 660M. That does not make every option free. This engine is sensitive to scene complexity, CPU-side submission, streaming pressure, and frame-time consistency. Rockstar also gives general compatibility warnings for some integrated and mobile graphics devices; that is a support disclaimer, not proof that the 660M cannot run the game.[2]

## 2. What the renderer is doing

The full internal RAGE renderer is proprietary, so this report stays with the documented D3D9, DXVK, FusionFix, and AMD behavior rather than inventing details about code no one can see.

| Stage | Native D3D9 route | FusionFix Vulkan route | Why it matters on this laptop |
|---|---|---|---|
| CPU and engine work | Simulation, streaming, visibility, state changes, and D3D9 submission run in the game. | The same game work still runs before translation. | Traffic, long visibility, and mod assets can make a scene CPU- or streaming-limited. |
| API and driver work | The AMD D3D9 driver handles legacy calls. | DXVK translates D3D9 work to Vulkan resources, pipelines, and command submission.[8] | Vulkan can improve overhead or pacing, but only keep it after a real stability test. |
| Geometry and raster | Models are transformed, assembled, and rasterized. | The equivalent work is represented in Vulkan pipelines and commands. | Vehicles, vegetation, and long draw distance increase geometry and bandwidth pressure. |
| Pixel shading and effects | Textures, fog, stencil, blending, depth, and render targets work through D3D9. | The translated pipeline runs on the same Radeon GPU. | Reflections, high water, AA, shadows, AO, fog, shafts, and lights raise fill-rate and memory cost. |
| Presentation | Frames enter the Windows display path. | DXVK presents through Vulkan; FusionFix handles limiter and windowing options. | A stable 60 FPS cap is better than an erratic higher rate in a timing-sensitive game.[12] |

Microsoft’s Direct3D 9 documentation describes the broad relationship here: vertex shaders create positions and data, rasterization interpolates it, pixel shaders operate per pixel, and fog, stencil, and blending follow in the output path.[4]

## 3. Why the 660M needs balance

AMD identifies the 6650U as a Zen 3+ / Rembrandt APU with a Radeon 660M running up to 1.9 GHz.[5] The Radeon is capable enough for the original game, but the archive turns GTA IV into a more modern, bandwidth-sensitive workload. Full-screen post effects, high-cost shadow targets, high-resolution reflections, and large assets are quality layers—not automatic defaults.

| Pressure point | What it means | Move first if it stays rough after cache warm-up |
|---|---|---|
| Shared memory | The GPU borrows system RAM and bandwidth instead of using separate VRAM. | Lower reflection resolution or output resolution before cutting texture quality. |
| Shared package power | Traffic can push CPU load while fog/AO/shadows push GPU load. | Use AC power, watch thermals, then cut traffic or the heaviest effects. |
| Streaming and visibility | Long draw distances, traffic, vegetation, and modded assets create constant movement through memory. | Lower Vehicle Density, View Distance, then Detail Distance. |
| First-run compilation | New shaders and pipelines have to be discovered and cached. | Replay the route; do not react to a cold-cache hitch with five unrelated edits. |

## 4. Keep the installation clean

The archive is an integrated package, not a pile of files that gets better when everything is manually reshuffled. FusionFix provides file overload and IMG-loading capabilities; the archive cautions against unlisted mods and automatic FusionFix updates.[9] [11]

| Order | Required action | Why it matters |
|---|---|---|
| 1. Base game | Begin with a clean GTA IV Complete Edition 1.2.0.59 install. | The archive expects a clean, non-downgraded foundation.[11] |
| 2. Archive payload | Keep its installed files together beside `GTAIV.exe`. | Piecemeal movement creates conflicts that are hard to trace. |
| 3. Loader layer | Keep one archive-supplied ASI/proxy route. | Do not add another `dinput8.dll`, `d3d9.dll`, `dxgi.dll`, or injector. |
| 4. FusionFix version | Keep the bundled FusionFix 5.0.1 while validating. | Do not move the foundation while checking the house for cracks.[10] [11] |
| 5. DXVK/API layer | Use only the archive/setup-utility DXVK route for Vulkan tests. | Do not overlay a standalone DXVK release. |
| 6. Content layer | Keep the archive’s own update and asset ordering intact. | Enable optional archive content only as the archive says. |
| 7. Launch layer | Use Steam, Rockstar Games Launcher, or `PlayGTAIV.exe` as directed. | This protects launcher and DLC behavior. |

## 5. Balanced 60 — the first clean run

Use AC power, select the best-performance power policy for the play session, and close unnecessary browser tabs, video capture, overlays, and extra launchers. AMD recommends the laptop maker’s validated graphics driver when available because it can include platform-specific compatibility and power tuning.[5]

If Vulkan/DXVK is stable, set the Graphics API to **Vulkan**, accept the required restart, and use FusionFix’s Windowed + Borderless options. If it crashes, use the archive’s documented recovery route: remove `d3d9.cfg`, do not reselect the API, and troubleshoot on native DirectX 9.[11] [12]

Use only these minimal launch arguments when the archive path has not already supplied them:

```text
-norestrictions -nomemrestrict
```

Do **not** use `-managed` with DXVK. Do **not** invent an oversized `-availablevidmem` value. FusionFix later than 4.0.0 does not need that override in Gillian’s guidance.[9] [12]

### Core graphics settings

| Setting | Balanced 60 start | Why it belongs here |
|---|---|---|
| Video mode | Native resolution if 1920×1080 or lower; otherwise start at 1920×1080 | Keeps pixel and render-target cost in check. |
| Texture Quality | High | Dual-channel 16 GB makes high textures reasonable, not unlimited. |
| Reflection Resolution | High | Very High adds meaningful bandwidth and render-target cost. |
| Water Quality | High | Preserves the look without beginning at the most demanding water setting. |
| Shadow Quality | High | Holds visual depth without the heaviest cost. |
| Night Shadows | Medium | Extra local shadow maps are expensive. |
| Extra Night Shadows | Off | FusionFix calls the original PC feature extremely broken and does not recommend it.[9] |
| Texture Filter | Anisotropic 16× | Strong image value with usually low cost on modern graphics. |
| View Distance | 35 | A reasonable ceiling on CPU, geometry, streaming, and bandwidth pressure. |
| Detail Distance | 25 | Keeps city detail credible without extreme cost. |
| Vehicle Density | 100 initially; then 80, then 70 if needed | The archive’s custom popcycle uses 100, but traffic is the first APU dial when the cache is already warm.[11] |
| Anti-aliasing | SMAA | A lighter post-process path than expensive multisampling. |
| FPS Limiter | 60 through FusionFix | Stable timing comes before a high but unstable counter.[12] |

### FusionFix visual settings

| Option | Balanced 60 start | Reason |
|---|---|---|
| Graphics API | Vulkan when stable | Revert to D3D9 if it crashes. |
| Volumetric Fog | Off initially | Documented as performance-heavy.[12] |
| Sun Shafts | Off initially | A discretionary full-screen layer. |
| Extended Sunlight Reach | Off initially | Add only after baseline testing. |
| Tone Mapping | On | Good image value with low expected incremental cost. |
| Ambient Occlusion | Off initially | Adds depth/post-processing work.[10] |
| Shadow Filter | Soft | Avoids the sharper, potentially heavier CHSS path. |
| Distant Lights | Default initially | Test only after the base profile is stable. |
| Definition | On | Recommended above 1280×720 to avoid PC blur.[12] |
| Depth of Field | Low or cutscenes only | Keeps cinematics without making ordinary play soft. |
| Motion Blur | Preference, default Off | Easier to judge clarity and performance with it off. |
| Reflection MSAA | Off | Experimental and driver/API-sensitive.[10] |

## 6. Cache and restart protocol

This is the part to keep close. The cache is not a flaw to be outsmarted; it is work the renderer must do as it meets shaders and pipeline combinations for the first time.

| When this happens | What to expect | What to do |
|---|---|---|
| First run, fresh area, new weather, or new effect | Temporary shader/pipeline compilation hitches can appear. | Drive the same demanding route again and compare the second or third pass. |
| In-game graphics/resolution change | Cache behavior can change, and some settings only fully apply after relaunch. | Restart GTA IV before judging the result. |
| DXVK, driver, renderer, or major mod change | Existing cache entries may no longer apply. | Restart, expect a fresh warm-up, and test one route repeatedly. |
| Stutter continues after repeat cache-warmed runs | The issue may be settings, heat, traffic, memory pressure, or a renderer conflict. | Roll back the last change and isolate one pressure point at a time. |

> **Do not keep changing settings while the cache is still learning. On an APU, that only creates a new cold path, new stutter, and no clear answer. There is no way around the cache-building phase.**

## 7. Controlled uplift and fallback

Do not enable several quality features in one move. Test a single new layer in the same hard scene: fast driving through dense traffic, rain at night, and a bridge or long-visibility district.

| Profile | Apply when | Change from Balanced 60 |
|---|---|---|
| Quality 60 | The warm profile is consistently stable and thermals are comfortable. | Raise Reflection Resolution to Very High; enable **one** of AO, fog, or distant lights; restart and test for 15–30 minutes. |
| Visual showcase 30 | Image quality matters more than 60 FPS pacing. | Set the FusionFix limiter to 30; raise shadows/reflections cautiously; enable AO or fog, but keep Extra Night Shadows and Reflection MSAA off. |
| Performance 60 | Repeat cache-warmed routes still stutter or sustained heat/fan throttling appears. | Use 1600×900 or 1280×720 if required; set reflection/water Medium, view 25, detail 15, traffic 70, and optional post-processing off. |
| Native D3D9 fallback | Vulkan crashes or remains less stable on the actual driver. | Select DirectX 9, restore the archive’s normal launch behavior, turn in-game VSync on, keep FusionFix at 60, and restart before testing. |

## 8. Validation order

The built-in benchmark is useful only as a first comparison. The real test is a cache-warmed drive with clean frame times, no texture eviction spikes, no reload crash, and no sustained thermal collapse.

| Test | Action | Read the result |
|---|---|---|
| 1 | Launch the unmodified installed archive with Balanced 60. | Confirms the intended base. |
| 2 | Test Vulkan, 60 FPS, borderless, and a dense daytime drive. | Keep it only if it is stable. If it crashes, use the D3D9 recovery path and stop changing Graphics API settings. |
| 3 | Test rain/night, fast driving, and a save load. | If repeat runs still stutter, lower Vehicle Density, View Distance, then Detail Distance. |
| 4 | Add one visual feature and restart if required. | If the change creates sustained drops or spikes after warm-up, turn that same feature back off. |
| 5 | Test cold launch and 30 minutes on AC. | Late decline points toward thermals or power contention. |
| 6 | Add optional content only after a stable profile exists. | Copy the known-good config first. |

## 9. Boundaries and source trail

This report records research and a configuration target. It does not claim that every driver, display, laptop power limit, or mod stack will behave the same. Make a backup before moving anything.

| Reference | Source |
|---|---|
| [1] | [Rockstar — GTA IV Complete Edition announcement](https://www.rockstargames.com/newswire/article/75o94113117o24/GTAIV-Complete-Edition-Now-Available-on-the-Rockstar-Games-Launcher-an) |
| [2] | [Rockstar Support — GTA IV PC system requirements](https://support.rockstargames.com/articles/7BOteXZsOSvgaEsyzz743i/grand-theft-auto-iv-pc-system-requirements) |
| [4] | [Microsoft Learn — Direct3D 9 shaders](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9) |
| [5] | [AMD — Ryzen 5 PRO 6650U specifications and drivers](https://www.amd.com/en/support/downloads/drivers.html/processors/ryzen-pro/ryzen-pro-6000-series/amd-ryzen-5-pro-6650u.html) |
| [8] | [DXVK project](https://github.com/doitsujin/dxvk) |
| [9] | [FusionFix project](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) |
| [10] | [FusionFix releases](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases) |
| [11] | [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/) |
| [12] | [Gillian’s additional setup guidance](https://gillian-guide.github.io/additional-setup/) |
