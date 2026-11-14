// The device thread: whether the back buffer was written since the mark. Only in a build whose product can arm: everything is
// inside #if DT_BBS_ON (dt_core.h), and every call from the layer is DTBBS(...) or DT_BBS_BESIDE(...), nothing at 0.
// The question (source/fg/fg_machine.h, B::BackMark and B::BackWritten): frame generation writes a picture into the game's back
// buffer and presents it, twice between two game presents. The back buffer needs saving first only if something wrote a back
// buffer since the pair was armed. A wrong "not written" leaves a generated frame's pixels in a real frame; a wrong "written"
// costs two copies. So every path not followed, state not seen since the layer armed, and race not ordered answers "written".
// A back buffer is known by the table below: DXVK makes back buffer surfaces only at creation and Reset (which
// disarms the layer first), and a Present swaps the images inside them. A write into any of them counts: Present rotates.
// The worker replays the stream, runs the injection body, marks and fires; its own writes are in order with both, no lock:
//   a replayed draw or Clear(TARGET) with a back buffer bound at any index; a replayed StretchRect, ColorFill or UpdateSurface
//   into one (a failed call counts too); the same calls made directly on the worker; a replayed Present, ahead of its call;
//   the binding, followed at every SetRenderTarget and read from DXVK at every mark (four GetRenderTarget calls). A
//   difference that could hide a write is counted (rtMissed) and must read 0.
// Beside the worker (the render thread in a drained scope or a passed call, another thread behind the ring) a call is counted
// before it is made (`in`) and after DXVK returns (`out`), both interlocked:
//   at the mark, `out` then `in`: unequal means a call in flight, and the pair answers "written" for its whole life;
//   at a fire, `in` against its value at the mark: moved means a call has begun. The count is taken ahead of the wait every
//   such call makes for the worker, so a call the fire does not see has not begun.
//   What counts: a draw or Clear, StretchRect, ColorFill or UpdateSurface into a back buffer, a bind of a back buffer, a
//   non-read-only LockRect, an Unlock, GetDC and ReleaseDC on a back buffer (DXVK uploads at unlock), DrawRectPatch and
//   DrawTriPatch, a non-replayed Present of the game's swap chain, and the mod's own raw write (the transport's home copy).
// Not seen, and what it answers:
//   - Frame generation's own fires: not counted on purpose; the question is whether the game's picture is there.
//   - DXVK's own calls on its surfaces: its blit reads the back buffer; its GDI fallback (PresentImageGDI) runs for a Present
//     with a destination rectangle on a non-rotating swap chain, so a pair whose Present carries a rectangle answers "written"
//     (DtBbsWritten's `rect`), and after a Vulkan present has thrown, where it only uploads the fire's own picture.
//   - Ex device slots: the game asks for no Ex device.
//   - A back buffer not in the table, or a swap chain with more back buffers than the table holds, answers "written".
//   - Before the first mark after an arm, and for a mark taken before the layer armed again: "written".
// Cost: a replayed call pays a byte, a compare and a branch; a draw or Clear only while a back buffer is bound and nothing has
// written since the mark; a mark, four GetRenderTarget calls; a fire, a handful of loads.
// tools/perfproof/dt_fg_saves.h proves each kind of write forces the save, and that each site switched off (DT_BBS_BLIND) loses
// the game's picture.
#pragma once
#ifndef DEVSTAT
#define DEVSTAT(...) __VA_ARGS__
#endif
#if DT_BBS_ON

// A site switched off, for the harness's controls only; in the mod the constant false.
#ifndef DT_BBS_BLIND
#define DT_BBS_BLIND(what) false
#endif
enum { DT_BBS_B_FLIGHT = 100,                // the mark's test for a call in flight
       DT_BBS_B_BESIDE = 101,                // the fire's test of the count beside the worker
       DT_BBS_B_REREAD = 102,                // the mark reads no binding from DXVK: the bookkeeping alone
       DT_BBS_B_ALL    = 103,                // the lying build: "not written", always
       DT_BBS_B_OBS    = 104 };              // the observers' look: no write the checkpoints and dtObs[30] carry reaches the tracker

// ---- the state (its type and itself here, at the end of the translation unit) ----
enum { DT_BBS_MAX = 8 };
// why a fire was answered "written": 0 it was not; 1 to DT_BBS_KINDS the worker's own first write since the mark, by its
// kind + 1; then the answers that are not a write the worker made
enum { DT_BBS_W_BESIDE = DT_BBS_KINDS + 1,   // a call beside the worker has begun since the mark
       DT_BBS_W_NOMARK,                      // no mark stands (the layer is not armed, armed again since, or never marked)
       DT_BBS_W_FLIGHT,                      // a call beside the worker was in flight at the mark
       DT_BBS_W_TABLE,                       // the table does not hold every back buffer, or not the one the fire writes, or DXVK would not say what is bound
       DT_BBS_W_RECT,                        // the pair's Present carries a rectangle
       DT_BBS_W_N };
struct DtBbs
{
    unsigned char slot[128];                 // by device slot: this replayed call is looked at (DtExec's one test)
    // written at an arm of the layer, the ring empty and the worker idle; read by any thread while it is armed
    void*    sc;                             // the swap chain whose Present armed the layer: the game's
    void*    bb[DT_BBS_MAX];                 // its back buffer surfaces, as DXVK hands them out (fixed between Resets)
    LONG     nBb, more;                      // how many; and the swap chain has more than the table holds
    LONG     arm;                            // the layer's arm this table is of (dtArmCount)
        // The worker's own
    LONG     rt;                             // the render targets that are a back buffer, one bit an index
    LONG     dirty;                          // 0, or the kind + 1 of the worker's first write since the mark
    LONG     marked, markArm, markIn, markDoubt;   // a mark stands; the arm it was taken in; `in` as it stood; DT_BBS_W_* if the pair answers "written" whatever follows
    // the counts (the worker's; the product's line reads them from another thread, a few words late at worst)
    LONG     marks;
    LONG     rtMissed, rtStale;              // at a mark: DXVK had a back buffer bound the bookkeeping did not (unexplained); the bookkeeping had one DXVK did not
    LONG     found[2][DT_BBS_W_N];           // by fire (0 the generated frame's, 1 the real frame's) and by answer
    LONG     foundBeside[DT_BBS_KINDS];      // of the answers "a call beside the worker": by the kind of the first one since the mark
        // The worker's: the replayed SetRenderTarget just made was refused by DXVK; its observer (which has no HRESULT) then leaves
    // the bookkeeping as it was.
    LONG     fail37;
        // Beside the worker, on a line of its own: any thread, interlocked
    __declspec(align(64)) volatile LONG in;  // calls begun
    volatile LONG out;                       // calls DXVK has returned from
    volatile LONG besideKind;                // the kind + 1 of the first one since the mark (0 none)
    volatile LONG forGood;                   // a raw write was made on a thread that is neither the worker nor the render thread: never out of flight again
};
#pragma section(".data$zfg", read, write)
__declspec(allocate(".data$zfg")) static DtBbs dtBbs;
static __forceinline bool DtBbsLooks(DWORD slot) { return dtBbs.slot[slot] != 0; }   // DtExec's one test, folded into its loop

// ---- is it a back buffer ----
static __forceinline int DtBbsIndex(const void* s)
{
    const DtBbs& b = dtBbs;
    for (LONG i = 0; i < b.nBb; i++) if (b.bb[i] == s) return (int)i;
    return -1;
}

// ---- the worker's own ----
// The five slots that draw or clear are looked at only while a back buffer is bound and nothing has written since the mark.
// Only the worker writes these bytes (and the arm, with the worker idle): a byte another thread could clear is a draw that
// could go unseen.
static void DtBbsHot()
{
    DtBbs& b = dtBbs;
    const unsigned char on = (b.rt != 0 && !b.dirty) ? 1 : 0;
    b.slot[81] = b.slot[82] = b.slot[83] = b.slot[84] = b.slot[43] = on;
}
static void DtBbsFirst(int kind)
{
    DtBbs& b = dtBbs;
    if (b.dirty || DT_BBS_BLIND(kind)) return;
    b.dirty = kind + 1;
    DtBbsHot();
}
static void DtBbsRt(DWORD i, const void* s)
{
    if (i >= 4) return;
    DtBbs& b = dtBbs;
    const LONG bit = 1L << i;
    b.rt = (s && DtBbsIndex(s) >= 0) ? (b.rt | bit) : (b.rt & ~bit);
    DtBbsHot();
}
// A replayed device call whose byte is set (DtExec), after DXVK returned.
static __declspec(noinline) void DtBbsSeen(DWORD slot, const DWORD* c, HRESULT hr)
{
    switch (slot)
    {
    case 37: if (SUCCEEDED(hr)) DtBbsRt(c[1], (const void*)(UINT_PTR)c[2]); return;   // a refused bind changed nothing: DXVK decides it from the arguments alone
    case 81: case 82: case 83: case 84: if (dtBbs.rt) DtBbsFirst(DT_BBS_DRAW); return;
    case 43: if (dtBbs.rt && (c[3] & D3DCLEAR_TARGET)) DtBbsFirst(DT_BBS_CLEAR); return;
    case 34: if (DtBbsIndex((const void*)(UINT_PTR)c[3]) >= 0) DtBbsFirst(DT_BBS_STRETCH); return;
    case 35: if (DtBbsIndex((const void*)(UINT_PTR)c[1]) >= 0) DtBbsFirst(DT_BBS_FILL); return;
    case 30: if (DtBbsIndex((const void*)(UINT_PTR)c[3]) >= 0) DtBbsFirst(DT_BBS_UPDATE); return;
    default: return;
    }
}
// A replayed Present, ahead of its call. Where that Present arms a pair, the mark inside the call clears this; otherwise (an
// inline pair, a present passed through, a stray one) it stands.
static __declspec(noinline) void DtBbsPresentAhead() { DtBbsFirst(DT_BBS_PRESENT); }

// ---- the calls the tracker follows, by the device thread's observers ----
// DtExec does not test this file's state on every replayed call (about 45,000 a frame); the calls it needs reach the tracker
// through the observers DtExec calls anyway (`if (dtObs[slot])`), which cost nothing on any other call:
//   SetRenderTarget, always: dtObs[37] is DtBbsObs37 from the layer's arm on, and while a pair is armed the machine's
//   checkpoint on slot 37 calls the same line ahead of its own work (DtBbsAt);
//   the draws, Clear, StretchRect, ColorFill: while a pair is armed, the machine's checkpoint observers call DtBbsAt ahead of
//   their own work (a fire from this call's observer still comes after the call's write);
//   UpdateSurface: while a pair is armed, dtObs[30] is DtBbsObs30.
// Outside an armed pair a write needs no look: the next mark clears `dirty`, and only a write between a mark and a fire counts.
// The checkpoints go in at the arm, behind the mark, and come out behind the pair's last fire.
// A refused SetRenderTarget: the observer has no HRESULT, so DtExec's failed branch says so first (DtBbsBindFailed).
static void DtBbsObs37(const DWORD* c)
{
    DtBbs& b = dtBbs;
    if (b.fail37) { b.fail37 = 0; return; }
    DtBbsRt(c[1], (const void*)(UINT_PTR)c[2]);
}
static void DtBbsObs30(const DWORD* c) { if (!DT_BBS_BLIND(DT_BBS_B_OBS)) DtBbsSeen(30, c, S_OK); }
template <DWORD SL> static __forceinline void DtBbsAt(const DWORD* c)
{
    if (SL == 37) { DtBbsObs37(c); return; }
    if (DtBbsLooks(SL) && !DT_BBS_BLIND(DT_BBS_B_OBS)) DtBbsSeen(SL, c, S_OK);
}
static void DtBbsBindFailed() { dtBbs.fail37 = 1; }

// ---- beside the worker ----
// Ahead of a call that reaches DXVK beside the replay. dst: the surface it writes or binds; null for a draw or a Clear of
// whatever is bound; DT_BBS_ANY where the layer cannot tell what will be bound. d: the device, or null for a surface's own call.
// On the worker it is the worker's own write, in order: returns 0 (a bind there is followed by DtBbsRtMoved). On any other
// thread: counted in, and the token says count it out.
static int DtBbsEnter(const void* d, int kind, const void* dst)
{
    if (!dtArmed || (d && d != dtDev)) return 0;
    DtBbs& b = dtBbs;
    const DWORD t = __readfsdword(0x24);
    const bool worker = t == dtWorkerTid;
    if (dst == DT_BBS_ANY) {}
    else if (dst) { if (DtBbsIndex(dst) < 0) return 0; }
    else if (b.rt == 0 && (worker || t == dtProducer)) return 0;   // the render thread's draw in a drained scope meets the known binding; another thread's counts whatever is bound
    if (worker) { if (kind != DT_BBS_BIND) DtBbsFirst(kind); return 0; }
    if (DT_BBS_BLIND(kind)) return 0;
    _InterlockedIncrement(&b.in);
    _InterlockedCompareExchange(&b.besideKind, kind + 1, 0);
    return 1;
}
static void DtBbsLeave(int entered) { if (entered) _InterlockedIncrement(&dtBbs.out); }
// A Present of a swap chain beside the replay (dt_res.h, dtScPresent's two direct paths), ahead of it: the game's own
// swap chain alone (another swap chain's back buffers are not the ones a fire writes).
static int DtBbsPresentBeside(void* sc)
{
    return (dtArmed && sc == dtBbs.sc) ? DtBbsEnter(nullptr, DT_BBS_PRESENT, DT_BBS_ANY) : 0;
}
// A SetRenderTarget that reached DXVK beside the replay and was taken: the worker's own is followed here; another thread's was
// counted ahead of its call if it bound a back buffer, and the next mark reads the binding from DXVK.
static void DtBbsRtMoved(const void* d, DWORD i, const void* s)
{
    if (dtArmed && d == dtDev && __readfsdword(0x24) == dtWorkerTid) DtBbsRt(i, s);
}
// The mod's own raw write into the back buffer (IvtHome's home copy), ahead of it. Inside DT_DIRECT on the render thread the
// ring is drained and the worker idle, so it is counted in and out at once; on the worker it is the worker's own. On any other
// thread (no path of the mod) it is counted in and never out, and every later pair answers "written".
static void DtBbsRawWrite()
{
    const int e = DtBbsEnter(nullptr, DT_BBS_RAW, DT_BBS_ANY);
    if (e && __readfsdword(0x24) != dtProducer) { _InterlockedExchange(&dtBbs.forGood, 1); return; }
    DtBbsLeave(e);
}

// ---- the arm, the mark, the question ----
// The layer's arm (DtMaybeArm, before dtArmed goes up, ring empty, worker idle): the table from DXVK, the slots always looked
// at, and no mark, so every question answers "written" until a pair is armed. The first mark reads the binding from DXVK.
static void DtBbsAtArm(IDirect3DSwapChain9* sc)
{
    DtBbs& b = dtBbs;
    memset(b.slot, 0, sizeof(b.slot));
    b.sc = sc; b.nBb = 0; b.more = 0;
    D3DPRESENT_PARAMETERS pp = {}; UINT n = 1;
    if (SUCCEEDED(sc->GetPresentParameters(&pp)) && pp.BackBufferCount > 1) n = pp.BackBufferCount;
    typedef HRESULT(__stdcall* Back_t)(void*, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**);
    UINT got = 0;
    for (UINT i = 0; i < n; i++)
    {
        IDirect3DSurface9* s = nullptr;
        if (FAILED(((Back_t)dtScBackO)(sc, i, D3DBACKBUFFER_TYPE_MONO, &s)) || !s) break;
        got++;
        if (b.nBb < DT_BBS_MAX) b.bb[b.nBb++] = s; else b.more = 1;
        ((ULONG(__stdcall*)(void*))dtRel[DTC_SURF])(s);      // DXVK's own Release: the swap chain keeps its surface
    }
    if (got < n) b.more = 1;                                 // DXVK did not hand out every back buffer it says it has: the table is not whole
    b.arm = dtArmCount;
    b.rt = 0; b.dirty = DT_BBS_PRESENT + 1; b.marked = 0; b.markDoubt = DT_BBS_W_NOMARK;
    b.slot[37] = b.slot[34] = b.slot[35] = b.slot[30] = 1;
    b.fail37 = 0;
    DtBbsHot();
        // The binding is followed by its observer from here on; the machine's checkpoint takes the slot while a pair is armed and
    // gives it back when the pair closes. Worker idle, ring empty: a plain store.
    if (!dtObs[37]) dtObs[37] = &DtBbsObs37;
}
// The mark (B::BackMark): on the worker, where a pair is armed. From here a write is the next frame's.
static void DtBbsMark()
{
    DtBbs& b = dtBbs;
    b.marks++;
    const LONG o = b.out, i = b.in;                          // `out` first: equal means no call beside the worker was inside DXVK between the two reads
    LONG doubt = 0;
    if (i != o && !DT_BBS_BLIND(DT_BBS_B_FLIGHT)) doubt = DT_BBS_W_FLIGHT;
    if (b.more) doubt = DT_BBS_W_TABLE;
        // The binding from DXVK itself, at this place in the stream
    if (!DT_BBS_BLIND(DT_BBS_B_REREAD))
    {
        typedef HRESULT(__stdcall* Get_t)(void*, DWORD, IDirect3DSurface9**);
        void* const dev = dtDev;
        LONG m = 0;
        for (DWORD k = 0; k < 4; k++)
        {
            IDirect3DSurface9* s = nullptr;
            const HRESULT hr = ((Get_t)dtDx[38])(dev, k, &s);
            if (SUCCEEDED(hr) && s)
            {
                if (DtBbsIndex(s) >= 0) m |= 1L << k;
                ((ULONG(__stdcall*)(void*))dtRel[DTC_SURF])(s);
            }
            else if (hr != D3DERR_NOTFOUND) doubt = DT_BBS_W_TABLE;   // (DXVK answers NOTFOUND for an index with nothing bound, and nothing else for one of these four)
        }
                // A back buffer DXVK holds bound that the bookkeeping did not, with no call beside the worker to explain it, is counted.
        DEVSTAT(if ((m & ~b.rt) != 0 && b.marked && b.markArm == b.arm && !b.markDoubt && b.in == b.markIn && !b.forGood) b.rtMissed++;)
        DEVSTAT(if ((b.rt & ~m) != 0) b.rtStale++;)
        b.rt = m;
    }
    if (b.in != i && !DT_BBS_BLIND(DT_BBS_B_FLIGHT)) doubt = DT_BBS_W_FLIGHT;   // (one began while the binding was read)
    b.markIn = i; b.markArm = b.arm; b.markDoubt = doubt; b.marked = 1;
    b.dirty = 0;
    _InterlockedExchange(&b.besideKind, 0);
    DtBbsHot();
}
// The question (B::BackWritten): on the worker, at a fire. bb: the surface the fire will write. fire: 0 the generated frame's,
// 1 the real frame's (for the counts). rect: the pair's Present carries a rectangle.
static bool DtBbsWritten(const void* bb, int fire, bool rect)
{
    DtBbs& b = dtBbs;
    int why = 0;
    if (!dtArmed || !b.marked || b.markArm != dtArmCount || b.arm != dtArmCount) why = DT_BBS_W_NOMARK;
    else if (b.markDoubt) why = (int)b.markDoubt;
    else if (rect) why = DT_BBS_W_RECT;
    else if (!bb || DtBbsIndex(bb) < 0) why = DT_BBS_W_TABLE;
    else if (b.dirty) why = (int)b.dirty;
    else if ((b.in != b.markIn || b.forGood) && !DT_BBS_BLIND(DT_BBS_B_BESIDE))
    {
        why = DT_BBS_W_BESIDE;
        const LONG k = b.besideKind;
        DEVSTAT(if (k > 0 && k <= DT_BBS_KINDS) b.foundBeside[k - 1]++;)
    }
    if (DT_BBS_BLIND(DT_BBS_B_ALL)) why = 0;
    b.found[fire ? 1 : 0][why]++;
    return why != 0;
}

#endif   // DT_BBS_ON
