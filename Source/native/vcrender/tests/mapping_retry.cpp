// Inject exactly one FILE_MAP_READ MapViewOfFile failure in the loaded ASI's
// IAT. The installed/staged DLL on disk is never modified.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static FILE* out;
#define R(...) do {fprintf(out,__VA_ARGS__);fputc('\n',out);fflush(out);}while(0)
using MapFn=LPVOID(WINAPI*)(HANDLE,DWORD,DWORD,DWORD,SIZE_T);
static MapFn originalMap;
static volatile LONG failArmed,failedOnce,readCalls,readSuccess;
static PVOID* patchedSlot;
static LPVOID WINAPI hookedMap(HANDLE mapping,DWORD access,DWORD high,DWORD low,SIZE_T bytes){
 if(access==FILE_MAP_READ){
  InterlockedIncrement(&readCalls);
  if(InterlockedCompareExchange(&failArmed,0,0)&&InterlockedCompareExchange(&failedOnce,1,0)==0){
   SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;
  }
  LPVOID p=originalMap(mapping,access,high,low,bytes);if(p)InterlockedIncrement(&readSuccess);return p;
 }
 return originalMap(mapping,access,high,low,bytes);
}
static bool patchIat(HMODULE module){
 BYTE*b=(BYTE*)module;auto*dos=(IMAGE_DOS_HEADER*)b;auto*nt=(IMAGE_NT_HEADERS32*)(b+dos->e_lfanew);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE||nt->Signature!=IMAGE_NT_SIGNATURE)return false;
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!dir.VirtualAddress)return false;
 for(auto*d=(IMAGE_IMPORT_DESCRIPTOR*)(b+dir.VirtualAddress);d->Name;d++){
  if(!d->OriginalFirstThunk)continue;
  auto*names=(IMAGE_THUNK_DATA32*)(b+d->OriginalFirstThunk);auto*slots=(IMAGE_THUNK_DATA32*)(b+d->FirstThunk);
  for(size_t i=0;names[i].u1.AddressOfData;i++){
   if(IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal))continue;
   auto*imp=(IMAGE_IMPORT_BY_NAME*)(b+names[i].u1.AddressOfData);
   if(strcmp((const char*)imp->Name,"MapViewOfFile"))continue;
   patchedSlot=(PVOID*)&slots[i].u1.Function;originalMap=(MapFn)*patchedSlot;DWORD protect;
   if(!VirtualProtect(patchedSlot,sizeof(PVOID),PAGE_READWRITE,&protect))return false;
   InterlockedExchangePointer((PVOID volatile*)patchedSlot,(PVOID)hookedMap);
   DWORD ignored;VirtualProtect(patchedSlot,sizeof(PVOID),protect,&ignored);return true;
  }
 }
 return false;
}
static void unpatchIat(){
 if(!patchedSlot)return;DWORD protect;if(VirtualProtect(patchedSlot,sizeof(PVOID),PAGE_READWRITE,&protect)){
  InterlockedExchangePointer((PVOID volatile*)patchedSlot,(PVOID)originalMap);
  DWORD ignored;VirtualProtect(patchedSlot,sizeof(PVOID),protect,&ignored);
 }
}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM a,LPARAM l){return DefWindowProcW(w,m,a,l);}
static void pump(DWORD ms){DWORD start=GetTickCount();do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount()-start<ms);}
static COLORREF pixel(HWND w){HDC dc=GetDC(w);COLORREF c=GetPixel(dc,32,32);ReleaseDC(w,dc);return c;}
static HRESULT frames(IDirect3DDevice9*d,DWORD color,int count){HRESULT last=S_OK;for(int i=0;i<count;i++){d->Clear(0,nullptr,D3DCLEAR_TARGET,color,1,0);last=d->Present(nullptr,nullptr,nullptr,nullptr);pump(50);}return last;}
int main(int argc,char**argv){
 if(argc<3)return 2;out=fopen(argv[2],"w");if(!out)return 3;
 HMODULE dll=LoadLibraryA(argv[1]);if(!dll){R("load-error %lu",GetLastError());return 4;}
 auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(dll,"Direct3DCreate9");IDirect3D9*api=create?create(D3D_SDK_VERSION):nullptr;if(!api)return 5;
 WNDCLASSW wc={};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"VCTransientFrameMappingProbe";RegisterClassW(&wc);
 HWND w=CreateWindowW(wc.lpszClassName,L"Transient frame mapping probe",WS_POPUP|WS_VISIBLE,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr);
 D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=64;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9*d=nullptr;HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&p,&d);
 R("device-create=%08lx",hr);if(!d)return 6;
 pump(50);HDC dc=GetDC(w);HBRUSH brush=CreateSolidBrush(RGB(17,34,51));RECT r={0,0,128,128};FillRect(dc,&r,brush);DeleteObject(brush);ReleaseDC(w,dc);GdiFlush();
 R("initial-window-pixel=%08lx",pixel(w));
 bool patched=patchIat(dll);R("iat-mapviewof-file-patched=%d",patched);if(!patched){d->Release();api->Release();return 7;}
 InterlockedExchange(&failArmed,(argc>3&&!strcmp(argv[3],"no-fault"))?0:1);
 hr=frames(d,0xff2468acu,30);pump(250);
 COLORREF afterFailure=pixel(w);R("after-30frames present=%08lx forced-failures=%ld read-map-calls=%ld read-map-success=%ld window-pixel=%08lx expected-frame-pixel=%08lx",hr,failedOnce,readCalls,readSuccess,afterFailure,(DWORD)RGB(0x24,0x68,0xac));
 hr=frames(d,0xff2468acu,30);pump(250);
 COLORREF beforeReset=pixel(w);R("after-60frames present=%08lx forced-failures=%ld read-map-calls=%ld read-map-success=%ld window-pixel=%08lx",hr,failedOnce,readCalls,readSuccess,beforeReset);
 D3DPRESENT_PARAMETERS next=p;hr=d->Reset(&next);R("explicit-reset=%08lx",hr);
 hr=frames(d,0xffab5729u,30);pump(250);COLORREF recovered=pixel(w);
 R("after-reset-30frames present=%08lx forced-failures=%ld read-map-calls=%ld read-map-success=%ld window-pixel=%08lx expected-frame-pixel=%08lx",hr,failedOnce,readCalls,readSuccess,recovered,(DWORD)RGB(0xab,0x57,0x29));
 InterlockedExchange(&failArmed,0);unpatchIat();R("iat-restored=1");
 R("device-release-count=%lu",d->Release());R("api-release-count=%lu",api->Release());DestroyWindow(w);R("done");fclose(out);return 0;
}
