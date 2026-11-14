// The pacing law for frame generation, a pure function. The reference is tools/perfproof/fglaw_oracle.py; equiv_fglaw.py runs
// this file against it step by step.
// - The sample is a frame's cost in ms; one between 0.2 and 100 ms counts.
// - A hitch (over 1.5 times the period in force) never enters the statistics, unless eight come in a row: then the level has
//   moved, and the next 240 ordinary samples open the door again.
// - Over the last 4 s (at most 1024 samples): E, the K-th largest with K = max(6, 5 percent), and L with K = max(5, 2 percent).
//   For moves down the same at half the rank. A 12 s memory (4096 samples) gives them again, read only to cross a band down.
// - At the floor it stays while E is at or under it; when E passes it (six frames since the last move), one move to the
//   resting place for head times L.
// - Resting places: the floor; the hold band up to holdMax in 0.2 ms steps; freeMin and over. freeMin is never under holdMax.
// - Off the floor: up after six frames when the place passes the period; down after twelve frames by half the gap within a
//   regime, to the regime's edge first, and across a band or to the floor only when the 12 s memory agrees.
// - The top: never over the 4 s median plus 18 ms (one pacer sleep is capped at 20 ms), nor 40 ms, nor under the floor.
// - The warm-up: 150 steps after a start in which nothing moves.
// It does not sleep or read a clock: the caller gives each sample and its time and takes the period back.
// THE FREE BAND'S FORM (FgLawStepForm, which every binding runs with a head of 1.0; FgLawStep is kept for the harness):
// - the places are 1.0 times L; the machine's room and ride floors keep the room a pair needs.
// - the way up has no tolerance under the place (FG_LAW_UP_TOL_PM 0): six frames after L passes the period, the period is L.
// - the way back to the floor asks FG_LAW_BACK_PM (1060) per mille of the level to fit it, so a game walking round the floor
//   does not cross it each time.
#pragma once
#include <math.h>
#include <string.h>

enum { FG_LAW_FAST = 1024, FG_LAW_SLOW = 4096 };
template <int CAP> struct FgLawRing
{
    int    n, head;                                  // FIFO count; its oldest entry
    double winMs;
    double t[CAP], c[CAP];                           // circular FIFO: each sample's time and cost
    double s[CAP + 1];                               // the same costs, ascending
};
struct FgLaw
{
    double floor, holdMax, freeMin, head;            // the regimes' edges (ms) and the headroom off the floor
    double holdKey, freeKey;                         // as asked for; FgLawFloor derives the edges again
    double per;
    double E, L;                                     // this step's statistics
    int    warm, dwell, moves, run, shiftLeft;       // warm-up steps left; steps since a move; moves; hitches in a row; the open door
    int    regime, hitch;                            // 0 the floor, 1 the hold band, 2 free; whether the sample was a hitch
    FgLawRing<FG_LAW_FAST> fast;                     // 4 s
    FgLawRing<FG_LAW_SLOW> slow;                     // 12 s
};

static inline int FgLawLower(const double* s, int n, double v)   // first index not under v
{
    int lo = 0, hi = n;
    while (lo < hi) { const int m = (lo + hi) / 2; if (s[m] < v) lo = m + 1; else hi = m; }
    return lo;
}
static inline int FgLawUpper(const double* s, int n, double v)   // first index over v
{
    int lo = 0, hi = n;
    while (lo < hi) { const int m = (lo + hi) / 2; if (s[m] <= v) lo = m + 1; else hi = m; }
    return lo;
}
template <int CAP> static inline void FgLawPop(FgLawRing<CAP>& r)
{
    const double old = r.c[r.head];
    const int i = FgLawLower(r.s, r.n, old);
    memmove(&r.s[i], &r.s[i + 1], (size_t)(r.n - i - 1) * sizeof(double));
    r.head = (r.head + 1) % CAP; r.n--;
}
template <int CAP> static inline void FgLawTrim(FgLawRing<CAP>& r, double t)
{
    while (r.n > 0 && t - r.t[r.head] > r.winMs) FgLawPop(r);
}
// equiv_fglaw.cpp defines it to count each path taken
#ifndef FG_LAW_PATH
#define FG_LAW_PATH(k)
#endif
template <int CAP> static inline void FgLawPush(FgLawRing<CAP>& r, double t, double c)
{
        // The steady state in one shift: where this push makes the oldest sample go, it is removed and the new one inserted by one
    // move of the samples between their two places, instead of a pop and a push each moving about half the array. Same result.
    if (r.n > 0 && (r.n == CAP || t - r.t[r.head] > r.winMs))
    {
        const double old = r.c[r.head];
        FG_LAW_PATH(r.n == CAP ? 0 : 1);
        const int j = FgLawLower(r.s, r.n, old);
        if (c >= old)
        {   // new over old: the samples between move down one
            FG_LAW_PATH(2);
            const int i = FgLawUpper(r.s, r.n, c) - 1;
            memmove(&r.s[j], &r.s[j + 1], (size_t)(i - j) * sizeof(double));
            r.s[i] = c;
        }
        else
        {   // new under old: the samples between move up one
            FG_LAW_PATH(3);
            const int i = FgLawUpper(r.s, r.n, c);
            memmove(&r.s[i + 1], &r.s[i], (size_t)(j - i) * sizeof(double));
            r.s[i] = c;
        }
        const int tail = (r.head + r.n) % CAP;       // the slot past the newest: the oldest's own when full
        r.t[tail] = t; r.c[tail] = c;
        r.head = (r.head + 1) % CAP;
        FgLawTrim(r, t);
        return;
    }
    FG_LAW_PATH(4);
    const int tail = (r.head + r.n) % CAP;
    r.t[tail] = t; r.c[tail] = c;
    const int i = FgLawUpper(r.s, r.n, c);
    memmove(&r.s[i + 1], &r.s[i], (size_t)(r.n - i) * sizeof(double));
    r.s[i] = c; r.n++;
    FgLawTrim(r, t);
}
template <int CAP> static inline double FgLawKth(const FgLawRing<CAP>& r, int k) { return r.n >= k ? r.s[r.n - k] : 0.0; }   // 0 while there are fewer
static inline int    FgLawMaxI(int a, int b) { return a > b ? a : b; }
static inline double FgLawMaxD(double a, double b) { return a > b ? a : b; }
static inline double FgLawMinD(double a, double b) { return a < b ? a : b; }

// The hold band's end is never under the floor (equal: no band), the free range's start never under the band's end.
static inline void FgLawEdges(FgLaw& w, double floorMs)
{
    w.floor = floorMs;
    w.holdMax = w.holdKey > floorMs ? w.holdKey : floorMs;
    w.freeMin = w.freeKey > w.holdMax ? w.freeKey : w.holdMax;
}
// A start: the period at the floor, nothing remembered, 150 steps of warm-up.
static inline void FgLawReset(FgLaw& w, double floorMs, double holdMaxMs, double freeMinMs, double head)
{
    memset(&w, 0, sizeof(w));
    w.holdKey = holdMaxMs; w.freeKey = freeMinMs; w.head = head;
    FgLawEdges(w, floorMs);
    w.per = floorMs; w.warm = 150;
    w.fast.winMs = 4000.0; w.slow.winMs = 12000.0;
}
// The floor moved: a period at the old floor, or now under the new one, goes to the new floor.
static inline void FgLawFloor(FgLaw& w, double floorMs)
{
    if (floorMs == w.floor) return;
    const bool at = w.per <= w.floor + 1e-9;
    FgLawEdges(w, floorMs);
    if (at || w.per < floorMs) w.per = floorMs;
}
// The floor, a 0.2 ms step of the hold band, or the free range.
static inline double FgLawSnap(const FgLaw& w, double want)
{
    if (want <= w.floor + 1e-9) return w.floor;
    if (want <= w.holdMax + 1e-9) return FgLawMinD(w.holdMax, w.floor + ceil((want - w.floor) / 0.2 - 1e-9) * 0.2);
    return FgLawMaxD(w.freeMin, want);
}
static inline int FgLawReg(const FgLaw& w, double p) { return p <= w.floor + 1e-9 ? 0 : (p <= w.holdMax + 1e-9 ? 1 : 2); }

// c: the sample in ms (taken only in 0.2 to 100); t: ms, any origin, never going back. Returns the period in force.
static inline double FgLawStep(FgLaw& w, double c, double t)
{
    w.hitch = 0;
    if (c > 0.2 && c < 100.0)
    {
        const bool hitch = c > 1.5 * w.per;
        w.hitch = hitch ? 1 : 0;
        w.run = hitch ? w.run + 1 : 0;
        if (w.run >= 8) w.shiftLeft = 240;                           // eight in a row: the level has moved
        if (!hitch && w.shiftLeft > 0) w.shiftLeft--;
        if (!hitch || w.shiftLeft > 0) { FgLawPush(w.fast, t, c); FgLawPush(w.slow, t, c); }
    }
    FgLawTrim(w.fast, t); FgLawTrim(w.slow, t);
    const int n = w.fast.n, m = w.slow.n;
    const int Ke = FgLawMaxI(6, (int)ceil(0.05 * (double)n)), Kl = FgLawMaxI(5, (int)ceil(0.02 * (double)n));
    const double E = FgLawKth(w.fast, Ke), Lv = FgLawMaxD(FgLawKth(w.fast, Kl), E);
    const double Ed = FgLawKth(w.fast, FgLawMaxI(3, Ke / 2)), Ld = FgLawMaxD(FgLawKth(w.fast, FgLawMaxI(2, Kl / 2)), Ed);   // half ranks: the way down
    const int Ks = FgLawMaxI(6, (int)ceil(0.05 * (double)m)), Kls = FgLawMaxI(5, (int)ceil(0.02 * (double)m));
    const double Es = FgLawKth(w.slow, FgLawMaxI(3, Ks / 2)), Ls = FgLawMaxD(FgLawKth(w.slow, FgLawMaxI(2, Kls / 2)), Es);  // the slow memory: to cross a band downward
    const double med = w.fast.n > 0 ? w.fast.s[w.fast.n / 2] : 0.0;
    w.E = E; w.L = Lv;
    if (w.warm > 0) { w.warm--; w.regime = FgLawReg(w, w.per); return w.per; }
    w.dwell++;
    const double P = w.per, F = w.floor;
    const double band = FgLawMaxD(P * 0.03, 0.35);
    const double top = FgLawMaxD(F, med > 0.0 ? FgLawMinD(40.0, med + 18.0) : 40.0);
    if (P <= F + 1e-9)
    {
        if (E > F && w.dwell >= 6)
        {
            const double to = FgLawMinD(top, FgLawSnap(w, w.head * Lv));
            if (to > F + 1e-9) { w.per = to; w.dwell = 0; w.moves++; }    // (the top at the floor: no place over it can be given, no rise)
        }
    }
    else
    {
        const double up = FgLawMinD(top, FgLawSnap(w, w.head * Lv));
        const double dn = w.head * Ed > F ? FgLawMinD(top, FgLawSnap(w, w.head * Ld)) : F;
        const int rP = FgLawReg(w, P);
        const double tol = rP == 1 ? 0.15 : band;
        if (up > P + (rP == 1 && FgLawReg(w, up) == 1 ? 0.15 : band))
        {
            if (w.dwell >= 6) { w.per = up; w.dwell = 0; w.moves++; }
        }
        else if (w.dwell >= 12 && (dn < P - tol || FgLawReg(w, dn) < rP))   // a lower regime's place is a way down whatever the tolerance
        {
            const double edge = rP == 2 ? w.freeMin : F + 0.2;
            const double least = rP == 2 ? 0.4 : 0.2;
            if (FgLawReg(w, dn) == rP)                                // inside the regime: half the gap
            { w.per = FgLawMaxD(dn, P - FgLawMaxD(least, (P - dn) * 0.5)); w.dwell = 0; w.moves++; }
            else if (P > edge + tol)                                  // first to the regime's edge
            { w.per = FgLawMaxD(edge, P - FgLawMaxD(least, (P - edge) * 0.5)); w.dwell = 0; w.moves++; }
            else                                                      // across: only when the slow memory agrees
            {
                const double dns = w.head * FgLawMaxD(Es, Ed) > F ? FgLawMinD(top, FgLawSnap(w, w.head * FgLawMaxD(Ls, Ld))) : F;
                if (FgLawReg(w, dns) < rP) { w.per = dns; w.dwell = 0; w.moves++; }
            }
        }
    }
    w.regime = FgLawReg(w, w.per);
    return w.per;
}

// ---- THE FREE BAND'S FORM: the way up's tolerance under the place and the level the way back to the floor asks, both per mille.
// Macros, so a desk proof can compile other values.
#ifndef FG_LAW_UP_TOL_PM
#define FG_LAW_UP_TOL_PM 0
#endif
#ifndef FG_LAW_BACK_PM
#define FG_LAW_BACK_PM 1060
#endif
// FgLawStep line for line but for the three lines marked FORM. A second body, not a shared one: FgLawStep is the harness's control
// and no shipped build compiles it. equiv_fglaw holds both against the oracle.
static inline double FgLawStepForm(FgLaw& w, double c, double t)
{
    w.hitch = 0;
    if (c > 0.2 && c < 100.0)
    {
        const bool hitch = c > 1.5 * w.per;
        w.hitch = hitch ? 1 : 0;
        w.run = hitch ? w.run + 1 : 0;
        if (w.run >= 8) w.shiftLeft = 240;                           // eight in a row: the level has moved
        if (!hitch && w.shiftLeft > 0) w.shiftLeft--;
        if (!hitch || w.shiftLeft > 0) { FgLawPush(w.fast, t, c); FgLawPush(w.slow, t, c); }
    }
    FgLawTrim(w.fast, t); FgLawTrim(w.slow, t);
    const int n = w.fast.n, m = w.slow.n;
    const int Ke = FgLawMaxI(6, (int)ceil(0.05 * (double)n)), Kl = FgLawMaxI(5, (int)ceil(0.02 * (double)n));
    const double E = FgLawKth(w.fast, Ke), Lv = FgLawMaxD(FgLawKth(w.fast, Kl), E);
    const double Ed = FgLawKth(w.fast, FgLawMaxI(3, Ke / 2)), Ld = FgLawMaxD(FgLawKth(w.fast, FgLawMaxI(2, Kl / 2)), Ed);   // half ranks: the way down
    const int Ks = FgLawMaxI(6, (int)ceil(0.05 * (double)m)), Kls = FgLawMaxI(5, (int)ceil(0.02 * (double)m));
    const double Es = FgLawKth(w.slow, FgLawMaxI(3, Ks / 2)), Ls = FgLawMaxD(FgLawKth(w.slow, FgLawMaxI(2, Kls / 2)), Es);  // the slow memory: to cross a band downward
    const double med = w.fast.n > 0 ? w.fast.s[w.fast.n / 2] : 0.0;
    w.E = E; w.L = Lv;
    if (w.warm > 0) { w.warm--; w.regime = FgLawReg(w, w.per); return w.per; }
    w.dwell++;
    const double P = w.per, F = w.floor;
    const double band = FgLawMaxD(P * 0.03, 0.35);
    const double top = FgLawMaxD(F, med > 0.0 ? FgLawMinD(40.0, med + 18.0) : 40.0);
    const double back = (double)FG_LAW_BACK_PM * 0.001;
    if (P <= F + 1e-9)
    {
        if (E > F && w.dwell >= 6)
        {
            const double to = FgLawMinD(top, FgLawSnap(w, w.head * Lv));
            if (to > F + 1e-9) { w.per = to; w.dwell = 0; w.moves++; }    // (the top at the floor: no place over it can be given, no rise)
        }
    }
    else
    {
        const double up = FgLawMinD(top, FgLawSnap(w, w.head * Lv));
                // FORM: to the floor only where the back level fits it; otherwise never under the first place over the floor
        const double dn = back * Ed > F ? FgLawMinD(top, FgLawSnap(w, FgLawMaxD(w.head * Ld, F + 1e-6))) : F;
        const int rP = FgLawReg(w, P);
        const double tol = rP == 1 ? 0.15 : band;
        if (up > P + P * ((double)FG_LAW_UP_TOL_PM * 0.001))          // FORM: the way up has no tolerance under the place
        {
            if (w.dwell >= 6) { w.per = up; w.dwell = 0; w.moves++; }
        }
        else if (w.dwell >= 12 && (dn < P - tol || FgLawReg(w, dn) < rP))   // a lower regime's place is a way down whatever the tolerance
        {
            const double edge = rP == 2 ? w.freeMin : F + 0.2;
            const double least = rP == 2 ? 0.4 : 0.2;
            if (FgLawReg(w, dn) == rP)                                // inside the regime: half the gap
            { w.per = FgLawMaxD(dn, P - FgLawMaxD(least, (P - dn) * 0.5)); w.dwell = 0; w.moves++; }
            else if (P > edge + tol)                                  // first to the regime's edge
            { w.per = FgLawMaxD(edge, P - FgLawMaxD(least, (P - edge) * 0.5)); w.dwell = 0; w.moves++; }
            else                                                      // across: only when the slow memory agrees
            {
                // FORM: the back level again, over the slow memory
                const double dns = back * FgLawMaxD(Es, Ed) > F ? FgLawMinD(top, FgLawSnap(w, FgLawMaxD(w.head * FgLawMaxD(Ls, Ld), F + 1e-6))) : F;
                if (FgLawReg(w, dns) < rP) { w.per = dns; w.dwell = 0; w.moves++; }
            }
        }
    }
    w.regime = FgLawReg(w, w.per);
    return w.per;
}

// ---- the switch-on's ramp
// On a card that is the limit the game standing by keeps the card's queue full, so switching on at the panel's floor added frame
// generation's work to a full queue and the first frames hitched. Gliding the period down before the first pair is worse: it
// drains the queue with nothing generated and the first pair then adds its latency at once. So the switch-on is at once to the
// right period with room to drain: at an engage's first present the pacer's period is the target plus FG_RAMP_DRAIN_PM per mille,
// easing to the target FG_RAMP_EASE_PM per mille a present, held while the law warms up, then let go FG_RAMP_STEP_PM per mille a
// present until the machine's own answer stands over it.
// The target (FgRampTargetUs, once a window on the waiter): 1 / R_on as the law predicts it. Where nothing is measured and the
// card is the limit or unknown, c = FG_RAMP_C0_PM per mille of the game's own period (measured about 0.5). Never past the floor's
// out-rate period. A card with idle room asks for no target. The machine takes the larger of the target and its own floors.
// Every stand-down is at once.
enum { FG_RAMP_EASE_PM = 4, FG_RAMP_STEP_PM = 40, FG_RAMP_C0_PM = 500, FG_RAMP_LEAST_PM = 30 };
#ifndef FG_RAMP_DRAIN_PM
#define FG_RAMP_DRAIN_PM 80                          // a macro, so a desk proof can compile another
#endif
// The target in us (0 none). aloneX10: the game's own rate standing by, tenths; predX10: the rate the law engages on
// (FgDrivePredHiX10); measured: c was measured; card: 1 the limit, 0 idle room, -1 unknown; floorOutX10: the floor's out-rate.
// Where c is measured the target is the prediction whatever the card says; the prior only where nothing is measured.
static inline int FgRampTargetUs(int aloneX10, int predX10, int measured, int card, int floorOutX10)
{
    if (aloneX10 <= 0) return 0;
    const double own = 10000000.0 / (double)aloneX10;
    if (!measured && card == 0) return (int)(own + 0.5);     // idle room and nothing measured: no switch
    double t = predX10 > 0 ? 10000000.0 / (double)predX10 : own;
    if (t < own) t = own;
    if (!measured) t = own * (1.0 + (double)FG_RAMP_C0_PM * 0.001);   // the first engage's prior
    const double cap = floorOutX10 > 0 ? 10000000.0 / (double)floorOutX10 : 40000.0;
    if (t > cap) t = cap > own ? cap : own;
    if (t > 40000.0) t = 40000.0;
    return (int)(t + 0.5);
}
// The longest period the switch may hold, us: the floor's out-rate period (0: none, 40 ms).
static inline int FgRampCapUs(int floorOutX10) { return floorOutX10 > 0 ? (int)(10000000.0 / (double)floorOutX10 + 0.5) : 40000; }
// The target plus the drain's margin, never over the cap unless the target is (ms).
static inline double FgRampFloorMs(double target, double capMs)
{
    double f = target * (1.0 + (double)FG_RAMP_DRAIN_PM * 0.001);
    const double c = capMs > target ? capMs : target;
    return f > c ? c : f;
}
// While the law warms up: eased toward the target, never under it (ms).
static inline double FgRampEaseMs(double floor, double target)
{
    const double n = floor * (1.0 - (double)FG_RAMP_EASE_PM * 0.001);
    return n > target ? n : target;
}
// After the warm-up: one present's step down (ms).
static inline double FgRampLetGoMs(double floor) { return floor * (1.0 - (double)FG_RAMP_STEP_PM * 0.001); }

