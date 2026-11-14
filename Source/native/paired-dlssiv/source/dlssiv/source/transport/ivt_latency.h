#pragma once
// The latency instrument, render-thread half: picture age, from a frame's CPU start to the GPU retiring the frame that shows
// it, from GPU-signalled timelines. Off unless the cfg asks. The waiter is in DlssIvTransport.h (LatWaiter).
#include <intrin.h>
#include <algorithm>
// windows.h's min and max macros would break the vendored header: pushed away for the include only.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#pragma warning(push)
#pragma warning(disable: 4244)   // the vendored header converts between int64 and double freely; its author's choice, kept unmodified
#include "../third_party/latencyflex/latencyflex.h"   // the pacer's estimator, Apache-2.0, unmodified (licenses\LICENSE-LatencyFleX.txt)
#pragma warning(pop)
#pragma pop_macro("max")
#pragma pop_macro("min")

// The read-mostly flags on a cache line of their own, away from per-frame writers; macros keep their old names.
struct __declspec(align(64)) LatFlagsLine
{
    bool on;        // latOn: set after the cfg is read, cleared when nothing would read it; read every present
    bool pacerOn;   // latPacerOn: pacer=1, or pacer=-1 with responsiveness >= 2
    bool vsync;     // latVsync: the effective present interval is not IMMEDIATE: the pacer stands down
    bool limiter;   // latLimiter: FusionFix's FPS limit or DXVK's d3d9.maxFrameRate is on: the pacer stands down
    bool endOn;     // latEndOn: the frame-end marker is on (DlssIvTransport.h, LatEndSignal); the presenting thread tests it every present
    bool guard;     // latGuard: the refresh guard is armed (RgArm, DLSS-IV.cpp); both present sites test it
};
static LatFlagsLine latFlags;                 // zero-initialised: every flag starts false
#define latOn      (latFlags.on)
#define latPacerOn (latFlags.pacerOn)
#define latVsync   (latFlags.vsync)
#define latLimiter (latFlags.limiter)
#define latEndOn   (latFlags.endOn)
#define latGuard   (latFlags.guard)
static int  latQueueAsked = 0;                // the d3d9.maxFrameLatency the mod put into DXVK_CONFIG (0: none); the verdict compares
static char latQueueNote[200] = "";           // what the setter and the verdict decided, printed on the log=1 latency line
enum { LAT_RING = 64, LAT_WIN = 120 };

// Stamped at each Present's return: the next frame's CPU start, keyed by the `frames` value that frame's capture will carry.
// Written -1, stamp, value; the waiter keeps a pair only if value reads the same before and after. Plain ordered stores suffice
// on x86; the stamp is one aligned 8-byte move, since WriteNoFence64 is two 32-bit moves on x86 and could tear.
static volatile LONG     latBeginFrame[LAT_RING];
alignas(8) static volatile LONGLONG latBeginQpc[LAT_RING];
static volatile LONG     latLeadAt[LAT_RING];       // the device thread's lead at that present: presents recorded minus presents done
static volatile LONG     latOrd[LAT_RING];          // injection n -> the frames value it carried; written on the injection thread
static volatile LONG     latPresFrame[LAT_RING];    // recorded present ordinal -> the frames value of the frame it ends (the worker's end signal reads it)
static volatile LONG     latOrdAsync[LAT_RING];     // injection n -> 1 pipelined, 0 same frame, as the body ran it (mode 4 flips per frame)
static volatile LONG     latAsyncWant = 1;          // mode 4's decision, read by the capture on the render thread: 1 pipelined, 0 same frame
static volatile LONGLONG latBlockTicks = 0;        // threaded=0 only: time inside the original Present, summed (the worker's own sum serves threaded=1)
static volatile LONG     latBlockN     = 0;
// Development builds: the worker's clock after each DXVK Present and the time inside it, so the flip classifier can check that
// DT_R_PACE ties the render thread's return to the worker's. Same -1, values, k protocol as the begin stamps.
DEVSTAT(DLSSIV_FGDATA alignas(8) static volatile LONGLONG latFlipRelQpc[LAT_RING];)
DEVSTAT(DLSSIV_FGDATA static volatile float latFlipRelBlk[LAT_RING];)   // the ticks the worker spent inside that Present, as a float
DEVSTAT(DLSSIV_FGDATA static volatile LONG latFlipRelN[LAT_RING];)      // -1 while the slot is written, then the ordinal k

// The pacer holds the render thread after Present until the GPU is about to be free (LatencyFleX's estimate), so a one-frame
// queue does not leave the GPU idle. No lock held while sleeping. Stands down under vsync, after a Reset, and with no live
// frames. Capped at 20 ms, under the device thread's 25 ms idle settle.
static char          latLimiterNote[240] = "";   // may hold a full user path
// FusionFix's cap, from LatReadLimiterCfg at load (only when latOn or log>=1). latFfCap: fixed fps, 0 none. latFfRel: n for a
// Custom FpsLimit of -n (refresh minus n). DLSSIV_FGDATA keeps them out of .bss here, where they would move hot statics.
DLSSIV_FGDATA static int latFfCap, latFfRel;
// DXVK's own fps cap, 0 none, for the flip classifier (part of its key). The pacer stands down only for FusionFix's cap.
DLSSIV_FGDATA static int latDxvkCap;
static int           latVsyncCfg  = -1;         // dxvk.conf's d3d9.presentInterval when set (0 off, 1 on), else -1
static SRWLOCK       latPaceLock  = SRWLOCK_INIT;
static lfx::LatencyFleX latLfx;
static volatile LONG latPaceArmed = 0;          // the waiter: live frames and no vsync
static volatile LONG latPaceSkip  = 0;          // frames to skip after a Reset or a resync
static volatile LONG latPaceHold  = 0;          // mode 4: the pacer is held (no sleep, the estimator untouched) below the ladder's rung 2
static volatile LONG latPeriodUs  = 0;          // the waiter's GPU period estimate, for the sleep cap
static volatile LONGLONG latPaceSleptTicks = 0; // stats: time slept, presents that slept, presents the pacer saw
static volatile LONG latPaceSleptN = 0, latPaceFrames = 0;
static HANDLE        latPaceTimer = nullptr;
static bool          latPaceTimerTried = false, latPaceHighRes = false;
static LONGLONG      latQpf = 0;
static double        latNsPerTick = 0.0;        // 1e9 / latQpf; 0 while latQpf is 0
static inline uint64_t LatQpcNs(LONGLONG t) { return (uint64_t)((double)t * latNsPerTick); }
// The first tick LatQpcNs maps to ns or later, so the spin compares raw ticks. With no frequency it answers "never".
static inline LONGLONG LatNsTick(uint64_t ns)
{
    if (!latQpf) return LLONG_MAX;
    LONGLONG t = (LONGLONG)((double)ns / latNsPerTick);
    while (t > 0 && LatQpcNs(t - 1) >= ns) t--;
    while (LatQpcNs(t) < ns) t++;
    return t;
}
static inline bool LatVsyncEffective(UINT interval)   // the cfg's override, else the game's interval
{
    if (latVsyncCfg >= 0) return latVsyncCfg != 0;
    return interval != D3DPRESENT_INTERVAL_IMMEDIATE;
}
static void LatPaceReset()
{
    AcquireSRWLockExclusive(&latPaceLock); latLfx.Reset(); ReleaseSRWLockExclusive(&latPaceLock);
    _InterlockedExchange(&latPaceSkip, 16);
}
static void LatPaceHoldSet(bool hold)            // mode 4's ladder: releasing the hold restarts the estimator, so it only ever paces frames it has seen
{
    if (hold) { _InterlockedExchange(&latPaceHold, 1); return; }
    if (!InterlockedCompareExchange(&latPaceHold, 0, 0)) return;
    LatPaceReset();
    _InterlockedExchange(&latPaceHold, 0);
}
// Development builds: the timer's wake lateness (sum, count, worst, past the slack), for the log=2 line.
DEVSTAT(static volatile LONGLONG latTimerLateNs = 0, latTimerLateWorst = 0; static volatile LONG latTimerN = 0, latTimerPastSlack = 0;)
// Returns its last clock reading, taken after any wait: the frame begin and the stamp both use it.
static LONGLONG LatSleepUntil(uint64_t targetNs, uint64_t nowNs)
{
    LARGE_INTEGER now = {};
    if (!latPaceTimerTried)
    {
        latPaceTimerTried = true;
        latPaceTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        latPaceHighRes = latPaceTimer != nullptr;
        if (!latPaceTimer) latPaceTimer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);   // Wine, or an old Windows
        QueryPerformanceCounter(&now); nowNs = LatQpcNs(now.QuadPart);   // once only: the caller's reading is older than the timer's creation
    }
        // The high-resolution timer wakes about 0.3 ms late, so it is set 0.6 ms short and a spin finishes.
    const uint64_t coarse = latPaceHighRes ? 600000ull : 1500000ull;   // the timer's own slack: 0.6 ms high-res, 1.5 ms otherwise
        // The slack is tested before it is subtracted, or the unsigned wait would wrap.
    if (targetNs > nowNs + 400000ull && targetNs - nowNs > coarse)
    {
        const uint64_t waitNs = targetNs - nowNs - coarse;
        if (latPaceTimer && waitNs > (latPaceHighRes ? 300000ull : coarse))   // a wait the timer is worth setting for
        {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(waitNs / 100ull);
            if (SetWaitableTimer(latPaceTimer, &due, 0, nullptr, nullptr, FALSE))
            {
                WaitForSingleObject(latPaceTimer, 30);
                DEVSTAT(LARGE_INTEGER wk; QueryPerformanceCounter(&wk); const LONGLONG late = (LONGLONG)LatQpcNs(wk.QuadPart) - (LONGLONG)(nowNs + waitNs); InterlockedExchangeAdd64(&latTimerLateNs, late); _InterlockedIncrement(&latTimerN); if (late > (LONGLONG)coarse) _InterlockedIncrement(&latTimerPastSlack); LONGLONG lw; do { lw = InterlockedCompareExchange64(&latTimerLateWorst, 0, 0); } while (late > lw && InterlockedCompareExchange64(&latTimerLateWorst, late, lw) != lw);)
            }
        }
        else if (waitNs > 2000000ull) Sleep((DWORD)((waitNs - 1000000ull) / 1000000ull));
    }
        // Stop at the target, or bail when more than 3 ms short (the estimate is off).
    const LONGLONG targetTick = LatNsTick(targetNs);
    const LONGLONG farTick    = targetNs > 3000000ull ? LatNsTick(targetNs - 3000000ull) : LLONG_MIN;
    for (int spins = 0; spins < 400000; spins++)              // the rest by the clock, bounded
    {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= targetTick) break;
        if (now.QuadPart < farTick) break;                    // never spin more than 3 ms: the estimate is off
        YieldProcessor();
    }
    return now.QuadPart;
}
// Returns its last clock reading, or 0 when it read none.
static LONGLONG LatPace(LONG f)
{
    latPaceFrames++;   // one writer; counts every present the pacer saw
    if (!latPaceArmed || latVsync || latLimiter || latPaceHold) return 0;
    if (InterlockedCompareExchange(&latPaceSkip, 0, 0) > 0) { _InterlockedDecrement(&latPaceSkip); return 0; }   // after the arm and hold tests, so free when off
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    const uint64_t nowNs = LatQpcNs(now.QuadPart);
    AcquireSRWLockExclusive(&latPaceLock);
    const uint64_t target = latLfx.GetWaitTarget((uint64_t)(ULONG)f);
    if (target <= nowNs)
    {   // No sleep: one hold, and BeginFrame gets the same reading
                // A hold never spans a sleep.
        latLfx.BeginFrame((uint64_t)(ULONG)f, target, nowNs);
        ReleaseSRWLockExclusive(&latPaceLock);
        return now.QuadPart;
    }
    ReleaseSRWLockExclusive(&latPaceLock);
    uint64_t capNs = 20000000ull;                                              // 20 ms, under the device thread's settle
    const uint64_t twoP = (uint64_t)latPeriodUs * 2000ull;
    if (twoP && twoP < capNs) capNs = twoP;
    const uint64_t until = (target - nowNs > capNs) ? nowNs + capNs : target;
    const LONGLONG after = LatSleepUntil(until, nowNs);
    AcquireSRWLockExclusive(&latPaceLock);
    latLfx.BeginFrame((uint64_t)(ULONG)f, target, LatQpcNs(after));
    ReleaseSRWLockExclusive(&latPaceLock);
    const LONGLONG slept = after - now.QuadPart;
    if (slept > 0) { InterlockedExchangeAdd64(&latPaceSleptTicks, slept); _InterlockedIncrement(&latPaceSleptN); }
    return after;
}

static void LatOnPresent(LONG lead, LONGLONG blockTicks)
{
    LARGE_INTEGER t0; t0.QuadPart = latPacerOn ? LatPace(frames) : 0;   // may sleep; its reading is taken after, so sleep cannot shorten the picture age
    if (!t0.QuadPart) QueryPerformanceCounter(&t0);                     // the pacer read no clock: off, stood down, held or skipping
    const LONG f = frames;                         // the value the next capture will carry (see the header comment)
    const int  i = (int)(f & (LAT_RING - 1));
    WriteRelease(&latBeginFrame[i], -1);           // plain ordered stores on x86 (see the declaration)
    __iso_volatile_store64(&latBeginQpc[i], t0.QuadPart);
    if (telemetryOn) WriteNoFence(&latLeadAt[i], lead);   // only when the log is on
    WriteRelease(&latBeginFrame[i], f);
    if (blockTicks) { InterlockedExchangeAdd64(&latBlockTicks, blockTicks); _InterlockedIncrement(&latBlockN); }
}

// Defined after DllMain (DLSS-IV.cpp), so no hot symbol moves.
static void RgOnPresent();
