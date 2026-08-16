# preload.list Reconciliation

The supplied list contains the base FusionFix-style shader names but not the additional shader names associated with the supplied LibertyCityPlates/FusionFix combination. The tuned list keeps the original order intact and appends:

```text
gta_trees_extended.fx
gta_vehicle_licenseplate.fx
gta_vehicle_licenseplate_ext.fx
gta_vehicle_track2.fx
gta_vehicle_track.fx
```

These entries are resource-preload metadata, not a general mod load order. They are safe to use only when the matching shader files are installed in the appropriate GTA IV shader directory. A missing shader file cannot be repaired by adding a name to the list. If a later FusionFix release dynamically registers the same shaders, retain the upstream method and do not create duplicate lists.

Primary evidence: [FusionFix issue #1347](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347) and [FusionFix issue #1431](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431).
