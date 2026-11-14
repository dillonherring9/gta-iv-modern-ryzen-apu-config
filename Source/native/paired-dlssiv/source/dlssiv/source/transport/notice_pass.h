// The on-screen notice's picture and draw, shared by the mod (source/notice.h) and tools/test-notice. The picture: two lines of
// GDI text on a dark box, premultiplied. The draw: one quad at the top centre, blended ONE, INVSRCALPHA; the blend states it
// changes are saved and restored here.
#pragma once

struct NoticeBitmap { unsigned w, h; unsigned char* bgra; };   // w x h, four bytes a pixel, b g r a, premultiplied; free() it

enum { NOTICE_BOX_ALPHA = 158 };                 // of 255: the box darkens what is behind it to 38 percent
static inline unsigned NoticeFontPx(unsigned screenH) { const unsigned px = (screenH * 22 + 500) / 1000; return px < 14 ? 14 : px; }
static inline unsigned NoticeTop(unsigned screenH)    { return (screenH * 30 + 500) / 1000; }   // the box's top edge: 3 percent down

// Makes the picture for a screen of screenW by screenH. False: Windows gave no bitmap or no font (nothing is allocated).
static bool NoticeRaster(const wchar_t* line1, const wchar_t* line2, unsigned screenW, unsigned screenH, NoticeBitmap* out)
{
    out->w = out->h = 0; out->bgra = nullptr;
    if (!line1 || !line1[0] || screenW < 320 || screenH < 200) return false;
    const int px1 = (int)NoticeFontPx(screenH), px2 = (px1 * 8 + 5) / 10;
    const int pad = (px1 * 6 + 5) / 10, gap = px1 / 4;
    const int maxText = (int)(screenW * 6 / 10) - 2 * pad;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT f1 = CreateFontW(-px1, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HFONT f2 = CreateFontW(-px2, 0, 0, 0, FW_NORMAL,   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    bool ok = f1 && f2;
    RECT r1 = { 0, 0, maxText, 0 }, r2 = { 0, 0, maxText, 0 };
    const bool two = line2 && line2[0];
    if (ok)
    {
        HGDIOBJ old = SelectObject(dc, f1);
        ok = DrawTextW(dc, line1, -1, &r1, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX) != 0;
        if (ok && two) { SelectObject(dc, f2); ok = DrawTextW(dc, line2, -1, &r2, DT_CALCRECT | DT_WORDBREAK | DT_CENTER | DT_NOPREFIX) != 0; }
        SelectObject(dc, old);
    }
    const int textW = ok ? (r1.right > r2.right || !two ? r1.right : r2.right) : 0;
    const int h1 = r1.bottom, h2 = two ? r2.bottom : 0;
    const int w = textW + 2 * pad, h = h1 + (two ? gap + h2 : 0) + 2 * pad;
    ok = ok && textW > 0 && h1 > 0 && w <= (int)screenW && h <= (int)screenH / 2;
    HBITMAP bm = nullptr; void* bits = nullptr;
    if (ok)
    {
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;   // top-down
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ok = bm && bits;
    }
    unsigned char* dst = ok ? (unsigned char*)malloc((size_t)w * h * 4) : nullptr;
    ok = ok && dst;
    if (ok)
    {
        memset(bits, 0, (size_t)w * h * 4);
        HGDIOBJ oldBm = SelectObject(dc, bm), oldF = SelectObject(dc, f1);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
        RECT a = { pad, pad, w - pad, pad + h1 };
        DrawTextW(dc, line1, -1, &a, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
        if (two) { SelectObject(dc, f2); RECT b = { pad, pad + h1 + gap, w - pad, pad + h1 + gap + h2 }; DrawTextW(dc, line2, -1, &b, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX); }
        GdiFlush();
        // white text on black: a pixel's green is the text's coverage there. The second line is a lighter grey.
        const unsigned char* src = (const unsigned char*)bits;
        const int y2 = pad + h1 + gap / 2;
        for (int y = 0; y < h; y++)
        {
            const unsigned grey = (two && y >= y2) ? 205u : 255u;
            for (int x = 0; x < w; x++)
            {
                const unsigned cov = src[((size_t)y * w + x) * 4 + 1];
                const unsigned col = (cov * grey + 127) / 255;
                const unsigned alpha = cov + ((255u - cov) * (unsigned)NOTICE_BOX_ALPHA + 127) / 255;
                unsigned char* d = dst + ((size_t)y * w + x) * 4;
                d[0] = d[1] = d[2] = (unsigned char)col; d[3] = (unsigned char)alpha;
            }
        }
        SelectObject(dc, oldF); SelectObject(dc, oldBm);
        out->w = (unsigned)w; out->h = (unsigned)h; out->bgra = dst;
    }
    if (bm) DeleteObject(bm);
    if (f1) DeleteObject(f1);
    if (f2) DeleteObject(f2);
    DeleteDC(dc);
    return ok;
}

// The picture as a texture of its own size (managed: it outlives a Reset). The caller owns it.
static inline HRESULT NoticeUpload(IDirect3DDevice9* dev, const NoticeBitmap* b, IDirect3DTexture9** tex)
{
    *tex = nullptr;
    HRESULT hr = dev->CreateTexture(b->w, b->h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, tex, nullptr);
    if (FAILED(hr) || !*tex) return FAILED(hr) ? hr : E_FAIL;
    D3DLOCKED_RECT lr;
    hr = (*tex)->LockRect(0, &lr, nullptr, 0);
    if (SUCCEEDED(hr))
    {
        for (unsigned y = 0; y < b->h; y++) memcpy((unsigned char*)lr.pBits + (size_t)y * lr.Pitch, b->bgra + (size_t)y * b->w * 4, (size_t)b->w * 4);
        hr = (*tex)->UnlockRect(0);
    }
    if (FAILED(hr)) { (*tex)->Release(); *tex = nullptr; }
    return hr;
}

// Where the box stands on a screen of screenW by screenH: its left and top pixel.
static inline void NoticePlace(unsigned screenW, unsigned screenH, unsigned bw, unsigned* x0, unsigned* y0)
{
    *x0 = screenW > bw ? (screenW - bw) / 2 : 0;
    *y0 = NoticeTop(screenH);
}

// The draw, on the caller's IvtSetupPass state. `show`: 0 to 1.
static inline HRESULT NoticeDraw(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DTexture9* tex, IDirect3DPixelShader9* ps,
                                 unsigned screenW, unsigned screenH, unsigned bw, unsigned bh, float show)
{
    unsigned x0, y0; NoticePlace(screenW, screenH, bw, &x0, &y0);
    // D3D9 puts pixel centres on whole numbers: the pixels x0 .. x0+bw-1 lie between these edges
    const float l = 2.0f * (float)x0 / (float)screenW - 1.0f, r = 2.0f * (float)(x0 + bw) / (float)screenW - 1.0f;
    const float t = 1.0f - 2.0f * (float)y0 / (float)screenH, b = 1.0f - 2.0f * (float)(y0 + bh) / (float)screenH;
    const float quad[6][4] = { { l, b, 0, 1 }, { l, t, 0, 1 }, { r, t, 0, 1 }, { l, b, 0, 1 }, { r, t, 0, 1 }, { r, b, 0, 1 } };
    const float c[2][4] = { { (float)x0, (float)y0, 1.0f / (float)bw, 1.0f / (float)bh }, { show, 0, 0, 0 } };
    const D3DVIEWPORT9 vp = { 0, 0, screenW, screenH, 0.0f, 1.0f };
        // RGB only: the separate alpha blend, not in the device thread's kBodyRs (devthread/dt_trk.h), is left untouched.
    DWORD oSrc = 0, oDst = 0, oOp = 0;
    dev->GetRenderState(D3DRS_SRCBLEND, &oSrc);  dev->GetRenderState(D3DRS_DESTBLEND, &oDst);
    dev->GetRenderState(D3DRS_BLENDOP, &oOp);
    dev->SetRenderTarget(0, target);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
    dev->SetTexture(0, tex);
    dev->SetPixelShaderConstantF(0, &c[0][0], 2);
    dev->SetPixelShader(ps);
    const HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(quad[0]));
    dev->SetRenderState(D3DRS_SRCBLEND, oSrc);   dev->SetRenderState(D3DRS_DESTBLEND, oDst);
    dev->SetRenderState(D3DRS_BLENDOP, oOp);
    return hr;
}
