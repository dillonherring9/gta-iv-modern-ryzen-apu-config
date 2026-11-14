// Frame generation settings from the player's cfg, and the shipped rules built on the conditions (fg_cond.h).
// Pure functions over plain numbers, so the harness (tools/perfproof/dt_fg_ship.h) runs the same code the product does.
#pragma once

// fg_min_fps as read from the cfg: 0 when absent (the floor then follows the display), else 30 to 240.
struct FgShipKeys { int minFps; };
static inline void FgShipCondKeys(FgCondK& k, const FgShipKeys& f)
{
    FgCondFloorKey(k, f.minFps);
}
// Which fixed-mode line a window says: 1 the game is held to half the refresh (frame generation runs),
// 2 half the refresh is under the floor (frame generation never runs), 0 neither. The caller says each line once a session.
static inline int FgShipFixWarn(const FgCondIn& in, const FgCondK& k, int row, int lawOn)
{
    if (!FgCondFixedMode(in)) return 0;
    if (lawOn && row == FGC_OK) return 1;
    if (row == FGC_SLOW && FgCondFixedUnderFloor(in, k)) return 2;
    return 0;
}
static inline void FgShipSeed(FgCondIn& in, const FgShipKeys& f)
{
    (void)f; in.fixedKey = 1;
}

// Whether the display runs variable refresh for the game. NVIDIA's driver answer is checked against the panel itself: the flip
// classifier (DLSS-IV.cpp, LatFlipStep) reads FREE when each present is shown as it comes and QUANTISED when presents land on a
// refresh grid. Its memory counts only from windows where the mod's own pacer did not hold the game, since that pace says nothing
// about the screen. Elsewhere (other vendors, Wine, no NVIDIA answer) the classifier alone decides.
// QUANTISED means fixed whatever NVIDIA says. FREE means variable even against NVIDIA's no, except under NVIDIA's Fast Sync,
// which looks the same on a fixed screen. AMD's Enhanced Sync on a fixed screen also reads FREE and cannot be told apart.
// Read once per display (a new refresh or a Reset clears it), never per frame, and never flipped inside one display.
struct FgShipVrr { unsigned long long key; int keyed, keyWin, heldWin, ev; unsigned long long waitHz; int waitKeyed, waitWin; };
// A new display key drops the evidence. The wait for the panel's answer restarts only at a new refresh, not at a Reset of the
// same display, so a panel that never answers is not asked again at every build. Returns 1 when the key changed.
static inline int FgShipVrrKey(FgShipVrr& v, unsigned long long key, unsigned long long hz, int win)
{
    if (!v.waitKeyed || hz != v.waitHz) { v.waitHz = hz; v.waitKeyed = 1; v.waitWin = win; }
    if (v.keyed && key == v.key) return 0;
    v.key = key; v.keyed = 1; v.keyWin = win; v.ev = 0;
    return 1;
}
// mem: the classifier's memory (0 none, 1 FREE, 2 QUANTISED), formed at window memSince. held: the mod's pacer held window win.
static inline void FgShipVrrTake(FgShipVrr& v, int mem, int memSince, int win, int held)
{
    if (held) { if (win > v.heldWin) v.heldWin = win; return; }
    if (v.ev || (mem != 1 && mem != 2)) return;
    if (memSince - 1 > v.heldWin && memSince > v.keyWin) v.ev = mem;
}
// The value FgCondIn::vrr takes: 1 variable, 0 fixed, -1 unknown. nv: NVIDIA's answer (1, 0, -1 none).
static inline int FgShipVrrAnswer(const FgShipVrr& v, int nv, int fastSync)
{
    if (v.ev == 2) return 0;
    if (v.ev == 1) return (nv == 0 && fastSync) ? 0 : 1;
    return nv;
}
// True while the answer may still form: no NVIDIA answer, none from the panel yet, vsync on (the classifier needs it), and fewer
// than FG_SHIP_VRR_WAIT_WIN windows since the refresh was read. Meanwhile frame generation stands by and the helper keeps its set,
// so an answer that forms before the next build can still run it. After the wait the row is said and no set is asked for.
enum { FG_SHIP_VRR_WAIT_WIN = 30 };
static inline int FgShipVrrMayForm(const FgShipVrr& v, int nv, int vsync, int win)
{
    return (nv < 0 && v.ev == 0 && vsync && v.waitKeyed && win - v.waitWin < FG_SHIP_VRR_WAIT_WIN) ? 1 : 0;
}

// The better refresh: on a fixed screen with vsync, where frame generation stands by because the game is under the fixed mode's
// floor, name once in the warnings file the highest offered refresh at which the fixed mode would run and show more than now.
// The mod never changes the display itself. The even rate a fixed screen of H Hz shows a game of G fps is the highest H/d
// (d = 1 to 16) that is no more than 2G (144 Hz at 62 fps: 72; 120 Hz at 62 fps: 120).
enum { FG_SHIP_BETTER_WIN = 5 };
static inline int FgShipEvenX10(int hzX1000, int gameX10)
{
    if (hzX1000 <= 0 || gameX10 <= 0) return 0;
    for (int d = 1; d <= 16; d++)
    {
        const int sX10 = (int)((long long)hzX1000 / 100 / d);
        if (sX10 <= 2 * gameX10) return sX10;
    }
    return 0;
}
struct FgShipBetter { int run, loX10, hiX10, said; };
// Once a window: the game's least rate over a steady run of FG_SHIP_BETTER_WIN windows (tenths of a frame), else 0.
static inline int FgShipBetterStep(FgShipBetter& b, const FgCondIn& in, int row, int lawOn)
{
    const int f = in.realFpsX10;
    if (b.said || lawOn || in.engaged || row != FGC_SLOW || !FgCondFixedMode(in) || f <= 0) { b.run = 0; return 0; }
    if (!b.run) { b.loX10 = f; b.hiX10 = f; }
    else { if (f < b.loX10) b.loX10 = f; if (f > b.hiX10) b.hiX10 = f; }
    b.run++;
    if ((long long)b.hiX10 * 10 > (long long)b.loX10 * 11) { b.run = 1; b.loX10 = f; b.hiX10 = f; }   // not steady: start a new run
    return b.run >= FG_SHIP_BETTER_WIN ? b.loX10 : 0;
}
// The highest of the n offered refreshes (whole Hz) where the conditions read no row with the fixed mode on and that beats the
// even rate at the current refresh; 0 when none.
static inline int FgShipBetterHz(const FgCondIn& in, const FgCondK& k, int gameX10, const int* hz, int n)
{
    const int hzNow = FgCondHzX1000(in), evenX10 = FgShipEvenX10(hzNow, gameX10), hzNowW = (hzNow + 500) / 1000;
    int best = 0;
    for (int i = 0; i < n; i++)
    {
        const int r = hz[i];
        if (r <= best || r == hzNowW || r * 10 <= evenX10) continue;
        FgCondIn t = in;
        t.refreshNum = r; t.refreshDen = 1;
        t.engaged = 0; t.engaging = 0; t.fixOver = 0; t.fixEntryX10 = 0; t.fixLUs = 0;
        t.realFpsX10 = gameX10; t.freeFpsX10 = 0;
        if (FgCondFixedMode(t) && FgCondFirst(t, k) == FGC_OK) best = r;
    }
    return best;
}

// The long stretch with no engage. The rate rows come and go and never reach the warnings file on their own. A session that asked
// for frame generation and never switched it on in its first FG_SHIP_LONG_MS of play (menus excluded) is told once why, when one
// family of rows stood in at least FG_SHIP_LONG_SHARE_PM per mille of the windows: FAST (the game is too fast for it to help) or
// SLOW/ONSLOW (too slow). A SLOW already explained by its own line is not counted. Any engage ends the rule for the session.
struct FgShipLong { int win, fast, heavy, done; unsigned last, playMs; };
enum { FG_SHIP_LONG_MS = 300000, FG_SHIP_LONG_MIN_WIN = 60, FG_SHIP_LONG_SHARE_PM = 750, FG_SHIP_LONG_GAP_MS = 10000 };
// fg_min_fps above what a variable refresh screen allows: the doubling's real rate there is C (59.1 at 120 Hz), so a key over C
// keeps FGC_SLOW standing for good. Returns the highest key this screen allows (C rounded down), or 0. Only on a display read as
// variable refresh: on a fixed screen with vsync the fixed mode runs at half the refresh, which the key may allow.
static inline int FgShipFloorUnreachable(const FgCondIn& in, const FgCondK& k)
{
    return (in.vrr == 1 && FgCondVrrUnderKey(in, k)) ? FgCondCFpsX10(in) / 10 : 0;
}
// Once a window. now: GetTickCount milliseconds (only differences are used, so the wrap is harmless). Returns the row to say
// (FGC_FAST, FGC_SLOW or FGC_ONSLOW) once, or 0. A gap over FG_SHIP_LONG_GAP_MS between windows (paused, unfocused) is not play.
static inline int FgShipLongStep(FgShipLong& L, const FgCondIn& in, const FgCondK& k, int row, int lawOn, int menu, unsigned now)
{
    if (L.done) return 0;
    if (lawOn) { L.done = 1; return 0; }
    const unsigned dt = L.last ? now - L.last : 0u;
    L.last = now ? now : 1u;
    if (menu) return 0;
    if (dt <= (unsigned)FG_SHIP_LONG_GAP_MS) L.playMs += dt;
    const bool heavyRow = (row == FGC_SLOW && !FgCondFixedUnderFloor(in, k) && !FgShipFloorUnreachable(in, k)) || row == FGC_ONSLOW;
    L.win++;
    if (row == FGC_FAST) L.fast++;
    else if (heavyRow) L.heavy++;
    if (L.playMs < (unsigned)FG_SHIP_LONG_MS || L.win < FG_SHIP_LONG_MIN_WIN) return 0;
    if (row == FGC_FAST && (long long)L.fast * 1000 >= (long long)L.win * FG_SHIP_LONG_SHARE_PM) { L.done = 1; return FGC_FAST; }
    if (heavyRow && (long long)L.heavy * 1000 >= (long long)L.win * FG_SHIP_LONG_SHARE_PM) { L.done = 1; return row; }
    return 0;
}
// The line for that row with the session's numbers. Returns the length written, 0 for any other row.
static inline int FgShipLongLine(int row, const FgCondIn& in, const FgCondK& k, char* out, size_t cap)
{
    if (!cap) return 0;
    out[0] = 0;
    const int mins = FG_SHIP_LONG_MS / 60000, floorX10 = FgCondFloorX10(in, k);
    int n = 0;
    if (row == FGC_FAST)
        n = snprintf(out, cap, "frame generation: it did not switch on in the first %d minutes of play: the game runs at about %d fps by itself, and frame generation only"
                               " switches on under about %d fps on this display, where it helps. It switches on by itself when the game runs slower",
                     mins, FgCondFastRateX10(in) / 10, FgCondTopX10(in, k) / 10);
    else if (row == FGC_SLOW || row == FGC_ONSLOW)
    {
                // The rate frame generation would run at. fg_min_fps=30 is suggested only where it would let that rate in.
        const int with = row == FGC_ONSLOW && in.predFpsX10 > 0 ? in.predFpsX10 : in.realFpsX10;
        const bool key = !FgCondFixedMode(in) && FgCondMinX10(in, k) > FG_COND_FLOOR_60HZ_X10 && with >= FG_COND_FLOOR_60HZ_X10;
        char w[96] = "";
        if (row == FGC_ONSLOW) snprintf(w, sizeof(w), ", and about %d with frame generation's own cost", with / 10);
        n = snprintf(out, cap, "frame generation: it did not switch on in the first %d minutes of play: the game runs at about %d fps by itself%s, under the %d fps"
                               " frame generation needs on this display. Lower graphics settings in the game can let it switch on%s",
                     mins, in.realFpsX10 / 10, w, floorX10 / 10, key ? ", and so does fg_min_fps=30 in DLSS-IV.cfg" : "");
    }
    else return 0;
    if (n < 0 || (size_t)n >= cap) { out[cap - 1] = 0; n = (int)cap - 1; }
    return n;
}
