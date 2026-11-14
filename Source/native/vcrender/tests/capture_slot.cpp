#ifndef VCR_PRESENT_SOURCE
#define VCR_PRESENT_SOURCE "../src/client/vcr_present.cpp"
#endif
#include VCR_PRESENT_SOURCE
#include <stdio.h>
static VcrControl ctl;
namespace vcr {
VcrControl* control(){return &ctl;}
const wchar_t* channelBase(){return L"capture-slot-isolated";}
int framesAheadSetting(){return 1;}
void log(const char*,...){}
}
static unsigned char* storage;
static int trapped,protectedSlot,overwrote;
static LONG CALLBACK trap(EXCEPTION_POINTERS* e){
 if(e->ExceptionRecord->ExceptionCode!=STATUS_GUARD_PAGE_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
 auto addr=(uintptr_t)e->ExceptionRecord->ExceptionInformation[1];
 if(addr<(uintptr_t)storage||addr>=(uintptr_t)storage+4096)return EXCEPTION_CONTINUE_SEARCH;
 trapped++;auto& fe=ctl.frames[0];protectedSlot=fe.readingSlot==0;
 // The exact host slot-selection rule, forced twice while capture is paused
 // at its first shared-memory read. No GPU or timing-dependent loop is needed.
 for(int n=0;n<2;n++){
  int s=0;for(;s<(int)VCR_FRAME_SLOTS;s++)if(s!=fe.latestSlot&&s!=fe.readingSlot)break;
  if(s==0)overwrote++;
  memset(storage+s*4096,0x77,4096);InterlockedExchange((volatile LONG*)&fe.latestSlot,s);
 }
 return EXCEPTION_CONTINUE_EXECUTION;
}
int main(int n,char**v){FILE*f=fopen(n>1?v[1]:"results.txt","w");if(!f)return 2;
 storage=(unsigned char*)VirtualAlloc(nullptr,VCR_FRAME_SLOTS*4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!storage)return 3;
 memset(storage,0x33,VCR_FRAME_SLOTS*4096);
 auto& fe=ctl.frames[0];fe.scId=99;fe.gen=1;fe.latestSlot=0;fe.readingSlot=-1;
 auto& t=(*vcr::g_targets)[99];t.view=storage;t.w=t.h=32;t.gen=1;
 PVOID handler=AddVectoredExceptionHandler(1,trap);DWORD old;VirtualProtect(storage,4096,PAGE_READWRITE|PAGE_GUARD,&old);
 std::vector<uint8_t> copy;uint32_t w=0,h=0;bool got=vcr::presenterLastFrame(99,copy,w,h);bool equal=copy.size()==4096;
 for(auto b:copy)equal=equal&&b==0x33;
 bool ok=got&&trapped==1&&protectedSlot&&overwrote==0&&equal&&fe.readingSlot==-1;
 fprintf(f,"capture=%d trap=%d pinned=%d host_overwrites=%d pixels_intact=%d released=%d\nSUMMARY %s\n",got,trapped,protectedSlot,overwrote,equal,fe.readingSlot==-1,ok?"PASS":"FAIL");
 RemoveVectoredExceptionHandler(handler);vcr::g_targets->erase(99);VirtualFree(storage,0,MEM_RELEASE);fclose(f);return ok?0:1;
}
