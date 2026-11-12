
#include <windows.h>
#include <stdio.h>
#include <string.h>
static unsigned files,loaded,directories,pathmode,count,seen[64];static char output[8192];
static const char *names[]={"VCAudioHeap.asi","VCMemory.asi","VCMusic.asi","VCScriptHook.asi"};
static unsigned indexof(const char *p){for(unsigned i=0;i<4;i++)if(strstr(p,names[i]))return i;return 31;}
static DWORD WINAPI test_path(HMODULE h,LPSTR p,DWORD n){(void)h;if(pathmode==1)return n;if(pathmode==2){strcpy(p,"VCCore.asi");return strlen(p);}strcpy(p,"C:\\fixture\\plugins\\VCCore.asi");return strlen(p);}
static DWORD WINAPI test_attr(LPCSTR p){unsigned i=indexof(p);return files&(1u<<i)?(directories?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL):INVALID_FILE_ATTRIBUTES;}
static HMODULE WINAPI test_loaded(LPCSTR p){return loaded&(1u<<indexof(p))?(HMODULE)1:NULL;}
static BOOL WINAPI test_delete(LPCSTR p){(void)p;return TRUE;}
static HANDLE WINAPI test_open(LPCSTR p,DWORD a,DWORD s,LPSECURITY_ATTRIBUTES x,DWORD c,DWORD f,HANDLE t){(void)p;(void)a;(void)s;(void)x;(void)c;(void)f;(void)t;return(HANDLE)1;}
static BOOL WINAPI test_write(HANDLE f,LPCVOID p,DWORD n,LPDWORD written,LPOVERLAPPED o){(void)f;(void)o;strncat(output,p,n);*written=n;return TRUE;}
static BOOL WINAPI test_close(HANDLE h){(void)h;return TRUE;}
#define GetModuleFileNameA test_path
#define GetFileAttributesA test_attr
#define GetModuleHandleA test_loaded
#define DeleteFileA test_delete
#define CreateFileA test_open
#define WriteFile test_write
#define CloseHandle test_close
#include "vccore.c"
static BOOL record(unsigned i,DWORD why){seen[count++]=why*10+i;return TRUE;}
BOOL WINAPI vcaudioheap_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(0,r);}
BOOL WINAPI vccutscene_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(1,r);}
BOOL WINAPI vcmusic_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(2,r);}
BOOL WINAPI vcsh_loader_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(3,r);}
BOOL WINAPI vcloading_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(4,r);}
BOOL WINAPI vcapc_DllMain(HINSTANCE i,DWORD r,LPVOID p){(void)i;(void)p;return record(5,r);}
static int check(unsigned f,unsigned l,unsigned d,unsigned mode){
 files=f;loaded=l;directories=d;pathmode=mode;count=0;active=0;output[0]=0;
 int expected=!mode&&!l&&(!f||d);
 if(!DllMain((HINSTANCE)1,DLL_PROCESS_ATTACH,NULL))return 1;
 if(count!=(expected?6:0))return 2;
 if(expected){for(unsigned i=0;i<6;i++)if(seen[i]!=10+i)return 3;}
 else if(!mode&&!strstr(output,"INACTIVE:"))return 4;
 DllMain((HINSTANCE)1,DLL_THREAD_ATTACH,NULL);DllMain((HINSTANCE)1,DLL_THREAD_DETACH,NULL);DllMain((HINSTANCE)1,DLL_PROCESS_DETACH,NULL);
 if(count!=(expected?24:0))return 5;
 if(expected){for(unsigned i=0;i<6;i++)if(seen[18+i]!=5-i)return 6;}
 return 0;
}
int main(void){unsigned cases=0;int r;
 for(unsigned mask=0;mask<16;mask++){if((r=check(mask,0,0,0)))return 10+r;cases++;}
 for(unsigned i=0;i<4;i++){if((r=check(0,1u<<i,0,0)))return 20+r;cases++;}
 for(unsigned mode=1;mode<=2;mode++){if((r=check(0,0,0,mode)))return 30+r;cases++;}
 if((r=check(15,0,1,0)))return 40+r;cases++;
 printf("PASS: %u compiled dispatcher controls: every legacy-file subset, loaded-name guards, invalid paths, directory names, ordered attach/reverse detach\n",cases);return 0;
}
