# GTA IV Complete Edition: Provided Configuration Assessment

The supplied files show a **FusionFix 5 / DXVK Vulkan configuration** with Fusion Overloader-era mods, Project2DFX distant lights, Project Glass-compatible reflections, Xbox Rain Droplets, LibertyCityPlates, a console episode selector, and a custom audio mapper. The performance-intensive combination is the renderer configuration, rather than the audio/menu/plate mappings.

| Component | Active configuration | 660M assessment | Staged action |
|---|---|---|---|
| Renderer | `GraphicsAPI=1` (Vulkan/DXVK), borderless windowed | DXVK/Vulkan is the preferred first route on modern AMD hardware if it is stable. Borderless presentation is appropriate. | Keep unchanged. |
| Frame pacing | `FpsLimitPreset=7`, which FusionFix defines as 60 fps; accurate limiter type is set in the INI | A single 60 fps FusionFix limit is appropriate for the Ryzen 5 PRO 6650U / Radeon 660M. | Keep the 60 preset and accurate limiter. Do not add a second driver, DXVK, or external FPS cap. |
| Anti-aliasing | `Antialiasing=7` (SMAA) | SMAA is a favorable quality/performance compromise for this iGPU. | Keep unchanged. |
| Trees and visibility | PC+ tree lighting/alpha, distant lights enabled, restored vegetation/assets assumed from archive | These increase draw/streaming work but preserve the archive’s visual intent. | Keep PC+ and distant lights; reduce the heavier extra dynamic-shadow tier. |
| Shadows | CHSS filter, high-resolution cascades off, extra dynamic shadows tier 2, extra night shadows off | CHSS plus tier-2 dynamic shadows is disproportionately expensive in dense city scenes, while high-resolution and extra-night shadow costs are already avoided. | Change filter to Soft and extra dynamic shadows to tier 1. |
| Post-processing | Low DoF, bloom, tone mapping, sun shafts, volumetric fog, ambient occlusion all enabled | SMAA/low DoF/bloom/tone mapping are reasonable; the cumulative AO + volumetric fog + sun shafts cost is the avoidable 660M frame-time risk. | Disable AO, volumetric fog and sun shafts; retain tone mapping, bloom, low DoF and motion blur. |
| Reflection path | Console car reflections, Project Glass-compatible renderer, reflection MSAA off | Reflection MSAA is correctly disabled. | Keep unchanged. |
| Vehicle streaming | Vehicle budget 100,000,000; extended limits on; Vehicle Pack + plates + custom traffic/popcycle likely present | The vehicle budget is deliberate for avoiding the Taxi Bug and is not changed without a symptom. It may consume more shared memory and can cause audio symptoms on some setups. | Keep unchanged; test dense traffic and only reduce if audio/streaming faults occur. |
| Rain plugin | 1,500 static drops and 3,000 moving drops | This is unusually heavy for an RDNA 2 iGPU in rain/night scenes. | Lower to 900 static and 1,800 moving drops while retaining all movement/gravity behavior. |
| Audio mapper | Vehicle-bank and weapon-sound path mapping | No direct material GPU cost. File references must exist exactly as written. | Preserve unchanged. |
| Console selector | Title menu enabled; CE logo enabled | Startup preference, not a rendering bottleneck. | Preserve unchanged. |
| LibertyCityPlates | Feature and physics fixes enabled | Low, intermittent CPU cost compared with visual/traffic load. | Preserve unchanged. |
| Launcher configuration | Targets `GTA-IV\GTAIV.exe` | Confirms the expected nested game directory but is not a graphics configuration. | Leave entitlement/emulation-related entries untouched. |

## Intended 660M profile

This is a **balanced 60 fps profile**, not an unsupported promise of a constant 60 fps in every CPU-heavy, high-traffic scene. It lowers three costly post-processing effects, changes the most expensive shadow filter to Soft, reduces secondary dynamic shadows, and trims rain-particle density. It deliberately retains Vulkan, 60 fps pacing, SMAA, PC+ trees, console vehicle reflections, bloom, tone mapping, low DoF, Project2DFX distant lights, the vehicle budget, and the archive’s gameplay/content modifications.

No extra renderer proxy, ASI loader, DXVK distribution, modloader, popcycle, or traffic package should be added on top of the archive. FusionFix’s `d3d9.dll`/`plugins`/`update` chain remains the only intended rendering and overload path.

## Deferred items

The supplied files do not include GTA IV’s base graphics settings file, `d3d9.cfg`, DXVK log/config, plugin inventory, `update` tree, or launcher arguments. Therefore, resolution, texture quality, water/reflection quality, in-game view distance, detail distance, and traffic-density slider are not altered here. Keep the archive’s required Traffic Density at 100 for Fidelity Popcycle initially; if dense-driving frame times are poor, reduce that slider to 80 before reducing texture quality.

The bundle generated from this assessment contains copied original files and ready-to-deploy replacements. Deployment is an overwrite of files only; it does not require renaming load-order folders or modifying IMG/RPF archives.

## References

[1]: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix
[2]: https://gillian-guide.github.io/drag-and-drop-archive/
[3]: https://gillian-guide.github.io/extras/modloading/
[4]: https://gillian-guide.github.io/enhancements/

[1] FusionFix documentation and source configuration definitions.  
[2] Gillian’s Drag-and-Drop Archive installation and performance notes.  
[3] Fusion Overloader precedence and update-folder guidance.  
[4] Archive enhancement ordering and compatibility guidance.
