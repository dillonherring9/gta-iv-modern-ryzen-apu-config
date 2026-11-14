// Replays GTAIV.exe's DXGI video-memory routine (0x41F7B0) and FusionFix's fallback, then reports whether Vulkan
// was loaded into this process. usage: dxgi_test.exe <control|VCRender.asi> <results>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include "../src/shared/vcr_maps.h"
int main(int argc, char** argv) {
  FILE* o = fopen(argv[2], "w");
  if (strcmp(argv[1], "control")) {
    HMODULE v = LoadLibraryA(argv[1]); auto init = (void (__cdecl*)()) GetProcAddress(v, "VCR_InitStandalone");
    fprintf(o, "plugin %s\n", init ? "loaded" : "MISSING"); if (init) init();
  } else fprintf(o, "plugin loaded\n");
  // the game: LoadLibrary, GetProcAddress, factory, adapters, outputs, adapter desc, FreeLibrary
  HMODULE dx = LoadLibraryA("dxgi.dll");
  auto create = (HRESULT (WINAPI*)(REFIID, void**)) GetProcAddress(dx, "CreateDXGIFactory");
  IDXGIFactory* f = nullptr; HRESULT hr = create(__uuidof(IDXGIFactory), (void**) &f);
  fprintf(o, "game CreateDXGIFactory %08lx\n", hr);
  IDXGIAdapter* a = nullptr;
  for (UINT i = 0; f && SUCCEEDED(f->EnumAdapters(i, &a)); i++) {
    IDXGIOutput* out = nullptr;
    for (UINT j = 0; SUCCEEDED(a->EnumOutputs(j, &out)); j++) {
      DXGI_OUTPUT_DESC od = {}; out->GetDesc(&od);
      POINT p = {0, 0};
      fprintf(o, "adapter %u output %u: %ldx%ld at %ld,%ld attached %d primary-monitor %d\n", i, j, od.DesktopCoordinates.right - od.DesktopCoordinates.left,
              od.DesktopCoordinates.bottom - od.DesktopCoordinates.top, od.DesktopCoordinates.left, od.DesktopCoordinates.top, od.AttachedToDesktop,
              od.Monitor == MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY));
      UINT n = 0; out->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, 0, &n, nullptr); fprintf(o, "  modes %u\n", n);
      out->Release();
    }
    DXGI_ADAPTER_DESC d = {}; a->GetDesc(&d);
    fprintf(o, "adapter %u: %ls vendor %04x device %04x dedicated video %lu bytes, dedicated system %lu, shared system %lu\n", i, d.Description,
            d.VendorId, d.DeviceId, (unsigned long) d.DedicatedVideoMemory, (unsigned long) d.DedicatedSystemMemory, (unsigned long) d.SharedSystemMemory);
    a->Release();
  }
  if (f) f->Release();
  FreeLibrary(dx);
  // FusionFix: CreateDXGIFactory2(IDXGIFactory3) else CreateDXGIFactory, then IDXGIAdapter3::QueryVideoMemoryInfo
  dx = LoadLibraryA("dxgi.dll");
  auto c2 = (HRESULT (WINAPI*)(UINT, REFIID, void**)) GetProcAddress(dx, "CreateDXGIFactory2");
  IDXGIFactory3* f3 = nullptr; HRESULT h2 = c2 ? c2(0, __uuidof(IDXGIFactory3), (void**) &f3) : E_FAIL;
  fprintf(o, "fusionfix CreateDXGIFactory2(IDXGIFactory3) %s\n", SUCCEEDED(h2) ? "ok" : "refused (falls back)");
  IDXGIFactory* ff = f3 ? (IDXGIFactory*) f3 : nullptr;
  if (!ff) { auto c = (HRESULT (WINAPI*)(REFIID, void**)) GetProcAddress(dx, "CreateDXGIFactory"); c(__uuidof(IDXGIFactory), (void**) &ff); }
  IDXGIAdapter* fa = nullptr; IDXGIAdapter3* a3 = nullptr;
  if (ff && SUCCEEDED(ff->EnumAdapters(0, &fa)) && SUCCEEDED(fa->QueryInterface(__uuidof(IDXGIAdapter3), (void**) &a3))) {
    DXGI_QUERY_VIDEO_MEMORY_INFO mi = {}; HRESULT q = a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi);
    fprintf(o, "fusionfix QueryVideoMemoryInfo %08lx budget nonzero %d\ninfo budget %llu MiB usage %llu MiB\n", q, mi.Budget > 0, mi.Budget >> 20, mi.CurrentUsage >> 20);
    a3->Release();
  }
  if (fa) fa->Release(); if (ff) ff->Release(); FreeLibrary(dx);
  VcrMaps m; vcrReadMaps(m);
  fprintf(o, "info winevulkan loaded %s; Linux maps: graphics %llu MiB (LLVM %llu), largest gap %llu MiB\n",
          GetModuleHandleA("winevulkan.dll") ? "yes" : "no", m.graphics >> 20, m.llvm >> 20, m.largestGap >> 20);
  fclose(o); return 0;
}
