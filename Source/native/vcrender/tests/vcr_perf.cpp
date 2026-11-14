// CPU-side throughput test: many small draws with state changes and dynamic buffer updates per frame,
// presented at 1920x1080. GPU work is tiny, so this measures API/transport cost and the present path.
// usage: vcr_perf.exe <d3d9 dll> <results file>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/shared/vcr_maps.h"

static FILE* out;
#define R(...) do { fprintf(out, __VA_ARGS__); fputc('\n', out); fflush(out); } while (0)
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM l) { return DefWindowProcW(w, m, a, l); }
struct VC { float x, y, z, rhw; DWORD c; };

static void addressInfo(const char* when) {
  VcrMaps m; vcrReadMaps(m);
  R("info %s: Linux maps below 4 GiB: mapped %llu MiB, unmapped %llu MiB, largest gap %llu MiB, graphics %llu MiB (GPU buffers %llu, LLVM %llu); winevulkan loaded: %s",
    when, m.mapped >> 20, m.unmapped >> 20, m.largestGap >> 20, m.graphics >> 20, m.gpuBuffers >> 20, m.llvm >> 20, GetModuleHandleW(L"winevulkan.dll") ? "yes" : "no");
}
int main(int argc, char** argv) {
  if (argc < 3) return 1;
  out = fopen(argv[2], "w"); if (!out) return 1;
  HMODULE dll = LoadLibraryA(argv[1]); if (!dll) return 2;
  auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(dll, "Direct3DCreate9");
  IDirect3D9* d3d = create(D3D_SDK_VERSION); if (!d3d) return 3;
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRenderPerf"; RegisterClassW(&wc);
  RECT rc = {0, 0, 1920, 1080}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  HWND w = CreateWindowW(wc.lpszClassName, L"VCRender perf", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = 1920; p.BackBufferHeight = 1080; p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = w; p.Windowed = TRUE; p.EnableAutoDepthStencil = TRUE; p.AutoDepthStencilFormat = D3DFMT_D24S8;
  p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9* dev = nullptr;
  if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p, &dev))) { R("CreateDevice failed"); return 4; }
  addressInfo("after device");
  IDirect3DVertexBuffer9* vb = nullptr; dev->CreateVertexBuffer(4 * sizeof(VC), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb, nullptr);
  VC* v; vb->Lock(0, 0, (void**) &v, 0); v[0] = {0, 0, 0, 1, 0xFF808080}; v[1] = {8, 0, 0, 1, 0xFF808080}; v[2] = {0, 8, 0, 1, 0xFF808080}; v[3] = {8, 8, 0, 1, 0xFF808080}; vb->Unlock();
  IDirect3DVertexBuffer9* dyn = nullptr; dev->CreateVertexBuffer(1 << 20, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT, &dyn, nullptr);
  IDirect3DTexture9* tex[8] = {};
  for (int i = 0; i < 8; i++) { dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex[i], nullptr); D3DLOCKED_RECT lr; tex[i]->LockRect(0, &lr, nullptr, 0); memset(lr.pBits, 0x40 + i * 16, 64 * lr.Pitch); tex[i]->UnlockRect(0); }
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); dev->SetRenderState(D3DRS_ZENABLE, FALSE); dev->SetRenderState(D3DRS_LIGHTING, FALSE); dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  const int frames = 300, draws = 2000, dynDraws = 100;
  LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
  double worst = 0; LARGE_INTEGER prev; QueryPerformanceCounter(&t0); prev = t0;
  uint32_t dynOff = 0;
  for (int fr = 0; fr < frames; fr++) {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000 | (fr & 255), 1, 0);
    dev->BeginScene();
    dev->SetStreamSource(0, vb, 0, sizeof(VC));
    for (int i = 0; i < draws; i++) {
      dev->SetRenderState(D3DRS_ALPHABLENDENABLE, i & 1); dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFF000000 | i);
      dev->SetTexture(0, tex[i & 7]); dev->SetSamplerState(0, D3DSAMP_MINFILTER, (i & 2) ? D3DTEXF_LINEAR : D3DTEXF_POINT);
      float c[16] = {(float) i}; dev->SetVertexShaderConstantF(0, c, 4);
      dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
    }
    for (int i = 0; i < dynDraws; i++) {
      if (dynOff + 4 * sizeof(VC) > (1u << 20)) dynOff = 0;
      VC* d; dyn->Lock(dynOff, 4 * sizeof(VC), (void**) &d, dynOff ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD);
      float x = (float) ((i * 37) % 1900), y = (float) ((i * 53) % 1060);
      d[0] = {x, y, 0, 1, 0xFFFF0000}; d[1] = {x + 16, y, 0, 1, 0xFF00FF00}; d[2] = {x, y + 16, 0, 1, 0xFF0000FF}; d[3] = {x + 16, y + 16, 0, 1, 0xFFFFFFFF};
      dyn->Unlock(); dev->SetStreamSource(0, dyn, 0, sizeof(VC)); dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, dynOff / sizeof(VC), 2); dynOff += 4 * sizeof(VC);
    }
    dev->EndScene();
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    double ms = (now.QuadPart - prev.QuadPart) * 1000.0 / f.QuadPart; if (fr > 10 && ms > worst) worst = ms; prev = now;
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
  }
  QueryPerformanceCounter(&t1);
  double total = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
  R("info %d frames x (%d draws + %d dynamic updates): %.2f ms per frame average, worst %.2f ms, %.0f calls per ms",
    frames, draws, dynDraws, total / frames, worst, frames * (draws * 6.0 + dynDraws * 4.0) / total);
  addressInfo("end");
  for (auto* t : tex) t->Release(); vb->Release(); dyn->Release();
  R("release %lu", dev->Release()); d3d->Release();
  fclose(out); return 0;
}
