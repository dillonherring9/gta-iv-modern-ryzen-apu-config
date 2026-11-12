/* VCCutscene: selective Tommy outfits and CE language menu support.
 * Derived from step220 vcmemory.c. Step248 removes every custom memory purge,
 * entity clearing, density override and model-budget override. GTA IV owns
 * streaming allocation, eviction and cutscene resource lifetimes.
 * Outfit cleanup below changes only the selected model-token substitution.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include "outfit_select.h"

#include "startup_log.h"
#include "profile_snapshot.h"

static char g_dir[MAX_PATH], g_logpath[MAX_PATH];
static void vlog(const char *fmt, ...) {
    va_list a; va_start(a, fmt); vc_log_v(g_logpath, "", fmt, a); va_end(a);
}
/* ---------------- CE natives ---------------- */
typedef struct { void *ret; uint32_t argc; void *args; uint32_t datacount; uint32_t fix[4]; uint8_t tmp[0x40]; uint32_t buf[32]; } NativeCtx;
typedef void (__cdecl *NativeFn)(NativeCtx *);
typedef NativeFn (__stdcall *GetNativeFn)(uint32_t);
static GetNativeFn g_getNative = (GetNativeFn)0x86FCE0;
enum { N_GET_PLAYER_CHAR, N_DOES_CHAR_EXIST, N_GET_ID_OF_THIS_THREAD,
       N_IS_THREAD_ACTIVE, N_COUNT };
static const uint32_t k_hash[N_COUNT] = {
    0x511454A9, 0x46531797, 0x051A131D, 0x052A30F7
};
static NativeFn g_nat[N_COUNT];
static int natives(void) {
    static int ok;
    if (ok) return ok > 0;
    if (!**(uint32_t **)(0x86FCE0 + 3)) return 0;            /* native table not built yet */
    for (int i = 0; i < N_COUNT; i++) if (!(g_nat[i] = g_getNative(k_hash[i]))) { vlog("native %08X missing", k_hash[i]); ok = -1; return 0; }
    ok = 1; return 1;
}
static uint32_t ncall(int id, int argc, const uint32_t *argv) {
    NativeCtx c; memset(&c, 0, sizeof c); c.ret = c.args = c.buf; c.argc = argc;
    for (int i = 0; i < argc; i++) c.buf[i] = argv[i];
    g_nat[id](&c); return c.buf[0];
}

/* ---------------- engine internals called by the original handlers ---------------- */
typedef void (__cdecl *StartCutFn)(const char *);
typedef uint8_t (__cdecl *BoolFn)(void);
static const StartCutFn start_cutscene = (StartCutFn)0xB8AE30;
static const BoolFn cutscene_finished = (BoolFn)0xB8ACF0;
/* ---------------- settings / state ---------------- */
static struct { int fallbackMin, outfits; } S;
static int ini(const char *sec, const char *key, int def) {
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\VCCutscene.ini", g_dir); return vc_profile_int(sec, key, def, p);
}
/* Only the CUT [MODELS] model token's lowercase call is intercepted. Neither
 * animation-name lowercase call is touched. Parser token and stored name slots
 * are 32 bytes; the longest replacement below is 9 characters plus NUL. */
static int g_outfit_ready, g_outfit_drawable = -1;
static int g_outfit_seen_active;
static char g_outfit_scene[12];
static size_t g_outfit_scene_bytes;
static uint32_t g_outfit_thread;
static DWORD g_outfit_start;
static int readable(uintptr_t a, size_t n);
typedef char *(__cdecl *LowerFn)(char *);
static const LowerFn cut_model_lower = (LowerFn)0xDFE539;

/* These natives dereference initialized CE player data. Validate that chain
 * before making optional outfit queries; do not query intro/menu state. */
static int outfit_player_ready(uint32_t *player) {
    if (!readable(0x1036F14,4) || !readable(0x18B6F1C,4)) return 0;
    uintptr_t pool=*(uintptr_t *)0x18B6F1C;
    if (!pool || !readable(pool,0x1c)) return 0;
    int index = *(int32_t *)0x1036F14;
    if (index < 0 || index > 31 || !readable(0x11A8808 + 4u*(unsigned)index,4)) return 0;
    uintptr_t info = *(uintptr_t *)(0x11A8808 + 4u*(unsigned)index);
    if (!info || !readable(info,0x59c)) return 0;
    int id = *(int8_t *)(info+0x4e6);
    if (id < 0 || id > 31 || !readable(0x11A8808 + 4u*(unsigned)id,4)) return 0;
    info = *(uintptr_t *)(0x11A8808 + 4u*(unsigned)id);
    if (!info || !readable(info,0x59c)) return 0;
    uintptr_t ped = *(uintptr_t *)(info+0x598);
    if (!ped || !readable(ped,0x220)) return 0;
    uintptr_t clothes = *(uintptr_t *)(ped+0x21c);
    if (!clothes || !readable(clothes,0x5e)) return 0;
    *player=(uint32_t)id;
    return 1;
}
static uint32_t optional_call(NativeFn fn, int argc, const uint32_t *args) {
    NativeCtx c; memset(&c,0,sizeof c); c.ret=c.args=c.buf; c.argc=argc;
    for (int i=0;i<argc;i++) c.buf[i]=args[i];
    fn(&c); return c.buf[0];
}
static void choose_outfit(const char *name) {
    g_outfit_drawable=-1; /* also clears state for unknown/intro/new cutscenes */
    g_outfit_seen_active=0;
    if (!g_outfit_ready || !vc_known_scene(name) || !natives()) return;
    uint32_t player=0;
    if (!outfit_player_ready(&player)) { vlog("outfit '%s': player not ready; keeping original",name); return; }
    NativeFn playing=g_getNative(0x08274BA4), drawable=g_getNative(0x1A1A6D83);
    if (!playing || !drawable) { vlog("outfit '%s': optional native missing; keeping original",name); return; }
    uint32_t a1[1]={player};
    if (!(optional_call(playing,1,a1)&0xff)) return;
    uint32_t ped=0, a2[2]={player,(uint32_t)(uintptr_t)&ped};
    ncall(N_GET_PLAYER_CHAR,2,a2);
    a1[0]=ped;
    if (!ped || !(ncall(N_DOES_CHAR_EXIST,1,a1)&0xff)) return;
    uint32_t outfit_args[2]={ped,1};
    int torso=(int)optional_call(drawable,2,outfit_args);
    if (!vc_outfit_model(torso)) { vlog("outfit '%s': torso %d; keeping original",name,torso); return; }
    g_outfit_thread=ncall(N_GET_ID_OF_THIS_THREAD,0,NULL);
    g_outfit_start=GetTickCount();
    size_t length=strlen(name);
    if (length>=sizeof g_outfit_scene) return;
    memcpy(g_outfit_scene,name,length+1);
    /* CE stores only eight name bytes before adjacent lifecycle flags. Match
     * through NUL for short names, or the intact first eight for existing long
     * _1 aliases. All139 registered CUT names have distinct such prefixes. */
    g_outfit_scene_bytes=length<8?length+1:8;
    g_outfit_drawable=torso;
    vlog("outfit '%s': torso %d -> %s (one model per section)",name,torso,vc_outfit_model(torso));
}
__attribute__((force_align_arg_pointer)) static char *__cdecl h_cut_model_lower(char *token) {
    char *result=cut_model_lower(token); /* preserve original lowercase and cdecl return */
    const char *model=NULL;
    if (readable(0x1295764,8) && g_outfit_scene_bytes &&
        memcmp((const void *)0x1295764,g_outfit_scene,g_outfit_scene_bytes)==0)
        model=vc_replace_tommy(token,g_outfit_drawable);
    if (model) {
        memcpy(token,model,strlen(model)+1);
        vlog("outfit model token -> %s",model);
    }
    return result;
}

/* ---------------- hooked handlers (run inside the calling script thread) ---------------- */
static void __cdecl h_start_cutscene_now(NativeCtx *c) {
    const char *name = *(const char **)c->args;
    choose_outfit(name);
    start_cutscene(name);
    if (g_outfit_drawable>=2 && *(uint32_t *)0x129475C) g_outfit_seen_active=1;
}
static void __cdecl h_has_cutscene_finished(NativeCtx *c) {
    uint8_t r = cutscene_finished();
    *(uint32_t *)c->ret = r;
    if (!r && g_outfit_drawable>=2) g_outfit_seen_active=1;
    if (r && g_outfit_seen_active && g_outfit_drawable>=2 && natives() &&
        ncall(N_GET_ID_OF_THIS_THREAD,0,NULL)==g_outfit_thread) g_outfit_drawable=-1;
}
/* ---------------- per-frame fallback ---------------- */
static void game_tick(void) {
    if (g_outfit_drawable >= 2 && natives()) {
        if (*(uint32_t *)0x129475C) g_outfit_seen_active=1;
        uint32_t owner[1]={g_outfit_thread};
        if (!(ncall(N_IS_THREAD_ACTIVE,1,owner)&0xff) ||
            GetTickCount()-g_outfit_start > (DWORD)S.fallbackMin*60000u) g_outfit_drawable=-1;
    }

}
uintptr_t vccut_prev __asm__("vccut_prev");
__attribute__((force_align_arg_pointer)) void vccut_tick(void) __asm__("vccut_tick");
void vccut_tick(void) { game_tick(); }
__attribute__((naked)) static void tick_stub(void) {
    __asm__ volatile("pushal\n\tpushfl\n\tcall vccut_tick\n\tpopfl\n\tpopal\n\tjmp *vccut_prev\n");
}

/* ---------------- patching ---------------- */
static int readable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return a + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static int bytes_eq(uintptr_t a, const char *hex) {
    const unsigned char *p = (const unsigned char *)a;
    size_t count=0;
    for (const char *q=hex; *q;) { while (*q==' ') q++; if (!*q) break; count++; q+=2; }
    if (!readable(a, count)) return 0;
    for (int i = 0; *hex; i++) {
        while (*hex == ' ') hex++;
        if (!*hex) break;
        if (hex[0] != '?') { unsigned v; sscanf(hex, "%2x", &v); if (p[i] != v) return 0; }
        hex += 2;
    }
    return 1;
}
/* CE 1.2.0.59 hardcodes the display-language count as six despite the
 * frontend XML listing seven options. Keep the native selector, FusionFix's
 * existing preference handling, and the Japanese-edition branch intact. */
static int enable_japanese_menu(void) {
    const uintptr_t address=0x8BB259, immediate=0x8BB266;
    if (!bytes_eq(address,"83 FE 05 75 14 80 3D 53 C2 16 01 00 B9 06 00 00 00 B8 01 00 00 00 0F 45 C8 03 5C 24 18 3B D9")) return 0;
    DWORD old, ignored;
    if (!VirtualProtect((void *)immediate,1,PAGE_EXECUTE_READWRITE,&old)) return 0;
    *(uint8_t *)immediate=7;
    int restored=VirtualProtect((void *)immediate,1,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),(void *)immediate,1);
    return restored && *(uint8_t *)immediate==7;
}
static void write_jmp(uintptr_t at, void *to) {
    DWORD old; VirtualProtect((void *)at, 5, PAGE_EXECUTE_READWRITE, &old);
    *(uint8_t *)at = 0xE9; *(int32_t *)(at + 1) = (int32_t)((uintptr_t)to - (at + 5));
    VirtualProtect((void *)at, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), (void *)at, 5);
}
static int write_call(uintptr_t at, void *to) {
    DWORD old;
    if (!VirtualProtect((void *)at,5,PAGE_EXECUTE_READWRITE,&old)) return 0;
    *(uint8_t *)at=0xE8;
    *(int32_t *)(at+1)=(int32_t)((uintptr_t)to-(at+5));
    DWORD ignored; VirtualProtect((void *)at,5,old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),(void *)at,5);
    return 1;
}
static int outfit_code_supported(void) {
    /* Exact CE data-access ABI and three adjacent parser calls checked before
     * changing the first call. Outfit lifecycle hooks have independent guards. */
    return bytes_eq(0x9C4CB5,"8D 84 24 68 01 00 00 50 E8 77 98 43 00 8D 84 24 E4 00 00 00 50 E8 6A 98 43 00 8D 84 24 38 01 00 00 50 E8 5D 98 43 00") &&
        bytes_eq(0xDFE539,"55 8B EC 83 3D C4 C3 7A 01 00 75 3B 8B 45 08 85 C0") &&
        bytes_eq(0xB9ED90,"56 8B 74 24 08 8B 46 08 FF 70 04 FF 30 E8 FE 79 00 00 8B 0E 83 C4 08 89 01 5E C3") &&
        bytes_eq(0xBA67A0,"FF 74 24 04 8B 0D 1C 6F 8B 01 E8 71 35 8F FF FF 74 24 08 50 E8 17 01 F1 FF 83 C4 08 0F B6 C0 C3") &&
        bytes_eq(0xAB68D0,"8B 44 24 04 8B 88 1C 02 00 00 8B 44 24 08 8A 44 01 5C C3") &&
        bytes_eq(0xB8ACF0,"E8 DB 8A E3 FF 33 C9 84 C0 0F 94 C1 8A C1 C3") &&
        bytes_eq(0x9C37D0,"83 3D 5C 47 29 01 00 0F 95 C0 C3") &&
        bytes_eq(0x9C31F8,"BA 64 57 29 01") &&
        bytes_eq(0xBB3880,"A1 14 6F 03 01 83 F8 FF 75 0A 33 C0 0F BE 80 E6 04 00 00 C3 8B 04 85 08 88 1A 01 0F BE 80 E6 04 00 00 C3") &&
        bytes_eq(0x93FBE0,"8B 44 24 04 83 F8 1F 77 08 8B 04 85 08 88 1A 01 C3 33 C0 C3") &&
        bytes_eq(0xBB37B0,"FF 74 24 04 E8 27 C4 D8 FF 8B 0D 1C 6F 8B 01 83 C4 04 FF B0 98 05 00 00 E8 53 38 90 FF 8B 4C 24 08 89 01 C3") &&
        bytes_eq(0xBB4320,"53 FF 74 24 08 32 DB E8 B4 B8 D8 FF 8B D0 83 C4 04 8B 82 98 05 00 00 32 C9 85 C0 74 08 38 88 11 02 00 00 74 02 B1 01 83 BA E8 04 00 00 02 75 0F 0F B6 C3 BA 01 00 00 00 84 C9 0F 44 C2 5B C3 8A C3 5B C3");
}
static const struct { const char *name; uintptr_t at; const char *bytes; void *to; } HOOKS[] = {
    { "START_CUTSCENE_NOW",         0xB8AC30, "8B 44 24 04 8B 40 08 FF 30 E8 F2 01 00 00 59 C3", (void *)h_start_cutscene_now },
    { "HAS_CUTSCENE_FINISHED",      0xB8AB30, "E8 BB 01 00 00 0F B6 C8 8B 44 24 04 8B 00 89 08 C3", (void *)h_has_cutscene_finished },

};
static void init(void) {
    S.fallbackMin = ini("Outfits", "FallbackMinutes", 20);
    if (S.fallbackMin < 1) S.fallbackMin = 1;
    S.outfits = ini("Outfits","Enabled",1);
    vlog("VCCutscene 1.0 - outfits=%d fallback=%d min; native streaming lifecycle", S.outfits, S.fallbackMin);
    if ((uintptr_t)GetModuleHandleA(NULL)==0x400000 && enable_japanese_menu())
        vlog("CE language menu expanded from 6 to 7 choices; FusionFix handles the saved preference");
    else vlog("CE language menu guard failed; no language menu change");
    if (!S.outfits) return;
    const uintptr_t slot = 0x5C2A41;
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000 ||
        !bytes_eq(0x86FCE0, "56 8B 35 ?? ?? ?? ?? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10") ||
        !bytes_eq(slot - 5, "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8") || !bytes_eq(slot + 15, "B9 ?? ?? ?? ?? E8")) { vlog("unsupported GTAIV.exe - inactive"); return; }
    for (unsigned i = 0; i < sizeof HOOKS / sizeof *HOOKS; i++)
        if (!bytes_eq(HOOKS[i].at, HOOKS[i].bytes)) { vlog("%s handler bytes differ - inactive (nothing patched)", HOOKS[i].name); return; }
    if (S.outfits) {
        if (!outfit_code_supported()) vlog("outfit parser/player ABI differs - outfit selection inactive");
        else if (!(g_outfit_ready=write_call(0x9C4CBD,(void *)h_cut_model_lower))) vlog("outfit parser patch failed - outfit selection inactive");
        else vlog("outfit model parser hooked at 009C4CBD; existing CUT names and animation tokens preserved");
    }
    for (unsigned i = 0; i < sizeof HOOKS / sizeof *HOOKS; i++) { write_jmp(HOOKS[i].at, HOOKS[i].to); vlog("hooked %s at %08X", HOOKS[i].name, (unsigned)HOOKS[i].at); }
    DWORD old; VirtualProtect((void *)slot, 5, PAGE_EXECUTE_READWRITE, &old);
    vccut_prev = slot + 5 + *(int32_t *)(slot + 1);
    *(int32_t *)(slot + 1) = (int32_t)((uintptr_t)tick_stub - (slot + 5));
    VirtualProtect((void *)slot, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), (void *)slot, 5);
    vlog("game-loop check chained at %08X (previous target %08X)", (unsigned)slot, (unsigned)vccut_prev);
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID _) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH); char *s = strrchr(g_dir, '\\'); if (s) *s = 0;
        snprintf(g_logpath, sizeof g_logpath, "%s\\VCCutscene.log", g_dir); DeleteFileA(g_logpath);
        char config[MAX_PATH]; snprintf(config, sizeof config, "%s\\VCCutscene.ini", g_dir);
        vc_profile_load(config);
        if (vc_ini_status < 0) { vlog("settings unreadable or oversized; inactive before hooks"); return TRUE; }
        init();
    }
    return TRUE;
}
