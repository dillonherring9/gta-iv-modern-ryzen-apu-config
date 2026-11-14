#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
static FILE* out;
static void row(const char* fmt, ...) { va_list a; va_start(a,fmt); vfprintf(out,fmt,a); va_end(a); fputc('\n',out); fflush(out); }
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM b) { return DefWindowProcW(w,m,a,b); }
int main(int argc,char** argv) {
  if(argc<3)return 2; out=fopen(argv[2],"wb"); if(!out)return 3;
  HMODULE mod=LoadLibraryA(argv[1]);
  auto make=mod?(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(mod,"Direct3DCreate9"):nullptr;
  IDirect3D9* api=make?make(D3D_SDK_VERSION):nullptr; if(!api){row("SETUP failed");return 4;}
  WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VCResetSurfaceUltra";RegisterClassW(&wc);
  HWND w=CreateWindowW(wc.lpszClassName,L"Reset surface ultra review",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,640,480,nullptr,nullptr,wc.hInstance,nullptr);
  unsigned failures=0;
  for(int kind=0;kind<4;kind++) {
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d);
    row("case=%d create=%08lx",kind,(unsigned long)hr);if(FAILED(hr)||!d)return 5;
    IDirect3DSurface9* bb=nullptr;IDirect3DSurface9* blocker=nullptr;
    hr=d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb);row("case=%d get-backbuffer=%08lx",kind,(unsigned long)hr);if(!bb)return 6;
    if(kind&1)bb->AddRef();
    if(kind&2) { hr=d->CreateRenderTarget(4,4,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&blocker,nullptr);row("case=%d create-blocker=%08lx",kind,(unsigned long)hr);if(!blocker)return 7; }
    D3DPRESENT_PARAMETERS next=pp;next.BackBufferWidth=128;next.BackBufferHeight=80;
    hr=d->Reset(&next);row("case=%d held-reset=%08lx",kind,(unsigned long)hr);
    D3DSURFACE_DESC desc={};hr=bb->GetDesc(&desc);row("case=%d held-desc=%08lx dims=%ux%u",kind,(unsigned long)hr,desc.Width,desc.Height);
    if(kind&1)bb->Release();bb->Release();if(blocker)blocker->Release();
    next=pp;next.BackBufferWidth=128;next.BackBufferHeight=80;hr=d->Reset(&next);row("case=%d recovery-reset=%08lx",kind,(unsigned long)hr);if(FAILED(hr))failures++;
    ULONG refs=d->Release();row("case=%d final-device-refs=%lu",kind,(unsigned long)refs);if(refs)failures++;
  }
  api->Release();DestroyWindow(w);row("SUMMARY unexpected=%u",failures);row("done");fclose(out);return failures?1:0;
}
