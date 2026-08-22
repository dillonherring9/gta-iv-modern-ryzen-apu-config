# Identify the Drag-and-Drop Archive Renderer Before You Change It

> **Do not assume that every Gillian Drag-and-Drop Archive installation uses GPLAsync.** The archive’s Setup Utility chooses a DXVK path from the detected graphics hardware and available Vulkan features. Your installed `vulkan.dll` may be stock DXVK, GPLAsync, DXVK-Sarek/async, or another compatible artifact selected by that utility.

This guide tells you how to identify the current renderer without replacing a working file. It does **not** tell you to update, reinstall, or switch DXVK merely because you are checking.

## Why the answer is conditional

Gillian’s Setup Utility is hardware-aware. Its current source selects a DXVK tier based on the detected GPU/Vulkan capability and makes the Async option visible when full Graphics Pipeline Library support is missing or an edge-case GPU needs it. With Async selected, one supported tier installs **DXVK-GPLAsync 2.6.2-1**; without Async, the same tier installs stock **DXVK 2.6.2**. Other tiers use DXVK-Sarek or upstream DXVK.[1] [2]

| What you have | What it means |
|---|---|
| Full GPL / Fast Linking support and Async left off | The normal route is a stock/current DXVK path, not GPLAsync. |
| Async selected because the Setup Utility enabled/offered it | The utility may select GPLAsync or another async-capable renderer, depending on the chosen DXVK tier. |
| A `vulkan.dll` in the game folder | This proves a Vulkan route exists. It does **not** identify the exact DXVK family or version by itself. |
| Old notes that mention GPLAsync | They are not proof that your own archive install is GPLAsync. The exact installed artifact must be checked locally. |

Gillian’s optimization guidance describes upstream DXVK as the normal modern route and presents `dxvk-gplasync` as a separate option for persistent stutter after driver checks. Its async configuration keys are specifically for async/GPLAsync builds, not generic stock DXVK settings.[1]

## Check the current renderer without changing files

Follow these steps in order. Reading a file is safe; do not run **Reinstall DXVK** merely to identify the current renderer.

1. **Open the game folder.** It is the folder containing `GTAIV.exe` and `vulkan.dll`.
2. **Open `dxvk.conf` in Notepad.** Look for the following lines:

   ```text
   dxvk.enableAsync = true
   dxvk.gplAsyncCache = true
   ```

   `dxvk.gplAsyncCache = true` is the strongest configuration sign that a GPLAsync-specific configuration was used. `dxvk.enableAsync = true` alone means an async setting is active but is not proof of GPLAsync, because the Setup Utility can use another async-capable build.

3. **Launch GTA IV once through your normal launcher with Vulkan selected, then close it.** Do not change settings during this identification run.
4. **Open `GTAIV_d3d9.log` in the game folder.** Record the renderer version/path information and any accepted or unsupported configuration lines. Keep the useful version/API/error lines; redact only personal folder/account paths if you share the log.
5. **Record the file hash before any later renderer experiment.** In PowerShell, from the game folder, run:

   ```powershell
   Get-FileHash -Algorithm SHA256 .\vulkan.dll
   ```

   Save the result with the log and the active `dxvk.conf`. A filename alone is not a reproducible renderer identity.

## How this affects Version 3

Version 3 intentionally installs the **stock upstream-compatible** `dxvk.conf` baseline. It contains no GPLAsync-only settings, so it is safe to use as the default configuration when the exact renderer installed by the archive has not yet been identified.

The package retains a separate `dxvk-gplasync-v2.6.2-1.conf` only as an advanced, version-locked reference. Do not apply it to stock DXVK, DXVK 3.0.2, DXVK-Sarek, or a different GPLAsync release. If a future test uses another GPLAsync artifact, make a separate backup, record the exact version/hash/log, test cold and warm routes, test GTA IV/TLAD/TBoGT, and restore the known-good renderer if the result is worse.[3] [4]

> **Do not “upgrade” just to identify.** Keep the working `vulkan.dll` until you have a documented reason to test another one. A newer DXVK or a GPLAsync build is an experiment, not an automatic improvement.

## References

[1] [Gillian’s GTA IV Modding Guide — Optimization](https://gillian-guide.github.io/optimization/)

[2] [Gillian’s GTA IV Setup Utility source](https://github.com/gillian-guide/GTAIVSetupUtility)

[3] [DXVK GPLAsync project](https://gitlab.com/Ph42oN/dxvk-gplasync)

[4] [Version 3 validation protocol](VALIDATION_PROTOCOL.md)
