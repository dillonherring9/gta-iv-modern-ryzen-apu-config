// The steady depth's draws, shared with tools/test-steady; the why is in shaders/ivt_steady_ps.hlsl and ivt_steady_home_ps.hlsl.
// On the caller's IvtSetupPass state. For speed, only pixels with something held are written.
#pragma once

// kSdEdgeLog: the depth spread that makes an edge under FusionFix's logarithmic depth (about three percent); 0 for the stock
// encoding. SdShare: how fast the held value follows a farther sample. SdDecay: freshness lost per frame unfound. kSdStillPx:
// motion in pixels below which a point is still. Tuned with tools/test-steady --explore.
static const float kSdEdgeLog = 0.003f;
static const float kSdStillPx = 0.05f;
static inline int   SdCycle(int phases) { return phases < 4 ? 4 : phases > 64 ? 64 : phases; }
static inline float SdShare(int phases) { return 1.0f / (8.0f * (float)SdCycle(phases)); }
static inline float SdDecay(int phases) { return 1.0f / (4.0f * (float)SdCycle(phases)); }

// The resolve into `held` (G16R16F, cleared first) from `now`, `hist` (the pair's other target) and `mv`. `share` 1 seeds.
static inline HRESULT SdResolve(IDirect3DDevice9* dev, IDirect3DBaseTexture9* now, IDirect3DBaseTexture9* hist,
                                IDirect3DBaseTexture9* mv, IDirect3DSurface9* held, IDirect3DPixelShader9* ps,
                                UINT w, UINT h, float share, float edge, float decay, bool camStill,
                                const float (*quad)[4], UINT quadStride)
{
    const float c[2][4] = { { 1.0f / (float)w, 1.0f / (float)h, share, edge },
                            { camStill ? 1.0f : 0.0f, kSdStillPx * kSdStillPx, decay, 0.0f } };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetRenderTarget(0, held);                // first: `mv` may still be bound as the target
    dev->SetViewport(&vp);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);   // nothing is held anywhere until the draw says so
    dev->SetTexture(0, now);
    dev->SetTexture(1, hist);
    dev->SetTexture(2, mv);
    for (DWORD s = 0; s < 3; s++)
    {
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    }
    dev->SetPixelShaderConstantF(0, &c[0][0], 2);
    dev->SetPixelShader(ps);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
}

// The home: held values that differ go back into the texture the post chain reads, either `colour` (FusionFix's copy, drawn as
// target) or `depth` (the scene's INTZ, written through the depth-stencil with `scratch` as the required colour target).
static inline HRESULT SdHome(IDirect3DDevice9* dev, IDirect3DBaseTexture9* held, IDirect3DSurface9* colour,
                             IDirect3DSurface9* depth, IDirect3DSurface9* scratch, IDirect3DPixelShader9* ps,
                             UINT w, UINT h, const float (*quad)[4], UINT quadStride)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetTexture(1, nullptr);                  // what the frame before held and the vectors leave their stages: the
    dev->SetTexture(2, nullptr);                  // first may be the scratch target below, the second is the guides'
    if (colour)
    {
        dev->SetRenderTarget(0, colour);          // before stage 0 takes `held`: it was the target until here, and
        dev->SetTexture(0, held);                 // stage 0 held the texture that is the target from here
        dev->SetViewport(&vp);
        dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetPixelShader(ps);
        return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
    }
    DWORD func = D3DCMP_LESSEQUAL;
    dev->GetRenderState(D3DRS_ZFUNC, &func);
    dev->SetRenderTarget(0, scratch);
    dev->SetTexture(0, held);                     // stage 0 held the depth texture: gone before it is the depth-stencil
    dev->SetDepthStencilSurface(depth);
    dev->SetViewport(&vp);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    dev->SetRenderState(D3DRS_ZFUNC, func);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetDepthStencilSurface(nullptr);
    return hr;
}

// Development control: counts `now`'s pixels holding any depth, in the caller's occlusion query.
static inline HRESULT SdControl(IDirect3DDevice9* dev, IDirect3DBaseTexture9* now, IDirect3DSurface9* target,
                                IDirect3DPixelShader9* ps, UINT w, UINT h, const float (*quad)[4], UINT quadStride)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetTexture(1, nullptr);
    dev->SetTexture(2, nullptr);
    dev->SetRenderTarget(0, target);
    dev->SetTexture(0, now);
    dev->SetViewport(&vp);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
}
