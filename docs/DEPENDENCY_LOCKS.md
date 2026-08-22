# Version 3.0.3 Dependency Locks

Version 3.0.3 does not redistribute third-party binaries. This document records the exact public artifacts relevant to the core installer and optional GPLAsync overlay so a configuration result can be reproduced or rejected on evidence.

| Component | Required use | Artifact | SHA-256 | Boundary |
|---|---|---|---|---|
| FusionFix | Required before the core installer writes its FusionFix profiles | `GTAIV.EFLC.FusionFix.zip`, release `v5.0.1` | `3c202398c133392be985854654f169514e055812cc302ef24e6aa97495975b41` | The installer checks for `plugins\GTAIV.EFLC.FusionFix.asi` and `vulkan.dll`, but it cannot prove this hash at runtime. Verify it before installation. |
| DXVK GPLAsync | Optional renderer overlay only | `dxvk-gplasync-v2.6.2-1.tar.gz` | `c645d3f2550ff66d0a51ba27a535aaa7351fd087feb7b298e7283cb2a3ee173b` | Required only for `tuned/renderers/dxvk-gplasync-v2.6.2-1.conf`. Do not apply that configuration to upstream DXVK or another GPLAsync version. |
| Upstream DXVK | Stock renderer profile | Player-selected compatible DXVK build | Not locked by this package | `tuned/dxvk-stock.conf` contains upstream-supported configuration keys. Record the exact artifact/hash in validation evidence. |

## Verification

Use the release pages named below and verify the downloaded archive before extracting it. On Windows PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 .\GTAIV.EFLC.FusionFix.zip
Get-FileHash -Algorithm SHA256 .\dxvk-gplasync-v2.6.2-1.tar.gz
```

A different hash is not automatically malicious, but it is not the artifact described by this Version 3.0.3 contract. Do not call a result comparable to the reference configuration without recording the actual artifact and hash.

## Source links

- [FusionFix v5.0.1 release](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/releases/tag/v5.0.1)
- [DXVK GPLAsync v2.6.2-1 release](https://gitlab.com/Ph42oN/dxvk-gplasync/-/releases/v2.6.2-1)
- [DXVK upstream releases](https://github.com/doitsujin/dxvk/releases)
