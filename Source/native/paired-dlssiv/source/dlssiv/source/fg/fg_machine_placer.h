// fg_machine_placer.h: FgMachine's arm and disarm, the placer, the stampers, the waiter, the pure pieces the harness proves
// and the present end's map. Included in place inside FgMachine's body by fg_machine.h: the text is part of that class.

        // ---- arm and disarm ----
    static void OnArm()
    {
        FgState& s = S();
        _InterlockedExchange(&s.sh.armQuiet, 0);
        if (s.ptr.prevArm) s.ptr.prevArm();
    }
        // First in the disarm tail, the ring drained: no present from a foreign thread, so an open pair's I and N are dropped,
    // counted, and the pair is published closed and answered.
    static void OnDisarm()
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        _InterlockedExchange(&s.sh.armQuiet, 1);
        if constexpr (B::kWire) ArmedStopped();     // before the wait below, as BeforeReset
        const LONGLONG t0 = B::Now();
        while (InterlockedCompareExchange(&s.sh.fireBusy, 0, 0) && B::Now() - t0 < B::Qpf()) Sleep(0);
        if (w.openPhase == FG_PAIR_ARMED)
        {
            if constexpr (B::kWire) ArmedDrop(FG_AD_DISARM);
            else
            {
            ResolveI(FG_RES_WITHHELD, FG_W_CLOSING, 0);
            ResolveN(FG_RES_DROPPED, FG_PT_CLOSING, 0);
            s.c.nDropped++;
            ClosePair();
            }
        }
        else if (w.openPhase == FG_PAIR_OPEN)
        {
            PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_DISARM, nullptr, 0, w.openSent);
            ClosePair();
        }
                // A 'Q' written at its injection whose hand has not come is closed with the layer: its 'G' is owed off the worker, as an
        // open pair's is; the first frame after the re-arm writes its 'Q' at its hand.
        if constexpr (B::kWire) { ShadowCloseEarly(); B::X().e.engaged = 0; }
        w.handFor = -1;
        if (s.ptr.prevDisarm) s.ptr.prevDisarm();
    }

        // ---- the placer ----
    static void Answer(LONG p, LONG val, LONG why)
    {
        FgState& s = S();
        FgAns a = {}; a.p = p; a.val = val; a.why = why;
        Publish(s.ans[p & (FG_RING - 1)], p, a);
        DEVSTAT(if (val == 2 * p) s.c.answers2p++; else s.c.answers2p1++;)
    }
    static void DueWrite(FgDue* ring, LONG p, LONG gen, LONG verdict, DWORD why, LONGLONG tDue, LONG eUs = 0)
    {
        FgDue d = {}; d.p = p; d.gen = gen; d.verdict = verdict; d.why = why; d.tDue = tDue; d.eUs = eUs;
        Publish(*ring, p, d);
    }
    static void CapLate()   { FgState& s = S(); s.pl.capClean = 0; if (InterlockedCompareExchange(&s.sh.padQms, 0, 0) < 16) InterlockedIncrement(&s.sh.padQms); DEVSTAT(s.c.lateCalls++;) }
    static void CapOnTime() { FgState& s = S(); if (++s.pl.capClean >= 60) { s.pl.capClean = 0; if (InterlockedCompareExchange(&s.sh.padQms, 0, 0) > 0) InterlockedDecrement(&s.sh.padQms); } DEVSTAT(s.c.onTimeCalls++;) }
    static void Retire(int i)
    {
        FgPlacerLine& pl = S().pl;
        for (int j = i + 1; j < pl.liveN; j++) pl.live[j - 1] = pl.live[j];
        pl.liveN--;
    }
    static LONGLONG PlacerStep(LONGLONG now)
    {
        if constexpr (B::kWire) return ArmedPlacerStep(now);
        else
        {
        FgState& s = S(); FgPlacerLine& pl = s.pl;
                // 1. Every new publication, in p order.
        const LONG top = ReadAcquire(&s.sh.pubP);
        while (pl.seen < top)
        {
            const LONG p = pl.seen + 1;
            FgPub v = {};
            if (!Read(s.pub[p & (FG_RING - 1)], p, &v))
            {
                // Overwritten, not incomplete: a placer eight publications behind finds a later pair in p's slot; waiting would wedge it
                // for the session. p is answered 2p+1, counted, and the placer goes on.
                if (ReadAcquire(&s.pub[p & (FG_RING - 1)].seq) > p) { pl.seen = p; DEVSTAT(s.c.pubLost++; s.c.answeredStale++;) Answer(p, 2 * p + 1, FG_CL_STALE); continue; }
                break;                                            // not complete yet
            }
            pl.seen = p;
            if (v.kind == FG_PUB_INLINE || v.kind == FG_PUB_CLOSED) { Answer(p, 2 * p + 1, v.why); continue; }
            // A newer generation retires every live pair of an older one.
            if (v.gen != pl.curGen)
            {
                for (int i = pl.liveN - 1; i >= 0; i--) if (pl.live[i].gen != v.gen) { if (!pl.live[i].answered) { DEVSTAT(s.c.answeredStale++;) Answer(pl.live[i].p, 2 * pl.live[i].p + 1, FG_CL_STALE); } Retire(i); }
                pl.curGen = v.gen;
            }
            // The oldest live pair makes room (every pair exactly one answer).
            if (pl.liveN >= 3) { DEVSTAT(s.c.liveOverflow++;) if (!pl.live[0].answered) { DEVSTAT(s.c.answeredStale++;) Answer(pl.live[0].p, 2 * pl.live[0].p + 1, FG_CL_STALE); } Retire(0); }
            FgLive& L = pl.live[pl.liveN++];
            memset(&L, 0, sizeof(L));
            L.p = p; L.gen = v.gen; L.sent = v.sent; L.hold = v.hold; L.warm = (v.flags & FG_HF_WARM) ? 1 : 0;
            L.periodUs = v.periodUs; L.hflags = v.flags;
            L.tEntry = v.tEntry;
            L.withhold = v.withhold | (v.sent ? 0u : FG_W_NOINJECT);
        }
                // 2. Each live pair, in p order.
        // next: the earliest instant this step still waits for; spinFor: that one is a raise, which PlacerRun meets exactly.
        LONGLONG next = 0; bool spinFor = false;
                // Two wants at one instant (an answer's arrival and I's raise): the raise's spin stands.
        auto want = [&next, &spinFor](LONGLONG t, bool spin = false)
        {
            if (t <= 0) return;
            if (next == 0 || t < next) { next = t; spinFor = spin; }
            else if (t == next && spin) spinFor = true;
        };
        const LONG wIUs = s.w.wIUs, wNUs = s.w.wNUs;
        const LONGLONG wI = UsToTicks(wIUs), wN = UsToTicks(wNUs);
        for (int i = 0; i < pl.liveN; i++)
        {
            FgLive& L = pl.live[i];
            // Retire on the worker's resolution, never on a stamp.
            FgRes r = {};
            if (Read(s.res[L.p & (FG_RING - 1)], L.p, &r) && r.nRes != FG_RES_OPEN)
            {
                // Closed before its arrival, by fact: N's fire start before the arrival is the pair's own; an arrival already passed when
                // the placer looked is the placer's lateness, counted apart.
                if (!L.answered)
                {
                    Answer(L.p, 2 * L.p + 1, FG_CL_UNARMED);
                    DEVSTAT(if (!L.timed || !r.nStart || r.nStart < L.arrival) s.c.closedBeforeArrival++; else s.c.answeredLate++;)
                }
                Retire(i); i--; continue;
            }
            if (!L.tS)
            {
                LONGLONG t = 0;
                if (!ReadStamp(s.st[FG_TL_IN], L.p, &t)) continue;   // the handoff's GPU retire is not in yet
                L.tS = t;
            }
            if (!L.timed)
            {
                L.timed = true;
                // Readiness: I at the modelled answer's arrival plus its service time.
                L.arrival = L.tS + UsToTicks(FG_STUB_GEN_US());
                L.tI = L.arrival + wI;
                // The period in force for this pair, as its own present published it (a live read here would be a move ahead).
                const double applied = (double)(L.periodUs > 0 ? L.periodUs : s.r.appliedUs) / 1000.0;
                const LONGLONG tReady = L.tI;
                const double h = HalfOf(applied);
                const LONG hUs = (LONG)(h * 1000.0);
                pl.hUs = hUs;
                L.tN = L.tI + (LONGLONG)(h * (double)B::Qpf() / 1000.0);
                // The plan, published before any due for this pair, so the worker finds it whenever a due exists.
                FgPlan pn = {}; pn.p = L.p; pn.gen = L.gen; pn.hUs = hUs; pn.holdUs = L.hold;
                pn.tS = L.tS; pn.arrival = L.arrival; pn.tI = L.tI; pn.tN = L.tN;
                pn.periodUs = (LONG)(applied * 1000.0);
                pn.tEntry = L.tEntry; pn.tReady = tReady; pn.wIUs = wIUs; pn.wNUs = wNUs;
                pn.kflags = L.hflags & (FG_HF_REG1 | FG_HF_REG2);
                Publish(s.plan[L.p & (FG_RING - 1)], L.p, pn);
            }
            const bool spin = true;                                   // raises are met exactly (PlacerRun)
                                                                      // The answer, once, at the arrival (every pair exactly one answer).
            if (!L.answered)
            {
                if (now >= L.arrival)
                {
                    L.answered = true;
                    const bool fact = (L.withhold & (FG_W_RENDER_FACTS | FG_W_NOINJECT)) != 0;
                    Answer(L.p, fact ? 2 * L.p + 1 : 2 * L.p, 0);
                    DEVSTAT(if (fact) s.c.answeredFact++;)                     // 2p+1 by the publication's fact
                }
                // An I raise within the slack after the arrival makes the arrival spin too, or the timer's wake lands late.
                else want(L.arrival, spin && !L.iRaised && (L.tI - wI) - L.arrival < UsToTicks(FG_SPIN_SLACK_US + FG_SPIN_LEAST_US));
            }
            // The I verdict, once, at tI - w_I.
            if (!L.iRaised)
            {
                if (now >= L.tI - wI)
                {
                    L.iRaised = true; L.tIRaised = now; L.atI = L.tI - wI;
                    const DWORD why = L.withhold & (FG_W_RENDER_FACTS | FG_W_NOINJECT);
                    // At readiness I is never late.
                    DueWrite(s.dueI, L.p, L.gen, why ? FG_DUE_WITHHOLD : FG_DUE_SHOW, why, L.tI);
                    if (!why) B::WakePace();
                }
                else want(L.tI - wI, spin);
            }
            // FIRE_N, once, at max(tN - w_N, the instant FIRE_I was raised): N never before I.
            if (!L.nRaised)
            {
                LONGLONG at = L.tN - wN;
                if (L.iRaised && at < L.tIRaised) at = L.tIRaised;
                if (L.iRaised && now >= at)
                {
                    L.nRaised = true;
                    DueWrite(s.dueN, L.p, L.gen, FG_DUE_FIRE, 0, L.tN);
                    B::WakePace();
                    // The pair's two raises, for the export's L row (after the due and the wake: nothing waits on it).
                    FgRaise rz = {}; rz.p = L.p; rz.atI = L.atI; rz.tIRaised = L.tIRaised; rz.atN = at; rz.tNRaised = now;
                    Publish(s.raise[L.p & (FG_RING - 1)], L.p, rz);
                }
                else want(L.iRaised ? at : L.tI - wI, spin);
            }
        }
        pl.nextSpin = next != 0 && spinFor;
        return next;
        }
    }
        // The exact raise: a timed raise on the bare timer is about 0.27 ms late, so the placer does as FgPace's sleep: the step,
    // then, while it waits for a raise within the slack plus FG_SPIN_LEAST_US, a bounded spin to it (a publication or an IN
    // stamp ends it) and the step again. Returns when to set the thread's timer: the slack before a raise to spin for, the
    // instant itself for anything else, 0 none.
    enum { FG_SPIN_SLACK_US = 600, FG_SPIN_LEAST_US = 300, FG_SPIN_PASSES = 8 };
    static LONGLONG PlacerRun()
    {
        FgPlacerLine& pl = S().pl;
        LONGLONG next = PlacerStep(B::Now());
        const LONGLONG slack = UsToTicks(FG_SPIN_SLACK_US), least = UsToTicks(FG_SPIN_LEAST_US);
        for (int pass = 0; pass < FG_SPIN_PASSES && next && pl.nextSpin; pass++)
        {
            const LONGLONG t0 = B::Now();
            if (next - slack - t0 > least) return next - slack;      // far enough for the timer, set short
            bool woke = false;
            for (int i = 0; i < 400000 && B::Now() < next; i++)
            {
                if ((i & 15) == 15 && B::PlacerPoll()) { woke = true; break; }
                YieldProcessor();
            }
            DEVSTAT(WriteRelease(&pl.spins, pl.spins + 1); if (woke) WriteRelease(&pl.spinWoke, pl.spinWoke + 1);)
            DEVSTAT(pl.spinTicks += B::Now() - t0;)
            next = PlacerStep(B::Now());
        }
        return (next && pl.nextSpin) ? next - slack : next;
    }

        // ---- the stampers ----
    // One step: read before waiting, so a value already there is stamped at once (counted late); the wait is for any value
    // above the last, so a value never signalled blocks nothing. False on a time-out.
    // __forceinline: without it this stays out of line.
    static __forceinline bool StampStep(int tl, LONG* last, DWORD sliceMs)
    {
        FgState& s = S();
        FgStamp* ring = s.st[tl];
        LONG c = (LONG)B::TlValue(tl);
        bool late = false;
        if (c <= *last)
        {
            if (!B::TlWait(tl, (ULONGLONG)(*last + 1), sliceMs)) return false;
            c = (LONG)B::TlValue(tl);
        }
        else late = true;
        const LONGLONG t = B::Now();
        DEVSTAT(if (tl == FG_TL_END && c > *last + 1 && *last >= 0) s.c.endSkipped++;)
        FgStamp& e = ring[c & (FG_STAMPS - 1)];
        WriteRelease(&e.v1, -1); e.t = t; e.late = late ? 1 : 0; WriteRelease(&e.v2, c); WriteRelease(&e.v1, c);
        DEVSTAT(s.c.stamped[tl]++; if (late) s.c.stampedLate[tl]++;)
        *last = c;
        if constexpr (B::kWire) { if (tl == FG_TL_OUT) ArmedOrdAtStamp(c, t); }   // the answer's stamper releases the order wait
        return true;
    }

        // ---- the waiter ----
    static bool OwnsEnd(LONG f)
    {
        const FgOwn& e = S().own[f & (FG_STAMPS - 1)];
        const LONG a = ReadAcquire(&e.f1); if (a != f) return false;
        const LONG owned = e.owned;
        const LONG b = ReadAcquire(&e.f2); return b == f && owned != 0;
    }
    static bool EndFor(LONG f, LONGLONG* t) { return ReadStamp(S().st[FG_TL_END], f, t); }
        // Deferred samples whose END stamp is back are taken (one not back by its fifth look is dropped); then, for a frame whose
    // end the rig owns, the END stamp of fe, or the sample is deferred. An end at or before its begin is dropped (beforeBegin).
    // Returns 1 when the rig took or deferred it, 0 when the waiter's own route stands.
    template <class Sink> static int Route(FgRoute& rt, LONG fe, LONG fp, bool valid, LONGLONG b, float p, float h, float q, float l, Sink& sink)
    {
        for (int i = 0; i < rt.pendN; )
        {
            FgPend& e = rt.pend[i];
            LONGLONG te = 0;
            const bool back = EndFor(e.fe, &te);
            if (back || ++e.iters > 4)
            {
                if (back && te > e.b) sink.Take(e, te);
                DEVSTAT(else if (back) rt.beforeBegin++;)
                DEVSTAT(else rt.notBack++;)
                rt.pend[i] = rt.pend[--rt.pendN];
                continue;
            }
            i++;
        }
        if (!valid || !OwnsEnd(fe)) return 0;
        LONGLONG te = 0;
        if (EndFor(fe, &te))
        {
            FgPend e = {}; e.fe = fe; e.fp = fp; e.b = b; e.p = p; e.h = h; e.q = q; e.l = l;
            if (te > b) sink.Take(e, te); DEVSTAT(else rt.beforeBegin++;)
            return 1;
        }
        if (rt.pendN < FG_PEND_N) { FgPend& e = rt.pend[rt.pendN++]; e = FgPend(); e.fe = fe; e.fp = fp; e.b = b; e.p = p; e.h = h; e.q = q; e.l = l; e.iters = 0; }
        DEVSTAT(else rt.notBack++;)
        return 1;
    }
