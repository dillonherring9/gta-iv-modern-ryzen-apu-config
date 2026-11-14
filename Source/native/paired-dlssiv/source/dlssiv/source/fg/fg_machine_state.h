// fg_machine_state.h: the records the threads exchange and the state, one line per writer: FgState, the product's shadow
// state and the merged path's FgArmedState. Included in place at file scope by fg_machine.h.

// ---- the records the threads exchange ----

// FG_HAND's payload: the facts travel in the note.
struct FgHandPayload
{
    LONG  k, gen, p, fEnd;                   // present ordinal, arm generation, pair, the frame this present ends
    DWORD withhold;
    DWORD flags;
    LONGLONG tEntry;                         // QPC at FgSwapPresent's entry
    LONG  hold;                              // the wire's: the build generation
        // Period in force, us: FgPace's own as this present published it (it moves after the present), else the applied one.
    LONG  periodUs;
};
enum : DWORD { FG_HF_WARM = 64u /* within 150 pairs of an engage: neither late nor clean */,
                                // FIRST: an engage's first pair
               FG_HF_FIRST = 512u,
               // the regime: REG1 the hold band, REG2 the free range
               FG_HF_REG1 = 8192u, FG_HF_REG2 = 16384u,
               // THE HUD-LESS PICTURE (merged path). HUDPASS: a HUD-less picture is drawn INTO THE BACK BUFFER behind this
               // hand and its note puts the real frame back from HOLD. NOHUD: no HUD-less picture: fed, not shown. HUDWIDE:
               // the copy point's check said so. HUDTEX: the picture stands in the binding's own image; nothing is drawn.
               FG_HF_HUDPASS = 65536u, FG_HF_NOHUD = 131072u, FG_HF_HUDWIDE = 262144u, FG_HF_HUDTEX = 524288u,
               // The ride: the real frame fires behind the next injection's body.
               FG_HF_RIDE = 1048576u };
struct FgFirePayload { LONG why, p, gen; };  // FG_FIRE: why is FG_PT_B (the placer) or FG_PT_D (a drain)
struct FgPairPayload { LONG k, gen, p; };    // FG_HUDLESS

// A publication, value, stamp, value: the reader keeps it only when seq reads the p it wants both times.
struct FgPub
{
    volatile LONG seq;
    LONG kind, why, p, gen, k, fEnd, sent, hold;
    DWORD withhold, flags;
    LONGLONG tArm;
    LONG periodUs;
    LONGLONG tEntry;                         // the frame's arrival at its Present
};
// A resolution of a pair's two parts, by the worker.
enum { FG_RES_OPEN = 0, FG_RES_SHOWN = 1, FG_RES_WITHHELD = 2, FG_RES_FIRED = 1, FG_RES_DROPPED = 2, FG_RES_CLOSED = 3 };
struct FgRes
{
    volatile LONG seq;
    LONG p, gen, iRes, iWhy, nRes, nPoint;
    LONGLONG iStart, nStart;                 // fire starts, 0 when not fired
};
// An answer: 2p, or 2p+1 at once for INLINE and CLOSED, with its reason. Written by the placer alone.
struct FgAns { volatile LONG seq; LONG p, val, why; };
// A due the placer raises for the worker's open pair. eUs: N's call-side correction (the sample adds it back).
enum { FG_DUE_NONE = 0, FG_DUE_SHOW = 1, FG_DUE_WITHHOLD = 2, FG_DUE_FIRE = 3 };
struct FgDue { volatile LONG seq; LONG p, gen, verdict; DWORD why; LONGLONG tDue; LONG eUs; };
// A stamp: a timeline value and the QPC the stamper first saw it, value, stamp, value.
struct FgStamp { volatile LONG v1; LONG late; LONGLONG t; volatile LONG v2; };
// Who owns frame f's end (FgSwapPresent), value, stamp, value.
struct FgOwn { volatile LONG f1; LONG owned, p; volatile LONG f2; };

// The stub placer's plan for a pair, value, stamp, value, published before any due it raises (the harness reads it).
struct FgPlan { volatile LONG seq; LONG p, gen, hUs, holdUs; LONGLONG tS, arrival, tI, tN; LONG periodUs, aUs, eUs;
                LONGLONG tEntry, tReady; LONG shiftUs, wIUs, wNUs; DWORD kflags; };
// The placer's raises of a pair (the L row): when each raise was wanted and made. Published at N's raise.
struct FgRaise { volatile LONG seq; LONG p; LONGLONG atI, tIRaised, atN, tNRaised; };
// The waiter's deferred samples (Route): a rig-owned end whose END stamp is not back yet. Waiter thread only.
enum { FG_PEND_N = 16 };
struct FgPend { LONG fe, fp; LONGLONG b; float p, h, q, l; int iters; };
struct FgRoute { FgPend pend[FG_PEND_N]; int pendN; LONG notBack, beforeBegin; };

// A present record, by the frame it ends: the render thread's cycle count at entry, and FgPace's figures.
// Written by the render thread after FgPace, value, stamp, value.
struct FgPresRec
{
    volatile LONG f1;
    LONG freeUs;
    ULONGLONG cycles;
    LONG inOrigUs, sleepUs, lateUs, sleeps;
        // The pacer's row (B row): entry, the sleep's grid instant (0 none), the release, the period, FG_PB_* bits.
    LONGLONG tEntry, due, tPost; LONG periodUs, pflags;
    LONG lawEUs, lawLUs;                     // the law's E and L, us (0: not run)
    volatile LONG f2;
};
enum : LONG { FG_PB_SLEPT = 1, FG_PB_CLAMP = 2, FG_PB_RESYNC = 4, FG_PB_MOVED = 8, FG_PB_CAPPED = 16, FG_PB_PASTSLACK = 32,
              FG_PB_WARM = 64, FG_PB_OWNER = 128 /* FgPace owned pacing at this present */,
                                                 // the law's: a hitch sample; the regime after the step
              FG_PB_HITCH = 256, FG_PB_REG1 = 512, FG_PB_REG2 = 1024 };

// ---- the state, one line per writer ----

enum { FG_RING = 8, FG_STAMPS = 64 };

// The pointers saved at the install, on the render thread with the worker drained.
struct __declspec(align(64)) FgPtrLine
{
    DtNoteFn    prevInject;                  // IvtInjectNoteW
    DtPresentFn prevPresent;                 // LatPresentObs
    void (*prevArm)(); void (*prevDisarm)(); // TrkOnArm, TrkOnDisarm
    DtObsFn     prevObs82;                   // TrkObsDraw
    void*       dxvkPresent;                 // DXVK's Present, as the install found it
    void*       origSwapPresent;             // dtScPresent
    volatile LONG installed;                 // the once-flag, set first
};
// The render thread's.
struct __declspec(align(64)) FgRenderLine
{
    bool  lastEngaged, parked, wasEngaged, paceRunning;
    LONG  lastP, lastGen, nextP, checkN, lastCam, lastEnc, want, sinceEngage;
    LONGLONG lastEntry, periods[10]; int nPeriods, iPeriod;
        // FgPace's state. The keep* fields are unused (the law in fg_law.h keeps its own); their places are kept for the layout.
    double keepAvg, keepWorst, period , prevPeriod; int keepDwell, keepWarm;
    LONGLONG due , prevPostSleep ;
    LONG  moves, resyncs, clamps, sleeps0, sleeps1, sleeps2, wakes, paceSlept;
    LONGLONG timerLateSum, timerLateWorst; LONG timerN, timerPastSlack;
    LONG  lastFreeUs; LONGLONG lastLateMax;  // this present's FgPace sample (-1 none) and its timer's worst lateness
    LONG  capShort;                          // sleeps the 20 ms cap cut short
        // this present's step for the B row
    LONGLONG lastDue; LONG lastPb;
        // published for the placer (plain aligned 32-bit stores)
    volatile LONG appliedUs;                 // the applied real period, us
    volatile LONG paceOwner;                 // 0 none, 1 FgPace, 2 FusionFix, 3 ambiguous
    LONG  periodUs;
    LONG  regime;                            // the law's regime of `period` (0 the floor, 2 free); 0 while FgPace does not run
    LONG  lawEUs, lawLUs;                    // the law's E and L at this step, us
        // The fixed-refresh mode (PaceFixed). fixOn (0 off, 1 on, 2 uncorrected) and fixBaseUs (the Present's own time, no hold)
    // are written by the conditions' step (FgpFixPublish) and read here and by HalfOf; the rest is PaceFixed's own.
    // The tail fits the line's alignment so the struct stays six 64-byte lines: hence the 16-bit counts and the 32-bit refresh.
    volatile LONG fixOn;
    LONG  fixPpm, fixIPpm, fixMedUs, fixLastBlkUs;
    volatile LONG fixBaseUs;
    LONG  fixPresN;
    SHORT fixLateAt;                         // 16 bits: a window's late count is its difference, under 60
    SHORT fixITrust;                         // the integral at the last restart or in-band window: the card's wait restores it
    LONGLONG fixPresT;
    LONG  fixRefresh;
    CHAR  fixWin, fixCard;                   // fixCard: the card's wait (fg_fixrule.h), its run in bits 0 and 1, the reading in bit 2
    SHORT fixSince;
    USHORT fixWins, fixHeld, fixStalls, fixPrevMedUs;   // fixPrevMedUs: the median at the last corrected window, 65535 at most
};
// The fixed mode's constants and its rule for the card's wait, shared with the replay.
#include "fg_fixrule.h"
// The worker's.
enum { FG_PAIR_NONE = 0, FG_PAIR_OPEN = 1, FG_PAIR_ARMED = 2 };
struct __declspec(align(64)) FgWorkerLine
{
    // the open pair (one at a time)
    LONG  openPhase, openP, openGen, openK, openFEnd, openHold;
    DWORD openWithhold, openFlags;
    // the hand's period in force and entry, for the armed publication
    LONG  openPeriodUs; LONGLONG openTEntry;
    bool  openHudlessSeen, openNosource, openSent, openQ, openG, iResolved, nResolved;
    LONG  handFor, handGen;                  // FgWorkerPresent's one-shot
    LONG  lastOwnedK;                        // the last ordinal the rig owns (its LatPresentObs is suppressed)
    // the present FgWorkerPresent deferred (the rects copied out of the ring)
    IDirect3DSwapChain9* sc; RECT src, dst; bool hasSrc, hasDst; HWND wnd; DWORD pflags;
    LONGLONG tArm;
    LONG  lastSentN, lockDepth, nest, slotWaits, ckInstalled, ckPair;   // ckPair: the armed pair's checkpoint calls
    LONG  firePoint;
    LONG  fireKind;                          // the fire in progress: 0 I, 1 N
    volatile LONG wIUs, wNUs;                // the service times, EWMA 0.10, 0 to 2 ms, for the placer
    double wI, wN;
    LONG  staleI, staleN;
};
// The placer's: at most three live pairs (counted beyond).
struct FgLive
{
    LONG p, gen, sent, hold, warm;
    DWORD withhold;
    LONGLONG tS, arrival, tI, tN, tIRaised;
    bool answered, iRaised, nRaised, timed;
    LONG periodUs; DWORD hflags;
    LONGLONG tEntry, atI;                    // the hand's entry; when I's raise was wanted
};
struct __declspec(align(64)) FgPlacerLine
{
    LONG   seen;
    FgLive live[3]; LONG liveN;
    LONG   capClean;                         // clean-pair count for the pad
    LONG   curGen;
    volatile LONG hUs;                       // the last pair's H without the hold
        // The exact raise (spin for this due; the spins' counts).
    bool   nextSpin;
    volatile LONG spins, spinWoke; volatile LONGLONG spinTicks;
};
// The waiter's.
struct __declspec(align(64)) FgWaiterLine { LONG want, window, half, pair; };
// Flags several threads touch, with interlocked operations.
struct __declspec(align(64)) FgSharedLine
{
    volatile LONG state, fireBusy, armQuiet, pairOpen, padQms;
    volatile LONG pubP;                      // the last published p (release store after the record)
        // Presents behind a real frame: the worker's time inside DXVK's Present on fires whose previous fire was a real frame's,
    // the part of dtPresTicksSum they added, and their count. The worker writes all three, PaceFixed reads two.
    // presLastGen: worker only, its last fire was a generated frame's.
    volatile LONGLONG presTicksR;
    volatile LONG presRN;
    LONG presLastGen;
};
static_assert(sizeof(FgSharedLine) == 64 && sizeof(FgRenderLine) == 384, "the shared line is one 64-byte line and the render thread's six: a field more moves every line behind them");

// Counters, per window, reset by the waiter. Each field has one writer thread, named.
struct FgCensus
{
    // worker
    volatile LONG pubArmed, pubInline[FG_IN_N], pubClosed[FG_CL_N];
    volatile LONG iShown, iWithheld[FG_W_N], nOnTime, nEarly, nLate, nDropped, nAtStop, staleDues, handOpen;
    volatile LONG points[FG_PT_N], presentsDxvk, presentsI, presentsN, presentsInline, presentsPass;
    volatile LONG qWritten, gWritten, noteRefused;
    volatile LONG pubOrder;
    volatile LONG nosourceBy[FG_NS_N];       // nosource pairs, by why
        // render
    volatile LONG paceWaitN;
    volatile LONG hands;
    // placer
    volatile LONG answers2p, answers2p1, lateCalls, onTimeCalls, liveOverflow, pubLost;
    // 2p+1 at retirement, closed before or after the arrival
    volatile LONG closedBeforeArrival, answeredLate;
    // 2p+1 answers by the fact and stale, so answers2p1 = inline + closed + closedBeforeArrival + answeredLate +
    // answeredFact + answeredStale, exactly
    volatile LONG answeredFact, answeredStale;
    // stampers
    volatile LONG stamped[FG_TL_N], stampedLate[FG_TL_N], endSkipped;
};

// The machine's state. The binding owns the instance (B::S()) and the ring memory (one VirtualAlloc at the install, each
// ring on its own 64-byte line), so .data holds only this struct.
struct FgState
{
    FgPtrLine     ptr;
    FgRenderLine  r;
    FgWorkerLine  w;
    FgPlacerLine  pl;
    FgWaiterLine  wt;
    FgSharedLine  sh;
    FgCensus      c;
    FgPub*     pub;
    FgRes*     res;
    FgAns*     ans;
    FgDue*     dueI;
    FgDue*     dueN;
    FgStamp*   st[FG_TL_N];   // [FG_STAMPS] each (FG_TL_LOCAL unused)
    FgOwn*     own;
    FgPresRec* pres;     // the rig's present records (the B row, replayed through the law's reference)
    FgPlan*    plan;
    FgRaise*   raise;
    FgLaw*     law;      // the pacing law's state, the render thread's (FgStateAttach gives every binding one)
};
// The ring memory, laid out by FgStateAttach: 64-byte lines, in this order.
static size_t FgStateBytes()
{
    auto line = [](size_t b) { return (b + 63) & ~(size_t)63; };
    return line(sizeof(FgPub) * FG_RING) + line(sizeof(FgRes) * FG_RING) + line(sizeof(FgAns) * FG_RING)
         + line(sizeof(FgDue)) * 2 + line(sizeof(FgStamp) * FG_STAMPS) * FG_TL_N + line(sizeof(FgOwn) * FG_STAMPS)
         DEVSTAT(+ line(sizeof(FgPresRec) * FG_STAMPS)) + line(sizeof(FgPlan) * FG_RING)
         + line(sizeof(FgRaise) * FG_RING) + line(sizeof(FgLaw));
}
static void FgStateAttach(FgState& s, void* mem)   // mem: FgStateBytes() of zeroed memory, 64-byte aligned
{
    BYTE* p = (BYTE*)mem;
    auto take = [&p](size_t b) { BYTE* q = p; p += (b + 63) & ~(size_t)63; return (void*)q; };
    s.pub = (FgPub*)take(sizeof(FgPub) * FG_RING);
    s.res = (FgRes*)take(sizeof(FgRes) * FG_RING);
    s.ans = (FgAns*)take(sizeof(FgAns) * FG_RING);
    s.dueI = (FgDue*)take(sizeof(FgDue));
    s.dueN = (FgDue*)take(sizeof(FgDue));
    for (int i = 0; i < FG_TL_N; i++) s.st[i] = (FgStamp*)take(sizeof(FgStamp) * FG_STAMPS);
    s.own = (FgOwn*)take(sizeof(FgOwn) * FG_STAMPS);
    DEVSTAT(s.pres = (FgPresRec*)take(sizeof(FgPresRec) * FG_STAMPS);)
    s.plan = (FgPlan*)take(sizeof(FgPlan) * FG_RING);
    s.raise = (FgRaise*)take(sizeof(FgRaise) * FG_RING);
    s.law = (FgLaw*)take(sizeof(FgLaw));
    for (int i = 0; i < FG_RING; i++) s.raise[i].seq = -1;
    DEVSTAT(for (int i = 0; i < FG_STAMPS; i++) s.pres[i].f1 = s.pres[i].f2 = -1;)
    for (int i = 0; i < FG_RING; i++) s.plan[i].seq = -1;
    s.pl.hUs = -1;
    // every seq starts at -1: no record holds p 0 until written
    for (int i = 0; i < FG_RING; i++) { s.pub[i].seq = -1; s.res[i].seq = -1; s.ans[i].seq = -1; }
    s.dueI->seq = -1; s.dueN->seq = -1;
    for (int t = 0; t < FG_TL_N; t++) for (int i = 0; i < FG_STAMPS; i++) { s.st[t][i].v1 = -1; s.st[t][i].v2 = -1; }
    for (int i = 0; i < FG_STAMPS; i++) s.own[i].f1 = s.own[i].f2 = -1;
        // Pairs count from 1: a pair's number is a timeline value, which must be greater than the semaphore's start of 0.
    s.r.lastP = -1; s.w.openP = -1; s.w.handFor = -1; s.w.lastOwnedK = -1; s.pl.seen = 0; s.sh.pubP = 0;
    s.r.nextP = 1;
}

// ---- the product's own state ----
// What a binding that cannot arm keeps beside FgState, owned by it (B::X()), so FgState stays as the rig compiles it.
// One 64-byte line per writer thread.
// The kinds of 'G' and the reset reasons of 'Q', by the wire's values (feed_ipc.h; the mod's binding asserts them equal).
enum : DWORD { FG_G_DISPATCH = 0, FG_G_CLOSED = 1, FG_G_NOSOURCE = 2, FG_G_WITHHELD = 3 };
enum : DWORD { FG_Q_CUT = 1, FG_Q_BUILD = 2, FG_Q_DEVICE = 4, FG_Q_GAP = 8 };
// The reasons ride 'G' too: B::WriteG's second argument is the kind in bits 0 to 1, the reasons in bits 4 to 7.
enum { FG_G_RESET_SHIFT = 4 };
// The count seam: bits 2 and 3 name the generated count (0 flow-only, 1, 2) only with FG_G_COUNTED; without it the word
// asks for one frame. The machine writes neither: these and the second input set's bits are wire vocabulary, which
// fg_product.h asserts and the harness's fake helper reads.
enum : DWORD { FG_G_COUNT_SHIFT = 2, FG_G_COUNT_MASK = 0x0Cu, FG_G_COUNTED = 0x100u };
// The second input set's bits (feed_ipc.h, FEED_FG_Q_SET1 and FEED_FG_G_SET1; fg_product.h asserts them): on 'Q' beside
// the reasons, never among them; on 'G' clear of FG_G_COUNTED.
enum : DWORD { FG_Q_SET1 = 64u, FG_G_SET1 = 0x200u };
// The helper's two timelines (fgPrep, fgOut), in rings a binding that cannot arm leaves free. FG_TL_IN is fgIn.
enum { FG_TL_PREP = FG_TL_MARKI, FG_TL_OUT = FG_TL_PRES };
enum { FG_SHADOW_LIVE = 4, FG_K_BINS = 64, FG_K_BIN_US = 250 };
enum { FG_SHADOW_PEND = 4 };                 // DISPATCH pairs awaiting their answer, at most
// The line: entry to the handoff's retire, its own histogram (64 bins of 500 us).
enum { FG_E_BINS = 64, FG_E_BIN_US = 500 };
struct FgShadowPair { LONG p, n, kind; LONGLONG tS, tPrep; LONGLONG tEntry; };   // tEntry: as the publication carried it (0: not carried)
// The worker's (its counts too).
struct __declspec(align(64)) FgShadowWorker
{
    LONG openN;                              // the open pair's number on the wire (0: no 'Q')
    LONG openKind;
    LONG lastQN;
    LONG lastDispatchN;                      // the last DISPATCH not yet answered (0 none)
    LONG lastBgenQ, lastGenQ;
    LONG gapLatch;                           // the next 'Q' carries FG_Q_GAP
    LONG failRun;                            // DISPATCHes answered "no frame" in a row (three end the set)
    volatile LONG gByKind[4];
    volatile LONG gOwedWritten;              // CLOSED 'G's written after their close (owed by a disarm)
    volatile LONG gUnsent;                   // owed when the transport stopped
    volatile LONG ansFrame, ansNone, ansUnknown;   // answers read at the next hand: 2n, 2n+1, overtaken
    volatile LONG resetBy[4];
    volatile LONG cutNoQ;                    // real cuts with no 'Q'
    volatile LONG handsOld;                  // hands of an older build, published CLOSED
    volatile LONG standDowns;                // sets frame generation ended itself
    volatile LONG prepGuards;                // waits queued behind fgPrep
    volatile LONG submitsRefused;            // copies not taken (NOSOURCE)
        // Added at the end so the fields above keep their offsets:
    LONG openReasons;                        // the open pair's reset reasons, for its 'G'
    // pend: DISPATCH pairs not yet answered, oldest first (lastDispatchN is the newest)
    LONG pend[FG_SHADOW_PEND];
};
static_assert(sizeof(FgShadowWorker) == 128, "FgShadowWorker is two 64-byte lines: a field more moves the shared and the placer's lines behind it");
// What several threads touch, with interlocked operations.
struct __declspec(align(64)) FgShadowShared
{
    volatile LONG owedG;                     // a CLOSED 'G' owed for this n (closed off the pipe's writers)
    volatile LONG bgen;                      // the build generation: raised before a build releases the set
    volatile LONG setDead;                   // bgen + 1 of a set that ended itself (0: none)
    volatile LONG stopped;                   // nothing more to the pipe (the transport stopped)
    volatile LONG prepTop;                   // the highest n waited for behind fgPrep: stored before the wait is queued
};
// The placer's: the pairs whose stamps are not all in, and what they said.
struct __declspec(align(64)) FgShadowPlacer
{
    LONG seen, liveN;
    FgShadowPair live[FG_SHADOW_LIVE];
    volatile LONG pubLost, liveOver;
    volatile LONG stampFrame, stampNone, stampMissed;   // answers by the OUT stamps: 2n, 2n+1, no stamp
    volatile LONG kN, prepAfterIn;           // pairs with a generation time; prepares retired after the handoff
    volatile LONG kHist[FG_K_BINS];          // K, the handoff's retire to the answer, binned
    volatile LONGLONG kSumUs, kMaxUs;
        // Added at the end so the fields above keep their offsets:
    volatile LONG eN;                        // pairs with an entry-to-handoff time
    volatile LONG eHist[FG_E_BINS];
    volatile LONG prepBehindUs;              // how far those prepares retired behind the handoff, summed, us
                                             // 32 bits that wrap: readers take differences
};
// The early 'Q', a line of its own at the end. Written by the worker in its notes; by a disarm's tail and the build's
// quiesce only with the ring drained.
struct __declspec(align(64)) FgShadowEarly
{
    LONG qN;                                 // an early 'Q' whose hand has not come (0: none)
    LONG qReasons;                           // its reasons: the hand adds what only it knows
    LONG engaged;                            // the last hand was accepted and nothing has ended the engagement
    LONG sentSeen;
    volatile LONG written;
    volatile LONG noHand;                    // of those, closed with no hand
};
struct FgShadowState { FgShadowWorker w; FgShadowShared sh; FgShadowPlacer pl; FgShadowEarly e; };

// ---- the merged path's own state ----
// What a binding that arms on the wire keeps (B::A()), apart so the rig and the shadow shape compile unchanged.
// Its images of the game's own live with the back buffer, not the helper's set: FG_IMG_HOLD, the open pair's real frame
// from its hand to its fire (written at every hand of a pair that may arm), and FG_IMG_SAVE, the back buffer put back
// after a fire's Present.
enum { FG_IMG_HOLD = FG_IMG_PATCH };
// Why a generated frame was not shown (the first reason). FgCensus::iWithheld gets the nearest of its fifteen.
enum FgArmedWhy
{
    FG_AW_SHOWN = 0,
    FG_AW_FACT,            // a render fact: a hitch, too few samples, no encode, the HDR view
    FG_AW_RESET,           // fed, never shown: its dispatch reset the history
    FG_AW_NOSOURCE,        // nothing handed over (the last dispatch unanswered)
    FG_AW_PIPELINED,       // its guides are a frame ahead of its picture
    FG_AW_LATE,            // its answer was not in at the slot
    FG_AW_NOFRAME,         // "no frame", or the fence past it (a helper gone)
    FG_AW_NOANSWER,        // read again inside the fire, fgOut was not this pair's frame
    FG_AW_LATEFIRE,        // no fire point early enough before the real frame's raise
    FG_AW_FORCED,          // the real frame was forced first
    FG_AW_NOBUFFER,        // no command buffer for its fire
    FG_AW_NOCOPY,          // its write not recorded or not taken: nothing presented
    FG_AW_DROPPED,         // the pair ended unshown (a disarm, a build, a dead machine)
        // Last, so no reason moves: an interface and no HUD-less picture: fed, never shown.
    FG_AW_NOHUD,
    FG_AW_N
};
// What a frame has for a HUD-less picture (B::HudWant, before the hand). SAME: no interface drew, the presented picture is
// its own. PASS: drawn into the back buffer behind the hand (an A/B form). NONE: none can be made. WIDE: the copy point was
// wrong. TEX, the product's: the picture stands in the binding's own image (the encode's second output) and the hand's one
// span copies it; nothing is drawn into the back buffer, so no Present can be lost to a picture not taken back.
enum { FG_HUD_SAME = 0, FG_HUD_PASS = 1, FG_HUD_NONE = 2, FG_HUD_WIDE = 3, FG_HUD_TEX = 4 };
// A 3D overlay ahead of the interface opens the interface (fg_hud.h, "THE PHONE"). The pure rule, shared with the harness:
// a draw into the real back buffer after the scene, the first back-buffer draw and the resolve, not with the scene's
// camera, no 2D transform, writing colour, its viewport not the whole target. Not while a full-screen pass of the mod's
// own is still owed (a copy ahead of it would differ everywhere).
struct FgHudDrawFacts
{
    bool  backBuffer;      // render target 0 is the back buffer itself
    bool  ortho;           // the transform keeps w constant (2D)
    bool  sceneCam;        // drawn with the scene's camera
    bool  colour;
    bool  resolved;        // the mod's resolve is in the back buffer's past
    bool  passOwed;        // a full-screen pass of the mod's own is still owed
    unsigned bbBefore;     // back-buffer draws before this one
    unsigned sceneBefore;  // scene-camera draws before this one
    DWORD vpX, vpY, vpW, vpH;   // the draw's viewport
    DWORD outW, outH;      // the back buffer's size
};
static inline bool FgHudOverlay3dRule(const FgHudDrawFacts& d)
{
    if (!d.backBuffer || d.ortho || d.sceneCam || !d.colour || !d.resolved || d.passOwed) return false;
    if (!d.bbBefore || !d.sceneBefore || !d.outW || !d.outH || !d.vpW || !d.vpH) return false;
    return d.vpX != 0 || d.vpY != 0 || d.vpW < d.outW || d.vpH < d.outH;
}
// Why a merged pair was presented inline, never armed.
enum FgArmedInline { FG_AI_UNSENT = 0, FG_AI_NOHOLD = 1, FG_AI_NOHUDLESS = 2, FG_AI_CLOSING = 3, FG_AI_N };
// Why an armed pair's real frame was dropped (HUDLESS: a HUD-less picture nothing could take back: not presented).
enum FgArmedDrop { FG_AD_DEAD = 0, FG_AD_DISARM = 1, FG_AD_BUILD = 2, FG_AD_NOBUFFER = 3, FG_AD_NOCOPY = 4, FG_AD_HUDLESS = 5, FG_AD_N };
// What a merged fire came to (ArmedFirePresent); only SHOWN presented. DEAD: read at the point and again behind the gate.
enum { FG_AF_SHOWN = 0, FG_AF_NOANSWER = 1, FG_AF_NOBUFFER = 2, FG_AF_NOCOPY = 3, FG_AF_DEAD = 4 };
// The merged close note, in the fire note's `why`.
enum : LONG { FG_FIRE_CLOSE = -1 };
// A build's drain note: spares nothing (a present's own drain note spares the open fill pair).
enum : LONG { FG_FIRE_BUILD = -2 };
// The placer's numbers, written by the binding before the first pair.
struct FgArmedKeys
{
    LONG kPlanUs;          // planned generation time: the slot is the handoff's retire plus this, so the real
                           // frame's due never hangs on the answer
    LONG lateUs;           // late allowance past the slot (0: never)
    // ---- each 0 until a binding says otherwise
    LONG minLeadUs;        // least lead: no generated frame fired closer than this to the real frame's raise
    LONG room;             // 1: the period never under the room floor (roomUs), no pad; 0 the pad
    LONG roomGuardUs;      // the room's margin before the next injection
    LONG slotCopy;         // copy mode: 1 fires the pair's real frame, from HOLD, as the generated frame
    // ---- the plan (fg_plan.h has the three rules)
    LONG planAns;          // 1: the plan is the answer's deadline: the slot is plan + the generated frame's service time
    LONG planUpPairs;      // two late answers fewer pairs apart than this raise the plan at once (FgPlanLateSpan)
    LONG planUpUs;         // ... by this much (a bin of K)
    LONG planMaxUs;
        // ---- the ride (ArmedRides)
    LONG ride;             // 1: under the room floor the pair rides and the pacer keeps the ride's floor (rideUs)
    LONG rideGuardUs;      // the ride's margin before the worker reaches the next body
        // ---- the ride's measured needs
    LONG rideKMaxUs;       // > 0: the plan counts in the ride's floors and deadline for no more than this
        // ---- the helper's answer on a card that is the limit
    LONG over;             // 1: a same-frame ride pair copies over one dispatch in flight where its home waited (HomeWaited)
    LONG card;             // 1: the helper's place on the card, once an engage that rides
    LONG ord;              // the order wait: 0 never; else on every sourced ride pair
    LONG ordMaxUs;         // ... and no longer than this behind the handoff's retire
        // ---- every generated frame shown on a busy card
    LONG rideEarly;        // 1: a ride pair's generated frame not yet due, its answer in, is shown at the next injection
    LONG rideNoWait;       // 1 (with rideEarly): the WAIT floor out of the period
    LONG armTeach;         // 1: a pair timed from the arm teaches the room floor by its late stamp
    LONG ordAtStamp;       // 1: the answer's stamper releases the order wait too
        // ---- the worker ahead of the card
    LONG rideWaitAns;      // 1: the worker waits there for a ride pair's answer behind its standing order wait
        // ---- the room seed
    LONG roomSeed;         // 1: an engage's first stamped pair seeds the room floor (ArmedRoomSeed)
    // ---- the prepare's wait
    LONG prepWait;         // 1: where a prepare's wait is due and the order wait stands, wait for the answer first (ArmedPrepWait)
    LONG prepWaitMs;       // ... bounded by this, ms (0: FG_EARLY_WAIT_MS)
    LONG place;            // 1: placing by the card (ArmedPlaceAt), where the binding has stamps
    LONG rideFitOnly;      // 1 (with placing): the ride's floor is FIT alone
        // ---- the second position
    LONG place2;           // 1 (with placing): a position with no answer moves on once
        // ---- the switch-on (RampWant)
    LONG ramp;             // 1: an engage's first period is the target; 0: the panel's floor
    volatile LONG rampAskUs;   // the target the law's window step publishes, us
    volatile LONG rampCapUs;   // the longest period the switch may hold, us
    LONG rampFirst;        // 1: an engage's first pair never rides (RampFirst)
    volatile LONG rampDrainOk; // 1: the law reads the card as the limit standing by (the drain may run)
};
enum { FG_RAMP_NONE = 0, FG_RAMP_ON = 1, FG_RAMP_DRAIN = 2 };   // rampSt: standing by; ON seen; the drain (CAP)
enum { FG_RAMP_KEY_ON = 1, FG_RAMP_KEY_DRAIN = 2 };                // ramp's bits: the switch; the drain
// 4: the drain's gate reads the queue directly (RampQueueTick)
enum { FG_RAMP_KEY_QREAD = 4 };
// The standing-by queue (RampQueueTick): the last presents' blocks inside DXVK's Present, us.
enum { FG_RAMPQ_N = 8 };
struct __declspec(align(64)) FgRampQ { LONGLONG sumT; LONG blk[FG_RAMPQ_N]; LONG at, n, cnt, blkAt; };
// The render thread's.
struct __declspec(align(64)) FgArmedRender
{
    LONG lastWas;          // the present before this one was engaged
    LONG hud;              // FG_HUD_*: this frame's HUD-less picture (B::HudWant)
    LONG hudDrawn;         // a HUD-less picture was drawn behind this hand
        // ---- at the end, for the layout: the game slower than the pacer (PublishApplied)
    LONG slowN;            // presents in a row past FgPace's due
    volatile LONG ownN;    // hands that carried the game's own period
        // ---- at the end: the ride (ArmedRides)
    LONG ride;
    volatile LONG rideN;   // hands that carried FG_HF_RIDE
        // ---- at the end
    volatile LONG spinWakes;   // the placer's wakes FgPace's spin heard
        // ---- the switch-on, at the end: inside the line's last 28 bytes (it stays one line)
    LONG rampSt;
    LONG rampToUs;
    LONG rampFloorUs;      // the period is never under this while it stands (0 none)
    volatile LONG rampHeld;    // presents the floor raised
    volatile LONG ramps;
    LONG rampDrain;        // the drain's presents left (CAP)
};
static_assert(sizeof(FgArmedRender) == 64, "FgArmedRender is one 64-byte line: a field more moves the worker's line behind it");
// The worker's.
struct __declspec(align(64)) FgArmedWorker
{
    LONG held;             // HOLD holds the open pair's real frame (its span was taken)
    LONG show;             // its generated frame may be shown
    LONG why;              // FG_AW_*: why not
    LONG primeNext;        // the helper's own "my next dispatch resets", mirrored
    LONG blitOut;          // a Present since the last flush: its blit may still stand in
                           // DXVK's stream
    volatile LONG iWhy[FG_AW_N];
    volatile LONG inl[FG_AI_N];
    volatile LONG dropped[FG_AD_N];
    volatile LONG guardForced;         // real frames forced out ahead of a wait behind fgPrep
    volatile LONG guardFlushes;        // flushes between a present and that wait
    volatile LONG saves, noSaves;
    volatile LONG restoreLost;         // a restore the queue did not take (the machine then stands down)
    volatile LONG fedReset;            // DISPATCHes written with a reset: fed, never shown
    volatile LONG gateRead;            // answers that changed while the present was held in the gate
    // ---- THE HUD-LESS PICTURE
    LONG passK;                        // the ordinal whose hand a HUD-less picture follows (0: none)
    LONG holdK;                        // the ordinal whose real frame HOLD holds
    LONG wrongK;                       // the ordinal whose picture could not be taken back: not
                                       // presented (0: none)
    LONG hudFed;                       // the picture went to the helper with fgIn
    volatile LONG hudPass;
    volatile LONG hudBack;
    volatile LONG hudBackLost;         // ... that could not be
    volatile LONG hudStore;            // hands that stored the frame for the restore alone
    volatile LONG hudFedN, hudFedLost; // pictures handed over; submits not taken
        // ---- the second-target form, at the end
    volatile LONG hudTex;
    volatile LONG hudTexFed;           // ... copied into HUDLESS in the hand's span
    volatile LONG hudTexLost;          // ... the image not named: fed with the presented picture,
                                       // never shown (FG_AW_NOHUD)
    LONG texAsk, texGot;               // the hand's question to its span, and the answer
        // ---- the ride, at the end
    LONG rideAt;                       // a ride pair's real frame is pending behind this body:
                                       // fired behind the saved observer (ArmedAfterInject)
    volatile LONG rode;
    volatile LONG rideGenLost;         // ... with the generated frame unresolved (withheld)
    volatile LONG rideAhead;           // ride frames fired ahead of the body all the same (a wait
                                       // about to be queued, or pipelined)
        // ---- the helper's answer on a card that is the limit, at the end
    LONG overPrev;                     // the dispatch the open pair copied over (0: none)
    LONG overClean;                    // clean hands in a row (FG_OVER_CLEAN)
    volatile LONG overN;
    LONG since;                        // dispatches since the last reset
    LONG cardAsked;                    // this engage asked for the card's place
    volatile LONG cardUp;              // it stands asked for (ArmedCardDown gives it back)
    volatile LONG cardAsks;
    LONG ordAsk;                       // the hand asks its span for the order wait
    LONG ordPrevTop;                   // ordTop at the worker's last present
    volatile LONG ordTop;              // the highest pair an order wait was queued for
    volatile LONG ordSig;              // the highest pair its fence was signalled for
    volatile LONG ordFirstP;           // the engage's first pair with an order wait (the helper line)
    volatile LONG ordWaits, ordRefused;            // order waits queued; refused
    volatile LONG ordRelPresent, ordRelHand, ordRelStop;   // released ahead of a present, at the hand, at a stop
    alignas(8) volatile LONGLONG ordAt;
        // ---- every generated frame shown on a busy card, at the end
    volatile LONG rideEarlyN;          // shown early at the next injection
    volatile LONG rideNoAns;           // their answer not in there
    volatile LONG rideEarlyNot;        // in, but the dispatch copied over gave no frame
    volatile LONG ordRelInj;           // order waits released there
        // ---- the worker ahead of the card, at the end
    volatile LONG rideWaitN;           // waits for the answer at the next injection
    volatile LONG rideWaitGot;         // ... that got this pair's frame
    volatile LONG rideWaitMaxUs;       // ... the longest, us (the line resets it)
        // ---- the per-pair instrument, at the end
    LONG rideSrc;                      // the pair rides and will dispatch with a source
    volatile LONG rideSrcN;            // ... and its span was taken
    volatile LONG ordRelFire;          // order waits released at the generated frame's fire
        // ---- the prepare's wait behind the answer, at the end
    volatile LONG prepWaitN;
    volatile LONG prepWaitGot;         // ... that ended with the prepare done
    volatile LONG prepWaitMaxUs;       // ... the longest, us (the line resets it)
        // ---- the generated frame placed by the card, at the end (the worker's, but the counts and the ring the line reads)
    LONG ckPrev;                       // the next frame's checkpoint count, as the last one ran
    LONG posAt;                        // the open pair's position (0: none, the clock due)
    LONG posP;                         // ... for this pair
    LONG posDone;                      // ... met
    LONG placeF;                       // f, in 32nds of the count
    LONG placeErr[16]; LONG placeErrN; // the last pairs' errors on the card, us
    LONG posF[64], posPP[64];          // positioned pairs, by frame end & 63
    volatile LONG posFires;
    volatile LONG posWaits, posWaitGot, posWaitMaxUs;   // waits at the position; got the answer; the longest
    volatile LONG posNoAns;            // positions with no answer: to the fallbacks
    volatile LONG posInj;              // fallen to the next injection
    volatile LONG posDue;              // fallen to the clock due
    volatile LONG placeSteps;          // moves of f
    volatile LONG placeN;              // places taken, every shown generated frame's
    volatile LONG placeV[128];         // ... the last 128, thousandths
    volatile LONG posArms;             // ride pairs armed with a position
    volatile LONG posRides;            // ride pairs armed with placing on
        // the second position
    LONG posTry;                       // 0 the first position, 1 the second
    volatile LONG pos2Arms;
    volatile LONG pos2Fires;
    volatile LONG pos2BoundUs;         // the last second wait's bound, us (0: none allowed)
    LONG posPlanUs;                    // the plan at the arm (the bound takes the smaller)
    // what fired each shown generated frame, by frame end, until its stamps come round
    LONG fireKind;                     // FG_PK_POS or _POS2 while the position fires
    LONG kindFE[64], kindPP[64], kindK[64];   // shown frames: frame end, ordinal, what fired them
    volatile LONG placeNK[5];          // places taken, by what fired the frame
    volatile LONG placePN;             // places of first-position pairs (f's teaching)
    volatile LONG placePV[128];
};
// The pair's record for the placer, written before its ARMED publication: its number on the wire, its 'G''s kind, whether
// its generated frame may be shown (why not: FG_AW_*), the dispatch it copied over, an order wait queued, and dispatches
// since the last reset.
struct FgArmedPair { volatile LONG seq; LONG p, n, kind, show, why; LONG prev, ord, since; };
// The placer's.
enum { FG_ARMED_LIVE = 3 };
struct FgArmedLive
{
    LONG p, n, gen, kind, show, warm, periodUs;
    DWORD withhold, hflags;
    LONGLONG tArm, tEntry, tS, tI, tN, tIRaised;
    bool timed, tsOwn, iRaised, nRaised, kTaken;
    LONG planUs;                       // the plan the slot was fixed with
    LONG fEnd;                         // the frame its real frame ends (where a ride was shown)
    LONG teach;                        // the pair's needs are taken
    LONG prev, ord, since;
    LONG armWait;                      // timed from the arm: its room need owed to its stamp
    LONG idx;                          // pairs since the engage's first
};
struct __declspec(align(64)) FgArmedPlacer
{
    LONG seen, liveN, curGen;
    FgArmedLive live[FG_ARMED_LIVE];
    volatile LONG pubLost, recLost, liveOver;
    volatile LONG tsOwn;               // timed by the placer's own clock (no stamp)
    volatile LONG tsNever;             // timed from the arm
    volatile LONG iShow;
    volatile LONG iNot[FG_AW_N];       // WITHHOLD verdicts, by why
    volatile LONG nRaised;
    volatile LONG nTimed;
        // ---- the room floor (ArmedRoom), the placer's but for roomUs
    volatile LONG roomUs;              // the room floor, us: twice a pair's need of half a period
                                       // (0 until six pairs); the pacer clamps to it
    volatile LONG roomN, roomUp, roomDown;   // pairs measured; rises and returns
    // ---- the floor's statistics (FgRoomTake) and the plan's step up
    volatile LONG roomUpUs, roomDownUs;      // the two statistics at the last pair
    LONG lateP;                        // the last late answer that counted (ArmedPlanLate)
    volatile LONG planUps;
    FgRoomMem room;
        // ---- the ride, at the end (ArmedRideTake)
    volatile LONG rideDUs;             // the ride's place: frame end less handoff retire, the middle
                                       // of the last FG_RIDE_D (0: none yet)
    volatile LONG rideUs;              // the ride's floor, us: the largest of the three below,
                                       // kept where a pair rides (0 until six)
    volatile LONG rideEvenUs, rideWaitUs, rideFitUs;      // EVEN, WAIT, FIT
    volatile LONG rideNeedE, rideNeedW, rideNeedF;        // the sixth largest need behind each
    volatile LONG rideN, rideNoEnd;    // ride pairs measured; rides with no end stamp in a period
    FgRideMem rideMem;
    FgRoomMem rideE, rideW, rideF;
        // ---- at the end: the ride's needs from stamps alone
    LONG rideLastP;                    // the last ride measured (0: none)
    LONG rideLastTeach;                // ... and whether it teaches
    LONGLONG rideLastEnd, rideLastStart;   // ... its frame end's stamp, and its fire's start
    LONG sinceFirst;                   // armed pairs since an engage's first
    volatile LONG rideDownE, rideDownW, rideDownF;   // the third largest need behind each (the way back)
        // ---- the helper's answer on a card that is the limit, at the end
    volatile LONG overPrevNot;         // withheld: the dispatch copied over left no frame
    volatile LONG ordRelAnswer, ordRelDeadline;    // order waits released: answer in; deadline
    LONG ordSeenN; LONGLONG ordSeenAt, ordInAt;    // the wait watched, first seen, first seen retired with no stamp
    volatile LONG kFullN, kSteadyN;    // K over full dispatches, and over steady ones (the 12th behind a reset or later)
    volatile LONG kFull[FG_K_BINS], kSteady[FG_K_BINS];
        // ---- every generated frame shown on a busy card, at the end
    volatile LONG armTaught;           // pairs taught by their late stamp (ArmedRoomFromArm)
    volatile LONG armUntaught;         // ... whose stamp never came
    volatile LONG ordRelStamp;         // releases by the answer's stamper
    volatile LONG ordLag[16];          // releases by lag behind the answer's stamp, 50 us bins
    volatile LONG ordLagMax;           // ... the longest (the line resets it)
        // ---- at the end: the room floor's seed (ArmedRoomSeed)
    LONG seedPend;                     // the seed is owed this engage
    volatile LONG roomSeeded;          // engages seeded
};
struct FgArmedState { FgArmedKeys k; FgArmedRender r; FgArmedWorker w; FgArmedPlacer pl; FgArmedPair pair[FG_RING]; FgRampQ rq; };
