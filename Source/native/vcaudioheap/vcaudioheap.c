#include "audio_heap.h"
#include <stdio.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    char path[MAX_PATH];
    FILE *log;
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
    log = fopen(path, "w");
    if (!log) return TRUE;
    fprintf(log, "VCAudioHeap 1.0, CE 1.2.0.59, result=%d\n", result);
    if (result == AH_APPLIED)
        fprintf(log, "APPLIED before audio initialization: physical audio heap 126/115 -> 192 MiB (both RAM branches).\n");
    else if (result == AH_RESTORE_FAILED)
        fprintf(log, "Capacity written, but restoring code-page protection failed.\n");
    else
        fprintf(log, "INACTIVE: unsupported/modified code, late load or protection failure; no capacity written.\n");
    fclose(log);
    return TRUE;
}
