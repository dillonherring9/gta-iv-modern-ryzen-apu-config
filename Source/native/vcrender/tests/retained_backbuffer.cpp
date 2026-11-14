// Hold only an implicit backbuffer across Reset, then release and retry.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
static FILE* out;static int failures;
#define R(...) do {fprintf(out,__VA_ARGS__);fputc('\n',out);fflush(out);}while(0)
static void check(const char*n,bool ok){R("%s %s",n,ok?"PASS":"FAIL");if(!ok)failures++;}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM l){return DefWindowProcW(w,m,a,l);}
int main(int argc,char**argv){
 if(argc<3)return 2;out=fopen(argv[2],"w");if(!out)return 3;
 HMODULE dll=LoadLibraryA(argv[1]);if(!dll){R("load-error %lu",GetLastError());return 4;}
 auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(dll,"Direct3DCreate9");
 IDirect3D9*api=create?create(D3D_SDK_VERSION):nullptr;if(!api)return 5;
 WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VCRetainedBackbufferProbe";
 RegisterClassW(&wc);HWND w=CreateWindowW(wc.lpszClassName,L"Retained backbuffer",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,240,200,nullptr,nullptr,wc.hInstance,nullptr);
 D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=64;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.BackBufferCount=1;
 p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9*d=nullptr;HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&p,&d);
 check("device-create",SUCCEEDED(hr)&&d);if(!d)return 6;
 IDirect3DSurface9*held=nullptr;hr=d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&held);
 check("retain-implicit-backbuffer",SUCCEEDED(hr)&&held);if(!held)return 7;
 D3DPRESENT_PARAMETERS retry=p;hr=d->Reset(&retry);
 R("reset-with-held-backbuffer hr=%08lx coop=%08lx",hr,d->TestCooperativeLevel());
 check("retained-backbuffer-reset-rejected",hr==D3DERR_INVALIDCALL);
 R("held-backbuffer-release-count %lu",held->Release());held=nullptr;
 retry=p;hr=d->Reset(&retry);R("reset-after-release hr=%08lx coop=%08lx",hr,d->TestCooperativeLevel());
 check("reset-after-release-succeeds",hr==D3D_OK&&d->TestCooperativeLevel()==D3D_OK);
 d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff2468acu,1,0);hr=d->Present(nullptr,nullptr,nullptr,nullptr);
 check("present-after-recovery",hr==D3D_OK);
 ULONG refs=d->Release();R("device-release-count %lu",refs);check("device-final-reference",refs==0);
 refs=api->Release();R("api-release-count %lu",refs);check("api-final-reference",refs==0);
 DestroyWindow(w);R("SUMMARY %s failures=%d",failures?"FAIL":"PASS",failures);R("done");fclose(out);return failures?1:0;
}
