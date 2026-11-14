// The device thread, part 1 of 5: the ring, the worker, the waits.
// With threaded=1 every D3D9 call the render thread makes is written into a ring and executed by DXVK on a worker a moment
// later, in the same order, so DXVK's D3D9 front end leaves the render thread, which sets the frame rate. The mod's own hooks
// still run on the render thread where they always did; only DXVK moves.
// The invariant: DXVK sees exactly the sequence of calls it would have seen without this layer, later and from another thread.
// Every call is one of:
//   RECORD  written to the ring and answered D3D_OK at once (state, draws);
//   SHADOW  a Get answered from the layer's copy of the state the ring has set;
//   SYNC    the ring is drained first, then the call runs here, directly;
//   PASS    runs here directly with no drain, because its answer or effect cannot depend on anything still in the ring.
// Another thread's call that changes the device or a resource waits until the worker has run everything the render thread
// committed before it (DtOrderForeign). Anything not proven safe to RECORD or SHADOW is SYNC. Code that talks to DXVK or
// Vulkan outside D3D9 opens DT_DIRECT(), which drains the ring and runs the rest of the scope directly.
// The includer provides LogRecord, LogHealth, telemetryOn, WriteSlot and IvtWarn; tools/perfproof/dt_test.cpp includes these
// five files unchanged. A development build also provides the flip classifier's return ring (latFlipRelQpc, latFlipRelBlk,
// latFlipRelN over LAT_RING slots, ivt_latency.h), which DtExec writes after each Present.
#pragma once

enum : DWORD
{
    DT_OP_WRAP     = 0,    // the rest of the ring is empty: go back to 0
    DT_OP_CALL0    = 1,    // DT_OP_CALL0 + n, n = 0..8: dtDx[slot](dtDev, a1..an), stdcall: a DEVICE method
    DT_OP_XCALL0   = 10,   // DT_OP_XCALL0 + n, n = 0..1: fn(self, a1..an), both in the command: a RESOURCE method
    DT_OP_PRESENT  = 16,   // a swapchain Present (five arguments) that also counts a frame
    DT_OP_RELEASE2 = 17,   // two Releases of one object: see dtRelease
    DT_OP_NOTE     = 18,   // a fact for the worker's observers at its place in the stream; DXVK never sees it
        // A recorded state block's Apply: [op] [its Apply original] [block]. Written by DtSbApplyTry (Windows, where the probe's rules
    // held), run by DtExec.
    DT_OP_SBAPPLY  = 19,
};
#define DT_F_VOID 0x100u   // the call returns void, a count or a BOOL: eax is not an HRESULT
#define DT_F_COUNTED 0x200u // a RELEASE2 whose pair dtPend counts (dt_res.h): the worker runs it under the lock
// Bits 9..15 of a CALL0+n command carry the device slot it replays (0..118), and the slot is the callee: the worker calls
// dtDx[slot] on dtDev and finds the slot's observer (dtObs) by the same number. A RELEASE2 keeps bit 9 as DT_F_COUNTED and
// carries no slot; an XCALL and the Present carry callee and object in the command; a NOTE carries its kind in these bits.
// tools/check-dt.py checks each recorder's slot against the dtDx[] index its direct path calls.
#define DT_SLOT(s)      ((DWORD)(s) << 9)
#define DT_SLOT_OF(h)   (((h) >> 9) & 0x7Fu)
static void DtRelease2Counted(void* fn, void* obj);

// The device slots as DXVK filled them, before any hook. Written once by the vtable hook's install, before DtStart makes the
// worker (check-dt.py greps for a second writer), so the worker reads them with no fence.
static void* dtDx[119];

// A device command is    [op | flags | slot << 9 | dwords << 16] [args...] [payload...].
// An explicit command is [op | flags | dwords << 16] [fn] [self] [args...] [payload...]:
// the Present, a RELEASE2 and the two resource methods (XCALL), a few dozen a frame.
// Every D3D9 argument is 32 bits on x86, floats included (stdcall pushes them),
// so one call shape per argument count covers every method.
// A note is  [DT_OP_NOTE | kind << 9 | dwords << 16] [bytes] [payload...].
typedef HRESULT(__stdcall* DtF0)(void*);
typedef HRESULT(__stdcall* DtF1)(void*, DWORD);
typedef HRESULT(__stdcall* DtF2)(void*, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF3)(void*, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF4)(void*, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF5)(void*, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF6)(void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF7)(void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT(__stdcall* DtF8)(void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);

// ---- state, one cache line per writer ----
// Each group is its own aligned line, so the worker's spin on dtPub does not take the producer's line on every recorded call.
//   dtProd  the producer's own, written on every call; only nDw's low dword is read elsewhere, once per foreign call
//           (DtForeignBarrier, dt_install.h: the count that does not wrap with the ring).
//   dtRd    read on every call by the producer and by the worker; written only at arm, disarm and start.
//   dtP2W   the producer's publish index: written every 128 commands, read by the worker.
//   dtW2P   the worker's progress and its Present results: written by the worker, read by the producer when it waits.
//   dtWake  the two sleep flags, exchanged rarely by both sides.
struct __declspec(align(64)) DtProdLine
{
        // How far dtW may go before the slow path must look at dtDone again; never larger than the truth.
    DWORD    w, lim, batch;
    LONGLONG nRec, nDw;                          // commands and dwords recorded; nDw is also the foreign barrier's count
};
struct __declspec(align(64)) DtReadLine
{
        // The switch: the producer's thread id while it is recording, 0 otherwise. Every wrapper asks `fs:[24h] == dtRecTid`, so a
    // call from another thread, or from the producer inside a SYNC or DIRECT scope, goes straight to DXVK. Only the producer
    // writes it.
    volatile DWORD recTid;
        // The ask: another thread that needs the layer unarmed (a Reset's) cannot drain the ring itself, since dtW, dtBatch and
    // dtRecTid are the producer's. It raises this and the producer answers at its next wrapper entry (DtRecAsked), in the branch
    // only the producer takes.
    volatile LONG  disarmAsk;
    DWORD          producer;                     // the render thread, while armed
    volatile bool  armed;
    IDirect3DDevice9* dev;                       // the one device whose calls are recorded
    DWORD*         ring;
    DWORD          ringDw;                       // capacity in dwords
    DWORD          workerTid;
};
struct __declspec(align(64)) DtPubLine
{
    // The producer writes dtPub (commands before it are complete); a ring index,
    // always below dtRingDw. dtPub == dtDone means the ring is empty and idle.
    volatile LONG pub;
};
struct __declspec(align(64)) DtDoneLine
{
    volatile LONG done;                          // the worker: commands before it have RUN
    volatile LONG presDone;                      // the worker: swapchain Presents run
    // A replayed Present that FAILED, kept until the producer's next Present hands it
    // back (dtScPresent). Only failures are written and the producer clears it as it
    // reads, so a success that runs later cannot overwrite one nobody has seen.
    volatile LONG presHr;
        // The worker's own count of dwords replayed, published with done, in step with the producer's nDw (a WRAP counts nothing). It
    // does not wrap with the ring, so the foreign barrier can tell a worker past a point from one a lap behind (DtForeignBarrier).
    volatile LONG doneDw;
};
struct __declspec(align(64)) DtWakeLine { volatile LONG workerSleeping, prodWaiting; };
static DtProdLine dtProd;
static DtReadLine dtRd;
static DtPubLine  dtP2W;
static DtDoneLine dtW2P;
static DtWakeLine dtWake;
#define dtW              (dtProd.w)
#define dtLim            (dtProd.lim)
#define dtBatch          (dtProd.batch)
#define dtNRec           (dtProd.nRec)
#define dtNDw            (dtProd.nDw)
#define dtRecTid         (dtRd.recTid)
#define dtDisarmAsk      (dtRd.disarmAsk)
#define dtProducer       (dtRd.producer)
#define dtArmed          (dtRd.armed)
#define dtDev            (dtRd.dev)
#define dtRing           (dtRd.ring)
#define dtRingDw         (dtRd.ringDw)
#define dtWorkerTid      (dtRd.workerTid)
#define dtPub            (dtP2W.pub)
#define dtDone           (dtW2P.done)
#define dtPresDone       (dtW2P.presDone)
#define dtPresHr         (dtW2P.presHr)
#define dtDoneDw         (dtW2P.doneDw)
#define dtWorkerSleeping (dtWake.workerSleeping)
#define dtProdWaiting    (dtWake.prodWaiting)
static HANDLE        dtWorkerEv = nullptr, dtProdEv = nullptr, dtWorker = nullptr;
static int           dtCfg      = 0;            // ivtCfg.threaded, handed over at install
#if defined(DLSSIV_DEVLOG) && !DLSSIV_DEVLOG
#define dtValidate false                        // a release clamps threaded=2 to 1, and every test of this folds
#define DT_THREADED_TOP 1
#else
static bool          dtValidate = false;        // threaded=2: every shadow answer is checked
#define DT_THREADED_TOP 2
#endif
static LONGLONG      dtSpinTicks = 0, dtIdleTicks = 0, dtQpf = 0;

static bool DtRecAsked();                       // dt_install.h: the producer's answer; always false
static __forceinline bool DtRec()
{
    if (__readfsdword(0x24) != dtRecTid) return false;
    return dtDisarmAsk ? DtRecAsked() : true;
}
static __forceinline bool DtRecFor(const void* d)
{
    if (__readfsdword(0x24) != dtRecTid || d != dtDev) return false;
    return dtDisarmAsk ? DtRecAsked() : true;
}

// ---- telemetry -----------------------------------------------------------------
// Waits by reason: the 119 device slots, then these. Producer-only counters.
enum
{
    DT_R_VBLOCK = 119, DT_R_IBLOCK, DT_R_TEXLOCK, DT_R_CUBELOCK, DT_R_VTEXLOCK, DT_R_SURFLOCK,
    DT_R_VOLLOCK, DT_R_TEXOTHER, DT_R_SURFDC, DT_R_QGETDATA, DT_R_SBAPPLY, DT_R_SCOTHER,
    DT_R_DIRECT, DT_R_RELOAD, DT_R_VALIDATE, DT_R_SPACE, DT_R_PACE, DT_R_BODY, DT_R_N
};
static const char* const kDtReason[DT_R_N - 119] = {
    "vertex buffer lock", "index buffer lock", "texture lock", "cube lock", "volume texture lock",
    "surface lock", "volume lock", "texture other", "surface DC", "query GetData", "state block Apply",
    "swapchain dirty-region Present or front buffer", "DT_DIRECT (transport, colour space)", "shadow settle", "validation",
    "ring full", "one frame ahead", "shadow fetch behind a worker-side body" };
// With a body on the worker, the first SYNC behind the post waits for all of it and later ones find the ring nearly empty; the
// waits by reason below cannot say which was first, the only one worth attacking. The counts are the development build's; the
// time waited (dtWaitT) stays, since the threaded=0 verdict reads it.
DEVSTAT(static LONG dtWaitN[DT_R_N];)         // how many times each reason came up
DEVSTAT(static LONG dtWaitHit[DT_R_N];)       // ...of which the worker was still busy
DEVSTAT(static LONG dtWaitBlocked[DT_R_N];)   // ...of which the spin was not enough and the thread went to the kernel
static LONGLONG dtWaitT[DT_R_N];               // QPC ticks spent waiting, when it was
static bool     dtWaitSaid[DT_R_N];            // the stuck line below, once per reason
static volatile LONGLONG dtPresTicksSum = 0;   // the worker's time inside the replayed Present (DtExec); 64-bit interlocked, like dtIdleTicksSum
// dtNRec and dtNDw (in dtProd) are monotonic: DtResetBarrier reads dtNRec to judge the producer quiet, so the report keeps its
// own window base (dt_install.h).
// Worker side: replayed calls that failed, which the recorder had answered D3D_OK (the development report and the harness).
DEVSTAT(static volatile LONG dtReplayErrN = 0;)
DEVSTAT(static DWORD   dtReplayErrWho[8];)
DEVSTAT(static HRESULT dtReplayErrHr[8];)
DEVSTAT(static LONG    dtReplayErrCnt[8];)

static const char* const dtDevName[119] = {
    "QueryInterface", "AddRef", "Release", "TestCooperativeLevel", "GetAvailableTextureMem",
    "EvictManagedResources", "GetDirect3D", "GetDeviceCaps", "GetDisplayMode", "GetCreationParameters",
    "SetCursorProperties", "SetCursorPosition", "ShowCursor", "CreateAdditionalSwapChain", "GetSwapChain",
    "GetNumberOfSwapChains", "Reset", "Present", "GetBackBuffer", "GetRasterStatus",
    "SetDialogBoxMode", "SetGammaRamp", "GetGammaRamp", "CreateTexture", "CreateVolumeTexture",
    "CreateCubeTexture", "CreateVertexBuffer", "CreateIndexBuffer", "CreateRenderTarget", "CreateDepthStencilSurface",
    "UpdateSurface", "UpdateTexture", "GetRenderTargetData", "GetFrontBufferData", "StretchRect",
    "ColorFill", "CreateOffscreenPlainSurface", "SetRenderTarget", "GetRenderTarget", "SetDepthStencilSurface",
    "GetDepthStencilSurface", "BeginScene", "EndScene", "Clear", "SetTransform",
    "GetTransform", "MultiplyTransform", "SetViewport", "GetViewport", "SetMaterial",
    "GetMaterial", "SetLight", "GetLight", "LightEnable", "GetLightEnable",
    "SetClipPlane", "GetClipPlane", "SetRenderState", "GetRenderState", "CreateStateBlock",
    "BeginStateBlock", "EndStateBlock", "SetClipStatus", "GetClipStatus", "GetTexture",
    "SetTexture", "GetTextureStageState", "SetTextureStageState", "GetSamplerState", "SetSamplerState",
    "ValidateDevice", "SetPaletteEntries", "GetPaletteEntries", "SetCurrentTexturePalette", "GetCurrentTexturePalette",
    "SetScissorRect", "GetScissorRect", "SetSoftwareVertexProcessing", "GetSoftwareVertexProcessing", "SetNPatchMode",
    "GetNPatchMode", "DrawPrimitive", "DrawIndexedPrimitive", "DrawPrimitiveUP", "DrawIndexedPrimitiveUP",
    "ProcessVertices", "CreateVertexDeclaration", "SetVertexDeclaration", "GetVertexDeclaration", "SetFVF",
    "GetFVF", "CreateVertexShader", "SetVertexShader", "GetVertexShader", "SetVertexShaderConstantF",
    "GetVertexShaderConstantF", "SetVertexShaderConstantI", "GetVertexShaderConstantI", "SetVertexShaderConstantB", "GetVertexShaderConstantB",
    "SetStreamSource", "GetStreamSource", "SetStreamSourceFreq", "GetStreamSourceFreq", "SetIndices",
    "GetIndices", "CreatePixelShader", "SetPixelShader", "GetPixelShader", "SetPixelShaderConstantF",
    "GetPixelShaderConstantF", "SetPixelShaderConstantI", "GetPixelShaderConstantI", "SetPixelShaderConstantB", "GetPixelShaderConstantB",
    "DrawRectPatch", "DrawTriPatch", "DeletePatch", "CreateQuery" };
static const char* DtReasonName(int r) { return r < 119 ? dtDevName[r] : kDtReason[r - 119]; }

// ---- another thread's call takes its place behind the ring ----
// A call from a thread that is not the producer would otherwise reach DXVK ahead of producer calls made before it and still
// queued, and the shadow would answer from before it. GTA IV's main thread binds the depth stencil as a load ends while the
// render thread draws the loading screen, and RAGE saves and restores the bound depth stencil with a Get around every target
// switch: queued restores undid the bind, or a Get answered from the copy returned the old surface, and the game drew the
// rest of the session without that depth stencil.
// DtForeignBarrier (dt_install.h): the other thread reads how far the producer has committed, publishes that much if the
// producer has not, waits for the worker to pass it, and only then calls DXVK, whose own device lock arbitrates from there. It
// asks nothing of the producer, so a blocked producer (IVSDKDotNet parks the render thread in its hook while its own thread
// draws) costs nothing. Disarming the layer instead is wrong for a caller that comes every frame.
// The call is counted in flight from its first instruction (dtForeignInFlight) and the foreign count is raised then, so a
// settle (what the producer's next Get becomes) waits the call out before it drains and asks DXVK: a Get made before the call
// began sees the old value, as natively, and one made after sees the new. The count is raised again after DXVK returns
// (dt_shadow.h), so an entry fetched while the call was in flight is fetched again. Each site wraps its call (DT_ORDERED).
// Not ordered, on purpose: a Create (nothing queued can name it), a Get (it reads DXVK as far as the worker has got; one
// polled every frame would otherwise keep the layer off), a Release (handed to the producer, dt_res.h) and the PASS calls.
// On the producer's path: nothing; the test sits in the branch a recorded call never takes.
static void DtForeignBarrier(const char* what);   // dt_install.h: counts the call in flight, raises the foreign count, waits for the worker
static void DtForeignLanded();                    // dt_install.h: DXVK has returned: raises the count again, then takes the call out of flight
static __forceinline bool DtOrderForeign(const void* d, int slot)
{
    if (!dtArmed || d != dtDev) return false;
    const DWORD t = __readfsdword(0x24);
    if (t == dtProducer || t == dtWorkerTid) return false;
    DtForeignBarrier(dtDevName[slot]);
    return true;
}
// A lambda and no object with a destructor: these sit in wrappers whose recorded path must keep
// its prologue (dt_device_rec.h, DtSync0's note on /EHa), and a closure has nothing to destroy.
template <class F> static __forceinline HRESULT DtOrdered(const void* d, int slot, F call)
{
    const bool foreign = DtOrderForeign(d, slot);
    const HRESULT hr = call();
    if (foreign) DtForeignLanded();
    return hr;
}
template <class F> static __forceinline void DtOrderedV(const void* d, int slot, F call)
{
    const bool foreign = DtOrderForeign(d, slot);
    call();
    if (foreign) DtForeignLanded();
}
#define DT_ORDERED(d, slot, CALL)   DtOrdered(d, slot, [&]() -> HRESULT { return CALL; })
#define DT_ORDERED_V(d, slot, CALL) DtOrderedV(d, slot, [&] { CALL; })

// ---- publishing -------------------------------------------------------------------
// Plain store, then a plain look at the sleeping flag. The two can pass each
// other (store to load), so a worker falling asleep at that instant may miss
// this one; it cannot miss a FENCED publish, and the producer makes one before
// every wait (DtPublishFenced). A missed plain publish therefore costs at most
// the commands since the last one, run a little later. It can never hang.
static __forceinline void DtPublish()
{
    dtBatch = 0;
    _ReadWriteBarrier();                    // the command's stores stay before the index's
    dtPub = (LONG)dtW;
    if (dtWorkerSleeping) SetEvent(dtWorkerEv);
}
static void DtPublishFenced()
{
    dtBatch = 0;
    _ReadWriteBarrier();
    dtPub = (LONG)dtW;
    _mm_mfence();                           // Dekker with the worker's exchange on dtWorkerSleeping
    if (dtWorkerSleeping) SetEvent(dtWorkerEv);
}

// ---- the producer's wait ----
// Spin first, since nearly every wait is the worker finishing a few calls; then sleep on dtProdEv, which the worker sets
// whenever dtProdWaiting is up. A wait that never ends is said once per reason after kDtStuckMs (a stuck worker is a freeze
// to the player); the wait itself cannot be given up, since the game's call needs its answer.
enum { kDtStuckMs = 5000 };
// Which wake a sleeping wait asks for. Kind 1: any progress of the worker (every 64th command, before a body, at the
// catch-up, at a Present). Kind 2: the catch-up or a Present only. A drain's predicate and the one-frame-ahead wait's can turn
// true only there, so kind 1 would wake the render thread every 64 commands for nothing. DtWaitPast (GetData) and the
// ring-full wait keep kind 1: their predicates turn true mid-stream. dt_test switches DT_WAKE_KIND as its control, counts the
// signals through DT_PROD_SIGNAL, and plants DT_WAKE_FAULT (the catch-up's signal dropped), which the 20 ms net must show.
#ifndef DT_WAKE_KIND
#define DT_WAKE_KIND() 2L
#endif
#ifndef DT_PROD_SIGNAL
#define DT_PROD_SIGNAL() SetEvent(dtProdEv)
#endif
#ifndef DT_WAKE_FAULT
#define DT_WAKE_FAULT() false
#endif
// The 5 s notice's two calls: LogRecord and IvtWarn unless the includer names its own. DLSS-IV.cpp names DtStuckSay and
// DtStuckWarn, which take the same arguments and name the cause (the end of that file).
#ifndef DT_STUCK_SAY
#define DT_STUCK_SAY LogRecord
#endif
#ifndef DT_STUCK_WARN
#define DT_STUCK_WARN IvtWarn
#endif
// The wakes, counted (development builds, log=2): the worker's signals and the render thread's returns from a sleeping wait,
// with those that found the wait not over. One writer each.
DEVSTAT(static volatile LONG dtProdSignals = 0;)
DEVSTAT(static volatile LONG dtProdWakes = 0, dtProdWakesFutile = 0;)
template <class P> static void DtWait(P ready, int reason, LONG kind = 1)
{
    DEVSTAT(dtWaitN[reason]++;)
    if (ready()) return;
    DEVSTAT(dtWaitHit[reason]++;)
    LARGE_INTEGER t0, t;
    QueryPerformanceCounter(&t0);
    bool ok = false;
    for (unsigned i = 1; !ok; i++)
    {
        _mm_pause();
        if (ready()) { ok = true; break; }
        if ((i & 63) == 0)
        {
            QueryPerformanceCounter(&t);
            if (t.QuadPart - t0.QuadPart > dtSpinTicks) break;
        }
    }
    DEVSTAT(if (!ok) dtWaitBlocked[reason]++;)
    while (!ok)
    {
        _InterlockedExchange(&dtProdWaiting, kind);  // full fence, then look again (the kind: see DT_WAKE_KIND)
        if (ready()) { ok = true; break; }
        _mm_mfence();
        if (dtWorkerSleeping) SetEvent(dtWorkerEv);  // it must be running to wake this thread
        WaitForSingleObject(dtProdEv, 20);           // 20 ms is only the safety net
        DEVSTAT(dtProdWakes++;)
        if (ready()) ok = true;
        else { DEVSTAT(dtProdWakesFutile++;) }
        if (!ok && !dtWaitSaid[reason])
        {
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000 >= (LONGLONG)kDtStuckMs * dtQpf)
            {
                dtWaitSaid[reason] = true;
                // DT_STUCK_SAY and DT_STUCK_WARN take LogRecord's and IvtWarn's arguments unchanged: the cause is named in DLSS-IV.cpp. A line
                // written into this template, or a stand-in not taking "...", puts more of its copies out of line.
                DT_STUCK_SAY("  threaded: the render thread has waited %d s for the device thread (%s): the worker is stuck inside DXVK or the driver", kDtStuckMs / 1000, DtReasonName(reason));
                // And in the warnings file for a log=0 player: the thread writing it is already frozen. Once per reason.
                DT_STUCK_WARN("threaded=%d: the game has been frozen for %d s waiting for the device thread (%s), stuck inside DXVK or the"
                " driver. If this repeats, set threaded=0", dtCfg, kDtStuckMs / 1000, DtReasonName(reason));
            }
        }
    }
    dtProdWaiting = 0;
    QueryPerformanceCounter(&t);
    dtWaitT[reason] += t.QuadPart - t0.QuadPart;
}

// A wait aimed at one command: it has run when this returns; later ones may not have. `at` is the ring index the command
// starts at, pending exactly while it lies in [dtDone, dtW) round the ring. dtDone is published lazily, so the test errs toward
// waiting; a stale index can only cost a wait no longer than a full drain. The one caller is a query's GetData (dt_res.h).
static __forceinline bool DtRingPending(DWORD at, DWORD w)
{
    const DWORD d = (DWORD)dtDone;
    return d <= w ? (at >= d && at < w) : (at >= d || at < w);
}
static void DtWaitPast(DWORD at, int reason)
{
    DtPublishFenced();
    const DWORD w = dtW;
    DtWait([at, w] { return !DtRingPending(at, w); }, reason);
}

// Everything recorded so far has run when this returns.
static void DtDrain(int reason)
{
    DtPublishFenced();
    const DWORD target = dtW;
    DtWait([target] { return (DWORD)dtDone == target; }, reason, DT_WAKE_KIND());   // true only at the catch-up
}

// ---- reserving and committing -----------------------------------------------------
// The producer may write [dtW, dtDone - 1) round the ring and never lets dtW
// catch dtDone, so dtW == dtDone always means empty. A command never straddles
// the end: when it will not fit, a WRAP goes where it would have started.
static __declspec(noinline) DWORD* DtReserveSlow(DWORD n)
{
    for (;;)
    {
        const DWORD d = (DWORD)dtDone;
        if (dtW >= d)
        {
            const DWORD end = d ? dtRingDw : dtRingDw - 1;   // with d == 0 the last dword stays free
            if (dtW + n <= end) { dtLim = end; return dtRing + dtW; }
            if (n < d) { dtRing[dtW] = DT_OP_WRAP; dtW = 0; dtLim = d - 1; return dtRing; }
        }
        else if (dtW + n <= d - 1) { dtLim = d - 1; return dtRing + dtW; }   // d >= 1 here
        DtPublishFenced();
        DtWait([d] { return (DWORD)dtDone != d; }, DT_R_SPACE);
    }
}
static __forceinline DWORD* DtReserve(DWORD n)
{
    if (dtW + n <= dtLim) return dtRing + dtW;
    return DtReserveSlow(n);
}
// Between DtCommit's two stores dtW can read dtRingDw: a command that ends exactly at the ring's end is committed, then
// wrapped to 0. Every reader on another thread normalises that (DtForeignBarrier, DtBarrier). DT_COMMIT_AT_END is nothing in
// the mod; dt_test parks the producer there.
#ifndef DT_COMMIT_AT_END
#define DT_COMMIT_AT_END()
#endif
// nDw first: the foreign barrier reads dtW and then nDw's low dword, so a command whose dtW it can see must already be
// counted; x86 keeps two stores in order.
static __forceinline void DtCommit(DWORD n)
{
    dtNDw += n;
    _ReadWriteBarrier();                         // the compiler keeps the two stores in this order too; no instruction
    dtW += n;
    if (dtW == dtRingDw) { DT_COMMIT_AT_END(); dtW = 0; dtLim = 0; }   // allowed only when dtDone != 0: see DtReserveSlow
    dtNRec++;
    if (++dtBatch >= 128) DtPublish();
}
// A device command is its header and arguments: the function is dtDx[slot] and the device is dtDev (DtRecFor has proved it),
// so neither is written. The caller fills p[1..] and commits n.
static __forceinline DWORD* DtCmdDev(DWORD op, DWORD n)
{
    DWORD* p = DtReserve(n);
    p[0] = op | (n << 16);
    return p;
}
// An explicit command's first three dwords, for a callee that is not a device slot or
// an object that is not the device. The caller fills p[3..] and commits n.
static __forceinline DWORD* DtCmdX(DWORD op, DWORD n, const void* fn, const void* self)
{
    DWORD* p = DtReserve(n);
    p[0] = op | (n << 16);
    p[1] = (DWORD)(UINT_PTR)fn;
    p[2] = (DWORD)(UINT_PTR)self;
    return p;
}
// The largest command the ring will take; anything bigger runs as a SYNC call.
#define DT_MAX_CMD_DW 16384u

// ---- observers, notes and the handoff ----
// Hooks for running analyses on the worker; nothing here changes what DXVK receives. The mod installs eight draw and state
// observers, the tracker's notes, the arm and disarm observers and the INJECTION note (dt_trk.h, TrkArm), whose observer
// drives the device from the worker (dtNoteDrives); dtPresentObs carries the latency instrument's present observer
// (LatPresentObs, the frame-end marker).
//   dtObs[slot]     called on the worker after it replayed a CALL0+n command for that slot, with the command as recorded:
//                   c[0] header, c[1..] the arguments, and the payload behind them. The device is dtDev, the method the slot.
//   dtNoteObs[kind] called on the worker with a note's payload at its place in the stream: how a fact the worker cannot see
//                   (a game-memory read, a render-thread decision) reaches it in order.
//   dtPresentObs    called on the worker for a Present it is about to complete, before the present count moves: what it
//                   writes into the handoff is complete when the producer's pacing wait for that present returns.
// An observer runs whether or not the replay failed (DtNoteReplayErr counts the error) and sees the command as recorded, not
// the device's answer. dtPresentObs runs for a failed Present too: the present count must advance or the next pacing wait
// never ends; the failure reaches the game once, through dtPresHr.
// The command pointer points into the ring and is valid only until the observer returns: copy anything kept.
// Cost on the worker: a shift, a mask and a load per command. On the producer: nothing; a note costs what it copies.
typedef void (*DtObsFn)(const DWORD* c);
typedef void (*DtNoteFn)(DWORD kind, const DWORD* payload, DWORD bytes);
typedef void (*DtPresentFn)(LONG present);
// kinds; the TRK three are the tracker's (dt_trk.h), INJECT is the injection run on the worker (DlssIvTransport.h)
enum { DT_NOTE_TEST = 0, DT_NOTE_TRK_WINDOW = 1, DT_NOTE_TRK_SCENE = 2, DT_NOTE_TRK_FRAME = 3,
       DT_NOTE_INJECT = 4, DT_NOTE_N = 16 };
static DtObsFn     dtObs[128];                        // by DT_SLOT_OF, which is 7 bits: no bound to test on the worker
static DtNoteFn    dtNoteObs[DT_NOTE_N];
static DtPresentFn dtPresentObs = nullptr;
// The tracker changes thread at two drained points (dt_trk.h).
static void (*dtArmObs)()    = nullptr;   // on the arming thread, just before dtArmed goes up: ring empty, worker idle
static void (*dtDisarmObs)() = nullptr;   // at the start of the disarm tail: flag down, ring drained
DEVSTAT(static LONG dtNoteRefused = 0;)              // notes of an unknown kind or larger than a command: dropped, counted (the harness reads it)
// A note whose observer drives the device: the injection body binds its own targets, shaders, textures and states on the
// worker and puts them back, but while it runs the device holds the mod's. The shadow's direct fetch of an unknown entry
// (dt_shadow.h) is exact only while no queued command names the entry, so the producer counts driving notes posted and not
// finished, and a fetch that could see the body's state waits for it (DtShFetchGate).
static bool          dtNoteDrives[DT_NOTE_N];         // set beside dtNoteObs[kind]
static volatile LONG dtShBodies = 0;                  // driving notes posted and not finished
static bool          dtShBodiesEver = false;          // one has been posted this session: producer only (dt_shadow.h, dtShConsts, reads it)
static volatile LONG dtSbBodyPosts = 0;              // bodies posted, ever: a state-block window is tainted by one inside it (dt_sbtab.h)
static DWORD         dtBodySampFree = 0;              // sampler slots (bit DtSampIdx) no body ever binds: fetchable beside one. 0 = none declared, every fetch waits
// The render states a body may set, one bit each. All ones until declared otherwise, the fail-safe: every render state fetch
// then waits for a body. The game asks D3DRS_LIGHTING once a frame right behind the seam, which no body touches.
static DWORD         dtBodyRsTouched[8] = { ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u, ~0u };
#if DLSSIV_DEV
// Development builds check both declarations: tools/check-body-touch.py proves dtBodyRsTouched from the source, while
// dtBodySampFree rests on reading the body's call tree. Every render state, sampler state and texture a body sets on the
// worker is counted (dtBodySets: the check ran), dtBodyUndeclared counts one outside the declarations, and the first is named
// (kind 1 render state, 2 sampler state, 3 texture; << 16 | the index). Only the worker's own calls reach these wrappers on
// the worker, so every one is a body's.
static volatile LONG dtBodySets = 0, dtBodyUndeclared = 0, dtBodyUndeclFirst = 0;
static __forceinline void DtBodyDecl(DWORD kind, DWORD idx, bool undeclared)
{
    if (__readfsdword(0x24) != dtWorkerTid) return;
    dtBodySets++;
    if (!undeclared) return;
    if (!dtBodyUndeclared++) dtBodyUndeclFirst = (LONG)(kind << 16 | (idx & 0xFFFF));
}
#define DT_BODY_DECL_RS(s)    DtBodyDecl(1, (DWORD)(s), (DWORD)(s) >= 256 || !(dtBodyRsTouched[(DWORD)(s) >> 5] >> ((DWORD)(s) & 31) & 1u));
#define DT_BODY_DECL_SAMP(i)  DtBodyDecl(2, (DWORD)(i), (i) >= 0 && (dtBodySampFree >> (i) & 1u) != 0);
#define DT_BODY_DECL_TEX(i)   DtBodyDecl(3, (DWORD)(i), (i) >= 0 && (dtBodySampFree >> (i) & 1u) != 0);
#else
#define DT_BODY_DECL_RS(s)
#define DT_BODY_DECL_SAMP(i)
#define DT_BODY_DECL_TEX(i)
#endif
static void          DtShKnowBody();                  // dt_shadow.h: called at the post, below

// A note, recorded by the code that owns the fact at the point in the stream where it is true. Producer only, outside a scope,
// like a recorded call; elsewhere it records nothing and returns false. The byte count travels with it.
// The re-test after the post's fetch: DtShKnowBody releases what it fetches, and a Release on the producer answers a disarm
// ask, so another thread's Reset can disarm the layer inside this function; the note would then sit in a ring nobody
// publishes and later replay against destroyed objects. Refused instead: the caller runs its body inline. A macro so the
// harness can switch it off (its negative control).
#ifndef DT_NOTE_RECHECK
#define DT_NOTE_RECHECK() true
#endif
// The state-block switch: true is the recorded Apply, false the drained one. A variable, not a literal, so static analysis
// does not treat every test of it as constant; the harness drives it (dt_test.cpp, gSbB).
#ifndef DT_SB_REC_ON
// Set at InitThread (DLSS-IV.cpp): true on Windows. Under Wine and Proton the drained Apply stays, and a DXVK the probe does
// not confirm keeps it on either (dtSbProbeOk).
static volatile bool dtSbRecOn = false;
#define DT_SB_REC_ON() dtSbRecOn
#endif
static void DtSbReplayFailed(HRESULT hr);   // dt_sbtab.h: a recorded state block's Apply that failed on the worker
static bool DtNote(DWORD kind, const void* payload, DWORD bytes)
{
    if (!DtRec()) return false;
        // The byte count is tested before the division: a negative difference wrapped would otherwise pass the size guard and copy
    // off the end of the ring.
    if (kind >= DT_NOTE_N || bytes > (DT_MAX_CMD_DW - 2) * 4u) { DEVSTAT(dtNoteRefused++;) return false; }
    const DWORD dw = (bytes + 3) / 4, n = 2 + dw;
        // While nothing is out, everything the body will touch becomes known, so nobody fetches it beside the body. Before the
    // reservation, because it may record: a fetched pointer's Release goes into the ring.
    if (dtNoteDrives[kind]) { DtShKnowBody(); if (DT_NOTE_RECHECK() && !DtRec()) return false; }
    DWORD* p = DtReserve(n);
    p[0] = DT_OP_NOTE | DT_SLOT(kind) | (n << 16);
    p[1] = bytes;
    if (bytes) memcpy(p + 2, payload, bytes);
    if (dtNoteDrives[kind])
    {
        dtShBodiesEver = true; _InterlockedIncrement(&dtShBodies);   // before the commit: the worker may run it at once
        _InterlockedIncrement(&dtSbBodyPosts);
    }
    DtCommit(n);
    return true;
}

// The handoff: what the worker knew at a Present, for the producer a frame later. Two slots by present parity: the Present
// observer for present k writes dtHand[k & 1] and stamps it k last; the producer's pacing wait at k+1 guarantees k has run,
// and the worker cannot reach k+2 (which reuses the slot) until the producer records it. DtHandoffFor(k) answers null until k
// is complete and stamped; a stale ask gets null too. One thread each side and x86 store order: compiler barriers suffice,
// and dtPresDone's interlocked increment fences the two. The pointer is valid until the caller records its next Present.
// Ordinals keep counting across a disarm, so DtMaybeArm stamps both slots -1 before arming; no ordinal is 0 or below.
#if DLSSIV_DEV   // the handoff is the harness's alone (dt_test): nothing of it in a release
struct __declspec(align(64)) DtHandoff { volatile LONG present; LONG pad; DWORD words[62]; };   // 256 bytes, four lines
static DtHandoff dtHand[2];
static void DtHandoffWrite(LONG present, const DWORD* words, DWORD n)        // the worker, from dtPresentObs
{
    DtHandoff& h = dtHand[present & 1];
    if (n > 62) n = 62;
    for (DWORD i = 0; i < n; i++) h.words[i] = words[i];
    _ReadWriteBarrier();
    h.present = present;
}
static const DtHandoff* DtHandoffFor(LONG present)                           // the producer
{
    if (present <= 0) return nullptr;                                        // ordinals are 1-based; the static zero slot is not present 0
    if ((LONG)(dtPresDone - present) < 0) return nullptr;                    // the worker has not completed it
    const DtHandoff& h = dtHand[present & 1];
    if (h.present != present) return nullptr;
    _ReadWriteBarrier();
    return &h;
}
#endif

// ---- the scopes ---------------------------------------------------------------------
// Direct execution on the producer. The destructor re-arms recording only if the
// layer is STILL armed, so a Reset or a lost device inside the scope ends it. Both
// enter through DtRec(), so a scope opened with the ask up answers it first and
// then is not a scope at all: the layer is unarmed and the call is direct anyway.
struct DtPassScope
{
    bool on;
    DtPassScope() : on(DtRec()) { if (on) dtRecTid = 0; }
    ~DtPassScope() { if (on) dtRecTid = dtArmed ? dtProducer : 0; }
};
struct DtSyncScope
{
    bool on;
    explicit DtSyncScope(int reason) : on(DtRec())
    { if (on) { DtDrain(reason); dtRecTid = 0; } }
    ~DtSyncScope() { if (on) dtRecTid = dtArmed ? dtProducer : 0; }
};
// For code that talks to DXVK outside D3D9 (the interop device, Vulkan on DXVK's queue, the extended swapchain): everything
// recorded reaches DXVK first, then the scope runs directly and in order. Nests; a load and a compare when the layer is off;
// nothing on other threads.
#define DT_DIRECT() DtSyncScope dtDirectScope_(DT_R_DIRECT)

// For the mod's own device hooks, the first line of each: they never run on the worker. An overlay that patches DXVK's code
// runs inside a replayed call on the worker, and what it draws comes back through the vtable into the hooks, on a second
// thread over the render thread's state; there a hook is just the call it wraps. DXVK's calls to its own methods are direct
// (D3D9DeviceEx is final). dtWorkerTid is 0 with threaded=0 and no thread id is 0, so a load and a compare.
#define DT_NOT_ON_WORKER(call) do { if (__readfsdword(0x24) == dtWorkerTid) return call; } while (0)

// ---- the back buffer, written since the mark or not (only in a build whose product can arm) ----
// A frame generation fire writes a picture into the game's back buffer, saving and restoring it first only where the game has
// written it since the pair was armed (source/fg/fg_machine.h, B::BackMark and B::BackWritten; source/fg/fg_arm.h). This layer
// is the one place every D3D9 call passes, so it answers; devthread/dt_bbs.h is the code. Every site is DTBBS(...) or
// DT_BBS_BESIDE(...), nothing with DT_BBS_ON at 0 as in the release (tools/prove-fg-off.py, tools/check-hot-layout.py).
// Replayed calls reach it through the device thread's observers (dt_bbs.h).
// Declarations in devthread/dt_bbs_top.h (top of the translation unit), state and bodies in devthread/dt_bbs.h (its end),
// macros alone here. The harness sets DT_BBS_ON itself (tools/perfproof/dt_fg_test.cpp).
#ifndef DT_BBS_ON
#define DT_BBS_ON (DLSSIV_FG)
#endif
// the kinds: what wrote, as the product's line says it
#define DT_BBS_DRAW    0
#define DT_BBS_CLEAR   1
#define DT_BBS_STRETCH 2
#define DT_BBS_FILL    3
#define DT_BBS_UPDATE  4
#define DT_BBS_LOCK    5        // a LockRect that is not read-only, an UnlockRect, a GetDC, a ReleaseDC
#define DT_BBS_PATCH   6        // DrawRectPatch, DrawTriPatch
#define DT_BBS_PRESENT 7        // a Present that reached DXVK and was not the pair's own
#define DT_BBS_RAW     8        // the mod's own raw write (the transport's home copy)
#define DT_BBS_BIND    9        // a back buffer bound as a render target beside the worker (not a write: the draws that follow it are)
#define DT_BBS_KINDS   10
// "whatever is bound": a draw, a Clear, a patch or a Present made where the layer cannot say what the device holds
#define DT_BBS_ANY ((const void*)(UINT_PTR)1)
#if DT_BBS_ON
#define DTBBS(...) __VA_ARGS__
// A call that reaches DXVK beside the replay, counted before it is made and after it returns (dt_bbs.h says why): with the
// switch at 0 the line is the call itself
#define DT_BBS_BESIDE(cond, d, kind, dst, CALL) DtBbsAround((cond) ? DtBbsEnter(d, kind, dst) : 0, [&]() -> HRESULT { return CALL; })
// a lambda and no object with a destructor, as DtOrdered: the wrapper's recorded path keeps its prologue
template <class F> static __forceinline HRESULT DtBbsAround(int entered, F call)
{
    const HRESULT hr = call();
    DtBbsLeave(entered);
    return hr;
}
#else
#define DTBBS(...)
#define DT_BBS_BESIDE(cond, d, kind, dst, CALL) CALL
#endif

// ---- the worker ----
// Who failed: a device slot (0..118), or one of these for a command that names no slot. DT_WHO_SBAPPLY: a recorded state
// block's Apply (DtSbReplayFailed).
enum : DWORD { DT_WHO_SBAPPLY = 124, DT_WHO_PRESENT = 125, DT_WHO_RES = 126, DT_WHO_OP = 127 };
static void DtNoteReplayErr(DWORD who, HRESULT hr)
{
#if DLSSIV_DEV
    for (int i = 0; i < 8; i++)
    {
        if (dtReplayErrCnt[i] && dtReplayErrWho[i] == who && dtReplayErrHr[i] == hr)
        { dtReplayErrCnt[i]++; dtReplayErrN++; return; }
        if (!dtReplayErrCnt[i])
        { dtReplayErrWho[i] = who; dtReplayErrHr[i] = hr; dtReplayErrCnt[i] = 1; dtReplayErrN++; return; }
    }
    dtReplayErrN++;
#else
    (void)who; (void)hr;      // the table is the development report's: a release reads none of it
#endif
}

// `dev` is dtDev as the worker read it at the head of this batch (DtWorkerMain), the device every command in it was recorded
// against. dtDev has one writer, DtMaybeArm on the producer, which runs only while unarmed and so only after a drain: every
// write lands on an empty ring with the worker idle, and a worker that sees the new dtPub reads the new dtDev (x86 store
// order). The read is volatile so no old value is carried across the idle wait.
static __forceinline void DtExec(DWORD h, const DWORD* c, void* const dev)
{
    const DWORD slot = DT_SLOT_OF(h);
    HRESULT hr;
    switch (h & 0xFF)
    {
    case DT_OP_CALL0 + 0: hr = ((DtF0)dtDx[slot])(dev); break;
    case DT_OP_CALL0 + 1: hr = ((DtF1)dtDx[slot])(dev, c[1]); break;
    case DT_OP_CALL0 + 2: hr = ((DtF2)dtDx[slot])(dev, c[1], c[2]); break;
    case DT_OP_CALL0 + 3: hr = ((DtF3)dtDx[slot])(dev, c[1], c[2], c[3]); break;
    case DT_OP_CALL0 + 4: hr = ((DtF4)dtDx[slot])(dev, c[1], c[2], c[3], c[4]); break;
    case DT_OP_CALL0 + 5: hr = ((DtF5)dtDx[slot])(dev, c[1], c[2], c[3], c[4], c[5]); break;
    case DT_OP_CALL0 + 6: hr = ((DtF6)dtDx[slot])(dev, c[1], c[2], c[3], c[4], c[5], c[6]); break;
    case DT_OP_CALL0 + 7: hr = ((DtF7)dtDx[slot])(dev, c[1], c[2], c[3], c[4], c[5], c[6], c[7]); break;
    case DT_OP_CALL0 + 8: hr = ((DtF8)dtDx[slot])(dev, c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8]); break;
        // The explicit forms: callee and object travel in the command and the arguments start at c[3]. None has a slot or an
    // observer; each returns from its own arm so the device arm stays one shape.
    case DT_OP_XCALL0 + 0:
        hr = ((DtF0)c[1])((void*)(UINT_PTR)c[2]);
        if (FAILED(hr)) DtNoteReplayErr(DT_WHO_RES, hr);
        return;
    case DT_OP_XCALL0 + 1:
        hr = ((DtF1)c[1])((void*)(UINT_PTR)c[2], c[3]);
        if (FAILED(hr)) DtNoteReplayErr(DT_WHO_RES, hr);
        return;
        // A recorded state block's Apply: the XCALL shape, no observer. A failure is counted against its own code and the foreign
    // count is raised so the producer's next Get settles and re-reads (dt_sbtab.h).
    case DT_OP_SBAPPLY:
        hr = ((DtF0)c[1])((void*)(UINT_PTR)c[2]);
        if (FAILED(hr)) DtSbReplayFailed(hr);
        return;
    case DT_OP_PRESENT:
    {
        void* const self = (void*)(UINT_PTR)c[2];
                // Timed: a Present that blocks on vsync or frame latency is the frame limiter, not the layer's cost; DtReport takes it out of
        // the 2 ms verdict's numerator.
        LARGE_INTEGER p0, p1;
        DTBBS(DtBbsPresentAhead();)                          // the save's question: ahead of the call, so the pair this Present arms is marked inside it and its mark stands
        QueryPerformanceCounter(&p0);
        hr = ((DtF5)c[1])(self, c[3], c[4], c[5], c[6], c[7]);
        QueryPerformanceCounter(&p1);
        InterlockedExchangeAdd64(&dtPresTicksSum, p1.QuadPart - p0.QuadPart);
                // The flip classifier's return ring, development builds only: this present's return and its time inside DXVK's Present, for
        // the waiter to pair with the begin stamps (ivt_latency.h, latFlipRelQpc). N = -1, the values, then N = k; plain ordered
        // stores on x86.
        DEVSTAT({ const LONG k = dtPresDone + 1; const int s = (int)(k & (LAT_RING - 1)); latFlipRelN[s] = -1; __iso_volatile_store64(&latFlipRelQpc[s], p1.QuadPart); latFlipRelBlk[s] = (float)(p1.QuadPart - p0.QuadPart); latFlipRelN[s] = k; })
        if (FAILED(hr)) dtPresHr = (LONG)hr;                 // before the count, whose increment fences it
        if (dtPresentObs) dtPresentObs(dtPresDone + 1);      // the ordinal about to complete; only this thread writes dtPresDone
        _InterlockedIncrement(&dtPresDone);                  // a full fence, then the flag
        if (dtProdWaiting) { DEVSTAT(dtProdSignals++;) SetEvent(dtProdEv); }   // any kind: a Present ends kind 2's wait too
        if (FAILED(hr)) DtNoteReplayErr(DT_WHO_PRESENT, hr);  // counted as a failed replay
        return;
    }
    case DT_OP_RELEASE2:
    {
        void* const self = (void*)(UINT_PTR)c[2];
        if (h & DT_F_COUNTED) DtRelease2Counted((void*)(UINT_PTR)c[1], self);
        else { ((DtF0)c[1])(self); ((DtF0)c[1])(self); }
        return;
    }
    case DT_OP_NOTE:
        // The kind is 7 bits in the header and the table has DT_NOTE_N entries: the
        // recorder refuses a kind at or above it, and the worker checks again rather
        // than trust the ring.
        if (DT_SLOT_OF(h) < DT_NOTE_N && dtNoteObs[DT_SLOT_OF(h)]) dtNoteObs[DT_SLOT_OF(h)](DT_SLOT_OF(h), c + 2, c[1]);
        // The body is over and the device is as the stream says again. Counted by the
        // kind's declaration, not by whether an observer ran: the post counted it up.
        if (DT_SLOT_OF(h) < DT_NOTE_N && dtNoteDrives[DT_SLOT_OF(h)]) _InterlockedDecrement(&dtShBodies);
        return;
    default:
        DtNoteReplayErr(DT_WHO_OP, E_UNEXPECTED);            // cannot happen; says so if it does
        return;
    }
    if (!(h & DT_F_VOID) && FAILED(hr)) { DtNoteReplayErr(slot, hr); DTBBS(if (slot == 37) DtBbsBindFailed();) }   // the save's question: a refused bind, for its observer below
        // The save's question follows its calls through dtObs (devthread/dt_bbs.h).
    if (dtObs[slot]) dtObs[slot](c);                         // only a device call reaches here; dtObs spans the slot's 7 bits
}

// dw: the dwords run so far (dtDoneDw). caughtUp: the worker has reached the published end, where a kind-2 wait can end.
static __forceinline void DtWorkerDone(DWORD r, DWORD dw, bool caughtUp = false)
{
    dtDoneDw = (LONG)dw;
    dtDone = (LONG)r;
    _mm_mfence();                                            // Dekker with DtWait's exchange
    const LONG k = dtProdWaiting;
    if (k == 1 || (k && caughtUp && !DT_WAKE_FAULT())) { DEVSTAT(dtProdSignals++;) DT_PROD_SIGNAL(); }
}

// How idle the worker is: its time here, spinning and asleep, against the frame. Written by the worker with a 64-bit
// interlocked add, read by the report with a 64-bit compare-exchange (cmpxchg8b on x86), so a 32-bit reader never sees a torn
// value.
static volatile LONGLONG dtIdleTicksSum = 0;
DEVSTAT(static volatile LONG dtIdleSleeps = 0;)   // the development report's
static void DtWorkerIdle(DWORD r)
{
    LARGE_INTEGER t0, t, tIn;
    QueryPerformanceCounter(&t0);
    tIn = t0;
    for (unsigned i = 1; (DWORD)dtPub == r; i++)
    {
        _mm_pause();
        if ((i & 63) == 0)
        {
            QueryPerformanceCounter(&t);
            if (t.QuadPart - t0.QuadPart > dtIdleTicks)
            {
                _InterlockedExchange(&dtWorkerSleeping, 1);  // full fence, then look again
                if ((DWORD)dtPub == r) { DEVSTAT(_InterlockedIncrement(&dtIdleSleeps);) WaitForSingleObject(dtWorkerEv, 100); }
                dtWorkerSleeping = 0;
                QueryPerformanceCounter(&t0);
                // Folded every round, so a long unarmed stretch is not booked into whichever report window it ends in; at most about 100 ms
                // behind.
                InterlockedExchangeAdd64(&dtIdleTicksSum, t0.QuadPart - tIn.QuadPart);
                tIn = t0;
            }
        }
    }
    QueryPerformanceCounter(&t);
    InterlockedExchangeAdd64(&dtIdleTicksSum, t.QuadPart - tIn.QuadPart);   // winnt.h's: on x86 a cmpxchg8b loop
}

static DWORD WINAPI DtWorkerMain(void*)
{
    DWORD r = 0, sinceDone = 0, dw = 0;                      // dw: the dwords run, dtDoneDw's source
    for (;;)
    {
        const DWORD pub = (DWORD)dtPub;
        if (r == pub)
        {
            DtWorkerDone(r, dw, true);
            DtWorkerIdle(r);
            continue;
        }
        _ReadWriteBarrier();                                 // read the commands after the index
        void* const dev = *(IDirect3DDevice9* volatile*)&dtRd.dev;   // after the index too: see DtExec
        do
        {
            const DWORD* c = dtRing + r;
            const DWORD h = c[0];
            if ((h & 0xFF) == DT_OP_WRAP) { r = 0; continue; }
            // Before a body, publish progress: dtDone is published every 64th command and a body runs for most of a millisecond, so a
            // wait aimed just ahead of it (DtWaitPast) would otherwise sit out the whole body. One compare a command.
            if ((h & 0xFF) == DT_OP_NOTE && DT_SLOT_OF(h) < DT_NOTE_N && dtNoteDrives[DT_SLOT_OF(h)]) { sinceDone = 0; DtWorkerDone(r, dw); }
            DtExec(h, c, dev);
            r += h >> 16;
            dw += h >> 16;                                   // as DtCommit counts nDw: a command by its length, a WRAP by nothing
            if (r == dtRingDw) r = 0;
            if (++sinceDone >= 64) { sinceDone = 0; DtWorkerDone(r, dw); }
        } while (r != pub);
    }
}

// The ring, the two events and the worker: once per process, never torn down (the ring is idle at process exit).
// The three tick constants are set on their own, so DtInstall can set them whether or not the ring is built. Idempotent.
static void DtTimebase()
{
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); dtQpf = f.QuadPart;
    dtSpinTicks = dtQpf / 20000;                 // 50 us
        // 20 us: a longer idle spin took most of a core the game, DXVK and the helper also want, for no frame rate.
    dtIdleTicks = dtQpf / 50000;
}

static bool DtStart(DWORD ringBytes)
{
    if (dtWorker) return true;
    // A command may be DT_MAX_CMD_DW long, and DtReserveSlow can only place one
    // that is under half the ring. Four of the largest is the floor, with room.
    if (ringBytes / 4 < 4 * DT_MAX_CMD_DW) { LogRecord("  threaded: the %u-byte ring is too small: off", ringBytes); return false; }
    DtTimebase();
    dtRing = (DWORD*)VirtualAlloc(nullptr, ringBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!dtRing) { LogRecord("  threaded: the %u MB ring could not be reserved: off", ringBytes >> 20); return false; }
    dtRingDw = ringBytes / 4;
    dtWorkerEv = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    dtProdEv   = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dtWorkerEv || !dtProdEv) { LogRecord("  threaded: its events could not be created: off"); return false; }
        // A megabyte of stack, as the game's own threads get: the worker makes DXVK's calls, the swapchain Present into the driver
    // among them.
    dtWorker = CreateThread(nullptr, 1024 * 1024, DtWorkerMain, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, &dtWorkerTid);
    if (!dtWorker) { LogRecord("  threaded: the worker thread could not start: off"); return false; }
        // Named, so a profiler lists it as it lists dxvk-cs. Looked up at runtime: SetThreadDescription does not exist before
    // Windows 10 1607.
    typedef HRESULT(WINAPI* SetDesc_t)(HANDLE, PCWSTR);
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        if (SetDesc_t sd = (SetDesc_t)GetProcAddress(k, "SetThreadDescription")) sd(dtWorker, L"DLSS-IV device thread");
    return true;
}
