/* ScriptHook.dll - native CE 1.2.0.59 replacement for the small part of aru's C++ ScriptHook API that
 * GTA_IV_DualSense.asi uses (Claude, 2026-11-02, step 161). No .NET, no ScriptHook .NET, no third-party hook.
 *
 * Exports (exact C++ names the DualSense ASI resolves with GetProcAddress):
 *   ??0NativeThread@@QAE@XZ                              NativeThread::NativeThread()  (thiscall)
 *   ?RegisterThread@ScriptHookManager@@SAXPAVNativeThread@@@Z   static void RegisterThread(NativeThread*)
 *   ?GetNativeAddress@Game@@SAPAXPBD@Z                   static void* GetNativeAddress(const char*)
 *   VCSH_Frame                                           called once per game frame by VCScriptHook.asi
 *
 * The client copies our 10-slot vtable and overrides slots 1-3 (rage::scrThread Reset/Run/Tick); every frame we
 * call slot 3 Tick(this, 0) (thiscall, ret 4: VERIFIED in the client at 0x10014117).
 * Native names -> CE native ids from SC-CL natives32.h (the ids CE scripts use; not joaat of the names), resolved
 * through CE's own lookup 0x86FCE0 (stdcall id -> handler; handlers are cdecl(ctx*), ctx: ret +0, argc +4, args +8).
 * Frames run from CE's game-process call slot 0x5C2A41, after the per-frame script draw/text list reset at 0x5C29D8.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "natives_table.h"   /* static const struct { const char *n; uint32_t id; } k_natives[] sorted by name */

static char g_log[MAX_PATH];
static CRITICAL_SECTION g_cs;
static void slog(const char *fmt, ...) {
    EnterCriticalSection(&g_cs);
    FILE *f = fopen(g_log, "a");
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_cs);
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

/* ---------------- CE ---------------- */
typedef void *(__stdcall *GetNativeFn)(uint32_t);
#define CE_LOOKUP 0x86FCE0u
static int g_ce = -1;
static int ce_ok(void) {
    if (g_ce < 0) g_ce = (uintptr_t)GetModuleHandleA(NULL) == 0x400000 &&
        bytes_eq(CE_LOOKUP, "56 8B 35 ?? ?? ?? ?? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10");
    return g_ce;
}
static int table_built(void) {
    uintptr_t p = *(uintptr_t *)(CE_LOOKUP + 3);
    return readable(p, 4) && *(uint32_t *)p;
}
/* Same validated player chain as VCMemory (step 155): only tick once the player ped exists (not in menus/loading). */
static int player_ready(void) {
    if (!readable(0x1036F14, 4) || !readable(0x18B6F1C, 4)) return 0;
    uintptr_t pool = *(uintptr_t *)0x18B6F1C;
    if (!pool || !readable(pool, 0x1c)) return 0;
    int index = *(int32_t *)0x1036F14;
    if (index < 0 || index > 31 || !readable(0x11A8808 + 4u * (unsigned)index, 4)) return 0;
    uintptr_t info = *(uintptr_t *)(0x11A8808 + 4u * (unsigned)index);
    if (!info || !readable(info, 0x59c)) return 0;
    uintptr_t ped = *(uintptr_t *)(info + 0x598);
    return ped && readable(ped, 0x220);
}
static int cmp_name(const void *k, const void *e) { return strcmp((const char *)k, ((const NativeName *)e)->n); }

/* ---------------- NativeThread ---------------- */
typedef void *(__attribute__((thiscall)) *Slot0)(void *, unsigned);
typedef int (__attribute__((thiscall)) *TickFn)(void *, unsigned);
static void *__attribute__((thiscall)) s_dtor(void *self, unsigned flags) { (void)flags; return self; }
static int __attribute__((thiscall)) s_reset(void *self, unsigned h, void *a, unsigned n) { (void)self; (void)h; (void)a; (void)n; return 0; }
static int __attribute__((thiscall)) s_run(void *self, unsigned ops) { (void)self; (void)ops; return 0; }
static int __attribute__((thiscall)) s_tick(void *self, unsigned ops) { (void)self; (void)ops; return 0; }
static void __attribute__((thiscall)) s_void(void *self) { (void)self; }
static void *g_vtbl[10] = { (void *)s_dtor, (void *)s_reset, (void *)s_run, (void *)s_tick, (void *)s_void,
                            (void *)s_void, (void *)s_void, (void *)s_void, (void *)s_void, (void *)s_void };
#define MAX_THREADS 16
static void *volatile g_threads[MAX_THREADS];
static volatile LONG g_nthreads;

void *__attribute__((thiscall)) sh_ctor(void *self) {
    if (self) { memset((char *)self + 4, 0, 0x1c); *(void ***)self = g_vtbl; }
    slog("NativeThread constructed at %p", self);
    return self;
}
void __cdecl sh_register(void *thread) {
    EnterCriticalSection(&g_cs);
    LONG n = g_nthreads;
    if (thread && n < MAX_THREADS) { g_threads[n] = thread; InterlockedExchange(&g_nthreads, n + 1); }
    LeaveCriticalSection(&g_cs);
    slog("RegisterThread(%p) -> %ld thread(s)", thread, (long)g_nthreads);
}
void *__cdecl sh_native(const char *name) {
    if (!name) return NULL;
    const NativeName *e = bsearch(name, k_natives, sizeof k_natives / sizeof *k_natives, sizeof *k_natives, cmp_name);
    if (!e) { slog("GetNativeAddress(%s): name unknown", name); return NULL; }
    if (!ce_ok() || !table_built()) { slog("GetNativeAddress(%s): CE native table not ready", name); return NULL; }
    void *h = ((GetNativeFn)CE_LOOKUP)(e->id);
    slog("GetNativeAddress(%s) id %08X -> %p", name, e->id, h);
    return h;
}

/* ---------------- frame ---------------- */
static int g_ready_logged;
static LONG g_loaded_logged;
void __cdecl VCSH_Frame(void) {
    /* fopen can take a CRT stream lock whose owner needs the DLL loader lock.
     * DllMain holds the loader lock: defer file logging to this game-frame entry. */
    if (!InterlockedExchange(&g_loaded_logged, 1))
        slog("ScriptHook.dll (VC native CE shim, step 161) loaded; %u native names", (unsigned)(sizeof k_natives / sizeof *k_natives));
    LONG n = g_nthreads;
    if (!n || !ce_ok() || !table_built() || !player_ready()) return;
    if (!g_ready_logged) { g_ready_logged = 1; slog("first game frame with player: ticking %ld thread(s)", (long)n); }
    for (LONG i = 0; i < n; i++) {
        void *t = g_threads[i];
        if (!t) continue;
        TickFn f = (TickFn)(*(void ***)t)[3];
        f(t, 0);
    }
}
/* test fixture only: tick regardless of game state */
void __cdecl VCSH_TestTick(void) {
    for (LONG i = 0; i < g_nthreads; i++) { void *t = g_threads[i]; ((TickFn)(*(void ***)t)[3])(t, 0); }
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(inst, g_log, MAX_PATH);
        char *s = strrchr(g_log, '\\'); if (s) s[1] = 0; else g_log[0] = 0;
        strncat(g_log, "VCScriptHook.log", MAX_PATH - strlen(g_log) - 1);
    }
    return TRUE;
}
