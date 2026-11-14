// The device thread, part 3 of 5: the recorded calls and the shadow Gets.
// Each wrapper has the device method's exact signature (d3d9.h), so DLSS-IV.cpp assigns them to its origX pointers without a
// cast: a wrong signature is a compile error there. Each one, in order:
//   1. not recording (another thread, another device, a direct scope, or the layer off): another thread's call on the armed
//      device first takes its place behind the ring (DtOrderForeign, dt_core.h); then follow the shadow if the call is the
//      producer's, and call DXVK directly;
//   2. a parameter DXVK would refuse, or a payload too big for the ring: SYNC, so the caller gets DXVK's own answer;
//   3. otherwise write the shadow, write the command, answer D3D_OK.
// Pointer arguments are copied into the command and the copy is what DXVK gets.
// The twelve wrappers whose state the shadow answers tell the state-block recording window (dt_sbtab.h) what went into a
// block, on both paths (DT_SBW_REC, DT_SBW_DIR) and at each refusal (DT_SBW_SYNC); their direct path is DT_ORDERED_TRK,
// which adds the unarmed bracket.
#pragma once

// SYNC from inside a recording wrapper, out of line. Under /EHa a function holding an object with a destructor gets an SEH
// frame in its prologue whether or not that branch runs, so the DtSyncScope lives in these, one per argument count, and a
// wrapper only casts and calls. Every argument is a dword on x86 (DtF0..DtF8). `reason` is the report's wait reason: the
// slot itself for a refused parameter, DT_R_VALIDATE for the check.
static __declspec(noinline) HRESULT DtSync0(int reason, int slot, void* d)
{ DtSyncScope s_(reason); return ((DtF0)dtDx[slot])(d); }
static __declspec(noinline) HRESULT DtSync1(int reason, int slot, void* d, DWORD a1)
{ DtSyncScope s_(reason); return ((DtF1)dtDx[slot])(d, a1); }
static __declspec(noinline) HRESULT DtSync2(int reason, int slot, void* d, DWORD a1, DWORD a2)
{ DtSyncScope s_(reason); return ((DtF2)dtDx[slot])(d, a1, a2); }
static __declspec(noinline) HRESULT DtSync3(int reason, int slot, void* d, DWORD a1, DWORD a2, DWORD a3)
{ DtSyncScope s_(reason); return ((DtF3)dtDx[slot])(d, a1, a2, a3); }
static __declspec(noinline) HRESULT DtSync4(int reason, int slot, void* d, DWORD a1, DWORD a2, DWORD a3, DWORD a4)
{ DtSyncScope s_(reason); return ((DtF4)dtDx[slot])(d, a1, a2, a3, a4); }
static __declspec(noinline) HRESULT DtSync5(int reason, int slot, void* d, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5)
{ DtSyncScope s_(reason); return ((DtF5)dtDx[slot])(d, a1, a2, a3, a4, a5); }
static __declspec(noinline) HRESULT DtSync6(int reason, int slot, void* d, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6)
{ DtSyncScope s_(reason); return ((DtF6)dtDx[slot])(d, a1, a2, a3, a4, a5, a6); }
static __declspec(noinline) HRESULT DtSync8(int reason, int slot, void* d, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7, DWORD a8)
{ DtSyncScope s_(reason); return ((DtF8)dtDx[slot])(d, a1, a2, a3, a4, a5, a6, a7, a8); }
#define DT_P(x) ((DWORD)(UINT_PTR)(x))          // a pointer argument, as the dword it is pushed as
static __forceinline DWORD DtFbits(float f) { DWORD v; memcpy(&v, &f, 4); return v; }

static __forceinline DWORD DtDw(UINT bytes) { return (bytes + 3u) >> 2; }

// ---- render state, samplers, textures -------------------------------------------
typedef HRESULT(__stdcall* dtT_SetRenderState)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
// No filter for redundant Sets: RAGE already filters its own state, and the costly Sets (SetTexture, SetStreamSource,
// SetIndices) are almost never redundant.

static HRESULT __stdcall dt_SetRenderState(IDirect3DDevice9* d, D3DRENDERSTATETYPE s, DWORD v)
{
    if (!DtRecFor(d))
    {
        DT_BODY_DECL_RS(s)
        const HRESULT hr = DT_ORDERED_TRK(d, 57, ((dtT_SetRenderState)dtDx[57])(d, s, v));
        if ((DWORD)s < 256 && DtShMine(d) && !dtSbRec) { dtSh.rs[s] = v; dtSh.rsOk[s] = true; }
        DT_SBW_DIR(hr, d, DtSbWinRs(d, (DWORD)s, v))
        return hr;
    }
    if ((DWORD)s >= 256) { DT_SBW_SYNC(d, DT_SBT_RS) return DtSync2(57, 57, d, (DWORD)s, v); }
    if (!dtSbRec) { dtSh.rs[s] = v; dtSh.rsOk[s] = true; }
    DT_SBW_REC(DtSbWinRs(d, (DWORD)s, v))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(57), 3);
    p[1] = (DWORD)s; p[2] = v;
    DtCommit(3);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetSamplerState)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
static HRESULT __stdcall dt_SetSamplerState(IDirect3DDevice9* d, DWORD smp, D3DSAMPLERSTATETYPE t, DWORD v)
{
    const int i = DtSampIdx(smp);
    const bool ok = i >= 0 && (DWORD)t >= 1 && (DWORD)t <= 13;
    if (!DtRecFor(d))
    {
        DT_BODY_DECL_SAMP(i)
        const HRESULT hr = DT_ORDERED_TRK(d, 69, ((dtT_SetSamplerState)dtDx[69])(d, smp, t, v));
        if (ok && DtShMine(d) && !dtSbRec) { dtSh.samp[i][t] = v; dtSh.sampOk[i][t] = true; }
        DT_SBW_DIR(hr, d, DtSbWinSamp(d, i, (DWORD)t, v, ok))
        return hr;
    }
    if (!ok) { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync3(69, 69, d, smp, (DWORD)t, v); }
    if (!dtSbRec) { dtSh.samp[i][t] = v; dtSh.sampOk[i][t] = true; }
    DT_SBW_REC(DtSbWinSamp(d, i, (DWORD)t, v, true))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(69), 4);
    p[1] = smp; p[2] = (DWORD)t; p[3] = v;
    DtCommit(4);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetTexture)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
static HRESULT __stdcall dt_SetTexture(IDirect3DDevice9* d, DWORD stage, IDirect3DBaseTexture9* tex)
{
    const int i = DtSampIdx(stage);
    if (!DtRecFor(d))
    {
        DT_BODY_DECL_TEX(i)
        const HRESULT hr = DT_ORDERED_TRK(d, 65, ((dtT_SetTexture)dtDx[65])(d, stage, tex));
        if (i >= 0 && DtShMine(d) && !dtSbRec) { dtSh.tex[i] = tex; dtSh.texOk[i] = true; }
        DT_SBW_DIR(hr, d, DtSbWinTex(d, i, tex))
        return hr;
    }
    if (i < 0) { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync2(65, 65, d, stage, DT_P(tex)); }
    if (!dtSbRec) { dtSh.tex[i] = tex; dtSh.texOk[i] = true; }
    DT_SBW_REC(DtSbWinTex(d, i, tex))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(65), 3);
    p[1] = stage; p[2] = (DWORD)(UINT_PTR)tex;
    DtCommit(3);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetTextureStageState)(IDirect3DDevice9*, DWORD, D3DTEXTURESTAGESTATETYPE, DWORD);
static HRESULT __stdcall dt_SetTextureStageState(IDirect3DDevice9* d, DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 67, ((dtT_SetTextureStageState)dtDx[67])(d, stage, t, v));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(67), 4);
    p[1] = stage; p[2] = (DWORD)t; p[3] = v;
    DtCommit(4);
    return D3D_OK;
}

// ---- targets, viewport, scissor ----
// DXVK refuses, and changes nothing, for an index past 3, a null target at 0, another device's surface, or a surface that is
// not a render target, deciding from the arguments alone before it touches device state (DXVK 3.1, d3d9_device.cpp,
// SetRenderTarget). So a refused bind needs no drain: DXVK answers at once beside the worker under its own lock, like a PASS
// call. Something in the process makes such a call once a frame, which would otherwise be a drain behind the seam.
// The test is DXVK's own, on the description DXVK reads (D3D9Surface::GetDesc's Usage). Should DXVK ever accept one, the
// bind has landed ahead of the queue: the ring is drained, the entries it reaches are marked unknown, and it is said once at
// log=1.
static __forceinline bool DtIsRenderTarget(IDirect3DSurface9* s)
{
    D3DSURFACE_DESC dd;
    return SUCCEEDED(s->GetDesc(&dd)) && (dd.Usage & D3DUSAGE_RENDERTARGET);
}
typedef HRESULT(__stdcall* dtT_SetRenderTarget)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
static __declspec(noinline) HRESULT DtRefusedRenderTarget(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s)
{
    const HRESULT hr = DT_BBS_BESIDE(s != nullptr, d, DT_BBS_BIND, s, ((dtT_SetRenderTarget)dtDx[37])(d, i, s));   // the save's question (dt_bbs.h): a back buffer bound beside the worker
    if (FAILED(hr)) return hr;
    DTBBS(DtBbsRtMoved(d, i, s);)           // the save's question (dt_bbs.h): the worker's own bind is followed here
    DtDrain(37);
    if (i < 4) { dtSh.rtOk[i] = false; if (i == 0) { dtSh.vpOk = false; dtSh.scOk = false; } }
    static bool said = false;
    if (!said)
    {
        said = true;
        LogRecord("  threaded: DXVK ACCEPTED a SetRenderTarget(%lu, %p) predicted refused: the ring drained, the shadow re-reads", (unsigned long)i, (void*)s);
    }
    return hr;
}
static HRESULT __stdcall dt_SetRenderTarget(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s)
{
    const bool ok = i < 4 && (i != 0 || s);
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_BBS_BESIDE(ok && s, d, DT_BBS_BIND, s, DT_ORDERED(d, 37, ((dtT_SetRenderTarget)dtDx[37])(d, i, s)));   // the save's question (dt_bbs.h): a back buffer bound beside the worker
        if (ok && SUCCEEDED(hr) && DtShMine(d)) DtShRenderTarget(i, s);
        DTBBS(if (ok && SUCCEEDED(hr)) DtBbsRtMoved(d, i, s);)   // the save's question (dt_bbs.h): the worker's own bind is followed here
        return hr;
    }
    if (!ok || (s && !DtIsRenderTarget(s))) return DtRefusedRenderTarget(d, i, s);   // no drain: see above
    DtShRenderTarget(i, s);                 // even while a state block records: see dtSbRec
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(37), 3);
    p[1] = i; p[2] = (DWORD)(UINT_PTR)s;
    DtCommit(3);
    return D3D_OK;
}

// DXVK refuses a surface that is not a depth-stencil, and changes nothing
// (DXVK 3.1, SetDepthStencilSurface): that one goes to DXVK as a SYNC.
typedef HRESULT(__stdcall* dtT_SetDepthStencilSurface)(IDirect3DDevice9*, IDirect3DSurface9*);
static HRESULT __stdcall dt_SetDepthStencilSurface(IDirect3DDevice9* d, IDirect3DSurface9* s)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED(d, 39, ((dtT_SetDepthStencilSurface)dtDx[39])(d, s));
        if (SUCCEEDED(hr) && DtShMine(d)) { dtSh.ds = s; dtSh.dsOk = true; }
        return hr;
    }
    if (s)
    {
        D3DSURFACE_DESC dd;
        if (FAILED(s->GetDesc(&dd)) || !(dd.Usage & D3DUSAGE_DEPTHSTENCIL))
            return DtSync1(39, 39, d, DT_P(s));
    }
    dtSh.ds = s; dtSh.dsOk = true;          // even while a state block records: see dtSbRec
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(39), 2);
    p[1] = (DWORD)(UINT_PTR)s;
    DtCommit(2);
    return D3D_OK;
}

// DXVK 3.1 stores MaxZ = MinZ + 0.001 whenever MinZ is not below MaxZ (SetViewport), and GetViewport answers that.
// The same expression, so a NaN goes the same way.
static __forceinline void DtShViewport(const D3DVIEWPORT9* vp)
{
    dtSh.vp = *vp;
    dtSh.vp.MaxZ = vp->MinZ < vp->MaxZ ? vp->MaxZ : vp->MinZ + 0.001f;
    dtSh.vpOk = true;
}
typedef HRESULT(__stdcall* dtT_SetViewport)(IDirect3DDevice9*, const D3DVIEWPORT9*);
static HRESULT __stdcall dt_SetViewport(IDirect3DDevice9* d, const D3DVIEWPORT9* vp)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 47, ((dtT_SetViewport)dtDx[47])(d, vp));
        if (vp && DtShMine(d) && !dtSbRec) DtShViewport(vp);
        DT_SBW_DIR(hr, d, DtSbWinVp(d, vp))
        return hr;
    }
    if (!vp) { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync1(47, 47, d, DT_P(vp)); }
    if (!dtSbRec) DtShViewport(vp);
    DT_SBW_REC(DtSbWinVp(d, vp))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(47), 2 + 6);
    memcpy(p + 2, vp, sizeof(D3DVIEWPORT9));
    p[1] = (DWORD)(UINT_PTR)(p + 2);
    DtCommit(2 + 6);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetScissorRect)(IDirect3DDevice9*, const RECT*);
static HRESULT __stdcall dt_SetScissorRect(IDirect3DDevice9* d, const RECT* r)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 75, ((dtT_SetScissorRect)dtDx[75])(d, r));
        if (r && DtShMine(d) && !dtSbRec) { dtSh.sc = *r; dtSh.scOk = true; }
        DT_SBW_DIR(hr, d, DtSbWinSc(d, r))
        return hr;
    }
    if (!r) { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync1(75, 75, d, DT_P(r)); }
    if (!dtSbRec) { dtSh.sc = *r; dtSh.scOk = true; }
    DT_SBW_REC(DtSbWinSc(d, r))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(75), 2 + 4);
    memcpy(p + 2, r, sizeof(RECT));
    p[1] = (DWORD)(UINT_PTR)(p + 2);
    DtCommit(2 + 4);
    return D3D_OK;
}

// ---- shaders, streams, indices, declarations ---------------------------------------
typedef HRESULT(__stdcall* dtT_SetVertexShader)(IDirect3DDevice9*, IDirect3DVertexShader9*);
static HRESULT __stdcall dt_SetVertexShader(IDirect3DDevice9* d, IDirect3DVertexShader9* s)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 92, ((dtT_SetVertexShader)dtDx[92])(d, s));
        if (DtShMine(d) && !dtSbRec) { dtSh.vs = s; dtSh.vsOk = true; }
        DT_SBW_DIR(hr, d, DtSbWinVs(d, s))
        return hr;
    }
    if (!dtSbRec) { dtSh.vs = s; dtSh.vsOk = true; }
    DT_SBW_REC(DtSbWinVs(d, s))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(92), 2);
    p[1] = (DWORD)(UINT_PTR)s;
    DtCommit(2);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetPixelShader)(IDirect3DDevice9*, IDirect3DPixelShader9*);
static HRESULT __stdcall dt_SetPixelShader(IDirect3DDevice9* d, IDirect3DPixelShader9* s)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 107, ((dtT_SetPixelShader)dtDx[107])(d, s));
        if (DtShMine(d) && !dtSbRec) { dtSh.ps = s; dtSh.psOk = true; }
        DT_SBW_DIR(hr, d, DtSbWinPs(d, s))
        return hr;
    }
    if (!dtSbRec) { dtSh.ps = s; dtSh.psOk = true; }
    DT_SBW_REC(DtSbWinPs(d, s))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(107), 2);
    p[1] = (DWORD)(UINT_PTR)s;
    DtCommit(2);
    return D3D_OK;
}

// A null buffer zeroes the offset and the stride in DXVK 3.1 (SetStreamSource), so GetStreamSource then answers null, 0, 0.
static __forceinline void DtShStream(UINT i, IDirect3DVertexBuffer9* vb, UINT off, UINT str)
{
    dtSh.vb[i] = vb;
    dtSh.vbOff[i] = vb ? off : 0; dtSh.vbStr[i] = vb ? str : 0;
    dtSh.vbOk[i] = true;
}
typedef HRESULT(__stdcall* dtT_SetStreamSource)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
static HRESULT __stdcall dt_SetStreamSource(IDirect3DDevice9* d, UINT i, IDirect3DVertexBuffer9* vb, UINT off, UINT str)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 100, ((dtT_SetStreamSource)dtDx[100])(d, i, vb, off, str));
        if (i < 16 && DtShMine(d) && !dtSbRec) DtShStream(i, vb, off, str);
        DT_SBW_DIR(hr, d, DtSbWinVb(d, i, vb, off, str))
        return hr;
    }
    if (i >= 16) { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync4(100, 100, d, i, DT_P(vb), off, str); }
    if (!dtSbRec) DtShStream(i, vb, off, str);
    DT_SBW_REC(DtSbWinVb(d, i, vb, off, str))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 4 | DT_SLOT(100), 5);
    p[1] = i; p[2] = (DWORD)(UINT_PTR)vb; p[3] = off; p[4] = str;
    DtCommit(5);
    return D3D_OK;
}

// The settings DXVK refuses (DXVK 3.1, SetStreamSourceFreq) go to it as a
// SYNC, so the refusal the caller sees is DXVK's own.
typedef HRESULT(__stdcall* dtT_SetStreamSourceFreq)(IDirect3DDevice9*, UINT, UINT);
static HRESULT __stdcall dt_SetStreamSourceFreq(IDirect3DDevice9* d, UINT i, UINT f)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 102, ((dtT_SetStreamSourceFreq)dtDx[102])(d, i, f));
    const bool inst = (f & D3DSTREAMSOURCE_INSTANCEDATA) != 0, idx = (f & D3DSTREAMSOURCE_INDEXEDDATA) != 0;
    if (i >= 16 || f == 0 || (i == 0 && inst) || (inst && idx)) return DtSync2(102, 102, d, i, f);
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(102), 3);
    p[1] = i; p[2] = f;
    DtCommit(3);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetIndices)(IDirect3DDevice9*, IDirect3DIndexBuffer9*);
static HRESULT __stdcall dt_SetIndices(IDirect3DDevice9* d, IDirect3DIndexBuffer9* ib)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 104, ((dtT_SetIndices)dtDx[104])(d, ib));
        if (DtShMine(d) && !dtSbRec) { dtSh.ib = ib; dtSh.ibOk = true; }
        DT_SBW_DIR(hr, d, DtSbWinIb(d, ib))
        return hr;
    }
    if (!dtSbRec) { dtSh.ib = ib; dtSh.ibOk = true; }
    DT_SBW_REC(DtSbWinIb(d, ib))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(104), 2);
    p[1] = (DWORD)(UINT_PTR)ib;
    DtCommit(2);
    return D3D_OK;
}

// The declaration and the FVF, followed (dt_shadow.h says why the FVF is the declaration's). Inside a state block's recording
// DXVK sends both to the block and leaves the device alone (SetFVF is SetVertexDeclaration underneath), so the copy does not
// follow them there either.
typedef HRESULT(__stdcall* dtT_SetVertexDeclaration)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
static HRESULT __stdcall dt_SetVertexDeclaration(IDirect3DDevice9* d, IDirect3DVertexDeclaration9* decl)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 87, ((dtT_SetVertexDeclaration)dtDx[87])(d, decl));
        if (DtShMine(d) && !dtSbRec) DtShDecl(decl);
        DT_SBW_DIR(hr, d, DtSbWinDecl(d, decl))
        return hr;
    }
    if (!dtSbRec) DtShDecl(decl);
    DT_SBW_REC(DtSbWinDecl(d, decl))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(87), 2);
    p[1] = (DWORD)(UINT_PTR)decl;
    DtCommit(2);
    return D3D_OK;
}
// SetFVF(0) changes nothing in DXVK 3.1 and is recorded as it always was. A value DXVK has
// not met makes it CONSTRUCT the table's declaration, which is one of the three events
// that empty the per-object FVF table (dtDeclGen); a value this thread has already sent
// cannot, and must not, or a game that sets an FVF every frame would empty it every frame.
typedef HRESULT(__stdcall* dtT_SetFVF)(IDirect3DDevice9*, DWORD);
static HRESULT __stdcall dt_SetFVF(IDirect3DDevice9* d, DWORD fvf)
{
    if (!DtRecFor(d))
    {
        if (fvf && !(d == dtDev && __readfsdword(0x24) == dtProducer && DtShFvfSeen(fvf))) _InterlockedIncrement(&dtDeclGen);
        const HRESULT hr = DT_ORDERED_TRK(d, 89, ((dtT_SetFVF)dtDx[89])(d, fvf));
        if (fvf && DtShMine(d) && !dtSbRec) DtShFvf(fvf);
        DT_SBW_DIR(hr, d, DtSbWinFvf(d, fvf))
        return hr;
    }
    if (fvf)
    {
        if (!DtShFvfSeen(fvf)) _InterlockedIncrement(&dtDeclGen);
        if (!dtSbRec) DtShFvf(fvf);
    }
    DT_SBW_REC(DtSbWinFvf(d, fvf))
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(89), 2);
    p[1] = fvf;
    DtCommit(2);
    return D3D_OK;
}

// A one-dword-argument call with nothing to follow: LightEnable's pair aside, the
// handful of scalar setters.
#define DT_REC1(slot, name, A1)                                                           \
    typedef HRESULT(__stdcall* dtT_##name)(IDirect3DDevice9*, A1);                        \
    static HRESULT __stdcall dt_##name(IDirect3DDevice9* d, A1 a)                         \
    {                                                                                     \
        if (!DtRecFor(d)) return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot])(d, a));    \
        DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(slot), 2);                              \
        DWORD v; static_assert(sizeof(A1) == 4, "one 32-bit argument"); memcpy(&v, &a, 4); \
        p[1] = v; DtCommit(2); return D3D_OK;                                             \
    }
DT_REC1(73, SetCurrentTexturePalette, UINT)
DT_REC1(77, SetSoftwareVertexProcessing, BOOL)
DT_REC1(79, SetNPatchMode, float)

typedef HRESULT(__stdcall* dtT_LightEnable)(IDirect3DDevice9*, DWORD, BOOL);
static HRESULT __stdcall dt_LightEnable(IDirect3DDevice9* d, DWORD i, BOOL on)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 53, ((dtT_LightEnable)dtDx[53])(d, i, on));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(53), 3);
    p[1] = i; p[2] = (DWORD)on;
    DtCommit(3);
    return D3D_OK;
}

// ---- scene, draws -------------------------------------------------------------------
#define DT_REC0(slot, name)                                                    \
    typedef HRESULT(__stdcall* dtT_##name)(IDirect3DDevice9*);                 \
    static HRESULT __stdcall dt_##name(IDirect3DDevice9* d)                    \
    {                                                                          \
        if (!DtRecFor(d)) return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot])(d));       \
        DtCmdDev(DT_OP_CALL0 | DT_SLOT(slot), 1); DtCommit(1); return D3D_OK;      \
    }
DT_REC0(41, BeginScene)
DT_REC0(42, EndScene)

typedef HRESULT(__stdcall* dtT_DrawPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
static HRESULT __stdcall dt_DrawPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT sv, UINT pc)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE(true, d, DT_BBS_DRAW, nullptr, DT_ORDERED(d, 81, ((dtT_DrawPrimitive)dtDx[81])(d, t, sv, pc)));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(81), 4);
    p[1] = (DWORD)t; p[2] = sv; p[3] = pc;
    DtCommit(4);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_DrawIndexedPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
static HRESULT __stdcall dt_DrawIndexedPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE(true, d, DT_BBS_DRAW, nullptr, DT_ORDERED(d, 82, ((dtT_DrawIndexedPrimitive)dtDx[82])(d, t, bv, mi, nv, si, pc)));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 6 | DT_SLOT(82), 7);
    p[1] = (DWORD)t; p[2] = (DWORD)bv; p[3] = mi; p[4] = nv; p[5] = si; p[6] = pc;
    DtCommit(7);
    return D3D_OK;
}

// Vertices a primitive count needs, by type; 0 for anything D3D9 does not define,
// which then goes to DXVK as a SYNC call for its own answer. 64-bit, and the
// callers bound it and the stride separately before multiplying the two.
static __forceinline unsigned long long DtPrimVerts(D3DPRIMITIVETYPE t, UINT pc)
{
    const unsigned long long n = pc;
    switch (t)
    {
    case D3DPT_POINTLIST:     return n;
    case D3DPT_LINELIST:      return n * 2;
    case D3DPT_LINESTRIP:     return n + 1;
    case D3DPT_TRIANGLELIST:  return n * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:   return n + 2;
    default:                  return 0;
    }
}

typedef HRESULT(__stdcall* dtT_DrawPrimitiveUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
static HRESULT __stdcall dt_DrawPrimitiveUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT pc, const void* v, UINT str)
{
    // DXVK 3.1, in this order (d3d9_device.cpp, DrawPrimitiveUP): a zero stride is
    // refused, no declaration is refused, no primitives is S_OK, and only then is
    // stream 0 unbound. So only a successful draw of something unbinds, and a zero
    // stride goes to DXVK as a SYNC for its refusal. The declaration is not
    // shadowed: a UP draw with none bound (which draws nothing natively either) is
    // answered D3D_OK, and its replay is counted as a failed one in the report.
    // Not gated on dtSbRec: see its declaration.
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_BBS_BESIDE(pc != 0, d, DT_BBS_DRAW, nullptr, DT_ORDERED(d, 83, ((dtT_DrawPrimitiveUP)dtDx[83])(d, t, pc, v, str)));   // (the save's question, dt_bbs.h)
        if (pc && SUCCEEDED(hr) && DtShMine(d)) DtShUpDraw(false);
        return hr;
    }
        // Each factor bounded before the product: a count near 2^32 times a stride near 2^32 wraps even in 64 bits.
    const unsigned long long lim = (DT_MAX_CMD_DW - 16) * 4ull;
    const unsigned long long nv = pc ? DtPrimVerts(t, pc) : 0;
    const unsigned long long bytes = (nv <= lim && str <= lim) ? nv * str : ~0ull;
    if (!nv || !v || !str || bytes > lim)
    {
        const HRESULT hr = DT_BBS_BESIDE(pc != 0, d, DT_BBS_DRAW, DT_BBS_ANY, DtSync4(83, 83, d, (DWORD)t, pc, DT_P(v), str));   // the save's question, asked ahead of the drain, where the binding is not yet the one this call meets
        if (pc && SUCCEEDED(hr)) DtShUpDraw(false);
        return hr;
    }
    const DWORD pay = DtDw((UINT)bytes), n = 5 + pay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 4 | DT_SLOT(83), n);
    memcpy(p + 5, v, (size_t)bytes);
    p[1] = (DWORD)t; p[2] = pc; p[3] = (DWORD)(UINT_PTR)(p + 5); p[4] = str;
    DtCommit(n);
    DtShUpDraw(false);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_DrawIndexedPrimitiveUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
static HRESULT __stdcall dt_DrawIndexedPrimitiveUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc,
                                                   const void* idx, D3DFORMAT fmt, const void* v, UINT str)
{
    // As dt_DrawPrimitiveUP, and here NumVertices 0 is S_OK before any unbinding too
    // (3.1: "!PrimitiveCount || !NumVertices"), so it goes to DXVK as a SYNC as well.
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_BBS_BESIDE(pc && nv, d, DT_BBS_DRAW, nullptr, DT_ORDERED(d, 84, ((dtT_DrawIndexedPrimitiveUP)dtDx[84])(d, t, mi, nv, pc, idx, fmt, v, str)));   // (the save's question, dt_bbs.h)
        if (pc && nv && SUCCEEDED(hr) && DtShMine(d)) DtShUpDraw(true);
        return hr;
    }
    // Each factor bounded before its product: see dt_DrawPrimitiveUP.
    const unsigned long long lim = (DT_MAX_CMD_DW - 24) * 4ull;
    const unsigned long long ni = pc ? DtPrimVerts(t, pc) : 0;
    const UINT isz = fmt == D3DFMT_INDEX32 ? 4u : fmt == D3DFMT_INDEX16 ? 2u : 0u;
    const unsigned long long nvt = (unsigned long long)mi + nv;
    const unsigned long long ib = ni <= lim ? ni * isz : ~0ull;
    const unsigned long long vb = (nvt <= lim && str <= lim) ? nvt * str : ~0ull;
    if (!ni || !nv || !str || !isz || !idx || !v || ib > lim || vb > lim || ib + vb > lim)
    {
        const HRESULT hr = DT_BBS_BESIDE(pc && nv, d, DT_BBS_DRAW, DT_BBS_ANY, DtSync8(84, 84, d, (DWORD)t, mi, nv, pc, DT_P(idx), (DWORD)fmt, DT_P(v), str));   // (the save's question: ahead of the drain, as the UP draw above)
        if (pc && nv && SUCCEEDED(hr)) DtShUpDraw(true);
        return hr;
    }
    const DWORD ipay = DtDw((UINT)ib), vpay = DtDw((UINT)vb), n = 9 + ipay + vpay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 8 | DT_SLOT(84), n);
    memcpy(p + 9, idx, (size_t)ib);
    memcpy(p + 9 + ipay, v, (size_t)vb);
    p[1] = (DWORD)t; p[2] = mi; p[3] = nv; p[4] = pc;
    p[5] = (DWORD)(UINT_PTR)(p + 9); p[6] = (DWORD)fmt; p[7] = (DWORD)(UINT_PTR)(p + 9 + ipay); p[8] = str;
    DtCommit(n);
    DtShUpDraw(true);
    return D3D_OK;
}

// Every recorder writes one header word and its arguments from p[1] (see DtCmdDev).
// The payload copy is inline for small payloads: up to sixteen whole 16-byte rows go as unaligned 16-byte loads and stores
// (exactly pay * 4 bytes, no tail, no over-read); larger ones and BOOL uploads (not whole rows) go to memcpy. A single row,
// the commonest upload, has its own branch and no loop.
static __forceinline void DtCopyPay(DWORD* dst, const void* src, DWORD pay)   // pay is in DWORDs, never 0 here
{
    if (pay == 4u) { _mm_storeu_si128((__m128i*)dst, _mm_loadu_si128((const __m128i*)src)); return; }
    if (pay <= 64u && !(pay & 3u))
    {
        const __m128i* s = (const __m128i*)src; __m128i* d = (__m128i*)dst;
        for (DWORD i = 0, rows = pay >> 2; i < rows; i++) _mm_storeu_si128(d + i, _mm_loadu_si128(s + i));
    }
    else memcpy(dst, src, pay * 4);
}

// ---- shader constants: the bulk of the traffic ----
// The limits are DXVK's with hardware vertex processing: 256 vertex and 224 pixel float registers, 16 integer and 16 boolean.
// Past them DXVK refuses, so the call goes to it as a SYNC.
#define DT_REC_CONST(slot, name, T, limit, dwPer)                                            \
    typedef HRESULT(__stdcall* dtT_##name)(IDirect3DDevice9*, UINT, const T*, UINT);         \
    static HRESULT __stdcall dt_##name(IDirect3DDevice9* d, UINT start, const T* data, UINT count) \
    {                                                                                        \
        if (!DtRecFor(d)) return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot])(d, start, data, count)); \
        if (start > (limit) || count > (limit) - start || (count && !data))                  \
            return DtSync3(slot, slot, d, start, DT_P(data), count);                         \
        const DWORD pay = count * (dwPer), n = 4 + pay;                                      \
        DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(slot), n);                                 \
        if (pay) DtCopyPay(p + 4, data, pay);   /* count 0 still writes nothing */          \
        p[1] = start; p[2] = (DWORD)(UINT_PTR)(p + 4); p[3] = count;                         \
        DtCommit(n);                                                                         \
        return D3D_OK;                                                                       \
    }
DT_REC_CONST(94,  SetVertexShaderConstantF, float, 256u, 4u)
DT_REC_CONST(96,  SetVertexShaderConstantI, int,   16u,  4u)
DT_REC_CONST(98,  SetVertexShaderConstantB, BOOL,  16u,  1u)
// The pixel float constants: the macro above plus one line. Once something has asked for one (dtShConsts, dt_shadow.h) the
// copy follows every upload; until then one load and a settled branch. Not inside a state block's recording, where DXVK
// sends the upload to the block (SetShaderConstants: ShouldRecord).
typedef HRESULT(__stdcall* dtT_SetPixelShaderConstantF)(IDirect3DDevice9*, UINT, const float*, UINT);
static HRESULT __stdcall dt_SetPixelShaderConstantF(IDirect3DDevice9* d, UINT start, const float* data, UINT count)
{
    if (!DtRecFor(d))
    {
        const HRESULT hr = DT_ORDERED_TRK(d, 109, ((dtT_SetPixelShaderConstantF)dtDx[109])(d, start, data, count));
        if (dtShConsts && SUCCEEDED(hr) && count && data && start < 224u && count <= 224u - start && DtShMine(d) && !dtSbRec)
            DtShPscFollow(start, data, count);
        DT_SBW_DIR(hr, d, DtSbWinPsc(d, start, data, count))
        return hr;
    }
    if (start > 224u || count > 224u - start || (count && !data))
        { DT_SBW_SYNC(d, DT_SBT_REFUSED) return DtSync3(109, 109, d, start, DT_P(data), count); }
    const DWORD pay = count * 4u, n = 4 + pay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(109), n);
    if (pay) DtCopyPay(p + 4, data, pay);   /* count 0 still writes nothing */
    if (dtShConsts && pay && !dtSbRec) DtShPscFollow(start, data, count);
    DT_SBW_REC(DtSbWinPsc(d, start, data, count))
    p[1] = start; p[2] = (DWORD)(UINT_PTR)(p + 4); p[3] = count;
    DtCommit(n);
    return D3D_OK;
}
DT_REC_CONST(111, SetPixelShaderConstantI,  int,   16u,  4u)
DT_REC_CONST(113, SetPixelShaderConstantB,  BOOL,  16u,  1u)

// ---- fixed-function state with a struct argument ------------------------------------
#define DT_REC_STRUCT(slot, name, A1, S)                                                 \
    typedef HRESULT(__stdcall* dtT_##name)(IDirect3DDevice9*, A1, const S*);            \
    static HRESULT __stdcall dt_##name(IDirect3DDevice9* d, A1 a, const S* s)           \
    {                                                                                   \
        if (!DtRecFor(d)) return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot])(d, a, s)); \
        DWORD av; static_assert(sizeof(A1) == 4, "one 32-bit argument"); memcpy(&av, &a, 4); \
        if (!s) return DtSync2(slot, slot, d, av, DT_P(s));                             \
        const DWORD pay = DtDw(sizeof(S)), n = 3 + pay;                                 \
        DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(slot), n);                            \
        memcpy(p + 3, s, sizeof(S));                                                    \
        p[1] = av; p[2] = (DWORD)(UINT_PTR)(p + 3);                                     \
        DtCommit(n);                                                                    \
        return D3D_OK;                                                                  \
    }
DT_REC_STRUCT(44, SetTransform,      D3DTRANSFORMSTATETYPE, D3DMATRIX)
DT_REC_STRUCT(46, MultiplyTransform, D3DTRANSFORMSTATETYPE, D3DMATRIX)
DT_REC_STRUCT(51, SetLight,          DWORD, D3DLIGHT9)

// A palette is 256 entries, always (d3d9.h: SetPaletteEntries reads 256).
typedef HRESULT(__stdcall* dtT_SetPaletteEntries)(IDirect3DDevice9*, UINT, const PALETTEENTRY*);
static HRESULT __stdcall dt_SetPaletteEntries(IDirect3DDevice9* d, UINT n, const PALETTEENTRY* e)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 71, ((dtT_SetPaletteEntries)dtDx[71])(d, n, e));
    if (!e) return DtSync2(71, 71, d, n, DT_P(e));
    const DWORD pay = 256, n2 = 3 + pay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(71), n2);
    memcpy(p + 3, e, 256 * sizeof(PALETTEENTRY));
    p[1] = n; p[2] = (DWORD)(UINT_PTR)(p + 3);
    DtCommit(n2);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_SetClipPlane)(IDirect3DDevice9*, DWORD, const float*);
static HRESULT __stdcall dt_SetClipPlane(IDirect3DDevice9* d, DWORD i, const float* pl)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 55, ((dtT_SetClipPlane)dtDx[55])(d, i, pl));
    if (!pl) return DtSync2(55, 55, d, i, DT_P(pl));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(55), 7);
    memcpy(p + 3, pl, 16);
    p[1] = i; p[2] = (DWORD)(UINT_PTR)(p + 3);
    DtCommit(7);
    return D3D_OK;
}

#define DT_REC_PSTRUCT(slot, name, S)                                               \
    typedef HRESULT(__stdcall* dtT_##name)(IDirect3DDevice9*, const S*);            \
    static HRESULT __stdcall dt_##name(IDirect3DDevice9* d, const S* s)             \
    {                                                                               \
        if (!DtRecFor(d)) return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot])(d, s));    \
        if (!s) return DtSync1(slot, slot, d, DT_P(s));                             \
        const DWORD pay = DtDw(sizeof(S)), n = 2 + pay;                             \
        DWORD* p = DtCmdDev(DT_OP_CALL0 + 1 | DT_SLOT(slot), n);                        \
        memcpy(p + 2, s, sizeof(S));                                                \
        p[1] = (DWORD)(UINT_PTR)(p + 2);                                            \
        DtCommit(n);                                                                \
        return D3D_OK;                                                              \
    }
DT_REC_PSTRUCT(49, SetMaterial,   D3DMATERIAL9)
DT_REC_PSTRUCT(62, SetClipStatus, D3DCLIPSTATUS9)

typedef void(__stdcall* dtT_SetGammaRamp)(IDirect3DDevice9*, UINT, DWORD, const D3DGAMMARAMP*);
static void __stdcall dt_SetGammaRamp(IDirect3DDevice9* d, UINT sc, DWORD f, const D3DGAMMARAMP* r)
{
    if (!DtRecFor(d)) { DT_ORDERED_V(d, 21, ((dtT_SetGammaRamp)dtDx[21])(d, sc, f, r)); return; }
    if (!r) { DtSync3(21, 21, d, sc, f, DT_P(r)); return; }   // void through an HRESULT pointer: eax is ignored, the stack is the callee's
    const DWORD pay = DtDw(sizeof(D3DGAMMARAMP)), n = 4 + pay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_F_VOID | DT_SLOT(21), n);
    memcpy(p + 4, r, sizeof(D3DGAMMARAMP));
    p[1] = sc; p[2] = f; p[3] = (DWORD)(UINT_PTR)(p + 4);
    DtCommit(n);
}

// ---- copies and clears ----------------------------------------------------------------
// A RECT or POINT argument that may be null: copied after the arguments when it
// is not, and the recorded argument points at the copy.
static __forceinline DWORD DtOpt(DWORD* p, DWORD& at, const void* src, UINT bytes)
{
    if (!src) return 0;
    memcpy(p + at, src, bytes);
    const DWORD a = (DWORD)(UINT_PTR)(p + at);
    at += DtDw(bytes);
    return a;
}

typedef HRESULT(__stdcall* dtT_Clear)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
static HRESULT __stdcall dt_Clear(IDirect3DDevice9* d, DWORD cnt, const D3DRECT* r, DWORD f, D3DCOLOR c, float z, DWORD st)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE((f & D3DCLEAR_TARGET) != 0, d, DT_BBS_CLEAR, nullptr, DT_ORDERED(d, 43, ((dtT_Clear)dtDx[43])(d, cnt, r, f, c, z, st)));
    const unsigned long long pay64 = r ? (unsigned long long)cnt * 4 : 0;
    if (pay64 > DT_MAX_CMD_DW - 16) return DT_BBS_BESIDE((f & D3DCLEAR_TARGET) != 0, d, DT_BBS_CLEAR, DT_BBS_ANY, DtSync6(43, 43, d, cnt, DT_P(r), f, c, DtFbits(z), st));
    const DWORD pay = (DWORD)pay64, n = 7 + pay;
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 6 | DT_SLOT(43), n);
        // A list stays a list, even an empty one: DXVK answers Clear(0, list) with D3D_OK and clears nothing, while Clear(0, NULL)
    // clears the whole viewport.
    if (pay) memcpy(p + 7, r, (size_t)pay * 4);
    p[1] = cnt; p[2] = r ? (DWORD)(UINT_PTR)(p + 7) : 0; p[3] = f; p[4] = c;
    memcpy(&p[5], &z, 4); p[6] = st;
    DtCommit(n);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_StretchRect)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
static HRESULT __stdcall dt_StretchRect(IDirect3DDevice9* d, IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* t, const RECT* tr, D3DTEXTUREFILTERTYPE f)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE(true, d, DT_BBS_STRETCH, t, DT_ORDERED(d, 34, ((dtT_StretchRect)dtDx[34])(d, s, sr, t, tr, f)));
    const DWORD n = 6 + (sr ? 4 : 0) + (tr ? 4 : 0);
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 5 | DT_SLOT(34), n);
    DWORD at = 6;
    p[1] = (DWORD)(UINT_PTR)s; p[2] = DtOpt(p, at, sr, sizeof(RECT));
    p[3] = (DWORD)(UINT_PTR)t; p[4] = DtOpt(p, at, tr, sizeof(RECT)); p[5] = (DWORD)f;
    DtCommit(n);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_ColorFill)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, D3DCOLOR);
static HRESULT __stdcall dt_ColorFill(IDirect3DDevice9* d, IDirect3DSurface9* s, const RECT* r, D3DCOLOR c)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE(true, d, DT_BBS_FILL, s, DT_ORDERED(d, 35, ((dtT_ColorFill)dtDx[35])(d, s, r, c)));
    const DWORD n = 4 + (r ? 4 : 0);
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 3 | DT_SLOT(35), n);
    DWORD at = 4;
    p[1] = (DWORD)(UINT_PTR)s; p[2] = DtOpt(p, at, r, sizeof(RECT)); p[3] = c;
    DtCommit(n);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_UpdateSurface)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*);
static HRESULT __stdcall dt_UpdateSurface(IDirect3DDevice9* d, IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* t, const POINT* tp)
{
    if (!DtRecFor(d)) return DT_BBS_BESIDE(true, d, DT_BBS_UPDATE, t, DT_ORDERED(d, 30, ((dtT_UpdateSurface)dtDx[30])(d, s, sr, t, tp)));
    const DWORD n = 5 + (sr ? 4 : 0) + (tp ? 2 : 0);
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 4 | DT_SLOT(30), n);
    DWORD at = 5;
    p[1] = (DWORD)(UINT_PTR)s; p[2] = DtOpt(p, at, sr, sizeof(RECT));
    p[3] = (DWORD)(UINT_PTR)t; p[4] = DtOpt(p, at, tp, sizeof(POINT));
    DtCommit(n);
    return D3D_OK;
}

typedef HRESULT(__stdcall* dtT_UpdateTexture)(IDirect3DDevice9*, IDirect3DBaseTexture9*, IDirect3DBaseTexture9*);
static HRESULT __stdcall dt_UpdateTexture(IDirect3DDevice9* d, IDirect3DBaseTexture9* s, IDirect3DBaseTexture9* t)
{
    if (!DtRecFor(d)) return DT_ORDERED(d, 31, ((dtT_UpdateTexture)dtDx[31])(d, s, t));
    DWORD* p = DtCmdDev(DT_OP_CALL0 + 2 | DT_SLOT(31), 3);
    p[1] = (DWORD)(UINT_PTR)s; p[2] = (DWORD)(UINT_PTR)t;
    DtCommit(3);
    return D3D_OK;
}
