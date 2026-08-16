# Initial Release — v1.0.0

This release provides a reversible, community-oriented configuration profile for **GTA IV Complete Edition** on modern Ryzen 5 PRO APUs and similar integrated-GPU systems. The reference profile targets a Ryzen 5 PRO 6650U with Radeon 660M graphics and 16 GB dual-channel memory.

## Included

| Item | Purpose |
|---|---|
| Final GitHub README | Installation order, hardware scope, tuning rationale, troubleshooting, rollback, credits, and sources. |
| Tuned FusionFix configuration | A Balanced 60 profile with reduced high-cost post-processing, CHSS replaced by soft shadows, reduced secondary dynamic shadows, and fullscreen as the default presentation mode. |
| `dxvk.conf` | DXVK-side compiler, memory-reporting, frame-latency, and presentation settings that preserve FusionFix as the single FPS limiter. |
| `preload.list` | Base shader sequence plus required FusionFix/LibertyCityPlates entries, to be used only when the matching shader files are installed. |
| Source ZIP | Documentation, original backups, tuned files, validation records, and checksums. |
| Release ZIP | End-user README and tuned configuration files only. |

## Important notes

The profile starts in fullscreen using `Windowed=0`. The separate `BorderlessWindowed=1` preference remains available for users who deliberately choose borderless windowed mode. The project does not include game files, game binaries, launcher/account data, or third-party mod binaries.

Do not stack frame limiters. Begin with FusionFix as the single 60 FPS limiter and test stability before adding Radeon Chill, RTSS, another DXVK limiter, or another overlay.

## Credits

This is not an original mod. Please credit and follow the licenses of FusionFix, DXVK, Gillian’s GTA IV Modding Guide, the Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Project2DFX, Xbox Rain Droplets, and every other third-party project used with the configuration.
