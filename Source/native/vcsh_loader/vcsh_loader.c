/* VCScriptHook.asi (step 187: + speaker filter, health/police light bar, siren, heartbeat - ds5extra.h; step 171: + Settings > Audio speaker slider, menusync.h; step 169: DualSense controller audio, ctlaudio.h) - loads plugins/ScriptHook.dll (our native CE shim, step 161) and drives its per-frame tick.
 * DllMain (single-threaded ASI-loader context, same as VCMemory/VCMusic): verify CE 1.2.0.59 code, chain the
 * game-process call slot 0x5C2A41. A worker thread then LoadLibrary's ScriptHook.dll (never inside DllMain) and
 * publishes its VCSH_Frame pointer; the frame stub calls it when set. Unsupported exe -> nothing patched. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static char g_dir[MAX_PATH], g_log[MAX_PATH], g_ini[MAX_PATH];
static void llog(const char *fmt, ...) {
    FILE *f = fopen(g_log, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d] loader: ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int readable(uintptr_t a, size_t n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((void *)a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
    return a + n <= (uintptr_t)mi.BaseAddress + mi.RegionSize;
}
static int bytes_eq(uintptr_t a, const char *hex) {
    size_t count = 0;
    for (const char *q = hex; *q;) { while (*q == ' ') q++; if (!*q) break; count++; q += 2; }
    if (!readable(a, count)) return 0;
    const unsigned char *p = (const unsigned char *)a;
    for (int i = 0; *hex; i++) {
        while (*hex == ' ') hex++;
        if (!*hex) break;
        if (hex[0] != '?') { unsigned v; sscanf(hex, "%2x", &v); if (p[i] != v) return 0; }
        hex += 2;
    }
    return 1;
}
#include "profile_snapshot.h"
#include "vpad.h"
#include "modpatch.h"
#include "ctlaudio.h"
#include "menusync.h"
#include "ds5extra.h"
typedef void (__cdecl *FrameFn)(void);
static FrameFn volatile g_frame;
uintptr_t vcsh_prev __asm__("vcsh_prev");
__attribute__((force_align_arg_pointer)) void vcsh_tick(void) __asm__("vcsh_tick");
static int g_vpad;
void vcsh_tick(void) { if (g_vpad) swap_ce_xinput(); FrameFn f = g_frame; if (f) { f(); ds5_frame(); } }   /* step 187: ds5extra.h */
__attribute__((naked)) static void tick_stub(void) {
    __asm__ volatile("pushal\n\tpushfl\n\tcall vcsh_tick\n\tpopfl\n\tpopal\n\tjmp *vcsh_prev\n");
}
static DWORD WINAPI load_thread(LPVOID _) {
    (void)_;
    char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\ScriptHook.dll", g_dir);
    patch_iat(GetModuleHandleA("GTA_IV_DualSense.asi"), "GTA_IV_DualSense.asi");   /* if it loaded after us */
    ctl_hook_mod();                                                                  /* idem (no-op if done) */
    if (vc_profile_int("Weapons", "VcWeaponIds", 1, g_ini)) patch_mod_weapons();   /* fallback; refuses if already done/read */
    HMODULE m = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) { llog("LoadLibrary(%s) failed (%lu)", path, GetLastError()); return 0; }
    FrameFn f = (FrameFn)GetProcAddress(m, "VCSH_Frame");
    if (!f) { llog("ScriptHook.dll has no VCSH_Frame - not ours; inactive"); return 0; }
    InterlockedExchangePointer((PVOID volatile *)&g_frame, (PVOID)f);
    llog("ScriptHook.dll loaded at %p; frame tick active", (void *)m);
    return 0;
}
static void init(void) {
    const uintptr_t slot = 0x5C2A41;
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000 ||
        !bytes_eq(0x86FCE0, "56 8B 35 ?? ?? ?? ?? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10") ||
        !bytes_eq(0x5C29D8, "E8 ?? ?? ?? ??") ||
        !bytes_eq(slot - 5, "E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8") || !bytes_eq(slot + 15, "B9 ?? ?? ?? ?? E8")) {
        llog("unsupported GTAIV.exe (not CE 1.2.0.59 layout) - inactive, nothing patched"); return;
    }
    llog("DIAG before INI snapshot"); vc_profile_load(g_ini);
    llog("DIAG after INI snapshot status=%d bytes=%lu", vc_ini_status, vc_ini_len);
    if (vc_ini_status < 0) { llog("settings unreadable or larger than 1 MiB: plugin inactive, no game hooks changed"); return; }
    DWORD old; VirtualProtect((void *)slot, 5, PAGE_EXECUTE_READWRITE, &old);
    vcsh_prev = slot + 5 + *(int32_t *)(slot + 1);
    *(int32_t *)(slot + 1) = (int32_t)((uintptr_t)tick_stub - (slot + 5));
    VirtualProtect((void *)slot, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), (void *)slot, 5);
    llog("game-loop tick chained at %08X (previous target %08X)", (unsigned)slot, (unsigned)vcsh_prev);
    llog("DIAG before profile Weapons");
    int weapons = vc_profile_int("Weapons", "VcWeaponIds", 1, g_ini);
    llog("DIAG after profile Weapons=%d", weapons);
    if (weapons) { llog("DIAG before patch_mod_weapons"); patch_mod_weapons(); llog("DIAG after patch_mod_weapons"); }
    llog("DIAG before CreateThread(load_thread)");
    HANDLE h = CreateThread(NULL, 0, load_thread, NULL, 0, NULL);
    llog("DIAG after CreateThread(load_thread) handle=%p", h);
    if (h) CloseHandle(h);
    llog("DIAG before profile VirtualPad");
    int vpad = vc_profile_int("VirtualPad", "Enabled", 1, g_ini);
    llog("DIAG after profile VirtualPad=%d", vpad);
    if (vpad) {
        llog("DIAG before InitializeCriticalSection");
        InitializeCriticalSection(&v_cs); g_vpad = 1;
        llog("DIAG after InitializeCriticalSection, before module lookup");
        HMODULE mod = GetModuleHandleA("GTA_IV_DualSense.asi");
        llog("DIAG after module lookup=%p, before patch_iat", mod);
        patch_iat(mod, "GTA_IV_DualSense.asi");
        llog("DIAG after patch_iat, before CreateThread(v_thread)");
        h = CreateThread(NULL, 0, v_thread, NULL, 0, NULL); if (h) CloseHandle(h);
        llog("DIAG after CreateThread(v_thread), before CreateThread(v_swap_thread)");
        h = CreateThread(NULL, 0, v_swap_thread, NULL, 0, NULL); if (h) CloseHandle(h);
        llog("DIAG after CreateThread(v_swap_thread)");
    } else llog("virtual pad disabled in VCScriptHook.ini");
    llog("DIAG before ctl_init"); ctl_init(); llog("DIAG after ctl_init");
    llog("DIAG before ms_init"); ms_init(); llog("DIAG after ms_init");
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH); char *s = strrchr(g_dir, '\\'); if (s) *s = 0;
        snprintf(g_log, sizeof g_log, "%s\\VCScriptHook.log", g_dir); snprintf(g_ini, sizeof g_ini, "%s\\VCScriptHook.ini", g_dir); DeleteFileA(g_log);
        init();
    }
    return TRUE;
}
