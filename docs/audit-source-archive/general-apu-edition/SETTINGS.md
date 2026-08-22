# General APU Edition — FusionFix settings

FusionFix 4.0+ moved almost all tuning into an **in-game menu** (its own release
notes now say "no more .ini file configuration required"). That menu is the
current source of truth, so set these there rather than hand-editing a `.ini`
with guessed key names — GTA IV's `.ini` schema is large, version-specific, and
easy to get subtly wrong by hand.

Open the menu in-game (default bind: **F9** — confirm in FusionFix's own docs if
it's been remapped in your build) and set:

## Graphics
| Setting | Value | Why |
|---|---|---|
| Graphics API | Vulkan (only after Gillian's/DXVK setup already works on native D3D9) | The Vulkan path is what DXVK translates |
| FPS Limiter | 60, "accurate/sleep-yield" mode if offered | One limiter, in one place — see `dxvk.conf`'s `maxFrameRate = 0` |
| Anti-Aliasing | SMAA | Cheap on shared memory, no MSAA bandwidth tax |
| Shadow Filter | Soft or Contact-Hardening (test both) | Cosmetic-only cost on an iGPU |
| High-Resolution Shadows | Off | The single most expensive shadow-quality toggle |
| Extra Dynamic Shadows | 1 (some vegetation) to start | 2 adds fences/grates/walls too — test after cache warms |
| Ambient Occlusion | Off | Reclaim shared memory bandwidth first |
| Sun Shafts / Volumetric Fog | Off | Same reasoning |
| Reflection MSAA Quality | Off/Low | New in 5.0 — can behave inconsistently across GPU vendors/drivers per FusionFix's own release notes |

## Weather / streaming
| Setting | Value |
|---|---|
| Rain droplet density | Reduced (start low, raise only if headroom allows) |
| Traffic Density | 80 (down from default 100) — first lever if traffic-heavy areas stutter |

## First-run checklist
1. Apply Graphics settings, then **fully restart GTA IV** — several of these
   require a restart to take effect, not just a menu-close.
2. Drive the same route twice. Judge the **second, cache-warmed** pass.
3. If dense traffic is still heavy after that: lower Traffic Density before
   touching texture quality.
4. If rain is the bottleneck: reduce droplet density further before anything else.
5. If shadows cause frame-time spikes: turn Extra Dynamic Shadows to 0 as the
   fallback step.

## The handful of `.ini`-only options worth knowing
A few options live only in `GTAIV.EFLC.FusionFix.ini` (not the in-game menu) as
of the 5.0 series — edit these directly if you want them, but leave everything
else to the menu:
```
MenuEnteringDelay = 0     ; 400 is default; 0 removes menu-open animation delay
MenuExitingDelay = 0      ; same, for menu-close
NoBloomColorShift = 1     ; stops bloom threshold from skewing colors under tone mapping
SmoothShorelines = 1      ; cosmetic, negligible cost
SmoothLightVolumes = 1    ; cosmetic, negligible cost
```
Verify against the copy of `GTAIV.EFLC.FusionFix.ini` that actually shipped with
your installed FusionFix build before editing — key names have changed across
major versions (4.0, 5.0), and the file itself documents every option inline.
