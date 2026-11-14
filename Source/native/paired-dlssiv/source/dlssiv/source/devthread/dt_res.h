// The device thread, part 5a of 5: resources, queries, state blocks, the swapchain.
// The lifetime rule: a recorded call names objects by pointer, and DXVK takes its own reference only when the call runs. So
// the producer's Release is itself recorded, behind every call before it. The game is told the count it would have got: one
// AddRef probe gives the count now, and the probe's own Release is recorded with the game's (DT_OP_RELEASE2).
// A Release from another thread is handed to the producer (dtFr, under a lock), which records it at its next Present. Until
// then the object is alive and the caller already has its answer. A disarm runs whatever is still waiting.
// Another thread's Lock, Unlock, Issue, Capture or Apply takes its place behind the ring first (DtOrderForeign, dt_core.h):
// a queued draw may read what the lock rewrites, and a Capture or Issue ahead of the queue captures or fences the wrong
// moment. Its Release is handed over and needs no barrier.
// Locks: a NOOVERWRITE lock of a DEFAULT-pool buffer runs at once (the game promises not to touch what a queued draw reads).
// A DISCARD, which would rename the buffer under a queued draw, is always drained first, as is every lock of any other pool
// (DtBufLock says why). A buffer unlock runs at once: nothing queued between lock and unlock can draw from it. Texture and
// surface unlocks drain.
#pragma once

enum { DTC_VB, DTC_IB, DTC_TEX, DTC_CUBE, DTC_VTEX, DTC_SURF, DTC_VOL, DTC_VS, DTC_PS, DTC_DECL,
       DTC_QUERY, DTC_SB, DTC_SC, DTC_N };
static const char* const kDtClass[DTC_N] = { "vertex buffer", "index buffer", "texture", "cube texture",
    "volume texture", "surface", "volume", "vertex shader", "pixel shader", "vertex declaration", "query",
    "state block", "swapchain" };
static void**   dtVt[DTC_N];                  // each class's vtable, as found at install
static void*    dtRel[DTC_N];                 // each class's own Release, before the hook
DEVSTAT(static LONG dtForeignLocks = 0;)      // locks from other threads while armed (reported)
DEVSTAT(static LONG dtForeignRels  = 0;)      // releases from other threads, handed to the producer (reported)
// Threads inside the hand-over, between the test of dtArmed and the push. A disarm turns dtArmed off, waits for this to read
// 0, then flushes the list once and has everything: the increment and the flag's store are both full fences, so a thread
// that read the flag before its increment landed re-reads it after and goes direct. The producer's path never touches it.
static volatile LONG dtFrBusy = 0;
static __forceinline bool DtForeignThread()
{
    const DWORD t = __readfsdword(0x24);
    return dtArmed && t != dtProducer && t != dtWorkerTid;
}

// Another thread's call on a resource, before it reaches DXVK. Counted as it always was.
static void DtForeignBarrier(const char* what);   // dt_install.h
static void DtForeignLanded();                    // dt_install.h
static __declspec(noinline) void DtForeignOrdered(const char* what)
{
    DEVSTAT(_InterlockedIncrement(&dtForeignLocks);)
    DtForeignBarrier(what);
}
// For the resource wrappers whose return types differ, and which hold a scope with a destructor already
// (DT_RSYNC's drain), so one more costs their prologue nothing: in flight from here to the closing brace.
struct DtForeignScope
{
    const bool on;
    explicit DtForeignScope(const char* what, bool count);
    ~DtForeignScope() { if (on) DtForeignLanded(); }
};

// ---- whose object, and whose count ----
// Another device's object is never recorded: these hooks sit in vtables every DXVK device shares, so a second device (an
// overlay's) would otherwise have some calls queued behind the mod's and the rest run at once. GetDevice's reference goes back
// through DXVK's own Release, so the device wrapper, which drains, never sees it. A device that cannot be named counts as the
// mod's.
template <int C> static bool DtOnDev(void* o)
{
    typedef HRESULT(__stdcall* GetDev_t)(void*, IDirect3DDevice9**);
    IDirect3DDevice9* d = nullptr;
    if (FAILED(((GetDev_t)(*(void***)o)[C == DTC_SC ? 8 : 3])(o, &d)) || !d) return true;
    ((ULONG(__stdcall*)(IDirect3DDevice9*))dtDx[2])(d);
    return d == dtDev;
}

// The count's owner: DXVK forwards a texture level's, a cube face's and a volume level's AddRef and Release to its texture
// (d3d9_subresource.h), so the count and every pair queued against it are the texture's. A level's Release is made as its
// texture's, through the texture's own original, which also keeps a counted pair from entering a hook while dtPendLock is
// held. GetContainer's reference goes back through that original (the caller still holds one). A back buffer or standalone
// surface has no texture (E_NOINTERFACE, which DXVK logs once) and counts for itself.
static const GUID kDtIidBaseTexture = { 0x580CA87E, 0x1D3C, 0x4D54, { 0x99, 0x1D, 0xB7, 0xD3, 0xE3, 0xC2, 0x98, 0xCE } };
template <int C> static IUnknown* DtOwner(IUnknown* o, void** rel)
{
    *rel = dtRel[C];
    if (C != DTC_SURF && C != DTC_VOL) return o;
    IUnknown* t = nullptr;
    const HRESULT hr = C == DTC_SURF ? ((IDirect3DSurface9*)o)->GetContainer(kDtIidBaseTexture, (void**)&t)
                                     : ((IDirect3DVolume9*)o)->GetContainer(kDtIidBaseTexture, (void**)&t);
    if (FAILED(hr) || !t) return o;
    void** const vt = *(void***)t;
    const int k = vt == dtVt[DTC_TEX] ? DTC_TEX : vt == dtVt[DTC_CUBE] ? DTC_CUBE : vt == dtVt[DTC_VTEX] ? DTC_VTEX : -1;
    if (k < 0) { t->Release(); return o; }     // not a class of ours: its own Release, and the level counts alone
    ((ULONG(__stdcall*)(IUnknown*))dtRel[k])(t);
    *rel = dtRel[k];
    return t;
}

// ---- exact counts ----
// A deferred Release leaves two references outstanding until the worker runs it: the caller's and the probe. A later Release
// of the same object before that would read a count two too high per such pair, and a `while (p->Release())` loop would
// release too often. dtPend counts the pairs still queued per object, and one lock makes the count and the pairs one reading:
// whoever defers a Release holds it around its AddRef probe, and the worker holds it around the pair's two Releases.
// 65536 entries (fill limit three quarters): a switch between borderless and exclusive fullscreen hands over about 21,500
// releases with the render thread idle; this holds that twice over. 512 KB, and as much for the compaction copy.
// dtPendPeak counts live objects (with a pair still queued), not entries.
// The home slot takes its width from the table's (DT_PEND_BITS): a narrower hash homes every object into part of the table
// and linear probing turns each find into thousands of compares. The assert below proves the homes span the table.
#define DT_PEND_BITS 16
#define DT_PEND_CAP  (1 << DT_PEND_BITS)
struct DtPendEnt { const void* obj; LONG n; };
static DtPendEnt dtPend[DT_PEND_CAP];
static int       dtPendUsed = 0;
DEVSTAT(static int dtPendLive = 0;)      // objects with a pair still queued (under the lock; the report's and the harness's)
DEVSTAT(static int dtPendPeak = 0;)      // the most dtPendLive has been (under the lock)
static bool      dtPendDead = false;     // some entry's count has come down to 0 since the last compaction (under the lock)
static SRWLOCK   dtPendLock = SRWLOCK_INIT;
static LONG      dtPendMiss = 0;         // deferred without a count because the table was full: reported
// Development builds: each compaction's count and its time under the pair lock, which the worker's counted Releases wait
// behind.
DEVSTAT(static LONG dtPendCompacts = 0; static LONGLONG dtPendCompactT = 0, dtPendCompactMax = 0;)
static_assert((0xFFFFFFFFu >> (32 - DT_PEND_BITS)) == (unsigned)(DT_PEND_CAP - 1), "the pending-pair hash must span the whole table");
static __forceinline int DtPendHome(const void* o) { return (int)(((((DWORD)(UINT_PTR)o) >> 4) * 2654435761u) >> (32 - DT_PEND_BITS)); }
// With the lock held. The object's entry, made if `make` and there is room.
static LONG* DtPendFind(const void* o, bool make)
{
    for (int k = 0, i = DtPendHome(o); k < DT_PEND_CAP; k++, i = (i + 1) & (DT_PEND_CAP - 1))
    {
        if (dtPend[i].obj == o) return &dtPend[i].n;
        if (!dtPend[i].obj)
        {
            if (!make || dtPendUsed >= DT_PEND_CAP * 3 / 4) return nullptr;
            dtPend[i].obj = o; dtPend[i].n = 0; dtPendUsed++;
            return &dtPend[i].n;
        }
    }
    return nullptr;
}
// With the lock held: keeps only the objects that still have pairs queued.
static void DtPendCompact()
{
    DEVSTAT(LARGE_INTEGER cq0; QueryPerformanceCounter(&cq0);)
    static DtPendEnt keep[DT_PEND_CAP];
    int nk = 0;
    for (int i = 0; i < DT_PEND_CAP; i++) if (dtPend[i].obj && dtPend[i].n) keep[nk++] = dtPend[i];
    memset(dtPend, 0, sizeof(dtPend)); dtPendUsed = 0; dtPendDead = false;
    for (int i = 0; i < nk; i++) if (LONG* s = DtPendFind(keep[i].obj, true)) *s = keep[i].n;
    DEVSTAT(LARGE_INTEGER cq1; QueryPerformanceCounter(&cq1);
            { const LONGLONG d = cq1.QuadPart - cq0.QuadPart; dtPendCompacts++; dtPendCompactT += d; if (d > dtPendCompactMax) dtPendCompactMax = d; })
}
// For a Release being deferred now: takes the probe reference and returns the
// count a native Release would have returned.
static ULONG DtDeferCount(IUnknown* o, bool* counted)
{
    AcquireSRWLockExclusive(&dtPendLock);
    LONG* pn = DtPendFind(o, true);
        // Only when a compaction can free something: a table full of live pairs stays full, and compacting it for every Release
    // rebuilds the whole table for nothing.
    if (!pn && dtPendDead) { DtPendCompact(); pn = DtPendFind(o, true); }
    const ULONG c = o->AddRef();               // AddRef is not hooked: DXVK's count, with the probe in it
    const LONG before = pn ? *pn : 0;
    bool firstMiss = false;
    if (pn) { if ((*pn)++ == 0) { DEVSTAT(if (++dtPendLive > dtPendPeak) dtPendPeak = dtPendLive;) } }
    else firstMiss = ++dtPendMiss == 1;
    ReleaseSRWLockExclusive(&dtPendLock);
        // Said once, outside the lock: from here on the count a Release returns is approximate.
    if (firstMiss)
        LogRecord("  threaded: the pending-pair table (%d entries) is full: deferred Release counts approximate", DT_PEND_CAP);
    *counted = pn != nullptr;
    const ULONG q = 2u + 2u * (ULONG)before;   // the probe, the caller's own, and each pair still queued
    return c >= q ? c - q : 0;
}
// The worker, for a counted pair: both Releases and the count move together. fn is the count's owner's own original
// (DtOwner), so no hook is entered with the lock held.
static void DtRelease2Counted(void* fn, void* obj)
{
    AcquireSRWLockExclusive(&dtPendLock);
    ((DtF0)fn)(obj);
    ((DtF0)fn)(obj);
    if (LONG* pn = DtPendFind(obj, false))
    {
        const LONG n = --(*pn);
        if (n <= 0) dtPendDead = true;
        DEVSTAT(if (n == 0) dtPendLive--;)
    }
    ReleaseSRWLockExclusive(&dtPendLock);
}

// ---- foreign releases ----
// The list grows and nothing runs early: a release run at once could free an object a queued call still names. Only an
// allocation failure falls back to that, and the report says so (dtFrOom).
struct DtFr { IUnknown* obj; void* fn; bool counted; };
static CRITICAL_SECTION dtFrLock;
static bool          dtFrInit = false;
static volatile LONG dtFrN = 0;
static LONG          dtFrCap = 0, dtFrOom = 0;
static DtFr*         dtFr = nullptr;
static bool DtFrPush(IUnknown* o, void* fn, bool counted)
{
    EnterCriticalSection(&dtFrLock);
    if (dtFrN == dtFrCap)
    {
        const LONG cap = dtFrCap ? dtFrCap * 2 : 256;
        DtFr* const p = (DtFr*)(dtFr ? HeapReAlloc(GetProcessHeap(), 0, dtFr, cap * sizeof(DtFr))
                                     : HeapAlloc(GetProcessHeap(), 0, cap * sizeof(DtFr)));
        if (!p) { LeaveCriticalSection(&dtFrLock); return false; }
        dtFr = p; dtFrCap = cap;
    }
    dtFr[dtFrN].obj = o; dtFr[dtFrN].fn = fn; dtFr[dtFrN].counted = counted;
    dtFrN = dtFrN + 1;
    LeaveCriticalSection(&dtFrLock);
    return true;
}
// The whole list, leaving an empty one behind; the caller frees it.
static DtFr* DtFrTake(LONG* n)
{
    EnterCriticalSection(&dtFrLock);
    DtFr* const a = dtFr; *n = dtFrN;
    dtFr = nullptr; dtFrN = 0; dtFrCap = 0;
    LeaveCriticalSection(&dtFrLock);
    return a;
}
// The producer, at each recorded Present: records every handed-over release in
// arrival order.
static void DtTakeForeignReleases()
{
    LONG n; DtFr* const a = DtFrTake(&n);
    for (LONG i = 0; i < n; i++) { DtCmdX(DT_OP_RELEASE2 | (a[i].counted ? DT_F_COUNTED : 0u), 3, a[i].fn, a[i].obj); DtCommit(3); }
    if (a) HeapFree(GetProcessHeap(), 0, a);
}
// After disarming: everything handed over runs now, in arrival order.
static void DtFlushForeignReleasesDirect()
{
    if (!dtFrInit) return;
    for (;;)
    {
        LONG n; DtFr* const a = DtFrTake(&n);
        for (LONG i = 0; i < n; i++)
            if (a[i].counted) DtRelease2Counted(a[i].fn, a[i].obj); else { ((DtF0)a[i].fn)(a[i].obj); ((DtF0)a[i].fn)(a[i].obj); }
        if (a) HeapFree(GetProcessHeap(), 0, a);
        if (!n) return;
    }
}

// A state block's record retires with its block, when its count (native, or what a deferred Release would have returned)
// reaches 0. The overwrite at every Create and End (dt_device_sync.h) is the other half, so a missed retirement costs nothing.
template <int C> static __forceinline ULONG DtSbRel(IUnknown* o, ULONG r)
{
    if (C == DTC_SB && (r == 0 || DT_SBM_FAULT(25))) DtSbRetire(o);     // negative control NC25: retired at every Release
    return r;
}
// Another thread's Release of one of ours while armed: handed to the producer, who
// records it at its next Present. Under dtFrBusy, and the flag is asked AGAIN under
// it: see the counter's declaration.
template <int C> static __declspec(noinline) ULONG DtForeignRelease(IUnknown* o)
{
    typedef ULONG(__stdcall* F)(IUnknown*);
    ULONG r;
    _InterlockedIncrement(&dtFrBusy);
    if (DtForeignThread() && DtOnDev<C>(o))
    {
        void* rel;
        IUnknown* const w = DtOwner<C>(o, &rel);      // a level's count is its texture's
        bool counted;
        r = DtDeferCount(w, &counted);
        if (DtFrPush(w, rel, counted)) { DEVSTAT(_InterlockedIncrement(&dtForeignRels);) }
        else
        {
            // No memory for the list: it runs now. Said once, before it runs: a recorded call that still names the object would use a
            // freed one.
            if (_InterlockedIncrement(&dtFrOom) == 1)
                LogRecord("  threaded: the hand-over list could not grow (out of memory): a Release from thread %lu runs NOW, not at the next"
                      " Present (a recorded call naming it would use a freed object)",
                    (unsigned long)__readfsdword(0x24));
            if (counted) DtRelease2Counted(rel, w); else { ((F)rel)(w); ((F)rel)(w); }
        }
    }
    else r = ((F)dtRel[C])(o);
    _InterlockedDecrement(&dtFrBusy);
    return DT_SB_REL(C, o, r);                         // what the caller's own Release would have returned
}

template <int C> static ULONG __stdcall dtRelease(IUnknown* o)
{
    typedef ULONG(__stdcall* F)(IUnknown*);
    if (DtRec())
    {
        if (!DtOnDev<C>(o)) return DT_SB_REL(C, o, ((F)dtRel[C])(o));
        void* rel;
        IUnknown* const w = DtOwner<C>(o, &rel);
        bool counted;
        const ULONG r = DtDeferCount(w, &counted);
        DtCmdX(DT_OP_RELEASE2 | (counted ? DT_F_COUNTED : 0u), 3, rel, w); DtCommit(3);
        return DT_SB_REL(C, o, r);
    }
    if (dtFrInit && DtForeignThread()) return DtForeignRelease<C>(o);
    return DT_SB_REL(C, o, ((F)dtRel[C])(o));
}

// ---- buffers ----------------------------------------------------------------------------
typedef HRESULT(__stdcall* dtT_BufLock)(void*, UINT, UINT, void**, DWORD);
typedef HRESULT(__stdcall* dtT_Unlock0)(void*);
static void* dtVbLockO; static void* dtVbUnlockO; static void* dtIbLockO; static void* dtIbUnlockO;
// A NOOVERWRITE lock runs at once only on a DEFAULT-pool buffer: DXVK drops NOOVERWRITE and DISCARD for every other pool
// (LockBuffer) and uploads such a buffer when a draw uses it, so there the game may rewrite what a queued draw has yet to
// upload: that lock drains. On a DEFAULT buffer DXVK drops DISCARD when NOOVERWRITE comes with it, so that pair is a
// NOOVERWRITE too. Nor on a buffer DXVK uploads at draw time anyway: d3d9.forceDrawTimeBufferUpload (off by default and in
// DXVK's GTAIV profile) makes every DEFAULT buffer that is not directly mapped (DYNAMIC with d3d9.allowDirectBufferMapping on)
// upload at draw (NeedsUpload, DetermineMapMode). dt_install.h reads both options the way DXVK does.
static bool dtDrawTimeUploads = false, dtDirectMapping = true;
static __forceinline bool DtBufNoOverwriteOk(void* s, bool vb)
{
    D3DPOOL pool; DWORD usage;
    if (vb) { D3DVERTEXBUFFER_DESC d; if (FAILED(((IDirect3DVertexBuffer9*)s)->GetDesc(&d))) return false; pool = d.Pool; usage = d.Usage; }
    else    { D3DINDEXBUFFER_DESC d;  if (FAILED(((IDirect3DIndexBuffer9*)s)->GetDesc(&d)))  return false; pool = d.Pool; usage = d.Usage; }
    if (pool != D3DPOOL_DEFAULT) return false;
    return !dtDrawTimeUploads || (dtDirectMapping && (usage & D3DUSAGE_DYNAMIC));
}
// The scopes out of line (as DtSync0 in dt_device_rec.h): the lock and unlock wrappers are on the hot path and hold nothing
// else with a destructor.
static __declspec(noinline) HRESULT DtBufLockPass(void* fn, void* s, UINT o, UINT n, void** pp, DWORD f)
{ DtPassScope p_; return ((dtT_BufLock)fn)(s, o, n, pp, f); }
static __declspec(noinline) HRESULT DtBufLockSync(int reason, void* fn, void* s, UINT o, UINT n, void** pp, DWORD f)
{ DtSyncScope s_(reason); return ((dtT_BufLock)fn)(s, o, n, pp, f); }
static __declspec(noinline) HRESULT DtBufUnlockPass(void* fn, void* s)
{ DtPassScope p_; return ((dtT_Unlock0)fn)(s); }
static __forceinline HRESULT DtBufLock(void* fn, int reason, bool vb, void* s, UINT o, UINT n, void** pp, DWORD f)
{
    if (!DtRec())
    {
        const bool foreign = DtForeignThread();
        if (foreign) DtForeignOrdered(DtReasonName(reason));
        const HRESULT hr = ((dtT_BufLock)fn)(s, o, n, pp, f);
        if (foreign) DtForeignLanded();
        return hr;
    }
    if ((f & D3DLOCK_NOOVERWRITE) && DtBufNoOverwriteOk(s, vb)) return DtBufLockPass(fn, s, o, n, pp, f);
    return DtBufLockSync(reason, fn, s, o, n, pp, f);
}
static __forceinline HRESULT DtBufUnlock(void* fn, void* s)
{
    if (!DtRec()) return ((dtT_Unlock0)fn)(s);
    return DtBufUnlockPass(fn, s);
}
static HRESULT __stdcall dtVbLock(void* s, UINT o, UINT n, void** pp, DWORD f) { return DtBufLock(dtVbLockO, DT_R_VBLOCK, true, s, o, n, pp, f); }
static HRESULT __stdcall dtIbLock(void* s, UINT o, UINT n, void** pp, DWORD f) { return DtBufLock(dtIbLockO, DT_R_IBLOCK, false, s, o, n, pp, f); }
static HRESULT __stdcall dtVbUnlock(void* s) { return DtBufUnlock(dtVbUnlockO, s); }
static HRESULT __stdcall dtIbUnlock(void* s) { return DtBufUnlock(dtIbUnlockO, s); }

// ---- everything else drains -------------------------------------------------------------
// One original per hooked slot, one wrapper each, all the same shape.
#define DT_RSYNC(name, reason, R, PARAMS, ARGS)                                          \
    static void* dt##name##O = nullptr;                                                  \
    typedef R(__stdcall* dtT_r##name) PARAMS;                                            \
    static R __stdcall dt##name PARAMS                                                   \
    {                                                                                    \
        if (!DtRec()) { DtForeignScope f_(DtReasonName(reason), true); return ((dtT_r##name)dt##name##O) ARGS; } \
        DtSyncScope s_(reason);                                                          \
        return ((dtT_r##name)dt##name##O) ARGS;                                          \
    }
// textures: SetLOD 11, SetAutoGenFilterType 14, GenerateMipSubLevels 16, LockRect 19, UnlockRect 20, AddDirtyRect 21
DT_RSYNC(TexSetLOD,   DT_R_TEXOTHER, DWORD,   (void* s, DWORD l), (s, l))
DT_RSYNC(TexAutoGen,  DT_R_TEXOTHER, HRESULT, (void* s, D3DTEXTUREFILTERTYPE f), (s, f))
DT_RSYNC(TexGenMips,  DT_R_TEXOTHER, void,    (void* s), (s))
DT_RSYNC(TexLock,     DT_R_TEXLOCK,  HRESULT, (void* s, UINT l, D3DLOCKED_RECT* r, const RECT* rc, DWORD f), (s, l, r, rc, f))
DT_RSYNC(TexUnlock,   DT_R_TEXLOCK,  HRESULT, (void* s, UINT l), (s, l))
DT_RSYNC(TexDirty,    DT_R_TEXOTHER, HRESULT, (void* s, const RECT* rc), (s, rc))
DT_RSYNC(CubeSetLOD,  DT_R_TEXOTHER, DWORD,   (void* s, DWORD l), (s, l))
DT_RSYNC(CubeAutoGen, DT_R_TEXOTHER, HRESULT, (void* s, D3DTEXTUREFILTERTYPE f), (s, f))
DT_RSYNC(CubeGenMips, DT_R_TEXOTHER, void,    (void* s), (s))
DT_RSYNC(CubeLock,    DT_R_CUBELOCK, HRESULT, (void* s, D3DCUBEMAP_FACES fc, UINT l, D3DLOCKED_RECT* r, const RECT* rc, DWORD f), (s, fc, l, r, rc, f))
DT_RSYNC(CubeUnlock,  DT_R_CUBELOCK, HRESULT, (void* s, D3DCUBEMAP_FACES fc, UINT l), (s, fc, l))
DT_RSYNC(CubeDirty,   DT_R_TEXOTHER, HRESULT, (void* s, D3DCUBEMAP_FACES fc, const RECT* rc), (s, fc, rc))
DT_RSYNC(VTexSetLOD,  DT_R_TEXOTHER, DWORD,   (void* s, DWORD l), (s, l))
DT_RSYNC(VTexAutoGen, DT_R_TEXOTHER, HRESULT, (void* s, D3DTEXTUREFILTERTYPE f), (s, f))
DT_RSYNC(VTexGenMips, DT_R_TEXOTHER, void,    (void* s), (s))
DT_RSYNC(VTexLock,    DT_R_VTEXLOCK, HRESULT, (void* s, UINT l, D3DLOCKED_BOX* b, const D3DBOX* bx, DWORD f), (s, l, b, bx, f))
DT_RSYNC(VTexUnlock,  DT_R_VTEXLOCK, HRESULT, (void* s, UINT l), (s, l))
DT_RSYNC(VTexDirty,   DT_R_TEXOTHER, HRESULT, (void* s, const D3DBOX* bx), (s, bx))
// surfaces: LockRect 13, UnlockRect 14, GetDC 15, ReleaseDC 16. volumes: LockBox 9, UnlockBox 10
// DT_RSYNC_W is DT_RSYNC for an HRESULT with statements ahead of and behind the call (r_ is DXVK's answer, ret_ the caller's).
// The save's question (dt_bbs.h), in a build whose product can arm: the four surface methods are counted ahead of the call
// and behind it. The unlock and ReleaseDC count too: DXVK uploads what a lock or a DC changed when it is given back. With
// DT_BBS_ON at 0 BEFORE is nothing and the two give-backs are plain DT_RSYNC.
#if DT_BBS_ON
#define DT_RSYNC_W(name, reason, PARAMS, ARGS, BEFORE, AFTER)                            \
    static void* dt##name##O = nullptr;                                                  \
    typedef HRESULT(__stdcall* dtT_r##name) PARAMS;                                      \
    static HRESULT __stdcall dt##name PARAMS                                             \
    {                                                                                    \
        const void* const ret_ = _ReturnAddress();                                       \
        BEFORE                                                                           \
        if (!DtRec()) { DtForeignScope f_(DtReasonName(reason), true); const HRESULT r_ = ((dtT_r##name)dt##name##O) ARGS; AFTER return r_; } \
        DtSyncScope s_(reason);                                                          \
        const HRESULT r_ = ((dtT_r##name)dt##name##O) ARGS;                              \
        AFTER                                                                            \
        return r_;                                                                       \
    }
DT_RSYNC_W(SurfLock,  DT_R_SURFLOCK, (void* s, D3DLOCKED_RECT* r, const RECT* rc, DWORD f), (s, r, rc, f), DTBBS((void)ret_; const int bbs_ = (f & D3DLOCK_READONLY) ? 0 : DtBbsEnter(nullptr, DT_BBS_LOCK, s);), DTBBS(DtBbsLeave(bbs_);))
#else
DT_RSYNC(SurfLock,    DT_R_SURFLOCK, HRESULT, (void* s, D3DLOCKED_RECT* r, const RECT* rc, DWORD f), (s, r, rc, f))
#endif
#if DT_BBS_ON
DT_RSYNC_W(SurfUnlock, DT_R_SURFLOCK, (void* s), (s), (void)ret_; const int bbs_ = DtBbsEnter(nullptr, DT_BBS_LOCK, s);, DtBbsLeave(bbs_);)
#else
DT_RSYNC(SurfUnlock,  DT_R_SURFLOCK, HRESULT, (void* s), (s))
#endif
#if DT_BBS_ON
DT_RSYNC_W(SurfGetDC, DT_R_SURFDC,   (void* s, HDC* h), (s, h), DTBBS((void)ret_; const int bbs_ = DtBbsEnter(nullptr, DT_BBS_LOCK, s);), DTBBS(DtBbsLeave(bbs_);))
#else
DT_RSYNC(SurfGetDC,   DT_R_SURFDC,   HRESULT, (void* s, HDC* h), (s, h))
#endif
#if DT_BBS_ON
DT_RSYNC_W(SurfRelDC, DT_R_SURFDC,   (void* s, HDC h), (s, h), (void)ret_; const int bbs_ = DtBbsEnter(nullptr, DT_BBS_LOCK, s);, DtBbsLeave(bbs_);)
#else
DT_RSYNC(SurfRelDC,   DT_R_SURFDC,   HRESULT, (void* s, HDC h), (s, h))
#endif
DT_RSYNC(VolLock,     DT_R_VOLLOCK,  HRESULT, (void* s, D3DLOCKED_BOX* b, const D3DBOX* bx, DWORD f), (s, b, bx, f))
DT_RSYNC(VolUnlock,   DT_R_VOLLOCK,  HRESULT, (void* s), (s))
// queries: GetData 7 (below, by hand). swapchain: GetFrontBufferData 4, GetBackBuffer 5
DT_RSYNC(ScFront,     DT_R_SCOTHER,  HRESULT, (void* s, IDirect3DSurface9* t), (s, t))
// A swapchain's back buffer is PASS, as the device's is (the PASS note in dt_device_sync.h).
#define DT_RPASS(name, R, PARAMS, ARGS)                                                  \
    static void* dt##name##O = nullptr;                                                  \
    typedef R(__stdcall* dtT_r##name) PARAMS;                                            \
    static R __stdcall dt##name PARAMS                                                   \
    {                                                                                    \
        if (!DtRec()) return ((dtT_r##name)dt##name##O) ARGS;                            \
        DtPassScope p_;                                                                  \
        return ((dtT_r##name)dt##name##O) ARGS;                                          \
    }
DT_RPASS(ScBack, HRESULT, (void* s, UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** pp), (s, i, t, pp))

// ---- recorded: a query's Issue, a state block's Capture ------------------------------
static void* dtQIssueO = nullptr; static void* dtSbCaptureO = nullptr; static void* dtSbApplyO = nullptr;
typedef HRESULT(__stdcall* dtT_QIssue)(void*, DWORD);
// A query's GetData waits for its own Issue, not the whole ring: DXVK answers GetData from the query's own state, set by its
// Issue, and the GPU (d3d9_query.cpp), touching nothing else of the device. The game asks one query's data at the top of
// every frame, which would otherwise wait for the injection body behind it. Issue stamps the ring index its command starts
// at; GetData waits for the worker to pass it (DtWaitPast) and then asks DXVK beside the worker, like a PASS call. A query
// with no stamp (issued before arming, or lost to a collision in this small table) drains. Emptied at every arm: ring indices
// do not survive one. GTA IV issues its query ahead of the body, so the wait is short.
struct DtQStamp { void* q; DWORD at; bool ok; };
static DtQStamp dtQStamp[16];
static __forceinline DtQStamp& DtQSlot(void* q) { return dtQStamp[((UINT_PTR)q >> 4) & 15]; }
typedef HRESULT(__stdcall* dtT_rQGetData)(void*, void*, DWORD, DWORD);
static void* dtQGetDataO = nullptr;
static __declspec(noinline) HRESULT DtQGetDataDrained(void* s, void* p, DWORD n, DWORD f)
{ DtSyncScope s_(DT_R_QGETDATA); return ((dtT_rQGetData)dtQGetDataO)(s, p, n, f); }
static __declspec(noinline) HRESULT DtQGetDataAimed(void* s, void* p, DWORD n, DWORD f, DWORD at)
{
    DtWaitPast(at, DT_R_QGETDATA);
    DtPassScope p_;
    return ((dtT_rQGetData)dtQGetDataO)(s, p, n, f);
}
static HRESULT __stdcall dtQGetData(void* s, void* p, DWORD n, DWORD f)
{
    if (!DtRec()) { DEVSTAT(if (DtForeignThread()) _InterlockedIncrement(&dtForeignLocks);) return ((dtT_rQGetData)dtQGetDataO)(s, p, n, f); }
    const DtQStamp& e = DtQSlot(s);
    if (e.ok && e.q == s) return DtQGetDataAimed(s, p, n, f, e.at);
    return DtQGetDataDrained(s, p, n, f);
}
static HRESULT __stdcall dtQIssue(void* q, DWORD f)
{
    if (DtForeignThread() && DtOnDev<DTC_QUERY>(q)) { DtForeignScope f_("a query's Issue", false); return ((dtT_QIssue)dtQIssueO)(q, f); }
    if (!DtRec() || !DtOnDev<DTC_QUERY>(q)) return ((dtT_QIssue)dtQIssueO)(q, f);   // another device's: at once
    DWORD* p = DtCmdX(DT_OP_XCALL0 + 1, 4, dtQIssueO, q);   // a resource method: its own callee and object, no slot, no observer
    p[3] = f;
    { DtQStamp& e = DtQSlot(q); e.q = q; e.at = (DWORD)(p - dtRing); e.ok = true; }   // where this command STARTS: GetData waits for the worker to pass it
    DtCommit(4);
    return D3D_OK;
}
// ---- state blocks: the record at a Capture, the writer, and the Apply's path (dt_sbtab.h) ----
// Every entry the record covers must be known to the shadow; with the drained Apply an unknown one only makes the values
// unknown. The declaration and the pixel constants may be unknown in a record (their Gets drain when unknown), so they do not
// count here.
static bool DtSbAllKnown(const DtSbMir& m)
{
    for (int w = 0; w < 8; w++) for (DWORD x = m.rsM[w]; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); if (!dtSh.rsOk[w * 32 + bit]) return false; }
    for (int i = 0; i < 21; i++) for (DWORD t = 1; t <= 13; t++) if ((m.sampM[i] >> t & 1u) && !dtSh.sampOk[i][t]) return false;
    for (int i = 0; i < 21; i++) if ((m.texM >> i & 1u) && !dtSh.texOk[i]) return false;
    for (int i = 0; i < 16; i++) if ((m.vbM >> i & 1u) && !dtSh.vbOk[i]) return false;
    return !(m.vsM && !dtSh.vsOk) && !(m.psM && !dtSh.psOk) && !(m.ibM && !dtSh.ibOk) && !(m.vpM && !dtSh.vpOk) && !(m.scM && !dtSh.scOk);
}
// With the recorded Apply each covered entry the shadow does not know is fetched from DXVK by the Get rule (exact: an unknown
// entry is named by no queued command, dt_shadow.h), but only where no injection body can hold it: none out, a render state
// outside dtBodyRsTouched, or a sampler slot in dtBodySampFree (streams, shaders, indices, viewport and scissor never are).
// One that cannot be fetched without waiting leaves the values unknown: this never waits. NC21 fetches past the gate.
struct DtSbCov { DWORD rsM[8]; WORD sampM[21]; DWORD texM, vbM; bool vsM, psM, ibM, vpM, scM; };
static bool DtSbFetchCovered(const DtSbCov& c)
{
    IDirect3DDevice9* const d = dtDev;
    const bool gate = dtShBodies != 0 && !DT_SBM_FAULT(21);
    for (int w = 0; w < 8; w++)
        for (DWORD x = c.rsM[w]; x; x &= x - 1)
        {
            DWORD bit; _BitScanForward(&bit, x); const DWORD s = w * 32 + bit;
            if (dtSh.rsOk[s]) continue;
            if (gate && (dtBodyRsTouched[w] >> bit & 1u)) return false;
            DWORD v = 0; if (FAILED(((DtGetRS_t)dtDx[58])(d, (D3DRENDERSTATETYPE)s, &v))) return false;
            dtSh.rs[s] = v; dtSh.rsOk[s] = true; DEVSTAT(dtShFetched++;)
        }
    for (int i = 0; i < 21; i++)
    {
        const bool free = !gate || (dtBodySampFree >> i & 1u);
        for (DWORD t = 1; t <= 13; t++)
        {
            if (!(c.sampM[i] >> t & 1u) || dtSh.sampOk[i][t]) continue;
            if (!free) return false;
            DWORD v = 0; if (FAILED(((DtGetSS_t)dtDx[68])(d, DtSampNum(i), (D3DSAMPLERSTATETYPE)t, &v))) return false;
            dtSh.samp[i][t] = v; dtSh.sampOk[i][t] = true; DEVSTAT(dtShFetched++;)
        }
        if (!(c.texM >> i & 1u) || dtSh.texOk[i]) continue;
        if (!free) return false;
        IDirect3DBaseTexture9* x = nullptr; if (FAILED(((DtGetTex_t)dtDx[64])(d, DtSampNum(i), &x))) return false;
        dtSh.tex[i] = DtRaw(x); dtSh.texOk[i] = true; DEVSTAT(dtShFetched++;)
    }
    for (UINT i = 0; i < 16; i++)
    {
        if (!(c.vbM >> i & 1u) || dtSh.vbOk[i]) continue;
        if (gate) return false;                                   // never free beside a body
        IDirect3DVertexBuffer9* x = nullptr; UINT off = 0, str = 0;
        if (FAILED(((DtGetSSrc_t)dtDx[101])(d, i, &x, &off, &str))) return false;
        dtSh.vb[i] = DtRaw(x); dtSh.vbOff[i] = off; dtSh.vbStr[i] = str; dtSh.vbOk[i] = true; DEVSTAT(dtShFetched++;)
    }
    if (c.vsM && !dtSh.vsOk) { if (gate) return false; IDirect3DVertexShader9* x = nullptr; if (FAILED(((DtGetVS_t)dtDx[93])(d, &x))) return false; dtSh.vs = DtRaw(x); dtSh.vsOk = true; DEVSTAT(dtShFetched++;) }
    if (c.psM && !dtSh.psOk) { if (gate) return false; IDirect3DPixelShader9* x = nullptr; if (FAILED(((DtGetPS_t)dtDx[108])(d, &x))) return false; dtSh.ps = DtRaw(x); dtSh.psOk = true; DEVSTAT(dtShFetched++;) }
    if (c.ibM && !dtSh.ibOk) { if (gate) return false; IDirect3DIndexBuffer9* x = nullptr; if (FAILED(((DtGetIB_t)dtDx[105])(d, &x))) return false; dtSh.ib = DtRaw(x); dtSh.ibOk = true; DEVSTAT(dtShFetched++;) }
    if (c.vpM && !dtSh.vpOk) { if (gate) return false; D3DVIEWPORT9 v; if (FAILED(((DtGetVP_t)dtDx[48])(d, &v))) return false; dtSh.vp = v; dtSh.vpOk = true; DEVSTAT(dtShFetched++;) }
    if (c.scM && !dtSh.scOk) { if (gate) return false; RECT r; if (FAILED(((DtGetSC_t)dtDx[76])(d, &r))) return false; dtSh.sc = r; dtSh.scOk = true; DEVSTAT(dtShFetched++;) }
    return true;
}
// The producer's Capture, armed, on the mod's device, before the command is recorded: the shadow holds exactly the state the
// worker's replay of it will read, so the record takes it from there by DXVK's Capture rules. With the recorded Apply,
// covered unknown entries are fetched first, outside the lock (a fetched pointer's Release can answer a disarm ask). False:
// that happened, so the Capture goes to DXVK directly and the record forgets.
static bool DtSbCaptureCopy(const void* sb)
{
    if (DT_SB_REC_ON() && !(dtSbWinDev == dtDev || dtSbRec || dtSbRecOpen)
        && !dtForeignInFlight && dtForeign == dtForeignSeen && !dtShNeedDrain && !dtSbUfIn)
    {
        DtSbCov c; bool have = false;
        AcquireSRWLockShared(&dtSbLock);
        for (int i = 0; i < DT_SB_CAP; i++)
            if (dtSbTab[i].kind == DT_SBK_REC && dtSbTab[i].sb == sb && dtSbTab[i].clean)
            {
                const DtSbMir& m = dtSbTab[i].m;
                memcpy(c.rsM, m.rsM, sizeof(c.rsM)); memcpy(c.sampM, m.sampM, sizeof(c.sampM)); c.texM = m.texM; c.vbM = m.vbM;
                c.vsM = m.vsM; c.psM = m.psM; c.ibM = m.ibM; c.vpM = m.vpM; c.scM = m.scM; have = true; break;
            }
        ReleaseSRWLockShared(&dtSbLock);
        if (have)
        {
            DtSbFetchCovered(c);
            if (!DtRec()) return false;     // disarmed from inside a fetch's Release
        }
    }
    AcquireSRWLockExclusive(&dtSbLock);
    DtSbEnt* e = nullptr;
    for (int i = 0; i < DT_SB_CAP; i++) if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].sb == sb) { e = &dtSbTab[i]; break; }
    if (!e || e->kind != DT_SBK_REC || !e->clean) { ReleaseSRWLockExclusive(&dtSbLock); return true; }
    DtSbMir& m = e->m;
        // 1. Inside a recording, on any thread, DXVK refuses the Capture when the worker replays it: the record does not follow it.
    if ((dtSbWinDev == dtDev || dtSbRec || dtSbRecOpen) && !DT_SBM_FAULT(12)) { e->valsOk = false; DEVSTAT(dtSbCapInRec++;) }
        // 3. Another thread's call unsettled: the shadow may not hold the device's value. In flight first, then the count (as
    // DtSbWinClose reads them), and a bracketed call that straddled an arm. NC27 skips this.
    else if ((dtForeignInFlight || dtForeign != dtForeignSeen || dtShNeedDrain || dtSbUfIn) && !DT_SBM_FAULT(27)) { e->valsOk = false; DEVSTAT(dtSbCapInvalid++;) }
        // 4. With the drained Apply an unknown entry only makes the values unknown (NC20 copies it as known).
    else if (!DT_SBM_FAULT(20) && !DtSbAllKnown(m)) { e->valsOk = false; DEVSTAT(dtSbCapUnknown++;) }
    else
    {
        // 6. By DXVK's Capture rules: a stream's buffer and stride, never its offset (NC5 takes it); the declaration always,
        // null included; a pixel constant's value only where the shadow follows it and knows it.
        for (int w = 0; w < 8; w++) for (DWORD x = m.rsM[w]; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); m.rs[w * 32 + bit] = dtSh.rs[w * 32 + bit]; }
        for (int i = 0; i < 21; i++) for (DWORD t = 1; t <= 13; t++) if (m.sampM[i] >> t & 1u) m.samp[i][t] = dtSh.samp[i][t];
        for (int i = 0; i < 21; i++) if (m.texM >> i & 1u) m.tex[i] = dtSh.tex[i];
        for (int i = 0; i < 16; i++) if (m.vbM >> i & 1u) { m.vb[i] = dtSh.vb[i]; m.vbStr[i] = dtSh.vbStr[i]; if (DT_SBM_FAULT(5)) m.vbOff[i] = dtSh.vbOff[i]; }
        if (m.vsM) m.vs = dtSh.vs;
        if (m.psM) m.ps = dtSh.ps;
        if (m.ibM) m.ib = dtSh.ib;
        if (m.vpM) m.vp = dtSh.vp;                         // already lifted, and the lift is idempotent on it
        if (m.scM) m.sc = dtSh.sc;
        if (m.declForm != DT_SBD_NONE)
        {
            DWORD f = 0;
            if (dtSh.declOk)
            {
                m.declForm = DT_SBD_OBJ; m.decl = dtSh.decl;
                if (dtSh.fvfOk) { m.fvf = dtSh.fvf; m.fvfOk = true; }
                else if (DtShFvfOf(dtSh.decl, &f)) { m.fvf = f; m.fvfOk = true; }
                else m.fvfOk = false;
            }
            else if (dtSh.declQueued && dtSh.fvfOk) { m.declForm = DT_SBD_FVF; m.decl = nullptr; m.fvf = dtSh.fvf; m.fvfOk = true; }
            else { m.declForm = DT_SBD_UNKNOWN; m.decl = nullptr; m.fvfOk = false; }
        }
        for (int w = 0; w < 7; w++)
        {
            const DWORD known = dtShConsts ? (m.pscM[w] & dtSh.pscOk[w]) : 0u;
            for (DWORD x = known; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); memcpy(m.psc[w * 32 + bit], dtSh.psc[w * 32 + bit], 16); }
            m.pscK[w] = known;
        }
        e->valsOk = true; e->caps++; DEVSTAT(dtSbCapCopied++;)
    }
    dtSbCapUfStamp = dtSbUfSeq;                            // after the test of dtSbUfIn above: DtSbCaptureAfter compares
    ReleaseSRWLockExclusive(&dtSbLock);
    return true;
}
// 7. After the command is in the ring: another thread's Set that read the producer's place before this commit may land before
// the Capture's replay; it raised the foreign count first, and this fence pairs with that raise (Dekker). Likewise a
// bracketed call that began or ended since the copy looked. NC28 skips this check.
static void DtSbCaptureAfter(const void* sb)
{
    _mm_mfence();
    if (DT_SBM_FAULT(28)) return;
    if (dtForeignInFlight || dtForeign != dtForeignSeen || dtSbUfIn || dtSbUfSeq != dtSbCapUfStamp) DtSbInvalidate(sb);
}
// The writer: the record into the shadow through the writers a live Set uses (the MaxZ lift, the null stream's zeroing, the
// null declaration's skip). The recorded Apply and the harness's oracle call it.
static void DtSbMirWrite(const DtSbMir& m)
{
    if (m.vpM) { if (DT_SBM_FAULT(7)) { dtSh.vp = m.vp; dtSh.vpOk = true; } else DtShViewport(&m.vp); }       // NC7: no lift
    for (UINT i = 0; i < 16; i++)
    {
        if (!(m.vbM >> i & 1u)) continue;
        if (DT_SBM_FAULT(6)) { dtSh.vb[i] = m.vb[i]; dtSh.vbOff[i] = m.vbOff[i]; dtSh.vbStr[i] = m.vbStr[i]; dtSh.vbOk[i] = true; }  // NC6
        else DtShStream(i, m.vb[i], m.vbOff[i], m.vbStr[i]);
    }
    for (int w = 0; w < 8; w++) for (DWORD x = m.rsM[w]; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); dtSh.rs[w * 32 + bit] = m.rs[w * 32 + bit]; dtSh.rsOk[w * 32 + bit] = true; }
    for (int i = 0; i < 21; i++) for (DWORD t = 1; t <= 13; t++) if (m.sampM[i] >> t & 1u) { dtSh.samp[i][t] = m.samp[i][t]; dtSh.sampOk[i][t] = true; }
    for (int i = 0; i < 21; i++) if (m.texM >> i & 1u) { dtSh.tex[i] = m.tex[i]; dtSh.texOk[i] = true; }
    if (m.vsM) { dtSh.vs = m.vs; dtSh.vsOk = true; }
    if (m.psM) { dtSh.ps = m.ps; dtSh.psOk = true; }
    if (m.ibM) { dtSh.ib = m.ib; dtSh.ibOk = true; }
    if (m.scM) { dtSh.sc = m.sc; dtSh.scOk = true; }
    switch (m.declForm)
    {
    case DT_SBD_OBJ:
        if (!m.decl && !DT_SBM_FAULT(4)) break;                                   // DXVK never applies a null one (NC4 does)
        DtShDecl(m.decl);
        if (m.fvfOk) { dtSh.fvf = m.fvf; dtSh.fvfOk = true; }
        break;
    case DT_SBD_FVF:     DtShFvf(m.fvf); break;
    case DT_SBD_UNKNOWN: dtSh.declOk = dtSh.declQueued = dtSh.fvfOk = false; break;   // its Get drains, which is exact
    default: break;
    }
    if (dtShConsts)
        for (int w = 0; w < 7; w++)
        {
            for (DWORD x = m.pscM[w] & m.pscK[w]; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); DtShPscFollow(w * 32 + bit, m.psc[w * 32 + bit], 1); }
            dtSh.pscOk[w] &= ~(m.pscM[w] & ~m.pscK[w]);                         // covered and unknown: its Get drains
        }
}
// The producer's Apply: which path it takes, counted by reason. With the recorded Apply on (DT_SB_REC_ON()) and every
// condition met, the block's record is written into the shadow through the writers a live Set uses, under the same hold of the
// lock that found it clean, then the Apply is recorded into the ring like a Capture, with the fence that pairs with another
// thread's raise of the foreign count. Nothing forgotten, fetched or waited for. True: the caller answers D3D_OK (DXVK refuses
// only while a block records, which the test rules out). False: the drained Apply.
// NC1 runs the Apply beside the ring; NC2 leaves the shadow as it was; NC3 forgets the covered entries.
static void DtSbForgetCovered(const DtSbMir& m)
{
    for (int w = 0; w < 8; w++) for (DWORD x = m.rsM[w]; x; x &= x - 1) { DWORD bit; _BitScanForward(&bit, x); dtSh.rsOk[w * 32 + bit] = false; }
    for (int i = 0; i < 21; i++) for (DWORD t = 1; t <= 13; t++) if (m.sampM[i] >> t & 1u) dtSh.sampOk[i][t] = false;
    for (int i = 0; i < 21; i++) if (m.texM >> i & 1u) dtSh.texOk[i] = false;
    for (int i = 0; i < 16; i++) if (m.vbM >> i & 1u) dtSh.vbOk[i] = false;
    if (m.vsM) dtSh.vsOk = false;
    if (m.psM) dtSh.psOk = false;
    if (m.ibM) dtSh.ibOk = false;
    if (m.vpM) dtSh.vpOk = false;
    if (m.scM) dtSh.scOk = false;
}
static bool DtSbApplyTry(void* sb)
{
    LONG why;
    bool recorded = false;
    if (!dtSbProbeOk) why = DT_SBF_PROBE;                                                                  // the probe's verdict on this DXVK
    else if ((dtSbWinDev == dtDev || dtSbRec || dtSbRecOpen) && !DT_SBM_FAULT(11)) why = DT_SBF_WINDOW;    // NC11 ignores the recording
    else
    {
        AcquireSRWLockShared(&dtSbLock);
        const DtSbEnt* e = nullptr;
        for (int i = 0; i < DT_SB_CAP; i++) if (dtSbTab[i].kind != DT_SBK_FREE && dtSbTab[i].sb == sb) { e = &dtSbTab[i]; break; }
        why = !e ? DT_SBF_NOENTRY : e->dev != dtDev ? DT_SBF_DEV : e->kind == DT_SBK_TYPED ? DT_SBF_TYPED
            : !e->clean ? DT_SBF_TAINTED : !e->valsOk ? DT_SBF_VALS : DT_SBF_OK;
        if (why == DT_SBF_OK && (dtForeignInFlight || dtForeign != dtForeignSeen || dtShNeedDrain || dtSbUfIn)) why = DT_SBF_FOREIGN;
        if (why == DT_SBF_OK && DT_SB_REC_ON())
        {
            if (DT_SBM_FAULT(3)) DtSbForgetCovered(e->m);
            else if (!DT_SBM_FAULT(2)) DtSbMirWrite(e->m);
            recorded = true;
        }
        ReleaseSRWLockShared(&dtSbLock);
    }
    DEVSTAT(dtSbApplyWhy[why]++;)
    DEVSTAT(dtSbLastWhy = why;)
    if (!recorded) return false;
    if (DT_SBM_FAULT(1)) ((DtF0)dtSbApplyO)(sb);                                                           // beside the ring
    else { DtCmdX(DT_OP_SBAPPLY, 3, dtSbApplyO, sb); DtCommit(3); }
    _mm_mfence();
    DEVSTAT(dtSbRecApplies++;)
    return true;
}
// Another thread's Capture, or one made unarmed or on another device, makes the block's values unknown to the record: the
// first inside the foreign scope, before its Landed takes the call out of flight (NC16 and NC19 keep the record).
#define DT_SB_CAP_FOREIGN(sb) { const HRESULT h_ = ((DtF0)dtSbCaptureO)(sb); if (!DT_SBM_FAULT(16)) DtSbInvalidate(sb); DEVSTAT(_InterlockedIncrement(&dtSbCapForeign);) return h_; }
#define DT_SB_CAP_DIRECT(sb)  { const HRESULT h_ = ((DtF0)dtSbCaptureO)(sb); if (!DT_SBM_FAULT(19)) DtSbInvalidate(sb); DEVSTAT(_InterlockedIncrement(&dtSbCapNotOurs);) return h_; }
#define DT_SB_CAP_COPY(sb)    if (!DtSbCaptureCopy(sb)) DT_SB_CAP_DIRECT(sb)
#define DT_SB_CAP_AFTER(sb)   DtSbCaptureAfter(sb);
#ifndef DT_SB_CAP_BETWEEN
#define DT_SB_CAP_BETWEEN()                  // the harness starts another thread's call here (dt_test.cpp, the Capture's Dekker case)
#endif
static HRESULT __stdcall dtSbCapture(void* sb)
{
    if (DtForeignThread() && DtOnDev<DTC_SB>(sb)) { DtForeignScope f_("a state block's Capture", false); DT_SB_CAP_FOREIGN(sb) }
    if (!DtRec() || !DtOnDev<DTC_SB>(sb)) DT_SB_CAP_DIRECT(sb)          // another device's: at once
    DT_SB_CAP_COPY(sb)                       // the record takes the shadow's values
    DT_SB_CAP_BETWEEN()
    DtCmdX(DT_OP_XCALL0, 3, dtSbCaptureO, sb);
    DtCommit(3);
    DT_SB_CAP_AFTER(sb)
    return D3D_OK;
}
// The drained Apply changes the device behind the shadow's back, so it drains, runs here, and marks every entry of the copy
// unknown, each fetched from DXVK when next asked; the drain makes that fetch exact.
// Development builds count the tracker's premise (devthread/dt_trk.h): an Apply marks the five states the tracker copies as
// possibly restored, the game's own Sets of them clear their bits (hkSetVsConstF, hkSetStreamSource, hkSetVertexDecl,
// hkSetVertexShader, hkSetIndices), and a scene draw with a bit still set is counted in hkDrawIndexed. Render thread only.
DEVSTAT(static unsigned trkApplyDirty = 0;)            // 1 world (c0..c3), 2 stream 0, 4 declaration, 8 vertex shader, 16 index buffer
DEVSTAT(static unsigned trkApplyDirtySeen = 0;)        // every bit a counted draw found set
DEVSTAT(static LONG trkApplies = 0, trkSceneAfterApply = 0;)
static HRESULT __stdcall dtSbApply(void* sb)
{
        // Another device's block is not the mod's to drain or forget for, by the rule at the top of this file (an overlay's device
    // applying its own block).
    if (!DtOnDev<DTC_SB>(sb)) return ((DtF0)dtSbApplyO)(sb);
    DEVSTAT(trkApplyDirty = 31u; trkApplies++;)
    HRESULT hr;
    if (DtRec()) { DT_SB_APPLY(sb) DtSyncScope s_(DT_R_SBAPPLY); hr = ((DtF0)dtSbApplyO)(sb); }
    else { DtForeignScope f_("a state block's Apply", false); hr = ((DtF0)dtSbApplyO)(sb); }
        // Another thread's Apply changes the device just the same, so it counts as a foreign Set: the next Get settles first.
    const DWORD t = __readfsdword(0x24);
    if (dtArmed) { if (t == dtProducer) DtShDirtyAll(true); else if (t != dtWorkerTid) _InterlockedIncrement(&dtForeign); }
    return hr;
}

// ---- the swapchain's Present: the frame boundary ---------------------------------------
static void* dtScPresentO = nullptr;
static LONG  dtPresRecN = 0;                         // presents recorded, producer-only
static LONG  dtForeignPresents = 0;                  // dtDev's presents from another thread while armed: reported
static void DtMaybeArm(IDirect3DSwapChain9* sc, HRESULT hr);   // dt_install.h
static void DtFrameEnd();                            // dt_install.h
typedef HRESULT(__stdcall* dtT_ScPresent)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
static HRESULT __stdcall dtScPresent(IDirect3DSwapChain9* sc, const RECT* s, const RECT* t, HWND w, const RGNDATA* r, DWORD f)
{
    if (!DtRec() || !DtOnDev<DTC_SC>(sc))
    {
        DTBBS(const int bbs_ = DtBbsPresentBeside(sc);)   // the save's question (dt_bbs.h): a Present of the game's swap chain beside the replay
        const HRESULT hr = ((dtT_ScPresent)dtScPresentO)(sc, s, t, w, r, f);
        DTBBS(DtBbsLeave(bbs_);)
        if (!dtArmed && dtCfg >= 1) DtMaybeArm(sc, hr);
        else if (DtForeignThread()) _InterlockedIncrement(&dtForeignPresents);
        return hr;
    }
    if (dtFrN) DtTakeForeignReleases();
        // Drained and direct: a dirty region, which this game never passes. DONOTWAIT is recorded: DXVK 3.1 never reads
    // D3DPRESENT_DONOTWAIT, so its Present cannot answer D3DERR_WASSTILLDRAWING, and GTA IV passes the flag on every Present.
    if (r)
    {
        HRESULT hr;
        DTBBS(const int bbs_ = DtBbsPresentBeside(sc);)   // the save's question (dt_bbs.h): a Present with a dirty region, drained and direct
        { DtSyncScope s_(DT_R_SCOTHER); hr = ((dtT_ScPresent)dtScPresentO)(sc, s, t, w, r, f); }
        DTBBS(DtBbsLeave(bbs_);)
        DtFrameEnd();
        return hr;
    }
    const DWORD n = 8 + (s ? 4 : 0) + (t ? 4 : 0);
    DWORD* p = DtCmdX(DT_OP_PRESENT, n, dtScPresentO, sc);   // its observer is dtPresentObs, not the slot table
    DWORD at = 8;
    p[3] = DtOpt(p, at, s, sizeof(RECT)); p[4] = DtOpt(p, at, t, sizeof(RECT));
    p[5] = (DWORD)(UINT_PTR)w; p[6] = 0; p[7] = f;
    if (latPacerOn) latPresFrame[(dtPresRecN + 1) & (LAT_RING - 1)] = frames - 1;   // the frame this Present ends, for the worker's end-of-frame signal
    DtCommit(n);
    const LONG mine = ++dtPresRecN;
    DtPublishFenced();
        // One frame ahead at most: this frame's Present may still be queued; the one before it must have run. That bounds the added
    // latency to a frame.
    DtWait([mine] { return (LONG)(dtPresDone - (mine - 1)) >= 0; }, DT_R_PACE, DT_WAKE_KIND());   // true only at a Present: kind 2 (dt_core.h)
    DtFrameEnd();
    if (latGuard) RgOnPresent();   // the refresh guard (DLSS-IV.cpp, after DllMain), after the pacing wait and before the stamp
    if (latOn) LatOnPresent((LONG)(dtPresRecN - dtPresDone), 0);   // the latency instrument's stamp: the CPU start of the next frame (transport/ivt_latency.h)
        // A Present that failed on the worker is answered here, by this one if it has already run, else by the next: a lost device is
    // told a frame late, not never.
    const LONG last = _InterlockedExchange(&dtPresHr, 0);
    return FAILED((HRESULT)last) ? (HRESULT)last : D3D_OK;
}
