// The FSR 4 start-up test. Where automatic FSR would pass FSR 4 by (FsrBelowFsr4: shader model below 6.6, a rule from one card),
// the helper starts a throwaway copy of itself that makes the same device, loads AMD's library the same way, creates the context
// automatic would create at the build's sizes, and runs its first dispatch. FSR 4 ran: automatic runs it. It failed, raised or
// died: the newest FSR 3, and the warnings file says so. No answer in time: the same, and the warnings file says how to skip the
// test. A second process because a library that raised is never called again in its process.
// It runs once a session (about a quarter of a second; a cold shader compile can take 19 to 22 s, hence the 30 s wait), never for
// fsr_version=3 or 4, never under Wine, and not when too little of the client's 60 s is left.
// The copy is this exe with --fsr4-test=W,H,OW,OH,FLAGS,CFMT,OFMT,LOG,LUID,FAULT,LOUD (Fsr4TestArg): no pipe, its own textures,
// and at most one log line, free of the words test kits read as faults (raised, failed, fault:, stopped, refused).
// Development builds: --fsr4_test_fault=N plants 1 an uncaught exception, 2 a hang, 3 a clean failure, 4 a silent death;
// --fsr4_test_ms=N shortens the wait; --fsr4_test_loud=1 lets the copy log everything.
#pragma once

static bool CreateFsrContext(UINT w, UINT hh, UINT outW, UINT outH, uint32_t flags);
static ID3D12Resource* MakeSharedTex(UINT w, UINT h_, DXGI_FORMAT fmt, bool uav, HANDLE* outHandle);

// the build's 55 s; what the helper's own context needs after the test; the longest wait for the copy; the copy's own life
enum { FSR4T_BUDGET_MS = 55000, FSR4T_AFTER_MS = 15000, FSR4T_MAX_MS = 30000, FSR4T_LIFE_MS = 45000 };

static bool          g_fsr4Test = false;          // this process is the copy
static unsigned      g_f4[8] = {};                // the copy's: w, h, outW, outH, flags, color format, output format, log level
static unsigned char g_f4Luid[8] = {};            // the copy's: the adapter its parent runs on
static int           g_f4Fault = 0, g_f4Loud = 0; // a development build's planted fault and loud copy; nothing writes them in a release
static DWORD         g_f4MaxMs = FSR4T_MAX_MS;
static int           g_fsrMajorInUse = 0;         // the FSR that survived the last CreateFsrContext, 0 none
static int           g_f4Answer = -1;             // this session's answer once it has asked: 1 runs, 0 does not

// FSR4 TEST PURE START (tools/test-fsr-pick cuts from here to the END line and holds it on a bench)
enum { FSR4T_RAN = 40, FSR4T_FAILED = 41, FSR4T_RAISED = 42 };       // the copy's exit codes; anything else is no answer
// WHAT THE COPY'S END SAYS: 1 FSR 4 runs, 0 it does not, -1 no answer (the rule decides, and the player is told how to
// skip the test). A copy that died of an exception no filter saw (a stack overflow, a fast fail) ends with that
// exception's code, and FSR 4 did not run. Any other code is a copy that never got to the question.
static int Fsr4TestAnswer(bool ended, DWORD code, const char** what)
{
    if (!ended)            { *what = "did not end"; return -1; }
    if (code == FSR4T_RAN) { *what = "runs"; return 1; }
    if (code == FSR4T_FAILED || code == FSR4T_RAISED || code >= 0x80000000u) { *what = "does not run"; return 0; }
    *what = "no answer"; return -1;
}
// FSR4 TEST PURE END

// The player is told when the test cost a wait and gave nothing: one line appended to the game's warnings file beside the log (the
// client owns and empties it at start); where it cannot be opened, the log's line has to do.
static void Fsr4TestWarn(unsigned long ms)
{
    char path[MAX_PATH];
    strcpy_s(path, MAX_PATH, g_logPath);
    char* s = strrchr(path, '\\');
    if (!s) return;
    s[1] = 0;
    if (strcat_s(path, MAX_PATH, "DLSS-IV-WARNINGS.txt")) return;
    FILE* f = _fsopen(path, "a", _SH_DENYNO);
    if (!f) return;
    fprintf(f, "FSR 3.1 is running this session: DLSS-IV tests FSR 4 when the game starts, and the test gave no answer in %lu seconds."
               " It runs again at the next start. To skip it, set fsr_version in DLSS-IV.cfg: 3 for FSR 3.1, or 4 if you know that"
               " FSR 4 runs on this PC.\n", (ms + 500) / 1000);
    fclose(f);
}

// ---- The parent's half ----
// Called by CreateFsrContext where the rule would pass FSR 4 by. True: FSR 4 is not passed by.
static bool Fsr4TestSaysRun(UINT w, UINT hh, UINT outW, UINT outH, uint32_t flags)
{
    if (g_fsr4Test) return true;                                                       // the copy IS the test: it runs what automatic would with no rule
    if (g_f4Answer >= 0) return g_f4Answer == 1;                                       // this session has asked
    if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) return false;   // Linux: the rule decides, as it did
    const ULONGLONG used = g_buildStartTick ? GetTickCount64() - g_buildStartTick : 0;
    if (used + g_f4MaxMs + FSR4T_AFTER_MS > FSR4T_BUDGET_MS) { LogRecord("FSR: FSR 4 test: not run, %llu ms of this build used", (unsigned long long)used); return false; }

    char exe[MAX_PATH] = "", cmd[1100], luid[20] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    for (int i = 0; i < 8; i++) _snprintf_s(luid + 2 * i, sizeof(luid) - 2 * i, _TRUNCATE, "%02X", g_luid[i]);
    if (_snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" %lu --backend=1 --fsr_version=0 --log=0%s%s%s --fsr4-test=%u,%u,%u,%u,%u,%u,%u,%d,%s,%d,%d",
                    exe, GetCurrentProcessId(), g_logPath[0] ? " \"--logpath=" : "", g_logPath, g_logPath[0] ? "\"" : "",
                    w, hh, outW, outH, (unsigned)flags, (unsigned)h.colorFmt, (unsigned)h.outputFmt, g_logLevel, luid, g_f4Fault, g_f4Loud) < 0)
    { LogRecord("FSR: FSR 4 test: not run, its command line does not fit"); return false; }
    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
    const ULONGLONG t0 = GetTickCount64();
    if (!CreateProcessA(exe, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    { LogRecord("FSR: FSR 4 test: not run, the copy did not start (%lu)", GetLastError()); return false; }
    const bool ended = WaitForSingleObject(pi.hProcess, g_f4MaxMs) == WAIT_OBJECT_0;
    DWORD code = 0xFFFFFFFFul;
    if (ended) GetExitCodeProcess(pi.hProcess, &code);
    else { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 2000); }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    const unsigned long ms = (unsigned long)(GetTickCount64() - t0);

    const char* what = "";
    const int answer = Fsr4TestAnswer(ended, code, &what);
    LogRecord("FSR: FSR 4 test: %s (exit 0x%lX, %lu ms)", what, code, ms);
    if (!ended) Fsr4TestWarn(ms);
    g_f4Answer = answer == 1 ? 1 : 0;                // asked once a session, whatever came back
    return answer == 1;
}

// ---- The copy's half ----
// main hands every argument to Fsr4TestArg, makes the device on Fsr4TestLuid's adapter and loads AMD's library as any helper does,
// then calls Fsr4TestChild in place of Serve.
static DWORD WINAPI Fsr4TestLife(LPVOID parent)
{   // the copy ends with its parent, and never outlives its use
    WaitForSingleObject((HANDLE)parent, FSR4T_LIFE_MS);
    TerminateProcess(GetCurrentProcess(), 1);
    return 0;
}
static bool Fsr4TestArg(const char* a)
{
#if DLSSIV_DEV
    if (strncmp(a, "--fsr4_test_fault=", 18) == 0) { g_f4Fault = atoi(a + 18); return true; }
    if (strncmp(a, "--fsr4_test_loud=", 17) == 0)  { g_f4Loud = atoi(a + 17); return true; }
    if (strncmp(a, "--fsr4_test_ms=", 15) == 0)    { const int v = atoi(a + 15); if (v > 0 && v < FSR4T_MAX_MS) g_f4MaxMs = (DWORD)v; return true; }
#endif
    if (strncmp(a, "--fsr4-test=", 12) != 0) return false;
    g_fsr4Test = true;
    // BEFORE THE DEVICE: nothing this copy does may put a box on the screen.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    char luid[20] = "";
    if (sscanf_s(a + 12, "%u,%u,%u,%u,%u,%u,%u,%u,%16[0-9A-Fa-f],%d,%d", &g_f4[0], &g_f4[1], &g_f4[2], &g_f4[3], &g_f4[4], &g_f4[5], &g_f4[6], &g_f4[7],
                 luid, (unsigned)sizeof(luid), &g_f4Fault, &g_f4Loud) != 11 || strlen(luid) != 16)
    { g_f4[0] = 0; return true; }                    // not a test this build knows: Fsr4TestChild ends with no answer
    for (int i = 0; i < 8; i++) { unsigned b = 0; sscanf_s(luid + 2 * i, "%2x", &b); g_f4Luid[i] = (unsigned char)b; }
#if !DLSSIV_DEV
    g_f4Fault = 0; g_f4Loud = 0;
#endif
    return true;
}
static const unsigned char* Fsr4TestLuid() { return g_fsr4Test ? g_f4Luid : nullptr; }
static void Fsr4TestSay(_Printf_format_string_ const char* fmt, ...)
{   // the copy's one line, at the level its parent logs at
    if ((int)g_f4[7] < 1) return;
    if (!g_logMutex) g_logMutex = CreateMutexA(nullptr, FALSE, "Local\\DlssIvLog");
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
static LONG WINAPI Fsr4TestUnhandled(EXCEPTION_POINTERS* xp)
{   // on a thread no wrapper guards (AMD's own, the driver's): said with its place, and the copy ends as a raise
    RaiseNote(xp);
    Fsr4TestSay("FSR: FSR 4 test: exception 0x%08X%s", (xp && xp->ExceptionRecord) ? (unsigned)xp->ExceptionRecord->ExceptionCode : 0u, g_raiseWhere);
    TerminateProcess(GetCurrentProcess(), (UINT)FSR4T_RAISED);
    return EXCEPTION_EXECUTE_HANDLER;
}
static int Fsr4TestChild(DWORD parentPid)
{
    SetUnhandledExceptionFilter(Fsr4TestUnhandled);
    HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!hp) return 1;                                // the parent is gone already: nobody to answer
    if (HANDLE t = CreateThread(nullptr, 0, Fsr4TestLife, hp, 0, nullptr)) CloseHandle(t);
    if (g_f4Loud) { g_logLevel = (int)g_f4[7]; if (g_logLevel > 0 && !g_logMutex) g_logMutex = CreateMutexA(nullptr, FALSE, "Local\\DlssIvLog"); }
    int code = 1;                                     // no device on the parent's adapter, or no library: no answer
    if (g_f4[0] && h.dev && g_luidValid && memcmp(g_luid, g_f4Luid, sizeof(g_luid)) == 0 && g_backend == 1 && g_ffxDll)
    {
        h.width = g_f4[0]; h.height = g_f4[1]; h.outW = g_f4[2]; h.outH = g_f4[3];
        h.colorFmt = (DXGI_FORMAT)g_f4[5]; h.outputFmt = (DXGI_FORMAT)g_f4[6];
        h.mvScaleX = 1.0f; h.mvScaleY = 1.0f; HOST_DEV(h.transportOnly = false;)
        struct SlotSpec { UINT w, hh; DXGI_FORMAT fmt; bool uav; };
        const SlotSpec spec[FEED_SLOTS] = {             // as the build handler makes them
            { h.width, h.height, h.colorFmt,               false },  // COLOR
            { h.outW,  h.outH,   h.outputFmt,              true  },  // OUTPUT
            { h.width, h.height, DXGI_FORMAT_R32_FLOAT,    false },  // DEPTH
            { h.width, h.height, DXGI_FORMAT_R16G16_FLOAT, false },  // MV
        };
        bool tex = true;
        for (int i = 0; i < FEED_SLOTS && tex; i++)
        {
            HANDLE local = nullptr;
            h.tex[i] = MakeSharedTex(spec[i].w, spec[i].hh, spec[i].fmt, spec[i].uav, &local);
            if (local) CloseHandle(local);
            if (!h.tex[i]) tex = false;
        }
#if DLSSIV_DEV
        if (g_f4Fault == 1) RaiseException(0xE0F54001, EXCEPTION_NONCONTINUABLE, 0, nullptr);   // an exception nothing catches
        if (g_f4Fault == 2) Sleep(INFINITE);                                                    // a copy that never ends
        if (g_f4Fault == 3) tex = false;                                                        // a clean failure
        if (g_f4Fault == 4) TerminateProcess(GetCurrentProcess(), 0xC0000409u);                 // a copy that dies without a word
#endif
        if (!tex) { code = FSR4T_FAILED; Fsr4TestSay("FSR: FSR 4 test: no textures of %ux%u -> %ux%u", h.width, h.height, h.outW, h.outH); }
        else
        {
            const bool ok = CreateFsrContext(h.width, h.height, h.outW, h.outH, (uint32_t)g_f4[4]);
            code = g_ffxRaised ? FSR4T_RAISED : (ok && g_fsrMajorInUse >= 4) ? FSR4T_RAN : FSR4T_FAILED;
            if (g_ffxRaised)            Fsr4TestSay("FSR: FSR 4 test: FSR 4 faulted%s", g_raiseWhere);
            else if (code != FSR4T_RAN) Fsr4TestSay(ok ? "FSR: FSR 4 test: FSR %d came up, not FSR 4" : "FSR: FSR 4 test: no FSR context", g_fsrMajorInUse);
        }
    }
    // AT ONCE, so that nothing in the library's or the driver's own teardown can turn the answer into something else.
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    return code;
}
