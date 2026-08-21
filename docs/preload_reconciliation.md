# Shader Preload Reconciliation

The supplied list keeps the base FusionFix-style shader names in their original order and adds the LibertyCityPlates/FusionFix-related entries below.

```text
gta_trees_extended.fx
gta_vehicle_licenseplate.fx
gta_vehicle_licenseplate_ext.fx
gta_vehicle_track2.fx
gta_vehicle_track.fx
```

These are **resource-preload metadata**, not a general mod load order and not a shortcut around renderer caches. They are safe only when the matching shader files are actually installed in the correct GTA IV shader directory. A missing shader file cannot be brought back by adding its name to this list.

> **Keep the distinction clean:** `preload.list` tells the game which existing resources to preload. Shader and pipeline caches still have to build as the renderer meets work in-game. On an APU, do not mistake a fresh cache hitch for a missing preload entry. If you changed graphics, renderer, drivers, or major shader content, restart GTA IV when required and let the cache settle through repeat runs.

If a later FusionFix release dynamically registers the same shaders, follow the current upstream method instead of keeping a duplicate list.

**Primary evidence:** [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347) and [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431).
