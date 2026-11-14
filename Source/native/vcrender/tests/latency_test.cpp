// GPU-bound stand-in for the game, for VCRender's Present-to-screen latency line (VCRender.log [present]).
// Each frame: 15 ms of "game logic" on the CPU, then many alpha-blended full-screen quads at 1920x1200 so the GPU is the
// bottleneck (as GTA IV is on the 680M), then Present. Runs ~16 s. usage: latency_test.exe VCRender.asi results.txt
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM a, LPARAM l) { return DefWindowProcW(w, m, a, l); }
struct V { float x, y, z, w; DWORD c; };
int main(int argc, char** argv) {
  FILE* out = fopen(argc > 2 ? argv[2] : "results.txt", "w");
  HMODULE dll = LoadLibraryA(argv[1]);
  auto init = (void (__cdecl*)()) GetProcAddress(dll, "VCR_InitStandalone"); if (init) init();
  auto create = (IDirect3D9* (WINAPI*)(UINT)) GetProcAddress(dll, "Direct3DCreate9");
  IDirect3D9* d3d = create(D3D_SDK_VERSION);
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"VCRLatency"; RegisterClassW(&wc);
  HWND w = CreateWindowW(wc.lpszClassName, L"VCRender latency test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 960, 600, nullptr, nullptr, wc.hInstance, nullptr);
  D3DPRESENT_PARAMETERS p = {}; p.BackBufferWidth = 1920; p.BackBufferHeight = 1200; p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
  p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = w; p.Windowed = TRUE; p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9* dev = nullptr;
  if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &p, &dev))) { fprintf(out, "CreateDevice failed\n"); return 1; }
  const int quads = argc > 3 ? atoi(argv[3]) : 300;
  V q[4] = { {-0.5f, -0.5f, 0, 1, 0x08FFFFFF}, {1919.5f, -0.5f, 0, 1, 0x08FFFFFF}, {-0.5f, 1199.5f, 0, 1, 0x08FFFFFF}, {1919.5f, 1199.5f, 0, 1, 0x08FFFFFF} };
  dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE); dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetRenderState(D3DRS_LIGHTING, FALSE); dev->SetRenderState(D3DRS_ZENABLE, FALSE);
  LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
  int frames = 0;
  for (;;) {
    QueryPerformanceCounter(&b); if ((b.QuadPart - a.QuadPart) / (double) f.QuadPart > 16.0) break;
    Sleep(15);   // game logic
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1, 0);
    dev->BeginScene(); for (int i = 0; i < quads; i++) dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V)); dev->EndScene();
    dev->Present(nullptr, nullptr, nullptr, nullptr); frames++;
    MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
  }
  QueryPerformanceCounter(&b);
  fprintf(out, "frames %d in %.1f s = %.1f fps (quads %d)\n", frames, (b.QuadPart - a.QuadPart) / (double) f.QuadPart, frames / ((b.QuadPart - a.QuadPart) / (double) f.QuadPart), quads);
  dev->Release(); d3d->Release(); DestroyWindow(w); fclose(out); return 0;
}
