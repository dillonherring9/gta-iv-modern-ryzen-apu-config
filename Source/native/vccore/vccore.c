/* VCCore.asi (step 252): VCAudioHeap, VCCutscene, VCMusic, VCScriptHook, native loading and VCAPC.
 *
 * Each module is a separate translation unit with DllMain renamed.
 * Step248 replaces VCMemory with outfit/language-only VCCutscene.
 * Dispatch follows their observed original relative order, not an ASI-loader sorting guarantee.
 * Thus the gameplay frame chain remains VCScriptHook -> VCMusic -> VCCutscene -> previous.
 * The optional, read-only VCPhysicsCheck developer logger remains independently removable;
 * its place in the chain can move relative to VCScriptHook and it is excluded from releases.
 * VCCutscene.ini/log replaces VCMemory.ini/log. VCMusic and ScriptHook paths remain.
 *
 * Never mix embedded and standalone hooks: if any legacy module file/name exists,
 * leave the complete old ASI set in charge and run no embedded modules. A partial
 * old set is an installation conflict, recorded in VCCore.log. The guarded installer
 * removes all four old ASIs together. This also protects extraction over old releases.
 * The loading module touches only its two guarded render-thread CALL sites; it
 * does not join or change the gameplay frame chain.
 * Step252 VCAPC attaches last, after the three retained frame hooks are installed
 * exactly as before, so the chain is VCAPC -> VCScriptHook -> VCMusic -> VCCutscene
 * -> previous. It only protects live APC/Rhino vehicles (see vcapc.c).
 * No LoadLibrary, CRT file streams, thread waits or new startup workers occur here.
 */
#include <windows.h>
#include <string.h>
#include "startup_log.h"

BOOL WINAPI vcaudioheap_DllMain(HINSTANCE, DWORD, LPVOID);
BOOL WINAPI vccutscene_DllMain(HINSTANCE, DWORD, LPVOID);
BOOL WINAPI vcmusic_DllMain(HINSTANCE, DWORD, LPVOID);
BOOL WINAPI vcsh_loader_DllMain(HINSTANCE, DWORD, LPVOID);
BOOL WINAPI vcloading_DllMain(HINSTANCE, DWORD, LPVOID);
BOOL WINAPI vcapc_DllMain(HINSTANCE, DWORD, LPVOID);
static const struct { const char *file; BOOL (WINAPI *main)(HINSTANCE, DWORD, LPVOID); } modules[] = {
    { "VCAudioHeap.asi", vcaudioheap_DllMain }, { "VCMemory.asi", vccutscene_DllMain },
    { "VCMusic.asi", vcmusic_DllMain }, { "VCScriptHook.asi", vcsh_loader_DllMain } };
enum { COUNT = sizeof modules / sizeof *modules };
static int active;

static void note(const char *log, const char *text) { vc_write_log(log, text, (DWORD)strlen(text), 1); }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        char dir[MAX_PATH], log[MAX_PATH], path[MAX_PATH], line[MAX_PATH + 96];
        DWORD n = GetModuleFileNameA(inst, dir, MAX_PATH);
        char *slash = (n && n < MAX_PATH) ? strrchr(dir, '\\') : NULL;
        if (!slash || (size_t)(slash - dir) + sizeof "\\VCScriptHook.asi" > sizeof dir)
            return TRUE;  /* Fail closed if the folder cannot be represented safely. */
        *slash = 0;
        snprintf(log, sizeof log, "%s\\VCCore.log", dir);
        DeleteFileA(log);
        note(log, "VCCore 1.3 (step 252): VCAudioHeap + VCCutscene + VCMusic + VCScriptHook + native loading handoff + VCAPC\n");
        unsigned standalone_count = 0;
        for (unsigned i = 0; i < COUNT; i++) {
            snprintf(path, sizeof path, "%s\\%s", dir, modules[i].file);
            DWORD attr = GetFileAttributesA(path);
            int standalone = (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                             || GetModuleHandleA(modules[i].file) != NULL;
            if (standalone) {
                standalone_count++;
                snprintf(line, sizeof line, "%s: legacy standalone file or loaded module detected\n", modules[i].file);
                note(log, line);
            }
        }
        active = standalone_count == 0;
        if (!active) {
            note(log, standalone_count == COUNT
                 ? "INACTIVE: complete legacy ASI set retained; no embedded hooks installed. Remove all four old ASIs to use VCCore.\n"
                 : "INACTIVE: partial legacy ASI conflict; no embedded hooks installed. Remove all four old ASIs to use VCCore.\n");
        } else {
            note(log, "ACTIVE: four retained modules plus guarded native loading handoff and VCAPC run from VCCore.\n");
        }
    }
    if (active) {
        /* All four pinned handlers return TRUE; they handle PROCESS_ATTACH only.
         * Their existing DisableThreadLibraryCalls calls now apply to this one DLL.
         * Reverse detach mirrors normal DLL unload order; current handlers ignore it. */
        if (reason == DLL_PROCESS_DETACH) {
            vcapc_DllMain(inst, reason, reserved);
            vcloading_DllMain(inst, reason, reserved);
            for (unsigned i = COUNT; i > 0; i--) modules[i - 1].main(inst, reason, reserved);
        } else {
            for (unsigned i = 0; i < COUNT; i++) modules[i].main(inst, reason, reserved);
            vcloading_DllMain(inst, reason, reserved);
            vcapc_DllMain(inst, reason, reserved);
        }
    }
    return TRUE;
}
