#include "audio_heap.h"
#include "startup_log.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    char path[MAX_PATH];
    enum ah_result result;
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000u)
        result = AH_WRONG_IMAGE;
    else result = ah_apply((unsigned char *)GetModuleHandleA(NULL));
    if (!GetModuleFileNameA(instance, path, sizeof path)) return TRUE;
    char *slash = strrchr(path, '\\');
    if (!slash || (size_t)(slash - path) + sizeof "\\VCAudioHeap.log" > sizeof path)
        return TRUE;
    strcpy(slash, "\\VCAudioHeap.log");
    char message[512];
    const char *detail = result == AH_APPLIED
        ? "APPLIED before audio initialization: physical audio heap 126/115 -> 192 MiB (both RAM branches).\n"
        : result == AH_RESTORE_FAILED
        ? "Capacity written, but restoring code-page protection failed.\n"
        : "INACTIVE: unsupported/modified code, late load or protection failure; no capacity written.\n";
    int n = snprintf(message, sizeof message, "VCAudioHeap 1.0, CE 1.2.0.59, result=%d\n%s", result, detail);
    if (n > 0 && n < (int)sizeof message) vc_write_log(path, message, (DWORD)n, 0);
    return TRUE;
}
