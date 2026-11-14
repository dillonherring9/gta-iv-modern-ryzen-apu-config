// Phase 3 isolated test (32-bit, like the game): VCRender.asi -> host -> DXVK x64.
//  A. DXVK interop and the Vulkan subset DLSS-IV's game side uses, carried to the host: handles, properties, image info,
//     command pool/buffers, a timeline semaphore, a recorded copy (colour texture -> colour texture, checked by readback)
//     and the depth-stencil -> INTZ copy DLSS-IV's depth snapshot makes, waits on the side channel.
//  B. The bulk upscale (VCB_UpscaleFrame -> native service -> dlssiv-helper-bridge.exe -> FSR 3.1): three frames, each a
//     new scene colour that must reach the output; a foreign pointer refused; status; shutdown, Reset, three more frames.
//     Adapted from research/codex-bbridge-standalone-2026-11-11/tests/rpc-host.cpp.
//  C. No Vulkan loader or graphics driver in this process afterwards.
// Layout: phase3_test.exe, VCRender.asi, VCRender\ (host, DXVK x64 d3d9/dxgi, dxvk.conf, dlssiv-helper-bridge.exe),
// DLSSIV\ (FidelityFX DLLs). usage: phase3_test.exe [results.txt]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d9.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include "../src/upscale/dxvk_interop_min.h"
#include "../src/upscale/vc_upscale_wire.h"
#include "../src/shared/vcr_maps.h"

static FILE* out;
static int failures;
static bool Check(bool ok, const char* what, long detail = 0) {
  std::fprintf(out, "RESULT %s %s (%ld)\n", ok ? "PASS" : "FAIL", what, detail); std::fflush(out);
  if (!ok) ++failures; return ok;
}
static float Half(uint16_t h) {
  const int sign = h & 0x8000 ? -1 : 1, exp = (h >> 10) & 31;
  const float mant = (float) (h & 1023);
  if (exp == 31) return mant ? NAN : sign * INFINITY;
  return exp ? sign * std::ldexp(1.0f + mant / 1024.0f, exp - 15) : sign * std::ldexp(mant, -24);
}
static bool Fill(IDirect3DDevice9* d, IDirect3DTexture9* t, D3DCOLOR color) {
  IDirect3DSurface9 *old = nullptr, *target = nullptr;
  d->GetRenderTarget(0, &old); t->GetSurfaceLevel(0, &target);
  const bool ok = target && SUCCEEDED(d->SetRenderTarget(0, target)) && SUCCEEDED(d->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0));
  if (old) { d->SetRenderTarget(0, old); old->Release(); }
  if (target) target->Release(); return ok;
}
static bool Texture(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f, IDirect3DTexture9** t, DWORD usage = D3DUSAGE_RENDERTARGET) {
  return SUCCEEDED(d->CreateTexture(w, h, 1, usage, f, D3DPOOL_DEFAULT, t, nullptr)) && *t;
}
static bool Read(IDirect3DDevice9* d, IDirect3DTexture9* t, UINT w, UINT h, float (&px)[4]) {
  IDirect3DSurface9 *gpu = nullptr, *cpu = nullptr;
  bool ok = SUCCEEDED(t->GetSurfaceLevel(0, &gpu)) && gpu &&
    SUCCEEDED(d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &cpu, nullptr)) && cpu &&
    SUCCEEDED(d->GetRenderTargetData(gpu, cpu));
  D3DLOCKED_RECT lock = {};
  if (ok && (ok = SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)))) {
    const uint16_t* p = (const uint16_t*) ((const unsigned char*) lock.pBits + (h / 2) * lock.Pitch) + (w / 2) * 4;
    for (int i = 0; i < 4; ++i) px[i] = Half(p[i]);
    cpu->UnlockRect();
  }
  if (cpu) cpu->Release(); if (gpu) gpu->Release(); return ok;
}
static bool Utf8(const wchar_t* w, char* o, int cap) { return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, o, cap, nullptr, nullptr) > 0; }
using UpscaleFn = HRESULT (WINAPI*)(const VCBConfig*, const VCBInput*, VCBResult*);
using DeviceFn = HRESULT (WINAPI*)(void*, VCBResult*);
struct Set { IDirect3DTexture9 *scene = nullptr, *home = nullptr, *depth = nullptr, *mv = nullptr; };
static void Free(Set& s) { IDirect3DTexture9** all[4] = { &s.scene, &s.home, &s.depth, &s.mv }; for (auto t : all) if (*t) { (*t)->Release(); *t = nullptr; } }

static void Frames(IDirect3DDevice9* d, UpscaleFn up, const VCBConfig& c, Set& s, uint64_t& seq, UINT rw, UINT rh, UINT ow, UINT oh, const char* label) {
  char what[128];
  Fill(d, s.home, D3DCOLOR_ARGB(255, 128, 128, 128)); Fill(d, s.depth, D3DCOLOR_ARGB(255, 128, 128, 128)); Fill(d, s.mv, 0);
  const D3DCOLOR colors[3] = { D3DCOLOR_ARGB(255, 192, 24, 8), D3DCOLOR_ARGB(255, 8, 192, 24), D3DCOLOR_ARGB(255, 24, 8, 192) };
  for (unsigned n = 0; n < 3; ++n) {
    Fill(d, s.scene, colors[n]);
    VCBInput in = {}; in.size = sizeof(in); in.version = VCB_ABI_VERSION;
    in.device = d; in.scene = s.scene; in.home = s.home; in.depth = s.depth; in.mv = s.mv;
    in.frame.sequence = ++seq; in.frame.render_width = rw; in.frame.render_height = rh; in.frame.output_width = ow; in.frame.output_height = oh;
    in.frame.flags = VCB_FRAME_VALID | VCB_FRAME_GUIDES_VALID | (n == 0 ? VCB_FRAME_RESET : VCB_FRAME_PREVIOUS_VALID);
    in.frame.camera_near = 0.1f; in.frame.fov_y_rad = 1.0f; in.frame.frame_time_ms = 16.667f; in.frame.mv_scale_x = 1.0f; in.frame.mv_scale_y = 1.0f;
    VCBResult r = {}; r.size = sizeof(r); r.version = VCB_ABI_VERSION;
    const HRESULT hr = up(&c, &in, &r);
    r.message[sizeof(r.message) - 1] = 0;
    std::fprintf(out, "%s frame %u: hr=%08lx service=%08lx backend=%d flags=%u seq=%llu %s\n", label, n + 1,
                 (unsigned long) hr, (unsigned long) r.hr, r.backend, r.flags, (unsigned long long) r.sequence, r.message);
    std::snprintf(what, sizeof what, "%s frame %u upscaled by FSR in the host and copied home", label, n + 1);
    Check(SUCCEEDED(hr) && SUCCEEDED((HRESULT) r.hr) && r.sequence == seq && (r.flags & 3u) == 3u && r.backend == 1, what, hr);
    float px[4] = {};
    const bool read = Read(d, s.home, ow, oh, px);
    std::fprintf(out, "%s frame %u readback %.5f %.5f %.5f %.5f\n", label, n + 1, px[0], px[1], px[2], px[3]);
    bool finite = read; for (float v : px) finite = finite && std::isfinite(v);
    std::snprintf(what, sizeof what, "%s frame %u output shows this frame's colour", label, n + 1);
    Check(finite && px[n] > 0.1f && px[n] > px[(n + 1) % 3] * 2.0f && px[n] > px[(n + 2) % 3] * 2.0f, what);
  }
}


// This fixture runs only in a copied private test prefix. Termination is limited
// to one exact helper whose Windows parent is the exact host parented by us.
static bool KillOwnHelper() {
  struct P { DWORD pid, parent; std::wstring name; };
  std::vector<P> rows;
  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snap==INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W pe={};pe.dwSize=sizeof(pe);
  if(Process32FirstW(snap,&pe)) do { rows.push_back({pe.th32ProcessID,pe.th32ParentProcessID,pe.szExeFile}); } while(Process32NextW(snap,&pe));
  CloseHandle(snap);
  DWORD host=0,helper=0;unsigned hosts=0,helpers=0;
  for(const auto& p:rows) if(p.parent==GetCurrentProcessId()&&!_wcsicmp(p.name.c_str(),L"VCRenderHost.exe")){host=p.pid;hosts++;}
  for(const auto& p:rows) if(p.parent==host&&!_wcsicmp(p.name.c_str(),L"dlssiv-helper-bridge.exe")){helper=p.pid;helpers++;}
  std::fprintf(out,"IDENTITY fixture=%lu host=%lu host_matches=%u helper=%lu helper_matches=%u\n",(unsigned long)GetCurrentProcessId(),(unsigned long)host,hosts,(unsigned long)helper,helpers);std::fflush(out);
  if(hosts!=1||helpers!=1) return false;
  HANDLE child=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,helper);
  if(!child)return false;
  wchar_t image[32768]={};DWORD n=32768;
  bool good=QueryFullProcessImageNameW(child,0,image,&n)!=FALSE;
  const wchar_t* tail=wcsrchr(image,L'\\');tail=tail?tail+1:image;
  good=good&&!_wcsicmp(tail,L"dlssiv-helper-bridge.exe");
  std::fprintf(out,"IDENTITY helper_path_verified=%d\n",good);std::fflush(out);
  if(good)good=TerminateProcess(child,73)!=FALSE;
  if(good)good=WaitForSingleObject(child,5000)==WAIT_OBJECT_0;
  CloseHandle(child);return good;
}

int main(int argc,char**argv){
  wchar_t dir[MAX_PATH],path[MAX_PATH];GetModuleFileNameW(nullptr,dir,MAX_PATH);*wcsrchr(dir,L'\\')=0;
  out=fopen(argc>1?argv[1]:"quarantine-results.txt","w");if(!out)return 3;
  std::fprintf(out,"QUARANTINE_PROBE private fixture, completed readback before helper termination; shutdown recorded before testing Reset and Release\n");
  swprintf(path,MAX_PATH,L"%ls\\VCRender.asi",dir);HMODULE vcr=LoadLibraryW(path);
  auto init=vcr?(void(__cdecl*)())GetProcAddress(vcr,"VCR_InitStandalone"):nullptr;
  if(!Check(init!=nullptr,"VCRender.asi loaded"))return 1;init();
  auto up=(UpscaleFn)GetProcAddress(vcr,"VCB_UpscaleFrame");auto shutdown=(DeviceFn)GetProcAddress(vcr,"VCB_UpscaleShutdown");
  if(!Check(up&&shutdown,"native upscale exports"))return 1;
  auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(vcr,"Direct3DCreate9");IDirect3D9*d3d=create(D3D_SDK_VERSION);
  if(!Check(d3d!=nullptr,"Direct3DCreate9"))return 1;
  WNDCLASSW wc={};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"QuarantineProbe";RegisterClassW(&wc);
  HWND wnd=CreateWindowExW(0,wc.lpszClassName,L"Isolated FSR quarantine probe",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,336,359,nullptr,nullptr,wc.hInstance,nullptr);
  D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.hDeviceWindow=wnd;pp.BackBufferWidth=pp.BackBufferHeight=320;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9*d=nullptr;HRESULT hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,wnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d);
  if(!Check(SUCCEEDED(hr)&&d,"CreateDevice",hr))return 1;
  VCBConfig c={};c.size=sizeof c;c.version=VCB_ABI_VERSION;c.backend=1;c.fsr_version=3;c.preset=11;c.timeout_ms=30000;c.depth_inverted=1;c.flags_override=-1;
  swprintf(path,MAX_PATH,L"%ls\\VCRender\\dlssiv-helper-bridge.exe",dir);Utf8(path,c.helper_path,sizeof c.helper_path);swprintf(path,MAX_PATH,L"%ls\\DLSSIV",dir);Utf8(path,c.vendor_dir,sizeof c.vendor_dir);
  Set s;uint64_t seq=0;
  if(!Check(Texture(d,64,64,D3DFMT_A16B16G16R16F,&s.scene)&&Texture(d,96,96,D3DFMT_A16B16G16R16F,&s.home)&&Texture(d,64,64,D3DFMT_R32F,&s.depth)&&Texture(d,64,64,D3DFMT_G16R16F,&s.mv),"textures"))return 1;
  Frames(d,up,c,s,seq,64,64,96,96,"pre-kill-retired");
  // Frames performs synchronous GetRenderTargetData+LockRect after each FSR
  // frame, so the final copy-out and preceding helper work have completed.
  if(failures){std::fprintf(out,"ABORT pre-kill setup failed; helper not terminated\n");std::fflush(out);return 2;}
  if(!Check(KillOwnHelper(),"only fixture-owned helper terminated and exited")){std::fflush(out);return 2;}
  Sleep(750); // let the existing FAST watchdog observe process exit (not a GPU hang)
  // One normal frame after known child death causes BRIDGE's broken-pipe
  // failure. Its input copy waits for retirement before sending to the pipe;
  // it queues no wait for a dead helper. FAST rejects via its death latch.
  VCBInput in={};in.size=sizeof in;in.version=VCB_ABI_VERSION;in.device=d;in.scene=s.scene;in.home=s.home;in.depth=s.depth;in.mv=s.mv;
  in.frame.sequence=++seq;in.frame.render_width=in.frame.render_height=64;in.frame.output_width=in.frame.output_height=96;in.frame.flags=VCB_FRAME_VALID|VCB_FRAME_GUIDES_VALID|VCB_FRAME_PREVIOUS_VALID;in.frame.camera_near=0.1f;in.frame.fov_y_rad=1.0f;in.frame.frame_time_ms=16.667f;in.frame.mv_scale_x=in.frame.mv_scale_y=1.0f;
  VCBResult failed={};failed.size=sizeof failed;failed.version=VCB_ABI_VERSION;hr=up(&c,&in,&failed);failed.message[sizeof(failed.message)-1]=0;
  std::fprintf(out,"POST_KILL_FRAME hr=%08lx service=%08lx flags=%u message=%s\n",(unsigned long)hr,(unsigned long)failed.hr,failed.flags,failed.message);std::fflush(out);
  Check(FAILED(hr)||FAILED((HRESULT)failed.hr),"frame after known helper exit is refused",hr);
  VCBResult sd={};sd.size=sizeof sd;sd.version=VCB_ABI_VERSION;hr=shutdown(d,&sd);sd.message[sizeof(sd.message)-1]=0;
  std::fprintf(out,"SHUTDOWN_AFTER_HELPER_EXIT hr=%08lx service=%08lx flags=%u message=%s\n",(unsigned long)hr,(unsigned long)sd.hr,sd.flags,sd.message);std::fflush(out);
  Check(FAILED(hr)||FAILED((HRESULT)sd.hr)||!(sd.flags&VCB_RESULT_SHUTDOWN),"quarantined service does not acknowledge healthy shutdown",hr);
  Free(s);
  D3DPRESENT_PARAMETERS retry=pp;DWORD start=GetTickCount();hr=d->Reset(&retry);
  std::fprintf(out,"RESET_AFTER_QUARANTINE hr=%08lx elapsed=%lu\n",(unsigned long)hr,GetTickCount()-start);
  Check(hr==D3DERR_DEVICELOST,"Reset refuses quarantined device",hr);
  start=GetTickCount();ULONG refs=d->Release();
  std::fprintf(out,"RELEASE_AFTER_QUARANTINE refs=%lu elapsed=%lu\n",(unsigned long)refs,GetTickCount()-start);
  Check(refs==0,"client device releases after quarantine");d3d->Release();
  Sleep(300);
  std::fprintf(out,"RECORDED after truthful shutdown, rejected Reset and bounded client release\nSUMMARY %s failures=%d\ndone\n",failures?"FAIL":"PASS",failures);std::fflush(out);fclose(out);
  // All GPU work completed before the targeted child termination. Exit the
  // private fixture after verifying the host refused unsafe cleanup under quarantine.
  ExitProcess(failures?1:0);
}
