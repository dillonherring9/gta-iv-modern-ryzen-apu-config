// Frame generation in the helper: its state and its calls into AMD's library. The loader reference, the probe of what the library
// offers on this device (handshake caps bits 256 and 512), the wire's three messages ('g' makes the set: its shared images, the
// context, an unseen warm-up pair, and the connection's three fences; 'Q' configures and prepares pair n; 'G' dispatches it or says
// there is no frame), and the serve loop's door (ServeFg, at the end). The wire's table is in source/transport/feed_ipc.h.
// Included from dlssiv-helper.cpp behind DLSSIV_FG, after the FSR section it builds on and before the development arm's headers.
// Rules of this file (the build proofs check each):
//   - One static, a pointer: everything lives in one zeroed heap struct, FgHost, made only with --fg=1, so .bss grows by eight
//     bytes and a helper without --fg carries a null pointer.
//   - No function-local static and no initialised static (the helper's .data length is gated).
//   - Its own latch: a raise inside AMD's frame generation ends frame generation for the process, never the upscaler
//     (FgHost::raised, not g_ffxRaised), with its own refusal line.
//   - The struct is never freed: the latch lasts the process, as the loader never frees the provider.
//   - Nothing here calls a static function with one caller that is inlined into Serve (MakeSharedTex, ResolveOutputFormat):
//     a second caller could end the inlining and change Serve.
#pragma once

// Frame generation's own allocator ring. While frames are served the ring is asked, never waited for: a list of 'Q' or 'G' takes
// its slot only when that slot's last list has retired; otherwise the pair gets "no frame" at once and is counted
// (FgHost::cRingBusy). Only the warm-up inside 'g' waits, up to 2 s: no frame is owed there.
// So the length bounds frame generation's work in flight. FG_RING is two lists for each frame the upscaler's ring can have queued,
// plus one frame: the evaluate of frame k begins only once frame k-3's has retired, so a 'G' of frame k reuses the slot of frame
// k-4's 'G', which stands before that evaluate, and eight never refuses while the upscaler's ring holds. It must exceed three:
// when the card is the limit the product copies over one dispatch in flight, and its early 'Q' is written behind the injection
// before the answers are read. A refused pair costs only its generated frame. The extra allocators cost about 2.7 MB of process
// memory and no video memory. Development builds can shorten it with --fgring=N.
enum { FG_RING = 2 * (Host::kFrames + 1) };

struct FgHost
{
    ffxContext ctx;            // the frame generation context, or null
    uint64_t   id;             // AMD's frame id: exactly one more per frame across Configure, Prepare and Dispatch
    bool       raised;         // THE LATCH: AMD's frame generation code raised in this process, and is never called again
    int        forceRaise;     // development only, the raise lever's bits: 1 a dispatch raises inside its wrapper, 2 a version query does (--fgraise=2)
    int        saidRefused;    // how often the refusal after a raise has been said (once)
        // The library and the provider (FgLoad, FgProbe, FgOffer below)
    HMODULE      dll;          // frame generation's OWN reference to AMD's loader, or null
    ffxFunctions fn;           // the loader's five entry points, read through that reference
    uint64_t     provider;     // the id of the provider the probe took on this device (every create is pinned to it), or 0
    char         providerName[24];   // its version as the library names it, COPIED: the library's string goes with the library
    uint64_t     offered;      // what the hello ack said of frame generation (caps bits 256 and 512). The messages are served
                               // by THIS, what the client was told, and not by the state now: a raise after the handshake
                               // does not turn the client's messages into unknown tags
    bool         probed;       // the question was asked on this device
    bool         agility;      // an Agility runtime beside the helper was taken (main sets it before the first probe)
        // The set and the wire (FgBuild, FgTeardown, FgOnQ, FgOnG below)
    ID3D12Fence*    fIn;       // the wire's three fences, once a connection: made at the first 'g' and never released
    ID3D12Fence*    fPrep;     //   before the process ends, so the game imports them once and its readers need no lock
    ID3D12Fence*    fOut;
    uint64_t        gameFence[3];            // their handles in the game (in, prep, out): the same three in every ack
    ID3D12Resource* img[FEED_FG_PLACES];     // the set's images, in the machine's order; null without a set (and HUDLESS, GEN2 where not asked;
                                             // PRESENT2 and HUDLESS2, the second input set, above the ack's four: where asked, FgIn picks)
    UINT            dispW, dispH;            // the display size the set was made for
    DXGI_FORMAT     fmt;                     // the presented format
    bool            hdr;                     // it is FP16: scRGB, and the luminance pair comes from the display's peak
    bool            built;                   // a context exists for this set and its warm-up ran
    bool            resetNext;               // the next dispatch resets, whatever the game's flags say
    bool            qOpen;                   // a 'Q' was read and its 'G' was not yet
    bool            qPrepared;               // that 'Q' was configured and its prepare was submitted
    bool            qReset;                  // that 'Q' said reset: its dispatch resets too
    int             failRow;                 // dispatches that failed in a row; at three frame generation is over for the set
    UINT64          qN, gN;                  // the last n read on 'Q' and on 'G' (an n at or below it is a DUPLICATE)
    UINT64          prepSig, outSig;         // the last value signalled on fgPrep and on fgOut: nothing at or below it ever is
        // What the helper counts from what it read itself, for the connection
    UINT64          cQ, cG, cDispatch, cNone, cDup, cDrift, cTry;
    UINT64          cReset[5];               // resets by reason: a cut, a build, a device Reset, a gap, and the helper's own
    UINT64          cBuilt, cRefused;        // 'g' answered CREATED, and refused
    int             saidDup, saidDrift, saidFail;
        // Development levers (set by main from its arguments; zero in a release)
    int             raiseAt;                 // --fgraise_at=K: the Kth dispatch of the connection raises inside its wrapper
    int             failAt;                  // --fgfail_at=K: dispatches K, K+1 and K+2 fail before AMD's call
    int             vramRefuse;              // --fgvram_refuse=1: the video memory rule refuses, with the real numbers
    int             slowG;                   // --fgslow_g=MS: sleep that long before the ack of a set that was built (the game's deadline)
    int             dieG;                    // --fgdie_g=1: end this process on 'g', its ack unwritten (the broken pipe the game must survive)
        // Frame generation's own command ring (FgRingMake below)
    ID3D12GraphicsCommandList* list;
    ID3D12CommandAllocator*    alloc[FG_RING];
    UINT64                     allocFence[FG_RING];   // the value of h.fence that retires each allocator's last list; 0 = never used
    int                        slot;
    int                        saidRing;              // the ring's lines so far (a slot that did not retire, a list that did not close): five at most
    bool                       saidNoLuid;            // the video memory reader said once that it has no adapter to ask
    HANDLE                     ev;                    // frame generation's own event for its fence waits (FgWaitFence)
#if DLSSIV_DEV
        // Development levers, behind the switch
    int                        holdQ;                 // --fghold_q=K: the Kth 'Q' of the connection sleeps 8 s before its prepare (a helper alive and not answering)
    int                        slowD;                 // --fgslow_d=MS: every 64th dispatch of the connection sleeps that long first (a dispatch still unanswered at the next pair)
        // The cost bench: one lever and one pointer
    int                        hudEmpty;              // --fghud_empty=1: every configure hands AMD an EMPTY HUD-less colour (the first lever's other arm)
    struct FgCost*             cost;                  // the cost marks (fg_cost.h), or null: made only when they are on
        // AMD's own checking (fg_lib.h)
    int                        check;                 // --fgcheck=1: AMD's own checking on every context, its message hook installed at the load
    UINT64                     checkMsgs;             // AMD's messages through the hook, this process (the first FG_CHECK_MSGS are logged)
        // The development load (fg_lib.h): one pointer
    struct FgLib*              lib;                   // the development load, the module's identity and the debug layer's count (fg_lib.h), or null: made only when asked
#endif
        // Wire 2 (feed_ipc.h): two product fields, a block of their own
    uint32_t                   qWhy;                  // the open 'Q''s reset reasons (its flags' bits 0 to 3): its 'G' may add to them, and each is counted once a pair
    UINT64                     cPipelined;            // 'Q' read with FEED_FG_Q_PIPELINED, for the connection's line
#if DLSSIV_DEV
        // The own arm (fg_own.h): one pointer, behind the product's block so no field before it moves
    struct FgOwn*              own;                   // the own arm: frame generation through the mod's own library and interface (fg_own.h), or null: made only when asked
#endif
        // The answer and ring rules (FgAnswerOut, FgBeginCommands): a block of their own
    UINT64                     outQueued;             // the last value the QUEUE took for fgOut (0: none): the CPU signals fgOut only once the fence has reached it (FgAnswerOut)
    UINT64                     cOutCpu;               // answers on fgOut that the queue refused, so the CPU's to make: each is "no frame"
    UINT64                     cRingBusy;             // lists a 'Q' or a 'G' asked the ring for and did not get, no slot having retired: each is a pair with no frame
    bool                       serving;               // a 'Q' or a 'G' is being served: FgBeginCommands asks the ring and does not wait (FgOnQ and FgOnG set it round their one list)
    bool                       ringBusy;              // the last FgBeginCommands found no retired slot (and nothing else was wrong): not a failure of AMD's
    int                        saidCpu;               // the CPU's answers said so far (three at most)
#if DLSSIV_DEV
        // Their levers and controls, behind the development switch
    int                        ringLen;               // --fgring=N: the ring's length in use, 2 to FG_RING (FG_RING when not given)
    int                        ringWait;              // --fgring_wait=1, a control: a served list waits up to 2 s for its slot
    int                        ringHoldAt, ringHoldN; // --fgring_hold=K, --fgring_holdn=M: the served lists K to K+M-1 of the connection find EVERY slot not retired (planted: the queue itself goes on)
    UINT64                     cBegin;                // served lists asked of the ring so far (the hold lever counts them)
    int                        cpuSame;               // --fgcpu_same=1, a control: a CPU answer carries the queue's own value, 2n behind a dispatch
    int                        refuseAt, refuseN;     // --fgrefuse_at=K, --fgrefuse_n=M: the answers of 'G' K to K+M-1 of the connection are made as if the queue had refused the signal
    int                        removeAt;              // --fgremove_at=K: the device is REMOVED (ID3D12Device5::RemoveDevice) behind the submit of the Kth dispatch of the connection, before its answer
        // The capture (fg_cap.h): one pointer
    struct FgCap*              cap;                   // short runs of consecutive frames with everything the library was given, saved for a desk's replay (--fgcap), or null: made only when asked
#endif
        // Wire 3 (feed_ipc.h, FEED_CAP_FG_WIRE3): a block of its own
    bool                       ackWire3;              // the 'g' being answered said FEED_FG_BUILD_WIRE3: its ack is wire 3's 124 bytes (FgAnswer)
    bool                       wire3;                 // the set in hand was asked at wire 3: its 'G' carry the count in bits 2 and 3
    uint32_t                   genCounts;             // the counts of generated frames the set in hand makes (FEED_FG_GENS_*), 0 without a set
    uint32_t                   genNow;                // the count the dispatch being recorded makes (FgDispatchSubmit sets it for every list): the own arm reads it
    UINT64                     cFlow, cTwo, cGenNo;   // pairs dispatched flow-only and with two frames; DISPATCH pairs that asked a count the set does not make
    int                        saidGen;               // the last of those said so far (three at most)
    bool                       saidWire2;             // a set asked at wire 2 was said, once a connection
        // The second input set (feed_ipc.h, FEED_CAP_FG_TWIN): a block of its own
    bool                       ackTwin;               // the 'g' being answered said FEED_FG_BUILD_TWIN: its ack is FeedFgAck4Ivt's 156 bytes (FgAnswer)
    bool                       twin;                  // the set in hand has the second input set (PRESENT2, and HUDLESS2 where it has HUDLESS)
    uint32_t                   qSet;                  // the set the open 'Q' named (0 the first, 1 the second): its configure's HUDLESS, its dispatch's PRESENT
    UINT64                     cSet1;                 // 'Q' read naming the second set (a set with it)
    UINT64                     cSetBad;               // 'Q' read naming the second set in a set without it: not prepared
    UINT64                     cSetMix;               // DISPATCH 'G' whose set is not its 'Q''s: NEVER DISPATCHED (must be 0)
    FeedFgImageIvt             in2Ack[FEED_FG_PLACES - FEED_FG_IMAGES];   // the places above the four as the ack of the 'g' being answered carries them (FgBuild fills, FgAnswer writes)
        // The prepare at the 'G': with the second set, the open 'Q''s numbers are kept for the prepare its dispatch records in the
    // dispatch's own list (chainOpen while that list is recorded: FgPrepareSubmit and FgDispatchSubmit then begin and end nothing).
    bool                       chainOpen;
    bool                       qCamOn;
    float                      qMs, qNear, qFov, qCam[12];
        // Under Wine and Proton (feed_fg.h): the three fences are the game's, opened here (FgFencesOpen), and the connection's verdict
    // when they could not be opened or the game's test signal was not seen (every later 'g' then answers FEED_FG_ST_FENCES).
    bool                       gameMade;
    bool                       gameFailed;
        // The mod's own library in a shipped helper (fg_own.h): the own arm's pointer where the development block lacks it, and what the
    // handshake adds to the offer.
#if !DLSSIV_DEV
    struct FgOwn*              own;                   // the own arm, made by main at --fg=1 (fg_own.h): frame generation's one arm in a shipped helper, or null
#endif
    uint64_t                   offerWhy;              // FEED_CAP_FG_NOLIB once the own arm refused its file by its identity (not there, not read, another size or hash); 0 otherwise
};
static FgHost* g_fg = nullptr;
// Which arm serves frame generation. Development: the own arm where --fgown asked, AMD's loader otherwise. A shipped helper: the
// own arm always, as a constant, so the stock arm's calls are not compiled in.
#if DLSSIV_DEV
#define FG_OWN_ARM (g_fg->own != nullptr)
#else
#define FG_OWN_ARM true
#endif
// The pair's input: PRESENT or HUDLESS of the set the open 'Q' named (the first set without a second, for the warm-up, and outside
// a pair). k is FEED_FG_PRESENT or FEED_FG_HUDLESS. A HUDLESS2 not made is null, as HUDLESS is.
static ID3D12Resource* FgIn(int k)
{
    if (!g_fg->twin || !g_fg->qSet) return g_fg->img[k];
    return g_fg->img[k == FEED_FG_PRESENT ? FEED_FG_PRESENT2 : FEED_FG_HUDLESS2];
}
// A guide place's format: depth R32_FLOAT, vectors R16G16_FLOAT, as the upscaler's shared two.
static DXGI_FORMAT FgGuideFormat(int k) { return ((k - FEED_FG_GDEPTH0) & 1) ? DXGI_FORMAT_R16G16_FLOAT : DXGI_FORMAT_R32_FLOAT; }
// The pair's guides: with the second set, its own set's two (the hand copied them in the submit that signals fgIn = n); elsewhere
// the upscaler's shared depth and vectors. mv: the vectors, else the depth.
static ID3D12Resource* FgGuide(bool mv)
{
    if (!g_fg->twin) return mv ? h.tex[FEED_MV] : h.tex[FEED_DEPTH];
    return g_fg->img[mv ? FEED_FG_GMV(g_fg->qSet) : FEED_FG_GDEPTH(g_fg->qSet)];
}

#if DLSSIV_DEV
// AMD's own checking (fg_lib.h, --fgcheck=1): its message hook, installed at every load. Declared here because FgLoad calls it;
// not inlined, for the reason at the declarations below.
__declspec(noinline) static void FgCheckAfterLoad();
// The capture (fg_cap.h, --fgcap=N), development builds only: each returns on one null pointer unless asked, and none is called
// from Serve. Not inlined, for the reason at the declarations below.
__declspec(noinline) static void FgCapArm();
__declspec(noinline) static void FgCapDisarm();
__declspec(noinline) static void FgCapOnQ(const FeedFgQIvt* q);
__declspec(noinline) static void FgCapInPrepare(const ffxDispatchDescFrameGenerationPrepareV2* p);
__declspec(noinline) static void FgCapInDispatch(const ffxDispatchDescFrameGeneration* d);
__declspec(noinline) static void FgCapAfterG(UINT64 n, bool generated, uint32_t gFlags, float peak);
__declspec(noinline) static void FgCapAtExit();
#endif

// The state, made once. False, with one line, when there is no memory for it: frame generation is then off for the process.
static bool FgHostMake()
{
    if (g_fg) return true;
    g_fg = (FgHost*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgHost));
    if (!g_fg) LogRecord("frame generation: no memory for its state (%lu bytes): off for this process", (unsigned long)sizeof(FgHost));
#if DLSSIV_DEV
    else g_fg->ringLen = FG_RING;                // the ring's length in use: the build's, until main hands over --fgring=N
#endif
    return g_fg != nullptr;
}

// After a raise nothing of AMD's frame generation is called again, the destroy included; said once. With no state or no library,
// nothing may be called either: every caller tests first, and this is the backstop.
static bool FgRefusedAfterRaise()
{
    if (!g_fg) return true;
    if (g_fg->raised)
    {
        if (g_fg->saidRefused < 1) { g_fg->saidRefused++; LogRecord("frame generation raised an exception earlier in this process: not called again"); }
        return true;
    }
    return g_fg->dll == nullptr;
}

// The calls into AMD's library, each behind frame generation's own latch and SEH frame. A raise is noted by RaiseNote, sets the
// latch and returns 0x7FFFFFFF with the exception's code in *code.
static ffxReturnCode_t SafeFgCreate(ffxCreateContextDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (FgRefusedAfterRaise()) return (ffxReturnCode_t)0x7FFFFFFF;
    __try { return g_fg->fn.CreateContext(&g_fg->ctx, hd, nullptr); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}
static ffxReturnCode_t SafeFgConfigure(ffxContext* ctx, ffxConfigureDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (FgRefusedAfterRaise()) return (ffxReturnCode_t)0x7FFFFFFF;
    __try { return g_fg->fn.Configure(ctx, hd); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}
static ffxReturnCode_t SafeFgDispatch(ffxDispatchDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (FgRefusedAfterRaise()) return (ffxReturnCode_t)0x7FFFFFFF;
    __try
    {
#if DLSSIV_DEV
        if (g_fg->forceRaise & 1) RaiseException(0xE0F60001u, 0, 0, nullptr);   // the fault a community DLL could throw, on purpose
#endif
        return g_fg->fn.Dispatch(&g_fg->ctx, hd);
    }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}
static ffxReturnCode_t SafeFgQuery(ffxContext* ctx, ffxQueryDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (FgRefusedAfterRaise()) return (ffxReturnCode_t)0x7FFFFFFF;
    __try
    {
#if DLSSIV_DEV
        if (g_fg->forceRaise & 2) RaiseException(0xE0F60002u, 0, 0, nullptr);   // the raise lever: a library that faults when asked what it offers
#endif
        return g_fg->fn.Query(ctx, hd);
    }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}
static void SafeFgDestroy()
{
    if (!g_fg || !g_fg->ctx) return;
    if (g_fg->raised || !g_fg->dll) { g_fg->ctx = nullptr; return; }   // a library that raised is not called again, the destroy included
    __try { g_fg->fn.DestroyContext(&g_fg->ctx, nullptr); }
    __except (RaiseNote(GetExceptionInformation())) { g_fg->raised = true; }
    g_fg->ctx = nullptr;
}

// ---- The library, by frame generation's own reference ----
// InitFsr cannot serve here: it sets g_ffxDll (read as "FSR available") and tests behind the upscaler's latch, where a raise would
// end a DLSS session. So frame generation takes its own reference to the loader and its own entry points, behind its own latch:
//   - when the upscaler holds the loader (an FSR session), the same module by its full path: one loader in the process;
//   - otherwise the loader beside the helper only, where the shipped provider lies;
//   - not the single-file SDK 1.1.4 layout (amd_fidelityfx_dx12.dll): its frame generation speaks older descriptions than 2.3.0;
//   - not after the upscaler's library raised.
// Given back at a rebind (FgUnload, just before RebindAdapter), so the loader leaves the process as it would without frame
// generation.
#define FG_PROVIDER_DLL "amd_fidelityfx_framegeneration_dx12.dll"

#if DLSSIV_DEV
// Development load, module identity and debug layer count (fg_lib.h): with --fglib=<file> FgLoad loads that file and never AMD's
// loader. Each returns on one null pointer unless one of its arguments was given; none is called from Serve or inlined.
__declspec(noinline) static int  FgLibLoad();
__declspec(noinline) static void FgLibAtProbe(bool taken);
__declspec(noinline) static void FgLibAtCreate(int status, UINT64 ctxBytes);
__declspec(noinline) static void FgLibAtTeardown();
// The own arm's stamps and rows (fg_own.h): development builds only
__declspec(noinline) static void FgOwnInList();
__declspec(noinline) static void FgOwnAfterG();
#endif
// The own arm (fg_own.h): frame generation through the mod's own library and interface (dlssiv-fg.dll; ffx-own/glue/dlssiv_fg.h),
// where a pair is three calls (prepare, optical flow, interpolate) and AMD's loader is asked for nothing. In a development build
// FgHost::own is null unless --fgown was given; a shipped helper has only this arm (FG_OWN_ARM). Each stands where the stock arm
// calls ffx-api, as one call. None is inlined.
__declspec(noinline) static bool FgOwnLoad();
__declspec(noinline) static void FgOwnAtProbe();
__declspec(noinline) static void FgOwnMemory(UINT dispW, UINT dispH, DXGI_FORMAT fmt, uint32_t ctxFlags, FfxApiEffectMemoryUsage* mu, DWORD* raised);
__declspec(noinline) static ffxReturnCode_t FgOwnCreate(UINT dispW, UINT dispH, DXGI_FORMAT fmt, uint32_t ctxFlags, DWORD* raised);
__declspec(noinline) static ffxReturnCode_t FgOwnConfigure(const ffxConfigureDescFrameGeneration* cfg);
__declspec(noinline) static ffxReturnCode_t FgOwnPrepare(const ffxDispatchDescFrameGenerationPrepareV2* p, DWORD* raised);
__declspec(noinline) static ffxReturnCode_t FgOwnDispatch(const ffxDispatchDescFrameGeneration* d, DWORD* raised);
__declspec(noinline) static void FgOwnDestroy();
// Which counts of generated frames the own arm's library makes (FEED_FG_GENS_*), and why when it makes one only.
__declspec(noinline) static uint32_t FgOwnGenCounts(const char** why);

static bool FgLoad()
{
    if (!g_fg) return false;
    if (g_fg->dll) return true;
    if (g_fg->raised) return false;              // its own line is said by the refusal, once
    if (g_ffxRaised)
    {
        LogRecord("frame generation: not offered, because AMD's library raised an exception earlier in this process (FSR's line above) and is not called again");
        return false;
    }
#if !DLSSIV_DEV
        // A shipped helper: the mod's own library beside it, held to its record, or no frame generation. AMD's provider is never loaded;
    // nothing below is compiled.
    return g_fg->own ? FgOwnLoad() : false;
#else
        // The development load (--fglib; fg_lib.h): the named file by its full path. A refusal is final, never a fallback: only "not
    // asked" (-1) goes on to AMD's loader below.
    if (g_fg->own) return FgOwnLoad();           // the own arm (--fgown; fg_own.h): loaded or refused, nothing below is reached
    if (g_fg->lib)
    {
        const int direct = FgLibLoad();
        if (direct > 0) FgCheckAfterLoad();      // AMD's message hook (--fgcheck=1) is installed at every load, direct or through AMD's loader
        if (direct >= 0) return direct != 0;
    }
    char path[MAX_PATH] = "";
    if (g_ffxDll)
    {
        if (g_ffxDllName != kFfxDlls[0])         // one of kFfxDlls' literals, never a copy: the pointers compare
        {
            LogRecord("frame generation: not offered, because FSR runs on %s here (the single-file layout); it needs %s with " FG_PROVIDER_DLL " beside it",
                      g_ffxDllName, kFfxDlls[0]);
            return false;
        }
        if (!GetModuleFileNameA(g_ffxDll, path, MAX_PATH)) path[0] = 0;
    }
    else _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", g_exeDir, kFfxDlls[0]);   // g_exeDir ends with its backslash
    const HMODULE m = path[0] ? LoadLibraryA(path) : nullptr;
    if (!m)
    {
        const DWORD e = GetLastError();
        LogRecord("frame generation: not offered, because %s would not load (%lu)", path[0] ? path : kFfxDlls[0], (unsigned long)e);
        return false;
    }
    ffxFunctions fn = {};
    ffxLoadFunctions(&fn, m);
    if (!fn.CreateContext || !fn.DestroyContext || !fn.Dispatch || !fn.Query || !fn.Configure)
    {
        LogRecord("frame generation: not offered, because %s does not export AMD's ffx-api entry points (not AMD's file)", path);
        FreeLibrary(m);
        return false;
    }
    g_fg->dll = m; g_fg->fn = fn;
    FgCheckAfterLoad();      // AMD's own checking (--fgcheck=1): its message hook, installed at every load of the library
    return true;
#endif
}

// The reference given back, and everything asked through it. No context exists when the handshake calls this; the destroy is here
// so nothing of the library's outlives the reference.
static void FgUnload()
{
    if (!g_fg) return;
    SafeFgDestroy();
    g_fg->probed = false; g_fg->provider = 0; g_fg->providerName[0] = 0; g_fg->offered = 0;
        // Not unloaded after a raise of either latch: FreeLibrary runs the library's detach code.
    if (g_fg->dll && !g_fg->raised && !g_ffxRaised) FreeLibrary(g_fg->dll);
    g_fg->dll = nullptr; memset(&g_fg->fn, 0, sizeof(g_fg->fn));
}

// ---- The probe ----
// What AMD's library offers for frame generation on this device, asked once in main before the pipe exists and again after a
// rebind. One line says the outcome, whatever it is (test kits require it). The pick: the newest offered version with major
// number 3, AMD's analytical frame generation; a "4.x" (the machine-learning provider) is listed, not taken, chosen by number
// rather than id so a later 3.x is taken without a rebuild. Every create is pinned to the id taken here (ffxOverrideVersion).
// An Agility runtime beside the helper is refused.
static void FgProbe()
{
    if (!g_fg || !h.dev) return;
    g_fg->probed = true; g_fg->provider = 0; g_fg->providerName[0] = 0;
#if DLSSIV_DEV
    FgLibAtProbe(false);     // development: this device's line (which shader variants AMD's code takes) and the debug layer's first count
#endif
    if (g_fg->raised) { FgRefusedAfterRaise(); return; }
    if (g_fg->agility)
    {
        LogRecord("frame generation: not offered with an Agility SDK runtime beside the helper (" DLSSIV_HOST_DIR "\\D3D12): it has not been run on one");
        return;
    }
#if DLSSIV_DEV
    LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0);
#endif
    if (!FgLoad()) return;
    if (FG_OWN_ARM) { FgOwnAtProbe(); return; }  // the own arm: no version query; its load said what it is (a shipped helper always; nothing below compiled)
#if DLSSIV_DEV
    uint64_t n = 0, ids[8] = {}; const char* names[8] = {};
    ffxQueryDescGetVersions q = {};
    q.header.type    = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    q.device         = h.dev;
    q.outputCount    = &n;                       // zero in: the count comes back
    DWORD raised = 0;
    ffxReturnCode_t rc = SafeFgQuery(nullptr, &q.header, &raised);
    if (rc == FFX_API_RETURN_OK && n)
    {
        if (n > 8) n = 8;                        // the capacity in, the count written out
        q.versionIds = ids; q.versionNames = names;
        rc = SafeFgQuery(nullptr, &q.header, &raised);
        if (n > 8) n = 8;
    }
    if (raised)
    {
        LogRecord("frame generation: not offered, because AMD's library RAISED exception 0x%08X%s in its version query; it is not called again in this process",
                  (unsigned)raised, g_raiseWhere);
        return;
    }
    if (rc != FFX_API_RETURN_OK || !n)
    {
        if (rc != FFX_API_RETURN_OK)
            LogRecord("frame generation: not offered, because the library answered the version query with %u (%s); " FG_PROVIDER_DLL " must be beside the loader",
                      (unsigned)rc, FfxName(rc));
        else
            LogRecord("frame generation: not offered, because the library lists no frame generation provider; " FG_PROVIDER_DLL " must be beside the loader");
        return;
    }
    int best = -1; unsigned bestV = 0; char list[512] = ""; int at = 0;
    for (uint64_t i = 0; i < n; i++)
    {
        const char* nm = names[i] ? names[i] : "?";
                // "3.1.6" as three numbers, read by hand: sscanf would pull the C runtime's input machinery into the helper.
        unsigned part[3] = { 0, 0, 0 }; int np = 0; bool digit = false;
        for (const char* p = nm; np < 3; p++)
        {
            if (*p >= '0' && *p <= '9') { part[np] = part[np] * 10 + (unsigned)(*p - '0'); digit = true; }
            else { if (!digit) break; np++; digit = false; if (*p != '.') break; }
        }
        const unsigned v = (part[0] << 20) | ((part[1] & 0x3FFu) << 10) | (part[2] & 0x3FFu);
        if (np >= 1 && part[0] == 3 && (best < 0 || v > bestV)) { best = (int)i; bestV = v; }
        const int w = _snprintf_s(list + at, sizeof(list) - at, _TRUNCATE, "%s\"%s\" id 0x%llX", i ? ", " : "", nm, (unsigned long long)ids[i]);
        at = w < 0 ? (int)sizeof(list) - 1 : at + w;
    }
    if (best < 0)
    {
        LogRecord("frame generation: not offered, because none of the %llu provider(s) the library lists is a 3.x, the kind this build has run (%s)",
                  (unsigned long long)n, list);
        return;
    }
    g_fg->provider = ids[best];
    strncpy_s(g_fg->providerName, sizeof(g_fg->providerName), names[best] ? names[best] : "?", _TRUNCATE);
    QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
    LogRecord("frame generation: available, provider \"%s\" id 0x%llX taken of %llu listed (%s); asked in %.1f ms; the upscaler's library is %s in this process",
              g_fg->providerName, (unsigned long long)g_fg->provider, (unsigned long long)n, list,
              (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart,
              GetModuleHandleA("amd_fidelityfx_upscaler_dx12.dll") ? "mapped" : "not mapped");
    FgLibAtProbe(true);      // development: which module answered, where AMD's loader chose it
#endif
}

// What the handshake says of frame generation, remembered as said: 256 "asked and looked"; 1024 "a usable provider answered, and
// wire 2 is spoken" (FEED_CAP_FG; 512 was wire 1's and is never offered, feed_ipc.h says why); with 1024 always 2048, wire 3
// (FEED_CAP_FG_WIRE3), and 4096, the second input set (FEED_CAP_FG_TWIN). Older games read past bits they do not know. Zero
// without --fg=1. Called after any rebind, so it speaks of the session's device.
static uint64_t FgOffer()
{
    if (!g_fg) return 0;
        // and FEED_CAP_FG_NOLIB where the own arm refused its file by its identity: the game's one warnings line
    g_fg->offered = !g_fg->probed ? 0ull : (FEED_CAP_FG_PROBED | ((g_fg->provider && !g_fg->raised) ? (FEED_CAP_FG | FEED_CAP_FG_WIRE3 | FEED_CAP_FG_TWIN) : 0ull) | g_fg->offerWhy);
    return g_fg->offered;
}

#if DLSSIV_DEV
// The cost marks (fg_cost.h): GPU stamps around AMD's prepare and dispatch and CPU clocks on the serve thread; development builds,
// and nothing unless on (--fg=1 with --log=2 or --gpu_probe=1). Declared above the ring, whose Begin and End call three of them.
// None is called from Serve or inlined.
enum { FGC_CLK_CFG0, FGC_CLK_CFG1, FGC_CLK_BEGIN, FGC_CLK_N_ };   // FgCostClock's three: before and after AMD's configure, and FgBeginCommands' first line
enum { FGC_PREPARE, FGC_DISPATCH };                               // FgCostHead's two kinds of list
__declspec(noinline) static void FgCostArm();
__declspec(noinline) static void FgCostDisarm();
__declspec(noinline) static void FgCostClock(int which);
__declspec(noinline) static void FgCostHead(int kind, bool reset);
__declspec(noinline) static void FgCostAfterAmd();
__declspec(noinline) static void FgCostBeforeClose();
__declspec(noinline) static void FgCostAfterExecute();
__declspec(noinline) static void FgCostAfterG(bool generated);
#endif

// ---- Frame generation's own command ring ----
// Its own: with one ring of three and three submissions a frame, the evaluate's BeginCommands would wait on the CPU for the
// previous frame's evaluate, and the upscaler's Begin and End carry the serve timer, which frame generation must never arm. Shared
// on purpose: the queue and the allocator fence. FgEndCommands raises h.fenceValue and signals h.fence as EndCommands does, so both
// rings retire in one order on one fence and every drain on h.fenceValue covers frame generation's work. One serving thread: no lock.
static void FgRingRelease()   // only with the queue drained: an allocator must not go while a list of its is in flight
{
    if (!g_fg) return;
    if (g_fg->list) { g_fg->list->Release(); g_fg->list = nullptr; }
    for (int i = 0; i < FG_RING; i++) { if (g_fg->alloc[i]) { g_fg->alloc[i]->Release(); g_fg->alloc[i] = nullptr; } g_fg->allocFence[i] = 0; }
    g_fg->slot = 0;
}
static bool FgRingMake()      // at first use; on a failure nothing is left half made
{
    if (!g_fg || !h.dev) return false;
    if (g_fg->list) return true;
    HRESULT hr = S_OK;
    for (int i = 0; i < FG_RING && SUCCEEDED(hr); i++)
        if (!g_fg->alloc[i]) hr = h.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g_fg->alloc[i]);
    if (SUCCEEDED(hr)) hr = h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_fg->alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_fg->list);
    if (FAILED(hr) || !g_fg->list)
    {
        LogRecord("frame generation: its command ring could not be made (0x%08X): frame generation is off for this set", (unsigned)hr);
        FgRingRelease();
        return false;
    }
    g_fg->list->Close();
    g_fg->slot = 0;
    return true;
}
// Frame generation's own fence wait, on an event of its own: a timed-out wait leaves its SetEventOnCompletion armed, which would
// wake a later wait on a shared event early and read as not reached. Each wait loops, re-arming, until its value or its deadline.
static bool FgWaitFence(ID3D12Fence* f, UINT64 v, DWORD ms)
{
    if (f->GetCompletedValue() >= v) return true;
    if (!g_fg) return WaitFenceValue(f, v, ms);
    if (!g_fg->ev) g_fg->ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_fg->ev) return WaitFenceValue(f, v, ms);              // no event to be had: the upscaler's wait
    const ULONGLONG t0 = GetTickCount64();
    for (;;)
    {
        ResetEvent(g_fg->ev);
        if (FAILED(f->SetEventOnCompletion(v, g_fg->ev))) return f->GetCompletedValue() >= v;
        const ULONGLONG gone = GetTickCount64() - t0;
        if (gone < ms) WaitForSingleObject(g_fg->ev, (DWORD)(ms - gone));
        if (f->GetCompletedValue() >= v) return true;
        if (GetTickCount64() - t0 >= ms) return false;
    }
}
// The ring's length in use: the build's, or in a development helper what --fgring=N asked (never more: per-slot state is FG_RING long).
#if DLSSIV_DEV
#define FG_RING_LEN (g_fg->ringLen)
#else
#define FG_RING_LEN FG_RING
#endif
// Served (FgHost::serving, set by FgOnQ and FgOnG): the ring is asked, never waited for (the rule at FG_RING); a slot not retired
// means no list now, with FgHost::ringBusy set so the caller does not take it for AMD's failure. Not served (the warm-up inside
// 'g', the development stamp): a wait of up to 2 s. A field, not a parameter, so this function's signature stays as it is.
static bool FgBeginCommands()
{
    if (!g_fg) return false;
    g_fg->ringBusy = false;
    bool served = g_fg->serving;
    if (!g_fg->list) return false;                               // FgAbortCommands could not recreate the list: a failed pass, as the upscaler's
#if DLSSIV_DEV
    FgCostClock(FGC_CLK_BEGIN);                                  // the cost marks: the CPU's clock before the slot is asked for
#endif
    UINT64 retire = g_fg->allocFence[g_fg->slot];
#if DLSSIV_DEV
        // The hold lever (--fgring_hold, --fgring_holdn): these served lists find the slot not retired, by a value the fence never reaches.
    if (served && g_fg->ringHoldAt > 0 && ++g_fg->cBegin >= (UINT64)g_fg->ringHoldAt && g_fg->cBegin < (UINT64)g_fg->ringHoldAt + (UINT64)g_fg->ringHoldN)
        retire = UINT64_MAX - 1;
    if (g_fg->ringWait) served = false;                          // the control (--fgring_wait=1): every list waits for its slot
#endif
    if (retire != 0 && h.fence->GetCompletedValue() < retire)
    {
        if (served)
        {
            g_fg->ringBusy = true; g_fg->cRingBusy++;
            if (g_fg->saidRing++ < 5) LogRecord("frame generation: ring: slot %d not retired: no frame for this pair", g_fg->slot);
            return false;
        }
        if (!FgWaitFence(h.fence, retire, 2000))
        {
            if (g_fg->saidRing++ < 5) LogRecord("frame generation: the GPU did not retire its allocator slot %d in 2 s", g_fg->slot);
            return false;
        }
    }
    if (FAILED(g_fg->alloc[g_fg->slot]->Reset())) return false;
    return SUCCEEDED(g_fg->list->Reset(g_fg->alloc[g_fg->slot], nullptr));
}
static void FgAbortCommands();   // defined below; FgEndCommands needs it on a failed Close
// Returns 0 when nothing was submitted (EndCommands' contract and reasons). waitIn: the game's fence value this list waits for on
// the queue, 0 for none; queued between a successful Close and the execute, so a failed Close leaves no orphan wait on the queue.
static UINT64 FgEndCommands(UINT64 waitIn = 0)
{
#if DLSSIV_DEV
    FgCostBeforeClose();     // the cost marks: this list's stamps resolved, then the CPU's clock before Close
#endif
    const HRESULT hrClose = g_fg->list->Close();
    if (FAILED(hrClose))
    {
        FgAbortCommands();
        if (g_fg->saidRing++ < 5) LogRecord("frame generation: its command list failed to close 0x%08X: nothing submitted this pass", (unsigned)hrClose);
        return 0;
    }
    if (waitIn) h.queue->Wait(g_fg->fIn, waitIn);
    ID3D12CommandList* lists[] = { g_fg->list };
    h.queue->ExecuteCommandLists(1, lists);
#if DLSSIV_DEV
    FgCostAfterExecute();    // the cost marks: the CPU's clock behind the execute; from here the slot's stamps are owed
#endif
    const UINT64 v = ++h.fenceValue;
    h.queue->Signal(h.fence, v);
    g_fg->allocFence[g_fg->slot] = v;
    g_fg->slot = (g_fg->slot + 1) % FG_RING_LEN;
    return v;
}
static void FgAbortCommands()    // never execute a list AMD's code faulted in
{
    if (!g_fg || !g_fg->list) return;
    __try { g_fg->list->Close(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_fg->list->Release();
    g_fg->list = nullptr;
    const HRESULT hr = h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_fg->alloc[g_fg->slot], nullptr,
                                                __uuidof(ID3D12GraphicsCommandList), (void**)&g_fg->list);
    if (SUCCEEDED(hr)) g_fg->list->Close();
    else
    {
        g_fg->list = nullptr;
        LogRecord("frame generation: its command list could not be recreated after an aborted pass (0x%08X): frame generation stops for this set", (unsigned)hr);
    }
}

// ---- The video memory reader ----
// DXGI's budget and usage of this process on the adapter's local segment: the budget is what the system would let the helper use
// now (it shrinks as others, the game among them, use the card); it is not the card's free memory. The adapter is found again by
// its LUID; with none on file: false, said once.
struct FgVram { UINT64 budget, usage, reservation, available; };
static bool FgVramRead(FgVram* out)
{
    memset(out, 0, sizeof(*out));
    if (!g_luidValid)
    {
        if (g_fg && !g_fg->saidNoLuid) { g_fg->saidNoLuid = true; LogRecord("frame generation: no adapter LUID on file, so the video memory is not read"); }
        return false;
    }
    IDXGIFactory4* fac = nullptr; IDXGIAdapter3* ad = nullptr;
    LUID luid = {}; memcpy(&luid, g_luid, sizeof(luid));
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&fac);
    if (SUCCEEDED(hr) && fac) hr = fac->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter3), (void**)&ad);
    DXGI_QUERY_VIDEO_MEMORY_INFO vi = {};
    if (SUCCEEDED(hr) && ad) hr = ad->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vi);
    else if (SUCCEEDED(hr)) hr = E_FAIL;
    if (ad) ad->Release();
    if (fac) fac->Release();
    if (FAILED(hr)) return false;
    out->budget = vi.Budget; out->usage = vi.CurrentUsage; out->reservation = vi.CurrentReservation; out->available = vi.AvailableForReservation;
    return true;
}

// ---- The set: three images, three fences, one context ----
// The fences are made at a connection's first 'g' and live to the process's end: the game imports them once, needs no lock, and
// every ack carries the same handles. The images and context are the set: made by 'g', given back by the next 'g', a release 'g',
// a rebuild of the upscaler's set (ReleaseSet) and the end, each after a drain.
// No fence is ever signalled to a value at or below its last; a teardown signals nothing; the two answer fences go to the top once
// at exit, as the upscaler's out-fence does.
#define FG_VRAM_MARGIN (256ull << 20)    // what must stay free beside the images and the context: the game's own next allocation

enum { FG_SERVE_HANDLED = 1, FG_SERVE_END = 2, FG_SERVE_GONE = 3 };   // what a message's handler tells the serve loop

#if DLSSIV_DEV
// Three different constants into the set's images, on the development message 'S': the fake client (tools\test-fg-pipe,
// --fake-fg-stamp) reads them through its own imports, after its first layout change, and requires each where the machine's order
// puts it, so a helper and a game that disagree on which handle is which image are caught. PRESENT (0.125, 0.25, 0.5), GEN (0.5,
// 0.125, 0.25), HUDLESS (0.25, 0.5, 0.125), GEN2 (0.5, 0.25, 0.125), PRESENT2 (0.25, 0.125, 0.5), HUDLESS2 (0.125, 0.5, 0.25);
// in 8-bit formats 32, 64, 128 the same way.
static uint16_t FgDevHalf(float f)   // half of a float, for these constants: not a general converter
{
    uint32_t u; memcpy(&u, &f, 4);
    const uint32_t sign = (u >> 16) & 0x8000u;
    const int32_t  exp  = (int32_t)((u >> 23) & 0xFF) - 127 + 15;
    const uint32_t man  = (u >> 13) & 0x3FFu;
    if (exp <= 0)  return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | man);
}
// A copy that fills every pixel with a 4- or 8-byte pattern, recorded on the caller's list; the upload buffer it returns must
// outlive the list.
static ID3D12Resource* FgDevFillTex(ID3D12GraphicsCommandList* cl, ID3D12Resource* tex, const void* pat, UINT patBytes)
{
    const D3D12_RESOURCE_DESC rd = tex->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    h.dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* up = nullptr;
    if (FAILED(h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&up))) return nullptr;
    uint8_t* p = nullptr; D3D12_RANGE none = { 0, 0 };
    if (FAILED(up->Map(0, &none, (void**)&p))) { up->Release(); return nullptr; }
    for (UINT y = 0; y < rows; y++)
    {
        uint8_t* row = p + (size_t)y * fp.Footprint.RowPitch;
        for (UINT64 x = 0; x + patBytes <= rowBytes; x += patBytes) memcpy(row + x, pat, patBytes);
    }
    up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = tex; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
    src.pResource = up;  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;  src.PlacedFootprint = fp;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    return up;
}
static bool FgDevStamp()
{
    static const float k[FEED_FG_PLACES][3] = { { 0.125f, 0.25f, 0.5f }, { 0.5f, 0.125f, 0.25f }, { 0.25f, 0.5f, 0.125f }, { 0.5f, 0.25f, 0.125f },
                                                { 0.25f, 0.125f, 0.5f }, { 0.125f, 0.5f, 0.25f } };
    if (!FgBeginCommands()) return false;
    ID3D12Resource* up[FEED_FG_PLACES] = {}; bool ok = true;
    for (int i = 0; i < FEED_FG_PLACES; i++)
    {
        if (!g_fg->img[i]) continue;                             // (wire 2: a set without the HUD-less image has two; wire 3: GEN2 only where asked)
        if (FEED_FG_IS_GUIDE(i)) continue;                       // (the guides are render-sized in other formats: the hand's copies fill them, nothing stamps them)
        uint16_t p16[4] = { FgDevHalf(k[i][0]), FgDevHalf(k[i][1]), FgDevHalf(k[i][2]), FgDevHalf(1.0f) };
        uint8_t  p8[4]  = { (uint8_t)(k[i][0] * 256.0f), (uint8_t)(k[i][1] * 256.0f), (uint8_t)(k[i][2] * 256.0f), 255 };
        if (g_fg->fmt == DXGI_FORMAT_B8G8R8A8_UNORM) { const uint8_t t = p8[0]; p8[0] = p8[2]; p8[2] = t; }
        up[i] = FgDevFillTex(g_fg->list, g_fg->img[i], g_fg->hdr ? (const void*)p16 : (const void*)p8, g_fg->hdr ? 8u : 4u);
        ok = ok && up[i];
    }
    const UINT64 v = FgEndCommands();
    ok = ok && v && WaitFenceValue(h.fence, v, 5000);
    for (int i = 0; i < FEED_FG_PLACES; i++) if (up[i]) up[i]->Release();
    return ok;
}
// The connection's end for the development arm (fg_cap.h). Not inlined on purpose: FgAtExit is inlined into Serve, so whatever it
// calls inline would be laid out inside Serve too.
__declspec(noinline) static void FgDevAtExit();
#endif

// The five presented formats the helper knows, as an allow-list: anything else is refused at 'g', never handed to AMD's mapping.
static uint32_t FgFfxFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_UNORM:  return FFX_API_SURFACE_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:     return FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM:     return FFX_API_SURFACE_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R11G11B10_FLOAT:    return FFX_API_SURFACE_FORMAT_R11G11B10_FLOAT;
    default:                             return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

static void FgCloseInGame(HANDLE hgame, uint64_t* handle)
{
    if (!*handle) return;
    if (!DuplicateHandle(hgame, (HANDLE)(uintptr_t)*handle, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE))
        LogRecord("frame generation: a handle of a refused set could not be closed in the game (%lu): held until the game exits", GetLastError());
    *handle = 0;
}

// One shared image of the set, its handle duplicated into the game. Frame generation's own maker: MakeSharedTex has one caller and
// is inlined into Serve, and a second caller could end that. COMMON, simultaneous access, unordered access only where asked.
static ID3D12Resource* FgMakeImage(UINT w, UINT hh, DXGI_FORMAT fmt, bool uav, HANDLE hgame, uint64_t* inGame)
{
    *inGame = 0;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = hh; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | (uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* t = nullptr; HANDLE local = nullptr, remote = nullptr;
    HRESULT hr = h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&t);
    if (SUCCEEDED(hr)) hr = h.dev->CreateSharedHandle(t, nullptr, GENERIC_ALL, nullptr, &local);
    if (SUCCEEDED(hr) && !DuplicateHandle(GetCurrentProcess(), local, hgame, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS)) hr = HRESULT_FROM_WIN32(GetLastError());
    if (local) CloseHandle(local);
    if (FAILED(hr))
    {
        LogRecord("frame generation: a shared image %ux%u of format %u could not be made (0x%08X)", w, hh, (unsigned)fmt, (unsigned)hr);
        if (t) t->Release();
        return nullptr;
    }
    *inGame = (uint64_t)(uintptr_t)remote;
    return t;
}

// Under Wine and Proton (feed_fg.h): the game's three fences, opened as the upscaler's two are (OpenGameSemaphores) and proved
// before anything waits on them: the game CPU-signalled its in-fence to FEED_FG_WINE_PROBE_N, which must read so here within the
// bound; then this side CPU-signals fgPrep and fgOut to the same value for the game to read. A fence that never connects costs the
// bound once and refuses frame generation for the connection, the upscaler untouched. The duplicated handles are closed here, once.
static bool FgFencesOpen(const FeedFgFencesIvt* fx)
{
    const HANDLE hs[3] = { (HANDLE)(uintptr_t)fx->in, (HANDLE)(uintptr_t)fx->prep, (HANDLE)(uintptr_t)fx->out };
    ID3D12Fence* f[3] = {}; HRESULT hr[3] = { E_HANDLE, E_HANDLE, E_HANDLE };
    for (int i = 0; i < 3; i++) if (hs[i]) hr[i] = h.dev->OpenSharedHandle(hs[i], __uuidof(ID3D12Fence), (void**)&f[i]);
    for (int i = 0; i < 3; i++) if (hs[i]) CloseHandle(hs[i]);
    const bool opened = f[0] && f[1] && f[2] && SUCCEEDED(hr[0]) && SUCCEEDED(hr[1]) && SUCCEEDED(hr[2]);
    const bool seen = opened && FgWaitFence(f[0], FEED_FG_WINE_PROBE_N, FEED_FG_WINE_PROBE_MS);
    if (!seen)
    {
        g_fg->gameFailed = true;
        if (!opened) LogRecord("frame generation: the game's three fences could not be opened (0x%08X, 0x%08X, 0x%08X): refused for this connection", (unsigned)hr[0], (unsigned)hr[1], (unsigned)hr[2]);
        else LogRecord("frame generation: the game's test signal on its in-fence was not seen within %u ms (it reads %llu): refused for this connection", FEED_FG_WINE_PROBE_MS, (unsigned long long)f[0]->GetCompletedValue());
        for (int i = 0; i < 3; i++) if (f[i]) f[i]->Release();
        return false;
    }
    f[1]->Signal(FEED_FG_WINE_PROBE_N); f[2]->Signal(FEED_FG_WINE_PROBE_N);
    g_fg->fIn = f[0]; g_fg->fPrep = f[1]; g_fg->fOut = f[2];
    g_fg->gameFence[0] = fx->in; g_fg->gameFence[1] = fx->prep; g_fg->gameFence[2] = fx->out;   // (the ack ECHOES the game's three)
    g_fg->prepSig = FEED_FG_WINE_PROBE_N; g_fg->outSig = FEED_FG_WINE_PROBE_N;   // never a value at or below the last
    g_fg->gameMade = true;
    return true;
}
// The three fences, once a connection. True when they exist (made now or before). On a failure nothing is left half made.
// (Wine: fx, the game's three, opened instead of made; a connection takes one kind or the other, never both)
static bool FgFencesMake(HANDLE hgame, const FeedFgFencesIvt* fx = nullptr)
{
    if (g_fg->fIn) return (fx != nullptr) == g_fg->gameMade;
    if (fx) return FgFencesOpen(fx);
    ID3D12Fence* made[3] = {}; uint64_t inGame[3] = {}; bool ok = true;
    for (int i = 0; i < 3 && ok; i++)
    {
        HANDLE local = nullptr, remote = nullptr;
        ok = SUCCEEDED(h.dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&made[i])) && made[i]
          && SUCCEEDED(h.dev->CreateSharedHandle(made[i], nullptr, GENERIC_ALL, nullptr, &local))
          && DuplicateHandle(GetCurrentProcess(), local, hgame, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (local) CloseHandle(local);
        inGame[i] = (uint64_t)(uintptr_t)remote;
    }
    if (!ok)
    {
        LogRecord("frame generation: its three shared fences could not be made (%lu)", GetLastError());
        for (int i = 0; i < 3; i++) { FgCloseInGame(hgame, &inGame[i]); if (made[i]) made[i]->Release(); }
        return false;
    }
    g_fg->fIn = made[0]; g_fg->fPrep = made[1]; g_fg->fOut = made[2];
    for (int i = 0; i < 3; i++) g_fg->gameFence[i] = inGame[i];
    g_fg->prepSig = 0; g_fg->outSig = 0;
    return true;
}

// The set given back: the context and the images, not the fences, latch, probe result or counters. The caller has drained the
// queue. Nothing is signalled. An open 'Q' stays open: its 'G' is still owed and is answered "no frame".
static void FgTeardown()
{
    if (!g_fg) return;
#if DLSSIV_DEV
    FgCapDisarm();           // the capture: an open run is dropped; after the connection's end its writer gets its time
    FgCostDisarm();          // the cost marks: the last lists' stamps read, their closing line, then their heap and buffers, behind that drain too
#endif
    FgOwnDestroy();          // the own arm: its last lists read, then its destroy in its own frame; ctx is null after, so the stock destroy finds nothing
    SafeFgDestroy();
    for (int i = 0; i < FEED_FG_PLACES; i++) if (g_fg->img[i]) { g_fg->img[i]->Release(); g_fg->img[i] = nullptr; }   // (and the second input set's two)
#if DLSSIV_DEV
    FgLibAtTeardown();       // development: the debug layer's count, and whether AMD's module is mapped; reads "built" before the line below clears it
#endif
    g_fg->built = false; g_fg->qPrepared = false; g_fg->failRow = 0; g_fg->resetNext = true;
    g_fg->wire3 = false; g_fg->genCounts = 0;   // (wire 3: what the set made goes with it)
    g_fg->twin = false; g_fg->qSet = 0;         // (the second input set goes with it too)
}

// A queue signal that never goes back: signalled behind what is queued, and only above the last value on that fence. Where the
// queue refuses (a removed device) the CPU signals, so no game wait is left on a dead queue. For fgPrep only: fgOut's answer is
// FgAnswerOut's.
static void FgSignal(ID3D12Fence* f, UINT64* last, UINT64 v)
{
    if (!f || v <= *last) return;
    *last = v;
    if (FAILED(h.queue->Signal(f, v))) f->Signal(v);
}

#if DLSSIV_DEV
// The removal lever (--fgremove_at=K): this process's own device is removed by D3D12's own call, behind a dispatch's submit and
// before its answer. FgOnG then finds the device gone and the session ends at that 'G'.
static void FgDevRemove(UINT64 n)
{
    ID3D12Device5* d5 = nullptr;
    const HRESULT qi = h.dev->QueryInterface(__uuidof(ID3D12Device5), (void**)&d5);
    if (SUCCEEDED(qi) && d5) { d5->RemoveDevice(); d5->Release(); }
    LogRecord("frame generation: --fgremove_at: the device is REMOVED behind the submit of pair %llu, before its answer (the interface 0x%08X, the device's reason now 0x%08X)",
              (unsigned long long)n, (unsigned)qi, (unsigned)h.dev->GetDeviceRemovedReason());
}
#endif

// fgOut's answer for pair n. Two rules:
//   1. A CPU signal never says "a frame". 2n means the generated image is pair n's, written by a dispatch that retired, and only a
//      queue signal behind the dispatch's list can say that. Where the queue refuses the signal the pair is answered 2n+1 from the
//      CPU, counted, and is no dispatch. (A queue may also accept signals after RemoveDevice and run nothing, so FgOnG asks the
//      device itself after a dispatch's submit.)
//   2. The CPU signals only when nothing the queue took for fgOut is still to land: a fence takes the last value signalled, so an
//      older queued 2m landing after a CPU 2n+1 would move it back into "a frame". Otherwise nothing is signalled for this pair; the
//      game reads an unanswered pair as no frame, and a later answer or the exit's top passes it.
// Nothing at or below the last value is ever signalled. Returns whether the answer says "a frame".
static bool FgAnswerOut(UINT64 n, bool generated)
{
    const UINT64 v = generated ? FEED_FG_OUT_GEN(n) : FEED_FG_OUT_NONE(n);
    if (!g_fg->fOut || v <= g_fg->outSig) return generated;
    bool took = false;
#if DLSSIV_DEV
    // THE REFUSAL LEVER (--fgrefuse_at, --fgrefuse_n): these answers are made as if the queue had refused the signal
    if (!(g_fg->refuseAt > 0 && g_fg->cG >= (UINT64)g_fg->refuseAt && g_fg->cG < (UINT64)g_fg->refuseAt + (UINT64)g_fg->refuseN))
#endif
    took = SUCCEEDED(h.queue->Signal(g_fg->fOut, v));
    if (took) { g_fg->outSig = v; g_fg->outQueued = v; return generated; }
    g_fg->cOutCpu++;
#if DLSSIV_DEV
    if (g_fg->cpuSame)       // the control (--fgcpu_same=1): the CPU says the queue's own value, at once
    {
        g_fg->outSig = v; g_fg->fOut->Signal(v);
        return generated;
    }
#endif
    const UINT64 none = FEED_FG_OUT_NONE(n);
    const bool landed = g_fg->fOut->GetCompletedValue() >= g_fg->outQueued;
    g_fg->outSig = none;
    if (landed) g_fg->fOut->Signal(none);
    if (g_fg->saidCpu++ < 3) LogRecord("frame generation: pair %llu: the queue refused its answer: %s", (unsigned long long)n,
                                       landed ? "no frame, from the CPU" : "not answered (an older answer is still queued)");
    return false;
}

// AMD's three per-pair calls, each on the state as it stands. The frame id is FgHost::id: the caller raises it by exactly
// one before a pair's configure, and the pair's prepare and dispatch carry the same one.
static ffxReturnCode_t FgConfigureNow(DWORD* raised)
{
    ffxConfigureDescFrameGeneration cfg = {};
    cfg.header.type            = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    cfg.frameGenerationEnabled = true;
    cfg.allowAsyncWorkloads    = false;
        // Only an image that exists is bound: a set asked with FEED_FG_BUILD_NO_HUDLESS has none, and its HUD-less colour stays empty, which
    // AMD's header allows and which gives the same generated image. With the second input set, the HUD-less image of the set the open
    // 'Q' named (FgIn).
    bool bindHud = FgIn(FEED_FG_HUDLESS) != nullptr;
#if DLSSIV_DEV
        // --fghud_empty=1: the HUD-less colour stays empty even for a set that has the image (for desk measurements)
    if (g_fg->hudEmpty) bindHud = false;
#endif
    if (bindHud) cfg.HUDLessColor = ffxApiGetResourceDX12(FgIn(FEED_FG_HUDLESS), FFX_API_RESOURCE_STATE_COMMON);
    cfg.flags                  = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
    cfg.generationRect.left = 0; cfg.generationRect.top = 0;
    cfg.generationRect.width = (int32_t)g_fg->dispW; cfg.generationRect.height = (int32_t)g_fg->dispH;
    cfg.frameID                = g_fg->id;
    if (FG_OWN_ARM) return FgOwnConfigure(&cfg); // the own arm: its interface has no configure; what this one says is kept for the pair's two calls
    return SafeFgConfigure(&g_fg->ctx, &cfg.header, raised);
}
// The prepare on a list of frame generation's own ring, submitted. Returns the allocator fence's value for that list, 0 when
// nothing was submitted (*rc says AMD's code, or stays OK when the ring gave no list).
// Chained (g_fg->chainOpen, a set with the second set): the caller began the list, and the dispatch that follows closes it, so
// nothing is begun or submitted here, the cost marks and the own arm's resolve are the dispatch's, a failed recording aborts the
// caller's list, and a recorded prepare returns 1.
static UINT64 FgPrepareSubmit(float frameMs, bool reset, float nearPlane, float fovY, const float* cam, ffxReturnCode_t* rc, DWORD* raised)
{
    *rc = FFX_API_RETURN_OK;
    const bool chained = g_fg->chainOpen;
    if (!chained && !FgBeginCommands()) return 0;
#if DLSSIV_DEV
    if (!chained) FgCostHead(FGC_PREPARE, reset);   // the cost marks: this list's first stamp, and the CPU's clock before AMD's recording
#endif
    ffxDispatchDescFrameGenerationPrepareV2 p = {};
    p.header.type    = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    p.frameID        = g_fg->id;
    p.commandList    = g_fg->list;
    p.renderSize.width = h.width; p.renderSize.height = h.height;
        // The scale is {1, 1}: FEED_MV holds pixels at the render size, and AMD's header says "(1.0, 1.0) if motion vectors are already in
    // pixel space". One over the render size left the vectors out of the interpolation entirely; tools\fg-replay's planted fault (the
    // scale negated) still sees every frame differ.
    p.motionVectorScale.x = 1.0f; p.motionVectorScale.y = 1.0f;
    p.frameTimeDelta = frameMs > 0.0f ? frameMs : 16.67f;
    p.reset          = reset;
    p.cameraNear     = FLT_MAX;                              // reverse-Z with an infinite far plane: the far end is the "near" here
    p.cameraFar      = nearPlane > 0.0f ? nearPlane : 0.05f;
    p.cameraFovAngleVertical  = fovY > 0.0f ? fovY : 0.7853981f;
    p.viewSpaceToMetersFactor = 1.0f;
        // the pair's guides (FgGuide)
    p.depth          = ffxApiGetResourceDX12(FgGuide(false), FFX_API_RESOURCE_STATE_COMMON);
    p.motionVectors  = ffxApiGetResourceDX12(FgGuide(true),  FFX_API_RESOURCE_STATE_COMMON);
    if (cam)
    {
        for (int k = 0; k < 3; k++) { p.cameraPosition[k] = cam[k]; p.cameraUp[k] = cam[3 + k]; p.cameraRight[k] = cam[6 + k]; p.cameraForward[k] = cam[9 + k]; }
    }
    else { p.cameraUp[1] = 1.0f; p.cameraRight[0] = 1.0f; p.cameraForward[2] = 1.0f; }   // the identity when no camera was sent
    if (FG_OWN_ARM) *rc = FgOwnPrepare(&p, raised); else  // the own arm: its interface's prepare, from this description, into this list
    *rc = SafeFgDispatch(&p.header, raised);
    if (*rc != FFX_API_RETURN_OK) { FgAbortCommands(); return 0; }
#if DLSSIV_DEV
    if (!chained) FgCostAfterAmd();   // the cost marks: the stamp closing AMD's prepare, before anything of the capture's
    FgCapInPrepare(&p);      // the capture: a run's frame begins here (the prepare's numbers, and the depth and vectors it read)
    if (!chained) FgOwnInList();      // the own arm's stamps, resolved into this list's slot: LAST, behind the marks' closing stamps
#endif
    if (chained) return 1;
    return FgEndCommands();
}
// The dispatch on its own list, submitted behind a queue wait on the game's fence when one is owed (waitIn: the value, 0 for none).
// The wait is queued only once the list is recorded and closed (FgEndCommands), so a failed recording leaves no wait on the queue.
// Returns the allocator fence's value, 0 when nothing was submitted. gen: the count this list makes (FEED_FG_G_GEN_*); only the
// own arm makes other than one, for a set whose gen_counts allow it: AMD's dispatch makes one frame at the middle, so another
// count is not recorded on the stock arm.
static UINT64 FgDispatchSubmit(bool reset, float peak, UINT64 waitIn, ffxReturnCode_t* rc, DWORD* raised, uint32_t gen = FEED_FG_G_GEN_ONE)
{
    *rc = FFX_API_RETURN_OK;
    g_fg->genNow = gen;
    const bool own = FG_OWN_ARM;
        // Chained (g_fg->chainOpen): the list is the caller's, begun, the prepare already in it; its head stamp is the caller's.
    const bool chained = g_fg->chainOpen;
    if (gen != FEED_FG_G_GEN_ONE && !own) { if (chained) FgAbortCommands(); *rc = (ffxReturnCode_t)FFX_API_RETURN_ERROR_PARAMETER; return 0; }
    if (!chained && !FgBeginCommands()) return 0;
#if DLSSIV_DEV
    if (!chained) FgCostHead(FGC_DISPATCH, reset);  // the cost marks: this list's first stamp (on the GPU behind the queue's wait on fgIn)
#endif
    ffxDispatchDescFrameGeneration d = {};
    d.header.type        = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
    d.commandList        = g_fg->list;
    d.presentColor       = ffxApiGetResourceDX12(FgIn(FEED_FG_PRESENT), FFX_API_RESOURCE_STATE_COMMON);   // (the pair's own set, FgIn)
    d.outputs[0]         = ffxApiGetResourceDX12(g_fg->img[FEED_FG_GEN],     FFX_API_RESOURCE_STATE_COMMON);
    d.numGeneratedFrames = 1;
    d.reset              = reset;
    d.backbufferTransferFunction = g_fg->hdr ? FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB : FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    d.minMaxLuminance[0] = 0.0f;
    d.minMaxLuminance[1] = (g_fg->hdr && peak > 0.0f) ? peak : 1000.0f;   // the display's peak where the game knows it
    d.generationRect.left = 0; d.generationRect.top = 0;
    d.generationRect.width = (int32_t)g_fg->dispW; d.generationRect.height = (int32_t)g_fg->dispH;
    d.frameID            = g_fg->id;
    if (FG_OWN_ARM) *rc = FgOwnDispatch(&d, raised); else  // the own arm: optical flow, then interpolate: two calls of its interface into this list
    *rc = SafeFgDispatch(&d.header, raised);
    if (*rc != FFX_API_RETURN_OK) { FgAbortCommands(); return 0; }
#if DLSSIV_DEV
    FgCostAfterAmd();        // the cost marks: the stamp that closes AMD's dispatch, the pad's copies and the empty bracket, before the capture's copies
    FgCapInDispatch(&d);     // the capture: the dispatch's numbers, the presented image it read and the image it wrote, copied in video memory
    FgOwnInList();           // the own arm's stamps, resolved into this list's slot, LAST (a resolve draws nothing)
#endif
    return FgEndCommands(waitIn);
}

// The ack in the layout its 'g' spoke (FgHost::ackWire3): wire 3's 124 bytes, or wire 2's 104 (FeedFgAck2Ivt) for a 'g' without
// FEED_FG_BUILD_WIRE3; a set answered in wire 2's layout has three places and makes one frame a pair, so nothing is lost. A 'g'
// with FEED_FG_BUILD_TWIN gets FeedFgAck4Ivt, 156 bytes: wire 3's plus the second set's two images (all zero where none, as on a
// refusal).
static int FgAnswer(HANDLE pipe, FeedFgAckIvt* a, int status)
{
    a->status = status;
    if (status == FEED_FG_ST_CREATED) g_fg->cBuilt++; else if (status != FEED_FG_ST_RELEASED) g_fg->cRefused++;
    if (g_fg->ackTwin)
    {
        FeedFgAck4Ivt a4 = {};
        a4.a = *a;
        if (status == FEED_FG_ST_CREATED)
            for (int k = FEED_FG_IMAGES; k < FEED_FG_PLACES; k++) a4.in2[k - FEED_FG_IMAGES] = g_fg->in2Ack[k - FEED_FG_IMAGES];
        return WriteFull(pipe, &a4, sizeof(a4)) ? FG_SERVE_HANDLED : FG_SERVE_GONE;
    }
    if (g_fg->ackWire3) return WriteFull(pipe, a, sizeof(*a)) ? FG_SERVE_HANDLED : FG_SERVE_GONE;
    FeedFgAck2Ivt a2 = {};
    a2.status = a->status; a2.ffx_rc = a->ffx_rc;
    for (int k = 0; k < 3; k++) a2.img[k] = a->img[k];
    a2.fence_in = a->fence_in; a2.fence_prep = a->fence_prep; a2.fence_out = a->fence_out;
    a2.ctx_bytes = a->ctx_bytes; a2.provider = a->provider; a2.warm_rc = a->warm_rc; a2.warm_us = a->warm_us;
    return WriteFull(pipe, &a2, sizeof(a2)) ? FG_SERVE_HANDLED : FG_SERVE_GONE;
}

// ---- 'g': the set is built, released or refused ----
// Answered synchronously by FeedFgAckIvt, whatever happens: the game's render thread waits for it inside its own build. A refusal
// refuses frame generation for this set, never the build, whose ack is already written. Each refusal is one status and one line.
static int FgBuild(HostPipeReader* rd, HANDLE pipe, HANDLE hgame)
{
    FeedFgBuildIvt b = {}; b.tag = 'g';
    if (!HostPipeRead(rd, ((BYTE*)&b) + 1, sizeof(b) - 1)) return FG_SERVE_GONE;
    // (Wine: the game's three fences follow in the same write, read before any answer so the pipe stays in step)
    FeedFgFencesIvt fx = {};
    const bool gameFences = (b.flags & FEED_FG_BUILD_GAMEFENCES) != 0;
    if (gameFences && !HostPipeRead(rd, &fx, sizeof(fx))) return FG_SERVE_GONE;
    g_fg->ackWire3 = (b.flags & FEED_FG_BUILD_WIRE3) != 0;     // (wire 3: every answer to THIS 'g' is in the layout it spoke, FgAnswer)
    g_fg->ackTwin = g_fg->ackWire3 && (b.flags & FEED_FG_BUILD_TWIN) != 0;   // (the second input set: with wire 3 only, as the game reads it)
    memset(g_fg->in2Ack, 0, sizeof(g_fg->in2Ack));
    FeedFgAckIvt a = {};
    a.fence_in = g_fg->gameFence[0]; a.fence_prep = g_fg->gameFence[1]; a.fence_out = g_fg->gameFence[2];
    a.provider = g_fg->provider;
    LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0); QueryPerformanceFrequency(&qf);
#if DLSSIV_DEV
    if (g_fg->dieG)
    {
        LogRecord("frame generation: --fgdie_g: this helper ends on 'g' with its ack unwritten");
        TerminateProcess(GetCurrentProcess(), 7);
    }
#endif
        // (Wine: a 'g' with the game's own fences is served on its semaphores, as is a release where the fences are the game's; never on
    // the CPU bridge, where nothing crosses on the card)
    if (g_sync != 0 && !gameFences && !((b.flags & FEED_FG_BUILD_RELEASE) && g_fg->gameMade))
    {
        LogRecord("frame generation: 'g' refused: not on this sync path (sync=%d)", g_sync);
        return FgAnswer(pipe, &a, FEED_FG_ST_PLATFORM);
    }
    if (gameFences && g_bridge)
    {
        LogRecord("frame generation: 'g' refused: the game's fences on the CPU bridge (sync=%d)", g_sync);
        return FgAnswer(pipe, &a, FEED_FG_ST_PLATFORM);
    }
    if (gameFences && g_fg->gameFailed) return FgAnswer(pipe, &a, FEED_FG_ST_FENCES);   // (the connection's verdict, said once where it was reached)
    if (b.flags & FEED_FG_BUILD_RELEASE)
    {
        if (h.fenceValue && !FgWaitFence(h.fence, h.fenceValue, FEED_FG_DRAIN_MS))
        {
            LogRecord("frame generation: the release was asked and this helper's queue did not drain in %u ms: nothing is freed", FEED_FG_DRAIN_MS);
            return FgAnswer(pipe, &a, FEED_FG_ST_BUSY);
        }
        const bool had = g_fg->built;
        FgTeardown();
        FgVram vr; const bool rr = FgVramRead(&vr);
        if (had) LogRecord("frame generation: the set is released at the game's word; this process holds %.1f MiB of video memory%s",
                           rr ? vr.usage / 1048576.0 : 0.0, rr ? "" : " (not read)");
        return FgAnswer(pipe, &a, FEED_FG_ST_RELEASED);
    }
    if (g_fg->agility)
    {
        LogRecord("frame generation: 'g' refused: an Agility SDK runtime beside the helper");
        return FgAnswer(pipe, &a, FEED_FG_ST_RUNTIME);
    }
    if (g_fg->raised) { FgRefusedAfterRaise(); LogRecord("frame generation: 'g' refused: AMD's library raised earlier in this process"); return FgAnswer(pipe, &a, FEED_FG_ST_RAISED); }
    if (!g_fg->provider)
    {
        LogRecord("frame generation: 'g' refused: no provider was taken at the probe");
        return FgAnswer(pipe, &a, FEED_FG_ST_NO_PROVIDER);
    }
    if (!h.tex[FEED_DEPTH] || !h.tex[FEED_MV])
    {
        LogRecord("frame generation: 'g' refused: there is no shared set with depth and motion vectors to prepare from");
        return FgAnswer(pipe, &a, FEED_FG_ST_NO_SET);
    }
    const DXGI_FORMAT fmt = (DXGI_FORMAT)b.present_fmt;
    const uint32_t ffxFmt = FgFfxFormat(fmt);
    if (ffxFmt == FFX_API_SURFACE_FORMAT_UNKNOWN)
    {
        LogRecord("frame generation: 'g' refused: the presented format %u is not one of the five this helper knows", (unsigned)b.present_fmt);
        return FgAnswer(pipe, &a, FEED_FG_ST_FORMAT);
    }
    if (b.disp_w < 16u || b.disp_w > 16384u || b.disp_h < 16u || b.disp_h > 16384u || b.render_w != h.width || b.render_h != h.height)
    {
        LogRecord("frame generation: 'g' refused: the display size %ux%u is out of range (16..16384), or the render size %ux%u is not the set's %ux%u",
                  b.disp_w, b.disp_h, b.render_w, b.render_h, h.width, h.height);
        return FgAnswer(pipe, &a, FEED_FG_ST_SIZE);
    }
    if (h.fenceValue && !FgWaitFence(h.fence, h.fenceValue, FEED_FG_DRAIN_MS))
    {
        LogRecord("frame generation: 'g' refused: this helper's queue did not drain in %u ms: the set before is kept, nothing is freed under live GPU work", FEED_FG_DRAIN_MS);
        return FgAnswer(pipe, &a, FEED_FG_ST_BUSY);
    }
    FgTeardown();
    const bool hdr = fmt == DXGI_FORMAT_R16G16B16A16_FLOAT;
    const uint32_t ctxFlags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE
#if DLSSIV_DEV
                            | (g_fg->check ? (uint32_t)FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING : 0u)   // --fgcheck=1: AMD's own checking, on the create and on the memory query (fg_lib.h)
#endif
                            | (hdr ? (uint32_t)FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE : 0u);
    DWORD raised = 0;
        // The video memory rule: what DXGI would still let this process take on the card's own memory must cover the images, the context
    // by the provider's own figure, and a margin; refused with the numbers. With no LUID the rule is not applied (FgVramRead said so).
    // A set asked with FEED_FG_BUILD_NO_HUDLESS has two images: none is made or counted for HUD-less, the memory query is told format
    // UNKNOWN (as AMD's header asks), and the ack's third image stays zero.
    const bool noHud = (b.flags & FEED_FG_BUILD_NO_HUDLESS) != 0;
    const char* const imgWord = noHud ? "two" : "three";
    const UINT64 imgBytes = (noHud ? 2ull : 3ull) * b.disp_w * b.disp_h * (hdr ? 8ull : 4ull);
        // Wire 3: a set asked with FEED_FG_BUILD_GEN2 has a fourth image, made as GEN is and counted by the video memory rule.
    // The set's line keeps its words for the first three (test kits read them); its own line names every wire 3 image with GEN2's size.
    const bool gen2 = g_fg->ackWire3 && (b.flags & FEED_FG_BUILD_GEN2) != 0;
    const UINT64 gen2Bytes = gen2 ? (UINT64)b.disp_w * b.disp_h * (hdr ? 8ull : 4ull) : 0ull;
        // The second input set (FEED_FG_BUILD_TWIN): PRESENT2, and HUDLESS2 where the set has HUDLESS, made as PRESENT and HUDLESS are,
    // counted by the video memory rule.
    const bool twin = g_fg->ackTwin;
        // With the two sets' guides too: a depth R32_FLOAT and a vector R16G16_FLOAT image each at the render size, 8 bytes a pixel a set;
    // such a set's prepare reads its own guides, never the upscaler's shared ones.
    const UINT64 twinBytes = twin ? (noHud ? 1ull : 2ull) * b.disp_w * b.disp_h * (hdr ? 8ull : 4ull) + 2ull * 8ull * (UINT64)h.width * h.height : 0ull;
    const char* const imgWordAll = twin ? (noHud ? (gen2 ? "eight (PRESENT, GEN, GEN2, PRESENT2, the four guides)" : "seven (PRESENT, GEN, PRESENT2, the four guides)") : (gen2 ? "ten" : "nine"))
                                 : !gen2 ? imgWord : noHud ? "three (PRESENT, GEN, GEN2)" : "four";
    FfxApiEffectMemoryUsage mu = {};
    {
        ffxQueryDescFrameGenerationGetGPUMemoryUsageV2 mq = {};
        mq.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE_V2;
        mq.device = h.dev; mq.maxRenderSize.width = h.width; mq.maxRenderSize.height = h.height;
        mq.displaySize.width = b.disp_w; mq.displaySize.height = b.disp_h;
        mq.createFlags = ctxFlags; mq.dispatchFlags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        mq.backBufferFormat = ffxFmt; mq.hudlessBackBufferFormat = noHud ? (uint32_t)FFX_API_SURFACE_FORMAT_UNKNOWN : ffxFmt; mq.gpuMemoryUsageFrameGeneration = &mu;
                // Pinned to the provider the probe took, as the create is: without a context the query goes to the loader's default provider,
        // which may be another one, and a raise there would latch a provider never chosen.
        ffxOverrideVersion mqv = {};
        mqv.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        mqv.versionId   = g_fg->provider;
        mq.header.pNext = &mqv.header;
        if (FG_OWN_ARM) FgOwnMemory(b.disp_w, b.disp_h, fmt, ctxFlags, &mu, &raised); else  // the own arm: what its library says a context of these sizes takes
        if (SafeFgQuery(nullptr, &mq.header, &raised) != FFX_API_RETURN_OK) mu = {};
    }
    if (raised)
    {
        LogRecord("frame generation: 'g' refused: AMD's library RAISED exception 0x%08X%s in its memory query; it is not called again in this process", (unsigned)raised, g_raiseWhere);
        return FgAnswer(pipe, &a, FEED_FG_ST_RAISED);
    }
    a.ctx_bytes = mu.totalUsageInBytes;
    FgVram v0; const bool vRead = FgVramRead(&v0);
    const UINT64 need = imgBytes + gen2Bytes + twinBytes + mu.totalUsageInBytes + FG_VRAM_MARGIN;
    const UINT64 room = (vRead && v0.budget > v0.usage) ? v0.budget - v0.usage : 0;
        // --fgvram_refuse and --fgfail_at are development levers: their tests and words are fenced, so the shipped helper carries neither.
    // FG_PLANTED_NAME is the dispatch's, below.
#if DLSSIV_DEV
#define FG_VRAM_FORCED g_fg->vramRefuse
#define FG_VRAM_FORCED_WORD (g_fg->vramRefuse ? " (FORCED by --fgvram_refuse)" : "")
#define FG_PLANTED_NAME(rc) ((rc) == (ffxReturnCode_t)0x7FFFFFFD ? "PLANTED by --fgfail_at" : FfxName(rc))
#else
#define FG_VRAM_FORCED false
#define FG_VRAM_FORCED_WORD ""
#define FG_PLANTED_NAME(rc) FfxName(rc)
#endif
    if ((vRead && room < need) || FG_VRAM_FORCED)
    {
        LogRecord("frame generation: 'g' refused by the video memory rule%s: the set needs %.1f MiB (%s images %.1f, the context %.1f, a margin %.1f) and this process may still take %.1f MiB (a budget of %.1f, %.1f in use)",
                  FG_VRAM_FORCED_WORD, need / 1048576.0, imgWordAll, (imgBytes + gen2Bytes + twinBytes) / 1048576.0, mu.totalUsageInBytes / 1048576.0,
                  FG_VRAM_MARGIN / 1048576.0, room / 1048576.0, v0.budget / 1048576.0, v0.usage / 1048576.0);
        return FgAnswer(pipe, &a, FEED_FG_ST_VRAM);
    }
    if (!FgFencesMake(hgame, gameFences ? &fx : nullptr)) return FgAnswer(pipe, &a, g_fg->gameFailed ? FEED_FG_ST_FENCES : FEED_FG_ST_OBJECTS);
    a.fence_in = g_fg->gameFence[0]; a.fence_prep = g_fg->gameFence[1]; a.fence_out = g_fg->gameFence[2];
        // The images, in the machine's order. Only GEN (and GEN2) is made with unordered access: frame generation writes it.
    uint64_t inGame[FEED_FG_PLACES] = {};
    bool ok = true;
    for (int k = 0; k < FEED_FG_PLACES && ok; k++)
    {
        if (noHud && k == FEED_FG_HUDLESS) continue;         // (wire 2: a set asked without it has none; its place in the ack stays zero)
        if (!gen2 && k == FEED_FG_GEN2) continue;            // (wire 3: nor the fourth, unless asked)
        if ((!twin || noHud) && k == FEED_FG_HUDLESS2) continue;   // (the second input set: only where asked, and HUDLESS2 only beside HUDLESS)
        if (!twin && (k == FEED_FG_PRESENT2 || FEED_FG_IS_GUIDE(k))) continue;
        // (A1: a guide at the render size in its own format; every other place at the display's size in the presented format)
        g_fg->img[k] = FEED_FG_IS_GUIDE(k) ? FgMakeImage(h.width, h.height, FgGuideFormat(k), false, hgame, &inGame[k])
                                           : FgMakeImage(b.disp_w, b.disp_h, fmt, k == FEED_FG_GEN || k == FEED_FG_GEN2, hgame, &inGame[k]);
        ok = g_fg->img[k] != nullptr;
    }
    ok = ok && FgRingMake();
    int status = ok ? 0 : FEED_FG_ST_OBJECTS;
    ffxReturnCode_t rc = FFX_API_RETURN_OK, warmRc = FFX_API_RETURN_OK;
    if (ok)
    {
        // THE CONTEXT, PINNED to the provider the probe took, then asked which provider it is
        ffxCreateBackendDX12Desc backend = {};
        backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
        backend.device      = h.dev;
        ffxOverrideVersion ovr = {};
        ovr.header.type  = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        ovr.header.pNext = &backend.header;
        ovr.versionId    = g_fg->provider;
        ffxCreateContextDescFrameGenerationVersion ver = {};
        ver.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
        ver.header.pNext = &ovr.header;
        ver.version      = FFX_FRAMEGENERATION_VERSION;
        ffxCreateContextDescFrameGeneration desc = {};
        desc.header.type  = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        desc.header.pNext = &ver.header;
        desc.flags        = ctxFlags;
        desc.displaySize.width   = b.disp_w; desc.displaySize.height   = b.disp_h;
        desc.maxRenderSize.width = h.width;  desc.maxRenderSize.height = h.height;
        desc.backBufferFormat    = ffxFmt;
        if (FG_OWN_ARM) rc = FgOwnCreate(b.disp_w, b.disp_h, fmt, ctxFlags, &raised); else  // the own arm: its interface's create, of the same sizes, format and flags
        rc = SafeFgCreate(&desc.header, &raised);
        a.ffx_rc = (int32_t)rc;
        if (raised) { LogRecord("frame generation: AMD's library RAISED exception 0x%08X%s in its create; it is not called again in this process", (unsigned)raised, g_raiseWhere); status = FEED_FG_ST_RAISED; }
        else if (rc != FFX_API_RETURN_OK || !g_fg->ctx) { LogRecord("frame generation: 'g' refused: the create answered %u (%s)", (unsigned)rc, FfxName(rc)); g_fg->ctx = nullptr; status = FEED_FG_ST_CREATE; }
        else
        if (!FG_OWN_ARM)     // (the own arm has no provider to ask: its library was held to its file's hash and to what it said at its load)
        {
            ffxQueryGetProviderVersion pv = {};
            pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            const ffxReturnCode_t qr = SafeFgQuery(&g_fg->ctx, &pv.header, &raised);
            if (qr != FFX_API_RETURN_OK || pv.versionId != g_fg->provider)
            {
                LogRecord("frame generation: 'g' refused: the context is not the provider that was pinned (asked 0x%llX, it says 0x%llX, query %u)",
                          (unsigned long long)g_fg->provider, (unsigned long long)(qr == FFX_API_RETURN_OK ? pv.versionId : 0), (unsigned)qr);
                status = raised ? FEED_FG_ST_RAISED : FEED_FG_ST_CREATE;
            }
        }
    }
    if (!status)
    {
        // THE WARM-UP: one pair nobody sees, from the blank images and the set's guides, with reset, so the first pair the
        // game sends does not pay for whatever the provider builds at its first dispatch. Bounded: a provider that does
        // not come back in FEED_FG_WARMUP_MS refuses frame generation for the set, and the build went through long ago.
        g_fg->dispW = b.disp_w; g_fg->dispH = b.disp_h; g_fg->fmt = fmt; g_fg->hdr = hdr;
        g_fg->id = 1;
        warmRc = FgConfigureNow(&raised);
        UINT64 vw = 0;
        if (warmRc == FFX_API_RETURN_OK && FgPrepareSubmit(16.67f, true, 0.0f, 0.0f, nullptr, &warmRc, &raised) != 0)
            vw = FgDispatchSubmit(true, 0.0f, 0, &warmRc, &raised);
        if (raised) { LogRecord("frame generation: AMD's library RAISED exception 0x%08X%s in the warm-up; it is not called again in this process", (unsigned)raised, g_raiseWhere); status = FEED_FG_ST_RAISED; }
        else if (!vw) { LogRecord("frame generation: 'g' refused: the warm-up pair was not submitted (%u, %s)", (unsigned)warmRc, FfxName(warmRc)); status = FEED_FG_ST_WARMUP; }
        else if (!FgWaitFence(h.fence, vw, FEED_FG_WARMUP_MS)) { LogRecord("frame generation: 'g' refused: the warm-up pair did not retire in %u ms", FEED_FG_WARMUP_MS); status = FEED_FG_ST_WARMUP; }
    }
    QueryPerformanceCounter(&q1);
    const double tookMs = (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart;
    a.warm_rc = (int32_t)warmRc;
    a.warm_us = (uint32_t)(tookMs * 1000.0);
#if DLSSIV_DEV
    FgLibAtCreate(status, mu.totalUsageInBytes);   // development: the debug layer's count for the create and warm-up, the set's line, and once a load which module generates frames
#endif
    if (status)
    {
                // A refusal after something was made: nothing of the set stays in the game, and the set goes behind a drain (a warm-up prepare may
        // still be on the queue). If the queue does not drain the set is kept, never freed under live work, until the next drain succeeds.
        if (!h.fenceValue || FgWaitFence(h.fence, h.fenceValue, FEED_FG_DRAIN_MS)) FgTeardown();
        else LogRecord("frame generation: the refused set is kept until this helper's queue drains");
        for (int k = 0; k < FEED_FG_PLACES; k++) FgCloseInGame(hgame, &inGame[k]);
        return FgAnswer(pipe, &a, status);
    }
    for (int k = 0; k < FEED_FG_PLACES; k++)
    {
        if (!g_fg->img[k]) continue;                         // (a set with no HUD-less image, or no GEN2: that image of the ack stays all zero)
        FeedFgImageIvt& at = k < FEED_FG_IMAGES ? a.img[k] : g_fg->in2Ack[k - FEED_FG_IMAGES];   // (the second input set's two ride in FeedFgAck4Ivt::in2)
        at.handle = inGame[k]; at.format = (uint32_t)(FEED_FG_IS_GUIDE(k) ? FgGuideFormat(k) : fmt); at.flags = (k == FEED_FG_GEN || k == FEED_FG_GEN2) ? FEED_FG_IMG_UAV : 0u;
    }
        // Wire 3: what this set makes. One frame always; the flow-only pair and two frames only on an arm that can (the own arm, on a
    // library of the mod's interface 3: FgOwnGenCounts), and two only with GEN2. A set of wire 2 makes one.
    g_fg->wire3 = g_fg->ackWire3;
    g_fg->twin = twin; g_fg->qSet = 0;                       // (the second input set: from the set's first pair on; the warm-up ran on the first set)
    g_fg->genCounts = FEED_FG_GENS_ONE;
    const char* genWhy = "on AMD's library, which makes one frame a pair";
    if (g_fg->wire3 && FG_OWN_ARM) g_fg->genCounts |= FgOwnGenCounts(&genWhy) & (FEED_FG_GENS_FLOW | (gen2 ? FEED_FG_GENS_TWO : 0u));
    a.gen_counts = g_fg->genCounts;
    g_fg->built = true; g_fg->resetNext = true; g_fg->failRow = 0; g_fg->qPrepared = false;
    g_fg->saidDup = g_fg->saidDrift = g_fg->saidFail = 0;        // "said at most N times a set": a new set says its own
    FgVram v1; const bool v1Read = FgVramRead(&v1);
    LogRecord("frame generation: created at %ux%u (guides %ux%u, format %u, %s), provider \"%s\"; the context %.1f MiB by the provider's figure, %s images %.1f MiB;"
              " this process's video memory %.1f -> %.1f MiB of a budget of %.1f%s; the create and the warm-up took %.1f ms",
              b.disp_w, b.disp_h, h.width, h.height, (unsigned)fmt, hdr ? "scRGB" : "sRGB", g_fg->providerName, mu.totalUsageInBytes / 1048576.0, imgWord, imgBytes / 1048576.0,
              vRead ? v0.usage / 1048576.0 : 0.0, v1Read ? v1.usage / 1048576.0 : 0.0, v1Read ? v1.budget / 1048576.0 : 0.0, (vRead && v1Read) ? "" : " (not read)", tookMs);
    if (!g_fg->wire3 && !g_fg->saidWire2)
    {   // (a game of wire 2, or one told to speak it, keeps wire 2; said once a connection)
        g_fg->saidWire2 = true;
        LogRecord("frame generation: the game asks its sets at wire 2 (its 'g' has no FEED_FG_BUILD_WIRE3): one generated frame a pair, as before wire 3");
    }
    if (g_fg->wire3)
    {   // (wire 3: the set's images by name, GEN2's size where it has one, and the counts it makes, with why where it is one alone)
                // (the second input set's two images named after the others, with their size)
        char gen2Say[40] = "", twinSay[96] = "";
        if (gen2) _snprintf_s(gen2Say, sizeof(gen2Say), _TRUNCATE, ", GEN2 %.1f MiB", gen2Bytes / 1048576.0);
        if (twin) _snprintf_s(twinSay, sizeof(twinSay), _TRUNCATE, ", the second input set %s and both sets' guides %.1f MiB", noHud ? "PRESENT2" : "PRESENT2, HUDLESS2", twinBytes / 1048576.0);
        LogRecord("frame generation: wire 3: the set's images are PRESENT, GEN%s%s%s; the counts of generated frames it makes a pair: %s1%s%s%s",
                  noHud ? "" : ", HUDLESS", gen2Say, twinSay, (g_fg->genCounts & FEED_FG_GENS_FLOW) ? "0 (flow-only), " : "", (g_fg->genCounts & FEED_FG_GENS_TWO) ? ", 2 (the thirds)" : "",
                  g_fg->genCounts == FEED_FG_GENS_ONE ? ", " : "", g_fg->genCounts == FEED_FG_GENS_ONE ? genWhy : "");
    }
#if DLSSIV_DEV
    FgCostArm();             // the cost marks, armed for this set when they are on: the warm-up pair above is nobody's and carried none
    FgCapArm();              // the capture, armed for this set when it was asked for: one line says what a frame and a run take
    if (g_fg->slowG > 0) { LogRecord("frame generation: --fgslow_g: this helper sleeps %d ms before its ack", g_fg->slowG); Sleep((DWORD)g_fg->slowG); }
#endif
    const int served = FgAnswer(pipe, &a, FEED_FG_ST_CREATED);
    if (served != FG_SERVE_HANDLED)
    {   // the ack was not written: the game never learnt the handles, so nothing there can close them, and an open handle keeps the
                // memory. The fences stay: they live once a connection.
        for (int k = 0; k < FEED_FG_PLACES; k++) { uint64_t unread = k < FEED_FG_IMAGES ? a.img[k].handle : g_fg->in2Ack[k - FEED_FG_IMAGES].handle; FgCloseInGame(hgame, &unread); }
    }
    return served;
}

// ---- 'Q': configure and prepare pair n ----
// The game writes it before its copy of the back buffer: the prepare reads the guides, which the frame message before it already
// queued (the in-fence wait and the evaluate), so it needs no wait and overlaps the game's copy. The answer is fgPrep = n, a queue
// signal, on every path but a duplicate (an n at or below the last 'Q'), which is counted and never signalled. The game's one GPU
// wait on a helper value hangs on that signal.
static int FgOnQ(HostPipeReader* rd)
{
    FeedFgQIvt q = {}; q.tag = 'Q';
    if (!HostPipeRead(rd, ((BYTE*)&q) + 1, sizeof(q) - 1)) return FG_SERVE_GONE;
    g_fg->cQ++;
    if (q.n <= g_fg->qN)
    {
        g_fg->cDup++;
        if (g_fg->saidDup++ < 3) LogRecord("frame generation: DUPLICATE: a 'Q' for pair %llu at or below the last one read (%llu): counted, not answered",
                                           (unsigned long long)q.n, (unsigned long long)g_fg->qN);
        return FG_SERVE_HANDLED;
    }
    if (g_fg->qOpen)
    {
        g_fg->cDrift++;
        if (g_fg->saidDrift++ < 3) LogRecord("frame generation: DRIFT: a 'Q' for pair %llu while pair %llu has had no 'G'", (unsigned long long)q.n, (unsigned long long)g_fg->qN);
    }
    g_fg->qN = q.n; g_fg->qOpen = true; g_fg->qPrepared = false;
        // Wire 2: 'Q''s bits 0 to 3 are the reset reasons known when it was written. The prepare resets on them (or on the helper's own
    // account); they are counted, and kept (qWhy) so the pair's 'G' does not count one twice. PIPELINED is counted. The jitter is on
    // the wire, but AMD is handed 0.
    g_fg->qWhy = q.flags & FEED_FG_Q_RESET_MASK;
    g_fg->qReset = g_fg->qWhy != 0;
    for (int k = 0; k < 4; k++) if (g_fg->qWhy & (1u << k)) g_fg->cReset[k]++;
    if (q.flags & FEED_FG_Q_PIPELINED) g_fg->cPipelined++;
        // The second input set: the set this pair's inputs are in. Its configure binds that set's HUD-less image (FgIn), its dispatch reads
    // that set's PRESENT, and its 'G' must name the same set. A set without the second refuses a 'Q' naming it: not prepared, so its
    // 'G' gets no frame (counted; fgPrep is answered as always).
    g_fg->qSet = (q.flags & FEED_FG_Q_SET1) ? 1u : 0u;
    const bool setBad = g_fg->qSet && !g_fg->twin;
    if (setBad) { g_fg->cSetBad++; g_fg->qSet = 0; }
    else if (g_fg->qSet) g_fg->cSet1++;
#if DLSSIV_DEV
    if (g_fg->holdQ > 0 && g_fg->cQ == (UINT64)g_fg->holdQ)
    {   // the hold lever: a helper alive and not answering a prepare, which the game's watch must end
        LogRecord("frame generation: --fghold_q: this helper sleeps 8 s before the prepare of pair %llu (its 'Q' number %llu of the connection)", (unsigned long long)q.n, (unsigned long long)g_fg->cQ);
        Sleep(8000);
    }
    FgCapOnQ(&q);            // the capture: what the wire carried for this pair, kept for the frame its prepare begins
#endif
    if (g_fg->built && !g_fg->raised && g_fg->failRow < 3 && h.tex[FEED_DEPTH] && h.tex[FEED_MV] && !setBad)
    {
        DWORD raised = 0;
        float cam[12]; memcpy(cam, q.cam, sizeof(cam));       // out of the packed message, onto the stack's own alignment
        g_fg->id++;
#if DLSSIV_DEV
        FgCostClock(FGC_CLK_CFG0);                            // the cost marks: the CPU's clock round AMD's configure
#endif
        ffxReturnCode_t rc = FgConfigureNow(&raised);
#if DLSSIV_DEV
        FgCostClock(FGC_CLK_CFG1);
#endif
        if (rc == FFX_API_RETURN_OK && g_fg->twin)
        {   // a set with the second set prepares at the 'G': the guides it reads are the hand's copies, in the submit that signals
            // fgIn = n, so the prepare goes behind the wait on fgIn in the dispatch's list, and an undispatched pair records nothing: no 'Q'
            // list can stand behind a wait the game may never satisfy. Its numbers are kept until then.
            g_fg->qMs = q.frame_time_ms; g_fg->qNear = q.camera_near; g_fg->qFov = q.fov_y_rad;
            g_fg->qCamOn = (q.flags & FEED_FG_Q_CAMERA) != 0; memcpy(g_fg->qCam, cam, sizeof(cam));
            g_fg->qPrepared = true;                           // (configured; its prepare is the 'G''s)
        }
        else if (rc == FFX_API_RETURN_OK)
        {
            g_fg->serving = true;                             // this list is asked of the ring, never waited for
            g_fg->qPrepared = FgPrepareSubmit(q.frame_time_ms, g_fg->qReset || g_fg->resetNext, q.camera_near, q.fov_y_rad,
                                              (q.flags & FEED_FG_Q_CAMERA) ? cam : nullptr, &rc, &raised) != 0;
            g_fg->serving = false;
        }
        if (raised) LogRecord("frame generation: AMD's library RAISED exception 0x%08X%s preparing pair %llu; it is not called again in this process, and every later pair is answered with no frame",
                              (unsigned)raised, g_raiseWhere, (unsigned long long)q.n);
        else if (!g_fg->qPrepared && rc == FFX_API_RETURN_OK && g_fg->ringBusy) {}   // the ring had no retired slot: counted and said there; the pair gets no frame at its 'G'
        else if (!g_fg->qPrepared && g_fg->saidFail++ < 5)
            LogRecord("frame generation: pair %llu was not prepared (%u, %s): it will be answered with no frame", (unsigned long long)q.n, (unsigned)rc, FfxName(rc));
    }
    FgSignal(g_fg->fPrep, &g_fg->prepSig, q.n);
    return FG_SERVE_HANDLED;
}

// ---- 'G': what became of pair n ----
// Exactly one for every 'Q'. The helper waits on the game's fence for FEED_FG_G_DISPATCH only: the game writes that kind only when
// the submit that signals fgIn = n was taken, so the wait cannot be left on the queue (the upscaler's queue too). Every other kind,
// and every pair the helper cannot generate (not prepared, the latch, three failures, no set), is answered FEED_FG_OUT_NONE(n)
// with no wait, and the next dispatch resets. The answer: fgOut = 2n behind a dispatch, 2n+1 otherwise; a duplicate is counted
// and never signalled.
static int FgOnG(HostPipeReader* rd)
{
    FeedFgGIvt g = {}; g.tag = 'G';
    if (!HostPipeRead(rd, ((BYTE*)&g) + 1, sizeof(g) - 1)) return FG_SERVE_GONE;
    g_fg->cG++;
    if (g.n <= g_fg->gN)
    {
        g_fg->cDup++;
        if (g_fg->saidDup++ < 3) LogRecord("frame generation: DUPLICATE: a 'G' for pair %llu at or below the last one read (%llu): counted, not answered",
                                           (unsigned long long)g.n, (unsigned long long)g_fg->gN);
        return FG_SERVE_HANDLED;
    }
    const bool mine = g_fg->qOpen && g.n == g_fg->qN;      // it closes the open 'Q'
    if (!mine)
    {
        g_fg->cDrift++;
        if (g_fg->saidDrift++ < 3) LogRecord("frame generation: DRIFT: a 'G' for pair %llu, and the open 'Q' is %s%llu", (unsigned long long)g.n,
                                             g_fg->qOpen ? "pair " : "none; the last was pair ", (unsigned long long)g_fg->qN);
    }
    g_fg->gN = g.n;
        // Wire 2: the reset reasons as known at the 'G' (bits 4 to 7). The dispatch resets on them, on its 'Q''s, or on the helper's own
    // account. Each reason is counted once a pair: here only the bits its 'Q' did not carry.
    const uint32_t why = (g.flags & FEED_FG_G_RESET_MASK) >> FEED_FG_G_RESET_SHIFT;
    const uint32_t qWhy = mine ? g_fg->qWhy : 0u;
    for (int k = 0; k < 4; k++) if ((why & ~qWhy) & (1u << k)) g_fg->cReset[k]++;
    const bool told = (why | qWhy) != 0;                     // the game gave a reason, on either message
    bool generated = false, gone = false;
        // Wire 3: the generated count, bits 2 and 3, in a set of wire 3 only (a wire 2 set makes one whatever they hold). A dispatch asking
    // a count the set does not make gets no frame and records nothing: counted, said three times a connection at most, and the next
    // dispatch resets. flowKept: a flow-only pair whose list went to the queue keeps the interpolator's history: no frame, no reset next.
    const uint32_t gen = g_fg->wire3 ? (g.flags & FEED_FG_G_GEN_MASK) >> FEED_FG_G_GEN_SHIFT : FEED_FG_G_GEN_ONE;
    const bool dispatch = (g.flags & FEED_FG_G_KIND_MASK) == FEED_FG_G_DISPATCH;
    const bool genOk = (g_fg->genCounts & (1u << gen)) != 0;
    bool flowKept = false;
    if (dispatch && mine && g_fg->built && !genOk)
    {
        g_fg->cGenNo++;
        if (g_fg->saidGen++ < 3) LogRecord("frame generation: pair %llu asked for %u generated frame(s), which this set does not make (it makes 0x%X by FEED_FG_GENS): no frame",
                                           (unsigned long long)g.n, (unsigned)gen, (unsigned)g_fg->genCounts);
    }
        // The second input set: a dispatch reads the set its 'Q' named, and only if its 'G' names the same one. A 'G' naming the other (or
    // the second in a set without it) is not dispatched, gets no frame, is counted (cSetMix, must stay 0), and the next dispatch resets.
    const uint32_t gSet = (g.flags & FEED_FG_G_SET1) ? 1u : 0u;
    const bool setMix = dispatch && mine && g_fg->built && (gSet != g_fg->qSet || (gSet && !g_fg->twin));
    if (setMix) g_fg->cSetMix++;
    if (dispatch && genOk && mine && !setMix && g_fg->qPrepared && g_fg->built && !g_fg->raised && g_fg->failRow < 3)
    {
        DWORD raised = 0; ffxReturnCode_t rc = FFX_API_RETURN_OK; UINT64 v = 0;
        g_fg->cTry++;
#if DLSSIV_DEV
        if (g_fg->raiseAt > 0 && g_fg->cTry == (UINT64)g_fg->raiseAt) g_fg->forceRaise |= 1;   // the raise lever: this dispatch raises inside its wrapper
        if (g_fg->slowD > 0 && (g_fg->cTry & 63) == 0) Sleep((DWORD)g_fg->slowD);   // the slow dispatch lever: exercises the game's "last dispatch unanswered" path
        const bool planted = g_fg->failAt > 0 && g_fg->cTry >= (UINT64)g_fg->failAt && g_fg->cTry < (UINT64)g_fg->failAt + 3;
        if (planted) rc = (ffxReturnCode_t)0x7FFFFFFD;       // the failure lever: three dispatches in a row fail before AMD's call
        else
#endif
        {
            g_fg->serving = true;                             // this list is asked of the ring, never waited for
            if (g_fg->twin)
            {   // (one list: the prepare from the pair's own guides then the dispatch, behind the wait on fgIn = n; the prepare resets with the
                // dispatch. The ring gives the list or not, as for any served list)
                const bool reset = told || g_fg->resetNext;
                if (FgBeginCommands())
                {
#if DLSSIV_DEV
                    FgCostHead(FGC_DISPATCH, reset);      // (the cost marks: the one list's head, read as a dispatch with its prepare in it)
#endif
                    g_fg->chainOpen = true;
                    if (FgPrepareSubmit(g_fg->qMs, reset, g_fg->qNear, g_fg->qFov, g_fg->qCamOn ? g_fg->qCam : nullptr, &rc, &raised) != 0)
                        v = FgDispatchSubmit(reset, g.peak, g.n, &rc, &raised, gen);
                    g_fg->chainOpen = false;
                }
            }
            else
            v = FgDispatchSubmit(told || g_fg->resetNext, g.peak, g.n, &rc, &raised, gen);
            g_fg->serving = false;
        }
#if DLSSIV_DEV
        g_fg->forceRaise &= ~1;
        if (g_fg->removeAt > 0 && g_fg->cTry == (UINT64)g_fg->removeAt) FgDevRemove(g.n);   // the removal lever: behind the submit, before the answer
#endif
                // The device is asked behind the submit: on a removed device the list still closes and the queue takes every signal while nothing
        // runs, so such a pair is no dispatch, and the session ends here as behind a failed evaluate.
        if (v
#if DLSSIV_DEV
            && !g_fg->cpuSame                                    // (the control: the device is not asked)
#endif
            && DeviceRemoved("a dispatch of frame generation's")) { gone = true; v = 0; }
        if (v)
        {
            g_fg->failRow = 0;
            if (gen == FEED_FG_G_GEN_FLOW) { flowKept = true; g_fg->cFlow++; }   // (wire 3: the flow and the store went; no picture, so no frame)
            else { generated = true; g_fg->cDispatch++; if (gen == FEED_FG_G_GEN_TWO) g_fg->cTwo++; }
            if (g_fg->resetNext && !told) g_fg->cReset[4]++;       // a reset of the helper's own: the pair before had no frame, and the game gave no reason
            g_fg->resetNext = false;
        }
        else if (gone) {}                                        // (its line is DeviceRemoved's; the pair is counted with no frame below)
        else if (!raised && rc == FFX_API_RETURN_OK && g_fg->ringBusy) {}   // the ring had no retired slot: no frame for this pair, counted and said there; not a failed dispatch
        else
        {
            g_fg->failRow++;
            if (raised) LogRecord("frame generation: AMD's library RAISED exception 0x%08X%s dispatching pair %llu; it is not called again in this process, and every later pair is answered with no frame",
                                  (unsigned)raised, g_raiseWhere, (unsigned long long)g.n);
            else if (g_fg->saidFail++ < 5)
                LogRecord("frame generation: the dispatch of pair %llu failed (%u, %s), %d in a row%s", (unsigned long long)g.n, (unsigned)rc,
                          FG_PLANTED_NAME(rc), g_fg->failRow,   // (the planted word is a development build's alone)
                          g_fg->failRow >= 3 ? ": frame generation is over for this set, every later pair is answered with no frame" : "");
        }
    }
    if (mine) g_fg->qOpen = false;
        // The answer (FgAnswerOut has its two rules). A submitted dispatch whose answer the queue refused is no dispatch: nobody can say its
    // list ran, so no frame and the next dispatch resets. A flow-only pair is answered "no frame"; if the queue refused that answer, its
    // history is not kept either.
    const UINT64 outCpu0 = g_fg->cOutCpu;
    if (FgAnswerOut(g.n, generated) != generated) { generated = false; g_fg->cDispatch--; if (gen == FEED_FG_G_GEN_TWO) g_fg->cTwo--; }
    if (flowKept && g_fg->cOutCpu != outCpu0) { flowKept = false; g_fg->cFlow--; }
    if (!generated) { g_fg->cNone++; if (!flowKept) g_fg->resetNext = true; }
#if DLSSIV_DEV
    FgCostAfterG(generated);         // the cost marks: the pair counted; every 64 pairs the line and rows, behind the answer
    FgCapAfterG(g.n, generated, g.flags, g.peak);   // the capture: the run's frame is whole or the run is broken; a whole run goes to its own thread; nothing waits
    FgOwnAfterG();                   // the own arm: its lists that have retired are read and their rows built, behind the answer; nothing waits
#endif
    return gone ? FG_SERVE_END : FG_SERVE_HANDLED;   // (a removed device behind the dispatch's submit: the session ends "by this helper", reason said above)
}

// The tail's teardown can raise too: ReleaseSet's destroy of frame generation's context may set its latch after the tail tested the
// latches. A library that raised is never called again, its detach code included. Called right after that ReleaseSet.
static void FgAfterTailRelease()
{
    if (!g_fg || !g_fg->raised) return;
    LogRecord("frame generation's library raised in the last teardown: this helper ends without unloading it");
    TerminateProcess(GetCurrentProcess(), g_deviceRemoved ? 3 : 0);
}

// At the process's exit, beside the upscaler's own signal and for its reason: the two answer fences go to the top from the CPU, once
// (not fgIn: the game signals that one), and the connection's counts are said.
static void FgAtExit()
{
    if (!g_fg) return;
#if DLSSIV_DEV
    FgDevAtExit();           // the development arm (fg_cap.h): said before a tail that may end without its teardown
#endif
        // The connection's counts, said first: they are final, and what follows can wait up to 2 s while the orphan watchdog may end the
    // process at any moment.
    if (g_fg->cBuilt || g_fg->cRefused || g_fg->cQ || g_fg->cG)
        LogRecord("frame generation: this connection: %llu set(s) built, %llu refused; 'Q' %llu, 'G' %llu, dispatched %llu, answered with no frame %llu; DUPLICATE %llu, DRIFT %llu;"
                  " resets by reason: cut %llu, build %llu, device %llu, gap %llu, the helper's own %llu; pipelined %llu",
                  (unsigned long long)g_fg->cBuilt, (unsigned long long)g_fg->cRefused, (unsigned long long)g_fg->cQ, (unsigned long long)g_fg->cG,
                  (unsigned long long)g_fg->cDispatch, (unsigned long long)g_fg->cNone, (unsigned long long)g_fg->cDup, (unsigned long long)g_fg->cDrift,
                  (unsigned long long)g_fg->cReset[0], (unsigned long long)g_fg->cReset[1], (unsigned long long)g_fg->cReset[2], (unsigned long long)g_fg->cReset[3],
                  (unsigned long long)g_fg->cReset[4], (unsigned long long)g_fg->cPipelined);
        // The two counts in a line of their own, only when one is not zero: the line above is read word for word by test kits.
    if (g_fg->cOutCpu || g_fg->cRingBusy)
        LogRecord("frame generation: this connection, more: answers the queue refused %llu; lists the ring refused %llu",
                  (unsigned long long)g_fg->cOutCpu, (unsigned long long)g_fg->cRingBusy);
        // Wire 3's counts, likewise only when one is not zero. Flow-only pairs are in "answered with no frame" above, pairs of two in
    // "dispatched".
    if (g_fg->cFlow || g_fg->cTwo || g_fg->cGenNo)
        LogRecord("frame generation: this connection, wire 3: flow-only pairs %llu, pairs of two frames %llu, pairs that asked a count their set does not make %llu",
                  (unsigned long long)g_fg->cFlow, (unsigned long long)g_fg->cTwo, (unsigned long long)g_fg->cGenNo);
        // Behind every queued answer first: the drain to the list fence does not prove the last queued answer ran, and a CPU signal to the
    // top landing before a queued signal of n would leave the fence at n. One marker on the queue behind them all, waited for with the
    // drain's bound; a stuck queue costs that bound once and the top is signalled regardless.
    if (!g_deviceRemoved && h.queue && h.fence && (g_fg->fPrep || g_fg->fOut))
    {
        const UINT64 m = ++h.fenceValue;
        if (SUCCEEDED(h.queue->Signal(h.fence, m))) FgWaitFence(h.fence, m, FEED_FG_DRAIN_MS);
    }
        // The top is no pair's "a frame": the game reads fgOut by equality with 2n, and the top is odd.
    if (g_fg->fPrep) g_fg->fPrep->Signal(UINT64_MAX);
    if (g_fg->fOut)  g_fg->fOut->Signal(UINT64_MAX);
    g_fg->prepSig = UINT64_MAX; g_fg->outSig = UINT64_MAX;
}

// ---- Frame generation's door in the serve loop ----
// Serve calls this from its final else, only when frame generation's state exists (--fg=1); without it 'g', 'Q' and 'G' are
// unknown tags. Not inlined: Serve holds the per-frame block. With the state every tag reaching the final else comes here, so the
// unknown-tag line is said here, in the loop's words. Returns FG_SERVE_HANDLED (go on), FG_SERVE_END (the session ends "by this
// helper", reason said here) or FG_SERVE_GONE (the pipe failed: ended "by the game").
__declspec(noinline) static int ServeFg(BYTE tag, HostPipeReader* rd, HANDLE pipe, HANDLE hgame)
{
#if DLSSIV_DEV
        // 'S', development builds of both halves only: three constants into the set's images, answered by one byte (1 stamped, 0 no set or
    // not landed). To a helper without it, an unknown tag.
    if (tag == 'S' && (g_fg->offered & FEED_CAP_FG))
    {
        const BYTE done = (g_fg->built && FgDevStamp()) ? 1 : 0;
        return WriteFull(pipe, &done, 1) ? FG_SERVE_HANDLED : FG_SERVE_GONE;
    }
#endif
    if (tag == 'g' || tag == 'Q' || tag == 'G')
    {
                // Nothing of frame generation is sent without its caps bit (1024): a client that does so has lost track of the handshake, and the
        // session ends with that reason.
        if (!(g_fg->offered & FEED_CAP_FG))
        {
            LogRecord("frame generation: the game sent its message '%c', and this helper did not offer frame generation at the handshake: stopping", (char)tag);
            return FG_SERVE_END;
        }
        if (tag == 'g') return FgBuild(rd, pipe, hgame);
        return tag == 'Q' ? FgOnQ(rd) : FgOnG(rd);
    }
    LogRecord("unknown tag 0x%02X: stopping", tag);
    return FG_SERVE_END;
}
