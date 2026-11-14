#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

static FILE* out;
static int failures;
#define R(...) do { fprintf(out, __VA_ARGS__); fputc('\n', out); fflush(out); } while (0)
static void check(const char* name, bool ok) { R("%s %s",name,ok?"PASS":"FAIL"); if(!ok) failures++; }
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM l) { return DefWindowProcW(w,m,a,l); }
static D3DPRESENT_PARAMETERS parameters(HWND w) {
  D3DPRESENT_PARAMETERS p={}; p.BackBufferWidth=64; p.BackBufferHeight=64; p.BackBufferFormat=D3DFMT_A8R8G8B8;
  p.BackBufferCount=1; p.SwapEffect=D3DSWAPEFFECT_DISCARD; p.hDeviceWindow=w; p.Windowed=TRUE;
  p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE; return p;
}
static bool pixel(IDirect3DSwapChain9* sc, IDirect3DDevice9* d,DWORD color,UINT width,UINT height) {
  IDirect3DSurface9 *bb=nullptr,*sys=nullptr; D3DLOCKED_RECT lr={}; D3DSURFACE_DESC desc={}; bool ok=false;
  HRESULT hr=sc->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&bb);
  if(SUCCEEDED(hr)&&bb) hr=bb->GetDesc(&desc); else hr=E_FAIL;
  if(SUCCEEDED(hr)) hr=d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sys,nullptr);
  if(SUCCEEDED(hr)) hr=d->GetRenderTargetData(bb,sys);
  if(SUCCEEDED(hr)) hr=sys->LockRect(&lr,nullptr,D3DLOCK_READONLY);
  if(SUCCEEDED(hr)) { ok=desc.Width==width&&desc.Height==height&&(*(DWORD*)lr.pBits|0xff000000u)==color; sys->UnlockRect(); }
  if(sys)sys->Release(); if(bb)bb->Release(); return ok;
}
int main(int argc,char** argv) {
  if(argc<3)return 2;out=fopen(argv[2],"w");if(!out)return 3;
  HMODULE dll=LoadLibraryA(argv[1]); if(!dll){R("load-error %lu",GetLastError());return 4;}
  auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(dll,"Direct3DCreate9");
  IDirect3D9* api=create?create(D3D_SDK_VERSION):nullptr;if(!api){R("api-error");return 5;}
  WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VCRetainedSwapchainProbe";
  RegisterClassW(&wc);HWND w=CreateWindowW(wc.lpszClassName,L"Retained swapchain probe",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,240,200,nullptr,nullptr,wc.hInstance,nullptr);
  D3DPRESENT_PARAMETERS p=parameters(w);IDirect3DDevice9* d=nullptr;
  HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&p,&d);
  check("device-create",SUCCEEDED(hr)&&d);if(!d)return 6;
  IDirect3DSwapChain9* retained=nullptr;hr=d->GetSwapChain(0,&retained);check("initial-chain",SUCCEEDED(hr)&&retained);
  if(!retained)return 7;
  for(int i=0;i<4;i++) {
    // Only the implicit swapchain reference survives Reset. Every backbuffer
    // and every default-pool surface acquired by this fixture is released.
    D3DPRESENT_PARAMETERS next=p;if(i==1||i==3){next.BackBufferWidth+=16;next.BackBufferHeight+=8;}
    hr=d->Reset(&next);IDirect3DSwapChain9* current=nullptr;HRESULT get=d->GetSwapChain(0,&current);
    DWORD color=0xff123456u+0x00111111u*i;d->Clear(0,nullptr,D3DCLEAR_TARGET,color,1,0);
    HRESULT oldPresent=retained->Present(nullptr,nullptr,nullptr,nullptr,0);
    HRESULT newPresent=current?current->Present(nullptr,nullptr,nullptr,nullptr,0):E_FAIL;
    D3DPRESENT_PARAMETERS got={};HRESULT oldParams=retained->GetPresentParameters(&got);
    bool oldPixel=pixel(retained,d,color,next.BackBufferWidth,next.BackBufferHeight);
    R("reset-%d hr=%08lx same=%d get=%08lx old-present=%08lx current-present=%08lx old-params=%08lx dims=%ux%u old-pixel=%d",i,hr,current==retained,get,oldPresent,newPresent,oldParams,got.BackBufferWidth,got.BackBufferHeight,oldPixel);
    check("reset-success",hr==D3D_OK);
    check("identity-preserved",current==retained);
    check("retained-present",oldPresent==D3D_OK);
    check("current-present",newPresent==D3D_OK);
    check("retained-parameters",oldParams==D3D_OK&&got.BackBufferWidth==next.BackBufferWidth&&got.BackBufferHeight==next.BackBufferHeight);
    check("retained-readback",oldPixel);
    if(current)current->Release();p=next;
  }
  IDirect3DSurface9* held=nullptr;
  hr=d->CreateRenderTarget(4,4,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&held,nullptr);
  check("create-held-default-resource",SUCCEEDED(hr)&&held);
  D3DPRESENT_PARAMETERS retry=p;hr=d->Reset(&retry);
  IDirect3DSwapChain9* failedChain=nullptr;HRESULT get=d->GetSwapChain(0,&failedChain);
  HRESULT failedPresent=retained->Present(nullptr,nullptr,nullptr,nullptr,0);
  R("failed-reset hr=%08lx same=%d get=%08lx old-present=%08lx",hr,failedChain==retained,get,failedPresent);
  check("backend-reset-rejected",hr==D3DERR_INVALIDCALL);
  check("failed-reset-identity",failedChain==retained);
  check("failed-reset-present-lost",failedPresent==D3DERR_DEVICELOST);
  if(failedChain)failedChain->Release();if(held)held->Release();
  retry=p;hr=d->Reset(&retry);IDirect3DSwapChain9* recovered=nullptr;get=d->GetSwapChain(0,&recovered);
  d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffabcdefu,1,0);
  HRESULT recoveredPresent=retained->Present(nullptr,nullptr,nullptr,nullptr,0);
  bool recoveredPixel=pixel(retained,d,0xffabcdefu,retry.BackBufferWidth,retry.BackBufferHeight);
  R("recovery-reset hr=%08lx same=%d get=%08lx old-present=%08lx old-pixel=%d",hr,recovered==retained,get,recoveredPresent,recoveredPixel);
  check("recovery-reset-success",hr==D3D_OK);
  check("recovery-reset-identity",recovered==retained);
  check("recovery-retained-present",recoveredPresent==D3D_OK);
  check("recovery-retained-readback",recoveredPixel);
  if(recovered)recovered->Release();retained->Release();
  ULONG refs=d->Release();R("device-release-count %lu",refs);check("device-final-reference",refs==0);
  api->Release();DestroyWindow(w);R("SUMMARY %s failures=%d",failures?"FAIL":"PASS",failures);R("done");fclose(out);return failures?1:0;
}
