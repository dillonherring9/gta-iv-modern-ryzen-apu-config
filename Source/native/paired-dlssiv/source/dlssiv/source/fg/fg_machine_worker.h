// fg_machine_worker.h: FgMachine's worker (FG_HAND, FG_HUDLESS, the present, FG_FIRE and the injection) and the fire routine.
// Included in place inside FgMachine's body by fg_machine.h: the text is part of that class.

        // ---- the worker: FG_HAND ----
    static void NoteHand(DWORD kind, const DWORD* payload, DWORD bytes)
    {
        (void)kind;
        FgState& s = S(); FgWorkerLine& w = s.w;
        FgHandPayload h = {};
        memcpy(&h, payload, bytes < sizeof(h) ? bytes : sizeof(h));              // out of the ring: it is freed after this
        const DWORD own = (DWORD)((payload - 2) - dtRing);
        if constexpr (B::kWire) { (void)w; const LONGLONG wt0 = B::WorkT(); ShadowHand(h, own); B::WorkAdd(wt0); }
        else
        {
                // Close anything still open (a refused drain note, a present that never matched: a fault). A pair found armed is fired at
        // "hand open", before this hand's copy into FG_PRESENT, where that fire reads N from.
        if (w.openPhase == FG_PAIR_ARMED) { DEVSTAT(s.c.handOpen++;) Fire(FG_PT_HANDOPEN, own); }
        if (w.openPhase == FG_PAIR_ARMED)       // a dead rig fires nothing: the open N is dropped and counted
        { ResolveI(FG_RES_WITHHELD, FG_W_CLOSING, 0); ResolveN(FG_RES_DROPPED, FG_PT_CLOSING, 0); s.c.nDropped++; ClosePair(); }
        else if (w.openPhase == FG_PAIR_OPEN) { PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_UNARMED, nullptr, 0, w.openSent); ClosePair(); }
        const LONG st = s.sh.state;
        if (st != FG_LIVE || h.gen != dtArmCount || s.sh.armQuiet)
        {
            PubRecord(h.p, FG_PUB_CLOSED, st == FG_LIVE ? FG_CL_STALE : FG_CL_DEAD, &h, 0, 0);
            return;
        }
                // Open pair p.
        w.openPhase = FG_PAIR_OPEN; w.openP = h.p; w.openGen = h.gen; DEVSTAT(w.openK = h.k;) w.openFEnd = h.fEnd;
        w.openWithhold = h.withhold; w.openFlags = h.flags; w.openHold = h.hold;
        w.openPeriodUs = h.periodUs; w.openTEntry = h.tEntry;
        w.openHudlessSeen = false; w.openQ = w.openG = false; w.iResolved = w.nResolved = false;
        _InterlockedExchange(&s.sh.pairOpen, 1);
                // The injection fact, "sent, not posted": ivtSentN reached this very frame's injection.
        const LONG sn = B::SentN();
        w.openSent = (sn > w.lastSentN && B::LatOrd(sn) == h.fEnd) ? 1 : 0;
        w.lastSentN = sn;
                // Not blocking: pair p-1's answer must be in, else NOSOURCE (presented inline: FG_PRESENT is N's only store once FG_HAND ran).
        bool nosource = false; int nsWhy = FG_NS_UNANSWERED;
        if (h.p > 1)                                              // pair 1 is a session's first (FgStateAttach): nothing before it
        {
            FgAns a = {};
            if (!Read(s.ans[(h.p - 1) & (FG_RING - 1)], h.p - 1, &a)) nosource = true;
        }
        w.openNosource = nosource;
        if (!nosource)
        {
            if (!SpanHand(own, h)) { w.openNosource = true; nsWhy = FG_NS_NOBUFFER; }   // no command buffer: the pair goes inline
        }
        DEVSTAT(if (w.openNosource) s.c.nosourceBy[nsWhy]++;)
                // One 'Q' per sent pair, answered by exactly one 'G'.
        if (w.openSent) { B::WriteQ(h.p); w.openQ = true; DEVSTAT(s.c.qWritten++;) }
        w.handFor = h.k; w.handGen = h.gen;
        }
    }
        // Where the worker is, from inside a note: the index, with the dword count as the worker last published it. Its one reader
    // then sees the worker a little behind: it can only wait longer, never pass early.
    static __forceinline void WorkerAt(DWORD own) { DtWorkerDone(own, (DWORD)dtDoneDw); }
        // FG_HAND's locked span: command buffers before the lock, fireBusy, publish the position, the lock, the flush, a fresh
    // back-buffer query, the raw copy under the submission lock, no D3D9 call while it is held.
    static bool SpanHand(DWORD own, const FgHandPayload& h)
    {
        FgState& s = S();
        if (!B::CbTake(1)) { DEVSTAT(s.w.slotWaits++;) return false; }
        _InterlockedExchange(&s.sh.fireBusy, 1);
        WorkerAt(own);
        Lock();
        B::Flush();
        const ULONGLONG bb = B::BBImage();
        B::QLock();
        B::RecBegin();
        B::RecCopyToImg(bb, FG_IMG_PRESENT);
                // On the product's wire the submit's result counts: a copy not submitted is no source. The stub drops it.
        bool took = true;
        if constexpr (!B::kWire) B::RecSubmit(); else took = B::RecSubmit() && bb != 0;   // a back buffer that could not be had is no source
        B::QUnlock();
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
        return took;
    }

        // ---- the worker: FG_HUDLESS ----
    static void NoteHudless(DWORD kind, const DWORD* payload, DWORD bytes)
    {
        (void)kind;
        FgState& s = S(); FgWorkerLine& w = s.w;
        FgPairPayload pp = {};
        memcpy(&pp, payload, bytes < sizeof(pp) ? bytes : sizeof(pp));
        const DWORD own = (DWORD)((payload - 2) - dtRing);
        if constexpr (B::kWire) { const LONGLONG wt0 = B::WorkT(); ShadowHudless(pp, own); B::WorkAdd(wt0); }
        else
        {
        if (w.openPhase != FG_PAIR_OPEN || pp.p != w.openP || s.sh.state != FG_LIVE || s.sh.armQuiet) { DEVSTAT(s.c.noteRefused++;) return; }
        if (!w.openNosource)
        {
            if (!SpanHudless(own)) { w.openNosource = true; DEVSTAT(s.c.nosourceBy[FG_NS_NOBUFFER]++;) }   // counted like the hand's
        }
        if (w.openQ && !w.openG) { B::WriteG(w.openP, w.openNosource ? 2u : 0u); w.openG = true; DEVSTAT(s.c.gWritten++;) }
        w.openHudlessSeen = true;
        }
    }
    static bool SpanHudless(DWORD own)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if (!B::CbTake(2)) { DEVSTAT(w.slotWaits++;) return false; }      // the restore is raw, in a second buffer
        _InterlockedExchange(&s.sh.fireBusy, 1);
        WorkerAt(own);
        Lock();
        B::Flush();
        const ULONGLONG bb = B::BBImage();
        B::QLock();
        B::RecBegin();
        B::RecCopyToImg(bb, FG_IMG_HUDLESS);
        B::RecSignal(FG_TL_IN, InValue(w));                       // tS: the GPU retire of N's handoff
                // The result of this submit is the one that matters: it carries the fgIn signal the helper waits on.
        bool took = true;
        if constexpr (!B::kWire) B::RecSubmit(); else took = B::RecSubmit() && bb != 0;   // a back buffer that could not be had is no source
                // The restore, in the next submit.
        B::RecBegin();
        B::RecCopyFromImg(FG_IMG_PRESENT, bb);
        B::RecSubmit();
        B::QUnlock();
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
        return took;
    }

        // ---- the worker: the present ----
    static HRESULT __stdcall WorkerPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        typedef HRESULT(__stdcall* Dxvk_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        const Dxvk_t dxvk = (Dxvk_t)s.ptr.dxvkPresent;
        const LONG k = dtPresDone + 1;
                // Merged path, first: a back buffer holding a HUD-less picture nothing could take back is not presented: a frame dropped
        // and counted, never the interface gone on the screen.
        if constexpr (B::kWire) { if (FG_ARMED_GUARD_ON(3) && OnWorker() && ArmedHudDropped(k)) return S_OK; }
                // Stray callers pass through: only the worker, for the ordinal its own FG_HAND armed, in this generation.
        if (!OnWorker() || w.handFor != k || w.handGen != dtArmCount || w.openPhase != FG_PAIR_OPEN)
        {
            DEVSTAT(s.c.presentsPass++;)
            // The gate: this thread, no lock held, before DXVK's Present. DtExec times this whole function into dtPresTicksSum,
            // so a held present reads as a long one there.
            if constexpr (B::kWire) { if (OnWorker()) ArmedOrdBeforePresent(); }   // no present behind a standing order wait
            if (FG_PRESENT_GATE_ON(0)) B::PresentGate();
            const HRESULT hr = dxvk(sc, src, dst, wnd, dirty, flags);
            if constexpr (B::kWire) { if (OnWorker()) B::A().w.blitOut = 1; }   // its blit may still stand in DXVK's stream; ArmedBeforeInject asks
            return hr;
        }
        w.handFor = -1;
                // Copy the present out of the ring: the rects point into it and are freed after this command.
        w.sc = sc; w.hasSrc = src != nullptr; w.hasDst = dst != nullptr;
        if (src) w.src = *src;
        if (dst) w.dst = *dst;
        w.wnd = wnd; w.pflags = flags;
        w.tArm = B::Now();
        w.lastOwnedK = k;
                // The merged path decides by the pair, never by the rig's mode key: ArmedPresent.
        if constexpr (B::kWire) return ArmedPresent(sc, src, dst, wnd, dirty, flags, k);
        else
        {
        int why = -1;
        const LONG st = s.sh.state;
        if (w.openNosource) why = FG_IN_NOSOURCE;
        else if (!w.openHudlessSeen) why = FG_IN_NOHUDLESS;
        else if (st == FG_CLOSING) why = FG_IN_CLOSING;
        else if (st != FG_LIVE || s.sh.armQuiet) why = FG_IN_CLOSING;

        if (why >= 0)
        {
            // Inline: nothing was written into the back buffer after N was final: no save, no restore.
            if (FG_PRESENT_GATE_ON(1)) B::PresentGate();          // the gate: no lock held, ahead of DXVK's Present
            const HRESULT hr = dxvk(sc, src, dst, wnd, dirty, flags);
            DEVSTAT(s.c.presentsInline++;) s.c.presentsDxvk++;
            B::LatEnd(w.openFEnd);
            if constexpr (B::kWire)
            {   // on the wire the publication carries the pair's number and what its 'G' said
                FgHandPayload hp = {}; hp.gen = w.openGen; hp.k = k; hp.fEnd = w.openFEnd; hp.hold = (LONG)B::X().w.openKind;
                hp.tEntry = w.openTEntry;                         // the frame's entry, for "entry to handoff"
                PubRecord(w.openP, FG_PUB_INLINE, why, &hp, w.tArm, w.openG ? B::X().w.openN : 0);
            }
            else PubRecord(w.openP, FG_PUB_INLINE, why, nullptr, w.tArm, w.openSent);
            ResolveI(FG_RES_WITHHELD, FG_W_NOSOURCE, 0);
            ResolveN(FG_RES_FIRED, FG_PT_INLINE, w.tArm);
            ClosePair();
            return hr;
        }
                // Arm: publish, install the checkpoints, wake the placer, return at once (the render thread's next DT_R_PACE waits for
        // exactly this return). The hand's period and entry go with it, else the placer times from a stale period.
        FgHandPayload h = {};
        h.k = k; h.gen = w.openGen; h.p = w.openP; h.fEnd = w.openFEnd; h.withhold = w.openWithhold; h.hold = w.openHold; h.flags = w.openFlags;
        h.periodUs = w.openPeriodUs; h.tEntry = w.openTEntry;
        w.openPhase = FG_PAIR_ARMED;
        w.ckPair = 0;
        PubRecord(w.openP, FG_PUB_ARMED, 0, &h, w.tArm, w.openSent);
        CheckpointsInstall();
        return S_OK;
        }
    }
        // For an ordinal the rig owns, LatPresentObs is not called: it would signal the frame end at the arm, and LatEndSignal's
    // guard would then drop N's true signal. N's end is LatEnd.
    static void PresentObs(LONG k)
    {
        FgState& s = S();
        if (k == s.w.lastOwnedK) return;
        if (s.ptr.prevPresent) s.ptr.prevPresent(k);
    }

        // ---- the worker: FG_FIRE and the injection ----
    static void NoteFire(DWORD kind, const DWORD* payload, DWORD bytes)
    {
        (void)kind;
        FgState& s = S();
        FgFirePayload fp = {};
        memcpy(&fp, payload, bytes < sizeof(fp) ? bytes : sizeof(fp));
        const DWORD own = (DWORD)((payload - 2) - dtRing);
        if constexpr (B::kWire) { (void)s; ArmedFireNote(fp, own); }   // merged path: a drain closes too, and the kind carries its close note
        else
        {
        if (s.w.openPhase != FG_PAIR_ARMED) return;               // drain with nothing open: a counted no-op
        Fire(fp.why == FG_PT_D ? FG_PT_D : FG_PT_B, own);
        }
    }
        // The injection's observer, chained ahead of IvtInjectNoteW. It must call the saved observer exactly once on every path,
    // a dead rig included: the injection's scope guard's decrement is the whole contract, and a skipped call hangs every seam.
    static void InjectObs(DWORD kind, const DWORD* payload, DWORD bytes)
    {
        FgState& s = S();
                // By shape: the rig counts the early rule, then fires. The merged path's order is the point (ArmedBeforeInject): the
        // pending real frame first, then a flush, then the guard's wait.
        if constexpr (B::kWire) ArmedBeforeInject(payload, bytes);
        if constexpr (!B::kWire)
        {
        if (s.w.openPhase == FG_PAIR_ARMED && (s.sh.state == FG_LIVE || s.sh.state == FG_CLOSING))
            Fire(FG_PT_C, (DWORD)((payload - 2) - dtRing));
        }
        if (s.ptr.prevInject) s.ptr.prevInject(kind, payload, bytes);
                // 'Q' right behind 'F', behind the saved observer: off the real frame's path.
        if constexpr (kEarlyQ) { const LONGLONG wt1 = B::WorkT(); ShadowAfterInject(); B::WorkAdd(wt1); }
                // The ride: behind the body and the early 'Q', the real frame of a ride pair the injection met armed.
        if constexpr (B::kWire) ArmedAfterInject(payload);
    }

        // ---- the fire routine ----
    static bool DueFor(FgDue* ring, FgDue* out, LONG* staleSeen)
    {
        FgWorkerLine& w = S().w;
        const LONG a = ReadAcquire(&ring->seq);
        if (a < 0) return false;
        FgDue d = {};
        memcpy(&d, (const void*)ring, sizeof(d));
        const LONG b = ReadAcquire(&ring->seq);
        if (a != b) return false;
        if (d.p != w.openP || d.gen != w.openGen)
        {
            if (d.p < w.openP && *staleSeen != d.p) { *staleSeen = d.p; DEVSTAT(S().c.staleDues++;) }   // counted once a due
            return false;
        }
        *out = d; return true;
    }
    static void Fire(int point, DWORD own)
    {
                // The merged path has its own fire; the checkpoints call this one name for both.
        if constexpr (B::kWire) ArmedFire(point, own, false);
        else
        {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if (w.openPhase != FG_PAIR_ARMED) return;
        const LONG st = s.sh.state;
        if (st != FG_LIVE && st != FG_CLOSING) return;
        const LONGLONG reached = B::Now();
        FgDue di = {}, dn = {};
        const bool hasI = DueFor(s.dueI, &di, &w.staleI);
        const bool hasN = DueFor(s.dueN, &dn, &w.staleN);
        // Points that force N now: a drain, a hand that found the pair open, CLOSING, and the next injection under rule C1.
        const bool c1 = point == FG_PT_C && FG_C1_ON();
        const bool forceN = point == FG_PT_D || point == FG_PT_HANDOPEN || point == FG_PT_CLOSING || st == FG_CLOSING || c1;
                // 1. I first.
        if (!w.iResolved)
        {
            if (hasI && di.verdict == FG_DUE_SHOW && !forceN) FireI(point, own, reached);
            else if (hasI && di.verdict == FG_DUE_WITHHOLD) ResolveI(FG_RES_WITHHELD, di.why, 0);
            else if (forceN)
            {
                // Closing first: in CLOSING the first point of any kind that finds an armed pair fires its N.
                const DWORD why = (point == FG_PT_CLOSING || st == FG_CLOSING) ? FG_W_CLOSING : point == FG_PT_D ? FG_W_DRAIN
                                : point == FG_PT_HANDOPEN ? FG_W_HANDOPEN : FG_W_EARLY;
                ResolveI(FG_RES_WITHHELD, why, 0);
            }
        }
                // 2. N second.
        if (!w.nResolved && (forceN || hasN))
        {
            if (!w.iResolved) ResolveI(FG_RES_WITHHELD, FG_W_NFIRST, 0);
            s.c.points[point]++;
            // Counted by when it fired only if it fired (an N with no command buffer is dropped, counted once by FireN).
            if (FireN(point, own, reached))
            {
                DEVSTAT(if (point == FG_PT_CLOSING || st == FG_CLOSING) s.c.nAtStop++;)     // closing first, as the I's reason above
                DEVSTAT(else if (point == FG_PT_C) s.c.nEarly++;)
                DEVSTAT(else if (point == FG_PT_D || point == FG_PT_HANDOPEN) s.c.nLate++;)
                DEVSTAT(else s.c.nOnTime++;)
            }
        }
        }
    }
        // One save-present-restore, for I or N, both from FG_PRESENT (the stub has no generated image: its I is N's copy). False
    // when no command buffer retired within 2 s: nothing is done, before any lock, and the caller withholds I or drops N.
    // Flush; BB -> SAVE and FG_PRESENT -> BB (raw); Present; [no rotation: flush]; marker; SAVE -> BB (raw).
    static bool FirePresent(bool isI, int point, DWORD own, LONGLONG reached)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        typedef HRESULT(__stdcall* Dxvk_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        const Dxvk_t dxvk = (Dxvk_t)s.ptr.dxvkPresent;
        const DWORD pflags = w.pflags;
        DEVSTAT(w.firePoint = point;) w.fireKind = isI ? 0 : 1;
                // The reservation covers the span: I: copy, marker, restore; N: copy, restore (its marker is LatEndSignal's own).
        if (!B::CbTake(isI ? 3 : 2)) { DEVSTAT(w.slotWaits++;) return false; }
        _InterlockedExchange(&s.sh.fireBusy, 1);
        WorkerAt(own);
        Lock();
        B::Flush();
        const ULONGLONG bbNow = B::BBImage();                     // fresh, never cached
        B::QLock();
        B::RecBegin();
        B::RecCopyToImg(bbNow, FG_IMG_SAVE);                      // the always-save (no dirty test)
        B::RecCopyFromImg(FG_IMG_PRESENT, bbNow);
        B::RecSubmit();
        B::QUnlock();
        const double wSvc = TicksToMs(B::Now() - reached);
                // No B::PresentGate here: the rig's fire path has none; the merged path's fires take the gate outside their lock.
        const LONGLONG t = B::Now();
        const LONGLONG pt0 = dtPresTicksSum;                     // only the worker writes the sum: no lock needed to read it
        const HRESULT hr = dxvk(w.sc, w.hasSrc ? &w.src : nullptr, w.hasDst ? &w.dst : nullptr, w.wnd, nullptr, pflags);
        const LONGLONG presentT = B::Now() - t;
        const LONGLONG pt1 = InterlockedExchangeAdd64(&dtPresTicksSum, presentT) + presentT;   // a fire's present is outside DtExec's timing
        if (!s.sh.presLastGen) { InterlockedExchangeAdd64(&s.sh.presTicksR, pt1 - pt0); s.sh.presRN++; }   // a present behind a real frame, for PaceFixed
        s.sh.presLastGen = isI ? 1 : 0;
        if (FAILED(hr)) dtPresHr = (LONG)hr;                     // the game hears a lost device from a fire too
        s.c.presentsDxvk++; if (isI) s.c.presentsI++; else s.c.presentsN++;
        const ULONGLONG bbAfter = B::BBImage();
                // One image: the present's blit is still in DXVK's stream and reads the image the raw restore is about to overwrite, so
        // the stream is flushed first.
        if (bbAfter == bbNow) B::Flush();
        if (isI) { B::QLock(); B::RecBegin(); B::RecSignal(FG_TL_MARKI, w.openP); B::RecSubmit(); B::QUnlock(); }
        else B::LatEnd(w.openFEnd);
                // The restore into the image current after the Present.
        B::QLock(); B::RecBegin(); B::RecCopyFromImg(FG_IMG_SAVE, bbAfter); B::RecSubmit(); B::QUnlock();
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
        // w_I and w_N, EWMA 0.10 of the service time (the point reached to the present call), clamped 0 to 2 ms.
        double& ew = isI ? w.wI : w.wN;
        ew += (wSvc - ew) * 0.10; if (ew < 0.0) ew = 0.0; if (ew > 2.0) ew = 2.0;
        if (isI) w.wIUs = (LONG)(ew * 1000.0); else w.wNUs = (LONG)(ew * 1000.0);
        return true;
    }
    static void FireI(int point, DWORD own, LONGLONG reached)
    {
        // a refused command buffer is the ring's, not lateness
        if (!FirePresent(true, point, own, reached)) { ResolveI(FG_RES_WITHHELD, FG_W_NOBUFFER, 0); return; }
        ResolveI(FG_RES_SHOWN, 0, reached);
        S().c.points[point]++;
    }
    static bool FireN(int point, DWORD own, LONGLONG reached)   // false: dropped
    {
        FgState& s = S();
                // A real frame with no command buffer is dropped and counted: after FG_HUDLESS the game may be drawing the next frame into
        // the back buffer, so presenting it would show a half-drawn frame.
        if (!FirePresent(false, point, own, reached)) { ResolveN(FG_RES_DROPPED, point, 0); s.c.nDropped++; ClosePair(); return false; }
        ResolveN(FG_RES_FIRED, point, reached);
        ClosePair();
        return true;
    }
