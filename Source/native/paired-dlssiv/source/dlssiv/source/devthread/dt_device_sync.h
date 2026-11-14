// ===== THE DEVICE THREAD, part 4 of 5: shadow Gets, drained calls, the table ===
#pragma once

static void DtDisarm(const char* why);
DEVSTAT(static LONG dtValGets = 0;)   // threaded=2's: the development report's and the harness's

// ---- the shadow Gets ----
// Each answers from the copy when the entry is known; otherwise it is fetched from DXVK directly without a drain, kept and
// marked known (dt_shadow.h says why that is exact). A parameter DXVK would refuse goes to it as a SYNC. threaded=2 also asks
// DXVK, after draining, for every answer from the copy and hands back DXVK's, logging any difference: the proof the copy is
// exact. The validation drain is out of line (DtSync*), so no exception frame here.

typedef HRESULT(__stdcall* dtT_GetRenderState)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD*);
static HRESULT __stdcall dt_GetRenderState(IDirect3DDevice9* d, D3DRENDERSTATETYPE s, DWORD* v)
{
    if (!DtRecFor(d)) return ((dtT_GetRenderState)dtDx[58])(d, s, v);
    if (v && (DWORD)s < 256)
    {
        DtShadowReady(d);
        if (!dtSh.rsOk[s])
        {
            DtShFetchGate(!(dtBodyRsTouched[(DWORD)s >> 5] >> ((DWORD)s & 31) & 1u));
            const HRESULT hr = ((dtT_GetRenderState)dtDx[58])(d, s, v);
            if (SUCCEEDED(hr)) { dtSh.rs[s] = *v; dtSh.rsOk[s] = true; DEVSTAT(dtShFetched++;) }
            return hr;
        }
        const DWORD ours = dtSh.rs[s];
        if (!dtValidate) { *v = ours; return D3D_OK; }
        const HRESULT vhr = DtSync2(DT_R_VALIDATE, 58, d, (DWORD)s, DT_P(v)); DEVSTAT(dtValGets++;)
        if (FAILED(vhr) || *v != ours) DtMismatch("GetRenderState", (int)s, ours, *v);
        return vhr;
    }
    return DtSync2(58, 58, d, (DWORD)s, DT_P(v));
}

typedef HRESULT(__stdcall* dtT_GetSamplerState)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD*);
static HRESULT __stdcall dt_GetSamplerState(IDirect3DDevice9* d, DWORD smp, D3DSAMPLERSTATETYPE t, DWORD* v)
{
    if (!DtRecFor(d)) return ((dtT_GetSamplerState)dtDx[68])(d, smp, t, v);
    const int i = DtSampIdx(smp);
    if (v && i >= 0 && (DWORD)t >= 1 && (DWORD)t <= 13)
    {
        DtShadowReady(d);
        if (!dtSh.sampOk[i][t])
        {
            DtShFetchGate((dtBodySampFree >> i & 1u) != 0);
            const HRESULT hr = ((dtT_GetSamplerState)dtDx[68])(d, smp, t, v);
            if (SUCCEEDED(hr)) { dtSh.samp[i][t] = *v; dtSh.sampOk[i][t] = true; DEVSTAT(dtShFetched++;) }
            return hr;
        }
        const DWORD ours = dtSh.samp[i][t];
        if (!dtValidate) { *v = ours; return D3D_OK; }
        const HRESULT vhr = DtSync3(DT_R_VALIDATE, 68, d, smp, (DWORD)t, DT_P(v)); DEVSTAT(dtValGets++;)
        if (FAILED(vhr) || *v != ours) DtMismatch("GetSamplerState", i * 100 + (int)t, ours, *v);
        return vhr;
    }
    return DtSync3(68, 68, d, smp, (DWORD)t, DT_P(v));
}

// A pointer Get: the copy's pointer with a reference, as DXVK would hand it out.
// `none` is what DXVK answers for an empty slot: D3D_OK for textures, shaders and
// indices, D3DERR_NOTFOUND for a render target or depth surface.
template <class T> static HRESULT DtShPtr(T* ours, T** pp, HRESULT none)
{
    *pp = ours;
    if (!ours) return none;
    ours->AddRef();
    return D3D_OK;
}
template <class T> static HRESULT DtShPtrCheck(const char* what, int idx, T* ours, HRESULT none, T** pp, HRESULT hr)
{
    // pp already holds DXVK's answer, with DXVK's reference; hr is DXVK's result.
    if (hr != (ours ? D3D_OK : none) || *pp != ours) DtMismatch(what, idx, (DWORD)(UINT_PTR)ours, (DWORD)(UINT_PTR)*pp);
    return hr;
}
// A pointer entry fetched on demand: DXVK's reference goes to the caller, the copy
// keeps the raw pointer as its binding does. `none` for an empty slot is known too.
template <class T> static __forceinline bool DtShFetched(HRESULT hr, HRESULT none, T** pp, T** slot, bool* ok)
{
    if (SUCCEEDED(hr)) *slot = *pp;
    else if (hr == none && none != D3D_OK) *slot = nullptr;
    else return false;
    *ok = true; DEVSTAT(dtShFetched++;)
    return true;
}

typedef HRESULT(__stdcall* dtT_GetTexture)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9**);
static HRESULT __stdcall dt_GetTexture(IDirect3DDevice9* d, DWORD stage, IDirect3DBaseTexture9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetTexture)dtDx[64])(d, stage, pp);
    const int i = DtSampIdx(stage);
    if (!pp || i < 0) return DtSync2(64, 64, d, stage, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.texOk[i])
    {
        DtShFetchGate((dtBodySampFree >> i & 1u) != 0);
        const HRESULT hr = ((dtT_GetTexture)dtDx[64])(d, stage, pp);
        DtShFetched(hr, D3D_OK, pp, &dtSh.tex[i], &dtSh.texOk[i]);
        return hr;
    }
    IDirect3DBaseTexture9* const ours = dtSh.tex[i];
    if (!dtValidate) return DtShPtr(ours, pp, D3D_OK);
    const HRESULT vhr = DtSync2(DT_R_VALIDATE, 64, d, stage, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetTexture", i, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetRenderTarget)(IDirect3DDevice9*, DWORD, IDirect3DSurface9**);
static HRESULT __stdcall dt_GetRenderTarget(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetRenderTarget)dtDx[38])(d, i, pp);
    if (!pp || i >= 4) return DtSync2(38, 38, d, i, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.rtOk[i])
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetRenderTarget)dtDx[38])(d, i, pp);
        DtShFetched(hr, D3DERR_NOTFOUND, pp, &dtSh.rt[i], &dtSh.rtOk[i]);
        return hr;
    }
    IDirect3DSurface9* const ours = dtSh.rt[i];
    if (!dtValidate) return DtShPtr(ours, pp, D3DERR_NOTFOUND);
    const HRESULT vhr = DtSync2(DT_R_VALIDATE, 38, d, i, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetRenderTarget", (int)i, ours, D3DERR_NOTFOUND, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetDepthStencilSurface)(IDirect3DDevice9*, IDirect3DSurface9**);
static HRESULT __stdcall dt_GetDepthStencilSurface(IDirect3DDevice9* d, IDirect3DSurface9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetDepthStencilSurface)dtDx[40])(d, pp);
    if (!pp) return DtSync1(40, 40, d, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.dsOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetDepthStencilSurface)dtDx[40])(d, pp);
        DtShFetched(hr, D3DERR_NOTFOUND, pp, &dtSh.ds, &dtSh.dsOk);
        return hr;
    }
    IDirect3DSurface9* const ours = dtSh.ds;
    if (!dtValidate) return DtShPtr(ours, pp, D3DERR_NOTFOUND);
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 40, d, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetDepthStencilSurface", 0, ours, D3DERR_NOTFOUND, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetVertexShader)(IDirect3DDevice9*, IDirect3DVertexShader9**);
static HRESULT __stdcall dt_GetVertexShader(IDirect3DDevice9* d, IDirect3DVertexShader9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetVertexShader)dtDx[93])(d, pp);
    if (!pp) return DtSync1(93, 93, d, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.vsOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetVertexShader)dtDx[93])(d, pp);
        DtShFetched(hr, D3D_OK, pp, &dtSh.vs, &dtSh.vsOk);
        return hr;
    }
    IDirect3DVertexShader9* const ours = dtSh.vs;
    if (!dtValidate) return DtShPtr(ours, pp, D3D_OK);
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 93, d, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetVertexShader", 0, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetPixelShader)(IDirect3DDevice9*, IDirect3DPixelShader9**);
static HRESULT __stdcall dt_GetPixelShader(IDirect3DDevice9* d, IDirect3DPixelShader9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetPixelShader)dtDx[108])(d, pp);
    if (!pp) return DtSync1(108, 108, d, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.psOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetPixelShader)dtDx[108])(d, pp);
        DtShFetched(hr, D3D_OK, pp, &dtSh.ps, &dtSh.psOk);
        return hr;
    }
    IDirect3DPixelShader9* const ours = dtSh.ps;
    if (!dtValidate) return DtShPtr(ours, pp, D3D_OK);
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 108, d, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetPixelShader", 0, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetIndices)(IDirect3DDevice9*, IDirect3DIndexBuffer9**);
static HRESULT __stdcall dt_GetIndices(IDirect3DDevice9* d, IDirect3DIndexBuffer9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetIndices)dtDx[105])(d, pp);
    if (!pp) return DtSync1(105, 105, d, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.ibOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetIndices)dtDx[105])(d, pp);
        DtShFetched(hr, D3D_OK, pp, &dtSh.ib, &dtSh.ibOk);
        return hr;
    }
    IDirect3DIndexBuffer9* const ours = dtSh.ib;
    if (!dtValidate) return DtShPtr(ours, pp, D3D_OK);
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 105, d, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetIndices", 0, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetStreamSource)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9**, UINT*, UINT*);
static HRESULT __stdcall dt_GetStreamSource(IDirect3DDevice9* d, UINT i, IDirect3DVertexBuffer9** pp, UINT* off, UINT* str)
{
    if (!DtRecFor(d)) return ((dtT_GetStreamSource)dtDx[101])(d, i, pp, off, str);
    if (!pp || !off || !str || i >= 16) return DtSync4(101, 101, d, i, DT_P(pp), DT_P(off), DT_P(str));
    DtShadowReady(d);
    if (!dtSh.vbOk[i])
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetStreamSource)dtDx[101])(d, i, pp, off, str);
        if (DtShFetched(hr, D3D_OK, pp, &dtSh.vb[i], &dtSh.vbOk[i])) { dtSh.vbOff[i] = *off; dtSh.vbStr[i] = *str; }
        return hr;
    }
    IDirect3DVertexBuffer9* const ours = dtSh.vb[i];
    const UINT oOff = dtSh.vbOff[i], oStr = dtSh.vbStr[i];
    if (!dtValidate) { *off = oOff; *str = oStr; return DtShPtr(ours, pp, D3D_OK); }
    const HRESULT vhr = DtSync4(DT_R_VALIDATE, 101, d, i, DT_P(pp), DT_P(off), DT_P(str)); DEVSTAT(dtValGets++;)
    if (*off != oOff || *str != oStr) DtMismatch("GetStreamSource offset/stride", (int)i, oOff << 16 | (oStr & 0xFFFF), *off << 16 | (*str & 0xFFFF));
    return DtShPtrCheck("GetStreamSource", (int)i, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetViewport)(IDirect3DDevice9*, D3DVIEWPORT9*);
static HRESULT __stdcall dt_GetViewport(IDirect3DDevice9* d, D3DVIEWPORT9* vp)
{
    if (!DtRecFor(d)) return ((dtT_GetViewport)dtDx[48])(d, vp);
    if (!vp) return DtSync1(48, 48, d, DT_P(vp));
    DtShadowReady(d);
    if (!dtSh.vpOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetViewport)dtDx[48])(d, vp);
        if (SUCCEEDED(hr)) { dtSh.vp = *vp; dtSh.vpOk = true; DEVSTAT(dtShFetched++;) }
        return hr;
    }
    const D3DVIEWPORT9 ours = dtSh.vp;
    if (!dtValidate) { *vp = ours; return D3D_OK; }
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 48, d, DT_P(vp)); DEVSTAT(dtValGets++;)
    if (FAILED(vhr) || memcmp(vp, &ours, sizeof(ours)))
    {
        DtMismatch("GetViewport (width<<16 | height)", 0, ours.Width << 16 | (ours.Height & 0xFFFF), vp->Width << 16 | (vp->Height & 0xFFFF));
        if (dtValLogged < 40)
            LogRecord("    the copy said %lu,%lu %lux%lu z %.3f-%.3f; DXVK says %lu,%lu %lux%lu z %.3f-%.3f",
                ours.X, ours.Y, ours.Width, ours.Height, ours.MinZ, ours.MaxZ, vp->X, vp->Y, vp->Width, vp->Height, vp->MinZ, vp->MaxZ);
    }
    return vhr;
}

typedef HRESULT(__stdcall* dtT_GetScissorRect)(IDirect3DDevice9*, RECT*);
static HRESULT __stdcall dt_GetScissorRect(IDirect3DDevice9* d, RECT* r)
{
    if (!DtRecFor(d)) return ((dtT_GetScissorRect)dtDx[76])(d, r);
    if (!r) return DtSync1(76, 76, d, DT_P(r));
    DtShadowReady(d);
    if (!dtSh.scOk)
    {
        DtShFetchGate(false);
        const HRESULT hr = ((dtT_GetScissorRect)dtDx[76])(d, r);
        if (SUCCEEDED(hr)) { dtSh.sc = *r; dtSh.scOk = true; DEVSTAT(dtShFetched++;) }
        return hr;
    }
    const RECT ours = dtSh.sc;
    if (!dtValidate) { *r = ours; return D3D_OK; }
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 76, d, DT_P(r)); DEVSTAT(dtValGets++;)
    if (FAILED(vhr) || memcmp(r, &ours, sizeof(ours)))
        DtMismatch("GetScissorRect (right,bottom)", 0, (DWORD)(ours.right << 16 | (ours.bottom & 0xFFFF)), (DWORD)(r->right << 16 | (r->bottom & 0xFFFF)));
    return vhr;
}

// ---- the declaration, the FVF and the pixel constants ----
// dt_shadow.h says what keeps them exact. None is fetched beside the ring: each answers from the copy or drains.
typedef HRESULT(__stdcall* dtT_GetVertexDeclaration)(IDirect3DDevice9*, IDirect3DVertexDeclaration9**);
static HRESULT __stdcall dt_GetVertexDeclaration(IDirect3DDevice9* d, IDirect3DVertexDeclaration9** pp)
{
    if (!DtRecFor(d)) return ((dtT_GetVertexDeclaration)dtDx[88])(d, pp);
    if (!pp) return DtSync1(88, 88, d, DT_P(pp));
    DtShadowReady(d);
    if (!dtSh.declOk)
    {
        // Behind a recorded SetFVF (declQueued), or a reload DXVK refused. DXVK's reference
        // goes to the caller; and the object an FVF bound is now one whose FVF is known.
        DT_SB_FVF_ASK()
        const HRESULT hr = DtSync1(88, 88, d, DT_P(pp));
        if (SUCCEEDED(hr))
        {
            DT_SB_FVF_LEARNED(*pp)
            dtSh.decl = *pp; dtSh.declOk = true; dtSh.declQueued = false; DEVSTAT(dtShFetched++;)
            if (dtSh.fvfOk) DtShFvfKeep(*pp, dtSh.fvf);
        }
        return hr;
    }
    IDirect3DVertexDeclaration9* const ours = dtSh.decl;
    if (!dtValidate) return DtShPtr(ours, pp, D3D_OK);
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 88, d, DT_P(pp)); DEVSTAT(dtValGets++;)
    return DtShPtrCheck("GetVertexDeclaration", 0, ours, D3D_OK, pp, vhr);
}

typedef HRESULT(__stdcall* dtT_GetFVF)(IDirect3DDevice9*, DWORD*);
static HRESULT __stdcall dt_GetFVF(IDirect3DDevice9* d, DWORD* f)
{
    if (!DtRecFor(d)) return ((dtT_GetFVF)dtDx[90])(d, f);
    if (!f) return DtSync1(90, 90, d, DT_P(f));
    DtShadowReady(d);
    DWORD ours = 0; bool known = false;
    if (dtSh.fvfOk) { ours = dtSh.fvf; known = true; }
    else if (dtSh.declOk) known = !dtSh.decl || DtShFvfOf(dtSh.decl, &ours);   // no declaration is FVF 0 in DXVK
    if (!known)
    {
        // The first ask for this object. Drained, so DXVK's bound declaration IS the
        // copy's, and what it answers is that object's for as long as the object lives.
        const HRESULT hr = DtSync1(90, 90, d, DT_P(f)); DEVSTAT(dtShFvfAsked++;)
        if (SUCCEEDED(hr) && dtSh.declOk) DtShFvfKeep(dtSh.decl, *f);
        return hr;
    }
    if (!dtValidate) { *f = ours; return D3D_OK; }
    const HRESULT vhr = DtSync1(DT_R_VALIDATE, 90, d, DT_P(f)); DEVSTAT(dtValGets++;)
    if (FAILED(vhr) || *f != ours) DtMismatch("GetFVF", 0, ours, *f);
    return vhr;
}

// The first ask turns this on: drained, and with the ring empty all 224 are loaded, inside the scope so nothing is recorded
// between the load and the flag. Out of line: the scope's destructor must not give the wrapper an exception frame.
typedef HRESULT(__stdcall* dtT_GetPixelShaderConstantF)(IDirect3DDevice9*, UINT, float*, UINT);
static __declspec(noinline) HRESULT DtShConstsOn(IDirect3DDevice9* d, UINT s, float* p, UINT n)
{
    DtSyncScope s_(110);
    const HRESULT hr = ((dtT_GetPixelShaderConstantF)dtDx[110])(d, s, p, n);
    if (SUCCEEDED(((dtT_GetPixelShaderConstantF)dtDx[110])(d, 0, dtSh.psc[0], 224)))
    { memset(dtSh.pscOk, 0xFF, sizeof(dtSh.pscOk)); dtShConsts = true; }
    return hr;
}
static HRESULT __stdcall dt_GetPixelShaderConstantF(IDirect3DDevice9* d, UINT s, float* p, UINT n)
{
    if (!DtRecFor(d)) return ((dtT_GetPixelShaderConstantF)dtDx[110])(d, s, p, n);
    // What DXVK refuses, clamps or answers without reading goes to it for its own answer.
    if (!p || !n || s >= 224u || n > 224u - s) return DtSync3(110, 110, d, s, DT_P(p), n);
    if (!dtShConsts) return dtShBodiesEver ? DtShConstsOn(d, s, p, n) : DtSync3(110, 110, d, s, DT_P(p), n);   // dt_shadow.h: only where it can pay
    DtShadowReady(d);
    if (!DtShPscKnown(s, n))
    {
        // Only after a reload DXVK refused. Drained, so what it answers is the stream's.
        const HRESULT hr = DtSync3(110, 110, d, s, DT_P(p), n);
        if (SUCCEEDED(hr)) { DtShPscFollow(s, p, n); DEVSTAT(dtShFetched++;) }
        return hr;
    }
    if (!dtValidate) { memcpy(p, dtSh.psc[s], n * 16); return D3D_OK; }
    const HRESULT vhr = DtSync3(DT_R_VALIDATE, 110, d, s, DT_P(p), n); DEVSTAT(dtValGets++;)
    if (FAILED(vhr) || memcmp(p, dtSh.psc[s], n * 16))
    { DWORD a, b; memcpy(&a, dtSh.psc[s], 4); memcpy(&b, p, 4); DtMismatch("GetPixelShaderConstantF (first x, as bits)", (int)s, a, b); }
    return vhr;
}

// The three ways a declaration object comes to exist, two here (SetFVF is in dt_device_rec.h): each empties the per-object FVF
// table, from any thread, after DXVK returns and before the caller can name the new object.
typedef HRESULT(__stdcall* dtT_CreateVertexDeclaration)(IDirect3DDevice9*, const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**);
static HRESULT __stdcall dt_CreateVertexDeclaration(IDirect3DDevice9* d, const D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** pp)
{
    const HRESULT hr = DtRecFor(d) ? DtSync2(86, 86, d, DT_P(e), DT_P(pp)) : ((dtT_CreateVertexDeclaration)dtDx[86])(d, e, pp);
    _InterlockedIncrement(&dtDeclGen);
    return hr;
}
typedef HRESULT(__stdcall* dtT_ProcessVertices)(IDirect3DDevice9*, UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD);
static HRESULT __stdcall dt_ProcessVertices(IDirect3DDevice9* d, UINT si, UINT di, UINT vc, IDirect3DVertexBuffer9* vb, IDirect3DVertexDeclaration9* vd, DWORD f)
{
    const HRESULT hr = DtRecFor(d) ? DtSync6(85, 85, d, si, di, vc, DT_P(vb), DT_P(vd), f) : ((dtT_ProcessVertices)dtDx[85])(d, si, di, vc, vb, vd, f);
    _InterlockedIncrement(&dtDeclGen);
    return hr;
}

// ---- the drained calls and the direct ones ----
// SYNC: the ring drains, then DXVK answers here. PASS: DXVK answers at once, for what is fixed at creation (caps, creation
// parameters, the display mode between Resets) or a plain reference count. The swapchain and its back buffer are PASS too:
// DXVK's back buffer surfaces are fixed between Resets (a Present swaps the images inside them) and a Reset disarms, and
// EndScene asks for the back buffer every frame. Every PASS call reaches DXVK beside the worker under DXVK's own device lock:
// the layer arms only on a MULTITHREADED device (dt_install.h).
#define DT_SYNC(slot, R, name, PARAMS, ARGS)                                             \
    typedef R(__stdcall* dtT_##name) PARAMS;                                             \
    static R __stdcall dt_##name PARAMS                                                  \
    {                                                                                    \
        if (DtRecFor(d)) { DtSyncScope s_(slot); return ((dtT_##name)dtDx[slot]) ARGS; } \
        return ((dtT_##name)dtDx[slot]) ARGS;                                            \
    }
// A SYNC call another thread must not make ahead of the queue. CreateStateBlock is a Create that dt_core.h's rule does not
// cover: DXVK captures the device's state inside the constructor (d3d9_stateblock.cpp, CaptureType), so made ahead of the
// queue it snapshots the worker's position. The two patch draws draw. On the producer these are SYNC; from another thread
// they wait for what was queued before them (DT_ORDERED). Not the read-backs (GetRenderTargetData, GetFrontBufferData), which
// are Gets, nor the cursor and dialog calls, which are not render-stream state.
// The switch is the harness's negative control; in the mod it is the constant true.
#ifndef DT_SYNC_ORDERED_ON
#define DT_SYNC_ORDERED_ON() true
#endif
#define DT_SYNC_ORD(slot, name, PARAMS, ARGS)                                            \
    typedef HRESULT(__stdcall* dtT_##name) PARAMS;                                       \
    static HRESULT __stdcall dt_##name PARAMS                                            \
    {                                                                                    \
        if (DtRecFor(d)) { DtSyncScope s_(slot); return ((dtT_##name)dtDx[slot]) ARGS; } \
        if (!DT_SYNC_ORDERED_ON()) return ((dtT_##name)dtDx[slot]) ARGS;                 \
        return DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot]) ARGS);                       \
    }
#define DT_PASS(slot, R, name, PARAMS, ARGS)                                             \
    typedef R(__stdcall* dtT_##name) PARAMS;                                             \
    static R __stdcall dt_##name PARAMS                                                  \
    {                                                                                    \
        if (DtRecFor(d)) { DtPassScope p_; return ((dtT_##name)dtDx[slot]) ARGS; }       \
        return ((dtT_##name)dtDx[slot]) ARGS;                                            \
    }
// The save's question (devthread/dt_bbs.h), in a build whose product can arm: the device's own Present and the two patch draws
// are DT_SYNC and DT_SYNC_ORD with the call counted ahead and behind, as "whatever is bound", since the count is taken ahead of
// the drain. With the switch at 0 they are plain DT_SYNC and DT_SYNC_ORD.
#if DT_BBS_ON
#define DT_SYNC_BBS(slot, name, PARAMS, ARGS, KIND)                                      \
    typedef HRESULT(__stdcall* dtT_##name) PARAMS;                                       \
    static HRESULT __stdcall dt_##name PARAMS                                            \
    {                                                                                    \
        const int bbs_ = DtBbsEnter(d, KIND, DT_BBS_ANY);                                \
        HRESULT hr_;                                                                     \
        if (DtRecFor(d)) { DtSyncScope s_(slot); hr_ = ((dtT_##name)dtDx[slot]) ARGS; }  \
        else hr_ = ((dtT_##name)dtDx[slot]) ARGS;                                        \
        DtBbsLeave(bbs_);                                                                \
        return hr_;                                                                      \
    }
#define DT_SYNC_ORD_BBS(slot, name, PARAMS, ARGS, KIND)                                  \
    typedef HRESULT(__stdcall* dtT_##name) PARAMS;                                       \
    static HRESULT __stdcall dt_##name PARAMS                                            \
    {                                                                                    \
        const int bbs_ = DtBbsEnter(d, KIND, DT_BBS_ANY);                                \
        HRESULT hr_;                                                                     \
        if (DtRecFor(d)) { DtSyncScope s_(slot); hr_ = ((dtT_##name)dtDx[slot]) ARGS; }  \
        else if (!DT_SYNC_ORDERED_ON()) hr_ = ((dtT_##name)dtDx[slot]) ARGS;             \
        else hr_ = DT_ORDERED(d, slot, ((dtT_##name)dtDx[slot]) ARGS);                   \
        DtBbsLeave(bbs_);                                                                \
        return hr_;                                                                      \
    }
#endif
typedef IDirect3DDevice9 DtD;
DT_PASS(0,   HRESULT, QueryInterface, (DtD* d, REFIID r, void** pp), (d, r, pp))
DT_PASS(1,   ULONG,   AddRef, (DtD* d), (d))
DT_SYNC(3,   HRESULT, TestCooperativeLevel, (DtD* d), (d))
DT_SYNC(4,   UINT,    GetAvailableTextureMem, (DtD* d), (d))
DT_SYNC(5,   HRESULT, EvictManagedResources, (DtD* d), (d))
DT_PASS(6,   HRESULT, GetDirect3D, (DtD* d, IDirect3D9** pp), (d, pp))
DT_PASS(7,   HRESULT, GetDeviceCaps, (DtD* d, D3DCAPS9* c), (d, c))
DT_PASS(8,   HRESULT, GetDisplayMode, (DtD* d, UINT sc, D3DDISPLAYMODE* m), (d, sc, m))
DT_PASS(9,   HRESULT, GetCreationParameters, (DtD* d, D3DDEVICE_CREATION_PARAMETERS* p), (d, p))
DT_SYNC(10,  HRESULT, SetCursorProperties, (DtD* d, UINT x, UINT y, IDirect3DSurface9* s), (d, x, y, s))
DT_SYNC(11,  void,    SetCursorPosition, (DtD* d, int x, int y, DWORD f), (d, x, y, f))
DT_SYNC(12,  BOOL,    ShowCursor, (DtD* d, BOOL b), (d, b))
DT_SYNC(13,  HRESULT, CreateAdditionalSwapChain, (DtD* d, D3DPRESENT_PARAMETERS* pp, IDirect3DSwapChain9** sc), (d, pp, sc))
DT_PASS(14,  HRESULT, GetSwapChain, (DtD* d, UINT i, IDirect3DSwapChain9** sc), (d, i, sc))
DT_PASS(15,  UINT,    GetNumberOfSwapChains, (DtD* d), (d))
#if DT_BBS_ON
DT_SYNC_BBS(17, Present, (DtD* d, const RECT* s, const RECT* t, HWND w, const RGNDATA* r), (d, s, t, w, r), DT_BBS_PRESENT)
#else
DT_SYNC(17,  HRESULT, Present, (DtD* d, const RECT* s, const RECT* t, HWND w, const RGNDATA* r), (d, s, t, w, r))
#endif
DT_PASS(18,  HRESULT, GetBackBuffer, (DtD* d, UINT sc, UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** pp), (d, sc, i, t, pp))
DT_PASS(19,  HRESULT, GetRasterStatus, (DtD* d, UINT sc, D3DRASTER_STATUS* rs), (d, sc, rs))
DT_SYNC(20,  HRESULT, SetDialogBoxMode, (DtD* d, BOOL b), (d, b))
DT_SYNC(22,  void,    GetGammaRamp, (DtD* d, UINT sc, D3DGAMMARAMP* r), (d, sc, r))
DT_SYNC(23,  HRESULT, CreateTexture, (DtD* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** pp, HANDLE* sh), (d, w, h, l, u, f, p, pp, sh))
DT_SYNC(24,  HRESULT, CreateVolumeTexture, (DtD* d, UINT w, UINT h, UINT dp, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DVolumeTexture9** pp, HANDLE* sh), (d, w, h, dp, l, u, f, p, pp, sh))
DT_SYNC(25,  HRESULT, CreateCubeTexture, (DtD* d, UINT e, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DCubeTexture9** pp, HANDLE* sh), (d, e, l, u, f, p, pp, sh))
DT_SYNC(26,  HRESULT, CreateVertexBuffer, (DtD* d, UINT len, DWORD u, DWORD fvf, D3DPOOL p, IDirect3DVertexBuffer9** pp, HANDLE* sh), (d, len, u, fvf, p, pp, sh))
DT_SYNC(27,  HRESULT, CreateIndexBuffer, (DtD* d, UINT len, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DIndexBuffer9** pp, HANDLE* sh), (d, len, u, f, p, pp, sh))
DT_SYNC(28,  HRESULT, CreateRenderTarget, (DtD* d, UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL lk, IDirect3DSurface9** pp, HANDLE* sh), (d, w, h, f, ms, q, lk, pp, sh))
DT_SYNC(29,  HRESULT, CreateDepthStencilSurface, (DtD* d, UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL dc, IDirect3DSurface9** pp, HANDLE* sh), (d, w, h, f, ms, q, dc, pp, sh))
DT_SYNC(32,  HRESULT, GetRenderTargetData, (DtD* d, IDirect3DSurface9* s, IDirect3DSurface9* t), (d, s, t))
DT_SYNC(33,  HRESULT, GetFrontBufferData, (DtD* d, UINT sc, IDirect3DSurface9* t), (d, sc, t))
DT_SYNC(36,  HRESULT, CreateOffscreenPlainSurface, (DtD* d, UINT w, UINT h, D3DFORMAT f, D3DPOOL p, IDirect3DSurface9** pp, HANDLE* sh), (d, w, h, f, p, pp, sh))
DT_SYNC(45,  HRESULT, GetTransform, (DtD* d, D3DTRANSFORMSTATETYPE s, D3DMATRIX* m), (d, s, m))
DT_SYNC(50,  HRESULT, GetMaterial, (DtD* d, D3DMATERIAL9* m), (d, m))
DT_SYNC(52,  HRESULT, GetLight, (DtD* d, DWORD i, D3DLIGHT9* l), (d, i, l))
DT_SYNC(54,  HRESULT, GetLightEnable, (DtD* d, DWORD i, BOOL* b), (d, i, b))
DT_SYNC(56,  HRESULT, GetClipPlane, (DtD* d, DWORD i, float* p), (d, i, p))
// CreateStateBlock by hand: the same SYNC_ORD, and the new block made known where it is made, from any thread, armed or not
// (D3DX makes its blocks at an effect's first use): the state-block table (dt_sbtab.h) overwrites whatever it held at the
// new block's address with "typed, no record".
typedef HRESULT(__stdcall* dtT_CreateStateBlock)(DtD*, D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**);
static HRESULT __stdcall dt_CreateStateBlock(DtD* d, D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** pp)
{
    HRESULT hr;
    if (DtRecFor(d)) { DtSyncScope s_(59); hr = ((dtT_CreateStateBlock)dtDx[59])(d, t, pp); }
    else if (!DT_SYNC_ORDERED_ON()) hr = ((dtT_CreateStateBlock)dtDx[59])(d, t, pp);
    else hr = DT_ORDERED(d, 59, ((dtT_CreateStateBlock)dtDx[59])(d, t, pp));
    DT_SB_CREATED(d, hr, pp)
    return hr;
}
DT_SYNC(63,  HRESULT, GetClipStatus, (DtD* d, D3DCLIPSTATUS9* c), (d, c))
DT_SYNC(66,  HRESULT, GetTextureStageState, (DtD* d, DWORD s, D3DTEXTURESTAGESTATETYPE t, DWORD* v), (d, s, t, v))
DT_SYNC(70,  HRESULT, ValidateDevice, (DtD* d, DWORD* n), (d, n))
DT_SYNC(72,  HRESULT, GetPaletteEntries, (DtD* d, UINT n, PALETTEENTRY* e), (d, n, e))
DT_SYNC(74,  HRESULT, GetCurrentTexturePalette, (DtD* d, UINT* n), (d, n))
DT_SYNC(78,  BOOL,    GetSoftwareVertexProcessing, (DtD* d), (d))
DT_SYNC(80,  float,   GetNPatchMode, (DtD* d), (d))
DT_SYNC(91,  HRESULT, CreateVertexShader, (DtD* d, const DWORD* fn, IDirect3DVertexShader9** pp), (d, fn, pp))
DT_SYNC(95,  HRESULT, GetVertexShaderConstantF, (DtD* d, UINT s, float* p, UINT n), (d, s, p, n))
DT_SYNC(97,  HRESULT, GetVertexShaderConstantI, (DtD* d, UINT s, int* p, UINT n), (d, s, p, n))
DT_SYNC(99,  HRESULT, GetVertexShaderConstantB, (DtD* d, UINT s, BOOL* p, UINT n), (d, s, p, n))
DT_SYNC(103, HRESULT, GetStreamSourceFreq, (DtD* d, UINT i, UINT* f), (d, i, f))
DT_SYNC(106, HRESULT, CreatePixelShader, (DtD* d, const DWORD* fn, IDirect3DPixelShader9** pp), (d, fn, pp))
DT_SYNC(112, HRESULT, GetPixelShaderConstantI, (DtD* d, UINT s, int* p, UINT n), (d, s, p, n))
DT_SYNC(114, HRESULT, GetPixelShaderConstantB, (DtD* d, UINT s, BOOL* p, UINT n), (d, s, p, n))
#if DT_BBS_ON
DT_SYNC_ORD_BBS(115, DrawRectPatch, (DtD* d, UINT h, const float* s, const D3DRECTPATCH_INFO* i), (d, h, s, i), DT_BBS_PATCH)
DT_SYNC_ORD_BBS(116, DrawTriPatch, (DtD* d, UINT h, const float* s, const D3DTRIPATCH_INFO* i), (d, h, s, i), DT_BBS_PATCH)
#else
DT_SYNC_ORD(115, DrawRectPatch, (DtD* d, UINT h, const float* s, const D3DRECTPATCH_INFO* i), (d, h, s, i))
DT_SYNC_ORD(116, DrawTriPatch, (DtD* d, UINT h, const float* s, const D3DTRIPATCH_INFO* i), (d, h, s, i))
#endif
DT_SYNC(117, HRESULT, DeletePatch, (DtD* d, UINT h), (d, h))
DT_SYNC(118, HRESULT, CreateQuery, (DtD* d, D3DQUERYTYPE t, IDirect3DQuery9** pp), (d, t, pp))

// ---- the four with extra duties ----
// The device's own Release drains first: a queued call may still name the device. When the count reaches 0 the device is gone
// and the layer stops.
typedef ULONG(__stdcall* dtT_Release)(DtD*);
static const char* DtResetBarrier(DWORD* msOut);     // dt_install.h; dt_Release and dt_Reset both stand behind it
static ULONG __stdcall dt_Release(DtD* d)
{
    ULONG r;
    if (DtRecFor(d)) { DtSyncScope s_(2); r = ((dtT_Release)dtDx[2])(d); if (!r) DtDisarm("the game released the device"); }
    else
    {
                // The last Release from another thread: the ring may still name this device, its swapchain and children, and all of it must
        // reach DXVK before the device goes. This thread may not call DtDisarm (the producer's alone); the barrier knows its thread.
        // The AddRef probe (PASS, DXVK's own) says whether the caller's is the last reference, so an overlay's GetDevice/Release pair
        // does not disarm the layer. Balanced on every path.
        const DWORD me = __readfsdword(0x24);
        if (dtArmed && d == dtDev && me != dtProducer && me != dtWorkerTid && !dtDisarmAsk)
        {
            const ULONG probe = ((dtT_AddRef)dtDx[1])(d);   // the count, with the probe's in it
            ((dtT_Release)dtDx[2])(d);                      // the probe's back; never the last
            if (probe == 2) DtResetBarrier(nullptr);        // the caller's is the last one
        }
        r = ((dtT_Release)dtDx[2])(d);
        if (!r && d == dtDev && dtArmed && me == dtProducer) DtDisarm("the device was released");
    }
    DT_SB_DEV_GONE(d, r)
    return r;
}

// Reset ends the armed state: everything is drained, the Reset runs directly, and the layer arms again 300 presents later
// from a freshly loaded shadow. From any thread: DXVK refuses a Reset while a D3DPOOL_DEFAULT object lives
// (d3d9_device.cpp:513), so Releases waiting on the hand-over list must land first. hkReset runs DtResetBarrier() before its
// first Release, so this is the second line and normally a no-op. Not when an ask is already up: that is hkReset's own
// barrier having just stood aside.
typedef HRESULT(__stdcall* dtT_Reset)(DtD*, D3DPRESENT_PARAMETERS*);
static HRESULT __stdcall dt_Reset(DtD* d, D3DPRESENT_PARAMETERS* pp)
{
    DT_SB_RESET(d)
    if (dtArmed && d == dtDev && !dtDisarmAsk) DtResetBarrier(nullptr);
    DT_SB_RESET_CALL(d, ((dtT_Reset)dtDx[16])(d, pp))
}

// Between BeginStateBlock and EndStateBlock a Set is recorded into the block and does not change the device, so the shadow
// must not follow it (dtSbRec). Another thread's Begin and End are ordered like its Sets (DT_ORDERED): otherwise the
// producer's earlier queued Sets would be replayed into that thread's block. What the producer queues after that Begin goes
// into the block natively too: the caller's race.
typedef HRESULT(__stdcall* dtT_BeginStateBlock)(DtD*);
static HRESULT __stdcall dt_BeginStateBlock(DtD* d)
{
    DT_SB_BEGIN_OPEN(d)                     // the state-block window, before DXVK records (dt_sbtab.h)
    HRESULT hr;
    if (DtRecFor(d)) { DtSyncScope s_(60); hr = ((dtT_BeginStateBlock)dtDx[60])(d); }
    else if (!DT_SYNC_ORDERED_ON()) hr = ((dtT_BeginStateBlock)dtDx[60])(d);
    else hr = DT_ORDERED(d, 60, ((dtT_BeginStateBlock)dtDx[60])(d));
    DT_SB_BEGIN_DONE(d, hr)
    if (SUCCEEDED(hr) && dtArmed && d == dtDev && __readfsdword(0x24) == dtProducer) dtSbRec = true;
    return hr;
}
typedef HRESULT(__stdcall* dtT_EndStateBlock)(DtD*, IDirect3DStateBlock9**);
static HRESULT __stdcall dt_EndStateBlock(DtD* d, IDirect3DStateBlock9** pp)
{
    HRESULT hr;
    if (DtRecFor(d)) { DtSyncScope s_(61); hr = ((dtT_EndStateBlock)dtDx[61])(d, pp); }
    else if (!DT_SYNC_ORDERED_ON()) hr = ((dtT_EndStateBlock)dtDx[61])(d, pp);
    else hr = DT_ORDERED(d, 61, ((dtT_EndStateBlock)dtDx[61])(d, pp));    // ordered, as Begin above
    DT_SB_END_DONE(d, hr, pp)               // the state-block window judged and its record written (dt_sbtab.h)
        // Only when it ended: DXVK 3.1 refuses EndStateBlock(nullptr) and goes on recording.
    if (SUCCEEDED(hr) && dtArmed && d == dtDev && __readfsdword(0x24) == dtProducer) dtSbRec = false;
    return hr;
}

// ---- the table: slot i of IDirect3DDevice9 -> its wrapper --------------------------
static void* const dtDevFn[119] = {
    (void*)&dt_QueryInterface, (void*)&dt_AddRef, (void*)&dt_Release, (void*)&dt_TestCooperativeLevel,          //   0-3
    (void*)&dt_GetAvailableTextureMem, (void*)&dt_EvictManagedResources, (void*)&dt_GetDirect3D,               //   4-6
    (void*)&dt_GetDeviceCaps, (void*)&dt_GetDisplayMode, (void*)&dt_GetCreationParameters,                     //   7-9
    (void*)&dt_SetCursorProperties, (void*)&dt_SetCursorPosition, (void*)&dt_ShowCursor,                       //  10-12
    (void*)&dt_CreateAdditionalSwapChain, (void*)&dt_GetSwapChain, (void*)&dt_GetNumberOfSwapChains,          //  13-15
    (void*)&dt_Reset, (void*)&dt_Present, (void*)&dt_GetBackBuffer, (void*)&dt_GetRasterStatus,                //  16-19
    (void*)&dt_SetDialogBoxMode, (void*)&dt_SetGammaRamp, (void*)&dt_GetGammaRamp, (void*)&dt_CreateTexture,   //  20-23
    (void*)&dt_CreateVolumeTexture, (void*)&dt_CreateCubeTexture, (void*)&dt_CreateVertexBuffer,               //  24-26
    (void*)&dt_CreateIndexBuffer, (void*)&dt_CreateRenderTarget, (void*)&dt_CreateDepthStencilSurface,         //  27-29
    (void*)&dt_UpdateSurface, (void*)&dt_UpdateTexture, (void*)&dt_GetRenderTargetData,                        //  30-32
    (void*)&dt_GetFrontBufferData, (void*)&dt_StretchRect, (void*)&dt_ColorFill,                               //  33-35
    (void*)&dt_CreateOffscreenPlainSurface, (void*)&dt_SetRenderTarget, (void*)&dt_GetRenderTarget,           //  36-38
    (void*)&dt_SetDepthStencilSurface, (void*)&dt_GetDepthStencilSurface, (void*)&dt_BeginScene,              //  39-41
    (void*)&dt_EndScene, (void*)&dt_Clear, (void*)&dt_SetTransform, (void*)&dt_GetTransform,                   //  42-45
    (void*)&dt_MultiplyTransform, (void*)&dt_SetViewport, (void*)&dt_GetViewport, (void*)&dt_SetMaterial,      //  46-49
    (void*)&dt_GetMaterial, (void*)&dt_SetLight, (void*)&dt_GetLight, (void*)&dt_LightEnable,                  //  50-53
    (void*)&dt_GetLightEnable, (void*)&dt_SetClipPlane, (void*)&dt_GetClipPlane, (void*)&dt_SetRenderState,    //  54-57
    (void*)&dt_GetRenderState, (void*)&dt_CreateStateBlock, (void*)&dt_BeginStateBlock,                        //  58-60
    (void*)&dt_EndStateBlock, (void*)&dt_SetClipStatus, (void*)&dt_GetClipStatus, (void*)&dt_GetTexture,      //  61-64
    (void*)&dt_SetTexture, (void*)&dt_GetTextureStageState, (void*)&dt_SetTextureStageState,                  //  65-67
    (void*)&dt_GetSamplerState, (void*)&dt_SetSamplerState, (void*)&dt_ValidateDevice,                         //  68-70
    (void*)&dt_SetPaletteEntries, (void*)&dt_GetPaletteEntries, (void*)&dt_SetCurrentTexturePalette,          //  71-73
    (void*)&dt_GetCurrentTexturePalette, (void*)&dt_SetScissorRect, (void*)&dt_GetScissorRect,                //  74-76
    (void*)&dt_SetSoftwareVertexProcessing, (void*)&dt_GetSoftwareVertexProcessing, (void*)&dt_SetNPatchMode, //  77-79
    (void*)&dt_GetNPatchMode, (void*)&dt_DrawPrimitive, (void*)&dt_DrawIndexedPrimitive,                       //  80-82
    (void*)&dt_DrawPrimitiveUP, (void*)&dt_DrawIndexedPrimitiveUP, (void*)&dt_ProcessVertices,                 //  83-85
    (void*)&dt_CreateVertexDeclaration, (void*)&dt_SetVertexDeclaration, (void*)&dt_GetVertexDeclaration,     //  86-88
    (void*)&dt_SetFVF, (void*)&dt_GetFVF, (void*)&dt_CreateVertexShader, (void*)&dt_SetVertexShader,           //  89-92
    (void*)&dt_GetVertexShader, (void*)&dt_SetVertexShaderConstantF, (void*)&dt_GetVertexShaderConstantF,     //  93-95
    (void*)&dt_SetVertexShaderConstantI, (void*)&dt_GetVertexShaderConstantI,                                  //  96-97
    (void*)&dt_SetVertexShaderConstantB, (void*)&dt_GetVertexShaderConstantB,                                  //  98-99
    (void*)&dt_SetStreamSource, (void*)&dt_GetStreamSource, (void*)&dt_SetStreamSourceFreq,                   // 100-102
    (void*)&dt_GetStreamSourceFreq, (void*)&dt_SetIndices, (void*)&dt_GetIndices, (void*)&dt_CreatePixelShader,// 103-106
    (void*)&dt_SetPixelShader, (void*)&dt_GetPixelShader, (void*)&dt_SetPixelShaderConstantF,                  // 107-109
    (void*)&dt_GetPixelShaderConstantF, (void*)&dt_SetPixelShaderConstantI, (void*)&dt_GetPixelShaderConstantI,// 110-112
    (void*)&dt_SetPixelShaderConstantB, (void*)&dt_GetPixelShaderConstantB, (void*)&dt_DrawRectPatch,          // 113-115
    (void*)&dt_DrawTriPatch, (void*)&dt_DeletePatch, (void*)&dt_CreateQuery };                                // 116-118
static_assert(sizeof(dtDevFn) / sizeof(dtDevFn[0]) == 119, "one wrapper per IDirect3DDevice9 slot");
