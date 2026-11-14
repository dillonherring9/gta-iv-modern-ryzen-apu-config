// 1080p present soak: ~30 frames per second for ~20 s with a moving bar, to measure the frame copy and GDI blit.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM l) { return DefWindowProcW(w, m, a, l); }
int main(int argc, char** argv) {
  FILE* out = fopen(argc > 2 ? argv[2] : "results.txt", "w");
  HMODULE dll = LoadLibraryA(argv[1]); auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(dll, "Direct3DCreate9");
  IDirect3D9* d3d = create(D3D_SDK_VERSION);
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRenderSoak"; RegisterClassW(&wc);
  RECT rc = {0, 0, 1920, 1080}; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  HWND w = CreateWindowW(wc.lpszClassName, L"VCRender soak", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = 1920; p.BackBufferHeight = 1080; p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = w; p.Windowed = TRUE; p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9* dev = nullptr; d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &p, &dev);
  LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a); double presentMs = 0, worst = 0;
  for (int i = 0; i < 600; i++) {
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1, 0);
    D3DRECT bar = {(i * 7) % 1800, 0, (i * 7) % 1800 + 120, 1080}; dev->Clear(1, &bar, D3DCLEAR_TARGET, 0xFFF0C040, 1, 0);
    LARGE_INTEGER s, e; QueryPerformanceCounter(&s); dev->Present(nullptr, nullptr, nullptr, nullptr); QueryPerformanceCounter(&e);
    double ms = (e.QuadPart - s.QuadPart) * 1000.0 / f.QuadPart; presentMs += ms; if (i > 10 && ms > worst) worst = ms;
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    Sleep(25);
  }
  QueryPerformanceCounter(&b);
  fprintf(out, "info 600 frames in %.1f s, Present call %.2f ms average, worst %.2f ms\n", (b.QuadPart - a.QuadPart) / (double) f.QuadPart, presentMs / 600, worst);
  dev->Release(); d3d->Release(); fclose(out); return 0;
}
