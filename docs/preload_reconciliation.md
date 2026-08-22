# Shader Preload Reconciliation — Version 3.0.5

`preload.list` is not a general mod load order, a substitute for missing shaders, or a way to bypass shader and pipeline cache work. It is shader-resource metadata whose correctness depends on the exact installed shader layout.

Version 3.0.5 therefore removes `preload.list` from the automatic installer. The previous list is retained as `tuned/legacy/preload.list` for forensic comparison and **legacy static setups only**. Do not copy it into a modern FusionFix installation merely because it is present in this repository.

Current FusionFix discussion establishes two complementary points. A valid preload list matters when a setup relies on one, but current 5.0.1 handling can generate normal shader metadata from the installed shader content rather than requiring manual additions for each shader-bearing mod.[^1] A mismatched LibertyCityPlates/shader installation can lead to invalid-resource failures; overwriting metadata cannot repair an absent or wrongly placed shader.[^2]

## Safe rule

| Situation | Correct action |
|---|---|
| Normal FusionFix 5.0.1 shader route | Leave the installation’s managed shader metadata alone; let the installed FusionFix/shader path determine it. |
| Verified legacy static setup | Copy a list only after comparing every named shader to the actual directory and preserving the original list. |
| Invalid resource, EFC20, or missing material | Verify the plugin, shader files, and their load path first. Do not add names blindly. |
| Fresh-route stutter | Treat it as a cache/measurement question, not a preload-list question; restart when required and compare warm passes. |

## References

[^1]: [FusionFix issue #1431 — preload-list behavior in 5.0.1](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)
[^2]: [FusionFix issue #1347 — invalid resource and LibertyCityPlates installation context](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)
