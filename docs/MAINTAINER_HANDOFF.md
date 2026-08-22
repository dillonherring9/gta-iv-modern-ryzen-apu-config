# Maintainer Handoff and Community Continuity Guide

> **Project posture:** This repository is intended to become self-sustaining. The current owner assembled and documented a reproducible GTA IV Complete Edition configuration path; they are not promising indefinite personal support. A future maintainer, organization, or independent successor may adopt the work only by preserving its evidence, credits, safety boundaries, and rollback behavior.

The goal is not to make people dependent on one person’s computer or one installer. The goal is to leave a clear enough record that a careful new maintainer can understand what Version 3 does, reproduce its release, decide whether a reported issue is real, and improve or supersede it honestly.

## What is stable today

The current recommended release is **Version 3 — Complete Cumulative Configuration**. It is for Windows GTA IV Complete Edition after Gillian’s complete Drag-and-Drop Archive has been installed in the folder containing `GTAIV.exe`. Version 3 writes seven managed configuration files, backs them up, enables the required per-user `RUNASADMIN` compatibility setting for `GTAIV.exe`, and restores both the managed files and previous compatibility value during uninstall.

| Stable commitment | What a successor must preserve |
|---|---|
| **Complete package** | Every public installer/package remains cumulative: configuration, source, credits, history, evidence, validation guidance, and rollback material travel together. |
| **Bounded installer** | The installer manages only `dxvk.conf`, `pc\stream.ini`, Console Select Menu, two FusionFix files, Xbox Rain Droplets, and LibertyCityPlates settings. It must not become a blind modpack/binary distributor. |
| **Recovery path** | The timestamped backup, restore prompt, and restoration of the former `GTAIV.exe` compatibility value remain mandatory. |
| **Known prerequisite** | The complete Drag-and-Drop Archive is already installed. The installer checks `GTAIV.exe`, `vulkan.dll`, and a closed game process; it does not require one exact FusionFix ASI filename. |
| **Truthful performance language** | The Ryzen 5 PRO 6650U/Radeon 660M setup is a reference context, not a universal benchmark, hardware requirement, or performance guarantee. |
| **Evidence retention** | Historical notes, original README reasoning, dependency records, source links, and past corrections stay available even when a current install rule changes. |
| **AudioMap exclusion** | AudioMap is historical evidence only. It must not return to an active payload or installer without new source evidence that it belongs to the archive. |

## What “taking over” actually means

A maintainer does not need to own the same laptop or promise support for every mod stack. They need to make changes in a way another person can inspect and reverse.

| Responsibility | Minimum standard |
|---|---|
| **Triage** | Ask for the GTA IV build, archive version/date, GPU/driver, resolution, RAM channel configuration, changed files, a precise symptom, whether the game was restarted, and whether the same route was repeated after cache warm-up. |
| **Scope control** | Separate archive/setup mistakes, third-party mod conflicts, graphics-driver problems, and Version 3 configuration regressions. Do not fix a one-off issue by silently adding more unrelated DLLs or plugins. |
| **Source checking** | Prefer the original mod/guide documentation for FusionFix, DXVK, Gillian’s archive, and individual mods. Record the URL, version/date, and why it applies. |
| **Controlled change** | Change one renderer, loader, configuration, or shader-metadata variable at a time. Preserve the prior file and define how to undo the test. |
| **Testing** | Test game startup, a save/new game, interiors, dense daytime traffic, night/rain, a reload, and both EFLC episodes where available. Restart and repeat the same route before judging cache-related performance. |
| **Release evidence** | Run the payload checksum verification, compile the NSIS installer, keep source/payload documents synchronized, and verify the published SHA-256 manifest after downloading the release assets. |
| **Honest notes** | State whether a result was reproduced, observed only on one setup, inferred from upstream documentation, or not tested. Never turn an untested suggestion into a guaranteed performance claim. |

## Safe lifecycle for a configuration change

A future maintainer should use this sequence for any non-trivial configuration, dependency, or installer change.

1. **Describe the problem before editing.** Link the report, define the supported game/archive context, and state what the expected behavior is.
2. **Record the current baseline.** Save the present file, dependency version, installer manifest, and a before-test note. The project history is evidence, not clutter.
3. **Use the smallest test.** Change one file or one setting. Do not combine an archive update, DXVK replacement, FusionFix update, and performance-tuning change in one experiment.
4. **Test repeatably.** Restart when a graphics/renderer change requires it, replay the same demanding route after cache warm-up, and test both main game and episodes when the change can affect them.
5. **Explain the decision.** If the change is accepted, write why it is safer/better, what it does not solve, where it was tested, and how to roll it back.
6. **Update the cumulative package.** Update the source configuration, installer payload, embedded reference copies, checksums, README/release notes, dependency records, and history as appropriate.
7. **Replace the current major asset in place.** Until there is a genuine long-lived compatibility break, Version 3 corrections replace the existing `v3` assets. Do not create `v3.0.1`, `v3.0.2`, or a string of partial installers.
8. **Verify the public artifact.** Download the release as an ordinary user would, run `sha256sum -c SHA256SUMS.txt`, confirm `SOURCE_COMMIT.txt`, and inspect the package/installer contents.

## Release decision rules

| Situation | Correct action |
|---|---|
| Documentation correction, better explanation, checksum refresh, installer fix, or evidence-backed adjustment within the same Complete Edition/archive contract | Update source, move only the existing `v3` tag to the reviewed commit, and replace the four existing Version 3 assets in place. |
| New statement based on one person’s test | Publish it as a clearly bounded observation, not a global recommendation. Ask for reproducible reports before promoting it to the default profile. |
| A proposed DXVK, loader, FusionFix, shader, or archive change | Treat it as an advanced controlled experiment. Keep the known-good copy, change one component, inspect the log, test warm runs, and preserve a rollback path. |
| A new requirement that materially breaks the established target | Create a documented compatibility decision first. A new major release is justified only if the new target is durable, complete, and cannot honestly remain Version 3’s contract. |
| A third party’s mod/binary/content | Link and credit the original source. Do not redistribute it without the rights holder’s permission or turn this repository into an unreviewed bundle. |
| A request to “fix performance” by adding random wrappers/limiters | Decline the stacking approach. Identify whether the issue is a cold cache, CPU/streaming pressure, driver behavior, archive problem, or genuine configuration regression. |

## How contributors can help without creating a mess

Reports are more valuable than vague praise or a raw FPS screenshot. A contributor should describe their hardware, operating system, game build, archive version/date, driver version, active renderer, resolution, memory channels, extra mods, exact symptom, reproduction route, cold-versus-warm behavior, restart behavior, and both-episode result where relevant.

A proposed contribution should identify every file changed; explain why the current version is insufficient; link authoritative upstream material; state what was tested; state what was not tested; preserve a rollback plan; and avoid removing sources, credits, history, or unrelated configuration data. A pull request that changes only a file but cannot explain the tested contract is not ready to merge.

## Users are free to adopt a better maintained route

This repository is a configuration reference, not a permanent loyalty obligation. If the underlying archive, FusionFix, a successor community project, or a trusted maintainer provides a more current and clearly documented route, users may adopt it. The correct transition is to back up the working game folder; read the successor’s scope and compatibility claims; uninstall/restore this package if it manages the same files; change one ecosystem at a time; and retain the old backup until the replacement has passed the same startup, repeat-route, and EFLC checks.

> **Do not mix two competing “complete” configurations in one game folder.** Decide which project owns the renderer/configuration path, then follow that project’s instructions. A clean move to a better successor is healthier than endlessly stacking both projects together.

## Ownership, transfer, and licensing boundary

A handoff is not the same as granting strangers unrestricted control. The repository owner should choose any GitHub collaborator, organization transfer, archival decision, or ownership transfer deliberately. This document does not add collaborators, transfer the repository, or make any legal commitment.

The repository currently has **no declared repository license**. That means a future maintainer should not assume that copying, relicensing, or redistributing the repository’s original documentation/configuration is automatically authorized merely because the repository is public. Before inviting formal forks, accepting outside code, or transferring control, the owner should make an intentional licensing decision that respects their own work and all third-party materials. The project’s existing boundary remains: it does not redistribute GTA IV, the Drag-and-Drop Archive, FusionFix/DXVK binaries, shaders, or other third-party mod content.

> **Practical handoff option:** Keep this repository as a documented reference while a successor maintains an independent project; link to that successor only after its scope, credits, license, and verification practices are clear. A repository transfer or new governance structure can happen later if the owner chooses it explicitly.

## Maintainer starting points

| If you need to understand… | Start here |
|---|---|
| The beginner installation path | [`GETTING_STARTED.md`](GETTING_STARTED.md) and the top of [`../README.md`](../README.md) |
| The original self-maintenance logic and DXVK replacement procedure | [`../README.md`](../README.md), especially **Original maintenance guide, preserved for self-maintenance** |
| What Version 3 is permitted to install | [`COMPLETE_INSTALL_CONTRACT.md`](COMPLETE_INSTALL_CONTRACT.md) |
| Why current rules differ from the original guide | [`ORIGINAL_README_RECONCILIATION.md`](ORIGINAL_README_RECONCILIATION.md) |
| Renderer/component/shader boundaries | [`COMPONENTS_AND_COMPATIBILITY.md`](COMPONENTS_AND_COMPATIBILITY.md) and [`preload_reconciliation.md`](preload_reconciliation.md) |
| Version/file context | [`DEPENDENCY_LOCKS.md`](DEPENDENCY_LOCKS.md) |
| How to evaluate results | [`VALIDATION_PROTOCOL.md`](VALIDATION_PROTOCOL.md) |
| How the installer is built and release assets are verified | [`../installer/BUILDING.md`](../installer/BUILDING.md) and [`../BUILD_ENVIRONMENT.md`](../BUILD_ENVIRONMENT.md) |
| What decisions and corrections came before | [`PROJECT_HISTORY.md`](PROJECT_HISTORY.md) and [`history/`](history/) |

## Source boundary

The handoff model relies on primary project documentation for current component behavior. The current underlying route is documented by FusionFix and Gillian’s Drag-and-Drop Archive.[1] [2]

## References

[1] [ThirteenAG — GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)

[2] [Gillian’s GTA IV Modding Guide — Drag-and-Drop Archive](https://gillian-guide.github.io/drag-and-drop-archive/)
