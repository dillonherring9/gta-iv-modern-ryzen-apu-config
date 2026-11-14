// The dither pass's draws, shared with the GPU proof (tools/test-dedither); the why is in shaders/ivt_dedither_ps.hlsl. The caller
// has set IvtSetupPass's state; this touches only state kinds it already sets. Stages 0 and 1 are LINEAR, tapped at pixel
// corners to average 2x2. c0.w is 0, or the counter's value for ivt_dedither_probe_ps.hlsl. Shipped as DedInPlace; DedCount
// (ivt_dedither_count_ps.hlsl) counts marked pixels first, and zero skips the pass.
#pragma once

static inline HRESULT DedDraw(IDirect3DDevice9* dev, IDirect3DBaseTexture9* scene, IDirect3DBaseTexture9* mark,
                              IDirect3DSurface9* target, IDirect3DPixelShader9* ps, UINT w, UINT h, int mode,
                              const float (*quad)[4], UINT quadStride, float count = 0.0f)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, (float)mode, count };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetTexture(0, scene);
    dev->SetTexture(1, mark);
    for (DWORD s = 0; s < 2; s++)
    {
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    }
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
}

// The second draw: the changed pixels (alpha 1) into the scene buffer, RGB only; the rest discarded.
static inline HRESULT DedHome(IDirect3DDevice9* dev, IDirect3DBaseTexture9* held, IDirect3DBaseTexture9* mark, IDirect3DSurface9* home,
                              IDirect3DPixelShader9* ps, UINT w, UINT h, const float (*quad)[4], UINT quadStride)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetTexture(0, nullptr);                  // the scene leaves its stage before it becomes the target
    dev->SetRenderTarget(0, home);
    dev->SetViewport(&vp);
    dev->SetTexture(0, held);
    dev->SetTexture(1, mark);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    dev->SetTexture(0, nullptr);
    dev->SetTexture(1, nullptr);
    return hr;
}

// Both draws. The pass's target is not cleared, and is render target 0 again on return.
static inline HRESULT DedInPlace(IDirect3DDevice9* dev, IDirect3DBaseTexture9* scene, IDirect3DSurface9* sceneSurf, IDirect3DBaseTexture9* mark,
                                 IDirect3DBaseTexture9* held, IDirect3DSurface9* heldSurf, IDirect3DPixelShader9* ps, IDirect3DPixelShader9* homePs,
                                 UINT w, UINT h, int mode, const float (*quad)[4], UINT quadStride)
{
    HRESULT hr = DedDraw(dev, scene, mark, heldSurf, ps, w, h, mode, quad, quadStride);
    if (SUCCEEDED(hr)) hr = DedHome(dev, held, mark, sceneSurf, homePs, w, h, quad, quadStride);
    dev->SetRenderTarget(0, heldSurf);
    return hr;
}

// The count: colour writes off, inside `count`, a quarter-size viewport (pixel (x, y) asks the mark's texel (4x, 4y)).
static inline HRESULT DedCount(IDirect3DDevice9* dev, IDirect3DBaseTexture9* mark, IDirect3DSurface9* target, IDirect3DPixelShader9* ps,
                               UINT w, UINT h, const float (*quad)[4], UINT quadStride, IDirect3DQuery9* count)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    const D3DVIEWPORT9 vp = { 0, 0, (w + 3) / 4, (h + 3) / 4, 0.0f, 1.0f };
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetTexture(1, mark);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    count->Issue(D3DISSUE_BEGIN);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
    count->Issue(D3DISSUE_END);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    return hr;
}
