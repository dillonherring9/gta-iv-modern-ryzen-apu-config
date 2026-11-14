// fg_machine_render.h: FgMachine's render thread (FgSwapPresent, FgPace).
// Included in place inside FgMachine's body by fg_machine.h: the text is part of that class.

        // ---- the render thread: FgSwapPresent ----
    static HRESULT __stdcall SwapPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
    {
        FgState& s = S();
        typedef HRESULT(__stdcall* Orig_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
        const Orig_t orig = (Orig_t)s.ptr.origSwapPresent;
        LONGLONG tEntry = B::Now();
                // Stray callers pass through: only the render thread, recording, on the device's own swapchain.
        if (__readfsdword(0x24) != B::RenderTid() || !DtRec() || !DtOnDev<DTC_SC>(sc)) return orig(sc, src, dst, wnd, dirty, flags);
        FgRenderLine& r = s.r;
        ULONG64 cycEntry = 0; if constexpr (B::kRig) QueryThreadCycleTime(GetCurrentThread(), &cycEntry);   // render-thread CPU a frame (rig only)
                // Step 1: the last present this thread recorded was engaged, so its pair may still be open on the worker. Ring order makes
        // the drain right whatever the worker has done (FgWorkerPresent(k) precedes this note). With nothing open it is a no-op.
        if constexpr (B::kWire) B::A().r.lastWas = r.lastEngaged ? 1 : 0;   // the close note below asks this once the hand is decided
        if (r.lastEngaged)
        {
            FgFirePayload fp = { FG_PT_D, r.lastP, r.lastGen };
            FGC_IF(!DtNote(DT_NOTE_FG_FIRE, &fp, sizeof(fp)), s.c.noteRefused++);
            r.lastEngaged = false;
        }
                // The binding's render-thread tick, after step 1's drain note, so a drain the binding makes here replays that note first
        // and never holds a pending N past its fire.
        B::RenderTick();
        // Re-read after every note: a note can answer a disarm ask and run the disarm tail on this very thread.
        const LONG st = s.sh.state;
        const LONG gen = dtArmCount;
                // "Was engaged" does not survive a re-arm (merged path only). While the layer is unarmed this function returns at its
        // first test, so `wasEngaged` still describes the last armed present, and the first present after a re-arm would miss its
        // FIRST flag (no cut, no warm-up pairs, no placer restart). Not restarted here: the ten period samples, and FgPace.
        if constexpr (B::kWire) { if (r.wasEngaged && r.lastGen != gen) r.wasEngaged = false; }
                // Step 2, the want. The waiter writes it; PASS while parked or not LIVE.
        LONG want = s.wt.want;
        if (st != FG_LIVE || r.parked || !DtRec()) want = FG_WANT_PASS;
                // On the wire: engaged only while this build's set is held and nothing forbids it (read every present, so the stand-down
        // is immediate when a rebuild is owed), and not for a set frame generation ended itself for.
        if constexpr (B::kWire) { if (want != FG_WANT_PASS && (!B::MayEngage() || B::X().sh.setDead == B::X().sh.bgen + 1)) want = FG_WANT_PASS; }
                // The switch-on (RampWant): ON may become CAP for the drain's presents; any other want ends the switch at once.
        if constexpr (B::kWire) want = RampWant(want);
                // Engaged: this present records a hand (merged path: ON). CAP paces and records none.
        const bool engaged = WantsWire(want);
        if (engaged && (!r.wasEngaged || ++r.checkN >= 300))
        {
            r.checkN = 0;
            if (const char* which = Unchained()) { B::Line(Named("  fg rig: unchained: %s is not the rig's (a second device?)", "  frame generation: unchained: %s is not its own (a second device?)"), which); Kill(Named("a pointer the rig installed was replaced", "a pointer it installed was replaced")); want = FG_WANT_PASS; }
        }
                // Step 3, the facts, carried in FG_HAND's payload.
        const LONG fEnd = B::Frames() - 1;
        const LONG cam = B::CamJumps(), enc = B::HdrEncodes();
        LONGLONG interval = r.lastEntry ? tEntry - r.lastEntry : 0;
        r.lastEntry = tEntry;
        if (interval > 0 && interval < B::Qpf() / 10) { r.periods[r.iPeriod] = interval; r.iPeriod = (r.iPeriod + 1) % 10; if (r.nPeriods < 10) r.nPeriods++; }
        DWORD withhold = 0;
        if (engaged)
        {
            if (!r.wasEngaged) { r.sinceEngage = 0; r.lastCam = cam - 1; }         // the first pair after any gap is a cut
            if (cam != r.lastCam) withhold |= FG_W_CUT;
            if (B::HdrOn() && enc == r.lastEnc) withhold |= FG_W_NOENCODE;
            if (B::HdrView()) withhold |= FG_W_VIEW;
            if (r.nPeriods < 4) withhold |= FG_W_SAMPLES;
            const double applied = (double)r.appliedUs / 1000.0;
            if (applied > 0.1 && interval > 0 && TicksToMs(interval) > applied * 1.5) withhold |= FG_W_HITCH;
            r.lastCam = cam;
        }
        r.lastEnc = enc;
        // The applied period the placer halves, published once a present.
        PublishApplied();
        const LONG ordinal = dtPresRecN + 1;
        bool hand = false;
                // Step 4's question, before the hand: what this frame has for a HUD-less picture. It rides the hand's flags, so the worker
        // knows before its span.
        if constexpr (B::kWire) { FgArmedRender& ar = B::A().r; ar.hudDrawn = 0; ar.hud = (engaged && B::HasHudless()) ? B::HudWant() : FG_HUD_SAME; }
        if (engaged)
        {
            FgHandPayload h = {};
            h.k = ordinal; h.gen = gen; h.p = r.nextP; h.fEnd = fEnd; h.withhold = withhold; h.tEntry = tEntry;
            h.flags = (r.sinceEngage < 150 ? FG_HF_WARM : 0u)
                    | (r.sinceEngage == 0 ? FG_HF_FIRST : 0u)
                    | (r.paceOwner == 1 ? (r.regime == 1 ? FG_HF_REG1 : r.regime == 2 ? FG_HF_REG2 : 0u) : 0u);
            if constexpr (B::kWire) { const LONG hud = B::A().r.hud; h.flags |= hud == FG_HUD_PASS ? FG_HF_HUDPASS : hud == FG_HUD_TEX ? FG_HF_HUDTEX : hud == FG_HUD_NONE ? FG_HF_NOHUD : hud == FG_HUD_WIDE ? (FG_HF_NOHUD | FG_HF_HUDWIDE) : 0u; }
            // The ride is decided from the period this hand carries, so the flag and the period the placer halves are one fact.
            if constexpr (B::kWire) { FgArmedRender& ar = B::A().r; ar.ride = (want == FG_WANT_ON && !RampFirst(h.flags) && ArmedRides(r.periodUs)) ? 1 : 0; if (ar.ride) { h.flags |= FG_HF_RIDE; WriteRelease(&ar.rideN, ar.rideN + 1); } }
            // The wire's hand carries the build generation in `hold`.
            if constexpr (B::kWire) h.hold = B::X().sh.bgen;
            h.periodUs = r.periodUs;
            if (DtNote(DT_NOTE_FG_HAND, &h, sizeof(h)))
            {
                hand = true;
                s.c.hands++;
                r.lastP = h.p; r.lastGen = gen; r.lastEngaged = true; r.nextP++; r.sinceEngage++;
                OwnWrite(fEnd, 1, h.p);
            }
            else { DEVSTAT(s.c.noteRefused++;) if constexpr (!kLean) OwnWrite(fEnd, 0, 0); }
        }
                // No hand: "not owned" is written, except in the lean shape (OwnsEnd also checks the slot's frame number, so an unwritten
        // slot still reads not owned: frames only count up).
        else { if constexpr (!kLean) OwnWrite(fEnd, 0, 0); }
        r.wasEngaged = engaged;
                // Step 4, the HUD-less pass, behind a recorded hand that asked for it. HOLD has the real frame before these draws replay,
        // and step 5's note puts it back.
        if constexpr (B::kWire) { if (hand && B::A().r.hud == FG_HUD_PASS) { B::HudDraw(); B::A().r.hudDrawn = 1; } }
                // Step 5's note.
        if (hand)
        {
            FgPairPayload pp = { ordinal, gen, r.lastP };
            if (!DtNote(DT_NOTE_FG_HUDLESS, &pp, sizeof(pp)))
            {
                DEVSTAT(s.c.noteRefused++;)
                // REFUSED WITH THE HUD-LESS PICTURE DRAWN (a disarm between hand and here): the binding puts the real picture
                // back itself, before the Present below.
                if constexpr (B::kWire) { if (B::A().r.hudDrawn) { B::A().r.hudDrawn = 0; B::HudUndo(); } }
            }
        }
                // The merged path's close note, at the first present after an engaged one that has no hand. Step 1's drain note cannot do
        // it (recorded before the hand is decided, and an early 'Q' must stay open for a hand that follows): this closes an early
        // 'Q' (its 'G' closed), a pair whose Present never matched, and what a disarm owed.
        if constexpr (B::kWire)
        {
            if (!hand && B::A().r.lastWas)
            {
                FgFirePayload fp = { FG_FIRE_CLOSE, r.lastP, r.lastGen };
                FGC_IF(!DtNote(DT_NOTE_FG_FIRE, &fp, sizeof(fp)), s.c.noteRefused++);
            }
        }
                // Step 6, the lean merged shape's tail. Not paced (standby): the original present only; FgPace is told once that it
        // stopped, so the next engage starts at its floor. Paced (ON, CAP): the time inside the original present (FgPace's sample
        // leaves it out), FgPace, and the sleeper count. The instrument's records stay behind kRig.
        if constexpr (!B::kRig)
        {
            if (!(WantsPace(want, engaged) && s.sh.state == FG_LIVE))
            {
                if (r.paceRunning) { r.paceRunning = false; r.prevPostSleep = 0; r.regime = 0; }
                (void)cycEntry; return orig(sc, src, dst, wnd, dirty, flags);
            }
            DEVSTAT(const LONG sleptBefore = B::PaceSleptN();)
            const LONGLONG t0 = B::Now();
            const HRESULT hr = orig(sc, src, dst, wnd, dirty, flags);
            const LONGLONG inOrig = B::Now() - t0;
            DEVSTAT(r.paceSlept = 0;)
            Pace(inOrig);
            if (!r.paceRunning) r.regime = 0;
            DEVSTAT(const LONG sleeps = (B::PaceSleptN() - sleptBefore) + (r.paceSlept ? 1 : 0);)
            DEVSTAT(if (sleeps <= 0) r.sleeps0++; else if (sleeps == 1) r.sleeps1++; else r.sleeps2++;)
            (void)cycEntry; return hr;
        }
        else
        {
        const LONG sleptBefore = B::PaceSleptN();
        const LONGLONG t0 = B::Now();
        const HRESULT hr = orig(sc, src, dst, wnd, dirty, flags);
        const LONGLONG inOrig = B::Now() - t0;
        s.c.paceWaitN++;
                // Step 7, the one sleep: FgPace. The sleeper count is taken inside each sleeper, never from the handover flags.
        r.paceSlept = 0; r.lastFreeUs = -1; r.lastLateMax = 0; r.lastDue = 0; r.lastPb = 0; r.lawEUs = 0; r.lawLUs = 0;
        const LONGLONG tp0 = B::Now();
        if (WantsPace(want, engaged) && s.sh.state == FG_LIVE) Pace(inOrig);
        else { r.paceRunning = false; r.prevPostSleep = B::Now(); }
        if (!r.paceRunning) r.regime = 0;                         // no regime while FgPace does not run
        const LONGLONG paceT = B::Now() - tp0;
        const LONG sleeps = (B::PaceSleptN() - sleptBefore) + (r.paceSlept ? 1 : 0);
        if (sleeps <= 0) r.sleeps0++; else if (sleeps == 1) r.sleeps1++; else r.sleeps2++;
                // This present's wait, the entry's cycle count and FgPace's figures (after FgPace).
        PresWrite(fEnd, cycEntry, inOrig, paceT, sleeps, tEntry);
        return hr;
        }
    }
        // Before a rebuild: a drain is not a fire point, so a pending N would sit out the build. The drain note goes in first and
    // the build's drain replays it.
    static void BeforeBuild()
    {
        FgState& s = S(); FgRenderLine& r = s.r;
        if (!r.lastEngaged || __readfsdword(0x24) != B::RenderTid()) return;
                // The merged path marks it as a build's: it spares nothing.
        FgFirePayload fp = { (B::kWire) ? (LONG)FG_FIRE_BUILD : (LONG)FG_PT_D, r.lastP, r.lastGen };
        FGC_IF(!DtNote(DT_NOTE_FG_FIRE, &fp, sizeof(fp)), s.c.noteRefused++);
        r.lastEngaged = false;
    }
    static void PresWrite(LONG f, ULONGLONG cyc, LONGLONG inOrig, LONGLONG paceT, LONG sleeps, LONGLONG tEntry = 0)
    {
        FgState& s = S(); const FgRenderLine& r = s.r;
        FgPresRec& e = s.pres[f & (FG_STAMPS - 1)];
        WriteRelease(&e.f1, -1);
        e.cycles = cyc;
        e.freeUs = r.lastFreeUs; e.inOrigUs = (LONG)(TicksToMs(inOrig) * 1000.0); e.sleepUs = (LONG)(TicksToMs(paceT) * 1000.0);
        e.lateUs = (LONG)(TicksToMs(r.lastLateMax) * 1000.0); e.sleeps = sleeps;
        e.tEntry = tEntry; e.due = r.lastDue; e.tPost = r.prevPostSleep; e.periodUs = (LONG)(r.period * 1000.0);
        e.pflags = r.lastPb; e.lawEUs = r.lawEUs; e.lawLUs = r.lawLUs;
        WriteRelease(&e.f2, f); WriteRelease(&e.f1, f);
    }
    static void OwnWrite(LONG f, LONG owned, LONG p)
    {
        FgOwn& e = S().own[f & (FG_STAMPS - 1)];
        WriteRelease(&e.f1, -1); e.owned = owned; e.p = p; WriteRelease(&e.f2, f); WriteRelease(&e.f1, f);
    }
        // The applied period: FgPace's while it owns pacing (the max of this and the previous period: a move is not a hitch),
    // FusionFix's while it owns, else the ten-sample median.
    static void PublishApplied()
    {
        FgState& s = S(); FgRenderLine& r = s.r;
        double ms = 0.0;
        const int owner = PaceOwner();
        r.paceOwner = owner;
        if (owner == 1 && r.period > 0.1) ms = r.period > r.prevPeriod ? r.period : r.prevPeriod;
        else if (owner == 2 && B::FfCap() > 0) ms = 1000.0 / (double)B::FfCap();
        else if (r.nPeriods > 0)
        {
            LONGLONG v[10]; int n = r.nPeriods;
            for (int i = 0; i < n; i++) v[i] = r.periods[i];
            for (int i = 1; i < n; i++) { LONGLONG x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; }
            ms = TicksToMs(v[n / 2]);
        }
        r.appliedUs = (LONG)(ms * 1000.0);
                // The period in force: FgPace's current one (it moves after the present), not the max, which stays in appliedUs for HITCH.
        r.periodUs = (owner == 1 && r.period > 0.1) ? (LONG)(r.period * 1000.0) : r.appliedUs;
                // Where the game is slower than the pacer (slowN: two presents in a row past their due): the median of the last ten
        // intervals, never under FgPace's, so a pair is not placed at half of a shorter period.
        if constexpr (B::kWire)
        {
            FgArmedRender& ar = B::A().r;
            if (FG_ARMED_GUARD_ON(4) && owner == 1 && ar.slowN >= 2 && r.nPeriods >= 4)
            {
                const LONG own = PeriodsMedianUs();
                if (own > r.periodUs) { r.periodUs = own; WriteRelease(&ar.ownN, ar.ownN + 1); }
                if (own > r.appliedUs) r.appliedUs = own;
            }
        }
    }
    // The median of the last ten present-to-present intervals, in microseconds.
    static LONG PeriodsMedianUs()
    {
        const FgRenderLine& r = S().r;
        LONGLONG v[10]; const int n = r.nPeriods;
        if (n <= 0) return 0;
        for (int i = 0; i < n; i++) v[i] = r.periods[i];
        for (int i = 1; i < n; i++) { LONGLONG x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; }
        return (LONG)(TicksToMs(v[n / 2]) * 1000.0);
    }
        // THE FREE BAND'S FORM (fg_law.h has why): head 1.0. Guard bit 12 off is the harness's control: the older law at 1.06.
    static bool LawForm() { return FG_ARMED_GUARD_ON(12); }
        // Who may sleep: 0 nobody, 1 FgPace, 2 FusionFix, 3 ambiguous.
    static int PaceOwner()
    {
        const double cp = CPeriodMs();
        const double cFps = cp > 0.1 ? 1000.0 / cp : 0.0;
        const int f = B::FfCap();
        if (B::Limiter() && f > 0 && cFps > 0.0)
        {
            if ((double)f <= cFps) return 2;
            if ((double)f <= 1.03 * cFps) return 3;
        }
        return cFps > 0.0 ? 1 : 0;
    }

        // ---- FgPace: the pacing law and the due ----
    // Steps 2 and 3 are the pacing law (fg_law.h). Steps 1 and 4: the sample excludes the time inside the original present,
    // the sleep is the mod's own timer loop woken by the placer, capped at 20 ms (equiv_fgpace.cpp holds them).
    // Fixed-refresh mode: two refreshes a pair, held a hair fast; the block inside DXVK's Present is tracked to its median and
    // held at FG_FIX_TARGET_US by a bounded PI correction in ppm every FG_FIX_WIN presents.
    static void PaceFixedRestart(FgRenderLine& r, LONG on)
    {
        r.fixSince = 0; r.fixWin = 0; r.fixMedUs = 0; r.fixCard = 0;
        if (!FG_ARMED_GUARD_ON(15)) r.fixIPpm = (r.fixIPpm + FG_FIX_PPM0) / 2;
        r.fixITrust = (SHORT)r.fixIPpm;                              // what the run-in starts from: the card's wait restores it
        if (on != 2) r.fixPpm = r.fixIPpm;
    }
    static void PaceFixed(LONGLONG tInOrig)
    {
        FgState& s = S(); FgRenderLine& r = s.r;
        const ULONGLONG pk = B::RefreshPacked();
        const LONG pkFold = (LONG)(ULONG)(pk ^ (pk >> 32) ^ (pk >> 17));
        if (r.fixRefresh != pkFold) { r.fixRefresh = pkFold; r.fixPpm = FG_FIX_PPM0; r.fixIPpm = FG_FIX_PPM0; r.fixITrust = FG_FIX_PPM0; r.fixWins = 0; r.fixCard = 0; }
        const LONG on = ReadAcquire(&r.fixOn);
        if (on == 2) { r.fixPpm = 0; r.fixIPpm = 0; }
        if (r.due == 0)                                              // an engage's first step: Pace's start put the due at 0
        {
            if (FG_ARMED_GUARD_ON(13)) PaceFixedRestart(r, on); else { r.fixSince = 0; r.fixWin = 0; r.fixMedUs = 0; }
            r.fixLateAt = (SHORT)(r.clamps + r.resyncs);
        }
                // The hold a present: the worker's time inside DXVK's Present since the last paced present, per present, less its own.
        // Only presents behind a real frame count (presTicksR, presRN): a real frame's present behind a generated one waits in
        // DXVK's frame latency for the generated frame's queued work (3 to 4.5 ms against 5 us for the generated frame's), which no
        // period mends. A real frame behind a real one is read: near the refresh it carries most of the FIFO's hold.
        // Bit 10 off: every present.
        const bool split = FG_ARMED_GUARD_ON(10);
        const LONGLONG pt = split ? InterlockedCompareExchange64(&s.sh.presTicksR, 0, 0) : InterlockedCompareExchange64(&dtPresTicksSum, 0, 0);
        const LONG np = split ? s.sh.presRN : s.c.presentsDxvk;
                // Also the render thread's own wait inside the original present past FG_FIX_INORIG_US: a queue a refresh too deep holds
        // the render thread there, and the presents' hold alone would read it dry.
        const LONG inWait = FG_ARMED_GUARD_ON(13) ? (LONG)(TicksToMs(tInOrig) * 1000.0) - FG_FIX_INORIG_US : 0;
        if (r.fixSince > 0 && pt >= r.fixPresT && (np != r.fixPresN || inWait > 0))
        {
            const LONG n = np - r.fixPresN;
            LONG blk = n > 0 ? (LONG)(TicksToMs(pt - r.fixPresT) * 1000.0 / (double)n) - ReadAcquire(&r.fixBaseUs) : 0;
            if (blk < 0) blk = 0;
            if (inWait > 0) blk += inWait;
            r.fixLastBlkUs = blk;
            if (FG_ARMED_GUARD_ON(13) && (double)blk > RefreshMs() * 1000.0 * FG_FIX_STALL_REFRESHES) { PaceFixedRestart(r, on); r.fixStalls++; }
            else
            {
                // Step a sixteenth of the gap, bounded, so a stall's deep queue is read drained quickly.
                LONG gap = blk - r.fixMedUs; if (gap < 0) gap = -gap;
                LONG st = FG_ARMED_GUARD_ON(13) ? gap / 16 : 0; if (st < FG_FIX_TRACK_US) st = FG_FIX_TRACK_US; if (st > 10 * FG_FIX_TRACK_US) st = 10 * FG_FIX_TRACK_US; if (st > gap) st = gap;
                r.fixMedUs += blk > r.fixMedUs ? st : -st;
                if (r.fixMedUs < 0) r.fixMedUs = 0;
            }
        }
        r.fixPresT = pt; r.fixPresN = np;
        if (r.fixSince < 30000) r.fixSince++;
        if (++r.fixWin >= FG_FIX_WIN)
        {
            r.fixWin = 0;
            const SHORT lateAt = (SHORT)(r.clamps + r.resyncs);
            const LONG late = (SHORT)(lateAt - r.fixLateAt);
            r.fixLateAt = lateAt;
            if (on != 2 && r.fixSince > 2 * FG_FIX_WIN)
            {
                const LONG e = r.fixMedUs - FG_FIX_TARGET_US;
                LONG want;
                // A median under one tracker step is no wait: jitter about the base is no block.
                const bool blind = r.fixMedUs < FG_FIX_TRACK_US;
                // The card's wait (FgFixCardStep): a block two windows at the cap is the card's, not the queue's, so the correction drops
                // to its integral, which learns nothing then and is put back to fixITrust (else it ratchets toward the cap). Meanwhile the
                // correction is no faster than the refresh, or the FIFO fills behind the card. Bits 11, 16, 18 turn these off.
                int cardRun = r.fixCard & 3;
                const int wasCard = (r.fixCard >> 2) & 1;
                const int card = FG_ARMED_GUARD_ON(11) ? FgFixCardStep(cardRun, wasCard, r.fixPpm, r.fixMedUs, r.fixPrevMedUs) : 0;
                r.fixCard = (CHAR)(cardRun | (card << 2));
                r.fixPrevMedUs = (USHORT)(r.fixMedUs < 65535 ? r.fixMedUs : 65535);
                if (card && !wasCard && FG_ARMED_GUARD_ON(16)) r.fixIPpm = r.fixITrust;
                if (card) { want = (FG_ARMED_GUARD_ON(18) && r.fixIPpm < 0) ? 0 : r.fixIPpm; r.fixPpm = want; }
                else if (FG_ARMED_GUARD_ON(13) && blind && late >= FG_FIX_LATE_MIN) { want = r.fixIPpm; r.fixHeld++; }   // the game set the pace: nothing to learn
                else
                {
                    // The integral learns slowly far from the target: such an error is a queue the proportional half drains.
                    LONG di = (!FG_ARMED_GUARD_ON(13) || (e <= FG_FIX_LEARN_US && e >= -FG_FIX_LEARN_US)) ? e / 10 : (e > 0 ? FG_FIX_STEP_PPM / 20 : -FG_FIX_STEP_PPM / 20);
                    if (di > FG_FIX_STEP_PPM / 10) di = FG_FIX_STEP_PPM / 10; if (di < -FG_FIX_STEP_PPM / 10) di = -FG_FIX_STEP_PPM / 10;
                    LONG in = r.fixIPpm + di;
                    // A blind window teaches only toward faster, never past FG_FIX_BLIND_I_PPM. Bit 14 off: none.
                    if (blind && di < 0)
                    {
                        if (!FG_ARMED_GUARD_ON(14)) in = r.fixIPpm;
                        else if (in < FG_FIX_BLIND_I_PPM) in = r.fixIPpm < FG_FIX_BLIND_I_PPM ? r.fixIPpm : (LONG)FG_FIX_BLIND_I_PPM;
                    }
                    if (in > FG_FIX_MAX_I_PPM) in = FG_FIX_MAX_I_PPM; if (in < -FG_FIX_MAX_I_PPM) in = -FG_FIX_MAX_I_PPM;
                    r.fixIPpm = in;
                    if (e <= FG_FIX_LEARN_US && e >= -FG_FIX_LEARN_US) r.fixITrust = (SHORT)in;   // a window the integral learnt from
                    want = in + e / 2;
                }
                LONG d = want - r.fixPpm;
                if (d > FG_FIX_STEP_PPM) d = FG_FIX_STEP_PPM; if (d < -FG_FIX_STEP_PPM) d = -FG_FIX_STEP_PPM;
                LONG to = r.fixPpm + d;
                if (to > FG_FIX_MAX_PPM) to = FG_FIX_MAX_PPM; if (to < -FG_FIX_MAX_PPM) to = -FG_FIX_MAX_PPM;
                r.fixPpm = to;
                r.fixWins++;
            }
        }
                // The period in force: two refreshes by the correction, whatever the law asked (its samples go on).
        const double per = CPeriodMs();
        DEVSTAT(if (per != r.period) r.moves++;)
        r.prevPeriod = per; r.period = per;
        r.regime = 0;
    }
        // Steps 2 and 3, the law (fg_law.h). On the merged path the room floor and then the switch-on's floor apply to the law's
    // answer; the pad on every other binding. The applied period keeps the old value for the step of a move alone. The regime
    // rides the hand with the period. The law is given exactly what the row records (whole-us sample, the entry's time), so a
    // run replays through its reference unchanged (tools/perfproof/replay_pd8.py); no clock read is added. Its edges are the
    // floor's: no hold band and the free range from the floor (FgLawFloor derives them again when the floor moves).
    static void PaceLaw(double sample, double floorMs)
    {
        FgState& s = S(); FgRenderLine& r = s.r; FgLaw& w = *s.law;
        FgLawFloor(w, floorMs);
        const bool warm = w.warm > 0;
        const double c = sample > 0.0 ? (double)(LONG)(sample * 1000.0) / 1000.0 : 0.0;
        const double t = (double)r.lastEntry * 1000.0 / (double)B::Qpf();
                // LawForm off is the harness's control: the older law at 1.06.
        double per = LawForm() ? FgLawStepForm(w, c, t) : FgLawStep(w, c, t);
                // Merged path: the room floor in the pad's place, then the switch-on's floor; otherwise the pad.
        if constexpr (B::kWire) { per = ArmedRoomPer(per); per = RampPer(per); }
        else per += (double)InterlockedCompareExchange(&s.sh.padQms, 0, 0) * 0.25;
        if (per > 40.0) per = 40.0;
        if (per != r.period) { r.prevPeriod = r.period; r.period = per; DEVSTAT(r.moves++;) DEVSTAT(r.lastPb |= FG_PB_MOVED;) }
        else r.prevPeriod = r.period;
        r.regime = w.regime;
        DEVSTAT(if (warm) r.lastPb |= FG_PB_WARM;)
        DEVSTAT(if (w.hitch) r.lastPb |= FG_PB_HITCH;)
        DEVSTAT(r.lastPb |= r.regime == 1 ? FG_PB_REG1 : r.regime == 2 ? FG_PB_REG2 : 0;)
        r.lawEUs = (LONG)(w.E * 1000.0 + 0.5); r.lawLUs = (LONG)(w.L * 1000.0 + 0.5);   // whole microseconds: they are samples
    }
    static void Pace(LONGLONG tInOrig)
    {
        FgState& s = S(); FgRenderLine& r = s.r;
        const LONGLONG qpf = B::Qpf();
        const LONGLONG now = B::Now();
        const double floorMs = CPeriodMs();
        DEVSTAT(r.lastDue = 0; r.lastPb = 0;)                                 // for the export's B row
        if (floorMs <= 0.1) { r.prevPostSleep = now; return; }
                // An engage starts at the floor with the law's 150 steps of warm-up, each time FgPace starts running: no hold band, the
        // free range from the floor.
        if (!r.paceRunning || r.period <= 0.1)
        {
            r.paceRunning = true; r.period = floorMs; r.prevPeriod = floorMs; r.due = 0;
            r.regime = 0;
            if constexpr (B::kWire) { B::A().r.slowN = 0; B::A().r.ride = 0; }
            FgLawReset(*s.law, floorMs, 0.0, 0.0, LawForm() ? 1.0 : 1.06);
        }
                // 1. The game's own cost, the time inside the original present excluded.
        double sample = 0.0;                                         // 0.2 to 100 ms taken
        if (r.prevPostSleep)
        {
            const double freeMs = (double)((now - r.prevPostSleep) - tInOrig) * 1000.0 / (double)qpf;
            sample = freeMs;
            DEVSTAT(if (freeMs > 0.2 && freeMs < 100.0) r.lastFreeUs = (LONG)(freeMs * 1000.0);)   // the instrument's record
        }
                // 2 and 3, the law.
        PaceLaw(sample, floorMs);
                // Fixed-refresh mode: two refreshes whatever the law asked.
        if constexpr (B::kWire) { if (ReadAcquire(&r.fixOn)) PaceFixed(tInOrig); }
                // 4. The due, no banking: both resync branches set due = now; resync only picks the counter.
        const int owner = PaceOwner();
        if constexpr (B::kWire)
        {   // The game slower than the pacer, for PublishApplied: this present came past the due the step before gave
            // it; 0 at a first step, a present FgPace does not own, or one it will sleep behind.
            LONG& slow = B::A().r.slowN;
            slow = (owner == 1 && r.due != 0 && now > r.due) ? (slow < 0x10000 ? slow + 1 : slow) : 0;
        }
        if (owner != 1) { r.due = 0; r.prevPostSleep = B::Now(); return; }   // FusionFix owns, or nobody: samples only
        DEVSTAT(r.lastPb |= FG_PB_OWNER;)
        const LONGLONG step = (LONGLONG)(r.period * (double)qpf / 1000.0);
        const LONGLONG resync = (step * 4 < qpf * 12 / 100) ? step * 4 : qpf * 12 / 100;
        if (r.due == 0 || now > r.due + resync) { r.due = now; r.resyncs++; DEVSTAT(r.lastPb |= FG_PB_RESYNC;) }
        else if (now > r.due) { r.due = now; r.clamps++; DEVSTAT(r.lastPb |= FG_PB_CLAMP;) }
        else if (r.due - now > qpf / 20000)
        {
            DEVSTAT(r.lastDue = r.due;)                                       // export's B row: the instant this sleep was given
                                                                     // No banking: a sleep the 20 ms cap cut short restarts the due from the cap's end (carrying the rest made a debt that grew
            // every present). SleepUntil says where the cap ended, so no clock read is added (equiv_fgpace counts them).
            const LONGLONG capEnd = SleepUntil(r.due); DEVSTAT(r.paceSlept = 1; r.lastPb |= FG_PB_SLEPT;)
            if (capEnd) { r.due = capEnd; DEVSTAT(r.capShort++;) DEVSTAT(r.lastPb |= FG_PB_CAPPED;) }
        }
        DEVSTAT(if (!r.lastDue) r.lastDue = r.due;)                           // no sleep: a resync, a clamp, or under 50 us left
        r.due += step;
        r.prevPostSleep = B::Now();
    }
        // The mod's own timer (never latPaceTimer), recording FG_FIRE(PLACER) on each wake, capped at 20 ms. The timer is set
    // 0.6 ms short (it wakes up to 0.43 ms late) and the rest is spun; not set under 0.3 ms. Returns where the cap ended the
    // sleep, or 0 when it ran to its target.
    static LONGLONG SleepUntil(LONGLONG due)
    {
        FgState& s = S(); FgRenderLine& r = s.r;
        const LONGLONG start = B::Now();
                // The 20 ms cap: the device thread's Reset barrier takes a render thread silent for 25 ms as idle and disarms under it.
        const LONGLONG cap = start + B::Qpf() / 50;
        const LONGLONG until = due < cap ? due : cap;
        const LONGLONG slack = UsToTicks(600);
        for (;;)
        {
            const LONGLONG timerDue = until - slack;
            if (timerDue - B::Now() <= UsToTicks(300)) break;   // what is left is spun
            const int woke = B::PaceWait(timerDue);
            if (woke)
            {
                DEVSTAT(r.wakes++;)
                if (s.sh.state == FG_LIVE || s.sh.state == FG_CLOSING)
                {
                    FgFirePayload fp = { FG_PT_B, r.lastP, r.lastGen };
                    if (DtNote(DT_NOTE_FG_FIRE, &fp, sizeof(fp))) DtPublishFenced(); DEVSTAT(else s.c.noteRefused++;)
                }
                continue;
            }
            DEVSTAT(const LONGLONG late = B::Now() - timerDue;)
            DEVSTAT(r.timerN++; r.timerLateSum += late; if (late > r.timerLateWorst) r.timerLateWorst = late;)
            DEVSTAT(if (late > r.lastLateMax) r.lastLateMax = late;)                 // this present's worst
            DEVSTAT(if (late > slack) { r.timerPastSlack++; r.lastPb |= FG_PB_PASTSLACK; })
            break;
        }
                // The spin hears the placer's wake (B::PacePoll): each turn polls the pace event, so a due raised in the last 0.6 ms is
        // answered at once. Costs about 0.35 ms of one core a paced frame. Wine: a wait with a timeout of 0 is a plain call there.
        for (int i = 0; i < 400000 && B::Now() < until; i++)
        {
            if constexpr (B::kWire && FgHasPacePoll<B>::value)
            {
                if (B::PacePoll())
                {
                    DEVSTAT(r.wakes++;) _InterlockedIncrement(&B::A().r.spinWakes);
                    if (s.sh.state == FG_LIVE || s.sh.state == FG_CLOSING)
                    {
                        FgFirePayload fp = { FG_PT_B, r.lastP, r.lastGen };
                        if (DtNote(DT_NOTE_FG_FIRE, &fp, sizeof(fp))) DtPublishFenced(); DEVSTAT(else s.c.noteRefused++;)
                    }
                }
            }
            YieldProcessor();
        }
        return due > cap ? cap : 0;
    }
