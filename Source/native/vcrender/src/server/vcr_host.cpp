// VCRender host: 64-bit process that owns the real Direct3D 9 device (DXVK) for the 32-bit game.
// It replays the client's command ring in order and copies finished frames into shared memory,
// where the game process draws them with GDI. No game memory is touched from here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <unordered_map>
#include "../shared/vcr_proto.h"
#include "vcr_host_vk.h"

static_assert(sizeof(D3DCAPS9) == 304, "D3DCAPS9 layout");
static_assert(sizeof(D3DVERTEXELEMENT9) == 8, "vertex element layout");

// ---------------------------------------------------------------- logging
static FILE* g_log;
static CRITICAL_SECTION g_logLock;
static void logf(const char* fmt, ...) {
  if (!g_log) return;
  EnterCriticalSection(&g_logLock);
  SYSTEMTIME t; GetLocalTime(&t);
  fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
  fputc('\n', g_log); fflush(g_log);
  LeaveCriticalSection(&g_logLock);
}
void hostLog(const char* fmt, ...) {
  char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  logf("%s", buf);
}
static uint32_t g_failCount[OP_COUNT];
static void opfail(uint16_t op, const char* what, HRESULT hr) {
  uint32_t n = ++g_failCount[op < OP_COUNT ? op : 0];
  if (n <= 20 || (n & 4095) == 0) logf("op %u %s failed: hr=0x%08lx (n=%u)", op, what, (unsigned long) hr, n);
}

// ---------------------------------------------------------------- shared state
static VcrControl* C;
static uint8_t* g_ring; static uint32_t g_ringMask;
static uint8_t* g_arena;
static uint8_t* g_resp;
static HANDLE evWake, evResp, evProgress, evFrame, hParent;
static wchar_t g_base[128];
static IDirect3D9Ex* g_d3d;      // for Ex devices and Ex-only queries
static IDirect3D9* g_d3d9;       // plain interface: its devices keep D3D9 (non-Ex) rules such as D3DPOOL_MANAGED
static HMODULE g_dxvk;
static IDXGIAdapter3* g_dxgiAdapter;   // from the DXVK x64 dxgi.dll beside this executable
static uint32_t g_tail, g_processed;
static std::vector<uint8_t> g_chunk;   // OP_UPLOAD_CHUNK accumulation

struct Obj { IUnknown* p = nullptr; uint32_t kind = K_NONE; uint32_t owner = 0; uint32_t dev = 0; bool weak = false; };
static std::vector<Obj> g_objs;
static std::unordered_map<uint32_t, std::vector<uint32_t>> g_children;   // container -> registered subresources
static Obj* slot(uint32_t id) {
  if (id == 0) return nullptr;
  if (id >= g_objs.size()) g_objs.resize(std::max<size_t>(id + 1, g_objs.size() * 2));
  return &g_objs[id];
}
template<class T> static T* getp(uint32_t id, uint32_t kind) {
  if (id == 0 || id >= g_objs.size()) return nullptr;
  Obj& o = g_objs[id];
  if (o.kind != kind) return nullptr;
  return (T*) o.p;
}
static void put(uint32_t id, IUnknown* p, uint32_t kind, uint32_t dev, bool weak = false, uint32_t owner = 0) {
  Obj* o = slot(id);
  if (!o) { if (p && !weak) p->Release(); return; }
  // After quarantine even a reused wire ID must not release old GPU storage.
  if (o->p && !o->weak && !vkDeviceQuarantined(nullptr)) o->p->Release();
  o->p = p; o->kind = kind; o->dev = dev; o->weak = weak; o->owner = owner;
  if (owner) g_children[owner].push_back(id);
}

struct FrameRes {
  IDirect3DSurface9* rb[VCR_FRAME_SLOTS] = {};
  IDirect3DQuery9* q[VCR_FRAME_SLOTS] = {};
  IDirect3DSurface9* resolve = nullptr;
  uint32_t w = 0, h = 0, fmt = 0, ms = 0;
  HANDLE map = nullptr; uint8_t* view = nullptr; size_t slotBytes = 0;
  int entry = -1; int gen = 0; uint32_t next = 0;
};
struct Dev { IDirect3DDevice9* d = nullptr; IDirect3DDevice9Ex* dx = nullptr; HWND hwnd = nullptr; uint32_t implicitSc = 0;
             uint32_t bbIds[4] = {}; uint32_t dsId = 0; uint32_t bbCount = 0; bool extended = false; };
struct Swap { uint32_t dev = 0; HWND hwnd = nullptr; FrameRes fr; uint32_t bbIds[4] = {}; uint32_t bbCount = 0; bool implicit = false; };
static std::unordered_map<uint32_t, Dev> g_devs;
static std::unordered_map<uint32_t, Swap> g_swaps;

static IDirect3DDevice9* devOf(uint32_t id) { auto it = g_devs.find(id); return it == g_devs.end() ? nullptr : it->second.d; }

static IDirect3DSurface9* surfOf(const VcrSub& s) {
  if (s.id == 0) return nullptr;
  if (s.kind == K_SURFACE) return getp<IDirect3DSurface9>(s.id, K_SURFACE);
  IDirect3DSurface9* out = nullptr;
  if (s.kind == K_TEXTURE) { auto* t = getp<IDirect3DTexture9>(s.id, K_TEXTURE); if (t && SUCCEEDED(t->GetSurfaceLevel(s.level, &out))) out->Release(); }
  else if (s.kind == K_CUBE) { auto* t = getp<IDirect3DCubeTexture9>(s.id, K_CUBE); if (t && SUCCEEDED(t->GetCubeMapSurface((D3DCUBEMAP_FACES) s.face, s.level, &out))) out->Release(); }
  return out;   // container keeps it alive
}
static IDirect3DBaseTexture9* texOf(uint32_t id, uint32_t kind) {
  if (!id) return nullptr;
  if (kind == K_TEXTURE) return getp<IDirect3DTexture9>(id, K_TEXTURE);
  if (kind == K_CUBE) return getp<IDirect3DCubeTexture9>(id, K_CUBE);
  if (kind == K_VOLTEX) return getp<IDirect3DVolumeTexture9>(id, K_VOLTEX);
  return nullptr;
}

// ---------------------------------------------------------------- hidden windows
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static HWND makeWindow(uint32_t w, uint32_t h) {
  static bool reg = false;
  if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRenderHostWindow"; RegisterClassW(&wc); reg = true; }
  RECT r = {0, 0, (LONG) (w ? w : 640), (LONG) (h ? h : 480)};
  return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"VCRenderHostWindow", L"VCRender", WS_POPUP, 0, 0,
                         r.right, r.bottom, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}
static void pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

static D3DPRESENT_PARAMETERS toPP(const VcrPP& v, HWND hwnd) {
  D3DPRESENT_PARAMETERS p = {};
  p.BackBufferWidth = v.w; p.BackBufferHeight = v.h; p.BackBufferFormat = (D3DFORMAT) v.fmt; p.BackBufferCount = v.count;
  p.MultiSampleType = (D3DMULTISAMPLE_TYPE) v.ms; p.MultiSampleQuality = v.msq; p.SwapEffect = (D3DSWAPEFFECT) v.swap;
  p.hDeviceWindow = hwnd; p.Windowed = TRUE; p.EnableAutoDepthStencil = v.autoDS; p.AutoDepthStencilFormat = (D3DFORMAT) v.dsFmt;
  p.Flags = v.flags; p.FullScreen_RefreshRateInHz = 0; p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  return p;
}
static VcrPP fromPP(const D3DPRESENT_PARAMETERS& p) {
  VcrPP v = {}; v.w = p.BackBufferWidth; v.h = p.BackBufferHeight; v.fmt = p.BackBufferFormat; v.count = p.BackBufferCount;
  v.ms = p.MultiSampleType; v.msq = p.MultiSampleQuality; v.swap = p.SwapEffect; v.windowed = p.Windowed; v.autoDS = p.EnableAutoDepthStencil;
  v.dsFmt = p.AutoDepthStencilFormat; v.flags = p.Flags; v.refresh = p.FullScreen_RefreshRateInHz; v.interval = p.PresentationInterval; return v;
}

// ---------------------------------------------------------------- responses
static uint8_t* respBegin() { return g_resp + sizeof(VcrResp); }
static void publishProgress();
static void respond(HRESULT hr, uint32_t bytes) {
  VcrResp* r = (VcrResp*) g_resp; r->hr = hr; r->bytes = bytes;
  publishProgress();
  MemoryBarrier();
  InterlockedIncrement((volatile LONG*) &C->respSeq);
  MemoryBarrier();
  if (C->clientWaiting & 1) SetEvent(evResp);
}
static uint32_t respCap() { return C->respBytes - sizeof(VcrResp); }

// ---------------------------------------------------------------- frame copy (host -> game window)
struct FrameRes;
struct Job { FrameRes* f; uint32_t k; uint32_t frame; };
static CRITICAL_SECTION g_jobLock; static CONDITION_VARIABLE g_jobCv, g_doneCv;
static std::vector<Job> g_jobs; static uint32_t g_inflight;

static int allocEntry(uint32_t scId) {
  for (uint32_t i = 0; i < VCR_MAX_SC; i++) if (C->frames[i].scId == (int32_t) scId) return i;
  for (uint32_t i = 0; i < VCR_MAX_SC; i++) if (C->frames[i].scId == 0) { C->frames[i].latestSlot = -1; C->frames[i].readingSlot = -1; C->frames[i].completedFrame = 0; C->frames[i].scId = scId; return i; }
  return -1;
}
static void releaseFrameRes(FrameRes& f, bool keepEntry) {
  for (auto& s : f.rb) if (s) { s->Release(); s = nullptr; }
  for (auto& q : f.q) if (q) { q->Release(); q = nullptr; }
  if (f.resolve) { f.resolve->Release(); f.resolve = nullptr; }
  if (f.view) { UnmapViewOfFile(f.view); f.view = nullptr; }
  if (f.map) { CloseHandle(f.map); f.map = nullptr; }
  f.w = f.h = 0;
  if (!keepEntry && f.entry >= 0) { C->frames[f.entry].scId = 0; f.entry = -1; }
}
static void drainJobs() {
  EnterCriticalSection(&g_jobLock);
  while (g_inflight) SleepConditionVariableCS(&g_doneCv, &g_jobLock, 50);
  LeaveCriticalSection(&g_jobLock);
}
static bool ensureFrameRes(uint32_t scId, Swap& s, IDirect3DDevice9* d, const D3DSURFACE_DESC& bd) {
  FrameRes& f = s.fr;
  if (f.w == bd.Width && f.h == bd.Height && f.fmt == (uint32_t) bd.Format && f.ms == (uint32_t) bd.MultiSampleType && f.view) return true;
  drainJobs();
  int entry = f.entry; int gen = f.gen;
  releaseFrameRes(f, true);
  f.entry = entry >= 0 ? entry : allocEntry(scId);
  if (f.entry < 0) { logf("no free frame entry for swapchain %u", scId); return false; }
  f.gen = gen + 1;
  f.w = bd.Width; f.h = bd.Height; f.fmt = bd.Format; f.ms = bd.MultiSampleType;
  for (uint32_t i = 0; i < VCR_FRAME_SLOTS; i++) {
    HRESULT hr = d->CreateOffscreenPlainSurface(bd.Width, bd.Height, bd.Format, D3DPOOL_SYSTEMMEM, &f.rb[i], nullptr);
    if (FAILED(hr)) { logf("readback surface %ux%u fmt %u failed 0x%08lx", bd.Width, bd.Height, bd.Format, (unsigned long) hr); return false; }
    hr = d->CreateQuery(D3DQUERYTYPE_EVENT, &f.q[i]);
    if (FAILED(hr)) { logf("event query failed 0x%08lx", (unsigned long) hr); return false; }
  }
  if (bd.MultiSampleType != D3DMULTISAMPLE_NONE) {
    HRESULT hr = d->CreateRenderTarget(bd.Width, bd.Height, bd.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &f.resolve, nullptr);
    if (FAILED(hr)) { logf("resolve target failed 0x%08lx", (unsigned long) hr); return false; }
  }
  f.slotBytes = (size_t) bd.Width * bd.Height * 4;
  wchar_t name[200]; swprintf(name, 200, L"%ls_F%u_%d", g_base, scId, f.gen);
  f.map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) (f.slotBytes * VCR_FRAME_SLOTS), name);
  if (!f.map) { logf("frame mapping failed %lu", GetLastError()); return false; }
  f.view = (uint8_t*) MapViewOfFile(f.map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!f.view) { logf("frame view failed %lu", GetLastError()); return false; }
  VcrFrameEntry& e = C->frames[f.entry];
  e.width = bd.Width; e.height = bd.Height; e.latestSlot = -1; e.readingSlot = -1;
  MemoryBarrier();
  e.gen = f.gen;
  logf("swapchain %u frame buffer %ux%u fmt %u gen %d (%zu bytes)", scId, bd.Width, bd.Height, bd.Format, f.gen, f.slotBytes * VCR_FRAME_SLOTS);
  return true;
}

static inline uint8_t clamp8(float v) { return v <= 0.f ? 0 : v >= 1.f ? 255 : (uint8_t) (v * 255.f + 0.5f); }
static float half2f(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023; float v;
  if (e == 0) v = m / 16777216.f; else if (e == 31) v = m ? 0.f : 65504.f; else v = ldexpf(1.f + m / 1024.f, (int) e - 15);
  return s ? -v : v;
}
static void convertRow(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t fmt) {
  switch (fmt) {
  case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: memcpy(dst, src, w * 4); for (uint32_t x = 0; x < w; x++) dst[x * 4 + 3] = 255; break;
  case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8:
    for (uint32_t x = 0; x < w; x++) { dst[x*4] = src[x*4+2]; dst[x*4+1] = src[x*4+1]; dst[x*4+2] = src[x*4]; dst[x*4+3] = 255; } break;
  case D3DFMT_A2R10G10B10: case D3DFMT_A2B10G10R10:
    for (uint32_t x = 0; x < w; x++) { uint32_t v; memcpy(&v, src + x * 4, 4); uint8_t a = (v >> 2) & 255, b = (v >> 12) & 255, c = (v >> 22) & 255;
      if (fmt == D3DFMT_A2R10G10B10) { dst[x*4] = a; dst[x*4+1] = b; dst[x*4+2] = c; } else { dst[x*4] = c; dst[x*4+1] = b; dst[x*4+2] = a; } dst[x*4+3] = 255; } break;
  case D3DFMT_R5G6B5:
    for (uint32_t x = 0; x < w; x++) { uint16_t v; memcpy(&v, src + x * 2, 2); dst[x*4] = (uint8_t) ((v & 31) * 255 / 31); dst[x*4+1] = (uint8_t) (((v >> 5) & 63) * 255 / 63); dst[x*4+2] = (uint8_t) (((v >> 11) & 31) * 255 / 31); dst[x*4+3] = 255; } break;
  case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    for (uint32_t x = 0; x < w; x++) { uint16_t v; memcpy(&v, src + x * 2, 2); dst[x*4] = (uint8_t) ((v & 31) * 255 / 31); dst[x*4+1] = (uint8_t) (((v >> 5) & 31) * 255 / 31); dst[x*4+2] = (uint8_t) (((v >> 10) & 31) * 255 / 31); dst[x*4+3] = 255; } break;
  case D3DFMT_A16B16G16R16F:
    for (uint32_t x = 0; x < w; x++) { uint16_t c[4]; memcpy(c, src + x * 8, 8); dst[x*4] = clamp8(half2f(c[2])); dst[x*4+1] = clamp8(half2f(c[1])); dst[x*4+2] = clamp8(half2f(c[0])); dst[x*4+3] = 255; } break;
  default: memset(dst, 0x40, w * 4); break;
  }
}

static DWORD WINAPI copierThread(void*) {
  for (;;) {
    EnterCriticalSection(&g_jobLock);
    while (g_jobs.empty()) SleepConditionVariableCS(&g_jobCv, &g_jobLock, INFINITE);
    Job j = g_jobs.front(); g_jobs.erase(g_jobs.begin());
    LeaveCriticalSection(&g_jobLock);
    {   // the main thread drains jobs before changing or freeing frame resources
      FrameRes& f = *j.f;
      IDirect3DQuery9* q = f.q[j.k];
      int spins = 0;
      for (;;) {
        HRESULT hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH);
        if (hr != S_FALSE) break;
        if (++spins < 50) Sleep(0); else Sleep(1);
        if (spins > 20000) { logf("frame %u: GPU event never completed", j.frame); break; }
      }
      D3DLOCKED_RECT lr;
      HRESULT hr = f.rb[j.k]->LockRect(&lr, nullptr, D3DLOCK_READONLY);
      if (SUCCEEDED(hr) && f.view && f.entry >= 0) {
        VcrFrameEntry& e = C->frames[f.entry];
        int latest = e.latestSlot, reading = e.readingSlot, s = 0;
        for (s = 0; s < (int) VCR_FRAME_SLOTS; s++) if (s != latest && s != reading) break;
        uint8_t* dst = f.view + f.slotBytes * s;
        for (uint32_t y = 0; y < f.h; y++) convertRow(dst + (size_t) y * f.w * 4, (const uint8_t*) lr.pBits + (size_t) y * lr.Pitch, f.w, f.fmt);
        f.rb[j.k]->UnlockRect();
        MemoryBarrier();
        e.latestFrame = j.frame;
        InterlockedExchange((volatile LONG*) &e.latestSlot, s);
        InterlockedExchange((volatile LONG*) &e.completedFrame, j.frame);
        SetEvent(evFrame);
      } else if (FAILED(hr)) {
        opfail(OP_PRESENT, "readback lock", hr);
        if (f.entry >= 0) InterlockedExchange((volatile LONG*) &C->frames[f.entry].completedFrame, j.frame);
      }
    }
    EnterCriticalSection(&g_jobLock); g_inflight--; WakeAllConditionVariable(&g_doneCv); LeaveCriticalSection(&g_jobLock);
    if (C->clientWaiting & 2) SetEvent(evProgress);
  }
  return 0;
}

static void doPresent(const PPresent& p) {
  auto it = g_swaps.find(p.scId);
  IDirect3DDevice9* d = devOf(p.devId);
  if (it == g_swaps.end() || !d) return;
  Swap& s = it->second;
  IDirect3DSwapChain9* sc = getp<IDirect3DSwapChain9>(p.scId, K_SWAPCHAIN);
  if (!sc) return;
  IDirect3DSurface9* bb = nullptr;
  if (FAILED(sc->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
  D3DSURFACE_DESC bd; bb->GetDesc(&bd);
  if (!ensureFrameRes(p.scId, s, d, bd)) { bb->Release(); if (s.fr.entry >= 0) C->frames[s.fr.entry].completedFrame = p.frameNo; return; }
  FrameRes& f = s.fr;
  EnterCriticalSection(&g_jobLock);
  while (g_inflight >= VCR_FRAME_SLOTS - 1) SleepConditionVariableCS(&g_doneCv, &g_jobLock, 50);
  LeaveCriticalSection(&g_jobLock);
  uint32_t k = f.next; f.next = (f.next + 1) % VCR_FRAME_SLOTS;
  IDirect3DSurface9* src = bb;
  HRESULT hr = S_OK;
  if (f.resolve) { hr = d->StretchRect(bb, nullptr, f.resolve, nullptr, D3DTEXF_NONE); src = f.resolve; }
  if (SUCCEEDED(hr)) hr = d->GetRenderTargetData(src, f.rb[k]);
  bb->Release();
  if (FAILED(hr)) { opfail(OP_PRESENT, "GetRenderTargetData", hr); C->frames[f.entry].completedFrame = p.frameNo; return; }
  f.q[k]->Issue(D3DISSUE_END);
  EnterCriticalSection(&g_jobLock);
  g_jobs.push_back({&f, k, p.frameNo}); g_inflight++;
  WakeConditionVariable(&g_jobCv);
  LeaveCriticalSection(&g_jobLock);
}

// ---------------------------------------------------------------- image upload / readback helpers
struct Locked { uint8_t* p = nullptr; uint32_t pitch = 0, slicePitch = 0; };
static HRESULT lockImage(const VcrSub& s, bool whole, const RECT& rc, const D3DBOX& box, DWORD flags, Locked& out) {
  HRESULT hr = E_FAIL; D3DLOCKED_RECT lr; D3DLOCKED_BOX lb;
  const RECT* prc = whole ? nullptr : &rc; const D3DBOX* pbx = whole ? nullptr : &box;
  switch (s.kind) {
  case K_SURFACE: { auto* o = getp<IDirect3DSurface9>(s.id, K_SURFACE); if (o) hr = o->LockRect(&lr, prc, flags); break; }
  case K_TEXTURE: { auto* o = getp<IDirect3DTexture9>(s.id, K_TEXTURE); if (o) hr = o->LockRect(s.level, &lr, prc, flags); break; }
  case K_CUBE:    { auto* o = getp<IDirect3DCubeTexture9>(s.id, K_CUBE); if (o) hr = o->LockRect((D3DCUBEMAP_FACES) s.face, s.level, &lr, prc, flags); break; }
  case K_VOLTEX:  { auto* o = getp<IDirect3DVolumeTexture9>(s.id, K_VOLTEX); if (o) hr = o->LockBox(s.level, &lb, pbx, flags); break; }
  case K_VOLUME:  { auto* o = getp<IDirect3DVolume9>(s.id, K_VOLUME); if (o) hr = o->LockBox(&lb, pbx, flags); break; }
  }
  if (FAILED(hr)) return hr;
  if (s.kind == K_VOLTEX || s.kind == K_VOLUME) { out.p = (uint8_t*) lb.pBits; out.pitch = lb.RowPitch; out.slicePitch = lb.SlicePitch; }
  else { out.p = (uint8_t*) lr.pBits; out.pitch = lr.Pitch; out.slicePitch = 0; }
  return hr;
}
static void unlockImage(const VcrSub& s) {
  switch (s.kind) {
  case K_SURFACE: { auto* o = getp<IDirect3DSurface9>(s.id, K_SURFACE); if (o) o->UnlockRect(); break; }
  case K_TEXTURE: { auto* o = getp<IDirect3DTexture9>(s.id, K_TEXTURE); if (o) o->UnlockRect(s.level); break; }
  case K_CUBE:    { auto* o = getp<IDirect3DCubeTexture9>(s.id, K_CUBE); if (o) o->UnlockRect((D3DCUBEMAP_FACES) s.face, s.level); break; }
  case K_VOLTEX:  { auto* o = getp<IDirect3DVolumeTexture9>(s.id, K_VOLTEX); if (o) o->UnlockBox(s.level); break; }
  case K_VOLUME:  { auto* o = getp<IDirect3DVolume9>(s.id, K_VOLUME); if (o) o->UnlockBox(); break; }
  }
}
static void doUploadImg(const PUploadImg& u, const uint8_t* inl) {
  const uint8_t* src = u.src == 0 ? inl : u.src == 1 ? g_arena + u.arenaOff : g_chunk.data();
  if (u.src == 2 && g_chunk.size() < (size_t) u.srcSlicePitch * (u.slices ? u.slices - 1 : 0) + (size_t) u.srcPitch * (u.rows ? u.rows - 1 : 0) + u.rowBytes) {
    opfail(OP_UPLOAD_IMG, "chunk size", E_FAIL); return;
  }
  RECT rc = {u.l, u.t, u.r, u.b}; D3DBOX box = {(UINT) u.l, (UINT) u.t, (UINT) u.r, (UINT) u.b, (UINT) u.front, (UINT) u.back};
  Locked L; HRESULT hr = lockImage(u.sub, u.whole != 0, rc, box, u.lockFlags, L);
  if (FAILED(hr)) { opfail(OP_UPLOAD_IMG, "lock", hr); return; }
  for (uint32_t z = 0; z < (u.slices ? u.slices : 1); z++)
    for (uint32_t y = 0; y < u.rows; y++)
      memcpy(L.p + (size_t) z * L.slicePitch + (size_t) y * L.pitch, src + (size_t) z * u.srcSlicePitch + (size_t) y * u.srcPitch, u.rowBytes);
  unlockImage(u.sub);
  if (u.src == 2) g_chunk.clear();
}
static void doReadImg(const PReadImg& u) {
  RECT rc = {u.l, u.t, u.r, u.b}; D3DBOX box = {(UINT) u.l, (UINT) u.t, (UINT) u.r, (UINT) u.b, (UINT) u.front, (UINT) u.back};
  Locked L; HRESULT hr = lockImage(u.sub, u.whole != 0, rc, box, D3DLOCK_READONLY, L);
  if (FAILED(hr)) { respond(hr, 0); return; }
  uint32_t bytes = u.rowBytes * u.rowCount;
  if (bytes > respCap()) { unlockImage(u.sub); respond(E_OUTOFMEMORY, 0); return; }
  uint8_t* out = respBegin();
  for (uint32_t y = 0; y < u.rowCount; y++)
    memcpy(out + (size_t) y * u.rowBytes, L.p + (size_t) u.slice * L.slicePitch + (size_t) (u.rowStart + y) * L.pitch, u.rowBytes);
  unlockImage(u.sub);
  respond(S_OK, bytes);
}
template<class B> static HRESULT lockBuf(uint32_t id, uint32_t kind, UINT off, UINT size, void** p, DWORD flags) {
  if (kind == K_VB) { auto* b = getp<IDirect3DVertexBuffer9>(id, K_VB); return b ? b->Lock(off, size, p, flags) : E_FAIL; }
  auto* b = getp<IDirect3DIndexBuffer9>(id, K_IB); return b ? b->Lock(off, size, p, flags) : E_FAIL;
}
static void unlockBuf(uint32_t id, uint32_t kind) {
  if (kind == K_VB) { auto* b = getp<IDirect3DVertexBuffer9>(id, K_VB); if (b) b->Unlock(); }
  else { auto* b = getp<IDirect3DIndexBuffer9>(id, K_IB); if (b) b->Unlock(); }
}

// ---------------------------------------------------------------- object destruction
static void destroyChildren(uint32_t id) {
  auto it = g_children.find(id);
  if (it == g_children.end()) return;
  std::vector<uint32_t> kids; kids.swap(it->second); g_children.erase(it);
  for (uint32_t c : kids) {
    if (c >= g_objs.size() || g_objs[c].owner != id) continue;
    if (g_objs[c].p && !g_objs[c].weak) g_objs[c].p->Release();
    g_objs[c] = Obj();
  }
}
static void destroy(uint32_t id) {
  if (id == 0 || id >= g_objs.size()) return;
  Obj o = g_objs[id];
  if (o.kind == K_DEVICE) {
    auto it = g_devs.find(id);
    if (it != g_devs.end() && !vkDeviceGone(it->second.d, id)) {
      logf("device %u quarantined; destruction pinned until process exit", id); return;
    }
  }
  if (vkDeviceQuarantined(nullptr)) {
    logf("object %u destruction skipped: GPU quarantine requires restart", id); return;
  }
  if (o.kind == K_DEVICE) {
    auto it = g_devs.find(id);
    if (it != g_devs.end()) {
      drainJobs();
      for (auto sit = g_swaps.begin(); sit != g_swaps.end();) {
        if (sit->second.dev == id) { releaseFrameRes(sit->second.fr, false); sit = g_swaps.erase(sit); } else ++sit;
      }
      // drop every object that still belongs to this device (client released or orphaned them)
      for (uint32_t i = 1; i < g_objs.size(); i++) if (i != id && g_objs[i].dev == id && g_objs[i].p) {
        if (!g_objs[i].weak) g_objs[i].p->Release();
        g_objs[i] = Obj();
      }
      for (auto cit = g_children.begin(); cit != g_children.end();) { if (cit->first < g_objs.size() && !g_objs[cit->first].p) cit = g_children.erase(cit); else ++cit; }
      HWND w = it->second.hwnd;
      ULONG left = it->second.d->Release();
      if (left) logf("device %u released with %lu references left", id, left);
      if (w) DestroyWindow(w);
      g_devs.erase(it);
    }
    g_objs[id] = Obj();
    return;
  }
  if (o.kind == K_SWAPCHAIN) {
    auto it = g_swaps.find(id);
    if (it != g_swaps.end()) { drainJobs(); releaseFrameRes(it->second.fr, false); if (!it->second.implicit && it->second.hwnd) DestroyWindow(it->second.hwnd); g_swaps.erase(it); }
  }
  destroyChildren(id);
  if (o.p && !o.weak) o.p->Release();
  g_objs[id] = Obj();
}

// register implicit swapchain objects for a device (after create or reset)
static void registerImplicit(uint32_t devId, Dev& dv, uint32_t scId, const uint32_t* bbIds, uint32_t dsId, const D3DPRESENT_PARAMETERS& pp) {
  IDirect3DSwapChain9* sc = nullptr;
  if (SUCCEEDED(dv.d->GetSwapChain(0, &sc))) {
    put(scId, sc, K_SWAPCHAIN, devId);
    Swap& s = g_swaps[scId]; s.dev = devId; s.hwnd = dv.hwnd; s.implicit = true;
    s.bbCount = pp.BackBufferCount ? pp.BackBufferCount : 1;
    for (uint32_t i = 0; i < s.bbCount && i < 4; i++) {
      IDirect3DSurface9* bb = nullptr;
      if (SUCCEEDED(sc->GetBackBuffer(i, D3DBACKBUFFER_TYPE_MONO, &bb))) { put(bbIds[i], bb, K_SURFACE, devId, false, scId); }
      s.bbIds[i] = bbIds[i]; dv.bbIds[i] = bbIds[i];
    }
    dv.bbCount = s.bbCount;
  }
  dv.implicitSc = scId;
  dv.dsId = 0;
  if (dsId && pp.EnableAutoDepthStencil) {
    IDirect3DSurface9* ds = nullptr;
    if (SUCCEEDED(dv.d->GetDepthStencilSurface(&ds)) && ds) { put(dsId, ds, K_SURFACE, devId); dv.dsId = dsId; }
  }
}
static void releaseImplicit(uint32_t devId, Dev& dv) {
  drainJobs();
  auto it = g_swaps.find(dv.implicitSc);
  if (it != g_swaps.end()) { releaseFrameRes(it->second.fr, false); g_swaps.erase(it); }
  if (dv.implicitSc) {
    if (dv.extended) {
      // Ex backbuffers retained by the client remain ordinary host resources.
      auto kids = g_children.find(dv.implicitSc);
      if (kids != g_children.end()) {
        for (uint32_t c : kids->second) if (c < g_objs.size() && g_objs[c].owner == dv.implicitSc) g_objs[c].owner = 0;
        g_children.erase(kids);
      }
    } else destroyChildren(dv.implicitSc);
  }
  if (dv.dsId && !dv.extended) destroy(dv.dsId);
  if (dv.implicitSc && dv.implicitSc < g_objs.size() && g_objs[dv.implicitSc].p) { g_objs[dv.implicitSc].p->Release(); g_objs[dv.implicitSc] = Obj(); }
  dv.implicitSc = 0; dv.dsId = 0; dv.bbCount = 0;
  memset(dv.bbIds, 0, sizeof dv.bbIds);
  (void) devId;
}

// ---------------------------------------------------------------- phase 3 lookups (vcr_host_vk.cpp)
static VkLookup g_lk = {
  [](uint32_t id) { return devOf(id); },
  [](const VcrSub& s) -> IUnknown* {
    if (s.kind == K_SURFACE) return surfOf(s);
    if (s.kind == K_TEXTURE || s.kind == K_CUBE) return texOf(s.id, s.kind);
    return nullptr;
  },
  [](uint32_t id) { return getp<IDirect3DTexture9>(id, K_TEXTURE); },
  [](uint32_t id) -> uint32_t { return id && id < g_objs.size() ? g_objs[id].dev : 0; },
};

// ---------------------------------------------------------------- dispatcher
#define P(T) const T& a = *(const T*) (r + 1); (void) a
static void dispatch(const VcrRec* r) {
  const uint8_t* body = (const uint8_t*) (r + 1);
  switch (r->op) {
  case OP_NOP: break;
  case OP_HELLO: { *(uint32_t*) respBegin() = VCR_PROTOCOL; respond(S_OK, 4); break; }
  case OP_SHUTDOWN: logf("client requested shutdown"); fflush(g_log); TerminateProcess(GetCurrentProcess(), 0); break;

  // ---------------- module queries
  case OP_ADAPTER_COUNT: { *(uint32_t*) respBegin() = g_d3d->GetAdapterCount(); respond(S_OK, 4); break; }
  case OP_ADAPTER_IDENTIFIER: { P(PAdapterQ); D3DADAPTER_IDENTIFIER9 id = {}; HRESULT hr = g_d3d->GetAdapterIdentifier(a.adapter, a.a, &id);
    RAdapterId* o = (RAdapterId*) respBegin(); memcpy(o->driver, id.Driver, 512); memcpy(o->description, id.Description, 512); memcpy(o->deviceName, id.DeviceName, 32);
    o->versionLow = id.DriverVersion.LowPart; o->versionHigh = (uint32_t) id.DriverVersion.HighPart; o->vendorId = id.VendorId; o->deviceId = id.DeviceId;
    o->subSysId = id.SubSysId; o->revision = id.Revision; memcpy(o->guid, &id.DeviceIdentifier, 16); o->whql = id.WHQLLevel;
    respond(hr, sizeof(RAdapterId)); break; }
  case OP_ADAPTER_MODE_COUNT: { P(PAdapterQ); *(uint32_t*) respBegin() = g_d3d->GetAdapterModeCount(a.adapter, (D3DFORMAT) a.a); respond(S_OK, 4); break; }
  case OP_ENUM_ADAPTER_MODES: { P(PAdapterQ); D3DDISPLAYMODE m = {}; HRESULT hr = g_d3d->EnumAdapterModes(a.adapter, (D3DFORMAT) a.a, a.b, &m);
    RMode* o = (RMode*) respBegin(); o->w = m.Width; o->h = m.Height; o->refresh = m.RefreshRate; o->fmt = m.Format; respond(hr, sizeof(RMode)); break; }
  case OP_ADAPTER_DISPLAY_MODE: { P(PAdapterQ); D3DDISPLAYMODE m = {}; HRESULT hr = g_d3d->GetAdapterDisplayMode(a.adapter, &m);
    RMode* o = (RMode*) respBegin(); o->w = m.Width; o->h = m.Height; o->refresh = m.RefreshRate; o->fmt = m.Format; respond(hr, sizeof(RMode)); break; }
  case OP_CHECK_DEVICE_TYPE: { P(PAdapterQ); respond(g_d3d->CheckDeviceType(a.adapter, (D3DDEVTYPE) a.devType, (D3DFORMAT) a.a, (D3DFORMAT) a.b, a.c), 0); break; }
  case OP_CHECK_DEVICE_FORMAT: { P(PAdapterQ); respond(g_d3d->CheckDeviceFormat(a.adapter, (D3DDEVTYPE) a.devType, (D3DFORMAT) a.a, a.b, (D3DRESOURCETYPE) a.c, (D3DFORMAT) a.d), 0); break; }
  case OP_CHECK_MULTISAMPLE: { P(PAdapterQ); DWORD q = 0; HRESULT hr = g_d3d->CheckDeviceMultiSampleType(a.adapter, (D3DDEVTYPE) a.devType, (D3DFORMAT) a.a, a.b, (D3DMULTISAMPLE_TYPE) a.c, &q);
    *(uint32_t*) respBegin() = q; respond(hr, 4); break; }
  case OP_CHECK_DEPTH_MATCH: { P(PAdapterQ); respond(g_d3d->CheckDepthStencilMatch(a.adapter, (D3DDEVTYPE) a.devType, (D3DFORMAT) a.a, (D3DFORMAT) a.b, (D3DFORMAT) a.c), 0); break; }
  case OP_CHECK_FORMAT_CONVERSION: { P(PAdapterQ); respond(g_d3d->CheckDeviceFormatConversion(a.adapter, (D3DDEVTYPE) a.devType, (D3DFORMAT) a.a, (D3DFORMAT) a.b), 0); break; }
  case OP_ADAPTER_CAPS: { P(PAdapterQ); D3DCAPS9 c = {}; HRESULT hr = g_d3d->GetDeviceCaps(a.adapter, (D3DDEVTYPE) a.devType, &c); memcpy(respBegin(), &c, sizeof c); respond(hr, sizeof c); break; }
  case OP_ADAPTER_MODE_COUNT_EX: { P(PAdapterQ); D3DDISPLAYMODEFILTER f = {sizeof f, (D3DFORMAT) a.a, (D3DSCANLINEORDERING) a.b};
    *(uint32_t*) respBegin() = g_d3d->GetAdapterModeCountEx(a.adapter, &f); respond(S_OK, 4); break; }
  case OP_ENUM_ADAPTER_MODES_EX: { P(PAdapterQ); D3DDISPLAYMODEFILTER f = {sizeof f, (D3DFORMAT) a.a, (D3DSCANLINEORDERING) a.b}; D3DDISPLAYMODEEX m = {sizeof m};
    HRESULT hr = g_d3d->EnumAdapterModesEx(a.adapter, &f, a.c, &m); RModeEx* o = (RModeEx*) respBegin();
    o->size = sizeof(D3DDISPLAYMODEEX); o->w = m.Width; o->h = m.Height; o->refresh = m.RefreshRate; o->fmt = m.Format; o->scan = m.ScanLineOrdering; respond(hr, sizeof(RModeEx)); break; }
  case OP_ADAPTER_DISPLAY_MODE_EX: { P(PAdapterQ); D3DDISPLAYMODEEX m = {sizeof m}; D3DDISPLAYROTATION rot = D3DDISPLAYROTATION_IDENTITY;
    HRESULT hr = g_d3d->GetAdapterDisplayModeEx(a.adapter, &m, &rot); RModeEx* o = (RModeEx*) respBegin();
    o->size = rot; o->w = m.Width; o->h = m.Height; o->refresh = m.RefreshRate; o->fmt = m.Format; o->scan = m.ScanLineOrdering; respond(hr, sizeof(RModeEx)); break; }
  case OP_ADAPTER_LUID: { P(PAdapterQ); LUID l = {}; HRESULT hr = g_d3d->GetAdapterLUID(a.adapter, &l); memcpy(respBegin(), &l, 8); respond(hr, 8); break; }

  // ---------------- devices and swapchains
  case OP_CREATE_DEVICE: {
    P(PCreateDevice);
    HWND w = makeWindow(a.pp.w, a.pp.h);
    D3DPRESENT_PARAMETERS pp = toPP(a.pp, w);
    DWORD beh = (a.behavior | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE) & ~D3DCREATE_NOWINDOWCHANGES;
    IDirect3DDevice9* d = nullptr; IDirect3DDevice9Ex* dx = nullptr; HRESULT hr;
    if (a.isEx) { hr = g_d3d->CreateDeviceEx(a.adapter, (D3DDEVTYPE) a.devType, w, beh, &pp, nullptr, &dx); d = dx; }
    else hr = g_d3d9->CreateDevice(a.adapter, (D3DDEVTYPE) a.devType, w, beh, &pp, &d);
    RCreateDevice* o = (RCreateDevice*) respBegin();
    if (FAILED(hr)) {
      logf("CreateDevice%s %ux%u fmt %u count %u ms %u swap %u autoDS %u/%u flags 0x%x behavior 0x%x failed 0x%08lx",
           a.isEx ? "Ex" : "", a.pp.w, a.pp.h, a.pp.fmt, a.pp.count, a.pp.ms, a.pp.swap, a.pp.autoDS, a.pp.dsFmt, a.pp.flags, a.behavior, (unsigned long) hr);
      DestroyWindow(w); respond(hr, 0); break;
    }
    if (!dx) d->QueryInterface(__uuidof(IDirect3DDevice9Ex), (void**) &dx); else dx->AddRef();
    if (dx) dx->Release();   // keep a non-owning Ex pointer
    Dev& dv = g_devs[a.devId]; dv.d = d; dv.dx = dx; dv.hwnd = w; dv.extended = a.isEx;
    put(a.devId, d, K_DEVICE, a.devId, true);   // device lifetime is managed by g_devs
    registerImplicit(a.devId, dv, a.scId, a.bbIds, a.dsId, pp);
    o->pp = fromPP(pp); D3DCAPS9 caps = {}; d->GetDeviceCaps(&caps); memcpy(o->caps, &caps, sizeof caps);
    logf("device %u: %ux%u fmt %u count %u autoDS %u/%u behavior 0x%x%s", a.devId, pp.BackBufferWidth, pp.BackBufferHeight,
         pp.BackBufferFormat, pp.BackBufferCount, pp.EnableAutoDepthStencil, pp.AutoDepthStencilFormat, a.behavior, a.isEx ? " (Ex)" : "");
    respond(S_OK, sizeof(RCreateDevice)); break;
  }
  case OP_RESET: {
    P(PReset);
    auto it = g_devs.find(a.devId); if (it == g_devs.end()) { respond(D3DERR_INVALIDCALL, 0); break; }
    Dev& dv = it->second;
    if (!vkDeviceReset(dv.d, a.devId) || vkDeviceQuarantined(nullptr)) {
      logf("Reset refused: GPU quarantine requires process restart"); respond(D3DERR_DEVICELOST, 0); break;
    }
    releaseImplicit(a.devId, dv);
    SetWindowPos(dv.hwnd, nullptr, 0, 0, a.pp.w ? a.pp.w : 640, a.pp.h ? a.pp.h : 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    D3DPRESENT_PARAMETERS pp = toPP(a.pp, dv.hwnd);
    HRESULT hr = (a.isEx && dv.dx) ? dv.dx->ResetEx(&pp, nullptr) : dv.d->Reset(&pp);
    if (FAILED(hr)) { logf("Reset %ux%u failed 0x%08lx", a.pp.w, a.pp.h, (unsigned long) hr); respond(hr, 0); break; }
    registerImplicit(a.devId, dv, a.scId, a.bbIds, a.dsId, pp);
    RCreateDevice* o = (RCreateDevice*) respBegin(); o->pp = fromPP(pp);
    D3DCAPS9 caps = {}; dv.d->GetDeviceCaps(&caps); memcpy(o->caps, &caps, sizeof caps);
    respond(hr, sizeof(RCreateDevice)); break;
  }
  case OP_DESTROY: { destroy(*(const uint32_t*) body); break; }
  case OP_AVAILABLE_TEXMEM: { P(PGeneric); IDirect3DDevice9* d = devOf(a.devId); *(uint32_t*) respBegin() = d ? d->GetAvailableTextureMem() : 0; respond(S_OK, 4); break; }
  case OP_EVICT_MANAGED: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) d->EvictManagedResources(); break; }
  case OP_DISPLAY_MODE: { P(PGeneric); IDirect3DDevice9* d = devOf(a.devId); D3DDISPLAYMODE m = {}; HRESULT hr = d ? d->GetDisplayMode(0, &m) : E_FAIL;
    RMode* o = (RMode*) respBegin(); o->w = m.Width; o->h = m.Height; o->refresh = m.RefreshRate; o->fmt = m.Format; respond(hr, sizeof(RMode)); break; }
  case OP_CREATE_SWAPCHAIN: {
    P(PCreateSwapchain); auto it = g_devs.find(a.devId); if (it == g_devs.end()) { respond(D3DERR_INVALIDCALL, 0); break; }
    HWND w = makeWindow(a.pp.w, a.pp.h); D3DPRESENT_PARAMETERS pp = toPP(a.pp, w);
    IDirect3DSwapChain9* sc = nullptr; HRESULT hr = it->second.d->CreateAdditionalSwapChain(&pp, &sc);
    if (FAILED(hr)) { DestroyWindow(w); logf("CreateAdditionalSwapChain failed 0x%08lx", (unsigned long) hr); respond(hr, 0); break; }
    put(a.scId, sc, K_SWAPCHAIN, a.devId);
    Swap& s = g_swaps[a.scId]; s.dev = a.devId; s.hwnd = w; s.implicit = false; s.bbCount = pp.BackBufferCount ? pp.BackBufferCount : 1;
    for (uint32_t i = 0; i < s.bbCount && i < 4; i++) { IDirect3DSurface9* bb = nullptr; if (SUCCEEDED(sc->GetBackBuffer(i, D3DBACKBUFFER_TYPE_MONO, &bb))) put(a.bbIds[i], bb, K_SURFACE, a.devId, false, a.scId); s.bbIds[i] = a.bbIds[i]; }
    RCreateDevice* o = (RCreateDevice*) respBegin(); o->pp = fromPP(pp); respond(S_OK, sizeof(RCreateDevice)); break;
  }
  case OP_PRESENT: { P(PPresent); doPresent(a); break; }

  // ---------------- resource creation
  case OP_CREATE_TEXTURE: { P(PCreateTex); IDirect3DDevice9* d = devOf(a.devId); IDirect3DTexture9* t = nullptr;
    HRESULT hr = d ? d->CreateTexture(a.w, a.h, a.levels, a.usage, (D3DFORMAT) a.fmt, (D3DPOOL) a.pool, &t, nullptr) : E_FAIL;
    if (FAILED(hr)) { opfail(r->op, "CreateTexture", hr); logf("  texture %ux%u levels %u usage 0x%x fmt %u pool %u", a.w, a.h, a.levels, a.usage, a.fmt, a.pool); }
    if (SUCCEEDED(hr)) put(a.id, t, K_TEXTURE, a.devId);
    else if (t) t->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_VOLUME_TEXTURE: { P(PCreateTex); IDirect3DDevice9* d = devOf(a.devId); IDirect3DVolumeTexture9* t = nullptr;
    HRESULT hr = d ? d->CreateVolumeTexture(a.w, a.h, a.d, a.levels, a.usage, (D3DFORMAT) a.fmt, (D3DPOOL) a.pool, &t, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateVolumeTexture", hr); if (SUCCEEDED(hr)) put(a.id, t, K_VOLTEX, a.devId);
    else if (t) t->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_CUBE_TEXTURE: { P(PCreateTex); IDirect3DDevice9* d = devOf(a.devId); IDirect3DCubeTexture9* t = nullptr;
    HRESULT hr = d ? d->CreateCubeTexture(a.w, a.levels, a.usage, (D3DFORMAT) a.fmt, (D3DPOOL) a.pool, &t, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateCubeTexture", hr); if (SUCCEEDED(hr)) put(a.id, t, K_CUBE, a.devId);
    else if (t) t->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_VB: { P(PCreateBuf); IDirect3DDevice9* d = devOf(a.devId); IDirect3DVertexBuffer9* b = nullptr;
    HRESULT hr = d ? d->CreateVertexBuffer(a.length, a.usage, a.fvfOrFmt, (D3DPOOL) a.pool, &b, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateVertexBuffer", hr); if (SUCCEEDED(hr)) put(a.id, b, K_VB, a.devId);
    else if (b) b->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_IB: { P(PCreateBuf); IDirect3DDevice9* d = devOf(a.devId); IDirect3DIndexBuffer9* b = nullptr;
    HRESULT hr = d ? d->CreateIndexBuffer(a.length, a.usage, (D3DFORMAT) a.fvfOrFmt, (D3DPOOL) a.pool, &b, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateIndexBuffer", hr); if (SUCCEEDED(hr)) put(a.id, b, K_IB, a.devId);
    else if (b) b->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_RT: { P(PCreateSurf); IDirect3DDevice9* d = devOf(a.devId); IDirect3DSurface9* s = nullptr;
    HRESULT hr = d ? d->CreateRenderTarget(a.w, a.h, (D3DFORMAT) a.fmt, (D3DMULTISAMPLE_TYPE) a.ms, a.msq, a.flag, &s, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateRenderTarget", hr); if (SUCCEEDED(hr)) put(a.id, s, K_SURFACE, a.devId);
    else if (s) s->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_DS: { P(PCreateSurf); IDirect3DDevice9* d = devOf(a.devId); IDirect3DSurface9* s = nullptr;
    HRESULT hr = d ? d->CreateDepthStencilSurface(a.w, a.h, (D3DFORMAT) a.fmt, (D3DMULTISAMPLE_TYPE) a.ms, a.msq, a.flag, &s, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateDepthStencilSurface", hr); if (SUCCEEDED(hr)) put(a.id, s, K_SURFACE, a.devId);
    else if (s) s->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_CREATE_OFFSCREEN: { P(PCreateSurf); IDirect3DDevice9* d = devOf(a.devId); IDirect3DSurface9* s = nullptr;
    HRESULT hr = d ? d->CreateOffscreenPlainSurface(a.w, a.h, (D3DFORMAT) a.fmt, (D3DPOOL) a.pool, &s, nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "CreateOffscreenPlainSurface", hr); if (SUCCEEDED(hr)) put(a.id, s, K_SURFACE, a.devId);
    else if (s) s->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_REGISTER_SUB: { P(PRegisterSub);
    uint32_t dev = a.parent < g_objs.size() ? g_objs[a.parent].dev : 0;
    if (a.kind == K_SURFACE) { VcrSub s = {a.parent, a.parentKind, a.face, a.level}; IDirect3DSurface9* p = surfOf(s); put(a.id, p, K_SURFACE, dev, true, a.parent); }
    else if (a.kind == K_VOLUME) { auto* vt = getp<IDirect3DVolumeTexture9>(a.parent, K_VOLTEX); IDirect3DVolume9* v = nullptr;
      if (vt && SUCCEEDED(vt->GetVolumeLevel(a.level, &v))) v->Release(); put(a.id, v, K_VOLUME, dev, true, a.parent); }
    break; }

  // ---------------- copies
  case OP_UPDATE_SURFACE: { P(PUpdateSurface); IDirect3DDevice9* d = devOf(a.devId); RECT rc = {a.sl, a.st, a.sr, a.sb}; POINT pt = {a.px, a.py};
    HRESULT hr = d ? d->UpdateSurface(surfOf(a.src), a.hasSrcRect ? &rc : nullptr, surfOf(a.dst), a.hasPt ? &pt : nullptr) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "UpdateSurface", hr); break; }
  case OP_UPDATE_TEXTURE: { P(PUpdateTexture); IDirect3DDevice9* d = devOf(a.devId);
    IDirect3DBaseTexture9* s = nullptr; IDirect3DBaseTexture9* t = nullptr;
    for (uint32_t k : {K_TEXTURE, K_CUBE, K_VOLTEX}) { if (!s) s = texOf(a.src, k); if (!t) t = texOf(a.dst, k); }
    HRESULT hr = d ? d->UpdateTexture(s, t) : E_FAIL; if (FAILED(hr)) opfail(r->op, "UpdateTexture", hr); break; }
  case OP_GET_RT_DATA: { P(PGetRTData); IDirect3DDevice9* d = devOf(a.devId);
    HRESULT hr = d ? d->GetRenderTargetData(surfOf(a.rt), surfOf(a.dst)) : E_FAIL; if (FAILED(hr)) opfail(r->op, "GetRenderTargetData", hr); respond(hr, 0); break; }
  case OP_STRETCH_RECT: { P(PStretchRect); IDirect3DDevice9* d = devOf(a.devId); RECT s = {a.sl, a.st, a.sr, a.sb}, t = {a.dl, a.dt, a.dr, a.db};
    HRESULT hr = d ? d->StretchRect(surfOf(a.src), a.hasSrc ? &s : nullptr, surfOf(a.dst), a.hasDst ? &t : nullptr, (D3DTEXTUREFILTERTYPE) a.filter) : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "StretchRect", hr); break; }
  case OP_COLOR_FILL: { P(PColorFill); IDirect3DDevice9* d = devOf(a.devId); RECT rc = {a.l, a.t, a.r, a.b};
    HRESULT hr = d ? d->ColorFill(surfOf(a.dst), a.hasRect ? &rc : nullptr, a.color) : E_FAIL; if (FAILED(hr)) opfail(r->op, "ColorFill", hr); break; }

  // ---------------- state
  case OP_SET_RT: { P(PSetRT); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->SetRenderTarget(a.index, surfOf(a.surf)); if (FAILED(hr)) opfail(r->op, "SetRenderTarget", hr); } break; }
  case OP_SET_DS: { P(PSetDS); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetDepthStencilSurface(surfOf(a.surf)); break; }
  case OP_BEGIN_SCENE: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) d->BeginScene(); break; }
  case OP_END_SCENE: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) d->EndScene(); break; }
  case OP_CLEAR: { P(PClear); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->Clear(a.count, a.count ? (const D3DRECT*) (&a + 1) : nullptr, a.flags, a.color, a.z, a.stencil); if (FAILED(hr)) opfail(r->op, "Clear", hr); } break; }
  case OP_SET_TRANSFORM: { P(PSetTransform); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetTransform((D3DTRANSFORMSTATETYPE) a.state, (const D3DMATRIX*) a.m); break; }
  case OP_MULTIPLY_TRANSFORM: { P(PSetTransform); if (IDirect3DDevice9* d = devOf(a.devId)) d->MultiplyTransform((D3DTRANSFORMSTATETYPE) a.state, (const D3DMATRIX*) a.m); break; }
  case OP_SET_VIEWPORT: { P(PViewport); if (IDirect3DDevice9* d = devOf(a.devId)) { D3DVIEWPORT9 v = {a.x, a.y, a.w, a.h, a.mn, a.mx}; d->SetViewport(&v); } break; }
  case OP_SET_MATERIAL: { P(PMaterial); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetMaterial((const D3DMATERIAL9*) a.v); break; }
  case OP_SET_LIGHT: { P(PLight); if (IDirect3DDevice9* d = devOf(a.devId)) { D3DLIGHT9 l; l.Type = (D3DLIGHTTYPE) a.type; memcpy(&l.Diffuse, a.v, sizeof(float) * 25); d->SetLight(a.index, &l); } break; }
  case OP_LIGHT_ENABLE: { P(PLightEnable); if (IDirect3DDevice9* d = devOf(a.devId)) d->LightEnable(a.index, a.enable); break; }
  case OP_SET_CLIP_PLANE: { P(PClipPlane); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetClipPlane(a.index, a.p); break; }
  case OP_SET_RS: { P(PRS); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetRenderState((D3DRENDERSTATETYPE) a.state, a.value); break; }
  case OP_CREATE_SB: { P(PSB); IDirect3DDevice9* d = devOf(a.devId); IDirect3DStateBlock9* sb = nullptr;
    HRESULT hr = d ? d->CreateStateBlock((D3DSTATEBLOCKTYPE) a.type, &sb) : E_FAIL; if (FAILED(hr)) opfail(r->op, "CreateStateBlock", hr); if (SUCCEEDED(hr)) put(a.id, sb, K_SB, a.devId);
    else if (sb) sb->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_BEGIN_SB: { P(PSB); IDirect3DDevice9* d = devOf(a.devId);
    HRESULT hr = d ? d->BeginStateBlock() : E_FAIL;
    if (FAILED(hr)) opfail(r->op, "BeginStateBlock", hr);
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_END_SB: { P(PSB); IDirect3DDevice9* d = devOf(a.devId); IDirect3DStateBlock9* sb = nullptr;
    HRESULT hr = d ? d->EndStateBlock(&sb) : E_FAIL; if (FAILED(hr)) opfail(r->op, "EndStateBlock", hr); if (SUCCEEDED(hr)) put(a.id, sb, K_SB, a.devId);
    else if (sb) sb->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_SB_CAPTURE: { P(PSB); if (auto* sb = getp<IDirect3DStateBlock9>(a.id, K_SB)) { HRESULT hr = sb->Capture(); if (FAILED(hr)) opfail(r->op, "Capture", hr); } break; }
  case OP_SB_APPLY: { P(PSB); if (auto* sb = getp<IDirect3DStateBlock9>(a.id, K_SB)) { HRESULT hr = sb->Apply(); if (FAILED(hr)) opfail(r->op, "Apply", hr); } break; }
  case OP_SET_TEXTURE: { P(PSetTexture); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->SetTexture(a.stage, texOf(a.id, a.kind)); if (FAILED(hr)) opfail(r->op, "SetTexture", hr); } break; }
  case OP_SET_TSS: { P(PTSS); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetTextureStageState(a.stage, (D3DTEXTURESTAGESTATETYPE) a.type, a.value); break; }
  case OP_SET_SS: { P(PSS); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetSamplerState(a.sampler, (D3DSAMPLERSTATETYPE) a.type, a.value); break; }
  case OP_VALIDATE: { P(PGeneric); IDirect3DDevice9* d = devOf(a.devId); DWORD n = 0; HRESULT hr = d ? d->ValidateDevice(&n) : E_FAIL; *(uint32_t*) respBegin() = n; respond(hr, 4); break; }
  case OP_SET_PALETTE: { P(PPalette); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetPaletteEntries(a.number, (const PALETTEENTRY*) a.entries); break; }
  case OP_SET_CUR_PALETTE: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetCurrentTexturePalette(a.a); break; }
  case OP_SET_SCISSOR: { P(PScissor); if (IDirect3DDevice9* d = devOf(a.devId)) { RECT rc = {a.l, a.t, a.r, a.b}; d->SetScissorRect(&rc); } break; }
  case OP_SET_SWVP: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetSoftwareVertexProcessing(a.a); break; }
  case OP_SET_NPATCH: { P(PGeneric); if (IDirect3DDevice9* d = devOf(a.devId)) { float f; memcpy(&f, &a.a, 4); d->SetNPatchMode(f); } break; }
  case OP_DRAW: { P(PDraw); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->DrawPrimitive((D3DPRIMITIVETYPE) a.prim, a.a, a.b); if (FAILED(hr)) opfail(r->op, "DrawPrimitive", hr); } break; }
  case OP_DRAW_INDEXED: { P(PDraw); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->DrawIndexedPrimitive((D3DPRIMITIVETYPE) a.prim, (INT) a.a, a.b, a.c, a.d, a.e); if (FAILED(hr)) opfail(r->op, "DrawIndexedPrimitive", hr); } break; }
  case OP_DRAW_UP: { P(PDrawUP); if (IDirect3DDevice9* d = devOf(a.devId)) { HRESULT hr = d->DrawPrimitiveUP((D3DPRIMITIVETYPE) a.prim, a.count, (const void*) (&a + 1), a.stride); if (FAILED(hr)) opfail(r->op, "DrawPrimitiveUP", hr); } break; }
  case OP_DRAW_INDEXED_UP: { P(PDrawIndexedUP); if (IDirect3DDevice9* d = devOf(a.devId)) { const uint8_t* idx = (const uint8_t*) (&a + 1);
      HRESULT hr = d->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE) a.prim, a.minIdx, a.numV, a.count, idx, (D3DFORMAT) a.idxFmt, idx + ((a.ibytes + 7) & ~7u), a.stride);
      if (FAILED(hr)) opfail(r->op, "DrawIndexedPrimitiveUP", hr); } break; }
  case OP_PROCESS_VERTICES: { P(PProcessVertices); if (IDirect3DDevice9* d = devOf(a.devId)) {
      HRESULT hr = d->ProcessVertices(a.src, a.dst, a.count, getp<IDirect3DVertexBuffer9>(a.destVB, K_VB), getp<IDirect3DVertexDeclaration9>(a.decl, K_DECL), a.flags);
      if (FAILED(hr)) opfail(r->op, "ProcessVertices", hr); } break; }
  case OP_CREATE_DECL: { P(PCreateDecl); IDirect3DDevice9* d = devOf(a.devId); IDirect3DVertexDeclaration9* v = nullptr;
    HRESULT hr = d ? d->CreateVertexDeclaration((const D3DVERTEXELEMENT9*) (&a + 1), &v) : E_FAIL; if (FAILED(hr)) opfail(r->op, "CreateVertexDeclaration", hr); if (SUCCEEDED(hr)) put(a.id, v, K_DECL, a.devId);
    else if (v) v->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_SET_DECL: { P(PSetObj); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetVertexDeclaration(getp<IDirect3DVertexDeclaration9>(a.id, K_DECL)); break; }
  case OP_SET_FVF: { P(PSetObj); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetFVF(a.id); break; }
  case OP_CREATE_VS: { P(PCreateShader); IDirect3DDevice9* d = devOf(a.devId); IDirect3DVertexShader9* v = nullptr;
    HRESULT hr = d ? d->CreateVertexShader((const DWORD*) (&a + 1), &v) : E_FAIL; if (FAILED(hr)) opfail(r->op, "CreateVertexShader", hr); if (SUCCEEDED(hr)) put(a.id, v, K_VS, a.devId);
    else if (v) v->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_SET_VS: { P(PSetObj); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetVertexShader(getp<IDirect3DVertexShader9>(a.id, K_VS)); break; }
  case OP_VS_CONST_F: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetVertexShaderConstantF(a.start, (const float*) (&a + 1), a.count); break; }
  case OP_VS_CONST_I: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetVertexShaderConstantI(a.start, (const int*) (&a + 1), a.count); break; }
  case OP_VS_CONST_B: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetVertexShaderConstantB(a.start, (const BOOL*) (&a + 1), a.count); break; }
  case OP_SET_STREAM: { P(PStream); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetStreamSource(a.stream, getp<IDirect3DVertexBuffer9>(a.id, K_VB), a.offset, a.stride); break; }
  case OP_SET_STREAM_FREQ: { P(PStreamFreq); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetStreamSourceFreq(a.stream, a.setting); break; }
  case OP_SET_INDICES: { P(PSetObj); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetIndices(getp<IDirect3DIndexBuffer9>(a.id, K_IB)); break; }
  case OP_CREATE_PS: { P(PCreateShader); IDirect3DDevice9* d = devOf(a.devId); IDirect3DPixelShader9* v = nullptr;
    HRESULT hr = d ? d->CreatePixelShader((const DWORD*) (&a + 1), &v) : E_FAIL; if (FAILED(hr)) opfail(r->op, "CreatePixelShader", hr); if (SUCCEEDED(hr)) put(a.id, v, K_PS, a.devId);
    else if (v) v->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_SET_PS: { P(PSetObj); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetPixelShader(getp<IDirect3DPixelShader9>(a.id, K_PS)); break; }
  case OP_PS_CONST_F: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetPixelShaderConstantF(a.start, (const float*) (&a + 1), a.count); break; }
  case OP_PS_CONST_I: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetPixelShaderConstantI(a.start, (const int*) (&a + 1), a.count); break; }
  case OP_PS_CONST_B: { P(PConst); if (IDirect3DDevice9* d = devOf(a.devId)) d->SetPixelShaderConstantB(a.start, (const BOOL*) (&a + 1), a.count); break; }
  case OP_QUERY_SUPPORT: { P(PCreateQuery); IDirect3DDevice9* d = devOf(a.devId); respond(d ? d->CreateQuery((D3DQUERYTYPE) a.type, nullptr) : E_FAIL, 0); break; }
  case OP_CREATE_QUERY: { P(PCreateQuery); IDirect3DDevice9* d = devOf(a.devId); IDirect3DQuery9* q = nullptr;
    HRESULT hr = d ? d->CreateQuery((D3DQUERYTYPE) a.type, &q) : E_FAIL; if (FAILED(hr)) opfail(r->op, "CreateQuery", hr); if (SUCCEEDED(hr)) put(a.id, q, K_QUERY, a.devId);
    else if (q) q->Release();
    if (r->flags & VCR_F_SYNC) respond(hr, 0);
    break; }
  case OP_QUERY_ISSUE: { P(PQueryIssue); if (auto* q = getp<IDirect3DQuery9>(a.id, K_QUERY)) q->Issue(a.flags); break; }
  case OP_QUERY_GETDATA: { P(PQueryGetData); auto* q = getp<IDirect3DQuery9>(a.id, K_QUERY);
    uint32_t n = a.size > 64 ? 64 : a.size; HRESULT hr = q ? q->GetData(n ? respBegin() : nullptr, n, a.flags) : D3DERR_INVALIDCALL;
    respond(hr, hr == S_OK ? n : 0); break; }
  case OP_TEX_SET_LOD: { P(PTexOp); if (auto* t = texOf(a.id, a.kind)) t->SetLOD(a.value); break; }
  case OP_TEX_AUTOGEN_FILTER: { P(PTexOp); if (auto* t = texOf(a.id, a.kind)) t->SetAutoGenFilterType((D3DTEXTUREFILTERTYPE) a.value); break; }
  case OP_TEX_GENMIPS: { P(PTexOp); if (auto* t = texOf(a.id, a.kind)) t->GenerateMipSubLevels(); break; }
  case OP_TEX_DIRTY: { P(PTexDirty);
    if (a.kind == K_TEXTURE) { if (auto* t = getp<IDirect3DTexture9>(a.id, K_TEXTURE)) { RECT rc = {a.l, a.t, a.r, a.b}; t->AddDirtyRect(a.hasBox ? &rc : nullptr); } }
    else if (a.kind == K_CUBE) { if (auto* t = getp<IDirect3DCubeTexture9>(a.id, K_CUBE)) { RECT rc = {a.l, a.t, a.r, a.b}; t->AddDirtyRect((D3DCUBEMAP_FACES) a.face, a.hasBox ? &rc : nullptr); } }
    else if (a.kind == K_VOLTEX) { if (auto* t = getp<IDirect3DVolumeTexture9>(a.id, K_VOLTEX)) { D3DBOX b = {(UINT) a.l, (UINT) a.t, (UINT) a.r, (UINT) a.b, (UINT) a.front, (UINT) a.back}; t->AddDirtyBox(a.hasBox ? &b : nullptr); } }
    break; }
  case OP_RES_PRELOAD: { P(PTexOp); if (a.id < g_objs.size() && g_objs[a.id].p) { IDirect3DResource9* res = nullptr; if (SUCCEEDED(g_objs[a.id].p->QueryInterface(__uuidof(IDirect3DResource9), (void**) &res))) { res->PreLoad(); res->Release(); } } break; }
  case OP_RES_PRIORITY: { P(PTexOp); if (a.id < g_objs.size() && g_objs[a.id].p) { IDirect3DResource9* res = nullptr; if (SUCCEEDED(g_objs[a.id].p->QueryInterface(__uuidof(IDirect3DResource9), (void**) &res))) { res->SetPriority(a.value); res->Release(); } } break; }

  // ---------------- data transfer
  case OP_UPLOAD_CHUNK: { P(PUploadChunk); if (g_chunk.size() < a.total) g_chunk.resize(a.total); if ((size_t) a.offset + a.bytes <= g_chunk.size()) memcpy(g_chunk.data() + a.offset, &a + 1, a.bytes); break; }
  case OP_UPLOAD_IMG: { P(PUploadImg); doUploadImg(a, (const uint8_t*) (&a + 1)); break; }
  case OP_UPLOAD_BUF: { P(PUploadBuf);
    const uint8_t* src = a.src == 0 ? (const uint8_t*) (&a + 1) : a.src == 1 ? g_arena + a.arenaOff : g_chunk.data();
    void* p = nullptr; HRESULT hr = a.kind == K_VB ? lockBuf<void>(a.id, K_VB, a.offset, a.size, &p, a.lockFlags) : lockBuf<void>(a.id, K_IB, a.offset, a.size, &p, a.lockFlags);
    if (FAILED(hr) || !p) { opfail(r->op, "buffer lock", hr); break; }
    memcpy(p, src, a.size); unlockBuf(a.id, a.kind); if (a.src == 2) g_chunk.clear(); break; }
  case OP_READ_IMG: { P(PReadImg); doReadImg(a); break; }
  case OP_READ_BUF: { P(PReadBuf); void* p = nullptr;
    if (a.size > respCap()) { respond(E_OUTOFMEMORY, 0); break; }
    HRESULT hr = lockBuf<void>(a.id, a.kind, a.offset, a.size, &p, D3DLOCK_READONLY);
    if (FAILED(hr) || !p) { respond(FAILED(hr) ? hr : E_FAIL, 0); break; }
    memcpy(respBegin(), p, a.size); unlockBuf(a.id, a.kind); respond(S_OK, a.size); break; }
  case OP_SET_CONVOLUTION: { P(PGeneric); auto it = g_devs.find(a.devId); if (it != g_devs.end() && it->second.dx) it->second.dx->SetConvolutionMonoKernel(a.a, a.b, nullptr, nullptr); break; }
  case OP_SET_GPU_PRIORITY: { P(PGeneric); auto it = g_devs.find(a.devId); if (it != g_devs.end() && it->second.dx) it->second.dx->SetGPUThreadPriority((INT) a.a); break; }
  case OP_SET_MAX_LATENCY: { P(PGeneric); auto it = g_devs.find(a.devId); if (it != g_devs.end() && it->second.dx) it->second.dx->SetMaximumFrameLatency(a.a); break; }
  case OP_COMPOSE_RECTS: break;
  case OP_DXGI_INFO: {
    RDxgiInfo* o = (RDxgiInfo*) respBegin(); memset(o, 0, sizeof *o);
    if (!g_dxgiAdapter) {
      wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
      wchar_t dll[MAX_PATH]; swprintf(dll, MAX_PATH, L"%ls\\dxgi.dll", dir);
      HMODULE m = LoadLibraryExW(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      auto cf = m ? (HRESULT (WINAPI*)(REFIID, void**)) GetProcAddress(m, "CreateDXGIFactory1") : nullptr;
      IDXGIFactory1* f = nullptr; IDXGIAdapter1* a = nullptr;
      if (cf && SUCCEEDED(cf(__uuidof(IDXGIFactory1), (void**) &f)) && SUCCEEDED(f->EnumAdapters1(0, &a))) {
        a->QueryInterface(__uuidof(IDXGIAdapter3), (void**) &g_dxgiAdapter); a->Release();
      }
      if (f) f->Release();
      logf("host DXGI %s (%ls)", g_dxgiAdapter ? "ready" : "unavailable", dll);
    }
    if (!g_dxgiAdapter) { respond(E_FAIL, 0); break; }
    DXGI_ADAPTER_DESC1 d = {}; g_dxgiAdapter->GetDesc1(&d);
    memcpy(o->description, d.Description, sizeof o->description);
    o->vendorId = d.VendorId; o->deviceId = d.DeviceId; o->subSysId = d.SubSysId; o->revision = d.Revision; o->flags = d.Flags;
    o->luidLow = d.AdapterLuid.LowPart; o->luidHigh = (uint32_t) d.AdapterLuid.HighPart;
    auto put64 = [](uint32_t& lo, uint32_t& hi, uint64_t v) { lo = (uint32_t) v; hi = (uint32_t) (v >> 32); };
    put64(o->dedicatedVideoLo, o->dedicatedVideoHi, d.DedicatedVideoMemory);
    put64(o->dedicatedSystemLo, o->dedicatedSystemHi, d.DedicatedSystemMemory);
    put64(o->sharedSystemLo, o->sharedSystemHi, d.SharedSystemMemory);
    for (int g = 0; g < 2; g++) {
      DXGI_QUERY_VIDEO_MEMORY_INFO mi = {};
      if (SUCCEEDED(g_dxgiAdapter->QueryVideoMemoryInfo(0, g ? DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi))) {
        put64(o->mem[g][0][0], o->mem[g][0][1], mi.Budget); put64(o->mem[g][1][0], o->mem[g][1][1], mi.CurrentUsage);
        put64(o->mem[g][2][0], o->mem[g][2][1], mi.AvailableForReservation); put64(o->mem[g][3][0], o->mem[g][3][1], mi.CurrentReservation);
      }
    }
    respond(S_OK, sizeof *o); break;
  }
  case OP_REMOTEVK: {
    vcrvk::Writer w;
    vkHandle(g_lk, body, r->size - (uint32_t) sizeof(VcrRec), w);
    if (r->flags & VCR_F_SYNC) {
      uint32_t n = (uint32_t) w.b.size(); if (n > respCap()) n = respCap();
      if (n) memcpy(respBegin(), w.b.data(), n);
      respond(S_OK, n);
    }
    break;
  }
  case OP_UPSCALE: {
    VCBResult res; upscaleHandle(g_lk, body, r->size - (uint32_t) sizeof(VcrRec), res);
    memcpy(respBegin(), &res, sizeof res); respond(res.hr, sizeof res); break;
  }
  default: {
    static uint32_t unknown; if (++unknown <= 20) logf("unknown op %u size %u", r->op, r->size);
    if (r->flags & VCR_F_SYNC) respond(E_NOTIMPL, 0);
  }
  }
}
#undef P

// ---------------------------------------------------------------- main loop
static void publishProgress() {
  MemoryBarrier();
  InterlockedExchange((volatile LONG*) &C->ringTail, (LONG) g_tail);
  InterlockedExchange((volatile LONG*) &C->processedSeq, (LONG) g_processed);
}
static void checkParent() {
  if (WaitForSingleObject(hParent, 0) == WAIT_OBJECT_0) {
    logf("game process exited; host exiting");
    fflush(g_log);
    TerminateProcess(GetCurrentProcess(), 0);
  }
}
static uint64_t qpcUs() { static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }(); LARGE_INTEGER c; QueryPerformanceCounter(&c); return (uint64_t) (c.QuadPart * 1000000ull / f.QuadPart); }

static void run() {
  uint64_t lastStat = qpcUs(), busyUs = 0, idleUs = 0; uint32_t recs = 0;
  for (;;) {
    uint32_t head = (uint32_t) InterlockedCompareExchange((volatile LONG*) &C->ringHead, 0, 0);
    if (head == g_tail) {
      publishProgress();
      if (C->clientWaiting & 2) SetEvent(evProgress);
      uint64_t t0 = qpcUs();
      // brief spin, then sleep on the wake event
      for (int i = 0; i < 200 && head == g_tail; i++) { YieldProcessor(); if ((i & 31) == 31) SwitchToThread(); head = (uint32_t) C->ringHead; }
      if (head == g_tail) {
        InterlockedExchange((volatile LONG*) &C->hostSleeping, 1);
        MemoryBarrier();
        head = (uint32_t) C->ringHead;
        if (head == g_tail) {
          WaitForSingleObject(evWake, 4);
          pump(); checkParent();
        }
        InterlockedExchange((volatile LONG*) &C->hostSleeping, 0);
      }
      idleUs += qpcUs() - t0;
      continue;
    }
    MemoryBarrier();   // acquire: records up to head are visible
    uint64_t t0 = qpcUs();
    uint32_t batch = 0;
    while (g_tail != head) {
      const VcrRec* r = (const VcrRec*) (g_ring + (g_tail & g_ringMask));
      if (r->size < sizeof(VcrRec) || (r->size & 7) || r->size > C->ringBytes) {
        logf("FATAL: corrupt record at %u (op %u size %u)", g_tail, r->op, r->size);
        C->hostFatal = 1; fflush(g_log); TerminateProcess(GetCurrentProcess(), 3);
      }
      if (r->op != OP_PAD) { dispatch(r); g_processed++; recs++; }
      g_tail += r->size;
      if (++batch == 64) { publishProgress(); if (C->clientWaiting & 2) SetEvent(evProgress); batch = 0; }
    }
    busyUs += qpcUs() - t0;
    uint64_t now = qpcUs();
    if (now - lastStat > 10000000ull) {
      logf("[stats] %u records in %.1f s, busy %.1f%%", recs, (now - lastStat) / 1e6, 100.0 * busyUs / (double) (now - lastStat));
      lastStat = now; busyUs = idleUs = 0; recs = 0;
      pump(); checkParent();
    }
  }
}

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* e) {
  HMODULE m = nullptr; wchar_t mod[MAX_PATH] = L"?";
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) e->ExceptionRecord->ExceptionAddress, &m) && m)
    GetModuleFileNameW(m, mod, MAX_PATH);
  logf("FATAL: exception 0x%08lx at %p (%ls +0x%llx), access %s %p", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, mod,
       (unsigned long long) ((uintptr_t) e->ExceptionRecord->ExceptionAddress - (uintptr_t) m),
       e->ExceptionRecord->NumberParameters >= 2 ? (e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read") : "?",
       e->ExceptionRecord->NumberParameters >= 2 ? (void*) e->ExceptionRecord->ExceptionInformation[1] : nullptr);
  {   // the call stack, unwound from the faulting context (x64 unwind data), as module + offset
    CONTEXT ctx = *e->ContextRecord;
    for (int i = 0; i < 24 && ctx.Rip; i++) {
      HMODULE fm = nullptr; wchar_t fn[MAX_PATH] = L"?";
      if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) ctx.Rip, &fm) && fm)
        GetModuleFileNameW(fm, fn, MAX_PATH);
      const wchar_t* base = wcsrchr(fn, L'\\'); base = base ? base + 1 : fn;
      logf("  #%02d %ls +0x%llx", i, base, (unsigned long long) (ctx.Rip - (uintptr_t) fm));
      DWORD64 imageBase = 0;
      PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
      if (!fe) { ctx.Rip = *(DWORD64*) ctx.Rsp; ctx.Rsp += 8; continue; }
      void* handlerData = nullptr; DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fe, &ctx, &handlerData, &establisher, nullptr);
    }
  }
  if (C) C->hostFatal = 2;
  if (g_log) fflush(g_log);
  return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  InitializeCriticalSection(&g_logLock);
  int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  // usage: VCRenderHost.exe <base-name> <client-pid> <log-path>
  if (argc >= 4) g_log = _wfopen(argv[3], L"wb");
  logf("VCRender host starting (protocol %u, pid %lu)", VCR_PROTOCOL, GetCurrentProcessId());
  if (argc < 4) { logf("bad command line"); return 2; }
  SetUnhandledExceptionFilter(crashFilter);
  wcsncpy(g_base, argv[1], 127);
  DWORD clientPid = (DWORD) wcstoul(argv[2], nullptr, 10);
  hParent = OpenProcess(SYNCHRONIZE, FALSE, clientPid);
  wchar_t name[200];
  swprintf(name, 200, L"%ls_mem", g_base);
  HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
  if (!map) { logf("cannot open channel %ls (%lu)", name, GetLastError()); return 2; }
  uint8_t* base = (uint8_t*) MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!base) { logf("cannot map channel (%lu)", GetLastError()); return 2; }
  C = (VcrControl*) base;
  if (C->magic != VCR_MAGIC || C->protocol != VCR_PROTOCOL) { logf("protocol mismatch (magic %08x protocol %u)", C->magic, C->protocol); return 2; }
  g_ring = base + C->controlBytes; g_ringMask = C->ringBytes - 1;
  g_arena = g_ring + C->ringBytes; g_resp = g_arena + C->arenaBytes;
  auto ev = [&](const wchar_t* tag) { swprintf(name, 200, L"%ls_%ls", g_base, tag); return OpenEventW(EVENT_ALL_ACCESS, FALSE, name); };
  evWake = ev(L"wake"); evResp = ev(L"resp"); evProgress = ev(L"progress"); evFrame = ev(L"frame");
  if (!evWake || !evResp || !evProgress || !evFrame) { logf("cannot open events (%lu)", GetLastError()); return 2; }
  C->hostPid = (int32_t) GetCurrentProcessId();

  // DXVK lives beside this executable
  wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* s = wcsrchr(dir, L'\\'); if (s) *s = 0;
  wchar_t dll[MAX_PATH]; swprintf(dll, MAX_PATH, L"%ls\\d3d9.dll", dir);
  g_dxvk = LoadLibraryExW(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!g_dxvk) { logf("cannot load %ls (%lu)", dll, GetLastError()); snprintf(C->hostMessage, 511, "cannot load DXVK d3d9.dll (%lu)", GetLastError()); C->hostReady = -1; return 2; }
  auto create = (HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)) GetProcAddress(g_dxvk, "Direct3DCreate9Ex");
  HRESULT hr = create ? create(D3D_SDK_VERSION, &g_d3d) : E_FAIL;
  if (FAILED(hr) || !g_d3d) { logf("Direct3DCreate9Ex failed 0x%08lx", (unsigned long) hr); snprintf(C->hostMessage, 511, "Direct3DCreate9Ex failed 0x%08lx", (unsigned long) hr); C->hostReady = -1; return 2; }
  auto create9 = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(g_dxvk, "Direct3DCreate9");
  g_d3d9 = create9 ? create9(D3D_SDK_VERSION) : nullptr;
  if (!g_d3d9) { logf("Direct3DCreate9 failed"); snprintf(C->hostMessage, 511, "Direct3DCreate9 failed"); C->hostReady = -1; return 2; }
  D3DADAPTER_IDENTIFIER9 id = {}; g_d3d->GetAdapterIdentifier(0, 0, &id);
  logf("DXVK loaded from %ls; adapter: %s", dll, id.Description);
  InitializeCriticalSection(&g_jobLock); InitializeConditionVariable(&g_jobCv); InitializeConditionVariable(&g_doneCv);
  g_objs.resize(65536);
  CreateThread(nullptr, 0, copierThread, nullptr, 0, nullptr);
  g_tail = (uint32_t) C->ringTail;
  MemoryBarrier();
  vkStartSideChannel(g_base);
  C->hostReady = 1;
  logf("ready");
  run();
  return 0;
}
