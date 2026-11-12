/* VCAPC (VCCore module, step 252): APC/Rhino-only native invulnerability for
 * GTA IV CE1.2.0.59. In this build the Vice City Rhino is the GTA IV "apc"
 * model (joaat 0x2189d250; no "rhino" model exists).
 *
 * Every game frame, at the shared 0x5C2A41 game-loop call slot (chained like
 * VCMusic/VCCutscene/VCScriptHook), enumerate the native vehicle pool. For each
 * live APC: set all five proofs, can't-be-damaged, no visible damage, no tyre
 * bursts and strong, then restore body/engine/petrol-tank health below 1000.
 * Destroyed wrecks are never repaired. No persistent entity handles, save
 * data, script globals, model loads, spawning or deletion.
 *
 * Native hashes, handler addresses and argument widths are taken from the CE
 * registration calls and handler bytes (evidence/native-contract.json) and are
 * byte-guarded here. Draft by Codex 2026-11-11 (history/vcapc.codex-draft.c);
 * revised by Claude: VCCore module entry, cached pool/model validation instead
 * of two VirtualQuery calls per vehicle per frame, one-time runtime logging.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "startup_log.h"
#include "ce_contract.h"

#define VA_MODEL_HASH 0x2189d250u      /* joaat("apc") */
#define VA_POOL_ADDRESS 0x012e22a4u    /* CPool<CVehicle>* (see GET_CAR_MODEL impl 0xbca830) */
#define VA_MODEL_TABLE 0x01295cd8u     /* CBaseModelInfo* [int16 index] in .data */
#define VA_MODEL_SLOTS 0x8000u         /* every non-negative int16 index */
#define VA_SLOT 0x005c2a41u
typedef struct { void *ret; uint32_t argc; void *args; uint32_t datacount;
    uint32_t fix[4]; uint8_t tmp[0x40]; uint32_t buf[32]; } VaCtx;
typedef void (__cdecl *VaNative)(VaCtx *);
typedef VaNative (__stdcall *VaLookup)(uint32_t);
static VaLookup va_lookup=(VaLookup)0x86fce0u;
static VaNative va_natives[VA_COUNT];
enum { VA_EXISTS, VA_MODEL, VA_DEAD, VA_PROOFS, VA_DAMAGE, VA_TYRES,
       VA_VISIBLE, VA_STRONG, VA_BODY_GET, VA_ENGINE_GET, VA_TANK_GET,
       VA_BODY_SET, VA_ENGINE_SET, VA_TANK_SET };
typedef struct { uint8_t *storage,*flags; int32_t count,stride; } VaPool;
static int va_enabled,va_ready,va_logged_first;
static uintptr_t va_table;  /* native table pointer global, from the guarded lookup bytes */
static char va_log[MAX_PATH];
uintptr_t vcapc_previous __asm__("vcapc_previous");
/* Validated pool shape, and the model pointer last validated per model index
 * with whether it is the APC. Model infos persist for the session; a changed
 * pointer is simply validated again. */
static uintptr_t va_pool_address;
static VaPool va_pool;
static uintptr_t va_model_seen[VA_MODEL_SLOTS];
static uint8_t va_model_apc[VA_MODEL_SLOTS];

static void va_note(const char *text) {
    if(va_log[0])vc_write_log(va_log,text,(DWORD)strlen(text),1);
}
static int va_readable(uintptr_t address,size_t bytes) {
    MEMORY_BASIC_INFORMATION mi;
    if (!address || bytes>UINTPTR_MAX-address ||
        !VirtualQuery((void *)address,&mi,sizeof mi) || mi.State!=MEM_COMMIT ||
        (mi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    uintptr_t end=(uintptr_t)mi.BaseAddress+mi.RegionSize;
    return end>=address && bytes<=end-address;
}
static int va_bytes(uintptr_t address,const void *expected,size_t bytes) {
    return va_readable(address,bytes) && !memcmp((void *)address,expected,bytes);
}
static uint32_t va_call(unsigned id,unsigned argc,const uint32_t *args) {
    VaCtx c;memset(&c,0,sizeof c);c.ret=c.args=c.buf;c.argc=argc;
    for(unsigned i=0;i<argc;i++)c.buf[i]=args[i];
    va_natives[id](&c);return c.buf[0];
}
static uint32_t va_float(float f) {uint32_t b;memcpy(&b,&f,4);return b;}
static float va_as_float(uint32_t b) {float f;memcpy(&f,&b,4);return f;}
static int va_resolve(void) {
    if(va_ready)return va_ready>0;
    if(!va_readable(va_table,4)||!*(uint32_t *)va_table)return 0; /* table not built yet */
    /* The pool resolver documents the handle/pool layout used below. Its page is
     * encrypted in the EXE file, so compare it only now, while the game runs. */
    if(!va_bytes(0x499d20,va_pool_resolver,sizeof va_pool_resolver)) {
        va_note("INACTIVE: CE vehicle-pool resolver differs at runtime; nothing changed.\n");
        va_ready=-1;return 0;
    }
    for(unsigned i=0;i<VA_COUNT;i++) {
        VaNative native=va_lookup(va_contracts[i].hash);
        if((uintptr_t)native!=va_contracts[i].handler) {
            char line[128];
            snprintf(line,sizeof line,"INACTIVE: native %08lX resolves to %08lX, expected %08lX; nothing changed.\n",
                     (unsigned long)va_contracts[i].hash,(unsigned long)(uintptr_t)native,
                     (unsigned long)va_contracts[i].handler);
            va_note(line);va_ready=-1;return 0;
        }
        va_natives[i]=native;
    }
    va_note("RUNNING: 14 vehicle natives resolved to their verified CE handlers.\n");
    va_ready=1;return 1;
}

/* Called only for validated current-generation handles belonging to APC. */
static void va_protect(uint32_t handle) {
    uint32_t one[1]={handle};
    if(!(va_call(VA_EXISTS,1,one)&0xffu)||(va_call(VA_DEAD,1,one)&0xffu))return;
    uint32_t model=0,model_args[2]={handle,(uint32_t)(uintptr_t)&model};
    va_call(VA_MODEL,2,model_args);
    if(model!=VA_MODEL_HASH)return;
    uint32_t proofs[6]={handle,1,1,1,1,1},off[2]={handle,0},on[2]={handle,1};
    va_call(VA_PROOFS,6,proofs);
    va_call(VA_DAMAGE,2,off);
    va_call(VA_TYRES,2,off);
    va_call(VA_VISIBLE,2,off);
    va_call(VA_STRONG,2,on);
    /* Restore still-live damaged tanks. IS_CAR_DEAD is checked before any write;
     * already destroyed wrecks are never repaired or resurrected. No FIX_CAR,
     * transforms, controls, ignition, tyre repair or passenger state is changed. */
    uint32_t body=1000,body_args[2]={handle,(uint32_t)(uintptr_t)&body};
    va_call(VA_BODY_GET,2,body_args);
    uint32_t health_args[2]={handle,1000};
    if((int32_t)body<1000)va_call(VA_BODY_SET,2,health_args);
    health_args[1]=va_float(1000.0f);
    if(va_as_float(va_call(VA_ENGINE_GET,1,one))<1000.0f)
        va_call(VA_ENGINE_SET,2,health_args);
    if(va_as_float(va_call(VA_TANK_GET,1,one))<1000.0f)
        va_call(VA_TANK_SET,2,health_args);
    if(!va_logged_first) {
        char line[96];
        snprintf(line,sizeof line,"PROTECTED: first APC/Rhino this session, handle %08lX.\n",(unsigned long)handle);
        va_note(line);va_logged_first=1;
    }
}

/* Validate the pool header and its flag/storage spans only when the pool
 * pointer or shape changes. Pools are created once at startup. */
static int va_pool_current(VaPool *out) {
    uintptr_t address=*(uintptr_t *)VA_POOL_ADDRESS;
    if(!address)return 0;
    if(address==va_pool_address&&!memcmp((void *)address,&va_pool,sizeof va_pool)) {*out=va_pool;return 1;}
    va_pool_address=0;
    if(!va_readable(address,sizeof(VaPool)))return 0;
    VaPool pool;memcpy(&pool,(void *)address,sizeof pool);
    if(pool.count<=0||pool.count>4096||pool.stride<0x10dc||pool.stride>0x10000||
       !va_readable((uintptr_t)pool.flags,(size_t)pool.count)||
       (size_t)pool.count>SIZE_MAX/(size_t)pool.stride||
       !va_readable((uintptr_t)pool.storage,(size_t)pool.count*(size_t)pool.stride))return 0;
    va_pool=pool;va_pool_address=address;*out=pool;return 1;
}
static int va_is_apc(int16_t model_index) {
    if(model_index<0)return 0;
    uintptr_t model=*(uintptr_t *)(VA_MODEL_TABLE+4u*(unsigned)model_index);
    if(!model)return 0;
    if(va_model_seen[model_index]!=model) {
        if(!va_readable(model+0x3c,4))return 0;  /* not cached; retried next frame */
        va_model_apc[model_index]=*(uint32_t *)(model+0x3c)==VA_MODEL_HASH;
        va_model_seen[model_index]=model;
    }
    return va_model_apc[model_index];
}

__attribute__((force_align_arg_pointer)) void vcapc_tick(void) __asm__("vcapc_tick");
void vcapc_tick(void) {
    VaPool pool;
    if(!va_enabled||!va_resolve()||!va_pool_current(&pool))return;
    for(int32_t i=0;i<pool.count;i++) {
        uint8_t generation=pool.flags[i];
        if(generation&0x80u)continue;  /* free slot */
        uint8_t *vehicle=pool.storage+(size_t)i*(size_t)pool.stride;
        int16_t model_index;memcpy(&model_index,vehicle+0x2e,2);
        if(!va_is_apc(model_index))continue;
        va_protect(((uint32_t)i<<8)|generation);
        /* The natives above never create or delete vehicles; stop if the pool
         * changed anyway and take a fresh snapshot next frame. */
        if(*(uintptr_t *)VA_POOL_ADDRESS!=va_pool_address)return;
    }
}
__attribute__((naked)) static void va_tick_stub(void) {
    __asm__ volatile("pushal\n\tpushfl\n\tcall vcapc_tick\n\tpopfl\n\tpopal\n\tjmp *vcapc_previous\n");
}

enum va_result {VA_ACTIVE,VA_WRONG_IMAGE,VA_WRONG_CODE,VA_PROTECT_FAILED};
static enum va_result va_apply(uint8_t *base) {
    if((uintptr_t)base!=0x400000u||!va_readable((uintptr_t)base,0x1000))return VA_WRONG_IMAGE;
    IMAGE_DOS_HEADER *dos=(void *)base;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||dos->e_lfanew>0x1000-(LONG)sizeof(IMAGE_NT_HEADERS32))return VA_WRONG_IMAGE;
    IMAGE_NT_HEADERS32 *pe=(void *)(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE||pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       pe->FileHeader.TimeDateStamp!=0x63d3e735u||pe->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||
       pe->OptionalHeader.SizeOfImage!=0x1be6400u||!(pe->FileHeader.Characteristics&IMAGE_FILE_LARGE_ADDRESS_AWARE))return VA_WRONG_IMAGE;
    /* Attach-time guards use only code that is plain in the EXE file. */
    if(!va_bytes(0x86fce0,va_native_lookup,sizeof va_native_lookup)||
       !va_bytes(0xbca830,va_model_lookup,sizeof va_model_lookup))return VA_WRONG_CODE;
    memcpy(&va_table,(void *)(0x86fce0u+3),4);  /* MOV ESI,[table] operand, guarded above */
    for(unsigned i=0;i<VA_COUNT;i++)if(!va_bytes(va_contracts[i].handler,va_contracts[i].code,va_contracts[i].bytes))return VA_WRONG_CODE;
    /* Pool pointer and the whole int16-indexed model table are static .data. */
    if(!va_readable(VA_POOL_ADDRESS,4)||!va_readable(VA_MODEL_TABLE,4u*VA_MODEL_SLOTS))return VA_WRONG_CODE;
    if(!va_readable(VA_SLOT-5,26)||*(uint8_t *)(VA_SLOT-5)!=0xe8||*(uint8_t *)VA_SLOT!=0xe8||
       *(uint8_t *)(VA_SLOT+5)!=0xe8||*(uint8_t *)(VA_SLOT+10)!=0xe8||
       *(uint8_t *)(VA_SLOT+15)!=0xb9||*(uint8_t *)(VA_SLOT+20)!=0xe8)return VA_WRONG_CODE;
    uintptr_t previous=VA_SLOT+5+*(int32_t *)(VA_SLOT+1);
    if(!va_readable(previous,1))return VA_WRONG_CODE;
    DWORD old,ignored;
    if(!VirtualProtect((void *)VA_SLOT,5,PAGE_EXECUTE_READWRITE,&old))return VA_PROTECT_FAILED;
    vcapc_previous=previous;
    int32_t relative=(int32_t)((uintptr_t)va_tick_stub-(VA_SLOT+5));
    memcpy((void *)(VA_SLOT+1),&relative,4);
    FlushInstructionCache(GetCurrentProcess(),(void *)VA_SLOT,5);
    VirtualProtect((void *)VA_SLOT,5,old,&ignored);
    va_enabled=1;return VA_ACTIVE;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char path[MAX_PATH];DWORD n=GetModuleFileNameA(instance,path,MAX_PATH);
        char *slash=n&&n<MAX_PATH?strrchr(path,'\\'):NULL;
        if(slash&&(size_t)(slash-path)+sizeof "\\VCAPC.log"<=sizeof path) {
            strcpy(slash,"\\VCAPC.log");memcpy(va_log,path,strlen(path)+1);
        }
        enum va_result result=va_apply((uint8_t *)GetModuleHandleA(NULL));
        if(va_log[0]) {
            static const char *messages[]={"ACTIVE: APC/Rhino native invulnerability; fresh vehicle-pool generations each frame; existing wrecks excluded.\n",
                "INACTIVE: unsupported executable layout.\n","INACTIVE: CE pool/native/frame contract differs; no hook installed.\n",
                "INACTIVE: frame call is not writable.\n"};
            char line[160];
            int len=snprintf(line,sizeof line,"%s",messages[result]);
            if(result==VA_ACTIVE)
                len+=snprintf(line+len,sizeof line-(size_t)len,"chained at 005C2A41 (previous target %08lX)\n",(unsigned long)vcapc_previous);
            vc_write_log(va_log,line,(DWORD)strlen(line),0);
        }
    }
    return TRUE;
}
