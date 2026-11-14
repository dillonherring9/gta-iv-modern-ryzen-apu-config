// The fixed mode's window line as the shipped build says it: only with news. The development build says it every window.
// News is the mode's first window, a stall, a hold for the game, the overrun (the law's L over two refreshes,
// or the share over the mode's line), or the share above FG_COND_FIX_SHARE_PM; then one calm line after it,
// a summary once a minute, and the last window at exit (FgpFixSayExit). The words up to the share clause match
// the development line (tools read both); a tail says why it was said.
// Pure, no Windows header (tools\fg-fixsay-replay runs it on real logs). On the waiter alone, never at a present.
#pragma once

// One window as the waiter copied it (FgpFixLinesT's window fields).
struct FgFixSayW { long win, perUs, ppm, medUs, lastUs, wins, held, stalls, lUs, over, sharePm, shareN, shareRun; };
// The waiter's alone. seen 0: the mode's next window is its first. The range covers the windows since the last line.
struct FgFixSaySt { long seen, prevHeld, prevStalls, news, n, from, medMin, medMax, ppmMin, ppmMax; unsigned long long saidMs; };
enum { FG_FIXSAY_NONE = 0, FG_FIXSAY_FIRST, FG_FIXSAY_STALL, FG_FIXSAY_HELD, FG_FIXSAY_OVER, FG_FIXSAY_SHARE, FG_FIXSAY_CALM, FG_FIXSAY_MINUTE,
       FG_FIXSAY_END };
enum { FG_FIXSAY_MINUTE_MS = 60000 };

// The mode's line said it is on: its next window is said, and the range starts there.
static inline void FgFixSayModeOn(FgFixSaySt& s) { s.seen = 0; s.news = 0; s.n = 0; }

// Whether this window's line is said (FG_FIXSAY_*, NONE when not). nowMs: any millisecond clock. A said line ends the range:
// call FgFixSaySaid after it.
static inline int FgFixSayStep(FgFixSaySt& s, const FgFixSayW& w, unsigned long long nowMs, long shareLinePm)
{
    if (!s.n) { s.from = w.win; s.medMin = s.medMax = w.medUs; s.ppmMin = s.ppmMax = w.ppm; }
    else
    {
        if (w.medUs < s.medMin) s.medMin = w.medUs;
        if (w.medUs > s.medMax) s.medMax = w.medUs;
        if (w.ppm < s.ppmMin) s.ppmMin = w.ppm;
        if (w.ppm > s.ppmMax) s.ppmMax = w.ppm;
    }
    s.n++;
    int why = FG_FIXSAY_NONE;
    if (!s.seen) why = FG_FIXSAY_FIRST;
    else if (w.stalls != s.prevStalls) why = FG_FIXSAY_STALL;
    else if (w.held != s.prevHeld) why = FG_FIXSAY_HELD;
    else if (w.over > 0 || w.shareRun > 0) why = FG_FIXSAY_OVER;
    else if (w.sharePm > shareLinePm) why = FG_FIXSAY_SHARE;
    else if (s.news) why = FG_FIXSAY_CALM;
    else if (nowMs - s.saidMs >= (unsigned long long)FG_FIXSAY_MINUTE_MS) why = FG_FIXSAY_MINUTE;
    s.seen = 1; s.prevHeld = w.held; s.prevStalls = w.stalls;
    s.news = (why >= FG_FIXSAY_STALL && why <= FG_FIXSAY_SHARE) ? 1 : 0;
    if (why) s.saidMs = nowMs;
    return why;
}
static inline void FgFixSaySaid(FgFixSaySt& s) { s.n = 0; }
// At exit: FG_FIXSAY_END where windows stand since the last line.
static inline int FgFixSayEnd(const FgFixSaySt& s) { return (s.seen && s.n > 0) ? FG_FIXSAY_END : FG_FIXSAY_NONE; }

// Returns the length, or 0 where it did not fit.
static inline int FgFixSayLine(char* b, size_t cap, const FgFixSayW& w, int why, const FgFixSaySt& s, int targetUs, int shareLinePm)
{
    static const char* const kWhy[] = { "", "the mode's first window", "a stall", "a hold for the game", "the overrun", "the share over the mode's line",
                                        "the first window without news", "the minute's summary", "the session's end" };
    const int n = _snprintf_s(b, cap, _TRUNCATE,
        "  frame generation: fixed refresh: window %ld: the period %ld us (the correction %+ld ppm), the block a present median %ld us (target %d), the last %ld us, windows corrected %ld, held for the game %ld, stalls %ld; the law's L %ld us (over two refreshes %ld window(s) running); the share over two refreshes %ld of 1000 of %ld sample(s) of the last 12 s (over %d of 1000 %ld window(s) running); said for %s, the line of %ld window(s) from window %ld: the block a present median %ld to %ld us, the correction %+ld to %+ld ppm",
        w.win, w.perUs, w.ppm, w.medUs, targetUs, w.lastUs, w.wins, w.held, w.stalls, w.lUs, w.over,
        w.sharePm, w.shareN, shareLinePm, w.shareRun,
        (why > 0 && why <= FG_FIXSAY_END) ? kWhy[why] : "?", s.n, s.from, s.medMin, s.medMax, s.ppmMin, s.ppmMax);
    return n > 0 ? n : 0;
}
