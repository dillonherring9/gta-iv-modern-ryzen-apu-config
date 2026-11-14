// Differential Direct3D 9 test: run the same scenes through any d3d9 implementation (path on the command line)
// and write pointer-free results, so in-process DXVK and VCRender outputs can be compared line by line.
// usage: vcr_test.exe <d3d9 dll> <results file>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

static FILE* out;
#define R(...) do { fprintf(out, __VA_ARGS__); fputc('\n', out); fflush(out); } while (0)
static IDirect3DDevice9* dev;
static HWND wnd;
static const UINT W = 64, H = 64;

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM l) { return DefWindowProcW(w, m, a, l); }

static uint32_t fnv(const uint8_t* p, size_t n, uint32_t h = 2166136261u) { for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }

// read the current render target 0 into memory
static bool readRT(std::vector<uint32_t>& px, UINT& w, UINT& h) {
  IDirect3DSurface9* rt = nullptr; if (FAILED(dev->GetRenderTarget(0, &rt))) return false;
  D3DSURFACE_DESC d; rt->GetDesc(&d); w = d.Width; h = d.Height;
  IDirect3DSurface9* sys = nullptr;
  HRESULT hr = dev->CreateOffscreenPlainSurface(w, h, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr);
  if (SUCCEEDED(hr)) hr = dev->GetRenderTargetData(rt, sys);
  D3DLOCKED_RECT lr;
  if (SUCCEEDED(hr)) hr = sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
  if (SUCCEEDED(hr)) { px.resize((size_t) w * h); for (UINT y = 0; y < h; y++) memcpy(&px[(size_t) y * w], (uint8_t*) lr.pBits + (size_t) y * lr.Pitch, w * 4); sys->UnlockRect(); }
  if (sys) sys->Release(); rt->Release();
  return SUCCEEDED(hr);
}
static void frame(const char* name) {
  std::vector<uint32_t> px; UINT w = 0, h = 0;
  if (!readRT(px, w, h)) { R("%s readback FAILED", name); return; }
  for (auto& p : px) p |= 0xFF000000u;   // X8 formats leave alpha undefined
  R("%s %ux%u hash %08x center %08x corner %08x", name, w, h, fnv((uint8_t*) px.data(), px.size() * 4), px[(h / 2) * w + w / 2], px[0]);
}
static void basicState() {
  dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); dev->SetRenderState(D3DRS_LIGHTING, FALSE);
  dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr);
}
struct VC { float x, y, z, rhw; DWORD c; };
struct VT { float x, y, z, rhw; float u, v; };
static void quadC(VC* v, float x0, float y0, float x1, float y1, DWORD c) {
  v[0] = {x0, y0, 0, 1, c}; v[1] = {x1, y0, 0, 1, c}; v[2] = {x0, y1, 0, 1, c}; v[3] = {x1, y1, 0, 1, c};
}

static void sceneClear() { dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF336699, 1, 0); frame("clear"); }
static void sceneUP() {
  basicState(); dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene();
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); VC v[4]; quadC(v, 8, 8, 56, 56, 0xFF20C040);
  R("up draw %08lx", dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VC))); dev->EndScene(); frame("up");
  IDirect3DVertexBuffer9* vb = nullptr; UINT off = 9, stride = 9; dev->GetStreamSource(0, &vb, &off, &stride);
  R("up stream0 after: vb %s off %u stride %u", vb ? "set" : "null", off, stride); if (vb) vb->Release();
}
static IDirect3DTexture9* makeTex(UINT w, UINT h, D3DFORMAT f, D3DPOOL pool, DWORD usage, uint32_t seed) {
  IDirect3DTexture9* t = nullptr; HRESULT hr = dev->CreateTexture(w, h, 1, usage, f, pool, &t, nullptr);
  if (FAILED(hr)) { R("createTexture %ux%u fmt %u failed %08lx", w, h, f, hr); return nullptr; }
  D3DLOCKED_RECT lr; hr = t->LockRect(0, &lr, nullptr, 0);
  if (SUCCEEDED(hr)) {
    if (f == D3DFMT_A8R8G8B8) { for (UINT y = 0; y < h; y++) for (UINT x = 0; x < w; x++) ((uint32_t*) ((uint8_t*) lr.pBits + y * lr.Pitch))[x] = ((x ^ y) & 1) ? 0xFFFF0000u ^ seed : 0xFF0000FFu ^ (seed << 8); }
    else if (f == D3DFMT_DXT1) { for (UINT by = 0; by < (h + 3) / 4; by++) for (UINT bx = 0; bx < (w + 3) / 4; bx++) { uint16_t* b = (uint16_t*) ((uint8_t*) lr.pBits + by * lr.Pitch + bx * 8);
        b[0] = (uint16_t) (0xF800 ^ (bx * 0x841) ^ seed); b[1] = (uint16_t) (0x001F ^ (by * 0x20)); b[2] = 0x5555; b[3] = (uint16_t) (0xAAAA ^ bx); } }
    t->UnlockRect(0);
  } else R("lock failed %08lx", hr);
  return t;
}
static void drawTex(IDirect3DBaseTexture9* t, float x0 = 0, float y0 = 0, float x1 = 64, float y1 = 64) {
  basicState(); dev->SetTexture(0, t);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT); dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
  VT v[4] = {{x0, y0, 0, 1, 0, 0}, {x1, y0, 0, 1, 1, 0}, {x0, y1, 0, 1, 0, 1}, {x1, y1, 0, 1, 1, 1}};
  dev->BeginScene(); dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VT)); dev->EndScene();
  dev->SetTexture(0, nullptr);
}
static void sceneTextures() {
  IDirect3DTexture9* t = makeTex(8, 8, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 0, 0x123456);
  if (t) { dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); drawTex(t); frame("tex_managed");
    D3DLOCKED_RECT lr; RECT rc = {2, 2, 6, 6};
    if (SUCCEEDED(t->LockRect(0, &lr, &rc, 0))) { for (int y = 0; y < 4; y++) for (int x = 0; x < 2; x++) ((uint32_t*) ((uint8_t*) lr.pBits + y * lr.Pitch))[x] = 0xFF00FF00; t->UnlockRect(0); }
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); drawTex(t); frame("tex_partial_relock");
    if (SUCCEEDED(t->LockRect(0, &lr, nullptr, D3DLOCK_READONLY))) { uint32_t h = 0; for (int y = 0; y < 8; y++) h = fnv((uint8_t*) lr.pBits + y * lr.Pitch, 32, h ? h : 2166136261u); t->UnlockRect(0); R("tex_readonly_lock hash %08x", h); }
    t->Release(); }
  IDirect3DTexture9* d = makeTex(16, 16, D3DFMT_DXT1, D3DPOOL_MANAGED, 0, 0x3);
  if (d) { dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); drawTex(d); frame("tex_dxt1");
    D3DLOCKED_RECT lr; RECT rc = {4, 4, 12, 8};
    if (SUCCEEDED(d->LockRect(0, &lr, &rc, 0))) { for (int bx = 0; bx < 2; bx++) { uint16_t* b = (uint16_t*) ((uint8_t*) lr.pBits + bx * 8); b[0] = 0x07E0; b[1] = 0x07E0; b[2] = b[3] = 0; } d->UnlockRect(0); }
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); drawTex(d); frame("tex_dxt1_rect"); d->Release(); }
  // mip levels: level count and per-level descriptions
  IDirect3DTexture9* m = nullptr; dev->CreateTexture(32, 16, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &m, nullptr);
  if (m) { R("mips levels %lu", m->GetLevelCount()); D3DSURFACE_DESC sd; m->GetLevelDesc(5, &sd); R("mips level5 %ux%u", sd.Width, sd.Height); m->Release(); }
}
static void sceneBuffers() {
  basicState();
  IDirect3DVertexBuffer9* vb = nullptr; dev->CreateVertexBuffer(4 * sizeof(VC), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb, nullptr);
  VC* p = nullptr; vb->Lock(0, 0, (void**) &p, 0); quadC(p, 4, 4, 60, 30, 0xFFE0E000); vb->Unlock();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene(); dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  dev->SetStreamSource(0, vb, 0, sizeof(VC)); R("vb_static draw %08lx", dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2)); dev->EndScene(); frame("vb_static");
  // up draw then rebinding the same buffer must draw it again (b-bridge dropped this rebind)
  VC u[4]; quadC(u, 40, 40, 60, 60, 0xFF0000FF);
  vb->Lock(0, 0, (void**) &p, 0); quadC(p, 4, 34, 30, 60, 0xFFFF8000); vb->Unlock();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene();
  dev->SetStreamSource(0, vb, 0, sizeof(VC));
  dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, u, sizeof(VC));
  dev->SetStreamSource(0, vb, 0, sizeof(VC));
  dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2); dev->EndScene(); frame("up_then_rebind");
  // static re-lock of part of the buffer without DISCARD keeps the rest
  vb->Lock(sizeof(VC) * 3, sizeof(VC), (void**) &p, 0); p[0].x = 50; p[0].y = 62; vb->Unlock();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene(); dev->SetStreamSource(0, vb, 0, sizeof(VC)); dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2); dev->EndScene(); frame("vb_partial_relock");
  // dynamic buffer: DISCARD then NOOVERWRITE appends
  IDirect3DVertexBuffer9* dyn = nullptr; dev->CreateVertexBuffer(64 * sizeof(VC), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &dyn, nullptr);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene(); dev->SetStreamSource(0, dyn, 0, sizeof(VC));
  for (int i = 0; i < 4; i++) {
    dyn->Lock(i * 4 * sizeof(VC), 4 * sizeof(VC), (void**) &p, i == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE);
    quadC(p, (float) (i * 16), 0, (float) (i * 16 + 14), 64, 0xFF000000u | (0x40u << (i * 6 % 24)) | (i * 0x30));
    dyn->Unlock(); dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, i * 4, 2);
  }
  dev->EndScene(); frame("vb_dynamic");
  // indexed
  IDirect3DIndexBuffer9* ib = nullptr; dev->CreateIndexBuffer(6 * 2, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr);
  uint16_t* ix = nullptr; ib->Lock(0, 0, (void**) &ix, 0); uint16_t idx[6] = {0, 1, 2, 2, 1, 3}; memcpy(ix, idx, 12); ib->Unlock();
  vb->Lock(0, 0, (void**) &p, 0); quadC(p, 10, 10, 54, 54, 0xFF80FF80); vb->Unlock();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene(); dev->SetStreamSource(0, vb, 0, sizeof(VC)); dev->SetIndices(ib);
  R("indexed draw %08lx", dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2)); dev->EndScene(); frame("indexed");
  // indexed UP clears indices and stream 0
  VC iu[4]; quadC(iu, 20, 20, 44, 44, 0xFFFF00FF);
  dev->BeginScene(); dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, iu, sizeof(VC)); dev->EndScene(); frame("indexed_up");
  IDirect3DIndexBuffer9* gi = nullptr; dev->GetIndices(&gi); R("indexed_up indices after: %s", gi ? "set" : "null"); if (gi) gi->Release();
  dev->SetStreamSource(0, nullptr, 0, 0); dev->SetIndices(nullptr);
  vb->Release(); dyn->Release(); ib->Release();
}
static void sceneShaders() {
  DWORD vs[] = {0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF};
  DWORD psA[] = {0xFFFF0200, 0x02000001, 0x800F0800, 0xA0E40000, 0x0000FFFF}, psB[] = {0xFFFF0200, 0x02000001, 0x800F0800, 0xA0E40001, 0x0000FFFF};
  IDirect3DVertexShader9* v = nullptr; IDirect3DPixelShader9 *a = nullptr, *b = nullptr;
  R("shader create vs %08lx ps %08lx %08lx", dev->CreateVertexShader(vs, &v), dev->CreatePixelShader(psA, &a), dev->CreatePixelShader(psB, &b));
  UINT n = 0; v->GetFunction(nullptr, &n); R("shader vs bytes %u", n);
  D3DVERTEXELEMENT9 el[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
  IDirect3DVertexDeclaration9* decl = nullptr; dev->CreateVertexDeclaration(el, &decl);
  float c[8] = {1, 0, 0, 1, 0, 1, 0, 1}; dev->SetPixelShaderConstantF(0, c, 2);
  float q[16] = {-1, -1, 0.5f, 1, 1, -1, 0.5f, 1, -1, 1, 0.5f, 1, 1, 1, 0.5f, 1};
  auto draw = [&](const char* name) {
    dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); dev->BeginScene(); dev->SetVertexDeclaration(decl); dev->SetVertexShader(v);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, 16); dev->EndScene(); frame(name);
  };
  dev->SetPixelShader(a); draw("shader_ps_a");
  // typed state block captures the pixel shader (b-bridge lost this)
  IDirect3DStateBlock9* all = nullptr; R("sb create %08lx", dev->CreateStateBlock(D3DSBT_ALL, &all));
  dev->SetPixelShader(b); all->Apply();
  IDirect3DPixelShader9* got = nullptr; dev->GetPixelShader(&got); R("sb after apply: shader %s", got == a ? "A" : got == b ? "B" : "other"); if (got) got->Release();
  draw("sb_apply_restores_a");
  dev->SetPixelShader(b); draw("sb_then_explicit_b");
  // recorded declaration goes into the block, not the device
  D3DVERTEXELEMENT9 e1[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
  IDirect3DVertexDeclaration9* d1 = nullptr; dev->CreateVertexDeclaration(e1, &d1); dev->SetVertexDeclaration(decl);
  IDirect3DStateBlock9* rec = nullptr; dev->BeginStateBlock(); dev->SetVertexDeclaration(d1); dev->EndStateBlock(&rec);
  IDirect3DVertexDeclaration9* gd = nullptr; dev->GetVertexDeclaration(&gd); R("sb recording keeps live decl: %s", gd == decl ? "yes" : "no"); if (gd) gd->Release();
  rec->Apply(); dev->GetVertexDeclaration(&gd); R("sb recorded apply sets decl: %s", gd == d1 ? "yes" : "no"); if (gd) gd->Release();
  dev->BeginStateBlock(); R("sb apply while recording %08lx", all->Apply()); IDirect3DStateBlock9* e = nullptr; dev->EndStateBlock(&e); if (e) e->Release();
  // sampler state through a typed block (b-bridge indexed this wrongly)
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); IDirect3DStateBlock9* ps = nullptr; dev->CreateStateBlock(D3DSBT_PIXELSTATE, &ps);
  dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_MIRROR); ps->Apply(); DWORD au = 0; dev->GetSamplerState(0, D3DSAMP_ADDRESSU, &au); R("sb sampler addressu after apply %lu", au);
  // capture keeps the block's stream offset (D3D9 behaviour)
  IDirect3DVertexBuffer9* vb = nullptr; dev->CreateVertexBuffer(256, 0, 0, D3DPOOL_MANAGED, &vb, nullptr);
  dev->SetStreamSource(0, vb, 32, 16); IDirect3DStateBlock9* sv = nullptr; dev->CreateStateBlock(D3DSBT_ALL, &sv);
  dev->SetStreamSource(0, vb, 64, 32); sv->Capture(); dev->SetStreamSource(0, nullptr, 0, 0); sv->Apply();
  IDirect3DVertexBuffer9* gv = nullptr; UINT off = 0, str = 0; dev->GetStreamSource(0, &gv, &off, &str); R("sb capture stream: vb %s off %u stride %u", gv ? "set" : "null", off, str); if (gv) gv->Release();
  dev->SetStreamSource(0, nullptr, 0, 0);
  sv->Release(); vb->Release(); ps->Release(); rec->Release(); d1->Release(); all->Release();
  dev->SetVertexShader(nullptr); dev->SetPixelShader(nullptr); dev->SetVertexDeclaration(nullptr);
  decl->Release(); v->Release(); a->Release(); b->Release();
}
static void sceneRenderTargets() {
  IDirect3DTexture9* rtt = nullptr; dev->CreateTexture(32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rtt, nullptr);
  IDirect3DSurface9 *s = nullptr, *bb = nullptr; rtt->GetSurfaceLevel(0, &s); dev->GetRenderTarget(0, &bb);
  dev->SetRenderTarget(0, s);
  D3DVIEWPORT9 vp; dev->GetViewport(&vp); R("rt viewport after set %ux%u", vp.Width, vp.Height);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFFC08040, 1, 0);
  basicState(); dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); VC v[4]; quadC(v, 0, 0, 16, 32, 0xFF4080C0);
  dev->BeginScene(); dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VC)); dev->EndScene();
  dev->SetRenderTarget(0, bb);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0); drawTex(rtt); frame("rt_texture");
  RECT dst = {0, 0, 16, 16}; R("stretchrect %08lx", dev->StretchRect(s, nullptr, bb, &dst, D3DTEXF_POINT)); frame("stretchrect");
  RECT cf = {40, 40, 60, 60}; dev->ColorFill(bb, &cf, 0xFF00FFFF); frame("colorfill");
  IDirect3DSurface9* c = nullptr; dev->GetDepthStencilSurface(&c); D3DSURFACE_DESC dd = {}; if (c) { c->GetDesc(&dd); c->Release(); } R("auto depth %ux%u fmt %u", dd.Width, dd.Height, dd.Format);
  s->Release(); bb->Release(); rtt->Release();
}
static void sceneQueries() {
  IDirect3DQuery9* e = nullptr; R("query event create %08lx", dev->CreateQuery(D3DQUERYTYPE_EVENT, &e));
  if (e) { e->Issue(D3DISSUE_END); int n = 0; HRESULT hr; while ((hr = e->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE && n < 5000) { Sleep(1); n++; } R("query event result %08lx size %lu", hr, e->GetDataSize()); e->Release(); }
  IDirect3DQuery9* o = nullptr; R("query occlusion create %08lx", dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &o));
  if (o) { basicState(); dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); VC v[4]; quadC(v, 0, 0, 32, 16, 0xFFFFFFFF);
    o->Issue(D3DISSUE_BEGIN); dev->BeginScene(); dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VC)); dev->EndScene(); o->Issue(D3DISSUE_END);
    DWORD cnt = 0; HRESULT hr; int n = 0; while ((hr = o->GetData(&cnt, 4, D3DGETDATA_FLUSH)) == S_FALSE && n < 5000) { Sleep(1); n++; }
    R("query occlusion %08lx samples %lu", hr, cnt); o->Release(); }
  R("query support timestamp %08lx", dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, nullptr));
}
static void sceneGetters() {
  DWORD v; dev->GetRenderState(D3DRS_SRCBLEND, &v); R("get rs srcblend %lu", v);
  dev->GetRenderState(D3DRS_COLORWRITEENABLE, &v); R("get rs colorwrite %lu", v);
  dev->GetRenderState(D3DRS_FOGEND, &v); R("get rs fogend %08lx", v);
  dev->GetTextureStageState(1, D3DTSS_COLOROP, &v); R("get tss1 colorop %lu", v);
  dev->GetTextureStageState(3, D3DTSS_TEXCOORDINDEX, &v); R("get tss3 texcoordindex %lu", v);
  dev->GetSamplerState(5, D3DSAMP_ADDRESSV, &v); R("get ss5 addressv %lu", v);
  dev->GetSamplerState(D3DVERTEXTEXTURESAMPLER1, D3DSAMP_MAXANISOTROPY, &v); R("get vs sampler1 aniso %lu", v);
  D3DMATRIX m; dev->GetTransform(D3DTS_WORLD, &m); R("get world identity %d", m._11 == 1 && m._22 == 1 && m._12 == 0);
  RECT sc; dev->GetScissorRect(&sc); R("get scissor %ld %ld %ld %ld", sc.left, sc.top, sc.right, sc.bottom);
  dev->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2); DWORD f = 0; dev->GetFVF(&f); R("get fvf %08lx", f);
  IDirect3DVertexDeclaration9* d = nullptr; dev->GetVertexDeclaration(&d); UINT n = 0; if (d) { d->GetDeclaration(nullptr, &n); d->Release(); } R("get fvf decl elements %u", n);
  UINT freq = 0; dev->GetStreamSourceFreq(2, &freq); R("get freq2 %u", freq);
  D3DCAPS9 caps; dev->GetDeviceCaps(&caps); R("caps vs %08lx ps %08lx maxtex %lu", caps.VertexShaderVersion, caps.PixelShaderVersion, caps.MaxTextureWidth);
  R("available texmem nonzero %d", dev->GetAvailableTextureMem() > 0);
}
static void sceneCubeVolume() {
  IDirect3DCubeTexture9* c = nullptr; R("cube create %08lx", dev->CreateCubeTexture(8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &c, nullptr));
  if (c) { D3DLOCKED_RECT lr; c->LockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0, &lr, nullptr, 0); for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) ((uint32_t*) ((uint8_t*) lr.pBits + y * lr.Pitch))[x] = 0xFF000000u + y * 8 + x; c->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0);
    c->LockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0, &lr, nullptr, D3DLOCK_READONLY); uint32_t h = 2166136261u; for (int y = 0; y < 8; y++) h = fnv((uint8_t*) lr.pBits + y * lr.Pitch, 32, h); c->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0);
    R("cube readback %08x", h); c->Release(); }
  IDirect3DVolumeTexture9* t = nullptr; R("volume create %08lx", dev->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr));
  if (t) { D3DLOCKED_BOX lb; t->LockBox(0, &lb, nullptr, 0); for (int z = 0; z < 4; z++) for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) ((uint32_t*) ((uint8_t*) lb.pBits + z * lb.SlicePitch + y * lb.RowPitch))[x] = 0xFF000000u + z * 16 + y * 4 + x; t->UnlockBox(0);
    t->LockBox(0, &lb, nullptr, D3DLOCK_READONLY); uint32_t h = 2166136261u; for (int z = 0; z < 4; z++) for (int y = 0; y < 4; y++) h = fnv((uint8_t*) lb.pBits + z * lb.SlicePitch + y * lb.RowPitch, 16, h); t->UnlockBox(0);
    R("volume readback %08x", h); t->Release(); }
}
static IDirect3DTexture9* g_mtTex[16];
static DWORD WINAPI mtThread(void*) {
  for (int i = 0; i < 16; i++) g_mtTex[i] = makeTex(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 0, 0x10101u * i);
  return 0;
}
static void sceneThreads() {
  HANDLE t = CreateThread(nullptr, 0, mtThread, nullptr, 0, nullptr);
  for (int i = 0; i < 50; i++) { dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000 + i, 1, 0); }
  WaitForSingleObject(t, INFINITE); CloseHandle(t);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1, 0);
  for (int i = 0; i < 16; i++) if (g_mtTex[i]) { drawTex(g_mtTex[i], (float) (i % 4) * 16, (float) (i / 4) * 16, (float) (i % 4) * 16 + 16, (float) (i / 4) * 16 + 16); g_mtTex[i]->Release(); }
  frame("threads");
}
static void scenePresent() {
  int ok = 0;
  for (int i = 0; i < 30; i++) { dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000 | (i * 8), 1, 0); if (SUCCEEDED(dev->Present(nullptr, nullptr, nullptr, nullptr))) ok++; }
  R("present ok %d of 30", ok);
}
// Failed real allocations/parameter validation must reach the caller, with no
// public object. These inputs avoid large allocations and exercise the backend
// rather than the client's existing zero-size/format guards.
static int allocationFailures;
template<class T> static void creationFailure(const char* name, HRESULT hr, T* object) {
  const bool ok = FAILED(hr) && !object;
  R("create-failure %s hr %08lx object %s %s", name, hr, object ? "set" : "null", ok ? "PASS" : "FAIL");
  if (!ok) ++allocationFailures;
  if (object) object->Release();
}
static void sceneCreationFailures() {
  IDirect3DTexture9* t = nullptr;
  { HRESULT hr = dev->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr); creationFailure("texture", hr, t); }
  IDirect3DVolumeTexture9* v = nullptr;
  { HRESULT hr = dev->CreateVolumeTexture(4, 4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &v, nullptr); creationFailure("volume", hr, v); }
  IDirect3DCubeTexture9* c = nullptr;
  { HRESULT hr = dev->CreateCubeTexture(4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &c, nullptr); creationFailure("cube", hr, c); }
  IDirect3DVertexBuffer9* vb = nullptr;
  { HRESULT hr = dev->CreateVertexBuffer(64, D3DUSAGE_DYNAMIC, 0, D3DPOOL_MANAGED, &vb, nullptr); creationFailure("vertex-buffer", hr, vb); }
  IDirect3DIndexBuffer9* ib = nullptr;
  { HRESULT hr = dev->CreateIndexBuffer(12, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr); creationFailure("index-buffer", hr, ib); }
  IDirect3DSurface9* surface = nullptr;
  const auto badSamples = (D3DMULTISAMPLE_TYPE) 17;
  { HRESULT hr = dev->CreateRenderTarget(4, 4, D3DFMT_A8R8G8B8, badSamples, 0, FALSE, &surface, nullptr); creationFailure("render-target", hr, surface); }
  surface = nullptr;
  { HRESULT hr = dev->CreateDepthStencilSurface(4, 4, D3DFMT_D24S8, badSamples, 0, FALSE, &surface, nullptr); creationFailure("depth-stencil", hr, surface); }
  surface = nullptr;
  { HRESULT hr = dev->CreateOffscreenPlainSurface(4, 4, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, &surface, nullptr); creationFailure("offscreen", hr, surface); }
  int recovered = 0;
  for (int i = 0; i < 16; ++i) {
    t = nullptr;
    HRESULT hr = dev->CreateTexture(4, 4, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr);
    if (!FAILED(hr) || t) { ++allocationFailures; if (t) t->Release(); }
    t = makeTex(4, 4, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 0, (uint32_t) i);
    if (t) {
      D3DLOCKED_RECT lock = {};
      if (SUCCEEDED(t->LockRect(0, &lock, nullptr, D3DLOCK_READONLY))) { ++recovered; t->UnlockRect(0); }
      if (i == 15) { drawTex(t); frame("after_create_failure"); }
      t->Release();
    }
  }
  R("create-failure recovery %d of 16 %s", recovered, recovered == 16 ? "PASS" : "FAIL");
  if (recovered != 16) ++allocationFailures;
}

static void sceneReset() {
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = 96; p.BackBufferHeight = 80; p.BackBufferFormat = D3DFMT_A8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = wnd; p.Windowed = TRUE; p.EnableAutoDepthStencil = TRUE; p.AutoDepthStencilFormat = D3DFMT_D24S8;
  p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  R("reset %08lx", dev->Reset(&p));
  IDirect3DSurface9* bb = nullptr; dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb); D3DSURFACE_DESC d = {}; if (bb) { bb->GetDesc(&d); bb->Release(); }
  R("reset backbuffer %ux%u", d.Width, d.Height);
  DWORD z = 0; dev->GetRenderState(D3DRS_ZENABLE, &z); R("reset zenable %lu", z);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF884422, 1, 0); frame("after_reset");
}

// DLSS-IV's state-block probe rules 3 and 4 (found 2026-11-13): device-side normalisation applies to a block's Apply too
static void sceneNormalise() {
  IDirect3DVertexBuffer9* vb = nullptr; dev->CreateVertexBuffer(64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr);
  IDirect3DVertexBuffer9* gv = nullptr; UINT off = 0, str = 0;
  dev->SetStreamSource(1, nullptr, 8, 12); dev->GetStreamSource(1, &gv, &off, &str);
  R("norm null stream on device: vb %s off %u stride %u", gv ? "set" : "null", off, str); if (gv) { gv->Release(); gv = nullptr; }
  IDirect3DStateBlock9* b = nullptr; dev->BeginStateBlock(); dev->SetStreamSource(1, nullptr, 8, 12); dev->EndStateBlock(&b);
  dev->SetStreamSource(1, vb, 4, 16); if (b) b->Apply(); dev->GetStreamSource(1, &gv, &off, &str);
  R("norm null stream from a block: vb %s off %u stride %u", gv ? "set" : "null", off, str); if (gv) gv->Release();
  if (b) { b->Release(); b = nullptr; }
  const D3DVIEWPORT9 flat = { 0, 0, 4, 4, 0.5f, 0.5f }, other = { 0, 0, 4, 4, 0.0f, 1.0f }, inv = { 0, 0, 4, 4, 0.75f, 0.25f };
  D3DVIEWPORT9 g = {};
  dev->SetViewport(&flat); dev->GetViewport(&g); R("norm flat viewport on device: %.4f %.4f", g.MinZ, g.MaxZ);
  dev->SetViewport(&inv); dev->GetViewport(&g); R("norm inverted viewport on device: %.4f %.4f", g.MinZ, g.MaxZ);
  dev->BeginStateBlock(); dev->SetViewport(&flat); dev->EndStateBlock(&b);
  dev->SetViewport(&other); if (b) b->Apply(); dev->GetViewport(&g); R("norm flat viewport from a block: %.4f %.4f", g.MinZ, g.MaxZ);
  if (b) b->Release();
  dev->SetStreamSource(1, nullptr, 0, 0); dev->SetViewport(&other);
  if (vb) vb->Release();
}

int main(int argc, char** argv) {
  if (argc < 3) return 1;
  out = fopen(argv[2], "w"); if (!out) return 1;
  HMODULE dll = LoadLibraryA(argv[1]);
  if (!dll) { R("load failed %lu", GetLastError()); return 2; }
  auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(dll, "Direct3DCreate9");
  IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
  if (!d3d) { R("Direct3DCreate9 failed"); return 3; }
  R("adapters %u", d3d->GetAdapterCount());
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRenderTest"; RegisterClassW(&wc);
  wnd = CreateWindowW(wc.lpszClassName, L"VCRender test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 200, 200, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = W; p.BackBufferHeight = H; p.BackBufferFormat = D3DFMT_A8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = wnd; p.Windowed = TRUE; p.EnableAutoDepthStencil = TRUE; p.AutoDepthStencilFormat = D3DFMT_D24S8;
  p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, wnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p, &dev);
  R("CreateDevice %08lx", hr); if (FAILED(hr)) return 4;
  sceneGetters(); sceneClear(); sceneUP(); sceneTextures(); sceneBuffers(); sceneShaders(); sceneRenderTargets(); sceneQueries();
  sceneCubeVolume(); sceneThreads(); sceneNormalise(); scenePresent(); sceneCreationFailures(); sceneReset(); scenePresent();
  ULONG left = dev->Release(); R("device release %lu", left); if (left) ++allocationFailures;
  d3d->Release(); DestroyWindow(wnd);
  R("creation assertions %s failures %d intended exit %d", allocationFailures ? "FAIL" : "PASS", allocationFailures, allocationFailures ? 5 : 0);
  R("done");
  fclose(out);
  return allocationFailures ? 5 : 0;
}
