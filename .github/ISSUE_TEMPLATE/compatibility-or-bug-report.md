---
name: Compatibility or bug report
aabout: Report a reproducible Version 3 issue or a carefully tested compatibility result
title: "[Report]: "
labels: "needs-triage"
---

## Before submitting

Please read [`CONTRIBUTING.md`](../../CONTRIBUTING.md) and [`docs/MAINTAINER_HANDOFF.md`](../../docs/MAINTAINER_HANDOFF.md). This project is a configuration layer for a pre-existing Windows GTA IV Complete Edition installation with Gillian’s Drag-and-Drop Archive. It cannot safely diagnose a game folder made from several unrelated guides at once.

Use complete sentences. A screenshot showing only an FPS number is not enough to tell a cold cache, driver issue, archive issue, extra-mod conflict, or Version 3 regression apart.

## Installation context

```text
GTA IV build:
Drag-and-Drop Archive version/date:
Version 3 asset/version used:
Did you install the archive into the folder containing GTAIV.exe?:
Did you run GTAIVSetupUtility.exe and set launch options?:
Is vulkan.dll present beside GTAIV.exe?:
Did you add/update FusionFix, DXVK, an ASI loader, or another mod after the archive?:
```

## Computer and display

```text
Windows version:
CPU:
GPU (integrated/discrete):
RAM amount and channel configuration:
GPU driver version:
Laptop power mode / desktop power context:
Resolution and refresh rate:
```

## The exact result

```text
Exact symptom:
When it begins:
Exact route/scene/mission used to reproduce it:
Does it happen in the main game?:
Does it happen in TLAD?:
Does it happen in TBoGT?:
Relevant error text or log excerpt:
```

## Cache-aware testing

```text
What changed immediately before the problem?:
Did you fully restart GTA IV after the change?:
Cold first-run result:
Repeat warm-run result after replaying the same route:
```

## Recovery test

```text
What did you restore or remove?:
Did Version 3 uninstall/backup restore the prior state?:
Did the symptom remain after reverting the last change?:
```

## Sources and extras

List every extra mod, renderer/loader replacement, and source link relevant to this report. If you propose a configuration change, explain why the current setting is insufficient, what you tested, and how a user can undo it.
