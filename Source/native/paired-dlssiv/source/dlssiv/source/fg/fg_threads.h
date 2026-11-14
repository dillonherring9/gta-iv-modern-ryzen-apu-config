// fg_threads.h: frame generation's thread loops, templates over the machine's binding B and a small binding T for what a
// thread waits on, so the product (FgPA) and the harness (tools/perfproof/dt_fg_threads.h) run the same lines.
// T's seams: Quit, PlacerWait (event, own timer or quit; no time-out), PlacerTimer (the placer's own timer: two waits on
// one timer cancel each other), PlacerRan, RunWait, GoneWait (a fence at the top: helper gone), StampFrom, StamperUp,
// WakePlacer. No D3D9, DXVK or Vulkan call here beyond B::TlValue and B::TlWait.
#pragma once

// The placer's thread: one pass of the machine after any wake (PlacerRun), then the timer. A binding that cannot arm times
// nothing: PlacerRun answers 0 and the timer is never set.
template <class B, class T> static DWORD FgThreadPlacer()
{
    for (;;)
    {
        if (T::Quit()) return 0;
        T::PlacerWait();
        if (T::Quit()) return 0;
        const LONGLONG next = FgMachine<B>::PlacerRun();
        T::PlacerRan();
        if (next) T::PlacerTimer(next);
    }
}

// Which stamps wake the placer: the handoff's (the slot hangs on it) and the answer's (it completes a pair's K and a
// verdict waiting inside a late allowance). No placer decision reads the prepare's or the frame end's.
static __forceinline bool FgThreadStampWakes(int tl) { return tl == FG_TL_IN || tl == FG_TL_OUT; }

// A stamper's thread: one timeline. It waits with no time-out while nothing is engaged. A fence at the top is a helper
// that is gone (its clean exit signals the top; the kernel leaves a dead helper's fences there), where a wait returns at
// once and the step would spin until the stop: it idles instead, in 250 ms slices.
// Not asserted for the wire: tools/test-fg-split.py's third control must fail on the stub's own statement, which an
// assert here would stop the compiler before.
template <class B, class T> static DWORD FgThreadStamper(int tl)
{
    LONG last = T::StampFrom(tl);
    T::StamperUp(tl);
    for (;;)
    {
        T::RunWait();
        if (T::Quit()) return 0;
        if (B::TlValue(tl) >= 0x7FFFFFFFull) { T::GoneWait(); continue; }
        if (FgMachine<B>::StampStep(tl, &last, 250) && FgThreadStampWakes(tl)) T::WakePlacer();
    }
}

// The stampers' run condition across the two edges. The driver that writes the machine's want asks this once a window,
// with the want it is about to write, before writing it, and sets or resets the run event by the answer:
// towards engaged: the stampers run from now, so they are up before the first hand;
// towards standby: they keep running. The last engaged pair is still armed when the want goes to PASS: its handoff's stamp
// times it and its frame end needs the frame end's stamper. They rest one whole window of standby later, with no pair open.
// passWindows: the caller's count of windows the want has been PASS. True: set the run event; false: it may be reset.
template <class B> static bool FgThreadsRun(LONG* passWindows, LONG wantNext)
{
    if (wantNext != FG_WANT_PASS) { *passWindows = 0; return true; }
    if (*passWindows < 0x7FFFFFFF) ++*passWindows;
    return *passWindows < 2 || InterlockedCompareExchange(&B::S().sh.pairOpen, 0, 0) != 0;
}
