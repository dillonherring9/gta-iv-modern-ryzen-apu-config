// Frame generation's machine, pure logic over a binding B whose static functions are every seam to DXVK, Vulkan, the transport
// and the latency instrument, so the same lines run in the mod and in the device-thread harness (tools/perfproof/dt_fg_test.cpp),
// which asserts the seams' order. The state is one struct the binding owns (B::S()), in DLSSIV_FGDATA so .bss does not move.
// docs/FG-MACHINE-NOTES.md holds the longer rationales. The file is in parts included in place, in this order:
// fg_machine_state.h at file scope, then inside FgMachine fg_machine_render.h, _worker.h, _armed.h and _placer.h.
// tools/fg_machine_text.py pastes them back for tools that read the machine by path.
#pragma once
#include "fg_law.h"                                  // the pacing law
#include "fg_plan.h"                                 // the planned generation time and the room floor's law

// ---- the vocabulary

// Here so dt_core.h is untouched.
enum : DWORD { DT_NOTE_FG_HAND = 5, DT_NOTE_FG_HUDLESS = 6, DT_NOTE_FG_FIRE = 7 };
static_assert(DT_NOTE_FG_HAND > DT_NOTE_INJECT && DT_NOTE_FG_FIRE < DT_NOTE_N, "frame generation's notes sit between the mod's and DT_NOTE_N");

enum FgWant  { FG_WANT_PASS = 0, FG_WANT_ON = 1, FG_WANT_CAP = 4 };
// LIVE runs the machine; CLOSING (a stop) arms nothing new and fires an open pair's N once; DEAD (a Reset barrier, a failed
// pointer check, a ring timeout) passes everything through.
enum FgRigState { FG_NONE = 0, FG_LIVE = 1, FG_CLOSING = 2, FG_DEAD = 3 };

// The fire points, and the reasons a point forces N.
enum FgPoint { FG_PT_A = 0, FG_PT_B = 1, FG_PT_C = 2, FG_PT_D = 3, FG_PT_E = 4, FG_PT_HANDOPEN = 5, FG_PT_CLOSING = 6, FG_PT_INLINE = 7, FG_PT_N };

// Every FG_HAND the worker executes is published exactly once, in p order.
enum FgPubKind { FG_PUB_NONE = 0, FG_PUB_ARMED = 1, FG_PUB_INLINE = 2, FG_PUB_CLOSED = 3 };
enum FgInlineWhy { FG_IN_NOSOURCE = 0, FG_IN_NOHUDLESS = 1, FG_IN_CLOSING = 2, FG_IN_PARKED = 3, FG_IN_N };
// Why a pair went NOSOURCE (the first cause): the pair before unanswered at its hand; no command buffer.
enum FgNosourceWhy { FG_NS_UNANSWERED = 0, FG_NS_NOBUFFER = 1, FG_NS_N };
enum FgClosedWhy { FG_CL_UNARMED = 0, FG_CL_STALE = 1, FG_CL_DEAD = 2, FG_CL_DISARM = 3, FG_CL_N };

// Why a generated frame is withheld, a bit each; the census counts the first reason in this order.
enum FgWhy : DWORD
{
    FG_W_CUT = 1u << 0, FG_W_HITCH = 1u << 1, FG_W_NOSOURCE = 1u << 2, FG_W_NOINJECT = 1u << 3, FG_W_NOENCODE = 1u << 4,
    FG_W_VIEW = 1u << 5, FG_W_SAMPLES = 1u << 6, FG_W_LATE = 1u << 7, FG_W_SPACING = 1u << 8, FG_W_EARLY = 1u << 9,
    FG_W_DRAIN = 1u << 10, FG_W_HANDOPEN = 1u << 11, FG_W_CLOSING = 1u << 12, FG_W_NFIRST = 1u << 13, FG_W_NOBUFFER = 1u << 14,
    FG_W_N = 15
};
// Facts decided on the render thread before the pair exists.
static const DWORD FG_W_RENDER_FACTS = FG_W_CUT | FG_W_HITCH | FG_W_NOENCODE | FG_W_VIEW | FG_W_SAMPLES;

// The bandwidth floor of one copy in us: a tenth of the DRAM-bound time (2 x w x h x bytes / 1008 GB/s), since cache and
// compression let real copies beat DRAM. Below it a GPU span is not a copy's and voids its window; over 2.5 DRAM times: slow.
enum FgCopyClass { FG_COPY_OK = 0, FG_COPY_BELOW = 1, FG_COPY_SLOW = 2 };
static inline double FgCopyDramUs(unsigned w, unsigned h, unsigned bpp)
{
    return 2.0 * (double)w * (double)h * (double)bpp / 1008e9 * 1e6;
}
static inline double FgCopyFloorUs(unsigned w, unsigned h, unsigned bpp)
{
    return 0.1 * FgCopyDramUs(w, h, bpp);
}
static inline int FgCopyClassify(double us, unsigned w, unsigned h, unsigned bpp)
{
    if (us < FgCopyFloorUs(w, h, bpp)) return FG_COPY_BELOW;
    if (us > 2.5 * FgCopyDramUs(w, h, bpp)) return FG_COPY_SLOW;
    return FG_COPY_OK;
}

// Not the back buffer: it is queried fresh before every copy (DXVK swaps it at every Present). GEN2 is the set's fourth place;
// SAVE and PATCH (HOLD) sit above every place of the set (fg_arm.h asserts it).
enum FgImg { FG_IMG_PRESENT = 0, FG_IMG_GEN = 1, FG_IMG_HUDLESS = 2, FG_IMG_GEN2 = 3, FG_IMG_SAVE = 4, FG_IMG_PATCH = 5, FG_IMG_N };
// One hold slot: the real frame of the open pair.
enum { FG_HOLD_SLOTS = 1 };
// The timelines: fgSemIn, fgSemMarkI, the transport's latSemEnd, a local one, and the present end's.
enum FgTl { FG_TL_IN = 0, FG_TL_MARKI = 1, FG_TL_END = 2, FG_TL_LOCAL = 3, FG_TL_PRES = 4, FG_TL_N };

// ---- the keys

// The floor is LOCK times two refreshes; LOCK in per mille.
#define FG_LOCK_PM 1015

// The records the threads exchange, and the state.
// The machine's counters no decision reads: DEVSTAT, kept by a development build and the harness, nothing in a release. A
// counter on a call that must still run: FGC_IF(call, counter). The fields stay, so no line of FgState moves.
#ifndef DEVSTAT
#define DEVSTAT(...) __VA_ARGS__
#endif
#if defined(DLSSIV_DEVLOG) && !DLSSIV_DEVLOG
#define FGC_IF(cond, ...) (void)(cond)
#else
#define FGC_IF(cond, ...) if (cond) __VA_ARGS__
#endif
#include "fg_machine_state.h"

// ---- the binding contract
// B provides a static function for every seam (docs/FG-MACHINE-NOTES.md, "THE BINDING CONTRACT", lists each). Rules that bite:
// DevLock is the raw interop lock, one guard slot that does not nest (Lock counts the depth); no D3D9 call while QLock is held;
// PresentGate is called on the presenting thread, with no lock held, before every present of the merged path, fires included.
// Install saves the chained observers and present pointers itself, from the device thread's own tables.
// Three traits, read with `if constexpr` (a statement behind one is not compiled for a binding that says false):
//   kRig     an instrument's binding (the rig, the harness): FgSwapPresent's instrument tail, its records, its line texts.
//   kEngage  it can arm a pair: the fire note, the checkpoints, the frame end's ownership, FgPace. Every binding says true.
//   kWire    it speaks the product's wire (B::X(), the injection's number on 'Q', 'G' and fgIn, the guard, MayEngage).
// kEngage and kWire: the merged path (FgPA, and the harness's FgArmedShape). kWire without kEngage does not compile.
// ---- the machine

// The gate's calls by site (0 pass-through, 1 inline, 2 a generated frame's fire, 3 a real frame's): a macro so the harness can
// switch one off.
#ifndef FG_PRESENT_GATE_ON
#define FG_PRESENT_GATE_ON(site) true
#endif
// Guards a macro so only the harness's controls can switch each off; elsewhere always true.
//   0, 1   the machine's state and the answer, read again after the gate
//   2      the stand-down at a lost restore
//   3      a back buffer holding a HUD-less picture nothing took back is not presented
//   4      the game's own period in the hand
//   12     the law's free band's form at a head of 1.0
//   6      the ride (ArmedRides, ArmedBeforeInject)
//   13     PaceFixed's anti-windup, stand-still, restarts, near-target learning, tracker, own wait
//   14, 15 PaceFixed's bounded blind teaching; the integral kept whole at a restart
//   10     PaceFixed reads the block of a real frame behind a real one, not of every present
//   11, 16, 18 PaceFixed where the card is the limit: the step, the trusted integral, no negative correction
#ifndef FG_ARMED_GUARD_ON
#define FG_ARMED_GUARD_ON(which) true
#endif
// Rule C1: a pair still armed at the next injection has its real frame forced there (point c). A macro so the harness can hold a
// pair armed across an injection; elsewhere always true.
#ifndef FG_C1_ON
#define FG_C1_ON() true
#endif
// The stub's modelled answer, us after the handoff's retire (the rig's placer only). A macro so the harness can move it.
#ifndef FG_STUB_GEN_US
#define FG_STUB_GEN_US() 1500
#endif
// The binding's PacePoll (1: the placer woke FgPace), where it has one; else FgPace's spin is the rig's.
template <class T, class = void> struct FgHasPacePoll { static constexpr bool value = false; };
template <class T> struct FgHasPacePoll<T, decltype((void)T::PacePoll())> { static constexpr bool value = true; };

template <class B>
struct FgMachine
{
    static_assert(B::kEngage, "a binding that cannot arm (the shadow shape) is gone since round 9's strip: every binding of the machine arms");
    static FgState& S() { return B::S(); }
    static LONGLONG UsToTicks(LONGLONG us) { return us * B::Qpf() / 1000000; }
    static double   TicksToMs(LONGLONG t) { return (double)t * 1000.0 / (double)B::Qpf(); }
    static double   Absd(double x) { return x < 0.0 ? -x : x; }
    static bool     OnWorker() { return __readfsdword(0x24) == dtWorkerTid; }
        // FireUnchained: the pointer check's term for the fire note.
    static __forceinline const char* Named(const char* rig, const char* product) { if constexpr (B::kRig) { (void)product; return rig; } else { (void)rig; return product; } }
    static __forceinline bool     FireUnchained() { return dtNoteObs[DT_NOTE_FG_FIRE] != &NoteFire; }
        // The want, by what it asks (the merged path):
    //   PASS     nothing: passed through, nobody sleeps
    //   ON       the wire and the pacer: a hand, a pair that may arm and show, FgPace
    //   CAP      the pacer alone at the same period on the same frames: nothing written, copied or generated (ON's control)
    // An unknown want asks nothing. WantsWire: a hand and 'Q'. WantsPace: FgPace runs. Any other binding: a hand while the want is
    // not PASS, and FgPace while engaged.
    static __forceinline bool     WantsWire(LONG want) { if constexpr (B::kWire) return want == FG_WANT_ON; else return want != FG_WANT_PASS; }
    static __forceinline bool     WantsPace(LONG want, bool engaged) { if constexpr (B::kWire) { (void)engaged; return want == FG_WANT_ON || want == FG_WANT_CAP; } else { (void)want; return engaged; } }
        // fgIn's value: the pair's ordinal on the stub; the injection's number on the product's wire.
    static __forceinline LONG     InValue(const FgWorkerLine& w) { if constexpr (!B::kWire) return w.openP; else { (void)w; return B::X().w.openN; } }
        // Who writes 'Q' after its injection's body: every binding on the wire (the early prepare comes out of K). B::QAtHand gives the
    // other order.
    static constexpr bool kEarlyQ = B::kWire;
        // The merged path in a binding that is not an instrument's: its own FgSwapPresent tail, no ring written at a standby present.
    static constexpr bool kLean = B::kWire && !B::kRig;
    // The panel's refresh period in ms (hdr.h's rational), 0 when unread.
    static double RefreshMs()
    {
        const ULONGLONG pk = B::RefreshPacked();
        const ULONG num = (ULONG)(pk >> 32), den = (ULONG)pk;
        return (num && den) ? 1000.0 * (double)den / (double)num : 0.0;
    }
        // Half the period, never under a refresh. In the fixed mode exactly half, which is two refreshes by PaceFixed's correction, so
    // each frame has a refresh of its own.
    static double HalfOf(double periodMs)
    {
        if constexpr (B::kWire) { if (ReadAcquire(&S().r.fixOn)) return periodMs > 0.0 ? periodMs * 0.5 : 0.0; }
        const double refreshMs = RefreshMs();
        double half = periodMs * 0.5;
        if (refreshMs > 0.1 && half < refreshMs) half = refreshMs;
        return half > 0.0 ? half : 0.0;
    }
        // C as a period: 2 x refresh x LOCK. In the fixed mode two refreshes as read; PaceFixed's correction takes the panel's true rate.
    static double CPeriodMs()
    {
        if constexpr (B::kWire)                    // only a binding on the wire reads the word
        {
            const FgRenderLine& r = S().r;
            if (ReadAcquire(&r.fixOn)) return 2.0 * RefreshMs() * (1.0 + (double)r.fixPpm * 1e-6);
        }
        return 2.0 * ((double)FG_LOCK_PM / 1000.0) * RefreshMs();
    }

        // ---- value, stamp, value
    // Single-writer records: the writer stores seq = -1, the fields, then seq = the key; the reader reads seq, the fields, seq
    // again, and keeps the copy only when both reads equal the key it wants. x86 keeps stores and loads in order; WriteRelease and
    // ReadAcquire keep the compiler's order.
    template <class R> static void Publish(R& rec, LONG key, const R& val)
    {
        WriteRelease(&rec.seq, -1);
        R tmp = val; tmp.seq = -1;
        memcpy((BYTE*)&rec + sizeof(LONG), (const BYTE*)&tmp + sizeof(LONG), sizeof(R) - sizeof(LONG));
        WriteRelease(&rec.seq, key);
    }
    template <class R> static bool Read(const R& rec, LONG key, R* out)
    {
        const LONG a = ReadAcquire(&rec.seq);
        if (a != key) return false;
        memcpy((BYTE*)out, (const BYTE*)&rec, sizeof(R));
        const LONG b = ReadAcquire(&rec.seq);
        return b == key;
    }
    static bool ReadStamp(const FgStamp* ring, LONG v, LONGLONG* t)
    {
        const FgStamp& e = ring[v & (FG_STAMPS - 1)];
        const LONG a = ReadAcquire(&e.v1);
        if (a != v) return false;
        const LONGLONG tt = e.t;
        const LONG b = ReadAcquire(&e.v2);
        if (b != v) return false;
        *t = tt; return true;
    }

        // ---- the single-lock rule
    // DXVK's interop keeps one guard: LockDevice is `m_lock = m_device->LockDevice()`, UnlockDevice is `m_lock =
    // D3D9DeviceLock()`, and an owner's second acquire returns an empty guard whose assignment over the held one releases it.
    // So the lock is taken through this depth count; only the 0-to-1 step calls DevLock, and a nested take is only counted.
    static void Lock()   { FgWorkerLine& w = S().w; if (w.lockDepth++ == 0) B::DevLock(); DEVSTAT(else w.nest++;) }
    static void Unlock() { FgWorkerLine& w = S().w; if (--w.lockDepth == 0) B::DevUnlock(); }

        // ---- the install
    // Render thread at IvtBuild's tail, the worker drained. The once-flag first, then the observers as plain stores (the worker is
    // idle), then dtScPresentO (the only value read at record time), then origSwapPresent last.
    static bool Install()
    {
        FgState& s = S();
        if (_InterlockedCompareExchange(&s.ptr.installed, 1, 0) != 0) return false;
        s.ptr.prevInject  = dtNoteObs[DT_NOTE_INJECT];
        s.ptr.prevPresent = dtPresentObs;
        s.ptr.prevArm     = dtArmObs;
        s.ptr.prevDisarm  = dtDisarmObs;
        s.ptr.prevObs82   = dtObs[82];
        dtNoteObs[DT_NOTE_FG_HAND]    = &NoteHand;
        dtNoteObs[DT_NOTE_FG_HUDLESS] = &NoteHudless;
        dtNoteObs[DT_NOTE_FG_FIRE] = &NoteFire;
        dtNoteObs[DT_NOTE_INJECT]     = &InjectObs;
        dtPresentObs = &PresentObs;
        dtArmObs     = &OnArm;
        dtDisarmObs  = &OnDisarm;
        s.ptr.dxvkPresent = B::DxvkBare(dtScPresentO);            // DXVK's own pointer, from behind any gate standing in this one
        dtScPresentO      = (void*)&WorkerPresent;
        void*& osp = B::OrigSwapPresentSlot();
        s.ptr.origSwapPresent = osp;
        osp = (void*)&SwapPresent;
        _InterlockedExchange(&s.sh.state, FG_LIVE);
        return true;
    }
        // A second device pass re-runs TrkArm and DtHookRes, which rewrite these; the machine then goes DEAD with a line rather than run
    // half-chained. Returns the name of the first one that is not the machine's, or nullptr.
    static const char* Unchained()
    {
        if (dtScPresentO != (void*)&WorkerPresent) return "dtScPresentO";
        if (dtNoteObs[DT_NOTE_FG_HAND] != &NoteHand || dtNoteObs[DT_NOTE_FG_HUDLESS] != &NoteHudless || FireUnchained()) return "a frame generation note observer";
        if (dtNoteObs[DT_NOTE_INJECT] != &InjectObs) return "the injection's note observer";
        if (dtArmObs != &OnArm || dtDisarmObs != &OnDisarm) return "the arm or disarm observer";
        if (dtPresentObs != &PresentObs) return "the present observer";
        return nullptr;
    }
    static void Kill(const char* why)
    {
        if (_InterlockedExchange(&S().sh.state, FG_DEAD) != FG_DEAD) B::Line(Named("  fg rig: dead: %s", "  frame generation: dead: %s"), why);
        if constexpr (B::kWire) ArmedStopped();     // no order wait stands, and the helper's place on the card is given back
    }
        // Where the machine stops, dies, is disarmed or meets a Reset or a build, on any thread.
    static void ArmedStopped() { ArmedOrdStop(); ArmedCardDown(); }
        // Callable on any thread, the worker included (IvtFail runs there), so it never drains, waits or joins. CLOSING arms nothing new
    // and fires an open pair's N at the next fire point; the present observer is unchained first, so LatWaiterStop finds
    // LatPresentObs at the head again.
    static void Stop(const char* why)
    {
        FgState& s = S();
        if constexpr (B::kWire) _InterlockedExchange(&B::X().sh.stopped, 1);   // nothing more goes to the pipe
        if (_InterlockedCompareExchange(&s.sh.state, FG_CLOSING, FG_LIVE) == FG_LIVE) B::Line(Named("  fg rig: closing: %s", "  frame generation: closing: %s"), why);
        if constexpr (B::kWire) ArmedStopped();
        if (dtPresentObs == &PresentObs) dtPresentObs = s.ptr.prevPresent;
    }

        // ---- publications, resolutions, answers
    static void PubRecord(LONG p, LONG kind, LONG why, const FgHandPayload* h, LONGLONG tArm, LONG sent)
    {
        FgState& s = S();
        FgPub v = {};
        v.kind = kind; v.why = why; v.p = p; v.tArm = tArm; v.sent = sent;
        if (h) { v.gen = h->gen; v.k = h->k; v.fEnd = h->fEnd; v.withhold = h->withhold; v.hold = h->hold; v.flags = h->flags; v.periodUs = h->periodUs; v.tEntry = h->tEntry; }
        Publish(s.pub[p & (FG_RING - 1)], p, v);
                // Publications are taken in p order and pubP only moves forward; a pair published twice is a machine fault, counted.
        if (p > s.sh.pubP) WriteRelease(&s.sh.pubP, p); DEVSTAT(else s.c.pubOrder++;)
        B::WakePlacer();                                          // INLINE and CLOSED are answered at once, and
                                                                  // the next FG_HAND's G5 test reads that answer
        DEVSTAT(if (kind == FG_PUB_ARMED) s.c.pubArmed++;)
        DEVSTAT(else if (kind == FG_PUB_INLINE && why >= 0 && why < FG_IN_N) s.c.pubInline[why]++;)
        DEVSTAT(else if (kind == FG_PUB_CLOSED && why >= 0 && why < FG_CL_N) s.c.pubClosed[why]++;)
    }
    static void ResWrite()
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        FgRes r = {};
        r.p = w.openP; r.gen = w.openGen;
        FgRes old = {};
        if (Read(s.res[w.openP & (FG_RING - 1)], w.openP, &old)) { r = old; }
        r.p = w.openP; r.gen = w.openGen;
        Publish(s.res[w.openP & (FG_RING - 1)], w.openP, r);
    }
    static void ResolveI(int res, DWORD why, LONGLONG start)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if (w.iResolved) return;
        w.iResolved = true;
        FgRes r = {};
        Read(s.res[w.openP & (FG_RING - 1)], w.openP, &r);
        r.p = w.openP; r.gen = w.openGen; r.iRes = res; r.iWhy = (LONG)why; r.iStart = start;
        Publish(s.res[w.openP & (FG_RING - 1)], w.openP, r);
        DEVSTAT(if (res == FG_RES_SHOWN) s.c.iShown++;)
        DEVSTAT(else { int b = 0; while (b < FG_W_N && !(why & (1u << b))) b++; if (b < FG_W_N) s.c.iWithheld[b]++; })
    }
    static void ResolveN(int res, int point, LONGLONG start)
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if (w.nResolved) return;
        w.nResolved = true;
        FgRes r = {};
        Read(s.res[w.openP & (FG_RING - 1)], w.openP, &r);
        r.p = w.openP; r.gen = w.openGen; r.nRes = res; r.nPoint = point; r.nStart = start;
        Publish(s.res[w.openP & (FG_RING - 1)], w.openP, r);
    }
        // Whatever state it is in: the 'Q' stub gets its 'G', the checkpoints go.
    static void ClosePair()
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if constexpr (B::kWire) ShadowCloseG();
        else { if (w.openQ && !w.openG) { B::WriteG(w.openP, 1u); w.openG = true; DEVSTAT(s.c.gWritten++;) } }
        CheckpointsRemove();
        w.openPhase = FG_PAIR_NONE;
        _InterlockedExchange(&s.sh.pairOpen, 0);
    }

        // ---- the checkpoint observers
    // Installed at the arm and removed when N has fired, both idempotent: an installed flag keeps a second install from saving
    // Obs82 as its own previous observer. The worker is the tables' only reader.
    static void CheckpointsInstall()
    {
        FgWorkerLine& w = S().w;
        if (w.ckInstalled) return;
        w.ckInstalled = 1;
#if DT_BBS_ON
                // Each slot's observer runs the saves' tracker first, then the checkpoint; UpdateSurface has the tracker's alone
        // (devthread/dt_bbs.h).
        dtObs[34] = &ObsCheckpointAt<34>; dtObs[35] = &ObsCheckpointAt<35>; dtObs[37] = &ObsCheckpointAt<37>; dtObs[43] = &ObsCheckpointAt<43>;
        dtObs[81] = &ObsCheckpointAt<81>; dtObs[83] = &ObsCheckpointAt<83>; dtObs[84] = &ObsCheckpointAt<84>;
        dtObs[30] = &DtBbsObs30;
#else
        dtObs[34] = dtObs[35] = dtObs[37] = dtObs[43] = dtObs[81] = dtObs[83] = dtObs[84] = &ObsCheckpoint;
#endif
        dtObs[82] = &Obs82;
    }
    static void CheckpointsRemove()
    {
        FgState& s = S(); FgWorkerLine& w = s.w;
        if (!w.ckInstalled) return;
        w.ckInstalled = 0;
        dtObs[34] = dtObs[35] = dtObs[37] = dtObs[43] = dtObs[81] = dtObs[83] = dtObs[84] = nullptr;
#if DT_BBS_ON
        dtObs[37] = &DtBbsObs37; dtObs[30] = nullptr;             // the binding's bookkeeping continues after the pair
#endif
        dtObs[82] = s.ptr.prevObs82;
    }
#if DT_BBS_ON
    template <DWORD SL> static void ObsCheckpointAt(const DWORD* c) { DtBbsAt<SL>(c); ObsCheckpoint(c); }
#endif
    static void ObsCheckpoint(const DWORD* c)
    {
        FgState& s = S();
        s.w.ckPair++;
        if (s.w.openPhase == FG_PAIR_ARMED) Fire(FG_PT_A, (DWORD)(c - dtRing));
    }
    static void Obs82(const DWORD* c)
    {
        FgState& s = S();
#if DT_BBS_ON
        DtBbsAt<82>(c);                                  // the saves' tracker, ahead of every observer
#endif
        if (s.ptr.prevObs82) s.ptr.prevObs82(c);        // the tracker's draw observer, first and always
        s.w.ckPair++;
        if (s.w.openPhase == FG_PAIR_ARMED) Fire(FG_PT_A, (DWORD)(c - dtRing));
    }

        // The render thread (FgSwapPresent, FgPace)
    #include "fg_machine_render.h"

        // The worker (FG_HAND, FG_HUDLESS, the present, FG_FIRE, the injection) and the fire routine
    #include "fg_machine_worker.h"

        // The product's paths: the wire's hand, the merged path, and what rides on it
    #include "fg_machine_armed.h"

        // Arm and disarm, the placer, the stampers, the waiter, the present end's map
    #include "fg_machine_placer.h"
};
