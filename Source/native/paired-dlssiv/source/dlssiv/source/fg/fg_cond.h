// Frame generation's conditions as pure functions over plain numbers, so a bench (tools/test-fg-cond) and the harness run every
// combination. The first row that holds is the answer and its line. SESSION rows need a relaunch, RESET rows are asked at every
// build and Reset, STANDBY rows clear by themselves. None stops the upscaler. The law (FgDrive*) is at the file's end.
#pragma once

enum FgCond
{
    FGC_OK = 0,
    // ---- SESSION
    FGC_DEAD,          // its machine stopped itself (fg_machine.h, FG_DEAD)
    FGC_RESTORE,       // a game image kept a presented picture
    FGC_PRESENT,       // the swapchain's saved Present is not the device thread's
    FGC_MARKER,        // the frame-end marker does not exist: the real frame's fire signals it
    FGC_AGILITY,       // an Agility runtime (DLSSIV\D3D12) beside the helper
    FGC_DXVK,          // a DXVK that is not 3.1
    FGC_ASYNC,         // async_home=1: a pipelined pair is never shown
    // ---- RESET
    FGC_REFRESH,       // the pacer has no floor without it
    FGC_SWAPCHAIN,     // an untested swapchain shape
    FGC_EXCLUSIVE,     // exclusive fullscreen: parked
    FGC_PANEL,         // C under the floor (a panel under about 97 Hz)
    FGC_FP16,          // an FP16 back buffer and HDR is not on
    FGC_SDR,           // SDR with no HUD-less picture; in a build that arms only
                       // where the back buffer is multisampled
    FGC_FIXED,         // known not to run variable refresh for the game
    FGC_VRR_UNKNOWN,   // the mod cannot tell whether it does
    // ---- STANDBY
    FGC_FFCAP,         // a FusionFix limiter at or under 1.03 C
    FGC_DXVKCAP,       // it counts presents
    FGC_MENU,          // a menu is up
    FGC_FAST,          // above what one hold fits (about 67)
    FGC_SLOW,          // under the floor (38; engaged R_free under 35)
    FGC_NVCAP,         // NVIDIA's Max Frame Rate at or under 2 C (3 percent margin): the driver caps every present
                       // rows from here on are appended, so fg_lift's bits keep their meaning
    FGC_ONSLOW,        // standing by, R_on = 1 / (1/R + c) is under the floor
    FGC_VSYNCOFF,      // a fixed screen the fixed mode may run on, with vsync off: a generated frame has no refresh to land on
    FGC_N
};
enum FgCondClass { FGCC_SESSION = 0, FGCC_RESET = 1, FGCC_STANDBY = 2 };

// -1 where the mod cannot tell.
struct FgCondIn
{
    int machineDead;
    int restoreLost;
    int presentOurs;
    int markerOk;           // 1: the frame-end marker exists
    int agility;
    int dxvk31;             // 1 DXVK 3.1, 0 another version, -1 not known
    int asyncNamed;
    int asyncHome;
    int refreshNum, refreshDen;   // 0/0 unread
    int lockPm;             // LOCK in thousandths (1015): C = refresh / (2 x LOCK)
    int swapEffect;         // 1 DISCARD, 2 FLIP, 3 COPY, 4 OVERLAY, 5 FLIPEX; 0 unread
    int backBuffers;        // 0 unread
    int windowed;           // 1 a window or borderless, 0 exclusive fullscreen
    int bbFp16;
    int hdrOn;
    int vrr;                // 1 variable, 0 fixed, -1 unknown: NVIDIA's answer checked against the flip classifier (FgShipVrr)
    int ffCap;              // fps, 0 none
    int ffRel;              // the refresh less this, 0 none
    int dxvkCap;            // 0 none
    int menu;
    int realFpsX10;         // tenths, 0 not yet: standing by the game's own; engaged R_on (SLOW reads it)
    int engaged;            // the two rate rows hold a little longer then
    unsigned lift;          // bit 1 << FGC_*
    int nvCap;              // fps, 0 none
    int freeFpsX10;         // engaged, R_free in tenths (FAST reads it; 0: realFpsX10)
    int predFpsX10;         // standing by, predicted with frame generation; 0 none
        // ---- the fixed-refresh mode
    int vsync;              // DXVK presents in a FIFO mode
    int flipQuant;          // 1: the classifier read QUANTISED where the pacer did not hold the game; -1 FREE there
    int fixedKey;           // fg_fixed: 0 refused, 1 the fixed mode, 2 the mode with its period's correction off
    int fixLUs;             // engaged in the fixed mode, the game's 98th percentile cost over 4 s, us; 0 none
    int fixOver;            // 1: most frames overrun the held period (each a refresh shown twice), FgCondFixShareStep
    int fixSharePm;         // that share, per mille; -1 none
    int fixDipFirst;        // held rate under 99 percent of half the refresh: 1 first window (SLOW waits), 2 third on (SLOW stands), else 0
    int fixEntryX10;        // standing by after an overrun, the rate the mode asks before it engages again; 0 none
    int engaging;           // FGC_ONSLOW holds only past its hysteresis
    int sdrMsaa;            // 1: multisampled back buffer, so no HUD-less picture of it can be made (FGC_SDR)
};
// The table's constants, each a parameter.
struct FgCondK
{
    int minCFpsX10;         // 480: C under this is a panel under about 97 Hz
    int ffNearPm;           // 1030: a FusionFix limiter at or under this many thousandths of C is refused (X10)
    int maxRealFpsX10;      // 670: above this the pair does not fit one hold
    int minRealFpsX10;      // 380: the floor; engaged, out under 35
    int rateHystX10;        // 30: engaged the rate rows hold only this far past the bound (no flapping)
    int minBackBuffers;     // 1, and
    int maxBackBuffers;     // 3: one to four swapchain images
                            // In the game only two back buffers have run: set both to 2 to hold to that.
    int unknownVrrRefused;  // 0 lets "cannot tell" through
    int minKeyed;           // 1: fg_min_fps set the floor on every display; 0: the floor by display (FgCondMinX10)
};
static inline FgCondK FgCondDefaults()
{
    FgCondK k = { 480, 1030, 670, 380, 30, 1, 3, 1, 0 };
    return k;
}
// The floor by display: 30 on a 59 to 61 Hz screen, minRealFpsX10 elsewhere, fg_min_fps everywhere when set. A 59.94 Hz
// screen's half refresh is rounded, not truncated, so it does not read 29.9 against 30 (FgCondHalfRoundX10).
enum { FG_COND_FLOOR_60HZ_X10 = 300, FG_COND_60HZ_LO_X1000 = 59000, FG_COND_60HZ_HI_X1000 = 61000 };
static inline int FgCondHzX1000(const FgCondIn& in);
static inline int FgCondHalfRoundX10(int hzX1000) { return hzX1000 > 0 ? (hzX1000 + 100) / 200 : 0; }
static inline int FgCondMinX10(const FgCondIn& in, const FgCondK& k)
{
    if (k.minKeyed) return k.minRealFpsX10;
    const int hz = FgCondHzX1000(in);
    if (hz >= FG_COND_60HZ_LO_X1000 && hz <= FG_COND_60HZ_HI_X1000) { const int h = FgCondHalfRoundX10(hz); return h < FG_COND_FLOOR_60HZ_X10 ? h : FG_COND_FLOOR_60HZ_X10; }
    return k.minRealFpsX10;
}

// The refresh in thousandths of a hertz, 0 unread; C, the ceiling of the real rate, in tenths.
static inline int FgCondHzX1000(const FgCondIn& in)
{
    if (in.refreshNum <= 0 || in.refreshDen <= 0) return 0;
    return (int)((long long)in.refreshNum * 1000 / in.refreshDen);
}
// The fixed mode: a present shows only on the refresh grid, so the game is held to two refreshes a pair. The screen is fixed by
// NVIDIA's no or the mode's memory (flipQuant); the panel's FREE (flipQuant -1) overrules NVIDIA's no.
static inline bool FgCondFixedSeen(const FgCondIn& in) { return in.vrr == 0 || in.flipQuant > 0; }
static inline bool FgCondFixedScreen(const FgCondIn& in) { return in.fixedKey > 0 && ((in.vrr == 0 && in.flipQuant >= 0) || (in.vrr < 0 && in.flipQuant > 0)); }
static inline bool FgCondFixedMode(const FgCondIn& in) { return FgCondFixedScreen(in) && in.vsync != 0; }
// 1015 on a variable display, exactly 1000 in the fixed mode (PaceFixed corrects toward the panel's true rate).
static inline int FgCondLockPm(const FgCondIn& in) { return FgCondFixedMode(in) ? 1000 : in.lockPm; }
static inline int FgCondCFpsX10(const FgCondIn& in)
{
    const int hz = FgCondHzX1000(in), lock = FgCondLockPm(in);
    if (!hz || lock <= 0) return 0;
    return (int)((long long)hz * 10 / (2LL * lock));
}
// Half the refresh in tenths.
static inline int FgCondHalfX10(const FgCondIn& in) { return (int)((long long)FgCondHzX1000(in) / 200); }
// In tenths, 0 none: the cap, or the integer refresh less the Custom figure.
static inline int FgCondFfFpsX10(const FgCondIn& in)
{
    if (in.ffCap > 0) return in.ffCap * 10;
    if (in.ffRel > 0) { const int hz = (FgCondHzX1000(in) + 500) / 1000; return hz > in.ffRel ? (hz - in.ffRel) * 10 : 0; }
    return 0;
}
// Two presents a real frame at C, in tenths.
static inline int FgCondPairPresentsX10(const FgCondIn& in) { return 2 * FgCondCFpsX10(in); }
// maxRealFpsX10 at a 120 Hz panel's C, scaled with C. In the fixed mode 97 percent of the refresh (vsync caps the game there).
static inline int FgCondTopX10(const FgCondIn& in, const FgCondK& k)
{
    if (FgCondFixedMode(in)) { const int hz = FgCondHzX1000(in); if (hz > 0) return (int)((long long)hz * 97 / 10000); }
    const int cf = FgCondCFpsX10(in);
    return cf > 0 ? (int)((long long)k.maxRealFpsX10 * cf / 591) : k.maxRealFpsX10;
}
static inline int FgCondFastRateX10(const FgCondIn& in) { return in.freeFpsX10 > 0 ? in.freeFpsX10 : in.realFpsX10; }
// With vsync on a fixed screen DXVK's FIFO shows every frame once, so the screen gets the game's mean rate, which the median
// misses. The mean caps each interval at three refreshes: GTA IV's missed frames are mostly 46 to 50 ms, and a stall still
// counts as one frame. FgCondFixedFreeX10 returns the field unchanged when engaged, variable, or with no intervals.
#define FG_COND_FIXMEAN 1   // tools/fg-cond-replay builds against older tables too
enum { FG_COND_FIX_MEAN_CAP = 3 };
static inline int FgCondFixedMeanUs(const float* ms, int n, int hzX1000)
{
    if (hzX1000 <= 0 || n <= 0) return 0;
    const double cap = (double)FG_COND_FIX_MEAN_CAP * 1.0e6 / (double)hzX1000;   // (ms)
    double s = 0.0; int k = 0;
    for (int i = 0; i < n; i++) { double v = (double)ms[i]; if (!(v > 0.0)) continue; if (v > cap) v = cap; s += v; k++; }
    return k > 0 ? (int)(s * 1000.0 / (double)k + 0.5) : 0;
}
static inline int FgCondFixedScreenX10(int meanUs) { return meanUs > 0 ? (int)(10000000LL / meanUs) : 0; }
static inline int FgCondFixedFreeX10(const FgCondIn& in, int n, int meanUs)
{
    if (in.engaged || in.freeFpsX10 > 0 || !FgCondFixedMode(in) || n <= 0) return in.freeFpsX10;
    const int s = FgCondFixedScreenX10(meanUs);
    return (s > 0 && s < in.realFpsX10) ? s : in.freeFpsX10;
}
// In the fixed mode, standing by, the same reading FAST used, else the median: with the median alone a game at 119.6 on a
// fixed 120 would engage and then be stood down by FGC_FAST every minute.
static inline int FgCondAloneX10(const FgCondIn& in)
{
    if (!in.engaged && FgCondFixedMode(in)) { const int f = FgCondFastRateX10(in); if (f > 0) return f; }
    return in.realFpsX10;
}
#define FG_COND_ALONE_FAST 1   // the harness builds against older tables too
// Held under the top less its hysteresis so the band is never empty. In the fixed mode half the refresh plus 3 percent.
static inline int FgCondFloorX10(const FgCondIn& in, const FgCondK& k)
{
    if (FgCondFixedMode(in)) { const int h = FgCondHalfX10(in); if (h > 0) return (int)(((long long)h * 103 + 99) / 100); }
    const int most = FgCondTopX10(in, k) - k.rateHystX10;
    const int mn = FgCondMinX10(in, k);
    return mn > most ? most : mn;
}
// The fixed mode's overrun. Engaged, the window's median stays at half the refresh whatever the game costs, so SLOW cannot see a
// game whose frames pass the period. A window is over where more than FG_COND_FIX_SHARE_PM per mille of the game's own frames of
// the last 12 s (the law's slow ring, time in Present left out) took longer than two refreshes plus FG_COND_FIX_OVER_US (half
// PaceFixed's 500 us of queue slack), and the last 4 s are still over (E over the line), so a passed burst stands nothing down.
// FG_COND_FIX_OVER_WIN windows over running stand the mode down (FGC_SLOW). Judged by the bulk, not the 98th percentile: GTA IV's
// ordinary streaming spikes would trip that. FG_COND_FIX_SHARE_MIN_N is one window of real frames.
enum { FG_COND_FIX_OVER_US = 250, FG_COND_FIX_OVER_WIN = 2, FG_COND_FIX_SHARE_PM = 100, FG_COND_FIX_SHARE_MIN_N = 120 };
// The share of a sorted ring (FgLawRing, ms ascending) over thrUs, per mille; -1 under minN. Read without a lock on purpose: the
// render thread's push and pop shift the array by one place, so a read may land a place off or see a torn double. A template, so
// this file needs no fg_law.h.
template <class R> static inline int FgCondRingOverPm(const R& r, int thrUs, int minN, int* nOut = nullptr)
{
    const int cap = (int)(sizeof(r.s) / sizeof(r.s[0]));
    int n = *(const volatile int*)&r.n;
    if (n < 0) n = 0; if (n > cap) n = cap;
    if (nOut) *nOut = n;
    if (n < minN || n <= 0 || thrUs <= 0) return -1;
    const double thr = (double)thrUs / 1000.0;
    int lo = 0, hi = n;                                           // the first place over thr
    while (lo < hi) { const int m = (lo + hi) / 2; if (*(const volatile double*)&r.s[m] <= thr) lo = m + 1; else hi = m; }
    return (int)((long long)(n - lo) * 1000 / n);
}
// eUs: the law's E (95th percentile of 4 s; 0 none); lineUs: two refreshes plus FG_COND_FIX_OVER_US. Returns fixOver.
static inline int FgCondFixShareStep(int& run, int sharePm, int eUs, int lineUs, int engagedFixed)
{
    if (!engagedFixed || sharePm < 0 || lineUs <= 0) { run = 0; return 0; }
    run = (sharePm > FG_COND_FIX_SHARE_PM && eUs > lineUs) ? (run < 1000 ? run + 1 : run) : 0;
    return run >= FG_COND_FIX_OVER_WIN ? 1 : 0;
}
// Does the held rate's dip under 99 percent of half the refresh stand the mode down? Its first window never (a one-second
// streaming burst moves one median); its second only where R_free is unread or under 103 percent of half the refresh; from its
// third always (a card-limited game waiting inside DXVK's Present would else repeat refreshes for the session).
static inline bool FgCondFixHeldDipStands(const FgCondIn& in)
{
    if (in.fixDipFirst == 1) return false;
    if (in.fixDipFirst == 2) return true;
    const long long half = (long long)FgCondHzX1000(in) / 200;
    return in.freeFpsX10 <= 0 || half <= 0 || (long long)in.freeFpsX10 * 100 < half * 103;
}
// Once a window. Returns 1 the dip's first window, 2 its third and later, else 0.
enum { FG_COND_FIX_DIP_LONG_WIN = 3 };
static inline int FgCondFixDipStep(int& dipRun, const FgCondIn& in)
{
    const long long half = (long long)FgCondHzX1000(in) / 200, f = in.realFpsX10;
    const bool dip = in.engaged && FgCondFixedMode(in) && f > 0 && half > 0 && f * 100 < half * 99;
    dipRun = dip ? (dipRun < 1000 ? dipRun + 1 : dipRun) : 0;
    return !dip ? 0 : dipRun < 2 ? 1 : dipRun >= FG_COND_FIX_DIP_LONG_WIN ? 2 : 0;
}
// A log's bound of the share over 4 s, for tools/fg-fix-replay: L (the Kl-th largest, Kl = max(5, 2 percent)) at or under the
// line leaves at most 40 per mille over it; E (Ke = max(6, 5 percent)) at most 50; neither, 1000. lUs, eUs in us.
static inline int FgCondFixShareBoundPm(int lUs, int eUs, int lineUs)
{
    if (lineUs <= 0) return 1000;
    if (lUs > 0 && lUs <= lineUs) return 40;
    if (eUs > 0 && eUs <= lineUs) return 50;
    return 1000;
}
// The way back in after an overrun: a game whose median clears the band can still overrun engaged by its spread, so the mode
// asks this rate standing by before it engages again: 1e7 x L / (median x floorUs), tenths, plus half a percent.
static inline int FgCondFixEntryX10(int lawLUs, int lawMedUs, int floorUs)
{
    if (lawLUs <= 0 || lawMedUs <= 0 || floorUs <= 0) return 0;
    return (int)(1.0e7 * (double)lawLUs / ((double)lawMedUs * (double)floorUs) * 1.005 + 0.5);
}
// Capped at FG_COND_FIX_ENTRY_CAP_PM of the plain floor and relaxing linearly to it over the length, then gone; the next engage,
// a Reset or a new refresh clear it. The length is 60 windows, doubled (300 at most) when an overrun returns within twice the
// last length. Returns fixEntryX10 (0 none).
struct FgFixEntry { int x10, n, win, age; };
enum { FG_COND_FIX_ENTRY_WIN = 60, FG_COND_FIX_ENTRY_MAX_WIN = 300, FG_COND_FIX_ENTRY_CAP_PM = 1080 };
static inline int FgCondFixEntryStep(FgFixEntry& E, int setX10, int standing, int floorX10)
{
    if (E.age < 100000) E.age++;
    if (setX10 > 0)
    {
        const int cap = (int)((long long)floorX10 * FG_COND_FIX_ENTRY_CAP_PM / 1000);
        E.win = (E.win > 0 && E.age < 2 * E.win) ? (E.win * 2 < FG_COND_FIX_ENTRY_MAX_WIN ? E.win * 2 : FG_COND_FIX_ENTRY_MAX_WIN) : FG_COND_FIX_ENTRY_WIN;
        E.x10 = setX10 < cap ? setX10 : cap; E.n = 0; E.age = 0;
    }
    else if (E.x10 > 0 && standing && ++E.n >= E.win) { E.x10 = 0; E.n = 0; }
    if (E.x10 <= floorX10 || E.win <= 0) return 0;
    return floorX10 + (int)((long long)(E.x10 - floorX10) * (E.win - E.n) / E.win);
}
// After a stand-down by SLOW too: set at its cap, relaxing over the longest length.
static inline void FgCondFixEntrySlowSet(FgFixEntry& E, int floorX10)
{
    E.x10 = (int)((long long)floorX10 * FG_COND_FIX_ENTRY_CAP_PM / 1000); E.n = 0; E.win = FG_COND_FIX_ENTRY_MAX_WIN; E.age = 0;
}
// The Present's own time: the worker's mean inside DXVK's Present, taken only standing by under vsync with the game under 97
// percent of the refresh (the FIFO never fills, so presents do not wait). The session's least, at most 2 ms. PaceFixed takes it
// off every hold.
static inline int FgCondFixBaseStep(int& base, int meanUs, int realX10, int hzX1000, int standbyVsync)
{
    if (standbyVsync && meanUs > 0 && meanUs <= 2000 && realX10 > 0 && hzX1000 > 0 && (long long)realX10 * 100 * 100 < (long long)hzX1000 * 97)
        base = (base < 0 || meanUs < base) ? meanUs : base;
    return base > 0 ? base : 0;
}
// fg_min_fps holds on a variable display as on a fixed one: where the doubling's real rate C is under the key, FGC_SLOW stands.
static inline bool FgCondVrrUnderKey(const FgCondIn& in, const FgCondK& k) { const int cf = FgCondCFpsX10(in); return k.minKeyed && !FgCondFixedMode(in) && cf > 0 && cf < k.minRealFpsX10; }
static inline bool FgCondFixedUnderFloor(const FgCondIn& in, const FgCondK& k) { const int h = FgCondHalfRoundX10(FgCondHzX1000(in)); return FgCondFixedMode(in) && h > 0 && h < FgCondMinX10(in, k); }
static inline int FgCondClassOf(int c)
{
    return c < FGC_REFRESH ? FGCC_SESSION : c < FGC_FFCAP ? FGCC_RESET : FGCC_STANDBY;
}
// Never the five the armed machine cannot run without.
static inline bool FgCondLiftable(int c)
{
    return c != FGC_DEAD && c != FGC_RESTORE && c != FGC_PRESENT && c != FGC_MARKER && c != FGC_REFRESH;
}
// Rows whose line also goes to the warnings file: those that keep a session at fg=1 from generating until the player changes
// something. Rate rows come and go; one that keeps generation off for good is told once by FgShipLongStep.
static inline bool FgCondWarns(int c)
{
    return c != FGC_OK && c != FGC_MENU && c != FGC_SLOW && c != FGC_FP16 && c != FGC_FAST && c != FGC_ONSLOW;
}
// Whether one row holds, whatever the others say and whatever is lifted.
static inline bool FgCondHolds(int c, const FgCondIn& in, const FgCondK& k)
{
    switch (c)
    {
    case FGC_DEAD:        return in.machineDead != 0;
    case FGC_RESTORE:     return in.restoreLost > 0;
    case FGC_PRESENT:     return in.presentOurs == 0;
    case FGC_MARKER:      return in.markerOk == 0;
    case FGC_AGILITY:     return in.agility != 0;
    case FGC_DXVK:        return in.dxvk31 == 0;
    case FGC_ASYNC:       return in.asyncNamed != 0 && in.asyncHome != 0;
    case FGC_REFRESH:     return FgCondHzX1000(in) == 0;
    case FGC_SWAPCHAIN:   return !((in.swapEffect == 1 || in.swapEffect == 2) && in.backBuffers >= k.minBackBuffers && in.backBuffers <= k.maxBackBuffers);
    case FGC_EXCLUSIVE:   return in.windowed == 0;
    case FGC_PANEL:       { if (FgCondFixedMode(in)) return false;   // the fixed mode's real rate is half the refresh, which SLOW holds to the floor
                                                                     // A variable panel of 59 Hz and up runs: the doubling's real rate is C, which SLOW holds to the floor.
                            if (!FgCondFixedSeen(in) && FgCondHzX1000(in) >= FG_COND_60HZ_LO_X1000) return false;
                            // A fixed screen of 59 Hz and up the mode may run on, with vsync off, is FGC_VSYNCOFF's.
                            if (FgCondFixedScreen(in) && !in.vsync && FgCondHzX1000(in) >= FG_COND_60HZ_LO_X1000) return false;
                            const int cf = FgCondCFpsX10(in); return cf > 0 && cf < k.minCFpsX10; }
    case FGC_FP16:        return in.bbFp16 != 0 && in.hdrOn == 0;
    case FGC_SDR:         return in.bbFp16 == 0 && in.hdrOn == 0 && in.sdrMsaa != 0;
    case FGC_FIXED:      return FgCondFixedSeen(in) && !FgCondFixedScreen(in);
    case FGC_VRR_UNKNOWN: return in.vrr < 0 && in.flipQuant <= 0 && k.unknownVrrRefused != 0;
    case FGC_FFCAP:       { const int ff = FgCondFfFpsX10(in), cf = FgCondCFpsX10(in);
                            return ff > 0 && cf > 0 && (long long)ff * 1000 <= (long long)k.ffNearPm * cf; }
    case FGC_DXVKCAP:     return in.dxvkCap > 0;
    case FGC_MENU:        return in.menu != 0;
        // a fixed screen with vsync off is FGC_VSYNCOFF's, not the rate rows'
    case FGC_FAST:        { if (FgCondFixedScreen(in) && !in.vsync) return false;
                            const int f = FgCondFastRateX10(in); return f > 0 && f > FgCondTopX10(in, k) + (in.engaged ? k.rateHystX10 : 0); }
    case FGC_SLOW:        { if (FgCondFixedScreen(in) && !in.vsync) return false;
                            const int f = in.realFpsX10;
                            if (f > 0 && (FgCondFixedUnderFloor(in, k) || FgCondVrrUnderKey(in, k))) return true;   // the key binds on a variable display too
                                                                                                                    // Engaged in the fixed mode the game must hold 99 percent of half the refresh (slower repeats refreshes), judged by the bulk
                            // (fixOver) or a dip that stands (FgCondFixHeldDipStands).
                            if (in.engaged && FgCondFixedMode(in)) return f > 0 && (in.fixOver != 0 || ((long long)f * 100 < (long long)FgCondHalfX10(in) * 99 && FgCondFixHeldDipStands(in)));
                            if (!in.engaged && FgCondFixedMode(in) && in.fixEntryX10 > FgCondFloorX10(in, k)) return f > 0 && f < in.fixEntryX10;   // the way back in
                            return f > 0 && f < FgCondFloorX10(in, k) - (in.engaged ? k.rateHystX10 : 0); }
    case FGC_ONSLOW:      if (FgCondFixedScreen(in) && !in.vsync) return false;
                          // In the wait for a cut the row holds only past the hysteresis: each entry and exit moves the ladder and the guard.
                          return !in.engaged && in.predFpsX10 > 0 && in.predFpsX10 < FgCondFloorX10(in, k) - (in.engaging ? k.rateHystX10 : 0);
    case FGC_NVCAP:       { const int p = FgCondPairPresentsX10(in);
                            return in.nvCap > 0 && p > 0 && (long long)in.nvCap * 10 * 1000 <= (long long)k.ffNearPm * p; }
    case FGC_VSYNCOFF:    return FgCondFixedScreen(in) && !in.vsync;
    default:              return false;
    }
}
static inline unsigned FgCondMask(const FgCondIn& in, const FgCondK& k)
{
    unsigned m = 0;
    for (int c = 1; c < FGC_N; c++)
        if (FgCondHolds(c, in, k) && !(FgCondLiftable(c) && ((in.lift >> c) & 1u))) m |= 1u << c;
    return m;
}
// The first row that holds, in the enum's order, or FGC_OK.
static inline int FgCondFirst(const FgCondIn& in, const FgCondK& k)
{
    const unsigned m = FgCondMask(in, k);
    for (int c = 1; c < FGC_N; c++) if (m & (1u << c)) return c;
    return FGC_OK;
}
static inline const char* FgCondName(int c)
{
        // a switch, not a table: no const table in a header the third build may compile
    switch (c)
    {
    case FGC_OK: return "ok";                         case FGC_DEAD: return "dead";                 case FGC_RESTORE: return "restore lost";
    case FGC_PRESENT: return "present not the mod's"; case FGC_MARKER: return "no marker";          case FGC_AGILITY: return "Agility runtime";
    case FGC_DXVK: return "DXVK not 3.1";             case FGC_ASYNC: return "async_home named";    case FGC_REFRESH: return "refresh unread";
    case FGC_SWAPCHAIN: return "swapchain shape";     case FGC_EXCLUSIVE: return "exclusive fullscreen"; case FGC_PANEL: return "panel under 97 Hz";
    case FGC_FP16: return "FP16 without HDR";         case FGC_SDR: return "SDR";                   case FGC_FIXED: return "fixed refresh";
    case FGC_VRR_UNKNOWN: return "variable refresh unknown"; case FGC_FFCAP: return "FusionFix limiter"; case FGC_DXVKCAP: return "DXVK limit";
    case FGC_MENU: return "menu";                     case FGC_FAST: return "rate above";           case FGC_SLOW: return "rate under";
    case FGC_NVCAP: return "NVIDIA's frame rate cap"; case FGC_ONSLOW: return "rate under with it";
    case FGC_VSYNCOFF: return "fixed refresh, vsync off";
    default: return "?";
    }
}
// The law's lines name the standing row by its enum name, which a tool matches.
static inline const char* FgCondEnumName(int c)
{
    switch (c)
    {
    case FGC_OK: return "FGC_OK";           case FGC_DEAD: return "FGC_DEAD";         case FGC_RESTORE: return "FGC_RESTORE";   case FGC_PRESENT: return "FGC_PRESENT";
    case FGC_MARKER: return "FGC_MARKER";   case FGC_AGILITY: return "FGC_AGILITY";   case FGC_DXVK: return "FGC_DXVK";         case FGC_ASYNC: return "FGC_ASYNC";
    case FGC_REFRESH: return "FGC_REFRESH"; case FGC_SWAPCHAIN: return "FGC_SWAPCHAIN"; case FGC_EXCLUSIVE: return "FGC_EXCLUSIVE"; case FGC_PANEL: return "FGC_PANEL";
    case FGC_FP16: return "FGC_FP16";       case FGC_SDR: return "FGC_SDR";           case FGC_FIXED: return "FGC_FIXED";       case FGC_VRR_UNKNOWN: return "FGC_VRR_UNKNOWN";
    case FGC_FFCAP: return "FGC_FFCAP";     case FGC_DXVKCAP: return "FGC_DXVKCAP";   case FGC_MENU: return "FGC_MENU";         case FGC_FAST: return "FGC_FAST";
    case FGC_SLOW: return "FGC_SLOW";       case FGC_NVCAP: return "FGC_NVCAP";       case FGC_ONSLOW: return "FGC_ONSLOW";
    case FGC_VSYNCOFF: return "FGC_VSYNCOFF";
    default: return "FGC_?";
    }
}
// Said once a session by the caller. Returns the length written (0 for FGC_OK or an unknown row).
static inline int FgCondLine(int c, const FgCondIn& in, const FgCondK& k, char* out, size_t cap)
{
    if (!cap) return 0;
    out[0] = 0;
    const int hz = FgCondHzX1000(in), cf = FgCondCFpsX10(in);
    int n = 0;
    switch (c)
    {
        // Rows a player cannot mend say so in plain words, with the reason in brackets for the log.
    case FGC_DEAD:        n = snprintf(out, cap, "frame generation: off for this session: it stopped itself; please send me DLSS-IV.log (the line before this one there says why)"); break;
    case FGC_RESTORE:     n = snprintf(out, cap, "frame generation: off for this session: it stopped after a problem; please send me DLSS-IV.log (a frame it showed could not be taken back out of the back buffer, %d time(s))", in.restoreLost); break;
    case FGC_PRESENT:     n = snprintf(out, cap, "frame generation: off for this session: it could not start; please send me DLSS-IV.log (the game's Present is not the device thread's)"); break;
    case FGC_MARKER:      n = snprintf(out, cap, "frame generation: off for this session: it could not start; please send me DLSS-IV.log (the frame-end marker could not be made)"); break;
    case FGC_AGILITY:     n = snprintf(out, cap, "frame generation: off: an Agility runtime lies in DLSSIV\\D3D12 and frame generation was not proved on it (the upscaler goes on)"); break;
    case FGC_DXVK:        n = snprintf(out, cap, "frame generation: off: this DXVK is not 3.1, the only one frame generation was proved on"); break;
    case FGC_ASYNC:       n = snprintf(out, cap, "frame generation: off: async_home=1 in the file makes every frame run pipelined, and a pipelined frame is never shown generated (take the line out)"); break;
    case FGC_REFRESH:     n = snprintf(out, cap, "frame generation: off: the display's refresh rate could not be read"); break;
    case FGC_SWAPCHAIN:   n = snprintf(out, cap, "frame generation: off: it could not start with the game's display setup; please send me DLSS-IV.log (SwapEffect %d, %d back buffer(s); it runs on DISCARD or FLIP, %d to %d)",
                                       in.swapEffect, in.backBuffers, k.minBackBuffers, k.maxBackBuffers); break;
    case FGC_EXCLUSIVE:   n = snprintf(out, cap, "frame generation: off: the game runs in exclusive fullscreen; set the game to Borderless in its display options"); break;
    case FGC_PANEL:       n = snprintf(out, cap, "frame generation: off: the display runs at %d.%02d Hz and it needs about 97 Hz or more", hz / 1000, (hz % 1000) / 10); break;
    case FGC_FP16:        n = snprintf(out, cap, "frame generation: standing by: the back buffer is FP16 and HDR is not on"); break;
    case FGC_SDR:         n = snprintf(out, cap, "frame generation: off: HDR is not on, and in SDR the HUD cannot be kept crisp with a multisampled back buffer"); break;
    case FGC_FIXED:       n = (in.fixedKey > 0 && in.vrr == 0 && in.flipQuant < 0)
                              ? snprintf(out, cap, "frame generation: off: the display was read as not running variable refresh for the game, but its presents were read off the refresh grid (it may follow the game, or Fast Sync is on), so the fixed mode does not run")
                              : (in.fixedKey > 0 && in.vrr == 1)
                              ? snprintf(out, cap, "frame generation: off: the display was read as running variable refresh for the game, but its presents were read on a fixed refresh grid (G-Sync may be off on the display itself)")
                              : snprintf(out, cap, "frame generation: off: the display does not run variable refresh for the game (G-Sync or FreeSync), and this version needs it"); break;
        // With vsync off the game's frames cannot show the answer (FgShipVrrMayForm), so the remedy is vsync from the game's start.
    case FGC_VRR_UNKNOWN: n = in.vsync
                              ? snprintf(out, cap, "frame generation: off: the mod could not tell whether the display runs variable refresh (G-Sync or FreeSync) for the game: the graphics driver did not say, and the game's frames did not show it")
                              : snprintf(out, cap, "frame generation: off: the mod could not tell whether the display runs variable refresh (G-Sync or FreeSync) for the game. Turn the game's vsync on and restart the game, so the mod can tell"); break;
    case FGC_FFCAP:       n = snprintf(out, cap, "frame generation: standing by: FusionFix's frame limiter is at %d fps, at or under %d.%d, where frame generation paces the game itself (set the limiter off or higher)",
                                       FgCondFfFpsX10(in) / 10, (int)((long long)k.ffNearPm * cf / 1000) / 10, (int)((long long)k.ffNearPm * cf / 1000) % 10); break;
    case FGC_DXVKCAP:     n = snprintf(out, cap, "frame generation: standing by: DXVK's frame rate limit (%d) counts generated frames too (take dxvk.maxFrameRate out)", in.dxvkCap); break;
    case FGC_MENU:        n = snprintf(out, cap, "frame generation: standing by while a menu is up"); break;
    case FGC_FAST:        n = snprintf(out, cap, "frame generation: standing by: the game runs at about %d fps by itself, and this version generates for a game of up to about %d on this display",
                                       FgCondFastRateX10(in) / 10, (FgCondTopX10(in, k) + (in.engaged ? k.rateHystX10 : 0)) / 10); break;
    case FGC_SLOW:
        if (FgCondFixedUnderFloor(in, k))
            n = snprintf(out, cap, "frame generation: standing by: the display runs at a fixed %d Hz, so the game would run at %d fps with it, under the %d it needs (fg_min_fps=30 in DLSS-IV.cfg allows 30)",
                         (hz + 500) / 1000, FgCondHalfX10(in) / 10, FgCondMinX10(in, k) / 10);
        else if (FgCondVrrUnderKey(in, k))
            n = snprintf(out, cap, "frame generation: standing by: the display runs at %d Hz, so the game would run at %d fps with it, under the %d fg_min_fps asks for in DLSS-IV.cfg",
                         (hz + 500) / 1000, cf / 10, k.minRealFpsX10 / 10);
        else if (FgCondFixedMode(in) && in.engaged && in.fixOver)
        {
            // the share in percent rounded up, so 101 to 104 per mille does not read "10 of 100"
            const int g = 2, two = hz > 0 ? (int)((long long)g * 1000000000LL / hz) : 0, sh = in.fixSharePm > 0 ? in.fixSharePm : 0;
            n = snprintf(out, cap, "frame generation: standing by: %d of 100 of the game's frames over the last 12 seconds took longer than the %d.%d ms a fixed %d.%02d Hz display gives each real frame (%s refreshes), and the mode holds at most %d of 100",
                         (sh + 9) / 10, two / 1000, (two % 1000) / 100, hz / 1000, (hz % 1000) / 10, "two", FG_COND_FIX_SHARE_PM / 10);
        }
        else if (FgCondFixedMode(in))
        {   // the fixed floor with its tenth: 61.8 at 120 Hz standing by, 59.4 engaged
            int need = in.engaged ? (int)((long long)FgCondHalfX10(in) * 99 / 100) : FgCondFloorX10(in, k);
            if (!in.engaged && in.fixEntryX10 > need) need = in.fixEntryX10;
            n = snprintf(out, cap, "frame generation: standing by: the game runs at about %d fps%s, under the %d.%d a fixed %d.%02d Hz display needs (half its refresh%s)",
                         in.realFpsX10 / 10, in.engaged ? " with it" : " by itself", need / 10, need % 10, hz / 1000, (hz % 1000) / 10, in.engaged ? "" : ", and 3 of 100 more");
        }
        else
            n = snprintf(out, cap, "frame generation: standing by: the game runs at about %d fps%s, under the %d it needs",
                         in.realFpsX10 / 10, in.engaged ? " with it" : " by itself", (FgCondFloorX10(in, k) - (in.engaged ? k.rateHystX10 : 0)) / 10);
        break;
    case FGC_ONSLOW:      n = snprintf(out, cap, "frame generation: standing by: the game runs at about %d fps by itself and would run at about %d with it here, under the %d it needs",
                                       in.realFpsX10 / 10, in.predFpsX10 / 10, FgCondFloorX10(in, k) / 10); break;
    case FGC_NVCAP:       n = snprintf(out, cap, "frame generation: standing by: NVIDIA's Max Frame Rate for GTA IV is %d fps, at or under the %d presents a second frame generation makes here (set it off for the game, or to %d or more)",
                                       in.nvCap, FgCondPairPresentsX10(in) / 10, (int)((long long)k.ffNearPm * FgCondPairPresentsX10(in) / 10000) + 1); break;
        // Where the half refresh is under the floor (75 Hz: 37.5 under 38) vsync alone is not enough, and the line names the key too.
    case FGC_VSYNCOFF:    n = (FgCondHalfRoundX10(hz) >= FG_COND_FLOOR_60HZ_X10 && FgCondHalfRoundX10(hz) < FgCondMinX10(in, k))
                              ? snprintf(out, cap, "frame generation: standing by: the display runs at a fixed %d.%02d Hz and vsync is off (turn vsync on in the game's video options and set fg_min_fps=30 in DLSS-IV.cfg: frame generation then starts by itself)",
                                         hz / 1000, (hz % 1000) / 10)
                              : snprintf(out, cap, "frame generation: standing by: the display runs at a fixed %d.%02d Hz and vsync is off, so a generated frame cannot be shown on time (turn vsync on in the game's video options: frame generation starts by itself)",
                                         hz / 1000, (hz % 1000) / 10); break;
    default:              return 0;
    }
    if (n < 0 || (size_t)n >= cap) { out[cap - 1] = 0; n = (int)cap - 1; }
    return n;
}

// ==================================================================================
// The game's own rate as the conditions are handed it, and the floor's key
// ==================================================================================
// The window's median real frame time, else the waiter's running period. Engaged that is R_on (SLOW's); FAST reads R_free.
static inline int FgCondRateX10(double medMs, int periodUs)
{
    if (medMs > 0.1) return (int)(10000.0 / medMs);
    return periodUs > 0 ? (int)(10000000LL / periodUs) : 0;
}
// R_free, the game's own rate while engaged. Where the pacer holds the game at the floor (period within 1 percent of it, median
// within 3 percent of the period) the measured rate says nothing of the game: R_free is then the rate last read unheld
// (aloneX10), capped by the held samples' bound; 0 none. Otherwise the measured rate. Units: us, ms, tenths.
static inline bool FgCondHeldAtFloor(int periodUs, int floorUs, double medMs)
{
    return periodUs > 0 && floorUs > 0 && medMs > 0.1 && (double)periodUs <= (double)floorUs * 1.01 && medMs * 1000.0 <= (double)periodUs * 1.03;
}
static inline int FgCondFreeX10(int lawMedUs, int periodUs, int floorUs, double medMs, int aloneX10)
{
    if (periodUs <= 0 || floorUs <= 0 || medMs <= 0.1) return 0;
    if (!FgCondHeldAtFloor(periodUs, floorUs, medMs)) return (int)(10000.0 / medMs);
    if (aloneX10 <= 0) return 0;
    const int upX10 = lawMedUs > 0 ? (int)(10000000LL / lawMedUs) : 0;
    return (upX10 > 0 && upX10 < aloneX10) ? upX10 : aloneX10;
}
// The law's median cost in us from its 4 s sorted ring, 0 with no sample. Read without a lock on purpose: a torn read lands
// between neighbours; it must lie in 0.2 to 100 ms or it is no sample.
template <class R> static inline int FgCondRingMedUs(const R& r)
{
    const int n = *(const volatile int*)&r.n;
    if (n <= 0 || n > (int)(sizeof(r.s) / sizeof(r.s[0]))) return 0;
    const double v = *(const volatile double*)&r.s[n / 2];
    return (v > 0.2 && v < 100.0) ? (int)(v * 1000.0 + 0.5) : 0;
}
// 2 x refresh x LOCK in us, 0 where the refresh is unread.
static inline int FgCondFloorUs(const FgCondIn& in)
{
    const int hz = FgCondHzX1000(in), lock = FgCondLockPm(in);   // exactly two refreshes in the fixed mode
    return (hz > 0 && lock > 0) ? (int)(2LL * lock * 1000000LL / hz) : 0;
}
// Whole frames a second, 30 to 240; anything else leaves the default 38. Keyed, it holds on every display.
static inline void FgCondFloorKey(FgCondK& k, int minFps) { if (minFps >= 30 && minFps <= 240) { k.minRealFpsX10 = minFps * 10; k.minKeyed = 1; } }
// Development key: whole frames a second, 50 to 240.
static inline void FgCondFastKey(FgCondK& k, int maxFps) { if (maxFps >= 50 && maxFps <= 240) k.maxRealFpsX10 = maxFps * 10; }

// ==================================================================================
// The fixed mode's memory of a quantised display
// ==================================================================================
// From the classifier's display memory (DLSS-IV.cpp, LatFlipStep), only in windows the pacer did not hold (held 0: QUANTISED
// sets, anything else clears); kept while it holds; cleared by a new display key or NVIDIA's yes. mem: 0 none, 1 FREE,
// 2 QUANTISED. freeMem: the memory read FREE by the same rules. Returns the latch.
struct FgFixLatch { int quant; unsigned long long key; int keyed; int freeMem; };
static inline int FgCondFixLatchStep(FgFixLatch& L, int mem, int held, unsigned long long key, int nvYes)
{
    if (!L.keyed || key != L.key || nvYes) { L.key = key; L.keyed = 1; L.quant = 0; L.freeMem = 0; }
    if (!held && !nvYes) { L.quant = mem == 2 ? 1 : 0; L.freeMem = mem == 1 ? 1 : 0; }
    return L.quant;
}
// 1 QUANTISED, -1 FREE, 0 neither.
static inline int FgCondFixQuantIn(const FgFixLatch& L) { return L.quant ? 1 : L.freeMem ? -1 : 0; }

// ==================================================================================
// The law: the armed product's driver
// ==================================================================================
// The want's one writer, once a window, from the mechanism's own state. STANDBY: dwellWin windows with no row and the CPU
// placement answered go to ENGAGING. ENGAGING (the wait for a cut): a camera cut, a menu or cutWaitMs goes to ON; a row goes
// back. ON: a row, or the health exit (too few shown, or the card's median place outside its band for sickWin windows), goes to
// REST. REST: restWin windows, then a back-off after a short stint that doubles to backoffMaxWin. A Reset, a build, or the band
// moving while nothing is generated clears the rest and back-off: no latch. Nothing here reads a clock or the game.
enum FgDriveState { FGDR_STANDBY = 0, FGDR_ENGAGING = 1, FGDR_ON = 2, FGDR_REST = 3, FGDR_N };
enum FgDriveWhy
{
    FGDRW_NONE = 0,
    FGDRW_DWELL,       // STANDBY -> ENGAGING: dwellWin windows with no row
    FGDRW_AT_ONCE,     // STANDBY -> ON: the same, with no cut wait
    FGDRW_CUT,         // ENGAGING -> ON: a camera cut
    FGDRW_MENU,        // ENGAGING -> ON: a menu is up
    FGDRW_WAITED,      // ENGAGING -> ON: cutWaitMs passed
    FGDRW_ROW,         // ON -> REST, ENGAGING -> STANDBY: a row stands
    FGDRW_SHOWN,       // ON -> REST: under shownMinPc percent shown, sickWin windows
    FGDRW_CARD,        // ON -> REST: the card's median place outside cardLoPm to cardHiPm, sickWin windows
    FGDRW_RESTED,      // REST -> STANDBY: rest and back-off over
    FGDRW_CLEARED,     // REST -> STANDBY: a Reset or a build cleared them
    FGDRW_BAND,        // REST -> STANDBY: the game's band moved while nothing was generated
    FGDRW_PROBE,       // ON -> REST: the probe (held samples' bound over the top probeWin windows): one window of the game's own frames
    FGDRW_PROBED,      // REST -> ON: the probe read the game in band; REST -> STANDBY: a row stood
    FGDRW_N
};
enum FgDriveBand { FGDB_NONE = 0, FGDB_SLOW = 1, FGDB_IN = 2, FGDB_FAST = 3 };
struct FgDriveK
{
    int dwellWin;           // 3: windows running with no row standing before an engage
    int restWin;            // 10: windows of rest after any exit
    int backoffWin;         // 60: the back-off's start, and a stint shorter than this is a flap
    int backoffMaxWin;      // 300: the back-off's cap
    int sickWin;            // 3: windows running a health reading must fail
    int shownMinPc;         // 90: generated frames shown per 100 real frames presented
    int shownPairsMin;      // 30: fewer real frames in a window and the shown share is not judged
    int cardLoPm, cardHiPm; // 300, 700: the median place of the generated frame on the card, per mille of the pair
    int cardPairsMin;       // 8: fewer places in a window and the card is not judged
    int cutWaitMs;          // 10000: the wait for a cut (0: switch on at once)
    int cpuWaitWin;         // 30: windows the first engage waits for the CPU placement at most
    int costWin;            // 5: windows after the warm-up whose rates measure c
    int costMaxUs;          // 20000: c is never taken over this
    int costNeedShown;      // 1: c only from a window that generated
    int warmWin;            // 3: the warm-up: no health exit and no rate row stand-down (0: none)
    int probeWin;           // 60: windows the held bound must stand over FAST's before the probe (0: none)
    int probeGapWin;        // 120: least windows between probes that found the game in band, doubling to backoffMaxWin
    int probeOverPm;        // 3200: per mille the held bound must stand over FAST's bound (FgDriveStep says why)
        // The quick come-back after a stand-down by a rate row (FgDriveExit has the rule):
    int quickRestWin;       // 1: the rest and no back-off (0: restWin and the back-off)
    int flapHoldMs;         // 30000: from the second short stint running, the dwell does not count for this long
    int flapHoldMaxMs;      // 60000: the hold doubles to this cap
};
#define FG_DRIVE_QUICK 1   // the harness builds against older tables too
static inline FgDriveK FgDriveDefaults()
{
    FgDriveK k = { 3, 10, 60, 300, 3, 90, 30, 300, 700, 8, 10000, 30, 5, 20000, 1, 3, 60, 120, 3200, 1, 30000, 60000 };
    return k;
}
struct FgDriveIn
{
    int row;                // FGC_OK none; read with R_free while engaged
    int band;               // FgDriveBandOf
    int shownGen, shownN;   // 0 standing by
    int cardMed, cardN;     // median place per mille, and how many (0 none)
    int cleared;            // a Reset or a build since the last window
    int cpuReady;
    int realX10;            // standing by the game's own, engaged R_on
    int card;               // standing by, is the card the limit: 1, 0 idle room, -1 unknown
    int lawPerUs;           // engaged, the law's own period in us, before the room floor or a forced
                            // period; 0 none: c is only the real frame time beyond it
    int floorUs;            // engaged, the pacer's least period (the panel's floor, or two refreshes in the fixed mode;
                            // 0 none): c never counts it
    int ramp;               // 1: the pacer ramped in this window (switch-on or stand-down): nothing is learnt from it
    int perUs;              // engaged, FgPace's period at the window's end, every pacer floor included (0 none): where the real frame
                            // runs at it above the law's own period, a pacer floor holds the game, not the card
    int heldUpX10;          // engaged at the floor: the held samples' upper bound (0 none)
    int fastX10;            // FAST's engaged bound, the top plus hysteresis (0 unread)
    int kUs;                // engaged, K: the stint's median helper answer on the card over full dispatches
                            // (0 none or under FG_DRIVE_K_PAIRS_MIN): the least c where the card is the limit
    int fixed;              // 1: the fixed mode. Every engage comes at once after the dwell, with no wait for a cut or for the CPU placement
                            // (on a fixed screen the switch needs no camera cut to hide it); variable refresh keeps both waits
};
#define FG_DRIVE_FIXED_AT_ONCE 1   // the harness builds against older tables too
// Window counts, each bounded by its constant, and the band; nothing else.
struct FgDriveMem
{
    int state, prev, why, whyRow;   // the state, the one before, the last change's reason and row
    int dwell, cpuWaited;
    int restLeft, backoffLeft;
    int backoffNext;
    int sickShown, sickCard;        // ON: windows running each health reading failed
    int onWin;                      // counted to backoffWin and no further
    int standBand;
    int camAt; unsigned engT0;      // the wait's start: cut count and ms
    int engages, downs;
        // c, the added time on the card, us a real frame, kept apart by the card's reading at the stint's engage: cLimUs (the limit or
    // unknown), cRoomUs (idle room). -1 none. Kept until a Reset or a build.
    int aloneX10;                   // the game's own rate, last read while nothing was generated
    int lastCard;
    int cardAtEngage;
    int cLimUs, cRoomUs;
    int cSum, cN;
    int warmLeft, warmAt;           // ON: warm-up windows left; the warm-up window last run (0 none)
    int postWin;                    // counted to costWin + 1
        // c's margin: the samples' standard deviation in us (0 none), and the stint's sum of squares.
    int cLimErrUs, cRoomErrUs;
    long long cSq;
        // Windows a pacer floor held give only an upper bound of c, kept apart; a c from them alone is marked a bound. The mark is also
    // its age (FgDriveBoundAge): past FG_DRIVE_BOUND_WIN windows read standing by the bound is dropped (mark -1), and the next one is
    // FG_DRIVE_BOUND_KEPT, which never ages. 0: a sample, or nothing.
    int bSum, bN; long long bSq;
    int cLimBound, cRoomBound;
    int idleRun;                    // windows running unheld with idle room (FgDriveCUs)
    int probeRun, probeWait, probeGap, probing;
    int cStampUs;                   // K as the last stint read it, from an engage where the card was the limit or
                                    // unknown (0 none); kept until a Reset or a build
        // The come-back's guard: short stints running stood down by a rate row, and the hold after the last: the dwell does not count
    // until holdMs from holdT0 (ms; 0 none). Cleared by a long stint, a Reset or a build, not a band move.
    int flapRun, holdMs; unsigned holdT0;
};
static inline void FgDriveInit(FgDriveMem& m, const FgDriveK& k)
{
    const FgDriveMem z = {};
    m = z;
    m.backoffNext = k.backoffWin; m.probeGap = k.probeGapWin;
    m.cLimUs = -1; m.cRoomUs = -1; m.lastCard = -1; m.cardAtEngage = -1;
}
static inline int FgDriveBandOf(const FgCondIn& in, const FgCondK& k)
{
    if (in.realFpsX10 <= 0) return FGDB_NONE;
    if (FgCondHolds(FGC_SLOW, in, k)) return FGDB_SLOW;
    if (FgCondHolds(FGC_FAST, in, k)) return FGDB_FAST;
    return FGDB_IN;
}
static inline bool FgDriveWantsOn(const FgDriveMem& m) { return m.state == FGDR_ON; }
// Where the law drives, its own ON state, never the want word; else the want. A standing row is never engaged.
static inline int FgCondEngagedOf(int lawDrives, int lawOn, int wantOn, int refusal)
{
    if (refusal != 0) return 0;
    return (lawDrives ? lawOn : wantOn) ? 1 : 0;
}
// Taken only from a window the law held nothing in (holding the ladder makes the median in flight read 0), kept through ENGAGING
// and ON. Idle room only where the last FG_DRIVE_CARD_RUN readings all say so; one LIMIT among them is the limit; else -1.
enum { FG_DRIVE_CARD_RUN = 3 };
struct FgDriveCardH { int r[FG_DRIVE_CARD_RUN]; int n, at, kept; };
static inline int FgDriveCardTake(FgDriveCardH& h, int reading, int held)
{
    if (held) return h.n > 0 ? h.kept : -1;
    h.r[h.at] = reading; h.at = (h.at + 1) % FG_DRIVE_CARD_RUN; if (h.n < FG_DRIVE_CARD_RUN) h.n++;
    int lim = 0, idle = 0;
    for (int i = 0; i < h.n; i++) { if (h.r[i] == 1) lim++; else if (h.r[i] == 0) idle++; }
    h.kept = lim > 0 ? 1 : (h.n == FG_DRIVE_CARD_RUN && idle == FG_DRIVE_CARD_RUN) ? 0 : -1;
    return h.kept;
}
static inline bool FgDriveHolds(const FgDriveMem& m) { return m.state == FGDR_ENGAGING || m.state == FGDR_ON; }
// every change of state ends a warm-up; only FgDriveOn starts one
static inline void FgDriveGo(FgDriveMem& m, int to, int why, int row) { m.prev = m.state; m.state = to; m.why = why; m.whyRow = row; m.warmLeft = 0; m.warmAt = 0; }
static inline void FgDriveOn(FgDriveMem& m, const FgDriveK& k, int why)
{
    FgDriveGo(m, FGDR_ON, why, FGC_OK);
    m.onWin = 0; m.sickShown = 0; m.sickCard = 0; m.engages++; m.cSum = 0; m.cN = 0; m.cSq = 0; m.bSum = 0; m.bN = 0; m.bSq = 0; m.cardAtEngage = m.lastCard; m.idleRun = 0;
    m.warmLeft = k.warmWin > 0 ? k.warmWin : 0; m.postWin = 0;
}
// The warm-up: for a stint's first warmWin windows the health exit and the rate rows do not stand it down (an engage's start shows
// little for a window or two); a settings or session row still does. Ended by any change of state, a Reset or a build.
static inline unsigned FgDriveWarmLift(const FgDriveMem& m)
{
    return (m.state == FGDR_ON && m.warmLeft > 0) ? ((1u << FGC_SLOW) | (1u << FGC_FAST) | (1u << FGC_ONSLOW)) : 0u;
}
static inline bool FgDriveRateRow(int row) { return row == FGC_SLOW || row == FGC_FAST || row == FGC_ONSLOW; }
// The prediction R_on = 1 / (1/R + c). Idle room takes the room's c, else the limit's, else 0; the limit takes the limit's, else
// the room's, else 0: a measured cost beats an unmeasured zero, so a marginal game engages once and does not cycle. The limit's c
// stands for idle room only until the idle run reaches FG_DRIVE_IDLE_PRIOR_WIN windows.
enum { FG_DRIVE_IDLE_PRIOR_WIN = 15 };
// Nothing measured is optimistic (c = 0): at most one engage and one stand-down a session on a bad route. which: 0 none, 1 the
// limit's, 2 the room's.
static inline int FgDriveCUs(const FgDriveMem& m, int card, int* which)
{
    int w = 0, c = 0;
    if (card == 0) { if (m.cRoomUs >= 0) { c = m.cRoomUs; w = 2; } else if (m.cLimUs >= 0 && m.idleRun < FG_DRIVE_IDLE_PRIOR_WIN) { c = m.cLimUs; w = 1; } }
    else if (m.cLimUs >= 0) { c = m.cLimUs; w = 1; }
    else if (m.cRoomUs >= 0) { c = m.cRoomUs; w = 2; }
    if (which) *which = w;
    return c;
}
static inline int FgDrivePredX10(const FgDriveMem& m, int aloneX10, int card)
{
    if (aloneX10 <= 0) return 0;
    const long long frameUs = 10000000LL / aloneX10 + FgDriveCUs(m, card, nullptr);
    return frameUs > 0 ? (int)(10000000LL / frameUs) : 0;
}
// c's margin: the engage tests 1 / (1/R + c + spread) against the floor, so a game over the floor only on its mean stays by.
// The spread from the sum and sum of squares, by an integer square root.
static inline int FgDriveSpreadUs(long long sum, long long sq, int n)
{
    if (n <= 1) return 0;
    const long long v = (sq * n - sum * sum) / ((long long)n * n);
    if (v <= 0) return 0;
    long long r = 0, b = 1LL << 40;
    while (b > v) b >>= 2;
    long long x = v;
    while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (int)r;
}
static inline int FgDriveCErrUs(const FgDriveMem& m, int card)
{
    int w = 0; (void)FgDriveCUs(m, card, &w);
    return w == 1 ? m.cLimErrUs : w == 2 ? m.cRoomErrUs : 0;
}
static inline int FgDrivePredHiX10(const FgDriveMem& m, int aloneX10, int card)
{
    if (aloneX10 <= 0) return 0;
    const long long frameUs = 10000000LL / aloneX10 + FgDriveCUs(m, card, nullptr) + FgDriveCErrUs(m, card);
    return frameUs > 0 ? (int)(10000000LL / frameUs) : 0;
}
// The least c from the card. Where the doubling holds the real rate, the rates read c as 0 whatever the card does; but K (the
// handoff's retire to the helper's answer, ArmedKTake) is timed anyway, and with the order wait on every ride pair it is part of
// c, never more. So FGC_ONSLOW takes the larger of c with its margin and K where the card is the limit or unknown, outside the
// fixed mode. K is a floor under c (about a third of it), not c. No generated frame yet, no K.
// FgDriveKMedUs: the median bin's middle of a stint's counts; 0 under FG_DRIVE_K_PAIRS_MIN pairs or in the first bin.
enum { FG_DRIVE_K_PAIRS_MIN = 30 };
static inline int FgDriveKMedUs(const long* d, int bins, int binUs)
{
    long long n = 0;
    for (int i = 0; i < bins; i++) n += d[i] > 0 ? d[i] : 0;
    if (n < FG_DRIVE_K_PAIRS_MIN) return 0;
    long long run = 0;
    for (int i = 0; i < bins; i++) { run += d[i] > 0 ? d[i] : 0; if (run * 2 >= n) return i > 0 ? i * binUs + binUs / 2 : 0; }
    return 0;
}
static inline int FgDrivePredOnX10(const FgDriveMem& m, int aloneX10, int card, int fixedMode)
{
    if (aloneX10 <= 0) return 0;
    long long add = (long long)FgDriveCUs(m, card, nullptr) + FgDriveCErrUs(m, card);
    if (!fixedMode && card != 0 && m.cStampUs > add) add = m.cStampUs;
    const long long frameUs = 10000000LL / aloneX10 + add;
    return frameUs > 0 ? (int)(10000000LL / frameUs) : 0;
}
// the card's state is the one at the stint's engage; a c filed only from held windows is an upper bound
static inline int FgDriveCBound(const FgDriveMem& m, int card)
{
    int w = 0; (void)FgDriveCUs(m, card, &w);
    return w == 1 ? m.cLimBound : w == 2 ? m.cRoomBound : 0;
}
// A bound is no measure, so it does not keep frame generation off for good. While nothing is generated each window that reads the
// game's own rate ages it; past FG_DRIVE_BOUND_WIN windows it is dropped and the next stint measures c again. The bound filed
// after a drop is kept (FG_DRIVE_BOUND_KEPT) until a sample, a Reset or a build, so a route that only files bounds cannot cycle.
enum { FG_DRIVE_BOUND_WIN = 5, FG_DRIVE_BOUND_KEPT = 0x4000 };
static inline void FgDriveBoundAge(int& mark, int& cUs, int& errUs)
{
    if (mark < 1 || mark >= FG_DRIVE_BOUND_KEPT || cUs < 0) return;
    if (++mark <= FG_DRIVE_BOUND_WIN) return;
    mark = -1; cUs = -1; errUs = 0;
}
static inline int FgDriveBoundMark(int was, bool bound) { return bound ? (was < 0 ? (int)FG_DRIVE_BOUND_KEPT : 1) : 0; }
static inline const char* FgDriveCWhy(int which, int card, int bound = 0)
{
    if (bound) return which == 1 ? "an upper bound: a floor of the pacer's held every window of its stint, the card the limit standing by"
                                 : "an upper bound: a floor of the pacer's held every window of its stint, the card with room standing by";
    return which == 1 ? "measured where the card was the limit standing by" : which == 2 ? "measured where the card had room standing by"
         : card == 0 ? "none measured, the card has idle room: 0" : "none measured yet: the first engage is optimistic, 0";
}
// Is the card the limit, standing by: from the median in flight (tenths, -1 unread), the share of presents the latency pacer
// slept on, and the worker's mean block inside DXVK's Present (with vsync the pacer never sleeps) over FG_DRIVE_CARD_BLOCK_PM of
// the game's interval and FG_DRIVE_CARD_BLOCK_US. blockUs -1 unread; intervalUs 0 unread.
enum { FG_DRIVE_CARD_BLOCK_PM = 300, FG_DRIVE_CARD_BLOCK_US = 1500 };
static inline int FgDriveCardOf(int inFlightX10, int slept, int presents, int blockUs, int intervalUs)
{
    if (inFlightX10 < 0 && presents <= 0 && blockUs < 0) return -1;
    if (inFlightX10 >= 5) return 1;
    if (presents > 0 && slept * 2 >= presents) return 1;
    if (blockUs >= FG_DRIVE_CARD_BLOCK_US && intervalUs > 0 && (long long)blockUs * 1000 >= (long long)FG_DRIVE_CARD_BLOCK_PM * intervalUs) return 1;
    return 0;
}
// The window's mean block a present, us (-1 none).
static inline int FgDriveBlockUs(long long presUs, int presents) { return (presents > 0 && presUs >= 0) ? (int)(presUs / presents) : -1; }
// The exit from ON: the rest, and a back-off after a short stint (under backoffWin windows). After a rate row (FGC_SLOW,
// FGC_FAST) the rest is quickRestWin and there is no back-off: the prediction decides as at a first engage, so generation comes
// back as soon as the frames allow. Against cycling, from the second short stint running the dwell does not count for flapHoldMs,
// from the third flapHoldMaxMs. The health exits keep the rest and back-off. now: ms.
static inline void FgDriveExit(FgDriveMem& m, const FgDriveK& k, int why, int row, unsigned now)
{
    const bool flap = m.onWin < k.backoffWin;
    if (k.quickRestWin > 0 && why == FGDRW_ROW && (row == FGC_SLOW || row == FGC_FAST))
    {
        m.restLeft = k.quickRestWin; m.backoffLeft = 0;
        m.flapRun = flap ? (m.flapRun < 1000 ? m.flapRun + 1 : m.flapRun) : 0;
        int hold = 0;
        if (m.flapRun >= 2 && k.flapHoldMs > 0)
        {
            hold = k.flapHoldMs;
            for (int i = 2; i < m.flapRun && hold < k.flapHoldMaxMs; i++) hold *= 2;
            if (hold > k.flapHoldMaxMs) hold = k.flapHoldMaxMs;
        }
        m.holdMs = hold; m.holdT0 = now;
    }
    else
    {
        m.restLeft = k.restWin;
        if (flap) { m.backoffLeft = m.backoffNext; m.backoffNext = m.backoffNext * 2 > k.backoffMaxWin ? k.backoffMaxWin : m.backoffNext * 2; }
        else { m.backoffLeft = 0; m.backoffNext = k.backoffWin; }
    }
    m.dwell = 0; m.downs++;
    FgDriveGo(m, FGDR_REST, why, row);
}
// ms, 0 none
static inline int FgDriveHoldLeftMs(const FgDriveMem& m, unsigned now)
{
    if (m.holdMs <= 0) return 0;
    const unsigned in = now - m.holdT0;
    return in >= (unsigned)m.holdMs ? 0 : (int)((unsigned)m.holdMs - in);
}
static inline bool FgDriveSettingsRow(int row) { return row == FGC_FFCAP || row == FGC_DXVKCAP || row == FGC_NVCAP || (row > FGC_OK && FgCondClassOf(row) != FGCC_STANDBY); }
static inline void FgDriveClear(FgDriveMem& m, const FgDriveK& k) { m.restLeft = 0; m.backoffLeft = 0; m.backoffNext = k.backoffWin; m.dwell = 0; m.probing = 0; }
// Asked every frame and by the window's step. now: ms; cam: the cut count; menu: 1 up. Returns 1 when switched on.
static inline int FgDriveCut(FgDriveMem& m, const FgDriveK& k, unsigned now, int cam, int menu)
{
    if (m.state != FGDR_ENGAGING) return 0;
    if (cam != m.camAt) FgDriveOn(m, k, FGDRW_CUT);
    else if (menu) FgDriveOn(m, k, FGDRW_MENU);
    else if (now - m.engT0 >= (unsigned)k.cutWaitMs) FgDriveOn(m, k, FGDRW_WAITED);
    else return 0;
    return 1;
}
// Once a window. Returns 1 where the state changed (at most one change a step).
static inline int FgDriveStep(FgDriveMem& m, const FgDriveIn& in, const FgDriveK& k, unsigned now, int cam, int menu)
{
    const int was = m.state;
        // A band move while nothing is generated clears the back-off (and in REST the rest), not c's margin.
    if (m.state != FGDR_ON && in.band != FGDB_NONE && !in.ramp)
    {
        const bool moved = m.standBand != FGDB_NONE && in.band != m.standBand;
        m.standBand = in.band;
        if (moved)
        {
            if (m.state == FGDR_REST) { FgDriveClear(m, k); FgDriveGo(m, FGDR_STANDBY, FGDRW_BAND, in.row); return 1; }
            m.backoffNext = k.backoffWin;
        }
    }
    if (m.state != FGDR_ON && in.realX10 > 0 && !in.ramp) { m.aloneX10 = in.realX10; FgDriveBoundAge(m.cLimBound, m.cLimUs, m.cLimErrUs); FgDriveBoundAge(m.cRoomBound, m.cRoomUs, m.cRoomErrUs); }
        // STANDBY windows only: from ENGAGING on the law holds the ladder and the latency pacer
    if (m.state == FGDR_STANDBY && !in.ramp) m.lastCard = in.card;
    if ((m.state == FGDR_STANDBY || m.state == FGDR_REST) && !in.ramp) m.idleRun = in.card == 0 ? (m.idleRun < 100000 ? m.idleRun + 1 : m.idleRun) : 0;
    if (in.cleared) { m.idleRun = 0; m.probeRun = 0; m.probeWait = 0; m.probeGap = k.probeGapWin; }
        // A Reset or a build: the display, swapchain, set and card settings may differ, so c, its margin and the guard are forgotten.
    if (in.cleared) { m.cLimUs = -1; m.cRoomUs = -1; m.cLimErrUs = 0; m.cRoomErrUs = 0; m.cLimBound = 0; m.cRoomBound = 0; m.cStampUs = 0; m.flapRun = 0; m.holdMs = 0; }
    if (in.cleared)
    {
        m.warmLeft = 0;
        if (m.state == FGDR_REST) { FgDriveClear(m, k); FgDriveGo(m, FGDR_STANDBY, FGDRW_CLEARED, in.row); return 1; }
        m.backoffNext = k.backoffWin;
        if (m.state == FGDR_STANDBY) { m.dwell = 0; return 0; }
    }
    switch (m.state)
    {
    case FGDR_STANDBY:
        if (!in.cpuReady && !in.fixed && m.cpuWaited < k.cpuWaitWin) { m.cpuWaited++; m.dwell = 0; break; }
        if (m.holdMs > 0) { if (FgDriveHoldLeftMs(m, now) > 0) { m.dwell = 0; break; } m.holdMs = 0; }
        m.dwell = in.row == FGC_OK ? m.dwell + 1 : 0;
        if (m.dwell >= k.dwellWin)
        {
            m.dwell = 0; m.camAt = cam; m.engT0 = now;
            if (k.cutWaitMs <= 0 || in.fixed) FgDriveOn(m, k, FGDRW_AT_ONCE);
            else FgDriveGo(m, FGDR_ENGAGING, FGDRW_DWELL, FGC_OK);
        }
        break;
    case FGDR_ENGAGING:
        if (in.row != FGC_OK) { m.dwell = 0; FgDriveGo(m, FGDR_STANDBY, FGDRW_ROW, in.row); break; }
        FgDriveCut(m, k, now, cam, menu);
        break;
    case FGDR_ON:
    {
        if (m.onWin < k.backoffWin) m.onWin++;
        const bool warm = m.warmLeft > 0;
        if (warm) { m.warmLeft--; m.warmAt = k.warmWin - m.warmLeft; }
        else { m.warmAt = 0; if (m.postWin <= k.costWin && !in.ramp) m.postWin++; }
                // K only after the warm-up of a stint that engaged on a card that was the limit or unknown: a stint too short to leave its
        // warm-up files nothing, so it cannot keep the next engage out.
        if (!warm && in.kUs > 0 && m.cardAtEngage != 0) m.cStampUs = in.kUs;
                // c: the stint's real frame time less the game's own before the engage, 0 to costMaxUs, filed by the card's reading at the
        // engage; measured after the warm-up, only from windows that generated, never the pacer's hold.
        if (!warm && !in.ramp && m.postWin <= k.costWin && !FgDriveSettingsRow(in.row) && (!k.costNeedShown || (in.shownN >= k.shownPairsMin && in.shownGen > 0)) && in.realX10 > 0 && m.aloneX10 > 0)
        {
            // More than 3 percent over the law's period (or its floor): the card holds the game, c = real - alone; else c = 0.
            const long long realUs = 10000000LL / in.realX10, aloneUs = 10000000LL / m.aloneX10;
            const int holdUs = in.lawPerUs > in.floorUs ? in.lawPerUs : in.floorUs;
            // At the period in force but over the hold, a pacer floor (ride or room) holds the game: real - alone is only an upper bound,
            // filed only where the stint has no sample.
            const int perUs = in.perUs > 0 ? in.perUs : holdUs;
            const bool cardHolds = perUs <= 0 || (double)realUs > (double)perUs * 1.03;
            const bool floorHolds = !cardHolds && holdUs > 0 && (double)perUs > (double)holdUs * 1.03;
            int c = (cardHolds || floorHolds) ? (int)(realUs - aloneUs) : 0;
            if (c < 0) c = 0; if (c > k.costMaxUs) c = k.costMaxUs;
            if (floorHolds) { m.bSum += c; m.bN++; m.bSq += (long long)c * c; }
            else { m.cSum += c; m.cN++; m.cSq += (long long)c * c; }
        }
        if (!warm && (m.cN > 0 || m.bN > 0) && (m.postWin == k.costWin || (in.row == FGC_SLOW && m.postWin <= k.costWin)))
        {
            // the margin: the samples' spread where any were read, else the bounds'
            const bool bound = m.cN == 0;
            const int n = bound ? m.bN : m.cN; const int sum = bound ? m.bSum : m.cSum; const long long sq = bound ? m.bSq : m.cSq;
            const int mean = sum / n, err = FgDriveSpreadUs(sum, sq, n);
            if (m.cardAtEngage == 0) { m.cRoomUs = mean; m.cRoomErrUs = err; m.cRoomBound = FgDriveBoundMark(m.cRoomBound, bound); } else { m.cLimUs = mean; m.cLimErrUs = err; m.cLimBound = FgDriveBoundMark(m.cLimBound, bound); }
        }
        if (warm && FgDriveRateRow(in.row)) { m.sickShown = 0; m.sickCard = 0; break; }
        if (in.row != FGC_OK)
        {
            // A settings or session row is no flap of the law's: STANDBY at once, no rest and no back-off.
            if (FgDriveSettingsRow(in.row)) { m.dwell = 0; m.downs++; FgDriveGo(m, FGDR_STANDBY, FGDRW_ROW, in.row); break; }
            FgDriveExit(m, k, FGDRW_ROW, in.row, now); break;
        }
        if (warm) { m.sickShown = 0; m.sickCard = 0; break; }
        if (in.shownN >= k.shownPairsMin) m.sickShown = (long long)in.shownGen * 100 < (long long)k.shownMinPc * in.shownN ? m.sickShown + 1 : 0;
        if (in.cardN >= k.cardPairsMin) m.sickCard = (in.cardMed < k.cardLoPm || in.cardMed > k.cardHiPm) ? m.sickCard + 1 : 0;
        if (m.sickShown >= k.sickWin) { FgDriveExit(m, k, FGDRW_SHOWN, FGC_OK, now); break; }
        else if (m.sickCard >= k.sickWin) { FgDriveExit(m, k, FGDRW_CARD, FGC_OK, now); break; }
                // The probe, else a game that got lighter while held at the floor could never leave the doubling: where the held bound stands
        // over FAST's bound by probeOverPm (a game in band reads at most about 3.1 times) for probeWin windows, one window unpaced, then ON
        // again or STANDBY with the row. At least probeGap windows between probes that found the game in band.
        if (k.probeWin > 0 && in.heldUpX10 > 0 && in.fastX10 > 0 && (long long)in.heldUpX10 * 1000 > (long long)in.fastX10 * (k.probeOverPm > 1000 ? k.probeOverPm : 1000)) { if (m.probeRun < 100000) m.probeRun++; }
        else m.probeRun = 0;
        if (m.probeWait > 0) m.probeWait--;
        if (k.probeWin > 0 && m.probeRun >= k.probeWin && m.probeWait == 0 && !in.ramp)
        {
            m.probeRun = 0; m.probing = 1; m.dwell = 0; m.downs++;
            FgDriveGo(m, FGDR_REST, FGDRW_PROBE, FGC_OK); m.restLeft = 0; m.backoffLeft = 0;
        }
        break;
    }
    case FGDR_REST:
        if (m.probing)
        {   // the game's own rate was read standing by at this step's head
            if (in.ramp) break;                                    // a ramped window says nothing of the game
            m.probing = 0;
            if (in.row == FGC_OK) { FgDriveOn(m, k, FGDRW_PROBED); m.probeWait = m.probeGap; m.probeGap = m.probeGap * 2 > k.backoffMaxWin ? k.backoffMaxWin : m.probeGap * 2; }
            else { m.dwell = 0; FgDriveGo(m, FGDR_STANDBY, FGDRW_PROBED, in.row); m.probeGap = k.probeGapWin; m.probeWait = 0; }
            break;
        }
        if (m.restLeft > 0) m.restLeft--;
        else if (m.backoffLeft > 0) m.backoffLeft--;
        if (m.restLeft == 0 && m.backoffLeft == 0) FgDriveGo(m, FGDR_STANDBY, FGDRW_RESTED, in.row);
        break;
    }
    return m.state != was ? 1 : 0;
}
// From the machine's ring of the last 128 places (value first, count last): the places since seen. Returns how many; *med their
// median.
template <class T> static inline int FgDrivePlaceMed(const volatile T* ring, T n, T seen, int* med)
{
    int cnt = (int)(n - seen);
    if (cnt > 128) cnt = 128;
    *med = 0;
    if (cnt <= 0) return 0;
    int v[128];
    for (int i = 0; i < cnt; i++)
    {
        const int x = (int)ring[(n - cnt + i) & 127];
        int j = i; while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; } v[j] = x;
    }
    *med = v[cnt / 2];
    return cnt;
}
static inline const char* FgDriveStateName(int s)
{
    return s == FGDR_STANDBY ? "STANDBY" : s == FGDR_ENGAGING ? "ENGAGING" : s == FGDR_ON ? "ON" : s == FGDR_REST ? "REST" : "?";
}
static inline const char* FgDriveWhyName(int w)
{
    switch (w)
    {
    case FGDRW_NONE: return "FGDRW_NONE";     case FGDRW_DWELL: return "FGDRW_DWELL";   case FGDRW_AT_ONCE: return "FGDRW_AT_ONCE"; case FGDRW_CUT: return "FGDRW_CUT";
    case FGDRW_MENU: return "FGDRW_MENU";     case FGDRW_WAITED: return "FGDRW_WAITED"; case FGDRW_ROW: return "FGDRW_ROW";         case FGDRW_SHOWN: return "FGDRW_SHOWN";
    case FGDRW_CARD: return "FGDRW_CARD";     case FGDRW_RESTED: return "FGDRW_RESTED"; case FGDRW_CLEARED: return "FGDRW_CLEARED"; case FGDRW_BAND: return "FGDRW_BAND";
    case FGDRW_PROBE: return "FGDRW_PROBE";   case FGDRW_PROBED: return "FGDRW_PROBED";
    default: return "FGDRW_?";
    }
}
static inline const char* FgDriveBandName(int b) { return b == FGDB_SLOW ? "SLOW" : b == FGDB_IN ? "IN" : b == FGDB_FAST ? "FAST" : "NONE"; }

// ==================================================================================
// The probe's readings
// ==================================================================================
// Held at the floor, the rate of the law's median sample (an upper bound of the game's own).
static inline int FgDriveHeldUpX10(int lawMedUs, int periodUs, int floorUs, double medMs)
{
    return (lawMedUs > 0 && FgCondHeldAtFloor(periodUs, floorUs, medMs)) ? (int)(10000000LL / lawMedUs) : 0;
}
static inline int FgDriveFastX10(const FgCondIn& in, const FgCondK& k) { const int t = FgCondTopX10(in, k); return t > 0 ? t + k.rateHystX10 : 0; }
