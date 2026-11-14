// fg_product.h: FgP, the product's base binding for the frame generation machine; FgPA (fg_arm.h) derives from it.
// Included by fg_client.h. Every seam that talks to DXVK opens DT_DIRECT (tools/check-dt.py). Only a failed pipe write
// stops the transport; any other failure turns frame generation off with one line. No function-local static and no
// const table (they would move .bss or .rdata).
#pragma once

// The back buffer this build's set was made for, and its layout at the last fresh query (FgPA::BBImage).
struct FgpVk
{
    VkImageLayout   bbLayout;           // the back buffer's layout at the last fresh query
    UINT            w, h; VkFormat fmt; // the back buffer this build's set was made for
};
// What the binding keeps beside the machine's state, in the same allocation.
struct FgpSide
{
    FgpVk    v;
        // The worker's own facts for 'Q' (the injection's note gives the first two; the hand's entry times the third).
    float    camNear, fovY;
    LONGLONG prevTEntry;
        // tools\check-fg-pipe.py holds the pipe writers' rule from the source.
    // Also from the injection's note, the worker's own: the frame's jitter in pixels, which 'Q' carries, and whether that
    // frame ran pipelined (asyncHome): 'Q''s PIPELINED bit.
    float    jitX, jitY;
    LONG     asyncHome;
};
struct FgpLine
{
    FgState*       s;                   // null until the install: every entry point returns on it
    FgShadowState* x;
    FgpSide*       side;
    void*          mem;
    HANDLE         placerEv, runEv, quitEv, thr[4];   // the placer and the first three stampers; the frame end's is FgpArm::thrEnd
    volatile LONG  quit;
    LONG           tried;
    LONG           dtHands;             // the render thread's: the hands recorded when the device thread's report last asked
    LONG           quiet;               // DllMain's detach has begun: no line of the machine's goes through the C runtime's stream
};
DLSSIV_FGDATA static FgpLine fgp;

struct FgP;
static void FgpDead(const char* why);   // the machine goes dead with one line

// ---- the binding ----
struct FgP
{
        // The three traits are FgPA's (fg_arm.h); this base says none.
    // State and clock.
    static FgState&       S() { return *fgp.s; }
    static FgShadowState& X() { return *fgp.x; }
    static LONGLONG Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
    static LONGLONG Qpf() { return latQpf; }
    static void Line(const char* fmt, ...)
    {
                // Not in DllMain's detach: the log's stream, or a lock a killed thread still holds, would wait for ever under the
        // loader's lock.
        if (fgp.quiet) return;
        char b[512]; va_list ap; va_start(ap, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap); va_end(ap);
        LogRecord("%s", b);
    }
        // Render-thread facts.
    static LONG Frames()     { return frames; }
    static LONG CamJumps()   { return statCamJumps; }
    static LONG HdrEncodes() { return (LONG)hdrEncodes; }
    static bool HdrOn()      { return hdrEngaged; }
    static bool HdrView()    { return hdrEngaged && IVT_HDR_VIEW != 0; }
    static ULONGLONG RefreshPacked() { return (ULONGLONG)__iso_volatile_load64((const volatile __int64*)&hdrRefreshPacked); }
    static int  FfCap()      { return latFfCap; }
    static bool Limiter()    { return latLimiter; }
    static void RenderTick() {}
        // May this present be engaged: a set of this build is held, imported and laid out; the injection runs on the worker (on
    // the render thread the sent count would run ahead of a worker a frame behind, and "this very frame was sent" would be
    // wrong); the transport lives. The worker reads it too, behind an injection's body, as a reason not to write that
    // frame's 'Q' early: a stale answer costs at most one prepare. Force-inlined into both readers.
    static __forceinline bool MayEngage()
    {
        const FgxClient* c = fgx.c;
        return fgx.live != 0 && c && c->laid && !c->inFlight && ivtCfg.inject_worker == 2 && !ivtFailed;
    }
        // Worker facts.
    static LONG SentN() { return (LONG)ivtSentN; }
    static LONG LatOrd(LONG n) { return latOrd[n & (LAT_RING - 1)]; }
        // The injection's note, seen ahead of its body: the two values of its frame message that 'Q' sends again, its jitter and
    // whether the frame runs pipelined. Copied out by bytes: its dwords are not aligned for the struct.
    static void InjectSeen(const DWORD* payload, DWORD bytes)
    {
        if (bytes != sizeof(IvtInjectCmd)) return;
        FgpSide& d = *fgp.side;
        memcpy(&d.camNear, (const BYTE*)payload + offsetof(IvtInjectCmd, cameraNear), sizeof(float));
        memcpy(&d.fovY,    (const BYTE*)payload + offsetof(IvtInjectCmd, fovYRad),    sizeof(float));
        memcpy(&d.jitX,    (const BYTE*)payload + offsetof(IvtInjectCmd, jitterX),    sizeof(float));
        memcpy(&d.jitY,    (const BYTE*)payload + offsetof(IvtInjectCmd, jitterY),    sizeof(float));
        d.asyncHome = *((const BYTE*)payload + offsetof(IvtInjectCmd, asyncHome)) ? 1 : 0;
    }
        // Worker spans. The machine owns their order; these only perform. (The command ring's seams are FgPA's: fg_arm.h.)
    static void DevLock()   { DT_DIRECT(); ivtIDev->LockDevice(); }      // the machine's Lock is the only caller (the single-lock rule)
    static void DevUnlock() { DT_DIRECT(); ivtIDev->UnlockDevice(); }
    static void Flush()     { DT_DIRECT(); ivtIDev->FlushRenderingCommands(); }
    static void QLock()     { DT_DIRECT(); ivtIDev->LockSubmissionQueue(); }
    static void QUnlock()   { DT_DIRECT(); ivtIDev->ReleaseSubmissionQueue(); }
    // Pipelined: what the last injection's note said of its frame (asyncHome).
    static bool Pipelined() { return fgp.side->asyncHome != 0; }
        // QAtHand: a development A/B that wrote 'Q' at the hand. False in every build: the machine writes 'Q' behind the injection's
    // body and that branch folds away.
    static bool QAtHand() { return false; }
        // Does the set in hand have a HUD-less image? The import is the fact: a set asked with FEED_FG_BUILD_NO_HUDLESS has none
    // (fg_xport.h, FgxAcquire), and the hand's span then copies PRESENT alone. No state of its own, so it cannot disagree
    // with what the helper made.
    static bool HasHudless() { return fgx.c->set.img[FEED_FG_HUDLESS] != VK_NULL_HANDLE; }
        // Timelines: the helper's three fences, imported once a session and never destroyed, so their readers take no lock.
    // Their value and wait are FgPA's (fg_arm.h).
    static VkSemaphore TlSem(int tl)
    {
        const FgxSet& set = fgx.c->set;
        return tl == FG_TL_IN ? set.semIn : tl == FG_TL_PREP ? set.semPrep : tl == FG_TL_OUT ? set.semOut : VK_NULL_HANDLE;
    }
        // Pointers outside the device thread.
    static void*& OrigSwapPresentSlot() { return reinterpret_cast<void*&>(origSwapPresent); }
        // DXVK's bare Present for the machine's install. The transport puts the release's gate before DXVK's Present with a thunk
    // in dtScPresentO (transport/present_gate.h, IvtGateWrap). The machine gates its presents itself (PresentGate), so its
    // install gets DXVK's own pointer and the thunk is out of the chain: one gate before one present. A pointer that is not
    // the thunk comes back as it is.
    static void* DxvkBare(void* p) { return (p == (void*)&IvtGatedPresent && ivtGateDxvkPresent) ? ivtGateDxvkPresent : p; }
    static DWORD RenderTid() { return renderTid; }
        // The pipe (fg_xport.h's lines over the mod's binding). A failed write stops the transport, as any message's does: its
    // pipe is broken or out of step.
    static void WriteQ(LONG n, DWORD flags)
    {
        FgpSide& d = *fgp.side;
                // The frame's time, from the two presents' entries the hands carried. 'Q' is written behind the injection's body, before
        // this frame's hand, so the time sent is one frame old; the pair behind an engage's first sends 0 ("not known"). AMD's
        // interpolation does not read the value, so no clock is read here for it.
        const LONGLONG te = S().w.openTEntry;
        float ms = 0.0f;
        if (d.prevTEntry && te > d.prevTEntry) { const double x = (double)(te - d.prevTEntry) * 1000.0 / (double)Qpf(); if (x > 0.0 && x < 1000.0) ms = (float)x; }
        d.prevTEntry = te;
                // The machine's reset reasons (bits 0 to 3), PIPELINED from the last injection's note (this frame's), the note's jitter,
        // and no camera (null: the identity).
        const uint32_t qf = (flags & FEED_FG_Q_RESET_MASK) | (d.asyncHome ? FEED_FG_Q_PIPELINED : 0u);
        if (!FgxWriteQ<FgxB>(&fgx.c->waits, (uint64_t)(ULONG)n, qf, ms, d.camNear, d.fovY, d.jitX, d.jitY, nullptr))
            IvtFail("frame generation's message to the host could not be written (the pipe is broken)");
    }
        // kind: FG_G_* in bits 0 to 1 and the reset's reasons shifted left by FG_G_RESET_SHIFT (the wire's layout, asserted below).
    static void WriteG(LONG n, DWORD kind)
    {
        if (!FgxWriteG<FgxB>((uint64_t)(ULONG)n, kind, hdrEngaged ? hdrPeak : 0.0f))
            IvtFail("frame generation's message to the host could not be written (the pipe is broken)");
    }
        // The one GPU wait on a helper's value (fg_xport.h, FgxPrepWait): a batch with no command buffer behind fgPrep, on the
    // worker, before the injection's body. Its release is wired into the transport's stop and its watch.
    static bool PrepGuard(LONG n) { return FgxPrepWait<FgxB>(&fgx.c->set, &fgx.c->waits, (uint64_t)(ULONG)n); }
        // The gate before DXVK's Present (fg_xport.h explains the freeze; the core is IvtGateCore in transport/present_gate.h,
    // on the one state ivtGate, so the machine's first present is held to the need the thunk stored last). Called only by
    // WorkerPresent, on the presenting thread with no lock held.
    // The out-fence's need is the last frame sent, less one when it ran pipelined (latOrdAsync): a pipelined frame queued its
    // injection behind n - 1. Only under vsync (the freeze needs DXVK's FIFO wait); with IMMEDIATE nothing is held and both
    // stored needs go back to 0. After the transport's stop the needs are 0 and nothing is read.
    static void PresentGate()
    {
        FgxClient* c = fgx.c;
        if (!c || !FeedVkHasTimelineQueries(&ivtVk) || ivtSemOut == VK_NULL_HANDLE) return;
        if (!latVsync)
        {   // free-running: no look, and no need left behind (a store only when one is there)
            if (ivtGate.prevOut) ivtGate.prevOut = 0;
            if (ivtGate.prevPrep) ivtGate.prevPrep = 0;
            return;
        }
        const bool stopped = ivtFailed;
        const uint64_t sent = stopped ? 0 : (uint64_t)ivtSentN;
        const uint64_t sentOut = sent ? sent - (latOrdAsync[sent & (LAT_RING - 1)] ? 1u : 0u) : 0;   // what the last batch queued behind the out-fence waits for
        const uint64_t prepTop = stopped ? 0 : (uint64_t)(ULONG)c->waits.prepTop;                     // and the highest value a batch waits behind fgPrep for
        const uint64_t needOut = (uint64_t)(ULONG)ivtGate.prevOut, needPrep = (uint64_t)(ULONG)ivtGate.prevPrep;
        const LONG gave0 = ivtGate.gaveUp;
        const DWORD ms = IvtGateCore(&ivtGate, &ivtVk, ivtSemOut, sentOut, c->set.semPrep, prepTop, FgxB::GateDown());
        if (ms >= 250)
            Line("  frame generation: present held %lu ms for the host (frame fence %llu of %llu, prepare fence %llu of %llu)%s", (unsigned long)ms,
                 (unsigned long long)FeedVkTimelineValue(&ivtVk, ivtSemOut), (unsigned long long)needOut,
                 (unsigned long long)(c->set.semPrep ? FeedVkTimelineValue(&ivtVk, c->set.semPrep) : 0), (unsigned long long)needPrep, ivtGate.gaveUp != gave0 ? " GAVE UP" : "");
    }
        // Events.
    static void WakePlacer() { if (fgp.placerEv) SetEvent(fgp.placerEv); }
        // The worker's own time in the three notes: measured only in a development build; here no clock is read.
    static LONGLONG WorkT() { return 0; }
    static void WorkAdd(LONGLONG t0) { (void)t0; }
};
// The binding that arms (fg_arm.h): FgPA, the one binding the product is compiled over.
#include "fg_arm.h"
typedef FgMachine<FgPA> FgPM;
static_assert(FG_IMG_PRESENT == FEED_FG_PRESENT && FG_IMG_GEN == FEED_FG_GEN && FG_IMG_HUDLESS == FEED_FG_HUDLESS, "the machine's image order is the set's (feed_ipc.h)");
static_assert(FG_G_DISPATCH == FEED_FG_G_DISPATCH && FG_G_CLOSED == FEED_FG_G_CLOSED && FG_G_NOSOURCE == FEED_FG_G_NOSOURCE && FG_G_WITHHELD == FEED_FG_G_WITHHELD,
              "the machine's kinds of 'G' are the wire's (feed_ipc.h)");
static_assert(FG_Q_CUT == FEED_FG_Q_RESET_CUT && FG_Q_BUILD == FEED_FG_Q_RESET_BUILD && FG_Q_DEVICE == FEED_FG_Q_RESET_DEVICE && FG_Q_GAP == FEED_FG_Q_RESET_GAP,
              "the machine's reset reasons are the wire's (feed_ipc.h)");
static_assert(FG_G_RESET_SHIFT == FEED_FG_G_RESET_SHIFT && ((FG_Q_CUT | FG_Q_BUILD | FG_Q_DEVICE | FG_Q_GAP) << FG_G_RESET_SHIFT) == FEED_FG_G_RESET_MASK,
              "the machine puts the reset's reasons on 'G' where the wire reads them (feed_ipc.h, wire 2)");
static_assert(FG_G_COUNT_SHIFT == FEED_FG_G_GEN_SHIFT && FG_G_COUNT_MASK == FEED_FG_G_GEN_MASK && !(FG_G_COUNTED & (FEED_FG_G_KIND_MASK | FEED_FG_G_RESET_MASK | FEED_FG_G_GEN_MASK))
              && FG_IMG_GEN2 == FEED_FG_GEN2, "the machine's count is the wire's (feed_ipc.h, wire 3), and its mark is none of the wire's bits");
static_assert(FG_Q_SET1 == FEED_FG_Q_SET1 && FG_G_SET1 == FEED_FG_G_SET1 && !(FG_G_SET1 & FG_G_COUNTED),
              "the machine's second input set's bits are the wire's, and not its own mark (feed_ipc.h, THE SECOND INPUT SET)");

static void FgpDead(const char* why) { FgPM::Kill(why); }

// The armed product's life (fg_arm_life.h): its ring, threads, HOLD and SAVE, the conditions, the plan, and the entry
// points the transport calls through FGA(...). That file is included at this file's end and the functions named here are
// declared here: the compiler pools string literals in the order it first sees them, so including it earlier moves
// .rdata in other builds.
static bool FgpArmInstall(FgState* s);
static void FgpArmThreadsJoin();
static void FgpArmAtBuild(FgxClient& c, unsigned w, unsigned h, int bbVkFormat);
static void FgpArmRetired();
static void FgpArmWant(LONG want);     // the want's one seam in a binding that arms

// ---- the threads' stop ----
// The quit flag and its event; the armed binding's five threads joined (FgpArmThreadsJoin: up to 600 ms), but never on
// the worker (the transport's stop can run there), where they are only told.
static void FgpThreadsStop(bool mayJoin)
{
    _InterlockedExchange(&fgp.quit, 1);
    if (fgp.quitEv) SetEvent(fgp.quitEv);
    if (!mayJoin) return;
    FgpArmThreadsJoin();
}

// ---- the install ----
// IvtBuild's tail (FgxAfterBuild), in its drained scope, with a set just acquired and imported: the install, once a
// session whatever happens (the once-flag first), and at every build this build's back buffer and the images' first
// layout (FgpArmAtBuild). It never fails the build: a failure here turns frame generation off with one line.
static void FgpAfterAcquire(FgxClient& c, unsigned w, unsigned h, int bbVkFormat)
{
    if (!fgp.tried)
    {
        fgp.tried = 1;
                // The one allocation: the machine's state, the product's own, this binding's side, the armed state (FgpArm), then the
        // machine's rings on their own 64-byte lines. Zeroed by VirtualAlloc; kept for the session (observers name it).
        const SIZE_T head = (sizeof(FgState) + 63) & ~(SIZE_T)63, xb = (sizeof(FgShadowState) + 63) & ~(SIZE_T)63, sb = (sizeof(FgpSide) + 63) & ~(SIZE_T)63;
        const SIZE_T ab = (sizeof(FgpArm) + 63) & ~(SIZE_T)63;
        const SIZE_T bytes = head + xb + sb + ab + FgStateBytes();
        void* mem = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mem) { LogRecord("  frame generation: off for this session: its memory (%lu bytes) could not be allocated (error %lu)", (unsigned long)bytes, GetLastError()); return; }
        _Analysis_assume_(bytes >= sizeof(FgState));
        FgState* s = ::new (mem) FgState();
        FgStateAttach(*s, (BYTE*)mem + head + xb + sb + ab);
        s->wt.want = FG_WANT_PASS;                               // standby until a driver says otherwise
        fgp.mem = mem;
        fgp.x = (FgShadowState*)((BYTE*)mem + head);
        fgp.side = (FgpSide*)((BYTE*)mem + head + xb);
                // The binding that arms: fg_vklines.h's ring of 32, the placer's numbers, five threads on fg_threads.h's loops.
        if (!FgpArmInstall(s)) return;
    }
    if (!fgp.s) return;
    FgpVk& v = fgp.side->v;
    v.w = w; v.h = h; v.fmt = (VkFormat)bbVkFormat;
        // The conditions asked again, HOLD and SAVE for this back buffer, and the first layout that ends with the release.
    FgpArmAtBuild(c, w, h, bbVkFormat);
}
// IvtBuild's head (FgxBeforeRelease), in its drained scope, before the set's imports go: no pair stays open across it,
// what a disarm left owed is written, the build generation moves (BeforeRelease), and the queue's copies have retired.
// Returns false when they have not within 2 s: frame generation is dead and the caller forgets the images unreleased.
// noinline: inlined, it would change IvtBuild's compiled code, which is held to its last reading.
__declspec(noinline) static bool FgpBeforeRelease()
{
    if (!fgp.s) return true;
    FgPM::BeforeRelease();
        // The ring's work has retired, so a late first layout has too, and its set may be destroyed like any other.
    if (FgPA::RetireWait(2000)) { FgpArmRetired(); return true; }
    FgpDead("its copies did not retire within 2 s before a build; this set's images are left to the device");
    return false;
}
// The session's frames line, in every build that arms: real frames shown, generated frames shown, and real frames dropped
// (FgCensus::nDropped). Cumulative worker counts read as they stand: the line can be one short, never torn. Said only
// where frame generation engaged, and once (the first stop says it). At exit it goes through the raw handle under the
// log's mutex, as LogSessionOver: the C runtime's stream may wait for ever there.
DLSSIV_FGDATA static LONG fgpFramesSaid;
static void FgpFramesLine(const char* when, bool atExit)
{
    if (!fgp.s || logLevel < 1 || !logPath[0]) return;
    const LONG real = fgp.s->c.presentsN, gen = fgp.s->c.presentsI, dropped = fgp.s->c.nDropped;
    if (!real && !gen && !dropped) return;
    if (InterlockedExchange(&fgpFramesSaid, 1) != 0) return;
    char b[200];
    int n = _snprintf_s(b, sizeof(b) - 2, _TRUNCATE, "  frame generation: the session's frames %s: real frames shown %ld, generated frames shown %ld, real frames dropped %ld",
                        when, (long)real, (long)gen, (long)dropped);
    if (n <= 0) return;
    if (!atExit) { LogRecord("%s", b); return; }
    b[n++] = '\r'; b[n++] = '\n';
    const DWORD w = logMutex ? WaitForSingleObject(logMutex, 200) : WAIT_FAILED;
    const HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD done = 0; WriteFile(h, b, (DWORD)n, &done, nullptr); CloseHandle(h); }
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ReleaseMutex(logMutex);
}
// A stop: the transport's (IvtFail, any thread, the worker included) and the shutdown's. Flags only: the machine closes,
// pipe writes are refused, no new wait is queued, the threads are told to quit. Nothing is joined here: IvtFail releases
// the game's queued waits first and joins afterwards (FgpJoin); the shutdown runs under the loader's lock and never joins.
// atExit: DllMain's detach, where nothing may go through the C runtime's stream. The count is printed after the flag.
// The shipped build's fixed-mode line at exit (FgpFixSayExit) follows the frames line.
#if !DLSSIV_DEV
static void FgpFixSayExit();
#endif
static void FgpStop(const char* why, bool atExit)
{
    if (!fgp.s) return;
    if (atExit) fgp.quiet = 1;
    FgPM::Stop(why);
    FgpFramesLine(atExit ? "at the exit" : "at the stop", atExit);
#if !DLSSIV_DEV
    if (atExit) FgpFixSayExit();
#endif
    FgpThreadsStop(false);
}
// IvtFail, after it has released the game's queued waits: the threads joined (600 ms), never on the worker.
static void FgpJoin()
{
    if (fgp.s && __readfsdword(0x24) != dtWorkerTid) FgpThreadsStop(true);
}
// The device thread's report (DtReport, render thread, every 600 armed frames) asks how many presents since the last were
// engaged: its "threaded=0 may be better" advice cannot judge a window where the worker also ran frame generation, so it
// is withheld. The line saying so is printed once a session.
static LONG FgpDtWindow()
{
    const FgState* s = fgp.s;
    if (!s) return 0;
    const LONG h = s->c.hands, n = h - fgp.dtHands;
    fgp.dtHands = h;
    if (n && FgxSayOnce2(FGX_SAID2_DT)) LogRecord("  threaded: frame generation was engaged on %ld present(s) of this window: the threaded=0 advice is not given for it, nor for any later window in which frame generation is engaged (said once a session)", (long)n);
    return n;
}
// Just before the game's Reset reaches DXVK, after the device thread's barrier (hkReset): BeforeReset.
static void FgpOnResetBarrier(const char* how)
{
    if (!fgp.s) return;
    FgPM::BeforeReset(dtArmed != 0, how);
}


// Included last so that no string literal it shares with this file is seen here first (see the declarations above).
#include "fg_arm_life.h"
