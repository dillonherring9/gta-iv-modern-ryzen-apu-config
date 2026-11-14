// Stand-in for GTAIV.exe start-up order: FusionFix's d3d9.dll proxy is mapped first (it loads vulkan.dll =
// 32-bit DXVK when d3d9.cfg says API=1), then the ASI loader runs plugins\VCRender.asi's InitializeASI,
// then the program calls Direct3DCreate9 through the proxy export.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>
#include <dxgi.h>
#include <stdio.h>
#include "../src/shared/vcr_maps.h"

static FILE* out; static int failures;
static void result(bool ok, const char* what, long detail) {
  fprintf(out, "RESULT %s %s (%ld)\n", ok ? "PASS" : "FAIL", what, detail); fflush(out); if (!ok) failures++;
}
static bool inside(HMODULE mod, const void* p) {
  MODULEINFO mi; if (!mod || !GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof mi)) return false;
  return (const BYTE*) p >= (const BYTE*) mi.lpBaseOfDll && (const BYTE*) p < (const BYTE*) mi.lpBaseOfDll + mi.SizeOfImage;
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int main() {
  out = fopen("proxy-results.txt", "w");
  HMODULE proxy = LoadLibraryA("d3d9.dll");
  result(proxy != nullptr, "FusionFix proxy loaded", proxy ? 0 : (long) GetLastError());
  if (!proxy) return 1;
  result(GetModuleHandleA("vulkan.dll") != nullptr, "proxy loaded vulkan.dll (API=1)", 0);
  result(GetModuleHandleA("winevulkan.dll") == nullptr, "no winevulkan.dll after the proxy", 0);
  HMODULE asi = LoadLibraryA("plugins\\VCRender.asi");
  result(asi != nullptr, "VCRender.asi loaded", asi ? 0 : (long) GetLastError());
  if (!asi) return 1;
  auto init = (void (__cdecl*)()) GetProcAddress(asi, "InitializeASI");
  result(init != nullptr, "InitializeASI export", 0);
  if (init) init();
  auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(proxy, "Direct3DCreate9");
  IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
  result(d3d != nullptr, "Direct3DCreate9 through the proxy", 0);
  if (!d3d) return 1;
  result(inside(asi, *(void**) d3d), "IDirect3D9 belongs to VCRender", 0);
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRenderProxyHost"; RegisterClassW(&wc);
  HWND w = CreateWindowW(wc.lpszClassName, L"VCRender proxy host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 660, 520, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = 640; p.BackBufferHeight = 480; p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = w; p.Windowed = TRUE; p.EnableAutoDepthStencil = TRUE; p.AutoDepthStencilFormat = D3DFMT_D24S8;
  IDirect3DDevice9* dev = nullptr;
  HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p, &dev);
  result(SUCCEEDED(hr), "CreateDevice", hr);
  if (!dev) return 1;
  result(inside(asi, *(void**) dev), "device belongs to VCRender", 0);
  int ok = 0;
  for (int i = 0; i < 120; i++) {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB((i * 2) & 255, 64, 128), 1, 0);
    dev->BeginScene(); dev->EndScene();
    if (SUCCEEDED(dev->Present(nullptr, nullptr, nullptr, nullptr))) ok++;
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    Sleep(8);
  }
  result(ok == 120, "120 presents", ok);
  {   // the game's own video-memory routine (GTAIV.exe 0x41F7B0) runs through dxgi.dll
    HMODULE dx = LoadLibraryA("dxgi.dll");
    auto cf = (HRESULT (WINAPI*)(REFIID, void**)) GetProcAddress(dx, "CreateDXGIFactory");
    IDXGIFactory* f = nullptr; IDXGIAdapter* a = nullptr; DXGI_ADAPTER_DESC d = {};
    HRESULT h = cf ? cf(__uuidof(IDXGIFactory), (void**) &f) : E_FAIL;
    if (SUCCEEDED(h)) h = f->EnumAdapters(0, &a);
    if (SUCCEEDED(h)) h = a->GetDesc(&d);
    result(SUCCEEDED(h) && d.DedicatedVideoMemory > 0, "DXGI adapter description (game routine)", (long) (d.DedicatedVideoMemory >> 20));
    if (a) a->Release(); if (f) f->Release(); FreeLibrary(dx);
  }
  result(GetModuleHandleA("winevulkan.dll") == nullptr, "no winevulkan.dll in the game process after rendering and DXGI", 0);
  VcrMaps m; vcrReadMaps(m);
  fprintf(out, "INFO Linux maps below 4 GiB: mapped %llu MiB, largest gap %llu MiB, graphics %llu MiB (LLVM %llu, GPU buffers %llu)\n",
          m.mapped >> 20, m.largestGap >> 20, m.graphics >> 20, m.llvm >> 20, m.gpuBuffers >> 20);
  result(m.ok && m.llvm == 0, "no LLVM/graphics driver mapped in the game process", (long) (m.graphics >> 20));
  ULONG left = dev->Release(); result(left == 0, "device released", (long) left);
  d3d->Release(); DestroyWindow(w);
  fprintf(out, "DONE failures=%d\n", failures); fclose(out);
  return failures ? 5 : 0;
}
