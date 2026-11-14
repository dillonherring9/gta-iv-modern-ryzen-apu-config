// The fixed mode's constants and its rule for the card's wait. Pure; also replayed by tools/fg-fix-replay/fixpace.cpp.
#pragma once

// The fixed mode's constants (PaceFixed). At most 0.3 percent in all; 3 late presents of 60: the game's pace.
enum { FG_FIX_WIN = 60, FG_FIX_TARGET_US = 500, FG_FIX_TRACK_US = 25, FG_FIX_PPM0 = -300, FG_FIX_STEP_PPM = 1000, FG_FIX_MAX_PPM = 3000,
       FG_FIX_MAX_I_PPM = 2500, FG_FIX_LATE_MIN = 3, FG_FIX_STALL_REFRESHES = 4, FG_FIX_INORIG_US = 100,
       FG_FIX_LEARN_US = 1000, FG_FIX_BLIND_I_PPM = -1200 };
// At the correction's cap a FIFO's hold drains by 2 ms and more a window, so a median block over the learning band that has not
// fallen by FG_FIX_TARGET_US across a window at the cap, two windows running, is DXVK's frame-latency wait on the card's own
// queued work, which no period mends. While read so, PaceFixed holds the correction and the integral learns nothing, until the
// median is back within FG_FIX_LEARN_US of the target. run: 0 to 2, the caller's. Returns 1 while the block is the card's.
static inline int FgFixCardStep(int& run, int card, long ppmInForce, long medUs, long prevMedUs)
{
    if (ppmInForce >= FG_FIX_MAX_PPM && medUs - FG_FIX_TARGET_US > FG_FIX_LEARN_US && medUs + FG_FIX_TARGET_US > prevMedUs) { if (run < 2) run++; }
    else run = 0;
    if (run >= 2) return 1;
    return (card && medUs - FG_FIX_TARGET_US > FG_FIX_LEARN_US) ? 1 : 0;
}
