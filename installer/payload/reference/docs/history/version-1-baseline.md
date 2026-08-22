# Version 1.0.0 — The Clean Start

This release is a reversible, community-built configuration profile for **GTA IV Complete Edition** on modern Ryzen 5 PRO APUs and similar integrated-GPU systems. The reference machine is a Ryzen 5 PRO 6650U with Radeon 660M graphics and 16 GB of dual-channel memory. It is built for a smoother night in Liberty City, not for a fake promise that every system will behave the same.

## Included

| Item | Purpose |
|---|---|
| Final GitHub README | Installation order, hardware scope, cache behavior, tuning rationale, troubleshooting, rollback, credits, and sources. |
| Tuned FusionFix configuration | A Balanced 60 profile with reduced high-cost post-processing, Soft shadows in place of CHSS, reduced secondary dynamic shadows, and fullscreen as the default presentation mode. |
| `dxvk.conf` | DXVK compiler, memory-reporting, frame-latency, presentation, and cache-related settings that leave FusionFix as the single FPS limiter. |
| `preload.list` | Base shader sequence plus the required FusionFix/LibertyCityPlates entries, used only when the matching shader files are installed. |
| Source ZIP | Documentation, original backups, tuned files, validation records, and checksums. |
| Release ZIP | End-user README and tuned configuration files only. |

## **Important: caches have to build**

> **On an APU, shader and pipeline caches are essential and must build. Stutter can happen on a fresh route, new weather/effect, or after changing graphics settings, resolution, DXVK, drivers, the renderer path, or major mods. Let the cache warm through repeat runs before you judge the profile.**
>
> **Some settings require a full GTA IV restart. If you change them and do not restart, the game can continue to stutter until you do. There is no way around cache building on a shared-memory APU.**

The profile starts in fullscreen with `Windowed=0`. The separate `BorderlessWindowed=1` preference remains available if you deliberately choose borderless-windowed mode. The project does not include game files, game binaries, launcher/account data, or third-party mod binaries.

Do not stack frame limiters. Start with FusionFix as the single 60 FPS limiter and prove stability after the cache settles before adding Radeon Chill, RTSS, another DXVK limiter, or another overlay.

## Credits

This is not an original mod. Credit and follow the licenses of FusionFix, DXVK, Gillian’s GTA IV Modding Guide, the Drag-and-Drop Archive, Fusion Overloader, LibertyCityPlates, Project2DFX, Xbox Rain Droplets, and every other third-party project used with this configuration.
