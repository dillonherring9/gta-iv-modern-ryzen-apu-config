// DlssIvTransport.h: the frame transport. The helper's protocol spoken from inside the .asi, on DXVK's own Vulkan device,
// at the injection point of the frame (the first orthographic draw into the back buffer after the scene), so the result is
// resolved there and the HUD and menus are drawn natively on top.
// The helper is launched by the mod and answers its hello; it builds the shared set for a Vulkan client; the mod imports
// the two D3D12 fences as timeline semaphores and the D3D12 textures as VkImages into DXVK's device, with the extensions
// DXVK already enables (no vkCreateDevice hook). At the injection point, from the draw hook before the first HUD draw:
//   FlushRenderingCommands   every D3D9 command so far (scene, post chain, composite) is handed to DXVK's submission queue;
//   LockSubmissionQueue      DXVK's queue drains to the driver and its mutex is held: nothing of DXVK's is submitted
//                            until it is released;
//   vkQueueSubmit (the mod)  one command buffer: barrier on the back buffer, acquire the shared set, copy back buffer ->
//                            Color, copy the previous frame's Output -> back buffer, release the set to the helper,
//                            barrier; waits the out-fence for the previous frame, signals the in-fence for this one;
//   ReleaseSubmissionQueue   the game's later draws (the HUD) are submitted after, in order, and the closing barrier makes
//                            the mod's writes visible to them.
// mode=1 (transport test) returns the left half of the previous frame's colour, so the screen is split with the HUD over
// both halves: the proof of the compositing order. async_home=1 (default) is pipelined: one submit per frame, the result one
// frame old. async_home=0 is same-frame: a second submit waits the out-fence for this frame and copies it home before the HUD.
// The helper and its protocol began as DLSS5-Feeder's (MIT, Jean-Laurent ROUZIES): feed_ipc.h and feed_vk.h in this folder
// are derived from that project's files (the protocol is DLSS-IV's own since v8, FEED_IPC_VERSION), with its licence
// alongside. One helper per game pid (the pipe name carries the pid), so this client refuses to start while the Feeder's
// own add-on is loaded in the process.
// Settings: DLSS-IV.cfg next to GTAIV.exe, key=value, no sections.
//   transport   0 off, 1 on (default)
//   stage       1 protocol only, 2 copies at the injection point, 3 the guides too (default)
//   mode        1 transport test, 2 DLSS (default)
//   async_home  1 pipelined, result one frame old (default); 0 same-frame
// Included once from DLSS-IV.cpp after Log, backbufferSurf, the jitter state, DrawRec and the scene-key gate are declared;
// the Vulkan headers first.

#pragma once
#include <dxgiformat.h>
#include "feed_ipc.h"
#include "feed_vk.h"

struct IvtCfg { int transport; int mode; int async_home;
                int flags; int depth_mode;
                int quality;
                float sharpness; int backend; int hdr; int hdr_paper_white; int hdr_eotf;
                int hdr_view; int hdr_peak; int hdr_engine_name;
                int hdr_restore;
                int cfg_live; int hdr_rolloff; int hdr_source; int hdr_console_gamma; int hdr_ui;
                int preset; int log;
                // Appended at the end, though it belongs beside quality: the initialiser below is positional.
                float lod_bias;
                // The initialiser is positional and ivtCfgDefaults supplies a value for every field: append to both, in the same order.
                // rs_ds=1 (default): the render-scale gate shrinks the game's screen-size depth-stencil textures with its colour targets.
                // 0 leaves them at output size, a diagnostic only (depth-dependent effects go wrong). Needs a relaunch.
                int rs_ds;
                // mv_glass, and this one changes the picture. 1: the guide keeps the object velocity when its surface is nearer than the
                // visible one (vehicle glass, wind-blown litter) instead of falling back to the camera vector. Right for a car seen from
                // outside, wrong for everything else seen through glass. Live. See the guide pass (ivt_guides_ps.hlsl).
                int mv_glass;
                // inject_worker: 1 runs the injection on the device thread: the render thread captures what it needs and posts it as a
                // note in the worker's stream, so the time blocked in FlushRenderingCommands is spent on a thread that can absorb it.
                // 2 (default, not in the shipped cfg) also runs the velocity drain on the worker. 0 keeps it inline. Live; nothing at threaded=0.
                int inject_worker;
                // threaded, the device thread: 1 records every D3D9 call the render thread makes and runs it on DXVK from a worker thread
                // (source/devthread/); 2 also checks every answer its shadow gives against DXVK (slower: how the shadow is proved).
                // 0 installs nothing. Needs a relaunch. When armed, the mover tracker, the pose store and the velocity batch run on its
                // worker (devthread/dt_trk.h).
                int threaded;
                // hdr_cg_absorb (hdr_cga.h has the design): 1 (default) withholds FusionFix's console gamma draw once its pixel shader is
                // proved against its own output, and the HDR encode applies the identical curve and dither: one copy and one draw a frame
                // fewer. 2 runs the recognition and the proof and withholds nothing. 0 does neither. Live.
                int hdr_cg_absorb;
                // fsr_version and fsr_debug_view go to the helper on its command line (--fsr_version=, --fsr_debug_view=) and matter only
                // when the upscaler is FSR. fsr_version 0 (default) takes the version AMD's DLL picks for this GPU; N asks for the newest
                // offered version with major number N (one not offered is a log line and the DLL's pick). fsr_debug_view 1 sets FSR's own
                // debug-view flag: development builds only. Both need a relaunch. The helper's CreateFsrContext has the rules.
                int fsr_version;
                // fsr_warmup (development builds only; a release helper holds -1): -1 (default) gives FSR 4 and newer, and an unknown
                // provider version, one dispatch inside the build that creates their context, because a community FSR 4 build spent its
                // first dispatch past the helper watch's 4 s. 0 never; 1 every FSR context. fsr_debug_view and fsr_warmup are declared
                // inside the development fence below.
                // sync, the Linux sync path: -1 by platform (0 on Windows, 1 under Wine), 0 the native path (the helper's D3D12 shared
                // fences), 1 the mod's own shared semaphores with the probe and the bridge as fallback, 2 the bridge forced. See ivtWine.
                int sync;
                // custom: a render scale in percent. 0 (default) leaves the `quality` table in charge; 34..99 renders at that percent of the
                // screen size in each axis and overrides `quality`. The client is size-agnostic (LOD bias, jitter phase count and target
                // redirection derive from the sizes); DLSS picks the nearest mode, FSR takes any size. 100 or more is DLAA, so it is 0.
                int custom;   // the cfg key "custom": a render size in percent, 34..99, or 0 for the quality preset
                              // responsiveness: 0 off, 1 the frame queue set by the mod, 2 plus the pacer, 3 plus the same-frame upscale, 4 (default) the
                // adaptive ladder. frame_queue: -1 from the mode, 0 touch nothing, 1..3 that depth by hand (overrides a player's dxvk.conf).
                int responsiveness; int frame_queue;   // responsiveness 4, the ladder, is the default
                // pacer: -1 from the mode (on at responsiveness >= 2), 0 off, 1 on. Read by release builds; not in the shipped file.
                int pacer;
                // unpin: 1 (default) gives the game's main thread back the process's processors at the game's first CreateDevice, undoing
                // its start-up pin to CPU 0; 0 leaves the pin. IvtUnpinMainThread (DLSS-IV.cpp) has the rules.
                int unpin;
// hdr_cs_test (development builds only): 1 makes the first present's colour-space check answer "not offered" without asking,
                // to exercise the Linux stand-down on Windows (hdr_encode.h). Fenced here and in the initialiser, so a release build's IvtCfg,
                // on the hot line (.data$dlhot), keeps its layout (tools/prove-fg-off.py). Before rename_sdr because tools/check-cfg.py finds
                // the struct's end by its last field's closing brace.
#if DLSSIV_DEV
                // mv_wheel (development builds only): 1 (default) gives a tyre the car's motion and leaves its spin out of the vectors
                // (VelWheelNoSpin, DLSS-IV.cpp), as a release build always does; 0 keeps the spin, to compare. Live.
                int mv_wheel;
#endif
                // rename_sdr: 1 (default) gives DXVK's instance the hdr_engine_name string in an SDR session too when an NVIDIA GPU drives
                // the primary display, so NVIDIA's driver presents a borderless window with G-Sync; 0 keeps "DXVK" in SDR. 2: as 1, and
                // renamed even where NVIDIA's settings force vsync on a fixed-refresh display, where 1 keeps "DXVK"
                // (IvtDriverVsyncOnFixedRefresh, DLSS-IV.cpp). Not in the shipped file. Appended last so no field before it moves.
                int rename_sdr;
                // cpu_place: 1 puts the threads that set the frame rate each on a logical processor of its own (source/cpu_place.h); 0
                // (default) puts none. Stutter reports came from single-cache CPUs, and the gain was measured on a two-cache CPU only; the
                // shipped file says who may want it on. unpin=0 still switches off everything. Appended last, so no field before it moves.
                int cpu_place; };
// The defaults are the shipping configuration, so a cfg that omits a key gets the right behaviour. The file's presence is
// the master switch: transport defaults to 1, and ivtCfgMissing (set in IvtReadCfg, tested by InitThread before anything is
// installed) is what stops a missing file running the mod on compiled defaults.
// sharpness 0 is a hard gate, not a strength of zero: the shader is never compiled and the pass never reached. (In FSR2 a
// sharpness of 0 still sharpens.)
// The field map. The initialiser is positional: a new field goes at the end of the struct, here and in the initialiser.
// No comment goes inside the initialiser: tools/check-cfg.py counts its commas.
//                              tr md ah fl dm rq  sharp  be hd pw eo hv hp he hrs cl hro hs hcg hui pre log lod  ds mvg  iw thr abs fv syn cus rsp fq pac unp fdv fwu hct rsd   (fdv, fwu, hct: fsr_debug_view, fsr_warmup, hdr_cs_test, fenced: development builds only)
// The effective async_home: responsiveness=3 asks for the same-frame upscale, but the stored cfg keeps what the file said,
// since the live re-parse compares against it. A file that names async_home wins over the mode.
// log is 1 by default (the value after preset's 11), so a player who reports stutter has a log to send.
// mv_glass is 3 by default (the second value after lod's 1.0f): a blended mover that writes another depth than the one the
// guides read is not redrawn into the velocity target, so what moves behind a pane keeps its own vector (VelRecordObject,
// DLSS-IV.cpp). A release build does not read the key: 3 is what it runs.
static bool ivtCfgNamedAsync = false, ivtParseSawAsync = false;   // ivtParseSawAsync is the last parse's; only IvtReadCfg copies it to ivtCfgNamedAsync, so a live edit cannot flip the ladder
DLSSIV_HOT static IvtCfg   ivtCfg       =
                                          { 1, 2, 1, -1, -1, 1, 0.0f, -1, 0, 0, 0, 0, 0, 3, 100, 0, 80, -1, -1, 0, 11, 1, 1.0f, 1, 3, 2, 1, 1, 0, -1, 0, 4, -1, -1, 1,
#if DLSSIV_DEV
                                          1,   // mv_wheel: development builds only
#endif
                                          1,   // rename_sdr
                                          0 }; // cpu_place
static bool IvtOnBridge();   // ivtBridge is declared below
static bool IvtServerUpscaleAvailable();   // vc_server_upscale.h, after the module/path helpers
// The same frame is never taken on the Linux bridge (the relay's round trip would sit inside every frame): modes 3 and 4
// run pipelined there unless the file names async_home itself.
static inline bool IvtAsyncHome() { if (IvtServerUpscaleAvailable()) return false; return ivtCfgNamedAsync ? ivtCfg.async_home != 0 : ((ivtCfg.responsiveness >= 3 && !IvtOnBridge()) ? false : ivtCfg.async_home != 0); }
static IvtCfg   ivtCfgDefaults;                 // the line above, copied before the file is read: live reload starts from it
// What the file asked for before IvtClampCfg: the startup parse runs before LogInit, so IvtLogCfg reports the clamps later.
static IvtCfg   ivtCfgAsked;
static void IvtReportClamps(const IvtCfg& asked, const IvtCfg& used, const char* when);
static bool     ivtCfgRead   = false;
static bool     ivtStarted   = false;
DLSSIV_HOT static bool     ivtFailed    = false;
DLSSIV_HOT static bool     ivtBuilt     = false;
static IDirect3DBaseTexture9* ivtBuiltForScene = nullptr;   // rebuild if the scene buffer changes
static HANDLE   ivtPipe      = nullptr;
static HANDLE   ivtHost      = nullptr;
static bool     ivtPipeOpened = false;   // the pipe was connected at least once this session (IvtShutdown asks)
static int      ivtConnectTries = 0;       // for the connected line only; the budget is ivtSpawnTick's
static ULONGLONG ivtSpawnTick = 0;         // when the helper was started: its pipe has 60 s from here
static ULONGLONG ivtRetryAt  = 0;          // build backoff deadline
static int      ivtBuildFails = 0;

static FeedVk   ivtVk        = {};
static VkDevice ivtDevice    = VK_NULL_HANDLE;
static VkQueue  ivtQueue     = VK_NULL_HANDLE;
static uint32_t ivtQueueFamily = 0;
static ID3D9VkInteropDevice* ivtIDev = nullptr;
// The adapter the game renders on, from Vulkan. Sent to the helper in FeedHello (feed_ipc.h), which reads it before it
// creates its D3D12 device.
static uint8_t  ivtLuid[8] = {};
static bool     ivtLuidValid = false;
static PFN_vkSignalSemaphore      ivtSignalSemaphore = nullptr;
static PFN_vkQueueSubmit          ivtQueueSubmit     = nullptr;
static PFN_vkCreateCommandPool    ivtCreateCommandPool = nullptr;
static PFN_vkAllocateCommandBuffers ivtAllocateCommandBuffers = nullptr;
static PFN_vkBeginCommandBuffer   ivtBeginCommandBuffer = nullptr;
static PFN_vkEndCommandBuffer     ivtEndCommandBuffer = nullptr;
static PFN_vkResetCommandBuffer   ivtResetCommandBuffer = nullptr;

static VkSemaphore ivtSemIn  = VK_NULL_HANDLE, ivtSemOut = VK_NULL_HANDLE;
// The helper watch's three words (IvtHostWatch): the low 32 bits of ivtSentN, stored by whichever thread runs the injection
// body; set by the watch when it has released a stuck helper's wait; its stop.
static volatile LONG ivtWatchAwait = 0;
static volatile LONG ivtWatchFired = 0;
static HANDLE        ivtWatchStop  = nullptr;
static HANDLE        ivtWatchHost  = nullptr;   // the watch's own duplicate of ivtHost, never closed by anyone else
static HANDLE   ivtFenceIn   = nullptr, ivtFenceOut = nullptr;
// The Linux sync path. The two fences the helper creates shared, imported by the game as timeline semaphores, are the
// whole GPU synchronisation between the processes. Some Proton builds emulate D3D12 fence values per process: the import
// (win32u_vkImportSemaphoreWin32HandleKHR, "d3d12 fence from other process") returns success and the values never connect,
// so the game froze at its first injection. Older Protons refuse the D3D12 memory handle type the texture import used.
// Only under Wine does anything change (ivtSyncMode is 0 on Windows):
//   1. Textures are imported as OPAQUE_WIN32 (what vkd3d-proton exports, accepted by every Proton) and the memory-type
//      narrowing is skipped (Wine's property query returns handle-type constants).
//   2. The game creates the two timeline semaphores, exported as OPAQUE_WIN32, duplicates the handles into the helper, and
//      the helper opens them as ID3D12Fences (FeedBuild::sem_in/sem_out).
//   3. A probe proves the link both ways before any GPU wait depends on it, since a broken import says success: both
//      sides CPU-signal and must see the other's. Both seen: fast mode, as on Windows.
//   4. Otherwise the bridge: a relay thread writes each frame message only after the game's copies retired, the helper
//      answers after its out-fence, and the relay signals the out-semaphore from the CPU. No cross-process GPU object.
//   5. Nothing may end in a frozen game: a relay that times out releases the queued GPU wait itself, then hands the stop to
//      IvtTick, as the helper watch does.
// sync = -1 (default): 0 on Windows, 1 under Wine. 0 forces the native path, 1 the shared semaphores with probe and bridge,
// 2 the bridge (which also exercises it on Windows).
static bool     ivtWine       = false;      // ntdll exports wine_get_version: Wine or Proton
// The engine name's prediction (DLSS-IV.cpp, HdrDecideOnce): 1 when DXVK was renamed in SDR for an NVIDIA GPU expected to
// render, 0 when a hybrid kept "DXVK" because another GPU was, -1 when nothing was predicted. Checked once the game's Vulkan
// device is known (IvtEnsureVulkan).
static int      ivtRenamePredict = -1;
// The game's main thread: the one that made the game's device (IvtUnpinMainThread, DLSS-IV.cpp). cpu_place.h walls it.
static DWORD    ivtMainTid = 0;
static char     ivtWineVer[40] = "";
static int      ivtSyncMode   = 0;          // 0 native, 1 the mod's semaphores (fast if proved, bridge if not), 2 bridge forced
static bool     ivtBridge     = false;      // per frame: the body queues the frame message for the relay instead of writing it
static bool IvtOnBridge() { return ivtBridge; }
static bool     ivtSemsMade   = false;      // the two semaphores are the mod's (sync 1 or 2), created before the first build
static bool     ivtProbeDone  = false;
static UINT64   ivtSemHostIn  = 0, ivtSemHostOut = 0;                  // the same, duplicated into the helper: FeedBuild carries them
static const UINT64 kIvtProbeN = 4;         // the probe's value; frame numbers start above it when the semaphores are the mod's
// Mode 4: the same frame is decided per frame by the waiter's measured loop (latAsyncWant), unless the file names async_home
// or the Linux bridge carries the frames.
static inline bool IvtAdaptiveOn() { return !IvtServerUpscaleAvailable() && ivtCfg.responsiveness >= 4 && !ivtCfgNamedAsync && !ivtBridge; }
static inline bool IvtAsyncForCapture() { return IvtAdaptiveOn() ? latAsyncWant != 0 : IvtAsyncHome(); }
// The relay (bridge mode only): a small queue the injection body fills and one thread drains.
#define IVT_RELAY_CAP 6                     // IVT_RING + 2; a static_assert at IVT_RING holds them together
static SRWLOCK  ivtRelayLock  = SRWLOCK_INIT;
static FeedTaggedFrameIvt ivtRelayQ[IVT_RELAY_CAP];
static int      ivtRelayHead  = 0, ivtRelayCount = 0, ivtRelayInFlight = 0;   // under ivtRelayLock
static HANDLE   ivtRelayWake  = nullptr, ivtRelayStop = nullptr, ivtRelayIdle = nullptr;
static HANDLE   ivtRelayPipe  = nullptr;    // the relay's own duplicate of ivtPipe: IvtFail closes ivtPipe from another thread with no join
static HANDLE   ivtRelayAnswer = nullptr;   // auto-reset, duplicated into the helper with the mode message,
                                            // set by the helper after each answer's write; null when the helper cannot (it polls)
// The relay's stop is published in ivtWatchFired as 2 (the helper watch's stop is 1): IvtTick already reads that flag every
// frame, so Windows pays nothing for a thread it never starts.
static char     ivtRelayWhy[200] = "";
#if DLSSIV_DEV
static LONG     ivtRelayTrips = 0;          // log=2: round trips, their worst and summed time (development builds only)
static double   ivtRelayUsMax = 0.0, ivtRelayUsSum = 0.0, ivtRelayGpuUsSum = 0.0;
#endif
static void IvtRelayStop();                 // defined with the relay, below IvtSignalOutTo; IvtFail calls it

// Wine or Windows, decided once at load from ntdll's own export, and the sync mode from it and the cfg.
static void IvtDetectWine()
{
    typedef const char* (__cdecl *WineGetVersion)(void);
    const HMODULE nt = GetModuleHandleA("ntdll.dll");
    const WineGetVersion f = nt ? (WineGetVersion)GetProcAddress(nt, "wine_get_version") : nullptr;
    ivtWine = f != nullptr;
    if (ivtWine) { const char* v = f(); strncpy_s(ivtWineVer, v ? v : "?", _TRUNCATE); }
    ivtSyncMode = ivtCfg.sync >= 0 ? ivtCfg.sync : (ivtWine ? 1 : 0);
}
static void IvtLogPlatform()
{
    if (!ivtWine && ivtCfg.sync < 0) return;      // Windows with nothing asked: not a line
    LogRecord("  platform: %s%s; sync=%d (%s)", ivtWine ? "Wine " : "Windows", ivtWine ? ivtWineVer : "",
        ivtCfg.sync,
        ivtSyncMode == 0 ? "D3D12 shared fences"
        : ivtSyncMode == 1 ? "shared semaphores, the CPU bridge if the probe fails"
        : "the CPU bridge, forced");
}
// Which Wine display driver the game runs on: the same code path can run at very different frame rates on Wayland and on
// XWayland, which the rest of the log cannot tell apart. win32u loads the driver as a module once a window exists, so at
// device creation its name is one GetModuleHandleA away. Wine only.
static void IvtLogWineDisplayDriver()
{
    static bool said = false;
    if (!ivtWine || said) return;
    said = true;
    const bool wl  = GetModuleHandleA("winewayland.drv") != nullptr;
    const bool x11 = GetModuleHandleA("winex11.drv") != nullptr;
    LogRecord("  platform: Wine display driver %s",
        wl && x11 ? "winewayland.drv and winex11.drv (both loaded)"
        : wl  ? "winewayland.drv (Wayland)"
        : x11 ? "winex11.drv (X11 or XWayland)"
        : "none loaded yet");
}
// What the helper actually built, from the ack: the default backend=-1 means the cfg never says which upscaler runs.
static bool   ivtBackendIsFsr  = false;
static HANDLE   ivtTexHandle[FEED_SLOTS] = {};
static VkImage  ivtImg[FEED_SLOTS] = {};
static VkDeviceMemory ivtMem[FEED_SLOTS] = {};
static UINT     ivtW = 0, ivtH = 0;          // the render size: the scene, the guides, the DLSS input
static UINT     ivtOutW = 0, ivtOutH = 0;    // the screen size: the backbuffer, the DLSS output
// The composite redirect: the mod's render-size colour target. From the composite to the injection every back-buffer draw
// lands here, so the depth-only quad and the coronas stay at the render size with the game's scaled depth-stencil.
// It is the resolve's colour input.
static IDirect3DTexture9* ivtRsColorTex = nullptr; static IDirect3DSurface9* ivtRsColorSurf = nullptr;
static bool     ivtRsColorTried  = false;
DLSSIV_HOT static bool     ivtRsDidRedirect = false;    // the redirect ran in THIS frame
static D3DVIEWPORT9 ivtRsSavedVp = {};       // the game's viewport, in screen pixels
DEVSTAT(static LONG     ivtRsRedirects = 0, ivtRsNoRedirect = 0, ivtRsLingering = 0, ivtRsNotQuad = 0;)
DEVSTAT(static LONG     ivtResetsUntrusted = 0, ivtResetsNoGuides = 0;)   // why a reset went out (report only)
DEVSTAT(static LONG     ivtRsUnsettled = 0;)
// Whether the resolve can run in this frame at all: the scene key must have settled, or IvtTick returns before the injection
// and a redirect started in that frame would never be closed.
DLSSIV_HOT static bool     ivtResolveArmed = false;
static DXGI_FORMAT ivtColorFmt = DXGI_FORMAT_UNKNOWN, ivtOutputFmt = DXGI_FORMAT_UNKNOWN;
static UINT64   ivtFrameN    = 0;
static bool     ivtNeedReset = true;

// The mod's command buffers, its own timeline to retire them, the shared set's ownership, the per-frame injection rule.
#define IVT_RING 4
static_assert(IVT_RELAY_CAP == IVT_RING + 2, "the Linux bridge queue holds a ring of frames and two more; IVT_RING moved");
static VkCommandPool   ivtPool = VK_NULL_HANDLE;
static VkCommandBuffer ivtCb[IVT_RING] = {};
static UINT64          ivtCbValue[IVT_RING] = {};
static VkSemaphore     ivtSemLocal = VK_NULL_HANDLE;
static UINT64          ivtSubmitN = 0;
// The highest value a submit actually took. ivtSubmitN is incremented before vkQueueSubmit (it must be in the signal list),
// so a refused submit leaves it one past anything ivtSemLocal can reach, and a waiter asking for it would time out every
// time. Waiters ask for this: the value the GPU will signal.
static UINT64          ivtSubmitOk = 0;
static bool     ivtLayoutInit = false;     // shared images moved UNDEFINED -> GENERAL once
// Who owns each shared image: true = released to the helper's side (VK_QUEUE_FAMILY_EXTERNAL), false = the mod's. Per image,
// because an unmatched queue-family transfer leaves the image's contents undefined. Every command buffer that touches the
// set records and submits under ivtSharedCs and transfers only the images it touches. Today one thread reaches it, so the
// lock is uncontended; it stays so a second recording thread cannot break the pairing of ownership barriers.
static bool     ivtImgReleased[FEED_SLOTS] = {};
static CRITICAL_SECTION ivtSharedCs;
// Initialised once at the top of InitThread, while it is the only thread, not lazily in the guard (a lazy init would race).
// A CRITICAL_SECTION, not an SRWLOCK: the caller holds this lock (see the barrier helper below), so a nested guard is
// possible and recursion may be load-bearing.
struct IvtSharedGuard
{
    IvtSharedGuard()
    {
        EnterCriticalSection(&ivtSharedCs);   // initialised in InitThread: see above
    }
    ~IvtSharedGuard() { LeaveCriticalSection(&ivtSharedCs); }
};
// Which images a command buffer touches, passed to the acquire and the release. The injection passes IVT_SET_INJECT.
enum { IVT_SET_COLOR = 1u << FEED_COLOR, IVT_SET_OUTPUT = 1u << FEED_OUTPUT, IVT_SET_DEPTH = 1u << FEED_DEPTH,
       IVT_SET_MV = 1u << FEED_MV,
       IVT_SET_INJECT = IVT_SET_COLOR | IVT_SET_OUTPUT | IVT_SET_DEPTH | IVT_SET_MV };
static bool     ivtOutValid   = false;     // Output holds a result worth carrying home
static UINT64   ivtSentN      = 0;         // the last frame message sent
// The back buffer rotates: DXVK's D3D9 swapchain swaps the image behind the app-visible surface at Present
// (d3d9_swapchain.cpp, m_backBuffers[i]->Swap), so the same IDirect3DSurface9 is a different VkImage each frame. Ask for the
// image at the injection, never cache it; nor the interop interface, which belongs to the swapped common texture.
// QueryInterface on the surface every injection.
DLSSIV_HOT static int      ivtSceneDraws = 0;         // scene-camera draws seen this frame
DLSSIV_HOT static bool     ivtInjected   = false;     // injected this frame
static LONG     ivtInjections = 0, ivtFramesNoInject = 0;
// Frames on which the game drew no scene at all, so no guides is correct: separates that from the injection rule failing.
static LONG     ivtFramesNoScene = 0;
// One window per line: each sum carries its own sample count and is reset with its max, so a mean is of exactly the samples
// that fed it (the timers do not each run once per injection).
DEVSTAT(static double   ivtInjectUsSum = 0.0, ivtInjectUsMax = 0.0; static LONG ivtInjectUsN = 0;)
DEVSTAT(static double   ivtFlushUsSum = 0.0, ivtFlushUsMax = 0.0;   static LONG ivtFlushUsN = 0;)   // FlushRenderingCommands at the injection (the only site that accumulates here)
// The second flush. The FlushRenderingCommands sites: phase A (only on the depth copy path), the injection (above) and the
// copy home (this one, only at async_home=0).
DEVSTAT(static double   ivtHomeFlushUsSum = 0.0, ivtHomeFlushUsMax = 0.0;)
DEVSTAT(static LONG     ivtHomeFlushes = 0;)
DEVSTAT(static double   ivtLockUsSum = 0.0, ivtLockUsMax = 0.0;     static LONG ivtLockUsN = 0;)    // LockSubmissionQueue .. Release
static LONGLONG ivtQpcFreq = 0;

// Per-300-frame instruments (development builds), changing no behaviour:
//   where      the injection's position in the frame: draw index, scene draws and back-buffer draws before it (about four)
//   host late  at the injection, the out-fence had not yet reached the frame about to be waited on
//   pending    at EndScene, how many of the mod's submits the GPU has not retired
// The draw index is two stores on every draw for a log=3 line: DEVSTAT, gone from a release build.
DEVSTAT(DLSSIV_HOT static int      ivtDrawIndex = 0;)                     // per frame, from IvtOnDraw
DEVSTAT(static LONG     ivtHostLate = 0;)
// Summed per frame, reported per 300 injections, so it needs its own count.
DEVSTAT(static LONGLONG ivtPendingSum = 0;)
DEVSTAT(static LONG     ivtPendingN   = 0;)
static LONGLONG ivtFrameQpcPrev = 0;
// DEVSTAT: read only by the log=2 frametime line. The frame gap line keeps its own test.
DEVSTAT(static double   ivtFtSum = 0.0, ivtFtMax = 0.0; static int ivtFtN = 0, ivtFtOver25 = 0, ivtFtOver40 = 0;)

// The guides. At the injection, before the helper handoff: (A) a submit copies the scene's depth-stencil image into the
// mod's INTZ texture (the same Vulkan format under DXVK, D24_UNORM_S8_UINT; DXVK refuses a depth StretchRect inside a scene,
// and a D3D9 shader cannot sample the game's depth surface), then (B) a full-screen pass samples that snapshot and the
// object velocity target and writes the motion-vector guide (RG16F, pixels) and the depth guide (R32F, reverse-Z), then (C)
// the handoff submit copies colour, depth and vectors to the helper.
DLSSIV_HOT static IDirect3DDevice9*  ivtDev = nullptr;
static IDirect3DTexture9* ivtIntzTex = nullptr;  static IDirect3DSurface9* ivtIntzSurf = nullptr;
static IDirect3DTexture9* ivtMvTex = nullptr;    static IDirect3DSurface9* ivtMvSurf = nullptr;
static IDirect3DTexture9* ivtDepthTex = nullptr; static IDirect3DSurface9* ivtDepthSurf = nullptr;
static IDirect3DVertexShader9* ivtFsVS = nullptr;
static IDirect3DPixelShader9*  ivtGuidesPS = nullptr;   // both guides, two render targets
// The game's dither, smoothed before the upscaler (shaders/ivt_dedither_ps.hlsl says what and why).
// ivtDedTex is the mod's: render size, the scene buffer's format, written only where a mark reaches (never cleared); the
// second draw (DedInPlace) puts the changed pixels into the scene buffer, which the upscaler is given. Only the development
// view writes a whole copy here. ivtDedMaskTex is the game's: the texture behind the scene's third extra target (index 2:
// FusionFix's deferred passes write the mark to oC2.w), held from the moment it proved to be an A8R8G8B8 of the render size,
// released in IvtReleaseGuides before any Reset; the hold makes the address-keyed answer sound. ivtDedSeenSurf is borrowed
// and used only on the frame it was noted (hkSetRenderTarget notes it, IvtDedProve proves it at the injection).
// The mark is FusionFix's or it is not read: the stock game writes the material's alpha to that channel, 1 nearly
// everywhere, and the test would then dim one-pixel lines over the whole picture (tools/test-dedither, proof 4). So the
// pass runs only after (a) FusionFix's constants were seen (shFlags, its c227 pack) and (b) a bound pixel shader carried the
// tag every FusionFix shader holds, the twelve bytes "FusionShader" in a def. Looked for at the injection until found or
// kDedFusionLooks frames have said no.
static IDirect3DTexture9*     ivtDedTex = nullptr;      static IDirect3DSurface9* ivtDedSurf = nullptr;
static IDirect3DPixelShader9* ivtDedPS = nullptr;
static IDirect3DPixelShader9* ivtDedHomePS = nullptr;   // the pass's second draw: its changed pixels into the scene buffer
static bool                   ivtDedTried = false, ivtDedOK = false;
static IDirect3DSurface9*     ivtDedSeenSurf = nullptr;   // not on the hot line: written a few times a frame, read once
static LONG                   ivtDedSeenFrame = -1;
static int                    ivtDedFusion = 0, ivtDedFusionLooks = 0;   // 1: the tag was found; the looks made so far
static const int              kDedFusionLooks = 300;
static IDirect3DSurface9*     ivtDedMaskSurf = nullptr; // the surface the answer below is for: an address, only compared
static IDirect3DTexture9*     ivtDedMaskTex = nullptr;  // held; nullptr when that surface is not a mark this pass can read
static int                    ivtDedSaid = 0;           // the lines about the mark, capped: a session has one surface or two
// The pass leaves itself out while nothing is dithered: every fourth frame one small draw inside an occlusion query
// (DedCount, one texel in sixteen) asks whether the mark is set anywhere; the answer is never waited for. While the last
// answer is zero the pass is not drawn. An unknown answer, a query that could not be made or one that stops answering all
// mean the pass runs: a failed question costs time, never the picture. Touched by the injection's body alone, and by the
// release with the guides.
static IDirect3DPixelShader9* ivtDedCountPS = nullptr;
static IDirect3DQuery9*       ivtDedCountQ = nullptr;
static bool                   ivtDedCountDead = false;   // no answer in 240 frames: not asked again. The query is released on the render thread (IvtReleaseGuides), never the worker
static bool                   ivtDedCountOut = false;   // a count was issued and its answer has not come yet
static int                    ivtDedCountWait = 0;      // frames it has been out
static LONG                   ivtDedLattice = -1;       // the last answer: texels of the one-in-sixteen lattice that hold the mark; -1 not known yet
static ULONG                  ivtDedBeat = 0;           // frames the body asked what to draw: the count goes out on every fourth
enum { DED_COUNT = 1, DED_PASS = 2, DED_PROBE = 4 };    // what IvtDedPlan asks of this frame
DEVSTAT(static LONG           ivtDedIdle = 0, ivtDedCounts = 0, ivtDedLatticeMax = 0;)   // this period: frames the pass was left out, counts issued, the largest answer
// A temporal resolve reset every frame degrades to a single-frame upscale: these count the resets.
DEVSTAT(static LONG     ivtResetsSent = 0, ivtValidEdges = 0, ivtInvalidFrames = 0;)
static bool     ivtFrameValid = true;                   // this frame's vectors are trustworthy
static bool     ivtPrevFrameValid = true;               // and the previous frame's
DEVSTAT(static LONG     ivtSkippedQuads = 0, ivtInjectAtEndScene = 0;)
DLSSIV_HOT static bool     ivtCaptured = false;        // this frame's capture is done
static bool ivtHomeAsync = true;   // the pending home's own flag (mode 4 flips per frame); true = pipelined, nothing to home here
DLSSIV_HOT static bool     ivtHomePending = false;     // the result has yet to be put back
static UINT64   ivtHomeN = 0;               // the frame the pending home belongs to
DEVSTAT(static LONG     ivtTwoPoint = 0, ivtHomeAtEndScene = 0;)
// The resolve lands in a screen-size texture of the scene buffer's format, handed to the game wherever it binds the scene
// buffer, so the whole post chain consumes it (substituting only at the composite would leave depth of field on the
// jittered original).

static IDirect3DVertexDeclaration9* ivtFsDecl = nullptr;
static bool     ivtGuidesTried = false, ivtGuidesOK = false;
DLSSIV_HOT static bool     ivtInOwnDraw = false;
// Three declarations in this order on purpose: the middle one is not development-only, and declaring it after the other two
// moves it in memory in a release build.
DEVSTAT(static LONG     ivtDepthCopies = 0;)
static LONG     ivtDepthMissing = 0;
DEVSTAT(static LONG     ivtDepthFormatMismatch = 0;)
DEVSTAT(static double   ivtPhaseAUsSum = 0.0, ivtPhaseAUsMax = 0.0, ivtPassesUsSum = 0.0, ivtPassesUsMax = 0.0;)
DEVSTAT(static int      ivtDsChangesSum = 0, ivtDsClearsSum = 0;)
static bool     ivtSaidDsFormat = false;
// When the snapshot is taken: the scene's depth is cleared once per frame between the last scene draw and the injection, so
// the copy is taken in the Clear hook before that clear; the injection-time copy is only for a frame that saw no such clear.
DEVSTAT(static LONG     ivtSnapAtClear = 0, ivtSnapAtInject = 0;)
// The depth without the copy, decided once at the first scene-depth clear:
//   1 direct      the scene depth-stencil is a depth texture; sample it
//   2 substitute  bind the mod's INTZ in its place when the game binds it; sample that
//   0 copy        phase A
static int      ivtDepthMode = -1;                      // -1 undecided
static IDirect3DTexture9* ivtGameDsTex = nullptr;       // direct: the game's depth texture (referenced)
static IDirect3DSurface9* ivtGameDsSurf = nullptr;      // direct: its surface (compare only)
static IDirect3DSurface9* ivtSceneDsOrig = nullptr;     // substitute: the surface the game binds (compare only)
static bool     ivtSubstituteLive = false;              // substitute: the mod's INTZ is what the game draws into
static bool     ivtGuidesThisFrame = false;             // the guide pass ran (any mode)
DEVSTAT(static LONG     ivtGuidesAtClear = 0, ivtGuidesAtInject = 0, ivtSubstitutions = 0;)
DEVSTAT(static LONG     ivtDedDrawn = 0, ivtDedWithGuides = 0, ivtDedNoMark = 0;)   // the dither pass, this period: frames drawn, those in the guides' bracket, frames with no mark

// The depth the post chain reads, steadied (shaders/ivt_steady_ps.hlsl says what and why).
// Which texture: behind the seam the post chain samples the scene's depth from one of two textures:
//   [1] FusionFix's own copy (its PreAlphaDepthCopyRT, [POSTFX] EnablePreAlphaDepth, on by default): an R32F render target
//       of the render size, bound at stage 1 for its depth of field, sun shafts and the game's composite. Not known in
//       advance, so it is met: the bind hook sees an unknown texture go to stage 1 behind the seam, asks the texture what it
//       is (IvtSdMeet: no device call), and holds it for the next post.
//   [0] the scene's own depth texture (ivtGameDsTex), for an install without FusionFix's copy: steadied only while no copy
//       was bound behind the seam in the frame before.
// Only while something blurs by it: FusionFix binds its copy once a frame for the composite whatever its settings, and once
// more per pass of its own that reads depth. With its depth of field off, the composite's depth blur is zero (timecyc.ixx).
// So the copy is steadied only when bound at least twice behind the seam in the frame before; otherwise nothing is drawn,
// and after 1200 frames its targets are given back. ([0] is steadied whenever it was bound at all.)
// In place: at the injection the resolve writes what it holds into its own target, and a second draw writes the held values
// that differ back into the game's texture (transport/steady_depth_pass.h: SdResolve, SdHome). The game rewrites the texture
// next frame, so nothing of the mod's outlives the frame in it. The pair of G16R16F targets is held from frame to frame.
// Who touches what: src, h and their surfaces are made and released on the render thread with the device thread parked or
// drained; cur and seen by the render thread alone; hist and last by the injection's body alone. The body draws from the
// pointers its command carries, never from this table's cur.
struct IvtSd
{
    IDirect3DTexture9* src;                             // [1] only: FusionFix's copy, held ([0]'s is ivtGameDsTex)
    IDirect3DTexture9* h[2]; IDirect3DSurface9* hs[2];  // what the resolve holds: this frame's and the frame before's, in turn
    int  cur;                                           // which of the two is this frame's (set at the capture)
    LONG seen;                                          // the last frame the post chain bound the game's texture behind the seam
    LONG binds, bindsWas;                               // how often in that frame, and in the frame before it
    LONG unserved;                                      // captures in a row that did not steady it while it had targets
    bool hist; LONG last;                               // the body's: the other held target is the frame before's; the frame last drawn
    bool refused;                                       // its targets could not be made: not asked again this device
};
static IvtSd                  ivtSd[2];
static IDirect3DPixelShader9* ivtSdPS = nullptr;                        // the resolve
static IDirect3DPixelShader9* ivtSdHomePS = nullptr;                    // the home
static bool                   ivtSdTried = false, ivtSdOK = false;      // the two shaders
static IDirect3DTexture9*     ivtSdCand = nullptr;                      // held: a copy met behind the seam, for the next post to take in
static IDirect3DBaseTexture9* ivtSdRej[16]; static int ivtSdRejN = 0, ivtSdRejAt = 0;   // met and not a depth copy: addresses, only compared
static const LONG             kSdGiveBack = 1200;                       // captures unsteadied after which an entry's targets are given back
static int                    ivtSdSaid = 0;                            // its lines about targets made and given back, capped
DEVSTAT(static LONG           ivtSdDrawn[2] = {}, ivtSdSeeds[2] = {}, ivtSdLeftOut = 0, ivtSdStill = 0;)   // this period: frames resolved per entry, seeds, frames with nothing steadied, frames the camera stood still

// The rain, kept out of the upscaler (shaders/ivt_rain_ps.hlsl says why).
// The rain is one draw: thousands of instances of one quad (gta_rmptfx_gpurender, technique draw_Rain_VERTEXTEX). Its
// signature: two triangles on four vertices, and a vertex shader with exactly two texldl (position and velocity textures).
// The draw is redirected, not repeated (DLSS-IV.cpp, RainBefore and RainAfter, in hkDrawIndexed): render target 0 is this
// layer for that one draw, with the viewport and scissor put back each way (a target bind resets both). Everything else is
// the game's. The layer has the scene buffer's size and format, is made on the first rain draw, cleared on the first rain
// draw of each frame, and given back after 1200 frames without rain. Only before the seam, only into the scene buffer, and
// only on a frame jittered for an upscaler.
// It comes back at the injection (IvtRainLay), added to the texture the upscaled picture is homed into. A frame whose
// injection does not run loses that frame's rain.
// Who touches what: texture, surface, size and frame: the render thread. The shader: made once on the render thread, read by
// the body, which draws from what its command carries.
static IDirect3DTexture9*     ivtRainTex = nullptr;
static IDirect3DSurface9*     ivtRainSurf = nullptr;
static UINT                   ivtRainW = 0, ivtRainH = 0;
static D3DFORMAT              ivtRainFmt = D3DFMT_UNKNOWN;
static LONG                   ivtRainFrame = -1;                        // the last frame a rain draw went into the layer
static bool                   ivtRainRefused = false;                   // the layer or its shader could not be made: not asked again this device
static IDirect3DPixelShader9* ivtRainPS = nullptr;
static const LONG             kRainGiveBack = 1200;                     // frames without rain after which the layer is given back
DEVSTAT(static LONG           ivtRainMrt = 0;)                          // this period: rain draws left alone because the game had more than one target bound
DEVSTAT(static LONG           ivtRainTaken = 0, ivtRainPosted = 0, ivtRainLaid = 0;)   // this period: rain draws made into the layer, frames the capture carried it, frames it was laid over the picture

// The coronas' depth, held while the camera stands still (shaders/ivt_corona_ps.hlsl says what and why; the draw and the
// arithmetic are in corona_pass.h, proved by tools/test-corona against FusionFix's corona shader).
// The draw hook (DLSS-IV.cpp, CorHoldBefore; render thread, before the seam) notes each corona draw's transform without the
// jitter and the rows the device holds, in ivtCorNow. A draw whose transform is in the snapshot's list is given the held
// texture on the first vertex sampler and the snapshot frame's rows, for that one draw.
// The capture (IvtCaptureCmd; render thread, the seam) decides the body's job: the snapshot when the camera stood still for a
// frame (a corona transform of this frame was in the frame before) and this frame's jitter is the cycle's smallest; the
// movers while a snapshot stands (a draw of this frame was served from it); otherwise nothing. A moving camera ends the hold
// within the frame, since no transform matches the snapshot.
// Every input of the test the frame changes is held: the depth, rows c8..c11, and FusionFix's depth pair c227 (which the
// game moves with its far clip while the camera stands still).
// The texture: R32F, the scene depth's size, made and given back at the post with the device thread parked (IvtCorProve);
// default pool, so it goes before any Reset.
struct IvtCorRow { float u[16]; float dev[16]; float lg[4]; };         // rows c8..c11 without the jitter and as the device held them, and c227
static const int              kCorRows = 8;                             // distinct transforms among one frame's corona draws (the game makes about four draws, with one)
static IvtCorRow              ivtCorNow[kCorRows], ivtCorWas[kCorRows], ivtCorSnap[kCorRows];   // this frame's, the frame before's, the snapshot's
static int                    ivtCorNowN = 0, ivtCorWasN = 0, ivtCorSnapN = 0;
static LONG                   ivtCorSeen = -1;                          // the last frame a corona draw was seen in (render thread)
static LONG                   ivtCorSnapFrame = -1;                     // the frame the held texture was asked to be a picture of; -1: nothing is held
static LONG                   ivtCorCapFrame = -1;                      // the last frame the capture gave the body a job
static int                    ivtCorServed = 0, ivtCorStillN = 0;       // this frame: draws served from the snapshot; draws whose transform the frame before had
static bool                   ivtInjectAtSeam = false;                  // the seam's injection (scene complete, post chain not begun): the only one that gives the hold a job
static IDirect3DTexture9*     ivtCorTex = nullptr;
static IDirect3DSurface9*     ivtCorSurf = nullptr;
static UINT                   ivtCorW = 0, ivtCorH = 0;
static IDirect3DPixelShader9* ivtCorPS = nullptr;
static bool                   ivtCorRefused = false;                    // the texture or its shader could not be made: not asked again this device
static LONGLONG               ivtCorQpc = 0;                            // the frame's time when the capture last saw a corona draw, for the movers' push
static LONG                   ivtCorQpcFrame = -1;
static const LONG             kCorGiveBack = 1200;                      // frames without a corona draw after which the texture is given back
// The period's counts (development): whether the hold engaged, and if not, why.
DEVSTAT(static LONG           ivtCorDraws = 0, ivtCorGiven = 0, ivtCorOdd = 0, ivtCorSnaps = 0, ivtCorMoverPasses = 0, ivtCorNoVec = 0;)
DEVSTAT(static LONG           ivtCorFrames = 0, ivtCorFramesStill = 0, ivtCorFramesHeld = 0, ivtCorOther12 = 0;)   // frames with a corona draw; camera still; a draw served; twelve-texldl draws that are not coronas
DEVSTAT(static LONG           ivtCorEndMoved = 0, ivtCorEndNoJob = 0, ivtCorEndAge = 0, ivtCorEndCant = 0;)        // why a hold ended
DEVSTAT(static float          ivtCorMoveMin = 1.0e9f, ivtCorMoveMax = 0.0f, ivtCorOddMin = 1.0e9f, ivtCorOddMax = 0.0f;)   // how far apart: a draw from the frame before's; an unserved draw from the snapshot's
DEVSTAT(static LONG           ivtCorCant = 0; static unsigned ivtCorCantWhy = 0;)   // frames with corona draws on which the capture could give no job, and why (bits, see the period's line)
DEVSTAT(static LONG           ivtCorHook[4] = {}, ivtCorOtherRT = 0;)   // signature draws by draw call before any gate; those that passed into a target other than the scene buffer

static bool IvtSceneSeenThisFrame() { return ivtSceneDraws > 0 && !ivtInjected; }
// Will the resolve run this frame? It is the only path by which the scene reaches the game's buffer, so when it will not run
// something else must carry the scene across.
static bool IvtResolveWillRun()
{
    return !dead && !ivtFailed && ivtBuilt && ivtResolveArmed;
}

static bool IvtInjectedThisFrame()  { return ivtInjected; }

DEVSTAT(static int         ivtSrBeforeSum = 0, ivtSrAfterSum = 0, ivtSrOtherSum = 0;)

// Watchdog: every blocking call on the mod's path goes through this; a call over 50 ms is named with the frame and its
// state. Behind telemetryOn. Slow calls and frame gaps have separate budgets of 40 lines, so a run of slow calls cannot
// silence the worse event; both keep counting, and the session's last line gives the totals.
static LONG ivtWatchSlow = 0;   // per-frame calls over 50 ms (not the start-up ones)
static LONG ivtFrameGaps = 0;   // frames over 500 ms
struct IvtWatch
{
    const char* name; LONGLONG t0; const bool startup;
        // `startup`: a call expected to take this long once, while a feature is created (the build exchange). Said at log=2 only;
    // every per-frame call stays at log=1.
    IvtWatch(const char* n_, bool startup_ = false) : name(n_), t0(0), startup(startup_) { if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&t0); }
    ~IvtWatch()
    {
        if (!telemetryOn) return;
        LONGLONG t1 = 0; QueryPerformanceCounter((LARGE_INTEGER*)&t1);
                // The frequency is fixed for the life of the process.
        static LONGLONG f = [] { LONGLONG v = 0; QueryPerformanceFrequency((LARGE_INTEGER*)&v); return v; }();
        const double ms = f ? (double)(t1 - t0) * 1e3 / (double)f : 0.0;
        if (ms > 50.0)
        {
            // LogRecord, not LogHealth: the shipped cfg offers log 0 and 1, and a hang report needs this line. Capped at 40 a session.
            if (startup) LogHealth("  transport: SLOW %s: %.0f ms at frame %ld", name, ms, (long)frames);
            else if (InterlockedIncrement(&ivtWatchSlow) <= 40) LogRecord("  transport: SLOW %s: %.0f ms at frame %ld", name, ms, (long)frames);
        }
    }
};

// Forward: IvtFail must release the queued GPU wait; the signal helper is declared further down.
static void IvtReleaseQueuedWait();
static void IvtHostWatchStart();
static void IvtHostWatchStop();
static void LatWaiterStart();
static void LatWaiterStop();
static void LatSummaryLine(bool final);
static int  LatSummaryText(char* out, size_t cap, bool final);   // the same text into a buffer: LogSessionOver writes it through its own handle

// Everything the transport needs, asked at the game's CreateDevice. rsActive is decided there, before the game's render
// targets exist, so a later transport failure would leave the session rendering small and blurry. Failing here means
// RsDecideRenderSize refuses on ivtFailed: native game, one clear log line. It does not block CreateDevice on the
// handshake; the capability word in FeedHelloAck tells the client what the helper can do.
static bool IvtProbeCapable(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, IDirect3DDevice9* dev)
{
    if (!d3d || !dev) return true;   // nothing to judge on; leave the later paths to it
        // The three formats the guides are made of. D3DFMT_INTZ is a FOURCC, not an enum value.
    const D3DFORMAT kIntz = (D3DFORMAT)MAKEFOURCC('I','N','T','Z');
    struct { D3DFORMAT f; DWORD usage; const char* what; } kNeed[] = {
        { D3DFMT_G16R16F, D3DUSAGE_RENDERTARGET,  "G16R16F render target (the motion vectors)" },
        { D3DFMT_R32F,    D3DUSAGE_RENDERTARGET,  "R32F render target (the depth guide)" },
        { kIntz,          D3DUSAGE_DEPTHSTENCIL,  "INTZ depth-stencil (sampleable depth)" },
    };
    for (int i = 0; i < 3; i++)
    {
        if (FAILED(d3d->CheckDeviceFormat(adapter, type, D3DFMT_X8R8G8B8, kNeed[i].usage,
                                          D3DRTYPE_TEXTURE, kNeed[i].f)))
        {
            LogRecord("  transport: the device cannot provide a %s: render scale and resolve OFF", kNeed[i].what);
            return false;
        }
    }
        // The mod's own shaders: a rejection means the header and the .asi disagree, a build fault no player can fix, and it must
    // not soften the session silently. Created and released at once: the answer is what is wanted.
    IDirect3DPixelShader9* ps = nullptr;
    if (FAILED(dev->CreatePixelShader((const DWORD*)kIvtGuidesPS, &ps)) || !ps)
    {
        LogRecord("  transport: the device refused the guide pixel shader: render scale and resolve OFF");
        return false;
    }
    ps->Release();
    return true;
}

// What raises the shader model for FSR 4, one text for the warning and the stop note. The helper takes a 64-bit
// D3D12Core.dll in its D3D12 folder (OptInAgilityRuntime: 1.600 and up; Windows refuses a preview build with 0x887E0003,
// D3D12_ERROR_INVALID_REDIST). `refused`: the helper says a file is there that it does not run on. Under Wine the D3D12 is
// vkd3d-proton, and what it offers comes with the Proton version and the graphics driver, not a Microsoft file.
static bool ivtFsr4SkipSaid = false;
static const char* IvtAgilityAdvice(bool refused)
{
    if (ivtWine)
        return "under Proton or Wine, Direct3D 12 comes from vkd3d-proton: a newer Proton and graphics driver offer a higher"
               " shader model.";
    return refused
        ? "the D3D12Core.dll in " DLSSIV_HOST_DIR "\\D3D12\\ could not be used. It has to be the 64-bit file (build\\native\\bin\\x64)"
          " from a RELEASE version of Microsoft's Agility SDK, not a preview version."
        : "download a RELEASE version (not a preview) of Microsoft's Agility SDK, the Microsoft.Direct3D.D3D12 package, and"
          " copy its 64-bit D3D12Core.dll (build\\native\\bin\\x64) into the game's " DLSSIV_HOST_DIR "\\D3D12\\ folder. Make the"
          " D3D12 folder if it is not there.";
}

static volatile LONG ivtFailOnce = 0;   // one thread fails the session
static void IvtFail(const char* why)
{
        // The game's thread and the device thread (or the relay) can both get here: the interlocked latch lets exactly one in.
    // ivtFailed stays what everything reads.
    if (InterlockedCompareExchange(&ivtFailOnce, 1, 0) != 0) return;
    ivtFailed = true;
    FGX(FgxStandDown();)                           // frame generation's set is no longer one to use (a store; nothing waits)
    FGX(FgpStop("the transport stopped", false);)  // and its machine closes: flags only, its threads told (joined below, after the releases)
    LogRecord("  transport: STOPPED: %s", why);
        // Said where log=0 can see it: the mod has given up for the session, and IvtFatalNote writes one plainly named file that
    // exists only on failure. The injection count says whether it stopped at start or after thousands of frames.
    IvtFatalNote(why, (long)ivtInjections, rsActive);   // any render size below the screen, the preset's or a custom one
        // Say what the player will see. The render scale cannot be stood down here: rsActive is decided at CreateDevice and the
    // game's screen-size targets are created once at the render size. So the scene keeps rasterising small, nothing
    // reconstructs it, and the picture is soft until a restart. kIvtSoftRecovery is one constant so the note and this line agree.
    if (rsActive)
        LogRecord("  render scale: the scene stays at %ux%u for %ux%u with no upscale: the picture is SOFT. %s",
                  rsW, rsH, rsOutW, rsOutH, kIvtSoftRecovery);
        // Order matters: release the GPU wait before closing the pipe. Every injection past the first queues a wait on ivtSemOut
    // for the frame the helper is about to produce, and the helper's only release of it is Signal(UINT64_MAX) after its message
    // loop breaks, which a TerminateProcess or a fault skips. Vulkan batches begin in submission order, so every later DXVK
    // submit, Present included, would queue behind it: a hard freeze with the GPU idle and no TDR. So do here what the helper's
    // clean exit does; the game keeps rendering.
    IvtHostWatchStop();          // first: from here this function is the only signaller
    LatWaiterStop();             // the latency instrument's waiter leaves the timelines before anything else moves
    IvtRelayStop();              // the Linux bridge's relay, likewise (it releases the wait itself when it is the one that stopped)
    IvtReleaseQueuedWait();
    FGX(FgxReleasePrepWait();)   // and frame generation's own queued wait on the helper's prepare: the same freeze, the same release
    FGX(FgpJoin();)              // joined only now: a join of up to 600 ms before the releases would hold the game's queue that long
        // A helper that never had a client: the pipe is not open, so the close below cannot reach it, and it would sit in
    // ConnectNamedPipe for the session holding a D3D12 device and NGX. Once connected, the pipe break is its exit.
    if (!ivtPipe && ivtHost)
    {
        TerminateProcess(ivtHost, 0); CloseHandle(ivtHost); ivtHost = nullptr;
                // Said here: a terminated process writes no exit line, and this is the path of the commonest misinstall (a runtime other than DXVK).
        LogRecord("  transport: host started but never connected; ended");
    }
    if (ivtPipe) { CloseHandle(ivtPipe); ivtPipe = nullptr; }   // the helper exits on the pipe break
        // Except a helper the watch found stuck, which may never read its pipe again and would keep the shared set's memory and NGX.
    if (ivtWatchFired == 1 && ivtHost) { TerminateProcess(ivtHost, 0); CloseHandle(ivtHost); ivtHost = nullptr; }   // 1: the watch's stop; 2 is the relay's, and the pipe break ends the helper
}

static bool IvtHostAlive()
{
    return ivtHost != nullptr && WaitForSingleObject(ivtHost, 0) == WAIT_TIMEOUT;
}

static void IvtGameDir(char* out, size_t cap)
{
    out[0] = 0;
    if (!GetModuleFileNameA(nullptr, out, (DWORD)cap)) return;
    char* s = strrchr(out, '\\');
    if (s) *(s + 1) = 0;
}

// The folder this .asi sits in, not necessarily the game's. GTA IV's loader takes .asi files from the game root and from
// plugins\, and mods there keep their .ini beside themselves. Everything else is anchored to the game exe, but the cfg is
// looked for in both places, so DLSS-IV.cfg beside an .asi in plugins\ is not a silently dead install.
static HMODULE selfModule = nullptr;
static void IvtOwnDir(char* out, size_t cap)
{
    out[0] = 0;
    if (!selfModule || !GetModuleFileNameA(selfModule, out, (DWORD)cap)) return;
    char* s = strrchr(out, '\\');
    if (s) *(s + 1) = 0;
}

#include "../vc_server_upscale.h"

// The key loop, shared by the startup read and the live reload. Every key lands in `c`, never in ivtCfg directly, so a live
// reload can parse into a scratch copy and apply only what may change while running.
// A value that is not a number must not become zero (atoi("high") is 0): an unreadable value keeps the shipping default.
// `c` already holds the default (or, on a live reload, the running value), so refusing to assign is the fallback.
// Scanned by hand, not with strtod: strtod accepts "0x10" as hexadecimal and accepts "inf" and "nan", which atoi and atof
// then turn into a silent zero. The grammar accepted is what atoi and atof mean by a number:
//     [+-]? ( digit+ ( '.' digit* )? | '.' digit+ ) ( [eE] [+-]? digit+ )?
// followed by end of string, whitespace, or a trailing comment. Anything else keeps the default.
static bool IvtValueIsNumber(const char* v)
{
    if (!v) return false;
    const char* p = v;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '+' || *p == '-') p++;
    int digits = 0;
    while (*p >= '0' && *p <= '9') { p++; digits++; }
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { p++; digits++; } }
    if (digits == 0) return false;                 // no digits at all, so not a number
    if (*p == 'e' || *p == 'E')
    {
        const char* save = p;
        p++;
        if (*p == '+' || *p == '-') p++;
        int ed = 0;
        while (*p >= '0' && *p <= '9') { p++; ed++; }
        if (ed == 0) p = save;                     // a bare trailing 'e' is not an exponent
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '#' || *p == ';') return true;       // a trailing comment
    if (p[0] == '/' && p[1] == '/') return true;
    return *p == 0;                                // otherwise it must be the end
}

// What the parser could not use, held until the log exists (IvtParseCfg runs before LogInit; IvtLogCfgSource says it).
// Bounded, so a pathological file cannot grow it.
#define IVT_CFG_COMPLAINTS 8
static char ivtCfgBad[IVT_CFG_COMPLAINTS][96] = {};
static int  ivtCfgBadN    = 0;    // how many are stored
static int  ivtCfgBadSeen = 0;    // how many there were, which can exceed the store

static void IvtNoteBadCfg(const char* fmt, const char* k, const char* v)
{
    ivtCfgBadSeen++;
    if (ivtCfgBadN >= IVT_CFG_COMPLAINTS) return;
    _snprintf_s(ivtCfgBad[ivtCfgBadN], sizeof(ivtCfgBad[0]), _TRUNCATE, fmt, k, v);
    ivtCfgBadN++;
}

// A whole number in any spelling IvtValueIsNumber accepts (atoi stops at '.' or 'e', so 1e3 would be 1). 1e3 is 1000 and
// 2.0 is 2; a fraction or a value past an int's range is refused and said.
// Returns 0: a whole number, in *out; 1: a fraction; 2: past an int's range.
static int IvtWholeInt(const char* v, int* out)
{
    const double d = atof(v);                                        // all of what IvtValueIsNumber accepted
    if (!(d >= -2147483648.0 && d <= 2147483647.0)) return 2;       // 3e9, 1e400
    const int i = (int)d;
    if ((double)i != d) return 1;                                    // 0.8, 2.5
    *out = i; return 0;
}

static void IvtParseCfg(FILE* f, IvtCfg* c)
{
        // 1024; see the split guard at the top of the loop.
    char line[1024];
    bool firstLine = true;
        // Each parse describes its own file, so a live reload that fixes a typo stops reporting it.
    ivtCfgBadN = 0; ivtCfgBadSeen = 0;
    while (fgets(line, sizeof(line), f))
    {
                // The line ending comes back from fgets: trimmed once here, since values are quoted back in log lines.
        size_t ln = strlen(line);
                // A line too long for the buffer is one line, not two: fgets hands the remainder back on the next call, where the comment
        // and BOM guards would see the middle of a line. The first chunk (key, '=' and value) is parsed; the overflow is dropped
        // and named. Tested on the buffer's size, not ln > 0: fgets returns without a newline only when it filled the buffer, and
        // a NUL byte makes strlen stop early. Residual: a line both over-long and holding a NUL is not drained.
        if (ln == sizeof(line) - 1 && line[ln - 1] != '\n')
        {
            int ch = 0; size_t dropped = 0;
            while ((ch = fgetc(f)) != EOF && ch != '\n') dropped++;
            if (dropped)
            {
                char head[48];
                _snprintf_s(head, sizeof(head), _TRUNCATE, "%s", line);
                IvtNoteBadCfg("a line starting \"%s\" is longer than %s bytes;"
                    " the overflow was ignored", head, "1023");
            }
        }
        while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r')) line[--ln] = 0;
        char* k = line;
                // A UTF-8 BOM, which editors add unasked: EF BB BF in front of the first key would make it match nothing.
        if (firstLine)
        {
            firstLine = false;
            // UTF-16 (Notepad's "Unicode" save): every line is NUL-interleaved and every key would be lost. Say it once and stop; the
            // compiled defaults then stand. A UTF-16 file without a BOM shows as one printable byte and then a NUL.
            if (((unsigned char)k[0] == 0xFFu && (unsigned char)k[1] == 0xFEu) ||
                ((unsigned char)k[0] == 0xFEu && (unsigned char)k[1] == 0xFFu) ||
                (ln == 1 && (unsigned char)k[0] >= 0x20u))
            {
                IvtNoteBadCfg("%s%s", "the file is saved as UTF-16, so no setting in it was read; save it as UTF-8 or ANSI", "");
                // And say it somewhere: the compiled default is log=0, and the player asked for a log by shipping a cfg with one.
                if (c->log < 1) c->log = 1;
                return;
            }
            if ((unsigned char)k[0] == 0xEFu && (unsigned char)k[1] == 0xBBu
                                             && (unsigned char)k[2] == 0xBFu) k += 3;
        }
                // Leading whitespace on the key.
        while (*k == ' ' || *k == '\t') k++;
                // A comment is not a key: the shipped file's comment lines are full of '='.
        if (*k == '#' || *k == ';' || (k[0] == '/' && k[1] == '/')) continue;
        char* eq = strchr(k, '=');
        if (!eq) continue;
        *eq = 0;
        for (char* e = eq - 1; e >= k && (*e == ' ' || *e == '\t'); e--) *e = 0;
        char* v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
                // The guard, here rather than at every assignment: skipping the chain leaves every field of `c` at its value, which is the
        // shipping default. No key can be forgotten.
        if (!IvtValueIsNumber(v))
        {
            IvtNoteBadCfg("%s = \"%s\" is not a number", k, v);   // the warnings file's wrapper says the default was kept
            continue;
        }
        int iv = 0;
        const int whole = IvtWholeInt(v, &iv);   // 0 whole, 1 a fraction, 2 out of range
        const IvtCfg before = *c; const bool sawAsyncBefore = ivtParseSawAsync;   // a refused fraction puts the line back (below)
        bool known = true;
        if      (_stricmp(k, "transport")   == 0) c->transport   = iv;
        else if (_stricmp(k, "mode")        == 0) c->mode        = iv;
        else if (_stricmp(k, "async_home")  == 0) { c->async_home  = iv; ivtParseSawAsync = true; }
        else if (_stricmp(k, "depth_mode")  == 0) c->depth_mode  = iv;
        else if (_stricmp(k, "quality") == 0) c->quality = iv;
        else if (_stricmp(k, "custom")  == 0) c->custom  = iv;
        else if (_stricmp(k, "responsiveness") == 0) c->responsiveness = iv;
        else if (_stricmp(k, "frame_queue") == 0) c->frame_queue = iv;
        else if (_stricmp(k, "pacer") == 0) c->pacer = iv;
        else if (_stricmp(k, "unpin") == 0) c->unpin = iv;   // 1 (default) undo the game's CPU 0 pin of its main thread, 0 leave it; needs a relaunch
        else if (_stricmp(k, "rename_sdr") == 0) c->rename_sdr = iv;   // 1 (default) the engine rename in SDR too where NVIDIA renders; 0 keeps "DXVK"; 2 also under forced vsync on fixed refresh
        else if (_stricmp(k, "cpu_place") == 0) c->cpu_place = iv;   // 0 (default) none; 1 the busiest threads on a logical processor each (cpu_place.h), with unpin=1; needs a relaunch
        else if (_stricmp(k, "sharpness")   == 0) c->sharpness   = (float)atof(v);  // 0 off, 0..1 strength
        else if (_stricmp(k, "hdr")         == 0) c->hdr         = iv;   // 0 off (default), 1 on
        else if (_stricmp(k, "hdr_paper_white") == 0) c->hdr_paper_white = (iv == -1) ? 0 : iv;   // nits, 0 = the OS value; -1 is taken as 0
        else if (_stricmp(k, "hdr_eotf")    == 0) c->hdr_eotf    = iv;   // 0 gamma 2.2 (default), 1 sRGB piecewise, 2 gamma 2.4 (BT.1886)
        else if (_stricmp(k, "hdr_view")    == 0) c->hdr_view    = iv;   // 0 off; the debug views (6, 14, 17)
        else if (_stricmp(k, "hdr_peak")    == 0) c->hdr_peak    = (iv == -1) ? 0 : iv;   // nits, the display peak (HGiG MaxTML); 0 (default) = what Windows reports, 1000 if nothing
        else if (_stricmp(k, "hdr_engine_name") == 0) c->hdr_engine_name = iv;   // DXVK's engine name: 0 "DXVK", 1 "D3D9 on Vulkan (HDR)", 2 "DXVK-HDR", 3 (default) "DXVK_NvRemix" on NVIDIA, else "DXVK-HDR"
        else if (_stricmp(k, "hdr_restore") == 0) c->hdr_restore = iv;   // highlight restoration, 0 off .. 100 full
        else if (_stricmp(k, "hdr_source")  == 0) c->hdr_source  = iv;   // -1 (default) auto, 0 the FP16 back buffer's own values above white, 1 the composite rebuilt from its inputs
        else if (_stricmp(k, "hdr_console_gamma") == 0) c->hdr_console_gamma = iv;   // -1 (default) detect; 0 none; 1 the Xbox 360 curve; 2 the PS3 curve
        else if (_stricmp(k, "hdr_ui") == 0) c->hdr_ui = iv;   // nits for the HUD and menus: 0 (default) the same as paper white, -1 no pre-HUD copy at all, N that many nits
        else if (_stricmp(k, "log")          == 0) c->log         = iv;
        else if (_stricmp(k, "inject_worker") == 0) c->inject_worker = iv; // the injection on the device thread instead of the render thread; live
        else if (_stricmp(k, "threaded")    == 0) c->threaded    = iv;   // the device thread: 0 off, 1 on, 2 on and validated; needs a relaunch
        else if (_stricmp(k, "preset")       == 0) c->preset      = iv;   // DLSS model, passed to the helper as --preset
                // fsr_version: -1 is taken as 0 silently, since -1 is "automatic" on other keys and a player may write it by analogy.
        else if (_stricmp(k, "fsr_version")    == 0) c->fsr_version    = (iv == -1) ? 0 : iv;   // 0 AMD's DLL picks, N the newest offered FSR N; passed as --fsr_version; needs a relaunch
#if DLSSIV_DEV
                // Development keys: a release build does not know them, and a cfg that carries one gets the unknown-key line. Each can
        // break the picture if copied from a post. Their fields and clamps stay in a release so the struct's layout does not move.
        else if (_stricmp(k, "flags")       == 0) c->flags       = iv;   // the NGX feature flags, -1 the helper's
        else if (_stricmp(k, "rs_ds")       == 0) c->rs_ds       = iv;   // the render-scale depth-stencil gate; needs a relaunch
        else if (_stricmp(k, "mv_glass")    == 0) c->mv_glass    = iv;   // 3 (default) blended or no-depth-write movers left out of the velocity pass; 2 only no-depth-write; 1 keep glass vectors; live
        else if (_stricmp(k, "lod_bias")     == 0) c->lod_bias    = (float)atof(v);   // 1 (default, and any positive) auto; 0 none; negative pins it
        else if (_stricmp(k, "mv_wheel")     == 0) c->mv_wheel     = iv;   // development builds only: 1 (default) a tyre's spin is left out of the vectors, as a release does; 0 keeps it; live
#endif
#if DLSSIV_FG
        else if (FgxParseKey(k, v)) {}     // fg: frame generation's key (source/fg/fg_client.h), read once at load, not an IvtCfg field
#endif
        else if (_stricmp(k, "sync")           == 0) c->sync           = iv;   // -1 by platform, 0 native fences, 1 the mod's shared semaphores, 2 the CPU bridge (the Linux sync path)
        else if (_stricmp(k, "hdr_cg_absorb") == 0) c->hdr_cg_absorb = iv;   // the absorb: 0 off, 1 FusionFix's proved console gamma draw withheld and applied by the encode, 2 compare only; live
        else if (_stricmp(k, "upscaler")       == 0) c->backend  = iv;    // -> backend, the player-facing name
        else if (_stricmp(k, "hdr_rolloff") == 0) c->hdr_rolloff = iv;   // percent of the ceiling where the highlight roll-off begins, 0 = hard clip
                // The live cfg.
        else if (_stricmp(k, "cfg_live")    == 0) c->cfg_live    = iv;   // 0 (default); 1 re-reads this file about once a second when it changes and applies the live keys
                // A key nobody knows, almost always a typo: still ignored (an old cfg with a retired key must keep working), but said.
        else { known = false; IvtNoteBadCfg("%s = \"%s\" is not a key this build knows", k, v); }
                // Not a whole number on an integer key: the line is put back and said (IvtWholeInt). After the chain, so a mistyped key with
        // a fraction is reported as the unknown key it is.
        if (known && whole != 0 && _stricmp(k, "sharpness") != 0 && _stricmp(k, "lod_bias") != 0)
        {
            *c = before; ivtParseSawAsync = sawAsyncBefore;
            IvtNoteBadCfg(whole == 1 ? "%s = \"%s\" is not a whole number" : "%s = \"%s\" is past the range of a whole number", k, v);
        }
    }
}

static void IvtClampCfg(IvtCfg* c)
{
        // To 2, not 1 (the transport test, no NGX). The rule across this table: an out-of-range value falls back to the shipping
    // behaviour, never to a diagnostic one.
    DEVSTAT(if (c->mode != 1 && c->mode != 2)) c->mode = 2;
        // To the default, not 0: mv_glass changes the buffer both upscalers read.
#if DLSSIV_DEV
    if (c->mv_glass != 4)      // 4, development builds only: 3 with a ped's blended hair left out too, to see the difference
#endif
    if (c->mv_glass < 0 || c->mv_glass > 3) c->mv_glass = 3;   // to the default, 3: movers drawn blended or with depth writes off are left out of the velocity pass (VelRecordObject)
        // A clamp is part of a cfg key's contract: a change that widens what a key means widens its clamp too.
    if (c->depth_mode < -1 || c->depth_mode > 2) c->depth_mode = -1;
    if (c->backend < -1 || c->backend > 2) c->backend = -1;   // -1 automatic; 0 DLSS, 1 FSR, 2 Intel XeSS
    if (c->sharpness < 0.0f) c->sharpness = 0.0f;
    if (c->sharpness > 1.0f) c->sharpness = 1.0f;
        // -4 is four mip levels of oversampling, beyond anything the render scale asks for. Anything positive means auto and is
    // stored as exactly 1, so a stray positive never becomes a blurring positive bias.
    if (c->lod_bias < -4.0f) c->lod_bias = -4.0f;
    if (c->lod_bias >  0.0f) c->lod_bias =  1.0f;
    if (c->hdr_restore < 0) c->hdr_restore = 0;
    if (c->hdr_restore > 100) c->hdr_restore = 100;
    if (c->hdr_rolloff < 0) c->hdr_rolloff = 0;
    if (c->hdr_rolloff > 95) c->hdr_rolloff = 95;      // the shoulder must have room to exist
        // 0 is auto; anything else is a nit figure the encode divides by, so a small typed value is a broken picture. 100..10000.
    if (c->hdr_peak < 0)                         c->hdr_peak = 0;          // a negative other than -1 (the parser's automatic) is reported
    if (c->hdr_peak != 0 && c->hdr_peak < 100)   c->hdr_peak = 100;
    if (c->hdr_peak > 10000)                     c->hdr_peak = 10000;
        // The same for paper white, a live key the encode multiplies by: 50 is dimmer than any reference, 1000 brighter than any
    // panel's SDR white.
    if (c->hdr_paper_white < 0)                             c->hdr_paper_white = 0;
    if (c->hdr_paper_white != 0 && c->hdr_paper_white < 50) c->hdr_paper_white = 50;
    if (c->hdr_paper_white > 1000)                          c->hdr_paper_white = 1000;
        // The legal range lives here, and IvtReportClamps reports a corrected value.
    if (c->quality < 0) c->quality = 0;
    if (c->quality > 4) c->quality = 4;
        // custom: 0 is off; 100 and above is the screen size (DLAA), so off too; below 34 is under DLSS's floor of a third and is
    // raised to it. IvtReportClamps says so when it changes a value.
    if (c->custom < 0 || c->custom >= 100) c->custom = 0;
    else if (c->custom > 0 && c->custom < 34) c->custom = 34;
    if (c->responsiveness < 0) c->responsiveness = 0; if (c->responsiveness > 4) c->responsiveness = 4;
    if (c->frame_queue < -1) c->frame_queue = -1; if (c->frame_queue > 3) c->frame_queue = 3;   // above 3 is 3, not the mode's default
    if (c->pacer < -1 || c->pacer > 1) c->pacer = -1;
    if (c->unpin < 0 || c->unpin > 1) c->unpin = 1;
    if (c->rename_sdr < 0 || c->rename_sdr > 2) c->rename_sdr = 1;   // 2: renamed under a driver-forced vsync on a fixed-refresh display too
    if (c->cpu_place < 0 || c->cpu_place > 1) c->cpu_place = 0;   // to the default, 0
        // To the default, as mv_glass: a typo must not change the picture. (Development builds only.)
#if DLSSIV_DEV
    if (c->mv_wheel < 0 || c->mv_wheel > 1) c->mv_wheel = 1;      // to the default: the spin left out
#endif
    DEVSTAT(if (c->hdr_view != 6 && c->hdr_view != 14 && c->hdr_view != 17)) c->hdr_view = 0;   // the three views; anything else is reported out of range below
    if (c->hdr_source < -1 || c->hdr_source > 1) c->hdr_source = -1;
    if (c->hdr_console_gamma < -1 || c->hdr_console_gamma > 2) c->hdr_console_gamma = -1;   // 2, the PS3 curve
        // hdr is 0 or 1 only: the engine-name hook tests > 0 while other gates test truthiness, so hdr=-1 would engage scRGB HDR
    // with DXVK still named "DXVK", which NVIDIA composites as SDR.
    if (c->hdr < 0) c->hdr = 0;
    if (c->hdr > 1) c->hdr = 1;
        // No cross-key rule for sharpness under HDR: HDR changes nothing about sharpening on either upscaler.
    if (c->log < 0) c->log = 0;
    if (c->log > 3) c->log = 3;
    if (c->preset < 0 || c->preset > 15) c->preset = 11;
        // The HUD's nits in paper white's range: hdr_ui=5 would be a near-black interface.
    if (c->hdr_ui < -1) c->hdr_ui = -1;
    if (c->hdr_ui > 0 && c->hdr_ui < 50) c->hdr_ui = 50;
    if (c->hdr_ui > 1000) c->hdr_ui = 1000;
        // Out of range is the shipping default: hdr_engine_name=-1 would present HDR on NVIDIA as SDR.
    if (c->hdr_engine_name < 0 || c->hdr_engine_name > 3) c->hdr_engine_name = 3;
    if (c->hdr_eotf < 0 || c->hdr_eotf > 2) c->hdr_eotf = 0;
        // To the default, not 0: a value from a development cfg must not silently turn the absorb off.
    if (c->hdr_cg_absorb < 0 || c->hdr_cg_absorb > 2) c->hdr_cg_absorb = 1;
        // Default 2; see the struct.
    if (c->inject_worker < 0 || c->inject_worker > 2) c->inject_worker = 2;
    if (c->inject_worker == 1) c->inject_worker = 2;      // the key is 0 or 2: mode 1 is taken as 2
    if (c->threaded < 0 || c->threaded > DT_THREADED_TOP) c->threaded = 1;   // the default; 0 is the way out
        // At most two characters each, which keeps the helper's command line inside its buffer (see the spawn).
    if (c->fsr_version < 0 || c->fsr_version > 9) c->fsr_version = 0;
    if (c->sync < -1 || c->sync > 2) c->sync = -1;               // the Linux sync path: -1 by platform
}

static void IvtLogCfg()
{
    IvtReportClamps(ivtCfgAsked, ivtCfg, "the cfg as read at startup");
        // One line at log=1: the keys that decide what a player sees. The full dump is health, a developer's reading.
    LogRecord("  cfg: quality=%d (%s) upscaler=%d (%s) hdr=%d sharpness=%.2f preset=%d threaded=%d",
        ivtCfg.quality,
        ivtCfg.quality <= 0 ? "DLAA" : ivtCfg.quality == 1 ? "Quality" : ivtCfg.quality == 2 ? "Balanced"
        : ivtCfg.quality == 3 ? "Performance" : "Ultra Performance",
        ivtCfg.backend, ivtCfg.backend < 0 ? "auto" : ivtCfg.backend == 1 ? "FSR" : ivtCfg.backend == 2 ? "XeSS" : "DLSS",
        ivtCfg.hdr, ivtCfg.sharpness, ivtCfg.preset, ivtCfg.threaded);
    // Only when set: at their defaults they change nothing, and the host names the FSR that ran either way.
    if (ivtCfg.custom)
        LogRecord("  cfg: custom=%d (%d percent in each axis; quality=%d not used)",
            ivtCfg.custom, ivtCfg.custom, ivtCfg.quality);
    if (ivtCfg.responsiveness)
        LogRecord("  cfg: responsiveness=%d (%s)%s", ivtCfg.responsiveness,
            ivtCfg.responsiveness == 1 ? "the frame queue" : ivtCfg.responsiveness == 2 ? "queue and pacer"
            : ivtCfg.responsiveness == 3 ? "queue, pacer, same-frame upscale" : "queue; pacer and same-frame upscale where measured free",
            ivtCfg.responsiveness >= 3 && ivtCfgNamedAsync ? "; async_home in the file wins over its same-frame upscale" : "");
    if (ivtCfg.pacer >= 0)
        LogRecord("  cfg: pacer=%d (%s)", ivtCfg.pacer, ivtCfg.pacer ? "on" : "off");
    if (!ivtCfg.unpin)
        LogRecord("  cfg: unpin=0");
    if (ivtCfg.cpu_place)
        LogRecord("  cfg: cpu_place=1%s",
                  ivtCfg.unpin ? "" : " (NOT with unpin=0)");
    else
        LogRecord("  cfg: cpu_place=0");
    if (!ivtCfg.rename_sdr && !ivtCfg.hdr)
        LogRecord("  cfg: rename_sdr=0");
    if (ivtCfg.frame_queue >= 0)
        LogRecord("  cfg: frame_queue=%d (%s)", ivtCfg.frame_queue, ivtCfg.frame_queue == 0 ? "DXVK's own" : "by hand, over dxvk.conf");
    if (ivtCfg.fsr_version)
        LogRecord("  cfg: fsr_version=%d%s", ivtCfg.fsr_version,
            ivtCfg.backend == 0 ? " (unused: DLSS)" : ivtCfg.backend == 2 ? " (unused: XeSS)" : "");
#if DLSSIV_DEV
    if (!ivtCfg.mv_wheel) LogRecord("  cfg: mv_wheel=0 (development key)");
#endif
    LogHealth("  transport: cfg flags=%d (%s) depth_mode=%d (%s)", ivtCfg.flags,
        ivtCfg.flags < 0 ? "host default: MVLowRes|AutoExposure, plus DepthInverted" : "override handed to the host",
        ivtCfg.depth_mode,
        ivtCfg.depth_mode < 0 ? "auto" : ivtCfg.depth_mode == 0 ? "copy" : ivtCfg.depth_mode == 1 ? "direct" : "substitute");
    LogHealth("  transport: cfg transport=%d mode=%d (%s) async_home=%d (%s)",
        ivtCfg.transport,
        IVT_MODE, IVT_MODE == 1 ? "transport test, no NGX" : "the upscaler",
        ivtCfg.async_home, ivtCfg.async_home ? "pipelined, one frame old" : "same frame");
        // The render size alone, no upscaler's setting name: each upscaler names the same size its own way (the mod's Quality, two
    // thirds, is XeSS's Ultra Quality). String-only: a branch on the upscaler here moved the development build's hot data.
    LogHealth("  transport: cfg quality=%d (%s)", ivtCfg.quality,
        ivtCfg.quality <= 0 ? "no scale: the scene at the screen size"
        : ivtCfg.quality == 1 ? "the scene at two thirds"
        : ivtCfg.quality == 2 ? "the scene at 0.58"
        : ivtCfg.quality == 3 ? "the scene at a half"
                                     : "the scene at a third");
    LogHealth("  transport: cfg hdr=%d hdr_paper_white=%d hdr_peak=%d hdr_eotf=%d hdr_restore=%d hdr_rolloff=%d hdr_source=%d hdr_console_gamma=%d hdr_view=%d cfg_live=%d",
        ivtCfg.hdr, ivtCfg.hdr_paper_white, ivtCfg.hdr_peak, ivtCfg.hdr_eotf, ivtCfg.hdr_restore,
        ivtCfg.hdr_rolloff, ivtCfg.hdr_source, ivtCfg.hdr_console_gamma, ivtCfg.hdr_view, ivtCfg.cfg_live);
    LogHealth("  transport: cfg hdr_ui=%d (%s)", ivtCfg.hdr_ui,
        ivtCfg.hdr_ui < 0 ? "off: no pre-HUD copy, the HUD rides the scene's highlights"
        : ivtCfg.hdr_ui == 0 ? "the HUD sits at paper white" : "nits");
    LogHealth("  transport: cfg hdr_cg_absorb=%d (%s)", ivtCfg.hdr_cg_absorb,
        ivtCfg.hdr_cg_absorb == 1 ? "FusionFix's console gamma draw is withheld once proved, and the encode applies the identical curve from FusionFix's own copy of the frame"
        : ivtCfg.hdr_cg_absorb == 2 ? "compare only: FusionFix's pass is recognised and proved against its own output, nothing is withheld"
        : "off");
        // The keys that change the picture: mv_glass changes the vectors both upscalers read, rs_ds leaves the depth-stencil at
    // output size, and sharpness otherwise reaches the log only through the RCAS build line, which never runs at 0.
    LogHealth("  transport: cfg sharpness=%.2f lod_bias=%.2f preset=%d backend=%d"
        " mv_glass=%d rs_ds=%d threaded=%d",
        ivtCfg.sharpness, ivtCfg.lod_bias, ivtCfg.preset, ivtCfg.backend,
        ivtCfg.mv_glass, ivtCfg.rs_ds,
        ivtCfg.threaded);
        // Preset is not narrowed: 14 and 15 are how a future NGX model gets tried, so a typo is named instead. NVIDIA's
    // nvsdk_ngx_defs.h marks A to D removed, E and F deprecated, and G, H, I, N and O "do not use"; CONFIG.md documents 0, or 10..13.
    if (ivtCfg.preset != 0 && (ivtCfg.preset < 10 || ivtCfg.preset > 13))
    {
        LogRecord("  cfg: preset=%d is outside 0, 10..13; passed to DLSS unchanged", ivtCfg.preset);
        IvtWarn("preset=%d is outside the documented set (0, or 10..13). It is passed to DLSS unchanged, and NVIDIA marks those presets"
                " removed or deprecated: if the picture is soft or broken, set preset=0", ivtCfg.preset);
    }
}

// Live reload state: the file that was read, and its stamp.
static char     ivtCfgPath[MAX_PATH] = {};
static FILETIME ivtCfgStamp  = {};
static DWORD    ivtCfgSizeLo = 0;
static UINT     ivtCfgTick   = 0;

// What IvtReadCfg found, so it can stay silent until the log exists.
static char ivtCfgOwnDir[MAX_PATH]  = {};
static char ivtCfgGameDir[MAX_PATH] = {};
static char ivtCfgFound[32]         = {};
static bool ivtCfgMissing           = false;   // no cfg file: the mod does not run at all

// What the parser refused, said once, where it is safe to speak: from IvtLogCfgSource at startup, after the reparse on a
// live reload.
static void IvtLogCfgComplaints(const char* when)
{
        // notice=0, the on-screen notices' override (source/notice.h), is read here, out of the list of lines the parser could not
    // use: an arm in IvtParseCfg or a line in IvtNoteBadCfg moved the release's hot data (tools/check-hot-layout.py), and this
    // call does not. NoticeCfgOverride takes the line out of the list, so it is not called unknown.
    // (tools/check-cfg.py finds the keys the cfg accepts by the text _stricmp(k, "notice"): for this key, here it is.)
    NoticeCfgOverride();
    CfgSayOff();                    // responsiveness=0 is said (the end of DLSS-IV.cpp says why from here)
    if (ivtCfgBadSeen <= 0) return;
    LogRecord("  cfg: %d line(s) of %s not used, the shipping default kept for each:", ivtCfgBadSeen, when);
    for (int i = 0; i < ivtCfgBadN; i++)
    {
        LogRecord("    %s", ivtCfgBad[i]);
        IvtWarn("DLSS-IV.cfg (%s): %s; the shipping default was kept", when, ivtCfgBad[i]);   // where a log=0 player sees it
    }
    if (ivtCfgBadSeen > ivtCfgBadN)
    {
        LogRecord("    and %d more", ivtCfgBadSeen - ivtCfgBadN);
        IvtWarn("DLSS-IV.cfg (%s): and %d more line(s) that could not be used", when, ivtCfgBadSeen - ivtCfgBadN);
    }
}

static void IvtLogCfgSource()
{
    if (ivtCfgFound[0])
    {
        LogRecord("  cfg: %s (%s)", ivtCfgPath, ivtCfgFound);
        IvtLogCfgComplaints("the cfg");
        return;
    }
        // Name both places.
    LogRecord("  cfg: no DLSS-IV.cfg found, nothing runs; looked in:");
    if (ivtCfgOwnDir[0])  LogRecord("    %s (beside the .asi)", ivtCfgOwnDir);
    if (ivtCfgGameDir[0] && _stricmp(ivtCfgOwnDir, ivtCfgGameDir) != 0)
        LogRecord("    %s (next to the game exe)", ivtCfgGameDir);
}

static void IvtReadCfg()
{
    ivtCfgRead = true;
        // Beside the .asi first (where every other plugin on this stack keeps its settings), then next to the game exe (the shipped
    // layout). When the .asi is in the game root the two are the same folder.
    char own[MAX_PATH] = {}, game[MAX_PATH] = {}, path[MAX_PATH] = {};
    IvtOwnDir(own, MAX_PATH);
    IvtGameDir(game, MAX_PATH);
    FILE* f = nullptr;
    const char* found = nullptr;
    if (own[0])
    {
        _snprintf_s(path, MAX_PATH, _TRUNCATE, "%sDLSS-IV.cfg", own);
                // _fsopen with _SH_DENYNO, not fopen_s, which does not share: a cfg held open in an editor must still be read.
        f = _fsopen(path, "rb", _SH_DENYNO);
        if (f) found = "beside the .asi";
    }
    if (!f && game[0] && _stricmp(own, game) != 0)
    {
        _snprintf_s(path, MAX_PATH, _TRUNCATE, "%sDLSS-IV.cfg", game);
        f = _fsopen(path, "rb", _SH_DENYNO);          // shared, same reason as above
        if (f) found = "next to the game exe";
    }
        // Nothing is logged here: this runs before LogInit (the log level it parses decides whether a log exists). What it found is
    // recorded, and IvtLogCfgSource says it once the log is open.
    strcpy_s(ivtCfgOwnDir, MAX_PATH, own);
    strcpy_s(ivtCfgGameDir, MAX_PATH, game);
    if (!f)
    {
                // The file is the master switch: transport defaults to 1, so without this the compiled defaults alone would run the mod.
        ivtCfgMissing = true;
        return;
    }
    strcpy_s(ivtCfgFound, sizeof(ivtCfgFound), found);
    strcpy_s(ivtCfgPath, MAX_PATH, path);
    {
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) { ivtCfgStamp = fa.ftLastWriteTime; ivtCfgSizeLo = fa.nFileSizeLow; }
    }
    ivtCfgDefaults = ivtCfg;               // still the compiled defaults at this point: the first read wins
    ivtParseSawAsync = false;
    IvtParseCfg(f, &ivtCfg);
    ivtCfgNamedAsync = ivtParseSawAsync;
    fclose(f);
    ivtCfgAsked = ivtCfg;                  // before the clamp; IvtLogCfg reports the difference once the log exists
    IvtClampCfg(&ivtCfg);
        // The latency instrument's switches, after the parse and the clamp (before them ivtCfg is still the compiled defaults).
    // A missing cfg returns above this point, and then nothing runs.
    latPacerOn = ivtCfg.pacer == 1 || (ivtCfg.pacer < 0 && ivtCfg.responsiveness >= 2);
        // The instrument runs only where something reads it: at log=0 with no pacer or ladder no latency line is written.
    // LatWaiterStart makes the second half of this decision, once the limiter and the bridge are known.
    latOn = latPacerOn || ivtCfg.responsiveness >= 4 || ivtCfg.log >= 2 || (ivtCfg.responsiveness != 0 && ivtCfg.log >= 1);   // the one per-present branch reads this: log=2 measures at step 0 too, the pacer and step 4's ladder need the stamps
    { LARGE_INTEGER qpf; if (QueryPerformanceFrequency(&qpf)) { latQpf = qpf.QuadPart; latNsPerTick = 1e9 / (double)latQpf; } }   // the divide once, here
        // IvtLogCfg is not called here: this runs before LogInit, so every line would be lost. InitThread calls it later.
}

// Live reload: the cfg file is the tuning surface and the log is the readout. Every 60 frames one GetFileAttributesEx on the
// file read at startup; when its stamp or size changes it is parsed again from the defaults (a removed key goes back to its
// default) and only the live keys are applied, each logged with old and new. A key that cannot change while running is
// named and left alone. cfg_live=0 removes it.
static void HdrApplyLiveCfg();             // hdr_encode.h: re-resolves the levels, retries a view, gives a switched-off feature's memory back

#define IVT_LIVE_INT_KEYS(X) X(hdr_paper_white) X(hdr_peak) X(hdr_eotf) X(hdr_restore) \
                             X(hdr_rolloff) X(hdr_source) X(hdr_console_gamma) X(hdr_ui) X(hdr_view) X(cfg_live) \
                             X(mv_glass) X(hdr_cg_absorb) X(inject_worker) IVT_DEV_ONLY(X(mv_wheel))
// Every int field of IvtCfg must be in exactly one of these two lists (tools/check-cfg.py enforces it); a field in neither
// would report neither an applied change nor "needs a relaunch".
// IVT_DEV_ONLY keeps a fenced field's name where tools/check-cfg.py reads it and expands to nothing in a release build,
// whose IvtCfg does not have the field. IVT_REL_ONLY is its opposite.
#if DLSSIV_DEV
#define IVT_DEV_ONLY(x) x
#define IVT_REL_ONLY(x)
#else
#define IVT_DEV_ONLY(x)
#define IVT_REL_ONLY(x) x
#endif
#define IVT_STATIC_INT_KEYS(X) X(transport) X(mode) X(async_home) X(flags) \
                               X(depth_mode) X(quality) \
                               X(backend) X(hdr) X(hdr_engine_name) X(preset) X(log) \
                               X(rs_ds) X(threaded) X(fsr_version) X(sync) X(custom) X(responsiveness) X(frame_queue) X(pacer) X(unpin) X(rename_sdr) X(cpu_place)

// Every clamp that fired, named, to the log and to the warnings file a log=0 player sees: a live edit is clamped before it is
// compared, so an out-of-range value would otherwise look like no change at all. Driven by the two key lists, so a later
// clamp cannot forget to report; the two floats are named explicitly because the int macro cannot carry them.
// The key by the name the player wrote: the field backend is the key upscaler. IVT_CFG_WORD(k) is the key's own name where
// an IVT_CFG_WORD_<field> below gives one, else the field's: a string literal either way, chosen by the preprocessor.
#define IVT_CFG_WORD_backend ~, "upscaler"
#define IVT_CFG_2ND_(a, b, ...) b
#define IVT_CFG_2ND(args) IVT_CFG_2ND_ args
#define IVT_CFG_WORD(k) IVT_CFG_2ND((IVT_CFG_WORD_##k, #k, ~))
#define IVT_CLAMP_SAY(k) if (asked.k != used.k) { \
    LogRecord("  cfg clamp: %s: %s=%d out of range; using %d", when, IVT_CFG_WORD(k), asked.k, used.k); \
    IvtWarn("DLSS-IV.cfg (%s): %s=%d is out of range; the mod is using %d", when, IVT_CFG_WORD(k), asked.k, used.k); }
static void IvtReportClamps(const IvtCfg& asked, const IvtCfg& used, const char* when)
{
    IVT_LIVE_INT_KEYS(IVT_CLAMP_SAY)
    IVT_STATIC_INT_KEYS(IVT_CLAMP_SAY)
        // A plain if, not else: an else would bind to the last key in the static list.
    if (asked.sharpness != used.sharpness)
    {
        LogRecord("  cfg clamp: %s: sharpness=%.2f out of range; using %.2f", when, asked.sharpness, used.sharpness);
        IvtWarn("DLSS-IV.cfg (%s): sharpness=%.2f is out of range; the mod is using %.2f", when, asked.sharpness, used.sharpness);
    }
    if (asked.lod_bias != used.lod_bias)
    {
        LogRecord("  cfg clamp: %s: lod_bias=%.2f out of range; using %.2f", when, asked.lod_bias, used.lod_bias);
        IvtWarn("DLSS-IV.cfg (%s): lod_bias=%.2f is out of range; the mod is using %.2f", when, asked.lod_bias, used.lod_bias);
    }
}
#undef IVT_CLAMP_SAY

static void IvtCfgLiveTick()
{
    if (!ivtCfg.cfg_live || !ivtCfgPath[0]) return;
    if ((++ivtCfgTick % 60) != 0) return;                  // about once a second
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExA(ivtCfgPath, GetFileExInfoStandard, &fa)) return;
    if (CompareFileTime(&fa.ftLastWriteTime, &ivtCfgStamp) == 0 && fa.nFileSizeLow == ivtCfgSizeLo) return;
    if (fa.nFileSizeLow == 0) return;                       // an editor mid-save
        // The stamp is not advanced until the read succeeds, so a file held by an editor or half written is read again next time.
    FILE* f = nullptr;
    f = _fsopen(ivtCfgPath, "rb", _SH_DENYNO);
    if (!f) return;
    ivtCfgStamp = fa.ftLastWriteTime; ivtCfgSizeLo = fa.nFileSizeLow;   // the read took
    IvtCfg n = ivtCfgDefaults;
    IvtParseCfg(f, &n);
    fclose(f);
    const IvtCfg asked = n;          // before the clamp, so the clamp can be reported
    IvtClampCfg(&n);
    IvtReportClamps(asked, n, "the cfg as just re-read");
        // The log exists by now, so this can speak: someone editing live is watching for an effect.
    IvtLogCfgComplaints("the cfg as just re-read");
    int changed = 0, later = 0;
        // `changed` counts every key in IVT_LIVE_INT_KEYS and a change calls HdrApplyLiveCfg, which is idempotent, so a non-HDR key
    // there (cfg_live) is harmless. The two floats are handled out of band below, and `lodMoved` makes a float-only edit
    // still report that something moved.
    int lodMoved = 0;
#define IVT_APPLY(k) if (n.k != ivtCfg.k) { LogRecord("  cfg live: %s %d -> %d", #k, ivtCfg.k, n.k); ivtCfg.k = n.k; changed++; }
    IVT_LIVE_INT_KEYS(IVT_APPLY)
#undef IVT_APPLY
// A live edit that did not take is said where log=0 can see it. Edit-driven, so the warnings file cannot grow per frame.
#define IVT_LATER(k) if (n.k != ivtCfg.k) { LogRecord("  cfg live: %s %d -> %d needs a relaunch; not applied", #k, ivtCfg.k, n.k); \
    IvtWarn("DLSS-IV.cfg was edited while the game runs: %s %d -> %d needs a relaunch and is not applied this session", #k, ivtCfg.k, n.k); later++; }
    IVT_STATIC_INT_KEYS(IVT_LATER)
#undef IVT_LATER
        // Sharpness is live within the session that built the pass: the strength is one pixel-shader constant. Whether the RCAS
    // shader and its scratch exist is decided at startup, so a rise from 0 needs a relaunch and says so. Setting 0 live stops
    // the pass without freeing the scratch, and says that too.
    if (n.sharpness != ivtCfg.sharpness)
    {
        if (!shpBuilt)
        {
            LogRecord("  cfg live: sharpness %.2f -> %.2f needs a relaunch",
                      ivtCfg.sharpness, n.sharpness);
            IvtWarn("DLSS-IV.cfg was edited while the game runs: sharpness %.2f -> %.2f needs a relaunch (a session that starts"
                    " at 0 never builds the sharpening pass); after that the strength can be changed live",
                    (double)ivtCfg.sharpness, (double)n.sharpness);
            later++;
        }
        else
        {
            // The old value is taken first, for the "X -> Y" lines.
            const float was = ivtCfg.sharpness;
            const float stops = (1.0f - n.sharpness) * 2.0f;
            ivtCfg.sharpness = n.sharpness;
            shpCon = powf(2.0f, -stops);
            shpOn = (n.sharpness > 0.0f);
            if (n.sharpness > 0.0f)
                LogRecord("  cfg live: sharpness %.2f -> %.2f (%.2f stops, attenuation %.4f), from the next frame%s",
                          was, n.sharpness, stops, shpCon,
                          (shpTried && !shpPS) ? "; but the pass failed to build this session, so nothing runs" : "");
            else
                LogRecord("  cfg live: sharpness %.2f -> 0: off from the next frame", was);
            lodMoved = 1;          // "something live moved", the same bucket the bias uses
        }
    }
    if (n.lod_bias != ivtCfg.lod_bias)
    { LogRecord("  cfg live: lod_bias %.3f -> %.3f", ivtCfg.lod_bias, n.lod_bias); ivtCfg.lod_bias = n.lod_bias; lodMoved = 1; }
    if (changed) HdrApplyLiveCfg();
    if (!changed && !later && !lodMoved) LogDetail("  cfg live: the file changed but no key did");
}

// ---- Vulkan: DXVK's device, the interop entry points ----

// The game's instance and physical device, from GetVulkanHandles: the texture import's failure line asks the physical device
// what it can import for the image it refused.
static VkInstance       ivtVkInst = VK_NULL_HANDLE;
static VkPhysicalDevice ivtVkPhys = VK_NULL_HANDLE;
// The handle type the session imports with: the platform's default first (OPAQUE_WIN32 under Wine, what vkd3d-proton exports
// and every Proton accepts; the D3D12 resource type on Windows). When an import fails the other type is tried once and the
// one that worked is kept. 0 until the first import.
static VkExternalMemoryHandleTypeFlagBits ivtImportType = (VkExternalMemoryHandleTypeFlagBits)0;

static bool IvtEnsureVulkan(IDirect3DDevice9* dev)
{
    if (ivtVk.ok) return true;
    DT_DIRECT();                 // the interop device and DXVK's queue: see devthread/dt_core.h
    if (!ivtIDev)
    {
        if (FAILED(dev->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&ivtIDev)) || !ivtIDev)
        { IvtFail("no ID3D9VkInteropDevice: not running on DXVK"); return false; }
    }
    VkInstance inst = VK_NULL_HANDLE; VkPhysicalDevice phys = VK_NULL_HANDLE;
    ivtIDev->GetVulkanHandles(&inst, &phys, &ivtDevice);
    ivtVkInst = inst; ivtVkPhys = phys;   // kept for the import's failure line
        // Which GPU this is, from Vulkan and never from D3D9. The helper must create its D3D12 device on the same adapter (on a
    // hybrid laptop the first adapter is often the integrated GPU). DXVK's GTA IV profile sets d3d9.hideNvidiaGpu, so the D3D9
    // identity is a spoof; the Vulkan physical device is the real one. deviceLUIDValid can be false: then the client says so once
    // and the helper takes DXGI's high-performance order.
    // GetModuleHandleA, not ivtVk.lib: FeedVkLoad has not run yet, the Vulkan module is loaded already (DXVK handed over the
    // handles), and GetModuleHandleA takes no reference on a module the mod did not load.
    ivtLuidValid = false;
        // Under Wine DXVK loads winevulkan.dll and never vulkan-1.dll, so it is asked for first there: a native vulkan-1.dll loaded
    // by another program would not know winevulkan's instance. Windows keeps its order.
    HMODULE vkLib = FeedVkRemoteModule();   // the bridge's remote interop first (feed_vk.h)
    if (!vkLib) vkLib = GetModuleHandleA(ivtWine ? "winevulkan.dll" : "vulkan-1.dll");
    if (!vkLib) vkLib = GetModuleHandleA(ivtWine ? "vulkan-1.dll" : "winevulkan.dll");
    if (phys != VK_NULL_HANDLE && vkLib)
    {
        PFN_vkGetInstanceProcAddr gipa =
            (PFN_vkGetInstanceProcAddr)FeedVkProc(vkLib, "vkGetInstanceProcAddr");
        PFN_vkGetPhysicalDeviceProperties2 props2 = gipa
            ? (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2") : nullptr;
        if (!props2 && gipa)
            props2 = (PFN_vkGetPhysicalDeviceProperties2)gipa(inst, "vkGetPhysicalDeviceProperties2KHR");
        if (props2)
        {
            VkPhysicalDeviceIDProperties idp = {};
            idp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
            VkPhysicalDeviceProperties2 p2 = {};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &idp;
            props2(phys, &p2);
            // The real card, whatever the LUID says (the D3D9 identity is DXVK's spoof).
            // The driver version, readable: NVIDIA packs 10 bits of major and 8 of minor from the top; Intel on Windows packs 18 and 14;
            // everyone else uses Vulkan's 10.10.12 (for AMD the Vulkan driver's number, not the Adrenalin release). The hex stays beside it.
            const DWORD dv = p2.properties.driverVersion, vid = p2.properties.vendorID;
            char drv[48];
            if (vid == 0x10DE)      _snprintf_s(drv, sizeof(drv), _TRUNCATE, "%lu.%02lu", (unsigned long)(dv >> 22), (unsigned long)((dv >> 14) & 0xFF));
            else if (vid == 0x8086) _snprintf_s(drv, sizeof(drv), _TRUNCATE, "%lu.%lu (Intel's numbering)", (unsigned long)(dv >> 14), (unsigned long)(dv & 0x3FFF));
            else                    _snprintf_s(drv, sizeof(drv), _TRUNCATE, "%lu.%lu.%lu (Vulkan's numbering)", (unsigned long)(dv >> 22), (unsigned long)((dv >> 12) & 0x3FF), (unsigned long)(dv & 0xFFF));
            LogRecord("  vulkan: the game renders on \"%s\", driver %s (%04X:%04X, 0x%08X), Vulkan %lu.%lu.%lu",
                p2.properties.deviceName, drv, vid, p2.properties.deviceID, dv,
                (unsigned long)(p2.properties.apiVersion >> 22), (unsigned long)((p2.properties.apiVersion >> 12) & 0x3FF), (unsigned long)(p2.properties.apiVersion & 0xFFF));
            hdrVkVendor = vid;           // hdr.h: the no-rename line is NVIDIA's alone
            HdrSayNoRename();
            if ((ivtRenamePredict == 1 && vid != 0x10DE) || (ivtRenamePredict == 0 && vid == 0x10DE))
            {
                // A missed prediction is the player's problem: in the warnings file too.
                if (ivtRenamePredict == 1)
                    IvtWarn("DXVK was given NVIDIA's engine name expecting the NVIDIA GPU to render, but the game renders on \"%s\"."
                            " If the game stutters or looks wrong, set rename_sdr=0 in DLSS-IV.cfg", p2.properties.deviceName);
                else
                    IvtWarn("DXVK kept its own engine name expecting another GPU to render, but the game renders on \"%s\", an NVIDIA GPU;"
                            " a borderless window may then be composited. Say which laptop this is", p2.properties.deviceName);
                LogRecord("  engine name: the prediction missed: DXVK %s, but the game renders on \"%s\"",
                          ivtRenamePredict == 1 ? "was renamed for NVIDIA" : "kept \"DXVK\" expecting another GPU", p2.properties.deviceName);
            }
            if (idp.deviceLUIDValid)
            {
                memcpy(ivtLuid, idp.deviceLUID, sizeof(ivtLuid));
                ivtLuidValid = true;
                LogHealth("  transport: its adapter LUID is %02X%02X%02X%02X%02X%02X%02X%02X",
                    ivtLuid[0], ivtLuid[1], ivtLuid[2], ivtLuid[3],
                    ivtLuid[4], ivtLuid[5], ivtLuid[6], ivtLuid[7]);
            }
            else
                LogRecord("  vulkan: no adapter LUID for this device: host GPU not checked");
        }
        else
            LogRecord("  vulkan: vkGetPhysicalDeviceProperties2 did not resolve: host GPU not checked");
    }
    uint32_t qi = 0;
    ivtIDev->GetSubmissionQueue(&ivtQueue, &qi, &ivtQueueFamily);
    if (!ivtDevice || !ivtQueue) { IvtFail("DXVK gave no VkDevice/VkQueue"); return false; }

    const char* vkMissing = nullptr;
    if (!FeedVkLoad(&ivtVk, ivtDevice, &vkMissing, ivtWine))
    {
                // DXVK enables VK_KHR_external_memory_win32 and VK_KHR_external_semaphore_win32 itself; if a DXVK build ever stopped,
        // hooking vkCreateDevice to add them would be the fallback. The reason names what failed; under Wine the likeliest failure
        // is the loader itself (see FeedVkLoad).
        char why[256];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "Vulkan interop missing (%s): the shared textures cannot be imported%s",
                    vkMissing ? vkMissing : "unknown",
                    ivtWine ? "; on Linux try a current proton-cachyos or GE-Proton" : "");
        IvtFail(why);
        return false;
    }
    #define IVT_GET(var, type, name) var = (type)ivtVk.GetDeviceProcAddr(ivtDevice, name)
    IVT_GET(ivtSignalSemaphore, PFN_vkSignalSemaphore, "vkSignalSemaphore");
    if (!ivtSignalSemaphore) IVT_GET(ivtSignalSemaphore, PFN_vkSignalSemaphore, "vkSignalSemaphoreKHR");
    IVT_GET(ivtQueueSubmit,            PFN_vkQueueSubmit,            "vkQueueSubmit");
    IVT_GET(ivtCreateCommandPool,      PFN_vkCreateCommandPool,      "vkCreateCommandPool");
    IVT_GET(ivtAllocateCommandBuffers, PFN_vkAllocateCommandBuffers, "vkAllocateCommandBuffers");
    IVT_GET(ivtBeginCommandBuffer,     PFN_vkBeginCommandBuffer,     "vkBeginCommandBuffer");
    IVT_GET(ivtEndCommandBuffer,       PFN_vkEndCommandBuffer,       "vkEndCommandBuffer");
    IVT_GET(ivtResetCommandBuffer,     PFN_vkResetCommandBuffer,     "vkResetCommandBuffer");
    #undef IVT_GET
        // No vkDestroyCommandPool: the ring's pool is deliberately not destroyed (IvtShutdown runs at process exit, where DXVK's
    // device may be gone).
    const bool cmdOk = ivtQueueSubmit && ivtCreateCommandPool && ivtAllocateCommandBuffers
                    && ivtBeginCommandBuffer && ivtEndCommandBuffer && ivtResetCommandBuffer;
    LogHealth("  transport: Vulkan entry points resolved on DXVK's device %p (queue %p, family %u, index %u);"
        " timeline queries %s, vkSignalSemaphore %s, command buffer entry points %s",
        (void*)ivtDevice, (void*)ivtQueue, ivtQueueFamily, qi,
        FeedVkHasTimelineQueries(&ivtVk) ? "yes" : "NO",
        ivtSignalSemaphore ? "yes" : "NO", cmdOk ? "yes" : "NO");
    if (!FeedVkHasTimelineQueries(&ivtVk)) { IvtFail("no timeline semaphore queries on this device"); return false; }
    if (!cmdOk) { IvtFail("core command buffer entry points missing (?)"); return false; }
    QueryPerformanceFrequency((LARGE_INTEGER*)&ivtQpcFreq);
    return true;
}

// The mod's command buffers and its own timeline semaphore: every submit signals the local timeline with a rising value,
// and a ring slot is reused only once that value is reached, so a command buffer is never rewritten while the GPU reads it.
static bool IvtEnsureRing()
{
    if (ivtPool) return true;
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = ivtQueueFamily;
    if (ivtCreateCommandPool(ivtDevice, &pci, nullptr, &ivtPool) != VK_SUCCESS)
    { IvtFail("vkCreateCommandPool failed"); return false; }
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool        = ivtPool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = IVT_RING;
    if (ivtAllocateCommandBuffers(ivtDevice, &cai, ivtCb) != VK_SUCCESS)
    { IvtFail("vkAllocateCommandBuffers failed"); return false; }
    VkSemaphoreTypeCreateInfo tci = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tci.initialValue  = 0;
    VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    sci.pNext = &tci;
    if (ivtVk.CreateSemaphore(ivtDevice, &sci, nullptr, &ivtSemLocal) != VK_SUCCESS)
    { IvtFail("local timeline semaphore creation failed"); return false; }
    LogDetail("  transport: command pool on family %u, %d command buffers, local timeline ready", ivtQueueFamily, IVT_RING);
    return true;
}

// Take the next ring slot: wait (CPU) until the GPU retired its previous use,
// reset and begin. Returns VK_NULL_HANDLE on a failure, which stops the transport.
static VkCommandBuffer IvtBeginCb(int* slotOut)
{
    const int slot = (int)(ivtSubmitN % IVT_RING);
    if (ivtCbValue[slot] != 0 && !FeedVkWaitTimeline(&ivtVk, ivtSemLocal, ivtCbValue[slot], 2000))
    { IvtFail("one of the mod's command buffers did not retire within 2 s"); return VK_NULL_HANDLE; }
    VkCommandBuffer cb = ivtCb[slot];
    ivtResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ivtBeginCommandBuffer(cb, &bi) != VK_SUCCESS) { IvtFail("vkBeginCommandBuffer failed"); return VK_NULL_HANDLE; }
    *slotOut = slot;
    return cb;
}

// Barriers on the game's images, scoped to what touches them. In: the game writes the back buffer through colour-attachment
// output and transfer. Out: its later draws use it as a colour attachment, sample it in fragment shaders and blit it at
// present. Naming those stages lets unrelated work overlap the copies. Layout transitions ride the same barrier when the
// image is not GENERAL (it is GENERAL under DXVK).
static const VkPipelineStageFlags kIvtGameWriteStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
static const VkAccessFlags        kIvtGameWriteAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
static const VkPipelineStageFlags kIvtGameUseStages   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                      | VK_PIPELINE_STAGE_TRANSFER_BIT;
static const VkAccessFlags        kIvtGameUseAccess   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                                                      | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

static void IvtBarrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
                       VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.srcAccessMask       = srcAccess;
    b.dstAccessMask       = dstAccess;
    b.oldLayout           = from;
    b.newLayout           = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = img;
    b.subresourceRange    = { aspect, 0, 1, 0, 1 };
    ivtVk.CmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// the game's writes -> the mod's transfer
static void IvtBarrierGameIn(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to)
{
    IvtBarrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, from, to, kIvtGameWriteStages, kIvtGameWriteAccess,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}
// the mod's transfer -> the game's later use
static void IvtBarrierGameOut(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to)
{
    IvtBarrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, from, to, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, kIvtGameUseStages, kIvtGameUseAccess);
}
// ---- The helper process and the pipe ----

// Why this write has no deadline of its own: it blocks only when the helper has left FEED_PIPE_BYTES unread, and a stuck
// helper cannot get there: the next frame's GPU wait on its fence stops the render thread inside DXVK within a frame or two,
// and the helper watch (IvtHostWatch) ends that after 4 s, with only a few small messages unread. The assert holds the
// pipe's size to a ring's worth of frames, a build and a hello.
static_assert((IVT_RING + 2) * sizeof(FeedTaggedFrameIvt) + 1 + sizeof(FeedBuild) + sizeof(FeedHello) <= FEED_PIPE_BYTES,
              "the pipe no longer holds what can sit unread: the frame write could block on a stuck host");
// The handle is a parameter: the relay thread uses its own duplicate of the pipe, everything else ivtPipe through the old
// names. __forceinline so the old names compile as before (a second caller can cost an inlining).
static __forceinline bool IvtPipeWriteTo(HANDLE pipe, const void* buf, DWORD len)
{
    DWORD put = 0;
    return pipe && WriteFile(pipe, buf, len, &put, nullptr) && put == len;
}
static bool IvtPipeWrite(const void* buf, DWORD len) { return IvtPipeWriteTo(ivtPipe, buf, len); }

// Reads until the message is whole: a byte-mode pipe may answer a ReadFile short.
static __forceinline bool IvtPipeReadFrom(HANDLE pipe, void* buf, DWORD len)
{
    BYTE* p = (BYTE*)buf;
    while (len)
    {
        DWORD got = 0;
        if (!pipe || !ReadFile(pipe, p, len, &got, nullptr) || got == 0) return false;
        p += got; len -= got;
    }
    return true;
}

// A read with a deadline, for the two answers the render thread waits for (the hello's ack and the build's), so a helper
// stuck inside a driver cannot freeze the game. The pipe is not overlapped, so the wait polls what has arrived with a 1 ms
// sleep; a broken pipe ends it at once. On the deadline the caller fails the transport with a reason.
// `stop`, when given, is an event whose set state fails the read at once instead of on the next Sleep(1) quantum.
static __forceinline bool IvtPipeReadWithinOn(HANDLE pipe, HANDLE stop, void* buf, DWORD len, DWORD ms, bool* timedOut)
{
    *timedOut = false;
    if (!pipe) return false;
    const ULONGLONG until = GetTickCount64() + ms;
    for (;;)
    {
        DWORD avail = 0;
                // A peek that fails hands over to the read: a broken pipe fails there too with the reason the caller reports, and a peek
        // refused for another cause does not end the transport on its own.
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return IvtPipeReadFrom(pipe, buf, len);
        if (avail >= len) return IvtPipeReadFrom(pipe, buf, len);
        if (GetTickCount64() >= until) { *timedOut = true; return false; }
        if (stop) { if (WaitForSingleObject(stop, 1) == WAIT_OBJECT_0) return false; }
        else Sleep(1);
    }
}
static bool IvtPipeReadWithin(void* buf, DWORD len, DWORD ms, bool* timedOut)
{
    return IvtPipeReadWithinOn(ivtPipe, nullptr, buf, len, ms, timedOut);
}

// The caller supplies the frame's own numbers (jitter, near plane, field of view), snapshotted at the post: the body may run
// on the worker, where the globals have already moved on to the next frame.
// __forceinline: with the relay as a second caller the compiler stopped inlining this into the Windows send path.
static __forceinline void IvtBuildFrameMsg(FeedTaggedFrameIvt* out, UINT64 n, UINT32 reset, float jitterX, float jitterY,
                                           float cameraNear, float fovYRad)
{
    FeedFrameMsg fm = {};
    fm.n     = n;
    fm.reset = reset;
        // The frame's jitter in pixels, reported exactly as applied, with no sign term: FSR2 hands ffxFsr2GetJitterOffset's pair
    // straight to the dispatch, and NGX documents InJitterOffsetX/Y as the offset the frame was rendered with. Zero when the
    // frame was not jittered (DLAA).
    fm.jitter_x = jitterX;      // read at the post, on the render thread
    fm.jitter_y = jitterY;
        // The frame delta DLSS asks for (InFrameTimeDeltaInMsec), measured at the handoff, the same point in every frame. The first
    // frame reports 0 ("no value"). A wild gap (alt-tab, a load) reports 0 too: a multi-second delta would make DLSS discard
    // history it should keep, and a real cut is signalled by fm.reset.
    {
        static LONGLONG prevQpc = 0;
        LONGLONG now = 0;
        QueryPerformanceCounter((LARGE_INTEGER*)&now);
        if (prevQpc && ivtQpcFreq)
        {
            const double ms = (double)(now - prevQpc) * 1000.0 / (double)ivtQpcFreq;
            fm.frame_time_ms = (ms > 0.0 && ms < 1000.0) ? (float)ms : 0.0f;
        }
                // The camera intrinsics FSR needs and DLSS ignores, taken at the post with the image they describe.
        {
            fm.camera_near = cameraNear;
            fm.fov_y_rad   = fovYRad;
        }
        prevQpc = now;
    }
    out->tag = 'F';
    out->fm  = fm;
}
static bool IvtSendFrame(UINT64 n, UINT32 reset, float jitterX, float jitterY, float cameraNear, float fovYRad)
{
    FeedTaggedFrameIvt msg;
    IvtBuildFrameMsg(&msg, n, reset, jitterX, jitterY, cameraNear, fovYRad);
    return IvtPipeWrite(&msg, sizeof(msg));
}

// Spawn the helper, split out of IvtEnsureHost so it can run at load time: none of the helper's startup (D3D12 device, NGX
// init, the feature) needs a viewport, camera or scene, so it runs while the game sits on its loading screen.
// IvtEnsureHost still calls this, so a late enable behaves as before. The helper's orphan watchdog covers a game killed
// before the pipe connects.
static bool IvtSpawnHost()
{
    if (ivtHost) return true;
    if (ivtFailed) return false;
    {
        char dir[MAX_PATH];
        IvtGameDir(dir, MAX_PATH);
        // cmd must hold the exe path AND the log path, so two MAX_PATHs plus the flags.
        char exe[MAX_PATH], wd[MAX_PATH], cmd[MAX_PATH * 2 + 128];
                // DLSSIV\dlssiv-helper.exe, named once in transport/dlssiv_layout.h. The pipe is dlssiv-feed and the protocol DLSS-IV's own
        // (FEED_IPC_VERSION), so a DLSS5-Feeder host could not serve the mod.
        _snprintf_s(exe, MAX_PATH, _TRUNCATE, "%s" DLSSIV_HOST_DIR "\\" DLSSIV_HOST_EXE, dir);
        _snprintf_s(wd,  MAX_PATH, _TRUNCATE, "%s" DLSSIV_HOST_DIR, dir);
        if (GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
        {
            // The path that was tested, and Windows' error number: the note, the only artefact at log=0, carries whatever this says.
            const DWORD err = GetLastError();
            char why[400];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "no host exe at \"%s\" (Windows error %lu; the path is resolved from GTAIV.exe, not"
                " from the .asi)", exe, err);
            IvtFail(why); return false;
        }
                // The back end goes on the command line, not in the protocol: the helper must know it before it initialises anything (at
        // backend=1 it skips NGX entirely), and FeedBuild arrives too late. preset and the log level travel here too, so a player
        // has one DLSS-IV.cfg and one DLSS-IV.log. --logpath is passed only when logging is on, so log=0 means no log from either
        // process. fsr_version rides on both arms; fsr_debug_view and fsr_warmup in a development build only.
        // The adapter LUID is not here: the helper is spawned during startup, before the game's Vulkan device exists, so the LUID
        // travels in FeedHello (feed_ipc.h), where it is a check rather than a choice.
        // The arguments are checked for fit: every number is clamped to two characters, so the fixed text is bounded and
        // MAX_PATH + 128 holds it with any log path (at most 259 characters). cmd, MAX_PATH * 2 + 128 = 648, needs at most 639:
        // the quoted exe path (261), a space, the pid (10) and these (367).
        char args[MAX_PATH + 128];
        int argsLen;
        if (logLevel > 0)
            // The path is quoted: install paths have spaces, and unquoted tails would reach the helper's pid parser.
            argsLen = _snprintf_s(args, sizeof(args), _TRUNCATE, " --backend=%d --preset=%d --fsr_version=%d --sync=%d --scale=%d --log=%d --logpath=\"%s\"",
                        ivtCfg.backend, ivtCfg.preset, ivtCfg.fsr_version, ivtSyncMode, ivtCfg.custom, logLevel, logPath);
        else
            argsLen = _snprintf_s(args, sizeof(args), _TRUNCATE, " --backend=%d --preset=%d --fsr_version=%d --sync=%d --scale=%d",
                        ivtCfg.backend, ivtCfg.preset, ivtCfg.fsr_version, ivtSyncMode, ivtCfg.custom);
#if DLSSIV_FG
                // Frame generation's flag, only under its static conditions, and it never costs the spawn: where its 7 characters do not
        // fit, here or in the command line below, it is left out and frame generation is off for the session with one line.
        int fgArgAt = -1;
        if (argsLen >= 0 && FgxHelperArg())
        {
            if (_snprintf_s(args + argsLen, sizeof(args) - argsLen, _TRUNCATE, " --fg=1") < 0) { args[argsLen] = 0; FgxArgDropped(); }
            else { fgArgAt = argsLen; argsLen += 7; }
        }
#endif
        if (argsLen < 0)
        {
            IvtFail("the host's arguments did not fit; the log path is too long");
            return false;
        }
                // Truncation is checked, not hoped against: _TRUNCATE cuts silently and returns -1, and a cut command line hands the helper
        // a broken argument list. Refusing with a reason beats a helper that exits for no stated cause.
#if DLSSIV_FG
        if (fgArgAt >= 0 && _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" %lu%s", exe, GetCurrentProcessId(), args) < 0)
        { args[fgArgAt] = 0; FgxArgDropped(); }   // fits without frame generation's flag, or fails below as it always did
#endif
        if (_snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" %lu%s", exe, GetCurrentProcessId(),
                        args) < 0)
        {
            IvtFail("the host command line did not fit; the game path is too long");
            return false;
        }
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wd, &si, &pi))
        {
            // The error number goes into the note too, with 5 named for what it is (access denied).
            const DWORD err = GetLastError();
            char why[200];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "could not start the 64-bit host (Windows error %lu%s)", err,
                err == 5 ? ", access denied" : "");
            IvtFail(why);
            return false;
        }
        CloseHandle(pi.hThread);
        ivtHost = pi.hProcess;
        ivtConnectTries = 0;
        ivtSpawnTick = GetTickCount64();
                // The affinity the child inherits: a game pinned to one CCD pins the helper with it, so it is said.
        {
            DWORD_PTR procMask = 0, sysMask = 0;
            GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask);
            if (procMask == sysMask)
                LogRecord("  transport: host started (pid %lu, +%lu ms)", pi.dwProcessId, (unsigned long)CenMs());
            else
                LogRecord("  transport: host started (pid %lu, +%lu ms) on the game's affinity 0x%llX (%u of %u logical processors)",
                    pi.dwProcessId, (unsigned long)CenMs(), (unsigned long long)procMask, BitCount(procMask), BitCount(sysMask));
        }
        LogHealth("  transport: host command line: %s", cmd);
        return true;
    }
}

// Returns true once connected and greeted; false while still starting (call
// again next frame) or after a failure (ivtFailed is set).
static bool IvtEnsureHost()
{
    if (ivtPipe) return true;
    if (!ivtHost) { IvtSpawnHost(); return false; }   // spawned this frame at the latest; connect next
        // One connect attempt per frame while the helper loads NGX or FidelityFX: never a blocking loop on the render thread.
    char name[128];
    _snprintf_s(name, sizeof(name), _TRUNCATE, FEED_PIPE_FMT, (unsigned long)GetCurrentProcessId());
        // At identification level: whatever holds the pipe's name can learn who the game is but cannot act as the player. The helper
    // never impersonates. Not under Wine, where the pipe is opened plainly.
    HANDLE p = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           ivtWine ? 0 : (SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION), nullptr);
    if (p == INVALID_HANDLE_VALUE)
    {
        if (!IvtHostAlive()) { IvtFail("the host exited during startup; its reason is in this log, prefixed \"host:\", when log is 1 or more"); return false; }
                // A time, not a frame count, so a fast machine does not give the helper less time to load NGX and the driver. The helper is
        // alive here (tested above), so the wait costs one failed CreateFile a frame.
        ++ivtConnectTries;
        if (GetTickCount64() - ivtSpawnTick > 60000)
        { IvtFail("the host's pipe did not appear within 60 s of its start, so the host is stopped"); return false; }
        return false;
    }
    ivtPipe = p;
    ivtPipeOpened = true;   // before the handshake on purpose: a failed handshake still leaves the helper a pipe break to exit on

    HANDLE selfInHost = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), ivtHost, &selfInHost,
                         PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0))
    {
        LogHealth("  transport: could not duplicate the game's process handle into the host (%lu); it will OpenProcess", GetLastError());
        selfInHost = nullptr;
    }
    FeedHello hello = { FEED_IPC_MAGIC, FEED_IPC_VERSION, GetCurrentProcessId(), FEED_CLIENT_VULKAN,
                        (uint64_t)(uintptr_t)selfInHost };
        // Which GPU the game is on. All zero when Vulkan reported no valid LUID, which the helper reads as "do not check".
    if (ivtLuidValid) memcpy(hello.luid, ivtLuid, sizeof(hello.luid));
    strncpy_s(hello.build, IVT_BUILD_ID, _TRUNCATE);   // the helper refuses a build that is not its own
    FeedHelloAck ack = {};
        // 30 s: the helper answers at once unless it is rebuilding its device on the game's GPU (a hybrid laptop), about a second.
    bool late = false;
    if (!IvtPipeWrite(&hello, sizeof(hello)) || !IvtPipeReadWithin(&ack, sizeof(ack), 30000, &late) || ack.magic != FEED_IPC_MAGIC)
    { IvtFail(late ? "the host did not answer the handshake within 30 s; its own lines in this log (\"host:\") say where it stopped" : "handshake failed"); return false; }
    if (ack.version != FEED_IPC_VERSION)
    {
        LogRecord("  transport: host protocol v%u, this .asi v%u", ack.version, FEED_IPC_VERSION);
        IvtFail("protocol version mismatch: " DLSSIV_HOST_DIR "\\ is from a different DLSS-IV build; the .asi and the host ship together");
        return false;
    }
        // What the helper can do, said at the handshake.
    LogRecord("  transport: host offers DLSS %s, FSR %s, XeSS %s",
        (ack.caps & FEED_CAP_DLSS_PROBED) ? ((ack.caps & FEED_CAP_DLSS) ? "available" : "NOT available") : "not probed",
        (ack.caps & FEED_CAP_FSR_PROBED)  ? ((ack.caps & FEED_CAP_FSR)  ? "available" : "NOT available") : "not probed",
        (ack.caps & FEED_CAP_XESS_PROBED) ? ((ack.caps & FEED_CAP_XESS) ? "available" : "NOT available") : "not probed");
    FGX(FgxOnCaps(ack.caps);)   // frame generation keeps what the helper said; one line in a session that asked (fg=1)
        // The two halves are not one build: the helper said so in the ack and named both builds in its own log line.
    if (ack.caps & FEED_CAP_BUILD_MISMATCH)
    { IvtFail("DLSS-IV.asi is build " IVT_BUILD_ID " and the host in " DLSSIV_HOST_DIR "\\ is from a different build (the first"
              " \"host:\" line in DLSS-IV.log names the host's build). The two ship together: copy BOTH"
              " DLSS-IV.asi and the " DLSSIV_HOST_DIR " folder from the same download"); return false; }
        // The helper refused: the game and the helper could not be put on one GPU (the rebind failed).
    if (ack.caps & FEED_CAP_REFUSED)
    { IvtFail("the host refused this session: the game and the upscaler host could not be put on the same GPU. Pin GTAIV.exe to one"
              " GPU in Windows Graphics settings, or make that GPU the system default, and relaunch (the \"host:\" line"
              " in DLSS-IV.log names the adapter)"); return false; }
        // The cfg's choice against what the helper can do, from the handshake (not in transport-test mode, mode=1). Why DLSS is not
    // running, in the helper's words: PROBED set means an NVIDIA adapter where NGX was tried, and the DRIVER bit says NGX blamed
    // the driver, which an update fixes. PROBED clear is a non-NVIDIA adapter, where DLSS was never tried.
    const bool dlssTried  = (ack.caps & FEED_CAP_DLSS_PROBED) != 0;
    const bool dlssDriver = (ack.caps & FEED_CAP_DLSS_DRIVER) != 0;
    const bool dlssNoFile = (ack.caps & FEED_CAP_DLSS_NOFILE) != 0;   // NGX found no nvngx_dlss.dll: it ships in DLSSIV\, so it went missing
        // An NVIDIA GPU the helper was not shown: under Proton the helper's DXGI is DXVK's, which reports an NVIDIA card as another
    // vendor unless NVAPI is enabled (dxgi_options.cpp, hideNvidiaGpu). The game's Vulkan device (hdrVkVendor) is the real card.
    const bool nvHidden = ivtWine && hdrVkVendor == 0x10DE && !dlssTried;
    const bool nvWine   = ivtWine && hdrVkVendor == 0x10DE;
    const char* noDlss = nvHidden ? "Wine hides this NVIDIA GPU from the upscaler (DXVK's DXGI without NVAPI), so DLSS was not tried: under"
                                    " Proton add PROTON_ENABLE_NVAPI=1 to the game's launch options, under other Wine set DXVK_ENABLE_NVAPI=1"
                                    " with dxvk-nvapi installed"
                       : !dlssTried ? "this GPU is not NVIDIA, so DLSS was not tried"
                       : dlssDriver ? "the host tried DLSS on this NVIDIA GPU and NGX said the NVIDIA driver is older than this DLSS needs"
                                      " (a driver update brings DLSS back)"
                       : dlssNoFile ? "NVIDIA's " DLSSIV_HOST_DIR "\\nvngx_dlss.dll is not there. It ships with this mod, so copy the " DLSSIV_HOST_DIR
                                      " folder over again. An RTX card (20 series or newer) needs it for DLSS; an NVIDIA card older than"
                                      " that cannot run DLSS, and FSR is right for it"
                       : nvWine     ? "the host tried DLSS on this NVIDIA GPU and could not get it running (the \"host:\" lines in DLSS-IV.log"
                                      " say whether that is the card or the driver); under Proton DLSS also needs PROTON_ENABLE_NVAPI=1"
                                      " in the game's launch options (other Wine: DXVK_ENABLE_NVAPI=1 with dxvk-nvapi)"
                                    : "the host tried DLSS on this NVIDIA GPU and could not get it running (the \"host:\" lines in DLSS-IV.log"
                                      " say whether that is the card or the driver)";
    if (ivtCfg.backend == 0 && IVT_MODE != 1 && dlssTried && !(ack.caps & FEED_CAP_DLSS))
    {
        char why[512];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "upscaler=0 asks for DLSS and %s. Set upscaler=-1 and the mod picks FSR", noDlss);
        IvtFail(why); return false;
    }
    if (ivtCfg.backend == 1 && IVT_MODE != 1 && (ack.caps & FEED_CAP_FSR_PROBED) && !(ack.caps & FEED_CAP_FSR))
    { IvtFail("upscaler=1 asks for FSR and AMD's FidelityFX files in " DLSSIV_HOST_DIR "\\ (amd_fidelityfx_loader_dx12.dll and"
              " amd_fidelityfx_upscaler_dx12.dll) are missing or would not load; copy the " DLSSIV_HOST_DIR " folder over again");
      return false; }
        // Intel XeSS: upscaler=2 that cannot run stops here with its reason, and automatic that fell to FSR on an Intel card says
    // so in the warnings file. One call, defined at DLSS-IV.cpp's end: the same lines written here moved the release's hot data.
    if (!IvtXessHandshake(ack.caps)) return false;
        // Automatic: the helper probes exactly the back end it resolved to, so FSR probed and not available means auto picked FSR
    // and its library did not load.
    if (ivtCfg.backend < 0 && IVT_MODE != 1
        && !(dlssTried && (ack.caps & FEED_CAP_DLSS))
        && (ack.caps & FEED_CAP_FSR_PROBED) && !(ack.caps & FEED_CAP_FSR))
    {
        char why[512];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "upscaler=-1 picked FSR because %s, and AMD's FidelityFX files in " DLSSIV_HOST_DIR "\\"
            " (amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_upscaler_dx12.dll) are missing or would not load; copy the "
            DLSSIV_HOST_DIR " folder over again", noDlss);
        IvtFail(why); return false;
    }
        // Automatic on an NVIDIA GPU that fell back to FSR is said in the warnings file (the fix, a driver update, is the player's).
    // On a non-NVIDIA GPU automatic means FSR, which is not a warning.
    if (ivtCfg.backend < 0 && IVT_MODE != 1 && (dlssTried || nvHidden) && !(ack.caps & FEED_CAP_DLSS) && (ack.caps & FEED_CAP_FSR))
        IvtWarn("upscaler=-1 (automatic) is running FSR, not DLSS: %s", noDlss);   // nvHidden: the NVIDIA card Proton hid
        // The ms stamps on the engage path ("armed", spawn, connect, build, "FIRST INJECTION") say which segment owns a delay.
    LogRecord("  transport: host connected (+%lu ms, attempt %d, protocol v%u)", (unsigned long)CenMs(), ivtConnectTries + 1, ack.version);
    return true;
}

// The back buffer's VkImage right now, from the surface the render-target hook says is bound. D3D9 calls, so only before
// the queue is locked. ivtBbLiveFmt is its real Vulkan format, refreshed on every fetch: the copy home decides from it, not
// from what the game asked for (the back buffer can be FP16 while the game believes it is A8R8G8B8, and a raw copy of an
// 8-bit output into it loses the device).
static VkFormat ivtBbLiveFmt = VK_FORMAT_UNDEFINED;

static bool IvtLiveBackbuffer(VkImage* img, VkImageLayout* layout)
{
    DT_DIRECT();                 // an image and its layout as DXVK holds them now: see devthread/dt_core.h
    IDirect3DSurface9* surf = backbufferSurf;
    if (!surf) return false;
        // Fresh every time: the interop object is the current common texture's.
    ID3D9VkInteropTexture* it = nullptr;
    if (FAILED(surf->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return false;
    VkImage i = VK_NULL_HANDLE;
    VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
        // sType must be set: DXVK validates it and returns D3DERR_INVALIDCALL for a zeroed one.
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    const HRESULT hr = it->GetVulkanImageInfo(&i, &l, &info);
    it->Release();
    if (FAILED(hr) || !i) return false;
    *img = i; *layout = l;
    if (info.format != VK_FORMAT_UNDEFINED && info.format != ivtBbLiveFmt)
    {
        LogHealth("  transport: the back buffer's Vulkan format is %d (was %d); the copy home will %s",
            (int)info.format, (int)ivtBbLiveFmt,
            FeedVkFormat(ivtOutputFmt) == info.format ? "copy raw" : "BLIT (formats differ; a raw copy would be undefined)");
        ivtBbLiveFmt = info.format;
    }
    return true;
}

// ---- The Linux sync path: the mod's semaphores, the probe, the relay ----
// The design is with the globals, above ivtWine. Defined here so IvtBuild can call them; IvtSignalOutTo and its setter,
// defined further down with the Vulkan entry points, are declared first.
static VkResult IvtSignalOutTo(UINT64 top, bool* did);
static void     IvtNoteSignalledTo(UINT64 v);   // the release bookkeeping learns a value the GPU or the helper already reached

// The two semaphores are the mod's when sync is 1 or 2: created on DXVK's device, exported as OPAQUE_WIN32 when the device
// can, and duplicated into the helper, which opens them as fences. Failing to export or duplicate is not a session failure:
// the semaphores stay local and the bridge carries the frames.
static bool IvtMakeOurSemaphores()
{
    HANDLE hin = nullptr, hout = nullptr;
    const int a = FeedVkCreateTimeline(&ivtVk, true, &ivtSemIn,  &hin);
    const int b = FeedVkCreateTimeline(&ivtVk, true, &ivtSemOut, &hout);
    if (a < 0 || b < 0) { IvtFail("the mod's timeline semaphores could not be created"); return false; }
    ivtSemsMade = true;
    if (hin && hout && ivtHost)
    {
        HANDLE hi = nullptr, ho = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), hin,  ivtHost, &hi, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
            DuplicateHandle(GetCurrentProcess(), hout, ivtHost, &ho, 0, FALSE, DUPLICATE_SAME_ACCESS))
        { ivtSemHostIn = (UINT64)(uintptr_t)hi; ivtSemHostOut = (UINT64)(uintptr_t)ho; }
        else
        {
            const DWORD err = GetLastError();
// Closing a handle that lives in the helper: DuplicateHandle with no target and DUPLICATE_CLOSE_SOURCE is the documented way
            // (CloseHandle only reaches this process). The analyzer misreads the two NULLs; the idiom is Microsoft's.
#pragma warning(suppress: 6387)
            if (hi) DuplicateHandle(ivtHost, hi, nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);   // the half that landed
            LogRecord("  transport: sync: semaphore handles not duplicated into the host (error %lu): CPU bridge", err);
        }
    }
    else if (!hin || !hout)
        LogRecord("  transport: sync: no timeline semaphore export as a Win32 handle (%s): CPU bridge",
                  ivtVk.GetSemaphoreWin32HandleKHR ? "the export was refused" : "no vkGetSemaphoreWin32HandleKHR");
        // Frame numbers start above the probe's value: the probe signals kIvtProbeN on both semaphores and a timeline only moves up.
    // Numbers are never reset in a session, so this happens once.
    if (ivtFrameN < kIvtProbeN) ivtFrameN = kIvtProbeN;
    return true;
}

// The relay queue (bridge mode): the body builds the frame message as it would have written it, so its timing fields are
// the same, and queues it; the relay thread does the rest.
static bool IvtRelayQueue(UINT64 n, UINT32 reset, float jitterX, float jitterY, float cameraNear, float fovYRad)
{
    if (ivtWatchFired == 2) { SetLastError(ERROR_BROKEN_PIPE); return false; }   // the relay has stopped; nothing would answer this frame
    FeedTaggedFrameIvt msg;
    IvtBuildFrameMsg(&msg, n, reset, jitterX, jitterY, cameraNear, fovYRad);
    bool ok = false;
    AcquireSRWLockExclusive(&ivtRelayLock);
    if (ivtRelayCount < IVT_RELAY_CAP)
    {
        ivtRelayQ[(ivtRelayHead + ivtRelayCount) % IVT_RELAY_CAP] = msg;
        ivtRelayCount++;
        if (ivtRelayInFlight++ == 0) ResetEvent(ivtRelayIdle);
        ok = true;
    }
    ReleaseSRWLockExclusive(&ivtRelayLock);
    if (ok) SetEvent(ivtRelayWake); else SetLastError(ERROR_BUSY);   // the body reads GetLastError into its reason
    return ok;
}
// Wait until every queued frame has been answered, for a build: the build's ack has no tag, so a frame's answer
// arriving in the middle of the exchange would be read as the ack.
static bool IvtRelayDrain(DWORD ms)
{
    AcquireSRWLockExclusive(&ivtRelayLock); const int inFlight = ivtRelayInFlight; ReleaseSRWLockExclusive(&ivtRelayLock);
    if (!inFlight || !ivtRelayIdle) return true;
    return WaitForSingleObject(ivtRelayIdle, ms) == WAIT_OBJECT_0;
}
// Wait for the helper's answer event, not the pipe: the helper sets `*answer` after the answer's write, so a wake means the
// bytes are in the pipe (a 1 ms peek loop would release the GPU up to a timer tick late). The stop is in the same wait and
// first: WaitForMultipleObjects reports the lowest set index, so IvtFail and IvtShutdown end this as they end the poll.
// The read that follows is the poll, so its outcomes are unchanged. A helper that answers without setting the event: every
// 100 ms slice peeks, and an answer whose set does not follow within 20 ms clears `*answer`, so the relay polls from then on.
// Returns what is left of `ms` for the read: at least 1 on every early return, since 0 would read as "within 6 s".
static DWORD IvtRelayAwaitAnswer(HANDLE pipe, HANDLE* answer, DWORD ms)
{
    const HANDLE hs[2] = { ivtRelayStop, *answer };
    const ULONGLONG until = GetTickCount64() + ms;
    auto rest = [until]() -> DWORD { const ULONGLONG now = GetTickCount64(); return now >= until ? 1u : (DWORD)(until - now); };
    for (;;)
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= until) return 0;
        const DWORD left = (DWORD)(until - now);
        const DWORD w    = WaitForMultipleObjects(2, hs, FALSE, left < 100 ? left : 100);
        if (w != WAIT_TIMEOUT) return rest();        // the answer is in (1), the stop (0), or a failed wait: the read decides
        DWORD avail = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) return rest();   // a broken pipe: the read reports it
        if (avail < sizeof(FeedAnswerIvt)) continue;
        if (WaitForMultipleObjects(2, hs, FALSE, 20) != WAIT_TIMEOUT) return rest();   // set a moment after the write (or the stop)
        *answer = nullptr;
        LogRecord("  transport: bridge: host answered without its answer event: relay polls");
        return rest();
    }
}
static DWORD WINAPI IvtRelayThread(void*)
{
    HANDLE evs[2] = { ivtRelayStop, ivtRelayWake };
    const HANDLE pipe = ivtRelayPipe;          // the relay's alone; closed on every way out of this function
    HANDLE answer = ivtRelayAnswer;            // null polls; cleared here, never closed, if the helper stops setting it
    for (;;)
    {
        FeedTaggedFrameIvt m = {}; bool have = false;
        AcquireSRWLockExclusive(&ivtRelayLock);
        if (ivtRelayCount) { m = ivtRelayQ[ivtRelayHead]; ivtRelayHead = (ivtRelayHead + 1) % IVT_RELAY_CAP; ivtRelayCount--; have = true; }
        ReleaseSRWLockExclusive(&ivtRelayLock);
        if (!have)
        {
            if (WaitForMultipleObjects(2, evs, FALSE, INFINITE) == WAIT_OBJECT_0) { CloseHandle(pipe); ivtRelayPipe = nullptr; return 0; }   // the stop
            continue;
        }
#if DLSSIV_DEV
        LONGLONG t0 = 0, t1 = 0; if (logLevel >= 2) QueryPerformanceCounter((LARGE_INTEGER*)&t0);
#endif
        const char* why = nullptr;
                // The mod's own copies, in slices: a slow game GPU is not a reason to end the upscaler. Wait again until the value arrives,
        // the stop is set, or the wait itself fails (a lost device). Only the last two end the relay.
        for (;;)
        {
            const VkResult wr = FeedVkWaitTimelineR(&ivtVk, ivtSemIn, m.fm.n, 500);
            if (wr == VK_SUCCESS) break;
            if (wr != VK_TIMEOUT) { why = "the mod's own copies could not be waited for (the device is lost?)"; break; }
            if (WaitForSingleObject(ivtRelayStop, 0) == WAIT_OBJECT_0)
            {
                // stopped by IvtFail or IvtShutdown, which release the wait themselves: leave quietly
                AcquireSRWLockExclusive(&ivtRelayLock); ivtRelayInFlight = 0; ReleaseSRWLockExclusive(&ivtRelayLock);
                SetEvent(ivtRelayIdle); CloseHandle(pipe); ivtRelayPipe = nullptr; return 0;
            }
        }
#if DLSSIV_DEV
        if (logLevel >= 2) QueryPerformanceCounter((LARGE_INTEGER*)&t1);
#endif
        if (!why)   // the round trip only for a frame whose copies retired
        {
            if (!IvtPipeWriteTo(pipe, &m, sizeof(m))) why = "the frame message could not be written (pipe broken)";
            else
            {
                FeedAnswerIvt a = {}; bool late = false;
                const DWORD within = answer ? IvtRelayAwaitAnswer(pipe, &answer, 6000) : 6000;   // the event first, then the read
                if (!IvtPipeReadWithinOn(pipe, ivtRelayStop, &a, sizeof(a), within, &late) || a.tag != 'R' || a.n != m.fm.n)
                    why = late ? "the host did not answer a frame within 6 s" : "the host's answer was missing or wrong (pipe broken, or stopped)";
            }
        }
        bool did = false;
        if (!why && FeedVkTimelineValue(&ivtVk, ivtSemOut) < m.fm.n && IvtSignalOutTo(m.fm.n, &did) != VK_SUCCESS)
            why = "the GPU wait could not be released from the CPU (vkSignalSemaphore failed)";
        if (why)
        {
            // The order: release the queued wait, write the reason, publish the stop, and only then say idle, so a rebuild's drain that
            // wakes on idle finds the stop already published.
            IvtSignalOutTo((UINT64)(ULONG)ivtWatchAwait + 1, &did);
            _snprintf_s(ivtRelayWhy, sizeof(ivtRelayWhy), _TRUNCATE, "the Linux bridge stopped: %s", why);
            _InterlockedCompareExchange(&ivtWatchFired, 2, 0);
            AcquireSRWLockExclusive(&ivtRelayLock); ivtRelayInFlight = 0; ReleaseSRWLockExclusive(&ivtRelayLock);
            SetEvent(ivtRelayIdle);
            CloseHandle(pipe); ivtRelayPipe = nullptr;
            return 0;
        }
        AcquireSRWLockExclusive(&ivtRelayLock);
        if (--ivtRelayInFlight <= 0) { ivtRelayInFlight = 0; SetEvent(ivtRelayIdle); }
        ReleaseSRWLockExclusive(&ivtRelayLock);
#if DLSSIV_DEV
        if (logLevel >= 2)   // the instrument runs only where its line prints
        {
            LONGLONG t2 = 0; QueryPerformanceCounter((LARGE_INTEGER*)&t2);
            const double trip = ivtQpcFreq ? (double)(t2 - t1) * 1e6 / (double)ivtQpcFreq : 0.0;   // message -> answer -> signal
            const double gpu  = ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0;   // waiting for the mod's own copies
            ivtRelayUsSum += trip; ivtRelayGpuUsSum += gpu; if (trip > ivtRelayUsMax) ivtRelayUsMax = trip;
            if ((++ivtRelayTrips % 1800) == 0)
            {
                LogHealth("  transport: bridge: %ld round trips, %.0f us a frame on average, worst %.0f us (message -> answer -> signal, the bridge's own cost);"
                    " before each, %.0f us a frame waiting for the mod's own copies to retire, which is the GPU's frame and not the bridge's",
                    ivtRelayTrips, ivtRelayUsSum / (double)ivtRelayTrips, ivtRelayUsMax, ivtRelayGpuUsSum / (double)ivtRelayTrips);
                ivtRelayUsMax = 0.0;
            }
        }
#endif
    }
}
static bool IvtRelayStartFailed()   // nothing left behind, so IvtFail's pipe close reaches the helper and a retry starts clean
{
    if (ivtRelayPipe) { CloseHandle(ivtRelayPipe); ivtRelayPipe = nullptr; }
    if (ivtRelayStop) { CloseHandle(ivtRelayStop); ivtRelayStop = nullptr; }
    if (ivtRelayWake) { CloseHandle(ivtRelayWake); ivtRelayWake = nullptr; }
    if (ivtRelayIdle) { CloseHandle(ivtRelayIdle); ivtRelayIdle = nullptr; }
    if (ivtRelayAnswer) { CloseHandle(ivtRelayAnswer); ivtRelayAnswer = nullptr; }   // no thread ever read it
    return false;
}
static bool IvtRelayStart()
{
    if (ivtRelayStop) return true;
    const HANDLE me = GetCurrentProcess(); HANDLE dup = nullptr;
    if (!ivtPipe || !me || !DuplicateHandle(me, ivtPipe, me, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS) || !dup)
        return false;                                                // the relay's own handle, see ivtRelayPipe
    ivtRelayPipe = dup;
    ivtRelayStop = CreateEventA(nullptr, TRUE, FALSE, nullptr);     // manual, once
    ivtRelayWake = CreateEventA(nullptr, FALSE, FALSE, nullptr);    // auto: one wake a queue
    ivtRelayIdle = CreateEventA(nullptr, TRUE, TRUE, nullptr);      // manual: set while nothing is in flight
    if (!ivtRelayStop || !ivtRelayWake || !ivtRelayIdle) return IvtRelayStartFailed();
    const HANDLE t = CreateThread(nullptr, 64 * 1024, IvtRelayThread, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!t) return IvtRelayStartFailed();
    CloseHandle(t);
    return true;
}
// Never a join, for the reason IvtHostWatchStop gives: the thread touches nothing that goes away, and the pipe
// closing after this is what ends a read it may be in the middle of.
static void IvtRelayStop() { if (ivtRelayStop) SetEvent(ivtRelayStop); }

// The probe, once a session, after the first build's ack and before the textures: decides FAST or BRIDGE from facts, never
// from the import's return value, and tells the helper.
static bool IvtSyncProbe()
{
    ivtProbeDone = true;
    const bool sharedSems = ivtSemHostIn != 0 && ivtSemHostOut != 0;
    if (ivtSignalSemaphore && ivtSemIn)
    {
        VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
        si.semaphore = ivtSemIn; si.value = kIvtProbeN;
        const VkResult sr = ivtSignalSemaphore(ivtDevice, &si);
        if (sr != VK_SUCCESS)   // a local failure must not read as a broken cross-process link
            LogRecord("  transport: sync probe: the CPU signal of the in-semaphore failed (VkResult %d): game -> host NOT connected", (int)sr);
    }
    FeedProbeIvt  p = { 'P', kIvtProbeN };
    FeedAnswerIvt a = {};
    bool late = false;
    {
        IvtWatch w("sync probe", true);
        if (!IvtPipeWrite(&p, sizeof(p)) || !IvtPipeReadWithin(&a, sizeof(a), 8000, &late) || a.tag != 'R' || a.n != kIvtProbeN)
        { IvtFail(late ? "the host did not answer the sync probe within 8 s" : "the sync probe failed (pipe broken, or a wrong answer)"); return false; }
    }
    const bool opened = (a.status & FEED_ANS_OPENED) != 0;
    const bool inOk   = (a.status & FEED_ANS_IN_OK) != 0;
    const bool outOk  = opened && FeedVkWaitTimeline(&ivtVk, ivtSemOut, kIvtProbeN, 1500);
        // The release bookkeeping learns the probe's value whatever the mode: the helper may have taken semOut to kIvtProbeN, and
    // IvtSignalOutTo compares only against what this side signalled, so a stop before the first frame would signal backwards.
    // Below it, signal it; at or above it, note it.
    {
        bool did = false;
        if (FeedVkTimelineValue(&ivtVk, ivtSemOut) < kIvtProbeN) IvtSignalOutTo(kIvtProbeN, &did);
        else IvtNoteSignalledTo(kIvtProbeN);
    }
    const bool fast   = ivtSyncMode == 1 && sharedSems && opened && inOk && outOk;
    ivtBridge = !fast;
        // The answer event goes with the mode (feed_ipc.h has the negotiation): only on the bridge, only to a helper that said
    // FEED_ANS_EVENT, and only once it is really in the helper (EVENT_MODIFY_STATE). Otherwise the relay polls.
    HANDLE ansHost = nullptr; DWORD ansErr = 0;
    if (ivtBridge && (a.status & FEED_ANS_EVENT) && ivtHost)
    {
        ivtRelayAnswer = CreateEventA(nullptr, FALSE, FALSE, nullptr);   // auto: one set an answer, one wake a frame
        if (!ivtRelayAnswer || !DuplicateHandle(GetCurrentProcess(), ivtRelayAnswer, ivtHost, &ansHost, EVENT_MODIFY_STATE, FALSE, 0) || !ansHost)
        {
            ansErr = GetLastError(); ansHost = nullptr;
            if (ivtRelayAnswer) { CloseHandle(ivtRelayAnswer); ivtRelayAnswer = nullptr; }
        }
    }
    FeedModeEventIvt m = { 'M', fast ? 1u : 2u, (UINT64)(uintptr_t)ansHost };
    if (ansHost) m.mode |= FEED_MODE_ANSWER_EVENT;
    if (!IvtPipeWrite(&m, ansHost ? (DWORD)sizeof(FeedModeEventIvt) : (DWORD)sizeof(FeedModeIvt)))
    {
        if (ivtRelayAnswer) { CloseHandle(ivtRelayAnswer); ivtRelayAnswer = nullptr; }
        IvtFail("the sync mode could not be sent (pipe broken)"); return false;
    }
    LogRecord("  transport: sync probe: semaphores %s, the host %s them, game -> host %s, host -> game %s: %s",
        sharedSems ? "shared with the host" : "local only", opened ? "opened" : "could not open",
        inOk ? "connected" : "NOT connected", outOk ? "connected" : "NOT connected",
        fast ? "FAST (GPU waits and signals)" : ivtSyncMode == 2 ? "BRIDGE (sync=2)" : "BRIDGE (the CPU relays each frame)");
    if (ivtBridge)   // which way the relay learns of each answer
    {
        if (ansHost)
            LogRecord("  transport: bridge: the relay wakes on the host's answer event");
        else if (a.status & FEED_ANS_EVENT)
            LogRecord("  transport: bridge: answer event not handed over (error %lu): relay polls", ansErr);
        else
            LogRecord("  transport: bridge: no answer event from this host: relay polls");
    }
    if (ivtBridge && !IvtRelayStart()) { IvtFail("the relay thread could not be started"); return false; }
    return true;
}

// ---- The shared set ----

// No back buffer at all is a different answer from "not DXVK": DXVK's Reset destroys the back buffers before either of its
// failure returns, so after a failed Reset (a lost device in exclusive fullscreen, until the game regains focus) GetBackBuffer
// fails for a while. This flag lets the caller wait instead of failing the session.
static bool ivtBbAbsent = false;

static bool IvtBackbufferInfo(IDirect3DDevice9* dev, VkImage* img, VkImageLayout* layout,
                              UINT* w, UINT* h, VkFormat* fmt)
{
    DT_DIRECT();                 // the image and layout DXVK holds now: see devthread/dt_core.h
    IDirect3DSurface9* bb = nullptr;
    ivtBbAbsent = FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb;
    if (ivtBbAbsent) return false;
    ID3D9VkInteropTexture* it = nullptr;
    bool ok = false;
    if (SUCCEEDED(bb->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) && it)
    {
        VkImage i = VK_NULL_HANDLE;
        VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        if (SUCCEEDED(it->GetVulkanImageInfo(&i, &l, &info)) && i)
        {
            *img = i; *layout = l;
            *w = info.extent.width; *h = info.extent.height; *fmt = info.format;
            ok = true;
            static bool said = false;
            if (!said)
            {
                said = true;
                LogDetail("  transport: backbuffer VkImage 0x%016llX %ux%u format %d, layout %d, usage 0x%X, samples %d",
                    (unsigned long long)i, *w, *h, (int)info.format, (int)l, (unsigned)info.usage, (int)info.samples);
            }
        }
        it->Release();
    }
    bb->Release();
    return ok;
}

static DXGI_FORMAT IvtColorFormatFor(VkFormat f)
{
    switch (f)
    {
    case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:                     return DXGI_FORMAT_R10G10B10A2_UNORM;
    case VK_FORMAT_R16G16B16A16_SFLOAT:                          return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:                                                     return DXGI_FORMAT_UNKNOWN;
    }
}

static void IvtReleaseImages()
{
        // Destroys images a recorded command buffer may reference: must not overlap a record-and-submit (see ivtSharedCs).
    IvtSharedGuard sharedGuard;
    if (!ivtVk.ok) return;
    for (int i = 0; i < FEED_SLOTS; i++)
    {
        if (ivtImg[i]) { ivtVk.DestroyImage(ivtDevice, ivtImg[i], nullptr); ivtImg[i] = VK_NULL_HANDLE; }
        if (ivtMem[i]) { ivtVk.FreeMemory(ivtDevice, ivtMem[i], nullptr);   ivtMem[i] = VK_NULL_HANDLE; }
        if (ivtTexHandle[i]) { CloseHandle(ivtTexHandle[i]); ivtTexHandle[i] = nullptr; }
    }
    ivtLayoutInit = false;
    for (int i = 0; i < FEED_SLOTS; i++) ivtImgReleased[i] = false;   // re-initialised with the next set
    ivtOutValid   = false;
}

static bool IvtOwnImage(IDirect3DBaseTexture9* tex, VkImage* img, VkImageLayout* layout, VkFormat* fmt);

// Why a texture import failed, in one line a player can send: the step, its VkResult, the memory types tried, and what
// vkGetPhysicalDeviceImageFormatProperties2 says of this image and handle type. Failure path only.
static const char* IvtVkResultName(VkResult r)
{
    switch ((int)r)
    {
    case 0: return "VK_SUCCESS"; case -1: return "VK_ERROR_OUT_OF_HOST_MEMORY"; case -2: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case -3: return "VK_ERROR_INITIALIZATION_FAILED"; case -4: return "VK_ERROR_DEVICE_LOST"; case -5: return "VK_ERROR_MEMORY_MAP_FAILED";
    case -7: return "VK_ERROR_EXTENSION_NOT_PRESENT"; case -8: return "VK_ERROR_FEATURE_NOT_PRESENT"; case -11: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case -13: return "VK_ERROR_UNKNOWN"; case -1000069000: return "VK_ERROR_OUT_OF_POOL_MEMORY"; case -1000072003: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    default: return "VkResult (see the number)";
    }
}
static const char* IvtHandleTypeName(VkExternalMemoryHandleTypeFlagBits t)
{
    return t == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT ? "OPAQUE_WIN32" : t == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT ? "D3D12_RESOURCE" : "other";
}
static void IvtLogImportFailure(int slot, const char* name, UINT w, UINT h, DXGI_FORMAT f, VkFormat vkf, bool storage, VkExternalMemoryHandleTypeFlagBits type)
{
    const char* step = feedVkImportFailStep == 1 ? "vkCreateImage" : feedVkImportFailStep == 2 ? "vkAllocateMemory (the import itself)" : feedVkImportFailStep == 3 ? "vkBindImageMemory" : "an unknown step";
    int tried = 0; for (uint32_t b = feedVkImportTriedTypes; b; b &= b - 1) tried++;
        // Memory types only once one was chosen: the first tried if the import failed, the one imported if the bind failed.
    char types[256] = "";
    if (feedVkImportFailStep >= 2)
        _snprintf_s(types, sizeof(types), _TRUNCATE, " after %d memory type(s) (mask 0x%X, %s %u); %s", tried, feedVkImportTriedTypes,
            feedVkImportFailStep == 2 ? "the first tried" : "imported as", feedVkImportTypeIndex,
            feedVkImportTypeHow == 1 ? "the handle's own properties narrowed the types" : feedVkImportTypeHow == 2 ? "the handle's properties shared no type with the image" : feedVkImportTypeHow == 3 ? "the handle's property query failed" : "the types were the image's own (no narrowing)");
    LogRecord("  transport: slot %d (%s) %ux%u dxgi %d vk %d as %s: %s refused with %s (%d)%s",
        slot, name, w, h, (int)f, (int)vkf, IvtHandleTypeName(type), step, IvtVkResultName(feedVkImportFailResult), (int)feedVkImportFailResult,
        types);
    // What the device says about this image and handle type.
    if (ivtVkPhys == VK_NULL_HANDLE || !ivtVk.lib) { LogRecord("  transport: no physical device to ask what it can import"); return; }
    typedef PFN_vkVoidFunction (VKAPI_PTR* GipaFn)(VkInstance, const char*);
    const GipaFn gipa = (GipaFn)(void*)FeedVkProc(ivtVk.lib, "vkGetInstanceProcAddr");
    PFN_vkGetPhysicalDeviceImageFormatProperties2 q = nullptr;
    if (gipa && ivtVkInst != VK_NULL_HANDLE)
    {
        q = (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(ivtVkInst, "vkGetPhysicalDeviceImageFormatProperties2");
        if (!q) q = (PFN_vkGetPhysicalDeviceImageFormatProperties2)gipa(ivtVkInst, "vkGetPhysicalDeviceImageFormatProperties2KHR");
    }
    if (!q) { LogRecord("  transport: no vkGetPhysicalDeviceImageFormatProperties2: import support not asked"); return; }
    VkPhysicalDeviceExternalImageFormatInfo ext = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO };
    ext.handleType = type;
    VkPhysicalDeviceImageFormatInfo2 info = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
    info.pNext  = &ext;
    info.format = vkf; info.type = VK_IMAGE_TYPE_2D; info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage  = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (storage ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT);
    VkExternalImageFormatProperties ep = { VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
    VkImageFormatProperties2 props = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
    props.pNext = &ep;
    const VkResult r = q(ivtVkPhys, &info, &props);
    if (r != VK_SUCCESS) { LogRecord("  transport: the device cannot create this image with %s: %s (%d)", IvtHandleTypeName(type), IvtVkResultName(r), (int)r); return; }
    const VkExternalMemoryFeatureFlags fe = ep.externalMemoryProperties.externalMemoryFeatures;
    LogRecord("  transport: %s for this image:%s importable,%s exportable,%s dedicated-only; compatible 0x%X, export-from 0x%X",
        IvtHandleTypeName(type), (fe & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ? "" : " NOT", (fe & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) ? "" : " not",
        (fe & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) ? "" : " not", (unsigned)ep.externalMemoryProperties.compatibleHandleTypes, (unsigned)ep.externalMemoryProperties.exportFromImportedHandleTypes);
}

static bool IvtBuild(IDirect3DDevice9* dev)
{
    DT_DIRECT();                 // creates, then asks DXVK for images: see devthread/dt_core.h
    VkImage bbImg = VK_NULL_HANDLE; VkImageLayout bbLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    UINT w = 0, h = 0; VkFormat bbf = VK_FORMAT_UNDEFINED;
    if (!IvtBackbufferInfo(dev, &bbImg, &bbLayout, &w, &h, &bbf))
    {
        if (ivtBbAbsent)
        {
            // See ivtBbAbsent: no Reset has succeeded yet; try again shortly.
            static LONG said = 0;
            if (telemetryOn && said++ < 3)
                LogRecord("  transport: no back buffer yet (no Reset has succeeded); again in 500 ms");
            ivtRetryAt = GetTickCount64() + 500;
            return false;
        }
        IvtFail("could not read the backbuffer's Vulkan image"); return false;
    }
        // The colour format comes from the captured scene buffer, so the build waits until the seam detector has found it.
    VkImage scImg = VK_NULL_HANDLE; VkImageLayout scL = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat scf = VK_FORMAT_UNDEFINED;
    if (!sceneBufTex || !IvtOwnImage(sceneBufTex, &scImg, &scL, &scf))
    {
                // Log why it waits: a silent wait looks like a menu.
        DEVSTAT(static LONG waitFrames = 0;)
        DEVSTAT(if (telemetryOn && ((++waitFrames) % 600) == 1)
            LogHealth("  transport: WAITING for the scene buffer, %ld frames. FP16 targets paired %d,"
                " render size %ux%u. Normal in a menu or a load; if it never clears, no"
                " render-size single-level FP16 target has been drawn into by the scene.",
                waitFrames, fpTabN, rsActive ? rsW : rsOutW, rsActive ? rsH : rsOutH);)
        return false;
    }
    const DXGI_FORMAT color = IvtColorFormatFor(scf);
    if (color == DXGI_FORMAT_UNKNOWN)
    {
        LogRecord("  transport: the scene buffer VkFormat %d has no shared-texture equivalent", (int)scf);
        IvtFail("unsupported scene buffer format");
        return false;
    }
        // Command buffers still reading the old set must retire before the images go. A timeout is fatal: destroying images in use on
    // the GPU removes the device, while leaking the set only costs VRAM. IvtFail also releases the queued GPU wait.
    if (ivtSubmitOk != 0 && !FeedVkWaitTimeline(&ivtVk, ivtSemLocal, ivtSubmitOk, 2000))
    {
        IvtFail("the mod's last submit did not retire within 2 s; the shared set cannot"
                " be released safely, so it is deliberately leaked");
        return false;
    }
    FGX(FgxBeforeRelease();)    // the game's imports of frame generation's set go first; the host frees its side in the build message
    IvtReleaseImages();
    ivtBuilt = false;
    ivtBuiltForScene = sceneBufTex;

        // Super Resolution when the scale is on: render size in, screen size out. A8R8G8B8 maps to the back buffer's Vulkan format, so
    // both copies stay raw.
    const bool sr = rsActive && rsW && rsH && (rsW != w || rsH != h);
    const UINT rw = sr ? rsW : w, rh = sr ? rsH : h;
    if (IvtServerUpscaleAvailable())
    {
        // The native x64 service owns the helper, shared set and bulk GPU handoff.
        // Only the local ring above remains: the depth snapshot uses it before the
        // game's D3D9 guide passes. There is no 32-bit helper or shared-set import.
        ivtColorFmt = color; ivtOutputFmt = color;
        ivtW = rw; ivtH = rh; ivtOutW = w; ivtOutH = h;
        ivtBuilt = true; ivtNeedReset = true; ivtBuildFails = 0;
        LatWaiterStart();   // explicitly reports why the legacy timeline instrument cannot run on this service
        LogHealth("  bridge upscale: game guides ready for native x64 service, %ux%u -> %ux%u; backend awaits the helper's first build", rw, rh, w, h);
        return true;
    }
        // Linux sync path: the mod's semaphores exist before the first build; a bridge is drained before a rebuild.
    if (ivtSyncMode != 0 && !ivtSemsMade && !IvtMakeOurSemaphores()) return false;
    if (ivtBridge && !IvtRelayDrain(5000)) { IvtFail(ivtWatchFired == 2 ? ivtRelayWhy : "the Linux bridge did not drain before a rebuild"); return false; }
    if (ivtBridge && ivtWatchFired == 2)    { IvtFail(ivtRelayWhy); return false; }   // a drain that returned on a dead bridge
    FeedBuild b = {};
    b.width          = rw;
    b.height         = rh;
    b.target_width   = sr ? w : 0;
    b.target_height  = sr ? h : 0;
    b.color_fmt      = color;
    b.output_fmt     = color;             // same channel order as the backbuffer: the way home is a raw copy
        // The scene buffer is pre-tonemap, so an FP16 capture is HDR.
    b.hdr            = (scf == VK_FORMAT_R16G16B16A16_SFLOAT) ? 1u : 0u;
    b.depth_inverted = 1;                 // the depth guide is reverse-Z (near / Z)
    b.flags_override = ivtCfg.flags;   // -1 = the host's default; the NGX feature flags otherwise
    b.transport      = IVT_MODE == 1 ? 1 : 0;
    b.mv_scale_x     = 1.0f;
    b.mv_scale_y     = 1.0f;
    b.sem_in         = ivtSemHostIn;      // 0 on the native path
    b.sem_out        = ivtSemHostOut;
    // tex[] zero: the host creates the set for a Vulkan client.
    // target 0 (DLAA), panel_tex 0.

    BYTE tag = 'B';
    FeedBuildAck ack = {};
    {
                // A synchronous round trip on the render thread: bounded on the host side, but a multi-second stall at a size, format, scene
        // buffer or render-scale change, so the watchdog covers it.
        IvtWatch wBuild("build exchange", true);
                // 60 s: the host's first build creates the shared set and the upscaler (FSR compiles pipelines cold, NGX may wait 4 s).
        bool late = false;
        if (!IvtPipeWrite(&tag, 1) || !IvtPipeWrite(&b, sizeof(b)) || !IvtPipeReadWithin(&ack, sizeof(ack), 60000, &late))
        { IvtFail(late ? "the host did not answer the build within 60 s; its own lines in this log (\"host:\") say where it stopped" : "build exchange failed"); return false; }
    }
    for (int i = 0; i < FEED_SLOTS; i++) ivtTexHandle[i] = (HANDLE)(uintptr_t)ack.tex[i];
    if (!ack.ok)
    {
        LogRecord("  transport: host build FAILED (ngx 0x%08X, flags 0x%X)", ack.ngx_result, ack.flags);
                // AMD's FSR library crashed (FEED_ACK_FSR_RAISED): the host never calls it again. Most likely an FSR 4 this card cannot run
        // (its first dispatch is the build's warm-up), so the note suggests fsr_version=3 unless that is already set.
        if (ack.flags & FEED_ACK_FSR_RAISED)
        {
            // Below FEED_FSR4_MIN_SM a crashing FSR 4 needs a release Agility runtime. With SKIPPED set too, the crash was FSR 3.1's
            // (automatic chose it), which gets its own text.
            const unsigned sm = (ack.flags & FEED_ACK_SM_MASK) >> FEED_ACK_SM_SHIFT;
            const bool fsr3Already = (ack.flags & FEED_ACK_FSR4_SKIPPED) != 0;
            if (ivtCfg.fsr_version != 3 && sm && sm < FEED_FSR4_MIN_SM && !fsr3Already)
            {
                char why[640];
                _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "FSR crashed while its first frame was being set up, so FSR is off for this session. Direct3D 12 on this"
                    " PC offers shader model %u.%u, which is too old for FSR 4. TO KEEP FSR: put fsr_version=3 in DLSS-IV.cfg,"
                    " which runs FSR 3.1. FOR FSR 4: %s",
                    sm >> 4, sm & 0xFu, IvtAgilityAdvice((ack.flags & FEED_ACK_AGILITY_REFUSED) != 0));
                IvtFail(why);
                return false;
            }
            if (ivtCfg.fsr_version == 3)
                IvtFail("FSR crashed while its first frame was being set up, so FSR is off for this session."
                    " DLSS-IV.cfg already asks for FSR 3 (fsr_version=3), so this is not an FSR 4 problem: copy"
                    " " DLSSIV_HOST_DIR "\\ from the archive again, in case the library there was replaced."
                    " DLSS-IV.log names the code that faulted, which is what a bug report needs.");
            else if (fsr3Already)
                IvtFail("FSR crashed while its first frame was being set up, so FSR is off for this session."
                    " DLSS-IV was already running FSR 3.1 on this PC, because its Direct3D 12 is too old for FSR 4, so this"
                    " is not an FSR 4 problem: copy " DLSSIV_HOST_DIR "\\ from the archive again, in case the library there"
                    " was replaced. DLSS-IV.log names the code that faulted, which is what a bug"
                    " report needs.");
            else
                IvtFail("FSR crashed while its first frame was being set up, so FSR is off for this session. TO KEEP"
                    " FSR: put fsr_version=3 in DLSS-IV.cfg, which runs FSR 3.1 instead of FSR 4. If you replaced"
                    " " DLSSIV_HOST_DIR "\\amd_fidelityfx_upscaler_dx12.dll with a community FSR 4 build, that build does not"
                    " run on this machine: copy " DLSSIV_HOST_DIR "\\ from the archive again to put AMD's file back."
                    " DLSS-IV.log names the code that faulted.");
            return false;
        }
        IvtXessFell(ack.flags);   // automatic fell from XeSS to FSR at this build: said once
        if (++ivtBuildFails >= 3)
        {
            // Words, not a count: at log=0 this is the player's whole diagnosis (IvtBuildRefusedWhy, DLSS-IV.cpp).
            char why[384];   // 365 characters at most
            IvtBuildRefusedWhy(why, sizeof(why), ack.flags);
            IvtFail(why);
            return false;
        }
        ivtRetryAt = GetTickCount64() + 5000;
        return false;
    }
    ivtOutputFmt = ack.output_fmt ? (DXGI_FORMAT)ack.output_fmt : color;
    ivtColorFmt  = color;
    ivtW = rw; ivtH = rh; ivtOutW = w; ivtOutH = h;
    if (sr)
                // The back end the host built (the ack), not the one the cfg asked for.
        ((ack.flags & FEED_ACK_SR_ACTIVE) ? LogHealth : LogRecord)("  transport: render scale: %ux%u -> %ux%u, the host says %s upscaling %s%s",
            rw, rh, w, h, (ack.flags & FEED_ACK_BACKEND_XESS) ? "XeSS" : (ack.flags & FEED_ACK_BACKEND_FSR) ? "FSR" : "DLSS",
            (ack.flags & FEED_ACK_SR_ACTIVE) ? "ACTIVE" : ((ack.flags & FEED_ACK_SR_UNAVAILABLE) ? "UNAVAILABLE" : "not acknowledged"),
            // sr_quality is an NGX enum: only meaningful with SR active on DLSS.
            ((ack.flags & FEED_ACK_SR_ACTIVE) && !(ack.flags & (FEED_ACK_BACKEND_FSR | FEED_ACK_BACKEND_XESS))) ? " (a DLSS quality preset was chosen)" : "");

    if (ivtSyncMode != 0 && !ivtProbeDone)
    {
                // Linux sync path: the probe decides FAST or BRIDGE and tells the host.
        if (!IvtSyncProbe()) { IvtReleaseImages(); return false; }
        IvtHostWatchStart();
        LatWaiterStart();
    }
    if (ivtSyncMode == 0 && (!ivtSemIn || !ivtSemOut))
    {
                // Imported once a connection (only while ivtSemIn or ivtSemOut is null) and kept for the session; a rebuild leaks nothing.
        ivtFenceIn  = (HANDLE)(uintptr_t)ack.fence_in;
        ivtFenceOut = (HANDLE)(uintptr_t)ack.fence_out;
        ivtSemIn  = FeedVkImportFence(&ivtVk, ivtFenceIn);
        ivtSemOut = FeedVkImportFence(&ivtVk, ivtFenceOut);
        LogDetail("  transport: D3D12 fence -> Vulkan timeline semaphore import: in %s, out %s",
            ivtSemIn ? "OK" : "FAILED", ivtSemOut ? "OK" : "FAILED");
                // The set goes back on every failure arm: IvtFail is one-way, so no later release would free it.
        if (!ivtSemIn || !ivtSemOut) { IvtReleaseImages(); IvtFail("fence import failed"); return false; }
        IvtHostWatchStart();     // once a session, with the fence it watches
        LatWaiterStart();        // likewise: the latency waiter, on the same two timelines
    }

        // One entry per slot. Color and Output take the back buffer's format below; their UNKNOWN fails loudly if ever read.
    const DXGI_FORMAT kFmt[FEED_SLOTS] = { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN,
                                           DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT };
    static const char* kSlot[FEED_SLOTS] = { "Color", "Output", "Depth", "MV" };
    for (int i = 0; i < FEED_SLOTS; i++)
    {
        if (!ivtTexHandle[i])
        {
            LogRecord("  transport: the host returned no handle for slot %d (%s)", i, kSlot[i]);
            IvtReleaseImages();
            IvtFail("shared texture missing");
            return false;
        }
        const DXGI_FORMAT f = i == FEED_COLOR ? ivtColorFmt : i == FEED_OUTPUT ? ivtOutputFmt : kFmt[i];
        const VkFormat vkf = FeedVkFormat(f);
        // The Output is the one slot written through a UAV.
        const bool storage = (i == FEED_OUTPUT);
                // Output is the screen size; colour, depth and vectors the render size.
        const bool disp = (i == FEED_OUTPUT);
        const UINT iw = disp ? w : rw, ih = disp ? h : rh;
                // The platform's handle type, or the other once that has worked. No narrowing under Wine: its handle-properties query writes
        // handle-type constants where memory-type bits belong.
        if (!ivtImportType) ivtImportType = ivtWine ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        bool ok = vkf != VK_FORMAT_UNDEFINED &&
                  FeedVkImportImage(&ivtVk, ivtTexHandle[i], iw, ih, vkf, storage, ivtImportType,
                                    !ivtWine && ivtImportType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, &ivtImg[i], &ivtMem[i]);
        if (!ok && vkf != VK_FORMAT_UNDEFINED)
        {   // Say why, then try the other handle type once; a type that works is kept.
            IvtLogImportFailure(i, kSlot[i], iw, ih, f, vkf, storage, ivtImportType);
            const VkExternalMemoryHandleTypeFlagBits other = ivtImportType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
                ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
            ok = FeedVkImportImage(&ivtVk, ivtTexHandle[i], iw, ih, vkf, storage, other,
                                   !ivtWine && other == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT, &ivtImg[i], &ivtMem[i]);
            if (ok) { LogRecord("  transport: slot %d (%s) imported as %s; the remaining slots use it too", i, kSlot[i], IvtHandleTypeName(other)); ivtImportType = other; }
            else IvtLogImportFailure(i, kSlot[i], iw, ih, f, vkf, storage, other);
        }
        if (!ok)
        {
            LogRecord("  transport: texture import FAILED: slot %d (%s) %ux%u dxgi %d vk %d handle %p",
                i, kSlot[i], iw, ih, (int)f, (int)vkf, ivtTexHandle[i]);
            IvtReleaseImages();
            IvtFail("shared texture import failed");
            return false;
        }
        LogDetail("  transport: slot %d (%-6s) imported: %ux%u dxgi %d -> VkImage 0x%016llX%s",
            i, kSlot[i], iw, ih, (int)f, (unsigned long long)ivtImg[i],
            storage ? ", storage usage" : "");
    }

        // What the host built, not what the cfg asked (backend=-1 is decided in the host). Not behind telemetry: the first-injection
    // line reads it.
    ivtBackendIsFsr = (ack.flags & FEED_ACK_BACKEND_FSR) != 0;
        // 0 DLSS, 1 FSR, 2 XeSS. XeSS's flag comes with FEED_ACK_BACKEND_FSR clear.
    ivtBackendId = (ack.flags & FEED_ACK_BACKEND_XESS) ? 2 : ivtBackendIsFsr ? 1 : 0;
        // FSR 3.1 ran where FSR 4 was offered (shader model too low): told once a session, as every rebuild repeats the bit.
    if ((ack.flags & FEED_ACK_FSR4_SKIPPED) && !ivtFsr4SkipSaid)
    {
        ivtFsr4SkipSaid = true;
        const unsigned sm = (ack.flags & FEED_ACK_SM_MASK) >> FEED_ACK_SM_SHIFT;
        IvtWarn("FSR 3.1 is running, not FSR 4: Direct3D 12 on this PC offers shader model %u.%u, which is too old for FSR 4."
                " FOR FSR 4: %s", sm >> 4, sm & 0xFu, IvtAgilityAdvice((ack.flags & FEED_ACK_AGILITY_REFUSED) != 0));
        LogRecord("  transport: FSR 3.1 instead of FSR 4 (shader model %u.%u): said in the warnings file", sm >> 4, sm & 0xFu);
    }
    LogHealth("  transport: shared set ready: %ux%u -> %ux%u color dxgi %d output dxgi %d (%s), host ngx 0x%08X, %s (+%lu ms)",
        rw, rh, w, h, (int)ivtColorFmt, (int)ivtOutputFmt,
        ivtOutputFmt == ivtColorFmt ? "raw copy home" : "blit home (converts)",
        ack.ngx_result, IVT_MODE == 1 ? "transport test"
                      : ivtBackendId == 2 ? "XeSS upscaler" : ivtBackendIsFsr ? "FSR upscaler" : "DLSS feature",
        (unsigned long)CenMs());
        // These globals describe the last of the four imports; all four are imported alike.
    LogHealth("  transport: the last imported image took memory type %u, %s", (unsigned)feedVkImportTypeIndex,
        feedVkImportTypeHow == 1 ? "allowed by both the shared handle and the image"
      : feedVkImportTypeHow == 2 ? "the image's lowest (the handle's types and the image's share none)"
      : feedVkImportTypeHow == 3 ? "the image's lowest (the handle's types could not be asked)"
                                 : "the image's lowest (this driver has no vkGetMemoryWin32HandlePropertiesKHR)");
        // Frame generation's set (source/fg/fg_client.h), asked for after the main set. Its refusal leaves the build standing; false
    // only when the pipe is out of step and the transport was stopped.
    FGX(if (!FgxAfterBuild(w, h, (int)bbf, rw, rh)) { IvtReleaseImages(); return false; })
    ivtBuilt = true;
    ivtNeedReset = true;
    ivtBuildFails = 0;
    return true;
}

// ---- stage 1: the per-frame exchange without GPU work ----

// Signal ivtSemOut past anything waited on, so no queued batch stays blocked; timelines only move forward, so this is
// idempotent. IvtSignalOutTo keeps IvtFail and the host watch from signalling the same value twice.
static SRWLOCK ivtSignalLock  = SRWLOCK_INIT;
static UINT64  ivtSignalledTo = 0;                 // under ivtSignalLock
static VkResult IvtSignalOutTo(UINT64 top, bool* did)
{
    VkResult r = VK_SUCCESS;
    *did = false;
    AcquireSRWLockExclusive(&ivtSignalLock);
    if (top > ivtSignalledTo)
    {
        VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
        si.semaphore = ivtSemOut;
        si.value     = top;
        r = ivtSignalSemaphore(ivtDevice, &si);
        if (r == VK_SUCCESS) ivtSignalledTo = top;
        *did = true;
    }
    ReleaseSRWLockExclusive(&ivtSignalLock);
    return r;
}
static void IvtNoteSignalledTo(UINT64 v)
{
    AcquireSRWLockExclusive(&ivtSignalLock);
    if (v > ivtSignalledTo) ivtSignalledTo = v;
    ReleaseSRWLockExclusive(&ivtSignalLock);
}
static void IvtReleaseQueuedWait()
{
    if (!ivtSignalSemaphore || ivtSemOut == VK_NULL_HANDLE || !ivtDevice) return;
    const UINT64 top = (ivtSentN > ivtHomeN ? ivtSentN : ivtHomeN) + 1;
    bool did = false;
    const VkResult r = IvtSignalOutTo(top, &did);
    if (did)
        LogHealth("  transport: released the queued GPU wait, signalled semOut to %llu (%s)."
            " Without this a host that died uncleanly would freeze the game behind"
            " the mod's own submit", (unsigned long long)top,
            r == VK_SUCCESS ? "ok" : "FAILED");
}

// A frame announced to the helper but never submitted leaves it waiting on n, so n is signalled from the CPU. First the mod's
// own pending signal must land (a CPU signal past a pending GPU one is invalid): wait ivtSemIn to ivtSentN with a deadline,
// and signal nothing if it runs out.
static void IvtSignalInUnsubmitted(UINT64 n)
{
    if (!ivtSignalSemaphore || ivtSemIn == VK_NULL_HANDLE || !ivtDevice) return;
    const VkResult wr = ivtSentN ? FeedVkWaitTimelineR(&ivtVk, ivtSemIn, ivtSentN, 1000) : VK_SUCCESS;
    VkResult sr = VK_NOT_READY;
    if (wr == VK_SUCCESS)
    {
        VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
        si.semaphore = ivtSemIn;
        si.value     = n;
        sr = ivtSignalSemaphore(ivtDevice, &si);
    }
    LogRecord("  transport: frame %llu announced to the host and never submitted; the in-semaphore %s", (unsigned long long)n,
                  wr != VK_SUCCESS ? "did not reach the last submitted frame within 1 s: left alone"
                  : sr == VK_SUCCESS ? "signalled from the CPU" : "could NOT be signalled from the CPU");
}

// The host watch, the one deadline off the render thread: a stuck host never signals its fence, and the render thread then
// blocks inside DXVK (FlushRenderingCommands). Twice a second: if the host's fence is behind ivtWatchAwait and still for 4 s
// (unbiased time) while ivtSemIn is ahead of it, end the host, signal past what was sent, and set ivtWatchFired for IvtTick.
static DWORD WINAPI IvtHostWatch(void*)
{
    UINT64 seen = ~0ull;
    ULONGLONG since = 0;
    bool fired = false;
    while (WaitForSingleObject(ivtWatchStop, 500) == WAIT_TIMEOUT)
    {
        const UINT64 want = (UINT64)(ULONG)ivtWatchAwait;
        bool did = false;
        if (fired) { IvtSignalOutTo(want + 1, &did); FGX(FgxReleasePrepWait();) continue; }
        FGX(if (FgxWatchLook()) { FgxWatchFire(want); fired = true; continue; })   // frame generation's term: a prepare that has stood still for 4 s
        const UINT64 done = FeedVkTimelineValue(&ivtVk, ivtSemOut);
        if (done >= want) { seen = ~0ull; continue; }                 // the host has answered everything sent
        const UINT64 mine = FeedVkTimelineValue(&ivtVk, ivtSemIn);    // the last frame whose input retired on the GPU; 0 = unreadable
        if (mine && mine <= done) { seen = ~0ull; continue; }         // nothing unanswered: the late work is the game's
        ULONGLONG now = 0;
        QueryUnbiasedInterruptTime(&now);                             // 100 ns units
        if (done != seen) { seen = done; since = now; continue; }     // behind, but moving
        if (now - since < 40000000ull) continue;                      // 4 s
                // End the host before signalling: the kernel will not move a fence shared with a suspended process.
        bool ended = false;
        if (ivtWatchHost)
        {
            ended = TerminateProcess(ivtWatchHost, 0) != 0;
            WaitForSingleObject(ivtWatchHost, 2000);      // its GPU contexts go with it
        }
        const VkResult r = IvtSignalOutTo(want + 1, &did);
        FGX(FgxReleasePrepWait();)                           // frame generation's queued wait hangs on the same helper
        fired = true;
        _InterlockedCompareExchange(&ivtWatchFired, 1, 0);   // 2 is the relay's stop; first publisher wins
                // Read back: "ok" is the call's answer, not the fence's.
        LogRecord("  transport: THE HOST HAS NOT FINISHED A FRAME FOR 4 s (frame %llu sent, input of %llu retired, %llu done there): ended %s, GPU wait released (%s; fence %llu)",
            (unsigned long long)want, (unsigned long long)mine, (unsigned long long)done, ended ? "ok" : "NOT (TerminateProcess failed, or no handle)",
            !did ? "already released" : r == VK_SUCCESS ? "ok" : "FAILED",
            (unsigned long long)FeedVkTimelineValue(&ivtVk, ivtSemOut));
    }
    return 0;
}
static void IvtHostWatchStart()
{
    if (ivtWatchStop || !ivtSignalSemaphore || !FeedVkHasTimelineQueries(&ivtVk)) return;
    ivtWatchStop = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!ivtWatchStop) return;
    if (ivtHost) DuplicateHandle(GetCurrentProcess(), ivtHost, GetCurrentProcess(), &ivtWatchHost, 0, FALSE, DUPLICATE_SAME_ACCESS);
    // 64 KB of stack, reserved not committed: this process counts its address space.
    const HANDLE t = CreateThread(nullptr, 64 * 1024, IvtHostWatch, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) CloseHandle(t);
    else LogRecord("  transport: the host's watch thread could not start (error %lu): a stuck host would hold the game", GetLastError());
}
// No join: IvtFail can run where waiting on a thread is unsafe.
static void IvtHostWatchStop() { if (ivtWatchStop) SetEvent(ivtWatchStop); }

// ---- the latency waiter (render-thread half: transport/ivt_latency.h) ----
// Waits on the two timelines each frame already signals (ivtSemIn: injection retired; ivtSemOut: helper answered) to measure
// picture age and per-frame period, turnaround, queue depth and lead, as medians over 120-frame windows. A timeout ends a window.
// Stopped in IvtFail and IvtShutdown and joined for up to 600 ms.
static HANDLE latStop = nullptr, latThread = nullptr;
// A plain flag rather than polling latStop (a wineserver round trip each under Wine); set before SetEvent.
static volatile LONG latStopReq = 0;
// The true end of a frame: after each Present an empty submit signals this timeline with the frame's number, on the presenting
// thread, behind LockSubmissionQueue (not FlushRenderingCommands, which would wait for DXVK's CS thread). Pacer on only.
static VkSemaphore   latSemEnd     = VK_NULL_HANDLE;
// latEndOn lives in latFlags (ivt_latency.h), away from per-frame writes.
static volatile LONG latEndLast    = 0;
DEVSTAT(static volatile LONGLONG latMarkTicks = 0, latMarkLockTicks = 0, latMarkWorst = 0; static volatile LONG latMarkN = 0;)   // the marker's cost, development builds
static void LatEndSignal(LONG f)
{
    if (!latEndOn || !ivtIDev || !ivtQueueSubmit || ivtQueue == VK_NULL_HANDLE) return;
    if ((LONG)(f - latEndLast) <= 0) return;                 // a timeline only moves up: the same frame twice, or a stale ordinal
    DT_DIRECT();                                             // a direct DXVK call: a no-op on the two threads that reach here
    UINT64 v = (UINT64)(ULONG)f;
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.signalSemaphoreValueCount = 1; tl.pSignalSemaphoreValues = &v;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &latSemEnd;
    DEVSTAT(LARGE_INTEGER m0; QueryPerformanceCounter(&m0);)
    ivtIDev->LockSubmissionQueue();
    DEVSTAT(LARGE_INTEGER m1; QueryPerformanceCounter(&m1);)
    const VkResult r = ivtQueueSubmit(ivtQueue, 1, &si, VK_NULL_HANDLE);
    ivtIDev->ReleaseSubmissionQueue();
    DEVSTAT(LARGE_INTEGER m2; QueryPerformanceCounter(&m2); const LONGLONG all = m2.QuadPart - m0.QuadPart; InterlockedExchangeAdd64(&latMarkTicks, all); InterlockedExchangeAdd64(&latMarkLockTicks, m1.QuadPart - m0.QuadPart); _InterlockedIncrement(&latMarkN); LONGLONG w; do { w = InterlockedCompareExchange64(&latMarkWorst, 0, 0); } while (all > w && InterlockedCompareExchange64(&latMarkWorst, all, w) != w);)
    if (r == VK_SUCCESS) latEndLast = f;
}
static DtPresentFn latPrevPresentObs = nullptr;
static void LatPresentObs(LONG present)                       // the worker, for the Present it is about to complete
{
    LatEndSignal(latPresFrame[present & (LAT_RING - 1)]);
    if (latPrevPresentObs) latPrevPresentObs(present);
}
static LONG   latWindows = 0, latStamps = 0, latLate = 0, latResyncs = 0, latQMax = 0;
static bool   latSaidOnce = false, latStarted = false;
enum { LAT_MAXWIN = 4096 };
static float  latMedAge[LAT_MAXWIN], latMedP[LAT_MAXWIN], latMedQ[LAT_MAXWIN], latMedH[LAT_MAXWIN], latMedL[LAT_MAXWIN], latMedB[LAT_MAXWIN];
static const char* latWhyNot = "the transport never built, so nothing was stamped";
static float LatMedian(float* v, int n) { if (n <= 0) return 0.0f; std::nth_element(v, v + n / 2, v + n); return v[n / 2]; }
static double LatMs(LONGLONG ticks)  { return ivtQpcFreq ? (double)ticks * 1000.0 / (double)ivtQpcFreq : 0.0; }
// The present block's source: the swapchain hook's sum while the device thread is not recording, the worker's after.
struct LatBlockMark_t { LONGLONG hookT; LONG hookN; LONGLONG workT; LONG workN; };
static void LatBlockMark(LatBlockMark_t* m)
{
    m->hookT = _InterlockedCompareExchange64(&latBlockTicks, 0, 0);  m->hookN = latBlockN;
    m->workT = _InterlockedCompareExchange64(&dtPresTicksSum, 0, 0); m->workN = (LONG)dtPresDone;
}
static double LatBlockMs(const LatBlockMark_t& m)
{
    const LONG nH = latBlockN - m.hookN, nW = (LONG)dtPresDone - m.workN;
    if (nH <= 0 && nW <= 0) return 0.0;
    if (nH >= nW) return LatMs(_InterlockedCompareExchange64(&latBlockTicks, 0, 0) - m.hookT) / (double)nH;
    return LatMs(_InterlockedCompareExchange64(&dtPresTicksSum, 0, 0) - m.workT) / (double)nW;
}
// THE ADAPTIVE LADDER, MODE 4: mode 1, plus as much of mode 3 as measures within its line of frame rate: 3 percent for the
// same-frame upscale, AdPaceLine for the pacer.
// Rungs: 0 the queue cap; 1 plus the same-frame upscale; 2 plus the pacer. Rungs are compared in ABBA halves by the median pair
// ratio; the pacer is kept only if it wins a quarter frame of picture age, and audited every 90 windows. A kept rung drops only
// one point over its line (hysteresis).
enum { AD_FORCED = 0, AD_MEASURE_SF = 1, AD_MEASURE_PACE = 2, AD_STAY0 = 3, AD_STAY1 = 4, AD_STAY2 = 5, AD_AUDIT = 6 };
enum { AD_HALF = 3, AD_PAIRS_MIN = 3, AD_PAIRS_MAX = 5, AD_AUDIT_EVERY = 90, AD_AUDIT_HOLD = 4, AD_LAST = 3 };
static int   adState = AD_STAY0, adCount = 0, adFlips = 0, adRung = 0, adMeasured = 0, adPairs = AD_PAIRS_MIN, adOut = 0, adAudits = 0;
static bool  adInit = false, adFirst = true;
static float adSumA[AD_PAIRS_MAX], adSumB[AD_PAIRS_MAX], adAgeA[AD_PAIRS_MAX], adAgeB[AD_PAIRS_MAX], adQA[AD_PAIRS_MAX], adQB[AD_PAIRS_MAX];
static bool  adKeptSf = false, adKeptPace = false;   // dropped only one point over its line
static float adBase = 0.0f, adCostSf = 0.0f, adCostPace = 0.0f, adWinSf = 0.0f, adWinPace = 0.0f, adQ1 = 0.0f;
static float adLastP[AD_LAST], adLastAge[AD_LAST]; static int adLastN = 0;                    // the last windows, any state: the audit's reference
static int   adAudSaved = 0, adAudN = 0; static float adAudRefP = 0.0f, adAudRefAge = 0.0f, adAudP = 0.0f, adAudAge = 0.0f, adAudQ = 0.0f;
static const char* AdName(int s) { return s == AD_FORCED ? "same frame (a limiter or vsync: no cost possible)" : s == AD_MEASURE_SF ? "measuring the same frame" : s == AD_MEASURE_PACE ? "measuring the pacer" : s == AD_STAY0 ? "queue only, holding" : s == AD_STAY1 ? "same frame, no pacer" : s == AD_STAY2 ? "same frame and pacer" : "same frame and pacer, auditing the pacer"; }
static const char* AdRungName(int r) { return r == 0 ? "queue only" : r == 1 ? "same frame" : "same frame and pacer"; }
static float AdMedianN(const float* v, int n) { float c[AD_PAIRS_MAX]; if (n <= 0) return 0.0f; if (n > AD_PAIRS_MAX) n = AD_PAIRS_MAX; for (int i = 0; i < n; i++) { c[i] = v[i]; for (int j = i; j > 0 && c[j] < c[j - 1]; j--) { const float x = c[j]; c[j] = c[j - 1]; c[j - 1] = x; } } return c[n / 2]; }
static void  AdPairsTxt(char* buf, size_t n, const float* r, int np) { size_t at = 0; buf[0] = 0; for (int i = 0; i < np && at + 1 < n; i++) { const int k = _snprintf_s(buf + at, n - at, _TRUNCATE, "%s%+.1f", i ? " " : "", (r[i] - 1.0f) * 100.0f); if (k < 0) break; at += (size_t)k; } }
static void AdRung(int rung, const char* why)   // 0 pipelined + pacer held, 1 same frame + pacer held, 2 same frame + pacer
{
    if (rung == adRung) return;
    adRung = rung; adFlips++;
    _InterlockedExchange(&latAsyncWant, rung == 0 ? 1 : 0);
    LatPaceHoldSet(rung < 2);
    LogHealth("  latency: adaptive ladder -> rung %d (%s): %s", rung, AdRungName(rung), why);   // log=2, development builds
}
static void AdMeasure(int state, const char* why)
{
    adState = state; adCount = 0; adOut = 0; adPairs = AD_PAIRS_MIN; adFirst = false; adHeld = false; adHeldUp = 0;
    for (int i = 0; i < AD_PAIRS_MAX; i++) { adSumA[i] = 0; adSumB[i] = 0; adAgeA[i] = 0; adAgeB[i] = 0; adQA[i] = 0; adQB[i] = 0; }
    LogRecord("  latency: ladder measures %s: %s", state == AD_MEASURE_SF ? "the same frame against the queue" : "the pacer on the same frame", why);
    AdRung(state == AD_MEASURE_SF ? 0 : 1, "the A half of the first pair");
}
static bool AdOutOfBand(float mP, bool lighterOnly)   // three consecutive windows outside the rung's band re-measure
{
    const bool out = adBase > 0.0f && (mP < adBase * 0.75f || (!lighterOnly && mP > adBase * 1.33f));
    adOut = out ? adOut + 1 : 0;
    return adOut >= 3 && adCount >= 5;
}
static void AdAuditStart()
{
    if (adLastN < AD_LAST) return;
    adAudSaved = adCount; adAudRefP = 0; adAudRefAge = 0;
    for (int i = 0; i < AD_LAST; i++) { adAudRefP += adLastP[i]; adAudRefAge += adLastAge[i]; }
    adAudRefP /= (float)AD_LAST; adAudRefAge /= (float)AD_LAST;
    adAudP = 0; adAudAge = 0; adAudQ = 0; adAudN = 0; adState = AD_AUDIT; adCount = 0; adAudits++;
    AdRung(1, "the audit holds the pacer for four windows");
}
// The pacer may cost 3 percent plus 3 per frame of picture age it wins, at most 9.
static float AdPaceLine(float wins) { const float l = 3.0f + 3.0f * (wins > 0.0f ? wins : 0.0f); return l > 9.0f ? 9.0f : l; }
static void LatAdaptiveStep(float mP, float mQ, float mAge)   // one window of 120 frames: its median period, in flight, picture age
{
    if (latLimiter || latVsync)
    {
        if (adState != AD_FORCED) { adState = AD_FORCED; adCount = 0; adKeptSf = false; adKeptPace = false; AdRung(1, "a frame limiter or vsync holds the frame rate, so the same frame costs nothing (the pacer stands down on its own rule)"); }
        return;
    }
    adLastP[adLastN % AD_LAST] = mP; adLastAge[adLastN % AD_LAST] = mAge; adLastN++;
    adCount++;
    switch (adState)
    {
    case AD_FORCED: AdMeasure(AD_MEASURE_SF, "the limiter is gone"); break;
    case AD_MEASURE_SF:
    case AD_MEASURE_PACE:
    {
        const int w = adCount - 1, half = w / AD_HALF, pair = half / 2, second = half & 1, inHalf = w % AD_HALF;
        const int isB = second ^ (pair & 1);              // ABBA: a drift in the scene cannot line up with the pairs
        const int low = adState == AD_MEASURE_SF ? 0 : 1;
        if (pair >= AD_PAIRS_MAX) { adState = AD_STAY1; adCount = 0; AdRung(1, "the measurement overran (a fault): the same frame without the pacer"); break; }
                // Runs at a window's end: the first window of a half is transitional and not counted, and the next half's flip is made here.
        if (inHalf != 0) { if (isB) { adSumB[pair] += mP; adAgeB[pair] += mAge; adQB[pair] += mQ; } else { adSumA[pair] += mP; adAgeA[pair] += mAge; adQA[pair] += mQ; } }
        if (inHalf == AD_HALF - 1) { const int nh = half + 1, nB = (nh & 1) ^ ((nh / 2) & 1); AdRung(nB ? low + 1 : low, nB ? "the B half" : "the A half"); }
        const int done = (second && inHalf == AD_HALF - 1) ? pair + 1 : 0;   // pairs complete after this window
        if (!done || done < adPairs) break;
        float r[AD_PAIRS_MAX], win[AD_PAIRS_MAX], qa[AD_PAIRS_MAX], qb[AD_PAIRS_MAX]; float base = 0.0f;
        for (int i = 0; i < done; i++)
        {
            r[i]   = adSumA[i] > 0.0f ? adSumB[i] / adSumA[i] : 1.0f;
            win[i] = adSumA[i] > 0.0f ? (adAgeA[i] - adAgeB[i]) / adSumA[i] : 0.0f;    // frames of picture age won by the upper rung
            qa[i]  = adQA[i] / (float)(AD_HALF - 1);
            qb[i]  = adQB[i] / (float)(AD_HALF - 1);
            base  += adSumA[i];
        }
        base /= (float)(done * (AD_HALF - 1));                                      // the lower rung's period
        const float cost = (AdMedianN(r, done) - 1.0f) * 100.0f;
        const float wins = AdMedianN(win, done);
        const bool  sf   = adState == AD_MEASURE_SF;
        const float total = sf ? cost : ((1.0f + adCostSf / 100.0f) * (1.0f + cost / 100.0f) - 1.0f) * 100.0f;
        const float lim  = (sf ? 3.0f : AdPaceLine(wins)) + ((sf ? adKeptSf : adKeptPace) ? 1.0f : 0.0f);
        if (done == AD_PAIRS_MIN && adPairs == AD_PAIRS_MIN && total > lim - 2.0f && total < lim + 2.0f)   // within two points of its line
        {
            adPairs = AD_PAIRS_MAX;
            LogRecord("  latency: ladder: %s %.1f percent after three pairs, near the %.1f percent line: two more pairs", sf ? "the same frame" : "the pacer", total, lim);
            break;
        }
        char pairs[64]; AdPairsTxt(pairs, sizeof(pairs), r, done);
        if (adState == AD_MEASURE_SF)
        {
            adCostSf = cost; adWinSf = wins; adMeasured |= 1;
            float qA = 0.0f, qB = 0.0f; for (int i = 0; i < done; i++) { qA += qa[i]; qB += qb[i]; } qA /= (float)done; qB /= (float)done;   // the MEAN frames in flight on either rung
            adQ1 = qA > qB ? qA : qB;                                                                    // a quarter of a frame queued on average is wait the pacer could convert
            if (cost <= lim + 0.05f && adQ1 >= 0.25f && latPacerOn)   // pacer=0 never opens the pacer stage
            {
                LogRecord("  latency: ladder: the same frame costs %.1f percent (pairs %s, line %.0f), wins %.2f frames: kept; queue in use (in flight %.1f, %.1f same frame): the pacer next", cost, pairs, lim, wins, qA, qB);
                AdMeasure(AD_MEASURE_PACE, "the same frame fits and the queue is in use on it");
            }
            else if (cost <= lim + 0.05f)
            {
                adState = AD_STAY1; adCount = 0; adOut = 0; adBase = base * (1.0f + cost / 100.0f);   // already on rung 1
                AdVerdictKept("  latency: ladder: the same frame costs %.1f percent (pairs %s, line %.0f), wins %.2f frames: kept; %s (in flight %.1f, %.1f same frame): no pacer for 300 windows", cost, pairs, lim, wins,
                          latPacerOn ? "queue empty" : "pacer=0", qA, qB);
            }
            else
            {
                adState = AD_STAY0; adCount = 0; adOut = 0; adBase = base;
                LogRecord("  latency: ladder: the same frame costs %.1f percent (pairs %s), over the %.0f percent line (would win %.2f frames): the queue alone for a minute", cost, pairs, lim, wins);
                AdRung(0, "the same frame costs too much here");
            }
        }
        else
        {
            adCostPace = cost; adWinPace = wins; adMeasured |= 2;
            if (total <= lim + 0.05f && wins >= 0.25f)
            {
                adState = AD_STAY2; adCount = 0; adOut = 0; adBase = base * (1.0f + cost / 100.0f);   // already on rung 2
                LogRecord("  latency: ladder: the pacer costs %.1f percent more (pairs %s), %.1f in all (line %.1f), wins %.2f frames: kept for 300 windows, audited every 90", cost, pairs, total, lim, wins);
            }
            else
            {
                adState = AD_STAY1; adCount = 0; adOut = 0; adBase = base;
                LogRecord("  latency: ladder: the pacer costs %.1f percent more (pairs %s), %.1f in all (line %.1f), wins %.2f frames: %s; no pacer for 300 windows", cost, pairs, total, lim, wins,
                          total > lim + 0.05f ? "over the line" : "under a quarter of a frame");
                AdRung(1, total > lim + 0.05f ? "the pacer costs too much on top" : "the pacer wins too little here");
            }
        }
        break;
    }
    case AD_STAY0:
        adKeptSf = false; adKeptPace = false;
        if (adCount >= 60) AdMeasure(AD_MEASURE_SF, adFirst ? "the first measurement, five live windows in" : "a minute on the queue alone is up");
        else if (AdOutOfBand(mP, true)) AdMeasure(AD_MEASURE_SF, "the scene got lighter for three windows");
        break;
    case AD_STAY1:
    case AD_STAY2:
        adKeptSf = true; adKeptPace = adState == AD_STAY2;
        if (adCount >= 300) AdMeasure(AD_MEASURE_SF, "the 300-window re-check");
        else if (AdOutOfBandHeld(mP, false)) AdMeasure(AD_MEASURE_SF, mP < adBase ? "the scene got lighter for three windows" : "the scene got heavier for three windows");
        else if (adState == AD_STAY2 && (adCount % AD_AUDIT_EVERY) == 0) AdAuditStart();
        break;
    case AD_AUDIT:
        if (adCount > 1) { adAudP += mP; adAudAge += mAge; adAudQ += mQ; adAudN++; }   // the first held window settles
        if (adCount >= AD_AUDIT_HOLD && adAudN)
        {
            const float holdP = adAudP / (float)adAudN, holdAge = adAudAge / (float)adAudN, holdQ = adAudQ / (float)adAudN;
            const float ratio = holdP > 0.0f ? adAudRefP / holdP : 1.0f;                   // the pacer's period over the period without it
            const float total = ((1.0f + adCostSf / 100.0f) * ratio - 1.0f) * 100.0f;
            const float win   = holdP > 0.0f ? (holdAge - adAudRefAge) / holdP : 0.0f;
            adCostPace = (ratio - 1.0f) * 100.0f; adWinPace = win;
            const float line = AdPaceLine(win) + 1.0f;   // plus one point of hysteresis
            const char* drop = total > line + 0.05f ? "over the line now" : win < 0.25f ? "wins less than a quarter of a frame now" : holdQ < 0.25f ? "less than a quarter of a frame in flight without it now, nothing left to convert" : nullptr;
            LogRecord("  latency: ladder audit %d: the pacer costs %.1f percent (%.1f in all, line %.1f), wins %.2f frames, in flight without it %.1f: %s", adAudits, adCostPace, total, line, win, holdQ, drop ? drop : "kept");
            if (drop) { adState = AD_STAY1; adCount = 0; adOut = 0; adBase = holdP; }      // already on rung 1
            else      { adState = AD_STAY2; adCount = adAudSaved; adOut = 0; AdRung(2, "the audit passed"); }
        }
        break;
    }
}
// The loop is defined after DllMain in DLSS-IV.cpp: code here would move the release's hot data. tools/test-ladder's cut ends
// at this line.
static void LatWaiterLoop();
static DWORD WINAPI LatWaiter(void*)
{
    LatWaiterLoop();
        // However the loop ended, the pacer stands down (only this thread arms it) and the ladder returns to rung 0.
    _InterlockedExchange(&latPaceArmed, 0);
    latEndOn = false;
    if (IvtAdaptiveOn()) { _InterlockedExchange(&latPaceHold, 1); _InterlockedExchange(&latAsyncWant, 1); }
    return 0;
}
static int LatSummaryText(char* out, size_t cap, bool final)
{
    if (!latOn) return 0;
    if (!latStarted)   return _snprintf_s(out, cap, _TRUNCATE, "  latency%s: NOT MEASURED: %s", final ? " (session end)" : "", latWhyNot);
    if (!latWindows)   return _snprintf_s(out, cap, _TRUNCATE, "  latency%s: NOT MEASURED: no window of %d consecutive frames completed (%ld stamps, %ld resyncs)", final ? " (session end)" : "", LAT_WIN, latStamps, latResyncs);
    const int n = latWindows < LAT_MAXWIN ? latWindows : LAT_MAXWIN;
        // Static, not on the waiter's small stack. The lock covers the window-30 line against the session line at shutdown.
    static SRWLOCK scratchLock = SRWLOCK_INIT;
    static float a[LAT_MAXWIN], pp[LAT_MAXWIN], q[LAT_MAXWIN], h[LAT_MAXWIN], l[LAT_MAXWIN], b[LAT_MAXWIN];
        // In DLL_PROCESS_DETACH a killed waiter's lock is never released, so it is tried, not taken.
    if (final) { if (!TryAcquireSRWLockExclusive(&scratchLock)) return _snprintf_s(out, cap, _TRUNCATE, "  latency (session end): the medians were being written when the game exited; the window-30 line above stands"); }
    else AcquireSRWLockExclusive(&scratchLock);
    memcpy(a, latMedAge, n * sizeof(float)); memcpy(pp, latMedP, n * sizeof(float)); memcpy(q, latMedQ, n * sizeof(float));
    memcpy(h, latMedH, n * sizeof(float)); memcpy(l, latMedL, n * sizeof(float)); memcpy(b, latMedB, n * sizeof(float));
    const float mAge = LatMedian(a, n), mP = LatMedian(pp, n), mQ = LatMedian(q, n), mH = LatMedian(h, n), mL = LatMedian(l, n), mB = LatMedian(b, n);
    ReleaseSRWLockExclusive(&scratchLock);
    char endTxt[96] = "", adTxt[200] = "";
    if (IvtAdaptiveOn())
    {
        char meas[128] = "";
        if (adMeasured & 1) _snprintf_s(meas, sizeof(meas), _TRUNCATE, "; measured: same frame %+.1f%% for %.2f fr", adCostSf, adWinSf);
        if (adMeasured & 2) { const size_t used = strlen(meas); _snprintf_s(meas + used, sizeof(meas) - used, _TRUNCATE, ", pacer %+.1f%% for %.2f fr", adCostPace, adWinPace); }
        if (adAudits) { const size_t used = strlen(meas); _snprintf_s(meas + used, sizeof(meas) - used, _TRUNCATE, ", %d audit(s)", adAudits); }
        _snprintf_s(adTxt, sizeof(adTxt), _TRUNCATE, "adaptive (%s now, %d flips%s)", AdName(adState), adFlips, meas);
    }
    if (latEndOn) _snprintf_s(endTxt, sizeof(endTxt), _TRUNCATE, " to the frame end");
    char lateTxt[160] = "";
    DEVSTAT(if (latLate * 10 > latStamps) strcat_s(lateTxt, " (MORE THAN A TENTH LATE: the figures are upper bounds)");)
    if (latWindows > LAT_MAXWIN) strcat_s(lateTxt, "; medians of the last 4096 windows");
    return _snprintf_s(out, cap, _TRUNCATE, "  latency%s: picture age median %.1f ms = %.2f frames%s at %.0f fps (GPU period %.2f ms); in flight %.1f (max %ld), helper %.2f ms,"
              " device-thread lead %.1f, present block %.2f ms; responsiveness=%d, queue: %s, pacer %s (slept %.2f ms a present on %ld%% of %ld), same frame %s;"
              " %ld windows of %d frames, %ld stamps" DEVSTAT(", late %ld") "%s, resyncs %ld",
        final ? " (session end)" : "", mAge, mP > 0.0f ? mAge / mP : 0.0f, endTxt, mP > 0.0f ? 1000.0f / mP : 0.0f, mP, mQ, latQMax, mH, mL, mB,
        ivtCfg.responsiveness, latQueueNote[0] ? latQueueNote : "not touched",
        !latPacerOn ? "off" : latVsync ? "stood down for vsync" : latLimiter ? "stood down for a frame limiter" : InterlockedCompareExchange(&latPaceHold, 0, 0) ? "held by the adaptive step" : "on",
        latPaceFrames ? LatMs(_InterlockedCompareExchange64(&latPaceSleptTicks, 0, 0)) / (double)latPaceFrames : 0.0,
        latPaceFrames ? (long)(100 * latPaceSleptN / latPaceFrames) : 0L, latPaceFrames,
        IvtAdaptiveOn() ? adTxt : IvtAsyncHome() ? "off (pipelined)" : "on",
        latWindows, LAT_WIN, latStamps, DEVSTAT(latLate,) lateTxt, latResyncs);
}
static void LatSummaryLine(bool final)   // the waiter's window-30 line; at session end LogSessionOver writes the same text itself
{
    char buf[2048];
    if (LatSummaryText(buf, sizeof(buf), final) > 0) LogRecord("%s", buf);
}
static void LatWaiterStart()
{
    if (IvtServerUpscaleAvailable())
    {
        // The service owns its timelines. The legacy game's waiter must not
        // query null semaphores or claim to pace/measure a native service frame.
        latPacerOn = false;
        latWhyNot = "the native x64 bridge owns frame synchronization; the legacy local timeline instrument is unavailable";
        static bool said = false;
        if (!said)
        {
            said = true;
            LogRecord("  bridge upscale: same-frame service synchronization; local adaptive/pacer measurements unavailable, configured queue cap retained");
        }
        return;
    }
    if (!latOn || latThread) return;
    if (!FeedVkHasTimelineQueries(&ivtVk) || ivtSemIn == VK_NULL_HANDLE || ivtSemOut == VK_NULL_HANDLE) { latWhyNot = "this Vulkan has no timeline queries, or the semaphores do not exist"; return; }
    latStop = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (!latStop) { latWhyNot = "the stop event could not be created"; return; }
    latStopReq = 0;                                            // cleared before the thread exists
        // Where the ladder cannot run, responsiveness=4 is the queue cap alone; pacer=1 still forces the pacer.
    const bool heldForGood = !IvtAdaptiveOn() && ivtCfg.responsiveness >= 4 && ivtCfg.pacer < 0;
        // Nothing would read the waiter: stop before the thread exists. Vsync keeps it, since a Reset can turn vsync off.
    if (!telemetryOn && !IvtAdaptiveOn() && (!latPacerOn || heldForGood || latLimiter))
    {
        latWhyNot = "nothing reads it at log=0 (no adaptive step, and the pacer off, held for good or stood down for a limiter)";
        latOn = false; latPacerOn = false;
        CloseHandle(latStop); latStop = nullptr;
        return;
    }
    if (IvtAdaptiveOn() && !adInit) { adInit = true; adState = AD_STAY0; adCount = 55; adRung = 0; _InterlockedExchange(&latAsyncWant, 1); _InterlockedExchange(&latPaceHold, 1); }   // mode 4: rung 0, first measurement after five live windows
    else if (!IvtAdaptiveOn()) _InterlockedExchange(&latPaceHold, heldForGood ? 1 : 0);
    if (heldForGood) LogRecord("  latency: no adaptive step (%s): queue cap alone", ivtBridge ? "the Linux bridge path" : "the file names async_home");
    latThread = CreateThread(nullptr, 256 * 1024, LatWaiter, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);   // 256 KB: it formats long lines and sorts windows
    if (!latThread) { latWhyNot = "the waiter thread could not be started"; LogRecord("  latency: the waiter thread could not be started (error %lu)", GetLastError()); return; }
        // Frame generation needs the marker under a cap too.
    if (latPacerOn && (!latLimiter FGX(|| FgxMarkerWanted())) && !heldForGood)   // not gated on vsync: a Reset can turn it off later
    {
        HANDLE hEnd = nullptr;
        if (FeedVkCreateTimeline(&ivtVk, false, &latSemEnd, &hEnd) >= 0 && latSemEnd != VK_NULL_HANDLE)
        {
            latEndOn = true;
            if (dtInstalled) { latPrevPresentObs = dtPresentObs; dtPresentObs = LatPresentObs; }   // the worker signals the end of each Present it replays
            LogHealth("  latency: the frame-end marker is on (%s)", dtInstalled ? "the worker's present observer" : "the swapchain hook");
        }
        else LogRecord("  latency: no frame-end timeline: the injection's retire is the frame end");
    }
    latStarted = true;
    LogHealth("  latency: waiter started on the two timelines (async_home %d in effect, %s, pacer %s)", IvtAsyncHome() ? 1 : 0, ivtBridge ? "bridge" : "GPU-signalled", latPacerOn ? "on" : "off");
}
static void LatWaiterStop()
{
    if (dtPresentObs == LatPresentObs) dtPresentObs = latPrevPresentObs;   // the worker stops signalling before the timeline goes
    latEndOn = false;
    latStopReq = 1;                                            // what the waiter tests; set before the event
    if (latStop) SetEvent(latStop);
    if (latThread) { WaitForSingleObject(latThread, 600); CloseHandle(latThread); latThread = nullptr; }
    _InterlockedExchange(&latPaceArmed, 0);                                    // the second guard: the waiter's own exit is the first
}


// ivtSignalSemaphore stays for IvtReleaseQueuedWait, so a dying host cannot hang the game behind the mod's submit (no
// FlushRenderingCommands needed). Stage 2 and mode=1 stay as bisection steps.

// ---- stage 3: the guides ----

// The render-size colour target, registered in the scaled table so the game's viewports on it are scaled.
static bool IvtRsEnsureColor(IDirect3DDevice9* dev)
{
    if (ivtRsColorSurf) return true;
        // The next line latches, so wait for a known back buffer rather than bake in a wrong format.
    if (ivtRsColorTried || !dev || !ivtW || !ivtH || !ivtOutW || !backbufferSurf) return false;
    ivtRsColorTried = true;
        // The back buffer's format: under hdr=1 it is FP16, and 8 bits would band the composite.
    D3DFORMAT rsFmt = D3DFMT_A8R8G8B8;
    { D3DSURFACE_DESC bd = {}; if (SUCCEEDED(backbufferSurf->GetDesc(&bd))) rsFmt = bd.Format; }
        // The guard, not a bare counter: it knows not to be taken on the worker (RsOwnCall, DLSS-IV.cpp).
    HRESULT hr;
    { RsOwnCreateCall rsOwnCreateGuard;
      hr = dev->CreateTexture(ivtW, ivtH, 1, D3DUSAGE_RENDERTARGET, rsFmt, D3DPOOL_DEFAULT, &ivtRsColorTex, nullptr); }
    if (FAILED(hr) || !ivtRsColorTex)
    { LogRecord("  render scale: the %ux%u colour target was refused (0x%08X): no redirect, no resolve", ivtW, ivtH, (unsigned)hr); return false; }
    ivtRsColorTex->GetSurfaceLevel(0, &ivtRsColorSurf);
    if (!ivtRsColorSurf)
    {
        LogRecord("  render scale: the %ux%u colour target's surface was refused: no redirect, no resolve", ivtW, ivtH);
        ivtRsColorTex->Release(); ivtRsColorTex = nullptr;
        return false;
    }
    rsRedirectSurf = ivtRsColorSurf;
    RsRegisterOwn(ivtRsColorTex, ivtRsColorSurf, ivtOutW, ivtOutH, ivtW, ivtH);
    LogDetail("  transport: render scale: colour target %ux%u d3dfmt %u ready; the composite, the depth quad and the coronas draw into it",
        ivtW, ivtH, (unsigned)rsFmt);
    return true;
}

// From the composite draw. DXVK resets the viewport when render target 0 changes, so the composite fills the target.
static void IvtRsBeginRedirect(IDirect3DDevice9* dev)
{
    if (rsRedirectOn || !rsRedirectSurf || !dev || !backbufferSurf) return;
    RsOwnCall rsOwnGuard;
    dev->GetViewport(&ivtRsSavedVp);          // the game's, in screen pixels
    rsGameVp = ivtRsSavedVp;                  // and its intent, until it says otherwise
    rsRedirectOn = true;                      // from here the hook maps the back buffer to the mod's target
    dev->SetRenderTarget(0, backbufferSurf);
        // Relies on DXVK's viewport reset; said once.
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            D3DVIEWPORT9 got = {};
            dev->GetViewport(&got);
            LogDetail("  render scale: after the redirect the composite's viewport is %ux%u at %u,%u"
                " (it must be %ux%u, the render size)", got.Width, got.Height, got.X, got.Y, ivtW, ivtH);
        }
    }
    ivtRsDidRedirect = true;
    DEVSTAT(if (telemetryOn) { ivtRsRedirects++; })
}

// The device half of ending the redirect: the back buffer and the game's viewport come back. With inject_worker=1 it runs at the
// note's place in the worker's stream; the caller clears the flag on the render thread.
static void IvtRsEndRedirectDevice(IDirect3DDevice9* dev)
{
    if (!dev || !backbufferSurf) return;
    RsOwnCall rsOwnGuard;
    dev->SetRenderTarget(0, backbufferSurf);
    dev->SetViewport(&rsGameVp);              // what the game last asked for, in screen pixels
}
static void IvtRsEndRedirect(IDirect3DDevice9* dev)
{
    if (!rsRedirectOn) return;
    rsRedirectOn = false;
    IvtRsEndRedirectDevice(dev);
}

// The injection copies from the guides in a command buffer DXVK does not track, so the last submit must retire before they are
// released. After 2 s the release goes ahead: a leaked default-pool texture would fail the Reset.
static void IvtDrainInjection();   // defined with the injection, below: IvtReleaseGuides waits on it
static void IvtGuidesQuiesce(const char* why)
{
    if (ivtSubmitOk == 0 || !ivtSemLocal || !ivtDevice) return;
    DT_DIRECT();               // vkWaitSemaphores: see devthread/dt_core.h
    if (!FeedVkWaitTimeline(&ivtVk, ivtSemLocal, ivtSubmitOk, 2000))
        LogRecord("  transport: the last submit had not retired 2 s into %s; guide textures released", why);
}

static void IvtReleaseGuides()
{
        // A posted injection holds these as raw pointers; wait on its count (bounded, any thread).
    IvtDrainInjection();
    if (ivtRsColorTex) RsForget(ivtRsColorTex);
    rsRedirectOn = false; rsRedirectSurf = nullptr; ivtRsDidRedirect = false;
    if (ivtRsColorSurf) { ivtRsColorSurf->Release(); ivtRsColorSurf = nullptr; }
    if (ivtRsColorTex)  { ivtRsColorTex->Release();  ivtRsColorTex  = nullptr; }
    ivtRsColorTried = false;
    if (ivtIntzSurf)  { ivtIntzSurf->Release();  ivtIntzSurf  = nullptr; }
    if (ivtIntzTex)   { ivtIntzTex->Release();   ivtIntzTex   = nullptr; }
    if (ivtMvSurf)    { ivtMvSurf->Release();    ivtMvSurf    = nullptr; }
    if (ivtMvTex)     { ivtMvTex->Release();     ivtMvTex     = nullptr; }
    if (ivtDepthSurf) { ivtDepthSurf->Release(); ivtDepthSurf = nullptr; }
    if (ivtDepthTex)  { ivtDepthTex->Release();  ivtDepthTex  = nullptr; }
    if (ivtGameDsTex) { ivtGameDsTex->Release(); ivtGameDsTex = nullptr; }
    ivtGameDsSurf = nullptr; ivtSceneDsOrig = nullptr; ivtSubstituteLive = false; ivtDepthMode = -1;
        // Not ivtFsVS or ivtFsDecl: other passes bind them every frame, and they survive a Reset.
    if (ivtGuidesPS)  { ivtGuidesPS->Release();  ivtGuidesPS  = nullptr; }
    ivtGuidesTried = false; ivtGuidesOK = false;
        // The dither pass's: default-pool, or held on the game's texture, so released before any Reset.
    if (ivtDedSurf)    { ivtDedSurf->Release();    ivtDedSurf    = nullptr; }
    if (ivtDedTex)     { ivtDedTex->Release();     ivtDedTex     = nullptr; }
    if (ivtDedPS)      { ivtDedPS->Release();      ivtDedPS      = nullptr; }
    if (ivtDedHomePS)  { ivtDedHomePS->Release();  ivtDedHomePS  = nullptr; }
    if (ivtDedMaskTex) { ivtDedMaskTex->Release(); ivtDedMaskTex = nullptr; }
    if (ivtDedCountPS) { ivtDedCountPS->Release(); ivtDedCountPS = nullptr; }
    if (ivtDedCountQ)  { ivtDedCountQ->Release();  ivtDedCountQ  = nullptr; }
    ivtDedCountDead = false;
    ivtDedCountOut = false; ivtDedCountWait = 0; ivtDedLattice = -1;
    ivtDedMaskSurf = nullptr; ivtDedSeenSurf = nullptr; ivtDedSeenFrame = -1;
    ivtDedTried = false; ivtDedOK = false;
        // The steady depth's: likewise.
    for (int i = 0; i < 2; i++)
    {
        for (int k = 0; k < 2; k++)
        {
            if (ivtSd[i].hs[k]) ivtSd[i].hs[k]->Release();
            if (ivtSd[i].h[k])  ivtSd[i].h[k]->Release();
        }
        if (ivtSd[i].src) ivtSd[i].src->Release();
        memset(&ivtSd[i], 0, sizeof(ivtSd[i]));
    }
    if (ivtSdCand)   { ivtSdCand->Release();   ivtSdCand   = nullptr; }
    if (ivtSdPS)     { ivtSdPS->Release();     ivtSdPS     = nullptr; }
    if (ivtSdHomePS) { ivtSdHomePS->Release(); ivtSdHomePS = nullptr; }
    ivtSdTried = false; ivtSdOK = false; ivtSdRejN = 0; ivtSdRejAt = 0; ivtSdSaid = 0;
        // The rain layer's: likewise.
    if (ivtRainSurf) { ivtRainSurf->Release(); ivtRainSurf = nullptr; }
    if (ivtRainTex)  { ivtRainTex->Release();  ivtRainTex  = nullptr; }
    if (ivtRainPS)   { ivtRainPS->Release();   ivtRainPS   = nullptr; }
    ivtRainW = 0; ivtRainH = 0; ivtRainFmt = D3DFMT_UNKNOWN; ivtRainFrame = -1; ivtRainRefused = false;
        // The coronas' held depth: likewise.
    if (ivtCorSurf) { ivtCorSurf->Release(); ivtCorSurf = nullptr; }
    if (ivtCorTex)  { ivtCorTex->Release();  ivtCorTex  = nullptr; }
    if (ivtCorPS)   { ivtCorPS->Release();   ivtCorPS   = nullptr; }
    ivtCorW = 0; ivtCorH = 0; ivtCorRefused = false; ivtCorSeen = -1; ivtCorSnapFrame = -1; ivtCorCapFrame = -1; ivtCorNowN = 0; ivtCorWasN = 0; ivtCorSnapN = 0;
}

// The full-screen quad's own resources, apart from the guides: every pass binds them, including sharpening with transport=0.
// They survive a Reset.
static bool IvtEnsureFsQuad(IDirect3DDevice9* dev)
{
    if (ivtFsVS && ivtFsDecl) return true;
    if (!dev) return false;
    if (!ivtFsVS && (FAILED(dev->CreateVertexShader((const DWORD*)kIvtFsVS, &ivtFsVS)) || !ivtFsVS))
    { ivtFsVS = nullptr; return false; }
    if (!ivtFsDecl)
    {
        D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT,
                                     D3DDECLUSAGE_POSITION, 0 }, D3DDECL_END() };
        if (FAILED(dev->CreateVertexDeclaration(el, &ivtFsDecl)) || !ivtFsDecl)
        { ivtFsDecl = nullptr; return false; }
    }
    return true;
}

static bool IvtEnsureGuides(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (ivtGuidesTried) return ivtGuidesOK;
    ivtGuidesTried = true;
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    HRESULT hr;
        // The INTZ snapshot is made on first need (IvtEnsureIntz): the usual DIRECT path never uses it.
    if (FAILED(hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F, D3DPOOL_DEFAULT, &ivtMvTex, nullptr)) || !ivtMvTex)
    { LogRecord("  transport: G16R16F motion-vector guide %ux%u refused (0x%08X)", w, h, (unsigned)hr); IvtFail("no MV guide texture"); return false; }
    ivtMvTex->GetSurfaceLevel(0, &ivtMvSurf);
    if (FAILED(hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &ivtDepthTex, nullptr)) || !ivtDepthTex)
    { LogRecord("  transport: R32F depth guide %ux%u refused (0x%08X)", w, h, (unsigned)hr); IvtFail("no depth guide texture"); return false; }
    ivtDepthTex->GetSurfaceLevel(0, &ivtDepthSurf);
    if (!IvtEnsureFsQuad(dev))
    { IvtFail("the full-screen quad's vertex shader or declaration was rejected"); return false; }
        // The debug shader is created apart: its failure must not stop the transport.
    if (FAILED(dev->CreatePixelShader((const DWORD*)kIvtGuidesPS, &ivtGuidesPS)))
    { IvtFail("the guide pixel shader was rejected"); return false; }
    LogDetail("  transport: guides ready: G16R16F vectors, R32F depth, %ux%u (the INTZ snapshot comes with the first path that needs it)", w, h);
    ivtGuidesOK = true;
    return true;
}

// The dither pass's target and shader, on first need. A refusal only skips the pass, said once.
static bool IvtEnsureDedither(IDirect3DDevice9* dev, UINT w, UINT h, IDirect3DBaseTexture9* sceneTex)
{
    if (ivtDedTried) return ivtDedOK;
    ivtDedTried = true;
        // The scene buffer's format and size: the output replaces it in the same Vulkan copy.
    IDirect3DTexture9* st = nullptr;
    D3DSURFACE_DESC d = {};
    if (!dev || !sceneTex || FAILED(sceneTex->QueryInterface(__uuidof(IDirect3DTexture9), (void**)&st)) || !st) return false;
    const HRESULT hd = st->GetLevelDesc(0, &d);
    st->Release();
    if (FAILED(hd) || d.Width != w || d.Height != h)
    { LogRecord("  dedither: the scene buffer is %ux%u and the upscaler's input %ux%u: the dither is not smoothed this session", d.Width, d.Height, w, h); return false; }
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    HRESULT hr;
    if (FAILED(hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &ivtDedTex, nullptr)) || !ivtDedTex)
    { ivtDedTex = nullptr; LogRecord("  dedither: its %ux%u target was refused (0x%08X): off this session", w, h, (unsigned)hr); return false; }
    if (FAILED(ivtDedTex->GetSurfaceLevel(0, &ivtDedSurf)) || !ivtDedSurf
        || FAILED(dev->CreatePixelShader((const DWORD*)kIvtDeditherPS, &ivtDedPS)) || !ivtDedPS
        || FAILED(dev->CreatePixelShader((const DWORD*)kIvtDeditherHomePS, &ivtDedHomePS)) || !ivtDedHomePS
        || !IvtEnsureFsQuad(dev))
    {
        if (ivtDedSurf) { ivtDedSurf->Release(); ivtDedSurf = nullptr; }
        if (ivtDedPS)   { ivtDedPS->Release();   ivtDedPS   = nullptr; }
        if (ivtDedHomePS) { ivtDedHomePS->Release(); ivtDedHomePS = nullptr; }
        ivtDedTex->Release(); ivtDedTex = nullptr;
        LogRecord("  dedither: the pass could not be built: off this session");
        return false;
    }
    // The question asked before the pass: its shader and its query. Without either the pass simply runs every frame.
    if (FAILED(dev->CreatePixelShader((const DWORD*)kIvtDeditherCountPS, &ivtDedCountPS)) || !ivtDedCountPS
        || FAILED(dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &ivtDedCountQ)) || !ivtDedCountQ)
    {
        if (ivtDedCountPS) { ivtDedCountPS->Release(); ivtDedCountPS = nullptr; }
        ivtDedCountQ = nullptr;
        LogRecord("  dedither: the count that lets the pass rest could not be made (a shader or an occlusion query refused): the pass runs on every frame");
    }
    LogHealth("  dedither: the pass is ready, %ux%u format %d", w, h, (int)d.Format);
    ivtDedOK = true;
    return true;
}

// Anything for the device this frame? (Render thread, at the post.) False sends the post into DT_DIRECT, where IvtDedProve runs.
static bool IvtDedSettled()
{
    if (!(shFlags & IVMV_FLAG_LOGDEPTH_CONST)) return true;        // no FusionFix: the pass does not run
    if (!ivtDedFusion) return ivtDedFusionLooks >= kDedFusionLooks;                    // the tag is still looked for
    if (!ivtDedTried) return false;                                                    // the target is still to be made
    return !(ivtDedSeenFrame == (LONG)frames && ivtDedSeenSurf && ivtDedSeenSurf != ivtDedMaskSurf);   // a surface not proved yet
}

// With the device thread parked (DT_DIRECT): asks the device's pixel shader and holds the mark's texture, which the scene bound
// this frame; the answer holds while the same surface comes back.
static void IvtDedProve(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (!dev || !(shFlags & IVMV_FLAG_LOGDEPTH_CONST)) return;
    if (!ivtDedFusion && ivtDedFusionLooks < kDedFusionLooks)
    {
        ivtDedFusionLooks++;
        IDirect3DPixelShader9* ps = nullptr;
        if (SUCCEEDED(dev->GetPixelShader(&ps)) && ps)
        {
            UINT size = 0;
            if (SUCCEEDED(ps->GetFunction(nullptr, &size)) && size >= 16 && size <= (1u << 20))
            {
                BYTE* tok = (BYTE*)malloc(size);
                if (tok && SUCCEEDED(ps->GetFunction(tok, &size)))
                    for (UINT i = 0; i + 12 <= size; i += 4)       // three float tokens of a def: on a token boundary
                        if (memcmp(tok + i, "FusionShader", 12) == 0) { ivtDedFusion = 1; break; }
                free(tok);
            }
            ps->Release();
        }
        if (ivtDedFusion)
            LogRecord("  dedither: FusionFix's shaders are live (look %d)", ivtDedFusionLooks);
        else if (ivtDedFusionLooks == kDedFusionLooks)
            LogRecord("  dedither: FusionFix's tag in no scene pixel shader in %d frames: mark not trusted, dither left",
                      kDedFusionLooks);
    }
    if (!ivtDedFusion || ivtDedSeenFrame != (LONG)frames || !ivtDedSeenSurf || ivtDedSeenSurf == ivtDedMaskSurf) return;
    IDirect3DSurface9* const s = ivtDedSeenSurf;
    if (ivtDedMaskTex) { ivtDedMaskTex->Release(); ivtDedMaskTex = nullptr; }
    ivtDedMaskSurf = s;
    D3DSURFACE_DESC d = {};
    IDirect3DTexture9* t = nullptr;
    const bool desc = SUCCEEDED(s->GetDesc(&d));
    if (desc && d.Format == D3DFMT_A8R8G8B8 && d.Width == w && d.Height == h
        && SUCCEEDED(s->GetContainer(__uuidof(IDirect3DTexture9), (void**)&t)) && t)
        ivtDedMaskTex = t;
    if (ivtDedSaid < 6)
    {
        ivtDedSaid++;
        if (ivtDedMaskTex) LogRecord("  dedither: the mark is read from the scene's third extra target, %ux%u", d.Width, d.Height);
        else               LogRecord("  dedither: the scene's third extra target is %ux%u format %d, not A8R8G8B8 %ux%u: no mark, dither left",
                                     d.Width, d.Height, (int)d.Format, w, h);
    }
}

// The mark for this frame, at the capture: no device or COM call.
static IDirect3DTexture9* IvtDedMask()
{
    return (ivtDedFusion && ivtDedSeenFrame == (LONG)frames && ivtDedSeenSurf && ivtDedSeenSurf == ivtDedMaskSurf) ? ivtDedMaskTex : nullptr;
}

// ---- the steady depth: which texture, and its targets ----
// A texture first bound at stage 1 behind the seam (IvtSdBind): is it FusionFix's depth copy? Judged by its description alone
// (an R32F render target of the render size, one level), no device call. Held from here; the next post takes it in.
static void IvtSdMeet(IDirect3DBaseTexture9* tex)
{
    if (ivtSdCand || !ivtBuilt || !ivtW || !ivtH) return;                                 // one at a time, and only with a size to hold it to
    for (int i = 0; i < ivtSdRejN; i++) if (ivtSdRej[i] == tex) return;
    if (tex == ivtDepthTex || tex == ivtCorTex) return;                                   // the mod's own R32F targets of that size
    D3DSURFACE_DESC d = {};
    IDirect3DTexture9* const t2 = (tex->GetType() == D3DRTYPE_TEXTURE) ? static_cast<IDirect3DTexture9*>(tex) : nullptr;
    if (t2 && t2->GetLevelCount() == 1 && SUCCEEDED(t2->GetLevelDesc(0, &d))
        && d.Format == D3DFMT_R32F && (d.Usage & D3DUSAGE_RENDERTARGET) && d.Width == ivtW && d.Height == ivtH)
    { t2->AddRef(); ivtSdCand = t2; return; }
    if (ivtSdRejN < 16) ivtSdRej[ivtSdRejN++] = tex; else ivtSdRej[ivtSdRejAt++ & 15] = tex;
}

// Made with the device thread parked. A refusal only leaves the depth jittered.
static bool IvtSdMake(IDirect3DDevice9* dev, int i, UINT w, UINT h)
{
    if (!ivtSdTried)
    {
        ivtSdTried = true;
        ivtSdOK = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kIvtSteadyPS, &ivtSdPS)) && ivtSdPS
               && SUCCEEDED(dev->CreatePixelShader((const DWORD*)kIvtSteadyHomePS, &ivtSdHomePS)) && ivtSdHomePS && IvtEnsureFsQuad(dev);
        if (!ivtSdOK)
        {
            if (ivtSdPS)     { ivtSdPS->Release();     ivtSdPS     = nullptr; }
            if (ivtSdHomePS) { ivtSdHomePS->Release(); ivtSdHomePS = nullptr; }
            LogRecord("  steady depth: its shaders were refused: the post chain reads the jittered depth this session");
        }
    }
    if (!ivtSdOK) return false;
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    IvtSd& e = ivtSd[i];
        // G16R16F: the value and its freshness (shaders/ivt_steady_ps.hlsl).
    HRESULT hr = S_OK;
    for (int k = 0; k < 2 && SUCCEEDED(hr); k++)
    {
        hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F, D3DPOOL_DEFAULT, &e.h[k], nullptr);
        if (SUCCEEDED(hr) && e.h[k]) hr = e.h[k]->GetSurfaceLevel(0, &e.hs[k]);
        if (SUCCEEDED(hr) && (!e.h[k] || !e.hs[k])) hr = E_FAIL;
    }
    if (FAILED(hr))
    {
        for (int k = 0; k < 2; k++)
        {
            if (e.hs[k]) { e.hs[k]->Release(); e.hs[k] = nullptr; }
            if (e.h[k])  { e.h[k]->Release();  e.h[k]  = nullptr; }
        }
        LogRecord("  steady depth: its two %ux%u targets were refused (0x%08X, video memory?): the post chain reads the jittered depth", w, h, (unsigned)hr);
        return false;
    }
    e.cur = 0; e.hist = false; e.last = -1;
    return true;
}
// Has entry i its targets? (Both or neither: IvtSdMake.)
static __forceinline bool IvtSdHas(int i) { return ivtSd[i].hs[1] != nullptr; }

// Binds of entry i behind the seam in the previous frame (this frame may already have some).
static LONG IvtSdBoundLast(int i)
{
    const LONG f = (LONG)frames;
    return ivtSd[i].seen == f ? ivtSd[i].bindsWas : ivtSd[i].seen == f - 1 ? ivtSd[i].binds : 0;
}
// The copy: bound at least twice (the composite and a blur). The scene's own depth: bound at all, with no copy.
static bool IvtSdWanted(int i)
{
    if (i == 1) return ivtSd[1].src && IvtSdBoundLast(1) >= 2;
    if (!ivtGameDsTex || ivtDepthMode != 1) return false;
    if (!(IvtSdBoundLast(0) >= 1 && !(ivtSd[1].src && IvtSdBoundLast(1) >= 1))) return false;
        // Only at the render size: the home writes it pixel for pixel.
    D3DSURFACE_DESC d = {};
    return SUCCEEDED(ivtGameDsTex->GetLevelDesc(0, &d)) && d.Width == ivtW && d.Height == ivtH;
}

// Anything for the device this frame? False sends the post into DT_DIRECT, where IvtSdProve runs.
static bool IvtSdSettled()
{
    if (ivtSdCand) return false;
    for (int i = 0; i < 2; i++)
    {
        if (!IvtSdHas(i) && !ivtSd[i].refused && IvtSdWanted(i)) return false;         // targets to make
        if (IvtSdHas(i) && ivtSd[i].unserved >= kSdGiveBack) return false;             // targets to give back
    }
    return true;
}

// The targets, made with the device thread parked.
static void IvtSdProve(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (!dev) return;
    if (ivtSdCand)
    {
        IDirect3DTexture9* const t2 = ivtSdCand; ivtSdCand = nullptr;
        IvtSd& e = ivtSd[1];
        if (e.src && (LONG)frames - e.seen <= 1)
        {   // the known copy is still bound: keep it
            if (ivtSdRejN < 16) ivtSdRej[ivtSdRejN++] = t2; else ivtSdRej[ivtSdRejAt++ & 15] = t2;
            t2->Release();
        }
        else
        {
            if (e.src)
            {   // FusionFix made a new copy: the old entry goes, its targets stay
                e.src->Release(); e.src = nullptr; e.hist = false;
            }
            // Held; targets come when first steadied.
            e.src = t2; e.seen = (LONG)frames - 1; e.binds = 1; e.bindsWas = 0; e.unserved = 0;
            if (ivtSdSaid < 8)
            {
                ivtSdSaid++;
                LogRecord("  steady depth: on (%ux%u R32F, FusionFix's own)", w, h);
            }
        }
    }
    for (int i = 0; i < 2; i++)
    {
        IvtSd& e = ivtSd[i];
        if (!IvtSdHas(i) && !e.refused && IvtSdWanted(i))
        {
            if (IvtSdMake(dev, i, w, h))
            {
                e.unserved = 0;
                if (ivtSdSaid < 8)
                {
                    ivtSdSaid++;
                    LogRecord("  steady depth: %s steadied in place (two targets, %.1f MiB)",
                              i ? "the copy" : "the scene's own depth texture",
                              2.0 * 4.0 * (double)w * (double)h / 1048576.0);
                }
            }
            else e.refused = true;
        }
        else if (IvtSdHas(i) && e.unserved >= kSdGiveBack)
        {
            // Unused for 1200 frames: released (no queued command can still name them).
            for (int k = 0; k < 2; k++)
            {
                if (e.hs[k]) { e.hs[k]->Release(); e.hs[k] = nullptr; }
                if (e.h[k])  { e.h[k]->Release();  e.h[k]  = nullptr; }
            }
            e.hist = false; e.unserved = 0;
            if (ivtSdSaid < 8)
            {
                ivtSdSaid++;
                LogRecord("  steady depth: %s not needed for %ld frames: pass rests, targets released", i ? "the depth copy" : "the scene's depth texture", (long)kSdGiveBack);
            }
        }
    }
}

// Which entries this frame (bit 1 the copy, bit 0 the scene's depth). An unpicked entry counts towards giving back its targets.
static int IvtSdPick()
{
    int pick = 0;
    if (ivtSdOK)
        for (int i = 0; i < 2; i++) if (IvtSdHas(i) && IvtSdWanted(i)) pick |= 1 << i;
    for (int i = 0; i < 2; i++)
        if (IvtSdHas(i)) { if (pick & (1 << i)) ivtSd[i].unserved = 0; else ivtSd[i].unserved++; }
    return pick;
}

// From hkSetTexture behind the seam (render thread): counts binds of the two textures and spots an unknown one. Nothing is
// substituted.
static __forceinline void IvtSdBind(DWORD stage, IDirect3DBaseTexture9* tex)
{
    const int i = (tex == (IDirect3DBaseTexture9*)ivtSd[1].src) ? 1 : (tex == (IDirect3DBaseTexture9*)ivtGameDsTex) ? 0 : -1;
    if (i < 0) { if (stage == 1) IvtSdMeet(tex); return; }
    IvtSd& e = ivtSd[i];
    const LONG f = (LONG)frames;
    if (e.seen != f) { e.bindsWas = (e.seen == f - 1) ? e.binds : 0; e.binds = 0; e.seen = f; }
    e.binds++;
}

// ---- the rain layer ----
// The scene buffer's size and format, made at the first rain draw (synchronous creation, so no queued command names an older
// layer). A refusal sends the rain through the upscaler.
static bool IvtRainEnsure(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt)
{
    if (ivtRainTex && ivtRainW == w && ivtRainH == h && ivtRainFmt == fmt) return true;
    if (ivtRainRefused || !dev || !w || !h) return false;
    if (!ivtRainPS && (FAILED(dev->CreatePixelShader((const DWORD*)kIvtRainPS, &ivtRainPS)) || !ivtRainPS || !IvtEnsureFsQuad(dev)))
    {
        if (ivtRainPS) { ivtRainPS->Release(); ivtRainPS = nullptr; }
        ivtRainRefused = true;
        LogRecord("  rain layer: its shader was refused: the rain goes through the upscaler this session");
        return false;
    }
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    IDirect3DTexture9* t = nullptr; IDirect3DSurface9* s = nullptr;
    HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &t, nullptr);
    if (SUCCEEDED(hr) && t) hr = t->GetSurfaceLevel(0, &s);
    if (FAILED(hr) || !t || !s)
    {
        if (s) s->Release();
        if (t) t->Release();
        ivtRainRefused = true;
        LogRecord("  rain layer: its %ux%u target (d3dfmt %u) was refused (0x%08X, video memory?): the rain goes through the upscaler", w, h, (unsigned)fmt, (unsigned)hr);
        return false;
    }
    if (ivtRainSurf) ivtRainSurf->Release();
    if (ivtRainTex)  ivtRainTex->Release();
    ivtRainTex = t; ivtRainSurf = s; ivtRainW = w; ivtRainH = h; ivtRainFmt = fmt; ivtRainFrame = (LONG)frames - 1;
    LogRecord("  rain layer: the game's rain is drawn into a layer of the mod's own from here (%ux%u, d3dfmt %u) and added to the picture after the upscaler", w, h, (unsigned)fmt);
    return true;
}
// A layer to give back? False sends the post into DT_DIRECT.
static bool IvtRainSettled() { return !(ivtRainTex && (LONG)frames - ivtRainFrame > kRainGiveBack); }
// No rain for 1200 frames: released, remade at the next rain draw.
static void IvtRainProve()
{
    if (IvtRainSettled()) return;
    if (ivtRainSurf) { ivtRainSurf->Release(); ivtRainSurf = nullptr; }
    const ULONG left = ivtRainTex->Release(); ivtRainTex = nullptr;
    ivtRainW = 0; ivtRainH = 0; ivtRainFmt = D3DFMT_UNKNOWN;
        // `left` must be 0
    LogDetail("  rain layer: no rain for %ld frames: the layer is given back (%lu reference(s) left on it; 0 is the whole of it)", (long)kRainGiveBack, (unsigned long)left);
}

#include "corona_pass.h"
// ---- the coronas' held depth ----
// Wanted when a corona draw was seen this frame or the last, the scene's depth is read in place, and it is FusionFix's
// logarithmic depth.
static bool IvtCorWanted()
{
    return (LONG)frames - ivtCorSeen <= 1 && ivtGameDsTex && ivtDepthMode == 1 && (shFlags & IVMV_FLAG_LOGDEPTH_CONST) && ivtW && ivtH;
}
// Anything for the device? False sends the post into DT_DIRECT.
static bool IvtCorSettled()
{
    if (ivtCorTex) return !((LONG)frames - ivtCorSeen > kCorGiveBack) && !(IvtCorWanted() && (ivtCorW != ivtW || ivtCorH != ivtH));
    return !(IvtCorWanted() && !ivtCorRefused);
}
// Made or released with the device thread parked. A refusal leaves the coronas on the game's depth.
static void IvtCorProve(IDirect3DDevice9* dev, UINT w, UINT h)
{
    if (!dev || IvtCorSettled()) return;
    if (ivtCorTex)
    {
        const bool idle = (LONG)frames - ivtCorSeen > kCorGiveBack;
        if (ivtCorSurf) { ivtCorSurf->Release(); ivtCorSurf = nullptr; }
        ivtCorTex->Release(); ivtCorTex = nullptr;
        ivtCorW = 0; ivtCorH = 0; ivtCorSnapFrame = -1;
        if (idle) { LogDetail("  corona hold: no corona drawn for %ld frames: the held depth is given back", (long)kCorGiveBack); return; }
    }
    if (!IvtCorWanted() || ivtCorRefused) return;
        // Only at the render size: copied pixel for pixel.
    D3DSURFACE_DESC d = {};
    if (FAILED(ivtGameDsTex->GetLevelDesc(0, &d)) || d.Width != w || d.Height != h)
    {
        ivtCorRefused = true;
        LogRecord("  corona hold: scene depth %ux%u, render size %ux%u: off", d.Width, d.Height, w, h);
        return;
    }
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    HRESULT hr = ivtCorPS ? S_OK : dev->CreatePixelShader((const DWORD*)kIvtCoronaPS, &ivtCorPS);
    if (SUCCEEDED(hr) && ivtCorPS && !IvtEnsureFsQuad(dev)) hr = E_FAIL;
    IDirect3DTexture9* t = nullptr; IDirect3DSurface9* s = nullptr;
    if (SUCCEEDED(hr) && ivtCorPS) hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &t, nullptr);
    if (SUCCEEDED(hr) && t) hr = t->GetSurfaceLevel(0, &s);
    if (FAILED(hr) || !ivtCorPS || !t || !s)
    {
        if (s) s->Release();
        if (t) t->Release();
        ivtCorRefused = true;
        LogRecord("  corona hold: its %ux%u target or shader was refused (0x%08X): off", w, h, (unsigned)hr);
        return;
    }
    ivtCorTex = t; ivtCorSurf = s; ivtCorW = w; ivtCorH = h; ivtCorSnapFrame = -1;
    LogRecord("  corona hold: on (%ux%u R32F, %.1f MiB)", w, h, 4.0 * (double)w * (double)h / 1048576.0);
}
// The phase of the jitter's cycle the snapshot is taken on (CorBestPhase), kept for the cycle's length in use.
static int IvtCorBest()
{
    static int forPhases = 0, best = 0;
    if (forPhases != jitterPhases) { forPhases = jitterPhases; best = CorBestPhase(jitterPhases); }
    return best;
}

// The INTZ snapshot, on first need: written by the copy path, bound by SUBSTITUTE, unused by DIRECT. A refusal fails the transport.
static bool IvtEnsureIntz(IDirect3DDevice9* dev)
{
    if (ivtIntzTex && ivtIntzSurf) return true;
    if (!dev || !ivtW || !ivtH) return false;
    const D3DFORMAT kIntz = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');
    RsOwnCreateCall rsOwnCreateGuard;   // the mod's, at the size asked for, never scaled
    HRESULT hr;
    if (FAILED(hr = dev->CreateTexture(ivtW, ivtH, 1, D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, &ivtIntzTex, nullptr)) || !ivtIntzTex)
    { ivtIntzTex = nullptr; LogRecord("  transport: INTZ depth snapshot texture %ux%u refused (0x%08X)", ivtW, ivtH, (unsigned)hr); IvtFail("no INTZ texture"); return false; }
    if (FAILED(ivtIntzTex->GetSurfaceLevel(0, &ivtIntzSurf)) || !ivtIntzSurf)
    { ivtIntzSurf = nullptr; ivtIntzTex->Release(); ivtIntzTex = nullptr; IvtFail("no INTZ surface"); return false; }
    LogHealth("  transport: INTZ depth snapshot %ux%u created on first need (the depth path is %s)", ivtW, ivtH,
        ivtDepthMode == 2 ? "SUBSTITUTE" : ivtDepthMode == 0 ? "COPY" : "still deciding, copy serves");
    return true;
}

// The VkImage behind one of the mod's textures, and its layout once the stream is flushed. Never cached.
static bool IvtOwnImage(IDirect3DBaseTexture9* tex, VkImage* img, VkImageLayout* layout, VkFormat* fmt)
{
    DT_DIRECT();                 // the image and layout DXVK holds now: see devthread/dt_core.h
    ID3D9VkInteropTexture* it = nullptr;
    if (!tex || FAILED(tex->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return false;
    VkImage i = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    const HRESULT hr = it->GetVulkanImageInfo(&i, &l, &info);
    it->Release();
    if (FAILED(hr) || !i) return false;
    *img = i; *layout = l; if (fmt) *fmt = info.format;
    return true;
}

static bool IvtSurfaceImage(IDirect3DSurface9* surf, VkImage* img, VkImageLayout* layout, VkFormat* fmt,
                            VkExtent3D* ext = nullptr)
{
    DT_DIRECT();                 // the image and layout DXVK holds now: see devthread/dt_core.h
    ID3D9VkInteropTexture* it = nullptr;
    if (!surf || FAILED(surf->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**)&it)) || !it) return false;
    VkImage i = VK_NULL_HANDLE; VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    const HRESULT hr = it->GetVulkanImageInfo(&i, &l, &info);
    it->Release();
    if (FAILED(hr) || !i) return false;
    *img = i; *layout = l; if (fmt) *fmt = info.format; if (ext) *ext = info.extent;
    return true;
}

// Phase A: scene depth to the INTZ snapshot on DXVK's queue. False when there is nothing to copy.
static bool IvtSnapshotDepth()
{
    DT_DIRECT();                 // a flush and a submit on DXVK's queue: see devthread/dt_core.h
    IDirect3DSurface9* ds = dsSceneMajority ? dsSceneMajority : dsAtSceneDraws;
    if (!ds) { ivtDepthMissing++; return false; }
    if (!IvtEnsureIntz(ivtDev)) { ivtDepthMissing++; return false; }   // first need: the copy path
    VkImage dsImg = VK_NULL_HANDLE, zImg = VK_NULL_HANDLE;
    VkImageLayout dsL = VK_IMAGE_LAYOUT_UNDEFINED, zL = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat dsF = VK_FORMAT_UNDEFINED, zF = VK_FORMAT_UNDEFINED;
    VkExtent3D dsExt = {};
    if (!IvtSurfaceImage(ds, &dsImg, &dsL, &dsF, &dsExt) || !IvtOwnImage(ivtIntzTex, &zImg, &zL, &zF)) { ivtDepthMissing++; return false; }
    if (!ivtSaidDsFormat)
    {
        ivtSaidDsFormat = true;
        LogDetail("  transport: scene depth-stencil VkImage 0x%016llX format %d layout %d; INTZ snapshot 0x%016llX format %d layout %d",
            (unsigned long long)dsImg, (int)dsF, (int)dsL, (unsigned long long)zImg, (int)zF, (int)zL);
    }
    if (dsF != zF) { DEVSTAT(if (telemetryOn) { ivtDepthFormatMismatch++; }) return false; }
        // The copy's extent is ivtW x ivtH and feed_vk.h does not clamp it, so a wrong-sized surface is refused (counted in
    // ivtDepthMissing).
    if (dsExt.width != ivtW || dsExt.height != ivtH)
    {
        static UINT lastW = 0, lastH = 0;
        if (dsExt.width != lastW || dsExt.height != lastH)
        {
            lastW = dsExt.width; lastH = dsExt.height;
            LogHealth("  transport: the depth snapshot source is %ux%u, not %ux%u; refused"
                " rather than copied past its end", dsExt.width, dsExt.height, ivtW, ivtH);
        }
        ivtDepthMissing++;
        return false;
    }
    const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    DEVSTAT(LONGLONG t0 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&t0);)
    { IvtWatch w("phase A flush"); ivtIDev->FlushRenderingCommands(); }
    int slot = -1;
    VkCommandBuffer cb;
    { IvtWatch w("phase A command buffer wait"); cb = IvtBeginCb(&slot); }
    if (!cb) return false;
        // Read in GENERAL without a transition, which spares a depth decompress.
    const VkImageLayout dsS = dsL == VK_IMAGE_LAYOUT_GENERAL ? dsL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImageLayout zD  = zL  == VK_IMAGE_LAYOUT_GENERAL ? zL  : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const VkPipelineStageFlags dsStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkAccessFlags        dsAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    IvtBarrier(cb, dsImg, aspect, dsL, dsS, dsStages, dsAccess, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    IvtBarrier(cb, zImg,  aspect, zL,  zD,  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy c = {};
    c.srcSubresource = { aspect, 0, 0, 1 };
    c.dstSubresource = { aspect, 0, 0, 1 };
    c.extent         = { ivtW, ivtH, 1 };
    ivtVk.CmdCopyImage(cb, dsImg, dsS, zImg, zD, 1, &c);
    IvtBarrier(cb, dsImg, aspect, dsS, dsL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, dsStages, dsAccess);
    IvtBarrier(cb, zImg,  aspect, zD,  zL,  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    ivtEndCommandBuffer(cb);
    const UINT64 localVal = ++ivtSubmitN;
    VkSemaphore sig[1] = { ivtSemLocal }; UINT64 sv[1] = { localVal };
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.signalSemaphoreValueCount = 1; tl.pSignalSemaphoreValues = sv;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = sig;
    ivtCbValue[slot] = localVal;
    VkResult r;
    { IvtWatch w("phase A lock"); ivtIDev->LockSubmissionQueue(); }
    { IvtWatch w("phase A submit"); r = ivtQueueSubmit(ivtQueue, 1, &si, VK_NULL_HANDLE); }
    if (r == VK_SUCCESS) ivtSubmitOk = localVal;
    ivtIDev->ReleaseSubmissionQueue();
    DEVSTAT(LONGLONG t1 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&t1);
    const double us = ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0;
    if (telemetryOn) { ivtPhaseAUsSum += us; if (us > ivtPhaseAUsMax) ivtPhaseAUsMax = us; })
    if (r != VK_SUCCESS) { char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "the depth snapshot submit was refused (VkResult %d)", (int)r); IvtFail(why); return false; }
    DEVSTAT(if (telemetryOn) { ivtDepthCopies++; })
    return true;
}

// The guide passes' constants: Publish()'s values, formed here because the passes run mid-frame.

static bool IvtGuideConstants(float c[9][4])
{
    memset(c, 0, sizeof(float) * 36);
    const bool valid = camCur.valid && camPrev.valid && haveSceneKey && sceneKeyStable >= kSceneKeySettleFrames;
        // Distance and angle cuts (CamCutThisFrame).
    const bool jumped = CamCutThisFrame(nullptr, nullptr);
    if (valid)
    {
        Mat4d viewCurD, viewPrevD, invViewCurD, reprojD;
        ToD(camCur.view, &viewCurD);
        ToD(camPrev.view, &viewPrevD);
        InvertRigidD(viewCurD, &invViewCurD);
        MulMatD(invViewCurD, viewPrevD, &reprojD);
        Mat4 reproj;
        ToF(reprojD, &reproj);
        for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) c[r][k] = reproj.m[r][k];
        c[4][0] = camCur.proj.m[0][0];  c[4][1] = camCur.proj.m[1][1];  c[4][2] = camCur.proj.m[2][2];  c[4][3] = camCur.proj.m[3][2];
        c[5][0] = camPrev.proj.m[0][0]; c[5][1] = camPrev.proj.m[1][1]; c[5][2] = camPrev.proj.m[2][2]; c[5][3] = camPrev.proj.m[3][2];
    }
    c[6][0] = camCur.valid ? camCur.nearC : 0.0f;
    c[6][1] = camCur.valid ? camCur.farC  : 0.0f;
        // Rejects a projection the shader cannot divide by, once per draw; an invalid frame fails here too.
    c[6][2] = (valid && !jumped && fabsf(c[4][0]) > 1e-9f && fabsf(c[4][1]) > 1e-9f) ? 1.0f : 0.0f;
    c[6][3] = (shFlags & IVMV_FLAG_LOGDEPTH_CONST) ? 1.0f : 0.0f;
        // After the `if (valid)` block, which fills c[5]: the decode bit must be right on an invalid frame too.
    {
        const float nC = c[6][0], fC = c[6][1];
        const bool  decodeOk = (nC > 0.0f && fC > nC);
        c[5][2] = decodeOk ? log2f(fC / nC) : 0.0f;
        c[5][3] = decodeOk ? 1.0f : 0.0f;
    }
    c[7][0] = (float)ivtW; c[7][1] = (float)ivtH; c[7][2] = 1.0f / (float)ivtW; c[7][3] = 1.0f / (float)ivtH;
        // c8: the shader reads z and w only.
    c[8][0] = 0.0f; c[8][1] = 0.0f;
    c[8][2] = ivtCfg.mv_glass == 1 ? 1.0f : 0.0f;   // "the nearer surface keeps its vector" at mv_glass=1 only
    c[8][3] = c[6][1] * 1.0001f;   // the far-plane slack
    return valid && !jumped;
}

// All D3D9 state the guide passes touch, saved and restored.
struct IvtSavedState
{
    IDirect3DSurface9* rt[4]; IDirect3DSurface9* ds;
    LONG dsSeq;                            // ds_bracket.h: another thread's depth-stencil bind inside this bracket goes back instead of `ds`
    IDirect3DVertexShader9* vs; IDirect3DPixelShader9* ps; IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9* tex[3]; DWORD samp[3][5];
    D3DVIEWPORT9 vp; float c[9][4];
        // Binding render target 0 resets the scissor rectangle as well as the viewport, so both are restored.
    RECT sc;
    DWORD z, zw, ab, at, cull, cw, scissor, stencil, fog, srgb;
        // DrawPrimitiveUP unbinds stream 0, so it and the indices go back. No FVF: under DXVK restoring the declaration restores it, and
    // a SetFVF would replace the game's declaration object.
    IDirect3DVertexBuffer9* vb0; UINT vb0Offset, vb0Stride; IDirect3DIndexBuffer9* ib;
};
static const D3DSAMPLERSTATETYPE kIvtSamp[5] = { D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV };
static const float kIvtQuad[6][4] = { { -1, -1, 0, 1 }, { -1, 1, 0, 1 }, { 1, 1, 0, 1 },
                                      { -1, -1, 0, 1 }, {  1, 1, 0, 1 }, { 1, -1, 0, 1 } };

static void IvtSaveState(IDirect3DDevice9* dev, IvtSavedState* o)
{
    memset(o, 0, sizeof(*o));
    o->dsSeq = DsBracketOpen();            // before the question below (this bracket holds a drain)
    for (int i = 0; i < 4; i++) dev->GetRenderTarget(i, &o->rt[i]);
    dev->GetDepthStencilSurface(&o->ds);
    dev->GetVertexShader(&o->vs);
    dev->GetPixelShader(&o->ps);
    dev->GetVertexDeclaration(&o->decl);
    for (int t = 0; t < 3; t++) { dev->GetTexture(t, &o->tex[t]); for (int k = 0; k < 5; k++) dev->GetSamplerState(t, kIvtSamp[k], &o->samp[t][k]); }
    dev->GetViewport(&o->vp);
    dev->GetScissorRect(&o->sc);
    dev->GetPixelShaderConstantF(0, &o->c[0][0], 9);
    dev->GetRenderState(D3DRS_ZENABLE, &o->z);           dev->GetRenderState(D3DRS_ZWRITEENABLE, &o->zw);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &o->ab); dev->GetRenderState(D3DRS_ALPHATESTENABLE, &o->at);
    dev->GetRenderState(D3DRS_CULLMODE, &o->cull);       dev->GetRenderState(D3DRS_COLORWRITEENABLE, &o->cw);
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &o->scissor); dev->GetRenderState(D3DRS_STENCILENABLE, &o->stencil);
    dev->GetRenderState(D3DRS_FOGENABLE, &o->fog);       dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &o->srgb);
    dev->GetStreamSource(0, &o->vb0, &o->vb0Offset, &o->vb0Stride);
    dev->GetIndices(&o->ib);
}


static void IvtRestoreState(IDirect3DDevice9* dev, IvtSavedState* o)
{
        // No viewport fixup: the restored target is never substituted after the seam.
    for (int i = 0; i < 4; i++) { dev->SetRenderTarget(i, o->rt[i]); if (o->rt[i]) o->rt[i]->Release(); }
    dev->SetDepthStencilSurface(DsBracketPutBack(o->ds, o->dsSeq)); if (o->ds) o->ds->Release();
    dev->SetViewport(&o->vp);
    dev->SetScissorRect(&o->sc);           // after the target binds, which reset it
    dev->SetVertexShader(o->vs); if (o->vs) o->vs->Release();
    dev->SetPixelShader(o->ps);  if (o->ps) o->ps->Release();
    dev->SetVertexDeclaration(o->decl); if (o->decl) o->decl->Release();
    for (int t = 0; t < 3; t++)
    {
        dev->SetTexture(t, o->tex[t]); if (o->tex[t]) o->tex[t]->Release();
        for (int k = 0; k < 5; k++) dev->SetSamplerState(t, kIvtSamp[k], o->samp[t][k]);
    }
    dev->SetPixelShaderConstantF(0, &o->c[0][0], 9);
    dev->SetRenderState(D3DRS_ZENABLE, o->z);            dev->SetRenderState(D3DRS_ZWRITEENABLE, o->zw);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, o->ab);  dev->SetRenderState(D3DRS_ALPHATESTENABLE, o->at);
    dev->SetRenderState(D3DRS_CULLMODE, o->cull);        dev->SetRenderState(D3DRS_COLORWRITEENABLE, o->cw);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, o->scissor); dev->SetRenderState(D3DRS_STENCILENABLE, o->stencil);
    dev->SetRenderState(D3DRS_FOGENABLE, o->fog);        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, o->srgb);
    dev->SetStreamSource(0, o->vb0, o->vb0Offset, o->vb0Stride); if (o->vb0) o->vb0->Release();
    dev->SetIndices(o->ib); if (o->ib) o->ib->Release();
}

// Common full-screen setup: no depth, blending or culling, point sampling, the mod's pass-through shader and declaration.
static void IvtSetupPass(IDirect3DDevice9* dev, const IvtSavedState* o, const float c[9][4])
{
    for (int i = 1; i < 4; i++) if (o->rt[i]) dev->SetRenderTarget(i, nullptr);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);           dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);  dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);         dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetVertexShader(ivtFsVS);
    dev->SetVertexDeclaration(ivtFsDecl);
    dev->SetPixelShaderConstantF(0, &c[0][0], 9);
    for (int t = 0; t < 3; t++)
    {
        dev->SetSamplerState(t, D3DSAMP_MAGFILTER, D3DTEXF_POINT); dev->SetSamplerState(t, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(t, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(t, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); dev->SetSamplerState(t, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
}

// Phase B: the guide pass, sampling the depth texture given. `pre`: constants computed on the render thread at the post, since a
// worker runs this after hkEndScene has advanced the camera. The dither pass shares the bracket (`ded`, from the injection's body
// only); `copy` means it wrote a whole development copy for the upscaler.
struct IvtDedArgs { IDirect3DBaseTexture9* scene; IDirect3DTexture9* mark; int mode; int plan; bool drawn; bool copy; };
#include "dedither_pass.h"
// The steady depth shares the bracket, after the guides (it reads their vectors) and the dither pass. One job per texture ([1]
// FusionFix's copy, [0] the scene's depth); `now` null when not steadied; `depth`: written through the depth-stencil.
struct IvtSdJob { IDirect3DTexture9* now; IDirect3DTexture9* histTex; IDirect3DSurface9* histSurf; IDirect3DTexture9* heldTex; IDirect3DSurface9* heldSurf; bool depth; };
struct IvtSdArgs { const IvtSdJob* job; IDirect3DBaseTexture9* mv; float edge; float share; float decay; LONG frame; bool reset; bool mvNew; bool camStill; bool go; };
#include "steady_depth_pass.h"
#include "rain_pass.h"
// The coronas' held depth shares it too, before the steady depth, which overwrites the game's depth. `all`: the snapshot, else
// movers only. `camStill`: the camera stood still, so a vector means a mover.
struct IvtCorArgs { IDirect3DTexture9* depth; IDirect3DBaseTexture9* mv; IDirect3DSurface9* held; bool all; float invNear, invLog, push; bool mvNew; bool camStill; bool go; };
// corona_pass.h is included further up, before the texture's own functions (they use CorBestPhase).
static bool IvtSdCamStill(const float c[9][4]);
static void IvtCorDrawHere(IDirect3DDevice9* dev, IvtCorArgs* a)
{
    a->go = false;
        // The movers' pass needs trusted vectors of this frame and a still camera; otherwise what is held stays.
    const bool trust = a->mvNew && ivtFrameValid && a->camStill;
    if (!a->all && !trust) { DEVSTAT(if (telemetryOn) ivtCorNoVec++;) return; }
    CorHoldDraw(dev, a->depth, a->mv, a->held, ivtCorPS, ivtW, ivtH, a->all, trust, a->invNear, a->invLog, a->push, kSdStillPx * kSdStillPx, kIvtQuad, sizeof(kIvtQuad[0]));
    dev->SetTexture(0, nullptr); dev->SetTexture(1, nullptr);         // the game's depth leaves the stage before the steady depth binds it as a depth-stencil
    DEVSTAT(if (telemetryOn) { if (a->all) ivtCorSnaps++; else ivtCorMoverPasses++; })
}
// Still when rows 0 to 3 (this view into the previous) are the identity. An untrusted frame is not still.
static bool IvtSdCamStill(const float c[9][4])
{
    if (c[6][2] < 0.5f) return false;
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++)
            if (fabsf(c[r][k] - (r == k ? 1.0f : 0.0f)) > ((r == 3 || k == 3) ? 2e-4f : 2e-5f)) return false;
    return c[4][0] == c[5][0] && c[4][1] == c[5][1];
}
static void IvtSdDrawHere(IDirect3DDevice9* dev, IvtSdArgs* a)
{
    DEVSTAT(if (telemetryOn && a->camStill) ivtSdStill++;)
    for (int i = 1; i >= 0; i--)
    {
        const IvtSdJob& j = a->job[i];
        if (!j.now) continue;
        IvtSd& e = ivtSd[i];
                // Seed (no blending) without a previous result, after a Reset, without vectors, or on a cut.
        const bool seed = !e.hist || e.last + 1 != a->frame || a->reset || !a->mvNew || !ivtFrameValid;
        e.hist = SUCCEEDED(SdResolve(dev, j.now, j.histTex, a->mv, j.heldSurf, ivtSdPS, ivtW, ivtH, seed ? 1.0f : a->share, a->edge, a->decay,
                                     a->camStill, kIvtQuad, sizeof(kIvtQuad[0])));
        e.last = a->frame;
        DEVSTAT(if (telemetryOn) { ivtSdDrawn[i]++; if (seed) ivtSdSeeds[i]++; })
        if (!e.hist) continue;
                // The home: level 0 of the game's texture, released after the draw. Not on a seed.
        IDirect3DSurface9* home = nullptr;
        if (!seed && SUCCEEDED(j.now->GetSurfaceLevel(0, &home)) && home)
        {
            SdHome(dev, j.heldTex, j.depth ? nullptr : home, j.depth ? home : nullptr, j.histSurf, ivtSdHomePS, ivtW, ivtH, kIvtQuad, sizeof(kIvtQuad[0]));
            // The texture leaves the device before its surface is released.
            dev->SetTexture(0, nullptr);
            dev->SetRenderTarget(0, j.heldSurf);
            home->Release();
        }
    }
}
// The dither plan, once a frame before any bracket: take the last count's answer if in (no wait); count every fourth frame,
// run the pass while the last count was not zero.
static int IvtDedPlan()
{
    if (!ivtDedCountQ || ivtDedCountDead) return DED_PASS;
    if (ivtDedCountOut)
    {
        DWORD n = 0;
        if (ivtDedCountQ->GetData(&n, sizeof(n), 0) == S_OK)
        {
            ivtDedCountOut = false; ivtDedCountWait = 0; ivtDedLattice = (LONG)n;
            DEVSTAT(if (telemetryOn && (LONG)n > ivtDedLatticeMax) ivtDedLatticeMax = (LONG)n;)
        }
        else if (++ivtDedCountWait > 240)
        {
            // No answer: run the pass from now on. Not released here: this may be the worker, which creates and releases nothing.
            ivtDedCountOut = false; ivtDedLattice = -1;
            ivtDedCountDead = true;
            LogRecord("  dedither: the count's occlusion query gave no answer in 240 frames: the pass runs on every frame from here");
            return DED_PASS;
        }
    }
    int plan = (ivtDedLattice != 0) ? DED_PASS : 0;
    if (!ivtDedCountOut && (ivtDedBeat++ & 3u) == 0) plan |= DED_COUNT;
    DEVSTAT(if (telemetryOn) { if (!(plan & DED_PASS)) ivtDedIdle++; if (plan & DED_COUNT) ivtDedCounts++; })
    return plan;
}
static void IvtDedDrawHere(IDirect3DDevice9* dev, IvtDedArgs* ded)
{
        // The count first, on the pass's target with colour writes off.
    if ((ded->plan & DED_COUNT) && ivtDedCountQ && ivtDedCountPS
        && SUCCEEDED(DedCount(dev, ded->mark, ivtDedSurf, ivtDedCountPS, ivtW, ivtH, kIvtQuad, sizeof(kIvtQuad[0]), ivtDedCountQ)))
    { ivtDedCountOut = true; ivtDedCountWait = 0; }
    if (ded->plan & DED_PASS)
    {
        {
            // The pass: changed pixels go into the scene buffer itself.
            IDirect3DTexture9* const st = (ded->scene && ded->scene->GetType() == D3DRTYPE_TEXTURE) ? static_cast<IDirect3DTexture9*>(ded->scene) : nullptr;
            IDirect3DSurface9* home = nullptr;
            if (st && ivtDedHomePS && SUCCEEDED(st->GetSurfaceLevel(0, &home)) && home)
            {
                ded->drawn = SUCCEEDED(DedInPlace(dev, ded->scene, home, ded->mark, ivtDedTex, ivtDedSurf, ivtDedPS, ivtDedHomePS, ivtW, ivtH, ded->mode, kIvtQuad, sizeof(kIvtQuad[0])));
                home->Release();
            }
        }
    }
}
static void IvtDrawGuidesFrom(IDirect3DDevice9* dev, IDirect3DBaseTexture9* depthTex,
                              const float (*pre)[4] = nullptr, bool preValid = false, IvtDedArgs* ded = nullptr, IvtSdArgs* sd = nullptr,
                              IvtCorArgs* cor = nullptr)
{
    IvtWatch w("guide passes");
    RsOwnCall rsOwnGuard;      // likewise: the state restore must not scale twice
    DEVSTAT(LONGLONG t0 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&t0);)
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4];
    if (pre) { memcpy(c, pre, sizeof(c)); ivtFrameValid = preValid; }
    else     ivtFrameValid = IvtGuideConstants(c);
        // ivtInOwnDraw is the render thread's flag; set from the worker it would make the render thread skip the game's draws. The
    // worker needs no guard (hooks open with DT_NOT_ON_WORKER); RsGuardHere() decides.
    const bool ownDrawGuard = RsGuardHere();
    if (ownDrawGuard) ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
    D3DVIEWPORT9 vp = { 0, 0, ivtW, ivtH, 0.0f, 1.0f };

    // Both guides in one pass: RT0 the vectors, RT1 the depth.
    dev->SetRenderTarget(0, ivtMvSurf);
    dev->SetRenderTarget(1, ivtDepthSurf);
    dev->SetViewport(&vp);
    dev->SetTexture(0, depthTex);
    dev->SetTexture(1, velTex);
    dev->SetPixelShader(ivtGuidesPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));
    dev->SetRenderTarget(1, nullptr);
    if (ded && ded->plan) { IvtDedDrawHere(dev, ded); ded->plan = 0; }   // after target 1 is unbound: the pass writes one target
    if (cor && cor->go) { cor->mvNew = true; cor->camStill = IvtSdCamStill(c); IvtCorDrawHere(dev, cor); }   // before the steady depth, which writes the depth this copies
    if (sd && sd->go) { sd->mvNew = true; sd->camStill = IvtSdCamStill(c); IvtSdDrawHere(dev, sd); sd->go = false; }   // its vectors are the ones just drawn
    if (ownDrawGuard) ivtInOwnDraw = false;       // see the note where it was set
    IvtRestoreState(dev, &o);
    DEVSTAT(LONGLONG t1 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&t1);
    const double us = ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0;
    if (telemetryOn) { ivtPassesUsSum += us; if (us > ivtPassesUsMax) ivtPassesUsMax = us; })
}


// The dither pass and the steady depth alone, when the guides were drawn at the depth clear (or not at all).
static void IvtDrawDedither(IDirect3DDevice9* dev, IvtDedArgs* ded, IvtSdArgs* sd, IvtCorArgs* cor = nullptr)
{
    IvtWatch w("dither pass");
    RsOwnCall rsOwnGuard;
    IvtSavedState o;
    IvtSaveState(dev, &o);
    const float c[9][4] = {};
    const bool ownDrawGuard = RsGuardHere();      // the render thread's flag, never the worker's: see IvtDrawGuidesFrom
    if (ownDrawGuard) ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
    if (ded && ded->plan) IvtDedDrawHere(dev, ded);
    if (cor && cor->go) IvtCorDrawHere(dev, cor);                      // before the steady depth, as in the guides' bracket
    if (sd && sd->go) { IvtSdDrawHere(dev, sd); sd->go = false; }
    if (ownDrawGuard) ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
}

// Which depth path applies, decided at the first scene-depth clear.
static int  ivtDecideTries = 0;
// COPY by giving up, as opposed to asked for or impossible: provisional, keeps looking.
static bool ivtDepthGaveUp = false;
static void IvtDecideDepthMode()
{
        // Decided for good unless it was a give-up. The vote (most scene draws last frame) can be a shadow map for a long time in a
    // menu, so a give-up is retried when a back-buffer-sized depth wins; COPY then is final.
    if (ivtDepthMode >= 0 && !ivtDepthGaveUp) return;
    IDirect3DSurface9* ds = dsSceneMajority;      // the DS with the most scene draws last frame
    if (!ds) return;
    D3DSURFACE_DESC desc = {};
    if (FAILED(ds->GetDesc(&desc))) { if (!ivtDepthGaveUp) ivtDepthMode = 0; return; }
        // Not back-buffer-sized: a shadow or reflection map. Copy meanwhile; give up on the fast paths after 300 tries.
    if (desc.Width != ivtW || desc.Height != ivtH)
    {
        if (ivtDepthGaveUp) return;   // still not the scene depth; COPY serves, say nothing more
        if (++ivtDecideTries == 1 || ivtDecideTries == 300)
            LogHealth("  transport: the majority depth-stencil %p is %ux%u, not the backbuffer's size; %s", (void*)ds, desc.Width, desc.Height,
                ivtDecideTries == 300 ? "giving up on the fast depth paths for now: copy, and still watching" : "waiting for a frame where the scene depth carries the scene");
        if (ivtDecideTries >= 300) { ivtDepthMode = 0; ivtDepthGaveUp = true; }
        return;
    }
        // After a give-up, this is the frame waited for: decide once.
    if (ivtDepthGaveUp)
    {
        ivtDepthGaveUp = false;
        LogHealth("  transport: the scene depth now carries the scene after %d frames on the copy path; re-deciding", ivtDecideTries);
    }
    IDirect3DTexture9* tex = nullptr;
    const bool isTexture = SUCCEEDED(ds->GetContainer(__uuidof(IDirect3DTexture9), (void**)&tex)) && tex;
    const bool isDepthTexFmt = desc.Format == (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z') || desc.Format == (D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4');
    const bool sizeOk = desc.Width == ivtW && desc.Height == ivtH && desc.MultiSampleType == D3DMULTISAMPLE_NONE;
    char fmt[16];
    if ((unsigned)desc.Format > 0xFF) _snprintf_s(fmt, sizeof(fmt), _TRUNCATE, "%c%c%c%c", (char)(desc.Format & 0xFF), (char)((desc.Format >> 8) & 0xFF), (char)((desc.Format >> 16) & 0xFF), (char)((desc.Format >> 24) & 0xFF));
    else _snprintf_s(fmt, sizeof(fmt), _TRUNCATE, "%u", (unsigned)desc.Format);
    LogDetail("  transport: the scene depth-stencil %p is %ux%u format %s, ms %d, %s", (void*)ds, desc.Width, desc.Height, fmt,
        (int)desc.MultiSampleType, isTexture ? "a texture" : "a plain surface");
    int want = ivtCfg.depth_mode;
    if (want < 0) want = (isTexture && isDepthTexFmt) ? 1 : (sizeOk ? 2 : 0);
    if (want == 1 && !(isTexture && isDepthTexFmt)) { LogHealth("  transport: direct depth asked for but the scene depth is not a depth texture; copy instead"); want = 0; }
    if (want == 2 && !sizeOk) { LogHealth("  transport: substitute depth asked for but the size or sampling differs from the backbuffer; copy instead"); want = 0; }
    if (want == 1) { ivtGameDsTex = tex; tex = nullptr; ivtGameDsSurf = ds; }
    if (want == 2) { ivtSceneDsOrig = ds; }
    if (tex) tex->Release();
        // SUBSTITUTE needs the snapshot before the next bind; a refusal takes the copy path.
    if (want == 2 && !IvtEnsureIntz(ivtDev)) { want = 0; ivtSceneDsOrig = nullptr; }
        // DIRECT: an interim snapshot is released (nothing binds it).
    if (want == 1 && ivtIntzTex && !ivtSubstituteLive)
    {
        LogHealth("  transport: the depth path is DIRECT; the interim INTZ snapshot %ux%u is released", ivtW, ivtH);
        if (ivtIntzSurf) { ivtIntzSurf->Release(); ivtIntzSurf = nullptr; }
        ivtIntzTex->Release(); ivtIntzTex = nullptr;
    }
    ivtDepthMode = want;
    LogHealth("  transport: depth path: %s", want == 1 ? "DIRECT (the guide pass samples the game's depth texture; no copy)"
                                     : want == 2 ? "SUBSTITUTE (the mod's INTZ is bound in place of the scene depth from its next bind; no copy)"
                                                 : "COPY (phase A, the old path)");
}

// Is the held reference still the game's depth? If the game replaced it without a Reset the guides would read a dead texture.
// Last frame's majority or this frame's surface both count, so an ordinary mismatch does not drop DIRECT to the per-frame copy.
// A mismatch is logged once and the copy takes over.
static bool IvtDirectDepthStillOurs()
{
    if (!ivtGameDsSurf || !ivtGameDsTex) return false;
    if (dsSceneMajority == ivtGameDsSurf || dsAtSceneDraws == ivtGameDsSurf) return true;
    static bool said = false;
    if (!said)
    {
        said = true;
        LogRecord("  transport: scene depth-stencil moved (%p, now %p, majority %p): snapshot copy until a Reset",
            (void*)ivtGameDsSurf, (void*)dsAtSceneDraws, (void*)dsSceneMajority);
    }
    return false;
}

// From the SetDepthStencilSurface hook: the game's surface, or the mod's in its place.
static IDirect3DSurface9* IvtSubstituteDepthStencil(IDirect3DSurface9* ds)
{
    if (ivtDepthMode != 2 || ivtInOwnDraw || !ds || !ivtIntzSurf) return ds;
    if (ds == ivtSceneDsOrig)
    {
        if (!ivtSubstituteLive) { ivtSubstituteLive = true; LogDetail("  transport: substitute depth is LIVE: the scene renders into the mod's INTZ from here"); }
        DEVSTAT(if (telemetryOn) { ivtSubstitutions++; })
        return ivtIntzSurf;
    }
    return ds;
}

// Defined with the injection command below.
static void IvtDrainInjection();

// From the Clear hook, before the scene's depth is cleared: the depth is complete. The guide pass runs here; copy mode needs
// phase A first.
static void IvtOnSceneDepthClear()
{
    if (dead || ivtFailed || !ivtBuilt || !ivtDev) return;
    if (ivtGuidesThisFrame) return;
        // The render thread never drives the device while the worker is inside the injection. One load when nothing is outstanding.
    IvtDrainInjection();
    if (!IvtEnsureGuides(ivtDev, ivtW, ivtH)) return;
    IvtDecideDepthMode();
    IDirect3DBaseTexture9* depthTex = nullptr;
    if (ivtDepthMode == 1 && curDS == ivtGameDsSurf) depthTex = ivtGameDsTex;
    else if (ivtDepthMode == 2 && ivtSubstituteLive && curDS == ivtIntzSurf) depthTex = ivtIntzTex;
    else
    {
                // copy mode, a substitute not live yet, or a clear of another surface
        if (!IvtSnapshotDepth()) return;
        DEVSTAT(if (telemetryOn) { ivtSnapAtClear++; })
        depthTex = ivtIntzTex;
    }
    IvtDrawGuidesFrom(ivtDev, depthTex);
    ivtGuidesThisFrame = true;
    DEVSTAT(if (telemetryOn) { ivtGuidesAtClear++; })
}

// ---- stage 2: the copies at the injection point ----

// The shared set's ownership step: first use UNDEFINED -> GENERAL, then acquired back from the host each frame. The caller holds
// ivtSharedCs from begin to submit, or ownership and queue order disagree.
static void IvtAcquireShared(VkCommandBuffer cb, unsigned mask)
{
    if (!ivtLayoutInit)
    {
                // Once per set: all to GENERAL; the untouched ones go straight to the host.
        VkImage all[FEED_SLOTS]; uint32_t na = 0;
        VkImage out[FEED_SLOTS]; uint32_t no = 0;
        for (int i = 0; i < FEED_SLOTS; i++)
        {
            if (!ivtImg[i]) continue;
            all[na++] = ivtImg[i];
            ivtImgReleased[i] = false;
            if (!(mask & (1u << i))) { out[no++] = ivtImg[i]; ivtImgReleased[i] = true; }
        }
        FeedVkBarrierN(&ivtVk, cb, all, na, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        if (no) FeedVkExternalTransferN(&ivtVk, cb, out, no, ivtQueueFamily, true /*release*/);
        ivtLayoutInit = true;
    }
    VkImage acq[FEED_SLOTS]; uint32_t nacq = 0;    // released to the host: acquire back
    VkImage same[FEED_SLOTS]; uint32_t nsame = 0;  // already the mod's: order its accesses
    for (int i = 0; i < FEED_SLOTS; i++)
    {
        if (!ivtImg[i] || !(mask & (1u << i))) continue;
        if (ivtImgReleased[i]) { acq[nacq++] = ivtImg[i]; ivtImgReleased[i] = false; }
        else                   same[nsame++] = ivtImg[i];
    }
    if (nacq)  FeedVkExternalTransferN(&ivtVk, cb, acq, nacq, ivtQueueFamily, false /*acquire*/);
    if (nsame) FeedVkBarrierN(&ivtVk, cb, same, nsame, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
}

static void IvtReleaseShared(VkCommandBuffer cb, unsigned mask)
{
    VkImage rel[FEED_SLOTS]; uint32_t nrel = 0;
    for (int i = 0; i < FEED_SLOTS; i++)
    {
        if (!ivtImg[i] || !(mask & (1u << i)) || ivtImgReleased[i]) continue;
        rel[nrel++] = ivtImg[i]; ivtImgReleased[i] = true;
    }
    if (nrel) FeedVkExternalTransferN(&ivtVk, cb, rel, nrel, ivtQueueFamily, true /*release*/);
}


// The copy home: raw only when the Vulkan formats are identical, else a blit (a raw copy across texel sizes is undefined, and
// across channel orders swaps channels). Without dstFmt the destination is the colour format by construction.
static void IvtCopyHome(VkCommandBuffer cb, VkImage dst, VkImageLayout dstLayout, VkFormat dstFmt = VK_FORMAT_UNDEFINED)
{
    const bool raw = (dstFmt != VK_FORMAT_UNDEFINED) ? (FeedVkFormat(ivtOutputFmt) == dstFmt)
                                                     : (ivtOutputFmt == ivtColorFmt);
    if (raw)
        FeedVkCopyImage(&ivtVk, cb, ivtImg[FEED_OUTPUT], VK_IMAGE_LAYOUT_GENERAL, dst, dstLayout, ivtOutW, ivtOutH);
    else
        FeedVkBlitImage(&ivtVk, cb, ivtImg[FEED_OUTPUT], VK_IMAGE_LAYOUT_GENERAL, dst, dstLayout, ivtOutW, ivtOutH);
}

// The injection: at the first orthographic draw into the back buffer after the scene, before it executes. No D3D9 call between
// LockSubmissionQueue and ReleaseSubmissionQueue.
// The command: everything the body reads that render-thread hooks may change after the seam, so it can run on the worker.
// needReset is consumed at capture, so a deferred body cannot lose a later wakeup.
struct IvtInjectCmd
{
    IDirect3DBaseTexture9*   sceneBufTex;
    IDirect3DBaseTexture9*   sceneDstTex;
    IDirect3DSurface9*       dsAtSceneDraws;
    int                      ivtDepthMode;
    IDirect3DTexture9*       ivtIntzTex;
    IDirect3DSurface9*       ivtIntzSurf;
    IDirect3DTexture9*       ivtGameDsTex;
    bool                     ivtSubstituteLive;
    IDirect3DDevice9*        ivtDev;
    UINT                     ivtW;
    UINT                     ivtH;
    bool                     rsActive;
    LONG                     frames;
    bool                     ivtFrameValid;
    bool                     ivtPrevFrameValid;
    IDirect3DTexture9*       ivtMvTex;
    IDirect3DTexture9*       ivtDepthTex;
    bool                     ivtBackendIsFsr;
    bool                     guidesDone;      // ivtGuidesThisFrame as the Clear hook left it
    bool                     needReset;       // ivtNeedReset, consumed by the capture
    bool                     homeInline;      // the parameter, carried so the worker has it
    bool                     asyncHome;       // this frame's pipelined (1) or same-frame (0) decision, taken on the render thread (mode 4 flips it)
    bool                     deferred;        // running on the worker, not the render thread
    bool                     guidesReady;     // IvtEnsureGuides, decided on the render thread
    bool                     rsRedirectWasOn; // rsRedirectOn as the render thread left it
        // Computed at the post: hkEndScene advances the camera before a worker would reach the guide pass.
    float                    guideC[9][4];
    bool                     guideValid;
        // The frame message's fields, captured likewise: the jitter flag is cleared in EndScene.
    float                    jitterX;         // as the render thread had it at the post
    float                    jitterY;
    float                    cameraNear;      // shared->nearCur, read at the post likewise
        // The velocity drain's camera, captured likewise.
    bool                     camValid;
    float                    camNear, camFar;
        // Its jitter in NDC (jitterX/Y above are pixels).
    float                    jitNdcX, jitNdcY;
    float                    fovYRad;
    int                      dedMode;         // 1, FusionFix's rule, where an upscaler resolves (mode 2); 0: the dither pass does not run this frame
    IDirect3DTexture9*       dedMaskTex;      // the mark's texture as proved on the render thread; nullptr: none
    bool                     dedReady;        // IvtEnsureDedither, decided on the render thread at the post (deferred only)
        // The steady depth's jobs ([1] FusionFix's copy, [0] the scene's depth).
    IvtSdJob                 sd[2];
    float                    sdEdge;          // the edge threshold for the depth's encoding (kSdEdgeLog, or 0)
    float                    sdShare;         // SdShare of the jitter's cycle
    float                    sdDecay;         // SdDecay of it
        // The rain layer: this frame's, or nullptr when no rain was drawn.
    IDirect3DTexture9*       rainTex;
    UINT                     rainW, rainH;
        // The coronas' held depth: corSurf null when there is nothing to do; corAll the snapshot, else movers; corDepth the scene's depth.
    IDirect3DSurface9*       corSurf;
    IDirect3DTexture9*       corDepth;
    bool                     corAll;
    float                    corInvNear, corInvLog, corPush;
};

// The body publishes ivtSentN, ivtOutValid, ivtHomePending and ivtHomeN at its end, and the render thread reads them, so it waits
// for an outstanding previous injection first. One frame of slack: it should never wait; ivtInjectWaits counts it.
static volatile LONG ivtInjectPending = 0;    // a command posted and not yet run
static LONG          ivtInjectWaits   = 0;    // times the render thread actually had to wait
DEVSTAT(static LONG          ivtInjectPosted  = 0;)    // deferred injections (development log only)
DEVSTAT(static LONG          ivtInjectInline  = 0;)    // injections that ran inline anyway (development log only)

static LONG ivtInjectWaitGaveUp = 0;          // waits that hit the ceiling and carried on

// Bounded: the body depends on the render thread's progress, so an unbounded wait can freeze the game.
static const DWORD kIvtDrainCeilingMs = 250;

// After one give-up, return at once until the count is seen at zero again.
static bool ivtDrainAbandoned = false;

static void IvtDrainInjection()
{
    if (!ivtInjectPending) { if (ivtDrainAbandoned) ivtDrainAbandoned = false; return; }   // the common case
    if (ivtDrainAbandoned) return;
    LONGLONG w0 = 0; QueryPerformanceCounter((LARGE_INTEGER*)&w0);
    const LONGLONG ceiling = ivtQpcFreq ? (LONGLONG)((double)ivtQpcFreq * kIvtDrainCeilingMs * 1e-3) : 0;
    bool gaveUp = false;
    for (int i = 0; ivtInjectPending; i++)
    {
        if (i < 64) _mm_pause();
        else        SwitchToThread();
        if (ceiling && (i & 1023) == 1023)
        {
            LONGLONG now = 0; QueryPerformanceCounter((LARGE_INTEGER*)&now);
            if (now - w0 > ceiling) { gaveUp = true; break; }
        }
    }
        // The player's log and the warnings file: the only record of why the game stalled.
    if (gaveUp)
    {
        ivtDrainAbandoned = true;
        const LONG nth = InterlockedIncrement(&ivtInjectWaitGaveUp);
        if (nth <= 8)
            LogRecord("  transport: THE INJECTION DID NOT FINISH in %u ms (#%ld): the render thread stopped waiting for the device thread,"
                      " and does not wait again until it runs",
                  kIvtDrainCeilingMs, nth);
        if (nth == 1)
            IvtWarn("the device thread did not run an injection within %u ms, so the game stopped waiting for it (a stall you may have felt)."
                " If it happens again, set threaded=0 in DLSS-IV.cfg", kIvtDrainCeilingMs);
    }
    DEVSTAT(if (telemetryOn) ivtInjectWaits++;)
}

// Filled on the render thread, at the seam, before anything else runs.
static void IvtCaptureCmd(IvtInjectCmd* c)
{
    c->guidesDone           = ivtGuidesThisFrame;
    c->needReset            = ivtNeedReset;
    ivtNeedReset            = false;         // the consume: see the note above the struct
    c->homeInline           = false;         // set by the caller, which owns the parameter
    c->deferred             = false;         // set at the post, so the body knows its thread
        // The post set guidesReady, rsRedirectWasOn, guideC and guideValid before this: not zeroed here.
    c->sceneBufTex          = sceneBufTex;
    c->sceneDstTex          = sceneDstTex;
    c->dsAtSceneDraws       = dsAtSceneDraws;
    c->ivtDepthMode         = ivtDepthMode;
    c->ivtIntzTex           = ivtIntzTex;
    c->ivtIntzSurf          = ivtIntzSurf;
    c->ivtGameDsTex         = ivtGameDsTex;
    c->ivtSubstituteLive    = ivtSubstituteLive;
    c->ivtDev               = ivtDev;
    c->ivtW                 = ivtW;
    c->ivtH                 = ivtH;
    c->rsActive             = rsActive;
    c->frames               = (LONG)frames;
    c->asyncHome            = IvtAsyncForCapture();
    c->ivtFrameValid        = ivtFrameValid;
    c->ivtPrevFrameValid    = ivtPrevFrameValid;
    c->ivtMvTex             = ivtMvTex;
    c->ivtDepthTex          = ivtDepthTex;
    c->ivtBackendIsFsr      = ivtBackendIsFsr;
        // Read on the render thread: patchedThisFrame is down by the time a worker builds the message.
    c->jitterX              = (jitterEnabled && patchedThisFrame) ? jitterPxX : 0.0f;
    c->jitterY              = (jitterEnabled && patchedThisFrame) ? jitterPxY : 0.0f;
    c->cameraNear           = shared ? shared->nearCur : 0.0f;
    c->jitNdcX              = (jitterEnabled && patchedThisFrame) ? jitterNdcX : 0.0f;
    c->jitNdcY              = (jitterEnabled && patchedThisFrame) ? jitterNdcY : 0.0f;
    c->camValid             = camCur.valid;
    c->camNear              = camCur.nearC;
    c->camFar               = camCur.farC;
    c->fovYRad              = shared ? shared->fovYRad : 0.0f;
        // The dither pass: only where an upscaler takes the frame, with a mark proved this frame.
    c->dedMode              = (IVT_MODE == 2) ? 1 : 0;
    c->dedMaskTex           = c->dedMode ? IvtDedMask() : nullptr;
        // The steady depth: only on a jittered frame with vectors, where the body cannot leave before the guides.
    c->sd[0].now = nullptr; c->sd[1].now = nullptr;
    {
        const int sp = IvtSdPick();          // asked on every injection: it also counts the frames an entry goes unsteadied
        const int sm = (IVT_MODE == 2 && jitterEnabled && patchedThisFrame
                        && ivtDev && ivtMvTex && !(rsActive && !sceneBufTex)) ? sp : 0;
        for (int si = 0; si < 2; si++)
        {
            if (!(sm & (1 << si))) continue;
            IvtSd& e = ivtSd[si];
            IvtSdJob& j = c->sd[si];
            e.cur ^= 1;
            j.now      = si ? e.src : ivtGameDsTex;
            j.histTex  = e.h[e.cur ^ 1];    j.histSurf = e.hs[e.cur ^ 1];
            j.heldTex  = e.h[e.cur];        j.heldSurf = e.hs[e.cur];
            j.depth    = si == 0;                             // the scene's own is a depth texture
        }
        c->sdEdge  = (shFlags & IVMV_FLAG_LOGDEPTH_CONST) ? kSdEdgeLog : 0.0f;
        c->sdShare = SdShare(jitterPhases);
        c->sdDecay = SdDecay(jitterPhases);
        DEVSTAT(if (!sm && telemetryOn) ivtSdLeftOut++;)
    }
        // The rain layer if drawn this frame: added after the upscaler's picture is homed.
    c->rainTex = (ivtRainTex && ivtRainFrame == (LONG)frames) ? ivtRainTex : nullptr;
    c->rainW = ivtRainW; c->rainH = ivtRainH;
    DEVSTAT(if (c->rainTex && telemetryOn) ivtRainPosted++;)
        // The corona hold: on a jittered frame with vectors, depth read in place, and corona draws seen.
    c->corSurf = nullptr; c->corDepth = nullptr; c->corAll = false;
    {
        const LONG f = (LONG)frames;
                // Only the seam's injection: elsewhere the post chain has begun or the depth was cleared.
        const bool seen = ivtCorSeen == f && ivtCorNowN > 0;
        const bool can = seen && ivtCorSurf && ivtCorPS && ivtInjectAtSeam && !ivtGuidesThisFrame
                      && IVT_MODE == 2 && jitterEnabled && patchedThisFrame && ivtDev && ivtMvTex && !(rsActive && !sceneBufTex)
                      && ivtGameDsTex && ivtDepthMode == 1 && (shFlags & IVMV_FLAG_LOGDEPTH_CONST) && camCur.valid && camCur.nearC > 0.0f && camCur.farC > camCur.nearC;
        const bool stood = ivtCorSnapFrame >= 0;
        DEVSTAT(if (telemetryOn && seen) { ivtCorFrames++; if (ivtCorStillN > 0) ivtCorFramesStill++; if (ivtCorServed > 0) ivtCorFramesHeld++; })
        DEVSTAT(if (telemetryOn && seen && !can)
        {
            ivtCorCant++;
            ivtCorCantWhy |= (!(ivtCorSurf && ivtCorPS) ? 1u : 0u) | (!ivtInjectAtSeam ? 2u : 0u) | (ivtGuidesThisFrame ? 4u : 0u)
                           | (!(IVT_MODE == 2 && jitterEnabled && patchedThisFrame && ivtDev && ivtMvTex && !(rsActive && !sceneBufTex)) ? 8u : 0u)
                           | (!(ivtGameDsTex && ivtDepthMode == 1 && (shFlags & IVMV_FLAG_LOGDEPTH_CONST)) ? 16u : 0u)
                           | (!(camCur.valid && camCur.nearC > 0.0f && camCur.farC > camCur.nearC) ? 32u : 0u);
        })
                // Capture-to-capture frame time, on every frame with a corona draw.
        double dt = 1.0 / 30.0;
        if (seen)
        {
            LARGE_INTEGER q; QueryPerformanceCounter(&q);
            if (ivtCorQpcFrame == f - 1 && ivtCorQpc && ivtQpcFreq) dt = (double)(q.QuadPart - ivtCorQpc) / (double)ivtQpcFreq;
            ivtCorQpc = q.QuadPart; ivtCorQpcFrame = f;
        }
        if (can && ivtCorStillN > 0 && (jitterPhaseUsed == IvtCorBest() || !stood))
        {   // The snapshot: this frame's depth and corona rows. On the
            // cycle's smallest jitter, or at once when nothing is held.
            memcpy(ivtCorSnap, ivtCorNow, sizeof(IvtCorRow) * (size_t)ivtCorNowN); ivtCorSnapN = ivtCorNowN; ivtCorSnapFrame = f;
            c->corSurf = ivtCorSurf; c->corAll = true;
        }
        else if (can && stood && ivtCorServed > 0 && f - ivtCorSnapFrame <= 2 * (LONG)jitterPhases) c->corSurf = ivtCorSurf;   // the hold stands: the movers
        else
        {   // the camera moved, nothing to hold for, or no snapshot for two cycles
            DEVSTAT(if (telemetryOn && stood) { if (!can) ivtCorEndCant++; else if (ivtCorServed <= 0) ivtCorEndMoved++; else ivtCorEndAge++; })
            ivtCorSnapFrame = -1;
        }
        if (c->corSurf)
        {
            ivtCorCapFrame = f;
            const float push = kCorMoverSpeed * (float)dt;
            c->corPush    = push > kCorPushMax ? kCorPushMax : push;
            c->corDepth   = ivtGameDsTex;
            c->corInvNear = 1.0f / camCur.nearC;
            c->corInvLog  = 1.0f / log2f(camCur.farC / camCur.nearC);
        }
    }
}

// The injection's body, inline on the render thread or deferred on the worker. Render-thread state travels in the command.
static void IvtInjectBody(const IvtInjectCmd& c, bool homeInline)
{
        // Shadowing the globals by name: reads bind to the snapshot, and const makes a write a compile error.
    IDirect3DBaseTexture9* const sceneBufTex = c.sceneBufTex;
    IDirect3DBaseTexture9* const sceneDstTex = c.sceneDstTex;
    IDirect3DSurface9* const dsAtSceneDraws = c.dsAtSceneDraws;
        // Not the depth set: the body calls IvtDecideDepthMode, which writes it, then reads it.
    bool const ivtSubstituteLive = c.ivtSubstituteLive;
    IDirect3DDevice9* const ivtDev = c.ivtDev;
    UINT const ivtW = c.ivtW;
    UINT const ivtH = c.ivtH;
    bool const rsActive = c.rsActive;
    LONG const frames = c.frames;
        // Not ivtFrameValid: the guide pass called below sets it, and the reset decision reads it after.
    bool const ivtPrevFrameValid = c.ivtPrevFrameValid;
    IDirect3DTexture9* const ivtMvTex = c.ivtMvTex;
    IDirect3DTexture9* const ivtDepthTex = c.ivtDepthTex;
    bool const ivtBackendIsFsr = c.ivtBackendIsFsr;
        // guidesRan is the body's own (the global's write is dropped). The scope belongs to the body: it drains on the render thread
    // and is a no-op nested inline or on the worker (check-dt.py checks it).
    DT_DIRECT();
    bool guidesRan = c.guidesDone;
        // Const, so a stray read of the live global is a compile error.
    bool const ivtNeedReset = c.needReset;        (void)ivtNeedReset;
        // ivtGuidesThisFrame is still written: IvtOnSceneDepthClear early-outs on it (ivtGuidesAtClear counts any clear after the seam).

        // The velocity batch goes out first (one render-target change for hundreds of redraws; see VelRecordObject), before velSurf is
    // read. On the worker the hook globals describe the render thread's future, so VelDrain saves state from the device there. Its
    // camera is the command's (velCamFromCmd).
    DEVSTAT(if (telemetryOn && c.deferred && c.camValid && !camCur.valid) velDrainAfterFrameEnd++;)
    DEVSTAT(if (telemetryOn && c.deferred && (c.jitNdcX != 0.0f || c.jitNdcY != 0.0f) && !patchedThisFrame) velDrainJitterGone++;)
    velCamCmdValid = c.camValid; velCamCmdNear = c.camNear; velCamCmdFar = c.camFar;
    velJitCmdX = c.jitNdcX; velJitCmdY = c.jitNdcY; velCamFromCmd = true;
    if (ivtDev) VelFlushBatch(ivtDev, true);
    velCamFromCmd = false;
    LONGLONG t0 = 0; if (telemetryOn RELSTAT(&& ivtInjections == 0)) QueryPerformanceCounter((LARGE_INTEGER*)&t0);
        // The back buffer and the game's viewport come back first. The device restore follows rsRedirectWasOn, which the post carried
    // after clearing the flag; deferred with it off there is nothing to do.
    if (c.rsRedirectWasOn) IvtRsEndRedirectDevice(ivtDev);
    else if (!c.deferred)  IvtRsEndRedirect(ivtDev);
        // The seam injects before any back-buffer draw, so the condition is the scene buffer.
    if (rsActive && !sceneBufTex) { DEVSTAT(if (telemetryOn) { ivtRsNoRedirect++; }) return; }
    const bool  async = c.asyncHome;     // per frame under mode 4
        // Pipelined: the previous result homes below, before the in-fence lets the host overwrite Output. Nothing here touches the back
    // buffer.

        // Stage 3: the depth snapshot on the queue, then the guide passes, before the handoff.
    bool guides = false;
    VkImage mvImg = VK_NULL_HANDLE, dpImg = VK_NULL_HANDLE;
    VkImageLayout mvL = VK_IMAGE_LAYOUT_UNDEFINED, dpL = VK_IMAGE_LAYOUT_UNDEFINED;
        // Deferred, IvtEnsureGuides and the dither target ran at the post (the render thread reads what they write).
    // The game's dither is smoothed before the upscaler, in the guides' bracket or alone.
    IvtDedArgs ded = { sceneBufTex, c.dedMaskTex, c.dedMode, 0, false };
    const bool dedGo = c.dedMode && c.dedMaskTex && sceneBufTex && ivtDev
                    && (c.deferred ? c.dedReady : IvtEnsureDedither(ivtDev, ivtW, ivtH, sceneBufTex));
    if (dedGo) ded.plan = IvtDedPlan();              // the count, the pass, or nothing at all this frame
        // The depth the post chain reads, steadied in place before anything behind the seam reads it.
    IvtSdArgs sd = { c.sd, ivtMvTex, c.sdEdge, c.sdShare, c.sdDecay, frames, c.needReset, false, false, (c.sd[0].now || c.sd[1].now) && ivtDev != nullptr };
        // The coronas' held depth, before the steady depth. Filled by name, not position.
    IvtCorArgs cor = {};
    cor.depth = c.corDepth; cor.mv = ivtMvTex; cor.held = c.corSurf; cor.all = c.corAll;
    cor.invNear = c.corInvNear; cor.invLog = c.corInvLog; cor.push = c.corPush;
    cor.go = c.corSurf != nullptr && c.corDepth != nullptr && ivtDev != nullptr;
    if (c.deferred ? c.guidesReady : (ivtDev && IvtEnsureGuides(ivtDev, ivtW, ivtH)))
    {
        if (!guidesRan)
        {
            // No depth clear came first, so take the depth now. Deferred, the decision already ran at the post.
            if (!c.deferred) IvtDecideDepthMode();
            IDirect3DBaseTexture9* depthTex = nullptr;
            if (ivtDepthMode == 1 && IvtDirectDepthStillOurs()) depthTex = ivtGameDsTex;
            else if (ivtDepthMode == 2 && ivtSubstituteLive && dsAtSceneDraws == ivtIntzSurf) depthTex = ivtIntzTex;
            else if (IvtSnapshotDepth()) { DEVSTAT(if (telemetryOn) { ivtSnapAtInject++; }) depthTex = ivtIntzTex; }
            // Only inline is the global written: a worker would set it after the frame-end clear and suppress the next frame's pass.
            if (depthTex)
            {
                // Deferred, the constants come from the post (the camera has moved since).
                IvtDrawGuidesFrom(ivtDev, depthTex,
                                  c.deferred ? c.guideC : nullptr, c.guideValid, dedGo ? &ded : nullptr, sd.go ? &sd : nullptr,
                                  cor.go ? &cor : nullptr);
                guidesRan = true;
                if (!c.deferred) ivtGuidesThisFrame = true;
                DEVSTAT(if (telemetryOn) { ivtGuidesAtInject++; })
            }
        }
        if (guidesRan)
            guides = IvtServerUpscaleAvailable() ? (ivtMvTex && ivtDepthTex) :
                (IvtOwnImage(ivtMvTex, &mvImg, &mvL, nullptr) && IvtOwnImage(ivtDepthTex, &dpImg, &dpL, nullptr));
    }
    DEVSTAT(if (telemetryOn && ded.drawn) ivtDedWithGuides++;)
        // What the guides' bracket did not take. The steady depth seeds unless the guides ran.
    if ((dedGo && ded.plan) || sd.go || cor.go)
    {
        sd.mvNew = guidesRan; cor.mvNew = guidesRan;
        IvtDrawDedither(ivtDev, (dedGo && ded.plan) ? &ded : nullptr, sd.go ? &sd : nullptr, cor.go ? &cor : nullptr);
    }
    DEVSTAT(if (telemetryOn) { if (ded.drawn) ivtDedDrawn++; else if (c.dedMode && !c.dedMaskTex) ivtDedNoMark++; })
        // Is the host late for the frame about to be waited on? (log=3)
    DEVSTAT(if (censusOn) { if (async && ivtOutValid && FeedVkTimelineValue(&ivtVk, ivtSemOut) < ivtSentN) ivtHostLate++; })

        // The colour: this frame's composite and everything up to the HUD.
    VkImage rsImg = VK_NULL_HANDLE; VkImageLayout rsL = VK_IMAGE_LAYOUT_UNDEFINED;
        // The scene buffer, not the back buffer, which does not hold the scene yet. Only the dither development view hands over a copy.
    IDirect3DBaseTexture9* const colourTex = ded.copy ? (IDirect3DBaseTexture9*)ivtDedTex : sceneBufTex;
    const bool useRs = colourTex && (IvtServerUpscaleAvailable() || IvtOwnImage(colourTex, &rsImg, &rsL, nullptr));
    if (!useRs) { IvtFail("the scene buffer has no Vulkan image at the injection"); return; }
        // Frame number and reset before the flush: the same-frame message carries both. Every failure before the first submit calls
    // IvtFail, so a number taken and not submitted has no successor.
    const UINT64 n = ++ivtFrameN;
    latOrd[n & (LAT_RING - 1)] = frames;   // pairs this injection with its render-thread frame
    latOrdAsync[n & (LAT_RING - 1)] = async ? 1 : 0;

        // Drop the upscaler's history on every frame whose vectors are untrusted or missing, not only the first.
    const bool untrusted = !ivtFrameValid;
    const bool noGuides  = !guides;
    const UINT32 reset = (c.needReset || untrusted || noGuides) ? 1u : 0u;
    DEVSTAT(if (telemetryOn) { if (untrusted) ivtResetsUntrusted++; if (noGuides) ivtResetsNoGuides++; })
    DEVSTAT(if (telemetryOn) { if (reset) ivtResetsSent++; })
    DEVSTAT(if (telemetryOn) { if (!ivtFrameValid) ivtInvalidFrames++; })
    DEVSTAT(if (telemetryOn) { if (ivtFrameValid != ivtPrevFrameValid) ivtValidEdges++; })
        // ivtNeedReset was consumed by IvtCaptureCmd.

    if (IvtServerUpscaleAvailable())
    {
        // Keep every D3D9 pass above in the worker's FIFO. This one synchronous
        // request flushes, evaluates and homes exactly n on the native server,
        // before the post chain and HUD calls following this injection note.
        IDirect3DBaseTexture9* const home = sceneDstTex ? sceneDstTex : sceneBufTex;
        if (!home || home->GetType() != D3DRTYPE_TEXTURE || colourTex->GetType() != D3DRTYPE_TEXTURE)
        { IvtFail("the native bridge upscale requires texture-backed scene and home resources"); return; }
        VCBInput input = {};
        input.size = sizeof(input); input.version = VCB_ABI_VERSION;
        input.device = ivtDev; input.scene = colourTex; input.home = home;
        input.depth = guides ? ivtDepthTex : nullptr; input.mv = guides ? ivtMvTex : nullptr;
        FeedTaggedFrameIvt timing = {};
        IvtBuildFrameMsg(&timing, n, reset, c.jitterX, c.jitterY, c.cameraNear, c.fovYRad);
        input.frame.sequence = n;
        input.frame.render_width = ivtW; input.frame.render_height = ivtH;
        input.frame.output_width = ivtOutW; input.frame.output_height = ivtOutH;
        input.frame.flags = (reset ? VCB_FRAME_RESET : 0) | (ivtFrameValid ? VCB_FRAME_VALID : 0) |
            (ivtPrevFrameValid ? VCB_FRAME_PREVIOUS_VALID : 0) | (guides ? VCB_FRAME_GUIDES_VALID : 0) |
            (ivtColorFmt == DXGI_FORMAT_R16G16B16A16_FLOAT ? VCB_FRAME_HDR : 0);
        input.frame.jitter_x = timing.fm.jitter_x; input.frame.jitter_y = timing.fm.jitter_y;
        input.frame.frame_time_ms = timing.fm.frame_time_ms;
        input.frame.camera_near = c.cameraNear; input.frame.fov_y_rad = c.fovYRad;
        input.frame.mv_scale_x = 1.0f; input.frame.mv_scale_y = 1.0f;
        if (!IvtServerUpscale(input)) return;
        ivtSentN = n;
        // The scene seam is finished here. The legacy pause fallback still
        // needs its exact completed frame copied after the opaque composite.
        ivtOutValid = false; ivtHomePending = false;
        if (!homeInline)
        {
            IvtServerRememberHome(static_cast<IDirect3DTexture9*>(home), n);
            ivtHomePending = true; ivtHomeN = n; ivtHomeAsync = false;
            DEVSTAT(if (telemetryOn) ivtTwoPoint++;)
        }
        else IvtServerClearHome();
        ivtInjections++;
        LONGLONG t1 = 0;
        if (telemetryOn RELSTAT(&& ivtInjections == 1)) QueryPerformanceCounter((LARGE_INTEGER*)&t1);
        DEVSTAT(const double us = ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0;)
        DEVSTAT(if (telemetryOn) { ivtInjectUsSum += us; ivtInjectUsN++; if (us > ivtInjectUsMax) ivtInjectUsMax = us; })
        if (ivtInjections == 1)
            LogRecord("  transport: FIRST NATIVE X64 INJECTION at frame %ld: same frame %llu, %s, %.0f us",
                static_cast<long>(frames), static_cast<unsigned long long>(n), ::ivtBackendIsFsr ? "FSR" : "DLSS",
                ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0);
        return;
    }

        // The same-frame message goes before the flush (the helper waits on the GPU, so it can record early). Native path only; the
    // bridge's relay writes its own. A failing branch before the first submit signals n from the CPU (IvtSignalInUnsubmitted).
    const bool msgEarly = !async && ivtSyncMode == 0 && !ivtBridge;
    bool sent = false; DWORD sendErr = 0;   // read where it happened
    if (msgEarly) { IvtWatch w("frame message"); sent = IvtSendFrame(n, reset, c.jitterX, c.jitterY, c.cameraNear, c.fovYRad); if (!sent) sendErr = GetLastError(); }

    // Everything the game recorded so far goes to the driver first.
    DEVSTAT(LONGLONG tf0 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&tf0);)
    { IvtWatch w("injection flush"); ivtIDev->FlushRenderingCommands(); }
    DEVSTAT(LONGLONG tf1 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&tf1);
    const double flushUs = ivtQpcFreq ? (double)(tf1 - tf0) * 1e6 / (double)ivtQpcFreq : 0.0;
    if (telemetryOn) { ivtFlushUsSum += flushUs; ivtFlushUsN++; if (flushUs > ivtFlushUsMax) ivtFlushUsMax = flushUs; })

    IvtSharedGuard sharedGuard;   // held through the submit (and the inline home's): see ivtImgReleased
    int slot = -1;
    VkCommandBuffer cb;
    { IvtWatch w("injection command buffer wait"); cb = IvtBeginCb(&slot); }
    if (!cb) { if (msgEarly && sent) IvtSignalInUnsubmitted(n); return; }   // IvtBeginCb has called IvtFail

        // A. no back-buffer barrier: each image used carries its own pair.
    // B. the four this command buffer touches: the mod's again
    IvtAcquireShared(cb, IVT_SET_INJECT);
        // C. this frame's colour -> Color, and the guides -> Depth and MV
    {
        const VkImageLayout rsS = rsL == VK_IMAGE_LAYOUT_GENERAL ? rsL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        IvtBarrierGameIn(cb, rsImg, rsL, rsS);
        FeedVkCopyImage(&ivtVk, cb, rsImg, rsS, ivtImg[FEED_COLOR], VK_IMAGE_LAYOUT_GENERAL, ivtW, ivtH);
        IvtBarrierGameOut(cb, rsImg, rsS, rsL);
    }
    if (guides)
    {
                // The mod's targets: written as colour, read by transfer; GENERAL needs no change.
        const VkImageLayout mvS = mvL == VK_IMAGE_LAYOUT_GENERAL ? mvL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        const VkImageLayout dpS = dpL == VK_IMAGE_LAYOUT_GENERAL ? dpL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        IvtBarrierGameIn(cb, mvImg, mvL, mvS);
        IvtBarrierGameIn(cb, dpImg, dpL, dpS);
        FeedVkCopyImage(&ivtVk, cb, mvImg, mvS, ivtImg[FEED_MV],    VK_IMAGE_LAYOUT_GENERAL, ivtW, ivtH);
        FeedVkCopyImage(&ivtVk, cb, dpImg, dpS, ivtImg[FEED_DEPTH], VK_IMAGE_LAYOUT_GENERAL, ivtW, ivtH);
        IvtBarrierGameOut(cb, mvImg, mvS, mvL);
        IvtBarrierGameOut(cb, dpImg, dpS, dpL);
    }
        // D. pipelined: the previous result goes home before the host may overwrite Output.
    const bool carried = async && ivtOutValid;
        // The resolve goes back into the scene buffer, the post chain's own ping-pong buffer (substituting a texture would break depth
    // of field and motion blur). Under a render scale it lands in the game's output-size buffer.
    IDirect3DBaseTexture9* homeTex = sceneDstTex ? sceneDstTex : sceneBufTex;
        // Said once: without sceneDstTex the fallback is the render-size counterpart, and the copy would overrun it.
    {
        static int said = 0;
        if (said < 3)
        {
            said++;
            LogHealth("  designA: the resolve homes into %p (sceneDstTex %p, sceneBufTex %p)%s",
                (void*)homeTex, (void*)sceneDstTex, (void*)sceneBufTex,
                (homeTex == sceneBufTex && sceneDstTex == nullptr)
                    ? "   <-- FALLBACK: it is writing THE MOD'S counterpart, at the wrong size" : "");
        }
    }
        // One destination for both paths.
    VkImage hmImg = VK_NULL_HANDLE; VkImageLayout hmL = VK_IMAGE_LAYOUT_UNDEFINED;
    const bool haveHome = homeTex && IvtOwnImage(homeTex, &hmImg, &hmL, nullptr);
    const VkImageLayout scD = hmL == VK_IMAGE_LAYOUT_GENERAL ? hmL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    const bool inPlace = carried && haveHome;
    if (inPlace)
    {
                // Step C read this image: the barriers order that read before this write.
        IvtBarrierGameIn(cb, hmImg, hmL, scD);
        IvtCopyHome(cb, hmImg, scD);
        IvtBarrierGameOut(cb, hmImg, scD, hmL);
    }
    else if (carried)
    {
                // A result with nowhere to go: the game's image shows. Logged, never silent.
        static int saidNoHome = 0;
        if (saidNoHome < 3)
        {
            saidNoHome++;
            LogRecord("  transport: FAULT: the carried resolve's home texture %p has no Vulkan image (sceneDstTex %p, sceneBufTex %p)"
                          " at frame %lld; the game's own image shows",
                (void*)homeTex, (void*)sceneDstTex, (void*)sceneBufTex, (long long)ivtFrameN);
        }
    }
        // E. hand them to the host
    IvtReleaseShared(cb, IVT_SET_INJECT);
    ivtEndCommandBuffer(cb);

        // Waits: the out-fence for the last frame sent (pipelined). Signals: this frame's in-fence and the ring slot's timeline.
    VkSemaphore waitSems[1]; UINT64 waitVals[1]; VkPipelineStageFlags waitStages[1]; uint32_t nWait = 0;
    if (carried) { waitSems[0] = ivtSemOut; waitVals[0] = ivtSentN; waitStages[0] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT; nWait = 1; }
    const UINT64 localVal = ++ivtSubmitN;
    VkSemaphore sigSems[2] = { ivtSemIn, ivtSemLocal }; UINT64 sigVals[2] = { n, localVal };
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.waitSemaphoreValueCount   = nWait; tl.pWaitSemaphoreValues   = waitVals;
    tl.signalSemaphoreValueCount = 2;     tl.pSignalSemaphoreValues = sigVals;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext                = &tl;
    si.waitSemaphoreCount   = nWait; si.pWaitSemaphores = waitSems; si.pWaitDstStageMask = waitStages;
    si.commandBufferCount   = 1;     si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 2;     si.pSignalSemaphores = sigSems;
    ivtCbValue[slot] = localVal;

        // Begun before the lock: IvtBeginCb may wait up to 2 s for its slot, which inside LockSubmissionQueue would hold DXVK's submit
    // thread.
    const bool wantCb2 = !async && homeInline && haveHome;
    int slot2 = -1;
    VkCommandBuffer cb2 = wantCb2 ? IvtBeginCb(&slot2) : VK_NULL_HANDLE;
    if (wantCb2 && !cb2) { if (msgEarly && sent) IvtSignalInUnsubmitted(n); return; }   // IvtBeginCb has called IvtFail
    DEVSTAT(LONGLONG tl0 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&tl0);)
    VkResult r;
    { IvtWatch w("injection lock"); ivtIDev->LockSubmissionQueue(); }
    { IvtWatch w("injection submit"); r = ivtQueueSubmit(ivtQueue, 1, &si, VK_NULL_HANDLE); }
    if (r == VK_SUCCESS) ivtSubmitOk = localVal;
    if (r == VK_SUCCESS && !msgEarly) { IvtWatch w("frame message"); sent = ivtBridge ? IvtRelayQueue(n, reset, c.jitterX, c.jitterY, c.cameraNear, c.fovYRad)
                                                                    : IvtSendFrame(n, reset, c.jitterX, c.jitterY, c.cameraNear, c.fovYRad); if (!sent) sendErr = GetLastError(); }
    VkResult r2 = VK_SUCCESS;
    if (r == VK_SUCCESS && sent && wantCb2)
    {
                // Same frame: a second submit waits for this frame's result and copies it home inside the lock, ahead of the HUD. The semaphore
        // chain orders it after step C's read.
        if (cb2)
        {
            IvtAcquireShared(cb2, IVT_SET_OUTPUT);
            IvtBarrierGameIn(cb2, hmImg, hmL, scD);
            IvtCopyHome(cb2, hmImg, scD);
            IvtBarrierGameOut(cb2, hmImg, scD, hmL);
            IvtReleaseShared(cb2, IVT_SET_OUTPUT);
            ivtEndCommandBuffer(cb2);
            const UINT64 localVal2 = ++ivtSubmitN;
            VkSemaphore w2[1] = { ivtSemOut }; UINT64 wv2[1] = { n }; VkPipelineStageFlags ws2[1] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
            VkSemaphore s2[1] = { ivtSemLocal }; UINT64 sv2[1] = { localVal2 };
            VkTimelineSemaphoreSubmitInfo tl2 = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
            tl2.waitSemaphoreValueCount = 1; tl2.pWaitSemaphoreValues = wv2;
            tl2.signalSemaphoreValueCount = 1; tl2.pSignalSemaphoreValues = sv2;
            VkSubmitInfo si2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            si2.pNext = &tl2;
            si2.waitSemaphoreCount = 1; si2.pWaitSemaphores = w2; si2.pWaitDstStageMask = ws2;
            si2.commandBufferCount = 1; si2.pCommandBuffers = &cb2;
            si2.signalSemaphoreCount = 1; si2.pSignalSemaphores = s2;
            ivtCbValue[slot2] = localVal2;
            r2 = ivtQueueSubmit(ivtQueue, 1, &si2, VK_NULL_HANDLE);
            if (r2 == VK_SUCCESS) ivtSubmitOk = localVal2;
            FGA(if (r2 == VK_SUCCESS) FgpHomeWaited((LONG)n);)   // armed product: this frame's home waits on the helper's answer (fg_machine.h, ArmedOverMay)
        }
    }
    else if (r == VK_SUCCESS && sent && !async && homeInline)
    {
                // No Vulkan image to home into: the game's picture shows. Logged.
        static int saidNoHomeSf = 0;
        if (saidNoHomeSf < 3)
        {
            saidNoHomeSf++;
            LogRecord("  transport: FAULT: the same-frame home has no Vulkan image for %p (sceneDstTex %p, sceneBufTex %p)"
                      " at frame %lld; the game's own image shows",
                (void*)homeTex, (void*)sceneDstTex, (void*)sceneBufTex, (long long)ivtFrameN);
        }
    }
    ivtIDev->ReleaseSubmissionQueue();
    DEVSTAT(LONGLONG tl1 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&tl1);
    const double lockUs = ivtQpcFreq ? (double)(tl1 - tl0) * 1e6 / (double)ivtQpcFreq : 0.0;
    if (telemetryOn) { ivtLockUsSum += lockUs; ivtLockUsN++; if (lockUs > ivtLockUsMax) ivtLockUsMax = lockUs; })

        // The VkResult names device lost from out of memory.
    if (r != VK_SUCCESS)  { char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "the mod's submit was refused (VkResult %d)", (int)r); IvtFail(why);
                            if (msgEarly && sent) IvtSignalInUnsubmitted(n);   // after IvtFail released the out-semaphore
                            return; }
    if (!sent)            { char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "frame message failed (pipe broken, Windows error %lu)", sendErr); IvtFail(why); return; }
    if (r2 != VK_SUCCESS) { char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "the mod's home submit was refused (VkResult %d)", (int)r2); IvtFail(why); return; }

    ivtSentN    = n;
    ivtWatchAwait = (LONG)n;     // for the host watch: one aligned 32-bit store a frame
    ivtOutValid = true;
    ivtInjections++;
    if (!homeInline) { ivtHomePending = true; ivtHomeN = n; ivtHomeAsync = async; DEVSTAT(if (telemetryOn) { ivtTwoPoint++; }) }
        // The debug view draws at the EndScene tail; here the post chain would overwrite it.
    LONGLONG t1 = 0; if (telemetryOn RELSTAT(&& ivtInjections == 1)) QueryPerformanceCounter((LARGE_INTEGER*)&t1);
    DEVSTAT(const double us = ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0;)
    DEVSTAT(if (telemetryOn) { ivtInjectUsSum += us; ivtInjectUsN++; })
    DEVSTAT(if (telemetryOn) { if (us > ivtInjectUsMax) ivtInjectUsMax = us; })
    if (ivtInjections == 1)
                // ivtBackendId is set at the build, before any injection, so the global is safe here.
        LogRecord("  transport: FIRST INJECTION at frame %ld (+%lu ms): %s, %s, %.0f us on the render thread",
            (long)frames, (unsigned long)CenMs(), async ? (carried ? "carried the previous result" : "nothing to carry yet") : "same frame",
            IVT_MODE == 1 ? "transport test" : ivtBackendIsFsr ? "FSR" : ivtBackendId == 2 ? "XeSS" : "DLSS", DEVSTAT(us) RELSTAT(ivtQpcFreq ? (double)(t1 - t0) * 1e6 / (double)ivtQpcFreq : 0.0));
}

// The worker side, at the note's place in the stream; its D3D9 calls bypass the mod's hooks. The decrement is a scope guard,
// since the body has many returns.
struct IvtInjectPendGuard { ~IvtInjectPendGuard() { InterlockedDecrement(&ivtInjectPending); } };

// The rain goes back on after the body, however it left: added to the texture the picture was homed into (transport/rain_pass.h).
static void IvtRainLay(const IvtInjectCmd& c)
{
    if (!c.rainTex || !c.ivtDev || !ivtRainPS || ivtFailed) return;
    IDirect3DBaseTexture9* const homeTex = c.sceneDstTex ? c.sceneDstTex : c.sceneBufTex;
    if (!homeTex || homeTex->GetType() != D3DRTYPE_TEXTURE) return;
    IDirect3DSurface9* home = nullptr;
    D3DSURFACE_DESC d = {};
    if (FAILED(static_cast<IDirect3DTexture9*>(homeTex)->GetSurfaceLevel(0, &home)) || !home) return;
    if (FAILED(home->GetDesc(&d)) || !(d.Usage & D3DUSAGE_RENDERTARGET)) { home->Release(); return; }
    IDirect3DDevice9* const dev = c.ivtDev;
    IvtWatch w("rain layer");
    RsOwnCall rsOwnGuard;
    IvtSavedState o;
    IvtSaveState(dev, &o);
    const float zero[9][4] = {};
    const bool ownDrawGuard = RsGuardHere();      // the render thread's flag, never the worker's: see IvtDrawGuidesFrom
    if (ownDrawGuard) ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, zero);
    RainLay(dev, c.rainTex, home, ivtRainPS, d.Width, d.Height, c.rainW != d.Width || c.rainH != d.Height, kIvtQuad, sizeof(kIvtQuad[0]));
    if (ownDrawGuard) ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
    home->Release();
    DEVSTAT(if (telemetryOn) ivtRainLaid++;)
}

static void IvtInjectNoteW(DWORD, const DWORD* payload, DWORD bytes)
{
    IvtInjectPendGuard guard;
    if (bytes != sizeof(IvtInjectCmd)) { DtNoteReplayErr(DT_WHO_OP, E_INVALIDARG); return; }
    IvtInjectCmd cmd = {};   // zeroed: rsRedirectWasOn false means the post did not run
    memcpy(&cmd, payload, sizeof cmd);        // the ring's dwords are not aligned for the struct
    IvtInjectBody(cmd, cmd.homeInline);
    IvtRainLay(cmd);
}

static void IvtInject(bool homeInline)
{
        // The device thread drains here (threaded>=1): flushes and submits are only right after every D3D9 call ahead has been made
    // (devthread/dt_core.h). The previous injection must also have published before this capture.
    IvtDrainInjection();

        // inject_worker=1: post the body as a note in the worker's stream, which needs no drain. Posted outside DT_DIRECT, where DtNote
    // refuses; its refusals are when inline is right. inject_worker=0 keeps its own order: drain, capture, body.
    if (ivtCfg.inject_worker)
    {
                // Decisions that render-thread hooks read run here; only the work is deferred. The scope closes before the post: DtNote refuses
        // inside DT_DIRECT.
        IvtInjectCmd cmd = {};   // zeroed: rsRedirectWasOn false means the post did not run
                // The drain must disappear, not move: a settled frame enters no scope and posts without waiting for the worker.
        const bool postNeedsDevice = !ivtGuidesTried                    // the guide targets are still to be made
                                  || ivtDepthMode < 0 || ivtDepthGaveUp    // the depth path is undecided, or being retried
                                  || !IvtDedSettled()                      // the dither pass has something to prove or make
                                  || !IvtSdSettled() || !IvtRainSettled() || !IvtCorSettled();   // the steady depth, rain layer or coronas' depth has targets to make or release
        if (!postNeedsDevice)
        {
            // Exactly what the scope below computes on a settled frame, without the drain.
            cmd.guidesReady     = ivtDev && ivtGuidesOK;
            cmd.rsRedirectWasOn = rsRedirectOn;
            rsRedirectOn        = false;
            cmd.guideValid      = IvtGuideConstants(cmd.guideC);
            ivtFrameValid       = cmd.guideValid;
            cmd.dedReady        = ivtDedOK;
        }
        else
        {
            DT_DIRECT();
            // IvtEnsureGuides and IvtDecideDepthMode write state the render thread reads (the substitution hook reads ivtDepthMode).
            cmd.guidesReady = ivtDev && IvtEnsureGuides(ivtDev, ivtW, ivtH);
            if (cmd.guidesReady && !ivtGuidesThisFrame) IvtDecideDepthMode();
            // Likewise the dither pass's proofs and target (none without FusionFix's shaders).
            IvtDedProve(ivtDev, ivtW, ivtH);
            cmd.dedReady = ivtDedFusion ? (ivtDev && IvtEnsureDedither(ivtDev, ivtW, ivtH, sceneBufTex)) : false;
            IvtSdProve(ivtDev, ivtW, ivtH);         // the steady depth's targets: the capture reads them
            IvtRainProve();                          // and a rain layer nothing has drawn into for a long while
            IvtCorProve(ivtDev, ivtW, ivtH);         // and the coronas' held depth, made or given back
                                                     // The flag half of ending the redirect, here, or the whole post chain stays redirected; the device half stays in the body.
            cmd.rsRedirectWasOn = rsRedirectOn;
            rsRedirectOn = false;
            // The reprojection, here: hkEndScene advances the camera before the worker would reach it.
            cmd.guideValid = IvtGuideConstants(cmd.guideC);
            ivtFrameValid  = cmd.guideValid;      // as the pass itself would have set it
        }
        IvtCaptureCmd(&cmd);          // after the decisions, so it snapshots their results
        cmd.homeInline = homeInline;
        cmd.deferred   = true;
                // Set here so IvtOnSceneDepthClear cannot run a second guide pass on the render thread while the worker is inside the
        // injection; the worker never writes it.
        ivtGuidesThisFrame = true;
        InterlockedIncrement(&ivtInjectPending);
        if (DtNote(DT_NOTE_INJECT, &cmd, sizeof cmd))
        {
            DEVSTAT(if (telemetryOn) ivtInjectPosted++;)
            if (cmd.rsRedirectWasOn) DtSbBodyForgets();   // the body's redirect half puts nothing back (devthread/dt_sbtab.h)
            return;
        }
        InterlockedDecrement(&ivtInjectPending);      // refused: run it here after all
        cmd.deferred = false;                         // not deferred, so the guides flag is written as inline
                // The flag stays clear: the body restores the device from rsRedirectWasOn on every path.
        DT_DIRECT();
        DEVSTAT(if (telemetryOn) ivtInjectInline++;)
        FGA(FgpInlineGuard();)                        // armed product: no note observer runs here, so its guard is asked here
        IvtInjectBody(cmd, homeInline);
        IvtRainLay(cmd);
        return;
    }

        // The device thread drains here (threaded>=1): see devthread/dt_core.h.
    DT_DIRECT();
    IvtDedProve(ivtDev, ivtW, ivtH);     // the dither pass's proofs, before the capture reads them
    IvtSdProve(ivtDev, ivtW, ivtH);      // and the steady depth's targets
    IvtRainProve();                      // and a rain layer nothing has drawn into for a long while
    IvtCorProve(ivtDev, ivtW, ivtH);     // and the coronas' held depth
    IvtInjectCmd cmd = {}; IvtCaptureCmd(&cmd);
    cmd.homeInline = homeInline;
    FGA(FgpInlineGuard();)               // the armed product's guard behind the helper's prepare, as on the refused path above
    IvtInjectBody(cmd, homeInline);
    IvtRainLay(cmd);
}

// The home, for a split frame: the result into the back buffer, after the pause path's opaque quad.
static void IvtHome()
{
        // It reads the injection's publications and drives the device, so it must not overlap the worker.
    IvtDrainInjection();
    if (!ivtHomePending) return;
    ivtHomePending = false;
        // The pipelined path homes inside IvtInject: return before any device work.
    if (ivtHomeAsync) return;    // the flag the injection ran with, not the cfg's: mode 4 flips it per frame
    DT_DIRECT();                 // a flush and a submit on DXVK's queue: see IvtInject and devthread/dt_core.h
    if (IvtServerUpscaleAvailable())
    {
        IvtServerCopyPendingHome(ivtDev, backbufferSurf, ivtHomeN);
        return;
    }

    VkImage bbImg = VK_NULL_HANDLE; VkImageLayout bbL = VK_IMAGE_LAYOUT_GENERAL;
    if (!IvtLiveBackbuffer(&bbImg, &bbL)) { IvtFail("the backbuffer's Vulkan image could not be read at the home"); return; }
    const VkImageLayout dstL = bbL == VK_IMAGE_LAYOUT_GENERAL ? bbL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    {
        DEVSTAT(LONGLONG hf0 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&hf0);)
        { IvtWatch w("home flush"); ivtIDev->FlushRenderingCommands(); }
        DEVSTAT(LONGLONG hf1 = 0; if (telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&hf1);
        if (ivtQpcFreq)
        {
            const double us = (double)(hf1 - hf0) * 1e6 / (double)ivtQpcFreq;
            if (telemetryOn) { ivtHomeFlushUsSum += us; if (us > ivtHomeFlushUsMax) ivtHomeFlushUsMax = us; }
            if (telemetryOn) { ivtHomeFlushes++; }
        })
    }
    IvtSharedGuard sharedGuard;   // held through the submit: see ivtImgReleased
    int slot = -1;
    VkCommandBuffer cb;
    { IvtWatch w("home command buffer wait"); cb = IvtBeginCb(&slot); }
    if (!cb) return;
    FGA(DtBbsRawWrite();)         // armed product: this copy writes the back buffer unseen by D3D9, so the device thread is told (devthread/dt_bbs.h)
    IvtBarrierGameIn(cb, bbImg, bbL, dstL);
    IvtAcquireShared(cb, IVT_SET_OUTPUT);
    IvtCopyHome(cb, bbImg, dstL, ivtBbLiveFmt);
    IvtReleaseShared(cb, IVT_SET_OUTPUT);
    IvtBarrierGameOut(cb, bbImg, dstL, bbL);
    ivtEndCommandBuffer(cb);

    VkSemaphore waitSems[1] = { ivtSemOut }; UINT64 waitVals[1] = { ivtHomeN }; VkPipelineStageFlags waitStages[1] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
    const UINT64 localVal = ++ivtSubmitN;
    VkSemaphore sig[1] = { ivtSemLocal }; UINT64 sv[1] = { localVal };
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
        // Always one wait: async is false below the return.
    tl.waitSemaphoreValueCount = 1; tl.pWaitSemaphoreValues = waitVals;
    tl.signalSemaphoreValueCount = 1; tl.pSignalSemaphoreValues = sv;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl;
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitSems; si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = sig;
    ivtCbValue[slot] = localVal;
    VkResult r;
    { IvtWatch w("home lock"); ivtIDev->LockSubmissionQueue(); }
    { IvtWatch w("home submit"); r = ivtQueueSubmit(ivtQueue, 1, &si, VK_NULL_HANDLE); }
    if (r == VK_SUCCESS) ivtSubmitOk = localVal;
    ivtIDev->ReleaseSubmissionQueue();
    if (r != VK_SUCCESS) { char why[96]; _snprintf_s(why, sizeof(why), _TRUNCATE, "the mod's home submit was refused (VkResult %d)", (int)r); IvtFail(why); return; }
}

// The pause path copies the back buffer right after the composite and redraws it through its opaque quad, so the resolve must
// be there before that copy or the first 2D draw, whichever comes first. From the StretchRect hook, before the copy.
DEVSTAT(static LONG ivtReadbackInjections = 0;)
static void IvtOnBackbufferReadback()
{
    if (dead || ivtFailed || !ivtBuilt || ivtInOwnDraw) return;
    if (ivtInjected || ivtCaptured || ivtSceneDraws == 0 || !backbufferSurf) return;
    if (rsActive && !ivtRsDidRedirect) return;   // the composite has not been captured yet
    ivtInjected = true;
    ivtCaptured = true;
    DEVSTAT(if (telemetryOn) { ivtReadbackInjections++; })
    IvtInject(true);
}

// The device is a parameter: HDR calls this before the transport's gate, where ivtDev can be null, and a null would let the
// console-gamma pass through as the interface boundary.
static bool IvtIsOpaqueQuad(const DrawRec& rec, IDirect3DDevice9* dev)
{
    if (rec.prims > 2 || !dev) return false;
    DWORD blendOn = 0, cw = 0xF;
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &blendOn);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
    return !blendOn && (cw & 0xF) == 0xF;
}

// From NoteDrawRec, before every draw: the first orthographic back-buffer draw after a scene-camera draw is the first HUD draw.
FGA(static bool FgHudOverlay3d(const DrawRec& rec, IDirect3DDevice9* dev);)   // (fg/fg_hud.h: the armed product's one question at the interface's boundary)
static __declspec(noinline) void IvtOnDraw(const DrawRec& rec)
{
        // ---- HDR, first ----
    // HDR's capture sites run ahead of the transport's gate, so they work without a transport; ivtInOwnDraw keeps the mod's encode
    // draw out of the counts. Gated on hdrHooks: the hottest hook in the mod.
    if (hdrHooks && !dead && !ivtInOwnDraw)
    {
        IDirect3DDevice9* const hdev = ivtDev ? ivtDev : hdrArmDev;
        if (hdev)
        {
            // The interface boundary (hdr.h): the first orthographic back-buffer draw after a scene draw, skipping opaque full-screen quads
            // (FusionFix's console gamma is one). HdrUiCoverage (shaders/hdr_common.hlsli) corrects for a gamma pass after the copy.
            if (rec.bb && rec.ortho && hdrSceneDraws > 0 && !IvtIsOpaqueQuad(rec, hdev))
            {
                if (hdrPreHudPending) HdrPreHudCapture(hdev);
                hdrPreHudDraws++;
            }
            // Armed product, frame generation engaged: a 3D overlay ahead of the interface (the phone) is not orthographic, so
            // FgHudOverlay3d (fg/fg_hud.h) takes its own copy before it. The encode's copy is untouched. No tokens in a non-arming build.
            FGA(else if (rec.bb && hdrBbDraws > 0 && hdrSceneDraws > 0) (void)FgHudOverlay3d(rec, hdev);)
            // The composite: the first back-buffer draw after the scene, two primitives; sampler 2 now holds the pre-tonemap image that
            // highlight restoration reads. With FusionFix's post AA the composite draws off-screen first (hdrAaDivert), searched for at most
            // 600 frames.
            if (hdrAaDivert && hdrOffscreenFrames < 600 && !rec.bb && hdrBbDraws == 0 && hdrSceneDraws > 0 && rec.prims <= 2 && !hdrSceneOk)
            {
                if (!HdrOffscreenComposite(hdev, curRT0, cenScreenW, cenScreenH, rsActive ? rsW : 0, rsActive ? rsH : 0)
                    && !hdrCompositeSurf && hdrOffscreenProbes == 1 && ++hdrOffscreenFrames == 600 && telemetryOn)
                    LogRecord("  hdr: no off-screen composite in 600 frames (8-bit %ux%u%s target, %ux%u float texture on sampler 2): highlight restoration off",
                        cenScreenW, cenScreenH, rsActive ? " or render-size" : "", cenScreenW, cenScreenH);
            }
            if (rec.bb)
            {
                hdrBbDraws++;
                // Not after an off-screen capture this frame: this draw is the AA pass.
                if (hdrBbDraws == 1 && hdrSceneDraws > 0 && rec.prims <= 2 && !hdrSceneOk) HdrCaptureScene(hdev);
                else if (hdrBbDraws == 1 && hdrSceneDraws > 0 && telemetryOn && !hdrSceneOk)
                {
                    // The first back-buffer draw is not a quad: said once.
                    static bool said = false;
                    if (!said)
                    {
                        said = true;
                        LogRecord("  hdr: the first back-buffer draw is %u primitives, not a full-screen quad: no pre-tonemap image this frame", rec.prims);
                    }
                }
            }
            // Last, as the transport counts: a draw can be both scene and back buffer (coronas).
            if (rec.sceneKnown && rec.scene) hdrSceneDraws++;
        }
    }
        // Armed product in SDR: the same boundary for frame generation's HUD-less picture (fg/fg_hud.h). No tokens in a non-arming build.
    FGA(else if (fgSdrHooks && !dead && !ivtInOwnDraw) FgHudSdrOnDraw(rec);)
    if (dead || ivtFailed || !ivtBuilt || ivtInOwnDraw) return;
    DEVSTAT(ivtDrawIndex++;)
    DEVSTAT(ivtDrawIndexForLog = ivtDrawIndex;)
    if (ivtInjected) return;
        // The injection point: the first draw after the seam's render-target bind, when the scene and its transparency are complete and
    // no post pass has run. The orthographic sites below are then unreachable.
    if (seamArmed && ivtResolveArmed && !ivtCaptured)
    {
        seamArmed = false;
        ivtInjected = true; ivtCaptured = true;
        ivtInjectAtSeam = true;              // the corona hold takes its job from this injection alone
        IvtInject(true);
        ivtInjectAtSeam = false;
        return;
    }
    if (rec.sceneKnown && rec.scene) { ivtSceneDraws++; return; }
        // Fallback when the post-chain twins could not be built: from the composite to the injection, back-buffer draws go to the
    // render-size target, so later depth tests keep the scaled depth-stencil.
    if (fpScN == 0 && rsActive && ivtResolveArmed && !ivtRsDidRedirect && !ivtCaptured)
    {
        if (ivtSceneDraws == 0 || !rec.bb || !backbufferSurf || !ivtDev) return;
        if (IvtIsOpaqueQuad(rec, ivtDev) && IvtRsEnsureColor(ivtDev)) IvtRsBeginRedirect(ivtDev);
        else if (telemetryOn)
        {
            // `said` limits the message per session; ivtRsNotQuad is a per-period count.
            static int said = 0;
            DEVSTAT(ivtRsNotQuad++;)                 // report only; the log=1 line below is capped by `said`, not by this
            if (said++ < 3)
                LogRecord("  render scale: the first back-buffer draw is not the composite (%u prims, %s, %s): no resolve this frame",
                    rec.prims, rec.ortho ? "2D" : "3D", ivtRsColorSurf ? "not an opaque quad" : "no colour target");
        }
        return;                       // the composite is never the injection point
    }
    // A split frame: the capture is done and the result waits for the first
    // backbuffer draw that is not another opaque quad.
    if (ivtCaptured)
    {
        if (ivtHomePending && rec.bb && !IvtIsOpaqueQuad(rec, ivtDev)) { ivtInjected = true; IvtHome(); }
        return;
    }
    if (ivtSceneDraws == 0 || !rec.ortho || !rec.bb || !backbufferSurf) return;
        // An opaque quad: the pause path redraws the composite. Capture before it; home at the next back-buffer draw, so the result
    // replaces the redraw and the UI draws on top.
    if (IvtIsOpaqueQuad(rec, ivtDev))
    {
        DEVSTAT(if (telemetryOn) { ivtSkippedQuads++; })
        ivtCaptured = true;
        IvtInject(false);
        return;
    }
    ivtInjected = true;
    ivtCaptured = true;
    IvtInject(true);
}

// The common draw, inline, called by NoteDrawRec: for a draw not into the back buffer it does exactly what IvtOnDraw would, in
// the same order, without its call overhead; everything else goes to IvtOnDraw. Keep the two in step.
static __forceinline void IvtOnDrawFront(const DrawRec& rec)
{
    if (rec.bb || (seamArmed && ivtResolveArmed && !ivtCaptured)
        || (hdrHooks && hdrAaDivert && hdrOffscreenFrames < 600 && hdrBbDraws == 0 && rec.prims <= 2 && !hdrSceneOk))
    {
        IvtOnDraw(rec);
        return;
    }
    if (hdrHooks && !dead && !ivtInOwnDraw && (ivtDev || hdrArmDev) && rec.sceneKnown && rec.scene)
        hdrSceneDraws++;
    if (dead || ivtFailed || !ivtBuilt || ivtInOwnDraw) return;
    DEVSTAT(ivtDrawIndex++;)
    DEVSTAT(ivtDrawIndexForLog = ivtDrawIndex;)
    if (ivtInjected) return;
    if (rec.sceneKnown && rec.scene) ivtSceneDraws++;
}

// No jitter while the vectors are untrusted, so a cut shows a few plain frames.
static bool IvtJitterSuppressed() { return ivtBuilt && !ivtFrameValid; }

// From EndScene. Non-blocking except the handshake and build exchanges.

// A paired A/B of a live cfg key: git show 0405dfc7:src/DLSS-IV/source/transport/DlssIvTransport.h has the form.


static void CpuPlaceTick();   // cpu_place.h, included after this header: the CPU placement's frame
static void IvtTick(IDirect3DDevice9* dev)
{
        // A stopped transport ends its own redirect, which IvtFail does not clear, or the back buffer stays swapped for good.
    if (ivtWatchFired && !ivtFailed)   // 1: the host watch ended a stuck host; 2: the Linux bridge's relay stopped
        IvtFail(ivtWatchFired == 2 ? (IvtHostAlive() ? ivtRelayWhy
                                                     : "the 64-bit host exited on its own while the Linux bridge was carrying frames; its own lines above say why")
                                   : "the 64-bit host is running but stopped answering for 4 s (stuck in a driver, or suspended), so it was ended");
    if (dead || ivtFailed) { if (rsRedirectOn) IvtRsEndRedirect(dev); if (!dead && ivtCfgRead) CpuPlaceTick(); return; }
    if (!ivtCfgRead) IvtReadCfg();
        // The CPU placement (cpu_place.h), every frame at every log level, outside the frame timer and after a stop.
    CpuPlaceTick();

    // Frame time, from EndScene to EndScene, reported every 120 frames whether
    // or not the injection runs.
    if (!ivtQpcFreq) QueryPerformanceFrequency((LARGE_INTEGER*)&ivtQpcFreq);
    if (telemetryOn)
    {
        LONGLONG now = 0; QueryPerformanceCounter((LARGE_INTEGER*)&now);
        if (ivtFrameQpcPrev && ivtQpcFreq)
        {
            const double ms = (double)(now - ivtFrameQpcPrev) * 1e3 / (double)ivtQpcFreq;
            DEVSTAT(ivtFtSum += ms; if (ms > ivtFtMax) ivtFtMax = ms; ivtFtN++;)
            DEVSTAT(if (ms > 25.0) ivtFtOver25++;)
            DEVSTAT(if (ms > 40.0) ivtFtOver40++;)
            if (ms > 500.0 && InterlockedIncrement(&ivtFrameGaps) <= 40)
            {
                LogRecord("  transport: FRAME GAP %.0f ms ending at frame %ld: valid %d prev %d, jitter %s, injected %d, scene draws %d",
                    ms, (long)frames, ivtFrameValid ? 1 : 0, ivtPrevFrameValid ? 1 : 0, jitterEnabled ? "on" : "off",
                    ivtInjected ? 1 : 0, ivtSceneDraws);
            }
#if DLSSIV_DEVLOG
            if (ivtFtN >= 120)
            {
                LogHealth("  frametime: %d frames, mean %.2f ms (%.1f fps), max %.1f ms, over 25 ms %d, over 40 ms %d  [injection %s]",
                    ivtFtN, ivtFtSum / ivtFtN, 1000.0 * ivtFtN / ivtFtSum, ivtFtMax, ivtFtOver25, ivtFtOver40,
                    !ivtBuilt ? "not built" : "running");
                ivtFtSum = 0.0; ivtFtMax = 0.0; ivtFtN = 0; ivtFtOver25 = 0; ivtFtOver40 = 0;
            }
#endif
        }
        ivtFrameQpcPrev = now;
    }
        // Below the frame timer, so transport=0 is measured by the same instrument. The mod is idle only with threaded=0 and hdr=0 too.
    if (!ivtCfg.transport) return;

    // The host is started as soon as the transport is on, so it is ready by the
    // time the scene has settled instead of a second and a half later.
    if (!ivtStarted)
    {
        ivtStarted = true;
        if (GetModuleHandleA("dlss5-feed.addon32"))
        {
            IvtFail("the DLSS5-Feeder add-on (dlss5-feed.addon32) is loaded in this process; one client per game."
                    " Rename it to .off to test this transport");
            return;
        }
        LogHealth("");
        LogHealth("transport: STAGE 3, the mod's copies on DXVK's queue at the injection point");
    }
    ivtDev = dev;
    if (!curDSKnown)
    {
        IDirect3DSurface9* ds = nullptr;
        if (SUCCEEDED(dev->GetDepthStencilSurface(&ds))) { curDS = ds; curDSKnown = true; if (ds) ds->Release(); }
    }
    if (!IvtEnsureVulkan(dev)) return;
    if (!IvtEnsureRing()) return;
    if (!IvtServerUpscaleAvailable())
    {
        // An older remote bridge cannot safely use the x86 interop handoff.
        if (FeedVkRemoteModule()) { IvtFail("the remote bridge lacks the paired native 64-bit upscale API"); return; }
        if (!IvtEnsureHost()) return;
        if (!IvtHostAlive()) { IvtFail("the host exited; DLSS-IV.log says why"); return; }
    }
        // An unsettled scene key cannot resolve: skip everything, and end the redirect.
    ivtResolveArmed = haveSceneKey && sceneKeyStable >= kSceneKeySettleFrames;
    if (!ivtResolveArmed)
    {
        if (rsRedirectOn) { IvtRsEndRedirect(dev); DEVSTAT(if (telemetryOn) { ivtRsLingering++; }) }
        ivtRsDidRedirect = false;
#if DLSSIV_DEVLOG
        if (++ivtRsUnsettled == 1 || ivtRsUnsettled == 600)
            LogHealth("  transport: the scene key is not settled, so nothing resolves this frame%s",
                ivtRsUnsettled == 600 ? " (600 frames now; the camera capture is being rejected)" : "");
#endif
        return;
    }
    DEVSTAT(ivtRsUnsettled = 0;)

        // A scene with no draw after the opaque quad (no HUD): inject at the frame's end.
    if (ivtBuilt && !ivtInjected && backbufferSurf)
    {
        if (ivtCaptured && ivtHomePending) { ivtInjected = true; DEVSTAT(if (telemetryOn) { ivtHomeAtEndScene++; }) IvtHome(); }
        else if (!ivtCaptured && ivtSceneDraws > 0 && (!rsActive || ivtRsDidRedirect))
        { ivtInjected = true; ivtCaptured = true; DEVSTAT(if (telemetryOn) { ivtInjectAtEndScene++; }) IvtInject(true); }
        else if (!ivtCaptured && ivtSceneDraws > 0 && rsActive) { DEVSTAT(if (telemetryOn) { ivtRsNoRedirect++; }) }
    }
    if (ivtHomePending)
    {
        ivtHomePending = false;   // a frame that never found its home: nothing is carried
        if (IvtServerUpscaleAvailable()) IvtServerClearHome();
    }
    if (telemetryOn) { if (ivtBuilt && !ivtInjected && ivtSceneDraws > 0) ivtFramesNoInject++; }
    if (telemetryOn) { if (ivtBuilt && !ivtInjected && ivtSceneDraws == 0) ivtFramesNoScene++; }
    if (ivtBuilt && ivtSemLocal)
        { DEVSTAT(if (censusOn) { ivtPendingSum += (LONGLONG)(ivtSubmitN - FeedVkTimelineValue(&ivtVk, ivtSemLocal)); ivtPendingN++; }) }   // log=3 only
    DEVSTAT(if (telemetryOn) { ivtSrBeforeSum += srBbBeforeInject; ivtSrAfterSum += srBbAfterInject; ivtSrOtherSum += srOther; })
    DEVSTAT(srBbBeforeInject = 0; srBbAfterInject = 0; srOther = 0;)
        // A redirect still on at the frame's end: the back buffer comes back.
    if (rsRedirectOn) { IvtRsEndRedirect(dev); DEVSTAT(if (telemetryOn) { ivtRsLingering++; }) }
    ivtRsDidRedirect = false;
    ivtInjected = false;
    ivtCaptured = false;
    ivtSceneDraws = 0;
    DEVSTAT(ivtDrawIndex = 0;)
    ivtPrevFrameValid = ivtFrameValid;
    DsPublishMajority();
    DEVSTAT(if (telemetryOn) { ivtDsChangesSum += dsChangesAfterScene; ivtDsClearsSum += dsClearsAfterScene; })
    DEVSTAT(dsChangesAfterScene = 0; dsClearsAfterScene = 0;) dsAtSceneDraws = nullptr;
    ivtGuidesThisFrame = false;

        // The scene buffer changed: rebuild. (A back-buffer change is a Reset, which clears ivtBuilt.)
    if (ivtBuilt && sceneBufTex && sceneBufTex != ivtBuiltForScene)
    {
        LogHealth("  transport: the scene buffer is now %p and the set was built for %p; rebuilding",
            (void*)sceneBufTex, (void*)ivtBuiltForScene);
        IvtGuidesQuiesce("a rebuild");
        IvtReleaseGuides();
        ivtBuilt = false;
        FGX(FgxStandDown();)
    }
    // The scale turned on or changed after the set was built.
    if (ivtBuilt && rsActive && (ivtW != rsW || ivtH != rsH))
    {
        LogHealth("  transport: the render scale wants %ux%u and the set is %ux%u; rebuilding", rsW, rsH, ivtW, ivtH);
        IvtGuidesQuiesce("a rebuild");
        IvtReleaseGuides();
        ivtBuilt = false;
        FGX(FgxStandDown();)
    }
    if (!ivtBuilt)
    {
        if (GetTickCount64() < ivtRetryAt) return;
        FGA(FgpBeforeBuild();)         // armed product: an armed pair is brought to its real frame in that drain
        if (!IvtBuild(dev)) return;
    }

    if (telemetryOn && ivtInjections > 0 && (ivtInjections % 300) == 0)
    {
        static LONG lastReported = 0;
        if (lastReported != ivtInjections)
        {
            lastReported = ivtInjections;
            const UINT64 completed = IvtServerUpscaleAvailable() ? ivtSentN : FeedVkTimelineValue(&ivtVk, ivtSemOut);

            // One health line that names the fault; the breakdown stays at level 3.
            {
                // Per-period deltas of the cumulative counters; the level-3 breakdown keeps the totals.
                static LONG prevInj = 0, prevNoInj = 0, prevMissing = 0, prevNoScene = 0;
                const LONG dInj     = ivtInjections     - prevInj;
                const LONG dNoInj   = ivtFramesNoInject - prevNoInj;
                const LONG dMissing = ivtDepthMissing   - prevMissing;
                // Frames built for that drew no scene (menus, loads), unlike dNoInj (a scene and no injection).
                const LONG dNoScene = ivtFramesNoScene  - prevNoScene;
                prevInj = ivtInjections; prevNoInj = ivtFramesNoInject; prevMissing = ivtDepthMissing;
                prevNoScene = ivtFramesNoScene;

                const long long lag = (long long)(ivtSentN - completed);
                char bad[192]; int n = 0; bad[0] = 0;
                if (dNoInj)
                    n += _snprintf_s(bad + n, sizeof(bad) - n, _TRUNCATE,
                                     "; %ld frames had a scene and no injection", dNoInj);
                if (dNoScene)
                    n += _snprintf_s(bad + n, sizeof(bad) - n, _TRUNCATE,
                                     "; %ld frames drew no scene", dNoScene);
                if (lag > 5)
                    n += _snprintf_s(bad + n, sizeof(bad) - n, _TRUNCATE,
                                     "; the host is %lld frames behind", lag);
                if (dMissing)
                    n += _snprintf_s(bad + n, sizeof(bad) - n, _TRUNCATE,
                                     "; depth missing on %ld frames", dMissing);
                                // inject_worker's health: the wait should be 0, and exactly one guide-pass counter should move per frame. The fence is around
                // the if header: this block is in the release build, where an extra `if` can permute .data (see LOGHELD in DLSS-IV.cpp).
#if DLSSIV_DEVLOG
                static LogHold holdGuidePass;
                if (!LOGHELD(holdGuidePass, ivtGuidesAtClear != 0 && ivtGuidesAtInject != 0, ivtGuidesAtClear, ivtGuidesAtInject))
#endif
                LogHealth("  transport: guide pass ran at the depth clear %ld, at the injection %ld"
                    " (this period; they are alternatives, so both rising together is the fault)%s",
                    ivtGuidesAtClear, ivtGuidesAtInject, LOGMARK);
                DEVSTAT(LogHealth("  dedither: mode %d, the pass drew on %ld frame(s) this period (%ld with the guides, %ld on its own) and was left out on %ld (nothing dithered);"
                    " %ld count(s) asked, the largest answer %ld lattice texel(s), the last %ld; no mark on %ld",
                    1, ivtDedDrawn, ivtDedWithGuides, ivtDedDrawn - ivtDedWithGuides, ivtDedIdle, ivtDedCounts, ivtDedLatticeMax, ivtDedLattice, ivtDedNoMark);)
                DEVSTAT(LogHealth("  rain layer: %ld rain draw(s) made into the layer this period; the capture carried it on %ld frame(s) and it was laid over the picture on %ld;"
                    " %ld rain draw(s) left alone because more than one render target was bound%s",
                    ivtRainTaken, ivtRainPosted, ivtRainLaid, ivtRainMrt, ivtRainTex ? "" : "; no layer is held (no rain yet, or none for 1200 frames)");)
                DEVSTAT(LogHealth("  corona hold, the draws with the corona's signature before any gate, by draw call: DrawIndexedPrimitive %ld, DrawPrimitive %ld, DrawPrimitiveUP %ld, DrawIndexedPrimitiveUP %ld;"
                    " %ld of those that passed the gates went into a target that is not the scene buffer."
                    " On %ld frame(s) with corona draws the capture could give no job (why, as bits: 1 no texture yet, 2 not the seam's injection, 4 the guides were drawn at a depth clear,"
                    " 8 not a jittered frame for an upscaler, 16 not FusionFix's depth read in place, 32 no camera): 0x%X",
                    ivtCorHook[0], ivtCorHook[1], ivtCorHook[2], ivtCorHook[3], ivtCorOtherRT, ivtCorCant, ivtCorCantWhy);)
                DEVSTAT(LogHealth("  corona hold: corona draws on %ld frame(s) this period (%ld draws; %ld other draw(s) of twelve texldl were not the corona's and were left alone)."
                    " The camera stood still on %ld of those frames; where it did not, the transform was apart from the frame before's by %.1e to %.1e of its size (the same camera: up to %.0e)."
                    " A draw was served from the held depth on %ld frame(s), %ld draw(s) in all; %ld draw(s) the snapshot did not know (apart by %.1e to %.1e)."
                    " %ld snapshot(s) and %ld movers' pass(es) drawn, %ld left out for want of trusted vectors. A hold ended %ld time(s): the camera moved %ld, no job in the frame before %ld, no new snapshot in two cycles %ld, not possible that frame %ld%s",
                    ivtCorFrames, ivtCorDraws, ivtCorOther12, ivtCorFramesStill, ivtCorMoveMax > 0.0f ? (double)ivtCorMoveMin : 0.0, (double)ivtCorMoveMax, (double)kCorSame,
                    ivtCorFramesHeld, ivtCorGiven, ivtCorOdd, ivtCorOddMax > 0.0f ? (double)ivtCorOddMin : 0.0, (double)ivtCorOddMax,
                    ivtCorSnaps, ivtCorMoverPasses, ivtCorNoVec, ivtCorEndMoved + ivtCorEndNoJob + ivtCorEndAge + ivtCorEndCant, ivtCorEndMoved, ivtCorEndNoJob, ivtCorEndAge, ivtCorEndCant,
                    ivtCorTex ? "" : ivtCorRefused ? "; no texture: it was refused" : "; no texture is held (no corona drawn, or not FusionFix's depth read in place)");)
                DEVSTAT(LogHealth("  steady depth: %s; the copy steadied on %ld frame(s) this period (%ld of them seeds), the scene's own depth on %ld (%ld seeds); the camera still on %ld; nothing steadied on %ld frame(s). Bound behind the seam in the frame before: the copy %ld time(s), the scene's own depth %ld",
                    ivtSdDrawn[0] + ivtSdDrawn[1] == 0 ? (ivtSd[1].src ? "RESTING: the depth copy is bound once a frame behind the seam, by the composite alone (no depth of field, no sun shafts)" : "nothing steadied: no depth texture is bound behind the seam")
                    : "written back into the game's own texture before the post chain reads it",
                    ivtSdDrawn[1], ivtSdSeeds[1], ivtSdDrawn[0], ivtSdSeeds[0], ivtSdStill, ivtSdLeftOut, IvtSdBoundLast(1), IvtSdBoundLast(0));)
#if DLSSIV_DEVLOG
                if (ivtCfg.inject_worker || ivtInjectPosted)
#endif
                    LogHealth("  transport: injection on the DEVICE THREAD: %ld posted, %ld ran inline anyway,"
                        " render thread waited for the previous one %ld time(s)%s",
                        ivtInjectPosted, ivtInjectInline, ivtInjectWaits,
                        ivtInjectWaits ? "   <-- NOT ZERO: the worker is not keeping up, see DESIGN-INJECTION-ON-THE-WORKER.md"
                                       : "   (zero is the design holding)");
                LogHealth("  transport: %ld injected, host lag %lld, %.0f us mean (max %.0f), depth %s%s",
                          dInj, lag,
                          ivtInjectUsN ? ivtInjectUsSum / (double)ivtInjectUsN : 0.0, ivtInjectUsMax,
                          ivtDepthMode == 1 ? "DIRECT" : ivtDepthMode == 2 ? "SUBSTITUTE" : "COPY",
                          bad[0] ? bad : "");
                                // The log=1 heartbeat: the first period and every thirtieth, naming the period's faults.
                static LONG periods = 0;
                if ((++periods % 30) == 0 || periods == 1)
                {
                    // The first period's few frames before the depth path was chosen are not a fault.
                    const bool startDepth = periods == 1 && dMissing > 0 && dMissing <= 4 && !dNoInj && lag <= 5;
                    LogRecord("  health at frame %ld (+%lu ms): %ld injected this session, %ld this period%s%s",
                        (long)frames, (unsigned long)CenMs(), ivtInjections, dInj, bad[0] ? bad : ", no faults",
                        startDepth ? " (expected before the depth path is chosen)" : "");
                    IvtWarnsToLog();
                }
            }

            // Counts are lifetime, times this period.
            LogDetail("  transport: injections %ld (frames with a scene but no injection %ld), sent %llu, host completed %llu (lag %lld);"
                " THIS PERIOD: cost mean %.0f us max %.0f us over %ld samples (flush mean %.0f max %.0f over %ld, lock+submit mean %.0f max %.0f over %ld)",
                ivtInjections, ivtFramesNoInject, (unsigned long long)ivtSentN, (unsigned long long)completed,
                (long long)(ivtSentN - completed),
                ivtInjectUsN ? ivtInjectUsSum / (double)ivtInjectUsN : 0.0, ivtInjectUsMax, ivtInjectUsN,
                ivtFlushUsN  ? ivtFlushUsSum  / (double)ivtFlushUsN  : 0.0, ivtFlushUsMax,  ivtFlushUsN,
                ivtLockUsN   ? ivtLockUsSum   / (double)ivtLockUsN   : 0.0, ivtLockUsMax,   ivtLockUsN);
                        // Total flush cost a frame, only when the split home's second flush ran (ivtHomeFlushes).
#if DLSSIV_DEVLOG
            if (ivtHomeFlushes > 0)
            {
                // Both terms this period.
                const double handoff = ivtFlushUsN     ? ivtFlushUsSum     / (double)ivtFlushUsN     : 0.0;
                const double home    = ivtHomeFlushes  ? ivtHomeFlushUsSum / (double)ivtHomeFlushes  : 0.0;
                LogDetail("  transport: flush total per frame, this period: handoff %.0f us + home %.0f us = %.0f us"
                    " (home max %.0f, %ld home flushes)",
                    handoff, home, handoff + home, ivtHomeFlushUsMax, ivtHomeFlushes);
            }
#endif
            DEVSTAT(ivtHomeFlushUsSum = 0.0; ivtHomeFlushUsMax = 0.0; ivtHomeFlushes = 0;)
            LogDetail("  transport: host late at the injection %ld of %ld injections;"
                " the mod's submits pending at EndScene %.2f on average over %ld frames",
                ivtHostLate, (long)(ivtInjections ? 300 : 0),
                ivtPendingN ? (double)ivtPendingSum / (double)ivtPendingN : 0.0, ivtPendingN);
            LogDetail("  transport: depth path %s: guide passes at the clear %ld, at the injection %ld, substitutions %ld;"
                    " copies %ld (at the clear %ld, at the injection %ld, missing %ld, format mismatch %ld), phase A mean %.0f us max %.0f,"
                    " passes mean %.0f us a pass, max %.0f; depth-stencil changes after the scene %d, clears of the scene depth %d (per 300 frames)",
                    ivtDepthMode == 1 ? "DIRECT" : ivtDepthMode == 2 ? (ivtSubstituteLive ? "SUBSTITUTE (live)" : "SUBSTITUTE (pending)") : "COPY",
                    ivtGuidesAtClear, ivtGuidesAtInject, ivtSubstitutions,
                    ivtDepthCopies, ivtSnapAtClear, ivtSnapAtInject, ivtDepthMissing, ivtDepthFormatMismatch,
                    ivtDepthCopies ? ivtPhaseAUsSum / (double)ivtDepthCopies : 0.0, ivtPhaseAUsMax,
                    // per guide pass, this period
                    (ivtGuidesAtClear + ivtGuidesAtInject) ? ivtPassesUsSum / (double)(ivtGuidesAtClear + ivtGuidesAtInject) : 0.0, ivtPassesUsMax,
                    ivtDsChangesSum, ivtDsClearsSum);
            DEVSTAT(ivtPhaseAUsMax = 0.0; ivtPassesUsMax = 0.0; ivtPassesUsSum = 0.0; ivtDsChangesSum = 0; ivtDsClearsSum = 0;)
            DEVSTAT(ivtGuidesAtClear = 0; ivtGuidesAtInject = 0; ivtSubstitutions = 0;)
            DEVSTAT(ivtDedDrawn = 0; ivtDedWithGuides = 0; ivtDedNoMark = 0; ivtDedIdle = 0; ivtDedCounts = 0; ivtDedLatticeMax = 0;)
            DEVSTAT(ivtSdDrawn[0] = 0; ivtSdDrawn[1] = 0; ivtSdSeeds[0] = 0; ivtSdSeeds[1] = 0; ivtSdLeftOut = 0; ivtSdStill = 0;)
            DEVSTAT(ivtRainTaken = 0; ivtRainPosted = 0; ivtRainLaid = 0; ivtRainMrt = 0;)
            DEVSTAT(ivtCorDraws = 0; ivtCorGiven = 0; ivtCorOdd = 0; ivtCorSnaps = 0; ivtCorMoverPasses = 0; ivtCorNoVec = 0; ivtCorFrames = 0; ivtCorFramesStill = 0; ivtCorFramesHeld = 0; ivtCorOther12 = 0;
                    ivtCorEndMoved = 0; ivtCorEndNoJob = 0; ivtCorEndAge = 0; ivtCorEndCant = 0; ivtCorMoveMin = 1.0e9f; ivtCorMoveMax = 0.0f; ivtCorOddMin = 1.0e9f; ivtCorOddMax = 0.0f;
                    ivtCorHook[0] = ivtCorHook[1] = ivtCorHook[2] = ivtCorHook[3] = 0; ivtCorOtherRT = 0; ivtCorCant = 0; ivtCorCantWhy = 0;)
            LogDetail("  transport: StretchRect from the backbuffer per 300 frames: %d before the injection, %d after it, other copies %d;"
                " opaque quads %ld (split frames %ld, homes at EndScene %ld), whole injections at EndScene %ld, at a read-back %ld",
                ivtSrBeforeSum, ivtSrAfterSum, ivtSrOtherSum, ivtSkippedQuads, ivtTwoPoint, ivtHomeAtEndScene, ivtInjectAtEndScene, ivtReadbackInjections);
            LogDetail("  transport: DLSS history per 300 frames: resets sent %ld (untrusted vectors %ld, no guides %ld),"
                " frames with untrusted vectors %ld, validity edges %ld%s", ivtResetsSent, ivtResetsUntrusted, ivtResetsNoGuides,
                ivtInvalidFrames, ivtValidEdges,
                ivtResetsSent > 30 ? "   <-- RESETTING OFTEN: DLSS cannot accumulate and degrades to a single-frame upscale" : "");
#if DLSSIV_DEVLOG
            ivtResetsSent = 0; ivtInvalidFrames = 0; ivtValidEdges = 0; ivtResetsUntrusted = 0; ivtResetsNoGuides = 0;
            // Held while unchanged; ivtRsLingering, a redirect open at EndScene, is the alarm.
            static LogHold holdTransportRs;
            if ((rsActive || ivtRsRedirects || ivtRsNoRedirect)
                && !LOGHELD(holdTransportRs, ivtRsLingering != 0, ivtRsRedirects, ivtRsNoRedirect, ivtRsLingering, ivtRsNotQuad))
                LogHealth("  transport: render scale per 300 frames: redirected composites %ld, frames with a scene and no redirect %ld,"
                    " redirects still open at EndScene %ld, first backbuffer draws that were not the composite %ld%s",
                    ivtRsRedirects, ivtRsNoRedirect, ivtRsLingering, ivtRsNotQuad, LOGMARK);
            ivtRsRedirects = 0; ivtRsNoRedirect = 0; ivtRsLingering = 0; ivtRsNotQuad = 0;
            ivtSkippedQuads = 0; ivtInjectAtEndScene = 0; ivtTwoPoint = 0; ivtHomeAtEndScene = 0;
#endif
            DEVSTAT(ivtReadbackInjections = 0;)
            DEVSTAT(ivtSrBeforeSum = 0; ivtSrAfterSum = 0; ivtSrOtherSum = 0;)
            // Sums and counts reset with the maxes.
            DEVSTAT(ivtInjectUsMax = 0.0; ivtFlushUsMax = 0.0; ivtLockUsMax = 0.0;)
            DEVSTAT(ivtInjectUsSum = 0.0; ivtFlushUsSum = 0.0; ivtLockUsSum = 0.0;)
            DEVSTAT(ivtInjectUsN   = 0;   ivtFlushUsN   = 0;   ivtLockUsN   = 0;)
            DEVSTAT(ivtHostLate = 0; ivtPendingSum = 0; ivtPendingN = 0;)
        }
    }
}

// Jitter only when frames reach an upscaler through this transport (mode 2, built); never in the transport test (mode 1).
static bool IvtResolveState(bool* transportOn)
{
    *transportOn = ivtCfgRead && ivtCfg.transport != 0 && ivtStarted && !ivtFailed;
    return *transportOn && ivtBuilt && IVT_MODE == 2;
}

static void IvtOnReset()
{
    // A native frame is synchronous, but its device-thread note may still be
    // pending. Drain it before the service drops resources for the Reset RPC.
    if (IvtServerUpscaleAvailable()) { IvtDrainInjection(); IvtServerReset(ivtDev); }
    if (latPacerOn) LatPaceReset();   // the pacer's estimate does not survive a Reset
        // The VkDevice survives a D3D9 Reset under DXVK; the back buffer may not.
    IvtGuidesQuiesce("a device Reset");
    IvtReleaseGuides();  // default pool: gone with the reset, recreated lazily
    ivtSaidDsFormat = false;

        // The sizes are written only in IvtBuild, and the injection runs before the rebuild tests, so a resolution change would use the
    // old sizes for a frame. Rebuild before injecting, and carry no stale Output home.
    ivtBuilt    = false;
    ivtOutValid = false;
    FGX(FgxStandDown();)   // frame generation's set was made for the device as it was: the next build asks again
}

static void IvtShutdown()
{
    FGX(FgpStop("shutdown", true);)   // frame generation's machine, likewise: flags only, no join and no line through the stream (DllMain's detach)
    IvtHostWatchStop();
    LatWaiterStop();
    IvtRelayStop();
    if (ivtPipe) { CloseHandle(ivtPipe); ivtPipe = nullptr; }
    if (ivtHost)
    {
                // A host exits on the pipe break after releasing its fence waits. One that never had a client can only exit via its orphan
        // watchdog, which cannot fire before DllMain returns, so it is terminated rather than waited for under the loader lock.
        if (!ivtPipeOpened) TerminateProcess(ivtHost, 0);
        else                WaitForSingleObject(ivtHost, 2000);
        CloseHandle(ivtHost);
        ivtHost = nullptr;
    }
    // Vulkan objects are not destroyed here: this runs at process exit, where
    // DXVK's device may already be gone, and the OS reclaims the imports.
}
