# Version 3.0.0 — The Log Came Back Clean

> The city was never asking for more noise. It was asking for the right signal.

Version 3 is the after-hours release: darker in mood, clearer in purpose, and built around what the machine already proved it could hold. We went back to the GTA IV runtime log instead of guessing. The reference Ryzen 5 PRO 6650U / Radeon 660M session reported **DXVK GPLAsync v2.6.2**, Graphics Pipeline Library support, full-screen-exclusive support, and a working `4 / 4` compiler-and-async worker balance. Version 3 brings those accepted settings back into the tracked package and the automatic installer.

## What returned from the log

| Runtime proof | Version 3 action | Why it matters |
|---|---|---|
| `dxvk.numCompilerThreads = 4` was active | Kept at `4` | Leaves room for GTA IV traffic, streaming, scripts, Windows, and the shared-power APU. |
| `dxvk.enableAsync = true` was active | Restored | Keeps the logged GPLAsync path intact for the matching renderer build. |
| `dxvk.numAsyncThreads = 4` was active | Restored | Retains the working async-worker balance instead of silently dropping it. |
| `dxvk.gplAsyncCache = True` was active | Kept | Preserves the GPLAsync cache behavior that the session recognized. |
| `dxvk.allowFse = true` was active | Restored | Uses the full-screen-exclusive capability confirmed by the Radeon 660M log. |
| Graphics Pipeline Library was supported | Kept at `Auto` | Lets DXVK and the AMD driver choose the compatible pipeline route. |
| 1080p and 720p swapchains both appeared in the log | Documented, not forced | Resolution remains the cleanest iGPU performance lever when headroom runs out. |

The result is not a fantasy config claiming every laptop becomes the same machine. It is a return to the renderer profile that was already loading, already accepted, and already moving correctly through this setup.

## The installer grew up with the package

The new **GTA IV Version 3 Automatic Installer** begins with a blank folder field, so it does not inherit stale Rockstar registry paths or duplicate folder layers. Choose the real GTA IV folder once, click **Install**, and let the installer verify it after the click instead of locking the button before you can move.

It creates a timestamped `GTAIV_V3_Installer_Backups` folder before replacing managed files, writes the nine-file Version 3 profile, and registers a Version 3 rollback path in Windows **Installed apps**. When Version 3 successfully installs over Version 2, it removes the obsolete Version 2 Installed-apps entry and old uninstaller only after the Version 3 uninstaller exists. The Version 2 backup folder remains untouched, while the new Version 3 backup captures the files it replaces.

| Choose this download | When it is the right move |
|---|---|
| `GTAIV_V3_Automatic_Installer_v3.0.0.exe` | You want the clean folder chooser, timestamped managed-file backup, log-validated profile, and rollback route. |
| `GTAIV_V3_Automatic_Installer_Source_v3.0.0.zip` | You want to audit or rebuild the NSIS installer and inspect the exact payload. |
| `GTAIV_V3_Automatic_Installer_v3.0.0_SHA256SUMS.txt` | You want to verify the published executable and source archive before use. |

## Keep the loose route loose

The high-resolution replacement resource remains a **loose-file replacement** in the tested path. Do not turn it into an `update`-folder mod simply because the folder looks tidier. In testing, that alternate path could keep the base story running while sending The Lost and Damned or The Ballad of Gay Tony into a launch crash. Loose replacement kept the result visible and EFLC alive.

Those assets are heavier than the originals. If they take too much from the Radeon 660M at 1080p, lower output resolution one sensible step, restart GTA IV, and let the cache settle before you judge the change. Save the road before you chase the reflection.

## The rules that keep the night smooth

Keep one intended renderer path, one intentional frame limiter, and one test variable at a time. FusionFix owns the 60 FPS target; DXVK’s own rate cap stays off. Do not add Radeon Chill, RTSS, another DXVK limiter, another wrapper DLL, a second ASI loader, or a surprise `update`-folder priority layer while you are trying to understand frame pacing.

A cold route can stutter. A new effect, weather condition, resolution, renderer setting, driver, or large loose asset can force fresh shader and pipeline work. Restart when the change calls for it, repeat the same demanding route, and judge the second or third pass. The cache needs time to learn the city.

## Compatibility boundary

This Version 3 `dxvk.conf` targets the **DXVK GPLAsync v2.6.2-style path shown in the runtime log**. `dxvk.gplAsyncCache`, `dxvk.enableAsync`, and `dxvk.numAsyncThreads` are build-specific values, not a universal promise for stock upstream DXVK. If an upstream build reports those async keys as unknown, keep the compatible standard settings and remove only the unsupported build-specific lines. Do not stack another renderer to make those messages disappear.

The release contains configuration, installer source, checksums, and documentation only. It does not redistribute GTA IV, Rockstar launcher files, saves, DXVK/FusionFix binaries, or third-party mod assets. Credits remain with the original tool and mod authors.

## References

[1]: [DXVK upstream configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf).

[2]: [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync).

[3]: [Gillian’s GTA IV optimization guide](https://gillian-guide.github.io/optimization/).
