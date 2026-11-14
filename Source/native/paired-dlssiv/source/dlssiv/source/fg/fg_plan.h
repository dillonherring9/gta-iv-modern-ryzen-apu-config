// fg_plan.h: the planned generation time and the room floor, as pure functions shared by the machine, the desk proofs
// (tools/test-fg-cond) and the log replay (tools/fg-pace-replay).
// A pair's slot is tS + kPlanUs, and every real frame pays kPlanUs; a generated frame not in at the slot is lost. The plan
// is an upper quantile of K (FgShadowPlacer::kHist, handoff retire to answer) as measured, never a table.
#pragma once
#include "fg_law.h"

struct FgPlanK
{
    LONG quantilePm;        // quantile of K the plan covers, per mille (950: nineteen answers in twenty are in at the slot)
    LONG minPairs;          // fewer pairs than this in the windows: the plan does not move
    LONG padUs;             // added to the quantile's upper edge
    LONG minUs, maxUs;      // the plan's bounds
    LONG startUs;           // the plan before any K was measured
    LONG windows;           // how many of the waiter's windows the quantile is taken over (1 to FG_PLAN_WINDOWS)
    LONG downWindows;       // the long memory (FgPlanStep): the plan comes down only when the quantile over this many windows
                            // fits lower, and not within as many windows of a rise. 0: no memory
};
enum { FG_PLAN_WINDOWS = 4 };
static inline FgPlanK FgPlanDefaults()
{
    FgPlanK k = { 950, 32, 0, 250, 8000, 2000, 2, 0 };
    return k;
}
// hist: K's counts over the windows the plan is taken from (bins of binUs; the last bin is "at or over").
// Returns the upper edge of the bin the quantile falls in, plus the pad, held to the bounds. A quantile in the last bin
// has no upper edge: the plan is its upper bound. Too few pairs: prev, unchanged (still held to the bounds).
static inline LONG FgPlanFromHist(const LONG* hist, int bins, int binUs, const FgPlanK& k, LONG prev)
{
    LONG out = prev > 0 ? prev : k.startUs;
    long long total = 0;
    for (int i = 0; i < bins; i++) if (hist[i] > 0) total += hist[i];
    if (total >= (long long)(k.minPairs > 1 ? k.minPairs : 1))
    {
        long long run = 0; int at = bins - 1;
        for (int i = 0; i < bins; i++)
        {
            if (hist[i] > 0) run += hist[i];
            if (run * 1000 >= total * (long long)k.quantilePm) { at = i; break; }
        }
        out = at >= bins - 1 ? k.maxUs : (LONG)(at + 1) * binUs + k.padUs;
    }
    if (out < k.minUs) out = k.minUs;
    if (out > k.maxUs) out = k.maxUs;
    return out;
}

// ---- the plan across windows ----
// What the step keeps from one window to the next.
struct FgPlanMem
{
    LONG last;              // the plan as the step last left it: one found above it was raised inside the window by the placer
    LONG hold;              // windows for which no way down is taken
};
// How close two late answers may come. A plan covering quantile q of K lets one answer in 1000 / (1000 - q) be late. A
// second late answer fewer pairs than that behind the last says the plan is too short now, and the placer raises it one
// bin at once (ArmedPlanLate): a late answer is cured by a longer plan, where a longer period cures none.
// 0: the quantile lets none be late, no such rule.
static inline LONG FgPlanLateSpan(LONG quantilePm)
{
    const LONG rest = 1000 - quantilePm;
    return rest > 0 ? (1000 + rest - 1) / rest : 0;
}
// One window's step. up: K's counts over the last k.windows windows; down: over the last k.downWindows; cur: the plan in
// force (the placer may have raised it since). Up at once, to the short memory's quantile. Down only when the long
// memory's quantile fits lower too, to the larger of the two, and not while a rise is younger than the long memory:
// otherwise a plan on the edge K drifts over moves up and down every other window. With downWindows at 0 the caller
// hands the same counts twice and the answer is FgPlanFromHist's.
static inline LONG FgPlanStep(FgPlanMem& m, const LONG* up, const LONG* down, int bins, int binUs, const FgPlanK& k, LONG cur)
{
    const LONG held = k.downWindows > 0 ? k.downWindows : 0;
    if (cur > m.last) m.hold = held;                              // raised inside the window: it stands as any rise does
    const LONG u = FgPlanFromHist(up, bins, binUs, k, cur), d = FgPlanFromHist(down, bins, binUs, k, cur);
    LONG next = cur;
    if (u > cur) { next = u; m.hold = held; }
    else if (m.hold > 0) m.hold--;
    else { const LONG to = d > u ? d : u; if (to < cur) next = to; }
    m.last = next;
    return next;
}

// ---- the room floor's law (ArmedRoom measures a pair's need and publishes the floor) ----
// An order statistic, so one slow pair moves nothing: it rises when the sixth largest need of the last 120 pairs asks for
// more, and returns when the third largest fits a lower step. Needs are kept without the plan and margin, which are added
// as they stand now.
enum { FG_ROOM_PAIRS = 120, FG_ROOM_RANK_PM = 50, FG_ROOM_RANK_MIN = 6, FG_ROOM_TOP = 8 };
static_assert(FG_ROOM_PAIRS * FG_ROOM_RANK_PM <= 1000 * FG_ROOM_TOP && FG_ROOM_RANK_MIN <= FG_ROOM_TOP, "FgRoomTake keeps the FG_ROOM_TOP largest: the rank must fit");
struct FgRoomMem
{
    LONG at, n;             // the next place, and how many are held (to FG_ROOM_PAIRS)
    LONG v[FG_ROOM_PAIRS];  // the last pairs' needs without the plan and the margin, us
};
// One pair's need in; out the two statistics: up, the rank-th largest (0 while fewer are held: nothing rises on fewer
// than six pairs), and down, the half-rank-th largest (0 while fewer than three).
static inline void FgRoomTake(FgRoomMem& m, LONG baseUs, LONG* up, LONG* down)
{
    m.v[m.at] = baseUs; m.at = (m.at + 1) % FG_ROOM_PAIRS;
    if (m.n < FG_ROOM_PAIRS) m.n++;
    LONG rank = (m.n * FG_ROOM_RANK_PM + 999) / 1000; if (rank < FG_ROOM_RANK_MIN) rank = FG_ROOM_RANK_MIN;
    LONG half = rank / 2; if (half < 3) half = 3;
    LONG top[FG_ROOM_TOP]; LONG held = 0;                         // the `rank` largest, descending, in one pass
    for (LONG i = 0; i < m.n; i++)
    {
        const LONG x = m.v[i];
        if (held == rank && x <= top[held - 1]) continue;
        LONG j = held < rank ? held++ : held - 1;
        while (j > 0 && top[j - 1] < x) { top[j] = top[j - 1]; j--; }
        top[j] = x;
    }
    *up = m.n >= rank ? top[rank - 1] : 0;
    *down = m.n >= half ? top[half - 1] : 0;
}
// The floor, a period in us, from the one in force and the two statistics: twice (the need plus the plan and margin
// added now), in steps of stepUs rounded up. down is never under up, so a floor between the two stays.
static inline LONG FgRoomFloor(LONG cur, LONG upUs, LONG downUs, LONG addUs, LONG stepUs)
{
    if (upUs > 0)   { const LONG f = ((2 * (upUs + addUs) + stepUs - 1) / stepUs) * stepUs; if (f > cur) return f; }
    if (downUs > 0) { const LONG f = ((2 * (downUs + addUs) + stepUs - 1) / stepUs) * stepUs; if (f < cur) return f; }
    return cur;
}

// ---- the ride's floors (see "the real frame rides behind the next frame's upscale" in fg_machine_armed.h) ----
// The law above with the multiple handed in: `times` is 2 for a need half a period must hold, 1 for the whole period.
// Its two tests are written the other way round from FgRoomFloor's on purpose: tools/fg-pace-replay breaks that
// function's lines by their text, and each must stand in this file once.
static inline LONG FgRoomFloorK(LONG cur, LONG upUs, LONG downUs, LONG addUs, LONG stepUs, LONG times)
{
    if (upUs > 0)   { const LONG f = ((times * (upUs + addUs) + stepUs - 1) / stepUs) * stepUs; if (cur < f) return f; }
    if (downUs > 0) { const LONG f = ((times * (downUs + addUs) + stepUs - 1) / stepUs) * stepUs; if (cur > f) return f; }
    return cur;
}
// The median of the last values (the ride's place behind its pair's handoff, ArmedRideTake): up to FG_RIDE_D of them,
// 0 while none is held. One odd frame moves nothing.
enum { FG_RIDE_D = 8 };
struct FgRideMem { LONG at, n; LONG v[FG_RIDE_D]; };
static inline LONG FgRideTake(FgRideMem& m, LONG us)
{
    m.v[m.at] = us; m.at = (m.at + 1) % FG_RIDE_D;
    if (m.n < FG_RIDE_D) m.n++;
    LONG t[FG_RIDE_D];
    for (LONG i = 0; i < m.n; i++) { LONG x = m.v[i], j = i; while (j > 0 && t[j - 1] > x) { t[j] = t[j - 1]; j--; } t[j] = x; }
    return t[m.n / 2];
}
