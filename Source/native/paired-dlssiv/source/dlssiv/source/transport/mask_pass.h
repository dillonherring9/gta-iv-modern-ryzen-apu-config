// The mask's draw, shared with the GPU proof (tools/test-mask); the why is in shaders/ivt_mask_ps.hlsl. Not used by the mod
// yet. At a half or quarter of the render size; the scene is read through the same LINEAR filter as MaskKeep, so an untouched
// place gives 0.
#pragma once

// The reference: one StretchRect of the scene, mid-frame after the fog quad; no state touched.
static inline HRESULT MaskKeep(IDirect3DDevice9* dev, IDirect3DSurface9* scene, IDirect3DSurface9* ref, bool filtered)
{
    return dev->StretchRect(scene, nullptr, ref, nullptr, filtered ? D3DTEXF_LINEAR : D3DTEXF_NONE);
}

// `filtered`: the mask is smaller than the scene, read LINEAR. The brightness difference is scaled by `strength`, clamped to
// `ceiling`.
static inline HRESULT MaskDraw(IDirect3DDevice9* dev, IDirect3DBaseTexture9* now, IDirect3DBaseTexture9* ref, IDirect3DSurface9* mask,
                               IDirect3DPixelShader9* ps, UINT w, UINT h, bool filtered, float strength, float ceiling,
                               const float (*quad)[4], UINT quadStride)
{
    const float c0[4] = { 1.0f / (float)w, 1.0f / (float)h, strength, ceiling };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetRenderTarget(0, mask);
    dev->SetViewport(&vp);
    dev->SetTexture(0, now);
    dev->SetTexture(1, ref);
    if (filtered) { dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR); dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); }
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->SetPixelShader(ps);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
}
