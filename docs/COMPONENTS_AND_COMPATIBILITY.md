# Components and Compatibility — Version 3

Version 3 configures **GTA IV Complete Edition with Gillian’s Drag-and-Drop Archive already installed**. The archive supplies FusionFix, its mod/plugin environment, content, and the Vulkan route. Version 3 supplies the seven configuration files below; it does not redistribute archive binaries or content.[^1]

## Full installed configuration

| Target | File | Contract |
|---|---|---|
| Game root | `dxvk.conf` | Stock upstream-compatible DXVK baseline. |
| `pc\` | `stream.ini` | Streaming profile. |
| `plugins\` | `ConsoleSelectMenuIV.ini` | Console Select Menu configuration for Complete Edition. |
| `plugins\` | `GTAIV.EFLC.FusionFix.cfg` | Main FusionFix configuration. |
| `plugins\` | `GTAIV.EFLC.FusionFix.ini` | Advanced FusionFix configuration. |
| `plugins\` | `GTAIV.XboxRainDroplets.ini` | Xbox Rain Droplets configuration. |
| `plugins\` | `LibertyCityPlates.txt` | LibertyCityPlates configuration. |

All seven are backed up before overwrite and restored by the uninstaller. `GTAIV.exe` is set to run as administrator because this layout requires it to create configuration files; the previous per-user compatibility value is restored on uninstall.

## What is deliberately retained but not overwritten

The complete package includes `dxvk-gplasync-v2.6.2-1.conf` as a version-locked alternative, and `preload.list` as a legacy shader reference. Do not deploy either automatically. GPLAsync is valid only with its exact renderer artifact. A static preload list must match the actual installed shader set; a mismatched LibertyCityPlates/shader layout can cause resource failures.[^2] [^3]

`AudioMap.ini` is excluded. It belongs to the separate DualSense Audio Mapper project and was an older installer carryover, not a documented Drag-and-Drop Archive component. Historical records retain its prior appearance without treating it as active configuration.

## Installer safety boundary

The installer verifies a real `GTAIV.exe` game root, `vulkan.dll`, and a closed `GTAIV.exe` process. It does not block on the absence of one FusionFix ASI filename, because valid archive layouts may differ. It cannot repair a broken archive, prove an external binary hash, replace shader content, or establish performance on another system.

## References

[^1]: [Gillian’s Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)

[^2]: [FusionFix issue #1431 — preload-list behavior](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1431)

[^3]: [FusionFix issue #1347 — LibertyCityPlates shader-resource context](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/issues/1347)
