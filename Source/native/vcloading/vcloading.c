/* SPDX-License-Identifier: GPL-3.0-or-later
 * Targeted adaptation of GTAIV.EFLC.FusionFix loadingdelays.ixx, commit
 * ed3ace47581c171f20d4557caaedfd7c89750ff7 (2026-11-09), Fusion Team.
 * Modified 2026-11-11: C translation, exact CE1.2.0.59 guards and VCCore entry.
 * Upstream source and license are preserved in ../upstream/.
 *
 * This changes two loading-screen render-thread callsites only. It never
 * purges resources, alters streaming budgets, clears the world or hooks a
 * script native. The two original waits remain whenever the render function
 * changes. No frame hook, worker, polling or external DLL load is added.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "startup_log.h"
#include "engine_contract.h"

typedef unsigned char (__fastcall *LoadingActive)(uint8_t *, void *);
typedef void (__fastcall *RenderFlush)(uint8_t *, void *, int);
static LoadingActive is_loading_active;
static RenderFlush original_flush;
static int loading_screen_already_up;

static void __fastcall flush_before_loading(uint8_t *self, void *edx, int mode) {
    /* Match upstream exactly: AL is the predicate's bool return, and the
     * complete active/pending render-function records must match (16 bytes).
     * Save the decision before SetRenderFunction changes the pending record. */
    loading_screen_already_up = is_loading_active(self, edx) &&
        memcmp(self + 0xfe0, self + 0xfd0, 16) == 0;
    if (!loading_screen_already_up)
        original_flush(self, edx, mode);
}

static void __fastcall flush_after_loading(uint8_t *self, void *edx, int mode) {
    if (!loading_screen_already_up)
        original_flush(self, edx, mode);
}

enum vl_result { VL_APPLIED, VL_WRONG_IMAGE, VL_WRONG_CODE,
                 VL_PROTECT_FAILED, VL_RESTORE_FAILED };

static void write_call(uint8_t *site, void *target) {
    uint32_t relative = (uint32_t)(uintptr_t)target - (uint32_t)(uintptr_t)(site + 5);
    site[0] = 0xe8;
    memcpy(site + 1, &relative, sizeof relative);
}

static enum vl_result loading_apply(uint8_t *base) {
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS32 *pe;
    DWORD old, ignored;
    uint8_t *first;
    const size_t span = VL_AFTER_RVA + 5 - VL_BEFORE_RVA;
    /* These CE code contracts contain original absolute addresses. Refuse
     * relocation as well as a different executable before dereferencing them. */
    if ((uintptr_t)base != 0x400000u)
        return VL_WRONG_IMAGE;
    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return VL_WRONG_IMAGE;
    pe = (IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE ||
        pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != 0x63d3e735u ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.SizeOfImage != VL_IMAGE_SIZE ||
        !(pe->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE))
        return VL_WRONG_IMAGE;
    /* Released FusionFix's InfiniteLoadingWorkaround2 can detour the first
     * ten bytes of SetLoadingRenderFunction. Guard its untouched body from
     * +0x0a onward and leave that loading FPS limiter's entry hook alone. */
    static const uint8_t stock_entry[10] = {0x83,0xec,0x28,0x83,0x3d,0x24,0x0c,0x16,0x01,0x34};
    static const uint8_t detour_tail[5] = {0x90,0x90,0x90,0x90,0x90};
    uint8_t *entry = base + VL_SET_LOADING_RVA - 10;
    int supported_entry = memcmp(entry, stock_entry, sizeof stock_entry) == 0 ||
        (entry[0] == 0xe9 && memcmp(entry + 5, detour_tail, sizeof detour_tail) == 0);
    if (!supported_entry ||
        memcmp(base + VL_SET_LOADING_RVA, vl_set_loading_code, sizeof vl_set_loading_code) ||
        memcmp(base + VL_LOADING_ACTIVE_RVA, vl_loading_active_code, sizeof vl_loading_active_code) ||
        memcmp(base + VL_FLUSH_RVA, vl_flush_code, sizeof vl_flush_code) ||
        memcmp(base + VL_SET_RENDER_RVA, vl_set_render_code, sizeof vl_set_render_code))
        return VL_WRONG_CODE;
    first = base + VL_BEFORE_RVA;
    if (!VirtualProtect(first, span, PAGE_EXECUTE_READWRITE, &old))
        return VL_PROTECT_FAILED;
    is_loading_active = (LoadingActive)(base + VL_LOADING_ACTIVE_RVA);
    original_flush = (RenderFlush)(base + VL_FLUSH_RVA);
    loading_screen_already_up = 0;
    write_call(first, (void *)flush_before_loading);
    write_call(base + VL_AFTER_RVA, (void *)flush_after_loading);
    FlushInstructionCache(GetCurrentProcess(), first, span);
    if (!VirtualProtect(first, span, old, &ignored))
        return VL_RESTORE_FAILED;
    return VL_APPLIED;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        char path[MAX_PATH], *slash;
        DWORD n;
        enum vl_result result;
        DisableThreadLibraryCalls(instance);
        result = loading_apply((uint8_t *)GetModuleHandleA(NULL));
        n = GetModuleFileNameA(instance, path, MAX_PATH);
        slash = n && n < MAX_PATH ? strrchr(path, '\\') : NULL;
        if (slash && (size_t)(slash - path) + sizeof "\\VCLoading.log" <= sizeof path) {
            strcpy(slash, "\\VCLoading.log");
            static const char *messages[] = {
                "ACTIVE: FusionFix ed3ace native loading handoff; two guarded CE callsites; transition flushes preserved.\n",
                "INACTIVE: unsupported executable layout.\n",
                "INACTIVE: native loading code differs or another loading hook is present; neither callsite changed.\n",
                "INACTIVE: unable to make both loading callsites writable.\n",
                "ACTIVE: native loading handoff installed; unable to restore original page protection.\n"
            };
            vc_write_log(path, messages[result], (DWORD)strlen(messages[result]), 0);
        }
    }
    return TRUE;
}
