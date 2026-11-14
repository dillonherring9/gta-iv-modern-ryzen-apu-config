// dlssiv-helper.exe - the 64-bit resolve host for DLSS-IV.
//
// NGX is 64-bit only and GTA IV is a 32-bit process, so the DLSS evaluate has
// to run in a helper process. This is that helper, and nothing else: a D3D12
// device, NGX or FidelityFX, the upscaler, four shared textures, two shared fences and
// one named pipe. No window, no swapchain, no ReShade, no neural consumer.
//
// It began from the DLSS5-Feeder protocol (feed_ipc.h, v7, MIT, Copyright (c)
// 2026 Jean-Laurent ROUZIES) and speaks DLSS-IV's own v16 of it, which only the DLSS-IV.asi of the same build can use
// (the handshake refuses any other build); its D3D12 and NGX sequences follow that project's host, the proven reference for this
// exact cross-process shape (shared committed resources with simultaneous
// access, D3D12 fences imported by the game as timeline semaphores, the
// allocator ring, the SEH around NGX). What it drops: everything the Feeder's
// host carries for its ReShade-hosted neural add-on.
//
// Per frame: wait the in-fence on the queue, evaluate, signal the out-fence.
// The pipe carries the frame number, the reset flag and the jitter offset.
//
// THERE IS NO SECOND CONFIG AND NO SECOND LOG. Everything arrives from the
// .asi on the command line and everything is written into the .asi's log, so
// a player has one DLSS-IV.cfg to read and one DLSS-IV.log to send.
//
// Usage: dlssiv-helper.exe <game pid> [--backend=N] [--preset=N] [--log=N]
//                                     [--fsr_version=N] [--scale=N]
//                                     [--gpu_probe=N]                                         (development builds)
//                                     [--fsr4_min_sm=N]                                       (development builds)
//                                     [--logpath=PATH] [--hide | --behind]
//   backend  -1 automatic, 0 DLSS, 1 FSR, 2 XeSS (Intel's libxess.dll beside this exe; host\xess_host.h)
//   preset   NVSDK_NGX_DLSS_Hint_Render_Preset (11 = K, 13 = M, 10 = J,
//            0 = driver default); default 11
//   fsr_version     0 the FidelityFX DLL's own pick for this GPU (default),
//                   N the newest version it offers with major number N
//   fsr4_min_sm     the shader model below which automatic does not run FSR 4,
//                   as a number: 102 is 6.6 (0x66, the default and the only
//                   value a release has), 105 is 6.9
//   (fsr4_min_sm is read by development builds only; a release helper skips it
//   like any flag it does not know. fsr_debug_view and fsr_warmup went in round
//   8's deep strip: the helper holds the values a release always had)
//   log      0 off, 1 record, 2 + health, 3 + detail. Off means this process
//            writes nothing at all.
//   logpath  the .asi's log file. Absent means nothing is written.
//   --hide and --behind are still SKIPPED rather than parsed, so an older
//   client's command line does no harm, but nothing sends them any more: the
//   client's host_window key was removed on 2026-09-09 (audit L8) because it
//   was parsed, logged, documented to players with three values, and did
//   nothing. There is no window to hide.

#include <windows.h>
#include <shellapi.h> // standalone bridge --vendor-dir from the Unicode argv
#include <share.h>     // _SH_DENYNO for the shared log
#include <d3d12.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>   // EnumAdapterByGpuPreference: the adapter pick on a hybrid laptop
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

#include "../source/transport/feed_ipc.h"
#include "pipe_reader.h"   // the serve loop's buffered pipe reader
// pipe_reader.h spells the size out (its offline test builds without feed_ipc.h); this ties the two.
static_assert(sizeof(((HostPipeReader*)0)->buf) == FEED_PIPE_BYTES, "the reader's buffer is the pipe's");
#include "../source/transport/dlssiv_layout.h"   // DLSSIV_HOST_DIR, shared with the client

// The commit that built this binary (build-host.ps1 defines it). Both halves must ship together: two build ids in one log show a
// mismatch as a mismatch.
#ifndef IVT_BUILD_ID
#define IVT_BUILD_ID "unknown-source (built " __DATE__ " " __TIME__ ")"
#endif

// ---- Logging ----

// The helper writes into the client's log, so a player has one file. Path and level arrive as --logpath= and --log=.
// Sharing is safe because both sides append whole lines: the client holds its handle with _SH_DENYNO, this side opens, appends one
// line and closes, under a named mutex. No --logpath: nothing is written. The client owns the file.
static char   g_logPath[MAX_PATH];
static int    g_logLevel = 0;
// The adapter this helper created its device on, recorded at InitDevice and checked against the game's from FeedHello. A check, not
// a choice: the device is made at spawn so NGX loads while the game loads.
static unsigned char g_luid[8] = {};
// Whether FSR was probed at all (InitFsr runs only on the FSR back end), so the capability word can tell absent from failed.
static bool          g_ffxTried = false;
static bool          g_luidValid = false;
static const char*   g_adapterHow = "";          // which tier of InitDevice chose the adapter, for the log
// The exe's folder, so the back end can be resolved and probed again after a rebind onto the game's adapter.
static char          g_exeDir[MAX_PATH];
static wchar_t       g_exeDirW[MAX_PATH];
static HANDLE g_logMutex = nullptr;
static bool   g_logOpenFailed = false;

static void LogEmitV(const char* fmt, va_list ap)
{
    if (!g_logPath[0]) return;
        // Release only a mutex that is held: a timeout took nothing, and WAIT_ABANDONED is owned now. The line is written either way.
    const DWORD w = g_logMutex ? WaitForSingleObject(g_logMutex, 200) : WAIT_FAILED;
    const bool held = w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
        // _fsopen with _SH_DENYNO, not fopen_s: fopen_s opens unshared, so every open fails while the client holds the file.
    FILE* f = _fsopen(g_logPath, "a", _SH_DENYNO);
    if (f)
    {
        fputs("  host: ", f);
        vfprintf(f, fmt, ap);
        fputc('\n', f);
        fclose(f);
    }
    else if (!g_logOpenFailed)
    {
                // Said once, to OutputDebugString: the process has no console (CREATE_NO_WINDOW), and the log is what failed.
        g_logOpenFailed = true;
        char msg[MAX_PATH + 64];
        _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                    "dlssiv-helper: cannot open %s for append\n", g_logPath);
        OutputDebugStringA(msg);
    }
    if (held) ReleaseMutex(g_logMutex);
}

static void LogRecord(_Printf_format_string_ const char* fmt, ...)
{
    if (g_logLevel < 1) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
// DLSSIV_DEV 1 is a development build, 0 a release (build-host.ps1 -Release). At 0 every LogHealth (log=2) and LogDetail (log=3)
// call compiles to nothing, arguments included; no argument has a side effect.
#ifndef DLSSIV_DEV
#define DLSSIV_DEV 1
#endif
// Frame generation's product code follows DLSSIV_DEV; build-host.ps1 -Release -Fg is the release build with it.
#ifndef DLSSIV_FG
#define DLSSIV_FG DLSSIV_DEV
#endif
#if DLSSIV_DEV && !DLSSIV_FG
#error "a development build carries frame generation's product code (the smoke test and the rig stand on it): DLSSIV_DEV=1 with DLSSIV_FG=0 is not a configuration"
#endif
// Which configuration this is, printed beside the build id.
#if DLSSIV_DEV
#define DLSSIV_FLAVOUR "development"
#else
#define DLSSIV_FLAVOUR "release"
#endif
#if DLSSIV_DEV
static void LogHealth(_Printf_format_string_ const char* fmt, ...)
{
    if (g_logLevel < 2) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
static void LogDetail(_Printf_format_string_ const char* fmt, ...)
{
    if (g_logLevel < 3) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
#else
static void LogHealth(const char*, ...) {}       // a site that picks its level through a pointer still names a function
static void LogDetail(const char*, ...) {}
#define LogHealth(...) ((void)0)
#define LogDetail(...) ((void)0)
#endif
// mode=1, the transport test, is a development build's: a release has no transport-only path.
#if DLSSIV_DEV
#define HOST_DEV(...) __VA_ARGS__
#define HOST_TRANSPORT_ONLY h.transportOnly
#else
#define HOST_DEV(...)
#define HOST_TRANSPORT_ONLY false
#endif

static const char* NgxName(NVSDK_NGX_Result r)
{
    switch (r)
    {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_Fail: return "Fail";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_ScratchBufferTooSmall: return "ScratchBufferTooSmall";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_RWFlagMissing: return "RWFlagMissing";
    case NVSDK_NGX_Result_FAIL_MissingInput: return "MissingInput";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnsupportedParameter: return "UnsupportedParameter";
    case NVSDK_NGX_Result_FAIL_Denied: return "Denied";
    case NVSDK_NGX_Result_FAIL_NotImplemented: return "NotImplemented";
    default: return "?";
    }
}

// ---- Config ----

// The DLSS model preset, from preset= in DLSS-IV.cfg via --preset=.
static int g_cfgPreset = 11;   // K
// fsr_version= from DLSS-IV.cfg: 0 the FidelityFX DLL's own pick for this GPU, N the newest offered version with major number N.
// Read at context creation; CreateFsrContext has the rule.
static int g_fsrVersion   = 0;
// --sync=, the Linux sync path (design in DlssIvTransport.h above ivtWine). 0: this helper creates the two shared fences and the ack
// carries them. 1 or 2: the game creates two timeline semaphores, FeedBuild carries them, and they are opened as fences
// (OpenGameSemaphores). The game's 'M' then says the mode: FAST, GPU-side as on Windows; or BRIDGE, where the serve loop skips the
// GPU wait on the in-fence (the frame message arrives after the game's copies retired) and answers each frame once its out-fence
// reached it.
static int  g_sync      = 0;
static int  g_scale     = 0;   // --scale=: the client's render scale in percent when set, else 0; only the optimal-size callout reads it
static bool g_bridge    = false;
static bool g_semOpened = false;
static HANDLE g_answerEvent = nullptr;   // the game's event from the mode message: set after each bridged answer to wake its relay
// Former development levers, held at the shipping value so their branches fold away. fsr_debug_view sets FSR's debug-view flag;
// fsr_warmup: -1 warms up FSR 4 and newer (and an unknown version), 0 never, 1 every context. A player's way out of a misbehaving
// FSR 4 is fsr_version=3, which is never warmed up.
static const int g_fsrDebugView = 0;
static const int g_fsrWarmup    = -1;
#if DLSSIV_DEV
static int g_gpuProbe     = 0;       // --gpu_probe=1: the serve timer armed and said at log=1
// --xess_auto=1: automatic chooses as on Intel graphics whatever the adapter, so the fall from XeSS to FSR can be tested on any card.
static int g_xessAutoAs   = 0;
#define XESS_AUTO_INTEL(v) ((v) == 0x8086 || g_xessAutoAs)
#else
#define XESS_AUTO_INTEL(v) ((v) == 0x8086)
#endif


// ---- State ----

struct Host
{
    ID3D12Device*              dev = nullptr;
    ID3D12CommandQueue*        queue = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    static const int           kFrames = 3;
    ID3D12CommandAllocator*    alloc[kFrames] = {};
    UINT64                     allocFence[kFrames] = {};
    int                        slot = 0;
    ID3D12Fence*               fence = nullptr;       // the allocator ring
    HANDLE                     fenceEvent = nullptr;
    UINT64                     fenceValue = 0;

    ID3D12Fence*               fenceIn = nullptr;     // the game signals, the helper waits
    ID3D12Fence*               fenceOut = nullptr;    // the helper signals, the game waits

    bool                       ngxInited = false;
    NVSDK_NGX_Parameter*       params = nullptr;
    NVSDK_NGX_Handle*          feature = nullptr;

    ID3D12Resource*            tex[FEED_SLOTS] = {};
    UINT                       width = 0, height = 0, outW = 0, outH = 0;
    DXGI_FORMAT                colorFmt = DXGI_FORMAT_UNKNOWN, outputFmt = DXGI_FORMAT_UNKNOWN;
    bool                       transportOnly = false;
    bool                       ngxOk         = false;   // NGX initialised; false is survivable
    bool                       ngxTried      = false;   // so the auto path does not init it twice
    bool                       ngxDriverOld  = false;   // NGX blamed the driver (OutOfDate, or NeedsUpdatedDriver=1): FEED_CAP_DLSS_DRIVER
    bool                       ngxNoFile     = false;   // NGX found no nvngx_dlss.dll beside this exe (FeatureNotFound): FEED_CAP_DLSS_NOFILE
    UINT                       vendorId      = 0;       // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
    float                      mvScaleX = 1.0f, mvScaleY = 1.0f;
    int                        srQuality = -1;
    const char*                srQualityName = "";
};
static Host h;
static bool g_deviceRemoved = false;
// ReleaseClient(n) signals from the queue on every path out of the frame loop, failure included, so a failed frame costs a stale
// frame rather than a deadlock. A fault-injection run proved it; if the release ordering changes, build such a harness and rerun it.

#if DLSSIV_DEV
// The serve thread's own timing (development builds, log>=2, one line per 300 served frames): the CPU from the frame message's read
// to ExecuteCommandLists returning; how often the in-fence had already reached the frame at submit (the chain was exposed; not
// counted on the bridge); the list's GPU time from two timestamps; and the gap from submit to the first timestamp on the CPU clock.
// Timestamps are read when their allocator slot retires, so GPU figures trail by the ring's depth. A failed creation is said once.
struct ServeTimer
{
    ID3D12QueryHeap* heap     = nullptr;
    ID3D12Resource*  readback = nullptr;     // two timestamps per allocator slot
    bool     tried   = false;                // once per device: a refusal is not retried every frame
    UINT64   gpuFreq = 0;
    LONGLONG qpcFreq = 0;
    bool     armed   = false;                // the list being recorded is a served frame's
    UINT64   n       = 0;                    // that frame, for the in-fence read
    LONGLONG wake    = 0;                    // its message's read
    bool     slotTimed[Host::kFrames] = {};
    LONGLONG slotExec[Host::kFrames]  = {};
    bool     calOk = false; UINT64 calGpu = 0, calCpu = 0;   // the queue's clock calibration, taken per window
    UINT32   frames = 0;                                     // the window
    UINT32   cpuN = 0, inN = 0, inPassed = 0; double cpuSum = 0, cpuMax = 0;
    UINT32   gpuN = 0;                        double gpuSum = 0, gpuMax = 0;
    UINT32   gapN = 0;                        double gapSum = 0, gapMax = 0;
};
static ServeTimer g_st;

static void ServeTimerCalibrate()
{
    g_st.calOk = h.queue && SUCCEEDED(h.queue->GetClockCalibration(&g_st.calGpu, &g_st.calCpu));
}

// Lazily, on the device that is current; ReleaseDevice lets it go with that device.
static bool ServeTimerReady()
{
    if (g_st.heap) return true;
    if (g_st.tried || !h.dev || !h.queue) return false;
    g_st.tried = true;
    D3D12_QUERY_HEAP_DESC qd = {};
    qd.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qd.Count = 2 * Host::kFrames;
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = 2 * Host::kFrames * sizeof(UINT64);
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT hq = h.dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&g_st.heap);
    const HRESULT hb = SUCCEEDED(hq) ? h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                                      nullptr, __uuidof(ID3D12Resource), (void**)&g_st.readback)
                                     : E_ABORT;
    const HRESULT hf = SUCCEEDED(hb) ? h.queue->GetTimestampFrequency(&g_st.gpuFreq) : E_ABORT;
    LARGE_INTEGER f = {}; QueryPerformanceFrequency(&f); g_st.qpcFreq = f.QuadPart;
    if (FAILED(hq) || FAILED(hb) || FAILED(hf) || !g_st.gpuFreq || !g_st.qpcFreq)
    {
        (g_gpuProbe ? LogRecord : LogHealth)("serve timing: off on this device (query heap 0x%08X, readback 0x%08X, timestamp frequency 0x%08X)",
                  (unsigned)hq, (unsigned)hb, (unsigned)hf);
        if (g_st.readback) { g_st.readback->Release(); g_st.readback = nullptr; }
        if (g_st.heap)     { g_st.heap->Release();     g_st.heap     = nullptr; }
        return false;
    }
    ServeTimerCalibrate();
    return true;
}

static void ServeTimerRelease()
{
    if (g_st.readback) { g_st.readback->Release(); g_st.readback = nullptr; }
    if (g_st.heap)     { g_st.heap->Release();     g_st.heap     = nullptr; }
    g_st.tried = false; g_st.armed = false; g_st.calOk = false;
    for (int i = 0; i < Host::kFrames; i++) g_st.slotTimed[i] = false;
}

// The slot has retired: its two timestamps are final.
static void ServeTimerCollect(int slot)
{
    if (!g_st.slotTimed[slot]) return;
    g_st.slotTimed[slot] = false;
    if (!g_st.readback) return;
    const SIZE_T at = (SIZE_T)slot * 2 * sizeof(UINT64);
    D3D12_RANGE rr = { at, at + 2 * sizeof(UINT64) };
    void* p = nullptr;
    if (FAILED(g_st.readback->Map(0, &rr, &p)) || !p) return;
    UINT64 t[2];
    memcpy(t, (const BYTE*)p + at, sizeof(t));
    D3D12_RANGE none = { 0, 0 };
    g_st.readback->Unmap(0, &none);
    if (t[1] <= t[0]) return;                  // an unwritten or disjoint pair is dropped, not averaged
    const double gpuUs = (double)(t[1] - t[0]) * 1e6 / (double)g_st.gpuFreq;
    g_st.gpuN++; g_st.gpuSum += gpuUs; if (gpuUs > g_st.gpuMax) g_st.gpuMax = gpuUs;
    if (g_st.calOk)
    {
        const double beginQpc = (double)(LONGLONG)g_st.calCpu
                              + (double)(LONGLONG)(t[0] - g_st.calGpu) * (double)g_st.qpcFreq / (double)g_st.gpuFreq;
        const double gapUs = (beginQpc - (double)g_st.slotExec[slot]) * 1e6 / (double)g_st.qpcFreq;
        g_st.gapN++; g_st.gapSum += gapUs; if (gapUs > g_st.gapMax) g_st.gapMax = gapUs;
    }
}

// Once per served frame, after its list: the window's line every 300.
static void ServeTimerTick()
{
    g_st.armed = false;
    if (++g_st.frames < 300) return;
    (g_gpuProbe ? LogRecord : LogHealth)("serve timing, %u frames: message read to execute %.0f us mean, worst %.0f (%u); the in-fence had already"
              " reached the frame at execute on %u of %u; GPU %.0f us mean, worst %.0f (%u); execute to the GPU's first"
              " timestamp %.0f us mean, worst %.0f (%u)",
              g_st.frames,
              g_st.cpuN ? g_st.cpuSum / g_st.cpuN : 0.0, g_st.cpuMax, g_st.cpuN,
              g_st.inPassed, g_st.inN,
              g_st.gpuN ? g_st.gpuSum / g_st.gpuN : 0.0, g_st.gpuMax, g_st.gpuN,
              g_st.gapN ? g_st.gapSum / g_st.gapN : 0.0, g_st.gapMax, g_st.gapN);
    g_st.frames = 0;
    g_st.cpuN = g_st.inN = g_st.inPassed = 0; g_st.cpuSum = g_st.cpuMax = 0;
    g_st.gpuN = 0; g_st.gpuSum = g_st.gpuMax = 0;
    g_st.gapN = 0; g_st.gapSum = g_st.gapMax = 0;
    ServeTimerCalibrate();
}
#endif

// ---- Commands: an allocator ring retired by an internal fence ----

static bool WaitFenceValue(ID3D12Fence* f, UINT64 v, DWORD ms)
{
    if (f->GetCompletedValue() >= v) return true;
    ResetEvent(h.fenceEvent);
    f->SetEventOnCompletion(v, h.fenceEvent);
    return WaitForSingleObject(h.fenceEvent, ms) == WAIT_OBJECT_0 && f->GetCompletedValue() >= v;
}

static bool BeginCommands()
{
        // AbortCommands can fail to recreate the list; every caller treats false as a failed frame.
    if (!h.list) return false;
    const UINT64 retire = h.allocFence[h.slot];
    if (retire != 0 && !WaitFenceValue(h.fence, retire, 2000))
    { LogRecord("the GPU did not retire allocator slot %d in 2 s", h.slot); return false; }
#if DLSSIV_DEV
    ServeTimerCollect(h.slot);   // the slot has retired, so its timestamps are final
#endif
    if (FAILED(h.alloc[h.slot]->Reset())) return false;
    const bool ok = SUCCEEDED(h.list->Reset(h.alloc[h.slot], nullptr));
#if DLSSIV_DEV
    if (ok && g_st.armed) h.list->EndQuery(g_st.heap, D3D12_QUERY_TYPE_TIMESTAMP, 2u * (UINT)h.slot);
#endif
    return ok;
}

static void AbortCommands();   // defined below; EndCommands needs it on a failed Close

// Returns 0 when nothing was submitted. A list whose Close failed must not be executed (Close reports deferred recording errors, such
// as inside NGX or ffx-api), so it goes to AbortCommands, which recreates it closed. The ring bookkeeping is skipped then: an
// allocFence nothing will signal would cost every later BeginCommands its 2 s timeout.
static UINT64 EndCommands()
{
#if DLSSIV_DEV
    if (g_st.armed)   // timer: the second timestamp, and both resolved into this slot's pair
    {
        h.list->EndQuery(g_st.heap, D3D12_QUERY_TYPE_TIMESTAMP, 2u * (UINT)h.slot + 1u);
        h.list->ResolveQueryData(g_st.heap, D3D12_QUERY_TYPE_TIMESTAMP, 2u * (UINT)h.slot, 2, g_st.readback,
                                 (UINT64)h.slot * 2 * sizeof(UINT64));
    }
#endif
    const HRESULT hrClose = h.list->Close();
    if (FAILED(hrClose))
    {
        AbortCommands();
        static int said = 0;
        if (said++ < 5)
            LogRecord("the command list failed to close 0x%08X: nothing submitted this pass", (unsigned)hrClose);
        return 0;
    }
    ID3D12CommandList* lists[] = { h.list };
    h.queue->ExecuteCommandLists(1, lists);
#if DLSSIV_DEV
    if (g_st.armed)   // timer: the CPU chain ends here, and whether the game's copies were already done
    {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if (!g_bridge && h.fenceIn) { g_st.inN++; if (h.fenceIn->GetCompletedValue() >= g_st.n) g_st.inPassed++; }
        const double cpuUs = (double)(q.QuadPart - g_st.wake) * 1e6 / (double)g_st.qpcFreq;
        g_st.cpuN++; g_st.cpuSum += cpuUs; if (cpuUs > g_st.cpuMax) g_st.cpuMax = cpuUs;
        g_st.slotTimed[h.slot] = true; g_st.slotExec[h.slot] = q.QuadPart;
        g_st.armed = false;           // one list a frame is timed
    }
#endif
    const UINT64 v = ++h.fenceValue;
    h.queue->Signal(h.fence, v);
    h.allocFence[h.slot] = v;
    h.slot = (h.slot + 1) % Host::kFrames;
    return v;
}

static void AbortCommands()   // never execute a list NGX faulted in
{
    if (!h.list) return;
    __try { h.list->Close(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    h.list->Release();
    h.list = nullptr;
    const HRESULT hr = h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, h.alloc[h.slot], nullptr,
                                                __uuidof(ID3D12GraphicsCommandList), (void**)&h.list);
    if (SUCCEEDED(hr)) h.list->Close();
    else
    {
                // Said once per abort; BeginCommands then fails each frame and the three-in-a-row rule ends the connection.
        h.list = nullptr;
        LogRecord("the command list could not be recreated after an aborted pass (0x%08X): the connection ends", (unsigned)hr);
    }
}

static bool DeviceRemoved(const char* where)
{
    if (!h.dev) return false;
    const HRESULT reason = h.dev->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) return false;
    if (!g_deviceRemoved) LogRecord("the D3D12 device was removed (0x%08X) during %s: exiting", (unsigned)reason, where);
    g_deviceRemoved = true;
    return true;
}

// The game's semaphores as this helper's fences (Linux sync path). The game duplicated the handles in; OpenSharedHandle does not
// consume them, so they are closed here either way. On a refusal the helper's own fences stand in and the game's probe picks the bridge.
static bool OpenGameSemaphores(UINT64 semIn, UINT64 semOut)   // false: no fences at all, the connection must end
{
    const HANDLE hi = (HANDLE)(uintptr_t)semIn, ho = (HANDLE)(uintptr_t)semOut;
    ID3D12Fence* fi = nullptr; ID3D12Fence* fo = nullptr;
    HRESULT ri = E_HANDLE, ro = E_HANDLE;
    if (hi && ho)
    {
        ri = h.dev->OpenSharedHandle(hi, __uuidof(ID3D12Fence), (void**)&fi);
        ro = h.dev->OpenSharedHandle(ho, __uuidof(ID3D12Fence), (void**)&fo);
        CloseHandle(hi); CloseHandle(ho);
    }
    if (SUCCEEDED(ri) && SUCCEEDED(ro) && fi && fo)
    {
        h.fenceIn = fi; h.fenceOut = fo; g_semOpened = true;
        LogRecord("sync: the game's two semaphores are open as this host's fences (sync=%d)", g_sync);
        return true;
    }
    if (fi) fi->Release();
    if (fo) fo->Release();
    const HRESULT ci = h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&h.fenceIn);
    const HRESULT co = h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&h.fenceOut);
    g_semOpened = false;   // the mode arrives in 'M': the game's probe reads FEED_ANS_OPENED false and picks the bridge
    if (FAILED(ci) || FAILED(co) || !h.fenceIn || !h.fenceOut)
    {
        LogRecord("sync: the game's semaphores could not be opened (in 0x%08X, out 0x%08X), nor plain fences created (0x%08X,"
                      " 0x%08X): the connection ends", (unsigned)ri, (unsigned)ro, (unsigned)ci, (unsigned)co);
        return false;
    }
    LogRecord("sync: the game's semaphores could not be opened as fences (%s; in 0x%08X, out 0x%08X): own fences, the CPU"
                  " bridge carries the frames", (hi && ho) ? "OpenSharedHandle refused" : "no handles were sent",
              (unsigned)ri, (unsigned)ro);
    return true;
}
// Bridge mode's answer, one per frame, written once the helper's out-fence reached the frame (the game may then read the Output).
// A fence not there in 5 s ends the connection rather than answer early.
static bool WriteFull(HANDLE pipe, const void* buf, DWORD len);
static bool g_answerFenceTimedOut = false;   // AnswerFrame's false was this helper's fence, not the pipe
static bool AnswerFrame(HANDLE pipe, UINT64 n, bool evaluatedOk)
{
    if (h.fenceOut && !WaitFenceValue(h.fenceOut, n, 5000))
    { LogRecord("bridge: the helper's own fence did not reach %llu within 5 s: the connection ends", (unsigned long long)n); g_answerFenceTimedOut = true; return false; }
    // Standalone native64 client checks bit 8: the matching frame actually
    // evaluated, in addition to its fence retiring. The old game relay ignores
    // status, so this additive result bit keeps the protocol-16 structure exact.
    FeedAnswerIvt a = { 'R', n, evaluatedOk ? 8u : 0u };
    if (!WriteFull(pipe, &a, sizeof(a))) return false;
    if (g_answerEvent) SetEvent(g_answerEvent);   // after the write, so a woken relay finds the whole answer
    return true;
}

// Release the client with a queue Signal(n), ordered behind the previous frame's. A CPU Signal(n) while the queued Signal(n-1) is
// pending would move the fence backwards when that lands, costing a stale frame. The CPU signal is kept for a removed device
// (nothing queued will run) and as the fallback when the queue call fails.
static void ReleaseClient(UINT64 n)
{
    if (!h.fenceOut) return;
    if (g_deviceRemoved || !h.queue || FAILED(h.queue->Signal(h.fenceOut, n)))
        h.fenceOut->Signal(n);
}

// ---- D3D12 + NGX ----

// Eight LUID bytes as the log prints them everywhere.
static const char* LuidStr(const unsigned char* l, char* buf)   // buf: 20 bytes
{
    _snprintf_s(buf, 20, _TRUNCATE, "%02X%02X%02X%02X%02X%02X%02X%02X", l[0], l[1], l[2], l[3], l[4], l[5], l[6], l[7]);
    return buf;
}

typedef HRESULT (WINAPI *PFN_D3D12Create)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

// One adapter tried: a hardware adapter that makes a D3D12 device becomes h.dev with its vendor and LUID. The LUID is logged only
// where two GPUs are in play.
static bool TryAdapter(PFN_D3D12Create create, IDXGIAdapter1* a, const char* how)
{
    DXGI_ADAPTER_DESC1 d = {}; a->GetDesc1(&d);
    if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return false;
    if (FAILED(create(a, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&h.dev))) { h.dev = nullptr; return false; }
    h.vendorId = d.VendorId;
    memcpy(g_luid, &d.AdapterLuid, sizeof(g_luid)); g_luidValid = true; g_adapterHow = how;
    LogRecord("D3D12 device on %ls (%llu MB), chosen by %s", d.Description, (unsigned long long)(d.DedicatedVideoMemory >> 20), how);
    return true;
}

// wantLuid: the game's adapter (8 bytes) once the handshake said which; null at startup. Order: 1. the game's adapter by LUID (the
// rebind from Serve); 2. DXGI's HIGH_PERFORMANCE preference (adapter 0 is the desktop's, the iGPU on a muxless laptop, while DXVK
// sorts discrete GPUs first); 3. enumeration order without IDXGIFactory6. The device is made before any client says its GPU so NGX
// loads while the game loads; the handshake rebuilds with tier 1 when the guess was wrong.
static bool InitDevice(const unsigned char* wantLuid)
{
    HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    HMODULE dxgi  = LoadLibraryW(L"dxgi.dll");
    typedef HRESULT (WINAPI *PFN_Factory)(REFIID, void**);
    auto create  = d3d12 ? (PFN_D3D12Create)GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr;
    auto factory = dxgi  ? (PFN_Factory)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    if (!create || !factory) { LogRecord("d3d12/dxgi exports missing"); return false; }

    IDXGIFactory4* fac = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED(factory(__uuidof(IDXGIFactory4), (void**)&fac)))
    {
                // Once per process: the adapter census, so a laptop log names every GPU.
        static bool counted = false;
        UINT hw = 0;
                // At log=1 only with more than one card (with one it repeats the device line), so the cards are counted first.
        UINT hwTotal = 0;
        for (UINT i = 0; !counted && fac->EnumAdapters1(i, &adapter) == S_OK && adapter; i++)
        {
            DXGI_ADAPTER_DESC1 d = {}; adapter->GetDesc1(&d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) hwTotal++;
            adapter->Release(); adapter = nullptr;
        }
        for (UINT i = 0; fac->EnumAdapters1(i, &adapter) == S_OK && adapter; i++)
        {
            DXGI_ADAPTER_DESC1 d = {}; adapter->GetDesc1(&d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                hw++;
                char l[20]; unsigned char lb[8]; memcpy(lb, &d.AdapterLuid, 8);
                if (!counted) (hwTotal > 1 ? LogRecord : LogHealth)("adapter %u: %ls (vendor 0x%04X, %llu MB dedicated video memory), LUID %s", i, d.Description,
                    d.VendorId, (unsigned long long)(d.DedicatedVideoMemory >> 20), LuidStr(lb, l));
            }
            adapter->Release(); adapter = nullptr;
        }
        counted = true;
        if (wantLuid)
        {
            LUID want = {}; memcpy(&want, wantLuid, sizeof(want));
            if (fac->EnumAdapterByLuid(want, __uuidof(IDXGIAdapter1), (void**)&adapter) == S_OK && adapter)
            { TryAdapter(create, adapter, "the game's own adapter LUID"); adapter->Release(); adapter = nullptr; }
            if (!h.dev) { char l[20]; LogRecord("the game's adapter LUID %s gave no D3D12 device here", LuidStr(wantLuid, l)); }
        }
        IDXGIFactory6* fac6 = nullptr;
        const bool hadFac6 = !h.dev && SUCCEEDED(fac->QueryInterface(__uuidof(IDXGIFactory6), (void**)&fac6)) && fac6;
        if (hadFac6)
        {
            for (UINT i = 0; !h.dev; i++)
            {
                if (fac6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void**)&adapter) != S_OK || !adapter) break;
                TryAdapter(create, adapter, hw > 1 ? "DXGI's HIGH_PERFORMANCE preference" : "being the only hardware adapter");
                adapter->Release(); adapter = nullptr;
            }
            fac6->Release();
        }
        for (UINT i = 0; !h.dev; i++)
        {
            if (fac->EnumAdapters1(i, &adapter) != S_OK || !adapter) break;
            // Reached with IDXGIFactory6 present only when every preferred adapter refused a device.
            TryAdapter(create, adapter, hadFac6 ? "enumeration order (no adapter DXGI preferred gave a D3D12 device)"
                                                : "enumeration order (no IDXGIFactory6 on this system)");
            adapter->Release(); adapter = nullptr;
        }
        fac->Release();
    }
    if (!h.dev)
    {
        HRESULT hr = create(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&h.dev);
        if (FAILED(hr)) { LogRecord("D3D12CreateDevice failed 0x%08X", (unsigned)hr); return false; }
        g_adapterHow = "D3D12's default adapter (no DXGI factory)";
        LogRecord("no DXGI factory: no vendor id or LUID for this adapter");
    }
    D3D12_COMMAND_QUEUE_DESC qd = {};
        // High priority: the helper sits in the game's critical path (the game presents after, or the next frame waits on, the out fence),
    // so queue precedence is round-trip latency. HIGH needs no privilege, unlike GLOBAL_REALTIME.
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    if (FAILED(h.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&h.queue)))
    {
                // A driver may refuse the tier: fall back and log which one was taken.
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        if (FAILED(h.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&h.queue)))
        { LogRecord("queue creation failed"); return false; }
        LogRecord("command queue at NORMAL priority: HIGH was refused");
    }
    else LogHealth("command queue at HIGH priority, so the upscale is not queued"
        " behind the game's own GPU work");
    for (int i = 0; i < Host::kFrames; i++)
        if (FAILED(h.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&h.alloc[i])))
        { LogRecord("allocator creation failed"); return false; }
    if (FAILED(h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, h.alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&h.list)))
    { LogRecord("command list creation failed"); return false; }
    h.list->Close();
    if (FAILED(h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&h.fence))) { LogRecord("fence creation failed"); return false; }
    h.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

// -1 automatic, 0 DLSS through NGX, 1 FSR through ffx-api, 2 XeSS through libxess.dll (xess_host.h). Set once from the command line
// and latched once automatic resolves. At 0 AMD's DLL is never looked for. g_backendCli keeps the command line's answer so automatic
// can resolve again after a rebind (an AMD iGPU resolves to FSR; the NVIDIA dGPU the game is on must not inherit that).
static int g_backend = -1;
static int g_backendCli = -1;

static bool InitNgx(const wchar_t* dataPath)
{
    NVSDK_NGX_Result r = NVSDK_NGX_D3D12_Init(0x1000000ULL, dataPath, h.dev, nullptr, NVSDK_NGX_Version_API);
    LogHealth("NVSDK_NGX_D3D12_Init -> 0x%08X (%s)", (unsigned)r, NgxName(r));
    if (NVSDK_NGX_FAILED(r))
    {
        r = NVSDK_NGX_D3D12_Init_with_ProjectID("7c2a5b1e-4d3f-4a8e-9b6c-1e2f3a4b5c6d", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                                                "1.0", dataPath, h.dev, nullptr, NVSDK_NGX_Version_API);
        LogHealth("Init_with_ProjectID -> 0x%08X (%s)", (unsigned)r, NgxName(r));
    }
    if (NVSDK_NGX_FAILED(r))
    {
                // At log=1: OutOfDate means the driver is older than this DLSS needs, not that the card cannot run it.
        LogRecord("NGX did not initialise: 0x%08X (%s)", (unsigned)r, NgxName(r));
        if (r == NVSDK_NGX_Result_FAIL_OutOfDate) h.ngxDriverOld = true;   // the client's note names the driver, not the card
        return false;
    }
    h.ngxInited = true;
        // The capability map is the caller's to free (nvsdk_ngx.h: NVSDK_NGX_DestroyParameters). The destroy sits outside the success test:
    // a failed call that still wrote a pointer would leak. avail defaults to 1: no map has never meant DLSS is unavailable.
    NVSDK_NGX_Parameter* caps = nullptr;
    r = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    int avail = 1;
    if (NVSDK_NGX_SUCCEED(r) && caps)
    {
        avail = 0;
        caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &avail);
        LogHealth("SuperSampling.Available=%d", avail);
        if (!avail)
        {
            // Why not, from the map: the driver or the card. Each Get's result is kept, since a missing key would read as a confident zero.
            // The version pair is unsigned, as in NVIDIA's sample (the Get overloads are distinct).
            int needsDrv = 0, initRes = 0; unsigned major = 0, minor = 0;
            const bool okDrv = NVSDK_NGX_SUCCEED(caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDrv));
            const bool okMaj = NVSDK_NGX_SUCCEED(caps->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &major));
            const bool okMin = NVSDK_NGX_SUCCEED(caps->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minor));
            const bool okRes = NVSDK_NGX_SUCCEED(caps->Get(NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &initRes));
            char sDrv[16] = "not reported", sMaj[16] = "?", sMin[16] = "?", sRes[48] = "not reported";
            if (okDrv) _snprintf_s(sDrv, sizeof(sDrv), _TRUNCATE, "%d", needsDrv);
            if (okMaj) _snprintf_s(sMaj, sizeof(sMaj), _TRUNCATE, "%u", major);
            if (okMin) _snprintf_s(sMin, sizeof(sMin), _TRUNCATE, "%u", minor);
            if (okRes) _snprintf_s(sRes, sizeof(sRes), _TRUNCATE, "0x%08X (%s)", (unsigned)initRes, NgxName((NVSDK_NGX_Result)initRes));
            const bool noFile = okRes && (NVSDK_NGX_Result)initRes == NVSDK_NGX_Result_FAIL_FeatureNotFound;
            LogRecord("NGX says DLSS is unavailable: NeedsUpdatedDriver=%s (minimum driver %s.%s), FeatureInitResult %s%s",
                  sDrv, sMaj, sMin, sRes, noFile ? " (no " DLSSIV_HOST_DIR "\\nvngx_dlss.dll)" : "");
            if (okDrv && needsDrv) h.ngxDriverOld = true;
            // FeatureNotFound: no nvngx_dlss.dll beside the exe (the driver installs only the NGX core). The client names the file from this bit.
            if (noFile) h.ngxNoFile = true;
        }
    }
    if (caps) { NVSDK_NGX_D3D12_DestroyParameters(caps); caps = nullptr; }
    if (!avail) return false;
    r = NVSDK_NGX_D3D12_AllocateParameters(&h.params);
    if (NVSDK_NGX_FAILED(r) || !h.params) { LogRecord("AllocateParameters failed 0x%08X", (unsigned)r); return false; }
    return true;
}

// DLSS Super Resolution: the quality preset whose render range contains the
// work size for this output size (the render scale, when the client asks).
static bool PickSrQuality(UINT w, UINT h_, UINT outW, UINT outH)
{
    static const struct { NVSDK_NGX_PerfQuality_Value q; const char* name; } kOrder[] = {
        { NVSDK_NGX_PerfQuality_Value_UltraQuality, "Ultra Quality" }, { NVSDK_NGX_PerfQuality_Value_MaxQuality, "Quality" },
        { NVSDK_NGX_PerfQuality_Value_Balanced, "Balanced" }, { NVSDK_NGX_PerfQuality_Value_MaxPerf, "Performance" },
        { NVSDK_NGX_PerfQuality_Value_UltraPerformance, "Ultra Performance" } };
        // This map is allocated per build (every resolution or format change) and must be destroyed like InitNgx's.
    NVSDK_NGX_Parameter* caps = nullptr;
    const NVSDK_NGX_Result rcaps = NVSDK_NGX_D3D12_GetCapabilityParameters(&caps);
    if (NVSDK_NGX_FAILED(rcaps) || !caps)
    {
                // A failed call may still write a pointer, so the free asks whether there is a map.
        if (caps) NVSDK_NGX_D3D12_DestroyParameters(caps);
        return false;
    }
        // Among the modes whose dynamic range holds the size, the one whose optimal size is nearest: the client derives the render size from
    // a mode's ratio, and overlapping ranges would otherwise name the wrong mode. When no range holds it, the nearest mode overall is
    // used and the ranges logged: NGX's ranges are dynamic-resolution bounds, not creation limits. Custom sizes: 34 to 99 percent.
    int best = -1; const char* bestName = nullptr; unsigned bestDist = 0xFFFFFFFFu;
    int nearest = -1; const char* nearName = nullptr; unsigned nearDist = 0xFFFFFFFFu;   // (near is a Windows macro)
    char ranges[320] = ""; size_t rl = 0;
    for (const auto& o : kOrder)
    {
        unsigned ow = 0, oh = 0, maxw = 0, maxh = 0, minw = 0, minh = 0; float sharp = 0.0f;
        const NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(caps, outW, outH, o.q, &ow, &oh, &maxw, &maxh, &minw, &minh, &sharp);
        if (NVSDK_NGX_FAILED(r) || ow == 0 || oh == 0) continue;
        const unsigned d = (w > ow ? w - ow : ow - w) + (h_ > oh ? h_ - oh : oh - h_);
        if (d < nearDist) { nearDist = d; nearest = (int)o.q; nearName = o.name; }
        if (rl < sizeof(ranges) - 1) rl += (size_t)_snprintf_s(ranges + rl, sizeof(ranges) - rl, _TRUNCATE, "%s%s %ux%u [%ux%u..%ux%u]", rl ? "; " : "", o.name, ow, oh, minw, minh, maxw, maxh);
        if (w < minw || w > maxw || h_ < minh || h_ > maxh) continue;
        if (d < bestDist) { bestDist = d; best = (int)o.q; bestName = o.name; }
    }
    if (best < 0 && nearest >= 0)
    {
        LogRecord("%ux%u -> %ux%u is inside no DLSS mode's dynamic range (%s): the nearest, %s, is used", w, h_, outW, outH, ranges, nearName);
        best = nearest; bestName = nearName; bestDist = nearDist;
    }
        // The loop above is the map's only reader: one free covers both exits.
    NVSDK_NGX_D3D12_DestroyParameters(caps); caps = nullptr;
    if (best < 0) return false;
    h.srQuality = best; h.srQualityName = bestName;
        // NGX's own optimal input for the mode at this output size. Nonzero means the scene renders at a size the mode was not tuned for:
    // log=1 then, log=2 when it matches.
    (bestDist == 0 ? LogHealth : LogRecord)("%ux%u -> %ux%u is DLSS %s (optimal size off by %u pixel(s) in total)%s",
        w, h_, outW, outH, bestName, bestDist,
        bestDist == 0 ? "" : g_scale > 0 ? "   (custom= is set: the nearest mode, as expected)"
                                        : "   <-- NOT the optimal input size for this mode");
    return true;
}

// A library that raised is never called again, by anything (release and shutdown included). __except unwinds only this module's
// frames: a lock held inside NVIDIA's or AMD's code stays held, so a second call can block for ever. A raise latches here, every
// wrapper refuses once latched, and the serve loop ends the connection.
static bool g_ngxRaised = false, g_ffxRaised = false;
// Where it faulted: an exception filter runs before the unwind, so the faulting address maps to its module and offset
// (GetModuleHandleExA, FROM_ADDRESS and UNCHANGED_REFCOUNT: no reference taken). A code alone would leave the note guessing
// amd_fidelityfx_upscaler_dx12.dll; the fault may be in the loader, a driver DLL or a community build.
// One buffer, written by the last raise; the filter clears it first, so a stale location never prints beside a fresh code.
static char g_raiseWhere[200];
static LONG RaiseNote(EXCEPTION_POINTERS* xp)
{
    g_raiseWhere[0] = 0;
    const EXCEPTION_RECORD* er = (xp && xp->ExceptionRecord) ? xp->ExceptionRecord : nullptr;
    if (!er) return EXCEPTION_EXECUTE_HANDLER;
        // A stack overflow gets no lookup: the loader and CRT calls would run on the exhausted stack.
    if (er->ExceptionCode == EXCEPTION_STACK_OVERFLOW) { strcpy_s(g_raiseWhere, " (stack overflow)"); return EXCEPTION_EXECUTE_HANDLER; }
        // A C++ throw (0xE06D7363) is named by its thrower: on x64 the fourth parameter is the throwing image's base, and the exception
    // address is RaiseException itself.
    const bool cxx = er->ExceptionCode == 0xE06D7363u && er->NumberParameters >= 4;
    void* at = cxx ? (void*)er->ExceptionInformation[3] : er->ExceptionAddress;
    if (!at) return EXCEPTION_EXECUTE_HANDLER;
    HMODULE m = nullptr;
    char path[MAX_PATH] = "";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)at, &m) && m && GetModuleFileNameA(m, path, MAX_PATH))
    {
        const char* base = strrchr(path, '\\');
        if (cxx) _snprintf_s(g_raiseWhere, sizeof(g_raiseWhere), _TRUNCATE, " thrown by %s", base ? base + 1 : path);
        else     _snprintf_s(g_raiseWhere, sizeof(g_raiseWhere), _TRUNCATE, " in %s+0x%llX", base ? base + 1 : path,
                     (unsigned long long)((const BYTE*)at - (const BYTE*)m));
    }
    else
        _snprintf_s(g_raiseWhere, sizeof(g_raiseWhere), _TRUNCATE, " at %p, which is in no loaded module", at);
    return EXCEPTION_EXECUTE_HANDLER;
}
// The highest shader model the device's D3D12 runtime offers, as 0x65 for 6.5 (ReadTopShaderModel); 0 until read. CreateFsrContext
// decides on it (FsrBelowFsr4) and FsrRaiseHint names it. At most ten CheckFeatureSupport calls per context.
static int g_topSm = 0;
static void ReadTopShaderModel()
{
    int top = 0;
    for (int sm = 0x69; h.dev && sm >= 0x60 && !top; sm--)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL d = { (D3D_SHADER_MODEL)sm };
        if (SUCCEEDED(h.dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &d, sizeof(d)))) top = (int)d.HighestShaderModel;
    }
    g_topSm = top;
}
// The shader model on which automatic passed FSR 4 by in the current context (0 when it did not), and whether a D3D12Core.dll in
// DLSSIV\D3D12\ is not the runtime this process runs (OptInAgilityRuntime). Both go to the client in every build ack.
static int  g_fsr4SkipSm     = 0;
static bool g_agilityRefused = false;
// The FSR 4 shader model line; development builds can move it with --fsr4_min_sm= to test the rule on any card.
static int  g_fsr4MinSm      = FEED_FSR4_MIN_SM;
// Bits every build ack carries for FSR: AMD's library raised (FEED_ACK_FSR_RAISED, FSR cannot return this session), the shader
// model, FSR 4 passed by, and an Agility file not used.
static uint32_t AckFsrBits()
{
    if (g_backend != 1) return 0u;
    return (g_ffxRaised ? FEED_ACK_FSR_RAISED : 0u) | (g_fsr4SkipSm ? FEED_ACK_FSR4_SKIPPED : 0u)
         | (g_agilityRefused ? FEED_ACK_AGILITY_REFUSED : 0u) | (((uint32_t)g_topSm << FEED_ACK_SM_SHIFT) & FEED_ACK_SM_MASK);
}
static bool RefusedAfterRaise(bool raised, const char* lib)
{
    if (!raised) return false;
    static bool saidNgx = false, saidFfx = false;
    bool& said = (lib[0] == 'N') ? saidNgx : saidFfx;
    if (!said) { said = true; LogRecord("%s raised an exception earlier in this process: not called again", lib); }
    return true;
}

static NVSDK_NGX_Result SafeCreate(NVSDK_NGX_DLSS_Create_Params* cp, DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ngxRaised, "NGX")) return (NVSDK_NGX_Result)0x7FFFFFFF;
    __try { return NGX_D3D12_CREATE_DLSS_EXT(h.list, 1, 1, &h.feature, h.params, cp); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ngxRaised = true; return (NVSDK_NGX_Result)0x7FFFFFFF; }
}

static NVSDK_NGX_Result SafeEvaluate(NVSDK_NGX_D3D12_DLSS_Eval_Params* ep, DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ngxRaised, "NGX")) return (NVSDK_NGX_Result)0x7FFFFFFF;
    __try { return NGX_D3D12_EVALUATE_DLSS_EXT(h.list, h.feature, h.params, ep); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ngxRaised = true; return (NVSDK_NGX_Result)0x7FFFFFFF; }
}

static void SafeRelease(NVSDK_NGX_Handle* f)
{
    if (!f || RefusedAfterRaise(g_ngxRaised, "NGX")) return;
    __try { NVSDK_NGX_D3D12_ReleaseFeature(f); }
    __except (RaiseNote(GetExceptionInformation())) { g_ngxRaised = true; LogRecord("ReleaseFeature raised 0x%08X%s (ignored)", GetExceptionCode(), g_raiseWhere); }
}

// The render preset hint, from preset= in DLSS-IV.cfg via --preset=. Set before the create.
static void SetPresetHints()
{
    if (!h.params) return;
    const unsigned p = (unsigned)g_cfgPreset;
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, p);
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, p);
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, p);
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, p);
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, p);
    h.params->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, p);
}

static bool CreateFeature(UINT w, UINT h_, int flags, NVSDK_NGX_Result* outR, UINT targetW, UINT targetH)
{
    const bool sr = targetW != 0 && targetH != 0 && (targetW != w || targetH != h_);
    NVSDK_NGX_DLSS_Create_Params cp = {};
    cp.Feature.InWidth            = w;
    cp.Feature.InHeight           = h_;
    cp.Feature.InTargetWidth      = sr ? targetW : w;
    cp.Feature.InTargetHeight     = sr ? targetH : h_;
    cp.Feature.InPerfQualityValue = sr ? (NVSDK_NGX_PerfQuality_Value)h.srQuality : NVSDK_NGX_PerfQuality_Value_DLAA;
    cp.InFeatureCreateFlags       = flags;
    cp.InEnableOutputSubrects     = false;
    SetPresetHints();
    if (!BeginCommands()) return false;
    DWORD code = 0;
    const NVSDK_NGX_Result rf = SafeCreate(&cp, &code);
    if (outR) *outR = rf;
    if (code != 0) { AbortCommands(); h.feature = nullptr; LogRecord("CreateFeature raised 0x%08X%s (caught)", code, g_raiseWhere); return false; }
    const UINT64 v = EndCommands();
        // Zero means the list was never submitted; test it before the wait, since WaitFenceValue(fence, 0) returns true at once. The create's
    // init commands never ran, so release whatever NGX handed back.
    if (v == 0)
    {
        LogRecord("the feature create's command list failed to close: build refused");
        SafeRelease(h.feature); h.feature = nullptr;
        return false;
    }
    if (!WaitFenceValue(h.fence, v, 4000)) { LogRecord("the feature create did not complete"); return false; }
    if (NVSDK_NGX_FAILED(rf) || !h.feature)
    {
        LogRecord("CreateFeature failed 0x%08X (%s)", (unsigned)rf, NgxName(rf));
                // Release whatever handle a failed create wrote (SafeRelease returns on null and wraps NGX in SEH). Below the fence wait on purpose:
        // that wait retires the create's own init submission.
        SafeRelease(h.feature); h.feature = nullptr;
        return false;
    }
    if (sr) LogHealth("feature ready: %ux%u -> %ux%u DLSS %s, preset hint %d, flags %d", w, h_, targetW, targetH, h.srQualityName, g_cfgPreset, flags);
    else    LogHealth("feature ready: %ux%u DLAA, preset hint %d, flags %d", w, h_, g_cfgPreset, flags);
    return true;
}

// ---- The FSR back end ----
// AMD FidelityFX Super Resolution through ffx-api, selected by --backend=1. ffx-api rather than the static backend because AMD's
// driver upgrades FSR 3.1 to FSR 4 by overriding a signed FidelityFX DLL (DirectX 12 only), and a static build has no DLL.
// Never linked: ffx_api_loader.h resolves every entry point by GetProcAddress, so a missing DLL is one log line.
// Two layouts, one API: SDK 1.1 is one DLL, amd_fidelityfx_dx12.dll (FSR 3.1.4); SDK 2.x's amd_fidelityfx_loader_dx12.dll exports
// the same entry points and loads effects from amd_fidelityfx_upscaler_dx12.dll beside it, where FSR 4 lives and which community
// FSR 4 builds replace. The descriptors are the same in both. Official FSR 4 (SDK 2.4.0) is offered on RX 6000, 7000 and 9000 and some Ryzen AI parts.
// DX12, not the Vulkan build: this process holds no Vulkan device.
#include <cfloat>            // FLT_MAX, for FSR's infinite far plane
// SDK 2.3.0's headers in the SDK's own layout (the shipped runtime is 2.4.0, whose API is the same): its frame generation speaks PrepareV2, which 1.1.4
// lacks. The upscale descriptions are the same in both (external/ffx/VERSION.txt).
#include <ffx_api_2.3.0/api/include/ffx_api.h>
#include <ffx_api_2.3.0/upscalers/include/ffx_upscale.h>
#include <ffx_api_2.3.0/framegeneration/include/ffx_framegeneration.h>
#define PLATFORM_WINDOWS 1   // the 2.3.0 loader header needs the platform named (its #else is a #pragma error)
#include <ffx_api_2.3.0/api/include/dx12/ffx_api_dx12.h>
#include <ffx_api_2.3.0/api/include/ffx_api_loader.h>

static HMODULE      g_ffxDll = nullptr;
static ffxFunctions g_ffx    = {};
static ffxContext   g_ffxCtx = nullptr;
// Which layout loaded: one of kFfxDlls' literals or the empty string, never a copy.
static const char*  g_ffxDllName = "";
// Set by a warm-up, consumed by the next real dispatch, which then resets FSR's history so nothing from the blank warm-up textures
// (auto-exposure included) reaches a shown frame.
static bool         g_fsrResetNext = false;
// When the build being served arrived (GetTickCount64), set by the 'B' handler. The client waits 60 s for a build's answer
// (IvtBuild) and a warm-up runs inside that wait, so WarmUpFsr budgets against it.
static ULONGLONG    g_buildStartTick = 0;
// Set when a warm-up the GPU never finished kept the shared set alive; ReleaseRefusedSet reads and clears it to say "kept".
static bool         g_setKept = false;
// The split loader first: it can offer FSR 4, and where it offers none it still offers FSR 3.1.
static const char* const kFfxDlls[] = { "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_dx12.dll" };

static const char* FfxName(ffxReturnCode_t r)
{
    switch (r)
    {
    case FFX_API_RETURN_OK:                     return "OK";
    case FFX_API_RETURN_ERROR:                  return "unspecified error";
    case FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE: return "unknown desc type";
    case FFX_API_RETURN_ERROR_RUNTIME_ERROR:    return "runtime error (D3D12 or the effect)";
    case FFX_API_RETURN_NO_PROVIDER:            return "no provider for this desc type";
    case FFX_API_RETURN_ERROR_MEMORY:           return "allocation failed";
    case FFX_API_RETURN_ERROR_PARAMETER:        return "invalid parameter";
    default:                                    return "?";
    }
}

// Which FSR versions the DLL offers (each layout picks one per GPU; a community upscaler changes the list). A log=1 line, and the
// list fsr_version= is matched against. Asked only when something reads the answer: the log at 1 and above, an fsr_version to match,
// a failed create looking for another version, InitFsr checking a loader has an upscaler, or automatic below shader model 6.6.
// A raise here ends FSR for the session (g_ffxRaised) and is said with its code. Behind SEH like every call into AMD's code.
static ffxReturnCode_t SafeFfxQuery(ffxContext* ctx, ffxQueryDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ffxRaised, "FSR")) return (ffxReturnCode_t)0x7FFFFFFF;   // see g_ngxRaised
    __try { return g_ffx.Query(ctx, hd); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ffxRaised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}

struct FfxVersions { uint64_t n; uint64_t ids[8]; const char* names[8]; };

// In the DLL's own order. The names point into the DLL and must be read at once: ReleaseFsr frees the DLL at a rebind.
// Returns the count, 0 when the DLL answered with none, -1 when it did not answer (an error, or a raise, logged here).
static int QueryFfxVersions(FfxVersions* v)
{
    *v = {};
    if (!g_ffx.Query) return -1;
    ffxQueryDescGetVersions q = {};
    q.header.type    = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    q.device         = h.dev;
    q.outputCount    = &v->n;              // zero in: the count comes back
    DWORD raised = 0;
    ffxReturnCode_t rc = SafeFfxQuery(nullptr, &q.header, &raised);
    if (rc == FFX_API_RETURN_OK && v->n)
    {
        if (v->n > 8) v->n = 8;            // the capacity in, the count written out
        q.versionIds   = v->ids;
        q.versionNames = v->names;
        rc = SafeFfxQuery(nullptr, &q.header, &raised);
        if (v->n > 8) v->n = 8;
    }
    if (raised)
        LogRecord("FSR: %s RAISED exception 0x%08X%s in its version query: not called again this session",
            g_ffxDllName, (unsigned)raised, g_raiseWhere);
    if (rc != FFX_API_RETURN_OK) { v->n = 0; return -1; }
    return (int)v->n;
}

static const char* FfxVerName(const FfxVersions& v, int i) { return (i >= 0 && v.names[i]) ? v.names[i] : "(unnamed)"; }

// FSR PICK START (tools/test-fsr-pick cuts from this line to FSR PICK END)
// "4.1.1" is major 4. A name not starting with a digit is 0, which no fsr_version asks for: offered, never picked.
static int FfxMajor(const char* s) { return (s && s[0] >= '0' && s[0] <= '9') ? atoi(s) : 0; }

// Numerically, so "3.1.10" is newer than "3.1.9". strtol, not sscanf_s: scanf's machinery costs this static-CRT exe 25 KB.
static void FfxDotted(const char* s, long out[3])
{
    out[0] = out[1] = out[2] = 0;
    for (int i = 0; i < 3 && s && *s >= '0' && *s <= '9'; i++)
    {
        char* end = nullptr;
        out[i] = strtol(s, &end, 10);
        s = (end && *end == '.') ? end + 1 : nullptr;
    }
}
static bool FfxNewer(const char* a, const char* b)
{
    long x[3], y[3];
    FfxDotted(a, x);
    FfxDotted(b, y);
    for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

// Automatic does not run FSR 4 where D3D12 offers a shader model below minSm (6.6): there AMD's FSR 4 warm-up was seen to raise
// inside D3D12Core.dll, which ends FSR for the session, while FSR 3.1 ran, and FSR 4 ran with an Agility runtime in DLSSIV\D3D12\.
// Where the list offers an FSR 4 and an FSR 3, automatic names the newest FSR 3; deciding before the create needs no second process.
// Only automatic: fsr_version=4 gets what it names. Shader model 0 is a runtime that did not answer; a name not starting with a
// digit is neither FSR 4 nor FSR 3. Returns the index to create, or -1 to change nothing.
static int FsrBelowFsr4(int fsrVersion, int topSm, int minSm, const char* const* names, int n)
{
    if (fsrVersion != 0 || topSm <= 0 || topSm >= minSm) return -1;
    bool four = false;
    int three = -1;
    for (int i = 0; i < n; i++)
    {
        const int major = FfxMajor(names[i]);
        if (major >= 4) four = true;
        else if (major == 3 && (three < 0 || FfxNewer(names[i], names[three]))) three = i;
    }
    return four ? three : -1;
}
// FSR PICK END

// Beside the exe first, both layouts in kFfxDlls' order, then the search path the same way. Never linked: a missing DLL refuses the
// build, not the process. The first file that can serve an upscaler is kept, not the first that loads: an
// amd_fidelityfx_loader_dx12.dll whose amd_fidelityfx_upscaler_dx12.dll is missing or will not load (Wine) gives way to the next
// file, as does a file without AMD's exports; every skip is one log line.
// All five ffx-api exports are required though ffxConfigure has no call site here: a DLL missing one is the wrong or a corrupt file.
static bool InitFsr(const char* dir)
{
    const int kinds = (int)(sizeof(kFfxDlls) / sizeof(kFfxDlls[0]));
        // A file found beside the exe is not looked for again on the search path, which starts at the exe's folder.
    bool besideExe[kinds] = {};
    char path[MAX_PATH];
    for (int pass = 0; pass < 2 && !g_ffxDll; pass++)
        for (int i = 0; i < kinds && !g_ffxDll; i++)
        {
            const char* const name = kFfxDlls[i];
            if (pass == 1 && besideExe[i]) continue;
            if (pass == 0) _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\%s", dir, name);
            const HMODULE m = LoadLibraryA(pass == 0 ? path : name);
            if (!m) continue;
            if (pass == 0) besideExe[i] = true;
            ffxFunctions f = {};
            ffxLoadFunctions(&f, m);
            if (!f.CreateContext || !f.DestroyContext || !f.Dispatch || !f.Query || !f.Configure)
            {
                LogRecord("FSR: %s does not export AMD's ffx-api entry points (not AMD's file): skipped", name);
                FreeLibrary(m);
                continue;
            }
            g_ffxDll = m; g_ffx = f; g_ffxDllName = name;
            // The loader holds no effect code, so whether an upscaler stands behind it is asked here; a lone loader gives way to the next file.
            if (i == 0)
            {
                FfxVersions v;
                const int n = QueryFfxVersions(&v);
                // A raise: AMD's code is not called again, FreeLibrary included (it runs the DLL's detach code). The module stays mapped, unused.
                if (g_ffxRaised) { g_ffxDll = nullptr; g_ffx = {}; g_ffxDllName = ""; return false; }
                if (n <= 0)
                {
                    LogRecord("FSR: %s %s: skipped (amd_fidelityfx_upscaler_dx12.dll missing or would not load)",
                        name, n < 0 ? "did not answer the version query" : "offers no upscaler");
                    FreeLibrary(m);
                    g_ffxDll = nullptr; g_ffx = {}; g_ffxDllName = "";
                    continue;
                }
            }
        }
    if (!g_ffxDll)
    {
        LogRecord("FSR: no usable FidelityFX library beside the host or on the search path: FSR cannot run");
        return false;
    }
        // At log=1: which file this session runs, with its full path. The lookup is inside the gate.
    if (g_logLevel >= 1)
    {
        char got[MAX_PATH] = "";
        GetModuleFileNameA(g_ffxDll, got, MAX_PATH);
        LogRecord("FSR: loaded %s", got[0] ? got : g_ffxDllName);
    }
    return true;
}

// AMD's runtime messages, sent to the log.
static void FfxMessage(uint32_t type, const wchar_t* message)
{
    if (!message) return;
    char buf[1024];
    const int n = WideCharToMultiByte(CP_UTF8, 0, message, -1, buf, (int)sizeof(buf) - 1, nullptr, nullptr);
    buf[(n > 0 && n < (int)sizeof(buf)) ? n - 1 : 0] = 0;
        // An ffx-api error is the runtime saying the dispatch is wrong: log=1; a warning log=2. Capped at five, since this callback can fire
    // on every dispatch.
    static int saidFfxError = 0;
    if (type == FFX_API_MESSAGE_TYPE_ERROR)
    {
        if (saidFfxError++ < 5) LogRecord("FFX ERROR: %s%s", buf, saidFfxError == 5 ? "   (the fifth; no more logged)" : "");
    }
    else LogHealth("FFX WARNING: %s", buf);
}

// Create and destroy behind SEH too: the create is where a driver's FSR override or a mismatched DLL first runs AMD's code.
// No C++ object with a destructor may live in these frames (C2712), so they take plain arguments and return plain codes.
static ffxReturnCode_t SafeFfxCreate(ffxCreateContextDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ffxRaised, "FSR")) return (ffxReturnCode_t)0x7FFFFFFF;   // see g_ngxRaised
    __try { return g_ffx.CreateContext(&g_ffxCtx, hd, nullptr); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ffxRaised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}
static void SafeFfxDestroy(DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ffxRaised, "FSR")) return;
    __try { g_ffx.DestroyContext(&g_ffxCtx, nullptr); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ffxRaised = true; }
}

static int WarmUpFsr(const char* name);   // defined after EvaluateFsr, which it runs

// A raise from an FSR 4 first dispatch has meant a D3D12 runtime too old for its shaders (a community build at shader model 6.9 on
// a runtime giving 6.8; the Agility SDK runtime in DLSSIV\D3D12\ fixed it). So a raise from FSR 4, or from an unreadable version,
// says what this runtime gives and how to raise it. Nothing for FSR 3, or for a runtime already at the top of what is asked.
static const char* FsrRaiseHint(const char* name)
{
    static char s[320];
    s[0] = 0;
    const int major = FfxMajor(name);
    if (!(major == 0 || major >= 4) || !g_topSm) return s;
    _snprintf_s(s, sizeof(s), _TRUNCATE, "this D3D12 runtime gives the device shader model %d.%d%s",
        g_topSm >> 4, g_topSm & 0xF, g_topSm < 0x69
            ? ", and shaders that need more raise exactly like this: a 64-bit D3D12Core.dll from Microsoft's"
              " Agility SDK in " DLSSIV_HOST_DIR "\\D3D12\\ raises what the runtime offers" : "");
    return s;
}

#include "fsr4_test.h"   // the FSR 4 start-up test: where the rule would pass FSR 4 by, a copy of this helper tries it once
#include "xess_host.h"   // Intel XeSS, the third upscaler, with its own raise latch; this file has only its call sites
static bool CreateFsrContext(UINT w, UINT hh, UINT outW, UINT outH, uint32_t flags)
{
    if (!g_ffxDll) return false;
        // After a raise, nothing: AMD's library is never called again, and RefusedAfterRaise has said so once.
    if (g_ffxRaised) return false;

        // The version list: what this DLL offers on this GPU (QueryFfxVersions says when it is asked).
    FfxVersions v = {};
    ReadTopShaderModel();                    // this device's, at any log level: the pick below reads it
    g_fsr4SkipSm = 0;                        // said again by the context made here, or not at all
    const bool smLow = g_fsrVersion == 0 && g_topSm > 0 && g_topSm < g_fsr4MinSm;
#if DLSSIV_DEV
    if (g_fsr4MinSm != FEED_FSR4_MIN_SM)
        LogRecord("FSR: DEVELOPMENT: the FSR 4 line is moved to shader model %d.%d (--fsr4_min_sm=%d); a release has it at %d.%d",
            g_fsr4MinSm >> 4, g_fsr4MinSm & 0xF, g_fsr4MinSm, FEED_FSR4_MIN_SM >> 4, FEED_FSR4_MIN_SM & 0xF);
#endif
    const int offered = (g_logLevel >= 1 || g_fsrVersion != 0 || smLow) ? QueryFfxVersions(&v) : -2;   // -2: not asked
    if (g_ffxRaised) return false;           // the query raised, and said so with its code
    bool haveList = offered > 0;
    if (g_logLevel >= 1)
    {
        char list[256] = "";
        size_t used = 0;
        for (int i = 0; i < (int)v.n && used < sizeof(list); i++)
        {
            const int k = _snprintf_s(list + used, sizeof(list) - used, _TRUNCATE, "%s\"%s\"", i ? ", " : "", FfxVerName(v, i));
            if (k < 0) break;
            used += (size_t)k;
        }
        if (haveList)          LogRecord("FSR: %s offers %s on this GPU", g_ffxDllName, list);
        else if (offered == 0) LogRecord("FSR: %s offers no upscaler versions on this GPU: its own pick is tried", g_ffxDllName);
        else                   LogRecord("FSR: %s did not answer the version query: its own pick is tried", g_ffxDllName);
    }

        // fsr_version: the newest offered version with that major number. Not offered is a line and the DLL's own pick, never a refusal.
    int pick = -1;
    if (g_fsrVersion != 0 && haveList)
        for (int i = 0; i < (int)v.n; i++)
            if (FfxMajor(v.names[i]) == g_fsrVersion && (pick < 0 || FfxNewer(v.names[i], v.names[pick])))
                pick = i;
    if (g_fsrVersion != 0)
    {
        if (pick >= 0)     LogRecord("FSR: fsr_version=%d asks for \"%s\"", g_fsrVersion, FfxVerName(v, pick));
        else if (haveList) LogRecord("FSR: fsr_version=%d asks for FSR %d, which %s does not offer on this GPU: its own pick is tried",
                               g_fsrVersion, g_fsrVersion, g_ffxDllName);
                // With no list nothing is known about what the GPU is offered, so the line does not say it.
        else               LogRecord("FSR: fsr_version=%d cannot be matched (%s gave no version list): its own pick is tried", g_fsrVersion, g_ffxDllName);
    }
        // Automatic on a D3D12 too old for FSR 4 (FsrBelowFsr4): the newest FSR 3 is named, as fsr_version=3 would, and the versions passed
    // by are marked so the retry loop cannot reach them. Not when FSR 4 already ran in a copy of this helper (fsr4_test.h).
    int below = haveList ? FsrBelowFsr4(g_fsrVersion, g_topSm, g_fsr4MinSm, v.names, (int)v.n) : -1;
    if (below >= 0 && Fsr4TestSaysRun(w, hh, outW, outH, flags)) below = -1;
    if (below >= 0)
    {
        pick = below;
        g_fsr4SkipSm = g_topSm;
        LogRecord("FSR: D3D12 here offers shader model %d.%d, below FSR 4's: automatic runs \"%s\"",
            g_topSm >> 4, g_topSm & 0xF, FfxVerName(v, pick));
    }

    ffxCreateBackendDX12Desc backend = {};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device      = h.dev;

    ffxCreateContextDescUpscale desc = {};
    desc.header.type   = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.header.pNext  = &backend.header;
    desc.flags         = flags;
    desc.maxRenderSize.width  = w;    desc.maxRenderSize.height  = hh;
    desc.maxUpscaleSize.width = outW; desc.maxUpscaleSize.height = outH;
        // Null at log=0, so AMD's runtime does not even format the message (a gate inside the callback would come too late, and this path
    // can fire every frame). g_logLevel is parsed in main before any context is created.
    desc.fpMessage     = (g_logLevel >= 1) ? FfxMessage : nullptr;

        // The override is chained after the backend: desc -> backend -> override. None unless asked: the DLL's own pick is the path AMD's
    // driver-level FSR 4 upgrade is built around.
    ffxOverrideVersion ov = {};
    ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;

        // A failed create tries the next version the DLL offers, in its order, FSR 3 and newer only, so a community FSR 4 that will not
    // create still leaves the FSR 3.1 it carries. A raise ends it at once. A warm-up that fails cleanly counts as a failed create; one
    // that raises, or that the GPU never finishes, ends it (WarmUpFsr).
    DWORD raised = 0;
    ffxReturnCode_t rc = FFX_API_RETURN_ERROR;
    bool tried[8] = {};
    if (below >= 0)
        for (int i = 0; i < (int)v.n; i++) if (FfxMajor(v.names[i]) >= 4) tried[i] = true;
    int attempt = pick;                      // -1: no override, the DLL's own pick
        // The provider of the context that survives the loop, for the line that names it. The name points into the DLL.
    ffxQueryGetProviderVersion pv = {};
    bool known = false;
    for (;;)
    {
        backend.header.pNext = nullptr;
        if (attempt >= 0) { ov.versionId = v.ids[attempt]; backend.header.pNext = &ov.header; tried[attempt] = true; }
        rc = SafeFfxCreate(&desc.header, &raised);
        const char* asked = (attempt >= 0) ? FfxVerName(v, attempt) : "the DLL's own pick";
        if (rc == FFX_API_RETURN_OK && g_ffxCtx)
        {
            // Which provider this context runs, asked at every log level because the warm-up decides on it.
            pv = {};
            pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            DWORD qraised = 0;
            known = g_ffx.Query && SafeFfxQuery(&g_ffxCtx, &pv.header, &qraised) == FFX_API_RETURN_OK && pv.versionName;
            // The provider query raised: this context can be neither used nor destroyed. Dropped, as a raising create is, and said with its code.
            if (qraised)
            {
                LogRecord("FSR: %s RAISED exception 0x%08X%s in its provider query: not called again this session",
                    g_ffxDllName, (unsigned)qraised, g_raiseWhere);
                g_ffxCtx = nullptr;
                return false;
            }

            // FSR 3 is not warmed up unless fsr_warmup=1 asks. A provider whose version is unknown is warmed up: it may be an FSR 4, and an
            // unneeded warm-up costs about 3 ms CPU and 1 ms GPU once, where a needed one missing costs the session its upscaler.
            const int major = known ? FfxMajor(pv.versionName) : 0;
            if (!(g_fsrWarmup > 0 || (g_fsrWarmup < 0 && (major == 0 || major >= 4)))) break;
            const int warmed = WarmUpFsr(known ? pv.versionName : "a provider whose version is unknown");
            if (warmed == 0) { g_fsrResetNext = true; break; }
            if (warmed == 2) { if (g_ffxRaised) g_ffxCtx = nullptr; return false; }
            // Failed cleanly: nothing was submitted (every failing arm of EvaluateFsr aborts before ExecuteCommandLists), so the context is
            // given back at once and the next version tried.
            DWORD draised = 0;
            SafeFfxDestroy(&draised);
            g_ffxCtx = nullptr;
            if (draised)
            {
                LogRecord("FSR ffxDestroyContext RAISED exception 0x%08X%s after a failed warm-up: no FSR context", (unsigned)draised, g_raiseWhere);
                return false;
            }
            if (!haveList) haveList = QueryFfxVersions(&v) > 0;
            if (g_ffxRaised) return false;   // the query raised, and said so with its code
            // By ID, so the DLL's own pick is marked too and is not created again.
            if (known) for (int i = 0; i < (int)v.n; i++) if (v.ids[i] == pv.versionId) tried[i] = true;
            known = false;
        }
        else
        {
            if (raised)
                LogRecord("FSR ffxCreateContext (%s) RAISED exception 0x%08X%s inside AMD's library or the driver (a driver FSR"
                          " override, or a %s not signed by AMD?): no FSR context", asked, (unsigned)raised, g_raiseWhere, g_ffxDllName);
            else
                LogRecord("FSR ffxCreateContext (%s) failed %u (%s)", asked, (unsigned)rc, FfxName(rc));
            g_ffxCtx = nullptr;
            if (raised || g_ffxRaised) return false;
            if (!haveList) haveList = QueryFfxVersions(&v) > 0;  // a failure path: asked whatever the log level
            if (g_ffxRaised) return false;                       // the query raised, and said so with its code
        }
        attempt = -1;
        for (int i = 0; i < (int)v.n; i++)
            if (!tried[i] && FfxMajor(v.names[i]) >= 3) { attempt = i; break; }
        if (attempt < 0) return false;
        LogRecord("FSR: trying \"%s\" instead", FfxVerName(v, attempt));
    }
        // The line that says which FSR runs ("4.x.x" is FSR 4, "3.1.x" FSR 3.1): the provider of the context that survived creation and
    // its warm-up, printed once after both. log=1; the test kit reads it.
    g_fsrMajorInUse = known ? FfxMajor(pv.versionName) : 0;   // fsr4_test.h reads it
    if (known)
        LogRecord("FSR provider in use: \"%s\"  (FSR %d, id 0x%llX, from %s)",
            pv.versionName, FfxMajor(pv.versionName), (unsigned long long)pv.versionId, g_ffxDllName);
    else
        LogRecord("FSR provider: the version query failed: which FSR runs is UNKNOWN");
    LogRecord("FSR context ready: %ux%u -> %ux%u, flags 0x%X%s%s",
        w, hh, outW, outH, flags, (w == outW && hh == outH) ? " (native AA)" : "",
        g_fsrDebugView ? ", FSR DEBUG VIEW ON (fsr_debug_view=1)" : "");
    return true;
}

// FSR'S OWN RCAS IS OFF, always: sharpness=0 means no sharpening on both upscalers, so one cfg value gives one picture.

// The dispatch behind SEH, like NGX's create and evaluate: a fault in AMD's DLL or the driver would otherwise end the process
// silently.
static ffxReturnCode_t SafeFfxDispatch(ffxDispatchDescHeader* hd, DWORD* code)
{
    *code = 0;
    if (RefusedAfterRaise(g_ffxRaised, "FSR")) return (ffxReturnCode_t)0x7FFFFFFF;   // see g_ngxRaised
    __try { return g_ffx.Dispatch(&g_ffxCtx, hd); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_ffxRaised = true; return (ffxReturnCode_t)0x7FFFFFFF; }
}

static bool EvaluateFsr(const FeedFrameMsg& fm)
{
    if (!g_ffxCtx || !BeginCommands()) return false;

    ffxDispatchDescUpscale d = {};
    d.header.type  = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList  = h.list;
        // COMMON: this helper issues no barriers; every shared texture is COMMON with ALLOW_SIMULTANEOUS_ACCESS, which promotes on use and
    // decays to COMMON at every ExecuteCommandLists, keeping the game's Vulkan view valid.
    d.color         = ffxApiGetResourceDX12(h.tex[FEED_COLOR],  FFX_API_RESOURCE_STATE_COMMON);
    d.depth         = ffxApiGetResourceDX12(h.tex[FEED_DEPTH],  FFX_API_RESOURCE_STATE_COMMON);
    d.motionVectors = ffxApiGetResourceDX12(h.tex[FEED_MV],     FFX_API_RESOURCE_STATE_COMMON);
    d.output        = ffxApiGetResourceDX12(h.tex[FEED_OUTPUT], FFX_API_RESOURCE_STATE_COMMON);
    d.jitterOffset.x = fm.jitter_x;
    d.jitterOffset.y = fm.jitter_y;
        // {1,1}: ffx-api divides by the render size internally, and the vectors are already prev-minus-current in render pixels (per AMD's
    // ffx_fsr3upscaler.cpp; the technique page's NDC example is off by a factor of two and a Y sign).
    d.motionVectorScale.x = 1.0f;
    d.motionVectorScale.y = 1.0f;
    d.renderSize.width  = h.width;  d.renderSize.height  = h.height;
    d.upscaleSize.width = h.outW;   d.upscaleSize.height = h.outH;
        // One sharpener: the client's RCAS after the game's tonemap, driven by sharpness= on both upscalers. FSR's own stays off.
    d.enableSharpening = false;
    d.sharpness        = 0.0f;
    d.frameTimeDelta   = (fm.frame_time_ms > 0.0f) ? fm.frame_time_ms : 16.67f;
        // Must exceed zero; zero gives a black frame.
    d.preExposure      = 1.0f;
        // g_fsrResetNext: the first real dispatch after a warm-up. Cleared at the end, only by a submitted dispatch, so a failed first frame
    // does not lose the reset.
    d.reset            = fm.reset != 0 || g_fsrResetNext;
        // Reverse-Z with DEPTH_INVERTED and DEPTH_INFINITE: FSR requires cameraNear == FLT_MAX > cameraFar ("near" labels the depth==0 end,
    // infinity here; "far" carries the finite plane). The game's own far plane (4500 to 25000) must not be sent: DEPTH_INFINITE makes
    // the constant correct.
    d.cameraNear       = FLT_MAX;
    d.cameraFar        = (fm.camera_near > 0.0f) ? fm.camera_near : 0.05f;
    d.cameraFovAngleVertical = (fm.fov_y_rad > 0.0f) ? fm.fov_y_rad : 0.7853981f;
    d.viewSpaceToMetersFactor = 1.0f;   // GTA IV world units are metres
        // No d.flags = 0 here: d is zero-initialised at declaration, and clearing it again would erase flags set in between.
    // FSR's debug view (fsr_debug_view) is ORed last: on FSR 3.1 a 3x3 grid of diagnostic panels around the upscaled picture.
    if (g_fsrDebugView) d.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;

    DWORD raised = 0;
    const ffxReturnCode_t rc = SafeFfxDispatch(&d.header, &raised);
    if (rc != FFX_API_RETURN_OK)
    {
        AbortCommands();
        static int said = 0;
        if (said++ < 5)
            LogRecord("FSR dispatch failed %u (%s)%s0x%08X%s: %ux%u -> %ux%u, jitter %.3f %.3f, reset %u, frame %.2f ms, near %.4f, fov %.4f",
                (unsigned)rc, raised ? "RAISED" : FfxName(rc), raised ? " exception " : ", code ", raised ? (unsigned)raised : (unsigned)rc,
                raised ? g_raiseWhere : "",
                h.width, h.height, h.outW, h.outH, fm.jitter_x, fm.jitter_y, fm.reset, fm.frame_time_ms, fm.camera_near, fm.fov_y_rad);
        if (raised) DeviceRemoved("an FSR dispatch that raised");   // names the reason when the driver took the device
        return false;
    }
        // A removed device surfaces in EndCommands. Zero there means the dispatch was recorded and thrown away: no Output this frame, so
    // report failure rather than release a fence over a stale image.
    const bool submitted = EndCommands() != 0;
    if (submitted) g_fsrResetNext = false;
    return submitted;
}

// FSR 4's first dispatch runs inside the build, before the answer: it can take far longer than the client's 4 s host watch
// (IvtHostWatch; a community FSR 4 build's cold setup is 19 to 22 s), while the client allows a build 60 s.
// It dispatches the blank new set (only the private fence is signalled); g_fsrResetNext makes the first real dispatch reset FSR's
// history. It runs in every build that creates an FSR 4 context. It stays within the client's 60 s, timed from g_buildStartTick:
// with under 10 s of its 55 s budget left it does not start, and the GPU wait is what is left, at most 45 s and at least 1 s.
// Returns 0 warmed up; 1 failed cleanly, nothing submitted (the caller may try another version); 2 failed hard: AMD's library
// raised, the device went, or the GPU did not finish in time.
static int WarmUpFsr(const char* name)
{
    const ULONGLONG kBudgetMs = 55000;             // the client's 60 s, less a margin
    const ULONGLONG t0 = GetTickCount64();
    const ULONGLONG used0 = g_buildStartTick ? t0 - g_buildStartTick : 0;
    if (used0 + 10000 > kBudgetMs)
    {
        LogRecord("FSR: no warm-up for \"%s\": %llu ms of the client's 60 s used: failed, the next version is tried", name, (unsigned long long)used0);
        return 1;
    }
    FeedFrameMsg fm = {};
    fm.reset         = 1;
    fm.jitter_x      = 0.25f;       // AMD: a jitter sequence must never produce the null vector
    fm.jitter_y      = -0.25f;
    fm.frame_time_ms = 16.67f;
    // camera_near and fov_y_rad stay 0: EvaluateFsr substitutes its own defaults for both.
    LogRecord("FSR: warming up \"%s\" (its first dispatch, inside the build)", name);
        // Timed only when there is a log; the budget runs on GetTickCount64 alone.
    LARGE_INTEGER f = {}, q0 = {}, q1 = {}, q2 = {};
    const bool timed = g_logLevel >= 1;
    if (timed) { QueryPerformanceFrequency(&f); QueryPerformanceCounter(&q0); }
    const bool ok = EvaluateFsr(fm);
    if (timed) QueryPerformanceCounter(&q1);
    const double cpuMs = timed ? 1000.0 * (double)(q1.QuadPart - q0.QuadPart) / (double)f.QuadPart : 0.0;
    if (!ok)
    {
        LogRecord("FSR: the warm-up dispatch of \"%s\" FAILED after %.0f ms%s%s%s", name, cpuMs,
            g_ffxRaised ? " by raising" : "; nothing was submitted", g_ffxRaised ? g_raiseWhere : "",
            g_ffxRaised ? ": not called again this session" : "");
        if (g_ffxRaised) { const char* hint = FsrRaiseHint(name); if (hint[0]) LogRecord("FSR: %s", hint); }
        return (g_ffxRaised || DeviceRemoved("the FSR warm-up")) ? 2 : 1;
    }
    const ULONGLONG used1 = g_buildStartTick ? GetTickCount64() - g_buildStartTick : GetTickCount64() - t0;
    const ULONGLONG left  = used1 < kBudgetMs ? kBudgetMs - used1 : 0;
    const DWORD waitMs    = (DWORD)(left > 45000 ? 45000 : (left < 1000 ? 1000 : left));
    if (!WaitFenceValue(h.fence, h.fenceValue, waitMs))
    {
                // The set is kept alive on purpose: the refused build would free the textures under live GPU work. One reference each leaks until
        // exit, and ReleaseSet destroys the context only once the GPU has retired it.
        for (int i = 0; i < FEED_SLOTS; i++) if (h.tex[i]) h.tex[i]->AddRef();
        g_setKept = true;
        LogRecord("FSR: the warm-up of \"%s\" was submitted after %.0f ms, and the GPU had not finished it %lu ms later: build"
                      " refused, its textures kept", name, cpuMs, (unsigned long)waitMs);
        DeviceRemoved("the FSR warm-up");
        return 2;
    }
        // A removed device completes every fence, so a returned wait is not yet a warm-up that ran.
    if (DeviceRemoved("the FSR warm-up")) return 2;
    if (timed)
    {
        QueryPerformanceCounter(&q2);
                // The CPU time spans all of EvaluateFsr (reset, close and execute included), not ffxDispatch alone.
        LogRecord("FSR: warm-up of \"%s\" done: %.0f ms on the CPU, then %.0f ms until the GPU finished", name, cpuMs, 1000.0 * (double)(q2.QuadPart - q1.QuadPart) / (double)f.QuadPart);
    }
    return 0;
}

// Is there a resolve to run: with FSR or XeSS h.feature is null by design, so the back end's own object is asked.
static bool ResolveReady()
{
        // And a set to run it on: a refusal frees the shared textures, and a set is either complete or absent, so all four are tested.
    for (int i = 0; i < FEED_SLOTS; i++) if (!h.tex[i]) return false;
    if (HOST_TRANSPORT_ONLY) return true;
    return (g_backend == 1) ? (g_ffxCtx != nullptr) : (g_backend == 2) ? (g_xessCtx != nullptr) : (h.feature != nullptr);
}

static void ReleaseFsr()
{
    if (g_ffxCtx && g_ffx.DestroyContext) { DWORD raised = 0; SafeFfxDestroy(&raised); if (raised) LogRecord("FSR ffxDestroyContext RAISED exception 0x%08X%s; the context handle is dropped", (unsigned)raised, g_raiseWhere); g_ffxCtx = nullptr; }
        // Not unloaded after a raise: FreeLibrary runs the DLL's detach code, and a library that raised is never called again.
    if (g_ffxDll) { if (!g_ffxRaised) FreeLibrary(g_ffxDll); g_ffxDll = nullptr; }
}

static bool Evaluate(const FeedFrameMsg& fm)
{
    if (!BeginCommands()) return false;
    NVSDK_NGX_D3D12_DLSS_Eval_Params ep = {};
    ep.Feature.pInColor  = h.tex[FEED_COLOR];
    ep.Feature.pInOutput = h.tex[FEED_OUTPUT];
    ep.pInDepth          = h.tex[FEED_DEPTH];
    ep.pInMotionVectors  = h.tex[FEED_MV];
    ep.InJitterOffsetX   = fm.jitter_x;
    ep.InJitterOffsetY   = fm.jitter_y;
    ep.InRenderSubrectDimensions.Width  = h.width;
    ep.InRenderSubrectDimensions.Height = h.height;
    ep.InReset           = (int)fm.reset;
    ep.InMVScaleX        = h.mvScaleX;
    ep.InMVScaleY        = h.mvScaleY;
    ep.InPreExposure     = 1.0f;
    ep.InExposureScale   = 1.0f;
        // The game's real frame period; zero when the client did not measure it.
    ep.InFrameTimeDeltaInMsec = fm.frame_time_ms;
    DWORD code = 0;
    const NVSDK_NGX_Result re = SafeEvaluate(&ep, &code);
    if (code != 0) { AbortCommands(); LogRecord("evaluate raised 0x%08X%s (caught; nothing submitted)", code, g_raiseWhere); return false; }
    const UINT64 ev = EndCommands();
    if (NVSDK_NGX_FAILED(re)) { static int said = 0; if (said++ < 5) LogRecord("evaluate failed 0x%08X (%s)", (unsigned)re, NgxName(re)); return false; }
        // NGX can report success while a failed Close kept its commands from running: report the frame failed.
    return ev != 0;
}

// The transport test: the left half of Color into Output, no NGX.
static bool TransportCopy()
{
    if (!BeginCommands()) return false;
    D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
    src.pResource = h.tex[FEED_COLOR];  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = h.tex[FEED_OUTPUT]; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_BOX box = { 0, 0, 0, h.width / 2, h.height, 1 };
    h.list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        // Zero: recorded, never submitted, so Output is stale. A failed frame.
    return EndCommands() != 0;
}

// ---- The shared set ----

static ID3D12Resource* MakeSharedTex(UINT w, UINT h_, DXGI_FORMAT fmt, bool uav, HANDLE* outHandle)
{
    *outHandle = nullptr;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width            = w;
    rd.Height           = h_;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | (uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    ID3D12Resource* t = nullptr;
    HRESULT hr = h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&t);
    if (SUCCEEDED(hr)) hr = h.dev->CreateSharedHandle(t, nullptr, GENERIC_ALL, nullptr, outHandle);
    if (FAILED(hr)) { LogRecord("shared texture %ux%u fmt %u failed 0x%08X", w, h_, (unsigned)fmt, (unsigned)hr); if (t) t->Release(); return nullptr; }
    return t;
}

// The Output is written through a UAV: a typed store to BGRA8 is not
// guaranteed; where the device lacks it the Output becomes RGBA8 and the
// client blits home (it checks the format the ack names).
static DXGI_FORMAT ResolveOutputFormat(DXGI_FORMAT want)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = { want };
    if (SUCCEEDED(h.dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) &&
        (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)) return want;
    LogRecord("typed UAV store to format %u is not supported: the Output is R8G8B8A8_UNORM", (unsigned)want);
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

// Returns false, having freed nothing, when the drain times out: nothing is freed under live GPU work.
#if DLSSIV_FG
static void FgTeardown();   // fg_host.h: frame generation's context and images go with the set they read from
#endif
static bool ReleaseSet()
{
        // The drain asks about submitted work, not back ends: EndCommands signals h.fence on every submission from every path (evaluate,
    // FSR dispatch, TransportCopy, feature create), so one wait on h.fenceValue covers them all, transport-only included.
    if (h.fenceValue && !WaitFenceValue(h.fence, h.fenceValue, 2000))
    {
        LogRecord("rebuild: the previous frame's work did not retire in 2 s: the shared set and the upscaler are kept, not freed"
                          " under live GPU work");
        return false;
    }
#if DLSSIV_FG
    FgTeardown();            // behind the drain, before the guides its context prepares from are released (no-op without a set)
#endif
    if (h.feature) { SafeRelease(h.feature); h.feature = nullptr; }
        // The FSR context is sized at creation (maxRenderSize, maxUpscaleSize), so a rebuild at another size drops it.
    if (g_ffxCtx && g_ffx.DestroyContext) { DWORD raised = 0; SafeFfxDestroy(&raised); if (raised) LogRecord("FSR ffxDestroyContext RAISED exception 0x%08X%s; the context handle is dropped", (unsigned)raised, g_raiseWhere); g_ffxCtx = nullptr; }
    ReleaseXessContext();    // XeSS's context is sized to the set's output too (xess_host.h); nothing without one
    for (int i = 0; i < FEED_SLOTS; i++) if (h.tex[i]) { h.tex[i]->Release(); h.tex[i] = nullptr; }
    return true;
}

// A refused build must not keep its slots: the four textures are allocated before the late refusals (no NGX, no FidelityFX library,
// CreateFsrContext or CreateFeature failed), and after three refusals the client stops building, so the set would stay all session.
// Releasing the helper's pointer is not enough: each slot was duplicated into the game as an NT handle, and the client does not
// close the handles of a refused ack. So the refusal sends no handles and closes the ones it made, with DuplicateHandle's
// DUPLICATE_CLOSE_SOURCE (null target process). gameTex is zeroed here so the client is never handed a closed value.
// No drain is needed: only the FSR warm-up submits on the new set, and every way out of it is safe (a clean failure submitted
// nothing, a success waited, a removed device completes every fence, an unfinished one AddRef'd each texture and set g_setKept).
static void ReleaseRefusedSet(HANDLE hgame, uint64_t* gameTex)
{
    int freed = 0;
    for (int i = 0; i < FEED_SLOTS; i++)
    {
        if (gameTex[i])
        {
            HANDLE remote = (HANDLE)(uintptr_t)gameTex[i];
            if (!DuplicateHandle(hgame, remote, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE))
                LogRecord("a refused build's slot %d handle could not be closed in the game (%lu): held until the game exits", i, GetLastError());
            gameTex[i] = 0;
        }
        if (h.tex[i]) { h.tex[i]->Release(); h.tex[i] = nullptr; freed++; }
    }
    if (freed && g_setKept)
        LogRecord("build refused: the %d shared texture%s already created %s KEPT until the helper exits",
                  freed, freed == 1 ? "" : "s", freed == 1 ? "is" : "are");
    else if (freed)
        LogRecord("build refused: the %d shared texture%s already created %s released here",
                  freed, freed == 1 ? "" : "s", freed == 1 ? "is" : "are");
    g_setKept = false;
}

// ---- The pipe ----

static bool ReadFull(HANDLE pipe, void* buf, DWORD len)
{
    BYTE* p = (BYTE*)buf;
    while (len)
    {
        DWORD got = 0;
        if (!ReadFile(pipe, p, len, &got, nullptr) || got == 0) return false;
        p += got; len -= got;
    }
    return true;
}

static bool WriteFull(HANDLE pipe, const void* buf, DWORD len)
{
    DWORD put = 0;
    return WriteFile(pipe, buf, len, &put, nullptr) && put == len;
}

// The orphan watchdog: waits on the game's process handle and exits when it ends. Serve() blocks in ConnectNamedPipe, and a game
// killed before connecting (End Task skips DLL_PROCESS_DETACH) would leave this process holding a D3D12 device for ever; pipe names
// carry the game's pid, so orphans would accumulate. A thread rather than an overlapped connect: ReadFull and WriteFull pass a NULL
// OVERLAPPED, which a FILE_FLAG_OVERLAPPED pipe forbids.
static DWORD WINAPI OrphanWatchdog(LPVOID param)
{
    HANDLE hgame = (HANDLE)param;
    WaitForSingleObject(hgame, INFINITE);
    LogRecord("the game process exited: exiting");
    ExitProcess(0);            // deliberate: GPU resources held and nothing left to serve
    return 0;
}

// The back end from the adapter, run again after a rebind. Automatic resolves here, once InitDevice has named the adapter.
// The test is whether NGX can run DLSS (SuperSampling.Available), not the vendor: a GTX 1080 is NVIDIA and cannot. On a non-NVIDIA
// adapter NGX is not loaded. NGX absent is not fatal: transport-only needs none, and a build that needs it is refused with a reason.
static void ResolveBackendAndProbe()
{
    g_backend = g_backendCli;
    if (g_backend < 0)
    {
                // On an Intel card XeSS, and FSR when XeSS cannot run there; the handshake's XeSS bits say why (xess_host.h).
        if (XESS_AUTO_INTEL(h.vendorId))
        {
            g_backend = InitXess() ? 2 : 1;
            LogRecord("backend AUTO -> %s (Intel; XeSS %s%s)", g_backend == 2 ? "XeSS" : "FSR",
                g_backend == 2 ? "can run here" : "cannot run here: ", g_backend == 2 ? "" : XessWhyText());
        }
        else if (h.vendorId != 0x10DE)
        {
            g_backend = 1;
            LogRecord("backend AUTO -> FSR (adapter vendor 0x%04X, not NVIDIA; NGX not tried)", h.vendorId);
        }
        else
        {
            h.ngxOk = InitNgx(g_exeDirW);
            h.ngxTried = true;
            g_backend = h.ngxOk ? 0 : 1;
            LogRecord("backend AUTO -> %s (NVIDIA; DLSS %s)",
                g_backend == 1 ? "FSR" : "DLSS",
                h.ngxOk ? "is available"
                : h.ngxNoFile    ? "is NOT available: " DLSSIV_HOST_DIR "\\nvngx_dlss.dll is missing"
                 : h.ngxDriverOld ? "is NOT available: the NVIDIA driver is too old"
                                  : "is NOT available");
        }
    }

    if (g_backend == 0)
    {
        if (!h.ngxTried) { h.ngxOk = InitNgx(g_exeDirW); h.ngxTried = true; }
        if (!h.ngxOk)
            LogRecord("NGX is NOT available: serving anyway (a build that needs it is refused)");
    }
    else if (g_backend == 2)
    {
                // XeSS, named (upscaler=2) or chosen above. Not fatal either: a build that needs it is refused with the reason.
        if (!g_xessTried) InitXess();
        if (g_backendCli == 2 && !g_xessOk)
            LogRecord("XeSS is selected (upscaler=2) and cannot run here (%s): builds that need it are refused", XessWhyText());
    }
    else
    {
        if (g_backendCli == 1)
            LogRecord("backend is FSR (upscaler=1), so NGX %s", h.ngxTried ? "was tried and is not available here" : "was not initialised");
                // Not fatal: the pipe still opens and a build that needs FSR is refused with a reason.
        g_ffxTried = true;
                // InitFsr has already said why, naming the file.
        if (!InitFsr(g_exeDir))
            LogRecord("FSR is selected but has no usable library: builds that need it are refused");
    }
}

// Everything built on the device, given back in reverse order. Reachable only before the first build, before anything shared exists.
static void ReleaseDevice()
{
#if DLSSIV_DEV
    ServeTimerRelease();   // the serve timer lives on this device
#endif
    if (h.params)    { NVSDK_NGX_D3D12_DestroyParameters(h.params); h.params = nullptr; }
    if (h.ngxInited && !g_ngxRaised) { NVSDK_NGX_D3D12_Shutdown1(h.dev); }   // never into a library that raised
    h.ngxInited = false;
    h.ngxTried = false; h.ngxOk = false; h.ngxDriverOld = false; h.ngxNoFile = false;
    ReleaseFsr(); g_ffxTried = false;
    ReleaseXess();           // XeSS's contexts and file, asked again on the next device (xess_host.h)
    for (int i = 0; i < Host::kFrames; i++) { if (h.alloc[i]) { h.alloc[i]->Release(); h.alloc[i] = nullptr; } h.allocFence[i] = 0; }
    h.slot = 0; h.fenceValue = 0;
    if (h.list)       { h.list->Release();  h.list  = nullptr; }
    if (h.fence)      { h.fence->Release(); h.fence = nullptr; }
    if (h.fenceEvent) { CloseHandle(h.fenceEvent); h.fenceEvent = nullptr; }
    if (h.queue)      { h.queue->Release(); h.queue = nullptr; }
    if (h.dev)        { h.dev->Release();   h.dev   = nullptr; }
    h.vendorId = 0; g_luidValid = false; memset(g_luid, 0, sizeof(g_luid)); g_adapterHow = "";
}

// The game is on another GPU than the one guessed: rebuild the device on the game's adapter by LUID and resolve the back end again
// (an AMD iGPU had resolved automatic to FSR for a game on an NVIDIA dGPU). If the LUID gives no device the caller refuses with the
// reason; the shared textures are not cross-adapter. Costs about a second on the render thread.
static void LogD3D12Runtime();   // defined beside main, with the runtime opt-in it reports on

static bool RebindAdapter(const unsigned char* luid)
{
    const ULONGLONG t0 = GetTickCount64();
    ReleaseDevice();
    if (!InitDevice(luid)) { ReleaseDevice(); LogRecord("rebuilding on the game's adapter FAILED: no D3D12 device"); return false; }
    if (!g_luidValid || memcmp(g_luid, luid, sizeof(g_luid)) != 0)
    {
                // The device came up on the wrong GPU: release it so the process ends clean.
        ReleaseDevice();
        LogRecord("rebuilding on the game's adapter FAILED: the device came up on a different adapter again"); return false;
    }
    ResolveBackendAndProbe();
    LogRecord("rebuilt on the game's adapter in %llu ms", (unsigned long long)(GetTickCount64() - t0));
    if (g_logLevel >= 1) LogD3D12Runtime();  // the device FSR will run on
    return true;
}

// The last line before a crash: code, address, module and offset; then the search continues so Windows still records the crash.
static LONG WINAPI HostUnhandled(EXCEPTION_POINTERS* ep)
{
    DWORD code = 0; void* at = nullptr;
    if (ep && ep->ExceptionRecord) { code = ep->ExceptionRecord->ExceptionCode; at = ep->ExceptionRecord->ExceptionAddress; }
    char mod[MAX_PATH] = "?"; HMODULE m = nullptr;
    if (at && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) && m)
        GetModuleFileNameA(m, mod, MAX_PATH);
    LogRecord("CRASH: unhandled exception 0x%08X at %p, %s+0x%llX%s", (unsigned)code, at, mod,
        m ? (unsigned long long)((const BYTE*)at - (const BYTE*)m) : 0ull,
        g_deviceRemoved ? " (the D3D12 device had already been removed)" : "");
    return EXCEPTION_CONTINUE_SEARCH;
}

// Frame generation, behind its switch: the wire's packed structs (constants and rules in feed_ipc.h), then the helper's state and
// its calls into AMD's library (fg_host.h says what it may hold). The development arm and its desk tools after them.
#if DLSSIV_FG
#include "../source/transport/feed_fg.h"
#include "fg_host.h"
#endif
#if DLSSIV_DEV
#include "fg_cost.h"    // the cost marks (--log=2 or --gpu_probe=1; --fgcost_* flags), development builds only
#include "fg_lib.h"     // library load (--fglib*), module identity (--fgid), debug layer (--d3d12_debug), AMD's checks (--fgcheck); development only
#include "fg_own.h"     // the own arm: the mod's own library through its own interface (--fgown*), development builds only
#include "fg_cap.h"     // the capture: short runs of frames with all the library was given, for tools\fg-replay (--fgcap*), development builds only
#endif
#if DLSSIV_FG && !DLSSIV_DEV
#include "fg_own.h"     // shipped frame generation: the own arm alone, dlssiv-fg.dll beside this helper held to its record (fg_own.h)
#endif

// Adding any line to Serve reallocates its registers, so its per-frame 'F' block is held to the same work, not the same bytes,
// by tools/prove-fg-off.py (@samework). Expect that proof to judge any line added here.
static int Serve(DWORD gamePid)
{
    // Armed before the pipe exists, so the blocking connect below is covered.
    if (HANDLE hwatch = OpenProcess(SYNCHRONIZE, FALSE, gamePid))
    {
        if (HANDLE t = CreateThread(nullptr, 0, OrphanWatchdog, hwatch, 0, nullptr)) CloseHandle(t);
        else { LogRecord("could not start the orphan watchdog (%lu)", GetLastError()); CloseHandle(hwatch); }
    }
    else
    {
        LogRecord("no SYNCHRONIZE handle to game pid %lu (%lu): it is already gone", (unsigned long)gamePid, GetLastError());
        return 1;
    }

    char name[128];
    _snprintf_s(name, sizeof(name), _TRUNCATE, FEED_PIPE_FMT, (unsigned long)gamePid);
        // Robustness against a broken peer, not security. FILE_FLAG_FIRST_PIPE_INSTANCE fails if the name is held (most likely an orphaned
    // helper of the same pid) rather than becoming a second instance. PIPE_REJECT_REMOTE_CLIENTS: local IPC only.
    HANDLE pipe = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                   1, FEED_PIPE_BYTES, FEED_PIPE_BYTES, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        LogRecord("CreateNamedPipe failed %lu%s", e,
            e == ERROR_ACCESS_DENIED ? ": the name is taken (another host serves this game pid)" : "");
        return 1;
    }
    LogHealth("serving on %s", name);
        // Only the game may hold the one instance: the default descriptor lets any local account open the pipe, so a client that is not the
    // game is dropped and the pipe waits again. Windows path only (sync=0); resolved at run time so the imports do not change.
    typedef BOOL (WINAPI* PipeClientPidFn)(HANDLE, PULONG);
    const PipeClientPidFn pipeClientPid = g_sync == 0
        ? (PipeClientPidFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetNamedPipeClientProcessId") : nullptr;
    for (;;)
    {
        if (!ConnectNamedPipe(pipe, nullptr))
        {
            const DWORD ce = GetLastError();
            // A client that opened and closed before this call (ERROR_NO_DATA) is not the game: free the instance and wait again.
            if (ce == ERROR_NO_DATA) { LogRecord("a client opened the pipe and closed it before it could be checked: waiting for the game"); DisconnectNamedPipe(pipe); continue; }
            if (ce != ERROR_PIPE_CONNECTED) { LogRecord("ConnectNamedPipe failed %lu", ce); return 1; }
        }
        ULONG peer = 0;
        if (pipeClientPid && pipeClientPid(pipe, &peer) && peer != (ULONG)gamePid)
        {
            LogRecord("a process that is not the game (pid %lu) opened the pipe: dropped, waiting for the game", peer);
            DisconnectNamedPipe(pipe);
            continue;
        }
        break;
    }

    FeedHello hello = {};
    if (!ReadFull(pipe, &hello, FEED_HELLO_V1_SIZE) || hello.magic != FEED_IPC_MAGIC) { LogRecord("bad hello"); return 1; }
    if (hello.version >= 2 && !ReadFull(pipe, &hello.client_kind, sizeof(hello.client_kind))) return 1;
    if (hello.version >= 4 && !ReadFull(pipe, &hello.self_process, sizeof(hello.self_process))) return 1;
    if (hello.version >= 14 && !ReadFull(pipe, &hello.luid, sizeof(hello.luid))) return 1;
    if (hello.version >= 15 && !ReadFull(pipe, &hello.build, sizeof(hello.build))) return 1;
    hello.build[sizeof(hello.build) - 1] = 0;
        // The two halves must be one build: the protocol version cannot say both binaries came from the same source. Refused after the ack,
    // so the client reads a sentence. Only when both ids prove something: hand builds carry the unknown-source form with their own clock.
    bool buildMismatch = false;
    if (hello.version == FEED_IPC_VERSION && strcmp(hello.build, IVT_BUILD_ID) != 0)
    {
        const bool mineProvable   = strncmp(IVT_BUILD_ID, "unknown-source", 14) != 0;
        const bool theirsProvable = strncmp(hello.build,  "unknown-source", 14) != 0;
        if (mineProvable && theirsProvable)
        {
            LogRecord("REFUSING: the game's DLSS-IV.asi is build %s, this host is build " IVT_BUILD_ID, hello.build);
            buildMismatch = true;
        }
        else
            LogRecord("the two halves carry different ids (%s there, " IVT_BUILD_ID " here), at least one without a commit id: not refused", hello.build);
    }
        // The game's GPU, or nothing: compared here, before the ack, since the ack's capability word describes the device a rebind changes.
    // An all-zero LUID (deviceLUIDValid false) has nothing to compare. Only for a client of this exact protocol.
    bool refuse = false;
    if (hello.version == FEED_IPC_VERSION)
    {
        static const unsigned char kNoLuid[8] = {};
        const bool gameSaid = (memcmp(hello.luid, kNoLuid, sizeof(kNoLuid)) != 0);
        char la[20], lb[20];
        if (!gameSaid)
            LogRecord("the game reported no adapter LUID: GPU match not checked (adapter chosen by %s)", g_adapterHow);
        else if (!g_luidValid)
            LogRecord("this host never recorded its own adapter LUID: the GPU match is not checked");
        else if (memcmp(hello.luid, g_luid, sizeof(g_luid)) != 0)
        {
            // Not rebuilt for a session already refused (a rebind costs about a second on the game's render thread); the line still says both.
            if (buildMismatch)
                LogRecord("the game renders on adapter LUID %s, this host started on %s (chosen by %s): not rebuilding (the build"
                              " mismatch above refuses the session)", LuidStr(hello.luid, la), LuidStr(g_luid, lb), g_adapterHow);
            else
            {
                LogRecord("the game renders on adapter LUID %s, this host started on %s (chosen by %s): rebuilding on the game's adapter", LuidStr(hello.luid, la), LuidStr(g_luid, lb), g_adapterHow);
#if DLSSIV_FG
                // Frame generation's reference to AMD's loader goes with the device, and its offer is asked again on the game's adapter.
                // Both do nothing without --fg=1.
                FgUnload();
#endif
                if (!RebindAdapter(hello.luid))
                {
                    LogRecord("REFUSING: the game renders on adapter LUID %s and this host could not create its D3D12 device there",
                                  LuidStr(hello.luid, la));
                    refuse = true;
                }
#if DLSSIV_FG
                else FgProbe();
#endif
            }
        }
        else
            LogRecord("same GPU as the game");
    }
        // What this helper can do, answered at the handshake. h.ngxTried and g_ffxDll are settled in main, and again by the rebind above.
    FeedHelloAck hack = { FEED_IPC_MAGIC, FEED_IPC_VERSION, 0 };
    if (h.ngxTried) { hack.caps |= FEED_CAP_DLSS_PROBED; if (h.ngxOk) hack.caps |= FEED_CAP_DLSS; if (!h.ngxOk && h.ngxDriverOld) hack.caps |= FEED_CAP_DLSS_DRIVER;
                      if (!h.ngxOk && h.ngxNoFile) hack.caps |= FEED_CAP_DLSS_NOFILE; }
    if (g_ffxTried) { hack.caps |= FEED_CAP_FSR_PROBED;  if (g_ffxDll) hack.caps |= FEED_CAP_FSR; }
    hack.caps |= XessCaps();                 // 16384 asked, 32768 can run, or one of the three reasons (xess_host.h); zero when not asked
#if DLSSIV_FG
    hack.caps |= FgOffer();                  // 256 "asked" and 512 "a usable provider answered" (feed_ipc.h); zero without --fg=1
#endif
        // A refusal is said in the ack: the rebind's refusal tore the device down, so the PROBED bits alone would read as a helper that exited.
    if (refuse) hack.caps |= FEED_CAP_REFUSED;
    if (buildMismatch) hack.caps |= FEED_CAP_BUILD_MISMATCH;
    WriteFull(pipe, &hack, sizeof(hack));
        // The ack must arrive before the pipe closes: DisconnectNamedPipe discards unread data, and FlushFileBuffers returns once the client
    // has read it or is gone.
    if (refuse || buildMismatch) { FlushFileBuffers(pipe); return 1; }   // after the ack, so the client reads a refusal rather than a broken handshake
    if (hello.version != FEED_IPC_VERSION) { LogRecord("the client speaks protocol v%u, this host v%u", hello.version, FEED_IPC_VERSION); return 1; }
    if (!FeedHostCreatesTextures(hello.client_kind)) { LogRecord("only Vulkan/GL clients (host-created textures) are served here"); return 1; }
        // The client must be the game this helper was spawned for (the pipe name carries that pid, and handles are duplicated into it).
    if (hello.pid != gamePid)
    {
        LogRecord("the client says it is pid %u, this host was started for pid %lu: refusing", hello.pid, (unsigned long)gamePid);
        return 1;
    }
        // The actual client kind is printed. The trust surface belongs in feed_ipc.h, where the protocol is defined.
    LogHealth("game pid %u connected (protocol v%u, %s client)", hello.pid, hello.version,
        hello.client_kind == FEED_CLIENT_VULKAN ? "Vulkan"
        : hello.client_kind == FEED_CLIENT_GL   ? "OpenGL"
        : hello.client_kind == FEED_CLIENT_D3D11 ? "D3D11" : "unknown");

    HANDLE hgame = nullptr;
    if (hello.self_process)
    {
        HANDLE h2 = (HANDLE)(uintptr_t)hello.self_process;
        if (GetProcessId(h2) == hello.pid) hgame = h2;
    }
    if (!hgame) hgame = OpenProcess(PROCESS_DUP_HANDLE, FALSE, hello.pid);
    if (!hgame) { LogRecord("no handle to the game process (%lu)", GetLastError()); return 1; }

    HANDLE hin = nullptr, hout = nullptr;
    HANDLE gameIn = nullptr, gameOut = nullptr;
    if (g_sync == 0)
    {
        h.dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&h.fenceIn);
        h.dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&h.fenceOut);
        if (!h.fenceIn || !h.fenceOut ||
            FAILED(h.dev->CreateSharedHandle(h.fenceIn, nullptr, GENERIC_ALL, nullptr, &hin)) ||
            FAILED(h.dev->CreateSharedHandle(h.fenceOut, nullptr, GENERIC_ALL, nullptr, &hout)))
        { LogRecord("shared fence creation failed"); return 1; }
        DuplicateHandle(GetCurrentProcess(), hin, hgame, &gameIn, 0, FALSE, DUPLICATE_SAME_ACCESS);
        DuplicateHandle(GetCurrentProcess(), hout, hgame, &gameOut, 0, FALSE, DUPLICATE_SAME_ACCESS);
    }
    else
        LogRecord("sync=%d: the game creates the two semaphores, this host opens them at the first build", g_sync);

    UINT64 evaluated = 0, failed = 0;
        // Frames this session, never reset (evaluated restarts at every build); the summary line prints this against failed.
    UINT64 evaluatedSession = 0;
    int    failedInARow = 0;                 // evaluates failed since the last one that worked (see the loop)
        // The reader takes a frame's tag and body in one ReadFile where it can (pipe_reader.h). The handshake above read exactly.
    HostPipeReader rd; HostPipeReaderInit(&rd, pipe);
    HOST_DEV(UINT64 messages = 0;)                     // tags read, beside rd.fills on the session line
        // Who ended the session: this helper's own breaks (a raise, three failed evaluates, device removal, the fences, an unknown tag) say
    // so; every pipe read or write failure is the game's. Test kits match the whole phrase.
    const char* endedBy = "the game";
    for (;;)
    {
        BYTE tag = 0;
        if (!HostPipeRead(&rd, &tag, 1)) break;   // pipe closed: the game is done
        HOST_DEV(messages++;)
        if (tag == 'B')
        {
            FeedBuild b = {};
            if (!HostPipeRead(&rd, &b, sizeof(b))) break;
            g_buildStartTick = GetTickCount64();   // the client's 60 s for this answer start about now (WarmUpFsr)
            if (g_sync != 0 && !h.fenceIn && !OpenGameSemaphores(b.sem_in, b.sem_out)) { endedBy = "this helper"; break; }   // once a connection; no fences at all ends it
            LogHealth("build: %ux%u color %u output %u hdr %d inverted %d transport %d target %ux%u flags %d",
                b.width, b.height, b.color_fmt, b.output_fmt, b.hdr, b.depth_inverted, b.transport, b.target_width, b.target_height, b.flags_override);
                        // Every dimension from the wire is bounded before it sizes an allocation: 16384 is D3D12's maximum 2D size, 16 a sane floor.
            // A refusal is a normal failed build ack.
            {
                const UINT32 kMinDim = 16u, kMaxDim = 16384u;
                const bool badRender = b.width  < kMinDim || b.width  > kMaxDim
                                    || b.height < kMinDim || b.height > kMaxDim;
                // The target pair is allowed to be 0/0, which means "no upscale".
                const bool haveTarget = (b.target_width != 0u) || (b.target_height != 0u);
                const bool badTarget = haveTarget && (b.target_width  < kMinDim || b.target_width  > kMaxDim
                                                   || b.target_height < kMinDim || b.target_height > kMaxDim);
                if (badRender || badTarget)
                {
                    LogRecord("build REFUSED: dimensions out of range (%ux%u -> %ux%u); this host accepts %u..%u",
                        b.width, b.height, b.target_width, b.target_height, kMinDim, kMaxDim);
                    FeedBuildAck bad = {};   // ok = 0; the client logs "host build FAILED"
                    if (g_backend == 1) bad.flags |= FEED_ACK_BACKEND_FSR;   // the client's three-strike text names the back end from this
                    if (g_backend == 2) bad.flags |= FEED_ACK_BACKEND_XESS;
                    bad.flags |= AckFsrBits();
                    WriteFull(pipe, &bad, sizeof(bad));
                    continue;
                }
            }
            if (!ReleaseSet())
            {
                // Nothing was freed: the retained set matches the old dimensions. The ack must still be written, or the client waits 60 s.
                FeedBuildAck bad = {};
                if (g_backend == 1) bad.flags |= FEED_ACK_BACKEND_FSR;
                if (g_backend == 2) bad.flags |= FEED_ACK_BACKEND_XESS;
                bad.flags |= AckFsrBits();
                WriteFull(pipe, &bad, sizeof(bad));
                continue;
            }
            h.width = b.width; h.height = b.height;
            HOST_DEV(h.transportOnly = b.transport != 0;)
            h.mvScaleX = b.mv_scale_x; h.mvScaleY = b.mv_scale_y;
            h.colorFmt = (DXGI_FORMAT)b.color_fmt;
            h.outputFmt = HOST_TRANSPORT_ONLY ? (DXGI_FORMAT)b.output_fmt : ResolveOutputFormat((DXGI_FORMAT)b.output_fmt);
            const bool wantSr = b.target_width && b.target_height && (b.target_width != b.width || b.target_height != b.height) && !HOST_TRANSPORT_ONLY;
            h.outW = wantSr ? b.target_width : b.width; h.outH = wantSr ? b.target_height : b.height;
            bool ok = true, srUnavailable = false;
            // NGX only: FSR takes the render and output sizes directly, and under it NGX was never initialised.
            if (g_backend == 0 && wantSr && !PickSrQuality(b.width, b.height, h.outW, h.outH))
            { LogRecord("NGX reported no DLSS mode for %ux%u -> %ux%u", b.width, b.height, h.outW, h.outH); srUnavailable = true; ok = false; }

            uint64_t gameTex[FEED_SLOTS] = {};
            if (ok)
            {
                // Per slot, indexed by the slot enum: the table needs exactly FEED_SLOTS rows, or the tail gets DXGI_FORMAT_UNKNOWN.
                struct SlotSpec { UINT w, hh; DXGI_FORMAT fmt; bool uav; };
                const SlotSpec spec[FEED_SLOTS] = {
                    { b.width, b.height, h.colorFmt,               false },  // COLOR
                    { h.outW,  h.outH,   h.outputFmt,              true  },  // OUTPUT
                    { b.width, b.height, DXGI_FORMAT_R32_FLOAT,    false },  // DEPTH
                    { b.width, b.height, DXGI_FORMAT_R16G16_FLOAT, false },  // MV
                };
                for (int i = 0; i < FEED_SLOTS && ok; i++)
                {
                    HANDLE local = nullptr;
                    h.tex[i] = MakeSharedTex(spec[i].w, spec[i].hh, spec[i].fmt, spec[i].uav, &local);
                    if (!h.tex[i]) { ok = false; break; }
                    HANDLE remote = nullptr;
                    if (!DuplicateHandle(GetCurrentProcess(), local, hgame, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS)) { LogRecord("DuplicateHandle(tex %d) failed %lu", i, GetLastError()); ok = false; }
                    CloseHandle(local);
                    gameTex[i] = (uint64_t)(uintptr_t)remote;
                }
            }
            NVSDK_NGX_Result rf = NVSDK_NGX_Result_Success;
            int flags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
            if (b.depth_inverted) flags |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
            if (b.hdr)            flags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
            if (b.flags_override >= 0) flags = b.flags_override;
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 0 && !h.ngxOk)
            {
                ok = false;
                rf = NVSDK_NGX_Result_FAIL_FeatureNotSupported;
                LogRecord("this build wants DLSS and NGX is not available: build refused");
            }
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 1 && !g_ffxDll)
            {
                ok = false;
                rf = NVSDK_NGX_Result_FAIL_FeatureNotSupported;
                LogRecord("this build wants FSR and no usable FidelityFX library was found: build refused");
            }
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 2 && !g_xessOk)
            {
                ok = false;
                rf = NVSDK_NGX_Result_FAIL_FeatureNotSupported;
                LogRecord("this build wants XeSS and %s: build refused", XessWhyText());
            }
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 2)
            {
                // XeSS's flags and setting are its own (CreateXessContext). Its sharpening is not used: the client's RCAS runs after the resolve.
                if (b.flags_override >= 0)
                    LogRecord("flags= (an NGX bit field) is IGNORED on XeSS");
                ok = CreateXessContext(b.width, b.height, h.outW, h.outH, b.hdr != 0);
                rf = ok ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_FAIL_FeatureNotSupported;
            }
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 1)
            {
                // Flag mapping, two inverted against NGX: NGX MVLowRes set = FSR DISPLAY_RESOLUTION_MOTION_VECTORS unset (render-res vectors);
                // NGX IsHDR set = FSR NON_LINEAR_COLORSPACE unset (linear input, as pre-tonemap FP16 is). DEPTH_INVERTED with DEPTH_INFINITE
                // describes reverse infinite Z, AMD's recommended case.
                uint32_t ff = FFX_UPSCALE_ENABLE_DEPTH_INVERTED
                            | FFX_UPSCALE_ENABLE_DEPTH_INFINITE
                            | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
                if (b.hdr) ff |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
                if (b.flags_override >= 0)
                    LogRecord("flags= (an NGX bit field) is IGNORED on FSR");
                // h.outW and h.outH already are (wantSr ? target : render); EvaluateFsr reads them too.
                ok = CreateFsrContext(b.width, b.height, h.outW, h.outH, ff);
                rf = ok ? NVSDK_NGX_Result_Success : NVSDK_NGX_Result_FAIL_FeatureNotSupported;   // FSR's own sharpening is off: the client's RCAS runs after the resolve
            }
            if (ok && !HOST_TRANSPORT_ONLY && g_backend == 0) ok = CreateFeature(b.width, b.height, flags, &rf, wantSr ? h.outW : 0, wantSr ? h.outH : 0);
            if (ok && HOST_TRANSPORT_ONLY) LogRecord("transport-only mode: the left half of Color is copied to Output, no evaluate");

            // The refusal is decided by here, so the slots go here: the client never imports a refused set (ReleaseRefusedSet says why the
            // game's handles go too).
            if (!ok) ReleaseRefusedSet(hgame, gameTex);

            FeedBuildAck ack = {};            ack.ok         = ok ? 1 : 0;
            ack.ngx_result = (uint32_t)rf;
            ack.fence_in   = (uint64_t)(uintptr_t)gameIn;
            ack.fence_out  = (uint64_t)(uintptr_t)gameOut;
            ack.output_fmt = (uint32_t)h.outputFmt;
            // g_backend is resolved by here: the client gets the answer, not the ask.
            if (g_backend == 1) ack.flags |= FEED_ACK_BACKEND_FSR;
            if (g_backend == 2) ack.flags |= FEED_ACK_BACKEND_XESS;   // with FEED_ACK_SR_ACTIVE below when it upscales
            ack.flags |= AckFsrBits();        // AMD's library raised (this build or earlier), FSR 4 passed by, the shader model
                                              // NGX could not report a DLSS mode for this output (PickSrQuality's query failed, or no mode answered). The build is refused, and
            // the client gets the reason.
            if (srUnavailable) ack.flags |= FEED_ACK_SR_UNAVAILABLE;
            else if (ok && wantSr) ack.flags |= FEED_ACK_SR_ACTIVE;   // the preset is in this log
            for (int i = 0; i < FEED_SLOTS; i++) ack.tex[i] = gameTex[i];
            WriteFull(pipe, &ack, sizeof(ack));
            if (!ok && DeviceRemoved("a build")) { endedBy = "this helper"; break; }
            evaluated = 0;
        }
        else if (tag == 'F')
        {
            FeedFrameMsg fm = {};
            if (!HostPipeRead(&rd, &fm, sizeof(fm))) break;
#if DLSSIV_DEV
            LONGLONG wakeQpc = 0;   // serve timer: the frame message has been read
            if (g_logLevel >= 2 || g_gpuProbe) QueryPerformanceCounter((LARGE_INTEGER*)&wakeQpc);
#endif
            if (!ResolveReady()) { ReleaseClient(fm.n); if (g_bridge && !AnswerFrame(pipe, fm.n, false)) { if (g_answerFenceTimedOut) endedBy = "this helper"; break; } continue; }
            // BEHIND THE GAME'S COPIES: on the GPU, by the in-fence, on the native and fast paths; in bridge mode the
            // message itself is the guarantee, written by the game's relay only after its copies retired.
            if (!g_bridge) h.queue->Wait(h.fenceIn, fm.n);
#if DLSSIV_DEV
            const bool timed = (g_logLevel >= 2 || g_gpuProbe) && ServeTimerReady();
            if (timed) { g_st.wake = wakeQpc; g_st.n = fm.n; g_st.armed = true; }
#endif
            const bool done = HOST_TRANSPORT_ONLY ? TransportCopy()
                            : (g_backend == 1 ? EvaluateFsr(fm) : g_backend == 2 ? EvaluateXess(fm) : Evaluate(fm));
#if DLSSIV_DEV
            if (timed) ServeTimerTick();
#endif
            // The client is released here: it reads the signal as "the helper has read this frame's colour, depth and vectors" and overwrites
            // them, so the signal sits behind every pass that reads them. A queue signal runs after everything submitted (here, the evaluate).
            // ReleaseClient falls back to a CPU signal only when the queue signal fails (a device removed in between).
            if (done) { ReleaseClient(fm.n); evaluated++; evaluatedSession++; failedInARow = 0; if (g_bridge && !AnswerFrame(pipe, fm.n, true)) { if (g_answerFenceTimedOut) endedBy = "this helper"; break; } }
            else
            {
                // The device question FIRST, then the release that fits the answer.
                const bool removed = DeviceRemoved("an evaluate");
                ReleaseClient(fm.n); failed++;
                if (g_bridge && !AnswerFrame(pipe, fm.n, false)) { if (g_answerFenceTimedOut) endedBy = "this helper"; break; }
                if (removed) { endedBy = "this helper"; break; }
                // A raise ends it now, not after three (see g_ngxRaised); the client falls back on the broken pipe.
                if (g_ngxRaised || g_ffxRaised || g_xessRaised)
                {
                    LogRecord("the upscaler's library raised an exception: the connection ends");
                    endedBy = "this helper";
                    break;
                }
                // Three failed evaluates in a row with the device intact end the connection; the client falls back for the session.
                if (++failedInARow >= 3)
                {
                    LogRecord("%d evaluates in a row FAILED with the device intact: the connection ends", failedInARow);
                    endedBy = "this helper";
                    break;
                }
            }
            if (evaluated + failed <= 3 || ((evaluated + failed) % 1800) == 0)
                LogHealth("frame %llu evaluated (%llu failed)", (unsigned long long)evaluated, (unsigned long long)failed);

        }
        else if (tag == 'P')
        {
            // The game's probe (Linux sync path): it has CPU-signalled its in-semaphore to p.n; reaching this fence proves game -> helper.
            // The helper's CPU signal of the out-fence tests the other direction on the game's side.
            FeedProbeIvt p = { 'P', 0 };
            if (!HostPipeRead(&rd, ((BYTE*)&p) + 1, sizeof(p) - 1)) break;
            const bool inOk = g_semOpened && h.fenceIn && WaitFenceValue(h.fenceIn, p.n, 1500);
            if (h.fenceOut) h.fenceOut->Signal(p.n);
            // FEED_ANS_EVENT: this helper sets an answer event when the mode message hands it one (feed_ipc.h)
            FeedAnswerIvt a = { 'R', p.n, (inOk ? FEED_ANS_IN_OK : 0u) | (g_semOpened ? FEED_ANS_OPENED : 0u) | FEED_ANS_EVENT };
            LogRecord("sync probe: the game's signal %s (value %llu)",
                inOk ? "arrived" : "did NOT arrive within 1.5 s", (unsigned long long)p.n);
            if (!WriteFull(pipe, &a, sizeof(a))) break;
        }
        else if (tag == 'M')
        {
            FeedModeEventIvt m = { 'M', 0, 0 };
            if (!HostPipeRead(&rd, ((BYTE*)&m) + 1, sizeof(FeedModeIvt) - 1)) break;
            // The handle follows only when the game set the flag (after FEED_ANS_EVENT); an older game sends the 5 bytes alone.
            if ((m.mode & FEED_MODE_ANSWER_EVENT) && !HostPipeRead(&rd, &m.answer_event, sizeof(m.answer_event))) break;
            m.mode &= ~FEED_MODE_ANSWER_EVENT;
            g_bridge = (m.mode == 2);
            if (g_bridge && m.answer_event) g_answerEvent = (HANDLE)(uintptr_t)m.answer_event;
            // In bridge mode the out-fence becomes private: the game's relay is then the only signaller of the out-semaphore, since two
            // signallers of a timeline could make it go backwards. A plain fence of this helper's carries the CPU wait; the opened one is released.
            if (g_bridge && g_semOpened)
            {
                ID3D12Fence* priv = nullptr;
                const HRESULT pr = h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&priv);
                if (SUCCEEDED(pr) && priv) { h.fenceOut->Release(); h.fenceOut = priv; }
                else
                {   // never signal the game's semaphore from two places, nor claim a state not reached
                    LogRecord("sync mode: BRIDGE asked, but a private out-fence could not be created (0x%08X): the connection ends", (unsigned)pr);
                    endedBy = "this helper";
                    break;
                }
            }
            LogRecord("sync mode: %s", g_bridge ? "BRIDGE (the game relays each frame over the pipe; this host answers each after its own private fence)"
                                              : "FAST (GPU-side fences, as on Windows)");
            if (g_bridge)
                LogRecord("sync mode: %s", g_answerEvent ? "each answer also sets the game's answer event, which its relay waits on"
                                                         : "the game sent no answer event; its relay polls the pipe for each answer");
        }
#if DLSSIV_FG
                // With frame generation (--fg=1) every tag reaching here is ServeFg's (fg_host.h). Kept disjoint so the tag need not live through
        // the loop.
        else if (g_fg)
        {
            const int took = ServeFg(tag, &rd, pipe, hgame);
            if (took != FG_SERVE_HANDLED) { if (took == FG_SERVE_END) endedBy = "this helper"; break; }   // FG_SERVE_GONE: the pipe, as any read here
        }
#endif
        else { LogRecord("unknown tag 0x%02X: stopping", tag); endedBy = "this helper"; break; }    }

        // Never leave the game holding a wait nothing will satisfy.
    LogRecord("pipe closed by %s after %llu frames (%llu failed)", endedBy,
        (unsigned long long)evaluatedSession, (unsigned long long)failed);
        // log=2: the reader's reads per message (one or fewer means the buffering works).
    LogHealth("the pipe: %lu reads for %llu messages (%.2f a message)", (unsigned long)rd.fills, (unsigned long long)messages,
        messages ? (double)rd.fills / (double)messages : 0.0);
    if (!g_deviceRemoved && h.fenceValue) WaitFenceValue(h.fence, h.fenceValue, 2000);
        // Unconditional: when the GPU is wedged the queue signal never runs, and the loop can end with the game still waiting (a raise,
    // three strikes, an unknown tag). This CPU signal is then the only release; skipping it would freeze the game (IvtFail).
    if (h.fenceOut) h.fenceOut->Signal(UINT64_MAX);
#if DLSSIV_FG
    FgAtExit();              // the two fences frame generation answers on, for the same reason, and its line for the connection
#endif
        // The pipe closes before the teardown: after the UINT64_MAX signal only a failed write stops the game's frames, and a teardown that
    // blocks (a raised library's detach, NGX's shutdown) would let the game fill the pipe and block in WriteFile inside DXVK's
    // submission lock. The drain (at most 2 s) and the signal come first on purpose.
    CloseHandle(pipe);
        // A library that raised is never called again, its detach code included: returning would run every DLL_PROCESS_DETACH through
    // ExitProcess. The log line is written first.
#if DLSSIV_FG
    if (g_ngxRaised || g_ffxRaised || g_xessRaised || (g_fg && g_fg->raised))   // frame generation's latch too: AMD's library again
#else
    if (g_ngxRaised || g_ffxRaised || g_xessRaised)
#endif
    {
        LogRecord("a library raised earlier: this helper ends without unloading it");
        TerminateProcess(GetCurrentProcess(), g_deviceRemoved ? 3 : 0);
    }
        // A drain that refused leaves the teardown to process exit: destroying under the same live work would only move the fault.
    if (ReleaseSet())
    {
#if DLSSIV_FG
        FgAfterTailRelease();   // frame generation's teardown, run inside ReleaseSet, may have raised: then nothing is unloaded
#endif
                // Not into a library that raised (see g_ngxRaised).
        if (h.params && !g_ngxRaised) { NVSDK_NGX_D3D12_DestroyParameters(h.params); h.params = nullptr; }
        ReleaseFsr();
        ReleaseXess();
        if (h.ngxInited && !g_ngxRaised) { NVSDK_NGX_D3D12_Shutdown1(h.dev); h.ngxInited = false; }
    }
    LogHealth("exit %d", g_deviceRemoved ? 3 : 0);
    return g_deviceRemoved ? 3 : 0;
}

// Microsoft's Agility D3D12 runtime, only when a D3D12Core.dll is added under DLSSIV\D3D12\: some community FSR 4 builds need
// shader model 6.9, which the system runtime may not offer. Opted in at run time (ID3D12SDKConfiguration1::CreateDeviceFactory, then
// ApplyToGlobalState), not with the D3D12SDKVersion/D3D12SDKPath exports: with those and no D3D12Core.dll, D3D12CreateDevice fails.
// Without the file it costs one GetFileAttributes; any failure past that is a log line and the system runtime. Must run before
// InitDevice creates the first device.
static const GUID kClsidD3D12SDKConfiguration = { 0x7cda6aca, 0xa03e, 0x49c8, { 0x94, 0x58, 0x03, 0x34, 0xd2, 0x0e, 0x07, 0xce } };
// The runtime file this process asked for, empty if none, copied only at log>=1 for LogD3D12Runtime (which compares it with the file
// loaded). g_agilityTaken is the same fact at every level.
static char g_agilityAsked[MAX_PATH];
static bool g_agilityTaken = false;
static void OptInAgilityRuntime()
{
    char dir[MAX_PATH], core[MAX_PATH];
    if (_snprintf_s(dir, MAX_PATH, _TRUNCATE, "%sD3D12\\", g_exeDir) < 0) return;
    if (_snprintf_s(core, MAX_PATH, _TRUNCATE, "%sD3D12Core.dll", dir) < 0) return;
    if (GetFileAttributesA(core) == INVALID_FILE_ATTRIBUTES) return;
    g_agilityRefused = true;                 // until it is taken, below: every return between here and there is a refusal
        // A 64-bit file or not tried: the same package carries a 32-bit D3D12Core.dll, so the PE header is read first.
    WORD machine = 0;
    {
        const HANDLE fh = CreateFileA(core, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (fh != INVALID_HANDLE_VALUE)
        {
            BYTE hdr[1024] = {};
            DWORD got = 0;
            if (ReadFile(fh, hdr, sizeof(hdr), &got, nullptr) && got >= 0x40)
            {
                const DWORD pe = *(const DWORD*)(hdr + 0x3C);
                if (pe <= got - 6 && memcmp(hdr + pe, "PE\0\0", 4) == 0) machine = *(const WORD*)(hdr + pe + 4);
            }
            CloseHandle(fh);
        }
    }
    if (machine != IMAGE_FILE_MACHINE_AMD64)
    {
        LogRecord("D3D12: %s is not a 64-bit DLL (machine 0x%04X): NOT used, the system's runtime is", core, (unsigned)machine);
        return;
    }
        // The SDK version is the file's own (Agility builds are numbered 1.<sdk>.<patch>, and CreateDeviceFactory needs that number).
    // version.dll is loaded here from System32, only when the file exists, so it is not an import every helper maps.
    UINT major = 0, sdk = 0, patch = 0;
    typedef DWORD (WINAPI *PFN_VerSize)(LPCSTR, LPDWORD);
    typedef BOOL  (WINAPI *PFN_VerInfo)(LPCSTR, DWORD, DWORD, LPVOID);
    typedef BOOL  (WINAPI *PFN_VerQuery)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    if (const HMODULE ver = LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
    {
        const auto verSize  = (PFN_VerSize) GetProcAddress(ver, "GetFileVersionInfoSizeA");
        const auto verInfo  = (PFN_VerInfo) GetProcAddress(ver, "GetFileVersionInfoA");
        const auto verQuery = (PFN_VerQuery)GetProcAddress(ver, "VerQueryValueA");
        DWORD unused = 0;
        const DWORD n = (verSize && verInfo && verQuery) ? verSize(core, &unused) : 0;
        if (n && n <= 65536)
            if (char* const info = (char*)malloc(n))
            {
                VS_FIXEDFILEINFO* fi = nullptr;
                UINT len = 0;
                if (verInfo(core, 0, n, info) && verQuery(info, "\\", (void**)&fi, &len) && fi && len >= sizeof(*fi))
                { major = HIWORD(fi->dwFileVersionMS); sdk = LOWORD(fi->dwFileVersionMS); patch = HIWORD(fi->dwFileVersionLS); }
                free(info);
            }
        FreeLibrary(ver);
    }
        // Numbered like an Agility runtime (1.600 and up) or not tried: CreateDeviceFactory and ApplyToGlobalState accept a DLL that is not
    // one, so their success proves nothing.
    if (major != 1 || sdk < 600)
    {
        LogRecord("D3D12: %s is not numbered like an Agility SDK runtime (version %u.%u): NOT used, the system's runtime is",
                  core, major, sdk);
        return;
    }
    typedef HRESULT (WINAPI *PFN_GetInterface)(REFCLSID, REFIID, void**);
    const HMODULE d3d12 = LoadLibraryW(L"d3d12.dll");
    const auto getIf = d3d12 ? (PFN_GetInterface)GetProcAddress(d3d12, "D3D12GetInterface") : nullptr;
    ID3D12SDKConfiguration1* sc = nullptr;
    ID3D12DeviceFactory*     df = nullptr;
    HRESULT hr = getIf ? getIf(kClsidD3D12SDKConfiguration, __uuidof(ID3D12SDKConfiguration1), (void**)&sc) : E_NOINTERFACE;
    if (SUCCEEDED(hr)) hr = sc->CreateDeviceFactory(sdk, dir, __uuidof(ID3D12DeviceFactory), (void**)&df);
    if (SUCCEEDED(hr)) hr = df->ApplyToGlobalState();
    if (df) df->Release();
    if (sc) sc->Release();
        // Asked, not confirmed: LogD3D12Runtime says which runtime answered once the device exists (under Wine, vkd3d-proton).
    if (SUCCEEDED(hr))
    {
        g_agilityTaken = true;
        g_agilityRefused = false;
        if (g_logLevel >= 1) strcpy_s(g_agilityAsked, MAX_PATH, core);
        LogRecord("D3D12: Agility SDK runtime 1.%u.%u asked (%s)", sdk, patch, core);
    }
    else
        LogRecord("D3D12: %s (1.%u.%u) could not be used (0x%08X): the system's runtime is", core, sdk, patch, (unsigned)hr);
}

// log>=1: the runtime file actually loaded and the highest shader model, after the first device and after a rebind (the device FSR
// runs on). With no separate D3D12Core.dll it names d3d12.dll, the whole runtime then (older Windows 10, Wine).
static void LogD3D12Runtime()
{
    if (!h.dev) return;
    char core[MAX_PATH] = "(unknown)";
    const char* whole = "";
    HMODULE m = GetModuleHandleW(L"D3D12Core.dll");
    if (!m) { m = GetModuleHandleW(L"d3d12.dll"); whole = " (no separate D3D12Core.dll is loaded: this is the whole runtime)"; }
    if (m) GetModuleFileNameA(m, core, MAX_PATH);
    ReadTopShaderModel();                    // for FsrRaiseHint and CreateFsrContext's pick
    const int top = g_topSm;
    LogRecord("D3D12: runtime %s%s, shader model %d.%d%s", core, whole, top >> 4, top & 0xF,
        !g_agilityAsked[0] ? "" : _stricmp(core, g_agilityAsked) == 0 ? " (the Agility runtime asked)" : " (NOT the Agility runtime asked: the process kept its own)");
}

// Standalone VC bridge: the paired helper lives in a private plugin folder,
// while the existing build's licensed vendor libraries stay in game/DLSSIV.
// Read the Unicode command line rather than round-tripping through main's ACP
// argv. The retained vendor loader has ANSI file paths, so a path not losslessly
// representable in the active code page is refused, never silently mangled.
static bool ApplyBridgeVendorDirectory()
{
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return false;
    bool ok = true, seen = false;
    for (int i = 1; i < count; ++i)
    {
        static const wchar_t prefix[] = L"--vendor-dir=";
        if (wcsncmp(args[i], prefix, _countof(prefix) - 1) != 0) continue;
        if (seen) { ok = false; break; }
        seen = true;
        const wchar_t* value = args[i] + _countof(prefix) - 1;
        const size_t n = wcslen(value);
        const bool absolute = (n > 2 && value[1] == L':' && (value[2] == L'\\' || value[2] == L'/'))
                           || (n > 2 && value[0] == L'\\' && value[1] == L'\\');
        wchar_t path[MAX_PATH] = {};
        const DWORD got = absolute ? GetFullPathNameW(value, MAX_PATH, path, nullptr) : 0;
        const DWORD attributes = got && got < MAX_PATH ? GetFileAttributesW(path) : INVALID_FILE_ATTRIBUTES;
        if (!got || got >= MAX_PATH - 1 || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        { ok = false; break; }
        size_t end = wcslen(path);
        if (path[end - 1] != L'\\' && path[end - 1] != L'/') { path[end++] = L'\\'; path[end] = 0; }
        BOOL substituted = FALSE;
        const UINT cp = GetACP();
        const DWORD flags = cp == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        if (!WideCharToMultiByte(cp, flags, path, -1, g_exeDir, MAX_PATH,
                               nullptr, cp == CP_UTF8 ? nullptr : &substituted) || substituted)
        { ok = false; break; }
        wcscpy_s(g_exeDirW, MAX_PATH, path);
        // AMD's loader, loaded by full path from this folder, resolves amd_fidelityfx_upscaler_dx12.dll (and AGS) by
        // bare name; without this the search starts in this helper's private folder and FSR finds no usable library.
        if (!SetDllDirectoryW(path)) { ok = false; break; }
    }
    LocalFree(args);
    return ok;
}

int main(int argc, char** argv)
{
    GetModuleFileNameA(nullptr, g_exeDir, MAX_PATH);
    if (char* s = strrchr(g_exeDir, '\\')) *(s + 1) = 0;
    MultiByteToWideChar(CP_ACP, 0, g_exeDir, -1, g_exeDirW, MAX_PATH);

        // Nothing is logged before argv is parsed: the level and the client's log path come from it.
    DWORD pid = 0;
#if DLSSIV_FG
    int fgArg = 0;   // --fg=1: the game is statically eligible for frame generation and asks the helper to be ready for it
#endif
#if DLSSIV_DEV
    int fgRaise = 0; // --fgraise=N: the raise lever (FgHost::forceRaise's bits): 2 = the version query raises
    int fgRaiseAt = 0, fgFailAt = 0, fgVramRefuse = 0, fgSlowG = 0, fgDieG = 0;   // the other frame generation levers (FgHost says what each does)
    int fgHoldQ = 0, fgSlowD = 0;                       // the Kth 'Q' held 8 s before its prepare; every 64th dispatch slowed
    int fgHudEmpty = 0, fgCostPad = 0, fgCostBar = 1;   // the HUD-less colour left empty; the cost marks' pad (fg_cost.h) and their barrier, on unless told 0
    const char* fgCostCsv = nullptr;                    // and where the marks write one row a list
    int fgCostHead = 0, fgCostPre = 0;                  // the marks' barrier before a list's head stamp too, off unless 1; the stranger's copies before each prepare's list (a control)
    int fgCheck = 0;                                    // AMD's own checking on every context, its message hook installed at the load (fg_lib.h)
    const char* fgLib = nullptr; const char* fgLibSha = nullptr;   // fg_lib.h: the library frame generation loads by its full path, and the SHA-256 it must have
    int fgLibStock = 0, fgId = 0;                       // --fglib may name a file of AMD's name; which module serves frame generation, said
    int d3d12Debug = 0;                                 // Direct3D 12's debug layer, on before the first device and counted (fg_lib.h)
    const char* fgOwn = nullptr; const char* fgOwnSha = nullptr; const char* fgOwnCsv = nullptr;   // fg_own.h: the mod's own library, the SHA-256 it must have, and where its rows go
    int fgOwnStamps = 0;                                // its stamps: 1 a GPU timestamp behind every job of AMD's backend, 2 the same with no barrier
        // Levers and controls of the frame generation ring and answer (fg_host.h, FgBeginCommands and FgAnswerOut): the ring's length in
    // use, its wait, its hold; a CPU answer with the queue's own value, the queue's refusal planted, the device removed.
    int fgRing = 0, fgRingWait = 0, fgRingHold = 0, fgRingHoldN = 4;
    int fgCpuSame = 0, fgRefuseAt = 0, fgRefuseN = 1, fgRemoveAt = 0;
        // The capture (fg_cap.h): frames a run, runs, pairs between runs, whether the go file is waited for, and where the runs go.
    int fgCap = 0, fgCapRuns = 0, fgCapGap = -1, fgCapGo = 0, fgCapRefuse = 0;
    const char* fgCapDir = nullptr;
        // Development builds: extra arguments from dlssiv-helper-args.txt beside this exe, one line of tokens as on the command line. Each
    // "--" token goes through the loop below after the command line's own, by the same rule; any other token is refused, never a pid.
    // Said in one line after the build line.
    static char devArgsLine[1024], devArgsSaid[1024]; static char* devArgv[128];
    int devArgsN = 0, devArgsRefused = 0;
    {
        char p[MAX_PATH]; const DWORD pn = GetModuleFileNameA(nullptr, p, MAX_PATH);
        char* sl = (pn > 0 && pn < MAX_PATH) ? strrchr(p, '\\') : nullptr;
        FILE* f = nullptr;
        if (sl && argc < 64 && strcpy_s(sl + 1, MAX_PATH - (size_t)(sl + 1 - p), "dlssiv-helper-args.txt") == 0 && fopen_s(&f, p, "rb") == 0 && f)
        {
            const size_t got = fread(devArgsLine, 1, sizeof devArgsLine - 1, f); fclose(f); devArgsLine[got] = 0;
            int k = 0; for (; k < argc; k++) devArgv[k] = argv[k];
            char* ctx = nullptr;
            for (char* t = strtok_s(devArgsLine, " \t\r\n", &ctx); t && k < 127; t = strtok_s(nullptr, " \t\r\n", &ctx))
            {
                if (t[0] == '-' && t[1] == '-') { devArgv[k++] = t; devArgsN++; strcat_s(devArgsSaid, sizeof devArgsSaid, " "); strcat_s(devArgsSaid, sizeof devArgsSaid, t); }
                else devArgsRefused++;
            }
            devArgv[k] = nullptr; argc = k; argv = devArgv;
        }
    }
#endif
        // Any --flag is skipped rather than parsed as the pid, so a flag this build does not know cannot turn into pid 0.
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] == '-')
        {
            if      (strncmp(argv[i], "--backend=", 10) == 0) g_backend   = g_backendCli = atoi(argv[i] + 10);
            else if (strncmp(argv[i], "--preset=",   9) == 0) g_cfgPreset = atoi(argv[i] + 9);
            else if (strncmp(argv[i], "--log=",      6) == 0) g_logLevel  = atoi(argv[i] + 6);
            else if (strncmp(argv[i], "--fsr_version=",    14) == 0) g_fsrVersion   = atoi(argv[i] + 14);
            else if (strncmp(argv[i], "--sync=",            7) == 0) g_sync         = atoi(argv[i] + 7);
            else if (strncmp(argv[i], "--scale=",           8) == 0) g_scale        = atoi(argv[i] + 8);
#if DLSSIV_FG
            // --fg=1: the game passes it only on Windows native and when its keys ask. Nothing is loaded or made here.
            else if (strncmp(argv[i], "--fg=",              5) == 0) fgArg          = atoi(argv[i] + 5);
#endif
#if DLSSIV_DEV
            else if (strncmp(argv[i], "--fgraise=",        10) == 0) fgRaise        = atoi(argv[i] + 10);        // the raise lever: does a raise end frame generation only
            else if (strncmp(argv[i], "--fgraise_at=",     13) == 0) fgRaiseAt      = atoi(argv[i] + 13);        // the Kth dispatch of the connection raises
            else if (strncmp(argv[i], "--fgfail_at=",      12) == 0) fgFailAt       = atoi(argv[i] + 12);        // dispatches K, K+1, K+2 fail before AMD's call
            else if (strncmp(argv[i], "--fgvram_refuse=",  16) == 0) fgVramRefuse   = atoi(argv[i] + 16);        // the video memory rule refuses, with the real numbers
            else if (strncmp(argv[i], "--fgslow_g=",       11) == 0) fgSlowG        = atoi(argv[i] + 11);        // sleep before the ack of a set that was built
            else if (strncmp(argv[i], "--fgdie_g=",        10) == 0) fgDieG         = atoi(argv[i] + 10);        // end on 'g', the ack unwritten
            else if (strncmp(argv[i], "--fghold_q=",       11) == 0) fgHoldQ        = atoi(argv[i] + 11);        // the Kth 'Q' sleeps 8 s before its prepare
            else if (strncmp(argv[i], "--fgslow_d=",       11) == 0) fgSlowD        = atoi(argv[i] + 11);        // every 64th dispatch sleeps this many ms first
            else if (strncmp(argv[i], "--fghud_empty=",    14) == 0) fgHudEmpty     = atoi(argv[i] + 14);        // every configure hands AMD an empty HUD-less colour
            else if (strncmp(argv[i], "--fgcost_pad=",     13) == 0) fgCostPad      = atoi(argv[i] + 13);        // the cost marks: K full-screen copies in each dispatch's list, between stamps of their own
            else if (strncmp(argv[i], "--fgcost_bar=",     13) == 0) fgCostBar      = atoi(argv[i] + 13);        // the cost marks: 0 leaves out the barrier before the closing stamps (its control)
            else if (strncmp(argv[i], "--fgcost_csv=",     13) == 0) fgCostCsv      = argv[i] + 13;              // the cost marks: one row a list into this file
            else if (strncmp(argv[i], "--fgcost_head=",    14) == 0) fgCostHead     = atoi(argv[i] + 14);        // the cost marks: 1 puts a barrier before a list's head stamp too
            else if (strncmp(argv[i], "--fgcost_pre=",     13) == 0) fgCostPre      = atoi(argv[i] + 13);        // the cost marks: K full-screen copies in a list of their own before each prepare's list (a control)
            else if (strncmp(argv[i], "--fgcheck=",        10) == 0) fgCheck        = atoi(argv[i] + 10);        // AMD's own checking and its message hook
            else if (strncmp(argv[i], "--fglib=",           8) == 0) fgLib          = argv[i] + 8;               // the development load: this file (a bare name: beside this helper; else a full path), never AMD's loader, no fallback
            else if (strncmp(argv[i], "--fglib_sha=",      12) == 0) fgLibSha       = argv[i] + 12;              // the SHA-256 that module must have: another one refuses
            else if (strncmp(argv[i], "--fglib_stock=",    14) == 0) fgLibStock     = atoi(argv[i] + 14);        // a desk control: --fglib may name a file of AMD's name
            else if (strncmp(argv[i], "--fgid=",            7) == 0) fgId           = atoi(argv[i] + 7);         // say which module serves frame generation: path, size, SHA-256
            else if (strncmp(argv[i], "--d3d12_debug=",    14) == 0) d3d12Debug     = atoi(argv[i] + 14);        // Direct3D 12's debug layer, its messages counted (2: with the count's own control)
            else if (strncmp(argv[i], "--fgown=",           8) == 0) fgOwn          = argv[i] + 8;               // the own arm: this library through the mod's own interface (bare name: beside this helper; else full path), no fallback
            else if (strncmp(argv[i], "--fgown_sha=",      12) == 0) fgOwnSha       = argv[i] + 12;              // the SHA-256 that module must have: another one refuses
            else if (strncmp(argv[i], "--fgown_stamps=",   15) == 0) fgOwnStamps    = atoi(argv[i] + 15);        // its stamps: 1 behind every job of AMD's backend, each behind a barrier; 2 with no barrier
            else if (strncmp(argv[i], "--fgown_csv=",      12) == 0) fgOwnCsv       = argv[i] + 12;              // one row a call, and with the stamps one a stamp, into this file
            else if (strncmp(argv[i], "--fgring=",          9) == 0) fgRing         = atoi(argv[i] + 9);         // the ring's length in use, 2 to the build's
            else if (strncmp(argv[i], "--fgring_wait=",    14) == 0) fgRingWait     = atoi(argv[i] + 14);        // control: a served list waits up to 2 s for its slot
            else if (strncmp(argv[i], "--fgring_hold=",    14) == 0) fgRingHold     = atoi(argv[i] + 14);        // from the Kth served list on, every slot of the ring reads as not retired
            else if (strncmp(argv[i], "--fgring_holdn=",   15) == 0) fgRingHoldN    = atoi(argv[i] + 15);        // for this many served lists (4 when not given)
            else if (strncmp(argv[i], "--fgcpu_same=",     13) == 0) fgCpuSame      = atoi(argv[i] + 13);        // control: a CPU answer carries the queue's own value
            else if (strncmp(argv[i], "--fgrefuse_at=",    14) == 0) fgRefuseAt     = atoi(argv[i] + 14);        // from the Kth 'G' on, the answer is made as if the queue had refused the signal
            else if (strncmp(argv[i], "--fgrefuse_n=",     13) == 0) fgRefuseN      = atoi(argv[i] + 13);        // for this many 'G' (1 when not given)
            else if (strncmp(argv[i], "--fgremove_at=",    14) == 0) fgRemoveAt     = atoi(argv[i] + 14);        // the device is removed behind the submit of the Kth dispatch
            else if (strncmp(argv[i], "--fgcap=",           8) == 0) fgCap          = atoi(argv[i] + 8);         // the capture: N consecutive frames a run, everything the library was given, for a desk's replay
            else if (strncmp(argv[i], "--fgcap_runs=",     13) == 0) fgCapRuns      = atoi(argv[i] + 13);        // runs a connection
            else if (strncmp(argv[i], "--fgcap_gap=",      12) == 0) fgCapGap       = atoi(argv[i] + 12);        // pairs between two runs
            else if (strncmp(argv[i], "--fgcap_go=",       11) == 0) fgCapGo        = atoi(argv[i] + 11);        // 1: no run before the go file appears in the capture's folder
            else if (strncmp(argv[i], "--fgcap_dir=",      12) == 0) fgCapDir       = argv[i] + 12;              // where the runs go: a full path, or a plain one from the folder above the helper's
            else if (strncmp(argv[i], "--fgcap_refuse=",   15) == 0) fgCapRefuse    = atoi(argv[i] + 15);        // a desk control: 1 its video memory rule refuses, 2 its disk rule, forced
            else if (strncmp(argv[i], "--gpu_probe=",      12) == 0) g_gpuProbe     = atoi(argv[i] + 12);
            else if (strncmp(argv[i], "--xess_auto=",      12) == 0) g_xessAutoAs   = atoi(argv[i] + 12);        // automatic chooses as on Intel graphics
            else if (strncmp(argv[i], "--fsr4_min_sm=",    14) == 0) g_fsr4MinSm    = atoi(argv[i] + 14);
#endif
            else if (strncmp(argv[i], "--logpath=", 10) == 0)
                strcpy_s(g_logPath, MAX_PATH, argv[i] + 10);
            else Fsr4TestArg(argv[i]);       // --fsr4-test=: this process is the FSR 4 start-up test's copy (fsr4_test.h)
            continue;
        }
                // The first non-flag token wins.
        if (!pid) pid = (DWORD)strtoul(argv[i], nullptr, 10);
    }
    if (!ApplyBridgeVendorDirectory())
    {
        if (g_logPath[0]) { g_logLevel = 1; LogRecord("invalid --vendor-dir: use one existing absolute directory representable in the Windows active code page; exiting"); }
        return 2;
    }
    if (!pid)
    {
                // Say so if there is anywhere to say it.
        if (g_logPath[0]) { g_logLevel = 1; LogRecord("no game pid on the command line: exiting"); }
        return 2;
    }
    if (g_backend < -1 || g_backend > 2) g_backend = -1;   // 2: XeSS
    g_backendCli = g_backend;
    if (g_cfgPreset < 0 || g_cfgPreset > 15) g_cfgPreset = 11;
    if (g_logLevel < 0) g_logLevel = 0;
    if (g_logLevel > 3) g_logLevel = 3;
    if (g_logLevel > 0)
        g_logMutex = CreateMutexA(nullptr, FALSE, "Local\\DlssIvLog");
    SetUnhandledExceptionFilter(HostUnhandled);   // one line before a crash; see HostUnhandled
        // The affinity is the client's to say: this helper inherits the game's, and the client's "transport: host started" line names it.
    LogRecord("build " IVT_BUILD_ID " " DLSSIV_FLAVOUR ", backend %s, preset %d",
        g_backend < 0 ? "auto" : g_backend == 1 ? "FSR" : g_backend == 2 ? "XeSS" : "DLSS", g_cfgPreset);
#if DLSSIV_DEV
    if (devArgsN || devArgsRefused) LogRecord("development: %d argument(s) from dlssiv-helper-args.txt beside this exe:%s; %d token(s) refused (not \"--\")", devArgsN, devArgsSaid, devArgsRefused);
#endif
        // The serve thread's scheduling: it serves every frame, and a windowless process may be throttled to efficiency cores on a hybrid
    // CPU. So this thread runs above normal and opts out of execution-speed throttling, as does the process (some Windows 11 builds
    // accept only the thread's opt-out). Refusals (old Windows, Wine) are only reported; the functions are looked up, not imported.
    {
        const BOOL prioOk = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        const DWORD prioErr = prioOk ? 0 : GetLastError();
        typedef BOOL (WINAPI* SetProcInfoFn)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
        const SetProcInfoFn setProcInfo = (SetProcInfoFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
        PROCESS_POWER_THROTTLING_STATE pt = {};
        pt.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;   // this policy is the helper's to decide
        pt.StateMask   = 0;                                           // and it is off
        const BOOL ptOk = setProcInfo ? setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt)) : FALSE;
        const DWORD ptErr = (setProcInfo && !ptOk) ? GetLastError() : 0;
        typedef BOOL (WINAPI* SetThreadInfoFn)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
        const SetThreadInfoFn setThreadInfo = (SetThreadInfoFn)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation");
        THREAD_POWER_THROTTLING_STATE tt = {};
        tt.Version     = THREAD_POWER_THROTTLING_CURRENT_VERSION;
        tt.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
        tt.StateMask   = 0;
        const BOOL ttOk = setThreadInfo ? setThreadInfo(GetCurrentThread(), ThreadPowerThrottling, &tt, sizeof(tt)) : FALSE;
        const DWORD ttErr = (setThreadInfo && !ttOk) ? GetLastError() : 0;
        char a[48], b[64], c[64];
        if (prioOk) strcpy_s(a, sizeof(a), "took"); else _snprintf_s(a, sizeof(a), _TRUNCATE, "refused (%lu)", prioErr);
        if (ptOk) strcpy_s(b, sizeof(b), "took"); else if (!setProcInfo) strcpy_s(b, sizeof(b), "not available here");
        else _snprintf_s(b, sizeof(b), _TRUNCATE, "refused (%lu)", ptErr);
        if (ttOk) strcpy_s(c, sizeof(c), "took"); else if (!setThreadInfo) strcpy_s(c, sizeof(c), "not available here");
        else _snprintf_s(c, sizeof(c), _TRUNCATE, "refused (%lu)", ttErr);
        LogRecord("scheduling: priority %s; throttling opt-out: process %s, thread %s", a, b, c);
    }
    OptInAgilityRuntime();                   // before the first device, or it cannot apply
#if DLSSIV_DEV
    FgLibAsk(fgArg, fgLib, fgLibSha, fgLibStock, fgId, d3d12Debug, g_agilityTaken);   // fg_lib.h, before the first device (the debug layer is a process switch); nothing unless one of its arguments was given
    FgOwnAsk(fgArg, fgOwn, fgOwnSha, fgOwnStamps, fgOwnCsv, fgLib && fgLib[0]);       // fg_own.h: the own arm; nothing unless one of its arguments was given
#elif DLSSIV_FG
    if (fgArg == 1) FgOwnAsk(1, DLSSIV_FG_OWN_FILE, DLSSIV_FG_OWN_SHA, 0, nullptr, false);   // the shipped arm at --fg=1: the file beside this helper and its record's hash (fg_own.h)
#endif
    if (!InitDevice(Fsr4TestLuid()))         // an ordinary helper: nullptr, its own pick. The test's copy: its parent's adapter
    {
                // Named as the suspect: a device that fails after the Agility opt-in points straight at the file.
        if (g_agilityTaken)
            LogRecord("no D3D12 device with " DLSSIV_HOST_DIR "\\D3D12\\D3D12Core.dll: that file cannot serve this helper (remove the " DLSSIV_HOST_DIR "\\D3D12 folder)");
        return 1;
    }
    if (g_logLevel >= 1) LogD3D12Runtime();
        // The back end from the adapter, and its probe (run again by the handshake after a rebind).
    ResolveBackendAndProbe();
#if DLSSIV_FG
        // Frame generation's state, made only with --fg=1 (fg_host.h): one heap struct behind one pointer; without it every frame
    // generation tag is unknown. The probe runs here, before the pipe exists, so it does not hold the game's render thread in the
    // handshake.
    if (fgArg == 1 && FgHostMake())
    {
#if DLSSIV_DEV
        g_fg->forceRaise = fgRaise; g_fg->raiseAt = fgRaiseAt; g_fg->failAt = fgFailAt; g_fg->vramRefuse = fgVramRefuse; g_fg->slowG = fgSlowG; g_fg->dieG = fgDieG;
        g_fg->holdQ = fgHoldQ; g_fg->slowD = fgSlowD; g_fg->hudEmpty = fgHudEmpty;
        if (fgSlowD > 0) LogRecord("frame generation: --fgslow_d: every 64th dispatch of this connection sleeps %d ms before it is recorded", fgSlowD);
        if (fgHudEmpty) LogRecord("frame generation: --fghud_empty: every configure hands AMD an EMPTY HUD-less colour, so nothing reads the set's HUDLESS image: an A/B's other arm");
        g_fg->check = fgCheck != 0;   // before the probe, which loads the library AMD's message hook goes into (fg_lib.h)
        if (g_fg->check) LogRecord("frame generation: --fgcheck: AMD's own checking ON for every context; its messages are counted, the first %d logged", FG_CHECK_MSGS);
                // the ring and answer levers and controls, one line for each that is on
        if (fgRing >= 2 && fgRing <= FG_RING) g_fg->ringLen = fgRing;
        g_fg->ringWait = fgRingWait != 0; g_fg->ringHoldAt = fgRingHold; g_fg->ringHoldN = fgRingHoldN;
        g_fg->cpuSame = fgCpuSame != 0; g_fg->refuseAt = fgRefuseAt; g_fg->refuseN = fgRefuseN; g_fg->removeAt = fgRemoveAt;
        if (fgRing) LogRecord("frame generation: --fgring: the ring's length in use is %d (asked %d; this build's is %d)", g_fg->ringLen, fgRing, (int)FG_RING);
        if (fgRingWait) LogRecord("frame generation: --fgring_wait: THE CONTROL: a served list WAITS up to 2 s for its slot, as until 2026-10-10");
        if (fgRingHold > 0) LogRecord("frame generation: --fgring_hold: the served lists %d to %d of this connection find every slot of the ring not retired", fgRingHold, fgRingHold + fgRingHoldN - 1);
        if (fgCpuSame) LogRecord("frame generation: --fgcpu_same: THE CONTROL: an answer the CPU signals carries the queue's own value, as until 2026-10-10");
        if (fgRefuseAt > 0) LogRecord("frame generation: --fgrefuse_at: the answers of 'G' %d to %d of this connection are made as if the queue had refused the signal", fgRefuseAt, fgRefuseAt + fgRefuseN - 1);
        if (fgRemoveAt > 0) LogRecord("frame generation: --fgremove_at: the device is removed behind the submit of dispatch %d of this connection", fgRemoveAt);
#endif
        g_fg->agility = g_agilityTaken;
        FgProbe();
    }
#endif
#if DLSSIV_DEV
    FgCostAsk(fgCostPad, fgCostBar, fgCostCsv, fgCostHead, fgCostPre);     // the cost marks (fg_cost.h): made only with --fg=1 and the serve timer's switch (--log=2, --gpu_probe=1)
    FgCapAsk(fgCap, fgCapRuns, fgCapGap, fgCapGo, fgCapDir, fgCapRefuse);   // the capture (fg_cap.h): made only when asked; after the probe, so the library's identity is on file
#endif

    if (g_fsr4Test) return Fsr4TestChild(pid);   // the copy answers its one question and ends (fsr4_test.h); pid is its parent's
    return Serve(pid);
}
