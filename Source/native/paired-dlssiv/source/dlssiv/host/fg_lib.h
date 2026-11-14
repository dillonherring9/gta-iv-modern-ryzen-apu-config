// The development load, the module's identity and the debug layer's count. The mod's ffx-own-fg.dll answers the same provider name
// and id as AMD's signed library, and AMD's code ignores its pipeline creations' results, so these instruments tell the two apart.
// Development builds only, when asked (FgHost::lib stays null otherwise), with --fg=1:
//     --fglib=<file>        load that file (a bare name beside this helper, or a full path) instead of asking AMD's loader; an
//                           AMD name is refused; any refusal is one line ending ": off", never a fallback. Hashed before
//                           LoadLibrary through a handle that denies writing.
//     --fglib_sha=<64 hex>  the SHA-256 the module must have
//     --fglib_stock=1       a desk control: --fglib may name a file of AMD's name
//     --fgid=1              name the module AMD's loader chose (implied by --fglib, --fglib_sha)
//     --d3d12_debug=1       D3D12's debug layer, its messages counted at the probe, a set's create and every teardown; 2 adds
//                           a junk pipeline as the count's own control
// What it says:
//     frame generation: fglib <full path>, <bytes> bytes, sha256 <hex>: loaded direct
//     frame generation: fglib <as given>: not a full path: off                 a value with a separator that is not one
//     frame generation: fglib <as given>: not a file beside the helper: off    a bare name that does not stay in its folder
//     frame generation: fglib <as given>: an AMD name: off
//     frame generation: fglib <full path>: not read (<system code>): off       2 is "no such file"
//     frame generation: fglib <full path>, <bytes> bytes, sha256 <hex>: not the expected sha256 <hex>: off
//     frame generation: fglib <full path>, <bytes> bytes, sha256 <hex>: would not load (<system code>): off
//     frame generation: fglib <full path>, <bytes> bytes, sha256 <hex>: lacks an ffx-api entry point: off
//     frame generation: fglib <full path>, <bytes> bytes, sha256 <hex>: the module mapped is <path>: off
//     frame generation: fglib_sha: not 64 hex digits: off
//     frame generation: fgid: <path>, <bytes> bytes, sha256 <hex>: mapped by AMD's loader
//     frame generation: device: ...        shader model, wave lanes, 16-bit support, and the variants AMD's code takes
//     frame generation: fgid: set WxH format F, guides WxH: context <bytes> bytes        every set that was created
//     frame generation: frames by <path>, <bytes> bytes, sha256 <hex>, <how>, provider "<name>" id 0x<id>
//                                          once a load, at its first created set: the module, not only the provider
//     frame generation: fgid: <set end|exit>: amd_fidelityfx_framegeneration_dx12.dll mapped: <no|yes, path>
//     d3d12 debug: <where>: <n> messages (...)                                           and the first twenty in full
// fg_host.h's rules hold: one pointer (FgHost::lib), no static but SHA-256's table, entry points not inlined, none called from Serve.
#pragma once

struct FgLib
{
    char   path[MAX_PATH];           // --fglib, as a full path; empty when it was not given
    BYTE   want[32]; bool haveWant;  // --fglib_sha
    bool   ident;                    // the module's lines are said (--fgid=1, or implied)
    bool   refused;                  // THE REFUSAL: said once, final for the process. FgLoad answers false from then on
    // the module that serves frame generation
    bool   direct;                   // it was loaded by --fglib
    bool   modKnown;
    char   modPath[MAX_PATH]; UINT64 modSize; BYTE modSha[32];
    bool   saidFrames;               // the "frames by" line was said for this load
    // the debug layer
    int    dbg;                      // --d3d12_debug
    bool   dbgOn;                    // EnableDebugLayer was called before the first device
    bool   dbgPlanted, dbgSaidNoQueue;
    int    dbgSaid;                  // messages printed in full so far (twenty at most)
    UINT64 dbgLostSeen;              // what the queue's own limit had dropped at the last count
};

#include "fg_sha.h"   // SHA-256, a file's size and hash, and two string tests (the shipped helper's own arm reads them too)

// ---- The debug layer ----
// Before the first device: the layer is a process switch, and a device made before it never has it.
static void FglDebugOn(FgLib* L, bool agility)
{
    if (agility) { LogRecord("d3d12 debug: not with an Agility runtime beside the helper: off"); return; }
    typedef HRESULT (WINAPI *PFN_GetDebug)(REFIID, void**);
    const HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    const auto get = d3d12 ? (PFN_GetDebug)GetProcAddress(d3d12, "D3D12GetDebugInterface") : nullptr;
    ID3D12Debug* dbg = nullptr;
    const HRESULT hr = get ? get(__uuidof(ID3D12Debug), (void**)&dbg) : E_NOINTERFACE;
    if (FAILED(hr) || !dbg) { LogRecord("d3d12 debug: not available here (0x%08X): the run goes on without it", (unsigned)hr); return; }
    dbg->EnableDebugLayer();
    dbg->Release();
    L->dbgOn = true;
    LogRecord("d3d12 debug: on, asked before the first device (--d3d12_debug=%d)", L->dbg);
}
// THE COUNT: what the device's queue holds is read, counted by severity, printed in full while fewer than twenty have
// been, and cleared. One line says the count when there is one to say or when the caller wants the line whatever it is.
static void FglDebugCount(FgLib* L, const char* where, bool always)
{
    if (!L->dbgOn || !h.dev) return;
    ID3D12InfoQueue* iq = nullptr;
    if (FAILED(h.dev->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&iq)) || !iq)
    {
        if (!L->dbgSaidNoQueue) { L->dbgSaidNoQueue = true; LogRecord("d3d12 debug: this device has no message queue: the layer is NOT on it, nothing is counted"); }
        return;
    }
    if (L->dbg >= 2 && !L->dbgPlanted)
    {   // the instrument's own control: a pipeline state the device must refuse, which the count must hold
        L->dbgPlanted = true;
        const BYTE junk[16] = { 'n', 'o', 't', ' ', 'a', ' ', 's', 'h', 'a', 'd', 'e', 'r' };
        D3D12_COMPUTE_PIPELINE_STATE_DESC d = {};
        d.CS.pShaderBytecode = junk; d.CS.BytecodeLength = sizeof(junk);
        ID3D12PipelineState* ps = nullptr;
        const HRESULT hr = h.dev->CreateComputePipelineState(&d, __uuidof(ID3D12PipelineState), (void**)&ps);
        if (ps) ps->Release();
        LogRecord("d3d12 debug: control: a compute pipeline of %u junk bytes answered 0x%08X: the next count must hold it", (unsigned)sizeof(junk), (unsigned)hr);
    }
    const char* const kSev[5] = { "corruption", "error", "warning", "info", "message" };
    const UINT64 n = iq->GetNumStoredMessages();
    UINT64 sev[5] = {}, read = 0;
    for (UINT64 i = 0; i < n; i++)
    {
        SIZE_T len = 0;
        iq->GetMessage(i, nullptr, &len);
        D3D12_MESSAGE* const m = len ? (D3D12_MESSAGE*)HeapAlloc(GetProcessHeap(), 0, len) : nullptr;
        if (m && SUCCEEDED(iq->GetMessage(i, m, &len)))
        {
            const int sv = (int)m->Severity >= 0 && (int)m->Severity < 5 ? (int)m->Severity : 4;
            sev[sv]++; read++;
            if (L->dbgSaid < 20)
            {
                L->dbgSaid++;
                LogRecord("d3d12 debug: #%d %s id %d: %.900s", L->dbgSaid, kSev[sv], (int)m->ID, m->pDescription ? m->pDescription : "");
            }
        }
        if (m) HeapFree(GetProcessHeap(), 0, m);
    }
    const UINT64 lostAll = iq->GetNumMessagesDiscardedByMessageCountLimit();
    const UINT64 lost = lostAll >= L->dbgLostSeen ? lostAll - L->dbgLostSeen : 0;
    L->dbgLostSeen = lostAll;
    iq->ClearStoredMessages();
    iq->Release();
    if (!always && !read && !lost && n == read) return;
    char more[96] = "";
    if (lost || n != read) _snprintf_s(more, sizeof(more), _TRUNCATE, "; %llu not read, %llu dropped by the queue's limit", (unsigned long long)(n - read), (unsigned long long)lost);
    LogRecord("d3d12 debug: %s: %llu messages (%llu corruption, %llu error, %llu warning, %llu info, %llu message)%s", where, (unsigned long long)read,
              (unsigned long long)sev[0], (unsigned long long)sev[1], (unsigned long long)sev[2], (unsigned long long)sev[3], (unsigned long long)sev[4], more);
}

// ---- Asked for ----
// From main, once, before the first device (the debug layer) and after the Agility opt-in (with that runtime taken the layer stays
// off, since its library would have to be that runtime's). With none of its arguments nothing is made or said.
__declspec(noinline) static void FgLibAsk(int fg, const char* path, const char* sha, int stock, int id, int dbg, bool agility)
{
    const bool hasPath = path && path[0], hasSha = sha && sha[0];
    if (!hasPath && !hasSha && !stock && !id && !dbg) return;
    if (fg != 1) { LogRecord("frame generation: --fglib, --fglib_sha, --fglib_stock, --fgid and --d3d12_debug need --fg=1: none of them is on"); return; }
    FgLib* const L = FgHostMake() ? (FgLib*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgLib)) : nullptr;
    if (!L)
    {   // NEVER A FALLBACK: without this state FgLoad would ask AMD's loader, which is not what was asked for
        LogRecord("frame generation: no memory for the development load's state (%lu bytes): this helper ends", (unsigned long)sizeof(FgLib));
        TerminateProcess(GetCurrentProcess(), 2);
        return;
    }
    g_fg->lib = L;
    L->ident = id != 0 || hasPath || hasSha;
    L->dbg = dbg;
    if (hasSha)
    {
        int n = 0;
        for (; n < 64; n++)
        {
            const char c = sha[n];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) break;
            L->want[n / 2] = (BYTE)((L->want[n / 2] << 4) | v);
        }
        L->haveWant = n == 64 && !sha[64];
        if (!L->haveWant) { L->refused = true; LogRecord("frame generation: fglib_sha: not 64 hex digits: off"); }
    }
    if (hasPath)
    {
                // Two forms: a bare file name (no slash, backslash or colon) is the file beside this helper, loaded by the full path made here, so
        // Windows' search order plays no part; anything else must be a full path. A name beginning with AMD's prefix is refused unless
        // --fglib_stock=1.
        bool bare = true;
        for (const char* p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') bare = false;
        char beside[MAX_PATH] = "";
        const bool fits = !bare || _snprintf_s(beside, MAX_PATH, _TRUNCATE, "%s%s", g_exeDir, path) > 0;   // g_exeDir ends with its backslash
        const char* const src = bare ? beside : path;
        const bool full = fits && ((src[0] && src[1] == ':' && (src[2] == '\\' || src[2] == '/')) || (src[0] == '\\' && src[1] == '\\'));
        const DWORD n = full ? GetFullPathNameA(src, MAX_PATH, L->path, nullptr) : 0;
        size_t dir = 0, name = 0;                       // the helper's folder's length; where the file's name starts in the full path
        while (g_exeDir[dir]) dir++;
        for (size_t i = 0; n && n < MAX_PATH && L->path[i]; i++) if (L->path[i] == '\\') name = i + 1;
        const char* why = nullptr;
        if (!n || n >= MAX_PATH) why = bare ? "not a file beside the helper" : "not a full path";
        else if (bare && (!FglSameStart(L->path, g_exeDir, dir) || name != dir)) why = "not a file beside the helper";
        else if (!stock && FglSameStart(L->path + name, "amd_fidelityfx", 14)) why = "an AMD name";
        if (why)
        {
            L->path[0] = '?'; L->path[1] = 0;               // not empty: FgLoad must come to the refusal, never to AMD's loader
            if (!L->refused) { L->refused = true; LogRecord("frame generation: fglib %s: %s: off", path, why); }
        }
    }
    if (dbg) FglDebugOn(L, agility);
}

// ---- The development load, from FgLoad ----
// Returns -1 when --fglib was not given (AMD's loader), 0 refused (said once; frame generation off for the process), 1 loaded:
// g_fg->dll and g_fg->fn are that module's.
__declspec(noinline) static int FgLibLoad()
{
    FgLib* const L = g_fg->lib;
    if (!L) return -1;
    if (L->refused) return 0;
    if (!L->path[0]) return -1;
    BYTE sha[32]; UINT64 size = 0; HANDLE held = nullptr; DWORD err = 0; char hex[65], want[65];
    if (!FglHashFile(L->path, true, &size, sha, &held, &err))
    {
        L->refused = true;
        LogRecord("frame generation: fglib %s: not read (%lu): off", L->path, (unsigned long)err);
        return 0;
    }
    FglHex(sha, hex);
    if (L->haveWant && !FglSameSha(sha, L->want))
    {
        CloseHandle(held); L->refused = true;
        LogRecord("frame generation: fglib %s, %llu bytes, sha256 %s: not the expected sha256 %s: off", L->path, (unsigned long long)size, hex, FglHex(L->want, want));
        return 0;
    }
    const HMODULE m = LoadLibraryA(L->path);
    err = m ? 0 : GetLastError();
    CloseHandle(held);                              // held to here: nobody wrote the file between its hash and its load
    if (!m)
    {
        L->refused = true;
        LogRecord("frame generation: fglib %s, %llu bytes, sha256 %s: would not load (%lu): off", L->path, (unsigned long long)size, hex, (unsigned long)err);
        return 0;
    }
    char mod[MAX_PATH] = "";
    if (!GetModuleFileNameA(m, mod, MAX_PATH)) mod[0] = 0;
    ffxFunctions fn = {};
    ffxLoadFunctions(&fn, m);
    const bool entries = fn.CreateContext && fn.DestroyContext && fn.Dispatch && fn.Query && fn.Configure;
    if (!entries || !FglSameStart(mod, L->path, MAX_PATH))
    {
        FreeLibrary(m); L->refused = true;
        if (!entries) LogRecord("frame generation: fglib %s, %llu bytes, sha256 %s: lacks an ffx-api entry point: off", L->path, (unsigned long long)size, hex);
        else LogRecord("frame generation: fglib %s, %llu bytes, sha256 %s: the module mapped is %s: off", L->path, (unsigned long long)size, hex, mod);
        return 0;
    }
    g_fg->dll = m; g_fg->fn = fn;
    L->direct = true; L->modKnown = true; L->saidFrames = false;
    memcpy(L->modPath, mod, MAX_PATH); L->modSize = size; memcpy(L->modSha, sha, 32);
    LogRecord("frame generation: fglib %s, %llu bytes, sha256 %s: loaded direct", mod, (unsigned long long)size, hex);
    return 1;
}

// ---- At the probe ----
// From FgProbe, twice: at its head (taken false: the device's line and the debug layer's first count) and behind a provider taken
// (taken true: the module's identity where AMD's loader chose it).
__declspec(noinline) static void FgLibAtProbe(bool taken)
{
    FgLib* const L = g_fg->lib;
    if (!L) return;
    if (!taken)
    {
        // WHICH VARIANTS AMD'S CODE TAKES ON THIS DEVICE, asked as AMD's backend asks (ffx_dx12.cpp, GetDeviceCapabilitiesDX12;
        // ffx_frameinterpolation.cpp and ffx_opticalflow.cpp, createPipelineStates): the wave64 containers only with shader
        // model 6.6 and 32 to 64 lanes; the 16-bit flag from the two 16-bit answers together (no container follows it)
        D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_6 };
        const bool smOk = SUCCEEDED(h.dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)));
        D3D12_FEATURE_DATA_D3D12_OPTIONS o0 = {}; D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = {}; D3D12_FEATURE_DATA_D3D12_OPTIONS4 o4 = {};
        h.dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof(o0));
        h.dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1));
        h.dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &o4, sizeof(o4));
        const int top = smOk ? (int)sm.HighestShaderModel : 0x51;
        const bool min16 = (o0.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT) != 0;
        const bool wave64 = top >= 0x66 && o1.WaveLaneCountMin == 32 && o1.WaveLaneCountMax == 64;
        LogRecord("frame generation: device: shader model %d.%d of 6.6 asked, wave lanes %u to %u, 16-bit min precision %s, native 16-bit ops %s: AMD's %s, 16-bit flag %s",
                  top >> 4, top & 0xF, o1.WaveLaneCountMin, o1.WaveLaneCountMax, min16 ? "yes" : "no", o4.Native16BitShaderOpsSupported ? "yes" : "no",
                  wave64 ? "WAVE64 variants (cs_6_6)" : "base variants (cs_6_2)", (min16 && o4.Native16BitShaderOpsSupported) ? "set" : "clear");
        FglDebugCount(L, "the probe", true);        // everything before it: the device, the upscaler's start
        return;
    }
    if (!L->ident || L->direct) return;             // a direct load said its own line, before it loaded
    char hex[65], want[65];
    L->modKnown = false; L->saidFrames = false;
    const HMODULE m = GetModuleHandleA(FG_PROVIDER_DLL);
    DWORD err = 0;
    if (!m || !GetModuleFileNameA(m, L->modPath, MAX_PATH))
        LogRecord("frame generation: fgid: no module named " FG_PROVIDER_DLL " is mapped: which module answered is NOT known");
    else if (!FglHashFile(L->modPath, false, &L->modSize, L->modSha, nullptr, &err))
        LogRecord("frame generation: fgid: %s: not read (%lu): its hash is NOT known", L->modPath, (unsigned long)err);
    else
    {
        L->modKnown = true;
        LogRecord("frame generation: fgid: %s, %llu bytes, sha256 %s: mapped by AMD's loader", L->modPath, (unsigned long long)L->modSize, FglHex(L->modSha, hex));
    }
    if (L->haveWant && (!L->modKnown || !FglSameSha(L->modSha, L->want)))
    {   // an expected hash that is not met refuses here too: the provider is given back, and no later load is tried
        L->refused = true; g_fg->provider = 0; g_fg->providerName[0] = 0;
        LogRecord("frame generation: fgid: not the expected sha256 %s: off", FglHex(L->want, want));
    }
}

// ---- Behind a set's create and warm-up ----
// From FgBuild, whatever became of them: the debug layer's count for that stretch, the set's line, and once a load the line naming
// the module that generates the frames.
__declspec(noinline) static void FgLibAtCreate(int status, UINT64 ctxBytes)
{
    FgLib* const L = g_fg->lib;
    if (!L) return;
    FglDebugCount(L, "create and warm-up", true);
    if (!L->ident) return;
    if (status) { LogRecord("frame generation: fgid: set refused, status %d: context %llu bytes", status, (unsigned long long)ctxBytes); return; }
    LogRecord("frame generation: fgid: set %ux%u format %u, guides %ux%u: context %llu bytes", g_fg->dispW, g_fg->dispH, (unsigned)g_fg->fmt, h.width, h.height, (unsigned long long)ctxBytes);
    if (L->saidFrames) return;
    L->saidFrames = true;
    char hex[65];
    if (L->modKnown)
        LogRecord("frame generation: frames by %s, %llu bytes, sha256 %s, %s, provider \"%s\" id 0x%llX", L->modPath, (unsigned long long)L->modSize, FglHex(L->modSha, hex),
                  L->direct ? "loaded direct" : "mapped by AMD's loader", g_fg->providerName, (unsigned long long)g_fg->provider);
    else
        LogRecord("frame generation: frames by a module that is NOT known, provider \"%s\" id 0x%llX", g_fg->providerName, (unsigned long long)g_fg->provider);
}

// ---- At every teardown ----
// From FgTeardown, after the destroy and before FgHost::built is cleared: the debug layer's count and, at a set's end and at exit,
// whether a module of AMD's frame generation name is mapped. At exit FgAtExit has already signalled the two fences to the top.
__declspec(noinline) static void FgLibAtTeardown()
{
    FgLib* const L = g_fg->lib;
    if (!L) return;
    const bool atExit = g_fg->outSig == UINT64_MAX;
    FglDebugCount(L, atExit ? "exit" : g_fg->built ? "set end" : "teardown, no set", atExit || g_fg->built);
    if (!L->ident || !(atExit || g_fg->built)) return;
    char mod[MAX_PATH] = "";
    const HMODULE m = GetModuleHandleA(FG_PROVIDER_DLL);
    if (m && !GetModuleFileNameA(m, mod, MAX_PATH)) mod[0] = 0;
    LogRecord("frame generation: fgid: %s: " FG_PROVIDER_DLL " mapped: %s%s", atExit ? "exit" : "set end", m ? "yes, " : "no", mod);
}

// ---- AMD's own checking (--fgcheck=1) ----
// Every context is created with AMD's debug checking, and its message hook is installed at every load (FgLoad calls
// FgCheckAfterLoad; the own arm hands the same hook to the mod's interface). The hook is the library's one global callback, so it
// finds its count through g_fg. Messages are counted, the first FG_CHECK_MSGS logged.
enum { FG_CHECK_MSGS = 5 };          // AMD's messages that are logged; every one is counted
static void FgCheckMessage(uint32_t type, const wchar_t* message)
{
    if (!g_fg || !g_fg->check) return;
    g_fg->checkMsgs++;
    if (g_fg->checkMsgs > FG_CHECK_MSGS || !message) return;
    char buf[600];
    const int n = WideCharToMultiByte(CP_UTF8, 0, message, -1, buf, (int)sizeof(buf) - 1, nullptr, nullptr);
    buf[(n > 0 && n < (int)sizeof(buf)) ? n - 1 : 0] = 0;
    if (n <= 0) strcpy_s(buf, sizeof(buf), "(a message longer than this line holds)");
    LogRecord("frame generation check: AMD message %llu (type %u): %s", (unsigned long long)g_fg->checkMsgs, type, buf);
}
__declspec(noinline) static void FgCheckAfterLoad()
{
    if (!g_fg->check) return;
    ffxConfigureDescGlobalDebug gd = {};
    gd.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
    gd.effectId    = FFX_API_EFFECT_ID_FRAMEGENERATION;
    gd.fpMessage   = FgCheckMessage;
    gd.debugLevel  = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS;
    DWORD raised = 0;
    const ffxReturnCode_t rc = SafeFgConfigure(nullptr, &gd.header, &raised);
    LogRecord("frame generation check: AMD's message hook: %u (%s)%s", (unsigned)rc, FfxName(rc), raised ? ": IT RAISED, and frame generation is off for this process" : "");
}
