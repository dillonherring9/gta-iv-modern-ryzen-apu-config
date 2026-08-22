# Version 3 Validation Protocol

Version 3 deliberately does **not** claim a new independently reproduced performance result. This document defines the minimum evidence required before calling any renderer or quality profile “tested,” “log-validated,” or suitable for another hardware class.

## 1. Record the complete environment

Capture the following before changing the game. A conclusion without this information is a personal impression, not a reusable compatibility report.

| Category | Required record |
|---|---|
| System | CPU/APU, GPU, RAM capacity, memory channel configuration, laptop power mode, and display resolution/refresh rate |
| Software | Windows build, AMD/NVIDIA/Intel driver version, GTA IV build, FusionFix release and checksum, renderer artifact/version/checksum, and all load-order tools |
| Game settings | In-game graphics values, FusionFix `.cfg` and `.ini` hashes, active `dxvk.conf` hash, frame cap, VSync policy, resolution, and launch options |
| Content | Archive/modpack revision, vehicle/traffic/texture content, plugins, shader files, and the episode under test |
| Temperature/power | Whether AC power was used, and whether sustained thermal/power throttling appeared |

## 2. Use one fixed test route

Use a saved game and repeat the same route for every profile comparison. The route must include dense daytime traffic, high-speed streaming, a rain-at-night pass, an interior transition, a save reload, and one vehicle using the configured audio path. Test GTA IV, TLAD, and TBoGT separately where all three are installed.

Record one cold pass and at least two warm passes without changing resolution, graphics settings, drivers, renderer files, or major content between passes. Restart the whole game after any renderer or graphics-path change that requires it.

## 3. Preserve evidence

Store the following under `evidence/<date>-<system-id>/` before publishing a conclusion:

```text
system.md
settings-hashes.txt
GTAIV_d3d9.log
cold-pass.csv or cold-pass.png
warm-pass-1.csv or warm-pass-1.png
warm-pass-2.csv or warm-pass-2.png
notes.md
```

`system.md` must contain the environment table above. `settings-hashes.txt` must contain SHA-256 values for every active configuration and renderer artifact. Redact account names and unrelated paths from `GTAIV_d3d9.log`; do not redact effective renderer version, accepted configuration lines, adapter name, API path, or errors.

## 4. Report results honestly

For each pass, report average FPS, 1% low, 0.1% low, sustained frame-time behavior, visible streaming issues, crashes, and whether the result changed after cache warm-up. Do not call first-route stutter a permanent profile regression without warm-pass comparison. Do not call a warm-pass result universal unless another documented system independently repeats it.

> **Evidence gate:** a renderer profile may be named as a reference configuration before testing, but it may be called **tested** or **log-validated** only after the required log, hashes, route description, and warm-pass capture are committed with it.

## 5. Compatibility reports

Open a GitHub issue or attach an evidence directory using this minimum report:

```text
CPU / APU:
GPU:
RAM amount and channel configuration:
Windows version:
Graphics driver and version:
GTA IV build:
FusionFix version and SHA-256:
DXVK or GPLAsync artifact and SHA-256:
Resolution, refresh rate, limiter, and VSync:
Active configuration hashes:
Extra content/plugins/shaders:
Episode tested:
Cold-pass result:
Warm-pass result:
Crash or visual symptoms:
GTAIV_d3d9.log excerpt:
```
