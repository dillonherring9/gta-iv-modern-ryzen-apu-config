// FusionFix's curve replayed, as D3D9 calls, apart from the mod's globals so tools\test-fg-hudless can call it on DXVK and read
// back the state it leaves. Binding render target 0 resets the viewport and scissor rect (D3D9's rule), so both are read first
// and set again after each bind; every other state is FusionFix's and untouched. draw(): the module's own draw, through the
// original entry point.
#pragma once
template <class DrawFn> static HRESULT FgHudSdrReplayD3D(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* backBuffer,
                                                         IDirect3DBaseTexture9* pre, IDirect3DBaseTexture9* t0, DrawFn draw)
{
    D3DVIEWPORT9 vp = {};
    RECT sc = {};
    dev->GetViewport(&vp);
    dev->GetScissorRect(&sc);
    dev->SetRenderTarget(0, target); dev->SetViewport(&vp); dev->SetScissorRect(&sc);
    dev->SetTexture(0, pre);
    const HRESULT hr = draw();
    dev->SetTexture(0, t0);
    dev->SetRenderTarget(0, backBuffer); dev->SetViewport(&vp); dev->SetScissorRect(&sc);
    return hr;
}
