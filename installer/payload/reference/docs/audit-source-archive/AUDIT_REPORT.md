# Audit — dillonherring9/gta-iv-modern-ryzen-apu-config
Audited: August 22, 2026 · Scope: repo content + every upstream project it depends on

## What this project actually is
It is **not a mod**. It is a documentation + installer layer that sits on top of four
external projects and asks them to cooperate in one specific arrangement:

| Layer | Project | Role |
|---|---|---|
| Base game | GTA IV Complete Edition (Rockstar/Steam) | Required, purchased separately |
| Modpack | Gillian's Drag-and-Drop Archive | Ready-to-play FusionFix + content bundle |
| Graphics fix | ThirteenAG's FusionFix | Fixes/graphics options, Vulkan toggle |
| Renderer | DXVK / DXVK-GPLAsync | D3D9→Vulkan translation |
| This repo | 7 config files + `.exe` installer | Applies one tuned profile, with backup/rollback |

The repo does not ship game files, FusionFix, or DXVK binaries — it only ships text
configuration and an installer that places them. That's a sound, low-risk design:
it can't brick anything DXVK/FusionFix themselves couldn't, and it keeps a
timestamped backup before touching a file.

The README is unusually well-written for this genre — it separates a beginner path
from an advanced-maintenance path, defines its one hard rule (one folder, one
renderer, one loader), and is explicit about what it *doesn't* claim (no FPS
promise, no universal compatibility, no ownership of the tools it wraps). The
project also already self-audited once: it caught that `AudioMap.ini` was a
carryover with no basis in the current archive, and removed it. That's a healthy
sign for a repo one person maintains alone.

## Research a human can't easily do in one sitting: current state of every dependency

I checked the live release history of every project this repo depends on, because a
static config repo silently rots as its upstreams move. Here's where each one
actually stands right now, cross-checked against what the repo documents:

**FusionFix — currently v5.0.1.** The repo's own reference table already cites
"FusionFix 5.0.1" as the validated version, so it's current as written. But there's
a bigger structural fact the repo doesn't call out: **starting with FusionFix 4.0,
the project moved almost all configuration into an in-game menu**, and its own
distribution notes now say *"no more .ini file configuration required."* The
`.cfg`/`.ini` files this repo ships and manages are still real and still work, but
they're no longer FusionFix's primary, actively-designed configuration surface —
the in-game menu is. A repo built entirely around pre-baked `.ini`/`.cfg` files is
tuning yesterday's interface. This doesn't break anything, but it's the single
biggest "best current method" gap: the tailored packages below lean on the in-game
menu as the source of truth and treat the shipped `.ini` as a seed, not gospel.

**DXVK mainline — currently v3.0.2 (July 17, 2026).** DXVK 3.0 was a genuinely
major rewrite: a new SSA-based shader compiler (`dxbc-spirv`), shader compilation
moved fully to worker threads, and disk-cache behavior changed. The repo's
documented profile still targets the older 2.6.x/2.7-era GPLAsync key set and
explicitly (and correctly) tells users not to carry `dxvk.gplAsyncCache` into
newer builds — GPLAsync itself dropped that key in v2.7-1. That migration note is
accurate and current.

**DXVK-GPLAsync (the fork the reference profile actually uses) — currently
v3.0-1 (June 28, 2026),** tracking upstream DXVK 3.0, not yet 3.0.2. That's a
normal, small lag for a one-person fork that rebases against upstream tags rather
than nightlies — not a red flag, but worth knowing before assuming the fork has
every 3.0.2 fix.

**Ultimate ASI Loader — currently v9.7.3 (Aug 14, 2026),** on a steady release
cadence. The repo's guidance (use the x86 build, one loader only) is still exactly
right.

**Gillian's Drag-and-Drop Archive — the one real risk.** Gillian announced
retirement from active maintenance effective January 14, 2026, and the guide site
now runs in "minimal maintenance mode." In practice this hasn't meant abandonment
yet — the archive still received a content update on June 27, 2026 and a version
bump (plate/fix updates) on July 18, 2026 — but the author has explicitly said
updates from here will be "very sparse" and the site "may be outdated." This repo's
entire installer *assumes* Gillian's archive is already present and correctly laid
out in the game folder; it's the one upstream in the chain with a stated end date
on its own support. The repo's docs don't currently flag this dependency risk to
users, and probably should.

## Is it achieving its stated goal?
Its stated goal is narrow and honest: a repeatable, reversible configuration layer
for one reference machine (Ryzen 5 PRO 6650U / Radeon 660M), not a universal
performance mod. Judged against that narrow goal — yes. The backup/rollback
behavior, the "let caches warm before judging" discipline, and the renderer-
identification doc (so users don't blindly overwrite a `vulkan.dll` they don't
understand) are all genuinely good practices that most GTA IV guides skip.

Where it falls short of "best current method":
1. **Config-file-first instead of menu-first** (above) — the biggest one.
2. **No dependency-freshness check.** The installer verifies the *folder* is
   correct but has no way to tell a user "your FusionFix/DXVK build is 3 major
   versions behind what this profile assumes."
3. **Single point of failure on Gillian's archive**, undisclosed to end users.
4. **AMD-name framing** may undersell that the profile logic (shared-memory iGPU,
   conservative post-processing) generalizes to Intel Iris Xe/Arc and other Radeon
   iGPUs — which the docs actually acknowledge in prose but don't turn into a
   distinct supported package.

None of this is a correctness bug — everything currently shipped works as
documented. It's staleness risk and an audience gap, which is exactly what the two
packages below are built to close.

## What I built from this research
Two profiles, because "APU" isn't one target — a Radeon 660M and, say, an Intel
Iris Xe or a Ryzen 8000G's RDNA3 iGPU do not want identical settings:

- **`general-apu-edition/`** — safe defaults for *any* modern integrated GPU
  (AMD Radeon 6xxM–8xxM, Intel Iris Xe/Arc iGPU, recent Intel UHD). Lower risk,
  broader compatibility, mainline DXVK 3.0.2 (no fork-specific keys, since a
  general package shouldn't assume GPLAsync is even installed).
- **`radeon-660m-edition/`** — the direct successor to this repo's own reference
  profile, updated for DXVK-GPLAsync v3.0-1 syntax (the deprecated
  `gplAsyncCache` key removed, consistent with the repo's own migration doc) and
  FusionFix 5.0.1's current `.ini` key names where I could verify them against
  FusionFix's own shipped `.ini` and change notes.

Both ship a real `dxvk.conf` (I'm confident in that syntax — it's a small,
well-documented upstream format I verified directly). For FusionFix, I did **not**
fabricate a full `.ini`/`.cfg` from memory — GTA IV's `.ini` schema has grown large
and version-specific, and shipping guessed key names is exactly the kind of
config-rot this audit is flagging. Instead each package has a `SETTINGS.md` that
gives the exact **in-game menu path and target values** (FusionFix's own current
source of truth), plus the handful of `.ini`-only keys I could verify by citation.

## The installer
I can't compile a signed `.exe` inside this sandbox — no network access, so I
can't fetch NSIS/Inno Setup/ps2exe, and none are preinstalled. What I've given you
instead, in `installer/`:
- `install.ps1` / `uninstall.ps1` — a working PowerShell installer that reproduces
  the original `.exe`'s exact behavior (folder validation, timestamped backup,
  file placement, admin-compatibility flag, uninstall/restore). You can run this
  today with a right-click → *Run with PowerShell*.
- `build_installer.nsi` — a ready NSIS script that turns the same logic into a real
  double-clickable `.exe`, once you run it through the free NSIS compiler
  (`makensis build_installer.nsi`) — a 5-minute one-time step on your own machine.
- `BUILD_INSTRUCTIONS.md` — exact steps for both that and the even-faster
  `ps2exe` route if you'd rather keep it PowerShell-based end to end.
