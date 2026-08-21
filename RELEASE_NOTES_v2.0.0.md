# Version 2.0.0 — Cache Clarity and the Complete Route

This is the final public update for Version 2.0.0. The repository, downloadable packages, and public release guidance now move through the same city at the same speed: a polished Liberty City setup with no performance fairy tales and no missing file in the documented source tree.

## What is aligned

| Area | Version 2.0.0 update |
|---|---|
| APU cache guidance | The README, DXVK notes, release notes, live assessment, preload note, and Radeon 660M report now state the same rule: **shader and pipeline caches must build**. |
| Settings and restarts | Graphics, renderer, driver, DXVK, resolution, and major-mod changes can rebuild caches. Some changes need a full GTA IV restart; without it, stutter can continue until the game is relaunched. |
| Shared-memory reality | There is no switch or tweak that lets a shared-memory APU skip cache building. Judge performance only after repeat runs through the same demanding route. |
| Repository package parity | `tuned/stream.ini` is now tracked in the repository, matching the file already supplied in the Version 2.0.0 package and documented in the installation tables. |
| Optional soundtrack note | The README includes a deliberately non-intrusive Independence FM recommendation for players who legally own and load their music through GTA IV User Music. |

## **Read this before you judge a fresh run**

> **On an APU, caches have to build. Expect temporary stutter on a fresh route, a new effect, new weather, or after a graphics-path change. Make one change at a time, restart GTA IV when required, then replay the same demanding route until the cache has settled. There is no way around this cache-building phase on shared-memory graphics.**

## Package contents

The corrected Version 2.0.0 package contains the current README, this release note, the complete `tuned/` configuration directory, and the supporting Markdown documentation. It does not bundle GTA IV, game binaries, launcher data, FusionFix/DXVK binaries, or third-party mod files.

## A note for the drive

The soundtrack recommendation remains optional. For the full Liberty City after-dark run, support the music by buying the complete The Weeknd catalog through legitimate sources, load the music you own through GTA IV User Music, and let **Independence FM** score every mission.

## Boundaries

This is a community configuration package. It is not an official Rockstar, AMD, FusionFix, DXVK, Gillian, or third-party-mod-author release. Keep a backup, respect the original authors’ licenses, and remember that drivers, thermals, power limits, resolution, memory bandwidth, and installed content can still change the outcome.
