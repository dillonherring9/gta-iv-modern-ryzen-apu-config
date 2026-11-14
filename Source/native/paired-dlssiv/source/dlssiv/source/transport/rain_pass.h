// The rain layer's draw, shared with tools/test-rain; the why is in shaders/ivt_rain_ps.hlsl. On the caller's IvtSetupPass state,
// it sets an additive blend (BLENDOP, SRCBLEND, DESTBLEND, restored here; listed in kBodyRs) and RGB-only writes.
#pragma once

// `home`: the texture the post chain reads. `filter`: the layer is another size, so it is sampled LINEAR.
static inline HRESULT RainLay(IDirect3DDevice9* dev, IDirect3DBaseTexture9* layer, IDirect3DSurface9* home,
                              IDirect3DPixelShader9* ps, UINT w, UINT h, bool filter,
                              const float (*quad)[4], UINT quadStride)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, 0.0f, 0.0f };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    DWORD src = D3DBLEND_ONE, dst = D3DBLEND_ZERO, op = D3DBLENDOP_ADD;
    dev->GetRenderState(D3DRS_SRCBLEND, &src);
    dev->GetRenderState(D3DRS_DESTBLEND, &dst);
    dev->GetRenderState(D3DRS_BLENDOP, &op);
    dev->SetRenderTarget(0, home);
    dev->SetViewport(&vp);
    dev->SetTexture(0, layer);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, filter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, filter ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x07);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    dev->SetRenderState(D3DRS_SRCBLEND, src);
    dev->SetRenderState(D3DRS_DESTBLEND, dst);
    dev->SetRenderState(D3DRS_BLENDOP, op);
    return hr;
}
