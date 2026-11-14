// FgMachine's product paths: the wire's hand, the merged path, the HUD-less picture, the placed frame, the switch-on, the ride,
// the helper's late answer, the order wait. Included in place in FgMachine's body by fg_machine.h.

        // ================================================================ the wire's hand, 'Q' and 'G'
    // What every binding on the product's wire runs for an engaged present (the Shadow names are historical). On the worker: FG_HAND
    // writes what a disarm left owed, opens the pair, checks the injection was sent (else nothing is written, copied or signalled)
    // and G5 (never blocking), writes 'Q' (or adopts the early one) and makes the hand's span (ArmedSpan); FG_HUDLESS writes 'G'
    // (ArmedG). Before an injection's body: one wait behind fgPrep (G4); after it: the early 'Q'.
    // The owed 'G': a pair closed off the pipe's writer threads (a disarm's tail) writes nothing there; the next writer context
    // writes its 'G'.
    static void ShadowCloseG()
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X();
        if (!w.openQ || w.openG) return;
        w.openG = true;
        x.w.gapLatch = 1;                                         // ended without a dispatch: the next 'Q' says so
        if (x.sh.stopped) { DEVSTAT(x.w.gUnsent++;) return; }
        if (OnWorker()) { B::WriteG(x.w.openN, FG_G_CLOSED); DEVSTAT(s.c.gWritten++; x.w.gByKind[FG_G_CLOSED]++;) x.w.openKind = FG_G_CLOSED; }
        else _InterlockedExchange(&x.sh.owedG, x.w.openN);
    }
        // On a writer context only: the worker inside a note, or the render thread with the ring drained.
    static void ShadowWriteOwed()
    {
        FgShadowState& x = B::X();
        const LONG n = _InterlockedExchange(&x.sh.owedG, 0);
        if (!n) return;
        if (x.sh.stopped) { DEVSTAT(x.w.gUnsent++;) return; }
        B::WriteG(n, FG_G_CLOSED); DEVSTAT(S().c.gWritten++; x.w.gByKind[FG_G_CLOSED]++; x.w.gOwedWritten++;)
    }
        // ---- 'Q' right after 'F'
    // The injection's observer writes 'Q' after the saved observer, so the helper records the prepare right after the frame message
    // and it retires before the hand's copy. 'Q' only if the last present was engaged; a 'Q' whose frame never reaches a hand is
    // closed CLOSED before the next, with the gap latch. No wait is added, moved or removed on any queue. writer: the caller is a
    // writer context; elsewhere the 'G' is owed.
    static void ShadowCloseEarly(bool writer = false)
    {
        FgShadowState& x = B::X();
        const LONG n = x.e.qN;
        if (!n) return;
        x.e.qN = 0;
        x.w.gapLatch = 1;
        DEVSTAT(x.e.noHand++;)
        if (x.sh.stopped) { DEVSTAT(x.w.gUnsent++;) return; }
        if (writer || OnWorker()) { B::WriteG(n, FG_G_CLOSED); DEVSTAT(S().c.gWritten++; x.w.gByKind[FG_G_CLOSED]++;) }
        else _InterlockedExchange(&x.sh.owedG, n);
    }
        // After the saved injection observer, on the worker. First every owed 'G'. Then 'Q'(n) only when this injection was sent, the
    // last present was engaged (an engage's first frame gets its 'Q' at its hand), and nothing says this present will not be. It
    // carries BUILD, DEVICE, GAP; the hand adds CUT and FIRST. Under B::QAtHand nothing here runs.
    static void ShadowAfterInject()
    {
        FgState& s = S(); FgShadowState& x = B::X();
        const LONG sn = B::SentN();
        const bool sent = sn > x.e.sentSeen;
        x.e.sentSeen = sn;
        if (B::QAtHand()) return;
        if (x.sh.owedG) ShadowWriteOwed();
        ShadowCloseG();
        ShadowCloseEarly();
        if (!sent || !x.e.engaged) return;
        if (x.sh.stopped || s.sh.state != FG_LIVE || s.sh.armQuiet || !WantsWire(ReadAcquire(&s.wt.want)) || !B::MayEngage() || x.sh.setDead == x.sh.bgen + 1) return;
        DWORD qf = 0;
        if (x.w.lastBgenQ != x.sh.bgen || !x.w.lastQN) qf |= FG_Q_BUILD;
        if (x.w.lastQN && dtArmCount != x.w.lastGenQ) qf |= FG_Q_DEVICE;
        if (!(qf & FG_Q_BUILD) && (x.w.gapLatch || sn != x.w.lastQN + 1)) qf |= FG_Q_GAP;   // a build is its own reason, not a gap too
        DEVSTAT(for (int b = 0; b < 4; b++) if (qf & (1u << b)) x.w.resetBy[b]++;)
        B::WriteQ(sn, qf); DEVSTAT(s.c.qWritten++;) x.e.written++;
        x.e.qN = sn; x.e.qReasons = (LONG)qf;
        x.w.lastQN = sn; x.w.lastBgenQ = x.sh.bgen; x.w.lastGenQ = dtArmCount; x.w.gapLatch = 0;
    }
        // The last dispatch's answer (out: fgOut as just read), counted once by its value: 2n or 2n+1, else the OUT stamps, else
    // "overtaken", never guessed. Three in a row with no frame end frame generation for the set, with one line.
    static void ShadowAnswerSeen(LONG n, ULONGLONG out)
    {
        FgState& s = S(); FgShadowState& x = B::X();
        const ULONGLONG n2 = 2ull * (ULONGLONG)n;
        int val = out == n2 ? 0 : out == n2 + 1 ? 1 : -1;
        if (val < 0) { LONGLONG t = 0; if (ReadStamp(s.st[FG_TL_OUT], (LONG)n2, &t)) val = 0; else if (ReadStamp(s.st[FG_TL_OUT], (LONG)n2 + 1, &t)) val = 1; }
                // mirrors the helper: a dispatch with no frame makes its next dispatch reset, with no reason on the wire
        if (val != 0) B::A().w.primeNext = 1;
        if (val == 0) { DEVSTAT(x.w.ansFrame++; s.c.answers2p++;) x.w.failRun = 0; }
        else if (val == 1)
        {
            DEVSTAT(x.w.ansNone++; s.c.answers2p1++;)
            if (++x.w.failRun >= 3 && x.sh.setDead != x.sh.bgen + 1)
            {
                _InterlockedExchange(&x.sh.setDead, x.sh.bgen + 1); DEVSTAT(x.w.standDowns++;)
                B::Line("  frame generation: three pairs in a row came back with no frame (the last was %ld): it ends for this set; the upscaler goes on", (long)n);
            }
        }
        DEVSTAT(else x.w.ansUnknown++;)
    }
    static void ShadowHand(const FgHandPayload& h, DWORD own)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X();
                // A pair still armed is brought to its real frame here, before this hand writes the next frame into HOLD (fired after, it would
        // show this frame under the earlier pair's name). DEAD drops it.
        {
            if (w.openPhase == FG_PAIR_ARMED)
            {
                DEVSTAT(s.c.handOpen++;)
                ArmedFire(FG_PT_HANDOPEN, own, false);
            }
            if (w.openPhase == FG_PAIR_ARMED) ArmedDrop(FG_AD_DEAD);
            // does a HUD-less picture follow this hand?
            FgArmedWorker& aw = B::A().w;
            aw.passK = (h.flags & FG_HF_HUDPASS) ? h.k : 0; aw.holdK = 0; DEVSTAT(aw.hudFed = 0;)
            DEVSTAT(if (aw.passK) aw.hudPass++;)
            // an engage's first hand: nothing asked yet
            if (h.flags & FG_HF_FIRST) { aw.cardAsked = 0; _InterlockedExchange(&aw.ordFirstP, 0); }
        }
                // close anything still open (a Present that never matched), then write what a disarm left owed
        if (w.openPhase == FG_PAIR_OPEN) { PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_UNARMED, nullptr, 0, 0); ClosePair(); }
        ShadowWriteOwed();
        const LONG st = s.sh.state;
                // an older arm generation or build, a stop, or a set that ended itself: published CLOSED, nothing written or copied
        if (st != FG_LIVE || h.gen != dtArmCount || s.sh.armQuiet || h.hold != x.sh.bgen || x.sh.setDead == x.sh.bgen + 1)
        {
            DEVSTAT(if (h.hold != x.sh.bgen) x.w.handsOld++;)
            x.w.gapLatch = 1;
            x.e.engaged = 0; ShadowCloseEarly();                  // not engaged: an early 'Q' has no hand
            PubRecord(h.p, FG_PUB_CLOSED, st == FG_LIVE ? FG_CL_STALE : FG_CL_DEAD, nullptr, 0, 0);
            ArmedHudStore(h, own);                                // the frame into HOLD anyway, where a HUD-less picture follows
            return;
        }
        x.e.engaged = 1;                                          // the next injection may write its 'Q' after its body
        w.openPhase = FG_PAIR_OPEN; w.openP = h.p; w.openGen = h.gen; DEVSTAT(w.openK = h.k;) w.openFEnd = h.fEnd;
        w.openWithhold = h.withhold; w.openFlags = h.flags; w.openHold = 0;
        w.openPeriodUs = h.periodUs; w.openTEntry = h.tEntry;
        w.openHudlessSeen = false; w.openQ = w.openG = false; w.iResolved = w.nResolved = false; w.openNosource = false;
        x.w.openN = 0; x.w.openKind = FG_G_CLOSED;
        { FgArmedWorker& aw = B::A().w; aw.held = 0; aw.show = 0; aw.why = FG_AW_NOSOURCE; aw.overPrev = 0; aw.ordAsk = 0; aw.rideSrc = 0; }
        _InterlockedExchange(&s.sh.pairOpen, 1);
        w.handFor = h.k; w.handGen = h.gen;
        const bool realCut = (h.withhold & FG_W_CUT) && !(h.flags & FG_HF_FIRST);   // an engage's first pair is a cut by rule, not the camera's
                // The pair's number is its injection's; a frame not sent gets no 'Q', no 'G', nothing copied.
        const LONG sn = B::SentN();
        const bool sent = sn > w.lastSentN && B::LatOrd(sn) == h.fEnd;
        w.lastSentN = sn; w.openSent = sent ? 1 : 0;
                // An early 'Q' of this frame is adopted; one of any other frame is closed CLOSED first.
        const bool early = sent && x.e.qN == sn;
        if (!early) ShadowCloseEarly();
        if (!sent)
        {
            x.w.gapLatch = 1; DEVSTAT(if (realCut) x.w.cutNoQ++;)
            ArmedHudStore(h, own);                                // an unsent frame copies nothing for the helper; with a HUD-less picture to follow, it goes into HOLD
            return;
        }
                // G5, never blocking: the last DISPATCH must be answered, or the helper may still read PRESENT and this pair copies nothing.
        // One read of fgOut answers every pending n with fgOut at or past 2n. If any remain, ArmedOverMay decides whether this frame's
        // home stands behind the helper on the card (else NO SOURCE). No wait is added.
        bool nosource = false;
        if (x.w.pend[0])
        {
            const ULONGLONG out = B::TlValue(FG_TL_OUT);
            int left = 0;
            for (int i = 0; i < FG_SHADOW_PEND; i++)
            {
                const LONG pn = x.w.pend[i];
                if (!pn) break;
                if (out >= 2ull * (ULONGLONG)pn) ShadowAnswerSeen(pn, out);
                else x.w.pend[left++] = pn;                       // still in flight, oldest first
            }
            for (int i = left; i < FG_SHADOW_PEND; i++) x.w.pend[i] = 0;
            DEVSTAT(x.w.lastDispatchN = left ? x.w.pend[left - 1] : 0;)
            if (left) nosource = !ArmedOverMay(h, sn, left, early, realCut);
        }
                // clean hands in a row, for the next ArmedOverMay
        { FgArmedWorker& ow = B::A().w; ow.overClean = (nosource || ow.overPrev) ? 0 : (ow.overClean < 0x10000 ? ow.overClean + 1 : ow.overClean); }
        if (x.sh.setDead == x.sh.bgen + 1)                        // that answer was the third with no frame: closed, unsent
        {
            x.w.gapLatch = 1;
            x.e.engaged = 0; ShadowCloseEarly();                  // the set is over
            PubRecord(h.p, FG_PUB_CLOSED, FG_CL_DEAD, nullptr, 0, 0);
            w.openPhase = FG_PAIR_NONE; _InterlockedExchange(&s.sh.pairOpen, 0); w.handFor = -1;
            ArmedHudStore(h, own);
            return;
        }
        DWORD qf = 0;
        if (early)
        {
            // 'Q'(n) is on the pipe already; the hand adds what only it knows (a cut, an engage's first gap), and the 'G' carries the union.
            const DWORD qe = (DWORD)x.e.qReasons;
            qf = qe;
            if (realCut) qf |= FG_Q_CUT;
            if (!(qf & FG_Q_BUILD) && (h.flags & FG_HF_FIRST)) qf |= FG_Q_GAP;
            DEVSTAT(for (int b = 0; b < 4; b++) if ((qf & ~qe) & (1u << b)) x.w.resetBy[b]++;)
            x.e.qN = 0;
            w.openQ = true; x.w.openN = sn;
        }
        else
        {
                // 'Q' first, so the helper prepares pair n while this side copies (a frame whose injection wrote none: an engage's first, a
        // set's first, after a re-arm).
        if (x.w.lastBgenQ != x.sh.bgen || !x.w.lastQN) qf |= FG_Q_BUILD;
        if (x.w.lastQN && h.gen != x.w.lastGenQ) qf |= FG_Q_DEVICE;
        if (realCut) qf |= FG_Q_CUT;
        if (!(qf & FG_Q_BUILD) && (x.w.gapLatch || (h.flags & FG_HF_FIRST) || sn != x.w.lastQN + 1)) qf |= FG_Q_GAP;   // a build is its own reason, not a gap too
        DEVSTAT(for (int b = 0; b < 4; b++) if (qf & (1u << b)) x.w.resetBy[b]++;)
        B::WriteQ(sn, qf); w.openQ = true; DEVSTAT(s.c.qWritten++;)
        x.w.openN = sn; x.w.lastQN = sn; x.w.lastBgenQ = x.sh.bgen; x.w.lastGenQ = h.gen; x.w.gapLatch = 0;
        }
        x.w.openReasons = (LONG)qf;                               // the pair's 'G' carries the same four
        w.openNosource = nosource;
        {
            // The hand's span, for every sent pair: the real frame into HOLD even with no source; a span not taken leaves no store and the
            // pair goes out inline. PASS (a HUD-less draw follows): PRESENT and HOLD only. TEX: the whole handoff in one span, HUDLESS from
            // the binding's image; where it cannot be named, fed with the back buffer and never shown.
            FgArmedWorker& aw = B::A().w;
            const bool pass = aw.passK != 0;
            DEVSTAT(if (h.flags & FG_HF_HUDTEX) aw.hudTex++;)
            aw.texAsk = ((h.flags & FG_HF_HUDTEX) && !pass && !nosource) ? 1 : 0; aw.texGot = 0;
            // the helper's place on the card, once an engage; the order wait only for a pair whose 'G' will say DISPATCH
            ArmedCardAsk(h);
            aw.ordAsk = (!nosource && !pass && !(w.openWithhold & FG_W_RENDER_FACTS & (FG_W_NOENCODE | FG_W_VIEW)) && ArmedOrdWants(h)) ? 1 : 0;
            // the same test without the key, to count the pairs an order wait is for
            aw.rideSrc = (!nosource && !pass && !(w.openWithhold & FG_W_RENDER_FACTS & (FG_W_NOENCODE | FG_W_VIEW)) && (h.flags & FG_HF_RIDE) && aw.passK == 0) ? 1 : 0;
            if (!ArmedSpan(own, !nosource, pass)) { w.openNosource = true; DEVSTAT(x.w.submitsRefused++;) }
            else { aw.held = 1; aw.holdK = h.k; }
            if (aw.texAsk && !w.openNosource)
            {
                if (aw.texGot) { DEVSTAT(aw.hudTexFed++;) }
                else { DEVSTAT(aw.hudTexLost++;) w.openFlags |= FG_HF_NOHUD; }
            }
            aw.texAsk = 0;
        }
    }
    static void ShadowHudless(const FgPairPayload& pp, DWORD own)
    {
        (void)own;
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X();
                // Whatever became of the pair, a HUD-less picture drawn after the hand is taken back from HOLD first: a closed or unsent pair's
        // Present would otherwise show it.
        ArmedHudBack(pp, own);
        if (w.openPhase != FG_PAIR_OPEN || pp.p != w.openP || s.sh.state != FG_LIVE || s.sh.armQuiet) { DEVSTAT(s.c.noteRefused++;) return; }
        w.openHudlessSeen = true;
        if (!w.openQ) return;                                     // not sent: nothing copied or signalled, no 'G'
        ArmedG();
    }
        // Before a build releases the set (render thread, drained, before the build message): no pair stays open, owed 'G's are
    // written, and the build generation moves, so a hand of the old set opens nothing on the new one.
    static void BeforeRelease()
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X();
                // No pair stays armed across a build (it would fire from the new set's images). One still armed here had its drain note
        // refused: its real frame is dropped, counted.
        if (w.openPhase == FG_PAIR_ARMED) ArmedDrop(FG_AD_BUILD);
        if constexpr (B::kWire) ArmedOrdStop();     // no order wait across a build: its drain waits for the queue
        if (w.openPhase == FG_PAIR_OPEN)
        {
            PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_DISARM, nullptr, 0, 0);
            if (w.openQ && !w.openG)
            {
                w.openG = true;
                if (x.sh.stopped) { DEVSTAT(x.w.gUnsent++;) }
                else { B::WriteG(x.w.openN, FG_G_CLOSED); DEVSTAT(s.c.gWritten++; x.w.gByKind[FG_G_CLOSED]++;) x.w.openKind = FG_G_CLOSED; }
            }
            w.openPhase = FG_PAIR_NONE; _InterlockedExchange(&s.sh.pairOpen, 0); w.handFor = -1;
        }
        ShadowWriteOwed();
                // an early 'Q' no hand took is closed here too
        ShadowCloseEarly(true); x.e.engaged = 0;
        x.w.gapLatch = 1; x.w.failRun = 0;
                // An injection sent before this build went to the old guides, so it is not "sent" for the new set; the gap latch makes the
        // next 'Q' say so.
        w.lastSentN = B::SentN();
        _InterlockedIncrement(&x.sh.bgen);
        s.r.lastEngaged = false;
                // else "was engaged" would carry across the build
        s.r.wasEngaged = false;
    }
        // G4: the last 'Q''s prepare reads the guides the body is about to write. Not retired yet: one wait-only batch behind fgPrep,
    // counted. Only for the held set's 'Q'.
    static void ShadowBeforeInject()
    {
        FgState& s = S(); FgShadowState& x = B::X();
        if (!x.w.lastQN || x.sh.stopped || s.sh.state != FG_LIVE || x.w.lastBgenQ != x.sh.bgen) return;
        if (B::TlValue(FG_TL_PREP) >= (ULONGLONG)x.w.lastQN) return;
        WriteRelease(&x.sh.prepTop, x.w.lastQN);                  // the wait's top, stored before the wait
        FGC_IF(B::PrepGuard(x.w.lastQN), x.w.prepGuards++);
    }

        // ================================================================ the merged path
    // What a binding that arms on the product's wire runs: the helper's generated frame is landed in the back buffer and presented,
    // then the real frame is brought back from HOLD and presented at its own time. One pair: ArmedBeforeInject, ArmedSpan, ArmedG at
    // the HUD-less note, ArmedPresent (inline or ARMED), ArmedPlacerStep, ArmedFire, ArmedFirePresent.
    // What could put a wrong picture on the screen, and what prevents it:
    //   another pair's generated image, or none    the fire reads fgOut == 2n itself; the hand closes the pair before it
    //   a reset dispatch's picture                 ArmedG: such a pair is never showable
    //   the next frame under this pair's name      the hand fires an armed pair before its own copy into HOLD
    //   a half-drawn frame                         no Present without the fire's write taken by the queue
    //   a frame of a set that is gone              BeforeRelease leaves nothing armed; HOLD and SAVE are the mod's own
    //   a real frame behind the helper             the pending real frame goes out, flushed, ahead of the guard's wait

        // Every present the merged path made is on the queue when this returns.
    static void ArmedFlush() { B::Flush(); B::A().w.blitOut = 0; }
        // Live, or closing (a stop fires the pending real frame once). A DEAD machine fires nothing.
    static bool ArmedMayFire() { const LONG st = S().sh.state; return st == FG_LIVE || st == FG_CLOSING; }
        // The pair ends with nothing more shown: its real frame dropped, counted with its reason. On the worker, or a thread holding the
    // ring drained.
    static void ArmedDrop(int why)
    {
        FgState& s = S();
        ArmedResolveI(FG_RES_WITHHELD, FG_AW_DROPPED, FG_W_CLOSING, 0);
        ResolveN(FG_RES_DROPPED, FG_PT_CLOSING, 0);
        s.c.nDropped++; B::A().w.dropped[why]++;
        ClosePair();
    }
        // Once: counted by the merged path's reason, and handed to the machine's record with the nearest of its fifteen.
    static void ArmedResolveI(int res, int why, DWORD nearest, LONGLONG start)
    {
        if (S().w.iResolved) return;
        B::A().w.iWhy[why]++;
        ResolveI(res, nearest, start);
    }
        // The hand's one span: the wire's handoff and the real frame into HOLD in the same buffer. feed: PRESENT may be written (false:
    // the last dispatch is unanswered, NOSOURCE). fgIn = n is signalled either way: its retire is the slot's anchor. Nothing is begun
    // unless every copy can be recorded. pass: a HUD-less draw follows (no HUDLESS, no fgIn here). HUDLESS from the binding's image is
    // asked after the flush (B::HudSrc); a no feeds the back buffer, never shown.
    static bool ArmedSpan(DWORD own, bool feed, bool pass = false)
    {
        FgState& s = S(); FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        if (!B::CbTake(1)) { DEVSTAT(s.w.slotWaits++;) return false; }
        _InterlockedExchange(&s.sh.fireBusy, 1);
        WorkerAt(own);
        Lock();
        ArmedFlush();
        const ULONGLONG bb = B::BBImage();
        const int inP = FG_IMG_PRESENT, inH = FG_IMG_HUDLESS;
        const bool fromOwn = aw.texAsk && feed && !pass && B::HasHudless() && B::ImgOk(inH) && B::HudSrc();
        aw.texGot = fromOwn ? 1 : 0;
        bool took = bb != 0 && B::ImgOk(FG_IMG_HOLD) && (!feed || B::ImgOk(inP));
        if (took)
        {
            B::QLock();
            B::RecBegin();
            if (feed)
            {
                B::RecCopyToImg(bb, inP);
                if (fromOwn) B::RecCopyHudToImg(inH);
                else if (!pass && B::HasHudless()) B::RecCopyToImg(bb, inH);
            }
            B::RecCopyToImg(bb, FG_IMG_HOLD);
            if (!pass) B::RecSignal(FG_TL_IN, x.w.openN);         // tS: the GPU retire of the pair's handoff
            took = B::RecSubmit();
            // the order wait right after this submit, under the same lock: nothing of the next frame between handoff and wait
            DEVSTAT(if (took && aw.rideSrc && feed && !pass) aw.rideSrcN++;)
            if (took && aw.ordAsk == 1 && feed && !pass && ArmedOrdWait(x.w.openN)) aw.ordAsk = 2;
            B::QUnlock();
        }
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
        return took;
    }
        // ================================================================ THE HUD-LESS PICTURE
    // Two forms, by the binding's answer at the hand. TEX: the encode wrote the picture into the binding's image before the hand,
    // the span takes it into HUDLESS, and nothing below runs for it. PASS, the older form: the binding draws it into the back buffer
    // after the hand; the HUD-less note's span copies it to HUDLESS with fgIn = n, then HOLD back into the back buffer. If it cannot
    // be taken back, its Present is not made (ArmedHudDropped). FG_HF_NOHUD frames are fed with the presented picture and never
    // shown; their dispatch does not reset.

        // A hand that opens no pair: where a HUD-less picture follows, the frame is copied into HOLD anyway, with no signal.
    static void ArmedHudStore(const FgHandPayload& h, DWORD own)
    {
        FgArmedWorker& aw = B::A().w;
        if (!aw.passK) return;
        DEVSTAT(aw.hudStore++;)
        if (ArmedSpan(own, false, true)) aw.holdK = h.k;
    }
        // feed: the picture to the helper with fgIn = n in that submit; with no source, fgIn in the restore's submit. Two submits under
    // one hold, the restore second. Bit 0: the feed's submit taken; bit 1: the restore's.
    static int ArmedHudSpan(DWORD own, bool feed, bool signal)
    {
        FgState& s = S(); FgShadowState& x = B::X();
        const int inH = FG_IMG_HUDLESS;
        const bool toHelper = feed && B::ImgOk(inH);
        if (!B::CbTake(toHelper ? 2 : 1)) { DEVSTAT(s.w.slotWaits++;) return 0; }
        _InterlockedExchange(&s.sh.fireBusy, 1);
        WorkerAt(own);
        Lock();
        ArmedFlush();
        const ULONGLONG bb = B::BBImage();
        int r = 0;
        if (bb != 0 && B::ImgOk(FG_IMG_HOLD))
        {
            B::QLock();
            if (toHelper)
            {
                B::RecBegin();
                B::RecCopyToImg(bb, inH);
                B::RecSignal(FG_TL_IN, x.w.openN);                // tS: the handoff is complete with this copy
                if (B::RecSubmit()) r |= 1;
            }
            B::RecBegin();
            B::RecCopyFromImg(FG_IMG_HOLD, bb);                   // the real frame, back
            if (signal && !toHelper) B::RecSignal(FG_TL_IN, x.w.openN);
            if (B::RecSubmit()) r |= 2;
            B::QUnlock();
        }
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
        return r;
    }
        // The picture drawn after this ordinal's hand goes to the helper and is taken back. One that did not reach the helper leaves
    // the pair with no source.
    static void ArmedHudBack(const FgPairPayload& pp, DWORD own)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        if (!aw.passK || aw.passK != pp.k) return;
        aw.passK = 0;
        const bool open = w.openPhase == FG_PAIR_OPEN && pp.p == w.openP && s.sh.state == FG_LIVE && !s.sh.armQuiet && w.openQ;
        const bool feed = open && !w.openNosource;
        const int r = aw.holdK == pp.k ? ArmedHudSpan(own, feed, open) : 0;
        if (feed)
        {
            if (r & 1) { DEVSTAT(aw.hudFed = 1; aw.hudFedN++;) }
            else { w.openNosource = true; DEVSTAT(x.w.submitsRefused++; aw.hudFedLost++;) }
        }
        if (r & 2) { DEVSTAT(aw.hudBack++;) }
        else { aw.wrongK = pp.k; DEVSTAT(aw.hudBackLost++;) }
    }
        // A back buffer holding a HUD-less picture nothing could take back is not presented (asked first in WorkerPresent). True:
    // dropped.
    static bool ArmedHudDropped(LONG k)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgArmedWorker& aw = B::A().w;
        if (!aw.wrongK || aw.wrongK != k) return false;
        aw.wrongK = 0;
        aw.dropped[FG_AD_HUDLESS]++; s.c.nDropped++;
        if (w.handFor == k && w.openPhase == FG_PAIR_OPEN)
        {
            w.handFor = -1;
            PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_DEAD, nullptr, 0, 0);
            ClosePair();
        }
        return true;
    }
        // The kind says what the pair means for the helper's history, never whether it is shown: WITHHELD only where the inputs must not
    // enter it; NOSOURCE where nothing was handed; DISPATCH otherwise (a cut or a first resets). A dispatch that resets is fed, never
    // shown. primeNext mirrors the helper's reset rule (host/fg_host.h, FgOnG).
    static void ArmedG()
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        const DWORD facts = w.openWithhold & FG_W_RENDER_FACTS;
        const DWORD kind = w.openNosource ? (DWORD)FG_G_NOSOURCE : (facts & (FG_W_NOENCODE | FG_W_VIEW)) ? (DWORD)FG_G_WITHHELD : (DWORD)FG_G_DISPATCH;
        const DWORD gw = kind | ((DWORD)x.w.openReasons << FG_G_RESET_SHIFT);
        B::WriteG(x.w.openN, gw); w.openG = true; DEVSTAT(s.c.gWritten++; x.w.gByKind[kind]++;) x.w.openKind = (LONG)kind;
        int why = FG_AW_SHOWN;
        if (kind == FG_G_DISPATCH)
        {   // pending until a later hand reads its answer
            int at = 0;
            while (at < FG_SHADOW_PEND && x.w.pend[at]) at++;
            if (at < FG_SHADOW_PEND) x.w.pend[at] = x.w.openN;
            DEVSTAT(x.w.lastDispatchN = x.w.openN;)
            const bool reset = x.w.openReasons != 0 || aw.primeNext != 0;
            aw.primeNext = 0;                                     // cleared by a dispatch with a frame
            DEVSTAT(if (reset) aw.fedReset++;)
            aw.since = reset ? 0 : (aw.since < 0x10000 ? aw.since + 1 : aw.since);   // for K over full, steady dispatches (ArmedKTake)
            why = reset ? FG_AW_RESET : facts ? FG_AW_FACT : (w.openFlags & FG_HF_NOHUD) ? FG_AW_NOHUD : B::Pipelined() ? FG_AW_PIPELINED : FG_AW_SHOWN;
        }
        else
        {
            if (kind != FG_G_WITHHELD) x.w.gapLatch = 1;          // after a withheld pair the helper resets on its own
            aw.primeNext = 1;
            why = kind == FG_G_NOSOURCE ? FG_AW_NOSOURCE : FG_AW_FACT;
        }
        aw.show = why == FG_AW_SHOWN ? 1 : 0; aw.why = why;
    }
        // On the worker. INLINE where the pair was not sent (it never arms: its fire would serve the frame before), has no HOLD, no
    // HUD-less note, or the machine is closing; else ARMED: record, publication, B::BackMark, the checkpoints, and S_OK at once.
    // It never waits.
    static HRESULT ArmedPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags, LONG k)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X(); FgArmedState& a = B::A();
        typedef HRESULT(__stdcall* Dxvk_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        const Dxvk_t dxvk = (Dxvk_t)s.ptr.dxvkPresent;
        const LONG st = s.sh.state;
        int why = -1, mine = -1;
        if (!w.openSent) { why = FG_IN_NOSOURCE; mine = FG_AI_UNSENT; }
        else if (!a.w.held) { why = FG_IN_NOSOURCE; mine = FG_AI_NOHOLD; }
        else if (!w.openHudlessSeen) { why = FG_IN_NOHUDLESS; mine = FG_AI_NOHUDLESS; }
        else if (st != FG_LIVE || s.sh.armQuiet) { why = FG_IN_CLOSING; mine = FG_AI_CLOSING; }
        if (why >= 0)
        {
            ArmedOrdBeforePresent();                              // no second present behind a standing order wait
            if (FG_PRESENT_GATE_ON(1)) B::PresentGate();          // the gate: no lock held, ahead of DXVK's Present
            const HRESULT hr = dxvk(sc, src, dst, wnd, dirty, flags);
            a.w.blitOut = 1;
            DEVSTAT(s.c.presentsInline++;) s.c.presentsDxvk++; DEVSTAT(a.w.inl[mine]++;)
            B::LatEnd(w.openFEnd);
            FgHandPayload hp = {}; hp.gen = w.openGen; hp.k = k; hp.fEnd = w.openFEnd; hp.hold = (LONG)x.w.openKind; hp.tEntry = w.openTEntry;
            PubRecord(w.openP, FG_PUB_INLINE, why, &hp, w.tArm, w.openG ? x.w.openN : 0);
            ResolveI(FG_RES_WITHHELD, FG_W_NOSOURCE, 0);
            ResolveN(FG_RES_FIRED, FG_PT_INLINE, w.tArm);
            ClosePair();
            return hr;
        }
        FgArmedPair rec = {}; rec.p = w.openP; rec.n = x.w.openN; rec.kind = x.w.openKind; rec.show = a.w.show; rec.why = a.w.why;
        rec.prev = a.w.overPrev; rec.ord = a.w.ordAsk == 2 ? 1 : 0; rec.since = a.w.since;
        Publish(a.pair[w.openP & (FG_RING - 1)], w.openP, rec);
                // The hand's hold field carried the build generation; the pair's own hold is 0.
        FgHandPayload h = {};
        h.k = k; h.gen = w.openGen; h.p = w.openP; h.fEnd = w.openFEnd; h.withhold = w.openWithhold; h.hold = 0; h.flags = w.openFlags;
        h.periodUs = w.openPeriodUs; h.tEntry = w.openTEntry;
        w.openPhase = FG_PAIR_ARMED;
        w.ckPair = 0;
        if (a.k.place) ArmedPlaceArm();
        if (!a.w.show)
            ArmedResolveI(FG_RES_WITHHELD, a.w.why, a.w.why == FG_AW_FACT ? (w.openWithhold & FG_W_RENDER_FACTS) : (DWORD)FG_W_NOSOURCE, 0);
        B::BackMark();
        PubRecord(w.openP, FG_PUB_ARMED, 0, &h, w.tArm, w.openSent);
        CheckpointsInstall();
        return S_OK;
    }
        // The merged path's fire note: the placer's wake (FG_PT_B), a drain (FG_PT_D) or the close note (FG_FIRE_CLOSE).
    // A drain closes too: an armed pair fires its real frame, an open pair is published closed with its 'G'. The close note
    // also means "not engaged": an early 'Q' has no hand and goes out closed.
    static void ArmedFireNote(const FgFirePayload& fp, DWORD own)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        const LONGLONG wt0 = B::WorkT();
        if (fp.why == FG_PT_B) ArmedFire(FG_PT_B, own, false);
        else
        {
            ArmedFire(FG_PT_D, own, false);
            if (fp.why == FG_FIRE_CLOSE && w.openPhase == FG_PAIR_ARMED) ArmedDrop(FG_AD_DEAD);   // fires nothing, and no hand will come
            if (w.openPhase == FG_PAIR_OPEN) { PubRecord(w.openP, FG_PUB_CLOSED, FG_CL_UNARMED, nullptr, 0, 0); ClosePair(); w.handFor = -1; }
            ShadowWriteOwed();
            if (fp.why == FG_FIRE_CLOSE) { B::X().e.engaged = 0; ShadowCloseEarly(); ArmedStopped(); }   // the engage ends: no order wait stands
        }
        (void)s; B::WorkAdd(wt0);
    }
        // Before an injection's body. The guard's batch waits on DXVK's queue for the helper's prepare, and the queue runs batches
    // in order, so a real frame queued behind it would be late or frozen behind a hung helper. When a wait is about to be queued:
    // 1. a pair still armed fires first, its real frame forced (guardForced);
    // 2. DXVK's stream is flushed if a present was made since the last flush (a Present returns before its blit reaches the
    // queue, so the wait could land ahead of that blit);
    // 3. then the guard (ShadowBeforeInject tests again: an answer that came meanwhile queues nothing).
    // With no wait due the fire here is point c: the early rule decides.
    static void ArmedBeforeInject(const DWORD* payload, DWORD bytes)
    {
        FgState& s = S(); FgShadowState& x = B::X(); FgArmedState& a = B::A();
        const LONGLONG wt0 = B::WorkT();
        B::InjectSeen(payload, bytes);
        bool wait = x.w.lastQN && !x.sh.stopped && s.sh.state == FG_LIVE && x.w.lastBgenQ == x.sh.bgen
                 && B::TlValue(FG_TL_PREP) < (ULONGLONG)x.w.lastQN;
                // The frame's checkpoint count up to its injection, for the next pair's position.
        if (a.k.place && s.w.openPhase == FG_PAIR_ARMED && (s.w.openFlags & FG_HF_RIDE)) a.w.ckPrev = s.w.ckPair;
                // The prepare's wait behind the answer (ArmedPrepWait).
        if (wait && a.k.prepWait && ArmedPrepWait()) wait = B::TlValue(FG_TL_PREP) < (ULONGLONG)x.w.lastQN;
                // A ride pair's real frame fires behind the body (ArmedAfterInject), not here, unless a wait is about to be queued, the
        // frame is pipelined, or the machine is closing. A raised due is still served here.
        a.w.rideAt = 0;
        if (s.w.openPhase == FG_PAIR_ARMED)
        {
            const bool rides = (s.w.openFlags & FG_HF_RIDE) != 0 && FG_ARMED_GUARD_ON(6);
            const bool behind = rides && !wait && !B::Pipelined() && s.sh.state == FG_LIVE;
            if (rides && !behind && !s.w.nResolved) a.w.rideAhead++;
            // Two calls: the one a queued wait takes must stay character for character, as tools/check-fg-fence.py checks its text.
            if (behind) ArmedFire(FG_PT_C, (DWORD)((payload - 2) - dtRing), false, true);
            else
            ArmedFire(FG_PT_C, (DWORD)((payload - 2) - dtRing), wait);
            if (behind && s.w.openPhase == FG_PAIR_ARMED) a.w.rideAt = 1;
        }
        if (wait && a.w.blitOut) { ArmedFlush(); DEVSTAT(a.w.guardFlushes++;) }
        ShadowBeforeInject();
        B::WorkAdd(wt0);
    }
        // After the body's submits: a ride pair still armed fires its real frame here, so it is shown when the helper's upscale of
    // this frame answers. An unresolved generated frame is withheld. Nothing fires twice.
    static void ArmedAfterInject(const DWORD* payload)
    {
        FgState& s = S(); FgArmedState& a = B::A();
        if (!a.w.rideAt) return;
        a.w.rideAt = 0;
        if (s.w.openPhase != FG_PAIR_ARMED) return;
        const LONGLONG wt0 = B::WorkT();
        ArmedFire(FG_PT_E, (DWORD)((payload - 2) - dtRing), false);
        B::WorkAdd(wt0);
    }
        // ---- every generated frame shown on a busy card ----
    // Where the next injection finds a ride pair armed with its generated frame not yet due, the answer is read by equality
    // and, if in, the generated frame fires here, ahead of the body; the real frame still rides behind it. Its order wait is
    // released first where it still stands.
    static void ArmedEarly(int point, DWORD own, LONGLONG reached)
    {
        FgState& s = S(); FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        const LONG n = x.w.openN;
        if (B::TlValue(FG_TL_OUT) < 2ull * (ULONGLONG)(ULONG)n && B::A().k.rideWaitAns) ArmedEarlyWait(n);
        if (B::TlValue(FG_TL_OUT) != 2ull * (ULONGLONG)(ULONG)n) { DEVSTAT(aw.rideNoAns++;) return; }
        if (aw.overPrev) { LONGLONG tp = 0; if (!ReadStamp(s.st[FG_TL_OUT], 2 * aw.overPrev, &tp)) { DEVSTAT(aw.rideEarlyNot++;) return; } }
        FGC_IF(ReadAcquire(&aw.ordTop) == n && ReadAcquire(&aw.ordSig) < n && ArmedOrdRelease(n), aw.ordRelInj++);
        const LONG shown0 = aw.iWhy[FG_AW_SHOWN];
        FgDue di = {};
        ArmedFireI(point, own, di, reached);
        DEVSTAT(if (aw.iWhy[FG_AW_SHOWN] != shown0) { aw.rideEarlyN++; if (aw.posAt > 0 && aw.posP == s.w.openP) aw.posInj++; })
    }
        // The injection reached the worker before the answer: the worker waits for it at no cost to the card (the order wait holds
    // the queued scene, flushed first). Never a wait on a helper's value alone: it ends at any release of the order wait, or
    // after FG_EARLY_WAIT_MS of the worker's own time.
    enum { FG_EARLY_WAIT_MS = 8 };
    static void ArmedEarlyWait(LONG n)
    {
        FgArmedWorker& aw = B::A().w;
        if (ReadAcquire(&aw.ordTop) != n || ReadAcquire(&aw.ordSig) >= n) return;   // no order wait of this pair's stands
        ArmedFlush();
        DEVSTAT(aw.rideWaitN++;)
        DEVSTAT(const LONGLONG t0 = B::Now();)
        LARGE_INTEGER q0, qf, q1; QueryPerformanceCounter(&q0); QueryPerformanceFrequency(&qf);
        const LONGLONG bound = q0.QuadPart + qf.QuadPart * FG_EARLY_WAIT_MS / 1000;
        const ULONGLONG want = 2ull * (ULONGLONG)(ULONG)n;
        for (;;)
        {
            if (B::TlValue(FG_TL_OUT) >= want || ReadAcquire(&aw.ordSig) >= n) break;
            QueryPerformanceCounter(&q1); if (q1.QuadPart >= bound) break;
            B::TlWait(FG_TL_OUT, want, 1);
        }
        DEVSTAT(if (B::TlValue(FG_TL_OUT) == want) aw.rideWaitGot++;)
        DEVSTAT(const LONG us = (LONG)((B::Now() - t0) * 1000000 / B::Qpf());)
        DEVSTAT(LONG cur = ReadAcquire(&aw.rideWaitMaxUs);)
        DEVSTAT(while (us > cur) { const LONG was = InterlockedCompareExchange(&aw.rideWaitMaxUs, us, cur); if (was == cur) break; cur = was; })
    }
        // Where a wait on the helper's prepare is due and the ride pair's order wait stands, the worker waits for the pair's answer
    // first (an answer in is a prepare done), so the real frame rides behind the body instead of being forced ahead. Bounded as
    // the early wait; never under a lock, only for the open pair. Returns true when a wait was made.
    static bool ArmedPrepWait()
    {
        FgState& s = S(); FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        if (s.w.openPhase != FG_PAIR_ARMED || !(s.w.openFlags & FG_HF_RIDE) || s.sh.state != FG_LIVE) return false;
        const LONG n = x.w.openN;
        if (n <= 0 || x.w.lastQN != n) return false;
        if (ReadAcquire(&aw.ordTop) != n || ReadAcquire(&aw.ordSig) >= n) return false;   // no order wait of this pair's stands
        DEVSTAT(aw.prepWaitN++;)
        ArmedAnsWait(n, &aw.prepWaitMaxUs);
        DEVSTAT(if (B::TlValue(FG_TL_PREP) >= (ULONGLONG)n) aw.prepWaitGot++;)
        return true;
    }
        // Waits for pair n's answer behind its order wait (stream flushed first): until the fence is at 2n or past, the order wait
    // is released, or prepWaitMs (0: FG_EARLY_WAIT_MS) of both the worker's time and the machine's clock pass.
    // The longest wait goes into *maxUs. boundUs > 0: a shorter bound.
    static void ArmedAnsWait(LONG n, volatile LONG* maxUs, LONG boundUs = 0)
    {
        FgArmedWorker& aw = B::A().w;
        ArmedFlush();
        const LONGLONG t0 = B::Now();
        LARGE_INTEGER q0, qf, q1; QueryPerformanceCounter(&q0); QueryPerformanceFrequency(&qf);
        LONG waitUs = (B::A().k.prepWaitMs > 0 ? B::A().k.prepWaitMs : (LONG)FG_EARLY_WAIT_MS) * 1000;
        if (boundUs > 0 && boundUs < waitUs) waitUs = boundUs;
        const LONGLONG bound = q0.QuadPart + qf.QuadPart * waitUs / 1000000;
        const ULONGLONG want = 2ull * (ULONGLONG)(ULONG)n;
        const LONGLONG boundNow = t0 + UsToTicks(waitUs);      // the machine's clock: the harness's in a replay
        for (;;)
        {
            if (B::TlValue(FG_TL_OUT) >= want || ReadAcquire(&aw.ordSig) >= n) break;
            QueryPerformanceCounter(&q1); if (q1.QuadPart >= bound || B::Now() >= boundNow) break;
            B::TlWait(FG_TL_OUT, want, 1);
        }
        DEVSTAT(const LONG us = (LONG)((B::Now() - t0) * 1000000 / B::Qpf());)
        DEVSTAT(LONG cur = ReadAcquire(maxUs);)
        DEVSTAT(while (us > cur) { const LONG was = InterlockedCompareExchange(maxUs, us, cur); if (was == cur) break; cur = was; })
    }
        // ---- the generated frame placed by the card (FgArmedKeys::place) ----
    // A ride pair's generated frame fires at a position in the next frame's replay: the checkpoint call at f/32 of the count the
    // frame before ran (ckPrev), so its present stands among that frame's own work; f is fed back from the writes' GPU stamps
    // toward the middle. Position or clock due, whichever comes first; fallbacks: the early show, the clock due. The second
    // position (place2, off by default) moves on once, bounded by the real frame's due.
    enum { FG_PLACE_F0 = 8, FG_PLACE_FMIN = 1, FG_PLACE_FMAX = 31, FG_PLACE_TOL_US = 300, FG_PLACE_F2 = 4 };
        // What fired a shown generated frame (FgArmedWorker::kindK, placeNK at kind - 1).
    enum { FG_PK_POS = 1, FG_PK_POS2 = 2, FG_PK_DUE = 3, FG_PK_INJ = 4, FG_PK_ELSE = 5 };
    static bool ArmedPlaceOn() { return B::A().k.place != 0 && B::PlaceStamps(); }
        // With placing on, the engage's seed replaces teaching from the arm.
    static bool ArmedArmTeach() { return B::A().k.armTeach != 0 && !ArmedPlaceOn(); }
        // At the arm (worker): the open pair's position, from the count the frame before ran and the current f. Both restart at an
    // engage's first hand.
    static void ArmedPlaceArm()
    {
        FgState& s = S(); FgArmedWorker& aw = B::A().w; const FgWorkerLine& w = s.w;
        if (w.openFlags & FG_HF_FIRST) { aw.placeF = FG_PLACE_F0; aw.placeErrN = 0; aw.ckPrev = 0; }
        if (aw.placeF <= 0) aw.placeF = FG_PLACE_F0;
        aw.posAt = 0; aw.posP = w.openP; aw.posDone = 0; aw.posTry = 0; aw.posPlanUs = ReadAcquire(&B::A().k.kPlanUs);
        if (!(w.openFlags & FG_HF_RIDE) || !B::PlaceStamps()) return;
        DEVSTAT(aw.posRides++;)
        if (aw.ckPrev <= 1) return;
        LONG at = (aw.ckPrev * aw.placeF + 16) / 32;
        if (at < 1) at = 1;
        if (at >= aw.ckPrev) at = aw.ckPrev - 1;
        aw.posAt = at; DEVSTAT(aw.posArms++;)
    }
        // At the position (a checkpoint call of the next frame): the answer, waited for where the order wait stands. If in, the
    // generated frame fires here; if not, the first position moves on to the second, and the second leaves the pair to the fallbacks.
    static void ArmedPlaceAt(int point, DWORD own, LONGLONG reached, const FgDue* di)
    {
        FgState& s = S(); FgShadowState& x = B::X(); FgArmedWorker& aw = B::A().w;
        const LONG n = x.w.openN;
        const ULONGLONG want = 2ull * (ULONGLONG)(ULONG)n;
        LONG boundUs = 0;
        bool mayWait = true;
        if (aw.posTry)
        {   // The second position's wait ends ahead of its real frame's due, or the real frame fires off its ride right
            // behind its generated one. The real frame is raised no earlier than arm + plan + half the period - its service time,
            // with the plan the smaller of the arm's and now's; margin: the ride's guard and one plan bin.
            const FgArmedKeys& k = B::A().k;
            if (s.w.openPeriodUs <= 0) mayWait = false;
            else
            {
                const LONG planNow = ReadAcquire(&k.kPlanUs);
                const LONG planUs = ArmedRidePlan(aw.posPlanUs > 0 && aw.posPlanUs < planNow ? aw.posPlanUs : planNow) + (k.planAns ? s.w.wIUs : 0);
                const LONG hUs = (LONG)(HalfOf((double)s.w.openPeriodUs / 1000.0) * 1000.0);
                const LONGLONG left = s.w.tArm + UsToTicks(planUs + hUs - s.w.wNUs - k.rideGuardUs - FG_K_BIN_US) - B::Now();
                if (left <= 0) mayWait = false; else boundUs = left >= UsToTicks(1000000) ? 1000000 : (LONG)(left * 1000000 / B::Qpf());
                if (mayWait && boundUs <= 0) mayWait = false;
                DEVSTAT(aw.pos2BoundUs = mayWait ? boundUs : 0;)
            }
        }
        if (mayWait && B::TlValue(FG_TL_OUT) < want && ReadAcquire(&aw.ordTop) == n && ReadAcquire(&aw.ordSig) < n)
        {
            DEVSTAT(aw.posWaits++;)
            ArmedAnsWait(n, &aw.posWaitMaxUs, boundUs);
            DEVSTAT(if (B::TlValue(FG_TL_OUT) == want) aw.posWaitGot++;)
        }
        if (B::TlValue(FG_TL_OUT) != want)
        {
            DEVSTAT(aw.posNoAns++;)
            if (!aw.posTry && B::A().k.place2 && B::TlValue(FG_TL_OUT) < want)
            {   // second position: FG_PLACE_F2 32nds on, at least one call, never at or past the count's last
                LONG at2 = aw.posAt + (aw.ckPrev * FG_PLACE_F2 + 16) / 32;
                if (at2 <= aw.posAt) at2 = aw.posAt + 1;
                if (at2 < aw.ckPrev) { aw.posTry = 1; aw.posAt = at2; aw.posDone = 0; DEVSTAT(aw.pos2Arms++;) }
            }
            return;
        }
        if (aw.overPrev) { LONGLONG tp = 0; if (!ReadStamp(s.st[FG_TL_OUT], 2 * aw.overPrev, &tp)) { DEVSTAT(aw.rideEarlyNot++;) return; } }
        const LONG shown0 = aw.iWhy[FG_AW_SHOWN];
        FgDue d = {}; if (di) d = *di;
        aw.fireKind = aw.posTry ? FG_PK_POS2 : FG_PK_POS;
        ArmedFireI(point, own, d, reached);
        aw.fireKind = 0;
        if (aw.iWhy[FG_AW_SHOWN] == shown0) return;
        DEVSTAT(aw.posFires++;)
        if (aw.posTry) { DEVSTAT(aw.pos2Fires++;) return; }               // not where f was put: teaches nothing
        const int i = (int)(s.w.openFEnd & 63);
        aw.posF[i] = s.w.openFEnd; aw.posPP[i] = s.w.openP;
    }
        // Where a shown generated frame's writes landed (GPU stamps, on the worker), by frame end; the harness passes it by ordinal
    // (ArmedPlaceTakeP). place: thousandths of the real frames' spacing (500 is the middle). Every shown frame goes into the
    // ring the health exit reads; a first-position pair also into the line's ring, and teaches f.
    static void ArmedPlaceTake(LONG frame, LONG place, LONG spanUs)
    {
        FgArmedWorker& aw = B::A().w;
        if (!B::PlaceStamps()) return;
        const int i = (int)(frame & 63);
        const LONG kind = aw.kindFE[i] == frame ? aw.kindK[i] : (LONG)FG_PK_ELSE;
        aw.kindFE[i] = 0;
        const bool teach = ArmedPlaceOn() && aw.posF[i] == frame;
        if (teach) aw.posF[i] = 0;
        ArmedPlaceRec(place, spanUs, kind, teach);
    }
    static void ArmedPlaceTakeP(LONG p, LONG place, LONG spanUs)
    {
        FgArmedWorker& aw = B::A().w;
        if (!B::PlaceStamps()) return;
        LONG kind = FG_PK_ELSE;
        for (int i = 0; i < 64; i++) if (aw.kindFE[i] && aw.kindPP[i] == p) { kind = aw.kindK[i]; aw.kindFE[i] = 0; break; }
        bool teach = false;
        if (ArmedPlaceOn()) for (int i = 0; i < 64; i++) if (aw.posF[i] && aw.posPP[i] == p) { aw.posF[i] = 0; teach = true; break; }
        ArmedPlaceRec(place, spanUs, kind, teach);
    }
    static void ArmedPlaceRec(LONG place, LONG spanUs, LONG kind, bool teach)
    {
        FgArmedWorker& aw = B::A().w;
        const LONG k = aw.placeN; aw.placeV[k & 127] = place; WriteRelease(&aw.placeN, k + 1);
        if (kind >= FG_PK_POS && kind <= FG_PK_ELSE) _InterlockedIncrement(&aw.placeNK[kind - 1]);
        DEVSTAT(if (kind == FG_PK_POS) { const LONG j = aw.placePN; aw.placePV[j & 127] = place; WriteRelease(&aw.placePN, j + 1); })
        if (teach) ArmedPlaceErr(place, spanUs);
    }
    static void ArmedPlaceErr(LONG place, LONG spanUs)
    {
        FgArmedWorker& aw = B::A().w;
        if (spanUs <= 0 || spanUs > 200000) return;
        aw.placeErr[aw.placeErrN++] = (LONG)((LONGLONG)(place - 500) * spanUs / 1000);
        if (aw.placeErrN < 16) return;
        LONG t[16];
        for (int a = 0; a < 16; a++) { LONG v = aw.placeErr[a]; int b = a; while (b > 0 && t[b - 1] > v) { t[b] = t[b - 1]; b--; } t[b] = v; }
        const LONG med = (t[7] + t[8]) / 2;
        aw.placeErrN = 0;
        if (med <= FG_PLACE_TOL_US && med >= -FG_PLACE_TOL_US) return;
        LONG steps = (med > 0 ? med : -med) / FG_PLACE_TOL_US; if (steps < 1) steps = 1; if (steps > 4) steps = 4;
        LONG f = aw.placeF + (med > 0 ? -steps : steps);           // late on the card: earlier in the frame
        if (f < FG_PLACE_FMIN) f = FG_PLACE_FMIN; if (f > FG_PLACE_FMAX) f = FG_PLACE_FMAX;
        if (f != aw.placeF) { aw.placeF = f; DEVSTAT(aw.placeSteps++;) }
    }
        // A fire point of the merged path: the generated frame first, the real frame second, forced at the rig's points and at
    // `guard` (a wait about to be queued behind fgPrep). The generated frame fires only if the pair may show one, its due says
    // SHOW, the real frame is not forced, the real frame's own due is not raised yet (it would flash in front of the frame it
    // leads to), and, inside the fire, the answer reads equal.
    static void ArmedFire(int point, DWORD own, bool guard, bool rideAhead = false)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgArmedState& a = B::A();
        if (w.openPhase != FG_PAIR_ARMED) return;
        const LONG st = s.sh.state;
        if (st != FG_LIVE && st != FG_CLOSING) return;
        const LONGLONG reached = B::Now();
        FgDue di = {}, dn = {};
        const bool hasI = DueFor(s.dueI, &di, &w.staleI);
        const bool hasN = DueFor(s.dueN, &dn, &w.staleN);
                // rideAhead: the real frame rides behind this body, so C1 does not force it; FG_PT_E is that ride.
        const bool c1 = point == FG_PT_C && FG_C1_ON() && !rideAhead;
        const bool forceN = guard || point == FG_PT_D || point == FG_PT_HANDOPEN || point == FG_PT_CLOSING || st == FG_CLOSING || c1 || point == FG_PT_E;
                // A placed pair: its position or its clock due, whichever first; a position with no answer leaves the fallbacks.
        if (point == FG_PT_A && a.w.posAt > 0 && a.w.posP == w.openP && !w.iResolved && !hasN && st == FG_LIVE && !(hasI && di.verdict == FG_DUE_WITHHOLD)
            && !a.w.posDone && w.ckPair >= a.w.posAt)
        {
            a.w.posDone = 1;
            ArmedPlaceAt(point, own, reached, hasI ? &di : nullptr);
            return;
        }
        DEVSTAT(if (a.w.posAt > 0 && a.w.posP == w.openP && !w.iResolved && hasI && di.verdict == FG_DUE_SHOW && !hasN && (point == FG_PT_A || point == FG_PT_B)) a.w.posDue++;)
                // 1. The generated frame first.
        if (!w.iResolved)
        {
            // Too late to show: the real frame's due is raised, or (with minLeadUs) this point is closer than that to when it will be
            // (carried in the due's eUs by the merged placer).
            const bool tooLate = hasN || (hasI && a.k.minLeadUs > 0 && di.tDue + UsToTicks(di.eUs) - reached < UsToTicks(a.k.minLeadUs));
            if (hasI && di.verdict == FG_DUE_SHOW && !forceN && !tooLate) ArmedFireI(point, own, di, reached);
            else if (hasI && di.verdict == FG_DUE_SHOW && !forceN) ArmedResolveI(FG_RES_WITHHELD, FG_AW_LATEFIRE, FG_W_LATE, 0);
            else if (hasI && di.verdict == FG_DUE_WITHHOLD) ArmedResolveI(FG_RES_WITHHELD, (int)(di.why >> 16) < FG_AW_N ? (int)(di.why >> 16) : FG_AW_LATE, di.why & 0xFFFFu, 0);
            else if (!hasI && !hasN && rideAhead && !forceN && a.k.rideEarly) ArmedEarly(point, own, reached);
            else if (forceN)
            {
                const DWORD nearest = (point == FG_PT_CLOSING || st == FG_CLOSING) ? FG_W_CLOSING : point == FG_PT_D ? FG_W_DRAIN
                                    : point == FG_PT_HANDOPEN ? FG_W_HANDOPEN : FG_W_EARLY;
                ArmedResolveI(FG_RES_WITHHELD, FG_AW_FORCED, nearest, 0);
                DEVSTAT(if (point == FG_PT_E) a.w.rideGenLost++;)
            }
        }
                // 2. The real frame second.
        if (!w.nResolved && (forceN || hasN))
        {
            if (!w.iResolved) ArmedResolveI(FG_RES_WITHHELD, FG_AW_FORCED, FG_W_NFIRST, 0);
            s.c.points[point]++;
            DEVSTAT(if (guard) a.w.guardForced++;)
            if (ArmedFireN(point, own, reached))
            {
                DEVSTAT(if (point == FG_PT_CLOSING || st == FG_CLOSING) s.c.nAtStop++;)
                DEVSTAT(else if (point == FG_PT_C) s.c.nEarly++;)
                DEVSTAT(else if (point == FG_PT_D || point == FG_PT_HANDOPEN) s.c.nLate++;)
                DEVSTAT(else { s.c.nOnTime++; if (point == FG_PT_E) a.w.rode++; })   // the ride is the pair's own place: on time
            }
        }
    }
    static void ArmedFireI(int point, DWORD own, const FgDue& di, LONGLONG reached)
    {
                // Source image: the helper's GEN, or in copy mode (slotCopy) the pair's own real frame in HOLD.
        const int r = ArmedFirePresent(true, B::A().k.slotCopy ? (int)FG_IMG_HOLD : (int)FG_IMG_GEN, point, own, reached);
        if (r == FG_AF_SHOWN)
        {   // what fired it, kept by frame end for ArmedPlaceTake
            FgArmedWorker& aw = B::A().w; const FgWorkerLine& w = S().w;
            const int i = (int)(w.openFEnd & 63);
            aw.kindFE[i] = w.openFEnd; aw.kindPP[i] = w.openP;
            aw.kindK[i] = aw.fireKind ? aw.fireKind : point == FG_PT_C ? (LONG)FG_PK_INJ : (point == FG_PT_A || point == FG_PT_B) ? (LONG)FG_PK_DUE : (LONG)FG_PK_ELSE;
            ArmedResolveI(FG_RES_SHOWN, FG_AW_SHOWN, 0, reached); S().c.points[point]++;
        }
        else if (r == FG_AF_NOANSWER) ArmedResolveI(FG_RES_WITHHELD, FG_AW_NOANSWER, FG_W_LATE, 0);
        else if (r == FG_AF_DEAD) ArmedResolveI(FG_RES_WITHHELD, FG_AW_DROPPED, FG_W_CLOSING, 0);   // the machine died while this present was held in the gate
        else ArmedResolveI(FG_RES_WITHHELD, r == FG_AF_NOBUFFER ? FG_AW_NOBUFFER : FG_AW_NOCOPY, FG_W_NOBUFFER, 0);
    }
        // The real frame, from HOLD. A real frame whose fire could not write is dropped and counted, never presented from the back
    // buffer: after the hand the game may be drawing the next frame there. Returns false when dropped.
    static bool ArmedFireN(int point, DWORD own, LONGLONG reached)
    {
        FgState& s = S();
        const int r = ArmedFirePresent(false, FG_IMG_HOLD, point, own, reached);
        if (r != FG_AF_SHOWN)
        {
            ResolveN(FG_RES_DROPPED, point, 0); s.c.nDropped++; B::A().w.dropped[r == FG_AF_NOBUFFER ? FG_AD_NOBUFFER : r == FG_AF_DEAD ? FG_AD_DEAD : FG_AD_NOCOPY]++;
            ClosePair();
            return false;
        }
        ResolveN(FG_RES_FIRED, point, reached);
        ClosePair();
        return true;
    }
        // One fire: a picture into the back buffer, presented, the back buffer restored. srcImg: GEN, or the real frame's store.
    // Order: the answer read by equality (fgOut exactly 2 x x.w.openN; 2n+1 no frame, lower not yet, higher overtaken), never
    // a wait; the command buffers; the gate (no lock held); after it the state and answer read again; the lock, a flush, a
    // fresh back buffer; SAVE then the picture (nothing begun unless all can be recorded; a write not taken ends the fire);
    // DXVK's Present; the restore into the image current after it.
    static int ArmedFirePresent(bool isI, int srcImg, int point, DWORD own, LONGLONG reached)
    {
        FgState& s = S(); FgWorkerLine& w = s.w; FgShadowState& x = B::X(); FgArmedState& a = B::A();
        typedef HRESULT(__stdcall* Dxvk_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        const Dxvk_t dxvk = (Dxvk_t)s.ptr.dxvkPresent;
        const LONG nI = x.w.openN;
        if (isI && B::TlValue(FG_TL_OUT) != 2ull * (ULONGLONG)(ULONG)nI) return FG_AF_NOANSWER;
                // The worker has read the answer: its order wait is released here, the earliest point, so the real frame's present behind
        // it never finds it standing.
        if (isI) { const LONG n = nI; FGC_IF(ReadAcquire(&a.w.ordTop) == n && ReadAcquire(&a.w.ordSig) < n && ArmedOrdRelease(n), a.w.ordRelFire++); }
        DEVSTAT(w.firePoint = point;) w.fireKind = isI ? 0 : 1;
        const bool save = B::BackWritten();
        if (!B::CbTake(save ? 2 : 1)) { DEVSTAT(w.slotWaits++;) return FG_AF_NOBUFFER; }
        LONGLONG gateT = 0;
        ArmedOrdBeforePresent();                                  // no present behind a standing order wait; no lock held here
        if (FG_PRESENT_GATE_ON(isI ? 2 : 3)) { const LONGLONG g0 = B::Now(); B::PresentGate(); gateT = B::Now() - g0; }
                // Behind the gate, read again (the gate can hold for seconds):
        // the machine may have died: a Reset kills it and goes on to DXVK. `fireBusy` is set first and the state read second, and
        // BeforeReset kills first and waits out `fireBusy` second, so either this fire writes nothing or the Reset waits for it (1 s).
        // The helper may have gone: a dead helper leaves its fence at the top and GEN holds a dead process's memory, so the
        // generated frame's answer is read again by equality (gateRead). The real frame reads no answer: HOLD is the game's own.
        _InterlockedExchange(&s.sh.fireBusy, 1);
        if (FG_ARMED_GUARD_ON(0) && !ArmedMayFire()) { _InterlockedExchange(&s.sh.fireBusy, 0); return FG_AF_DEAD; }
        if (FG_ARMED_GUARD_ON(1) && isI && B::TlValue(FG_TL_OUT) != 2ull * (ULONGLONG)(ULONG)nI) { _InterlockedExchange(&s.sh.fireBusy, 0); DEVSTAT(a.w.gateRead++;) return FG_AF_NOANSWER; }
        WorkerAt(own);
        Lock();
        ArmedFlush();
        const ULONGLONG bbNow = B::BBImage();                     // fresh, never cached
        bool wrote = bbNow != 0 && B::ImgOk(srcImg) && (!save || B::ImgOk(FG_IMG_SAVE));
        if (wrote)
        {
            B::QLock();
            B::RecBegin();
            if (save) B::RecCopyToImg(bbNow, FG_IMG_SAVE);
            B::RecCopyFromImg(srcImg, bbNow);
            wrote = B::RecSubmit();
            B::QUnlock();
        }
        if (!wrote)
        {
            Unlock();
            _InterlockedExchange(&s.sh.fireBusy, 0);
            return FG_AF_NOCOPY;
        }
                // Service time for the placer's lead, less the gate's hold (a hold is the helper's).
        const double wSvc = TicksToMs(B::Now() - reached - gateT);
        const LONG fEnd = w.openFEnd;
        const LONGLONG t0 = B::Now();
        const LONGLONG pt0 = dtPresTicksSum;                      // only the worker writes the sum: no lock needed to read it
        const HRESULT hr = dxvk(w.sc, w.hasSrc ? &w.src : nullptr, w.hasDst ? &w.dst : nullptr, w.wnd, nullptr, w.pflags);
        const LONGLONG dPt = B::Now() - t0;
        const LONGLONG pt1 = InterlockedExchangeAdd64(&dtPresTicksSum, dPt) + dPt;   // a fire's present is outside DtExec's timing
        if (!s.sh.presLastGen) { InterlockedExchangeAdd64(&s.sh.presTicksR, pt1 - pt0); s.sh.presRN++; }   // a present behind a real frame, for PaceFixed
        s.sh.presLastGen = isI ? 1 : 0;
        a.w.blitOut = 1;
        if (FAILED(hr)) dtPresHr = (LONG)hr;                      // the game hears a lost device from a fire too
        s.c.presentsDxvk++; if (isI) s.c.presentsI++; else s.c.presentsN++;
        ULONGLONG bbAfter = 0;
        if (save)
        {
            bbAfter = B::BBImage();
            if (bbAfter == bbNow) ArmedFlush();   // one image: the blit reads what the restore is about to overwrite
        }
        if (!isI) B::LatEnd(fEnd);
        bool lost = false;
        if (save)
        {
            bool back = bbAfter != 0 && B::ImgOk(FG_IMG_SAVE);
            if (back) { B::QLock(); B::RecBegin(); B::RecCopyFromImg(FG_IMG_SAVE, bbAfter); back = B::RecSubmit(); B::QUnlock(); }
            if (!back) { a.w.restoreLost++; lost = true; }
            DEVSTAT(a.w.saves++;)
        }
        DEVSTAT(else a.w.noSaves++;)
        Unlock();
        _InterlockedExchange(&s.sh.fireBusy, 0);
                // A lost restore stands the machine down for the session: a game image now keeps a presented picture, and a queue that
        // refuses a copy is not one to write the back buffer through.
        if (lost && FG_ARMED_GUARD_ON(2)) Kill("a fire's restore was not taken by the queue: a game image keeps a presented picture");
        double& ew = isI ? w.wI : w.wN;
        ew += (wSvc - ew) * 0.10; if (ew < 0.0) ew = 0.0; if (ew > 2.0) ew = 2.0;
        if (isI) w.wIUs = (LONG)(ew * 1000.0); else w.wNUs = (LONG)(ew * 1000.0);
        return FG_AF_SHOWN;
    }
        // ---- the merged path's placer ----
    // Never hang the real frame on the helper: the generated frame's due is tS (the handoff's IN stamp) + kPlanUs, and the
    // real frame's is that slot + half the period, fixed at tS. The verdict reads the fence by equality at the slot less the
    // service time (2n SHOW, above WITHHOLD, below LATE), never a stamp: a stamper can miss a value between wakes.
    // A handoff with no stamp is timed now, or from the arm after half a period.
    static void ArmedRetire(int i)
    {
        FgArmedPlacer& pl = B::A().pl;
        for (int j = i + 1; j < pl.liveN; j++) pl.live[j - 1] = pl.live[j];
        pl.liveN--;
    }
        // K (handoff retire to answer) into the shadow placer's histogram: once a pair, when both stamps are in, for a pair that
    // dispatched and was timed by its own stamp.
    static void ArmedKTake(FgArmedLive& L)
    {
        FgState& s = S(); FgShadowPlacer& sp = B::X().pl;
        LONGLONG tOut = 0;
        if (L.kTaken || !L.timed || L.tsOwn || L.kind != (LONG)FG_G_DISPATCH) return;
        if (!ReadStamp(s.st[FG_TL_OUT], 2 * L.n, &tOut) || tOut < L.tS) return;
        L.kTaken = true;
        const LONGLONG us = (tOut - L.tS) * 1000000 / B::Qpf();
        int bin = (int)(us / FG_K_BIN_US); if (bin >= FG_K_BINS) bin = FG_K_BINS - 1;
        sp.kHist[bin]++; DEVSTAT(sp.kN++; sp.kSumUs += us; if (us > sp.kMaxUs) sp.kMaxUs = us;)
                // K over full dispatches alone, and over steady ones: a reset's dispatch and the first behind it skew it.
        FgArmedPlacer& pl = B::A().pl;
        if (L.since >= 2) { pl.kFull[bin]++; DEVSTAT(pl.kFullN++; if (L.since >= FG_K_STEADY) { pl.kSteady[bin]++; pl.kSteadyN++; }) }
    }
    static LONGLONG ArmedPlacerStep(LONGLONG now)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
                // 1. Every new publication, in p order. Only an armed pair is placed: an inline one was presented at its own Present.
        const LONG top = ReadAcquire(&s.sh.pubP);
        while (pl.seen < top)
        {
            const LONG p = pl.seen + 1;
            FgPub v = {};
            if (!Read(s.pub[p & (FG_RING - 1)], p, &v))
            {
                if (ReadAcquire(&s.pub[p & (FG_RING - 1)].seq) > p) { pl.seen = p; DEVSTAT(pl.pubLost++;) continue; }   // overwritten: the ring is eight deep
                break;                                            // not complete yet
            }
            pl.seen = p;
            if ((v.flags & FG_HF_FIRST) && a.k.roomSeed) pl.seedPend = 1;   // the engage's first stamped pair seeds the room floor
                                                                            // A pair presented inline that wrote a 'G' is not placed, only followed for K and the line.
            if (v.kind == FG_PUB_INLINE && v.sent > 0) { ArmedInlineTake(p, v); continue; }
            if (v.kind != FG_PUB_ARMED) continue;
            FgArmedPair rec = {};
            if (!Read(a.pair[p & (FG_RING - 1)], p, &rec)) { DEVSTAT(pl.recLost++;) continue; }   // lapped: a point that forces brings its real frame
            if (v.gen != pl.curGen) { pl.liveN = 0; pl.curGen = v.gen; }   // a newer arm generation: the older pairs ended at the disarm
            if (pl.liveN >= FG_ARMED_LIVE) { DEVSTAT(pl.liveOver++;) ArmedRetire(0); }
            FgArmedLive& L = pl.live[pl.liveN++];
            memset(&L, 0, sizeof(L));
            L.p = p; L.n = rec.n; L.gen = v.gen; L.kind = rec.kind; L.show = rec.show; L.warm = (v.flags & FG_HF_WARM) ? 1 : 0;
            L.periodUs = v.periodUs; L.withhold = v.withhold; L.hflags = v.flags; L.tArm = v.tArm; L.tEntry = v.tEntry;
            L.fEnd = v.fEnd;
            // How far behind an engage's first pair, for what it may teach.
            pl.sinceFirst = (v.flags & FG_HF_FIRST) ? 0 : (pl.sinceFirst < 0x10000 ? pl.sinceFirst + 1 : pl.sinceFirst);
            L.teach = (!L.warm || (a.k.rideKMaxUs > 0 && pl.sinceFirst >= FG_RIDE_SKIP)) ? 1 : 0;
            L.idx = pl.sinceFirst;
            L.prev = rec.prev; L.ord = rec.ord; L.since = rec.since;
        }
                // 1b. The standing order wait: released at its answer or its deadline.
        const LONGLONG ordNext = ArmedOrdWatch(now);
                // 2. Each live pair, in p order.
        LONGLONG next = 0; bool spinFor = false;
        auto want = [&next, &spinFor](LONGLONG t, bool spin = false)
        {
            if (t <= 0) return;
            if (next == 0 || t < next) { next = t; spinFor = spin; }
            else if (t == next && spin) spinFor = true;
        };
        const LONGLONG wI = UsToTicks(s.w.wIUs), wN = UsToTicks(s.w.wNUs);
        want(ordNext);
        for (int i = 0; i < pl.liveN; i++)
        {
            FgArmedLive& L = pl.live[i];
            // A pair timed from the arm whose stamp came now: its room need, from that stamp.
            if (L.armWait) { LONGLONG ta = 0; if (ReadStamp(s.st[FG_TL_IN], L.n, &ta)) { L.armWait = 0; ArmedRoomFromArm(L, ta); } }
            // Retired on the worker's resolution of the real frame, never on a stamp.
            FgRes r = {};
            if (Read(s.res[L.p & (FG_RING - 1)], L.p, &r) && r.nRes != FG_RES_OPEN)
            {
                // Kept for its stamp, two periods behind the arm at most.
                if (L.armWait)
                {
                    const LONGLONG bound = L.tArm + 2 * UsToTicks(L.periodUs > 0 ? L.periodUs : 40000);
                    if (now < bound) { ArmedKTake(L); want(bound); continue; }
                    L.armWait = 0; DEVSTAT(pl.armUntaught++;)
                }
                // A ride's real frame is shown when the upscale answers, which only its frame end's stamp says: kept for that stamp,
                // a period at most, and its place and needs taken from it.
                if (r.nRes == FG_RES_FIRED && r.nPoint == FG_PT_E && L.timed && !L.tsOwn && r.nStart)
                {
                    LONGLONG te = 0;
                    const LONGLONG bound = r.nStart + UsToTicks(L.periodUs > 0 ? L.periodUs : 40000);
                    if (ReadStamp(s.st[FG_TL_END], L.fEnd, &te)) ArmedRideTake(L, r.nStart, te);
                    else if (now < bound) { ArmedKTake(L); want(bound); continue; }
                    DEVSTAT(else pl.rideNoEnd++;)
                }
                ArmedKTake(L); ArmedRetire(i); i--; continue;
            }
            // The period in force for this pair, as its own present published it, and half of it.
            const double applied = (double)(L.periodUs > 0 ? L.periodUs : s.r.appliedUs) / 1000.0;
            double h = HalfOf(applied);
            if (!L.timed)
            {
                const LONGLONG hTicks = (LONGLONG)(h * (double)B::Qpf() / 1000.0);
                LONGLONG t = 0;
                if (ReadStamp(s.st[FG_TL_IN], L.n, &t)) { }
                else if (B::TlValue(FG_TL_IN) >= (ULONGLONG)(ULONG)L.n) { t = now; L.tsOwn = true; pl.tsOwn++; }
                // A ride handoff gets the whole period to retire (it retires late, which is why it rides).
                else if (now >= L.tArm + ((L.hflags & FG_HF_RIDE) ? (LONGLONG)(applied * (double)B::Qpf() / 1000.0) : hTicks)) { t = L.tArm; L.tsOwn = true; DEVSTAT(pl.tsNever++;) L.armWait = (ArmedArmTeach() || pl.seedPend) ? 1 : 0; }
                else { want(L.tArm + ((L.hflags & FG_HF_RIDE) ? (LONGLONG)(applied * (double)B::Qpf() / 1000.0) : hTicks)); continue; }   // not retired yet: its stamper or the bound wakes this thread
                L.timed = true; L.tS = t; DEVSTAT(pl.nTimed++;)
                // The line: the frame's entry at its Present to the handoff's retire, by its own stamp.
                DEVSTAT(if (!L.tsOwn && L.tEntry && t >= L.tEntry)
                {
                    FgShadowPlacer& sp = B::X().pl;
                    const LONGLONG eus = (t - L.tEntry) * 1000000 / B::Qpf();
                    int eb = (int)(eus / FG_E_BIN_US); if (eb >= FG_E_BINS) eb = FG_E_BINS - 1;
                    sp.eHist[eb]++; sp.eN++;
                })
                // The slot, and the real frame's due behind it: fixed here from tS. The plan is read once (two writers). With planAns the
                // slot is plan + the fire's service time, so the verdict falls on the plan.
                L.planUs = ReadAcquire(&a.k.kPlanUs);
                L.tI = L.tS + UsToTicks(L.planUs) + (a.k.planAns ? wI : 0);
                // A ride: the generated frame half a period ahead of the ride's place, where later than the deadline, so the pair is even.
                if (L.hflags & FG_HF_RIDE)
                {
                    const LONG dUs = ReadAcquire(&pl.rideDUs);
                    // A ride pair's deadline is the plan, never over the cap: slower answers are withheld.
                    if (a.k.rideKMaxUs > 0) L.tI = L.tS + UsToTicks(ArmedRidePlan(L.planUs)) + (a.k.planAns ? wI : 0);
                    if (dUs > 0) { const LONGLONG even = L.tS + UsToTicks(dUs) - hTicks; if (even > L.tI) L.tI = even; }
                    ArmedRideNext(L);
                }
                s.pl.hUs = (LONG)(h * 1000.0);
                L.tN = L.tI + (LONGLONG)(h * (double)B::Qpf() / 1000.0);
                ArmedRoom(L);
                // Also kept, teaching or not, while the engage's seed is owed (ArmedRoomSeed).
                if (L.armWait && !(L.tEntry && ((ArmedArmTeach() && L.teach) || pl.seedPend))) L.armWait = 0;
            }
            const bool spin = true;                               // raises are met exactly (PlacerRun)
                                                                  // The generated frame's verdict, once, at the slot less the service time.
            if (!L.iRaised)
            {
                const LONGLONG at = L.tI - wI;
                if (now < at) want(at, spin);
                else
                {
                    // Deadline: the slot's raise plus the late allowance, never past the real frame's own raise.
                    LONGLONG dline = at + UsToTicks(a.k.lateUs);
                    const LONGLONG nextAt = L.tN - wN;
                    if (dline > nextAt) dline = nextAt;
                    if (dline < at) dline = at;
                    int why = -1;                                 // -1 SHOW; -2 not decided yet (inside the allowance)
                    if (L.show)
                    {
                        const ULONGLONG out = B::TlValue(FG_TL_OUT), n2 = 2ull * (ULONGLONG)(ULONG)L.n;   // the fence itself, by equality
                        if (out == n2) why = -1;
                        else if (out > n2) why = FG_AW_NOFRAME;
                        else if (now >= dline) why = FG_AW_LATE;
                        else { why = -2; want(dline, spin); }      // the answer's stamp wakes this thread too
                                                                   // A pair that copied over a dispatch in flight shows only if that dispatch gave a frame (its 2n stamp); one that gave none
                        // made the helper reset this one.
                        if (why == -1 && L.prev) { LONGLONG tp = 0; if (!ReadStamp(s.st[FG_TL_OUT], 2 * L.prev, &tp)) { why = FG_AW_RESET; DEVSTAT(pl.overPrevNot++;) } }
                    }
                    if (why != -2)
                    {
                        L.iRaised = true; L.tIRaised = now;
                        if (L.show)
                        {
                            // A warm-up pair is neither late nor clean for the pad.
                            if (why < 0)
                            {
                                // The lead to the real frame's raise rides in eUs: no generated frame too close to it.
                                const LONGLONG tShow = now > at ? now + wI : L.tI;
                                LONGLONG nAt = nextAt; if (nAt < now) nAt = now;
                                DueWrite(s.dueI, L.p, L.gen, FG_DUE_SHOW, 0, tShow, (LONG)((nAt - tShow) * 1000000 / B::Qpf()));
                                DEVSTAT(pl.iShow++;) if (!L.warm) CapOnTime();
                                B::WakePace();
                            }
                            else
                            {
                                DueWrite(s.dueI, L.p, L.gen, FG_DUE_WITHHOLD, ((DWORD)why << 16) | (why == FG_AW_LATE ? (DWORD)FG_W_LATE : (DWORD)FG_W_NOSOURCE), L.tI);
                                // With the ride, a pair with no handoff stamp does not raise the plan (its lateness is the handoff's), nor a ride pair
                                // whose deadline was the cap.
                                DEVSTAT(pl.iNot[why]++;) if (why == FG_AW_LATE && !L.warm) { CapLate(); if (!(a.k.ride && L.tsOwn) && !(a.k.rideKMaxUs > 0 && (L.hflags & FG_HF_RIDE) && L.planUs > a.k.rideKMaxUs)) ArmedPlanLate(L); }
                            }
                        }
                    }
                }
            }
            // The real frame's due, once, at its time less the service time, never before the generated frame's verdict.
            if (!L.nRaised)
            {
                LONGLONG at = L.tN - wN;
                if (L.iRaised && at < L.tIRaised) at = L.tIRaised;
                if (L.iRaised && now >= at)
                {
                    L.nRaised = true; pl.nRaised++;
                    DueWrite(s.dueN, L.p, L.gen, FG_DUE_FIRE, 0, L.tN, 0);
                    B::WakePace();
                }
                else if (L.iRaised) want(at, spin);
            }
            ArmedKTake(L);
        }
        ArmedInlineK();
        s.pl.nextSpin = next != 0 && spinFor;
        return next;
    }
        // ---- pairs the merged placer does not place (inline, with a 'G') ----
    // Followed until the answer is in, into B::X().pl's histograms. Nothing is raised.
    static void ArmedInlineTake(LONG p, const FgPub& v)
    {
        FgShadowPlacer& pl = B::X().pl;
        if (pl.liveN >= FG_SHADOW_LIVE) { DEVSTAT(pl.liveOver++;) for (int j = 1; j < pl.liveN; j++) pl.live[j - 1] = pl.live[j]; pl.liveN--; }
        FgShadowPair& L = pl.live[pl.liveN++];
        L.p = p; L.n = v.sent; L.kind = v.hold; L.tS = 0; L.tPrep = 0; L.tEntry = v.tEntry;
    }
    static void ArmedInlineK()
    {
        FgState& s = S(); FgShadowPlacer& pl = B::X().pl;
        if (!pl.liveN) return;                                    // nothing followed: no read of the fence
        const ULONGLONG outNow = B::TlValue(FG_TL_OUT);
        for (int i = 0; i < pl.liveN; i++)
        {
            FgShadowPair& L = pl.live[i];
            LONGLONG t = 0;
            if (!L.tS && (L.kind == (LONG)FG_G_DISPATCH || L.kind == (LONG)FG_G_WITHHELD) && ReadStamp(s.st[FG_TL_IN], L.n, &t))
            {
                L.tS = t;
                DEVSTAT(if (L.tEntry && t >= L.tEntry)
                {
                    const LONGLONG eus = (t - L.tEntry) * 1000000 / B::Qpf();
                    int eb = (int)(eus / FG_E_BIN_US); if (eb >= FG_E_BINS) eb = FG_E_BINS - 1;
                    pl.eHist[eb]++; pl.eN++;
                })
            }
            if (!L.tPrep && ReadStamp(s.st[FG_TL_PREP], L.n, &t)) L.tPrep = t;
            LONGLONG tOut = 0; int val = -1;
            if (ReadStamp(s.st[FG_TL_OUT], 2 * L.n, &tOut)) val = 0;
            else if (ReadStamp(s.st[FG_TL_OUT], 2 * L.n + 1, &tOut)) val = 1;
            else if (outNow > 2ull * (ULONGLONG)L.n + 1) val = 2; // answered, and the stamper never saw its value pass
            if (val < 0) continue;
            DEVSTAT(if (val == 0) pl.stampFrame++; else if (val == 1) pl.stampNone++; else pl.stampMissed++;)
            if (val == 0 && L.tS && tOut >= L.tS)
            {
                const LONGLONG us = (tOut - L.tS) * 1000000 / B::Qpf();
                int bin = (int)(us / FG_K_BIN_US); if (bin >= FG_K_BINS) bin = FG_K_BINS - 1;
                pl.kHist[bin]++;   // FgpPlanStep reads this array as fgp.x->pl.kHist: the plan's samples, a decision
                DEVSTAT(pl.kN++; pl.kSumUs += us; if (us > pl.kMaxUs) pl.kMaxUs = us;)
                DEVSTAT(if (L.tPrep > L.tS) { pl.prepAfterIn++; _InterlockedExchangeAdd(&pl.prepBehindUs, (LONG)((L.tPrep - L.tS) * 1000000 / B::Qpf())); })
            }
            for (int j = i + 1; j < pl.liveN; j++) pl.live[j - 1] = pl.live[j];
            pl.liveN--; i--;
        }
    }
        // ---- the pair's room, and the pacer's room floor ----
    // A pair's real frame must fire before the next frame's work reaches the worker, or C1 forces it early with its generated
    // frame. So half a period must hold L_S (entry to handoff retire) + the plan + both service times + the shift + a margin;
    // the room floor is twice that, published in quarter-ms steps (roomUs). By fg_plan.h's law it rises where six of the last
    // 120 pairs need more and comes back where fewer than three need its step; warm-up pairs teach nothing. The pacer clamps
    // to it (ArmedRoomPer, FgArmedKeys::room).
    enum { FG_ROOM_STEP_US = 250 };
        // The room's margin (roomGuardUs), for every pair: it decides whether a pair fits or rides.
    static LONG ArmedRoomMargin(const FgArmedLive& L)
    {
        (void)L;
        return B::A().k.roomGuardUs > 0 ? B::A().k.roomGuardUs : 0;
    }
    static void ArmedRoom(const FgArmedLive& L)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
                // The engage's first stamped pair seeds the floor where the seed is owed.
        if (pl.seedPend && !L.tsOwn && L.tEntry && L.tS >= L.tEntry) ArmedRoomSeed(L, (L.tS - L.tEntry) * 1000000 / B::Qpf());
                // With the ride's measured needs, a pair past FG_RIDE_SKIP teaches; one that does not still takes the floor to the
        // current plan.
        if (!L.teach && a.k.rideKMaxUs > 0 && pl.roomN > 0 && L.planUs > 0)
        {
            const LONG was0 = pl.roomUs, to0 = FgRoomFloor(was0, pl.roomUpUs, pl.roomDownUs, L.planUs + ArmedRoomMargin(L), FG_ROOM_STEP_US);
            if (to0 != was0) { WriteRelease(&pl.roomUs, to0); DEVSTAT(if (to0 > was0) pl.roomUp++; else pl.roomDown++;) }
        }
        if (!L.teach || L.tsOwn || !L.tEntry || L.tS < L.tEntry) return;
        const LONGLONG lsUs = (L.tS - L.tEntry) * 1000000 / B::Qpf();
        if (lsUs > 100000) return;                                // over 0.1 s is a stall, not a room
        LONG up = 0, down = 0;
        FgRoomTake(pl.room, (LONG)lsUs + s.w.wIUs + s.w.wNUs, &up, &down);
        pl.roomN++; pl.roomUpUs = up; pl.roomDownUs = down;
        const LONG was = pl.roomUs, to = FgRoomFloor(was, up, down, L.planUs + ArmedRoomMargin(L), FG_ROOM_STEP_US);
        if (to == was) return;
        WriteRelease(&pl.roomUs, to);
        DEVSTAT(if (to > was) pl.roomUp++; else pl.roomDown++;)
    }
        // A pair timed from the arm teaches the room floor (armTeach) when its late stamp comes: otherwise, with a floor of 0 and a
    // handoff that retires past half a period, every pair is timed from the arm and nothing ever teaches. Same law, same need.
    static void ArmedRoomFromArm(const FgArmedLive& L, LONGLONG tS)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (!L.tEntry || tS < L.tEntry) return;
        const LONGLONG lsUs = (tS - L.tEntry) * 1000000 / B::Qpf();
        if (pl.seedPend) ArmedRoomSeed(L, lsUs);
        if (!ArmedArmTeach() || !L.teach) return;                 // kept for the seed alone: teaches nothing
        if (lsUs > 100000) return;                                // a stall
        LONG up = 0, down = 0;
        FgRoomTake(pl.room, (LONG)lsUs + s.w.wIUs + s.w.wNUs, &up, &down);
        pl.roomN++; pl.roomUpUs = up; pl.roomDownUs = down; DEVSTAT(pl.armTaught++;)
        const LONG was = pl.roomUs, to = FgRoomFloor(was, up, down, L.planUs + ArmedRoomMargin(L), FG_ROOM_STEP_US);
        if (to == was) return;
        WriteRelease(&pl.roomUs, to);
        DEVSTAT(if (to > was) pl.roomUp++; else pl.roomDown++;)
    }
        // The room floor seeded by an engage's first stamped pair (roomSeed): its need raises the floor at once, once an engage,
    // never lowered here (from 0 the six-of-120 law would leave a busy card riding nothing for most of a window). A stall seeds
    // nothing and the seed stays owed.
    static void ArmedRoomSeed(const FgArmedLive& L, LONGLONG lsUs)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (!pl.seedPend || lsUs < 0 || lsUs > 100000 || L.planUs <= 0) return;
        pl.seedPend = 0;
        const LONG was = pl.roomUs, to = FgRoomFloor(was, (LONG)lsUs + s.w.wIUs + s.w.wNUs, 0, L.planUs + ArmedRoomMargin(L), FG_ROOM_STEP_US);
        DEVSTAT(_InterlockedIncrement(&pl.roomSeeded);)
        if (to <= was) return;
        WriteRelease(&pl.roomUs, to);
        DEVSTAT(pl.roomUp++;)
    }
        // Two late answers closer than the plan's quantile allows (fg_plan.h, FgPlanLateSpan): the plan goes up one bin at once.
    // A pair timed with a since-replaced plan is not counted. FgpPlanStep writes the plan too: each by compare and exchange,
    // and the loser leaves it to the other.
    static void ArmedPlanLate(const FgArmedLive& L)
    {
        FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (a.k.planUpPairs <= 0) return;
        const LONG cur = ReadAcquire(&a.k.kPlanUs);
        if (L.planUs != cur) return;
        const bool close = pl.lateP != 0 && L.p - pl.lateP < a.k.planUpPairs;
        pl.lateP = L.p;
        if (!close || cur + a.k.planUpUs > a.k.planMaxUs) return;
        FGC_IF(InterlockedCompareExchange(&a.k.kPlanUs, cur + a.k.planUpUs, cur) == cur, pl.planUps++);
    }
        // The pacer's period given the room: with `room`, never under the room floor and no pad (the pad answers late verdicts,
    // which a longer period does not cure); without, the rig's pad.
    static double ArmedRoomPer(double per)
    {
        FgArmedState& a = B::A();
        if (!a.k.room) return per + (double)InterlockedCompareExchange(&S().sh.padQms, 0, 0) * 0.25;
        const double room = (double)ReadAcquire(&a.pl.roomUs) / 1000.0;
                // A ride: a period under the room floor is not clamped up to it; the pacer keeps the ride's own floor.
        if (ArmedRides((LONG)(per * 1000.0)))
        {
            const double ride = (double)ReadAcquire(&a.pl.rideUs) / 1000.0;
            return per < ride ? (ride < room ? ride : room) : per;
        }
        return per < room ? room : per;
    }
        // ---- the switch-on (FgArmedKeys::ramp) ----
    // fg_law.h, "THE SWITCH-ON'S RAMP", describes the hitch this answers. An engage's first present takes its period from the
    // target (RampStart: the law's prediction or the remembered floors, with the drain's margin), not the panel's floor; the
    // floor (RampPer) holds it while the law warms up, then lets go. None where it would not help (close to the start, the
    // pacer not holding, no law, just after a build, the fixed mode). Every stand-down and every want not ON ends it at once.
    static LONG RampWant(LONG want)
    {
        FgArmedState& a = B::A(); FgArmedRender& ar = a.r;
        if (!a.k.ramp) return want;
        if (want != FG_WANT_ON)
        {
            if (ar.rampSt != FG_RAMP_NONE) { ar.rampSt = FG_RAMP_NONE; ar.rampFloorUs = 0; ar.rampDrain = 0; }
            if (want == FG_WANT_PASS && (a.k.ramp & FG_RAMP_KEY_QREAD)) RampQueueTick();   // the standing-by queue, read at every such present
            return want;
        }
        if (ar.rampSt == FG_RAMP_DRAIN) { if (--ar.rampDrain > 0) return FG_WANT_CAP; ar.rampSt = FG_RAMP_ON; return want; }
        if (ar.rampSt != FG_RAMP_NONE) return want;
        ar.rampSt = FG_RAMP_ON; a.rq.blkAt = 0;
        const bool first = !S().r.wasEngaged;
        if (first && (a.k.ramp & FG_RAMP_KEY_QREAD)) { a.rq.blkAt = RampQueueUs(); a.rq.n = 0; a.rq.cnt = 0; }   // the ring taken at the engage
        const bool target = first && RampStart();
        if (target) WriteRelease(&ar.ramps, ar.ramps + 1);
        else ar.rampFloorUs = 0;
                // With the queue read directly RampDrainN decides the drain, else only behind a target; either way only where a stint's
        // room floor stands.
        if (first && (target || (a.k.ramp & FG_RAMP_KEY_QREAD)) && (a.k.ramp & FG_RAMP_KEY_DRAIN))
        { const LONG n = RampDrainN(); if (n > 0) { ar.rampSt = FG_RAMP_DRAIN; ar.rampDrain = n; return FG_WANT_CAP; } }
        return want;
    }
        // The standing-by queue read directly (FG_RAMP_KEY_QREAD): the worker's time inside DXVK's Present per present (on a
    // variable panel it waits only for the card), into a ring of FG_RAMPQ_N, read at an engage as their median.
    static void RampQueueTick()
    {
        FgRampQ& q = B::A().rq;
        const LONGLONG t = InterlockedCompareExchange64(&dtPresTicksSum, 0, 0);
        const LONG c = (LONG)dtPresDone;
        if (q.cnt && c != q.cnt && t >= q.sumT)
        {
            const LONG n = c - q.cnt;
            q.blk[q.at] = n > 0 ? (LONG)(TicksToMs(t - q.sumT) * 1000.0 / (double)n) : 0;
            q.at = (q.at + 1) % FG_RAMPQ_N; if (q.n < FG_RAMPQ_N) q.n++;
        }
        q.sumT = t; q.cnt = c;
    }
    static LONG RampQueueUs()
    {
        FgRampQ& q = B::A().rq;
        if (q.n < FG_RAMPQ_N / 2) return 0;                       // too few presents standing by to say
        LONG v[FG_RAMPQ_N]; const int k = q.n;
        for (int i = 0; i < k; i++) { const LONG x = q.blk[i]; int j = i; while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; } v[j] = x; }
        return v[k / 2];
    }
        // The queue's drain ahead of the first pair: the game standing by keeps a busy card's queue a frame deep, so the switch's
    // first 2 to 6 presents are the pacer alone (CAP) until it drains. Only where it pays: a stint's room floor, and the card
    // read as the limit (rampDrainOk) or a queue block over FG_RAMP_QPM, FG_RAMP_QMIN_US and enough slack. Never in the fixed mode.
    enum { FG_RAMP_QPM = 150, FG_RAMP_QMIN_US = 1500, FG_RAMP_QSLACK_PM = 50 };
    static LONG RampDrainN()
    {
        FgArmedState& a = B::A(); const FgArmedRender& ar = a.r;
        if (a.k.ramp & FG_RAMP_KEY_QREAD)
        {
            const LONG blk = a.rq.blkAt;                          // taken at this engage's first present
            FgState& s = S(); const FgRenderLine& r = s.r;
            if (ReadAcquire(&a.pl.roomUs) <= 0) return 0;         // the helper's first engage: no drain
            if (r.nPeriods < 4 || PaceOwner() != 1 || ReadAcquire(&r.fixOn)) return 0;
            const double own = (double)PeriodsMedianUs();
            if (own <= 100.0 || (double)blk < own * (double)FG_RAMP_QPM * 0.001 || blk < FG_RAMP_QMIN_US) return 0;
            const double start = CPeriodMs() * 1000.0;
            const double f = ar.rampFloorUs > 0 ? (double)ar.rampFloorUs : (start > own ? start : own);
            if (f - own < own * (double)FG_RAMP_QSLACK_PM * 0.001) return 0;
            // The depth drained is the larger of the block and the game's own frame still queued behind it.
            const LONG n = (LONG)(((double)blk > own ? (double)blk : own) / (f - own) + 0.999) + 1;
            return n < 2 ? 2 : n > 6 ? 6 : n;
        }
        if (!ReadAcquire(&a.k.rampDrainOk) || ReadAcquire(&a.pl.roomUs) <= 0) return 0;
        const double own = (double)PeriodsMedianUs(), f = (double)ar.rampFloorUs;
        if (own <= 100.0 || f <= own) return 6;
        LONG n = (LONG)(own / (f - own) + 0.999) + 1;
        return n < 2 ? 2 : n > 6 ? 6 : n;
    }
        // An engage's first pair never rides (rampFirst): it is a cut (its generated frame never shown), so it has no place to ride
    // for, and riding on the standing-by queue shows one long gap. Its real frame fires at its clock due. Not in the fixed mode.
    static bool RampFirst(DWORD flags)
    {
        return (flags & FG_HF_FIRST) && B::A().k.rampFirst && !ReadAcquire(&S().r.fixOn);
    }
        // An engage's first present: the target and the floor, or none (false).
    static bool RampStart()
    {
        FgState& s = S(); FgRenderLine& r = s.r; FgArmedState& a = B::A(); FgArmedRender& ar = a.r;
        if (r.nPeriods < 4 || PaceOwner() != 1 || ReadAcquire(&r.fixOn)) return false;
        const double own = (double)PeriodsMedianUs() / 1000.0, floorMs = CPeriodMs();
        const double ask = (double)ReadAcquire(&a.k.rampAskUs) / 1000.0, cap = (double)ReadAcquire(&a.k.rampCapUs) / 1000.0;
        if (own <= 0.1 || floorMs <= 0.1) return false;
        const double start = own > floorMs ? own : floorMs;
        double to = ArmedRoomPer(start);
        if (ask > to) to = ask;
        if (to > 40.0) to = 40.0;
        if (to <= start * (1.0 + (double)FG_RAMP_LEAST_PM * 0.001)) return false;
        ar.rampToUs = (LONG)(to * 1000.0);
        ar.rampFloorUs = (LONG)(FgRampFloorMs(to, cap > 0.1 ? cap : 40.0) * 1000.0);
        return true;
    }
        // The period, in the law's place behind the room floor (PaceLaw): never under the switch's floor while it stands.
    static double RampPer(double per)
    {
        FgArmedState& a = B::A(); FgArmedRender& ar = a.r;
        if (!a.k.ramp || ar.rampFloorUs <= 0) return per;
        const double f = (double)ar.rampFloorUs / 1000.0;
        const FgLaw* lw = S().law;
        const bool warm = lw && lw->warm > 0;
        if (per < f) { WriteRelease(&ar.rampHeld, ar.rampHeld + 1); per = f; }
        else if (!warm) { ar.rampFloorUs = 0; return per; }      // the machine's own answer stands over it
        const double g = warm ? FgRampEaseMs(f, (double)ar.rampToUs / 1000.0) : FgRampLetGoMs(f);
        ar.rampFloorUs = g > 0.5 ? (LONG)(g * 1000.0) : 0;
        return per;
    }
        // ---- the real frame rides behind the next frame's upscale ----
    // DXVK has one queue, so a present is shown when the work ahead of it retires. A same-frame body ends with the home copy,
    // which waits for the helper's upscale, so a present right behind the body is shown when the upscale answers. A pair whose
    // period is under the room floor (FG_HF_RIDE) fires its real frame there (ArmedAfterInject); the placer puts the generated
    // frame half a period ahead of that place, and the pacer keeps the ride's floor (ArmedRideTake).
    enum { FG_RIDE_HYST_US = 2000 };
    static bool ArmedRides(LONG perUs)
    {
        FgArmedState& a = B::A();
        if (!a.k.ride || !a.k.room || !FG_ARMED_GUARD_ON(6)) return false;
        const LONG room = ReadAcquire(&a.pl.roomUs);
                // The regime holds FG_RIDE_HYST_US past its edge, so a period hovering at the floor does not flip pair by pair.
        return room > 0 && perUs > 0 && perUs < room + (a.r.ride ? (LONG)FG_RIDE_HYST_US : 0);
    }
        // One ride, measured (placer thread). tS: its handoff's retire; nStart: the worker's clock where the ride's fire began;
    // tEnd: the frame end's stamp, where it was shown. A warm-up pair's needs are not taken; its place is.
    static void ArmedRideTake(const FgArmedLive& L, LONGLONG nStart, LONGLONG tEnd)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (tEnd <= L.tS || nStart <= L.tS) return;
        const LONGLONG q = B::Qpf();
        const LONGLONG dUs = (tEnd - L.tS) * 1000000 / q, rUs = (nStart - L.tS) * 1000000 / q;
        if (dUs > 100000) return;                                 // over 0.1 s behind the handoff is a stall
        WriteRelease(&pl.rideDUs, FgRideTake(pl.rideMem, (LONG)dUs));
        if (a.k.rideKMaxUs > 0) { ArmedRideMeasured(L, nStart, tEnd, (LONG)dUs, (LONG)rUs); return; }
        if (L.warm || L.periodUs <= 0) return;
        const LONG per = L.periodUs, wI = s.w.wIUs;
        LONG up = 0, down = 0;
        FgRoomTake(pl.rideE, per > (LONG)dUs ? per - (LONG)dUs + wI : wI, &up, &down);
        const LONG e = FgRoomFloorK(pl.rideEvenUs, up, down, L.planUs, FG_ROOM_STEP_US, 2);   // no room margin in a ride's floor
        pl.rideNeedE = up; if (e != pl.rideEvenUs) WriteRelease(&pl.rideEvenUs, e);
        FgRoomTake(pl.rideW, dUs > rUs ? (LONG)(dUs - rUs) : 0, &up, &down);
        const LONG w2 = FgRoomFloorK(pl.rideWaitUs, up, down, a.k.rideGuardUs, FG_ROOM_STEP_US, 2);
        pl.rideNeedW = up; if (w2 != pl.rideWaitUs) WriteRelease(&pl.rideWaitUs, w2);
        FgRoomTake(pl.rideF, per > (LONG)rUs ? per - (LONG)rUs + wI : wI, &up, &down);
        const LONG f = FgRoomFloorK(pl.rideFitUs, up, down, L.planUs + a.k.rideGuardUs, FG_ROOM_STEP_US, 1);
        pl.rideNeedF = up; if (f != pl.rideFitUs) WriteRelease(&pl.rideFitUs, f);
        const LONG most = e > w2 ? (e > f ? e : f) : (w2 > f ? w2 : f);
        if (most != pl.rideUs) WriteRelease(&pl.rideUs, most);
        pl.rideN++;
    }
        // ---- the ride's needs are measured, not planned (rideKMaxUs > 0) ----
    // The plan counts for no more than the cap, in the floors and as a ride pair's deadline: slower answers are withheld,
    // never bought with the period. The needs are differences of stamps of pair n's ride and pair n+1:
    // EVEN = n+1's handoff retire - n's frame end, FIT = n+1's handoff retire - n's ride start (each + the service time),
    // WAIT = n's frame end - its ride start. The first FG_RIDE_SKIP pairs of an engage teach nothing, and every measured pair
    // takes each floor to the current plan.
    enum { FG_RIDE_SKIP = 12 };
    // The plan as a ride pair is given it: never over the cap.
    static LONG ArmedRidePlan(LONG planUs) { const LONG cap = B::A().k.rideKMaxUs; return (cap > 0 && planUs > cap) ? cap : planUs; }
        // The three floors from the current statistics and plan (planUs: the plan the measuring pair was timed with), and the
    // ride's floor, their largest.
    static void ArmedRideFloors(LONG planUs)
    {
        FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        const LONG plan = ArmedRidePlan(planUs);
                // No room margin in the ride's floors: the ride fires behind the body, where the margin keeps nothing.
        const LONG e = FgRoomFloorK(pl.rideEvenUs, pl.rideNeedE, pl.rideDownE, plan, FG_ROOM_STEP_US, 2);
        if (e != pl.rideEvenUs) WriteRelease(&pl.rideEvenUs, e);
        const LONG w2 = FgRoomFloorK(pl.rideWaitUs, pl.rideNeedW, pl.rideDownW, a.k.rideGuardUs, FG_ROOM_STEP_US, 2);
        if (w2 != pl.rideWaitUs) WriteRelease(&pl.rideWaitUs, w2);
        const LONG f = FgRoomFloorK(pl.rideFitUs, pl.rideNeedF, pl.rideDownF, plan + a.k.rideGuardUs, FG_ROOM_STEP_US, 1);
        if (f != pl.rideFitUs) WriteRelease(&pl.rideFitUs, f);
                // The wait floor stays at twice the ride's wait: under it the generated frame cannot be placed evenly and the queue grows
        // behind an expensive frame. rideNoWait takes it out for an A/B.
        LONG most = (a.k.rideEarly && a.k.rideNoWait) ? (e > f ? e : f) : e > w2 ? (e > f ? e : f) : (w2 > f ? w2 : f);
        if (a.k.rideFitOnly && ArmedPlaceOn()) most = f;
        if (most != pl.rideUs) WriteRelease(&pl.rideUs, most);
    }
        // A ride of pair L, measured: the WAIT need, and what the pair behind it will take of this one (ArmedRideNext).
    static void ArmedRideMeasured(const FgArmedLive& L, LONGLONG nStart, LONGLONG tEnd, LONG dUs, LONG rUs)
    {
        FgArmedPlacer& pl = B::A().pl;
        pl.rideLastP = L.p; pl.rideLastTeach = L.teach; pl.rideLastEnd = tEnd; pl.rideLastStart = nStart;
        if (L.teach)
        {
            LONG up = 0, down = 0;
            FgRoomTake(pl.rideW, dUs > rUs ? dUs - rUs : 0, &up, &down);
            pl.rideNeedW = up; pl.rideDownW = down;
            pl.rideN++;
        }
        ArmedRideFloors(L.planUs);
    }
        // The pair behind a measured ride, timed by its own handoff's stamp: the EVEN and FIT needs of the ride before it, and the
    // floors to the current plan, whether a need was taken or not.
    static void ArmedRideNext(const FgArmedLive& L)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (a.k.rideKMaxUs <= 0) return;
        const bool mine = pl.rideLastP != 0 && pl.rideLastP == L.p - 1 && !L.tsOwn && pl.rideLastTeach && L.tS > pl.rideLastEnd && L.tS > pl.rideLastStart;
        pl.rideLastP = 0;
        if (mine)
        {
            const LONGLONG q = B::Qpf();
            const LONGLONG postUs = (L.tS - pl.rideLastEnd) * 1000000 / q, fitUs = (L.tS - pl.rideLastStart) * 1000000 / q;
            if (postUs <= 100000 && fitUs <= 100000)              // over 0.1 s is a stall, not a need
            {
                const LONG wI = s.w.wIUs;
                LONG up = 0, down = 0;
                FgRoomTake(pl.rideE, (LONG)postUs + wI, &up, &down); pl.rideNeedE = up; pl.rideDownE = down;
                FgRoomTake(pl.rideF, (LONG)fitUs + wI, &up, &down);  pl.rideNeedF = up; pl.rideDownF = down;
            }
        }
        ArmedRideFloors(L.planUs);
    }

        // ---- the helper's answer on a card that is the limit ----
    // The game's queue outranks the helper's, and one late answer costs three generated frames. Three mends:
    // 1. `over`: copy over one unanswered dispatch where the card's order holds the copy behind it (ArmedOverMay);
    // 2. `card`: raise the helper's scheduling class for the engage (B::CardClass);
    // 3. `ord`: the order wait, a batch behind the hand's submit on a fence of the game's own, so the next frame waits.
    enum { FG_K_STEADY = 12, FG_ORD_NOHAND_US = 40000, FG_OVER_CLEAN = 8 };
    static bool ArmedOverMay(const FgHandPayload& h, LONG sn, int left, bool early, bool realCut)
    {
        FgShadowState& x = B::X(); FgArmedState& a = B::A(); FgArmedWorker& aw = a.w;
        if (!a.k.over || left != 1 || x.w.pend[0] != sn - 1) return false;
        if (!early || x.e.qReasons != 0 || realCut) return false;
        if ((h.flags & FG_HF_FIRST) || !(h.flags & FG_HF_RIDE) || aw.passK != 0) return false;
        if (B::Pipelined() || !B::HomeWaited(sn)) return false;
        if (aw.overClean < FG_OVER_CLEAN && !ArmedPlaceOn()) return false;   // a late answer among late answers; not asked with placing on
        aw.overPrev = x.w.pend[0]; DEVSTAT(aw.overN++;)
        return true;
    }
        // The helper's place on the card: asked at a hand that opened a sent pair (worker), given back on whichever thread ends
    // the engage. One exchange decides who gives it back, so it is given back once.
    static void ArmedCardAsk(const FgHandPayload& h)
    {
        FgArmedState& a = B::A(); FgArmedWorker& aw = a.w;
        if (!a.k.card || aw.cardAsked || !(h.flags & FG_HF_RIDE)) return;
        aw.cardAsked = 1; DEVSTAT(aw.cardAsks++;)
        _InterlockedExchange(&aw.cardUp, 1);
        B::CardClass(1);
    }
    static void ArmedCardDown() { FgArmedWorker& aw = B::A().w; if (_InterlockedExchange(&aw.cardUp, 0)) B::CardClass(0); }
        // Signals the order's own fence up to n. Any thread. The binding's signal is asked first and the mark moved second, so
    // nobody reads "released" of a fence not yet signalled; a second signal of the same value does nothing.
    // Returns true when this call moved the mark.
    static bool ArmedOrdRelease(LONG n)
    {
        FgArmedWorker& aw = B::A().w;
        LONG cur = ReadAcquire(&aw.ordSig);
        if (cur >= n) return false;
        B::OrdSignal(n);
        while (cur < n)
        {
            const LONG was = InterlockedCompareExchange(&aw.ordSig, n, cur);
            if (was == cur) return true;
            cur = was;
        }
        return false;
    }
        // ---- the order wait, per pair ----
    // The wait stands on every sourced ride pair of an engage, and each is released by whoever comes first, nothing of it
    // outliving the pair: the worker at the generated frame's fire, the answer's stamper, the placer at the answer or the
    // deadline, the worker ahead of an early show or of any present, the next hand, a stop.
    // ArmedOrdWants: a sourced ride pair, not a HUD-less pass, with ord > 0. The engage's first is kept for the helper line.
    static bool ArmedOrdWants(const FgHandPayload& h)
    {
        FgArmedState& a = B::A();
        if (!(h.flags & FG_HF_RIDE) || a.w.passK != 0) return false;
        if (a.k.ord == 0) return false;
        if (!ReadAcquire(&a.w.ordFirstP)) WriteRelease(&a.w.ordFirstP, h.p > 0 ? h.p : 1);
        return true;
    }
        // The wait itself, on the worker, under the submission lock right behind the hand's submit, so nothing of the next frame
    // gets between. An earlier one still standing is released first. The top is stored before the submit, so a release in
    // between still signals past it. A refused submit is counted and the mark moved.
    static bool ArmedOrdWait(LONG n)
    {
        FgArmedWorker& aw = B::A().w;
        const LONG top = aw.ordTop;
        FGC_IF(top && ArmedOrdRelease(top), aw.ordRelHand++);
        InterlockedExchange64(&aw.ordAt, B::Now());
        WriteRelease(&aw.ordTop, n);
        if (!B::OrdWait(n)) { DEVSTAT(aw.ordRefused++;) ArmedOrdRelease(n); return false; }
        DEVSTAT(aw.ordWaits++;)
        return true;
    }
        // Ahead of every present the worker makes: a wait still standing is released now, so at most one present ever stands
    // behind an order wait.
    static void ArmedOrdBeforePresent()
    {
        FgArmedWorker& aw = B::A().w;
        FGC_IF(aw.ordPrevTop && ArmedOrdRelease(aw.ordPrevTop), aw.ordRelPresent++);
        aw.ordPrevTop = aw.ordTop;
    }
        // Where the machine stops, dies, is disarmed or meets a Reset or a build, on any thread: nothing stands.
    static void ArmedOrdStop()
    {
        FgArmedWorker& aw = B::A().w;
        const LONG top = ReadAcquire(&aw.ordTop);
        FGC_IF(top && ArmedOrdRelease(top), _InterlockedIncrement(&aw.ordRelStop));
    }
        // The placer's part, once a step: the wait is released when the answer is in (fence at or past 2n), else at the deadline:
    // ordMaxUs behind the handoff's retire by its stamp, or behind the first sight of a retire with no stamp, or
    // FG_ORD_NOHAND_US more where the handoff has not retired. Returns when to look again (0: nothing stands).
    static LONGLONG ArmedOrdWatch(LONGLONG now)
    {
        FgState& s = S(); FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        const LONG top = ReadAcquire(&a.w.ordTop);
        if (!top || ReadAcquire(&a.w.ordSig) >= top) return 0;
        if (B::TlValue(FG_TL_OUT) >= 2ull * (ULONGLONG)(ULONG)top)
        {
            if (ArmedOrdRelease(top))
            {
                DEVSTAT(pl.ordRelAnswer++;)
                LONGLONG ts = 0;
                if (ReadStamp(s.st[FG_TL_OUT], 2 * top, &ts) || ReadStamp(s.st[FG_TL_OUT], 2 * top + 1, &ts)) ArmedOrdLag(now - ts);
            }
            return 0;
        }
        if (pl.ordSeenN != top) { pl.ordSeenN = top; pl.ordSeenAt = now; pl.ordInAt = 0; }
        LONGLONG t = 0, due = 0;
        if (ReadStamp(s.st[FG_TL_IN], top, &t)) due = t + UsToTicks(a.k.ordMaxUs);
        else if (B::TlValue(FG_TL_IN) >= (ULONGLONG)(ULONG)top) { if (!pl.ordInAt) pl.ordInAt = now; due = pl.ordInAt + UsToTicks(a.k.ordMaxUs); }
        else due = pl.ordSeenAt + UsToTicks(a.k.ordMaxUs) + UsToTicks(FG_ORD_NOHAND_US);
        if (now < due) return due;
        FGC_IF(ArmedOrdRelease(top), pl.ordRelDeadline++);
        return 0;
    }
        // The release's lag behind the answer: from the answer's stamp to the release. Any thread.
    static void ArmedOrdLag(LONGLONG ticks)
    {
        FgArmedPlacer& pl = B::A().pl;
        const LONG us = ticks > 0 ? (LONG)(ticks * 1000000 / B::Qpf()) : 0;
        int bin = us / 50; if (bin > 15) bin = 15;
        _InterlockedIncrement(&pl.ordLag[bin]);
        LONG cur = ReadAcquire(&pl.ordLagMax);
        while (us > cur) { const LONG was = InterlockedCompareExchange(&pl.ordLagMax, us, cur); if (was == cur) break; cur = was; }
    }
        // The answer's stamper releases the order wait (ordAtStamp) behind its stamp of c, for a wait at or under c / 2.
    // The release is the CPU's, never a wait on a helper's value.
    static void ArmedOrdAtStamp(LONG c, LONGLONG t)
    {
        FgArmedState& a = B::A(); FgArmedPlacer& pl = a.pl;
        if (!a.k.ordAtStamp) return;
        const LONG top = ReadAcquire(&a.w.ordTop);
        if (!top || ReadAcquire(&a.w.ordSig) >= top || (ULONGLONG)(ULONG)c < 2ull * (ULONGLONG)(ULONG)top) return;
        if (!ArmedOrdRelease(top)) return;
        DEVSTAT(_InterlockedIncrement(&pl.ordRelStamp);)
        ArmedOrdLag(B::Now() - t);
    }

        // Before a device Reset, on the Reset's thread. `ahead`: the barrier stood aside, so queued notes would run after DXVK's
    // Reset: the machine goes dead first. Then a span in flight is waited out (1 s) and the binding's GPU work (2 s): DXVK's
    // Reset releases back buffers with no GPU idle, and the binding's command buffers name them. RetireWait asks only for
    // values a submit took. On a time-out: dead for the session, images left alone. Returns bit 0: dead for an ahead Reset;
    // bit 1: timed out.
    static int BeforeReset(bool ahead, const char* how)
    {
        FgState& s = S();
        int r = 0;
        if (ahead)
        {
            char why[160];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "a Reset went ahead of the device thread's queue (the barrier: %s)", how ? how : "?");
            Kill(why);
            r |= 1;
        }
                // The order wait is released before anything here waits: the retire wait may stand behind it. The engage ends here.
        if constexpr (B::kWire) ArmedStopped();
        const LONGLONG t0 = B::Now();
        while (InterlockedCompareExchange(&s.sh.fireBusy, 0, 0) && B::Now() - t0 < B::Qpf()) Sleep(0);
        if (!B::RetireWait(2000))
        {
            Kill(Named("the rig's GPU work did not retire within 2 s before a Reset; its images are left alone for the session",
                       "its GPU work did not retire within 2 s before a Reset; its images are left alone for the session"));
            r |= 2;
        }
        return r;
    }
