// The device thread: the state-block table.
// A state block's Apply on the render thread would drain the ring and forget the whole shadow (dt_res.h, dtSbApply). Instead
// the Apply is recorded into the ring, as a Capture is, and the shadow takes the block's values from a per-block record kept
// here, so nothing waits and nothing is forgotten. The record is learned from the block's recording (BeginStateBlock to
// EndStateBlock and the Sets between, on either path of the tracked wrappers) and refreshed at each Capture the render thread
// makes from what the shadow holds. Anything the record cannot vouch for keeps the drained Apply.
// The recorded Apply runs on Windows (dtSbRecOn, set at InitThread); under Wine the drained one stays. Development builds add
// the planted faults and the report line (DtSbReport); the counts stay in both builds, since the release's code writes them.
// The faults (DT_SBM_FAULT(n)) are planted by the harness to prove each check can fail; in the mod they are false.
#pragma once

#ifndef DT_SBM_FAULT
#if DLSSIV_DEV
// In the mod a variable nothing writes: a literal false made static analysis read every fault test as a constant (C6237,
// C6239, C6240).
static int dtSbmFault = 0;
#define DT_SBM_FAULT(n) (dtSbmFault == (n))
#else
#define DT_SBM_FAULT(n) false             // a release build plants nothing
#endif
#endif
enum : BYTE { DT_SBK_FREE = 0, DT_SBK_TYPED = 1, DT_SBK_REC = 2 };
// The declaration a record holds: none (not in the block), an object (null included: DXVK's Apply skips a null one), the
// table declaration of an FVF (the object only DXVK can name), or unknown (a Capture over a declaration the shadow did not know).
enum : BYTE { DT_SBD_NONE = 0, DT_SBD_OBJ = 1, DT_SBD_FVF = 2, DT_SBD_UNKNOWN = 3 };
// Why a window was not learned clean.
enum : LONG
{
    DT_SBT_NONE = 0,
    DT_SBT_RS,           // a render state outside 7..209 (DXVK's Get rule and its refusal of 1..6 and >255: not worth modelling)
    DT_SBT_REFUSED,      // a Set DXVK refuses, or one the wrapper sent as a SYNC
    DT_SBT_FOREIGN,      // armed: another thread's call on the device moved the foreign count, or was in flight at End
    DT_SBT_UFOREIGN,     // unarmed: another thread's tracked Set was bracketed during the window
    DT_SBT_USTUCK,       // unarmed: such a Set was still inside DXVK 1 ms after End
    DT_SBT_OTHERTHREAD,  // unarmed, opened by a thread that is not the render thread while renderTid is known (its Sets are not bracketed)
    DT_SBT_NOTPRODUCER,  // armed, opened by a thread that is not the producer
    DT_SBT_OTHERDEV,     // armed, on another device
    DT_SBT_ARM,          // the layer armed or disarmed inside the window
    DT_SBT_RESET,        // a Reset inside the window
    DT_SBT_FOREIGNEND,   // another thread ended the window's recording
    DT_SBT_NOWINDOW,     // an End with no window learned for it (the one window was taken, or the Begin was not seen)
    DT_SBT_BODY,         // an injection body was posted inside it (its binds run on the worker, into the recording)
    DT_SBT_GAVEUP,       // another thread's call gave up waiting for the worker and went ahead of the queue
    DT_SBT_N
};
static const char* const kDtSbTaintName[DT_SBT_N] = { "clean", "a render state outside 7..209", "a refused Set",
    "another thread (armed)", "another thread (bracketed)", "another thread's Set still inside DXVK", "off the render thread",
    "opened off the producer", "another device", "an arm or disarm inside it", "a Reset inside it", "ended by another thread",
    "no window", "a body posted inside it", "a call that went ahead of the queue" };

// The record: exactly the kinds the shadow answers or drains for when unknown. Values are raw as DXVK's recorder keeps them
// (viewport not lifted, a null stream's offset and stride kept); the writer (dt_res.h, DtSbMirWrite) applies the device's
// rules. A stream's offset is the recorded one: a Capture never takes it (d3d9_stateblock.h:216-230).
struct DtSbMir
{
    DWORD rsM[8];   DWORD rs[256];
    WORD  sampM[21]; DWORD samp[21][14];
    DWORD texM;     IDirect3DBaseTexture9* tex[21];
    DWORD vbM;      IDirect3DVertexBuffer9* vb[16]; UINT vbOff[16], vbStr[16];
    bool  vsM, psM, ibM, vpM, scM;
    IDirect3DVertexShader9* vs; IDirect3DPixelShader9* ps; IDirect3DIndexBuffer9* ib; D3DVIEWPORT9 vp; RECT sc;
    BYTE  declForm; IDirect3DVertexDeclaration9* decl; DWORD fvf; bool fvfOk;   // OBJ: decl, with its FVF when fvfOk; FVF: fvf
    DWORD pscM[7], pscK[7];                                                      // covered, and value known
    float psc[224][4];
};
struct DtSbEnt
{
    const void* sb; const void* dev;
    BYTE kind; bool clean, valsOk; LONG taint;
    LONG caps;                      // Captures copied into it (the harness's floors: which values came from a Capture)
    DtSbMir m;
};
// 64 entries under a leaf lock. A block with no entry, a full table included, keeps the drained Apply. The harness sets more
// (its programs keep hundreds of blocks alive). The report prints the peak.
#ifndef DT_SB_CAP_N
#define DT_SB_CAP_N 64
#endif
enum { DT_SB_CAP = DT_SB_CAP_N };
static DtSbEnt dtSbTab[DT_SB_CAP];
static SRWLOCK dtSbLock = SRWLOCK_INIT;
DEVSTAT(static LONG dtSbFull = 0, dtSbLivePeak = 0, dtSbNotStored = 0;)   // the development report's and the harness's
static int     dtSbCapLimit = DT_SB_CAP;       // records live at most: DT_SB_CAP in the mod; the harness lowers it to see a full table

// The window: the recording being learned, one at a time, as DXVK records one block at a time per device. Only its own thread
// writes dtSbWinM and reads it; other threads read the device and thread id to see the window is not theirs. Closed as thread
// id 0 first, then the device null, so a thread never reads its own id beside a device another thread opened (DtOrderedTrk).
static const void* volatile dtSbWinDev = nullptr;
static volatile DWORD dtSbWinTid = 0;
static volatile LONG  dtSbWinTaint = DT_SBT_NONE;   // the first reason; another thread's Reset or End may write it
static volatile LONG  dtSbWinEnded = 0;             // another thread ended this window's recording: only the owner shuts it
static bool           dtSbWinArmed = false;
static LONG           dtSbWinForeign = 0, dtSbWinUf = 0, dtSbWinArmGen = 0, dtSbWinBody = 0, dtSbWinReset = 0, dtSbWinGiveUp = 0;
static DtSbMir        dtSbWinM;
// Facts a window is judged by at its close: stamped at open and compared at close, since a bool taken at open misses an arm
// and a disarm inside one window.
static volatile LONG  dtSbArmGen = 0;               // every arm and every disarm
static volatile LONG  dtSbResetIn = 0, dtSbResetSeq = 0;   // Resets in flight, and done
static volatile LONG  dtSbGiveUps = 0;              // another thread's calls that gave up waiting (dt_install.h, DtForeignBarrier)
// Any recording open on any device, window or not (DXVK's recorder is the device's: ShouldRecord is m_recorder != nullptr):
// raised before a Begin reaches DXVK, lowered if it fails and after a successful End. DXVK refuses a Capture or Apply while it
// is up, so the record never follows one.
static volatile LONG  dtSbRecOpen = 0;
// Each device's open recordings: dtSbRecOpen is their sum, written under the table's lock, so a device released mid-recording
// takes only its own opens with it and an End never counts below zero. More than eight devices recording at once are counted
// in dtSbRecOver.
static const void* dtSbRecDev[8] = {};
static LONG        dtSbRecDevN[8] = {};
static LONG        dtSbRecOver = 0;
// The unarmed bracket: another thread's tracked Set while the layer is not armed, in flight and done. dtForeign cannot see
// these: DtShMine counts only on dtDev, which is null before the first arm.
static volatile LONG dtSbUfIn = 0, dtSbUfSeq = 0;

// The counts the report line and the harness read: development builds and harnesses only.
DEVSTAT(static LONG dtSbWinBusy = 0, dtSbWinLearned = 0, dtSbWinTaintN[DT_SBT_N] = {}, dtSbWinAtArm = 0, dtSbArmDeferred = 0;)
DEVSTAT(static LONG dtSbCapCopied = 0, dtSbCapInvalid = 0, dtSbCapInRec = 0, dtSbCapUnknown = 0, dtSbCapForeign = 0, dtSbCapNotOurs = 0;)
static LONG dtSbCapUfStamp = 0;                     // the bracket's count as a Capture's copy read it: DtSbCaptureAfter compares
enum : LONG { DT_SBF_OK = 0, DT_SBF_WINDOW, DT_SBF_NOENTRY, DT_SBF_DEV, DT_SBF_TYPED, DT_SBF_TAINTED, DT_SBF_VALS, DT_SBF_FOREIGN, DT_SBF_PROBE, DT_SBF_N };
static const char* const kDtSbWhyName[DT_SBF_N] = { "would take it", "a recording open", "no record", "another device", "a typed block",
    "a tainted recording", "values not known", "another thread unsettled", "DXVK's rules not confirmed" };
DEVSTAT(static LONG dtSbApplyWhy[DT_SBF_N] = {};)
DEVSTAT(static volatile LONG dtSbLastWhy = -1;)     // the last Apply's answer, for the harness
static bool        dtSbProbeOk = false;             // the conformance probe's answer at install (DtSbProbe, below)
DEVSTAT(static LONG dtSbRecApplies = 0;)            // Applies recorded
DEVSTAT(static volatile LONG dtSbRecApplyFailed = 0;)   // ...of which the worker's replay failed
static const char* dtSbProbeWhy = "not run";

// ---- the table ------------------------------------------------------------------------------------------------------
// Under the lock, exclusive. Finds sb's entry, or a free one; null when the table is full.
static DtSbEnt* DtSbSlotX(const void* sb)
{
    DtSbEnt* freeE = nullptr; int live = 0;
    for (int i = 0; i < DT_SB_CAP; i++)
    {
        if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].sb == sb) return &dtSbTab[i];
        if (dtSbTab[i].kind != DT_SBK_FREE) live++;
        else if (!freeE) freeE = &dtSbTab[i];
    }
    return live < dtSbCapLimit ? freeE : nullptr;
}
// Overwrite whatever was at that address: every successful EndStateBlock (the learned record, or a tainted one) and every
// CreateStateBlock (typed, no record), from any thread and any device.
static bool DtSbPut(const void* sb, const void* dev, BYTE kind, LONG taint, const DtSbMir* m)
{
    if (!sb) return false;
    AcquireSRWLockExclusive(&dtSbLock);
    DtSbEnt* const e = DtSbSlotX(sb);
    if (!e) { DEVSTAT(dtSbFull++;) ReleaseSRWLockExclusive(&dtSbLock); return false; }
    e->sb = sb; e->dev = dev; e->kind = kind; e->taint = taint; e->caps = 0;
    e->clean = kind == DT_SBK_REC && taint == DT_SBT_NONE;
    e->valsOk = e->clean;                                  // a recording's values are exact as recorded
    if (m && e->clean) e->m = *m; else memset(&e->m, 0, sizeof(e->m));
    DEVSTAT(LONG live = 0; for (int i = 0; i < DT_SB_CAP; i++) live += dtSbTab[i].kind != DT_SBK_FREE;
    if (live > dtSbLivePeak) dtSbLivePeak = live;)
    ReleaseSRWLockExclusive(&dtSbLock);
    return true;
}
// Retire at the final Release: the native count reached 0.
static void DtSbRetire(const void* sb)
{
    AcquireSRWLockExclusive(&dtSbLock);
    for (int i = 0; i < DT_SB_CAP; i++) if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].sb == sb) { dtSbTab[i].kind = DT_SBK_FREE; dtSbTab[i].sb = nullptr; }
    ReleaseSRWLockExclusive(&dtSbLock);
}
// A device's final Release: its blocks are gone with it.
static void DtSbDropDev(const void* dev)
{
    AcquireSRWLockExclusive(&dtSbLock);
    for (int i = 0; i < DT_SB_CAP; i++) if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].dev == dev) { dtSbTab[i].kind = DT_SBK_FREE; dtSbTab[i].sb = nullptr; }
    ReleaseSRWLockExclusive(&dtSbLock);
}
// A recording opened (+1), closed or refused (-1), or its device gone (0: all of that device's).
static void DtSbRecCount(const void* d, LONG by)
{
    AcquireSRWLockExclusive(&dtSbLock);
    int k = -1;
    for (int i = 0; i < 8; i++) if (dtSbRecDevN[i] && dtSbRecDev[i] == d) { k = i; break; }
    if (k < 0 && by > 0) for (int i = 0; i < 8; i++) if (!dtSbRecDevN[i]) { k = i; dtSbRecDev[i] = d; break; }
    if (k >= 0) dtSbRecDevN[k] = by && dtSbRecDevN[k] + by > 0 ? dtSbRecDevN[k] + by : 0;
    else if (by > 0) dtSbRecOver++;
    else if (by < 0 && dtSbRecOver > 0) dtSbRecOver--;
    LONG t = dtSbRecOver; for (int i = 0; i < 8; i++) t += dtSbRecDevN[i];
    _InterlockedExchange(&dtSbRecOpen, t);
    ReleaseSRWLockExclusive(&dtSbLock);
}
// The block's values can no longer be vouched for: a Capture another thread made, or one made unarmed or on another device.
static void DtSbInvalidate(const void* sb)
{
    AcquireSRWLockExclusive(&dtSbLock);
    for (int i = 0; i < DT_SB_CAP; i++) if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].sb == sb) dtSbTab[i].valsOk = false;
    ReleaseSRWLockExclusive(&dtSbLock);
}

// Another thread's call gave up waiting for the worker (DtForeignBarrier, after 1 s) and lands ahead of whatever is queued, a
// Capture's replay included: no record's values can be vouched for, and an open window is tainted.
static void DtSbVoidAll()
{
    _InterlockedIncrement(&dtSbGiveUps);
    AcquireSRWLockExclusive(&dtSbLock);
    for (int i = 0; i < DT_SB_CAP; i++) dtSbTab[i].valsOk = false;
    ReleaseSRWLockExclusive(&dtSbLock);
    if (dtSbWinDev) _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_GAVEUP, DT_SBT_NONE);
}

// ---- the window ----
// Only the window's own thread shuts it. Readers read the device, then the thread id; the owner opens with the device (a CAS)
// then its id, and shuts with id 0 then a null device. So a thread reads its own id beside a device only inside its own window.
static __forceinline bool DtSbMine(const void* d) { return dtSbWinDev == d && dtSbWinTid == __readfsdword(0x24); }
static void DtSbWinTaint(const void* d, LONG why)
{
    if (DtSbMine(d)) _InterlockedCompareExchange(&dtSbWinTaint, why, DT_SBT_NONE);
}
static void DtSbWinShut()
{
    dtSbWinTaint = DT_SBT_NONE; dtSbWinEnded = 0;
    dtSbWinTid = 0;
    _InterlockedExchangePointer((PVOID volatile*)&dtSbWinDev, nullptr);
}
// BeginStateBlock, before the DXVK call: a Set DXVK diverts into this recording reaches DXVK after the Begin, and so (through
// DXVK's device lock) after this. False when another window is open; this recording is then not learned.
static bool DtSbWinOpen(const void* d)
{
    if (DT_SBM_FAULT(23)) return false;                    // NC23: never learn here
    if (DT_SBM_FAULT(18) && !dtArmed) return false;        // NC18: learning only while armed
    const DWORD t = __readfsdword(0x24);
    // this thread's own window, whose recording another thread ended: over (read interlocked, as the CAS below writes it)
    if (InterlockedCompareExchangePointer((PVOID volatile*)&dtSbWinDev, nullptr, nullptr) && dtSbWinTid == t && dtSbWinEnded) DtSbWinShut();
    if (InterlockedCompareExchangePointer((PVOID volatile*)&dtSbWinDev, (PVOID)d, nullptr) != nullptr) { DEVSTAT(dtSbWinBusy++;) return false; }
    dtSbWinTid = t;
    memset(&dtSbWinM, 0, sizeof(dtSbWinM));
    dtSbWinArmed = dtArmed;
    dtSbWinArmGen = dtSbArmGen;
    dtSbWinForeign = dtForeign;
    dtSbWinUf = dtSbUfSeq;
    dtSbWinBody = dtSbBodyPosts;
    dtSbWinReset = dtSbResetSeq;
    dtSbWinGiveUp = dtSbGiveUps;
    LONG why = DT_SBT_NONE;
    if (dtSbWinArmed) { if (d != dtDev) why = DT_SBT_OTHERDEV; else if (t != dtProducer) why = DT_SBT_NOTPRODUCER; }
    else if (renderTid && t != renderTid) why = DT_SBT_OTHERTHREAD;
    if (why) _InterlockedCompareExchange(&dtSbWinTaint, why, DT_SBT_NONE);   // a Reset's taint that landed first stays
    return true;
}
// The Begin failed: DXVK is not recording for this thread, so there is nothing to learn.
static void DtSbWinRetract(const void* d) { if (DtSbMine(d)) DtSbWinShut(); }
// A device's final Release: its blocks, its open recordings and a window on it go with it. The window is shut whoever owns
// it: nothing can call a device whose last reference went, and left open it would hold off every arm (DT_SB_ARM_DEFER).
// Thread id first, then the device by compare-exchange, so a window since opened on a new device stays.
static void DtSbDevGone(const void* d)
{
    DtSbDropDev(d);
    DtSbRecCount(d, 0);
    if (InterlockedCompareExchangePointer((PVOID volatile*)&dtSbWinDev, nullptr, nullptr) == d)   // read interlocked, as the CAS below writes it
    {
        dtSbWinTaint = DT_SBT_NONE; dtSbWinEnded = 0; dtSbWinTid = 0;
        InterlockedCompareExchangePointer((PVOID volatile*)&dtSbWinDev, nullptr, (PVOID)d);
    }
}
static const char* DtResetBarrier(DWORD* msOut);     // dt_install.h: another thread's End of the producer's recording runs it
// EndStateBlock succeeded: judge the window and write the entry at the block's address, overwriting what was there.
static void DtSbWinClose(const void* d, IDirect3DStateBlock9* sb)
{
    const DWORD t = __readfsdword(0x24);
    const DWORD owner = dtSbWinDev == d ? dtSbWinTid : 0;  // 0: no window here, or one opening or closing on another thread
    if (!owner) { DtSbPut(sb, d, DT_SBK_REC, DT_SBT_NOWINDOW, nullptr); DEVSTAT(dtSbWinTaintN[DT_SBT_NOWINDOW]++;) return; }
    if (owner != t)
    {
                // Another thread ended this window's recording, so DXVK handed that thread the block. Not learned; the window is marked over
        // and its owner shuts it. If the owner is the armed producer, its dtSbRec stays up while its Sets now reach the device, so the
        // Reset barrier disarms the layer, which arms again from a freshly read shadow.
        _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_FOREIGNEND, DT_SBT_NONE);
        _InterlockedExchange(&dtSbWinEnded, 1);
        DtSbPut(sb, d, DT_SBK_REC, DT_SBT_FOREIGNEND, nullptr); DEVSTAT(dtSbWinTaintN[DT_SBT_FOREIGNEND]++;)
        if (dtArmed && d == dtDev && owner == dtProducer && t != dtWorkerTid && !dtDisarmAsk && !DT_SBM_FAULT(30))   // NC30: no barrier
        {
            static volatile LONG said = 0;
            if (_InterlockedExchange(&said, 1) == 0)
                LogRecord("  threaded: thread %lu ended a state block the render thread was recording: disarm (the Reset barrier), arm"
                          " again from a fresh copy. Said once.", (unsigned long)t);
            DtResetBarrier(nullptr);
        }
        return;
    }
    LONG why = _InterlockedOr(&dtSbWinTaint, 0);          // read interlocked: other threads write it with a CAS
    if (!why && (dtArmed != dtSbWinArmed || (dtSbArmGen != dtSbWinArmGen && !DT_SBM_FAULT(29)))) why = DT_SBT_ARM;   // NC29: test the flag only
    if (!why && renderTid && renderTid != t && !DT_SBM_FAULT(31)) why = DT_SBT_OTHERTHREAD;   // the render thread named inside it: its Sets are not bracketed (NC31)
    if (!why && dtSbBodyPosts != dtSbWinBody) why = DT_SBT_BODY;
    if (!why && (dtSbResetIn || dtSbResetSeq != dtSbWinReset)) why = DT_SBT_RESET;
    if (!why && dtSbGiveUps != dtSbWinGiveUp) why = DT_SBT_GAVEUP;
    if (!why && dtSbWinArmed && !DT_SBM_FAULT(17))
    {
                // In flight first, then the count: DtForeignLanded raises the count and only then leaves flight, so a call read as out of
        // flight has already raised it.
        if (dtForeignInFlight || dtForeign != dtSbWinForeign) why = DT_SBT_FOREIGN;
        else if (dtSbUfIn || dtSbUfSeq != dtSbWinUf) why = DT_SBT_UFOREIGN;    // a bracketed call that straddled an arm; armed, no wait
    }
    if (!why && !dtSbWinArmed)
    {
        // A bracketed Set in flight may have reached DXVK before the End (so into this block) and not yet bumped the count.
        LARGE_INTEGER t0, tn; QueryPerformanceCounter(&t0);
        while (dtSbUfIn)
        {
            _mm_pause(); QueryPerformanceCounter(&tn);
            if ((tn.QuadPart - t0.QuadPart) * 1000 >= dtQpf) { why = DT_SBT_USTUCK; break; }
        }
        if (!why && dtSbUfSeq != dtSbWinUf) why = DT_SBT_UFOREIGN;
    }
    const bool stored = DtSbPut(sb, d, DT_SBK_REC, why, &dtSbWinM);
    DEVSTAT(if (!stored) dtSbNotStored++;
    else if (why) dtSbWinTaintN[why]++;
    else dtSbWinLearned++;)
    (void)stored;
    DtSbWinShut();
}
// A RESET (dt_device_sync.h, dt_Reset): in flight from before its barrier to after DXVK returns, so a window opened in between is
// judged by it at its close; an open window on the device is tainted at both ends.
static void DtSbResetIn(const void* d)
{
    _InterlockedIncrement(&dtSbResetIn);
    if (dtSbWinDev == d) _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_RESET, DT_SBT_NONE);
}
static void DtSbResetOut(const void* d)
{
    if (dtSbWinDev == d) _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_RESET, DT_SBT_NONE);
    _InterlockedIncrement(&dtSbResetSeq);
    _InterlockedDecrement(&dtSbResetIn);
}

// THE ADDS: a Set that went into the window's recording, as DXVK's recorder keeps it. Called from the tracked wrappers
// (dt_device_rec.h) on the recorded path while the producer records, and on the direct path after DXVK took the call.
static void DtSbWinRs(const void* d, DWORD s, DWORD v)
{
    if (!DtSbMine(d)) return;
    const bool refusedOk = DT_SBM_FAULT(10) && s >= 1 && s <= 6;       // NC10: record a Set DXVK refuses
    if ((s < 7 || s > 209) && !refusedOk) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_RS, DT_SBT_NONE); return; }
    dtSbWinM.rsM[s >> 5] |= 1u << (s & 31); dtSbWinM.rs[s] = v;
}
static void DtSbWinSamp(const void* d, int i, DWORD t, DWORD v, bool ok)
{
    if (!DtSbMine(d)) return;
    if (!ok) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    if (DT_SBM_FAULT(9)) return;                                          // NC9: drop one diverted Set
    dtSbWinM.sampM[i] |= (WORD)(1u << t); dtSbWinM.samp[i][t] = v;
}
static void DtSbWinTex(const void* d, int i, IDirect3DBaseTexture9* tex)
{
    if (!DtSbMine(d)) return;
    if (i < 0) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    dtSbWinM.texM |= 1u << i; dtSbWinM.tex[i] = tex;
}
static void DtSbWinVp(const void* d, const D3DVIEWPORT9* vp)
{
    if (!DtSbMine(d)) return;
    if (!vp) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    dtSbWinM.vpM = true; dtSbWinM.vp = *vp;                               // raw: DXVK lifts MaxZ only when it sets the device
}
static void DtSbWinSc(const void* d, const RECT* r)
{
    if (!DtSbMine(d)) return;
    if (!r) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    dtSbWinM.scM = true; dtSbWinM.sc = *r;
}
static void DtSbWinVs(const void* d, IDirect3DVertexShader9* s) { if (DtSbMine(d)) { dtSbWinM.vsM = true; dtSbWinM.vs = s; } }
static void DtSbWinPs(const void* d, IDirect3DPixelShader9* s)  { if (DtSbMine(d)) { dtSbWinM.psM = true; dtSbWinM.ps = s; } }
static void DtSbWinIb(const void* d, IDirect3DIndexBuffer9* ib) { if (DtSbMine(d)) { dtSbWinM.ibM = true; dtSbWinM.ib = ib; } }
static void DtSbWinVb(const void* d, UINT i, IDirect3DVertexBuffer9* vb, UINT off, UINT str)
{
    if (!DtSbMine(d)) return;
    if (i >= 16) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    dtSbWinM.vbM |= 1u << i; dtSbWinM.vb[i] = vb; dtSbWinM.vbOff[i] = off; dtSbWinM.vbStr[i] = str;   // raw, a null buffer's pair kept
}
static void DtSbWinDecl(const void* d, IDirect3DVertexDeclaration9* x)
{
    if (!DtSbMine(d)) return;
    dtSbWinM.declForm = DT_SBD_OBJ; dtSbWinM.decl = x; dtSbWinM.fvfOk = false;       // null included: Apply skips it
}
static void DtSbWinFvf(const void* d, DWORD f)
{
    if (!f || !DtSbMine(d)) return;                                        // SetFVF(0) records nothing (d3d9_device.cpp:3491-3492)
    dtSbWinM.declForm = DT_SBD_FVF; dtSbWinM.decl = nullptr; dtSbWinM.fvf = f; dtSbWinM.fvfOk = true;
}
static void DtSbWinPsc(const void* d, UINT start, const float* data, UINT count)
{
    if (!DtSbMine(d)) return;
    if (!count)                                                            // records nothing (d3d9_device.cpp:8059-8060)
    {
        if (start > 224u) _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE);
        return;
    }
    if (start >= 224u || count > 224u - start || !data) { _InterlockedCompareExchange(&dtSbWinTaint, DT_SBT_REFUSED, DT_SBT_NONE); return; }
    memcpy(dtSbWinM.psc[start], data, count * 16);
    for (UINT r = start; r < start + count; r++) { dtSbWinM.pscM[r >> 5] |= 1u << (r & 31); dtSbWinM.pscK[r >> 5] |= 1u << (r & 31); }
}

// ---- the bracket ----
// The tracked wrappers' direct path (dt_device_rec.h). A thread that is not the render thread (every thread while renderTid
// is 0) and not inside its own window brackets its call: in flight, then done, armed or not, and in flight before the arm is
// read, so a call that straddles an arm or a disarm is still counted. Armed, the call is DT_ORDERED inside the bracket.
// NC26: no bracket.
// Not the worker: an injection body's Sets on the worker are ordered in the stream; a body posted inside a recording is its
// own taint (DT_SBT_BODY). NC33 brackets the worker.
template <class F> static __forceinline HRESULT DtOrderedTrk(const void* d, int slot, F call)
{
    const DWORD t = __readfsdword(0x24);
    if (t == renderTid || (t == dtWorkerTid && !DT_SBM_FAULT(33)) || DtSbMine(d) || DT_SBM_FAULT(26)) return dtArmed ? DtOrdered(d, slot, call) : call();
    _InterlockedIncrement(&dtSbUfIn);
    const HRESULT hr = dtArmed ? DtOrdered(d, slot, call) : call();
    _InterlockedIncrement(&dtSbUfSeq);
    _InterlockedDecrement(&dtSbUfIn);
    return hr;
}

// ---- the FVF map: the object DXVK binds for an FVF, learned where a drained GetVertexDeclaration names it ----
static struct { DWORD fvf; IDirect3DVertexDeclaration9* obj; } dtShFvfObj[16];
static unsigned dtShFvfObjN = 0;
DEVSTAT(static LONG dtShFvfObjLearned = 0, dtSbFvfAsks = 0, dtSbFvfKnown = 0;)   // the development report's
// Every drain for a declaration a recorded SetFVF bound is counted, and so is each one whose object the map already knew:
// with the recorded Apply those are answered from the copy.
static void DtSbFvfLearn(DWORD f, IDirect3DVertexDeclaration9* obj)
{
    DEVSTAT(dtSbFvfAsks++;)
    if (!f || !obj) return;
    for (unsigned i = 0; i < dtShFvfObjN; i++) if (dtShFvfObj[i].fvf == f) { DEVSTAT(if (dtShFvfObj[i].obj == obj) dtSbFvfKnown++;) dtShFvfObj[i].obj = obj; return; }
    if (dtShFvfObjN < 16) { dtShFvfObj[dtShFvfObjN].fvf = f; dtShFvfObj[dtShFvfObjN].obj = obj; dtShFvfObjN++; DEVSTAT(dtShFvfObjLearned++;) }
}

// The worker's recorded Apply failed: counted against its own code, and the foreign count raised so the producer's next Get
// settles, drains, and re-reads the device. NC24: counted as a resource method, no re-sync.
static void DtSbReplayFailed(HRESULT hr)
{
    if (DT_SBM_FAULT(24)) { DtNoteReplayErr(DT_WHO_RES, hr); return; }
    DtNoteReplayErr(DT_WHO_SBAPPLY, hr);
    DEVSTAT(_InterlockedIncrement(&dtSbRecApplyFailed);)
    _InterlockedIncrement(&dtForeign);
}
// The FVF map's answer: only with the recorded Apply, only on a DXVK the probe confirmed.
static IDirect3DVertexDeclaration9* DtSbFvfObjOf(DWORD f)
{
    if (!DT_SB_REC_ON() || !dtSbProbeOk) return nullptr;
    for (unsigned i = 0; i < dtShFvfObjN; i++) if (dtShFvfObj[i].fvf == f) return dtShFvfObj[i].obj;
    return nullptr;
}

// ---- the one body path that puts nothing back ----
// The render-size redirect's device half (DlssIvTransport.h, IvtRsEndRedirectDevice) runs in the body on the worker: binding
// render target 0 resets the viewport and scissor, and it sets the viewport, restoring none of them. The post calls this after
// the note is in the ring (DtShKnowBody has run), so the next Get of each waits the body out and asks DXVK, and a Capture
// beside the body leaves its values unknown. Dormant: the redirect is not armed when the seam fires.
static void DtSbBodyForgets()
{
    dtSh.rtOk[0] = false; dtSh.vpOk = false; dtSh.scOk = false;
}

#if DLSSIV_DEV
// ---- the development report (DtReport, every 600 frames at log=2) ----
// The state-block line: with the recorded Apply (Windows), what it did; with the drained one (Wine, or a DXVK the probe
// refused), what it would do. Applies are counted a frame over the report's window; recordings and Captures are the session's.
static LONG dtSbApplyWhyBase[DT_SBF_N] = {}, dtSbFvfAsksBase = 0, dtSbFvfKnownBase = 0;
static void DtSbReport(double n)
{
    char tw[480]; size_t a = 0; tw[0] = 0; LONG notClean = 0;
    for (int k = 1; k < DT_SBT_N; k++)
        if (dtSbWinTaintN[k]) { notClean += dtSbWinTaintN[k]; a += _snprintf_s(tw + a, sizeof(tw) - a, _TRUNCATE, "%s%s %ld", a ? ", " : "", kDtSbTaintName[k], dtSbWinTaintN[k]); }
    char ap[480]; size_t b = 0; ap[0] = 0; LONG tot = 0;
    for (int k = 0; k < DT_SBF_N; k++) tot += dtSbApplyWhy[k] - dtSbApplyWhyBase[k];
    for (int k = 0; k < DT_SBF_N; k++)
    {
        const LONG c = dtSbApplyWhy[k] - dtSbApplyWhyBase[k];
        if (c) b += _snprintf_s(ap + b, sizeof(ap) - b, _TRUNCATE, "%s%s %.2f", b ? ", " : "", kDtSbWhyName[k], c / n);
        dtSbApplyWhyBase[k] = dtSbApplyWhy[k];
    }
    LogHealth("    state blocks (C1; %s; DXVK probe: %s): the render thread's Applies %.2f a frame, which at B go: %s;"
        " a GetVertexDeclaration drained behind a recorded SetFVF %.2f a frame, %.2f of them for an object already known. Session: %ld recordings learned clean,"
        " %ld not (%s), %ld Begins while another was being learned, %ld recordings open at an arm; records peak %ld of %d%s;"
        " the render thread's Captures %ld copied, %ld inside a recording, %ld with another thread unsettled, %ld over an entry the shadow did not know;"
        " %ld Captures by another thread, %ld unarmed or on another device; FVF objects learned %ld; recorded Applies %ld (%ld failed on the"
        " worker); arms held off while a recording was open %ld",
        DT_SB_REC_ON() ? "at B, the recorded Apply ON" : "at A, learned only", dtSbProbeWhy, tot / n, ap[0] ? ap : "none", (dtSbFvfAsks - dtSbFvfAsksBase) / n, (dtSbFvfKnown - dtSbFvfKnownBase) / n, dtSbWinLearned, notClean, tw[0] ? tw : "none",
        dtSbWinBusy, dtSbWinAtArm, dtSbLivePeak, (int)DT_SB_CAP, dtSbFull ? " (FULL: blocks went without a record)" : "",
        dtSbCapCopied, dtSbCapInRec, dtSbCapInvalid, dtSbCapUnknown, (long)dtSbCapForeign, (long)dtSbCapNotOurs, dtShFvfObjLearned,
        dtSbRecApplies, (long)dtSbRecApplyFailed, dtSbArmDeferred);
    dtSbFvfAsksBase = dtSbFvfAsks; dtSbFvfKnownBase = dtSbFvfKnown;
}
#endif

// ---- the DXVK conformance probe ----
// The record is written into the shadow by DXVK 3.1's rules, and Linux players run whatever DXVK Proton gives them. So once,
// at install, before any hook, the rules the writer depends on are tried on the fresh device through DXVK's own functions, and
// what was touched is put back: a recorded stream offset survives a Capture that changed the device's; a captured null
// declaration is not applied; a null stream in a block zeroes the offset and the stride; a recorded viewport with MinZ = MaxZ
// reads back lifted; Apply and Capture are refused while recording, and an Apply leaves a state outside its block alone;
// SetFVF(f) twice binds one object. Any rule that fails keeps the recorded path and the FVF map off for the session, said
// once. About forty calls.
static const char* const kDtSbProbeStuck = "the device may still be recording: an End failed twice";   // compared by pointer
static const char* DtSbProbeRun(IDirect3DDevice9* d, IDirect3DVertexBuffer9* vb, IDirect3DVertexDeclaration9* de)
{
    typedef HRESULT(__stdcall* Beg_t)(IDirect3DDevice9*);
    typedef HRESULT(__stdcall* End_t)(IDirect3DDevice9*, IDirect3DStateBlock9**);
    typedef HRESULT(__stdcall* SSS_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
    typedef HRESULT(__stdcall* SDecl_t)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
    typedef HRESULT(__stdcall* SVP_t)(IDirect3DDevice9*, const D3DVIEWPORT9*);
    typedef HRESULT(__stdcall* SRS_t)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
    typedef HRESULT(__stdcall* SFvf_t)(IDirect3DDevice9*, DWORD);
    if (!vb || !de) return "the probe's buffer or declaration could not be made";
    const Beg_t Beg = (Beg_t)dtDx[60]; const End_t End = (End_t)dtDx[61];
    const SSS_t SSS = (SSS_t)dtDx[100]; const DtGetSSrc_t GSS = (DtGetSSrc_t)dtDx[101];
    const SDecl_t SDecl = (SDecl_t)dtDx[87]; const DtGetDecl_t GDecl = (DtGetDecl_t)dtDx[88];
    const SVP_t SVP = (SVP_t)dtDx[47]; const DtGetVP_t GVP = (DtGetVP_t)dtDx[48];
    const SRS_t SRS = (SRS_t)dtDx[57]; const DtGetRS_t GRS = (DtGetRS_t)dtDx[58]; const SFvf_t SFvf = (SFvf_t)dtDx[89];
    const char* why = nullptr;
    IDirect3DStateBlock9* b[5] = {};
    // what is touched, to put back
    IDirect3DVertexBuffer9* v0 = nullptr, * v1 = nullptr; UINT o0 = 0, s0 = 0, o1 = 0, s1 = 0;
    IDirect3DVertexDeclaration9* d0 = nullptr; D3DVIEWPORT9 vp0 = {}; DWORD rs0 = 0;
    const bool had0 = SUCCEEDED(GSS(d, 0, &v0, &o0, &s0)), had1 = SUCCEEDED(GSS(d, 1, &v1, &o1, &s1));
    const bool hadD = SUCCEEDED(GDecl(d, &d0)), hadVp = SUCCEEDED(GVP(d, &vp0)), hadRs = SUCCEEDED(GRS(d, D3DRS_TEXTUREFACTOR, &rs0));
    IDirect3DVertexBuffer9* gv = nullptr; UINT go = 0, gs = 0; IDirect3DVertexDeclaration9* gd = nullptr;
    // A RECORDING IS ALWAYS ENDED once it has begun: a device left recording would divert every Set the game makes after.
    bool stuck = false;
    auto rec = [&](IDirect3DStateBlock9** out, auto set) -> bool
    {
        if (FAILED(Beg(d))) return false;
        set();
        IDirect3DStateBlock9* t = nullptr;
        HRESULT h = End(d, &t);
        if (FAILED(h)) { t = nullptr; h = End(d, &t); if (FAILED(h)) stuck = true; }   // DXVK 3.1 cannot fail here; another might
        *out = t;
        return SUCCEEDED(h) && t;
    };
    // NOTHING IS TOUCHED if the very first recording cannot be made (a device already recording, say): the put-back below would
    // then be recorded into somebody else's block.
    if (FAILED(Beg(d)))
    {
        if (v0) v0->Release();
        if (v1) v1->Release();
        if (d0) d0->Release();
        return "1: a recording could not be made";
    }
    { SSS(d, 0, vb, 16, 16); IDirect3DStateBlock9* t = nullptr; HRESULT h = End(d, &t); if (FAILED(h)) { t = nullptr; h = End(d, &t); if (FAILED(h)) stuck = true; } b[0] = t; }
    // 1. a recorded offset survives a Capture that changed the device's; the stride is Captured
    if (!b[0]) why = "1: a recording could not be made";
    else
    {
        SSS(d, 0, vb, 32, 20); b[0]->Capture(); SSS(d, 0, vb, 48, 16); b[0]->Apply();
        if (FAILED(GSS(d, 0, &gv, &go, &gs)) || gv != vb || go != 16 || gs != 20) why = "1: a recorded stream offset did not survive a Capture, or its stride was not Captured";
        if (gv) { gv->Release(); gv = nullptr; }
    }
    // 2. a captured null declaration is not applied
    if (!why)
    {
        if (!rec(&b[1], [&] { SDecl(d, de); })) why = "2: a recording could not be made";
        else
        {
            SDecl(d, nullptr); b[1]->Capture(); SDecl(d, de); b[1]->Apply();
            if (FAILED(GDecl(d, &gd)) || gd != de) why = "2: a captured null declaration was applied";
            if (gd) { gd->Release(); gd = nullptr; }
            // 2b. a Capture takes a non-null declaration: the FVF table's object, Captured over `de`, is what Apply binds
            if (!why)
            {
                IDirect3DVertexDeclaration9* fo = nullptr;
                SFvf(d, D3DFVF_XYZ | D3DFVF_DIFFUSE); GDecl(d, &fo);
                b[1]->Capture(); SDecl(d, de); b[1]->Apply();
                if (FAILED(GDecl(d, &gd)) || !fo || gd != fo) why = "2: a Capture did not take a non-null declaration";
                if (gd) { gd->Release(); gd = nullptr; }
                if (fo) fo->Release();
            }
        }
    }
    // 3. a null stream in a block zeroes the offset and the stride
    if (!why)
    {
        if (!rec(&b[2], [&] { SSS(d, 1, nullptr, 8, 12); })) why = "3: a recording could not be made";
        else
        {
            SSS(d, 1, vb, 4, 16); b[2]->Apply();
            if (FAILED(GSS(d, 1, &gv, &go, &gs)) || gv || go || gs) why = "3: a null stream from a block kept an offset or a stride";
            if (gv) { gv->Release(); gv = nullptr; }
        }
    }
    // 4. a recorded viewport with MinZ = MaxZ reads back lifted
    if (!why)
    {
        const D3DVIEWPORT9 flat = { 0, 0, 4, 4, 0.5f, 0.5f }, other = { 0, 0, 4, 4, 0.0f, 1.0f };
        if (!rec(&b[3], [&] { SVP(d, &flat); })) why = "4: a recording could not be made";
        else
        {
            SVP(d, &other); b[3]->Apply();
            D3DVIEWPORT9 g = {};
            const float lifted = flat.MinZ < flat.MaxZ ? flat.MaxZ : flat.MinZ + 0.001f;
            if (FAILED(GVP(d, &g)) || g.MinZ != 0.5f || g.MaxZ != lifted) why = "4: a recorded viewport with MinZ = MaxZ did not read back lifted";
        }
    }
    // 5. an Apply leaves a render state and another stream outside its block alone; Apply and Capture are each refused while
    // recording
    if (!why)
    {
        DWORD x = 0;
        SRS(d, D3DRS_TEXTUREFACTOR, 0x12345678u); SSS(d, 1, vb, 8, 12); b[0]->Apply();
        if (FAILED(GRS(d, D3DRS_TEXTUREFACTOR, &x)) || x != 0x12345678u) why = "5: an Apply changed a render state outside its block";
        else if (FAILED(GSS(d, 1, &gv, &go, &gs)) || gv != vb || go != 8 || gs != 12) why = "5: an Apply changed a stream outside its block";
        if (gv) { gv->Release(); gv = nullptr; }
        if (!why)
        {
            HRESULT ha = S_OK, hc = S_OK;
            if (!rec(&b[4], [&] { ha = b[0]->Apply(); hc = b[0]->Capture(); })) why = "5: a recording could not be made";
            else if (SUCCEEDED(ha)) why = "5: an Apply was accepted while a block recorded";
            else if (SUCCEEDED(hc)) why = "5: a Capture was accepted while a block recorded";
        }
    }
    // the FVF table: SetFVF(f) twice binds one object
    if (!why)
    {
        IDirect3DVertexDeclaration9* f1 = nullptr, * f2 = nullptr;
        SFvf(d, D3DFVF_XYZ | D3DFVF_DIFFUSE); GDecl(d, &f1);
        SDecl(d, de);
        SFvf(d, D3DFVF_XYZ | D3DFVF_DIFFUSE); GDecl(d, &f2);
        if (!f1 || f1 != f2) why = "6: SetFVF of one value bound two different declarations";
        if (f1) f1->Release();
        if (f2) f2->Release();
    }
    // put back what was touched, and let the blocks go (a live block makes DXVK refuse a Reset)
    SSS(d, 0, had0 ? v0 : nullptr, o0, s0); SSS(d, 1, had1 ? v1 : nullptr, o1, s1);
    if (hadD) SDecl(d, d0);
    if (hadVp) SVP(d, &vp0);
    if (hadRs) SRS(d, D3DRS_TEXTUREFACTOR, rs0);
    if (v0) v0->Release();
    if (v1) v1->Release();
    if (d0) d0->Release();
    for (IDirect3DStateBlock9* x : b) if (x) x->Release();
    if (stuck) return kDtSbProbeStuck;
    return why;
}
static void DtSbProbe(IDirect3DDevice9* d, IDirect3DVertexBuffer9* vb, IDirect3DVertexDeclaration9* de)
{
    const char* const why = DtSbProbeRun(d, vb, de);
    dtSbProbeOk = !why;
    dtSbProbeWhy = why ? why : "every rule holds";
    LogRecord("  threaded: the state-block probe (C1): %s%s", why ? "RULE " : "", dtSbProbeWhy);
        // Only the stuck recording warns: the other failures touched nothing.
    if (why == kDtSbProbeStuck)
        IvtWarn("DXVK could not end a state block recording when the device thread checked it at the game's start, so the game may"
                " render wrongly: threaded=0 in DLSS-IV.cfg turns the device thread off");
}

// ---- the call-site hooks ---------------------------------------------------------------------------------------------
#define DT_SBW_REC(CALL)         if (dtSbRec) { CALL; }
#define DT_SBW_DIR(hr, d, CALL)  if (dtSbWinDev == (const void*)(d) && SUCCEEDED(hr)) { CALL; }
#define DT_SBW_SYNC(d, why)      DtSbWinTaint((d), (why));
#define DT_ORDERED_TRK(d, slot, CALL) DtOrderedTrk(d, slot, [&]() -> HRESULT { return CALL; })
#define DT_SB_BEGIN_OPEN(d)          DtSbRecCount((d), 1); const bool sbOpened_ = DtSbWinOpen(d);
#define DT_SB_BEGIN_DONE(d, hr)      if (FAILED(hr)) { DtSbRecCount((d), -1); if (sbOpened_) DtSbWinRetract(d); }
// A refused End with somewhere to put the block means DXVK was not recording: another thread ended this window's recording,
// and its owner, this thread, shuts it.
#define DT_SB_END_DONE(d, hr, pp)    if (SUCCEEDED(hr) && (pp) && *(pp)) { DtSbRecCount((d), -1); DtSbWinClose((d), *(pp)); } \
                                     else if (FAILED(hr) && (pp) && DtSbMine(d)) DtSbWinShut();
#define DT_SB_CREATED(d, hr, pp)     if (SUCCEEDED(hr) && (pp) && *(pp) && !DT_SBM_FAULT(15)) DtSbPut(*(pp), (d), DT_SBK_TYPED, DT_SBT_NONE, nullptr);
#define DT_SB_RESET(d)               DtSbResetIn(d);
// dt_Reset's DXVK call, and the Reset out of flight once it returns
#define DT_SB_RESET_CALL(d, CALL)    { const HRESULT h_ = CALL; DtSbResetOut(d); return h_; }
#define DT_SB_DEV_GONE(d, r)         if (!(r)) DtSbDevGone(d);
#define DT_SB_AT_DISARM()            _InterlockedIncrement(&dtSbArmGen);
#define DT_SB_GAVE_UP()              DtSbVoidAll();
// At every arm, the only place dtDev changes: a recording open across it is counted (and tainted by DtSbWinClose), and the
// FVF map starts again.
#define DT_SB_AT_ARM()               { _InterlockedIncrement(&dtSbArmGen); DEVSTAT(if (dtSbWinDev) dtSbWinAtArm++;) dtShFvfObjN = 0; }
// dt_GetVertexDeclaration's drained branch: behind a recorded SetFVF(f) the object DXVK hands back is its table's for f,
// unless another thread's call (a SetVertexDeclaration, a SetFVF or an Apply) reached DXVK between the drain and the Get. So
// the foreign counts are taken with the ask and the pair is learned only if none moved: in flight first, then the count.
// NC36 learns it anyway (dt_test.cpp, C1FvfForeign).
#define DT_SB_FVF_ASK()              const bool sbFvfAsk_ = dtSh.declQueued && dtSh.fvfOk; const DWORD sbFvf_ = dtSh.fvf; \
                                     const LONG sbFg_ = dtForeign, sbUf_ = dtSbUfSeq;
#define DT_SB_FVF_LEARNED(obj)       if (sbFvfAsk_ && (DT_SBM_FAULT(36) || (!dtForeignInFlight && dtForeign == sbFg_ && !dtSbUfIn && dtSbUfSeq == sbUf_))) \
                                         DtSbFvfLearn(sbFvf_, (obj));
// dtSbApply on the render thread (dt_res.h, DtSbApplyTry): the path counted by reason, and the recorded Apply where it applies.
#define DT_SB_APPLY(sb)              if (DtSbApplyTry(sb)) return D3D_OK;
// DtMaybeArm: no arm while a recording is open on any thread, or the arm's dtSbRec = false would let the shadow follow Sets
// DXVK is diverting into a block. The harness can switch it off (DT_SB_ARM_DEFER_ON) to reach the arm generation's check
// behind it.
#ifndef DT_SB_ARM_DEFER_ON
#define DT_SB_ARM_DEFER_ON() true
#endif
#define DT_SB_ARM_DEFER(dev)         if (DT_SB_ARM_DEFER_ON() && !DT_SBM_FAULT(32) && (dtSbWinDev || dtSbRecOpen)) { DEVSTAT(dtSbArmDeferred++;) return; }   // NC32: no deferral
// A Release's count, and at 0 (the object gone, or going when the worker reaches it) the record retired (dt_res.h, DtSbRel).
#define DT_SB_REL(C, o, x)           DtSbRel<C>((o), (x))
// DtInstall, before any hook is written: the probe on the fresh device, with the install's own throwaway buffer and declaration.
#define DT_SB_PROBE(d, vb, de)       DtSbProbe((d), (vb), (de));
