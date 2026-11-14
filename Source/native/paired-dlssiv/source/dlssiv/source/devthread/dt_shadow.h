// The device thread, part 2 of 5: the shadow.
// The game makes about 300 Gets a frame (GetRenderTarget, GetViewport, GetSamplerState, GetTexture, GetDepthStencilSurface,
// GetRenderState) and the mod's velocity pass about 2,000 GetRenderState; a Get that drained the ring would cost the render
// thread whatever the worker had left, so these are answered from this copy.
// The copy is the state DXVK will hold once the ring has drained, the state it would hold now without this layer. Every Set
// the producer makes on dtDev, recorded or direct, writes it after the mod's own hooks have changed the call, so a Get
// returns what DXVK itself would have returned.
// Where it cannot follow, an entry is fetched from DXVK on demand. Every entry carries a known flag. A Set the copy follows
// marks its entry known; a state block's drained Apply or another thread's Set marks every entry unknown, and a Get of an
// unknown entry asks DXVK for that one value directly, without a drain, and keeps it. That is exact by one invariant: an
// entry is unknown only while no queued command names it, since both events that unmark leave the ring empty behind them (the
// Apply is drained; a foreign Set makes DtShadowSettle drain). Loaded whole only at arming and for threaded=2's compare.
// The recorded state-block Apply keeps the invariant: it writes every entry it covers from the block's record (dt_sbtab.h)
// rather than forgetting them. The declaration and the pixel constants, whose Gets always drain when unknown, may be unknown
// with a queued Apply naming them, as declQueued already was. DtShKnowBody fetches neither.
#pragma once

// dtDx[], the device slots as DXVK filled them, is declared in dt_core.h: the worker replays through it.

struct DtShadow
{
    DWORD rs[256];     bool rsOk[256];      // render states; Ok = the copy holds DXVK's value (loaded, set, or fetched)
    DWORD samp[21][14]; bool sampOk[21][14];// samplers 0..15, D3DDMAPSAMPLER, vertex samplers 0..3
    IDirect3DBaseTexture9* tex[21];  bool texOk[21];
    IDirect3DSurface9*     rt[4];    bool rtOk[4];
    IDirect3DSurface9*     ds;       bool dsOk;
    D3DVIEWPORT9           vp;       bool vpOk;
    RECT                   sc;       bool scOk;
    IDirect3DVertexShader9* vs;      bool vsOk;
    IDirect3DPixelShader9*  ps;      bool psOk;
    IDirect3DVertexBuffer9* vb[16]; UINT vbOff[16], vbStr[16]; bool vbOk[16];
    IDirect3DIndexBuffer9*  ib;      bool ibOk;
        // The vertex declaration, the FVF and the pixel float constants (why: at DtShFvfOf).
    IDirect3DVertexDeclaration9* decl; bool declOk;
    bool  declQueued;                // a recorded SetFVF binds a declaration only DXVK knows: unknown and named by a queued command, so its Get drains first
    DWORD fvf;       bool fvfOk;     // the FVF a SetFVF bound, while that is still the binding
    float psc[224][4]; DWORD pscOk[7];  // one bit a register; followed only once somebody has asked (dtShConsts)
};
static DtShadow      dtSh;
static bool          dtShNeedDrain = false; // a recorded bind whose size could not be read: drain before the next Get (DtShRenderTarget)
static volatile LONG dtForeign = 0;         // Sets on dtDev from other threads, ever
static volatile LONG dtForeignInFlight = 0; // another thread's ordered calls between their first instruction and DXVK's return (dt_core.h)
DEVSTAT(static LONG  dtShWaitedOut = 0;)    // settles that found one in flight and waited it out (reported)
static LONG          dtForeignSeen = 0;
DEVSTAT(static LONG  dtShFetched = 0;)      // entries fetched on demand (reported)
DEVSTAT(static LONG  dtShDirtied = 0;)      // times every entry was marked unknown (reported)
// Inside BeginStateBlock..EndStateBlock DXVK records the state-block Sets into the block and leaves the device alone, so the
// copy must not follow them. Not all of them (DXVK 3.1, d3d9_device.cpp): SetRenderTarget, SetDepthStencilSurface and a UP
// draw's unbinding change the device regardless, so those always follow.
static bool          dtSbRec = false;
DEVSTAT(static LONG  dtShReloads = 0;)      // the development report's and the harness's
static LONG          dtValMis = 0, dtValLogged = 0;
DEVSTAT(static LONG  dtValChecks = 0, dtValRaced = 0;)   // threaded=2's counts: the development report's and the harness's
// The whole-copy compare HOLDS what it finds until it knows no other thread's Set raced its read
// (DtShadowCompare says why); the per-Get check reports at once, as it always did.
struct DtHeldMis { const char* what; int idx; DWORD ours, dxvk; };
static DtHeldMis     dtValHeld[8];
static int           dtValHeldN = -1;      // -1 = not holding

// D3D9's sampler numbers, 0..15, D3DDMAPSAMPLER (256), D3DVERTEXTEXTURESAMPLER0..3
// (257..260), onto 0..20; -1 for anything else, which then goes to DXVK as a SYNC.
static __forceinline int DtSampIdx(DWORD s) { return s < 16 ? (int)s : (s >= 256 && s <= 260) ? (int)(s - 240) : -1; }
static __forceinline DWORD DtSampNum(int i) { return i < 16 ? (DWORD)i : (DWORD)(i + 240); }

// Who may write the shadow for this call: the producer, on dtDev, while armed, recording or in a direct scope. A Set from any
// other thread on dtDev cannot be followed, so it marks the copy stale. The worker never calls these wrappers. dtSbRec is
// each caller's own question.
// Call it only once the Set has reached DXVK: the count it raises for a foreign Set lets a settle mark a fetched entry known,
// so raising it before the call could fetch the old value and call it current. (DtForeignBarrier also raises it before the
// call; the raise after still re-fetches an entry fetched while the call was in flight.)
// Unarmed, another thread's Set is still counted: DtShadowLoad reads the count before its Gets at arming, with dtArmed down,
// so a Set racing the load re-settles at the first Get after it. dtDev is set before that load and never cleared.
static __forceinline bool DtShMine(const void* d)
{
    if (d != dtDev) return false;
    const DWORD t = __readfsdword(0x24);
    if (!dtArmed) { if (t != renderTid && t != dtWorkerTid) _InterlockedIncrement(&dtForeign); return false; }
    if (t == dtProducer) return true;
    if (t != dtWorkerTid) _InterlockedIncrement(&dtForeign);
    return false;
}

typedef HRESULT(__stdcall* DtGetRS_t)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD*);
typedef HRESULT(__stdcall* DtGetSS_t)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
typedef HRESULT(__stdcall* DtGetTex_t)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);
typedef HRESULT(__stdcall* DtGetRT_t)(IDirect3DDevice9*, DWORD, IDirect3DSurface9**);
typedef HRESULT(__stdcall* DtGetDS_t)(IDirect3DDevice9*, IDirect3DSurface9**);
typedef HRESULT(__stdcall* DtGetVP_t)(IDirect3DDevice9*, D3DVIEWPORT9*);
typedef HRESULT(__stdcall* DtGetSC_t)(IDirect3DDevice9*, RECT*);
typedef HRESULT(__stdcall* DtGetVS_t)(IDirect3DDevice9*, IDirect3DVertexShader9**);
typedef HRESULT(__stdcall* DtGetPS_t)(IDirect3DDevice9*, IDirect3DPixelShader9**);
typedef HRESULT(__stdcall* DtGetSSrc_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
typedef HRESULT(__stdcall* DtGetIB_t)(IDirect3DDevice9*, IDirect3DIndexBuffer9**);
typedef HRESULT(__stdcall* DtGetDecl_t)(IDirect3DDevice9*, IDirect3DVertexDeclaration9**);
typedef HRESULT(__stdcall* DtGetFVF_t)(IDirect3DDevice9*, DWORD*);
typedef HRESULT(__stdcall* DtGetPSC_t)(IDirect3DDevice9*, UINT, float*, UINT);

// ---- the declaration, the FVF and the pixel constants ----
// These were the only device questions asked every frame that the copy could not answer, so each drained the ring: the
// game's GetVertexDeclaration and GetFVF, and at hdr=1 the mod's GetPixelShaderConstantF. With the injection body on the
// worker the first drain after the seam waits for all of it. Answering from the copy, by the invariant at the top of this
// file, fixes the game's calls and the mod's alike.
// The FVF is not state: in DXVK 3.1 GetFVF is `vertexDecl ? vertexDecl->GetFVF() : 0`, a declaration's FVF is fixed at
// construction, SetFVF(f) is SetVertexDeclaration(the table's declaration for f), and SetFVF(0) does nothing. So:
//   - after a recorded SetFVF(f) the answer is f, and the declaration is one only DXVK knows (declQueued: its Get drains);
//   - otherwise the answer belongs to the bound declaration object, remembered per object once DXVK has given it
//     (dtShFvfC). The first ask for an object drains; the game asks at the same place each frame.
// A remembered object cannot be a different one at the same address: an address is reused only by a later object, the three
// ways one is created (CreateVertexDeclaration, a SetFVF of a value DXVK has not met, ProcessVertices with no declaration)
// all pass through this layer and empty the table (dtDeclGen, from any thread), and a queued object cannot die before its
// command runs.
// The pixel constants are followed only where it pays: once something has asked for one and an injection body has been
// posted to the worker (dtShBodiesEver). Nobody asking: the game uploads them some twenty thousand times a frame and never
// reads one back, so no store per upload. Asks with nothing on the worker: the drain is cheap and stays. Both: the drain
// would wait for the whole body, so the stores pay. The first qualifying ask drains, loads all 224 with the ring empty, and
// turns following on (dtShConsts) for the session. Until then a player pays one load and a settled branch.
static volatile LONG dtDeclGen = 0;             // declarations created, ever: any thread
static LONG          dtShFvfGen = 0;            // ...as of the table below
static struct { IDirect3DVertexDeclaration9* decl; DWORD fvf; } dtShFvfC[4];
static unsigned      dtShFvfNext = 0;
static bool          dtShConsts = false;        // the pixel float constants are being followed
DEVSTAT(static LONG  dtShFvfAsked = 0;)         // FVF asks that had to go to DXVK, drained (reported)

// The generation is read ONCE: a creation on another thread between the compare and the
// store would otherwise be recorded as seen without the table having been emptied for it.
static __forceinline void DtShFvfFresh()
{
    const LONG g = dtDeclGen;
    if (dtShFvfGen != g) { memset(dtShFvfC, 0, sizeof(dtShFvfC)); dtShFvfGen = g; }
}
static bool DtShFvfOf(IDirect3DVertexDeclaration9* decl, DWORD* fvf)
{
    DtShFvfFresh();
    for (int i = 0; i < 4; i++) if (dtShFvfC[i].decl == decl) { *fvf = dtShFvfC[i].fvf; return true; }
    return false;
}
static void DtShFvfKeep(IDirect3DVertexDeclaration9* decl, DWORD fvf)
{
    if (!decl) return;
    DtShFvfFresh();
    for (int i = 0; i < 4; i++) if (dtShFvfC[i].decl == decl) { dtShFvfC[i].fvf = fvf; return; }
    dtShFvfC[dtShFvfNext & 3].decl = decl; dtShFvfC[dtShFvfNext & 3].fvf = fvf; dtShFvfNext++;
}
// The FVF values this thread has sent DXVK, which therefore already have their table
// declaration: DXVK keeps that table for the life of the device and never empties it.
// Producer only. Full means "not seen", which costs an emptied table and nothing else.
static DWORD    dtShFvfSent[16];
static unsigned dtShFvfSentN = 0;
static bool DtShFvfSeen(DWORD fvf)
{
    for (unsigned i = 0; i < dtShFvfSentN; i++) if (dtShFvfSent[i] == fvf) return true;
    if (dtShFvfSentN < 16) dtShFvfSent[dtShFvfSentN++] = fvf;
    return false;
}
// The two writers. A declaration bound by name is known and nothing is queued behind it;
// an FVF binds a declaration only DXVK can name, and its FVF is the one just given.
static __forceinline void DtShDecl(IDirect3DVertexDeclaration9* decl)
{
    dtSh.decl = decl; dtSh.declOk = true; dtSh.declQueued = false; dtSh.fvfOk = false;
}
static IDirect3DVertexDeclaration9* DtSbFvfObjOf(DWORD f);   // dt_sbtab.h: the FVF map's object for f, with the recorded Apply only
static __forceinline void DtShFvf(DWORD fvf)
{
        // DXVK binds its table's object for f and never drops it, so once a drained ask has named it the copy can too, and
    // GetVertexDeclaration stops draining behind a recorded SetFVF.
    if (IDirect3DVertexDeclaration9* const o = DtSbFvfObjOf(fvf)) { dtSh.decl = o; dtSh.declOk = true; dtSh.declQueued = false; dtSh.fvf = fvf; dtSh.fvfOk = true; return; }
    dtSh.declOk = false; dtSh.declQueued = true; dtSh.fvf = fvf; dtSh.fvfOk = true;
}
static __forceinline bool DtShPscKnown(UINT start, UINT count)
{
    for (UINT r = start; r < start + count; r++) if (!(dtSh.pscOk[r >> 5] & (1u << (r & 31)))) return false;
    return true;
}
// Mark registers [start, start + count) known a word at a time: one bit a register made each wait on the previous store.
// At most eight words, each one OR; dt_test.cpp proves the bits equal for every range, with a control.
static __forceinline void DtShPscMark(UINT start, UINT count)
{
    const UINT end = start + count;
    for (UINT r = start; r < end; )
    {
        const UINT lo = r & 31, n = (end - r < 32u - lo) ? end - r : 32u - lo;
        dtSh.pscOk[r >> 5] |= (n == 32u ? 0xFFFFFFFFu : (1u << n) - 1u) << lo;
        r += n;
    }
}
// One register is the commonest upload by far (dt_device_rec.h, DtCopyPay), so it gets the
// same branch of its own here: one store and one OR.
static __forceinline void DtShPscFollow(UINT start, const float* data, UINT count)
{
    if (count == 1u)
    {
        _mm_storeu_ps(dtSh.psc[start], _mm_loadu_ps(data));
        dtSh.pscOk[start >> 5] |= 1u << (start & 31);
        return;
    }
    memcpy(dtSh.psc[start], data, count * 16);
    DtShPscMark(start, count);
}

// A Get that AddRefs hands back a reference; the copy keeps a raw pointer, the
// way DXVK's own binding does, so the reference goes straight back. This runs
// with the ring drained and the producer direct, so the Release is immediate.
template <class T> static T* DtRaw(T* p) { if (p) p->Release(); return p; }

// The declaration and the pixel constants, read whole (two DXVK calls, one a 3.5 KB copy), only with the ring drained and
// nothing recorded since, so what DXVK holds is what the stream holds. That is why these are reloaded where the others are
// marked unknown: the game asks for its declaration right behind the seam, where the injection body may be running on the
// worker with the mod's declaration bound, and a direct fetch there would hand the game the mod's. These are never fetched
// beside anything: known, or drained for.
static void DtShReadDeclConsts(IDirect3DDevice9* d, DtShadow* o, bool consts)
{
    IDirect3DVertexDeclaration9* x = nullptr;
    o->decl = nullptr;
    if ((o->declOk = SUCCEEDED(((DtGetDecl_t)dtDx[88])(d, &x)))) o->decl = DtRaw(x);
    o->declQueued = false; o->fvfOk = false;
    if (consts && SUCCEEDED(((DtGetPSC_t)dtDx[110])(d, 0, o->psc[0], 224))) memset(o->pscOk, 0xFF, sizeof(o->pscOk));
    else memset(o->pscOk, 0, sizeof(o->pscOk));
}

// Reads the whole shadowed state out of DXVK. into == &dtSh at load; a scratch
// copy for the validation compare.
static void DtShadowRead(IDirect3DDevice9* d, DtShadow* o)
{
    memset(o, 0, sizeof(*o));
    for (DWORD s = 7; s <= 209; s++)
        o->rsOk[s] = SUCCEEDED(((DtGetRS_t)dtDx[58])(d, (D3DRENDERSTATETYPE)s, &o->rs[s]));
    for (int i = 0; i < 21; i++)
    {
        for (DWORD t = 1; t <= 13; t++)
            o->sampOk[i][t] = SUCCEEDED(((DtGetSS_t)dtDx[68])(d, DtSampNum(i), (D3DSAMPLERSTATETYPE)t, &o->samp[i][t]));
        IDirect3DBaseTexture9* x = nullptr;
        if ((o->texOk[i] = SUCCEEDED(((DtGetTex_t)dtDx[64])(d, DtSampNum(i), &x)))) o->tex[i] = DtRaw(x);
    }
    for (DWORD i = 0; i < 4; i++)
    {
        // An empty slot is D3DERR_NOTFOUND with a null: known, and none.
        IDirect3DSurface9* x = nullptr;
        const HRESULT hr = ((DtGetRT_t)dtDx[38])(d, i, &x);
        o->rtOk[i] = SUCCEEDED(hr) || hr == D3DERR_NOTFOUND;
        if (SUCCEEDED(hr)) o->rt[i] = DtRaw(x);
    }
    {
        IDirect3DSurface9* x = nullptr;
        const HRESULT hr = ((DtGetDS_t)dtDx[40])(d, &x);
        o->dsOk = SUCCEEDED(hr) || hr == D3DERR_NOTFOUND;
        if (SUCCEEDED(hr)) o->ds = DtRaw(x);
    }
    o->vpOk = SUCCEEDED(((DtGetVP_t)dtDx[48])(d, &o->vp));
    o->scOk = SUCCEEDED(((DtGetSC_t)dtDx[76])(d, &o->sc));
    { IDirect3DVertexShader9* x = nullptr; if ((o->vsOk = SUCCEEDED(((DtGetVS_t)dtDx[93])(d, &x)))) o->vs = DtRaw(x); }
    { IDirect3DPixelShader9*  x = nullptr; if ((o->psOk = SUCCEEDED(((DtGetPS_t)dtDx[108])(d, &x)))) o->ps = DtRaw(x); }
    for (UINT i = 0; i < 16; i++)
    {
        IDirect3DVertexBuffer9* x = nullptr;
        if ((o->vbOk[i] = SUCCEEDED(((DtGetSSrc_t)dtDx[101])(d, i, &x, &o->vbOff[i], &o->vbStr[i])))) o->vb[i] = DtRaw(x);
    }
    { IDirect3DIndexBuffer9* x = nullptr; if ((o->ibOk = SUCCEEDED(((DtGetIB_t)dtDx[105])(d, &x)))) o->ib = DtRaw(x); }
    DtShReadDeclConsts(d, o, o != &dtSh || dtShConsts);   // the compare's scratch copy always; ours only while followed
}

// With the ring drained and the producer direct (the caller's DtSyncScope): at
// arming, and for the validation compare.
static void DtShadowLoad(IDirect3DDevice9* d)
{
    const LONG f = dtForeign;                 // BEFORE the read: a Set racing it re-dirties
    // Arming may be on a new device, whose FVF table is empty and whose objects may sit
    // where the last one's did: nothing remembered about declarations survives it.
    dtShFvfSentN = 0; _InterlockedIncrement(&dtDeclGen);
    DtShadowRead(d, &dtSh);
    dtForeignSeen = f;
    dtShNeedDrain = false;
    DEVSTAT(dtShReloads++;)
}

// Every entry unknown at once; the next Get of each fetches it (dt_device_sync.h).
// Except, after a state block's Apply, the render targets and the depth stencil: a DXVK 3.1 state block cannot hold either
// (D3D9CapturedStateFlag has no member for them), so an Apply cannot move them, and they are the state the game asks for most
// and the injection body binds. `apply` false is the other caller, another thread's Set, which can be a SetRenderTarget:
// there everything goes.
static void DtShDirtyAll(bool apply)
{
    memset(dtSh.rsOk, 0, sizeof(dtSh.rsOk));   memset(dtSh.sampOk, 0, sizeof(dtSh.sampOk));
    memset(dtSh.texOk, 0, sizeof(dtSh.texOk));
    memset(dtSh.vbOk, 0, sizeof(dtSh.vbOk));
    dtSh.vpOk = dtSh.scOk = dtSh.vsOk = dtSh.psOk = dtSh.ibOk = false;
    if (!apply) { memset(dtSh.rtOk, 0, sizeof(dtSh.rtOk)); dtSh.dsOk = false; }
        // Both callers stand on a drained ring with nothing recorded since, so these two are reloaded, not forgotten
    // (DtShReadDeclConsts says why).
    if (dtDev) DtShReadDeclConsts(dtDev, &dtSh, dtShConsts);
    DEVSTAT(dtShDirtied++;)
}

// Before an unknown entry is fetched from DXVK: the fetch is exact only while no queued command names the entry, and an
// injection body posted to the worker names everything it binds while queued or running (dt_core.h, dtNoteDrives). While one
// is out the fetch waits for it, a settle under its own reason. `free` is the caller's proof that no body touches the entry
// (a sampler slot in dtBodySampFree). With inject_worker=0 this is one load that is zero.
// At the post of a body, before it counts as out: every entry of every kind the gate guards that the body is not declared
// free of, and that the copy does not know, is fetched now (exact by the invariant, with no body yet out). Until the next
// event that forgets entries (an Apply or a foreign settle, both of which drain) anything the body touches is answered from
// the copy, so the gate is only the net under a skipped post. Not when another thread's Set is unsettled: the settle is a
// drain, and the post is the worst place in the frame for one. Cost: at most 52 sampler states, 4 textures, 16 streams and
// ten single entries, only those unknown.
static void DtShKnowBody()
{
    if (!dtDev || dtShBodies || dtShNeedDrain || dtForeign != dtForeignSeen) return;
    IDirect3DDevice9* const d = dtDev;
    for (int i = 0; i < 21; i++)
    {
        if (dtBodySampFree >> i & 1u) continue;           // a slot the body never binds: its fetch is not gated either
        for (DWORD t = 1; t <= 13; t++)
        {
            DWORD v = 0;
            if (!dtSh.sampOk[i][t] && SUCCEEDED(((DtGetSS_t)dtDx[68])(d, DtSampNum(i), (D3DSAMPLERSTATETYPE)t, &v)))
            { dtSh.samp[i][t] = v; dtSh.sampOk[i][t] = true; DEVSTAT(dtShFetched++;) }
        }
        IDirect3DBaseTexture9* x = nullptr;
        if (!dtSh.texOk[i] && SUCCEEDED(((DtGetTex_t)dtDx[64])(d, DtSampNum(i), &x))) { dtSh.tex[i] = DtRaw(x); dtSh.texOk[i] = true; DEVSTAT(dtShFetched++;) }
    }
    for (DWORD i = 0; i < 4; i++)
    {
        if (dtSh.rtOk[i]) continue;
        IDirect3DSurface9* x = nullptr;
        const HRESULT hr = ((DtGetRT_t)dtDx[38])(d, i, &x);           // an empty slot is D3DERR_NOTFOUND with a null: known, and none
        if (SUCCEEDED(hr) || hr == D3DERR_NOTFOUND) { dtSh.rt[i] = SUCCEEDED(hr) ? DtRaw(x) : nullptr; dtSh.rtOk[i] = true; DEVSTAT(dtShFetched++;) }
    }
    if (!dtSh.dsOk)
    {
        IDirect3DSurface9* x = nullptr;
        const HRESULT hr = ((DtGetDS_t)dtDx[40])(d, &x);
        if (SUCCEEDED(hr) || hr == D3DERR_NOTFOUND) { dtSh.ds = SUCCEEDED(hr) ? DtRaw(x) : nullptr; dtSh.dsOk = true; DEVSTAT(dtShFetched++;) }
    }
    if (!dtSh.vpOk) { D3DVIEWPORT9 v; if (SUCCEEDED(((DtGetVP_t)dtDx[48])(d, &v))) { dtSh.vp = v; dtSh.vpOk = true; DEVSTAT(dtShFetched++;) } }
    if (!dtSh.scOk) { RECT r; if (SUCCEEDED(((DtGetSC_t)dtDx[76])(d, &r))) { dtSh.sc = r; dtSh.scOk = true; DEVSTAT(dtShFetched++;) } }
    if (!dtSh.vsOk) { IDirect3DVertexShader9* x = nullptr; if (SUCCEEDED(((DtGetVS_t)dtDx[93])(d, &x))) { dtSh.vs = DtRaw(x); dtSh.vsOk = true; DEVSTAT(dtShFetched++;) } }
    if (!dtSh.psOk) { IDirect3DPixelShader9*  x = nullptr; if (SUCCEEDED(((DtGetPS_t)dtDx[108])(d, &x))) { dtSh.ps = DtRaw(x); dtSh.psOk = true; DEVSTAT(dtShFetched++;) } }
    if (!dtSh.ibOk) { IDirect3DIndexBuffer9*  x = nullptr; if (SUCCEEDED(((DtGetIB_t)dtDx[105])(d, &x))) { dtSh.ib = DtRaw(x); dtSh.ibOk = true; DEVSTAT(dtShFetched++;) } }
    for (UINT i = 0; i < 16; i++)
    {
        if (dtSh.vbOk[i]) continue;
        IDirect3DVertexBuffer9* x = nullptr; UINT off = 0, str = 0;
        if (SUCCEEDED(((DtGetSSrc_t)dtDx[101])(d, i, &x, &off, &str))) { dtSh.vb[i] = DtRaw(x); dtSh.vbOff[i] = off; dtSh.vbStr[i] = str; dtSh.vbOk[i] = true; DEVSTAT(dtShFetched++;) }
    }
    for (DWORD w = 0; w < 8; w++)
    {
        DWORD m = dtBodyRsTouched[w];
        if (m == ~0u) continue;                           // an undeclared word: nothing to say about it
        for (; m; m &= m - 1)
        {
            DWORD bit = 0; _BitScanForward(&bit, m);
            const DWORD s = w * 32 + bit;
            if (s < 7 || s > 209 || dtSh.rsOk[s]) continue;
            DWORD v = 0;
            if (SUCCEEDED(((DtGetRS_t)dtDx[58])(dtDev, (D3DRENDERSTATETYPE)s, &v))) { dtSh.rs[s] = v; dtSh.rsOk[s] = true; DEVSTAT(dtShFetched++;) }
        }
    }
}
static __declspec(noinline) void DtShBodySettle()
{
    { DtSyncScope s(DT_R_BODY); }
}
static __forceinline void DtShFetchGate(bool free)
{
    if (dtShBodies && !free) DtShBodySettle();
}

// Another thread changed dtDev's state, or a recorded bind left the viewport unknown: the ring drains, so an entry fetched on
// demand reads DXVK's final value, and after a foreign Set every entry is unknown. Out of line: the scope's destructor would
// otherwise give every Get wrapper an exception frame (DtSync0 in dt_device_rec.h).
// dtShWaitedOutT: how long the settle below has waited another thread's call out, in QPC ticks, for the session.
DEVSTAT(static LONGLONG dtShWaitedOutT = 0;)   // the harness's

static __declspec(noinline) void DtShadowSettle()
{
        // Another thread's call in flight is waited out first: this Get must be told what the device holds once that call has
    // landed; answering before it is how a stale depth stencil gets put back over the main thread's bind. Timed into
    // DT_R_RELOAD, so the report, the threaded=0 verdict and the stuck line see it. The wait includes the other thread's DXVK
    // call, which has no bound of its own.
    if (dtForeignInFlight)
    {
        DEVSTAT(dtShWaitedOut++;)
        LARGE_INTEGER t0, t; QueryPerformanceCounter(&t0); t = t0;
        static bool saidStuck = false;
        for (unsigned i = 1; dtForeignInFlight; i++)
        {
            _mm_pause();
            if ((i & 255) != 0) continue;
            Sleep(0);
            if (saidStuck || (i & 0xFFFF) != 0) continue;
            QueryPerformanceCounter(&t);
            if ((t.QuadPart - t0.QuadPart) * 1000 < (LONGLONG)kDtStuckMs * dtQpf) continue;
            saidStuck = true;
            LogRecord("  threaded: the render thread has waited %d s for ANOTHER THREAD'S call on the device (waiting for the device"
                      " thread, or inside DXVK): the game is frozen until it returns", kDtStuckMs / 1000);
            IvtWarn("threaded=%d: the game has been frozen for %d s waiting for another thread's call on the device to finish;"
                    " if this repeats, set threaded=0", dtCfg, kDtStuckMs / 1000);
        }
        QueryPerformanceCounter(&t);
        DEVSTAT(dtShWaitedOutT += t.QuadPart - t0.QuadPart;)
        dtWaitT[DT_R_RELOAD] += t.QuadPart - t0.QuadPart;     // so the report's total and the threaded=0 verdict see it
    }
    const LONG f = dtForeign;                 // BEFORE the drain: a Set racing it re-settles
    { DtSyncScope s(DT_R_RELOAD); }
    if (f != dtForeignSeen) DtShDirtyAll(false);
    dtForeignSeen = f;
    dtShNeedDrain = false;
    DEVSTAT(dtShReloads++;)
}

// Before every shadow answer: a compare, and the settle only when it is needed.
static __forceinline void DtShadowReady(IDirect3DDevice9*)
{
    if (dtShNeedDrain || dtForeign != dtForeignSeen) DtShadowSettle();
}

static void DtMismatch(const char* what, int idx, DWORD ours, DWORD dxvk)
{
    if (dtValHeldN >= 0) { if (dtValHeldN < 8) dtValHeld[dtValHeldN] = { what, idx, ours, dxvk }; dtValHeldN++; return; }
    dtValMis++;
    if (dtValLogged < 40)
    {
        dtValLogged++;
        LogRecord("  threaded: SHADOW MISMATCH %s[%d]: the copy said 0x%08lX, DXVK says 0x%08lX%s", what, idx,
            (unsigned long)ours, (unsigned long)dxvk, dtValLogged == 40 ? " (the last one logged)" : "");
    }
}

// The whole copy against DXVK, with the ring drained: threaded=2, every Present. An entry the copy holds as unknown is not a
// claim and is not compared.
// Another thread's Set during the read is not a mismatch: the read is several hundred DXVK calls, each taking DXVK's lock, so
// a Set can land mid-read and leave DXVK newer than the copy until the next frame reloads. So what the compare finds is held,
// and thrown away (and counted) if the foreign count moved across the read.
static void DtShadowCompare(IDirect3DDevice9* d)
{
    static DtShadow r;
    const LONG foreignBefore = dtForeign;
    DtShadowRead(d, &r);
    DEVSTAT(dtValChecks++;)
    dtValHeldN = 0;
    for (int s = 7; s <= 209; s++)
        if (dtSh.rsOk[s] && r.rsOk[s] && r.rs[s] != dtSh.rs[s]) DtMismatch("render state", s, dtSh.rs[s], r.rs[s]);
    for (int i = 0; i < 21; i++)
    {
        for (int t = 1; t <= 13; t++)
            if (dtSh.sampOk[i][t] && r.sampOk[i][t] && r.samp[i][t] != dtSh.samp[i][t]) DtMismatch("sampler state", i * 100 + t, dtSh.samp[i][t], r.samp[i][t]);
        if (dtSh.texOk[i] && r.tex[i] != dtSh.tex[i]) DtMismatch("texture", i, (DWORD)(UINT_PTR)dtSh.tex[i], (DWORD)(UINT_PTR)r.tex[i]);
    }
    for (int i = 0; i < 4; i++)
        if (dtSh.rtOk[i] && r.rt[i] != dtSh.rt[i]) DtMismatch("render target", i, (DWORD)(UINT_PTR)dtSh.rt[i], (DWORD)(UINT_PTR)r.rt[i]);
    if (dtSh.dsOk && r.ds != dtSh.ds) DtMismatch("depth stencil", 0, (DWORD)(UINT_PTR)dtSh.ds, (DWORD)(UINT_PTR)r.ds);
    if (dtSh.vpOk && memcmp(&r.vp, &dtSh.vp, sizeof(r.vp)))
        DtMismatch("viewport", 0, (DWORD)(dtSh.vp.Width << 16 | (dtSh.vp.Height & 0xFFFF)), (DWORD)(r.vp.Width << 16 | (r.vp.Height & 0xFFFF)));
    if (dtSh.scOk && memcmp(&r.sc, &dtSh.sc, sizeof(r.sc)))
        DtMismatch("scissor", 0, (DWORD)(dtSh.sc.right << 16 | (dtSh.sc.bottom & 0xFFFF)), (DWORD)(r.sc.right << 16 | (r.sc.bottom & 0xFFFF)));
    if (dtSh.vsOk && r.vs != dtSh.vs) DtMismatch("vertex shader", 0, (DWORD)(UINT_PTR)dtSh.vs, (DWORD)(UINT_PTR)r.vs);
    if (dtSh.psOk && r.ps != dtSh.ps) DtMismatch("pixel shader", 0, (DWORD)(UINT_PTR)dtSh.ps, (DWORD)(UINT_PTR)r.ps);
    for (int i = 0; i < 16; i++)
    {
        if (!dtSh.vbOk[i]) continue;
        if (r.vb[i] != dtSh.vb[i]) DtMismatch("stream source", i, (DWORD)(UINT_PTR)dtSh.vb[i], (DWORD)(UINT_PTR)r.vb[i]);
        if (r.vbOff[i] != dtSh.vbOff[i] || r.vbStr[i] != dtSh.vbStr[i])
            DtMismatch("stream offset/stride", i, dtSh.vbOff[i] << 16 | (dtSh.vbStr[i] & 0xFFFF), r.vbOff[i] << 16 | (r.vbStr[i] & 0xFFFF));
    }
    if (dtSh.ibOk && r.ib != dtSh.ib) DtMismatch("indices", 0, (DWORD)(UINT_PTR)dtSh.ib, (DWORD)(UINT_PTR)r.ib);
    if (dtSh.declOk && r.decl != dtSh.decl) DtMismatch("vertex declaration", 0, (DWORD)(UINT_PTR)dtSh.decl, (DWORD)(UINT_PTR)r.decl);
    // The FVF the copy WOULD answer (from a SetFVF, or remembered for the bound object)
    // against DXVK's; an answer it would have to ask for is not a claim.
    {
        DWORD ours = 0, theirs = 0; bool claim = false;
        if (dtSh.fvfOk) { ours = dtSh.fvf; claim = true; }
        else if (dtSh.declOk) claim = !dtSh.decl || DtShFvfOf(dtSh.decl, &ours);
        if (claim && SUCCEEDED(((DtGetFVF_t)dtDx[90])(d, &theirs)) && theirs != ours) DtMismatch("FVF", 0, ours, theirs);
    }
    // DXVK stores a float constant bit for bit (d3d9_state.h, UpdateStateConstants), so
    // the compare is of bits; the register's x is what the line shows.
    if (dtShConsts)
        for (int i = 0; i < 224; i++)
            if ((dtSh.pscOk[i >> 5] >> (i & 31) & 1u) && memcmp(r.psc[i], dtSh.psc[i], 16))
            { DWORD a, b; memcpy(&a, dtSh.psc[i], 4); memcpy(&b, r.psc[i], 4); DtMismatch("pixel constant (x, as bits)", i, a, b); }
    const int held = dtValHeldN; dtValHeldN = -1;
    if (!held) return;
        // Or the layer was unarmed under the read: the copy is dead anyway.
    if (dtForeign != foreignBefore || !dtArmed) { DEVSTAT(dtValRaced++;) return; }   // raced: the next arm reloads, and compares after that
    for (int i = 0; i < held && i < 8; i++) DtMismatch(dtValHeld[i].what, dtValHeld[i].idx, dtValHeld[i].ours, dtValHeld[i].dxvk);
    if (held > 8) dtValMis += held - 8;
}

// ---- the shadow's writers, one per Set the copy follows ------------------------------
// SetRenderTarget(0, x) also resets the viewport and the scissor to x's full
// size, in D3D9 and in DXVK. The size comes from x itself (GetDesc is a plain
// read of a description DXVK fixed at creation). A failure marks the copy stale.
static void DtShRenderTarget(DWORD i, IDirect3DSurface9* s)
{
    dtSh.rt[i] = s; dtSh.rtOk[i] = true;
    if (i != 0 || !s) return;
    D3DSURFACE_DESC dd;
    // The size could not be read: the viewport and scissor this bind sets are
    // unknown, and the bind may still be queued, so the next Get of either drains
    // first and then fetches (dtShNeedDrain). Everything else stays known.
    if (FAILED(s->GetDesc(&dd))) { dtSh.vpOk = false; dtSh.scOk = false; dtShNeedDrain = true; return; }
    dtSh.vp.X = 0; dtSh.vp.Y = 0; dtSh.vp.Width = dd.Width; dtSh.vp.Height = dd.Height;
    dtSh.vp.MinZ = 0.0f; dtSh.vp.MaxZ = 1.0f; dtSh.vpOk = true;
    dtSh.sc.left = 0; dtSh.sc.top = 0; dtSh.sc.right = (LONG)dd.Width; dtSh.sc.bottom = (LONG)dd.Height; dtSh.scOk = true;
}
// DrawPrimitiveUP unbinds stream 0; DrawIndexedPrimitiveUP unbinds the indices too.
static __forceinline void DtShUpDraw(bool indexed)
{
    dtSh.vb[0] = nullptr; dtSh.vbOff[0] = 0; dtSh.vbStr[0] = 0; dtSh.vbOk[0] = true;
    if (indexed) { dtSh.ib = nullptr; dtSh.ibOk = true; }
}
