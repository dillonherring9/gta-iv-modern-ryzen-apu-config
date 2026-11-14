// Intel XeSS Super Resolution, the third upscaler. Everything of it lives here; dlssiv-helper.cpp has only the call sites
// (InitXess, XessCaps, CreateXessContext, EvaluateXess, ReleaseXessContext, ReleaseXess).
// libxess.dll is loaded by full path beside the helper, its entry points by GetProcAddress: no import library, so a missing file is
// a logged refusal. Only Intel's public header is compiled in; Intel's licence forbids alteration "including during execution".
// The refusals the client tells apart (FEED_CAP_XESS_*), each with FEED_CAP_XESS_PROBED and without FEED_CAP_XESS:
//   NOFILE       DLSSIV\libxess.dll is not there.
//   NORUNTIME    LoadLibraryEx gave ERROR_MOD_NOT_FOUND: the Visual C++ runtime (14.40 or later) is missing.
//   UNSUPPORTED  xessD3D12CreateContext said XESS_RESULT_ERROR_UNSUPPORTED_DEVICE or _DRIVER.
// Any other failure sets PROBED alone and logs why. XESS_RESULT_WARNING_OLD_DRIVER is only a log line.
// Every call goes through XessSafe; a raise latches g_xessRaised (XeSS's own) and the library is never called again. Nothing here
// touches the FSR state (g_ffxDll, InitFsr, g_ffxRaised): frame generation must see a XeSS session as a DLSS one.
// InitXess makes a context and builds its pipelines without blocking while the game loads, for the usual FP16 capture (its flags
// must match xessD3D12Init's). A build with matching flags takes it; another gives it back or leaves it parked (XessPreDrop). A
// build's context is sized to the shared set, so ReleaseSet destroys it before the slots.
// Flags: INVERTED_DEPTH (reverse-Z); HIGH_RES_MV, JITTERED_MV and USE_NDC_VELOCITY clear (render-pixel vectors without jitter,
// previous minus current, scales 1, 1); LDR_INPUT_COLOR for an SDR build; ENABLE_AUTOEXPOSURE always with exposureScale 1.0, as
// DLSS and FSR run their own auto-exposure.
// A frame costs four barriers, one xessD3D12Execute and four barriers back; everything else is once a build.
#pragma once
#include "xess/xess.h"
#include "xess/xess_d3d12.h"

// ---------------------------------------------------------------------------------------------------------- the pick
// XESS PICK PURE START (tools/test-xess-pick cuts from this line to XESS PICK PURE END and holds it on a bench)
// The setting is picked by ratio, never by name. libxess 2.0.2's table at 3840x2160: Ultra Performance 3.0, Performance 2.3,
// Balanced 2.0, Quality 1.7, Ultra Quality 1.5, Ultra Quality Plus 1.3, Native Anti-Aliasing 1.0, each range from its optimal input
// to the output size. The mod's render sizes use DLSS's ratios (DLSS-IV.cpp, kRatio), so the mod's "Quality" (1.5) is XeSS's
// "Ultra Quality". The distance is the sum of both axes' differences between the output-to-render ratio and a setting's
// output-to-optimal ratio. Among settings whose range holds the render size the nearest wins (Intel refuses an input outside the
// range); when none holds it, the nearest of all, with inRange false. A row whose query failed or answered a zero size is not a
// candidate. Equal distances: the earlier row.
// One pixel under a setting's least is held: Intel rounds its optimal size up, the mod to the nearest pixel, so the mod's ratio can
// land one pixel under (2293x960 for 3440x1440 at 1.5 runs correctly). Two or more pixels are not held.
struct XessPickRow { int setting; const char* name; unsigned optW, optH, minW, minH, maxW, maxH; bool ok; };
struct XessPick { int index; bool inRange; double ratioX, ratioY, optRatioX, optRatioY; };
static XessPick PickXessQuality(unsigned w, unsigned hh, unsigned outW, unsigned outH, const XessPickRow* rows, int n)
{
    XessPick p = { -1, false, 0.0, 0.0, 0.0, 0.0 };
    if (!w || !hh || !outW || !outH || !rows) return p;
    p.ratioX = (double)outW / (double)w; p.ratioY = (double)outH / (double)hh;
    int held = -1, nearest = -1;
    double heldD = 1e300, nearD = 1e300;
    for (int i = 0; i < n; i++)
    {
        const XessPickRow& r = rows[i];
        if (!r.ok || !r.optW || !r.optH) continue;
        double dx = p.ratioX - (double)outW / (double)r.optW, dy = p.ratioY - (double)outH / (double)r.optH;
        if (dx < 0.0) dx = -dx;
        if (dy < 0.0) dy = -dy;
        const double d = dx + dy;
        if (d < nearD) { nearD = d; nearest = i; }
        const bool holds = w + 1 >= r.minW && w <= r.maxW && hh + 1 >= r.minH && hh <= r.maxH;
        if (holds && d < heldD) { heldD = d; held = i; }
    }
    p.index = held >= 0 ? held : nearest;
    p.inRange = held >= 0;
    if (p.index >= 0) { p.optRatioX = (double)outW / (double)rows[p.index].optW; p.optRatioY = (double)outH / (double)rows[p.index].optH; }
    return p;
}
// XESS PICK PURE END

// ---------------------------------------------------------------------------------------------------------- the state
enum { XESS_WHY_NONE = 0, XESS_WHY_NOFILE, XESS_WHY_NORUNTIME, XESS_WHY_UNSUPPORTED, XESS_WHY_OTHER, XESS_WHY_RAISED };
static HMODULE               g_xessDll      = nullptr;
static bool                  g_xessTried    = false;      // InitXess was asked on this device: FEED_CAP_XESS_PROBED
static bool                  g_xessOk       = false;      // a context was created on this device: FEED_CAP_XESS
static int                   g_xessWhy      = XESS_WHY_NONE;
static bool                  g_xessRaised   = false;      // Intel's library raised: never called again (see the head)
static xess_context_handle_t g_xessCtx      = nullptr;    // the build's context, sized to the shared set
static xess_context_handle_t g_xessPre      = nullptr;    // InitXess's context, its pipelines building for g_xessPreFlags
static uint32_t              g_xessPreFlags = 0;
static bool                  g_xessResetNext = false;     // the first real frame after the warm-up resets the history
static xess_version_t        g_xessVer      = {};

#define XESS_FN(n) static decltype(&n) p_##n = nullptr
XESS_FN(xessGetVersion); XESS_FN(xessD3D12CreateContext); XESS_FN(xessD3D12BuildPipelines); XESS_FN(xessD3D12Init);
XESS_FN(xessD3D12Execute); XESS_FN(xessDestroyContext); XESS_FN(xessGetOptimalInputResolution); XESS_FN(xessGetProperties);
XESS_FN(xessIsOptimalDriver); XESS_FN(xessSetVelocityScale); XESS_FN(xessSetJitterScale); XESS_FN(xessSetLoggingCallback);
XESS_FN(xessGetPipelineBuildStatus);
#undef XESS_FN

static const char* XessName(xess_result_t r)
{
    switch (r)
    {
    case XESS_RESULT_SUCCESS:                     return "SUCCESS";
    case XESS_RESULT_WARNING_NONEXISTING_FOLDER:  return "WARNING_NONEXISTING_FOLDER";
    case XESS_RESULT_WARNING_OLD_DRIVER:          return "WARNING_OLD_DRIVER";
    case XESS_RESULT_ERROR_UNSUPPORTED_DEVICE:    return "ERROR_UNSUPPORTED_DEVICE";
    case XESS_RESULT_ERROR_UNSUPPORTED_DRIVER:    return "ERROR_UNSUPPORTED_DRIVER";
    case XESS_RESULT_ERROR_UNINITIALIZED:         return "ERROR_UNINITIALIZED";
    case XESS_RESULT_ERROR_INVALID_ARGUMENT:      return "ERROR_INVALID_ARGUMENT";
    case XESS_RESULT_ERROR_DEVICE_OUT_OF_MEMORY:  return "ERROR_DEVICE_OUT_OF_MEMORY";
    case XESS_RESULT_ERROR_DEVICE:                return "ERROR_DEVICE";
    case XESS_RESULT_ERROR_NOT_IMPLEMENTED:       return "ERROR_NOT_IMPLEMENTED";
    case XESS_RESULT_ERROR_INVALID_CONTEXT:       return "ERROR_INVALID_CONTEXT";
    case XESS_RESULT_ERROR_OPERATION_IN_PROGRESS: return "ERROR_OPERATION_IN_PROGRESS";
    case XESS_RESULT_ERROR_UNSUPPORTED:           return "ERROR_UNSUPPORTED";
    case XESS_RESULT_ERROR_CANT_LOAD_LIBRARY:     return "ERROR_CANT_LOAD_LIBRARY";
    case XESS_RESULT_ERROR_WRONG_CALL_ORDER:      return "ERROR_WRONG_CALL_ORDER";
    default:                                      return "ERROR_UNKNOWN";
    }
}

// THE REASON, in the words of the build's refusal line and the automatic choice's line.
static const char* XessWhyText()
{
    switch (g_xessWhy)
    {
    case XESS_WHY_NOFILE:      return "Intel's " DLSSIV_HOST_DIR "\\libxess.dll is missing";
    case XESS_WHY_NORUNTIME:   return "libxess.dll would not load: it needs the Microsoft Visual C++ runtime 14.40 or later";
    case XESS_WHY_UNSUPPORTED: return "Intel's library says this graphics card or driver cannot run XeSS";
    case XESS_WHY_RAISED:      return "Intel's library raised an exception and is not called again";
    case XESS_WHY_OTHER:       return "libxess.dll could not be used (the XeSS line above says why)";
    default:                   return "XeSS was not set up";
    }
}

// ONE __try FOR EVERY CALL INTO INTEL'S CODE. No object with a destructor may live in this frame (C2712), so the arguments
// travel in a plain struct. After a raise nothing is called, the destroy included, and the refusal is said once.
enum { XOP_VERSION, XOP_CREATE, XOP_DRIVER, XOP_LOGCB, XOP_BUILD, XOP_OPTIMAL, XOP_INIT, XOP_SCALES, XOP_PROPS, XOP_EXECUTE,
       XOP_DESTROY, XOP_BUILDSTATUS };
struct XessArgs
{
    xess_context_handle_t ctx; xess_context_handle_t* outCtx; uint32_t flags; xess_quality_settings_t q; const xess_2d_t* res;
    xess_2d_t* opt; xess_2d_t* mn; xess_2d_t* mx; const xess_d3d12_init_params_t* ip; const xess_d3d12_execute_params_t* ep;
    ID3D12GraphicsCommandList* list; xess_properties_t* props; xess_version_t* ver; xess_app_log_callback_t cb;
};
static xess_result_t XessSafe(int op, XessArgs* a, DWORD* code)
{
    *code = 0;
    if (g_xessRaised)
    {
        static bool said = false;
        if (!said) { said = true; LogRecord("XeSS raised an exception earlier in this process: not called again"); }
        return XESS_RESULT_ERROR_UNKNOWN;
    }
    __try
    {
        switch (op)
        {
        case XOP_VERSION:     return p_xessGetVersion(a->ver);
        case XOP_CREATE:      return p_xessD3D12CreateContext(h.dev, a->outCtx);
        case XOP_DRIVER:      return p_xessIsOptimalDriver(a->ctx);
        case XOP_LOGCB:       return p_xessSetLoggingCallback(a->ctx, XESS_LOGGING_LEVEL_WARNING, a->cb);
        case XOP_BUILD:       return p_xessD3D12BuildPipelines(a->ctx, nullptr, false, a->flags);
        case XOP_OPTIMAL:     return p_xessGetOptimalInputResolution(a->ctx, a->res, a->q, a->opt, a->mn, a->mx);
        case XOP_INIT:        return p_xessD3D12Init(a->ctx, a->ip);
        case XOP_SCALES:      { const xess_result_t r = p_xessSetVelocityScale(a->ctx, 1.0f, 1.0f);
                                return r != XESS_RESULT_SUCCESS ? r : p_xessSetJitterScale(a->ctx, 1.0f, 1.0f); }
        case XOP_PROPS:       return p_xessGetProperties(a->ctx, a->res, a->props);
        case XOP_EXECUTE:     return p_xessD3D12Execute(a->ctx, a->list, a->ep);
        case XOP_DESTROY:     return p_xessDestroyContext(a->ctx);
        case XOP_BUILDSTATUS: return p_xessGetPipelineBuildStatus(a->ctx);
        default:              return XESS_RESULT_ERROR_INVALID_ARGUMENT;
        }
    }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_xessRaised = true; return XESS_RESULT_ERROR_UNKNOWN; }
}

// Intel's messages, at log>=1 only (the callback is unset at log=0): an error at log=1, five at most, a warning at log=2. Intel may
// call from its own thread (the pipeline build), so the count is interlocked.
static void XessMessage(const char* message, xess_logging_level_t level)
{
    if (!message) return;
    static volatile LONG errors = 0;
    if (level >= XESS_LOGGING_LEVEL_ERROR)
    {
        const LONG n = InterlockedIncrement(&errors);
        if (n <= 5) LogRecord("XeSS ERROR: %s%s", message, n == 5 ? "   (the fifth; no more logged)" : "");
    }
    else LogHealth("XeSS warning: %s", message);
}

// A context given back. Never after a raise (XessSafe refuses, and says so once); a destroy that raises drops the handle.
static void XessDestroy(xess_context_handle_t* ctx, const char* what)
{
    if (!*ctx) return;
    if (!g_xessRaised)
    {
        XessArgs a = {}; a.ctx = *ctx; DWORD code = 0;
        const xess_result_t r = XessSafe(XOP_DESTROY, &a, &code);
        if (code) LogRecord("XeSS xessDestroyContext (%s) RAISED exception 0x%08X%s: the handle is dropped", what, (unsigned)code, g_raiseWhere);
        else if (r != XESS_RESULT_SUCCESS) LogRecord("XeSS xessDestroyContext (%s) said %s", what, XessName(r));
    }
    *ctx = nullptr;
}

// The start-up context given back. Its pipelines may still be building on Intel's threads, and the header says nothing of a destroy
// during a build, so with wait the status is asked for up to 10 s first (the device's release). At a build (wait false) it is asked
// once: a finished build is given back at once, one still running is left parked and given back later (ReleaseXess, or a later
// build that finds it done or takes it). In the game every build matches the start-up flags, so this path runs only in SDR test
// sessions.
static void XessPreDrop(const char* why, bool wait)
{
    if (!g_xessPre) return;
    if (!g_xessRaised)
    {
        const ULONGLONG t0 = GetTickCount64();
        XessArgs a = {}; a.ctx = g_xessPre; DWORD code = 0;
        xess_result_t st = XessSafe(XOP_BUILDSTATUS, &a, &code);
        while (wait && st == XESS_RESULT_ERROR_OPERATION_IN_PROGRESS && !code && GetTickCount64() - t0 < 10000) { Sleep(5); st = XessSafe(XOP_BUILDSTATUS, &a, &code); }
        if (code) { LogRecord("XeSS: xessGetPipelineBuildStatus RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); g_xessPre = nullptr; return; }
        if (!wait && st == XESS_RESULT_ERROR_OPERATION_IN_PROGRESS)
        {
            LogHealth("XeSS: the start-up context is still building its pipelines (%s): left parked, not waited for; given back at the device's release", why);
            return;
        }
        LogHealth("XeSS: the start-up context is given back (%s), after %llu ms", why, (unsigned long long)(GetTickCount64() - t0));
    }
    XessDestroy(&g_xessPre, "the start-up one");
}

// ---------------------------------------------------------------------------------------------------------- the probe
// Loads XeSS, resolves it, creates a context and starts the pipelines on this device; a rebind asks again on the game's adapter.
// Returns true when XeSS can run here (FEED_CAP_XESS).
static bool InitXess()
{
    g_xessTried = true; g_xessOk = false; g_xessWhy = XESS_WHY_NONE;
    if (g_xessRaised) { g_xessWhy = XESS_WHY_RAISED; return false; }
    wchar_t path[MAX_PATH];   // ends with its backslash. No wide printf: it pulls in the CRT's whole wide formatter
    if (wcscpy_s(path, MAX_PATH, g_exeDirW) != 0 || wcscat_s(path, MAX_PATH, L"libxess.dll") != 0)
    { g_xessWhy = XESS_WHY_OTHER; LogRecord("XeSS: the path of libxess.dll beside this helper is too long: XeSS cannot run"); return false; }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        g_xessWhy = XESS_WHY_NOFILE;
        LogRecord("XeSS: %ls is not there: XeSS cannot run", path);
        return false;
    }
    if (!g_xessDll)
    {
        // LOAD_WITH_ALTERED_SEARCH_PATH: the file's own imports are looked for beside it first, then where Windows looks.
        g_xessDll = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_xessDll)
        {
            const DWORD e = GetLastError();
            g_xessWhy = e == ERROR_MOD_NOT_FOUND ? XESS_WHY_NORUNTIME : XESS_WHY_OTHER;
            LogRecord("XeSS: %ls would not load (error %lu%s): XeSS cannot run", path, e,
                e == ERROR_MOD_NOT_FOUND ? ", a module it needs is missing: the Microsoft Visual C++ runtime 14.40 or later" : "");
            return false;
        }
        bool all = true;
#define XESS_GET(n) do { p_##n = (decltype(p_##n))(void*)GetProcAddress(g_xessDll, #n); if (!p_##n) { all = false; LogRecord("XeSS: libxess.dll has no entry point %s", #n); } } while (0)
        XESS_GET(xessGetVersion); XESS_GET(xessD3D12CreateContext); XESS_GET(xessD3D12BuildPipelines); XESS_GET(xessD3D12Init);
        XESS_GET(xessD3D12Execute); XESS_GET(xessDestroyContext); XESS_GET(xessGetOptimalInputResolution); XESS_GET(xessGetProperties);
        XESS_GET(xessIsOptimalDriver); XESS_GET(xessSetVelocityScale); XESS_GET(xessSetJitterScale); XESS_GET(xessSetLoggingCallback);
        XESS_GET(xessGetPipelineBuildStatus);
#undef XESS_GET
        if (!all)
        {
            g_xessWhy = XESS_WHY_OTHER;
            LogRecord("XeSS: %ls is not Intel's XeSS library (entry points missing): XeSS cannot run", path);
            FreeLibrary(g_xessDll); g_xessDll = nullptr;
            return false;
        }
    }
    XessArgs a = {}; DWORD code = 0;
    a.ver = &g_xessVer;
    XessSafe(XOP_VERSION, &a, &code);
    if (code) { g_xessWhy = XESS_WHY_RAISED; LogRecord("XeSS: xessGetVersion RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }

    xess_context_handle_t ctx = nullptr;
    a = {}; a.outCtx = &ctx;
    const xess_result_t rc = XessSafe(XOP_CREATE, &a, &code);
    if (code) { g_xessWhy = XESS_WHY_RAISED; LogRecord("XeSS: xessD3D12CreateContext RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
    if (rc != XESS_RESULT_SUCCESS || !ctx)
    {
        g_xessWhy = (rc == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE || rc == XESS_RESULT_ERROR_UNSUPPORTED_DRIVER) ? XESS_WHY_UNSUPPORTED : XESS_WHY_OTHER;
        LogRecord("XeSS %u.%u.%u: xessD3D12CreateContext said %s: XeSS cannot run on this device", g_xessVer.major, g_xessVer.minor, g_xessVer.patch, XessName(rc));
        if (ctx) XessDestroy(&ctx, "a refused create");
        return false;
    }
    g_xessOk = true;
    a = {}; a.ctx = ctx;
    const xess_result_t od = XessSafe(XOP_DRIVER, &a, &code);
    if (!code && g_logLevel >= 1) { a.cb = XessMessage; XessSafe(XOP_LOGCB, &a, &code); }
    if (code) { g_xessWhy = XESS_WHY_RAISED; g_xessOk = false; LogRecord("XeSS: Intel's library RAISED exception 0x%08X%s after its create: not called again this session", (unsigned)code, g_raiseWhere); return false; }
        // Pipelines built without blocking, for the FP16 capture's flags. A refusal costs only the head start: the context is given
    // back and the build makes its own.
    const uint32_t pre = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
    a = {}; a.ctx = ctx; a.flags = pre;
    const xess_result_t rb = XessSafe(XOP_BUILD, &a, &code);
    if (code) { g_xessWhy = XESS_WHY_RAISED; g_xessOk = false; LogRecord("XeSS: xessD3D12BuildPipelines RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
    if (rb == XESS_RESULT_SUCCESS) { g_xessPre = ctx; g_xessPreFlags = pre; }
    else XessDestroy(&ctx, "pipelines refused");
    char full[MAX_PATH] = "";
    if (g_logLevel >= 1) GetModuleFileNameA(g_xessDll, full, MAX_PATH);
    LogRecord("XeSS %u.%u.%u loaded from %s; a context created on this device; the driver %s; pipelines %s",
        g_xessVer.major, g_xessVer.minor, g_xessVer.patch, full[0] ? full : "libxess.dll",
        od == XESS_RESULT_SUCCESS ? "is the one it prefers" : od == XESS_RESULT_WARNING_OLD_DRIVER ? "is OLDER than it prefers (an update may help)" : XessName(od),
        rb == XESS_RESULT_SUCCESS ? "building in the background for the HDR capture" : XessName(rb));
    return true;
}

// The handshake's bits (feed_ipc.h); 0 when XeSS was not asked on this device.
static uint64_t XessCaps()
{
    if (!g_xessTried) return 0;
    uint64_t c = FEED_CAP_XESS_PROBED;
    if (g_xessOk) return c | FEED_CAP_XESS;
    if (g_xessWhy == XESS_WHY_NOFILE)      c |= FEED_CAP_XESS_NOFILE;
    if (g_xessWhy == XESS_WHY_NORUNTIME)   c |= FEED_CAP_XESS_NORUNTIME;
    if (g_xessWhy == XESS_WHY_UNSUPPORTED) c |= FEED_CAP_XESS_UNSUPPORTED;
    return c;
}

// ---------------------------------------------------------------------------------------------------------- the frame
// XeSS does no synchronisation of its inputs and output: inputs must be NON_PIXEL_SHADER_RESOURCE, the output UNORDERED_ACCESS.
// The slots live in COMMON and all go back to COMMON before the list closes: frame generation's prepare (fg_host.h) and the
// game's import expect COMMON.
static void XessBarriers(bool toXess)
{
    D3D12_RESOURCE_BARRIER br[4] = {};
    static const int kSlot[4] = { FEED_COLOR, FEED_DEPTH, FEED_MV, FEED_OUTPUT };
    for (int i = 0; i < 4; i++)
    {
        const D3D12_RESOURCE_STATES xs = kSlot[i] == FEED_OUTPUT ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        br[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br[i].Transition.pResource   = h.tex[kSlot[i]];
        br[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        br[i].Transition.StateBefore = toXess ? D3D12_RESOURCE_STATE_COMMON : xs;
        br[i].Transition.StateAfter  = toXess ? xs : D3D12_RESOURCE_STATE_COMMON;
    }
    h.list->ResourceBarrier(4, br);
}

static bool EvaluateXess(const FeedFrameMsg& fm)
{
    if (!g_xessCtx || !BeginCommands()) return false;
    XessBarriers(true);
    xess_d3d12_execute_params_t ep = {};
    ep.pColorTexture    = h.tex[FEED_COLOR];
    ep.pVelocityTexture = h.tex[FEED_MV];
    ep.pDepthTexture    = h.tex[FEED_DEPTH];
    ep.pOutputTexture   = h.tex[FEED_OUTPUT];
    ep.jitterOffsetX    = fm.jitter_x;          // the same jitter DLSS and FSR are handed, scale 1
    ep.jitterOffsetY    = fm.jitter_y;
    ep.exposureScale    = 1.0f;                 // ignored: auto-exposure is on
        // g_xessResetNext: the first real frame after the warm-up; cleared only by a frame that was submitted, as for FSR.
    ep.resetHistory     = (fm.reset != 0 || g_xessResetNext) ? 1u : 0u;
    ep.inputWidth       = h.width;
    ep.inputHeight      = h.height;
    XessArgs a = {}; a.ctx = g_xessCtx; a.list = h.list; a.ep = &ep;
    DWORD code = 0;
    const xess_result_t r = XessSafe(XOP_EXECUTE, &a, &code);
    if (r != XESS_RESULT_SUCCESS)
    {
        AbortCommands();
        static int said = 0;
        if (said++ < 5)
            LogRecord("XeSS execute failed: %s%s: %ux%u -> %ux%u, jitter %.3f %.3f, reset %u", code ? "RAISED an exception" : XessName(r),
                code ? g_raiseWhere : "", h.width, h.height, h.outW, h.outH, fm.jitter_x, fm.jitter_y, ep.resetHistory);
        if (code) { LogRecord("XeSS: the exception was 0x%08X: Intel's library is not called again this session", (unsigned)code); DeviceRemoved("a XeSS execute that raised"); }
        return false;
    }
    XessBarriers(false);
    const bool submitted = EndCommands() != 0;
    if (submitted) g_xessResetNext = false;
    return submitted;
}

// XeSS's first execute, inside the build (as WarmUpFsr): the client allows a build 60 s and the host watch only times frames,
// so the first execute's cost is paid here. Runs on the blank set just made and signals only the private fence.
// Returns 0 done; 1 failed cleanly, nothing submitted; 2 failed hard (a raise, the device, or a GPU timeout): the set is then
// kept alive and the context left for ReleaseSet, which drains first.
static int WarmUpXess()
{
    const ULONGLONG kBudgetMs = 55000;
    const ULONGLONG used0 = g_buildStartTick ? GetTickCount64() - g_buildStartTick : 0;
    if (used0 + 10000 > kBudgetMs)
    {
        LogRecord("XeSS: no warm-up: %llu ms of the client's 60 s used: build refused", (unsigned long long)used0);
        return 1;
    }
    FeedFrameMsg fm = {};
    fm.reset = 1; fm.jitter_x = 0.25f; fm.jitter_y = -0.25f; fm.frame_time_ms = 16.67f;
    LARGE_INTEGER f = {}, q0 = {}, q1 = {}, q2 = {};
    const bool timed = g_logLevel >= 1;
    if (timed) { QueryPerformanceFrequency(&f); QueryPerformanceCounter(&q0); }
    const bool ok = EvaluateXess(fm);
    if (timed) QueryPerformanceCounter(&q1);
    if (!ok) return (g_xessRaised || DeviceRemoved("the XeSS warm-up")) ? 2 : 1;
    const ULONGLONG used1 = g_buildStartTick ? GetTickCount64() - g_buildStartTick : 0;
    const ULONGLONG left  = used1 < kBudgetMs ? kBudgetMs - used1 : 0;
    const DWORD waitMs    = (DWORD)(left > 45000 ? 45000 : (left < 1000 ? 1000 : left));
    if (!WaitFenceValue(h.fence, h.fenceValue, waitMs))
    {
        for (int i = 0; i < FEED_SLOTS; i++) if (h.tex[i]) h.tex[i]->AddRef();
        g_setKept = true;
        LogRecord("XeSS: the warm-up was submitted and the GPU had not finished it %lu ms later: build refused, its textures kept", (unsigned long)waitMs);
        DeviceRemoved("the XeSS warm-up");
        return 2;
    }
    if (DeviceRemoved("the XeSS warm-up")) return 2;
    if (timed)
    {
        QueryPerformanceCounter(&q2);
        LogRecord("XeSS: warm-up done: %.0f ms on the CPU, then %.0f ms until the GPU finished",
            1000.0 * (double)(q1.QuadPart - q0.QuadPart) / (double)f.QuadPart, 1000.0 * (double)(q2.QuadPart - q1.QuadPart) / (double)f.QuadPart);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------------------- the build
static bool XessBuildContext(UINT w, UINT hh, UINT outW, UINT outH, bool hdr)
{
    if (!g_xessOk || g_xessRaised) return false;
    const uint32_t flags = XESS_INIT_FLAG_INVERTED_DEPTH | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE | (hdr ? 0u : (uint32_t)XESS_INIT_FLAG_LDR_INPUT_COLOR);
    XessArgs a = {}; DWORD code = 0;
        // The context: InitXess's when its pipelines were built for these flags (taken once), else a new one.
    xess_context_handle_t ctx = nullptr;
    const bool took = g_xessPre && g_xessPreFlags == flags;
    if (took) { ctx = g_xessPre; g_xessPre = nullptr; }
    else
    {
                // A session's capture keeps its format, so a start-up context built for other flags would only hold memory: give it back first.
        XessPreDrop(hdr ? "built for other flags" : "built for the FP16 capture, this build is SDR", false);
        a.outCtx = &ctx;
        const xess_result_t rc = XessSafe(XOP_CREATE, &a, &code);
        if (code) { LogRecord("XeSS: xessD3D12CreateContext RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
        if (rc != XESS_RESULT_SUCCESS || !ctx) { LogRecord("XeSS: xessD3D12CreateContext said %s: build refused", XessName(rc)); if (ctx) XessDestroy(&ctx, "a refused create"); return false; }
        if (g_logLevel >= 1)
        {
            a = {}; a.ctx = ctx; a.cb = XessMessage; XessSafe(XOP_LOGCB, &a, &code);
            if (code) { LogRecord("XeSS: xessSetLoggingCallback RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
        }
    }
        // The settings table at this output size, picked by ratio.
    static const struct { xess_quality_settings_t q; const char* name; } kSet[] = {
        { XESS_QUALITY_SETTING_AA, "Native Anti-Aliasing" }, { XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, "Ultra Quality Plus" },
        { XESS_QUALITY_SETTING_ULTRA_QUALITY, "Ultra Quality" }, { XESS_QUALITY_SETTING_QUALITY, "Quality" },
        { XESS_QUALITY_SETTING_BALANCED, "Balanced" }, { XESS_QUALITY_SETTING_PERFORMANCE, "Performance" },
        { XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, "Ultra Performance" } };
    const int kN = (int)(sizeof(kSet) / sizeof(kSet[0]));
    XessPickRow rows[kN] = {};
    const xess_2d_t out = { outW, outH };
    char ranges[512] = ""; size_t rl = 0;
    for (int i = 0; i < kN; i++)
    {
        xess_2d_t opt = {}, mn = {}, mx = {};
        a = {}; a.ctx = ctx; a.res = &out; a.q = kSet[i].q; a.opt = &opt; a.mn = &mn; a.mx = &mx;
        const xess_result_t r = XessSafe(XOP_OPTIMAL, &a, &code);
        if (code) { LogRecord("XeSS: xessGetOptimalInputResolution RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
        rows[i] = { (int)kSet[i].q, kSet[i].name, opt.x, opt.y, mn.x, mn.y, mx.x, mx.y, r == XESS_RESULT_SUCCESS };
        if (rl < sizeof(ranges) - 1)
            rl += (size_t)_snprintf_s(ranges + rl, sizeof(ranges) - rl, _TRUNCATE, "%s%s %ux%u [%ux%u..%ux%u]%s", rl ? "; " : "", kSet[i].name,
                                      opt.x, opt.y, mn.x, mn.y, mx.x, mx.y, r == XESS_RESULT_SUCCESS ? "" : " (refused)");
    }
    const XessPick pk = PickXessQuality(w, hh, outW, outH, rows, kN);
    if (pk.index < 0)
    {
        LogRecord("XeSS: no setting answered for %ux%u -> %ux%u (%s): build refused", w, hh, outW, outH, ranges);
        XessDestroy(&ctx, "no setting");
        return false;
    }
    const XessPickRow& pr = rows[pk.index];
    if (!pk.inRange)
        LogRecord("XeSS: %ux%u -> %ux%u is inside no XeSS setting's range (%s): the nearest by ratio, %s, is used", w, hh, outW, outH, ranges, pr.name);
    LogRecord("XeSS: %ux%u -> %ux%u (ratio %.3f) is %s (setting %d, its optimal %ux%u, ratio %.3f)%s", w, hh, outW, outH, pk.ratioX, pr.name,
        pr.setting, pr.optW, pr.optH, pk.optRatioX, pk.inRange ? "" : ", OUTSIDE its range");
        // Init: output size, setting and flags, then the two scales.
    xess_d3d12_init_params_t ip = {};
    ip.outputResolution = out;
    ip.qualitySetting   = (xess_quality_settings_t)pr.setting;
    ip.initFlags        = flags;
    a = {}; a.ctx = ctx; a.ip = &ip;
    LARGE_INTEGER f = {}, t0 = {}, t1 = {};
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    const xess_result_t ri = XessSafe(XOP_INIT, &a, &code);
    QueryPerformanceCounter(&t1);
    if (code) { LogRecord("XeSS: xessD3D12Init RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
    if (ri != XESS_RESULT_SUCCESS) { LogRecord("XeSS: xessD3D12Init said %s (%ux%u, %s, flags 0x%X): build refused", XessName(ri), outW, outH, pr.name, flags); XessDestroy(&ctx, "a refused init"); return false; }
    a = {}; a.ctx = ctx;
    const xess_result_t rs = XessSafe(XOP_SCALES, &a, &code);
    if (code) { LogRecord("XeSS: setting its scales RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
    if (rs != XESS_RESULT_SUCCESS) { LogRecord("XeSS: setting the velocity and jitter scales to 1 said %s: build refused", XessName(rs)); XessDestroy(&ctx, "refused scales"); return false; }
    xess_properties_t props = {};
    a = {}; a.ctx = ctx; a.res = &out; a.props = &props;
    const xess_result_t rp = XessSafe(XOP_PROPS, &a, &code);
    if (code) { LogRecord("XeSS: xessGetProperties RAISED exception 0x%08X%s: not called again this session", (unsigned)code, g_raiseWhere); return false; }
    g_xessCtx = ctx;
    const int warmed = WarmUpXess();
    if (warmed != 0)
    {
        LogRecord("XeSS: the warm-up execute FAILED%s%s: build refused", g_xessRaised ? " by raising" : (warmed == 1 ? "; nothing was submitted" : ""), g_xessRaised ? g_raiseWhere : "");
        if (warmed == 1) XessDestroy(&g_xessCtx, "a failed warm-up");
        else if (g_xessRaised) g_xessCtx = nullptr;   // never called again; left mapped
        return false;                                 // 2 without a raise: ReleaseSet gives it back after its drain
    }
    g_xessResetNext = true;
    LogRecord("XeSS context ready: %ux%u -> %ux%u, %s, %s colour, flags 0x%X, %s context, init %.0f ms; temporary memory %s%.1f MB of"
              " textures and %.1f MB of buffers, %u descriptors",
        w, hh, outW, outH, pr.name, hdr ? "HDR" : "SDR", flags, took ? "the start-up" : "a new", 1000.0 * (double)(t1.QuadPart - t0.QuadPart) / (double)f.QuadPart,
        rp == XESS_RESULT_SUCCESS ? "" : "(not answered) ", props.tempTextureHeapSize / 1048576.0, props.tempBufferHeapSize / 1048576.0, props.requiredDescriptorCount);
    return true;
}
// In automatic (upscaler=-1) a failed XeSS build falls to FSR: the build is refused (its ack names FSR, g_backend being FSR's by
// then), the start-up context is dropped without a wait, FSR is loaded as ResolveBackendAndProbe loads it, and every later build
// of the session is FSR's. The client reads the fall from that ack (DLSS-IV.cpp, IvtXessFell). upscaler=2 does not fall.
// noinline on purpose: Serve calls this out of line, which keeps its build handler and per-frame block unchanged.
static __declspec(noinline) bool CreateXessContext(UINT w, UINT hh, UINT outW, UINT outH, bool hdr)
{
    if (XessBuildContext(w, hh, outW, outH, hdr)) return true;
    if (g_backendCli >= 0 || g_backend != 2) return false;
    g_backend = 1;
    XessPreDrop("automatic falls to FSR", false);
    LogRecord("backend AUTO: XeSS failed at this build (the XeSS line above says why): this build is refused, and the next runs FSR");
    if (!g_ffxTried)
    {
        g_ffxTried = true;
        if (!InitFsr(g_exeDir)) LogRecord("FSR is selected but has no usable library: builds that need it are refused");
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------- teardown
// The build's context, released by ReleaseSet after its drain and before the slots it is sized to.
static void ReleaseXessContext()
{
    XessDestroy(&g_xessCtx, "the set's");
}

// All of XeSS on this device, from ReleaseDevice (a rebind) and the serve loop's tail. The DLL is not unloaded after a raise:
// FreeLibrary would run its detach code, and a library that raised is not called again.
static void ReleaseXess()
{
    XessDestroy(&g_xessCtx, "the set's");
    XessPreDrop("the device goes", true);
    if (g_xessDll && !g_xessRaised) FreeLibrary(g_xessDll);
    g_xessDll = nullptr;
    g_xessTried = false; g_xessOk = false; g_xessWhy = XESS_WHY_NONE; g_xessResetNext = false;
}
