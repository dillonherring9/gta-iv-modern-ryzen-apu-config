# Live Configuration Assessment — 660M After the Cache Settles

The supplied files form a **FusionFix 5 / DXVK Vulkan configuration** with Fusion Overloader-era mods, Project2DFX distant lights, Project Glass-compatible reflections, Xbox Rain Droplets, LibertyCityPlates, a console episode selector, and a custom audio mapper. The real performance weight lives in the renderer and visual stack, not the menu, audio, or plate mappings.

| Component | Active configuration | 660M read | Staged action |
|---|---|---|---|
| Renderer | `GraphicsAPI=1` (Vulkan/DXVK), borderless windowed | DXVK/Vulkan is the preferred first route on current AMD hardware when it is stable. | Keep it unchanged for the first baseline. |
| Frame pacing | `FpsLimitPreset=7` / FusionFix 60 FPS with accurate limiter | One FusionFix limiter is right for the 6650U/660M profile. | Keep it. Do not add a driver, DXVK, or external cap. |
| Anti-aliasing | `Antialiasing=7` (SMAA) | A strong quality/performance balance for this iGPU. | Keep it. |
| Trees and visibility | PC+ lighting/alpha, distant lights, restored vegetation/assets | The intended visual character, with added draw and streaming work. | Keep PC+ and distant lights; reduce the extra shadow tier first. |
| Shadows | CHSS filter, high-resolution cascades off, tier-2 extra dynamic shadows, extra night shadows off | CHSS plus tier-2 shadows hit dense city scenes too hard. | Move to Soft filter and tier-1 extra dynamic shadows. |
| Post-processing | Low DoF, bloom, tone mapping, shafts, fog, AO enabled | SMAA, low DoF, bloom, and tone mapping are reasonable; AO + fog + shafts together are the avoidable risk. | Disable AO, fog, and shafts; keep tone mapping, bloom, low DoF, and motion blur. |
| Reflections | Console car reflections, Project Glass-compatible renderer, reflection MSAA off | Reflection MSAA is correctly avoided. | Keep it unchanged. |
| Vehicle streaming | 100,000,000 vehicle budget, extended limits, vehicle pack/plates/custom traffic likely present | The budget is deliberate for the Taxi Bug, but it can eat shared memory and expose audio issues. | Keep it until a symptom proves otherwise. |
| Rain plugin | 1,500 static and 3,000 moving drops | Too heavy for an RDNA 2 iGPU in rain-at-night scenes. | Use 900 static and 1,800 moving drops. |
| Audio mapper / console selector / plates | Mapping and quality-of-life configuration | No material sustained GPU load. | Preserve them. |

## Intended 660M profile

This is a **balanced 60 FPS profile**, not an unsupported promise that every crowded, CPU-heavy scene will hold a perfect number. It lowers three expensive post-processing layers, softens the most costly shadow route, reduces secondary dynamic shadows, and cuts rain density. It deliberately preserves Vulkan, 60 FPS pacing, SMAA, PC+ trees, console vehicle reflections, bloom, tone mapping, low DoF, Project2DFX distant lights, the vehicle budget, and the archive’s content changes.

> **Cache truth:** on an APU, shader and pipeline caches must build. Fresh routes, new effects, changed resolution, renderer changes, driver updates, or other graphics changes can stutter while that work happens. **Restart GTA IV when the changed setting needs it, then repeat the same demanding route before you judge the profile. There is no way around cache building on a shared-memory APU.**

Do not add another renderer proxy, ASI loader, DXVK distribution, modloader, popcycle, or traffic package on top of the archive. FusionFix’s `d3d9.dll` / `plugins` / `update` chain is the only intended rendering and overload path.

## Deferred items

The supplied files do not include GTA IV’s base `d3d9.cfg`, a DXVK log, a plugin inventory, the `update` tree, or launcher arguments. For that reason, this assessment does not alter resolution, texture quality, water/reflection quality, in-game view distance, detail distance, or the traffic-density slider.

Keep the archive’s required Traffic Density at `100` for Fidelity Popcycle at first. If dense-driving frame times remain poor **after the cache has warmed and after a required restart**, lower that slider to `80` before reducing texture quality. The bundle copies originals and ready-to-deploy replacements; deployment only overwrites configuration files and does not require renamed load-order folders or IMG/RPF archive edits.

## References

[1]: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix
[2]: https://gillian-guide.github.io/drag-and-drop-archive/
[3]: https://gillian-guide.github.io/extras/modloading/
[4]: https://gillian-guide.github.io/enhancements/
