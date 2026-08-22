# Radeon 660M Reference Edition — FusionFix settings

Same menu-first approach as the general edition (FusionFix 4.0+ runs primarily
through its in-game menu now), tuned tighter for the specific reference hardware:
**Ryzen 5 PRO 6650U, Radeon 660M (RDNA2, 12 CU, shared memory), 16GB dual-channel.**

Open the in-game menu (**F9** by default — confirm in your installed build) and set:

## Graphics
| Setting | Value | Why, specific to 660M |
|---|---|---|
| Graphics API | Vulkan | Required for the GPLAsync `dxvk.conf` in this folder to do anything |
| FPS Limiter | 60, accurate/sleep-yield | 660M can sustain 60 in most street scenes once caches are warm; don't chase higher without headroom to spare |
| Anti-Aliasing | SMAA | MSAA's bandwidth cost is disproportionate on a 12-CU shared-memory part |
| Shadow Filter | Soft (index 4 in older builds) | Matches this repo's original validated profile |
| High-Resolution Shadows | Off | Non-negotiable on 660M — this is the single biggest shared-memory tax |
| Extra Dynamic Shadows | 1 | 660M has headroom for vegetation shadows but not the full fences/walls/grates set (2) at 60 FPS in dense traffic — test 2 only if you have frame-time margin |
| Ambient Occlusion / Sun Shafts / Volumetric Fog | Off | All three, off — reclaim shared memory for streaming and traffic instead |
| Reflection MSAA Quality | Off | Explicitly flagged by FusionFix's own 5.0 notes as inconsistent across GPU vendors — don't spend 660M budget on an unstable feature |

## Weather / streaming (matches this repo's original validated numbers)
| Setting | Value |
|---|---|
| Max rain drops | ~900 |
| Max moving rain drops | ~1800 (drop to 1400 if rain scenes stutter after cache warm-up) |
| Traffic Density | 100 to start; drop to 80 if dense traffic stutters after cache warm-up, *before* touching textures |

## 660M-specific first-run checklist
1. Confirm `vulkan.dll` in the game root is the **DXVK-GPLAsync v3.0-1** build
   (32-bit `x32\d3d9.dll` renamed), not a stock DXVK build — GPLAsync's async
   shader compilation is what this profile's `dxvk.conf` assumes.
2. Restart fully after applying graphics settings.
3. Drive the same route twice; judge the second pass only.
4. Levers in order, cheapest-first: Traffic Density → rain droplets →
   Extra Dynamic Shadows → texture quality (last resort).
5. If `GTAIV_d3d9.log` shows `Unknown option` for `dxvk.gplAsyncCache`, that
   confirms you're on a v3.0-1-style build correctly rejecting the old key —
   this is expected and matches `dxvk.conf` in this folder, which omits it.

## Verified `.ini`-only options (FusionFix 5.0 series)
```
ExtraDynamicShadows = 2        ; overrides the menu toggle if you want more control;
                                ; 1 = some vegetation, 2 = + fences/grates/walls
ReflectionMSAAQuality = 0      ; keep at 0/off per the reasoning above
ConsoleCarReflectionsAndDirt = 1  ; cosmetic, negligible iGPU cost, matches
                                   ; the "after-dark Complete Edition" look this
                                   ; repo is going for
NoBloomColorShift = 1
MenuEnteringDelay = 0
MenuExitingDelay = 0
```
As with the general edition: confirm these key names against the exact
`GTAIV.EFLC.FusionFix.ini` shipped with your installed FusionFix version before
editing by hand — the file documents every option inline, and is the
authoritative source, not this guide.
