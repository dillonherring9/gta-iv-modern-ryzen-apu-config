// DLSS-IV.asi: real motion vectors for GTA IV, read out of the RAGE engine rather than guessed from pixels.
//
// The camera comes from rage::grcViewport (0x3F0 bytes): +0x100 mWorldViewProjMatrix, +0x180 mViewMatrix,
// +0x1C0 mProjectionMatrix, +0x2B8 mFov, +0x2C0 mNearClip, +0x2C4 mFarClip, +0x3E0 mInvertZInProjectionMatrix.
// RAGE pins its shader constants in every .fxc: c0..c3 gWorld (per draw), c4..c7 gWorldView, c8..c11 gWorldViewProj,
// c12..c15 gViewInverse, c64..c207 gBoneMtx[48], c128 NearFarPlane. c8 includes the world matrix and dividing it back out is
// ill-conditioned, so the camera is read from grcViewport and the constants give the per-draw world matrix.
// Verdict's three camera checks (a perspective proj, fov(proj) == mFov, viewport+0x100 == c8) gate nothing and feed a log=3
// line only. The published camera is guarded by NoteSceneC227's gates, the election and the 30-frame scene-key settle.
//
// Fail-safe: every pointer is probed before it is dereferenced, game memory is touched only inside SEH, and a failure sets
// `dead` and stops the mod, never the process.
//
// Build: build.ps1 (x86, GTA IV is a 32-bit process).

#include <windows.h>
#include <shlobj.h>          // SHGetFolderPathA: FusionFix's own cfg folders
#pragma comment(lib, "shell32.lib")
#include <d3d9.h>
#include <stdio.h>
#include <share.h>   // _SH_DENYNO: the log stays readable while the game runs
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <tlhelp32.h>
#include "transport/dlssiv_layout.h"   // DLSSIV_HOST_DIR: the host's folder, one definition shared with the host

// Declared here at the very top on purpose: in a release a declaration between the hot statics (.data$dlhot, below) can
// reorder them, so new functions are defined at the end of this file and only named here.
// The gate before DXVK's Present (transport/present_gate.h), for the two cold sites that install it:
static void* IvtGateWrap(void* present);
// The device thread's 5 s notice and warning (called by dt_core.h's DtWait, defined at the end of this file):
static void DtStuckSay(_Printf_format_string_ const char* fmt, ...);    // (%d seconds, %s the wait's reason)
static void DtStuckWarn(_Printf_format_string_ const char* fmt, ...);   // (%d threaded, %d seconds, %s the wait's reason)
#define DT_STUCK_SAY  DtStuckSay
#define DT_STUCK_WARN DtStuckWarn
// The latency waiter's wait on the in-fence (end of this file). A VkResult as an int and the semaphore as its 64-bit handle,
// because Vulkan's types are not declared yet here:
struct FeedVk;
static int LatWaitIn(FeedVk* vk, unsigned long long sem, unsigned long long m, unsigned timeout_ms);
// The on-screen notice (source/notice.h, included at the end of this file), named here for the same reason.
// ntcOff is notice=0 in the cfg (unlisted: for someone recording).
struct IDirect3DDevice9;
// The ladder's held frame rate (defined at this file's end). LatAdaptiveStep (DlssIvTransport.h) only calls these,
// because a declaration in that header, or a statement added to that function, moves the release's hot data (tools\check-hot-layout.py).
static bool adHeld = false;            // the last verdict on the same frame found the frame rate held from outside
static int  adHeldUp = 0;              // windows in a row whose rate rose over the held one
static void AdVerdictKept(const char* fmt, ...);
static bool AdOutOfBandHeld(float mP, bool lighterOnly);
// Tyre spin left out of the vectors (defined at this file's end): TrkOnDrawPre calls it on a tyre that moved.
static volatile long velWheelAxle = -1;      // the row of a tyre's matrix last seen clearly as the axle, -1 none yet
static int VelWheelNoSpin(const float* cur, float* prev, volatile long* axleSeen);
// The dented vehicle's redraw (defined at this file's end): VelRecordObject notes the game's damage with a mover, VelDrain binds
// it, draws the variant and puts the slot back.
struct VelPend;
#define VEL_DMG_SAMP_N 13   // D3DSAMP_ADDRESSU (1) to D3DSAMP_DMAPOFFSET (13): every sampler state the map's draw had, copied and put back
static void VelDmgNote(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, VelPend* e);
static IDirect3DVertexShader9* VelDmgBind(IDirect3DDevice9* dev, const VelPend* e, IDirect3DVertexShader9* plain);
static void VelDmgEnd(IDirect3DDevice9* dev);
static void VelDmgReport(LONG nFrames, double f);   // a development build's line (DLSSIV_DEV is not defined yet here)
static void NoticeFromWarn(const char* line);
static void IvtWarnHdrCameOn();      // the stale "HDR is not on" warning, taken back when HDR engages
static int  IvtFatalCause(const char* why);   // which of the did-not-start note's three causes a reason belongs to, 0 none
static void HdrCgSayUndecided(int why, const double* e, int n);   // the console gamma probe could not decide: said once per reason
static void WarnCloudFix10();        // CloudFixIV 1.0 is loaded: said once in the warnings file
static int  IvtFfWindowedAtStart();  // FusionFix's [MAIN] Windowed at start: 1 windowed, 0 exclusive fullscreen, -1 no FusionFix
static void CfgSayOff();             // a cfg switch that is off is said in the log
static void NoticeFatal(bool stopped, bool elsewhere);
static bool IvtXessHandshake(unsigned long long caps);   // XeSS at the handshake: upscaler=2 refused, or automatic fell to FSR
static void IvtXessFell(unsigned flags);                 // a refused build where automatic fell from XeSS to FSR
static void IvtBuildRefusedWhy(char* why, size_t cap, unsigned flags);   // the third refused build's stop, in words
static void NoticeFrame(IDirect3DDevice9* dev);
[[maybe_unused]] static bool NoticeDrewThisFrame();   // frame generation's hand-over (notice.h): a notice was drawn into the frame just ended. Unused on the release line.
static void NoticeWatchSecondCopy();
static void NoticeTellFirstCopy();
static bool ntcOff = false;
static void NoticeCfgOverride();

// Which source built this binary: build.ps1 defines it as the short commit, plus "-dirty" for uncommitted changes. Never a
// clock, so two builds of one source are byte identical (with /Brepro). The fallback is for a source archive with no git.
#ifndef IVT_BUILD_ID
#define IVT_BUILD_ID "unknown-source (built " __DATE__ " " __TIME__ ")"
#endif

// DLSSIV_FG_ARMS is gone: frame generation's product is always the armed one, and a build that still names the switch is refused.
// Declarations that cold call sites need go here, before the first hot variable, or at the end of this file.
#ifdef DLSSIV_FG_ARMS
#error "DLSSIV_FG_ARMS is gone (round 9's sweep): the product of frame generation is the armed one in every build"
#endif
#if (defined(DLSSIV_FG) && DLSSIV_FG) || (!defined(DLSSIV_FG) && (!defined(DLSSIV_DEV) || DLSSIV_DEV))
static void FgpBeforeBuild();                                                  // IvtTick, before a (re)build: a pair found armed fires in IvtBuild's own drain (fg_arm_life.h)
static void FgpOnResetDone(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* pp);   // hkReset, after a successful call: the conditions asked again
static void FgpInlineGuard();                                                  // IvtInject's two inline paths: the guard behind the helper's prepare
static void FgpHomeWaited(LONG n);                                             // IvtInjectBody, after the same-frame home's submit (fg_arm_life.h)
static bool FgpWantsRefresh();                                                 // HdrReadRefresh: frame generation needs the display's refresh at log=0 too
static void FgHudCapture(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps, IDirect3DBaseTexture9* frame, const float c[9][4], bool held, bool g10);   // HdrEncode, behind its draw: what the HUD-less pass draws with (fg/fg_hud.h)
static bool FgHudWantsCopy();                                                  // HdrEncode's tail and HdrApplyLiveCfg: the copy at the interface's boundary is frame generation's too
static void FgHudRelease();                                                    // HdrEncodeRelease, before a Reset: what the HUD-less pass kept and made
// THE HUD-LESS PICTURE IN SDR (fg/fg_hud.h): its three flags, set at EndScene by FgHudSdrFrame and read by the hooks below,
// and its entry points, declared here where they move no hot static.
static bool fgSdrHooks = false;      // this frame's back-buffer draws are asked (SDR, a HUD-less picture wanted, its target standing)
static bool fgSdrWatch = false;      // the session is SDR and the machine installed: another module's point copy of the back buffer is followed
static bool fgSdrCgaArmed = false;   // such a copy came inside the frame: its module's next draw into the back buffer is asked
struct DrawRec;
static void FgHudSdrOnDraw(const DrawRec& rec);                                // IvtOnDraw, behind HDR's block: a back-buffer draw
static void FgHudSdrCgaArm(IDirect3DDevice9* dev, void* ret, IDirect3DSurface9* dst);   // hkStretchRect: a point copy of the whole back buffer
static void FgHudSdrCgaDraw(IDirect3DDevice9* dev, void* ret, bool up, D3DPRIMITIVETYPE type, UINT start, UINT prims, const void* vtx, UINT stride);   // hkDrawPrim, hkDrawPrimUP
static void FgHudSdrFrame(IDirect3DDevice9* dev);                              // hkEndScene, behind HdrEncode: the frame's end
#include "devthread/dt_bbs_top.h"   // the back buffer written since the mark or not: the device thread's answer to the save (devthread/dt_core.h says why)
#endif
// Frame generation's product entry points (source/fg/fg_client.h, included at the end of this file), declared here where they
// move no hot static. The test spells out DLSSIV_FG's value because its #define is further down.
#if (defined(DLSSIV_FG) && DLSSIV_FG) || (!defined(DLSSIV_FG) && (!defined(DLSSIV_DEV) || DLSSIV_DEV))
static bool FgxParseKey(const char* k, const char* v);                         // IvtParseCfg, for a key it does not know
static bool FgxHelperArg();                                                    // IvtSpawnHost: " --fg=1" belongs on the helper's line
static void FgxArgDropped();                                                   // IvtSpawnHost: the flag did not fit and was left out
static void FgxOnCaps(unsigned long long caps);                                // IvtEnsureHost, after the hello ack
static bool FgxMarkerWanted();                                                 // LatWaiterStart: the frame-end marker under a limiter
static void FgxStandDown();                                                    // wherever ivtBuilt goes false, and IvtFail
static void FgxBeforeRelease();                                                // IvtBuild's head, before the main set's own release
static bool FgxAfterBuild(unsigned w, unsigned h, int bbVkFormat, unsigned rw, unsigned rh);   // IvtBuild's tail; false: the transport was stopped
static void FgxReleasePrepWait();                                              // IvtFail and IvtHostWatch: the queued wait on the helper's prepare, released
static bool FgxWatchLook();                                                    // IvtHostWatch, twice a second: a prepare that has stood still for 4 s
static void FgxWatchFire(unsigned long long want);                             // IvtHostWatch: the helper ended, both waits released, the stop asked
static void FgpStop(const char* why, bool atExit);                             // IvtFail and IvtShutdown: frame generation's machine closes (fg_product.h)
static void FgpJoin();                                                         // IvtFail, after its releases: frame generation's threads joined, never on the worker
static void FgpOnResetBarrier(const char* how);                                // hkReset, just before HdrEncodeRelease: the machine's wait before a Reset
static LONG FgpDtWindow();                                                     // DtReport: how many presents of the report's window were engaged
#endif

// Hot globals: every variable a hot hook reads on its common path at log=0 is declared DLSSIV_HOT, which packs them into one
// section so a draw hook touches few cache lines. Only a one-line declaration with no large array on it can be marked
// (tools/cpuprobe/hotsyms.py lists candidates from a capture).
#pragma section(".data$dlhot", read, write)
#define DLSSIV_HOT __declspec(allocate(".data$dlhot"))
// `dead`, `telemetryOn` and `trkWorkerLive` are read on every draw by the render thread and the device thread's tracker, and
// written only at start-up, a log change, an arm or a disarm. This section sorts after .data$dlhot and `dead` is 64-aligned, so
// their lines share nothing the render thread writes. Nothing else may be declared DLSSIV_SHARED.
#pragma section(".data$dlro", read, write)
#define DLSSIV_SHARED __declspec(allocate(".data$dlro"))
// The tracker's own line (m4dL) is the only object in .data$dlrx, which sorts next: it seals the end of .data$dlro.
#pragma section(".data$dlrx", read, write)
#define DLSSIV_TRKLINE __declspec(allocate(".data$dlrx"))
// Cold zero-initialised statics kept out of .bss: the compiler fills .bss alignment holes with small statics in an order no source
// line controls, so one new static can change the cache-line neighbours of hundreds. A zero-initialised static added for frame
// generation is marked DLSSIV_FGDATA (tools/prove-fg-off.py's .bss gate fails otherwise). The section sorts after the runtime's
// .data$r and .data$rs. Everything here is cold (written at load, CreateDevice or Reset).
#pragma section(".data$zfg", read, write)
#define DLSSIV_FGDATA __declspec(allocate(".data$zfg"))
// Which upscaler the helper built: 0 DLSS, 1 FSR, 2 XeSS, from the build ack's FEED_ACK_BACKEND_FSR and FEED_ACK_BACKEND_XESS
// (transport/feed_ipc.h). Written at the ack, read by the texts that name the upscaler; cold, in .data$zfg so nothing hot moves.
DLSSIV_FGDATA static int ivtBackendId;
// Automatic's fall from XeSS to FSR at a build (IvtXessHandshake, IvtXessFell): 1 the handshake said XeSS runs here
// (FEED_CAP_XESS), 2 the fall was said. Cold, in .data$zfg.
DLSSIV_FGDATA static int ivtXessFell;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

DLSSIV_SHARED __declspec(align(64)) static bool dead = false;   // one-way: a failure stops the mod
// Every report-only counter, timer, census and periodic report sits behind this, so none of it runs when the log is off.
// Latched in LogInit from the level (log=0 leaves it false). The guard itself is not telemetry.
DLSSIV_SHARED static bool telemetryOn = false;
static char  logPath[MAX_PATH] = { 0 };

struct Mat4 { float m[4][4]; };

// The last gWorldViewProj handed to a vertex shader, and whether there is one. Render thread only.
DLSSIV_HOT static Mat4  lastC8;
DLSSIV_HOT static bool  haveC8 = false;

// The engine's grcViewport* slot, not a cached object: the game swaps the current viewport many times a frame (scene, shadow
// cascades, mirror, water reflection), so it is dereferenced per read, as FusionFix does.
DLSSIV_HOT static BYTE** viewportSlot = nullptr;

// Pronounced exactly once. LONG because two threads race for it: the constant hook on the render thread, and the poll thread
// that pronounces even when the hook never fires.
DLSSIV_HOT static volatile LONG toldVerdict = 0;
static LONG  c8Writes  = 0;        // perspective c8 writes seen; development log only (above DEVSTAT, so a plain declaration a release never writes)
static LONG  c8Ortho   = 0;        // orthographic c8 writes seen (UI/HUD)
static DWORD armedAt  = 0;         // GetTickCount when the mod went live

// The velocity target's size, read by the camera gate (GATE 3); declared here because its readers come before its owner.
static UINT32 velW = 0, velH = 0;

#include <intrin.h>
// Set bits by Kernighan's loop, not __popcnt: POPCNT faults on a CPU without it, and this runs only a few times a session.
static unsigned BitCount(DWORD_PTR m) { unsigned n = 0; for (; m; m &= m - 1) n++; return n; }
static void IvtWarn(_Printf_format_string_ const char* fmt, ...);   // the warnings file a log=0 player sees; defined beside the did-not-start note
static void LatSayRefresh(bool atReset);   // the display's refresh, said and marked for the recorder; defined after DllMain
static void RgArm();                       // the refresh guard's arming; the guard and its arming are defined after DllMain
static UINT IvtDriverVsyncInterval(UINT asked, bool say);   // NVIDIA's forced vsync, asked of DXVK in the game's place; defined after DllMain
static int  IvtDxvkPinnedInterval();       // d3d9.presentInterval as dxvk.conf or DXVK_CONFIG sets it, or -1; defined after DllMain
static void LatReadLimiterCfg(bool again); // FusionFix's FPS limit or DXVK's maxFrameRate; defined beside the other latency readers
static bool latLimiterReread = false;      // InitThread read FusionFix's cfg before FusionFix was loaded: read again at the game's device
// NVIDIA's own frame rate cap for the game (see LatNvCapSeen): fps, 0 none or not read, and the profile it came from.
// Kept apart from latLimiter because LatReadLimiterCfg resets that at each of its two reads.
static unsigned latNvCapFps = 0;
static char     latNvCapNote[96] = "";     // the limiter's note for it, kept so the second read puts back the same words
// Two cold log readers, declared here and defined last: where a function is declared or defined in this file changes the layout
// of the DLSSIV_HOT globals, and here they move none. Check the hot layout again after adding one.
static void HdrReadRefresh(IDirect3DDevice9* dev, const char* where);                           // defined last in this file (see there)
static void LogGameSwapchain(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* pp, BOOL windowed, const char* where); // defined last (see there)
// ===========================================================================
// Strippable switches: every build switch is listed here so none is left behind. The defaults here are the shipping defaults.
//   DLSSIV_DEV               1 a development build, 0 the release a player gets.
//   DLSSIV_DEVLOG            follows DLSSIV_DEV: the development log (LogHealth at log=2, LogDetail at log=3) and its call
//                            sites; censusOn becomes false. At 0, log=1 is whole, and log=2 or 3 write a log=1 file and say so.
//   DLSSIV_FG                follows DLSSIV_DEV: frame generation's product code. `build.ps1 -Ship` sets it to 1 with
//                            DLSSIV_DEV at 0.
//   DLSSIV_M4D_INDEX_VERIFY  0: the dual-gather equivalence check (/DDLSSIV_M4D_INDEX_VERIFY=1 builds it; prove-all compiles it).
//   DTBBS(...)               follows DLSSIV_FG: whether the back buffer was written since the mark (devthread/dt_bbs.h). Every
//                            device-thread site is DTBBS(...) or DT_BBS_BESIDE(...) (dt_core.h, DT_BBS_ON); at 0 they are nothing.
//   DEVSTAT(...), RELSTAT(...)  follow DLSSIV_DEVLOG: the tokens in a development build (RELSTAT: in a release), nothing in the other.
//                            The harness defines DEVSTAT as its tokens, so it keeps the machine's counters (FGC_IF, fg_machine.h).
//   IVT_HDR_VIEW, IVT_MODE   follow DLSSIV_DEVLOG: the cfg's field, or the constant a release clamps it to (0 and 2).
//   DT_THREADED_TOP          follows DLSSIV_DEVLOG (dt_core.h): threaded's top, 2 or 1; at 1 dtValidate is the constant false.
// A new switch gets its row in the commit that adds it.
// ===========================================================================

#ifndef DLSSIV_DEV
#define DLSSIV_DEV 1
#endif
#ifndef DLSSIV_DEVLOG
#define DLSSIV_DEVLOG DLSSIV_DEV
#endif
#ifndef DLSSIV_FG
#define DLSSIV_FG DLSSIV_DEV
#endif
// The build's flavour, one word beside the build id in every file a player can send: "development", or "release" for every
// other build. Frame generation builds are told apart by the log's frame generation lines.
#if DLSSIV_DEV
#define DLSSIV_FLAVOUR "development"
#else
#define DLSSIV_FLAVOUR "release"
#endif
// DEVSTAT(x): a statement or declaration that only feeds the development log. Exactly `x` in a development build, token for
// token (so wrapping changes no byte), and nothing in a release. To wrap one safely, wrap the declaration first and build
// -Release: every remaining use is then a compile error, and each is read before it is wrapped.
#if DLSSIV_DEVLOG
#define DEVSTAT(...) __VA_ARGS__
#define RELSTAT(...)
#define IVT_HDR_VIEW ivtCfg.hdr_view
#define IVT_MODE ivtCfg.mode
#else
#define DEVSTAT(...)
#define RELSTAT(...) __VA_ARGS__
#define IVT_HDR_VIEW 0
#define IVT_MODE 2
#endif
// FGX(...): a call site of frame generation's product code inside a function a release compiles: the tokens with DLSSIV_FG,
// nothing without it. tools/check-fg-fence.py finds its must-find sites by this wrapper's name.
#if DLSSIV_FG
#define FGX(...) __VA_ARGS__
#else
#define FGX(...)
#endif
// FGA(...): a call site of the armed path, present in every build with DLSSIV_FG; tools/test-fg-arms lists each site by callee.
#if DLSSIV_FG
#define FGA(...) __VA_ARGS__
#else
#define FGA(...)
#endif
// Runs both gathers every scan and compares the candidate lists (log=3 only), so it is a verification build. A clean pass prints
// nothing, so its liveness shows only as cost (about twice the tracker's time a call). Build with it at 1 after touching
// M4dBuildIndex; prove-all compiles it at 1 so it cannot rot.
#ifndef DLSSIV_M4D_INDEX_VERIFY
#define DLSSIV_M4D_INDEX_VERIFY 0
#endif



// The render thread, captured at the first EndScene. Device hooks keep per-frame state for this one thread. A second thread on
// the same device (a presenter's StretchRect of the back buffer, say) would otherwise run the render thread's work, such as the
// injection, in the middle of the scene: any hook a second thread can reach hands it straight to the original.
static DWORD            renderTid     = 0;
// The game's main thread also calls the device while the menu or a loading screen is up (depth stencil, SetTexture, sampler
// states, resource creation), never during play. The device thread's log=1 line "disarmed (another thread's <method>)" names
// any such call. True before the first EndScene, while the render thread is still unknown.
static inline bool RenderThreadHere() { return !renderTid || GetCurrentThreadId() == renderTid; }

// ---------------------------------------------------------------------------
// Render scale
// ---------------------------------------------------------------------------
// The game's screen-sized render targets and depth-stencils are created at rsW x rsH; the back buffer stays at rsOutW x rsOutH.
// The game is told nothing: its sizes, viewports and HUD coordinates stay in screen pixels, and this layer maps them.
DLSSIV_HOT static bool  rsActive = false;
DLSSIV_HOT static UINT  rsW = 0, rsH = 0;            // the render size (the scene)
DLSSIV_HOT static UINT  rsOutW = 0, rsOutH = 0;      // the screen size (the backbuffer, the DLSS output)

// Is this DXVK? Latched in hkCreateDevice from a QueryInterface for ID3D9VkInteropDevice on the game's device, released at once,
// before RsOnScreenSize. It is the interface the transport requires (IvtBuild fails with "no ID3D9VkInteropDevice: not running on DXVK"),
// and there is no non-DXVK path. Starts true: the render scale is refused only after looking and finding nothing.
static bool  ivtDxvkOk = true;
// The mip LOD bias: below display resolution every sampler needs log2(render / output) of negative bias, or the detail the
// upscaler reconstructs is never sampled. Derived in LodBiasForScale, pushed in LodApplyAll, composed with the game's own in
// hkSetSamplerState.
static float lodBiasAuto   = 0.0f;   // log2(rsW / rsOutW); 0 at DLAA, and 0 is correct there
DLSSIV_HOT static float lodBias       = 0.0f;   // what is actually being pushed, after the cfg
static DWORD lodBiasBits   = 0;      // the same float's bits: the draw-time compare is exact
DLSSIV_HOT static bool  lodActive     = false;  // a non-zero bias is in force
static bool  lodEverSet    = false;  // a non-zero bias was pushed at least once, so 0 must be pushed to undo it
DEVSTAT(static UINT  lodPushes     = 0;)      // frames on which all sixteen stages were written (development log only)
DEVSTAT(static UINT  lodGameSets   = 0;)      // times the GAME asked for a bias of its own (development log only)
// One lock for the writers of the surface tables below (rsTab, fpTab, fpSc, fpScNo, rsIntz): the creation hook runs on whatever
// thread creates, and the render thread inserts and removes too. An insert fills the slot, then a barrier, then the count, so a
// reader sees an entry whole or not at all. Readers take nothing (RsFind runs thousands of times a frame). Taken only around
// inserts and removals. No RAII: FpSceneGet is inlined into the render-target hook, and a destructor would give it an exception frame.
static SRWLOCK rsTablesLock = SRWLOCK_INIT;
static int   rsOwnCreate = 0;             // > 0 while the mod creates a texture: never scaled
DLSSIV_HOT static int   rsOwn = 0;                   // > 0 while the mod drives the device: rectangles pass through
// These two counters belong to the thread that enters the hooks, never the worker: every hook that reads them opens with
// DT_NOT_ON_WORKER, so a guard taken on the worker protects nothing and would only switch off the render thread's whole
// render-scale layer while held (and leave targets created meanwhile unscaled for life). RsGuardHere() is true on every other
// thread. Defined below devthread.h, where dtWorkerTid appears.
static bool RsGuardHere();
struct RsOwnCall       { const bool mine; RsOwnCall()       : mine(RsGuardHere()) { if (mine) rsOwn++; }       ~RsOwnCall()       { if (mine) rsOwn--; } };
struct RsOwnCreateCall { const bool mine; RsOwnCreateCall() : mine(RsGuardHere()) { if (mine) rsOwnCreate++; } ~RsOwnCreateCall() { if (mine) rsOwnCreate--; } };
// The composite redirect: while on, every bind of the back buffer as render target 0 goes to the mod's render-size colour
// target, so the composite, the depth-only quad and the coronas land there.
DLSSIV_HOT static IDirect3DSurface9* rsRedirectSurf = nullptr;   // the transport owns the texture
DLSSIV_HOT static bool               rsRedirectOn   = false;
// Every scaled target and its mip surfaces. The game's textures are referenced because D3D9 reuses object addresses, and a stale
// pointer matching a new surface would scale the wrong viewport. The references go back in hkReset, before origReset, or the
// reset fails. The mod's own targets are not referenced (held = false).
struct RsSurf { IDirect3DBaseTexture9* tex; IDirect3DSurface9* surf; UINT origW, origH, newW, newH; bool held; };
static RsSurf rsTab[128];
static int    rsTabN = 0;
DEVSTAT(static LONG   rsScaledCreations = 0, rsTabOverflow = 0, rsVpScaled = 0, rsScissorScaled = 0, rsClearScaled = 0;)
DEVSTAT(static LONG   rsQuadSeen = 0, rsQuadFixed = 0, rsQuadUnread = 0;)   // the game's full-screen quads (quad_uv; RsQuadDraw)
// The seam. Several screen-size FP16 targets exist in one frame (the scene buffer and two post-chain ping-pong buffers), so size
// and format cannot identify the capture source: the scene buffer is whichever FP16 target the scene drew into. Every FP16
// render target is paired here at creation so a bound target can be matched back to the texture a shader samples.
struct FpRt { IDirect3DBaseTexture9* tex; IDirect3DSurface9* surf; UINT w, h, levels; };
// Referenced, one per entry, and returned in hkReset before origReset, as in rsTab: the entries are keyed only on the address and
// decide where the resolve reads from, and the reference keeps the address from being reused while the entry lives. Surfaces stay
// raw (a level lives as long as its texture). The release loop must stay before origReset (a held default-pool reference makes
// Reset fail); hkCreateDevice has the matching loop for a device that is destroyed.
static FpRt   fpTab[24]; static int fpTabN = 0;
// Design A: the scene rasterises at the render size and the post chain at the output size, on the same FP16 buffers. The game's
// FP16 targets stay at the size it asked for, so the post chain runs on its own buffers, viewports and constants. The scene
// renders into a render-size counterpart, and at the seam the upscaler resolves that into the game's buffer in place; nothing
// after the seam is touched. (Giving the post chain the mod's buffers instead needs every consumer of a buffer's size found and
// fixed, an open-ended set.) Counterparts are made lazily on first bind, so there is no start-up race.
struct FpScene
{
    IDirect3DBaseTexture9* gameTex;    // the game's, at the OUTPUT size
    IDirect3DSurface9*     gameSurf;
    IDirect3DBaseTexture9* ourTex;     // the mod's, at the render size: what the scene draws into
    IDirect3DSurface9*     ourSurf;
    UINT w, h;                         // the counterpart's
    long wroteOurs;                    // the frame the counterpart last took a write: a scene-phase target bind or blit
    long wroteOrig;                    // the frame the game's original last took a write AFTER the seam; -1 = never
};
static FpScene fpSc[8];
DLSSIV_HOT static int     fpScN = 0;
// StretchRect copies involving the mod's own surfaces (development log). The counterpart is also in rsTab, the table the
// read-back redirect searches.
DEVSTAT(static LONG fpScBlitOurs = 0, fpScBlitLogged = 0, fpScRbHitOurs = 0;)
static LONG fpScBridgeFailed = 0;        // not DEVSTAT: it caps the log=1 line "the bridge blit was REFUSED" at four
// The blit substitution: blits redirected to or from a counterpart, and those skipped because a rectangle would not map.
DEVSTAT(static LONG fpScBlitDstSubs = 0, fpScBlitSrcSubs = 0, fpScBlitSkipped = 0;)
// ColorFill: its redirect and two one-shot log=1 lines stay as insurance; the game has never been seen to call it.
// The game's buffer that the scene's counterpart stands in for: the resolve reads the counterpart and lands in this.
static IDirect3DBaseTexture9* sceneDstTex = nullptr;
static inline FpScene* FpSceneForSurf(IDirect3DSurface9* v)
{ for (int i = 0; i < fpScN; i++) if (fpSc[i].gameSurf == v) return &fpSc[i]; return nullptr; }
static inline FpScene* FpSceneForTex(IDirect3DBaseTexture9* v)
{ for (int i = 0; i < fpScN; i++) if (fpSc[i].gameTex  == v) return &fpSc[i]; return nullptr; }
static inline FpScene* FpSceneForOurSurf(IDirect3DSurface9* v)
{ for (int i = 0; i < fpScN; i++) if (fpSc[i].ourSurf  == v) return &fpSc[i]; return nullptr; }
// Surfaces already asked and rejected, so the desc query does not run on every render-target bind. Referenced like fpTab and
// rsTab: a released address reused by a new FP16 buffer would otherwise read as rejected and get no counterpart for the session.
// Keying on the desc would need the very query the cache avoids. At most 64, released in FpSceneRelease before origReset.
static IDirect3DSurface9* fpScNo[64];
static int  fpScNoN = 0;
// A counterpart that could not be created is tried again: a CreateTexture refusal is momentary memory pressure, not a property of
// the surface. After five refusals 300 frames apart the surface is rejected. Both reset with the device.
static LONG fpScCreateFails = 0;
static LONG fpScRetryFrame  = -1;
static inline bool FpSceneRejected(IDirect3DSurface9* v)
{ for (int i = 0; i < fpScNoN; i++) if (fpScNo[i] == v) return true; return false; }
static inline void FpSceneReject(IDirect3DSurface9* v)
{
    if (!v) return;
    AcquireSRWLockExclusive(&rsTablesLock);
    const int n = fpScNoN;
    if (n < 64) { v->AddRef(); fpScNo[n] = v; _ReadWriteBarrier(); fpScNoN = n + 1; }
    ReleaseSRWLockExclusive(&rsTablesLock);
}
// Everything held for the seam, let go before a reset. ourSurf and ourTex are the mod's; gameTex is referenced because gameSurf
// and gameTex are the lookup keys; fpScNo is the rejection cache. gameSurf stays raw: it is a level of gameTex.
static void FpSceneRelease()
{
    AcquireSRWLockExclusive(&rsTablesLock);
    for (int i = 0; i < fpScN; i++)
    {
        if (fpSc[i].ourSurf) fpSc[i].ourSurf->Release();
        if (fpSc[i].ourTex)  fpSc[i].ourTex->Release();
        if (fpSc[i].gameTex) fpSc[i].gameTex->Release();
    }
    for (int i = 0; i < fpScNoN; i++)
        if (fpScNo[i]) fpScNo[i]->Release();
    fpScN = 0; fpScNoN = 0; sceneDstTex = nullptr;
    fpScCreateFails = 0; fpScRetryFrame = -1;
    ReleaseSRWLockExclusive(&rsTablesLock);
}
DLSSIV_HOT static IDirect3DBaseTexture9* sceneBufTex = nullptr;
DLSSIV_HOT static IDirect3DSurface9*     sceneBufSurf = nullptr;
DEVSTAT(static LONG   seamLogged = 0;)
DLSSIV_HOT static bool   seamThisFrame = false;
DEVSTAT(static int    seamDrawIdx = 0, seamBbSeen = 0;)
DEVSTAT(static int    seamDrawsSeen = 0;)
// D3D9 bindings persist across frames, so the next frame's first draw trivially samples the scene buffer while not rendering
// into it. The seam also requires that the scene wrote the buffer this frame, which makes it a phase change.
DLSSIV_HOT static bool   seamSceneWrote = false;
DLSSIV_HOT static bool   seamArmed = false;   // the seam fired; inject on the next draw

// The chevron. Design A leaves the game's depth-stencil at the render size. After the resolve the game draws into its
// output-size buffer while still binding that depth, and DXVK refuses to bind a depth-stencil smaller than render target 0
// (d3d9_device.cpp, BindFramebuffer), so those draws lose their depth test (the map chevron and the coronas drew through walls).
// Repaired per draw, not over a span of the frame: an output-size depth is bound for that one draw and the game's is back before
// the call returns. A span would hand draws that were not broken a depth holding content meant for something else.
// The test is the defect restated: an output-size target, a depth the mod scaled, and a draw that tests depth.
#ifndef D3DFMT_INTZ
#define D3DFMT_INTZ ((D3DFORMAT)MAKEFOURCC('I','N','T','Z'))
#endif
static IDirect3DSurface9* chvDepth = nullptr;   // the mod's, output size, never in rsTab
// The lent depth as a texture: the game's overlay phase also reads depth back through a sampler (a colour-write-disabled quad
// restores scene depth, then the phone's composite samples it). INTZ is bindable as a depth-stencil and sampleable, so one
// allocation serves both; it replaces the D24S8 surface.
static IDirect3DTexture9*     chvDepthTex = nullptr;
// The game's screen-size depth textures that the render scale shrank (the game makes two or three). A read of one during the
// overlay phase wants the lent depth instead.
static IDirect3DBaseTexture9* rsIntz[8] = {};
static int                    rsIntzN = 0;
static IDirect3DSurface9* chvSaved = nullptr;   // the game's own, for the length of one draw
static LONG               chvSavedSeq = 0;      // ds_bracket.h: another thread's bind inside that draw goes back instead
static bool chvTried  = false;
static bool chvFilled = false;   // cleared yet this frame
static float chvFilledFar = -1.0f; // the value it was cleared to (the compare decides which end is far)
// The lent-depth clear must cover the whole target: D3D9 clears the viewport intersected with the scissor, and chvFilled latches
// for the frame. ChvBefore clears with the full target as viewport and the scissor test off whenever the borrowing draw's own
// state is narrower, and puts both back (rsDxvkVp, which hkSetViewport writes even on the mod's own calls). Usually the
// full-screen depth-restore quad comes first and nothing is set.
static bool chvDsScaled = false; // the bound depth is one the mod scaled: computed at the bind, not per draw
DLSSIV_HOT static bool chvArmed  = false;   // the whole precondition, hoisted out of the draw path
// The overlay phase has opened this frame: latched, not re-probed, and cleared with chvFilled at the frame boundary.
static bool chvOverlayPhase = false;
// The overlay viewports whose depth was already cleared this frame: four distinct rects before the ring recycles (about 1.2
// clears a frame are seen).
struct ChvVp { DWORD x, y, w, h; };
static ChvVp chvVpCleared[4] = {};
static int   chvVpClearedN = 0;
// Why the depth is lent, and how. At a render scale the scene depth is render-size and the back buffer output-size; a Vulkan
// framebuffer's render area is its smallest attachment, so a back-buffer draw with the scene depth is cut to the top-left, and
// the game draws its phone in the bottom-right.
// The overlay phase is latched on its first sighting each frame (GetTexture returns what is bound, not what a draw uses, and
// bindings persist), and the candidates split by the draw's own viewport:
//   full-target viewport: a 2D composite (radar, HUD); the depth attachment is withheld.
//   partial viewport: a 3D model in a corner (the phone) that must occlude itself: it gets an output-size, writable depth,
//   cleared to far once per rect per frame. Clear obeys the game's viewport, so the restored world depth stays intact elsewhere.
// No phone's dimensions are known here: the rect comes from each draw's viewport. The clear changes nothing for a draw that does
// not overlap itself (a far-cleared buffer under LESSEQUAL passes its first fragment everywhere).



// Post-resolve sharpening: RCAS after the game's tonemap, before the HUD.
// shpBuilt is the build gate, latched at start-up: at sharpness 0 the shader and the scratch target are never made. shpOn is the
// run gate the live tick moves; sharpness is live within a session that built the pass, and 0 set live keeps the memory until a
// relaunch.
// A D3D9 pass, not part of the resolve: a sharpener belongs after the tonemap on a perceptual image (NVIDIA's NIS guidance), and
// the 64-bit host never sees that image. It runs at the first orthographic back-buffer draw after the seam: the frame's own
// boundary between the 3D world and the overlay.
static IDirect3DPixelShader9* shpPS   = nullptr;
static IDirect3DTexture9*     shpTex  = nullptr;   // the copy the shader reads
static IDirect3DSurface9*     shpSurf = nullptr;   // its level 0, for the blit
static bool  shpBuilt = false;    // the build gate: sharpness > 0 at start-up, latched, never moves
DLSSIV_HOT static bool  shpOn    = false;    // the run gate: shpBuilt and the live strength above zero
static bool  shpTried = false;
DLSSIV_HOT static bool  shpDone  = false;    // already sharpened this frame
DLSSIV_HOT static bool  shpBbSeen = false;   // a back-buffer draw has landed since the seam, so the composite is in (hkStretchRect reads it)
static float shpCon   = 0.0f;     // exp2(-stops), AMD's attenuation constant
// On a frame this is set the three ShpSharpen sites do not run, and the HDR encode sharpens at its own sample instead, HUD
// included (hdr_encode.h, shaders/hdr_encode_ps.hlsl under HDR_G10). Latched once a frame right after the encode in hkEndScene,
// so the sharpen sites and the encode of one frame agree.
static bool shpG10 = false;
// And only on a frame with a seam, as for the pass: every ShpSharpen site requires seamThisFrame while the encode runs on every
// HDR frame (title, loading and pause screens too). Taken at the top of hkEndScene, beside shpOwed, before the frame-end chain
// puts the seam down.
static bool shpG10Seam = false;
#define SHP_G10() shpG10
static D3DFORMAT shpFmtUsed = D3DFMT_UNKNOWN;   // the scratch's format, which is the back buffer's: only FP16 can hold a value above white
DEVSTAT(static LONG  shpPasses = 0, shpFailed = 0;)      // read by one development-log line
// Defined after the transport include, which owns the full-screen pass machinery.
static void ShpSharpen(IDirect3DDevice9* dev);
// Where the pass ran, and how many frames owed one: shpSeamFrames is the denominator a pass count needs.
DEVSTAT(static LONG shpBackstops = 0, shpAtCopy = 0, shpSeamFrames = 0;)

// What consumes the screen-size capture handed back (development log).
DEVSTAT(static LONG   rsTexSubLogged = 0;)
DEVSTAT(static LONG   rsStaleBinds = 0, rsStaleBindLogged = 0;)
// What is bound at stage 0: 0 nothing recognised, 1 the mod's screen-size capture of this frame, 2 the game's own copy, which
// still holds a render-size image. Separates the game repainting the whole screen from an ordinary HUD sprite (also two opaque
// triangles).
DLSSIV_HOT static int    rsScreenTexKind = 0;
static bool   rsOwnRegisterFailed = false;
static void LogRecord(_Printf_format_string_ const char* fmt, ...);   // defined with the log, below: RsRegisterOwn says a full table once
// RsFind(curRT0) is cached at hkSetRenderTarget, the only place curRT0 changes. Scalars, not a pointer: RsForget compacts the
// table with a swap, so a cached RsSurf* could come to describe a different surface.
DEVSTAT(DLSSIV_HOT static int   ivtDrawIndexForLog = 0;)   // mirrored from the transport; its one reader is a log=3 line
DLSSIV_HOT static bool  curRT0Scaled = false;
static RsSurf* RsFind(IDirect3DSurface9* surf)
{
    if (!surf) return nullptr;
    for (int i = 0; i < rsTabN; i++) if (rsTab[i].surf == surf) return &rsTab[i];
    return nullptr;
}
// A target of the mod's that stands in for a screen-sized one (the redirect's colour target), registered so the game's
// viewports on it are scaled too.
static void RsRegisterOwn(IDirect3DBaseTexture9* tex, IDirect3DSurface9* surf, UINT origW, UINT origH, UINT newW, UINT newH)
{
    if (!tex || !surf) return;
        // A full table means the game's viewports on the mod's colour target are not scaled and the scene crops.
    AcquireSRWLockExclusive(&rsTablesLock);
    const int n = rsTabN;
    if (n >= 128)
    {
                // Said once in the player's log, after the lock is given back.
        const bool first = !rsOwnRegisterFailed;
        rsOwnRegisterFailed = true; ReleaseSRWLockExclusive(&rsTablesLock);
        if (first) LogRecord("  render scale: the scaled-surface table is FULL (128): the mod's colour target is not in it, the scene draws cropped");
        return;
    }
    RsSurf& e = rsTab[n];
    e.tex = tex; e.surf = surf; e.origW = origW; e.origH = origH; e.newW = newW; e.newH = newH; e.held = false;
    _ReadWriteBarrier(); rsTabN = n + 1;        // the count LAST: see rsTablesLock
    ReleaseSRWLockExclusive(&rsTablesLock);
    // No invalidation needed: this surface was created moments ago and is
    // bound immediately afterwards, and that bind fills the cache.
}
static void RsForget(IDirect3DBaseTexture9* tex)
{
    AcquireSRWLockExclusive(&rsTablesLock);      // against an insert from a creating thread; readers are this thread's own
    for (int i = 0; i < rsTabN; )
    {
        if (rsTab[i].tex == tex) { if (rsTab[i].held) rsTab[i].tex->Release(); rsTab[i] = rsTab[--rsTabN]; }
        else i++;
    }
    ReleaseSRWLockExclusive(&rsTablesLock);
        // The removed entry may be the cached one, so invalidate. The next render-target bind fills it again; until then nothing is
    // scaled, the safe direction.
    curRT0Scaled = false;
}
// The screen-size shader constants. The game uploads its screen size as constants and its shaders build screen-space UVs and tap
// offsets from them; at a render scale every such pass would misregister by the scale ratio (deferred lighting reads the G-buffer
// this way). The predicate demands two adjacent floats whose ratio is the output aspect and which are both large or both tiny, so
// a lone gAspectRatio or a 3840-valued fog distance is rejected.
// The anchors depend only on the output size, so they are built once: divisions per upload are too costly on this path.
DLSSIV_HOT static float rsAnchW[6], rsAnchH[6], rsAnchRW[6], rsAnchRH[6];
DLSSIV_HOT static int   rsAnchN = 0;
DLSSIV_HOT static float rsAnchLo = 0.0f, rsAnchHi = 0.0f;
// The scan's broadcasts, built with the anchor table (a handful of times a session) instead of on every call. RsAnchorVecs is
// called from every place that assigns an anchor or a dead zone, and nowhere else.
DLSSIV_HOT static __m128 rsVLo, rsVHi, rsVDLo, rsVDHi;
// The render ladder: values already at the render size, or a power-of-two division of it. Never scaled a second time (see
// RsPairKind).
DLSSIV_HOT static float rsRenW[6], rsRenH[6], rsRenRW[6], rsRenRH[6];
DLSSIV_HOT static int   rsRenN = 0;
// The dead zone between the reciprocal cluster and the size cluster (see RsPairKind). Zero means not built.
DLSSIV_HOT static float rsDeadLo = 0.0f, rsDeadHi = 0.0f;
// Rebuild the hoisted broadcasts. Called from every assignment to the four floats
// above; there is no other way for them to change, so the vectors cannot go stale.
static void RsAnchorVecs()
{
    rsVLo  = _mm_set1_ps(rsAnchLo);
    rsVHi  = _mm_set1_ps(rsAnchHi);
    const bool dz = rsDeadHi > 0.0f;               // no zone built: a zone nothing is inside
    rsVDLo = _mm_set1_ps(dz ? rsDeadLo : 0.0f);
    rsVDHi = _mm_set1_ps(dz ? rsDeadHi : 0.0f);
}
// The ladders' mantissas, for RsMantNear: halving or doubling leaves a float's mantissa alone, so the four ladders carry at most
// four distinct mantissas. Built from every entry and deduplicated anyway; more than eight turns the filter off (rsMantN = -1).
DLSSIV_HOT static unsigned rsMant[8];
DLSSIV_HOT static int      rsMantN = -1;
// The output ladder halves exactly; the game's half-size chains truncate, so from the first rung that is not whole a game constant
// carrying the truncated size misses the anchor by more than the tolerance and is left alone.
static void RsBuildAnchors()
{
    rsAnchN = 0; rsAnchLo = 0.0f; rsAnchHi = 0.0f; rsMantN = -1;
    RsAnchorVecs();                                // the early return below leaves them zeroed, correctly
    if (!rsOutW || !rsOutH) return;
    float lo = 3.4e38f, hi = 0.0f;
    for (int k = 0; k < 6; k++)
    {
        const double w = (double)rsOutW / (double)(1 << k);
        const double h = (double)rsOutH / (double)(1 << k);
        if (w < 4.0 || h < 4.0) break;
        rsAnchW[rsAnchN]  = (float)w;       rsAnchH[rsAnchN]  = (float)h;
        rsAnchRW[rsAnchN] = (float)(1.0/w); rsAnchRH[rsAnchN] = (float)(1.0/h);
        if (rsAnchRW[rsAnchN] < lo) lo = rsAnchRW[rsAnchN];
        if (rsAnchW[rsAnchN]  > hi) hi = rsAnchW[rsAnchN];
        rsAnchN++;
    }
    rsAnchLo = lo * 0.99f; rsAnchHi = hi * 1.01f;
    RsAnchorVecs();

        // The render ladder, values already correct: FusionFix's screen-space passes read the grcViewport this layer has scaled, so a
    // constant like the SSAO's c44 arrives at the render size. At Performance (ratio 2.0) the render and output ladders share
    // values and no test on the value can tell them apart, so this ladder is consulted first. At other modes the ladders do not
    // intersect and it refuses nothing.
    rsRenN = 0;
    if (rsW && rsH)
    {
        for (int k = 0; k < 6; k++)
        {
            const double w = (double)rsW / (double)(1 << k);
            const double h = (double)rsH / (double)(1 << k);
            if (w < 4.0 || h < 4.0) break;
            rsRenW[rsRenN]  = (float)w;       rsRenH[rsRenN]  = (float)h;
            rsRenRW[rsRenN] = (float)(1.0/w); rsRenRH[rsRenN] = (float)(1.0/h);
            rsRenN++;
        }
    }

        // The dead zone, built once from the ladders: everything RsPairKind can match on `a` is a reciprocal (tiny) or a size (large), so
    // a value in the gap matches nothing. Only the W and RW arrays: `a` meets only those, `b` meets the height ladders.
    // The match is fabsf(a - X) <= X*1e-3, so the margins 1.002 and 0.998 exclude nothing that could match. The ladders stop at
    // w < 4, so the gap always exists; the else branch is unreachable and kept for safety.
    {
        float hiRecip = 0.0f, loSize = 3.4e38f;
        for (int k = 0; k < rsAnchN; k++)
        { if (rsAnchRW[k] > hiRecip) hiRecip = rsAnchRW[k];
          if (rsAnchW[k]  < loSize)  loSize  = rsAnchW[k]; }
        for (int k = 0; k < rsRenN; k++)
        { if (rsRenRW[k] > hiRecip) hiRecip = rsRenRW[k];
          if (rsRenW[k]  < loSize)  loSize  = rsRenW[k]; }
        const float dLo = hiRecip * 1.002f, dHi = loSize * 0.998f;
        if (hiRecip > 0.0f && loSize < 3.4e38f && dLo < dHi) { rsDeadLo = dLo; rsDeadHi = dHi; }
        else                                                 { rsDeadLo = rsDeadHi = 0.0f; }
        RsAnchorVecs();
    }

        // The mantissa filter's table (see rsMant and RsMantNear).
    {
        rsMantN = 0;
        const float* fam[4] = { rsAnchW, rsAnchRW, rsRenW, rsRenRW };
        const int    cnt[4] = { rsAnchN, rsAnchN, rsRenN, rsRenN };
        for (int f = 0; f < 4 && rsMantN >= 0; f++)
            for (int k = 0; k < cnt[f] && rsMantN >= 0; k++)
            {
                unsigned u;
                memcpy(&u, &fam[f][k], sizeof(u));
                const unsigned m = u & 0x7FFFFFu;
                bool have = false;
                for (int i = 0; i < rsMantN; i++) if (rsMant[i] == m) { have = true; break; }
                if (have) continue;
                if (rsMantN >= 8) { rsMantN = -1; break; }
                rsMant[rsMantN++] = m;
            }
    }
}

// The ladder half: one copy of the walk shared by both callers, since two copies of a matching rule drift.
// Precondition, which every caller establishes: a > rsAnchLo && a <= rsAnchHi && b > 0 (RsPairKind in scalar, RsFixNeededRows in SSE).
static __declspec(noinline) int RsPairLadderWalk(float a, float b)
{
        // The outer range compare rejects only about 78 percent: its bounds span six to seven orders of magnitude, so most positive
    // constants reach the walk. Hence the dead zone and the single-cluster test below.

        // The dead zone: exact, not a heuristic (RsBuildAnchors has the derivation).
    if (rsDeadHi > 0.0f)
    {
        if (a > rsDeadLo && a < rsDeadHi) return 0;
                // Then only one cluster can match: below the gap `a` is smaller than every size, above it larger than every reciprocal.
        // The verdicts are unchanged: the render ladder 0, an output-ladder size 1, a reciprocal 2.
        if (a < rsDeadLo)
        {
            for (int k = 0; k < rsRenN; k++)
                if (fabsf(a - rsRenRW[k]) <= rsRenRW[k] * 1e-3f
                 && fabsf(b - rsRenRH[k]) <= rsRenRH[k] * 1e-3f) return 0;
            for (int k = 0; k < rsAnchN; k++)
                if (fabsf(a - rsAnchRW[k]) <= rsAnchRW[k] * 1e-3f
                 && fabsf(b - rsAnchRH[k]) <= rsAnchRH[k] * 1e-3f) return 2;
            return 0;
        }
        for (int k = 0; k < rsRenN; k++)
            if (fabsf(a - rsRenW[k]) <= rsRenW[k] * 1e-3f
             && fabsf(b - rsRenH[k]) <= rsRenH[k] * 1e-3f) return 0;
        for (int k = 0; k < rsAnchN; k++)
            if (fabsf(a - rsAnchW[k]) <= rsAnchW[k] * 1e-3f
             && fabsf(b - rsAnchH[k]) <= rsAnchH[k] * 1e-3f) return 1;
        return 0;
    }
        // Unreachable by the proof in RsBuildAnchors; kept as the fallback for a dead zone that failed to build.
    // Already at the render size? Then it is correct and left alone. Checked before the output ladder: where the two overlap the safe
    // reading is already correct, since scaling a correct constant breaks a screen-space effect.
    for (int k = 0; k < rsRenN; k++)
    {
        if (fabsf(a - rsRenW[k])  <= rsRenW[k]  * 1e-3f
         && fabsf(b - rsRenH[k])  <= rsRenH[k]  * 1e-3f) return 0;
        if (fabsf(a - rsRenRW[k]) <= rsRenRW[k] * 1e-3f
         && fabsf(b - rsRenRH[k]) <= rsRenRH[k] * 1e-3f) return 0;
    }
    for (int k = 0; k < rsAnchN; k++)
    {
        if (fabsf(a - rsAnchW[k])  <= rsAnchW[k]  * 1e-3f
         && fabsf(b - rsAnchH[k])  <= rsAnchH[k]  * 1e-3f) return 1;
        if (fabsf(a - rsAnchRW[k]) <= rsAnchRW[k] * 1e-3f
         && fabsf(b - rsAnchRH[k]) <= rsAnchRH[k] * 1e-3f) return 2;
    }
    return 0;
}

// The mantissa filter, exact because it only ever says cannot match: a match needs `a` within 0.1 percent of a ladder entry, and
// two floats that close have mantissa fields within 16,778 of each other round the circle (a value just past a power of two
// wraps). The window, 2^15, is twice that. A value inside a window goes to the walk, which decides as before.
static const unsigned kRsMantWin = 1u << 15;
static __forceinline bool RsMantNear(float a)
{
    if (rsMantN < 0) return true;             // no table: let the walk decide
    unsigned u;
    memcpy(&u, &a, sizeof(u));
    const unsigned m = u & 0x7FFFFFu;
    for (int i = 0; i < rsMantN; i++)
        if (((m - rsMant[i] + kRsMantWin) & 0x7FFFFFu) <= 2u * kRsMantWin) return true;
    return false;
}

// The front of the ladder, inlined into the scan: the two cheap refusals (the dead zone and the mantissa filter) run in the
// caller, and everything else goes to the walk. Same answer for every pair.
static __forceinline int RsPairLadder(float a, float b)
{
    if (rsDeadHi > 0.0f && a > rsDeadLo && a < rsDeadHi) return 0;
    if (!RsMantNear(a)) return 0;
    return RsPairLadderWalk(a, b);
}

// The gate and the ladder together, for RsFixConstBlockRows on the few uploads a frame that are rewritten.
static inline int RsPairKind(float a, float b)
{
    if (!(a > rsAnchLo) || a > rsAnchHi || !(b > 0.0f)) return 0;
    return RsPairLadder(a, b);
}

DEVSTAT(static LONG rsConstFixedVs = 0, rsConstFixedPs = 0;)

// ===== The screen-size registers ================================================
// rage binds every shader variable to a fixed register, the same in every shader (gWorld c0, gWorldViewProj c8), so a screen
// size can only arrive in the registers of the variables that hold one, and only those rows are scanned.
// The list comes from tools/shaders/fxc_regs.py over every live .fxc (update/common/shaders over common/):
//   vertex  globalScreenSize c44, gooDeferredLightScreenSize c208
//   pixel   globalScreenSize c44, TexelSize c64 c66 c72 c76, currentResolution c66, gInvScreenSize c129,
//           gooDeferredLightScreenSize c66 c72 c75 c77 c78 c79 c84 c86
// FusionFix's embedded shaders put PixelOffset, a half-pixel offset, at vertex c5 (tools/shaders/ctab_bin.py), so c5 is listed.
// Its runtime-compiled effects (ambient occlusion, SMAA, FXAA, sun shafts) get compiler-picked pixel registers no file can name:
// RsHealMaybe, below, scans one gated pixel upload in sixteen on its unlisted rows, and a match promotes that register into the
// live mask, rewrites the block whole and says so in the log.
// Listed vertex registers are still tested by value, since other variables share them in other shaders. A vertex pair that
// matches the ladders in any other register is not rewritten: the only such matches seen (c7.xy, c11.xy) were coincidences.
struct RsRegMask { unsigned w[8]; };                   // one bit a register, c0..c255
static constexpr RsRegMask RsMakeMask(const unsigned short* r, int n)
{
    RsRegMask m = {};
    for (int i = 0; i < n; i++) m.w[(r[i] >> 5) & 7] |= 1u << (r[i] & 31);
    return m;
}
static constexpr unsigned short kRsRegsVS[] = { 5, 44, 208 };                  // c5: FusionFix's PixelOffset
// c7 and c11 are in no shader's variable table: they are the two registers the field census saw the whole-block pixel scan
// rewriting outside the list, listed so the narrowed scan rewrites exactly what the whole one did.
static constexpr unsigned short kRsRegsPS[] = { 7, 11, 44, 64, 66, 72, 75, 76, 77, 78, 79, 84, 86, 129 };
DLSSIV_HOT static RsRegMask rsScanVS = RsMakeMask(kRsRegsVS, (int)_countof(kRsRegsVS));
DLSSIV_HOT static RsRegMask rsScanPS = RsMakeMask(kRsRegsPS, (int)_countof(kRsRegsPS));

// Bit r set when register start + r is in the list, for an upload of `count`
// registers from `start`. count is 1..16, which both callers' gates guarantee.
static __forceinline unsigned RsScanRows(const RsRegMask& m, UINT start, UINT count)
{
    const UINT w = start >> 5, s = start & 31;
    if (w > 7) return 0;
    const unsigned lo = m.w[w], hi = (w < 7) ? m.w[w + 1] : 0u;
    const unsigned bits = s ? ((lo >> s) | (hi << (32 - s))) : lo;
    return bits & ((1u << count) - 1u);
}

// The scan on the listed rows only: each row goes through the same compares, paired with a zero row that adds no bit, so this is
// the whole-block scan on a copy whose other rows are zero (tools/perfproof/equiv_batch5.cpp). It reads no row it was not given.
static __declspec(noinline) bool RsFixNeededRows(const float* src, unsigned rows)
{
    const __m128 vLo = rsVLo, vHi = rsVHi, vDLo = rsVDLo, vDHi = rsVDHi;   // hoisted; see RsAnchorVecs
    const __m128 vZero = _mm_setzero_ps();
    unsigned cand = 0;
    for (unsigned rr = rows; rr; rr &= rr - 1)
    {
        unsigned long r;
        _BitScanForward(&r, rr);
        const __m128 v0 = _mm_loadu_ps(src + r * 4);
        const __m128 a  = _mm_shuffle_ps(v0, vZero, _MM_SHUFFLE(2, 0, 2, 0));   // x z 0 0
        const __m128 b  = _mm_shuffle_ps(v0, vZero, _MM_SHUFFLE(3, 1, 3, 1));   // y w 0 0
        const __m128 inRange = _mm_and_ps(_mm_cmpgt_ps(a, vLo), _mm_cmple_ps(a, vHi));
        const __m128 inDead  = _mm_and_ps(_mm_cmpgt_ps(a, vDLo), _mm_cmplt_ps(a, vDHi));
        const __m128 ok      = _mm_and_ps(_mm_andnot_ps(inDead, inRange), _mm_cmpgt_ps(b, vZero));
        cand |= (unsigned)_mm_movemask_ps(ok) << (2 * r);
    }
    while (cand)
    {
        unsigned long i;
        _BitScanForward(&i, cand);
        cand &= cand - 1;
        const float* pair = src + (i >> 1) * 4 + (i & 1) * 2;
        if (RsPairLadder(pair[0], pair[1])) return true;
    }
    return false;
}

// The rewrite on the listed rows only; every other row of dst is left as the caller copied it.
static void RsFixConstBlockRows(float* dst, const float* src, unsigned rows)
{
    if (!rsW || !rsH || !rsOutW || !rsOutH) return;
    const double sx = (double)rsW / (double)rsOutW, sy = (double)rsH / (double)rsOutH;
    for (unsigned rr = rows; rr; rr &= rr - 1)
    {
        unsigned long r;
        _BitScanForward(&r, rr);
        const float* v = src + r * 4;
        float* o = dst + r * 4;
        for (int p = 0; p < 4; p += 2)          // (x,y) then (z,w)
        {
            const int k = RsPairKind(v[p], v[p + 1]);
            if (!k) continue;
            if (k == 1) { o[p] = (float)(v[p] * sx); o[p + 1] = (float)(v[p + 1] * sy); }
            else        { o[p] = (float)(v[p] / sx); o[p + 1] = (float)(v[p + 1] / sy); }
        }
    }
}
// ===== end the screen-size registers ============================================


static inline void RsScaleSpan(UINT x, UINT w, UINT orig, UINT neu, UINT* xo, UINT* wo)
{
    if (!orig) { *xo = x; *wo = w; return; }
    const double sc = (double)neu / (double)orig;
    UINT x0 = (UINT)((double)x * sc);
    UINT x1 = (UINT)((double)(x + w) * sc + 0.5);
    if (x1 > neu) x1 = neu;
    if (x0 > x1) x0 = x1;
    *xo = x0; *wo = x1 - x0;
}
// What DXVK actually holds: binding render target 0 resets the viewport and scissor to that target's size, and the mod's scaled
// SetViewport replaces them. Whoever reads one back and sets it again (the game around a pass, or the mod's state restore) echoes
// a rectangle already in target pixels, which must not be scaled twice. Tracking the live value catches both sources.
static D3DVIEWPORT9 rsDxvkVp = {};
static bool         rsHaveDxvkVp = false;
static RECT         rsDxvkSc = {};
static bool         rsHaveDxvkSc = false;
DEVSTAT(static LONG         rsVpEchoes = 0, rsScEchoes = 0;)
// The size DXVK is about to reset both of them to, for a surface being bound.
static void RsNoteBoundTarget(IDirect3DSurface9* rt)
{
    if (!rsActive) { curRT0Scaled = false; return; }
    UINT tw = 0, th = 0;
    const RsSurf* e = RsFind(rt);
    curRT0Scaled = (e != nullptr);
    if (e) { tw = e->newW; th = e->newH; }
    else if (rt)
    {
        D3DSURFACE_DESC d = {};
        if (SUCCEEDED(rt->GetDesc(&d))) { tw = d.Width; th = d.Height; }
    }
    if (!tw || !th) { rsHaveDxvkVp = false; rsHaveDxvkSc = false; return; }
    rsDxvkVp.X = 0; rsDxvkVp.Y = 0; rsDxvkVp.Width = tw; rsDxvkVp.Height = th;
    rsDxvkVp.MinZ = 0.0f; rsDxvkVp.MaxZ = 1.0f; rsHaveDxvkVp = true;
    rsDxvkSc.left = 0; rsDxvkSc.top = 0; rsDxvkSc.right = (LONG)tw; rsDxvkSc.bottom = (LONG)th;
    rsHaveDxvkSc = true;
}
// The viewport the game last asked for while it believed it drew into the back buffer, in screen pixels, unscaled. Ending the
// redirect puts this back: the game may have set its own since the composite.
static D3DVIEWPORT9 rsGameVp = {};
DLSSIV_HOT static bool   velFailed = false;
static LONG   statDeviceResets = 0;    // Reset() calls seen and survived
// The bone registers are columns: velocity_skin_prev_vs.hlsl hard-codes that, and c244.w is an unread literal 0.
// What viewports the skipped movers were in (development log): reflection-sized or odd-fov passes mean the gate works; the scene
// signature here would mean it rejects real draws.
struct SkipSig { BYTE* vp; int w, h, fovQ, nearQ; LONG hits; };
DEVSTAT(static SkipSig velSkipSig[12];)
static int     velSkipSigN = 0;
// Deferred so the redraw can happen AFTER the game's own draw.
DLSSIV_HOT static bool   velPending = false;
// Who runs the tracker: true while the device thread's worker owns the per-draw chain, the tables, the pose store and the batch.
// devthread/dt_trk.h raises it at every arm and lowers it at every disarm, both drained points, so either thread sees one owner
// per command. With threaded=0, or below four logical processors, it never goes up. Volatile: written by whichever thread
// disarms and read several times inside one hkEndScene, so each read must happen; a one-way byte flag needs no barrier.
DLSSIV_SHARED static volatile bool trkWorkerLive = false;
static D3DPRIMITIVETYPE velPendingType = D3DPT_TRIANGLELIST;
static INT    velPendingBase = 0;
static UINT   velPendingMin = 0, velPendingNum = 0, velPendingStart = 0, velPendingPrim = 0;
static Mat4   velPendingWVP;
static int    velPendingBoneRow = -1;

// The tyre's extra matrix: gta_vehicle_tire multiplies position by matWheelTransform (c64..c67, uploaded start=64 count=4) before
// c8; every other vehicle shader uses c8 alone. The tyre is tracked as (M x world) and redrawn with (M x c8), last frame's M coming
// from the tracker's entry. The consuming shader is identified by its bytecode (position multiplied by c64..c66, never by c8..c10
// directly), so a stale c64 never leaks onto another part.
DLSSIV_HOT static float  m4bCurWheel[16];            // last start=64 count=4 upload
DLSSIV_HOT static bool   m4bHaveWheel   = false;     // ... still intact (no palette wrote over it)
DLSSIV_HOT static float  m4bCurWVP[16];              // the game's c8..c11 for the draw in flight
DLSSIV_HOT static bool   m4bHaveWVP     = false;
DLSSIV_HOT static bool   velCurVSPremul = false;     // the bound vertex shader is a c64-premultiply shader
static bool   velPendingPremul = false;
static Mat4   velPendingWVP8;             // M x c8, the redraw's current-frame transform
static const int VEL_VS_CACHE = 512;
static IDirect3DVertexShader9* velVSPtr[VEL_VS_CACHE];
static bool   velVSPremul[VEL_VS_CACHE];
// How many texldl each cached vertex shader holds (saturating), and the bound one's count. Exactly two on a draw of two triangles
// on four vertices is the game's rain (see ivtRainTex).
static BYTE   velVSTexld[VEL_VS_CACHE];
static BYTE   velCurVSTexld = 0;
// And how many read the first vertex sampler (s0): twelve is the corona's visibility test (FusionFix's deferred_lighting VS11).
// The total is not enough: the same file's waterFx shader (VS16) holds twelve, on s3.
static BYTE   velVSTexld0[VEL_VS_CACHE];
static BYTE   velCurVSTexld0 = 0;
// And whether it passes the position straight through (transport/quad_uv.h, QuvVsStraight): the gate on which two-triangle draws
// RsQuadDraw reads back. True for a shader past the cache's end: the gate only saves work, and such a draw is still judged by its vertices.
static bool   velVSStraight[VEL_VS_CACHE];
DLSSIV_HOT static bool velCurVSStraight = false;
// The corona hold's two facts about the device (see ivtCorTex), render thread only. vtf0Tex: what the game last bound to the first
// vertex sampler, an address only compared. c8DevJit: the last write covering c8..c11 was a scene-camera block with this frame's
// jitter, so the device holds m4bCurWVP there and CorUnjitter may take the jitter out; any other write to those rows, and every
// new frame, clears it.
static IDirect3DBaseTexture9* vtf0Tex = nullptr;
static bool   c8DevJit = false;
// FusionFix's depth pair (c227) as last uploaded, which the device holds: the corona's test encodes its centre with it, so the
// hold gives a corona draw the snapshot frame's pair and puts this one back. Render thread only.
static float  c227Cur[4] = {};
static bool   c227Have   = false;
DLSSIV_HOT static int    velVSCount = 0;
DEVSTAT(static LONG velVSPastCap = 0;)   // uncached vertex shaders fingerprinted per call once the cache is full

DLSSIV_HOT static IDirect3DVertexShader9*      velCurVS   = nullptr;
DLSSIV_HOT static IDirect3DVertexDeclaration9* velCurDecl = nullptr;
// The bindings as objects: a redraw later than its draw needs the buffers themselves, with offset and stride, since the game has
// bound others by then. Tracked, not queried, because GetStreamSource and GetIndices AddRef. Borrowed, not referenced, like
// curRT0: anything that holds one past its frame takes its own reference (the batch does).
DLSSIV_HOT static IDirect3DVertexBuffer9*      velCurVB     = nullptr;
DLSSIV_HOT static UINT                         velCurVBOff  = 0;
DLSSIV_HOT static UINT                         velCurVBStr  = 0;
DLSSIV_HOT static IDirect3DIndexBuffer9*       velCurIB     = nullptr;
struct DrawRec
{
    UINT  prims;
    bool  indexed, sceneKnown, scene;
    IDirect3DVertexShader9*      vs;
    bool  ortho, bb;              // 2D transform (w constant), drawn into the backbuffer
    bool  posT;                   // pre-transformed vertices (screen-space sprite)
    int   kind;                   // 0 indexed, 1 DrawPrimitive, 2 DrawPrimitiveUP, 3 DrawIndexedPrimitiveUP
};

// The opaque-quad test, defined in the transport header far below; the sharpen gate shares it so both users of the interface
// boundary apply one rule.
static bool IvtIsOpaqueQuad(const DrawRec& rec, IDirect3DDevice9* dev);

DLSSIV_HOT static int    velVSLastHit = -1;      // the previous fingerprint hit, checked before the scan

// Jitter state, declared up here because the publisher, earlier in the file, reports the offset.
DLSSIV_HOT static bool  jitterEnabled = false;        // off by default
static int   jitterPhase   = 0;
static int   jitterPhaseUsed = 0;         // the phase this frame's offsets were made from (jitterPhase has moved on): read by the corona hold's snapshot
static int   jitterPhases  = 8;            // derived from the render ratio (JitterPhasesForScale)
// Jitter amplitude as a fraction of the full +/-0.5 pixel Halton pattern. 1.0 is the full pattern, as NVIDIA specifies:
// InJitterOffsetX/Y is documented over [-0.5, 0.5] pixels, and a resolve told the true offset wants the whole range.
// A named constant so the reason stays attached to it; the multiplies by 1.0 fold away.
static const float jitterAmplitude = 1.0f;
static float jitterPxX  = 0.0f, jitterPxY  = 0.0f;

// The jitter chain is FSR2's reference line for line: the Halton pair is ffxFsr2GetJitterOffset's, the projection jitter is its
// 2x/w and -2y/h (Y negated), and the pair handed to the dispatch is that same pair unmodified. The projection patch subtracts
// because GTA IV has m[2][3] = -1 where vendor documentation assumes +1. What is published is what is applied.
DLSSIV_HOT static float jitterNdcX = 0.0f, jitterNdcY = 0.0f;

// Where the jitter goes: into every gWorldViewProj the game uploads for the scene camera, post-multiplied at the upload, which
// reaches every draw of every pass. Patching grcViewport+0x1C0 once a frame is not enough: about a quarter of draws use the
// original projection, so those passes would render unjittered while the upscaler un-jitters the whole frame.
DEVSTAT(static LONG  statC8Jittered = 0, statC8NotScene = 0, statC8NoCamera = 0;)

// Jitter ships with the resolve: jitter with nothing consuming it makes the image worse. The transport is the resolve authority
// (IvtResolveState).
static bool         resolveActive = false;

// The render target of every draw, and which one is the back buffer: the HUD is drawn into the back buffer after the last scene
// draw and post-processing into intermediate targets first, so knowing both per draw finds where a resolve runs before the HUD.
DLSSIV_HOT static IDirect3DSurface9* curRT0         = nullptr;   // tracked, not referenced
// And targets 1..3, tracked for the same reason as curRT0: the velocity redraw would otherwise query all four targets per mover
// (each an AddRef and a Release), and the game binds targets far less often than that. The mod's own binds go through
// origSetRenderTarget and bypass the hook, so the shadow keeps the game's targets while the mod's is bound: the value the
// restore needs. Not referenced: the game owns them, and no game code runs between the save and the restore.
static IDirect3DSurface9* curRT123[3]    = { nullptr, nullptr, nullptr };
// Borrowed, never AddRef'd, and refreshed at the top of hkEndScene, so it serves every draw of the following frame; hkReset
// clears and re-fetches it, or it would be stale for a frame after a Reset.
DLSSIV_HOT static IDirect3DSurface9* backbufferSurf = nullptr;   // refreshed every frame, not referenced
DLSSIV_HOT static IDirect3DSurface9* curDS          = nullptr;   // tracked via SetDepthStencilSurface, not referenced
static bool               curDSKnown     = false;     // seeded once from GetDepthStencilSurface
#include "ds_bracket.h"                                // what a bracket of ours puts back when another thread bound a depth stencil inside it
// Events on the depth buffer the transport snapshots, per frame: how often
// the bound depth-stencil changed, and how often the one the scene used was
// cleared, AFTER the first scene draw and BEFORE the injection. The
// transport reads and resets them at EndScene.
DEVSTAT(static int                dsChangesAfterScene = 0;)
DEVSTAT(static int                dsClearsAfterScene  = 0;)
DLSSIV_HOT static IDirect3DSurface9* dsAtSceneDraws      = nullptr;   // the DS bound at the last scene draw
// The DS that carried the most scene-camera draws in the previous frame: the
// scene depth. Counted per frame in a small table, published at EndScene.
static IDirect3DSurface9* dsSceneMajority     = nullptr;
static IDirect3DSurface9* dsCountSurf[6]      = {};
static int                dsCountN[6]         = {};
static int                dsCountUsed         = 0;
static inline void DsCountSceneDraw(IDirect3DSurface9* ds)
{
    if (!ds) return;
    for (int i = 0; i < dsCountUsed; i++) if (dsCountSurf[i] == ds) { dsCountN[i]++; return; }
    if (dsCountUsed < 6) { dsCountSurf[dsCountUsed] = ds; dsCountN[dsCountUsed] = 1; dsCountUsed++; }
}
static inline void DsPublishMajority()
{
    int best = -1;
    for (int i = 0; i < dsCountUsed; i++) if (best < 0 || dsCountN[i] > dsCountN[best]) best = i;
    if (best >= 0 && dsCountN[best] > 0) dsSceneMajority = dsCountSurf[best];
    dsCountUsed = 0;
}
// StretchRect census (see hkStretchRect): copies whose source is the backbuffer, per frame
DEVSTAT(static int  srBbBeforeInject = 0, srBbAfterInject = 0, srOther = 0;)

// The frame counter, declared here because the publisher, earlier in the file, needs it.
DLSSIV_HOT static volatile LONG frames = 0;

// The bone block, accumulated: RAGE pins gBoneMtx at c64..c207 and the game uploads it in variable pieces, so no single write
// holds a complete pose, and a shadow accumulated across writes is the only way to have one.
static float boneShadow[144 * 4];
DLSSIV_HOT static int   boneMaxReg = -1;      // highest register touched, relative to c64
// Bumped on every write that reaches the shadow, before the copy: the copy runs under __try on game memory, and a partial copy
// that faults leaves the shadow changed, so advancing only on success would certify a stale hash.
DLSSIV_HOT static unsigned __int64 boneShadowSerial = 0;

// ===== DLSSIV_M4D_INDEX_VERIFY state: kept at 0; build with 1 after touching M4dBuildIndex (prove-all compiles it). =====
#if DLSSIV_M4D_INDEX_VERIFY
static unsigned __int64 m4dIdxAgree = 0, m4dIdxMismatch = 0;
static int m4dIdxSaid = 0;
#endif
// ===== END DLSSIV_M4D_INDEX_VERIFY STATE =====================================
#include "bonehash.h"   // BoneHashWords: four FNV lanes, proved by tools/perfproof/equiv_bonehash.cpp

// FNV over the shadowed bone block: a hash, not a 2304-byte memcmp, because this runs on every skinned draw.
// One body, two sides: every tracker body that reads a per-draw input is a template on a side, a struct of inline accessors.
// TrkRender answers with the render thread's globals (its instructions match the unfactored code: tools/trkdiff.py); TrkWorker
// (devthread/dt_trk.h) with the device thread's copies rebuilt from the recorded stream. One side is live per session. The memo
// below is a function static, so each instantiation keeps its own.
template<class S> static unsigned HashBones()
{
        // Memoised on the shadow serial: boneShadow and boneMaxReg have one writer, which bumps the serial before its memcpy, so an
    // unchanged serial means the cached hash is exact. ~0ull is unreachable (the serial starts at 0 and only increments), so the
    // first call always computes. The early return stays below the census block, which must count every call.
    static unsigned __int64 hbCachedSerial = ~0ull;
    static unsigned         hbCachedHash   = 0;
    if (hbCachedSerial == S::BoneSerial()) return hbCachedHash;

    const unsigned h = BoneHashWords((const unsigned*)S::BoneShadow(), (S::BoneMaxReg() + 1) * 4);
    hbCachedSerial = S::BoneSerial();
    hbCachedHash   = h;
    return h;
}

// ---------------------------------------------------------------------------
// Stage 3: the previous-frame bone store
// ---------------------------------------------------------------------------
// One pool, not two: poses are uploaded to a texture at EndScene and the GPU reads them there, so the CPU side holds only the
// frame being built, and a row recorded this frame stays valid in the texture for exactly the next frame.
// 144 registers wide: the game writes at most 138 of c64..c207 (46 bones); wider is free, narrower is a corrupted limb.
#define BONE_REGS       144
// Rows: a busy traffic spot stores about 500 poses a frame (a vehicle uploads its bones per part), and a density mod goes past
// that. A full store costs the next frame its previous poses and sends the overflow back to the immediate redraw. 2.36 MB here
// and as much in the texture; BoneTexUpload locks only a rectangle, so the size costs no upload.
#define BONE_STORE_ROWS 1024

static float bonePool[BONE_STORE_ROWS][BONE_REGS * 4];
static int   bonePoolNext = 0;
DEVSTAT(static LONG  velStatRowsPeak = 0;)      // the most rows a frame since the velocity line last printed (VelEndFrame)
DEVSTAT(static LONG  velStatTrackerLost = 0;)   // BoneStoreAlloc answered -1 since then: a pose the tracker could not keep
// The device thread's two backstops, counted for life and said once at log=1. Only the worker's instantiation of
// VelRecordObject reaches them: it may not draw, so a full batch loses its newest mover and a mover that cannot be deferred is
// not recorded. Both measure zero in the field (cap 2048 against a peak of 848), and each is a lost vector when not.
static LONG  velStatDropFull = 0;      // the device thread's batch was full: the newest mover was not recorded
static LONG  velStatDropNow = 0;       // a mover the device thread could not defer, and may not draw
static bool  velDropSaid = false;

// A one-entry dedupe: a pedestrian is drawn as several sub-meshes back to back with one pose, so a single slot saves a row and a
// 2304-byte copy for each.
static unsigned bonePoolLastHash = 0;
static int      bonePoolLastRow  = -1;


static void LogRecord(_Printf_format_string_ const char* fmt, ...);   // defined with the log, below: the store's two said-once lines need it
// Returns the row this pose was stored in, or -1 if the store is full.
// `dedupe` is false for BoneStoreRowFor alone: the one-entry dedupe is a hash compare, fine for the tracker (a collision gives a
// stale previous pose), but BoneStoreRowFor's row is the current pose the redraw is skinned by, and it has just rejected that
// hash's row by memcmp.
template<class S> static int BoneStoreAlloc(unsigned poseHash, bool dedupe = true)
{
    if (dedupe && bonePoolLastRow >= 0 && bonePoolLastHash == poseHash)
    {
        return bonePoolLastRow;
    }
    if (bonePoolNext >= BONE_STORE_ROWS)
    {
        DEVSTAT(if (telemetryOn) { velStatTrackerLost++; })
                // Said once a session at log=1: the store is sized from a busy known spot, and somewhere may be busier. Past this point the
        // characters drawn later in the frame lose their previous pose next frame.
        static bool said = false;
        if (!said)
        {
            said = true;
            LogRecord("  velocity: the pose store (%d poses a frame) FILLED at frame %ld",
                      BONE_STORE_ROWS, (long)S::Frames());
        }
        return -1;
    }
    int row = bonePoolNext++;
    memcpy(bonePool[row], S::BoneShadow(), sizeof(float) * BONE_REGS * 4);
    bonePoolLastHash = poseHash;
    bonePoolLastRow  = row;
    return row;
}

// The row holding this pose, for the velocity batch: the tracker's own row when it has just stored this pose (nearly always),
// without counting a deduplicated store; otherwise a row of its own, or -1.
// Exact, not hashed: the row is the current pose the redraw is skinned by, so a hit is confirmed against the shadow over the
// registers the game has written. Never at the tracker's expense: the last quarter of the store is the tracker's alone, since
// its -1 loses a previous pose next frame; past it this answers -1 and the mover is redrawn at once (a render-target round trip).
#define BONE_ROWS_TRACKER_ONLY (BONE_STORE_ROWS / 4)
template<class S> static int BoneStoreRowFor(unsigned poseHash)
{
    if (bonePoolLastRow >= 0 && bonePoolLastHash == poseHash
        && memcmp(bonePool[bonePoolLastRow], S::BoneShadow(), sizeof(float) * 4 * (size_t)(S::BoneMaxReg() + 1)) == 0)
        return bonePoolLastRow;
    if (bonePoolNext >= BONE_STORE_ROWS - BONE_ROWS_TRACKER_ONLY)
    {
                // Said once a session. On the render thread the mover is redrawn one at a time: the picture is unchanged, the frame rate is not.
        // The worker cannot redraw and drops the mover, which loses its vectors for that frame (ghosting). The line says which happened.
        static bool said = false;
        if (!said)
        {
            said = true;
            LogRecord("  velocity: more than %d skinned poses in one frame (frame %ld): %s",
                      BONE_STORE_ROWS - BONE_ROWS_TRACKER_ONLY, (long)S::Frames(),
                      trkWorkerLive ? "the rest are DROPPED on the device thread (camera vectors only; limbs may ghost)"
                             : "the rest are redrawn one at a time (slower, the same picture)");
        }
        return -1;
    }
    return BoneStoreAlloc<S>(poseHash, false);   // the hash matched and the bytes did not: a fresh row, never the rejected one
}

// c244 and c246 change within a frame, once (PatchProjection sets patchedThisFrame; the c227 capture assigns camCur), so this is
// a shadow of what the device holds, re-uploaded only when the value moves, not a per-frame hoist. The forced upload on the
// frame's first redraw bounds any outside write to one frame.
static float velLastDp[4] = { 0, 0, 0, 0 };
// The reciprocal logarithm below, memoised on its two inputs: see its use.
static bool  velDpHave = false;
static float velDpKeyN = 0.0f, velDpKeyF = 0.0f, velDpLgInv = 0.0f;
static float velLastJt[4] = { 0, 0, 0, 0 };
static LONG  velConstFrame = -1;          // -1 forces both on the next redraw

// The QPC frequency, read once where it is first needed (the camera's frame time, the development census timers).
static LARGE_INTEGER qpcFreq = { 0 };

// The GPU side of the store and the shader that reads it, declared with their state because the report that prints their
// status sits far earlier in this file.
static IDirect3DTexture9*      boneTex      = nullptr;
static bool                    boneTexTried = false;
static bool                    boneTexOK    = false;
static IDirect3DVertexShader9* velSkinPrevVS = nullptr;
static bool                    vtfProven     = false;  // set by the stage 1 round trip
// The report reads it: the probe runs only when the transport is on, so not run and failed are different answers.
static bool                    vtfTried      = false;  // the probe has been attempted

// Every condition in one place: without deliverable previous bones, flagging a stationary character as a mover only adds draws
// for an identical picture (the redraw would use the current pose twice), so detection and delivery share this gate.
static bool PrevBonesLive()
{
    return vtfProven && boneTexOK && boneTex && velSkinPrevVS;
}


// grcViewport field offsets (see header).
static const int kOffViewInverse   = 0x140;
static const int kOffWorldViewProj = 0x100;
static const int kOffView          = 0x180;
static const int kOffProj          = 0x1C0;
static const int kOffFov           = 0x2B8;
static const int kOffNear          = 0x2C0;
static const int kOffFar           = 0x2C4;
static const int kOffWidth         = 0x2B0;
static const int kOffHeight        = 0x2B4;
static const int kOffIsPerspective = 0x2F0;   // CPU-side bool, per FusionFix
// The reversed-Z flag: every reprojection and the depth guide assume conventional forward Z (GTA IV is D24S8). The flag is
// checked at every camera capture and logged if ever set, rather than handled by an untestable reverse-Z branch on the vector path.
static const int kOffInvertZ       = 0x3E0;
static const int kViewportSize     = 0x3F0;

// IDirect3DDevice9 vtable slots, proven against this game (walked from GetVertexShader=93 / GetPixelShader=108).
static const int kSlotReset       = 16;
static const int kSlotSetVsConstF = 94;
static const int kSlotSetPsConstF = 109;
static const int kSlotEndScene    = 42;
static const int kSlotDrawPrim       = 81;
static const int kSlotDrawPrimUP     = 83;
static const int kSlotDrawIdxUP      = 84;
static const int kSlotDrawIndexed    = 82;
static const int kSlotSetStreamSource= 100;
static const int kSlotSetIndices     = 104;
static const int kSlotSetVertexDecl  = 87;
static const int kSlotSetVertexShader = 92;
static const int kSlotSetRenderTarget = 37;
static const int kSlotSetDepthStencil = 39;
static const int kSlotClear           = 43;
static const int kSlotCreateDepthStencilSurface = 29;
static const int kSlotStretchRect = 34;
static const int kSlotCreateTexture      = 23;   // render-scale census
static const int kSlotCreateRenderTarget = 28;
static const int kSlotSetViewport        = 47;
static const int kSlotSetTexture         = 65;   // render scale
static const int kSlotSetScissorRect     = 75;
// Derived from the SDK's IDirect3DDevice9 declaration and checked against the slots above.
static const int kSlotSetSamplerState    = 69;   // the mip LOD bias
// Derived from the SDK's IDirect3DDevice9 declaration and cross-checked against StretchRect, SetRenderTarget, Clear, SetTexture
// and SetSamplerState.
static const int kSlotColorFill          = 35;   // a write path nothing else watches
static const int kSlotGetRenderTargetData = 32;  // the absorb's read-side guard (hdr_cga.h)
static const int kSlotGetDepthStencil    = 40;   // the engine's question before it puts a depth stencil back (ds_bracket.h)
static bool hdrSwapHookDirty = true;              // HDR: hook the swapchain's Present at first EndScene and after each Reset
static const int kD3dSlotCreateDevice    = 16;   // on the IDirect3D9 vtable

// The clock every "(+ms)" stamp reads, relative to when the hooks went in (initAt), and the game's back buffer size.
static DWORD initAt = 0;                       // GetTickCount at InitThread start
static UINT  cenScreenW = 0, cenScreenH = 0;   // the game's backbuffer size (its CreateDevice/Reset, else the first backbuffer seen)
static inline DWORD CenMs() { return GetTickCount() - initAt; }
// D3D resource bytes, split by owner (rsOwnCreate is set around every creation of the mod's). Formats this cannot size are
// counted, never treated as zero.
DEVSTAT(static UINT64 memOursBytes = 0, memGameBytes = 0;)
DEVSTAT(static LONG   memOursN = 0, memGameN = 0, memUnsized = 0;)

static UINT MemBitsPerPixel(D3DFORMAT f)
{
    switch (f)
    {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8: case D3DFMT_A2B10G10R10: case D3DFMT_A2R10G10B10:
    case D3DFMT_G16R16:   case D3DFMT_D24S8:       case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:  case D3DFMT_D32:         case D3DFMT_R32F:
    case D3DFMT_G16R16F:  case D3DFMT_Q8W8V8U8:    case D3DFMT_V16U16:
        return 32;
    case D3DFMT_A16B16G16R16F: case D3DFMT_A16B16G16R16: case D3DFMT_G32R32F:
        return 64;
    case D3DFMT_A32B32G32R32F:
        return 128;
    case D3DFMT_R5G6B5:   case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_D16:      case D3DFMT_D15S1:
    case D3DFMT_R16F:     case D3DFMT_L16:      case D3DFMT_A8L8:
    case D3DFMT_V8U8:
        return 16;
    case D3DFMT_L8: case D3DFMT_A8: case D3DFMT_P8: case D3DFMT_A4L4:
        return 8;
    // The game's art is nearly all block compressed, so getting these right is
    // most of the game's side of the number.
    case D3DFMT_DXT1: return 4;
    case D3DFMT_DXT2: case D3DFMT_DXT3: case D3DFMT_DXT4: case D3DFMT_DXT5: return 8;
    default: return 0;      // FOURCC and the rest: counted as unsized, not as zero
    }
}

// Approximate: block formats are not rounded to their 4x4 block and driver padding is invisible, so this is a floor.
static UINT64 MemResourceBytes(UINT w, UINT h, UINT levels, D3DFORMAT f)
{
    const UINT bpp = MemBitsPerPixel(f);
    if (!bpp || !w || !h) return 0;
    UINT64 total = 0;
    const UINT n = levels ? levels : 32;          // 0 means the full mip chain
    for (UINT i = 0; i < n; i++)
    {
        total += ((UINT64)w * (UINT64)h * bpp) / 8u;
        if (w == 1 && h == 1) break;
        w = w > 1 ? w >> 1 : 1;
        h = h > 1 ? h >> 1 : 1;
    }
    return total;
}

// A cumulative creation census: nothing is subtracted (there is no Release hook), so on a streaming game it measures what was
// streamed, not what is resident, and cannot prove a memory saving. Reached from hkCreateTexture, hkCreateRenderTarget and
// hkCreateDepthStencilSurface only; vertex, index, cube and volume buffers, offscreen surfaces and the host's allocations are
// not counted. "Ours" means rsOwnCreate > 0, so a creation of the mod's outside an RsOwnCreateCall counts as the game's.
// Nothing is counted unless telemetryOn.
static void MemNote(UINT w, UINT h, UINT levels, D3DFORMAT f, bool ours)
{
#if DLSSIV_DEVLOG                // a development-log census: nothing of it in a release
    if (!telemetryOn) return;   // a byte census: printed at the fixed-frame reports, read by nothing else
    const UINT64 b = MemResourceBytes(w, h, levels, f);
    if (!b) { InterlockedIncrement(&memUnsized); return; }
    if (ours) { memOursBytes += b; InterlockedIncrement(&memOursN); }
    else      { memGameBytes += b; InterlockedIncrement(&memGameN); }
#endif
}

// Defined far below with the other reports and called from the EndScene hook above them, hence declared here.
static void MemCensus(IDirect3DDevice9* dev, const char* when);
static void MemCensusWalk(const char* when, UINT avail);

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

// The level, from the cfg key "log". 0 until LogInit is told otherwise, so
// nothing can be written before the cfg has been read. See the three Log
// functions below for what each level means.
static int logLevel = 0;
// The same predicate as LogDetail, latched: some per-draw censuses exist only for LogDetail, and one costs an SEH frame on
// thousands of draws a frame. Latched in LogInit beside logLevel, its one writer, so the two cannot disagree.
#if DLSSIV_DEVLOG
DLSSIV_HOT static bool censusOn = false;
#else
static const bool censusOn = false;      // no log=3 in this build: everything it gates is dead code, and the compiler is told so
#endif

// The level is a parameter (the caller passes ivtCfg.log) because this is defined above the cfg struct.
static void LogInit(int level)
{
    telemetryOn = (level >= 1);   // telemetry runs only for a log that will be read
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH))
        return;
    char* slash = strrchr(exe, '\\');
    if (!slash)
        return;
    *slash = 0;

        // Off means nothing happens: no file, no rotation, no truncation. A player who never turns logging on cannot tell a logger exists.
    logLevel = level;
#if DLSSIV_DEVLOG
    censusOn = (logLevel >= 3);   // must track LogDetail's guard exactly
#endif
    if (logLevel < 1)
    {
        logPath[0] = 0;
        return;
    }
        // Keep the previous run: one rename (MoveFileEx replaces any older .prev), so this run and the one before it are on disk.
    // The game folder, then %TEMP%: a game under Program Files without elevation refuses the write. Decided here, before the host is
    // spawned, so the host is handed the folder that works; the header names a refused folder and its error. _fsopen because
    // fopen_s does not share, and a stale host still holding the file would cost the header.
    char prev[MAX_PATH], refused[MAX_PATH] = "";
    DWORD refusedErr = 0;
    BOOL  moved = FALSE; DWORD moveErr = 0;
    FILE* f = nullptr;
    for (int attempt = 0; attempt < 2 && !f; attempt++)
    {
                // MAX_PATH + 1 is GetTempPathA's longest answer; a buffer too short gets the required size back, non-zero, with nothing written.
        char dir[MAX_PATH + 1];
        if (attempt == 0) strcpy_s(dir, exe);
        else
        {
            const DWORD tn = GetTempPathA(MAX_PATH + 1, dir);
            if (!tn || tn > MAX_PATH) break;
            size_t n = strlen(dir);
            if (n && dir[n - 1] == '\\') dir[n - 1] = 0;
        }
        if (_snprintf_s(logPath, MAX_PATH, _TRUNCATE, "%s\\DLSS-IV.log", dir) < 0
         || _snprintf_s(prev,    MAX_PATH, _TRUNCATE, "%s\\DLSS-IV.prev.log", dir) < 0) { logPath[0] = 0; break; }   // a path that does not fit names nothing
        moved = MoveFileExA(logPath, prev, MOVEFILE_REPLACE_EXISTING);
        moveErr = moved ? 0 : GetLastError();
        f = _fsopen(logPath, "w", _SH_DENYNO);
        if (!f && attempt == 0) { refusedErr = GetLastError(); strcpy_s(refused, logPath); }
    }
    if (!f)
    {
        char ods[400];
        _snprintf_s(ods, sizeof(ods), _TRUNCATE, "DLSS-IV: log=%d but the log could not be created beside the game (%s, error %lu)"
            " nor under %%TEMP%%; this session writes no log\n", level, refused, refusedErr);
        OutputDebugStringA(ods);
        IvtWarn("log=%d is set, but no log file could be created beside the game (%s, Windows error %lu) nor under %%TEMP%%; this"
                " session writes no log", level, refused, refusedErr);
        logPath[0] = 0;
        return;
    }
        // The build, the clock and the pid, nothing else: the date dates a file found later, and the pid ties it to a crash dump or a
    // DXVK log of the same run. The levels are documented in CONFIG.md.
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "DLSS-IV build " IVT_BUILD_ID " " DLSSIV_FLAVOUR "   %04u-%02u-%02u %02u:%02u:%02u   log=%d   pid %lu\n",
        (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
        (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, level, (unsigned long)GetCurrentProcessId());
    if (refused[0])
    {
        fprintf(f, "THIS LOG IS UNDER %%TEMP%%, NOT BESIDE THE GAME: the game's folder refused it (%s, Windows error %lu)\n", refused, refusedErr);
        char ods[400];
        _snprintf_s(ods, sizeof(ods), _TRUNCATE, "DLSS-IV: the game folder refused the log (%s, error %lu); it is at %s\n", refused, refusedErr, logPath);
        OutputDebugStringA(ods);
        IvtWarn("log=%d is set; the game's folder refused the log file (%s, Windows error %lu), so DLSS-IV.log is under %%TEMP%%: %s",
            level, refused, refusedErr, logPath);
    }
        // A rotation that failed for any reason but nothing to rotate is said: the previous run's log was overwritten.
    if (!moved && moveErr != ERROR_FILE_NOT_FOUND && moveErr != ERROR_PATH_NOT_FOUND)
        fprintf(f, "the previous run's log could not be moved to DLSS-IV.prev.log (Windows error %lu; a program still holds it,"
                   " or the folder refuses renames), so this file has overwritten it\n", moveErr);
    fputc('\n', f);
    fclose(f);
}

// The one thing written even at log=0: a separate, clearly named note that exists only when the mod could not start or stopped,
// so a player on a machine where it does nothing learns why. It is the product's own error report, not telemetry.
// Once per session, on the terminal path only.
static bool fatalNoteWritten = false;
static void** hookedVtbl = nullptr;       // the device vtable the hooks went into (HookVtable); read by the not-DXVK stop for the note
static bool fatalBeforeHooks = false;   // InitThread stopped before hooking the game's device: nothing of the mod runs
// The recovery sentence, shared with the transport's SOFT-picture line so a player reads one instruction, not two.
static const char kIvtSoftRecovery[] = "CHANGE THE RESOLUTION OR TOGGLE FULLSCREEN and it comes back to full resolution"
    " without a relaunch: that is a device Reset, and the scale is decided again there.";
static bool IvtSideFilePath(char* path, bool temp, const char* name)   // MAX_PATH; beside GTAIV.exe, or under %TEMP% when that folder refuses
{
    char dir[MAX_PATH + 1];   // GetTempPathA's contract: see LogInit
    if (temp)
    {
        const DWORD tn = GetTempPathA(MAX_PATH + 1, dir);
        if (!tn || tn > MAX_PATH) return false;
        size_t n = strlen(dir);
        if (n && dir[n - 1] == '\\') dir[n - 1] = 0;
    }
    else
    {
        if (!GetModuleFileNameA(nullptr, dir, MAX_PATH)) return false;
        char* slash = strrchr(dir, '\\');
        if (!slash) return false;
        *slash = 0;
    }
    return _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\%s", dir, name) >= 0;   // truncated is not a path
}
static bool IvtFatalNotePath(char* path, bool temp) { return IvtSideFilePath(path, temp, "DLSS-IV-DID-NOT-START.txt"); }
static bool IvtWarnPath(char* path, bool temp)      { return IvtSideFilePath(path, temp, "DLSS-IV-WARNINGS.txt"); }
// The previous session's note is deleted at start-up, in both folders, so a fixed install stops saying the mod did not start;
// this session writes a fresh one if it fails again. The warnings file (IvtWarn) goes the same way.
static void IvtFatalNoteClear()
{
    char path[MAX_PATH];
    if (IvtFatalNotePath(path, false)) DeleteFileA(path);
    if (IvtFatalNotePath(path, true))  DeleteFileA(path);
    if (IvtWarnPath(path, false))      DeleteFileA(path);
    if (IvtWarnPath(path, true))       DeleteFileA(path);
}
static void IvtFatalNote(const char* why, long injections = 0, bool soft = false, bool noCfg = false)
{
    if (fatalNoteWritten) return;
    fatalNoteWritten = true;
        // The game folder, then %TEMP% (Program Files without elevation refuses the write). The debugger string is for DebugView and
    // costs nothing when nobody listens.
    char path[MAX_PATH] = "", refused[MAX_PATH] = "";   // path initialised: strcpy_s below reads it on the refused arm
    DWORD refusedErr = 0;
    FILE* f = nullptr;
    if (IvtFatalNotePath(path, false)) f = _fsopen(path, "w", _SH_DENYNO);
    if (!f)
    {
        refusedErr = GetLastError();
        strcpy_s(refused, path);
        if (IvtFatalNotePath(path, true)) f = _fsopen(path, "w", _SH_DENYNO);
    }
    if (!f)
    {
        char ods[600];
        _snprintf_s(ods, sizeof(ods), _TRUNCATE, "DLSS-IV: could not start (%s), and the note could not be written beside the"
            " game (%s, error %lu) nor under %%TEMP%%\n", why ? why : "?", refused, refusedErr);
        OutputDebugStringA(ods);
        return;
    }
    if (refused[0])
    {
        char ods[400];
        _snprintf_s(ods, sizeof(ods), _TRUNCATE, "DLSS-IV: the game folder refused the note (%s, error %lu); it is at %s\n", refused, refusedErr, path);
        OutputDebugStringA(ods);
        fprintf(f, "THIS FILE IS HERE, NOT BESIDE THE GAME, because the game's folder refused to be written\n"
                   "(%s, Windows error %lu). DLSS-IV.log is next to this file for the same reason.\n\n",
                refused, refusedErr);
    }
        // The heading says which: a mod that never started, or one that stopped part way through the session.
    NoticeFatal(injections > 0, refused[0] != 0);   // and on the screen, where a hook can still draw (source/notice.h)
    if (injections > 0)
        fprintf(f, "DLSS-IV STOPPED PART WAY THROUGH THE SESSION, after %ld upscaled frames, so the game is running"
                   " WITHOUT it from that point.\n", injections);
    else
        fprintf(f, "DLSS-IV could not start, so the game is running WITHOUT it.\n");
        // A stop after the game's device exists ends the upscaling only: the device thread stays between the game and DXVK and the HDR
    // encode keeps running, so the note must not say the mod is out of the picture. Except before any device hook (a missing cfg,
    // the scan's window refused, no Direct3DCreate9, a device not on DXVK): then neither ran. A helper that fails to start is not
    // one of these. DXVK settings set at start and the engine name's hook may already be in place, so it does not say nothing ran.
    if (noCfg || fatalBeforeHooks)
        fprintf(f, "DLSS-IV stopped before hooking the game's graphics device: no upscaling, no device thread and no HDR this session.\n");
    else
        fprintf(f, "Only the upscaling is off. If they were on, the device thread (threaded=1, the default) and the HDR output"
                   " (hdr=1) are still running; if the game now freezes or crashes, add the line threaded=0 to DLSS-IV.cfg.\n");
    if (soft)
        fprintf(f, "The picture is SOFT: the scene renders at the internal resolution and nothing upscales it. %s\n", kIvtSoftRecovery);
    fprintf(f,
        "Build " IVT_BUILD_ID " " DLSSIV_FLAVOUR "\n"
        "\n"
        "WHY:\n"
        "  %s\n"
        "\n",
        why ? why : "(no reason recorded, which is itself a bug: please report it)");
        // A missing cfg ends here: none of the three causes below applies. The player is told the note returns every launch, and how to
    // turn the mod off without it.
    if (noCfg)
    {
        fprintf(f, "While DLSS-IV.asi is installed and DLSS-IV.cfg is not, this file is written at every launch. To turn\n"
                   "DLSS-IV off without it, remove DLSS-IV.asi instead.\n");
        fclose(f);
        return;
    }
        // One cause per reason, not three: IvtFatalCause (end of this file) sorts the reason by its own words, so "the runtime is not DXVK"
    // gets only its own cause's text. Any other reason gets a short plain paragraph.
    const int cause = IvtFatalCause(why);
    if (cause == 1)
        fprintf(f,
        "WHAT TO CHECK:\n"
        "\n"
        "DXVK IS NOT INSTALLED, or the game is not using it.\n"
        "   DLSS-IV requires DXVK. There is no fallback and there will not be\n"
        "   one: the whole mod is built on a Vulkan interop interface that\n"
        "   native Direct3D 9 does not have.\n"
        "   This mod ships DXVK as vulkan.dll, next to GTAIV.exe. FusionFix\n"
        "   only uses it when its Graphics API option, in the game's own\n"
        "   settings menu, is set to Vulkan. If it says DirectX 9, change it\n"
        "   to Vulkan and restart the game. (The menu writes API = 1 under\n"
        "   [MAIN] in d3d9.cfg, next to GTAIV.exe; that file is what decides.)\n"
        "\n");
    else if (cause == 2)
        fprintf(f,
        "WHAT TO CHECK:\n"
        "\n"
        "" DLSSIV_HOST_DIR "\\" DLSSIV_HOST_EXE " IS MISSING.\n"
        "   It ships in the same archive as this .asi and the two must always\n"
        "   be from the same build. If you copied only the .asi, copy the\n"
        "   " DLSSIV_HOST_DIR " folder too.\n"
        "   Upgrading from a version that had a host64 folder? It is called\n"
        "   " DLSSIV_HOST_DIR " now: copy the new one, move anything you put in\n"
        "   host64 into it, and delete host64.\n"
        "\n");
    else if (cause == 3)
        fprintf(f,
        "WHAT TO CHECK:\n"
        "\n"
        "THE UPSCALER'S OWN LIBRARY IS MISSING.\n"
        "   Both libraries live in " DLSSIV_HOST_DIR "\\, beside " DLSSIV_HOST_EXE ". That\n"
        "   is the directory the host hands to the upscaler, not the folder\n"
        "   GTAIV.exe is in.\n"
        "\n"
        "     NVIDIA (DLSS):  nvngx_dlss.dll        SHIPS WITH THIS MOD\n"
        "         Used on NVIDIA RTX cards (the 20 series or newer). It is\n"
        "         NVIDIA's own signed file and it is in " DLSSIV_HOST_DIR "\\ already.\n"
        "         If it is not there, copy " DLSSIV_HOST_DIR "\\ over again. With\n"
        "         upscaler=-1 a card without it runs FSR; with upscaler=0 the\n"
        "         mod cannot start without it.\n"
        "\n"
        "     AMD (FSR):      amd_fidelityfx_loader_dx12.dll and\n"
        "                     amd_fidelityfx_upscaler_dx12.dll\n"
        "                     SHIP WITH THIS MOD\n"
        "         Used on ANY card that cannot run DLSS. YOU DO NOT NEED TO\n"
        "         DOWNLOAD THEM: they are AMD's own signed files and they are\n"
        "         in " DLSSIV_HOST_DIR "\\ already. If they are not there, copy\n"
        "         " DLSSIV_HOST_DIR "\\ over again. If you replaced\n"
        "         amd_fidelityfx_upscaler_dx12.dll with a community build, put\n"
        "         AMD's back first: a crash inside that file ends FSR.\n"
        "\n");
    else
        fprintf(f,
        "This is not one of the usual install problems. Something else stopped\n"
        "the mod, and the reason is written above. If that reason tells you what\n"
        "to do, try that first. If not, send me the DLSS-IV.log file. It is\n"
        "next to this file.\n"
        "\n");
    fprintf(f,
        "This file is rewritten every time the mod fails to start, and is not\n"
        "created at all when it works. You can delete it.\n");
    fclose(f);
}

// The warnings file. For what a player at log=0 could never see: something DLSS-IV.cfg asked for
// that is not running because a condition it needs is not met (HDR without Windows HDR or in exclusive fullscreen, the device
// thread refused, a cfg line unusable or clamped, a DXVK that is not 3.1, a second copy of this .asi, sharpening refused, the log
// refused). One line each, appended as they happen, with a header only while the file is still empty (two copies may both
// write); deleted at the next start with the note. The first eight are also kept, and the log lists them at each log=1
// heartbeat when the count has moved and in the session-over line, since a player sends one file, the log.
static char          ivtWarnKept[8][420];
static volatile LONG ivtWarnCount = 0;      // every IvtWarn this session, from any thread
static LONG          ivtWarnSaid  = 0;      // how many of them the log has listed
static void IvtWarn(_Printf_format_string_ const char* fmt, ...)
{
    char line[1024];
    va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
        // Kept before the file is tried: a refused warnings file is no reason for the log to lose the line too.
    const LONG kept = _InterlockedIncrement(&ivtWarnCount) - 1;
    if (kept >= 0 && kept < 8) strncpy_s(ivtWarnKept[kept], sizeof(ivtWarnKept[kept]), line, _TRUNCATE);
    NoticeFromWarn(line);       // the few of these that are also said on the screen (source/notice.h)
    char path[MAX_PATH];
    FILE* f = nullptr;
    if (IvtWarnPath(path, false)) f = _fsopen(path, "a", _SH_DENYNO);
    if (!f && IvtWarnPath(path, true)) f = _fsopen(path, "a", _SH_DENYNO);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0)
        fprintf(f, "DLSS-IV build " IVT_BUILD_ID " " DLSSIV_FLAVOUR ": what DLSS-IV.cfg asked for that is NOT running this session, and why.\n"
                   "This file exists only when there is something to say, and is deleted at the next start.\n"
                   "The full details are in DLSS-IV.log, next to this file.\n\n");
    fprintf(f, "- %s\n", line);
    fclose(f);
}

// One copy of this .asi per process: a second (in plugins and beside the exe, or two ASI loaders) would hook the same vtable
// twice. The first to create the mutex is the mod; any other installs nothing and says so in the warnings file. Named by pid,
// so two games do not see each other. The handle is held for the life of the process.
static HANDLE dlssivInstanceMutex = nullptr;
static HANDLE dlssivClearedEv     = nullptr;   // set by the first copy once last session's files are cleared (InitThread)
static bool   dlssivDuplicate     = false;

// The log handle is held open: an open and close per line cost a visible hitch. fflush after every line hands the bytes to the
// OS, so a crash still leaves a complete log; _fsopen with _SH_DENYNO keeps it readable while the game runs. LogInit rotates
// before any Log call, so opening lazily cannot fight the rename.
static FILE* logFile = nullptr;

// The three levels; there is deliberately no plain Log(), so every call picks one:
//   log=0  nothing is written and no file is created
//   log=1  LogRecord   what happened and whether it worked: what a player sends with a bug report
//   log=2  + LogHealth periodic counters, one line per subsystem per period
//   log=3  + LogDetail everything else (engine dumps, per-draw tables, timing). Development only.
// The level controls what is written, not what is measured. The level test comes first, so a suppressed line costs one compare;
// a site that formats into a buffer first hoists its own gate.
// The same named mutex the host takes: both sides append, and the UCRT's append is a seek then a write, so two processes can
// land on one offset. Created lazily; a failure just means unserialised writes.
static HANDLE logMutex = nullptr;

static void LogEmitV(const char* fmt, va_list ap)
{
    if (!logPath[0])
        return;
    if (!logMutex) logMutex = CreateMutexA(nullptr, FALSE, "Local\\DlssIvLog");
        // WAIT_ABANDONED grants ownership too: the other side died holding it, and not releasing it would keep every later wait on its
    // 200 ms timeout.
    const DWORD w = logMutex ? WaitForSingleObject(logMutex, 200) : WAIT_FAILED;
    const bool held = w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
    if (!logFile)
    {
        logFile = _fsopen(logPath, "a", _SH_DENYNO);
                // Release on this path too, or every host write burns its 200 ms timeout. Said once to the debugger: the file was created
        // moments ago, so a refused append means a folder that changed its mind or a program holding the file.
        if (!logFile)
        {
            char ods[400];
            _snprintf_s(ods, sizeof(ods), _TRUNCATE, "DLSS-IV: the log could not be reopened for append (%s, error %lu); no more lines this session\n",
                logPath, GetLastError());
            OutputDebugStringA(ods);
            logPath[0] = 0; if (held) ReleaseMutex(logMutex); return;
        }
    }
    vfprintf(logFile, fmt, ap);
    fputc('\n', logFile);
    fflush(logFile);            // crash safety: the bytes reach the OS at once
    if (held) ReleaseMutex(logMutex);
}

static void LogRecord(_Printf_format_string_ const char* fmt, ...)
{
    if (logLevel < 1) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
#if DLSSIV_DEVLOG
static void LogHealth(_Printf_format_string_ const char* fmt, ...)
{
    if (logLevel < 2) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
static void LogDetail(_Printf_format_string_ const char* fmt, ...)
{
    if (logLevel < 3) return;
    va_list ap; va_start(ap, fmt); LogEmitV(fmt, ap); va_end(ap);
}
#else
// A release build has no development log (DLSSIV_DEVLOG 0): every call below compiles to nothing, arguments included, which is
// sound only because no call's arguments have side effects. A new site must keep that true. The two functions stay, empty, for
// the sites that pick a level through a pointer, `(said ? LogHealth : LogRecord)(...)`, where the macro is not expanded.
static void LogHealth(const char*, ...) {}
static void LogDetail(const char*, ...) {}
#define LogHealth(...) ((void)0)
#define LogDetail(...) ((void)0)
#endif

// ===== Print on change: the development log's periodic blocks ===================================
// Development log only (inside DLSSIV_DEVLOG). A periodic line whose values have not changed is held; it prints again when one
// changes, and at least every twentieth period. A printed line is marked LOGMARK, and LogHeld states the rule once per session.
// A line reporting a problem is never held: the call site passes its alarm condition, and each site says which branch is the alarm.
// Only for periodic report blocks, never on a per-draw, per-upload or per-frame path. The key is an FNV mix of every value that
// can change the line's text (including a %s selector); a string is never hashed, as a pointer is not its contents.
// Where the surrounding function survives into a release, put the `static LogHold` and the `if (!LOGHELD(...))` inside
// #if DLSSIV_DEVLOG and leave the LogHealth statement outside, so the release sees the statement unchanged.
// Lines of per-frame averages are not held: their raw values almost always differ.
#if DLSSIV_DEVLOG
struct LogHold { unsigned __int64 key; unsigned held; bool seen; };
static const char* logHoldMark = "";      // the note for the line LogHeld has just let through

static inline unsigned __int64 LogBits(double v) { unsigned __int64 b; memcpy(&b, &v, sizeof b); return b; }
static inline unsigned __int64 LogBits(float v)  { return LogBits((double)v); }
static inline unsigned __int64 LogBits(const char*) = delete;          // hash the selector, not the pointer
template <class T> static inline unsigned __int64 LogBits(T v) { return (unsigned __int64)(long long)v; }
static inline unsigned __int64 LogKeyMix(unsigned __int64 h) { return h; }
template <class A, class... R> static inline unsigned __int64 LogKeyMix(unsigned __int64 h, A a, R... r)
{
    return LogKeyMix((h ^ LogBits(a)) * 1099511628211ULL, r...);      // FNV-1a's prime, over whole fields
}
#define LogKey(...) LogKeyMix(14695981039346656037ULL, __VA_ARGS__)

static bool LogHeld(LogHold& s, bool alarm, unsigned __int64 key)
{
    logHoldMark = "";
    if (alarm) { s.seen = false; return false; }                       // a fault is reported every period
    if (s.seen && s.key == key && s.held < 19) { s.held++; return true; }
    s.key = key; s.held = 0; s.seen = true;
    static bool saidRule = false;
    if (!saidRule)
    {
        saidRule = true;
        LogHealth("  log: a periodic line marked \"(held while unchanged)\" is not repeated while every number on it"
            " stays the same. It prints again as soon as one changes, and at least every twentieth period. A line"
            " reporting a fault is never held.");
    }
    logHoldMark = "   (held while unchanged)";
    return false;
}
#define LOGHELD(name, alarm, ...) LogHeld(name, (alarm), LogKey(__VA_ARGS__))
#define LOGMARK logHoldMark
#else
// Macros, not functions: even an unused inline function changes the release's .data ordering and loses byte identity.
#define LOGHELD(name, alarm, ...) (false)
#define LOGMARK ""
#endif
// ===== end of print on change ===================================================================

// The warnings the log has not listed yet (see IvtWarn). One load when there are none.
static void IvtWarnsToLog()
{
    const LONG n = ivtWarnCount;
    if (n == ivtWarnSaid || logLevel < 1) return;
    LogRecord("  warnings: %ld so far this session (DLSS-IV-WARNINGS.txt)%s", n, ivtWarnSaid >= 8 ? ", the first eight quoted earlier"
              : ivtWarnSaid > 0 ? (n > 8 ? ", the new ones up to the eighth:" : ", the new ones:") : n > 8 ? ", the first eight:" : ":");
    for (LONG i = ivtWarnSaid; i < n && i < 8; i++) LogRecord("    - %s", ivtWarnKept[i]);
    ivtWarnSaid = n;
}

// A FILE'S VERSION RESOURCE, as "a.b.c.d": GTAIV.exe's, and FusionFix's.
static bool FileVersionOf(const char* path, char* out, size_t cap);

// FusionFix, named in the log: the absorb, the SMAA sRGB-write suppression, the console gamma probe and the render-scale register
// list exist because of it. Asked at the game's CreateDevice, when the ASI loader has loaded every .asi.
static void LogFusionFixOnce()
{
    static bool said = false;
    if (said || logLevel < 1) return;
    said = true;
    const HMODULE ff = GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");
    char path[MAX_PATH] = "", ver[64] = "";
    if (!ff || !GetModuleFileNameA(ff, path, MAX_PATH))
    {
        LogRecord("FusionFix: NOT LOADED (no GTAIV.EFLC.FusionFix.asi); DLSS-IV is untested without it");
        return;
    }
    const char* leaf = strrchr(path, '\\');
    if (FileVersionOf(path, ver, sizeof(ver))) LogRecord("FusionFix: %s v%s loaded", leaf ? leaf + 1 : path, ver);
    else                                       LogRecord("FusionFix: %s loaded (no version resource)", leaf ? leaf + 1 : path);
}


// The device the draw hooks run on, noted at every SetRenderTarget; the sharpen pass and its gate run on it (ivtDev lives in the
// transport, included below).
DLSSIV_HOT static IDirect3DDevice9* drawDev = nullptr;

static void LogMat(const char* label, const Mat4& m)
{
    LogDetail("  %s", label);
    for (int r = 0; r < 4; r++)
        LogDetail("    % 12.4f % 12.4f % 12.4f % 12.4f",
            m.m[r][0], m.m[r][1], m.m[r][2], m.m[r][3]);
}

// Module attribution, per thread: two threads formatting into one static buffer garble each other's strings.
static const char* Where(void* addr)
{
    static __declspec(thread) char buf[160];
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(addr, &mbi, sizeof(mbi)) || !mbi.AllocationBase)
    {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%p (no module)", addr);
        return buf;
    }
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA((HMODULE)mbi.AllocationBase, path, MAX_PATH);
    const char* name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%p = %s+0x%X", addr,
                name[0] ? name : "?",
                (unsigned)((BYTE*)addr - (BYTE*)mbi.AllocationBase));
    return buf;
}

// D3DPRESENT_INTERVAL is not a small integer: DEFAULT is 0 and means one refresh (vsync on); IMMEDIATE is 0x80000000.
// It is only what the game asked for: DXVK's d3d9.presentInterval overrides it, and the "latency: your dxvk.conf sets
// d3d9.presentInterval" line says when dxvk.conf or DXVK_CONFIG does.
static const char* PresentIntervalName(UINT iv)
{
    switch (iv)
    {
    case D3DPRESENT_INTERVAL_DEFAULT:   return "DEFAULT (one refresh, vsync on)";
    case D3DPRESENT_INTERVAL_ONE:       return "ONE (vsync on)";
    case D3DPRESENT_INTERVAL_TWO:       return "TWO (vsync on, every second refresh)";
    case D3DPRESENT_INTERVAL_THREE:     return "THREE (vsync on, every third refresh)";
    case D3DPRESENT_INTERVAL_FOUR:      return "FOUR (vsync on, every fourth refresh)";
    case D3DPRESENT_INTERVAL_IMMEDIATE: return "IMMEDIATE (vsync off)";
    default:                            return "unrecognised";
    }
}

// ---------------------------------------------------------------------------
// Memory safety
// ---------------------------------------------------------------------------

// True when [p, p+n) is committed, readable, and not a guard page. Every read
// of game memory in this file goes through this first.
static bool IsReadable(const void* p, size_t n)
{
    if (!p)
        return false;
    MEMORY_BASIC_INFORMATION mbi;
    const BYTE* cur = (const BYTE*)p;
    const BYTE* end = cur + n;
    while (cur < end)
    {
        if (!VirtualQuery(cur, &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;
        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                             | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                             | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & readable))
            return false;
        cur = (const BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    return true;
}

// IsReadable is a syscall (VirtualQuery takes the address-space lock, which DXVK and the streaming threads contend), too costly
// on the per-draw path. The __try/__except below is the real protection and gives the same answer. After kReadFaultLimit faults
// the pre-check comes back for good, bounding the cost of a pointer that always faults. Other callers keep it.
static LONG readFaults = 0;
static const LONG kReadFaultLimit = 64;

static bool ReadMat(const void* p, Mat4* out)
{
    if (!p) return false;
    if (readFaults >= kReadFaultLimit && !IsReadable(p, sizeof(Mat4)))
        return false;
    __try { memcpy(out, p, sizeof(Mat4)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement(&readFaults); return false; }
    return true;
}

static bool ReadFloat(const void* p, float* out)
{
    if (!p) return false;
    if (readFaults >= kReadFaultLimit && !IsReadable(p, sizeof(float)))
        return false;
    __try { *out = *(const float*)p; }
    __except (EXCEPTION_EXECUTE_HANDLER) { InterlockedIncrement(&readFaults); return false; }
    return true;
}

// A matrix full of NaN, denormals or absurd magnitudes is a wrong pointer that
// happened to be readable. Cheap to reject and it costs nothing to be sure.
static bool Sane(const Mat4& m)
{
    bool anyNonZero = false;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            float v = m.m[r][c];
            if (v != v)                     // NaN
                return false;
            if (fabsf(v) > 1.0e12f)
                return false;
            if (v != 0.0f)
                anyNonZero = true;
        }
        // An all-zero matrix is an unwritten or wrongly addressed buffer (m[3][3] == 0 would pass the perspective check).
    return anyNonZero;
}

static bool NearlyEqual(const Mat4& a, const Mat4& b, float eps)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (fabsf(a.m[r][c] - b.m[r][c]) > eps)
                return false;
    return true;
}

// ---------------------------------------------------------------------------
// Finding grcViewport
// ---------------------------------------------------------------------------
// The byte-pattern scan FusionFix ships, which finds rage::grcViewport** pCurrentViewport directly: the current viewport, so it
// follows scene, shadow cascades, mirror and reflection, and it works without a per-build RVA, Complete Edition included.
//     8B 35 ?? ?? ?? ??  75 14              mov esi, [imm32] / jnz +0x14
//     8B 3D ?? ?? ?? ??  75 14  6A 00       mov edi, [imm32] / jnz +0x14 / push 0
// The imm32 at +2 is the grcViewport**. A match is not proof, so every candidate is validated against its object.
// (The CDraw::Scene chain, Scene -> [+0x04] CViewportGame* -> [+0x10] grcViewport, does not work: on 1.0.8.0 it lands in float data.)

// Hot path, called thousands of times a frame from the constant hook: no VirtualQuery here; SEH catches a bad read instead (free
// when nothing faults). The slot was validated once already.
static BYTE* CurrentViewportFast()
{
    if (!viewportSlot)
        return nullptr;
    BYTE* p = nullptr;
    __try { p = *viewportSlot; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return p;
}

// COLD PATH, fully checked. Used by Verdict, which runs a handful of times.
// Never cache the result across frames.
static BYTE* CurrentViewport()
{
    if (!viewportSlot || !IsReadable(viewportSlot, sizeof(void*)))
        return nullptr;
    BYTE* p = nullptr;
    __try { p = *viewportSlot; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return IsReadable(p, kViewportSize) ? p : nullptr;
}

// The executable section of GTAIV.exe.
static bool TextRange(BYTE** outStart, size_t* outSize)
{
    BYTE* base = (BYTE*)GetModuleHandleA(nullptr);
    if (!base)
        return false;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
        {
            *outStart = base + sec[i].VirtualAddress;
            *outSize  = sec[i].Misc.VirtualSize;
            return true;
        }
    return false;
}

#define MAX_CANDIDATES 64
static BYTE** candidate[MAX_CANDIDATES];
static int    candidates = 0;

DEVSTAT(static int candidateHits = 0;)

static void AddCandidate(BYTE** slot)
{
    DEVSTAT(if (telemetryOn) { candidateHits++; })
    if (!slot)
        return;
    for (int i = 0; i < candidates; i++)
        if (candidate[i] == slot)
            return;                      // the two patterns can find the same one
    if (candidates < MAX_CANDIDATES)
        candidate[candidates++] = slot;
}

// mask: 'x' must match, '?' any. Returns the number of matches (counted here: candidateHits exists only under telemetryOn and the
// refusal text runs at log=0), or -1 when .text could not be read, so a scan that never ran is not 0 hits.
static int ScanFor(const BYTE* bytes, const char* mask, int immOffset)
{
    int hits = 0;
    BYTE* s = nullptr; size_t n = 0;
    if (!TextRange(&s, &n))
        return -1;
    size_t len = strlen(mask);
    if (n < len)
        return -1;
    for (size_t i = 0; i + len <= n; i++)
    {
        bool hit = true;
        for (size_t j = 0; j < len; j++)
            if (mask[j] == 'x' && s[i + j] != bytes[j]) { hit = false; break; }
        if (hit)
        {
            hits++;
            AddCandidate(*(BYTE***)(s + i + immOffset));
        }
    }
    return hits;
}

// The game's menu-up flag, CMenuManager::m_MenuActive: one byte, true while the pause menu (map included) or the front end is up.
// FusionFix's pattern for it matches 1.0.8.0 only, so this rule needs no pattern per build: the game tests the flag in one
// shape many times over,
//     cmp byte ptr [flag], 0 / jz +0D / call / test al, al        80 3D a a a a 00 74 0D E8 r r r r 84 C0
// and the address that shape names most often is the flag (6 sites to 1 on 1.0.4.0 and 1.0.8.0, 7 to 1 on the Complete Edition).
// Accepted only on at least 4 votes and three times the runner-up, inside the image, readable and holding 0 or 1; otherwise the
// one reader (hdr_encode.h, the UI mask) behaves as if there were no flag. Read only, one byte a frame.
static const BYTE* gameMenuActive = nullptr;
static __forceinline bool GameMenuUp() { return gameMenuActive && *gameMenuActive != 0; }
static void MenuFlagFind()
{
    BYTE* s = nullptr; size_t n = 0;
    BYTE* const img = (BYTE*)GetModuleHandleA(nullptr);
    if (!img || !TextRange(&s, &n) || n < 16) { LogRecord("  menu flag: the game's code could not be read: hdr_ui masks the pause menu by difference"); return; }
    const size_t imgSize = ((IMAGE_NT_HEADERS*)(img + ((IMAGE_DOS_HEADER*)img)->e_lfanew))->OptionalHeader.SizeOfImage;
    struct Vote { const BYTE* a; int n; } v[16] = {}; int vn = 0;
    for (size_t i = 0; i + 16 <= n; i++)
    {
        const BYTE* p = s + i;
        if (p[0] != 0x80 || p[1] != 0x3D || p[6] != 0x00 || p[7] != 0x74 || p[8] != 0x0D || p[9] != 0xE8 || p[14] != 0x84 || p[15] != 0xC0) continue;
        const BYTE* const a = *(const BYTE* const*)(p + 2);
        if (a < img || a >= img + imgSize) continue;
        int k = 0; while (k < vn && v[k].a != a) k++;
        if (k == vn) { if (vn == 16) continue; v[vn++].a = a; }
        v[k].n++;
    }
    int best = -1, second = 0;
    for (int k = 0; k < vn; k++) if (best < 0 || v[k].n > v[best].n) best = k;
    for (int k = 0; k < vn; k++) if (k != best && v[k].n > second) second = v[k].n;
    const bool ok = best >= 0 && v[best].n >= 4 && v[best].n >= 3 * max(second, 1) && IsReadable(v[best].a, 1) && *v[best].a <= 1;
    if (ok) gameMenuActive = v[best].a;
    LogRecord(ok ? "  menu flag: GTAIV.exe+0x%IX (%d sites to %d)"
                 : "  menu flag: NOT found (best %IX with %d sites, runner-up %d): hdr_ui masks the pause menu by difference",
        best >= 0 ? (size_t)(v[best].a - img) : (size_t)0, best >= 0 ? v[best].n : 0, second);
}

// Fields a random readable address cannot all satisfy at once. This is the
// whole defence against accepting a wrong pattern match.
static bool ValidViewport(BYTE* p, char* why, size_t cap)
{
    if (!p)                          { _snprintf_s(why, cap, _TRUNCATE, "null"); return false; }
    if (!IsReadable(p, kViewportSize)) { _snprintf_s(why, cap, _TRUNCATE, "not 0x3F0 readable bytes"); return false; }

    int w = 0, h = 0;
    unsigned char persp = 0;
    float nearC = 0, farC = 0, fov = 0;
    __try
    {
        w     = *(int*)(p + kOffWidth);
        h     = *(int*)(p + kOffHeight);
        persp = *(unsigned char*)(p + kOffIsPerspective);
        nearC = *(float*)(p + kOffNear);
        farC  = *(float*)(p + kOffFar);
        fov   = *(float*)(p + kOffFov);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    { _snprintf_s(why, cap, _TRUNCATE, "faulted reading fields"); return false; }

    if (w < 16 || w > 16384 || h < 16 || h > 16384)
    { _snprintf_s(why, cap, _TRUNCATE, "size %dx%d implausible", w, h); return false; }
    if (persp > 1)
    { _snprintf_s(why, cap, _TRUNCATE, "mIsPerspective = %u, not a bool", persp); return false; }
    if (!(nearC > 0.0f && farC > nearC && farC < 1.0e7f))
    { _snprintf_s(why, cap, _TRUNCATE, "near %.4f far %.2f out of range", nearC, farC); return false; }
    if (!(fov > 1.0f && fov < 179.0f))
    { _snprintf_s(why, cap, _TRUNCATE, "fov %.2f out of range", fov); return false; }

    _snprintf_s(why, cap, _TRUNCATE, "%dx%d fov %.1f near %.3f far %.0f persp %u",
                w, h, fov, nearC, farC, persp);
    return true;
}

// GTAIV.exe's file version, as "a.b.c.d".
static bool ExeVersion(char* out, size_t cap)
{
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH))
        return false;
    return FileVersionOf(exe, out, cap);
}
static bool FileVersionOf(const char* exe, char* out, size_t cap)
{
    DWORD handle = 0;
    DWORD sz = GetFileVersionInfoSizeA(exe, &handle);
    if (!sz)
        return false;
    void* buf = malloc(sz);
    if (!buf)
        return false;
    bool ok = false;
    if (GetFileVersionInfoA(exe, handle, sz, buf))
    {
        VS_FIXEDFILEINFO* ffi = nullptr;
        UINT len = 0;
        if (VerQueryValueA(buf, "\\", (LPVOID*)&ffi, &len) && ffi && len)
        {
            _snprintf_s(out, cap, _TRUNCATE, "%u.%u.%u.%u",
                        HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                        HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
            ok = true;
        }
    }
    free(buf);
    return ok;
}


// ---------------------------------------------------------------------------
// The three checks
// ---------------------------------------------------------------------------

// `fromHook` says which thread this is. c8 is written on the render thread inside the hook, and a read from another thread can
// tear, giving a spurious check-3 failure that is logged once and so stands for good: check 3 runs only on the hook's thread.
// The poll thread covers the case where the hook never fires, so a failed vtable install does not leave an empty log.
static void Verdict(bool fromHook)
{
    if (toldVerdict || dead)
        return;
    BYTE* viewport = CurrentViewport();
    if (!viewport)
        return;
    if (!fromHook)
    {
        if (haveC8)
            return;                      // the hook will pronounce, and better
        if (GetTickCount() - armedAt < 10000)
            return;                      // give the hook 10 s to show up
    }

    Mat4 vpWvp, view, proj;
    float fov = 0, nearC = 0, farC = 0;
    unsigned char isPersp = 0;

    if (!ReadMat(viewport + kOffWorldViewProj, &vpWvp)) return;
    if (!ReadMat(viewport + kOffView, &view))           return;
    if (!ReadMat(viewport + kOffProj, &proj))           return;
    if (!ReadFloat(viewport + kOffFov,  &fov))          return;
    if (!ReadFloat(viewport + kOffNear, &nearC))        return;
    if (!ReadFloat(viewport + kOffFar,  &farC))         return;
    if (!IsReadable(viewport + kOffIsPerspective, 1))   return;
    __try { isPersp = *(unsigned char*)(viewport + kOffIsPerspective); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        // The assumption every reprojection rests on (forward Z), checked. A failure to read is not a failure of the frame: this is a
    // check, not an input.
    {
        static bool saidInvertZ = false;
        unsigned char invZ = 0;
        bool got = false;
        if (IsReadable(viewport + kOffInvertZ, 1))
        {
            __try { invZ = *(unsigned char*)(viewport + kOffInvertZ); got = true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { got = false; }
        }
        if (got && invZ && !saidInvertZ)
        {
            saidInvertZ = true;
            LogRecord("  camera: mInvertZInProjectionMatrix is SET (grcViewport+0x3E0 = %u): depth and motion vectors are WRONG", (unsigned)invZ);
        }
    }

        // Sane() rejects an all-zero matrix, so a block of zeros cannot pass check 1.
    if (!Sane(proj) || !Sane(view))
        return;                                  // wait for a real frame

        // Check 1: perspective, not orthographic. The radar, the HUD and the rain batches use the same registers with m[3][3] == 1.
    bool perspective = fabsf(proj.m[3][3]) < 0.0001f;

    // CHECK 2. FOV derived from the matrix against the FOV the viewport
    // reports. proj.m[1][1] = 1/tan(fovY/2) for a standard D3D projection.
    float derivedFov = 0.0f;
    bool fovOk = false;
    if (perspective && fabsf(proj.m[1][1]) > 0.0001f)
    {
        derivedFov = 2.0f * atanf(1.0f / proj.m[1][1]) * (180.0f / 3.14159265f);
        fovOk = fabsf(derivedFov - fov) < 2.0f;   // degrees
    }

    // CHECK 3. The strong one: the engine global against the shader constant,
    // two entirely separate paths to the same 16 floats. Loose epsilon because
    // the two are sampled a few instructions apart and the game may have moved
    // on to the next draw.
    bool c8Ok = haveC8 && NearlyEqual(vpWvp, lastC8, 0.01f);

    // Exactly once, whichever thread arrives first.
    if (InterlockedCompareExchange(&toldVerdict, 1, 0) != 0)
        return;

    LogDetail("");
    LogDetail("=== VERDICT (%ld perspective c8 writes, %ld ortho ignored) ===",
        c8Writes, c8Ortho);
    LogMat("projection (grcViewport+0x1C0):", proj);
    LogMat("view       (grcViewport+0x180):", view);
    LogDetail("  near %.4f  far %.2f  mFov %.2f", nearC, farC, fov);
    LogDetail("");
    LogDetail("  [%s] 0. engine says persp    mIsPerspective = %u (want 1)",
        isPersp == 1 ? "PASS" : "FAIL", isPersp);
    LogDetail("  [%s] 1. perspective        proj.m[3][3] = %.4f (want 0)",
        perspective ? "PASS" : "FAIL", proj.m[3][3]);
    LogDetail("  [%s] 2. fov agrees         derived %.2f vs viewport %.2f",
        fovOk ? "PASS" : "FAIL", derivedFov, fov);
    if (haveC8)
    {
        LogDetail("  [%s] 3. global == constant  grcViewport+0x100 vs vertex c8",
            c8Ok ? "PASS" : "FAIL");
        if (!c8Ok)
        {
            LogMat("      grcViewport+0x100:", vpWvp);
            LogMat("      vertex constant c8:", lastC8);
        }
    }
    else
    {
        LogDetail("  [ -- ] 3. global == constant  NO c8 SEEN - the constant hook"
            " never fired. See the hook section above.");
    }
    LogDetail("");
    if (isPersp == 1 && perspective && fovOk && c8Ok)
        LogDetail("  ALL FOUR AGREE. The camera is readable and correct.");
    else
        LogDetail("  NOT PROVEN. Do not build on this until every check passes.");
}

// Small 4x4 helpers for the camera maths below.
static void MulMat(const Mat4& a, const Mat4& b, Mat4* out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            float v = 0.0f;
            for (int k = 0; k < 4; k++)
                v += a.m[r][k] * b.m[k][c];
            out->m[r][c] = v;
        }
}

static float MaxDevFromIdentity(const Mat4& m)
{
    float d = 0.0f;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            float want = (r == c) ? 1.0f : 0.0f;
            float x = fabsf(m.m[r][c] - want);
            if (x > d) d = x;
        }
    return d;
}

// ---------------------------------------------------------------------------
// Double-precision path for the reprojection
// ---------------------------------------------------------------------------
// The camera sits near world 1745, so a float32 world-space view*proj and its inverse fail an identity round trip at about 0.03.
// So: compute in double, forming the translation (a difference of two large camera positions) before anything rounds it, and
// publish a view-space reprojection (a rotation plus a small translation, exact in float32), the form the engine uses for c72.

struct Mat4d { double m[4][4]; };

static void ToD(const Mat4& s, Mat4d* d)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            d->m[r][c] = (double)s.m[r][c];
}

static void ToF(const Mat4d& s, Mat4* f)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            f->m[r][c] = (float)s.m[r][c];
}

static void MulMatD(const Mat4d& a, const Mat4d& b, Mat4d* out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
        {
            double v = 0.0;
            for (int k = 0; k < 4; k++)
                v += a.m[r][k] * b.m[k][c];
            out->m[r][c] = v;
        }
}

static void InvertRigidD(const Mat4d& m, Mat4d* out)
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            out->m[r][c] = m.m[c][r];
    for (int c = 0; c < 3; c++)
    {
        double v = 0.0;
        for (int k = 0; k < 3; k++)
            v += m.m[3][k] * out->m[k][c];
        out->m[3][c] = -v;
    }
    out->m[0][3] = out->m[1][3] = out->m[2][3] = 0.0;
    out->m[3][3] = 1.0;
}

// ---------------------------------------------------------------------------
// Publish the camera to shared memory
// ---------------------------------------------------------------------------
// An unnamed section, rewritten every frame from EndScene, forty bytes: the two camera intrinsics the transport puts on the wire
// for FSR (nearCur, fovYRad), the size and far plane the first-valid line prints, and `valid`. A seqlock, not a mutex: the writer
// is the render thread and must never block on a reader, and a torn read is detectable. Unnamed so two game instances do not
// share one copy.

#define IVMV_MAGIC    0x564D5649u          // 'IVMV'
#define IVMV_VERSION  5u   // 5: the ReShade-era fields removed (4 had appended fovYRad)

#define IVMV_FLAG_LOGDEPTH_CONST  0x1u     // FusionFix's c227 pack was observed: shFlags, read in-process (no longer published)

#pragma pack(push, 4)
struct IvmvShared
{
    UINT32 magic;
    UINT32 version;
    volatile UINT32 seq;                   // odd while writing
    UINT32 frame;
    float  nearCur, farCur;
    UINT32 width, height;
    UINT32 valid;                          // 0 when this frame was not captured
        // FSR's dispatch wants cameraFovAngleVertical in radians, derived from the projection (the FOV it actually has; check 2
    // cross-checks it against grcViewport+0x2B8).
    float  fovYRad;                        // 0 until a perspective frame lands
};
#pragma pack(pop)

// The layout is a contract with any reader outside this process (none in the tree); the asserts keep a field from moving.
static_assert(sizeof(IvmvShared)              == 40, "size drift: bump IVMV_VERSION and say so in DECISIONS");
static_assert(offsetof(IvmvShared, seq)       ==  8, "offset drift: seq");
static_assert(offsetof(IvmvShared, nearCur)   == 16, "offset drift: nearCur");
static_assert(offsetof(IvmvShared, width)     == 24, "offset drift: width");
static_assert(offsetof(IvmvShared, valid)     == 32, "offset drift: valid");
static_assert(offsetof(IvmvShared, fovYRad)   == 36, "offset drift: fovYRad");

static HANDLE       mapHandle = nullptr;
static IvmvShared*  shared    = nullptr;
DLSSIV_HOT static UINT32       shFlags   = 0;

// ---------------------------------------------------------------------------
// Which viewport is the scene?
// ---------------------------------------------------------------------------
// Not simply the one at back buffer size: a full-resolution viewport that is not the scene exists, and a half-built one appears
// once a frame. The scene is where the geometry goes: draws are counted per viewport signature each frame, and the next frame
// captures whichever led (the scene wins by orders of magnitude). far is left out of the signature (it legitimately moves), and
// fov and near are quantised so small jitter does not split the key.

struct VpKey { int w, h, fovQ, nearQ; };
// Census writes the key with one 16-byte store (see there), which needs exactly
// this layout: four ints, w first, nothing else.
static_assert(sizeof(VpKey) == 16 && offsetof(VpKey, nearQ) == 12, "Census stores VpKey as four ints");

static bool KeyEq(const VpKey& a, const VpKey& b)
{
    return a.w == b.w && a.h == b.h && a.fovQ == b.fovQ && a.nearQ == b.nearQ;
}

DLSSIV_HOT static VpKey frameKey[12];
DLSSIV_HOT static LONG  frameKeyHits[12];
DLSSIV_HOT static int   frameKeys = 0;
DLSSIV_HOT static int   frameKeyLast = -1;   // the entry the last NoteFrameKey hit; -1 at each frame's start

DLSSIV_HOT static __declspec(align(16)) VpKey lastKey;   // signature of the most recent write
DLSSIV_HOT static VpKey sceneKey;
DLSSIV_HOT static bool  haveSceneKey = false;

// How long the scene key must hold before the data is valid: without a settle, a 16x16 probe viewport that led the first frames
// was published as the scene camera.
static LONG  sceneKeyStable = 0;
static const LONG kSceneKeySettleFrames = 30;

// Hysteresis, and a settle that never re-arms: a challenger must lead 12 consecutive frames to take over, and the settle counter
// is never reset after the first adoption. A blanked camera means zero vectors, which a temporal filter reads as no motion, so a
// wobble must not blank it; the settle is for start-up only.
static VpKey sceneKeyChallenger;
static bool  haveKeyChallenger = false;
static LONG  sceneKeyChallengeRun = 0;   // the hysteresis
static const LONG kSceneKeyTakeover = 12;

static void NoteFrameKey(const VpKey& k)
{
        // The last key first: Census calls this on about two thousand perspective c8 uploads a frame, and the key is nearly always the
    // one before. Same entry, same increment, so PickSceneKey reads the counts the search alone would give.
    if (frameKeyLast >= 0 && KeyEq(frameKey[frameKeyLast], k)) { frameKeyHits[frameKeyLast]++; return; }
    for (int i = 0; i < frameKeys; i++)
        if (KeyEq(frameKey[i], k)) { frameKeyHits[i]++; frameKeyLast = i; return; }
    if (frameKeys >= (int)(sizeof(frameKey) / sizeof(frameKey[0])))
        return;
    frameKey[frameKeys]     = k;
    frameKeyHits[frameKeys] = 1;
    frameKeyLast            = frameKeys;
    frameKeys++;
}

// Called at EndScene: whichever signature took the most draws this frame is the
// scene for the next one.
static void PickSceneKey()
{
    int  best = -1;
    LONG bestHits = 0;
    for (int i = 0; i < frameKeys; i++)
        if (frameKeyHits[i] > bestHits) { bestHits = frameKeyHits[i]; best = i; }

    if (best >= 0)
    {
        if (!haveSceneKey)
        {
            sceneKey       = frameKey[best];
            haveSceneKey   = true;
            sceneKeyStable = 0;
            LogDetail("  scene: %dx%d fov %d near %.3f took %ld of this frame's draws",
                sceneKey.w, sceneKey.h, sceneKey.fovQ,
                sceneKey.nearQ / 100.0f, bestHits);
        }
        else if (KeyEq(sceneKey, frameKey[best]))
        {
            haveKeyChallenger   = false;
            sceneKeyChallengeRun = 0;
            if (sceneKeyStable < kSceneKeySettleFrames) sceneKeyStable++;
        }
        else
        {
            if (haveKeyChallenger && KeyEq(sceneKeyChallenger, frameKey[best]))
                sceneKeyChallengeRun++;
            else
            {
                sceneKeyChallenger   = frameKey[best];
                haveKeyChallenger    = true;
                sceneKeyChallengeRun = 1;
            }
            if (sceneKeyChallengeRun >= kSceneKeyTakeover)
            {
                sceneKey             = sceneKeyChallenger;
                haveKeyChallenger    = false;
                sceneKeyChallengeRun = 0;
            // sceneKeyStable deliberately not reset (see above).
            }
            // One lost frame does not invalidate the camera published last frame, so settled time keeps accruing.
            if (sceneKeyStable < kSceneKeySettleFrames) sceneKeyStable++;
        }
    }
    frameKeys = 0; frameKeyLast = -1;
}

// The velocity pass's vertex-declaration cache.
#define VEL_DECL_CACHE 128
static IDirect3DVertexDeclaration9* velDeclPtr[VEL_DECL_CACHE] = { nullptr };
static bool velDeclSkin[VEL_DECL_CACHE] = { false };
static bool velDeclPosT[VEL_DECL_CACHE] = { false };   // POSITIONT: pre-transformed screen-space vertices
// Which extra streams a declaration uses: bits 0..2 are streams 1..3; VEL_XMASK_BEYOND is a stream above that, which nothing here
// carries. See VelPend::xMask.
#define VEL_XSTREAMS      3
#define VEL_XMASK_BEYOND  0x80u
static unsigned char velDeclXMask[VEL_DECL_CACHE] = { 0 };
static __forceinline unsigned char VelXMaskBit(WORD stream)
{
    return stream == 0 ? (unsigned char)0 : stream <= VEL_XSTREAMS ? (unsigned char)(1u << (stream - 1)) : (unsigned char)VEL_XMASK_BEYOND;
}
DLSSIV_HOT static bool velCurPosT = false;
DLSSIV_HOT static unsigned char velCurXMask = 0;
// Where a game quad keeps its texture coordinate (transport/quad_uv.h): the byte offset of TEXCOORD0 when the declaration is a
// FLOAT3 position at 0 and a FLOAT2 coordinate, both in stream 0 (the game's immediate vertex); kNoQuadUv otherwise. Read once
// per declaration, so the draw hook's question is one byte.
static const unsigned char kNoQuadUv = 0xFF;
static unsigned char velDeclQuadUv[VEL_DECL_CACHE];
DLSSIV_HOT static unsigned char velCurQuadUv = kNoQuadUv;
// The game's streams 1..3 as last bound, beside velCurVB for stream 0. Borrowed: an entry that carries one takes its own
// reference at the draw, as for stream 0.
DLSSIV_HOT static IDirect3DVertexBuffer9* velCurXVB[VEL_XSTREAMS]  = { nullptr };
DLSSIV_HOT static UINT                    velCurXOff[VEL_XSTREAMS] = { 0 };
DLSSIV_HOT static UINT                    velCurXStr[VEL_XSTREAMS] = { 0 };
DLSSIV_HOT static int  velDeclCount = 0;
DEVSTAT(static LONG velDeclPastCap = 0;)   // uncached declarations classified per call once the cache is full

DEVSTAT(static LONG statCurInvalidRun    = 0;)

// The camera the draw is actually rendered with: RAGE pins gViewInverse at c12..c15 and the engine uploads c0..c15 as one block
// per draw, so every draw carries its own camera. A better identity than the grcViewport object, which is pooled and can be
// caught half-written.
DLSSIV_HOT static float m4bCurViewInv[16];
DLSSIV_HOT static bool  m4bHaveViewInv   = false;
DLSSIV_HOT static float sceneViewInv[16];
DLSSIV_HOT static bool  haveSceneViewInv = false;
// The scene-camera test compares two 4x4s on every indexed draw, but its inputs change a few times a frame, and every write to
// them goes through CamMatSet, which bumps this serial. So the cached answer is exact, recomputed lazily at the point of use.
DLSSIV_HOT static unsigned camCmpSerial   = 0;
DLSSIV_HOT static unsigned camCmpFor      = 0xFFFFFFFFu;   // the serial the scene cache holds
DLSSIV_HOT static float    camCmpDRot = 0.0f, camCmpDPos = 0.0f;
DLSSIV_HOT static unsigned camCmpPrevFor  = 0xFFFFFFFFu;   // the serial the previous-camera cache holds
DLSSIV_HOT static float    camCmpPRot = 0.0f, camCmpPPos = 0.0f;
// The only way these three matrices are written: a stale camera here would silently misclassify scene draws, so there is no raw
// memcpy into them.
static inline void CamMatSet(float* dst, const void* src)
{
    memcpy(dst, src, 64);
    camCmpSerial++;
}

// The camera difference, four lanes at a time: the largest |a - b| over elements 0..11 (rotation rows) and 12..14 (position),
// as the scalar loops return. A NaN element is skipped as `if (e > d)` skips it (_mm_max_ps(e, d) returns d when unordered).
// Where a loop broke early its partial maximum was already past the threshold, so every threshold test reads the same
// (tools/perfproof/equiv_batch1.cpp).
// [equiv_c227: camdiff]
static __forceinline void CamDiffMax(const float* a, const float* b, float* rotMax, float* posMax)
{
    const __m128 absMask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    __m128 m = _mm_setzero_ps();
    m = _mm_max_ps(_mm_and_ps(_mm_sub_ps(_mm_loadu_ps(a + 0), _mm_loadu_ps(b + 0)), absMask), m);
    m = _mm_max_ps(_mm_and_ps(_mm_sub_ps(_mm_loadu_ps(a + 4), _mm_loadu_ps(b + 4)), absMask), m);
    m = _mm_max_ps(_mm_and_ps(_mm_sub_ps(_mm_loadu_ps(a + 8), _mm_loadu_ps(b + 8)), absMask), m);
    m = _mm_max_ps(m, _mm_movehl_ps(m, m));
    m = _mm_max_ps(m, _mm_shuffle_ps(m, m, 1));
    __m128 p = _mm_and_ps(_mm_sub_ps(_mm_loadu_ps(a + 12), _mm_loadu_ps(b + 12)), absMask);
    p = _mm_and_ps(p, _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1)));   // element 15 is not compared
    p = _mm_max_ps(p, _mm_setzero_ps());                                  // a NaN lane becomes the 0 it started as
    p = _mm_max_ps(p, _mm_movehl_ps(p, p));
    p = _mm_max_ps(p, _mm_shuffle_ps(p, p, 1));
    *rotMax = _mm_cvtss_f32(m);
    *posMax = _mm_cvtss_f32(p);
}
// [/equiv_c227: camdiff]

// The previous frame's scene view-inverse: gViewInverse row 3 is the camera's world position, so the frame-to-frame distance is
// the camera's actual travel with no matrix work.
DLSSIV_HOT static float sceneViewInvPrev[16];
DLSSIV_HOT static bool  haveSceneViewInvPrev = false;

// A cut is not motion, and a cut can be a turn as well as a jump: across one the vectors are meaningless and the upscaler must
// drop its history. Camera-mode switches, vehicle entry and exit and cutscene starts spin the camera without moving it far.
// 15 units: a 5-unit bar tripped on a fast hitch. 30 degrees: 3600 degrees a second at 120 fps, 900 at 30; a real cut clears it
// in one frame, and deliberate flicks at 40 fps reach it only rarely.
// The cost of being wrong is not symmetric: a missed cut ghosts until the history washes out; a false trip holds jitter off for
// every frame a turn stays above the bar, then the upscaler reconverges over its jitter cycle (8 frames at DLAA, 32 at
// Performance). The transport warns when resets exceed 30 per 300 frames.
static const float kCamCutUnits   = 15.0f;
static const float kCamCutDegrees = 30.0f;
DEVSTAT(static long  statCamCutTurns = 0;)         // cuts the angle caught that the distance did not

static bool CamCutThisFrame(float* outDist, float* outDeg)
{
    if (outDist) *outDist = 0.0f;
    if (outDeg)  *outDeg  = 0.0f;
    if (!haveSceneViewInv || !haveSceneViewInvPrev) return false;
    const float dx = sceneViewInv[12] - sceneViewInvPrev[12];
    const float dy = sceneViewInv[13] - sceneViewInvPrev[13];
    const float dz = sceneViewInv[14] - sceneViewInvPrev[14];
    const float d  = sqrtf(dx * dx + dy * dy + dz * dz);
    // Row 2 of a camera-to-world matrix is the view direction. Normalised
    // rather than assumed unit: the matrix comes from the game's own memory and
    // a scaled basis would give an out-of-range cosine, which acosf answers with
    // a NaN and every comparison below then reads false.
    const float ax = sceneViewInv[8],     ay = sceneViewInv[9],     az = sceneViewInv[10];
    const float bx = sceneViewInvPrev[8], by = sceneViewInvPrev[9], bz = sceneViewInvPrev[10];
    const float la = sqrtf(ax * ax + ay * ay + az * az);
    const float lb = sqrtf(bx * bx + by * by + bz * bz);
    float deg = 0.0f;
    if (la > 1e-6f && lb > 1e-6f)
    {
        float c = (ax * bx + ay * by + az * bz) / (la * lb);
        if (c >  1.0f) c =  1.0f;
        if (c < -1.0f) c = -1.0f;
        deg = acosf(c) * 57.29577951f;
    }
    if (outDist) *outDist = d;
    if (outDeg)  *outDeg  = deg;
        // Pure: two callers a frame ask (the shared block and the guide constants), so counting here would count each cut twice; the
    // once-a-frame caller counts.
    return d > kCamCutUnits || deg > kCamCutDegrees;
}

// ---------------------------------------------------------------------------
// The scene camera is the one the geometry is drawn with
// ---------------------------------------------------------------------------
// Two full-resolution cameras exist, about 1765 units apart (near 1.0 and near 0.05). If camCur and camPrev come from different
// ones, every pixel reprojects behind the previous near plane: a confident wrong vector everywhere. c12..c15 is gViewInverse
// (verified exact against grcViewport+0x140) and rides with every draw, which the per-draw camera gate below uses; the scene
// camera's identity is the c227 capture and its election.

static LONG  statCamJumps = 0;
static float statCamJumpWorst = 0.0f;
DLSSIV_HOT static bool  viewInvProven    = false;   // set only by the measurement below

DEVSTAT(static bool statEverValid        = false;)
static bool toldNeverValid       = false;

// One scene-camera sample, taken once per frame.
struct CamSnap
{
    Mat4  view, proj;
    float nearC, farC;
    int   w, h;
    bool  valid;
};
static CamSnap camCur, camPrev;
// viewPrev * projPrev in double, formed once a frame at the camPrev roll and read by every mover. No separate valid flag:
// camPrev's two writers are the roll (followed at once by the recompute) and the device-reset path (which only sets .valid), and
// the one reader checks camPrev.valid. Reassociating as prevWorld*(view*proj) is not bit identical, but its double error is far
// below the float32 rounding at ToF.
static Mat4d   camPrevVPd;

// The scene camera captured at its own depth-encode upload (see NoteSceneC227). Draw-count contests (the viewport pointer, the
// signature census, a per-draw c12 tally) all fail because an auxiliary pass can out-vote the scene.
static CamSnap sceneCand;
static float   sceneCandViewInv[16] = { 0 };
DLSSIV_HOT static bool    sceneCandHave       = false;
DEVSTAT(static LONG    statSceneCandFrames = 0;)   // frames a scene camera qualified
static LONG    statC227Seen        = 0;   // c227 uploads observed post-verdict
DEVSTAT(static LONG    statC227NoMatch     = 0;)   // ... that failed a capture gate
DEVSTAT(static LONG    statC227Shallow     = 0;)   // ... that failed GATE 4: a frustum too shallow to be the scene's (the night-shadow lights)

// The frame's candidates: every viewport that passes the gates offers itself once per UpdateTransform, and at DLAA gate 3 admits
// any screen-size perspective viewport (the phone's model camera, each Extra Night Shadows light, the water reflection while
// velW is 0). A stolen anchor is a camera cut on every frame it lasts. Two rules:
//   Live, at the upload: a candidate continuous with last frame's elected camera outranks one that is not; among continuous
//   ones the nearest to last frame's camera wins (a headlight shadow camera a few metres ahead is continuous too, with a
//   smaller near). Among the rest, on a genuine cut or at start-up, the smallest near.
//   Elected, at EndScene: the candidate whose view-inverse the most draws carried (a draw that misses the live anchor is
//   matched against the others, memoised per c12 upload). If that is not the live one, camCur and sceneViewInv switch to it
//   before Publish reads them, so a cut while an impostor is present costs one frame.
// Under a render scale gate 3 already rejects these (they report the output size).
// The [equiv_c227] markers here and at the three code sites below are read by tools/perfproof/gen_equiv_c227.py, which lifts
// the text between them verbatim into the offline proof of this selector. Move the code, move the markers.
// [equiv_c227: table]
struct SceneCandE { CamSnap s; float vinv[16]; LONG draws; float dist; unsigned char rank; };
static const int  kSceneCandMax = 12;
static SceneCandE sceneCands[kSceneCandMax];
static int        sceneCandN    = 0;                    // candidates this frame
static int        sceneCandLive = -1;                   // the live anchor; sceneCand is a copy of it
DLSSIV_HOT static unsigned camCandFor = 0xFFFFFFFFu;    // the serial the candidate lookup holds (see camCmpSerial)
DLSSIV_HOT static int      camCandIdx = -1;             // the candidate the current c12 matches, -1 none
// Not DEVSTAT and not to be fenced: this line is inside the [equiv_c227: table] region, and the offline proof reads this
// counter to learn whether a frame re-elected.
static LONG statSceneReelected = 0;                     // frames the busiest candidate was not the live one
static LONG statSceneCandEvict = 0;                     // candidates evicted for want of a slot
// [/equiv_c227: table]

// Analytic inverse of a standard D3D perspective projection:
//     P = [[a,0,0,0],[0,b,0,0],[0,0,c,-1],[0,0,d,0]]
//     clip = (x*a, y*b, z*c + w*d, -z)
//  => x = X/a, y = Y/b, z = -W, w = (Z + W*c)/d
// A general 4x4 inverse would work but loses precision here; this is exact.
static bool InvertProjection(const Mat4& P, Mat4* out)
{
    float a = P.m[0][0], b = P.m[1][1], c = P.m[2][2], d = P.m[3][2];
    if (fabsf(a) < 1e-12f || fabsf(b) < 1e-12f || fabsf(d) < 1e-12f)
        return false;
    memset(out, 0, sizeof(Mat4));
    out->m[0][0] = 1.0f / a;
    out->m[1][1] = 1.0f / b;
    out->m[2][3] = 1.0f / d;
    out->m[3][2] = -1.0f;
    out->m[3][3] = c / d;
    return true;
}

// A projection is the scene's only if it is a genuine clip-space perspective matrix; the ortho test rejects the origin phantom
// (m[3][3] == 1).
static bool PerspProj(const Mat4& P)
{
    if (fabsf(P.m[3][3]) > 0.01f)          return false;  // ortho: the phantom
    if (fabsf(P.m[2][3] + 1.0f) > 0.1f)    return false;  // not clip perspective
    if (fabsf(P.m[0][0]) < 1e-6f || fabsf(P.m[1][1]) < 1e-6f) return false;
    if (fabsf(P.m[3][2]) < 1e-9f)          return false;  // mid-rebuild
    return true;
}

// A view matrix's upper-left 3x3 is a rotation, so its rows are unit length (the phantom's were about 1e-4).
static bool RigidView(const Mat4& V)
{
    for (int r = 0; r < 3; r++)
    {
        float nr = V.m[r][0]*V.m[r][0] + V.m[r][1]*V.m[r][1] + V.m[r][2]*V.m[r][2];
        if (nr < 0.81f || nr > 1.21f) return false;   // outside 0.9 .. 1.1
    }
    return true;
}

// ---------------------------------------------------------------------------
// The scene camera, captured at the depth buffer's own near/far upload
// ---------------------------------------------------------------------------
// FusionFix computes a log-depth pack from the viewport being drawn and uploads it to c227 as
// [1/near, 1/log2(far/near), far/near, near]. At that upload pCurrentViewport is that viewport, so its matrices are the ones the
// depth was encoded with: the one signal that cannot be out-voted.
//   GATE 1  a coherent perspective camera        (rejects the ortho phantom)
//   GATE 2  the viewport near/far equal the pack  (rejects a torn read)
//   GATE 3  full back buffer resolution           (rejects mirror and water)
//   GATE 4  far/near of at least 4                (rejects the night-shadow lights)
//   rank:   continuous with last frame's elected camera first (see the candidate table)
//   tiebreak: smallest near within a rank
// and at EndScene the busiest candidate is elected over the live one. About 7 calls a frame.
static void PatchProjection(const CamSnap& cam);   // defined with the jitter machinery

static void NoteSceneC227(const float* pack)
{
    if (telemetryOn) { statC227Seen++; }
    float packNear, packFar;
    __try
    {
        packNear = pack[3];
        packFar  = pack[2] * pack[3];
        if (!(packNear > 0.0f) || !(packFar > packNear) ||
            fabsf(pack[0] * packNear - 1.0f) > 0.05f)
        { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }

    BYTE* vp = CurrentViewportFast();
    if (!vp) { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }

    CamSnap s; s.valid = false; s.w = s.h = 0;
    Mat4 vinv;
    unsigned char persp = 0;
    if (!(ReadMat(vp + kOffView,        &s.view) &&
          ReadMat(vp + kOffProj,        &s.proj) &&
          ReadMat(vp + kOffViewInverse, &vinv)   &&
          ReadFloat(vp + kOffNear, &s.nearC) &&
          ReadFloat(vp + kOffFar,  &s.farC)))
    { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }
    __try
    {
        s.w   = *(int*)(vp + kOffWidth);
        s.h   = *(int*)(vp + kOffHeight);
        persp = *(unsigned char*)(vp + kOffIsPerspective);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }

    // GATE 1. The phantom is ortho with a ~1e-4 view; both halves fail here.
    if (persp != 1 || !PerspProj(s.proj) || !RigidView(s.view) || !Sane(s.view))
    { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }

    // GATE 2. The viewport's own near/far must equal the encode it just
    // uploaded, which guards a torn read or a viewport swapped under us.
    float nerr = fabsf(s.nearC - packNear) / packNear;
    float ferr = fabsf(s.farC  - packFar)  / (packFar > 1.0f ? packFar : 1.0f);
    if (nerr > 0.02f || ferr > 0.02f) { DEVSTAT(if (telemetryOn) { statC227NoMatch++; }) return; }

        // GATE 3: mirror and water encode with the pack too, into smaller targets; the scene's is back buffer sized. velW/velH are 0
    // until the target exists (about frame 32); until then GATE 1 and the tiebreak carry it.
    if (velW && velH && ((UINT32)s.w != velW || (UINT32)s.h != velH))
        return;   // a smaller pass, not a capture failure

        // GATE 4: the frustum must be deep enough to be the scene's. FusionFix's Extra Night Shadows draw each light's shadow pass
    // through a screen-sized viewport a few units deep, which passes the gates above at DLAA and can out-draw the scene in the
    // election. Every real camera has far/near of 400 or more; the lights sit under 2. The bar is 4, counted apart from the others.
    if (s.farC < 4.0f * s.nearC)
    { DEVSTAT(if (telemetryOn) { statC227Shallow++; }) return; }

        // [equiv_c227: offer]
    // The same camera again? The scene's viewport uploads c227 about ten times a frame: one entry per camera, matched as the draw
    // gate matches a draw; a repeat with a smaller near refreshes the snapshot.
    s.valid = true;
    int idx = -1;
    for (int i = 0; i < sceneCandN; i++)
    {
        float dR = 0.0f, dP = 0.0f;
        CamDiffMax(&vinv.m[0][0], sceneCands[i].vinv, &dR, &dP);
        if (dR < 1e-4f && dP < 0.05f) { idx = i; break; }
    }
    if (idx >= 0)
    {
        if (s.nearC < sceneCands[idx].s.nearC)
        {
            sceneCands[idx].s = s;
            if (idx == sceneCandLive) { sceneCand = s; camCur = s; }
        }
        return;
    }

        // The rank: continuous with last frame's elected camera, or not. The bars are the cut detector's: within kCamCutUnits, and a
    // rotation element within 0.6 (about 37 degrees). With no previous camera, or after a cut, every candidate is rank 1 and near
    // decides. dist (units plus scaled rotation) decides between two continuous candidates.
    unsigned char rank = 1;
    float dist = 0.0f;
    if (haveSceneViewInvPrev)
    {
        float cR = 0.0f, cP = 0.0f;
        CamDiffMax(&vinv.m[0][0], sceneViewInvPrev, &cR, &cP);
        if (cR < 0.6f && cP < kCamCutUnits) rank = 0;
        dist = cP + 10.0f * cR;
    }
    if (sceneCandN >= kSceneCandMax)
    {
                // Full: the passes come before the scene, so the scene must still find a slot. Evict the non-live entry with the fewest draws,
        // rank 1 before any rank 0 (the continuous candidates are the ones the election is between).
        int victim = -1;
        for (int i = 0; i < sceneCandN; i++)
        {
            if (i == sceneCandLive) continue;
            const SceneCandE& c = sceneCands[i];
            if (victim < 0 || c.rank > sceneCands[victim].rank
                || (c.rank == sceneCands[victim].rank && c.draws < sceneCands[victim].draws)) victim = i;
        }
        if (victim < 0) return;
        DEVSTAT(if (telemetryOn) { statSceneCandEvict++; })
        idx = victim;
    }
    else idx = sceneCandN++;
    {
        SceneCandE& e = sceneCands[idx];
        e.s = s; memcpy(e.vinv, vinv.m, 64); e.draws = 0; e.dist = dist; e.rank = rank;
    }
    camCandFor = 0xFFFFFFFFu;                  // the draw gate's lookup must see the new entry

        // The live anchor: a better rank; within rank 0 the candidate nearest last frame's camera; within rank 1 the smaller near.
    if (sceneCandLive >= 0)
    {
        const SceneCandE& L = sceneCands[sceneCandLive];
        const bool better = rank < L.rank
                         || (rank == L.rank && (rank == 0 ? dist < L.dist : s.nearC < L.s.nearC));
        if (!better) return;
    }
    sceneCandLive = idx;
    sceneCand = s;
    memcpy(sceneCandViewInv, vinv.m, 64);
    sceneCandHave = true;

        // Make it current now, not at EndScene: this upload precedes the scene's own draws, and the draw hook tests each draw's c12
    // against sceneViewInv, gated on camCur.valid. EndScene still copies the same values.
    camCur           = s;
    CamMatSet(sceneViewInv, vinv.m);
    haveSceneViewInv = true;

        // The jitter write belongs here: the one place per frame where the scene viewport is known and its matrices have just been read
    // unjittered (`s` is captured before the write, so camCur stays jitter-free). One Halton pair, guarded by patchedThisFrame.
    PatchProjection(s);
    // [/equiv_c227: offer]
}

static bool OpenShared()
{
    mapHandle = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   0, sizeof(IvmvShared), nullptr);
    if (!mapHandle)
    {
        LogRecord("  publish: CreateFileMapping failed (%lu)", GetLastError());
        return false;
    }
    shared = (IvmvShared*)MapViewOfFile(mapHandle, FILE_MAP_ALL_ACCESS, 0, 0,
                                        sizeof(IvmvShared));
    if (!shared)
    {
        LogRecord("  publish: MapViewOfFile failed (%lu)", GetLastError());
        CloseHandle(mapHandle);
        mapHandle = nullptr;
        return false;
    }
    memset(shared, 0, sizeof(IvmvShared));
    shared->magic   = IVMV_MAGIC;
    shared->version = IVMV_VERSION;
    LogDetail("  publish: section ready, %u bytes", (unsigned)sizeof(IvmvShared));
    return true;
}

static bool toldPublish  = false;
#if DLSSIV_DEVLOG
static bool toldSelfTest = false;
#endif

static void Publish()
{
    if (!shared)
        return;

        // Said once when it does not happen, naming each term of the condition. Above the early return, since a session whose camera was
    // never valid is the one it exists for. With the c227 count: zero means the depth-encode upload never happened (FusionFix absent
    // or not on its Vulkan path). Counted from the first call, since Publish starts only after the verdict.
    static long pubFirstFrame = -1;
    if (pubFirstFrame < 0) pubFirstFrame = (long)frames;
    if (!toldPublish && !toldNeverValid && (long)frames - pubFirstFrame > 300)
    {
        toldNeverValid = true;
        LogRecord("  publish: no valid frame in %ld frames since the verdict: camera captured %d, scene key %d, settled %ld of %ld, c227 uploads %ld%s",
            (long)frames - pubFirstFrame, camPrev.valid ? 1 : 0, haveSceneKey ? 1 : 0, sceneKeyStable, kSceneKeySettleFrames,
            statC227Seen,
            statC227Seen == 0 ? "   <-- NONE: the upload the scene camera is elected from never came (FusionFix missing, or not on Vulkan?)" : "");
    }

    // Nothing captured this frame: say so, rather than republish stale matrices
    // as though they were current. A consumer that cannot tell the difference
    // would reproject against the wrong frame and never know.
    if (!camCur.valid)
    {
        shared->seq++;
        MemoryBarrier();
        shared->valid = 0u;
        shared->frame = (UINT32)frames;
        MemoryBarrier();
        shared->seq++;
                // Count the blank: a blank after the first valid frame is a real on-screen zero-vector frame.
#if DLSSIV_DEVLOG
        if (statEverValid) statCurInvalidRun++;
#endif
        return;
    }

        // A degenerate projection is not published: the analytic inverse is the gate. Until the first valid frame it also feeds the
    // self-test the first-valid line prints (invProj * proj must be identity).
    Mat4 invProj;
    if (!InvertProjection(camCur.proj, &invProj))
        return;
#if DLSSIV_DEVLOG                // the self-test feeds development-log lines only
    float self = -1.0f;
    if (!toldPublish)
    {
        Mat4 ident;
        MulMat(camCur.proj, invProj, &ident);
        self = MaxDevFromIdentity(ident);
        if (!toldSelfTest) { toldSelfTest = true; LogDetail("  publish: self-test  max|proj * invProj - I| = %.9g", self); }
    }
#endif

    shared->seq++;                            // -> odd, write in progress
    MemoryBarrier();

    shared->nearCur  = camCur.nearC;
    shared->farCur   = camCur.farC;
    // m[1][1] is cot(fovY/2), so fovY = 2*atan(1/m11). Guarded, because a
    // degenerate projection would otherwise publish an infinity that a consumer
    // would take at face value.
    shared->fovYRad  = (camCur.proj.m[1][1] > 1e-6f)
                     ? 2.0f * atanf(1.0f / camCur.proj.m[1][1]) : 0.0f;
    shared->width    = (UINT32)camCur.w;
    shared->height   = (UINT32)camCur.h;
    shared->frame    = (UINT32)frames;
        // Not valid until the scene viewport has held steady (see kSceneKeySettleFrames), nor across a cut: distance and angle from the
    // one detector the transport uses, so the frame blanked here and the frame the upscaler resets on are the same.
    // The frame time and the frame before, for the cut line only (development build: one QPC a frame while the log is on).
    DEVSTAT(static LONGLONG camQpcPrev = 0; static float camDegPrev = 0.0f; float camDtMs = 0.0f;)
    DEVSTAT(if (telemetryOn)
    {
        LONGLONG now = 0; QueryPerformanceCounter((LARGE_INTEGER*)&now);
        if (!qpcFreq.QuadPart) QueryPerformanceFrequency(&qpcFreq);
        if (camQpcPrev && qpcFreq.QuadPart) camDtMs = (float)((double)(now - camQpcPrev) * 1000.0 / (double)qpcFreq.QuadPart);
        camQpcPrev = now;
    })
    float cutD = 0.0f, cutDeg = 0.0f;
    const bool camJumpedThisFrame = CamCutThisFrame(&cutD, &cutDeg);
    if (camJumpedThisFrame)
    {
        statCamJumps++;
        if (telemetryOn)
        {
            // A cut the angle caught and the distance would have missed.
            DEVSTAT(if (cutD <= kCamCutUnits) statCamCutTurns++;)
            if (cutD > statCamJumpWorst) statCamJumpWorst = cutD;
            // An event line with the frame before it, which tells a flick (the turn ramps over frames) from a camera switch (a turn from
            // nothing). At log=2 on purpose: log=1's session-over line carries the count and the worst jump. Capped at 120 a session.
            if (statCamJumps <= 120)
                LogHealth("  camera CUT at %ld: %.2f units and %.1f degrees in one frame of %.1f ms"
                    " (the frame before turned %.1f degrees; near %.3f -> %.3f, far %.0f -> %.0f)."
                    " Blanked: across a cut there is no history.",
                    (long)frames, cutD, cutDeg, camDtMs, camDegPrev, camPrev.nearC, camCur.nearC,
                    camPrev.farC, camCur.farC);
        }
    }
    DEVSTAT(if (telemetryOn) camDegPrev = cutDeg;)

        // Across a cut there is no previous frame: the frame is blanked rather than published with a confident wrong vector on every
    // pixel. camCur.valid is already true past the early return; it is named so the contract reads whole.
    shared->valid    = (camCur.valid && camPrev.valid && !camJumpedThisFrame &&
                        haveSceneKey &&
                        sceneKeyStable >= kSceneKeySettleFrames) ? 1u : 0u;
        // Track the blanks: valid = 0 makes the effect emit a zero vector, which tells the temporal filter nothing moved. Only blanks that
    // are faults count; the first frames are invalid by design while the scene viewport settles.
    if (shared->valid)
    {
        DEVSTAT(if (!statEverValid)
        {
            statEverValid       = true;
        })
                // Said when a blank run ends, not only in a periodic report.
#if DLSSIV_DEVLOG
        if (statCurInvalidRun > 0)
            LogHealth("  publish: BLANK RUN ENDED after %ld frame(s) at frame %ld."
                " Every pixel had a zero motion vector for that whole stretch.",
                statCurInvalidRun, (long)frames);
        statCurInvalidRun = 0;
#endif
    }
#if DLSSIV_DEVLOG
    else if (statEverValid) statCurInvalidRun++;
#endif

    MemoryBarrier();
    shared->seq++;                            // -> even, write complete

        // The "no valid frame" one-shot is at the top of this function, above the early return for an invalid camera.
    if (!toldPublish && shared->valid)
    {
        toldPublish = true;
        LogHealth("  publish: first valid frame (%ux%u, near %.4f far %.1f, self-test %.9g)",
            shared->width, shared->height, shared->nearCur, shared->farCur, self);
    }
}

// ---------------------------------------------------------------------------
// The jitter maths
// ---------------------------------------------------------------------------
// The jitter reaches the draws through the per-draw c8 patch; PatchProjection writes no game memory.
// Post-multiplying by a jitter matrix J equals jittering the projection alone: W*V*(P*J) = (W*V*P)*J. With row vectors J is
// identity with J[3][0] = jx, J[3][1] = jy, so column0 += jx * column3 and column1 += jy * column3. For P alone column 3 is
// (0, 0, P[2][3], 0), and GTA IV has P[2][3] = -1:
//     P[2][0] -= jx_ndc;   P[2][1] -= jy_ndc
// The "M[2][0] += jitter" recipe in the DLSS, XeSS and Unreal docs assumes M[2][3] = +1 and would jitter the wrong way here
// (dxvk-remix carries a Sign(proj[2][3]) correction for this).
//     jx_ndc = +2 * Jx_px / width      jy_ndc = -2 * Jy_px / height
// Jitter leaves clip.z and clip.w untouched, so FusionFix's logarithmic depth is unaffected.

// Jitter follows the resolve (UpdateJitterAuto): on exactly when the transport resolves. Jitter with nothing consuming it makes the
// image worse (alpha-test crawl, reflection noise, HUD shimmer), so jitter and the resolve land together or neither does.

// The transport (transport/DlssIvTransport.h, included later) is the only resolve authority: with it off nothing may jitter.
static bool IvtResolveState(bool* transportOn);
static bool IvtJitterSuppressed();

// Once per frame from EndScene: the resolve's state decides the jitter.
static void UpdateJitterAuto()
{
    bool nowActive = false;
    {
        bool transportOn = false;
        const bool transportActive = IvtResolveState(&transportOn);
        if (transportOn) nowActive = transportActive;   // in-band jitter goes with the mod's frames, or not at all
    }
    if (nowActive != resolveActive)
    {
        resolveActive = nowActive;
        LogRecord("  resolve %s", nowActive ? "on" : "off");
    }
        // Jitter follows the resolve.
    bool want = resolveActive;
    if (IvtJitterSuppressed()) want = false;   // a cut: no jitter until the vectors are trusted again
    if (want != jitterEnabled)
    {
        jitterEnabled = want;
                // Edge triggered, one line per change, the first forty only: a stolen scene camera can flip this at every phone raise.
        DEVSTAT(static LONG edges = 0;)
        DEVSTAT(if (++edges <= 40) LogHealth("  jitter %s%s", want ? "on" : "off", edges == 40 ? " (the last of these lines this session)" : "");)
    }
}

// Halton, 1-indexed. FSR2 indexes from 1 specifically so the sequence never
// produces a null vector, which would waste a phase on no jitter at all.
static float Halton(int index, int base)
{
    float f = 1.0f, r = 0.0f;
    while (index > 0)
    {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

// Frames patched (development log), and the once-a-frame guard.
DEVSTAT(static LONG  patchFrames    = 0;)    // frames patched
DLSSIV_HOT static bool  patchedThisFrame = false;

// Called once per frame on the scene viewport, right after camCur is captured. Writes no game memory: it derives the frame's
// offsets from the projection the camera carries.
static void PatchProjection(const CamSnap& cam)
{
    if (!jitterEnabled || patchedThisFrame)
        return;
    if (cam.w <= 0 || cam.h <= 0)
        return;

        // m[2][0] is 0 in an unjittered GTA IV projection. The offsets are applied per draw to a copy in hkSetVsConstF and never reach
    // proj, so they cannot accumulate there.

    jitterPxX = (Halton(jitterPhase + 1, 2) - 0.5f) * jitterAmplitude;
    jitterPxY = (Halton(jitterPhase + 1, 3) - 0.5f) * jitterAmplitude;
    jitterPhaseUsed = jitterPhase;
    jitterPhase = (jitterPhase + 1) % jitterPhases;

        // In pixels of the target the scene renders into. cam.w/h already follow the scale (the engine fills grcViewport from the D3D
    // viewport this layer rewrites); rsW/rsH keep it true regardless.
    const float jw = (rsActive && rsW) ? (float)rsW : (float)cam.w;
    const float jh = (rsActive && rsH) ? (float)rsH : (float)cam.h;
    jitterNdcX =  2.0f * jitterPxX / jw;
    jitterNdcY = -2.0f * jitterPxY / jh;

        // Applied per draw in hkSetVsConstF; no viewport memory is touched, so the engine's proj stays what it composes, which keeps
    // FusionFix's SSAO consistent (it reads proj._31/_32 from that object and would read the offsets as camera skew).
    patchedThisFrame = true;
    DEVSTAT(InterlockedIncrement(&patchFrames);)
}

// ---------------------------------------------------------------------------
// Per-object motion: the draw records
// ---------------------------------------------------------------------------
// Few draws a frame carry a world matrix that changed, so moving geometry is re-rendered into a velocity buffer rather than
// tagged in the G-buffer. The tracker is M4d, below.

// The bespoke transport's injection rule runs on every draw record, before the
// draw executes (transport/DlssIvTransport.h, included later).
static __declspec(noinline) void IvtOnDraw(const DrawRec& rec);
// Its inline front, which is what NoteDrawRec calls; defined right after
// IvtOnDraw, where the transport's state is in scope.
static __forceinline void IvtOnDrawFront(const DrawRec& rec);

// Defined further down with the render-scale code; NoteDrawRec needs it here.

static void NoteDrawRec(const DrawRec& rec)
{
        // Gated: every reader of these two is behind telemetryOn or a log call. telemetryOn, not censusOn, because the seam line is
    // LogHealth at level 2.
    DEVSTAT(if (telemetryOn) { seamDrawIdx = ++seamDrawsSeen; })
        // The scene buffer identifies itself: it is the render-size, single-level FP16 target the scene draws into. Size cannot separate
    // it from the two post-chain ping-pong buffers, and "the scene draws into it" cannot separate it from the reflection target.
    // Lazy pairing: every FP16 target normally pairs at creation; this catches one made before the hooks armed (another mod may delay
    // them). The mod's own counterpart is skipped: it is kept out of fpTab and its surface is sceneBufSurf.
    if (rec.scene && curRT0 && !sceneBufTex && fpTabN < 24 && !FpSceneForOurSurf(curRT0))
    {
        bool known = false;
        for (int i = 0; i < fpTabN; i++) if (fpTab[i].surf == curRT0) { known = true; break; }
        if (!known)
        {
            IDirect3DTexture9* parent = nullptr;
            if (SUCCEEDED(curRT0->GetContainer(__uuidof(IDirect3DTexture9), (void**)&parent)) && parent)
            {
                D3DSURFACE_DESC pd = {};
                if (SUCCEEDED(parent->GetLevelDesc(0, &pd)) && pd.Format == D3DFMT_A16B16G16R16F)
                {
                    AcquireSRWLockExclusive(&rsTablesLock);
                    const int fn = fpTabN;              // asked again under the lock: a creating thread may have filled the table since the test above
                    if (fn < 24)
                    {
                        parent->AddRef();   // one per entry, returned in hkReset: see fpTab's declaration
                        FpRt& e = fpTab[fn];
                        e.tex = parent; e.surf = curRT0; e.w = pd.Width; e.h = pd.Height;
                        e.levels = parent->GetLevelCount();
                        _ReadWriteBarrier(); fpTabN = fn + 1;   // the count LAST: see rsTablesLock
                    }
                    ReleaseSRWLockExclusive(&rsTablesLock);
                    LogDetail("  seam: FP16 render target #%d found by BIND, not paired at creation"
                        "  tex %p surf %p  %ux%u lv%u", fn + 1, (void*)parent, (void*)curRT0,
                        pd.Width, pd.Height, parent->GetLevelCount());
                }
                parent->Release();   // GetContainer's own; the entry above took its own
            }
        }
    }
    if (rec.scene && curRT0 == sceneBufSurf)
        seamSceneWrote = true;   // one compare, the common case
        // Design A: the scene draws into the mod's counterpart, which is never in fpTab, so the fpTab scan below cannot match. A scene
    // draw landing in a counterpart is the scene buffer by construction (counterparts exist only for full-screen FP16 targets).
    else if (rec.scene && curRT0 && fpScN)
    {
        if (FpScene* e = FpSceneForOurSurf(curRT0))
        {
            if (sceneBufTex != e->ourTex)
                LogHealth("  designA: the scene renders into counterpart %p (%ux%u);"
                    " the resolve will land in the game's own %p",
                    (void*)e->ourTex, e->w, e->h, (void*)e->gameTex);
            sceneBufTex = e->ourTex; sceneBufSurf = e->ourSurf;
            sceneDstTex = e->gameTex;      // set on a confirmed scene draw, never on a bare bind
            seamSceneWrote = true;
        }
    }
    else if (rec.scene && curRT0)                 // DLAA: the game's own buffer, found in fpTab
    {
        const UINT wantW = rsActive ? rsW : rsOutW, wantH = rsActive ? rsH : rsOutH;
        for (int i = 0; i < fpTabN; i++)
        {
            if (fpTab[i].surf != curRT0) continue;
            if (fpTab[i].levels != 1) break;                 // a mip chain is not the scene
            if (!wantW || fpTab[i].w != wantW || fpTab[i].h != wantH) break;
            if (sceneBufTex != fpTab[i].tex)
                LogHealth("  seam: the scene buffer is %p, %ux%u lv%u (was %p)", (void*)fpTab[i].tex,
                    fpTab[i].w, fpTab[i].h, fpTab[i].levels, (void*)sceneBufTex);
            sceneBufTex = fpTab[i].tex; sceneBufSurf = fpTab[i].surf; seamSceneWrote = true;
            break;
        }
    }

        // The game's draws only: the mod's own HDR encode quad also reaches the back buffer through the draw hook.
    DEVSTAT(if (telemetryOn && rec.bb && !rsOwn) seamBbSeen++;)
        // The sharpen. With sharpening off this costs one always-false bool. shpOn is the run gate the live path moves, shpBuilt the build
    // gate; this site sits above the transport include and cannot read ivtCfg, hence two bools.
    // On a frame with a HUD the first qualifying draw is the first HUD sprite (FusionFix's console gamma draws from its onEndScene,
    // after the interface). The opaque-quad skip is for a frame with no interface draw, where FusionFix's quad is the first
    // orthographic back-buffer draw, and for the pause path's opaque redraw. A frame with no qualifying draw is sharpened before
    // FusionFix's copy of the back buffer (hkStretchRect), or at EndScene when nobody copies it.
    // shpBbSeen: the first back-buffer draw after the seam is the composite (or the last pass of FusionFix's anti-aliasing); from
    // there a copy out of the back buffer takes a finished picture.
    if (shpOn && !SHP_G10() && !shpDone && seamThisFrame && rec.bb)
    {
        shpBbSeen = true;
        if (rec.ortho && drawDev && !IvtIsOpaqueQuad(rec, drawDev))
        {
            shpDone = true;      // before the pass: the mod's own quad re-enters here
            ShpSharpen(drawDev);
        }
    }
    if (rec.sceneKnown && rec.scene) { dsAtSceneDraws = curDS; DsCountSceneDraw(curDS); }
    IvtOnDrawFront(rec);
}



DLSSIV_HOT static UINT_PTR m4bCurVB = 0;
DLSSIV_HOT static UINT_PTR m4bCurIB = 0;
DLSSIV_HOT static float    m4bCurWorld[16];
DLSSIV_HOT static bool     m4bHaveWorld = false;
// Bumped on every write that reaches m4bCurWorld, before the copy, for the reason boneShadowSerial is.
DLSSIV_HOT static unsigned __int64 m4bWorldSerial = 0;

// ---------------------------------------------------------------------------
// The engine-side velocity target
// ---------------------------------------------------------------------------
// Per-object velocity is rendered in-engine, where the geometry is; the transport's guide pass reads it through DXVK's interop.

// The full Vulkan headers (external/vulkan, Khronos) ahead of the DXVK interop
// stand-ins, for the transport's imports; VK_NO_PROTOTYPES keeps the link clean.
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include "dxvk/dxvk_interop_min.h"

static IDirect3DTexture9* velTex     = nullptr;
DLSSIV_HOT static IDirect3DSurface9* velSurf    = nullptr;
static ID3D9VkInteropTexture* velInterop = nullptr;
static bool   velTried = false;

// The transport's own shaders (fxc-compiled headers), ahead of the transport.
#include "shaders/ivt_fs_vs.h"
#include "shaders/ivt_guides_ps.h"
#include "shaders/ivt_dedither_ps.h"        // the game's dither smoothed before the upscaler
#include "shaders/ivt_dedither_home_ps.h"   // the pixels the pass changed, written into the scene buffer itself
#include "shaders/ivt_dedither_count_ps.h"  // is anything dithered at all: the pass is left out while the answer is no
#include "shaders/ivt_steady_ps.h"          // the depth the post chain reads, resolved over time
#include "shaders/ivt_steady_home_ps.h"     // what it holds, written back into the texture the post chain reads
#include "shaders/ivt_rain_ps.h"            // the rain, drawn into the mod's own layer, added to the upscaled picture
#include "shaders/ivt_corona_ps.h"          // the depth the game's corona test reads, held while the camera stands still
#include "hdr.h"                          // HDR: state, the display, the pre-tonemap capture
// The velocity drain, declared here because IvtInject, in the header below, is the one choke point every injection passes through
// (the seam, the pause quad, the back-buffer read-back, EndScene), and the batch must be empty before the guide pass reads velSurf.
static void VelFlushBatch(IDirect3DDevice9* dev, bool rebind);
static void VelBatchDrop();
// The drain's camera when the injection body runs it: VelDrain encodes depth from the scene camera's near and far, and on the
// worker the render thread's camCur may already be invalidated by hkEndScene. So the pair travels in the command, captured at the
// post, and the body sets these three around its one call of the drain (the same thread sets, drains and clears). Elsewhere the
// drain reads camCur on the thread that owns it.
static bool  velCamFromCmd = false, velCamCmdValid = false;
static float velCamCmdNear = 0.05f, velCamCmdFar = 4500.0f;
// And the frame's jitter, for c246: the drain subtracts it from every mover's vector, and the render-thread state it was formed from
// (patchedThisFrame, jitterNdcX/Y) is taken down even earlier at the frame's end. It travels in the command too.
static float velJitCmdX = 0.0f, velJitCmdY = 0.0f;
DEVSTAT(static LONG velDrainJitterGone = 0;)       // worker drains of a jittered frame that found patchedThisFrame already down
DEVSTAT(static LONG velDrainAfterFrameEnd = 0;)    // worker drains that found camCur already invalidated

#include "transport/ivt_latency.h"  // the latency instrument's render-thread half: before the device thread, whose Present stamps it
#include "devthread/devthread.h"   // threaded=1: DXVK on its own thread; before the transport, which drains it
// See RsOwnCall. dtWorkerTid is 0 until the worker starts and a thread id is never 0, so this is always true with threaded=0 and on
// every game thread.
static bool RsGuardHere() { return __readfsdword(0x24) != dtWorkerTid; }
// The tracker's inputs rebuilt on the device thread, and the verifier the injection calls (devthread/dt_trk.h, included after the
// hooks it mirrors).
static void TrkArm(); static void TrkSendWindow(); static void TrkSendScene(bool sceneNow);
static void TrkSendFrame();

#include "transport/DlssIvTransport.h"
#include "cpu_place.h"          // the CPU placement: the pin and the wall, ticked from IvtTick
#include "shaders/rcas_ps.h"
#include "hdr_encode.h"          // the HDR display encode pass, after the shared pass machinery

// Made once, on the first frame that reaches the pass. Nothing is created when
// sharpening is off, because nothing calls this.
static bool ShpEnsure(IDirect3DDevice9* dev)
{
    if (shpPS && shpTex) return true;
    if (shpTried) return false;
    shpTried = true;
    if (!rsOutW || !rsOutH) { shpTried = false; return false; }   // retry once the size is known
    RsOwnCreateCall guard;          // the mod's: never scaled, never tabled
        // The pass needs the full-screen quad, which must exist even with transport=0.
    bool ok = IvtEnsureFsQuad(dev);
    if (!ok)
    {
        LogRecord("  sharpen: the full-screen quad was refused: sharpening off");
        IvtWarn("sharpness=%.2f is set, but the sharpening pass could not be created (its full-screen quad was refused): no sharpening this session",
                    (double)ivtCfg.sharpness);
        return false;   // nothing below was created, so the block at the end must not name a second cause
    }
    ok = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kRcasPS, &shpPS)) && shpPS;
        // The back buffer's own format: the scratch takes a copy of the back buffer and is drawn back into it, and a fixed A8R8G8B8 would
    // round-trip an FP16 HDR back buffer through 8 bits.
    D3DFORMAT shpFmt = D3DFMT_A8R8G8B8;
    if (backbufferSurf)
    {
        D3DSURFACE_DESC bd = {};
        if (SUCCEEDED(backbufferSurf->GetDesc(&bd))) shpFmt = bd.Format;
    }
        // One scratch for two passes: the HDR encode keeps a target of exactly this shape, used at Present, and this pass uses it
    // mid-frame; they never hold data at once, so it is shared by reference instead of a second 66 MB in the 32-bit space.
    if (ok && hdrTex && hdrSurf && HdrEncodeScratchMatches(rsOutW, rsOutH, shpFmt))
    {
        shpTex = hdrTex;  shpTex->AddRef();
        shpSurf = hdrSurf; shpSurf->AddRef();
    }
    else
    {
        if (ok)
            ok = SUCCEEDED(dev->CreateTexture(rsOutW, rsOutH, 1, D3DUSAGE_RENDERTARGET,
                                              shpFmt, D3DPOOL_DEFAULT, &shpTex, nullptr)) && shpTex;
        if (ok) ok = SUCCEEDED(shpTex->GetSurfaceLevel(0, &shpSurf)) && shpSurf;
    }
    if (!ok)
    {
        if (shpSurf) { shpSurf->Release(); shpSurf = nullptr; }
        if (shpTex)  { shpTex->Release();  shpTex  = nullptr; }
        if (shpPS)   { shpPS->Release();   shpPS   = nullptr; }
        LogRecord("  sharpen: the %ux%u pass could not be created: sharpening off", rsOutW, rsOutH);
        IvtWarn("sharpness=%.2f is set, but the %ux%u sharpening pass could not be created (video memory, or the shader was refused):"
                " no sharpening this session", (double)ivtCfg.sharpness, rsOutW, rsOutH);
        return false;
    }
        // AMD's constant is in stops, 0 the sharpest, each whole step halving it. FSR2 maps a 0..1 slider as stops = (1 - s) * 2, the
    // calibration players know. This runs once; the live path recomputes shpCon with the same two lines (a call here would cross the
    // transport include).
    const float stops = (1.0f - ivtCfg.sharpness) * 2.0f;
    shpCon = powf(2.0f, -stops);
    shpFmtUsed = shpFmt;                        // only reached on success
    LogRecord("  sharpen: RCAS at %ux%u, sharpness %.2f (%.2f stops, attenuation %.4f)",
        rsOutW, rsOutH, ivtCfg.sharpness, stops, shpCon);
    LogHealth("  sharpen: scratch format %d (the back buffer's), denoise gate on, after the tonemap and before the HUD", (int)shpFmt);
    return true;
}

// The ceiling RCAS measures headroom against, in back buffer units (white is 1). AMD's 1.0 unless the back buffer is FP16 (an
// 8-bit one cannot hold a value above white) and values above white reach the display (hdrAboveWhiteShown). With FusionFix's
// tone mapping on, the frame is decoded with a clamp and the ceiling stays 1; so hdr_restore and hdr_source move the sharpener
// too, one frame late (CONFIG.md says so).
// Six times paper white, not the display's peak: the encode's roll-off never clips, and a peak-based ceiling would leave bright
// night emitters unsharpened and move with a display setting. Brighter headlight interiors stay unsharpened. Not to be raised:
// the ceiling also bounds the filter's ringing. Encoded as the encode decodes (hdr_eotf): 2.258 at gamma 2.2.
static float ShpCeiling()
{
    if (shpFmtUsed != D3DFMT_A16B16G16R16F || !hdrAboveWhiteShown) return 1.0f;
    static int   forEotf = -99;
    static float ceil = 1.0f;
    if (forEotf != ivtCfg.hdr_eotf)             // one powf per change of the key, not per frame
    {
        forEotf = ivtCfg.hdr_eotf;
        const float lin = 6.0f;
        ceil = forEotf == 1 ? 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f : powf(lin, 1.0f / (forEotf == 2 ? 2.4f : 2.2f));
    }
    return ceil;
}

// One copy and one full-screen pass through the pass-through vertex shader, so no vertex constant is set: this wrapper device
// S_OKs SetVertexShaderConstantF and silently drops it.
static void ShpSharpen(IDirect3DDevice9* dev)
{
    if (!dev || !backbufferSurf || !ShpEnsure(dev)) return;
    RsOwnCall rsOwnGuard;        // the mod's own binds are not the game's state
        // The shader cannot sample the surface it writes, so the image is copied out first (same size and format: a plain blit).
    if (FAILED(dev->StretchRect(backbufferSurf, nullptr, shpSurf, nullptr, D3DTEXF_NONE)))
    { DEVSTAT(if (telemetryOn) { shpFailed++; }) return; }

    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    c[0][0] = shpCon;
    const float ceiling = ShpCeiling();
    c[0][1] = ceiling;
    static float saidCeiling = -1.0f;
    if (ceiling != saidCeiling)                 // facts only, once a change
    {
        saidCeiling = ceiling;
        LogRecord("  sharpen: headroom measured against %.3f (white 1); back buffer format %d, above white shown: %s (hdr=%d, hdr_restore=%d, hdr_eotf=%d)", (double)ceiling, (int)shpFmtUsed, hdrAboveWhiteShown ? "yes" : "no",
            ivtCfg.hdr, ivtCfg.hdr_restore, ivtCfg.hdr_eotf);
    }
    c[0][2] = 1.0f / (float)rsOutW;
    c[0][3] = 1.0f / (float)rsOutH;
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
        // Sampler sRGB conversion is not in IvtSetupPass's save-and-restore set; left on by the game it would linearise the reads, so it
    // is saved and restored here.
    DWORD oldSrgb = 0;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

    const D3DVIEWPORT9 vp = { 0, 0, rsOutW, rsOutH, 0.0f, 1.0f };
    dev->SetRenderTarget(0, backbufferSurf);
    dev->SetViewport(&vp);
    dev->SetTexture(0, shpTex);
    dev->SetPixelShader(shpPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));

    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb);
    ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
    DEVSTAT(if (telemetryOn) { shpPasses++; })
}

// Who is copying the back buffer, for the sharpen's rule in hkStretchRect: true when `ret` is in neither the game's image nor this
// module's (several of the mod's copies run outside an RsOwnCall). Both ranges are read once from the PE headers.
static bool ShpCallerIsForeign(const void* ret)
{
    static const BYTE* gameLo = nullptr, *gameHi = nullptr, *ownLo = nullptr, *ownHi = nullptr;
    if (!gameLo)
    {
        HMODULE own = nullptr;
        const BYTE* const img = (const BYTE*)GetModuleHandleA(nullptr);
        if (!img || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        (LPCSTR)&ShpCallerIsForeign, &own) || !own) return false;   // unknown is not foreign
        const BYTE* const o = (const BYTE*)own;
        ownLo = o;  ownHi = o + ((const IMAGE_NT_HEADERS*)(o + ((const IMAGE_DOS_HEADER*)o)->e_lfanew))->OptionalHeader.SizeOfImage;
        gameHi = img + ((const IMAGE_NT_HEADERS*)(img + ((const IMAGE_DOS_HEADER*)img)->e_lfanew))->OptionalHeader.SizeOfImage;
        gameLo = img;           // last: the test above reads it as "the ranges are known"
    }
    const BYTE* const a = (const BYTE*)ret;
    return !(a >= gameLo && a < gameHi) && !(a >= ownLo && a < ownHi);
}

// Half to float, the file's one decoder; a denormal is normalised.
static inline float HalfToFloat(unsigned short h)
{
    const unsigned s = (unsigned)(h >> 15) & 1u, e = (unsigned)(h >> 10) & 0x1Fu;
    unsigned m = (unsigned)h & 0x3FFu, f;
    if (e == 0)
    {
        if (!m) f = s << 31;
        else { unsigned sh = 0; while (!(m & 0x400u)) { m <<= 1; sh++; } f = (s << 31) | ((127u - 15u + 1u - sh) << 23) | ((m & 0x3FFu) << 13); }
    }
    else if (e == 31) f = (s << 31) | (0xFFu << 23) | (m << 13);
    else              f = (s << 31) | ((e - 15u + 127u) << 23) | (m << 13);
    float out; memcpy(&out, &f, sizeof(out)); return out;
}

// The Halton phase count, from the DLSS programming guide: 8 * (output / render)^2, so the pattern covers each output pixel the
// same number of times at any scale.
//   DLAA        1.00x  ->   8
//   Quality     1.50x  ->  18
//   Balanced    1.72x  ->  24
//   Performance 2.00x  ->  32
//   Ultra Perf  3.00x  ->  72
// A longer sequence converges more slowly on a moving camera; the cfg can pin any count (jitter_phases). Halton is evaluated per
// frame, so any count works. XeSS asks for the same minimum (Intel's guide, "Jitter Sequence"): tools/test-lod-xess cuts the lines
// between the two markers below and holds the count at or above Intel's minimum at every ratio, with a planted half count as its
// control.
static void JitterPhasesForScale()
{
    // JITTER PHASE RULE START (tools/test-lod-xess cuts from this line to JITTER PHASE RULE END)
    double ratio = 1.0;
    if (rsActive && rsW && rsH && rsOutW && rsOutH)
        ratio = (double)rsOutW / (double)rsW;
    int p = (int)(8.0 * ratio * ratio + 0.999);
    if (p < 8)   p = 8;
    if (p > 128) p = 128;
        // JITTER PHASE RULE END
    // Logged at log=1 once per value the rule decides, even when it equals the default (as at DLAA).
    static int said = -1;
    if (p != said)
    {
        said = p;
        LogRecord("  jitter phases: %d (NVIDIA's rule, %.3fx) (XeSS asks for the same count)", p, ratio);
    }
    if (p == jitterPhases) return;
    jitterPhases = p;
    jitterPhase  = 0;
}

// The render size, decided from the screen size and the cfg at the game's CreateDevice, before any of its targets exist.
static void RsDecideRenderSize(UINT w, UINT h)
{
    if (!w || !h) return;
    rsOutW = w; rsOutH = h;
    const UINT wasW = rsW, wasH = rsH;
    const bool wasActive = rsActive;
    rsActive = false;
    if (!ivtCfgRead) IvtReadCfg();
    int q = ivtCfg.quality;
    const int sc = ivtCfg.custom;         // a percent, 34..99, or 0: the quality table decides (clamped in IvtClampCfg)
    if (sc <= 0 && q <= 0) return;
        // IvtClampCfg alone clamps quality to 0..4, and says so in the log.
    if (!ivtCfg.transport || IVT_MODE != 2)
    {
        static bool said = false;
        if (!said)
        {
            said = true;
            LogRecord("  render scale: quality=%d custom=%d asked, but nothing resolves (transport=%d mode=%d): off", q, sc, ivtCfg.transport, IVT_MODE);
        }
        return;
    }
        // Refuse when the transport is already known to be impossible, as after a misinstall:
    //   the host exe in the DLSSIV folder is missing: IvtSpawnHost calls IvtFail from InitThread, above the D3D9 module scan, so
    //   ivtFailed is already true at the game's CreateDevice.
    //   the runtime is not DXVK: ivtDxvkOk is latched in hkCreateDevice from the ID3D9VkInteropDevice query, just before
    //   RsOnScreenSize.
    // The scale cannot be stood down later (rsActive is decided before the game's targets exist), so refusing here gives a native
    // resolution game instead of a soft one for the whole session.
    if (ivtFailed || !ivtDxvkOk)
    {
        static bool saidDead = false;
        if (!saidDead)
        {
            saidDead = true;
            LogRecord("  render scale: quality=%d custom=%d asked, but %s: OFF, the game renders at %ux%u",
                q, sc, ivtFailed ? "the transport failed (the line above)" : "the runtime is not DXVK",
                w, h);
        }
        return;
    }
        // NVIDIA's DLSS ratios, rounded and not snapped to any multiple: they land exactly on each mode's optimal input size (Quality
    // 2560x1440, Balanced 2227x1253, Performance 1920x1080 at 4K), so the host picks the mode asked for. The game's half and quarter
    // chains truncate from these, a pixel or two off, which a bloom chain does not care about.
    static const double kRatio[5] = { 1.0, 2.0 / 3.0, 0.58, 0.5, 1.0 / 3.0 };
    static const char*  kName[5]  = { "off", "Quality", "Balanced", "Performance", "Ultra Performance" };
        // A percent (custom) takes precedence over the table: everything below works from the resulting sizes.
    const double ratio = sc > 0 ? (double)sc / 100.0 : kRatio[q];
    const UINT rw = (UINT)((double)w * ratio + 0.5), rh = (UINT)((double)h * ratio + 0.5);
    if (rw < 64 || rh < 64 || rw >= w || rh >= h) return;
    rsW = rw; rsH = rh; rsActive = true;
    RsBuildAnchors();          // once per size change, never in the hot path
    if (!wasActive || wasW != rw || wasH != rh)
    {
        if (sc > 0)
            LogRecord("  render scale: custom=%d percent (quality=%d not used): %ux%u -> %ux%u", sc, q, rsW, rsH, w, h);
        else
            LogRecord("  render scale: %s (%d): %ux%u -> %ux%u", kName[q], q, rsW, rsH, w, h);
    }
}

// The size and the phase count are one decision, redone whenever the ratio can change (including the early returns at 1.0).
// The bias the render scale implies, from the sizes actually achieved: log2(rsW / rsOutW) is exact, and right at any resolution.
//   DLAA        3840 -> 3840   0.0000     Quality     2560 -> 3840  -0.5850
//   Balanced    2227 -> 3840  -0.7860     Performance 1920 -> 3840  -1.0000
//   Ultra Perf  1280 -> 3840  -1.5850
// Too negative aliases the input; not negative enough and the detail was never sampled.
// XeSS takes the same bias (Intel's guide, "Mip Bias": log2(render / output)), as do DLSS and FSR, with no extra -1.
// tools/test-lod-xess cuts the lines between the two markers below and holds them against Intel's values and rule at every
// ratio, with a planted "minus one" as its control.
static void LodBiasForScale()
{
    // LOD BIAS RULE START (tools/test-lod-xess cuts from this line to LOD BIAS RULE END)
    const double ratio = (rsActive && rsW && rsOutW) ? (double)rsW / (double)rsOutW : 1.0;
    // log(x)/log(2) rather than log2: the same value on every compiler this has
    // to build on, and this runs once per resolution change.
    const float was = lodBiasAuto;
    lodBiasAuto = (ratio > 0.0 && ratio < 1.0) ? (float)(log(ratio) / log(2.0)) : 0.0f;
        // LOD BIAS RULE END
    // Said only when it changes; zero is still said when it is the answer (correct at DLAA, wrong elsewhere).
    static bool lodSaid = false;
    if (!lodSaid || was != lodBiasAuto)
    {
        lodSaid = true;
        LogHealth("  lod bias: %+.4f from log2(%u / %u), a %.4fx render scale%s",
            lodBiasAuto, rsActive ? rsW : rsOutW, rsOutW, ratio,
            rsActive ? "" : "   (the scene renders at the output size, so zero is correct)");
    }
}

// The size, the phase count and the bias are one decision: all three are
// functions of the same ratio, so all three are redone every time it can change,
// including the early returns where the scale stays off and the ratio is 1.
static void RsOnScreenSize(UINT w, UINT h)
{
    RsDecideRenderSize(w, h);
    JitterPhasesForScale();
    LodBiasForScale();
}


static void VelCreate(IDirect3DDevice9* dev)
{
    if (velTried)
        return;
    velTried = true;

        // Size from the back buffer, and only once the scene key has settled: before that a transient reflection or probe pass can be
    // taken for the scene.
    if (!haveSceneKey || sceneKeyStable < kSceneKeySettleFrames)
    {
        velTried = false;              // not settled yet; ask again next frame
        return;
    }
        // Below that return: it runs every frame until the key settles, and a drain above it buys nothing.
    DT_DIRECT();       // ends in DXVK interop (GetVulkanImageInfo): see devthread/dt_core.h

    IDirect3DSwapChain9* sc = nullptr;
    if (SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc)
    {
        D3DPRESENT_PARAMETERS pp;
        if (SUCCEEDED(sc->GetPresentParameters(&pp)))
        {
            velW = pp.BackBufferWidth;
            velH = pp.BackBufferHeight;
        }
        sc->Release();
    }
    if (velW == 0 || velH == 0)
    {
        velTried = false;
        return;
    }
        // The scene's own size, not the back buffer's: with the render scale on, grcViewport reports the render size, and gate 3 of the
    // scene-camera capture compares against velW/velH, so a mismatch would reject every capture.
    if (rsActive && rsW && rsH)
    {
        velW = rsW; velH = rsH;
        LogHealth("  velocity RT: the scene renders at %ux%u (the screen is %ux%u);"
            " the velocity target and the camera gate follow the scene",
            velW, velH, ivtOutW ? ivtOutW : rsOutW, ivtOutH ? ivtOutH : rsOutH);
    }
    else if (camCur.valid && ((UINT32)camCur.w != velW || (UINT32)camCur.h != velH))
        LogHealth("  velocity RT: backbuffer %ux%u but scene viewport %dx%d -- using"
            " the backbuffer", velW, velH, camCur.w, camCur.h);

        // Four FP16 channels: RG the delta-UV (the shape of texMotionVectors), B the view-space distance so a consumer can reject pixels
    // this object no longer covers, A validity, since a zero motion vector is legitimate and cannot mean "nothing here".
    const UINT32 tw = velW, th = velH;   // set above: the size the scene renders at
    rsOwnCreate++;
    HRESULT hr = dev->CreateTexture(tw, th, 1, D3DUSAGE_RENDERTARGET,
                                    D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT,
                                    &velTex, nullptr);
    rsOwnCreate--;
    if (FAILED(hr) || !velTex)
    {
        LogRecord("  velocity RT: CreateTexture(%ux%u A16B16G16R16F) failed 0x%08X",
            tw, th, hr);
        velFailed = true;
        return;
    }

    velTex->GetSurfaceLevel(0, &velSurf);

    // --- Vulkan interop ---------------------------------------------------
    if (FAILED(velTex->QueryInterface(__uuidof(ID3D9VkInteropTexture),
                                      (void**)&velInterop)) || !velInterop)
    {
        LogRecord("  velocity RT: no ID3D9VkInteropTexture: the velocity pass is off");
        velFailed = true;
        return;
    }
    VkImage img = VK_NULL_HANDLE;
    if (FAILED(velInterop->GetVulkanImageInfo(&img, nullptr, nullptr)) || !img)
    {
        LogRecord("  velocity RT: GetVulkanImageInfo gave no image");
        velFailed = true;
        return;
    }
    LogDetail("  velocity RT: %ux%u A16B16G16R16F, VkImage 0x%016llX", tw, th, (unsigned long long)img);
}

// ---------------------------------------------------------------------------
// M4d: exact match for statics, proximity only for movers
// ---------------------------------------------------------------------------
// The tracker the velocity redraw uses (VelRecordObject). A static object's world matrix is identical frame to frame, so statics
// match exactly; proximity (nearest neighbour in world space) handles only the misses, which are the movers or new geometry.
// Proximity alone pairs the wrong instances of a mesh drawn hundreds of times (railings, fence posts).
//   1. Exact:     key = mesh (VB, IB, draw params) + hash of the 64 world bytes.
//   2. Proximity: only for draws that missed, where an 8-unit window is sane.
// If the engine ever recomputed world matrices with float noise, everything would fall to the unmatched set, and the report's
// "missed exact" line would say so.

struct ObjKey
{
    UINT_PTR vb, ib;
    UINT     baseVertex, startIndex, primCount;
};

static bool KeyEqObj(const ObjKey& a, const ObjKey& b)
{
    return a.vb == b.vb && a.ib == b.ib && a.baseVertex == b.baseVertex &&
           a.startIndex == b.startIndex && a.primCount == b.primCount;
}

static unsigned HashMesh(const ObjKey& k)
{
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)(k.vb & 0xFFFFFFFFu)) * 16777619u;
    h = (h ^ (unsigned)(k.ib & 0xFFFFFFFFu)) * 16777619u;
    h = (h ^ k.baseVertex) * 16777619u;
    h = (h ^ k.startIndex) * 16777619u;
    h = (h ^ k.primCount)  * 16777619u;
    return h;
}

// Bit-exact over the whole 4x4. Deliberately NOT quantised: if the engine has
// float noise in its world matrices we want to find that out, not paper over it.
static unsigned HashWorld(const float* w)
{
    const unsigned* u = (const unsigned*)w;
    unsigned h = 2166136261u;
    for (int i = 0; i < 16; i++)
        h = (h ^ u[i]) * 16777619u;
    return h;
}

// --- table 1: exact (mesh + world) ---------------------------------------------
// used, worldHash and lastSeen live in m4dHot and m4dUsedBits beside this table (see the probe loop in M4dNoteDraw).
struct ExactEnt
{
    ObjKey   mesh;
        // lastMoved sits beside mesh: an exact hit (most hits) reads only those two, and here they straddle a cache line less often.
    LONG     lastMoved;     // frame this object last matched as a mover, -1 never
        // Identity and movement are separate questions: a stationary character's world matrix is a perfect identity key but cannot show
    // movement, since skinning never touches it, so the pose is tracked alongside.
    int      boneRowCur;    // row allocated this frame, -1 if none
    int      boneRowPrev;   // row from last frame, which the texture now holds
    LONG     boneFrame;     // frame boneRowCur belongs to
    unsigned boneHash;
    bool     hasBone;
    bool     boneAnimated;  // decided once per frame, reused by sibling draws
};
#define M4D_EXACT_SLOTS 65536
#define M4D_EXACT_PROBES 8
#define M4D_STALE_FRAMES 120           // reclaim entries not seen this long
static ExactEnt m4dExact[M4D_EXACT_SLOTS];

// The probe's own arrays: the probe is linear ((h + p) & mask, p = 0..7) and the table runs full, so every miss walks all eight
// slots. The probe needs only used, the world hash and lastSeen; at 8 bytes, eight consecutive probes are one 64-byte cache line,
// and the keys and pose rows are touched only on a hash match or an insert. The used bits are a separate array, not a sentinel
// stolen from the hash: 8 KB, and zero-initialised means unused.
struct M4dHot { unsigned hash; LONG lastSeen; };
static M4dHot   m4dHot[M4D_EXACT_SLOTS];              // 512 KB
static unsigned m4dUsedBits[M4D_EXACT_SLOTS / 32];    // 8 KB
static inline bool M4dSlotUsed(unsigned slot)
{ return (m4dUsedBits[slot >> 5] & (1u << (slot & 31))) != 0; }
// M4dHot must stay 8 bytes so eight linear probes fit one 64-byte cache line.
static_assert(sizeof(M4dHot) == 8, "M4dHot must stay 8 bytes: 8 linear probes = one cache line");
static_assert(sizeof(ExactEnt) == 44, "ExactEnt size drift: update the memory note above");
static_assert(sizeof(m4dHot) + sizeof(m4dUsedBits) + sizeof(m4dExact) < 3670016,
              "the tracker's tables must stay under the 3.6 MB they replaced (tenet 16)");

// --- table 2: this frame's misses, matched against last frame's -------------
struct Unmatched
{
    ObjKey mesh;
    float  world[16];
    float  tx, ty, tz;
    bool   claimed;
    int    boneRow;        // -1 when the draw was not skinned
    unsigned boneHash;     // so a crossover can tell whether the pose changed
};
#define M4D_UNMATCHED 4096
static Unmatched m4dUmA[M4D_UNMATCHED], m4dUmB[M4D_UNMATCHED];
// This frame's list and last frame's, and their counts, are m4dL.umCur, umPrev, umCurN and umPrevN (the tracker's
// own line, below the prefetch sequence).

// The next draw's lines, asked for one draw early: the scene is drawn in nearly the same order every frame, so the slot the next
// tracked draw will probe is usually the one the next draw resolved to last frame. Only prefetches read this, so a wrong guess
// wastes one request and cannot change an answer.
#define M4D_SEQ 4096
static_assert(M4D_EXACT_SLOTS <= 65536, "a slot must fit the sequence's 16 bits");
static unsigned short m4dSeqA[M4D_SEQ], m4dSeqB[M4D_SEQ];

// The tracker's own state on a cache line of its own: the tracker runs on the device thread, and its per-draw stores would
// otherwise evict the render thread's hot flags (`dead`, `haveSceneKey`). Aligned to 64 bytes and exactly one line long, so the
// line is the tracker's whatever the linker puts around it. With threaded=0 it is simply the render thread's.
struct __declspec(align(64)) M4dLine
{
    Unmatched*      umCur;  Unmatched*      umPrev;  int umCurN, umPrevN;            // table 2, this frame's misses and last frame's
    unsigned short* seqCur; unsigned short* seqPrev; int seqCurN, seqPrevN, seqAt;   // the prefetch sequence (above)
    bool            idxBuilt;                                                        // M4dBuildIndex (below)
    bool            everTracked;                                                     // any draw tracked yet (below)
};
static_assert(sizeof(M4dLine) == 64, "the tracker's state is one cache line");
DLSSIV_TRKLINE static M4dLine m4dL = { m4dUmA, m4dUmB, 0, 0, m4dSeqA, m4dSeqB, 0, 0, 0, false, false };

// `slot` is where this draw's entry lives now. m4dL.seqAt is the position in last
// frame's sequence of the draw expected next.
static __forceinline void M4dSeqNote(unsigned slot)
{
    if (m4dL.seqCurN < M4D_SEQ) m4dL.seqCur[m4dL.seqCurN++] = (unsigned short)slot;
    int at = m4dL.seqAt;
    if (at < m4dL.seqPrevN && m4dL.seqPrev[at] == slot) at++;          // as predicted
    else
    {
                // Resync: a draw culled since last frame shifts the sequence, so this one is found a little ahead; a new draw or a mover is found
        // nowhere, and stepping over its position keeps the rest of the frame in line.
        int j = at > 0 ? at - 1 : 0;
        const int end = (at + 9 < m4dL.seqPrevN) ? at + 9 : m4dL.seqPrevN;
        while (j < end && m4dL.seqPrev[j] != slot) j++;
        at = (j < end) ? j + 1 : at + 1;
    }
    m4dL.seqAt = at;
    if (at < m4dL.seqPrevN)
    {
        const unsigned nx = m4dL.seqPrev[at];
        _mm_prefetch((const char*)&m4dUsedBits[nx >> 5], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dHot[nx], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dExact[nx], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dExact[nx] + sizeof(ExactEnt) - 1, _MM_HINT_T0);
    }
}

// ===== Mesh-key index for the proximity scan ====================================
// The linear gather it replaced survives only in the DLSSIV_M4D_INDEX_VERIFY build, as the reference. The linear scan walked tens
// of thousands of cache-missing entries a frame for the few hundred that pass KeyEqObj.
// Exactly equivalent: HashMesh consumes the five fields KeyEqObj compares (vb, ib, baseVertex, startIndex, primCount) field by
// field, so equal keys hash equal and a bucket is a superset of what the linear scan selects. Four things must hold:
//  1. Chains enumerate in ascending j. The test `d2 < bestD * bestD` uses a sqrtf round trip, so on a tie the later candidate
//     can win: order decides the answer. The build runs backward with head insertion, which yields ascending j.
//  2. j < scanTo is still enforced per node. The cap keeps the head of the list: the engine draws near geometry first.
//  3. The hash is computed here, from u.mesh, in one place, so a new insert site cannot land in no bucket.
//  4. KeyEqObj still runs on every candidate, so a collision cannot fabricate a match.
// Raising M4D_SCAN_CAP is not equivalent (more matches, reordered claims) and needs its own test.
#define M4D_IDX_BITS 11
#define M4D_IDX_SIZE (1 << M4D_IDX_BITS)
static int  m4dIdxHead[M4D_IDX_SIZE];
static int  m4dIdxNext[M4D_UNMATCHED];
// m4dL.idxBuilt says the index is built: its heads are zero-initialised, which would read as "entry 0 is in every
// bucket".

static inline int M4dBucket(unsigned h)
{
    return (int)((h ^ (h >> M4D_IDX_BITS)) & (M4D_IDX_SIZE - 1));
}

// Called once a frame, right after the buffer swap, over exactly [0, m4dL.umPrevN): one HashMesh per entry.
static void M4dBuildIndex()
{
        // Cleared every rebuild: a generation stamp is the shortcut that goes stale silently.
    memset(m4dIdxHead, 0xFF, sizeof(m4dIdxHead));      // every head to -1
    int n = m4dL.umPrevN;
    if (n > M4D_UNMATCHED) n = M4D_UNMATCHED;          // the array is the truth
    // BACKWARD. See condition 1 above; this is the whole reason.
    for (int j = n - 1; j >= 0; j--)
    {
        const int b = M4dBucket(HashMesh(m4dL.umPrev[j].mesh));
        m4dIdxNext[j] = m4dIdxHead[b];
        m4dIdxHead[b] = j;
    }
    m4dL.idxBuilt = true;
}
// ===== end mesh-key index =======================================================

// The proximity window, in world units, scaled to frame time by
// M4dUpdateWindow below. Declared here because the crossover lookup is the
// earliest user of it.
DLSSIV_HOT static float m4dWindow = 2.0f;

// Crossover recovery: a standing pedestrian shifts weight, so the world matrix is bit-identical on some frames and not others,
// and the object drifts between the exact and proximity paths. Both share one bone history through the double-buffered
// unmatched lists: an exact hit with no chain of its own looks for itself here, and every skinned exact hit leaves a shadow
// entry so the reverse crossing works too.
template<class S> static int BoneHistFindPrev(const ObjKey& k, const float* world, unsigned* outHash)
{
    int   best  = -1;
    float bestD = S::Window();
    for (int j = 0; j < m4dL.umPrevN; j++)
    {
        const Unmatched& pv = m4dL.umPrev[j];
        if (pv.boneRow < 0) continue;               // carries no pose
        if (!KeyEqObj(pv.mesh, k)) continue;        // cheap reject first
        float dx = pv.tx - world[12];
        float dy = pv.ty - world[13];
        float dz = pv.tz - world[14];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < bestD * bestD) { bestD = sqrtf(d2); best = j; }
    }
    if (best < 0) return -1;
    *outHash = m4dL.umPrev[best].boneHash;
    return m4dL.umPrev[best].boneRow;
}

// Whether the tracker has seen a draw, which M4dEndFrame tests: it guards the unmatched-list swap and M4dBuildIndex, the tracker
// itself, so it must not be a telemetry counter. It is m4dL.everTracked.
// M4D_EXACT_SLOTS: do not shrink it. The table saturates within about a minute of driving and then reclaims stale entries every
// frame; an overflowed entry loses an object's history, and it gets the camera vector for a frame. Overflow runs at about 0.06
// percent of tracked draws. The levers are M4D_STALE_FRAMES, then M4D_EXACT_PROBES, never the size without re-measuring the
// overflow rate (the fill saturates and stops informing).
#define M4D_SCAN_CAP 1200

// Match window in world units, recomputed each frame from the frame time. Err tight: a false match gives an object a confident
// wrong vector, a missed one falls back to the camera vector. 90 units a second (about 320 km/h) is above anything in GTA IV.
static double m4dLastTick = 0.0;
DLSSIV_HOT static LONG   m4dWindowFrame = -1;     // the frame the window was last sized for
DEVSTAT(static int    m4dReoffsetSampled = 0;)
// A rigid object that stopped keeps leaving a trail this long, so the frame it starts again its previous placement can be found.
// 900 frames covers a traffic-light wait without flooding the proximity list; an object stopped longer loses one frame when it
// pulls away.
#define M4D_TRAIL_FRAMES 900
DEVSTAT(static LONG m4dRepeatServed = 0;)   // skinned movers drawn again in their own frame and given their first draw's previous transform
static double m4dQpcFreq = 0.0;

static void M4dUpdateWindow()
{
    LARGE_INTEGER now;
    if (m4dQpcFreq == 0.0)
    {
        LARGE_INTEGER f;
        if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0) { m4dWindow = 2.0f; return; }
        m4dQpcFreq = (double)f.QuadPart;
    }
    if (!QueryPerformanceCounter(&now))
        return;
    double tick = (double)now.QuadPart;
    if (m4dLastTick > 0.0)
    {
        double dt = (tick - m4dLastTick) / m4dQpcFreq;
        if (dt > 0.0005 && dt < 0.5)                 // ignore hitches and stalls
        {
            float w = 90.0f * (float)dt;             // ~320 km/h
            if (w < 0.35f) w = 0.35f;
            if (w > 4.0f)  w = 4.0f;
            m4dWindow = w;
        }
    }
    m4dLastTick = tick;
}

// Hot: about 5300 calls a frame. No VirtualQuery, no D3D9, no allocation, no log.
// Returns true and fills prevWorldOut when this draw is a mover whose previous placement is known. Matched here, mid-frame,
// because the redraw that needs the answer happens mid-frame.
template<class S> static bool M4dNoteDraw(UINT baseVertex, UINT startIndex, UINT primCount,
                        const float* world, float* prevWorldOut,
                        bool skinned, int* prevBoneRowOut)
{
    *prevBoneRowOut = -1;
    m4dL.everTracked = true;
        // The window is sized by the caller (hkDrawIndexed's gate, on the render thread in both modes; the device thread gets a copy):
    // this body has no clock, only S::Window().
    if (S::VBKey() == 0) return false;

    ObjKey k;
    k.vb = S::VBKey(); k.ib = S::IBKey();
    k.baseVertex = baseVertex; k.startIndex = startIndex; k.primCount = primCount;

        // Memoised: an object with several sub-meshes hashes the same 64 bytes each time. Keyed on the serial and on pointer identity,
    // because the premul tyre path passes a local effWorld rather than m4bCurWorld.
    unsigned wh;
    if (world == S::World())
    {
        static unsigned __int64 whSerial = ~0ull;   // unreachable: the serial only increments
        static unsigned         whCached = 0;
        if (whSerial != S::WorldSerial())
        {
            whCached = HashWorld(world);
            whSerial = S::WorldSerial();
        }
        wh = whCached;
    }
    else
    {
        wh = HashWorld(world);
    }
    unsigned h  = HashMesh(k) ^ (wh * 2654435761u);

        // The first probe's three reads (used bit, hot entry, cold key) are dependent misses; prefetching all three lets them arrive
    // together. A prefetch changes no value.
    {
        const unsigned s0 = h & (M4D_EXACT_SLOTS - 1);
        _mm_prefetch((const char*)&m4dUsedBits[s0 >> 5], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dHot[s0], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dExact[s0], _MM_HINT_T0);
        _mm_prefetch((const char*)&m4dExact[s0] + sizeof(ExactEnt) - 1, _MM_HINT_T0);
    }

    ExactEnt* freeEnt = nullptr;
    unsigned  freeSlot = 0;
    for (int p = 0; p < M4D_EXACT_PROBES; p++)
    {
                // One cache line for the whole chain: m4dHot is 8 bytes an entry and the probe is linear, so these eight reads are one line; the
        // cold table is reached only when the hash matches.
        const unsigned slot = (h + (unsigned)p) & (M4D_EXACT_SLOTS - 1);
        if (!M4dSlotUsed(slot))
        {
            if (!freeEnt) { freeEnt = &m4dExact[slot]; freeSlot = slot; }
            break;                      // empty slot ends the probe chain
        }
        M4dHot& hot = m4dHot[slot];
        ExactEnt& e = m4dExact[slot];
        if (hot.hash == wh && KeyEqObj(e.mesh, k))
        {
            // Seen before with this exact placement: it did not translate.
            hot.lastSeen = S::Frames();
            M4dSeqNote(slot);           // the next draw's lines: see M4dSeqNote

            if (!skinned || !PrevBonesLive())
            {
                // Leave a trail if it moved recently: a rigid object that stops is an exact hit every frame, so without a trail the frame it
                // starts again the proximity search has nothing to find (a car pulling away). Gated on having moved before, so static draws do
                // not flood the unmatched list.
                if (e.lastMoved >= 0 && (S::Frames() - e.lastMoved) < M4D_TRAIL_FRAMES
                    && m4dL.umCurN < M4D_UNMATCHED)
                {
                    Unmatched& u = m4dL.umCur[m4dL.umCurN++];
                    u.mesh = k;
                    memcpy(u.world, world, 64);
                    u.tx = world[12]; u.ty = world[13]; u.tz = world[14];
                    u.claimed  = false;
                    u.boneRow  = -1;
                    u.boneHash = 0;
                }
                return false;          // furniture really is furniture
            }

            // A skinned mover drawn again in its own frame. The game draws a ped's hair six times a frame from one mesh
            // (gta_hair_sorted_alpha_exp: three layers, alpha-only and blended each). When the character moves, the first draw misses the
            // exact table, is matched by proximity and makes this entry; the other five hit it and would read as pose-only, losing the whole
            // world motion (a driver's hair standing still in the world). The first draw left a note (which previous entry it took and that
            // entry's pose row), and a repeat gets exactly what the first draw got. Told apart by three things together: matched as a mover
            // this frame, no pose stored on the entry yet, and a note.
            if (e.lastMoved == S::Frames() && !e.hasBone && e.boneHash != 0)
            {
                DEVSTAT(m4dRepeatServed++;)
                memcpy(prevWorldOut, m4dL.umPrev[e.boneHash - 1u].world, 64);
                *prevBoneRowOut = e.boneRowPrev;
                return true;
            }

            // A character standing still is still animating: idle animation always plays, so a skinned object is never in the same place with
            // the same pose, and this branch is every stationary pedestrian.
            if (e.boneFrame != S::Frames())
            {
                // Contiguity is required: a row index is meaningful only against the texture uploaded at the end of the previous frame. If the
                // object was not drawn last frame, boneRowCur indexes an older frame and that row now holds another character.
                const bool contiguous = (e.boneFrame == S::Frames() - 1);
                unsigned bh = HashBones<S>();

                int      prevRow  = contiguous ? e.boneRowCur : -1;
                unsigned prevHash = e.boneHash;
                bool     havePrev = contiguous && e.hasBone;

                // The chain is broken, so look for it: a standing pedestrian shifts weight and gets a new entry with no history, although the
                // same character was tracked last frame on the proximity path.
                if (!havePrev)
                {
                    unsigned h2 = 0;
                    int r2 = BoneHistFindPrev<S>(k, world, &h2);
                    if (r2 >= 0)
                    {
                        prevRow  = r2;
                        prevHash = h2;
                        havePrev = true;
                    }
                }

                e.boneRowPrev  = prevRow;
                e.boneRowCur   = BoneStoreAlloc<S>(bh);
                e.boneFrame    = S::Frames();
                e.boneAnimated = havePrev && (prevHash != bh);
                e.boneHash     = bh;
                e.hasBone      = true;

                // And leave a trail: next frame this character may arrive on the proximity path, and must find the pose stored here.
                if (m4dL.umCurN < M4D_UNMATCHED)
                {
                    Unmatched& u = m4dL.umCur[m4dL.umCurN++];
                    u.mesh = k;
                    memcpy(u.world, world, 64);
                    u.tx = world[12]; u.ty = world[13]; u.tz = world[14];
                    u.claimed  = false;
                    u.boneRow  = e.boneRowCur;
                    u.boneHash = bh;
                }
            }
            // Decided once per frame and reused by the sub-meshes that share one pose. A row of -1 means last frame overflowed the store: no
            // previous pose, and redrawing would only reproduce the camera vector.
            if (!e.boneAnimated || e.boneRowPrev < 0)
                return false;

            // Only the POSE changed, so the previous world transform is this
            // one. All of the motion lives in the bones.
            memcpy(prevWorldOut, world, 64);
            *prevBoneRowOut = e.boneRowPrev;
            return true;
        }
        if (!freeEnt && (S::Frames() - hot.lastSeen) > M4D_STALE_FRAMES)
        {
            freeEnt = &e; freeSlot = slot;   // reclaimable: streamed out long ago
        }
    }

    // MISSED: either it moved (so its world changed and the key with it), or it
    // is new geometry. Record it so a stop next frame matches exactly, and
    // queue it for proximity matching against last frame's misses.
    if (freeEnt)
    {
        m4dUsedBits[freeSlot >> 5] |= (1u << (freeSlot & 31));
        m4dHot[freeSlot].hash     = wh;
        m4dHot[freeSlot].lastSeen = S::Frames();
        M4dSeqNote(freeSlot);           // the next draw's lines: see M4dSeqNote
        freeEnt->mesh = k;
                // Explicit: the table is zero-initialised, and a zero boneRow would read as row 0 rather than "no pose yet".
        freeEnt->boneRowCur   = -1;
        freeEnt->boneRowPrev  = -1;
        freeEnt->boneFrame    = -1;
        freeEnt->boneHash     = 0;
        freeEnt->hasBone      = false;
        freeEnt->boneAnimated = false;
        freeEnt->lastMoved    = -1;      // explicit: zero would read as "moved at frame 0"
    }


    // Record it either way, so that if this object stops next frame it matches
    // exactly and costs nothing.
    if (m4dL.umCurN < M4D_UNMATCHED)
    {
        Unmatched& u = m4dL.umCur[m4dL.umCurN++];
        u.mesh = k;
        memcpy(u.world, world, 64);
        u.tx = world[12]; u.ty = world[13]; u.tz = world[14];
        u.claimed = false;
        // Store the pose alongside the transform. Next frame this entry becomes
        // the PREVIOUS one, and whichever draw matches it needs both.
        unsigned bhm = skinned ? HashBones<S>() : 0u;
        u.boneRow  = skinned ? BoneStoreAlloc<S>(bhm) : -1;
        u.boneHash = bhm;
    }

        // Same mesh, nearest previous placement inside the frame-time window. Only movers reach this, so the set is small. Past the cap
    // the scan is bounded, not abandoned: partial coverage beats none.
    int scanN  = m4dL.umPrevN;
    int scanTo = scanN;
    if (scanN > M4D_SCAN_CAP)
    {
                // Scan the head, not the tail: the list is in draw order and the engine draws near opaque geometry first, the player included,
        // so the cap drops late-drawn distant objects, whose one-frame miss is invisible.
        scanTo = M4D_SCAN_CAP;
    }

    int   best  = -1,        bestBone  = -1;
    float bestD = S::Window(), bestBoneD = S::Window();

        // Gather, then match: the index gathers the candidates and the matching rule below runs once (two copies of a rule drift).
    // The list cannot overflow (candidates are a subset of [0, scanTo)); bounded anyway.
    static int m4dCand[M4D_SCAN_CAP];
    int nc = 0;
    if (m4dL.idxBuilt)
    {
        const int bkt = M4dBucket(HashMesh(k));
        for (int node = m4dIdxHead[bkt]; node >= 0; node = m4dIdxNext[node])
        {
            // continue, not break, although the chain is ascending: a break would make the ordering guarantee load-bearing for termination
            // too, for nothing.
            if (node >= scanTo) continue;
            const Unmatched& pv = m4dL.umPrev[node];
            if (pv.claimed) continue;
            if (!KeyEqObj(pv.mesh, k)) continue;    // a collision must not match
            if (nc < M4D_SCAN_CAP) m4dCand[nc++] = node;
        }
    }
#if DLSSIV_M4D_INDEX_VERIFY
        // Both gathers, compared as lists (verification build): identical candidates in identical order give identical answers, and a
    // wrong order is caught even when it does not change the winner.
    if (censusOn)                  // log=3, in a verification build
    {
        static int lin[M4D_SCAN_CAP];
        int nl = 0;
        for (int j = 0; j < scanTo; j++)
        {
            const Unmatched& pv = m4dL.umPrev[j];
            if (pv.claimed) continue;
            if (!KeyEqObj(pv.mesh, k)) continue;
            if (nl < M4D_SCAN_CAP) lin[nl++] = j;
        }
        bool same = (nl == nc);
        for (int i = 0; same && i < nl; i++) same = (lin[i] == m4dCand[i]);
        if (same) { if (telemetryOn) { m4dIdxAgree++; } }
        else
        {
            if (telemetryOn) { m4dIdxMismatch++; }
            if (m4dIdxSaid < 5)
            {
                m4dIdxSaid++;
                LogRecord("M4D INDEX MISMATCH: linear gathered %d candidates, indexed %d (scanTo %d, list %d)",
                    nl, nc, scanTo, m4dL.umPrevN);
            }
        }
    }
#endif
    for (int i = 0; i < nc; i++)
    {
        const int j = m4dCand[i];
        const Unmatched& pv = m4dL.umPrev[j];
        float dx = pv.tx - world[12];
        float dy = pv.ty - world[13];
        float dz = pv.tz - world[14];
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < bestD * bestD) { bestD = sqrtf(d2); best = j; }
                // Prefer a candidate that carries a pose: the same mesh also goes into the shadow pass with a position-only declaration, and that
        // entry has no bones.
        if (skinned && pv.boneRow >= 0 && d2 < bestBoneD * bestBoneD)
        { bestBoneD = sqrtf(d2); bestBone = j; }
    }
    if (skinned && bestBone >= 0) { best = bestBone; bestD = bestBoneD; }
    if (best < 0)
    {
                // The same mesh at a new buffer offset: rotating parts (tyres, rims) drawn from a dynamic range re-offset every frame get a new
        // key every frame, and the LOD bridge below rejects them because a wheel at speed turns too far. Same primCount and same
        // placement within the window, any rotation, is the same geometry drawn again. Nearest translation wins (the other wheels of a
        // car are 1.8 units or more away), rotation breaks ties. Rigid, so no bone row.
        {
            int   rb  = -1;
            float rbD = S::Window(), rbRot = 1e9f;
            for (int j = 0; j < scanTo; j++)
            {
                const Unmatched& pv = m4dL.umPrev[j];
                if (pv.claimed) continue;
                if (pv.mesh.primCount != k.primCount) continue;
                float dx = pv.tx - world[12];
                float dy = pv.ty - world[13];
                float dz = pv.tz - world[14];
                float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > rbD * rbD) continue;
                float rot = 0.0f;
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                    {
                        float e = fabsf(pv.world[r * 4 + c] - world[r * 4 + c]);
                        if (e > rot) rot = e;
                    }
                float d = sqrtf(d2);
                if (d < rbD - 1e-4f || (fabsf(d - rbD) <= 1e-4f && rot < rbRot))
                { rbD = d; rbRot = rot; rb = j; }
            }
            if (rb >= 0)
            {
// Development log: the two keys side by side say whether the offsets move (a dynamic range) or the buffers themselves.
#if DLSSIV_DEVLOG
                if (m4dReoffsetSampled < 5)
                {
                    m4dReoffsetSampled++;
                    const ObjKey& pk = m4dL.umPrev[rb].mesh;
                    LogDetail("  re-offset mesh: prims %u at %.1f %.1f %.1f, rotated %.2f;"
                        " prev key vb %08X ib %08X bv %u si %u -> now vb %08X ib %08X bv %u si %u",
                        (unsigned)k.primCount, world[12], world[13], world[14], rbRot,
                        (unsigned)pk.vb, (unsigned)pk.ib, (unsigned)pk.baseVertex, (unsigned)pk.startIndex,
                        (unsigned)k.vb,  (unsigned)k.ib,  (unsigned)k.baseVertex,  (unsigned)k.startIndex);
                }
#endif
                m4dL.umPrev[rb].claimed = true;
                memcpy(prevWorldOut, m4dL.umPrev[rb].world, 64);
                *prevBoneRowOut = -1;
                if (freeEnt) { freeEnt->lastMoved = S::Frames(); if (skinned) { freeEnt->boneHash = (unsigned)rb + 1u; freeEnt->boneRowPrev = -1; } }   // the repeat's note (see the exact hit's skinned branch)
                return true;
            }
        }

                // A LOD swap is the same object wearing a new mesh: every sub-mesh arrives with an unseen buffer identity, but the world matrix
        // survives the swap. So look for last frame's entry with the same placement regardless of mesh: translation within the window
        // and orientation within 0.15 per element (a turning car changes about 0.03 a frame). Claimed entries are allowed (the old
        // model's sub-meshes shared this transform). Bones are not carried across a swap: a skinned object gets the rigid
        // approximation for that frame.
        for (int j = 0; j < scanTo; j++)
        {
            const Unmatched& pv = m4dL.umPrev[j];
            float dx = pv.tx - world[12];
            float dy = pv.ty - world[13];
            float dz = pv.tz - world[14];
            float d2 = dx*dx + dy*dy + dz*dz;
            if (d2 >= bestD * bestD) continue;
            float rot = 0.0f;
            for (int r = 0; r < 3 && rot < 0.15f; r++)
                for (int c = 0; c < 3; c++)
                {
                    float e = fabsf(pv.world[r * 4 + c] - world[r * 4 + c]);
                    if (e > rot) rot = e;
                }
            if (rot >= 0.15f) continue;
            bestD = sqrtf(d2);
            best  = j;
        }
        if (best >= 0)
        {
            memcpy(prevWorldOut, m4dL.umPrev[best].world, 64);
            *prevBoneRowOut = -1;
            if (freeEnt) { freeEnt->lastMoved = S::Frames(); if (skinned) { freeEnt->boneHash = (unsigned)best + 1u; freeEnt->boneRowPrev = -1; } }   // the repeat's note
            return true;
        }
        return false;                  // no history: camera vector is the honest answer
    }

    m4dL.umPrev[best].claimed = true;
    memcpy(prevWorldOut, m4dL.umPrev[best].world, 64);
    *prevBoneRowOut = m4dL.umPrev[best].boneRow;
    if (freeEnt)
    {
        freeEnt->lastMoved = S::Frames();   // it moved: trail it when it stops
                // The repeat's note: which of last frame's entries this mover took, and its pose row. A fresh entry holds no pose yet (hasBone
        // false, boneFrame -1), so these two fields are free until the exact hit's skinned branch fills them.
        if (skinned) { freeEnt->boneHash = (unsigned)best + 1u; freeEnt->boneRowPrev = m4dL.umPrev[best].boneRow; }
    }
    return true;
}

// Matching happens at draw time, where the answer is needed; this rotates the buffers.
static void M4dEndFrame()
{
        // m4dL.everTracked, not a counter: see the flag's declaration.
    if (!m4dL.everTracked) return;

    Unmatched* sw = m4dL.umPrev; m4dL.umPrev = m4dL.umCur; m4dL.umCur = sw;
    m4dL.umPrevN = m4dL.umCurN; m4dL.umCurN = 0;
        // Immediately after the swap and nowhere else, so the index and the list it describes cannot be separated by an edit.
    M4dBuildIndex();
    // The prefetch sequence turns over with the lists (M4dSeqNote).
    unsigned short* sq = m4dL.seqPrev; m4dL.seqPrev = m4dL.seqCur; m4dL.seqCur = sq;
    m4dL.seqPrevN = m4dL.seqCurN; m4dL.seqCurN = 0; m4dL.seqAt = 0;
}

// ---------------------------------------------------------------------------
// Census: the frame key
// ---------------------------------------------------------------------------
// pCurrentViewport follows whatever the engine last set (the scene, a shadow cascade, a mirror, the water reflection), so a
// sample at an arbitrary moment is not the camera. Census() counts draws per viewport signature for PickSceneKey.


// The scene's viewport key and the census behind it. The five fields arrive already read inside hkSetVsConstF's guarded region;
// `ok` is false when that read faulted, so a torn viewport fails as before.
static void Census(int w, int h, float fov, float nearC, float farC, bool ok)
{
    lastKey.w = 0;                 // invalidate: a failed read must not leave a stale signature that matches
    (void)farC;
    if (!ok) return;

        // The scene-selection signature, on every write. fov and near are quantised so small jitter does not split the scene's key.
    // One 16-byte store for the whole key, so the 16-byte load of lastKey that follows can be forwarded from it (four 4-byte stores
    // cannot be); lastKey is 16-byte aligned so the store never splits a cache line.
    _mm_storeu_si128((__m128i*)&lastKey,
        _mm_setr_epi32(w, h, (int)(fov * 1.0f + 0.5f), (int)(nearC * 100.0f + 0.5f)));
    NoteFrameKey(lastKey);      // feeds PickSceneKey and the scene selection
        // farC is read by the caller, so a torn viewport fails the __try as it always did.
    (void)farC;
}

// ---------------------------------------------------------------------------
// The constant hook: capture c8 independently
// ---------------------------------------------------------------------------

typedef HRESULT(__stdcall* SetVsConstF_t)(IDirect3DDevice9*, UINT, const float*, UINT);
DLSSIV_HOT static SetVsConstF_t origSetVsConstF = nullptr;
#if DLSSIV_DEVLOG
DLSSIV_HOT static bool toldConstFired = false;
#endif

// ===== The pixel heal and the cold shape table ===================================
// No vertex register outside the list has been seen to carry a screen size: the only matches, c7.xy and c11.xy inside the
// per-draw matrix block, were a matrix element equal to a ladder size, so they are not rewritten. If a FusionFix update ever
// leaves a vertex screen size unscaled, restore a vertex audit: a real miss repeats every frame.

static void RsShapeReset();        // the cold shape table, defined below; the heal clears it
// The pixel heal. Not telemetry: FusionFix compiles its ambient occlusion, SMAA, FXAA and sun shafts at runtime, and the register
// a screen size lands in is that machine's compiler's choice, so a pixel miss is a visible artifact on someone else's install.
// One gated upload in sixteen, drawn at random (a fixed stride could keep landing on one slot of a repeating per-draw pattern),
// has its rows outside the live mask tested with RsFixNeededRows; a match promotes that register into rsScanPS for the session
// and widens the row mask, so the upload that found it is rewritten correctly.
DEVSTAT(static LONG     rsHealFired = 0;)                     // uploads where the net caught something
DEVSTAT(static LONG     rsHealAdded = 0;)                     // registers promoted into the live mask
static unsigned rsHealRng   = 0x2545F491u;

static __declspec(noinline) unsigned RsHealPixel(UINT start, UINT count, const float* src, unsigned listed)
{
    const unsigned other = ((1u << count) - 1u) & ~listed;
    if (!other || !RsFixNeededRows(src, other)) return listed;

        // Test the rows one at a time, so only the registers that carry a size are promoted.
    unsigned added = 0;
    for (unsigned rr = other; rr; rr &= rr - 1)
    {
        unsigned long r;
        _BitScanForward(&r, rr);
        if (!RsFixNeededRows(src, 1u << r)) continue;
        const unsigned reg = start + r;
        if (reg > 255) continue;                     // RsScanRows cannot address it anyway
        added |= 1u << r;
        InterlockedOr((volatile LONG*)&rsScanPS.w[(reg >> 5) & 7], (LONG)(1u << (reg & 31)));
        DEVSTAT(InterlockedIncrement(&rsHealAdded);)
        LogHealth("  render scale: PIXEL c%u carries a screen size and was NOT in the list. Promoted at frame"
                  " %ld; it is rewritten from here on, and the upload that found it was rewritten too."
                  "   <-- a runtime-compiled FusionFix effect no shader file names: add c%u to kRsRegsPS",
                  reg, (long)frames, reg);
    }
    if (!added) return listed;                       // the pair straddled rows; nothing to promote
    RsShapeReset();                                  // every cold verdict was reached on the old mask
    DEVSTAT(InterlockedIncrement(&rsHealFired);)
    return listed | added;
}

// ===== The cold shape table =====================================================
// rsScanPS is the union of screen-size registers over every shader, so a shader that never carries one still pays for the
// registers others use, and only a few shapes ever match. A shape scanned kRsColdAfter times without a match goes cold and is
// then scanned one time in sixteen (randomly sampled); any match makes it hot again for good. Direct-mapped and never probed,
// since it runs a quarter of a million times a second; a collision is conservative (a match in either keeps both hot).
// This is the optimisation, not a diagnostic.
struct RsShapeEnt { unsigned scans, hits; };
DLSSIV_HOT static RsShapeEnt rsShape[256];
DLSSIV_HOT static unsigned   rsColdRng = 0x8BADF00Du;
static const unsigned kRsColdAfter = 1024;     // scans with no match before a shape goes cold

static __forceinline unsigned RsShapeIdx(UINT start, UINT count)
{
    return (unsigned)(start * 31u + count) & 255u;
}
// A heal promotion changes which rows are listed, so every cold verdict starts again (a few times a session at most).
static void RsShapeReset() { for (int i = 0; i < 256; i++) { rsShape[i].scans = 0; rsShape[i].hits = 0; } }
// ===== end the cold shape table =================================================

static __forceinline unsigned RsHealMaybe(UINT start, UINT count, const float* src, unsigned listed)
{
    rsHealRng = rsHealRng * 1664525u + 1013904223u;
    if (rsHealRng >> 28) return listed;
    return RsHealPixel(start, count, src, listed);
}
// ===== end the pixel heal and the cold shape table ===============================

// The five __try blocks below share one exception frame and stay inline: more than half the uploads enter one, so a noinline
// helper would add a call to most of them.
static HRESULT __stdcall hkSetVsConstF(IDirect3DDevice9* dev, UINT start,
                                       const float* data, UINT count)
{
    DT_NOT_ON_WORKER(origSetVsConstF(dev, start, data, count));   // see devthread/dt_core.h
    DEVSTAT(if (start == 0 && count >= 4) trkApplyDirty &= ~1u;)   // the tracker's premise (dt_res.h)
        // A pass writing to a scaled target must be told the scaled size, or its screen-space UVs and tap offsets land off by the ratio.
    // curRT0Scaled is the right gate: true for the counterpart and the G-buffer, false for the post chain, which runs at output size.
    // Cheap: the gate early-outs on its first compare, and only a handful of uploads a frame are rewritten.
    static float rsFixedVs[16 * 4];
    if (rsActive && data && count && count <= 16 && !rsOwn && !ivtInOwnDraw
        && rsScreenTexKind == 0 && curRT0Scaled)
    {
                // The listed registers only (see RsMakeMask): most uploads, the per-draw c0..c15 block among them, touch none and cost one lookup.
        const unsigned listed = RsScanRows(rsScanVS, start, count);
        if (listed && RsFixNeededRows(data, listed))
        {
            memcpy(rsFixedVs, data, (size_t)count * 16);
            RsFixConstBlockRows(rsFixedVs, data, listed);
            data = rsFixedVs; DEVSTAT(if (telemetryOn) { InterlockedIncrement(&rsConstFixedVs); })
        }
    }

        // Jitter, per draw: c0..c15 carries gWorldViewProj at c8 and its camera at c12 (gViewInverse). For an upload whose camera is the
    // scene camera the frame's jitter is post-multiplied into c8: with P[2][3] = -1, c8[r][3] is -WV[r][2], so
    // c8[r][0] += jx * c8[r][3] and c8[r][1] += jy * c8[r][3] (see PatchProjection). Shadow cascades, mirrors and water carry other
    // cameras and are left alone. Everything below sees the jittered c8, which is what the device holds.
    static float jitteredBlock[64 * 4];
    bool c8Jit = false;                           // this call puts a jittered scene-camera transform on c8..c11

    if (!dead && data && jitterEnabled && patchedThisFrame
        && start == 0 && count >= 16 && count <= 64)
    {
        if (viewInvProven && camCur.valid && haveSceneViewInv)
        {
            bool match = true;
            __try
            {
                // Four lanes at a time (see CamDiffMax): the maximum is past a threshold exactly when some element is, so `match` reads the same.
                float rotMax, posMax;
                CamDiffMax(data + 48, sceneViewInv, &rotMax, &posMax);
                match = (rotMax < 1e-4f && posMax < 0.05f);
                if (match)
                {
                    // One store per row: each row is loaded once, rows 8..11 are patched in the register and each is stored once, so the m4bCurWVP
                    // copy below can forward from the stores. Lanes 0 and 1 are v + j*w as in scalar; lanes 2 and 3 come from v by a mask, so a
                    // non-finite w cannot reach them through 0*w.
                    const __m128 jxy  = _mm_setr_ps(jitterNdcX, jitterNdcY, 0.0f, 0.0f);
                    const __m128 lo01 = _mm_castsi128_ps(_mm_setr_epi32(-1, -1, 0, 0));
                    for (UINT r = 0; r < count; r++)
                    {
                        __m128 v = _mm_loadu_ps(data + r * 4);
                        if (r - 8u < 4u)
                        {
                            const __m128 w  = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3));
                            const __m128 pv = _mm_add_ps(v, _mm_mul_ps(jxy, w));
                            v = _mm_or_ps(_mm_and_ps(lo01, pv), _mm_andnot_ps(lo01, v));
                        }
                        _mm_storeu_ps(jitteredBlock + r * 4, v);
                    }
                    data = jitteredBlock;
                    c8Jit = true;
                    DEVSTAT(if (telemetryOn) { InterlockedIncrement(&statC8Jittered); })
                }
                else { DEVSTAT(if (telemetryOn) { InterlockedIncrement(&statC8NotScene); }) }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { }
        }
        else { DEVSTAT(if (telemetryOn) { InterlockedIncrement(&statC8NoCamera); }) }
    }


        // FusionFix pushes its log-depth near/far pack to c227: seeing it means the logarithmic-depth shaders are live, so depth decodes as
    // viewZ = pow(far/near, d) * near, not the standard linearisation.

    if (!dead && start <= 227 && start + count >= 228 && data)
    {
        shFlags |= IVMV_FLAG_LOGDEPTH_CONST;
                // The four floats are the depth buffer's near/far: capture the camera that owns them (the scene-camera selector, NoteSceneC227).
        if (toldVerdict)
            NoteSceneC227(data + (227 - (int)start) * 4);
        memcpy(c227Cur, data + (227 - (int)start) * 4, sizeof(c227Cur)); c227Have = true;   // what the device holds now (corona_hold)
    }


        // Every upload carrying gWorld at c0..c3: the per-draw object transform (a start=0 count=16 block).
    // Must stay below the jitter block: `data` now points at jitteredBlock, so m4bCurWVP holds the jittered c8..c11. A pre-multiplied
    // tyre redraw puts m4bCurWVP back on the device (VelDrain); captured unjittered, that restore would leave the device off the
    // jitter sequence for whatever the game draws next.

    if (start < 12 && start + count > 8) c8DevJit = false;   // any write touching c8..c11; the capture below says when it is the jittered block
    if (!dead && toldVerdict && data && start == 0 && count >= 4)
    {
        m4bWorldSerial++;        // BEFORE the copy: see its declaration
        __try
        {
            memcpy(m4bCurWorld, data, 64);   // the draw hook reads this
            m4bHaveWorld = true;
            if (count >= 12)
            {
                memcpy(m4bCurWVP, data + 32, 64);   // c8..c11, restored after a pre-multiplied redraw
                m4bHaveWVP = true;
                c8DevJit = c8Jit;                   // m4bCurWVP and the device now hold the same rows; with this frame's jitter in them or not
            }
            // c12..c15 is gViewInverse: the camera's own world transform,
            // uploaded per draw. 16 registers means 64 floats, so c12 begins
            // at float 48.
            if (count >= 16)
            {
                CamMatSet(m4bCurViewInv, data + 48);
                m4bHaveViewInv = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }


    // Any upload overlapping c64..c207 updates the pose shadow. Clipped to
    // the range on both sides, because writes that merely straddle it are
    // common and must contribute only their overlapping part.

    if (!dead && data && start < 208 && start + count > 64)
    {
        int lo = (int)start < 64 ? 64 : (int)start;
        int hi = (int)(start + count) > 208 ? 208 : (int)(start + count);
        boneShadowSerial++;     // BEFORE the copy: see its declaration
        __try
        {
            memcpy(boneShadow + (lo - 64) * 4, data + (lo - (int)start) * 4,
                   (size_t)(hi - lo) * 16);
            if (hi - 1 - 64 > boneMaxReg) boneMaxReg = hi - 1 - 64;
            // matWheelTransform is exactly four registers at c64: the matrix is whatever the last write covering c64..c67 put there (the tyre
            // sends start=64 count=6, deform params behind). A palette-sized write is bones and a partial write leaves the four mixed: both
            // drop it, so a tyre drawn without a fresh upload is not placed with bone rows.
            if (start <= 64 && start + count >= 68 && count <= 16)
            {
                memcpy(m4bCurWheel, data + (64 - (int)start) * 4, 64);
                m4bHaveWheel = true;
            }
            else if (lo < 68)
            {
                m4bHaveWheel = false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }

    // gWorldViewProj occupies c8..c11. A caller may set a wider block that
    // merely contains it, so accept any write that covers all four registers.
    if (!dead && data && start <= 8 && start + count >= 12)
    {
        Mat4 got;
        bool copied = false;
        __try { memcpy(&got, data + (8 - start) * 4, sizeof(Mat4)); copied = true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { }

                // Not `fabsf(got.m[3][3]) < 0.0001f`: that tests a projection, but c8 holds the composed gWorldViewProj, where WVP[3][3] is the
        // object's view-space depth. The engine's own perspective flag answers directly.
        // One guarded region for the viewport: the slot load, the flag and the five Census fields. Partial assignments stand on a fault:
        // a slot fault leaves curVp null and the flag 0 (skipped); a flag fault leaves the flag 0 (skipped); a fault in the five fields
        // leaves vpOk false and the block still runs. The five are read only where Census runs. `if (viewportSlot)` stays outside, so a
        // null slot is not a caught fault on every upload.
        BYTE* curVp = nullptr;
        unsigned char vpPersp = 0;
        bool  vpOk = false;
        int   vpW = 0, vpH = 0;
        float vpFov = 0.0f, vpNear = 0.0f, vpFar = 0.0f;
        if (viewportSlot)
        {
            __try
            {
                curVp = *viewportSlot;
                if (curVp)
                {
                    vpPersp = *(unsigned char*)(curVp + kOffIsPerspective);
                    if (vpPersp == 1 && copied && toldVerdict)
                    {
                        vpW    = *(int*)(curVp + kOffWidth);
                        vpH    = *(int*)(curVp + kOffHeight);
                        vpFov  = *(float*)(curVp + kOffFov);
                        vpNear = *(float*)(curVp + kOffNear);
                        vpFar  = *(float*)(curVp + kOffFar);
                        vpOk   = true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { }
        }


        if (copied && vpPersp == 1)
        {
            lastC8 = got;
            haveC8 = true;
            DEVSTAT(if (telemetryOn) { InterlockedIncrement(&c8Writes); })

#if DLSSIV_DEVLOG
            if (!toldConstFired)
            {
                toldConstFired = true;
                // Positive, told once: a hook that only speaks when it fails cannot show that it ran.
                LogDetail("  hook: SetVertexShaderConstantF fired, perspective"
                    " c8..c11 captured (start=%u count=%u)", start, count);
            }
#endif

            // Only until the verdict: Verdict() makes several VirtualQuery calls.
            if (!toldVerdict)
                Verdict(true);
            else
            {
                // Census after the verdict, so frames of a proven camera are counted. The slot was validated once and every read is inside SEH.
                BYTE* vp = curVp;
                if (vp)
                {
                    Census(vpW, vpH, vpFov, vpNear, vpFar, vpOk);

                // The camera is captured at its c227 upload (NoteSceneC227). Do not bring back a per-upload capture here: it probed the viewport
                    // on every upload through VirtualQuery. Two findings still bind: size alone does not select the scene viewport (a full-resolution
                    // fov-14.25 near-448 viewport is not the scene), and a signature census can elect the default viewport at the origin, which
                    // decodes the log-depth buffer with the wrong near and far at every pixel.

                }
            }
        }
        else if (copied)
        {
            // Gated on telemetryOn: its only reader is a log line.
            DEVSTAT(if (telemetryOn) { InterlockedIncrement(&c8Ortho); })
        }
    }

    return origSetVsConstF(dev, start, data, count);
}

// Declared here because the velocity pass below needs it, and hkEndScene below that needs the velocity pass.
typedef HRESULT(__stdcall* DrawIndexed_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
DLSSIV_HOT static DrawIndexed_t     origDrawIndexed     = nullptr;
// Same reason: the velocity redraw drives the device through the originals, so these are defined above it (a second definition
// would be a redefinition, so they cannot be forward declared).
typedef HRESULT(__stdcall* SetRenderTarget_t)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT(__stdcall* SetTexture_t)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT(__stdcall* SetVertexShader_t)(IDirect3DDevice9*, IDirect3DVertexShader9*);
typedef HRESULT(__stdcall* SetSamplerState_t)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE, DWORD);
DLSSIV_HOT static SetRenderTarget_t origSetRenderTarget = nullptr;
DLSSIV_HOT static SetTexture_t      origSetTexture      = nullptr;
DLSSIV_HOT static SetVertexShader_t origSetVertexShader = nullptr;
DLSSIV_HOT static SetSamplerState_t origSetSamplerState = nullptr;
typedef HRESULT(__stdcall* ColorFill_t)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, D3DCOLOR);
static ColorFill_t       origColorFill       = nullptr;

// What the cfg asks for: lod_bias defaults to 1 and any positive means auto (the derived value); 0 writes nothing at all; a
// negative pins that bias in every mode, DLAA included.
static float LodBiasWanted()
{
    const float k = ivtCfg.lod_bias;
    if (k == 0.0f) return 0.0f;
    if (k <  0.0f) return k;
    return lodBiasAuto;
}

// Every frame, to all sixteen pixel samplers: a device Reset restores default sampler state, and a state block the game applies
// restores its own without passing through the hook; DXVK drops writes that do not change the value. Nothing is written until a
// non-zero bias is wanted or has been pushed, so lod_bias=0 leaves the game's state untouched; once one was pushed, zero is
// pushed to undo it. Through origSetSamplerState, so the mod's writes never re-enter the hook.
static void LodApplyAll(IDirect3DDevice9* dev)
{
    if (!dev || dead) return;
    const float want = LodBiasWanted();
    if (want == 0.0f && !lodEverSet) { lodActive = false; return; }
    lodBias   = want;
    memcpy(&lodBiasBits, &want, sizeof(lodBiasBits));
    lodActive = (want != 0.0f);
    if (lodActive) lodEverSet = true;
    for (DWORD s = 0; s < 16; s++)
    {
        if (origSetSamplerState) origSetSamplerState(dev, s, D3DSAMP_MIPMAPLODBIAS, lodBiasBits);
        else                     dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, lodBiasBits);
    }
    DEVSTAT(if (telemetryOn) { lodPushes++; })
}

// ---------------------------------------------------------------------------
// M4f: the per-object velocity pass
// ---------------------------------------------------------------------------
// Redraws each moving object with the mod's shaders into the velocity target. Occlusion is handled by depth: the target stores
// distance, and the consumer discards pixels where it disagrees with the final scene depth.
// Everything touched is restored: render targets 0..3, the viewport, both shaders and the changed render states. c240..c243 is
// not restored: nothing in this game writes above c237 (FusionFix's own map agrees).

#include "shaders/velocity_vs.h"
#include "shaders/velocity_skin_vs.h"
#include "shaders/velocity_skin_prev_vs.h"

// The previous-pose store on the GPU: rows are objects, columns bone registers, rewritten once a frame at EndScene so the next
// frame reads it as the previous pose.
static void BoneTexCreate(IDirect3DDevice9* dev)
{
    if (boneTexTried)
        return;
    boneTexTried = true;

        // Gated on the stage 1 round trip, not a capability bit: without it, skinned redraws use the shader that already works.
    if (!vtfProven)
    {
        LogRecord("  bone texture: not created (self-test failed): skinned movers stay rigid");
        return;
    }
        // The mod's: rsOwnCreate separates its allocations from the game's in the footprint figures.
    RsOwnCreateCall guard;
    HRESULT hr = dev->CreateTexture(BONE_REGS, BONE_STORE_ROWS, 1,
                                    D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F,
                                    D3DPOOL_DEFAULT, &boneTex, nullptr);
    if (FAILED(hr) || !boneTex)
    {
        LogRecord("  bone texture: CreateTexture(%dx%d) failed 0x%08X",
            BONE_REGS, BONE_STORE_ROWS, hr);
        boneTex = nullptr;
        return;
    }
    boneTexOK = true;
    LogHealth("  bone texture: %dx%d A32B32G32R32F, up to %d poses a frame",
        BONE_REGS, BONE_STORE_ROWS, BONE_STORE_ROWS);
}

// Called at EndScene, after every draw of the frame has had its say.
static void BoneTexUpload()
{
    DEVSTAT(if (telemetryOn) { if (bonePoolNext > velStatRowsPeak) velStatRowsPeak = bonePoolNext; })

    if (boneTexOK && boneTex && bonePoolNext > 0)
    {
        D3DLOCKED_RECT lr;
                // Only the rows used are locked: DXVK uploads a texture's dirty box at the unlock (d3d9_device.cpp, FlushImage), and a lock with
        // no rectangle dirties all of it. DXVK drops DISCARD for a partial lock; harmless, since this texture is never rendered to or
        // read back, and the upload goes through its own staging copy, so the GPU reading last frame's rows is not disturbed.
        // lr.pBits is row 0. Rows past bonePoolNext are left as they were: only the allocator hands out row indices.
        const RECT used = { 0, 0, BONE_REGS, bonePoolNext };
        if (SUCCEEDED(boneTex->LockRect(0, &lr, &used, D3DLOCK_DISCARD)))
        {
            // One copy when the pitch allows it: a tightly packed row is 144 * 16 = 2304 bytes, bonePool's own stride. The row loop stays as
            // the fallback, because the pitch is the driver's to choose and a padded one would spread bones across row boundaries.
            const size_t rowBytes = sizeof(float) * BONE_REGS * 4;
            static_assert(sizeof(bonePool[0]) == sizeof(float) * BONE_REGS * 4,
                          "bonePool rows are not tightly packed: the single-copy path below is wrong");
            if ((size_t)lr.Pitch == rowBytes)
                memcpy(lr.pBits, bonePool, (size_t)bonePoolNext * rowBytes);
            else
                for (int r = 0; r < bonePoolNext; r++)
                    memcpy((BYTE*)lr.pBits + (size_t)r * lr.Pitch, bonePool[r], rowBytes);
            boneTex->UnlockRect(0);
        }
    }
    bonePoolNext     = 0;
    bonePoolLastRow  = -1;
    bonePoolLastHash = 0;
}

#include "shaders/velocity_ps.h"

static IDirect3DVertexShader9* velVS     = nullptr;
static IDirect3DVertexShader9* velSkinVS = nullptr;

// Is the CURRENT vertex declaration skinned? Tracked from SetVertexDeclaration
// rather than queried per draw: GetVertexDeclaration AddRefs, and this runs on
// every redraw.
DLSSIV_HOT static bool velCurSkinned = false;

static IDirect3DPixelShader9*  velPS = nullptr;
static bool velShadersTried = false;
DLSSIV_HOT static bool velNeedClear = true;
DEVSTAT(static bool toldVelDraw = false;)

static bool VelShaders(IDirect3DDevice9* dev)
{
    if (velVS && velPS)
        return true;
    if (velShadersTried)
        return false;
    velShadersTried = true;

    HRESULT hv = dev->CreateVertexShader((const DWORD*)kVelocityVS, &velVS);
    HRESULT hs = dev->CreateVertexShader((const DWORD*)kVelocitySkinVS, &velSkinVS);
        // A separate shader, not a branch: fxc may flatten a runtime if and pay for every texture fetch, and one binary would let a
    // mistake in either path break both.
    HRESULT hsp = dev->CreateVertexShader((const DWORD*)kVelocitySkinPrevVS,
                                          &velSkinPrevVS);
    if (FAILED(hsp) || !velSkinPrevVS)
    {
        LogRecord("  velocity: previous-bone shader failed (0x%08X): skinned movers stay rigid", hsp);
        velSkinPrevVS = nullptr;
    }
    HRESULT hp = dev->CreatePixelShader((const DWORD*)kVelocityPS, &velPS);
    if (FAILED(hv) || FAILED(hp) || !velVS || !velPS)
    {
        LogRecord("  velocity: shader creation failed (vs 0x%08X, ps 0x%08X)", hv, hp);
        velFailed = true;
        return false;
    }
    if (FAILED(hs) || !velSkinVS)
    {
        // Not fatal: rigid movers still work, skinned ones fall back to the
        // rest pose they had before. Say so rather than degrade silently.
        LogRecord("  velocity: SKINNED shader failed (0x%08X): skinned movers will be wrong", hs);
        velSkinVS = nullptr;
    }
    LogHealth("  velocity: shaders created (rigid%s%s)",
        velSkinVS ? " + skinned" : "",
        velSkinPrevVS ? " + skinned-with-previous-bones" : "");
    return true;
}

// The clear, out of line: RsOwnCall has a destructor, and under /EHa a function holding one carries an exception frame, which
// inlined into hkDrawIndexed would cost every call for a clear that runs once a frame. The early-out stays inline in
// VelClearIfNeeded, above any guard.
static __declspec(noinline) void VelClearDo(IDirect3DDevice9* dev)
{
    RsOwnCall rsOwnGuard;   // the viewport this restores was read back already scaled
    velNeedClear = false;

    IDirect3DSurface9* oldRT = nullptr;
    IDirect3DSurface9* oldDS = nullptr;
    D3DVIEWPORT9 oldVp;
    RECT oldSc;                              // a bind of target 0 resets the scissor rectangle too (see IvtSavedState)
    if (FAILED(dev->GetRenderTarget(0, &oldRT))) return;
    const LONG dsSeq = DsBracketOpen();      // ds_bracket.h
    dev->GetDepthStencilSurface(&oldDS);
    dev->GetViewport(&oldVp);
    dev->GetScissorRect(&oldSc);

    if (SUCCEEDED(dev->SetRenderTarget(0, velSurf)))
    {
        dev->SetDepthStencilSurface(nullptr);
        // Zero everywhere, alpha included: alpha is the validity flag, so a
        // zeroed pixel reads as "no object", while a stationary object still
        // gets to write a legitimate zero motion vector with alpha 1.
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 1.0f, 0);
    }
    dev->SetRenderTarget(0, oldRT);
    dev->SetDepthStencilSurface(DsBracketPutBack(oldDS, dsSeq));
    dev->SetViewport(&oldVp);
    dev->SetScissorRect(&oldSc);             // under rsOwnGuard above: it was read back already scaled
    if (oldRT) oldRT->Release();
    if (oldDS) oldDS->Release();
}

// Cleared to zero at the start of each frame, before its first redraw: the transport's guide pass reads it at the injection, so
// the clear must come before every write of the frame.
static void VelClearIfNeeded(IDirect3DDevice9* dev)
{
        // The early-out comes first: rsOwn is tested by several hooks, so a guard taken on every draw would cost them all, and the clear
    // runs at most once a frame.
    if (!velNeedClear || !velSurf || velFailed)
        return;
    VelClearDo(dev);        // the guard and the device calls are there
}

// Redraw one moving object into the velocity target. `prevWVP` is
// prevWorld * viewPrev * projPrev, computed on the CPU in double.
typedef HRESULT(__stdcall* SetStreamSource_t)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
typedef HRESULT(__stdcall* SetIndices_t)(IDirect3DDevice9*, IDirect3DIndexBuffer9*);
typedef HRESULT(__stdcall* SetVertexDecl_t)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
typedef HRESULT(__stdcall* SetDepthStencil_t)(IDirect3DDevice9*, IDirect3DSurface9*);

DLSSIV_HOT static SetStreamSource_t origSetStreamSource = nullptr;
DLSSIV_HOT static SetIndices_t      origSetIndices      = nullptr;
DLSSIV_HOT static SetVertexDecl_t   origSetVertexDecl   = nullptr;
DLSSIV_HOT static SetDepthStencil_t origSetDepthStencil = nullptr;

// ===========================================================================
// The deferred velocity redraw
// Movers are recorded here and drained once, so the target changes twice a frame instead of twice per mover (in DXVK each
// render-target change ends one render pass and begins another).
// A deferred draw inherits nothing: every value the redraw needs from the device is captured per entry (the buffers, the
// declaration, velCurSkinned at the draw, c8, the viewport, the cull mode). References are taken: the game's streamer runs on
// another thread and can free a model between the draw and the seam. sameAsPrev keeps the cost down: a vehicle's parts share
// buffers. The pass depth-tests LESSEQUAL against the complete scene depth at the seam, so a mover later occluded writes no
// vectors there.
// ===========================================================================
#define VEL_BATCH_MAX 2048          // about 800 a frame at a busy traffic spot; overflow drains early rather than dropping

struct VelPend
{
    D3DPRIMITIVETYPE type;
    INT   base;
    UINT  minv, numv, start, prim;
    float prevWVP[16];
    float wvp8[16];
        // The game's own c8 for this object: all three velocity shaders read `row_major float4x4 gWVP : register(c8)`, uploaded per
    // object, and a deferred draw would otherwise get whatever the game uploaded last.
    float curWVP[16];
    bool  haveCurWVP;
        // And its viewport: at the injection the bound viewport is the full output rect, while the velocity target is render-sized.
    D3DVIEWPORT9 vp;
        // The cull mode, for the same reason: drained later, the bound one is the injection quad's.
    DWORD cull;
    int   boneRow;
    int   curRow;                   // a deferred skinned entry's current pose: its row in bonePool. -1 on the immediate path
    bool  premul;
    bool  skinned;                  // velCurSkinned at the draw, not at the drain
    bool  sameAsPrev;               // bindings equal the previous entry: no ref, no rebind
    IDirect3DVertexBuffer9*      vb;
    UINT                         vbOff, vbStr;
    IDirect3DIndexBuffer9*       ib;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DSurface9*           ds;
        // The streams beyond 0 this object's declaration reads: 0 for every single-stream mover (all of ordinary play), and then nothing
    // below is touched. The device thread may not draw, so a mover it cannot defer is dropped; cutscene characters carry one
    // multi-stream skinned mesh each, which showed as ghosting on faces. Fenced three ways: only where the alternative is a drop
    // (the render thread still draws on the spot); skinned only (a rigid multi-stream draw may be instancing, whose stream frequency
    // this does not carry); and any doubt (a stream above 3, or a binding never seen) leaves the mover non-deferrable.
    unsigned char                xMask;
    IDirect3DVertexBuffer9*      xvb[VEL_XSTREAMS];
    UINT                         xoff[VEL_XSTREAMS], xstr[VEL_XSTREAMS];
        // The game's damage for a dented vehicle (VelDmgNote, at the file's end): the map its draw read on the first vertex sampler (this
    // entry's own reference, released in VelDropArr whatever sameAsPrev says), its BoundRadius (c208.x) and every sampler state of the
    // map, as whole DWORDs. Null on every other mover, and the drain then draws as before.
    IDirect3DBaseTexture9*       dmgTex;
    float                        dmgRadius;
    DWORD                        dmgSamp[VEL_DMG_SAMP_N];
};
static VelPend velBatch[VEL_BATCH_MAX];
static int     velBatchN = 0;
// Skinned movers are deferred too: both skinned shaders read the current pose at c64, which exists on the device only during the
// object's own draw, but the tracker has already stored it in bonePool, and that row is this frame's pose on the CPU. A skinned
// entry records its row, the drain uploads it to c64 when it differs from the last bound, and the game's palette goes back at the
// end from boneShadow, as c8 does. This scratch is where one goes when the store has no row left.
static VelPend velScratch;
DEVSTAT(static LONG    velBatchOverflow = 0, velBatchPeak = 0;)
// What ran, a frame, said every 600 frames at log=2 (VelEndFrame).
DEVSTAT(static LONG    velStatDrains = 0, velStatEntries = 0, velStatSkinDeferred = 0, velStatSkinImmediate = 0, velStatPalettes = 0;)
#if DLSSIV_DEV      // the key's own fence: the key is DLSSIV_DEV's
static volatile LONG velStatWheel[6] = { 0, 0, 0, 0, 0, 0 };   // mv_wheel: tyres by VelWheelNoSpin's answer (0 to 4, beside TrkOnDrawPre), and [5] the tyres met with mv_wheel=0
#endif
DEVSTAT(static volatile LONG velStatOut3[4] = { 0, 0, 0, 0 };)   // mv_glass=3: blended movers with depth writes ON: left out, their depth not the guides' (rigid, skinned: a vehicle's panes are skinned), and redrawn, their depth the guides' (rigid, skinned: hair)
DEVSTAT(static volatile LONG velStatZwKind[4] = { 0, 0, 0, 0 }, velStatZwOut = 0;)   // movers drawn with depth writes off by kind (blended rigid/skinned, not blended rigid/skinned), and those left out (mv_glass=2)
static LONG    velStatFrames = 0;

// Releases what the batch referenced and empties it. Every exit from the drain
// goes through here, including the early refusals, or the references leak.
static void VelDropArr(VelPend* arr, int n)
{
    for (int i = 0; i < n; i++)
    {
        VelPend& e = arr[i];
        if (e.dmgTex) { e.dmgTex->Release(); e.dmgTex = nullptr; }   // the entry's own, taken by VelDmgNote
        if (e.sameAsPrev) continue;
        if (e.vb)   e.vb->Release();
        if (e.ib)   e.ib->Release();
        if (e.decl) e.decl->Release();
        for (int s = 0; s < VEL_XSTREAMS; s++)
            if (e.xMask & (1u << s)) e.xvb[s]->Release();      // taken with the three above, in VelRecordObject
    }
}
static void VelBatchDrop() { VelDropArr(velBatch, velBatchN); velBatchN = 0; }

// Draws a run of recorded entries. Never releases: the caller owns and drops the array.
static void VelDrain(IDirect3DDevice9* dev, bool rebind, VelPend* arr, int n);
static void VelFlushBatch(IDirect3DDevice9* dev, bool rebind);

static void VelFlushBatch(IDirect3DDevice9* dev, bool rebind)
{
    VelDrain(dev, rebind, velBatch, velBatchN);
    VelBatchDrop();
}

// Called from the game's draw, with the game's state for it still bound.
template<class S> static void VelRecordObject(IDirect3DDevice9* dev, const float* prevWVP,
                              D3DPRIMITIVETYPE type, INT baseVertex, UINT minVertex,
                              UINT numVertices, UINT startIndex, UINT primCount,
                              int prevBoneRow, const float* wvpOverride)
{
        // Blended movers, and those drawn with depth writes off, are not redrawn (mv_glass=3): a pane's redraw would cover the
    // vectors behind it. Except a blended mover drawn against the depth the guide pass reads (a ped's hair).
#if DLSSIV_DEV
    const bool velAskZw = true;
#else
    const bool velAskZw = ivtCfg.mv_glass >= 2;
#endif
    if (velAskZw)
    {
        DWORD zw = TRUE;
        dev->GetRenderState(D3DRS_ZWRITEENABLE, &zw);
        if (!zw)
        {
            DEVSTAT({ DWORD ab = FALSE; dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
                      InterlockedIncrement(&velStatZwKind[(ab ? 0 : 2) + (S::Skinned() ? 1 : 0)]); })
            if (ivtCfg.mv_glass >= 2) { DEVSTAT(InterlockedIncrement(&velStatZwOut);) return; }
        }
        else if (ivtCfg.mv_glass >= 3)
        {
            const bool guides = S::DS() == (dsSceneMajority ? dsSceneMajority : dsAtSceneDraws);   // the depth the guide pass reads
#if DLSSIV_DEV      // a development build asks every mover, to count them, and has mode 4 (hair left out too)
            DWORD ab = FALSE;
            dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
            if (ab)
            {
                DEVSTAT(InterlockedIncrement(&velStatOut3[(guides ? 2 : 0) + (S::Skinned() ? 1 : 0)]);)
                if (!guides || ivtCfg.mv_glass == 4) return;
            }
#else
            if (!guides)
            {
                DWORD ab = FALSE;
                dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
                if (ab) return;
            }
#endif
        }
    }
        // Full means drain, not drop: dropping would lose an object's vectors and show as a ghost.
    if (velBatchN >= VEL_BATCH_MAX)
    {
        DEVSTAT(if (telemetryOn) { InterlockedIncrement(&velBatchOverflow); })
        if (S::kMayDraw) VelFlushBatch(dev, true);
        else
        {
            // The device thread may not draw: a full batch there loses this mover (velStatDropFull, said once). The cap of 2048
            // is well above the largest frame seen (848).
            velStatDropFull++;
            return;
        }
    }
        // Skinned is drawn now from the scratch: its bone palette is live only for its own draw. Not deferred when it reads a
    // stream other than 0 unless carried (worker side, VelPend::xMask). On the render thread `carry` is constant false.
    unsigned char carry = 0;
    if (!S::kMayDraw && S::Multi() && S::Skinned())
    {
        const unsigned char xm = S::XMask();
        bool ok = xm != 0 && !(xm & VEL_XMASK_BEYOND);
        for (int s = 0; ok && s < VEL_XSTREAMS; s++)
            if ((xm & (1u << s)) && !S::XVB(s)) ok = false;
        if (ok) carry = xm;
    }
    const bool multiBlocks   = S::Multi() && !carry;
    const int  curRow        = (S::Skinned() && S::BoneMaxReg() >= 0 && !multiBlocks) ? BoneStoreRowFor<S>(HashBones<S>()) : -1;
    const bool velDeferrable = !multiBlocks && (!S::Skinned() || curRow >= 0);
    DEVSTAT(if (telemetryOn && S::Skinned()) { if (velDeferrable) velStatSkinDeferred++; else velStatSkinImmediate++; })
    VelPend& e = velDeferrable ? velBatch[velBatchN] : velScratch;
    e.type = type; e.base = baseVertex; e.minv = minVertex;
    e.numv = numVertices; e.start = startIndex; e.prim = primCount;
    memcpy(e.prevWVP, prevWVP, sizeof(e.prevWVP));
    e.premul = (wvpOverride != nullptr);
    if (e.premul) memcpy(e.wvp8, wvpOverride, sizeof(e.wvp8));
    e.boneRow = prevBoneRow;
    e.curRow  = velDeferrable ? curRow : -1;
    e.skinned = S::Skinned();      // the trap: this is a global, and it moves
    e.haveCurWVP = S::HaveWVP();
    if (S::HaveWVP()) memcpy(e.curWVP, S::WVP(), sizeof(e.curWVP));
        // Queried, not shadowed: the viewport shadows here are render-scale specific, and GetViewport neither AddRefs nor
    // allocates.
    dev->GetViewport(&e.vp);
    dev->GetRenderState(D3DRS_CULLMODE, &e.cull);
    e.vb = S::VB(); e.vbOff = S::VBOff(); e.vbStr = S::VBStr();
    e.ib = S::IB(); e.decl = S::Decl(); e.ds = S::DS();
    VelDmgNote(dev, S::Texld0() == 4 ? S::VS() : nullptr, &e);   // a dented vehicle's damage map and states, or null; only a candidate's shader is asked
        // Always written, on the scratch too: VelDropArr reads it and the scratch is reused.
    e.xMask = velDeferrable ? carry : 0;
    for (int s = 0; s < VEL_XSTREAMS; s++)
        if (e.xMask & (1u << s)) { e.xvb[s] = S::XVB(s); e.xoff[s] = S::XOff(s); e.xstr[s] = S::XStr(s); }
        // sameAsPrev covers the references and the rebinds, so it compares every field the drain binds. The scratch never inherits
    // a neighbour's bindings.
    const VelPend* p = (velDeferrable && velBatchN > 0) ? &velBatch[velBatchN - 1] : nullptr;
    e.sameAsPrev = p && p->vb == e.vb && p->vbOff == e.vbOff && p->vbStr == e.vbStr
                     && p->ib == e.ib && p->decl == e.decl && p->xMask == e.xMask;
    for (int s = 0; e.sameAsPrev && s < VEL_XSTREAMS; s++)
        if ((e.xMask & (1u << s)) && (p->xvb[s] != e.xvb[s] || p->xoff[s] != e.xoff[s] || p->xstr[s] != e.xstr[s]))
            e.sameAsPrev = false;
    if (!e.sameAsPrev)
    {
        if (e.vb)   e.vb->AddRef();
        if (e.ib)   e.ib->AddRef();
        if (e.decl) e.decl->AddRef();
        for (int s = 0; s < VEL_XSTREAMS; s++)
            if (e.xMask & (1u << s)) e.xvb[s]->AddRef();      // non-null: `carry` required every one of them
    }
    if (!velDeferrable)
    {
        if (S::kMayDraw)
        {
            // rebind=false: the game bound this object a moment ago and nothing has
            // run since, so its buffers, declaration and bone palette are all live.
            VelDrain(dev, false, &velScratch, 1);
        }
        else velStatDropNow++;      // the device thread may not draw: counted, said once
        VelDropArr(&velScratch, 1);
        return;
    }
    velBatchN++;
    DEVSTAT(if (telemetryOn && velBatchN > velBatchPeak) velBatchPeak = velBatchN;)
}

// Drains the batch. `rebind` is false when the game's own bindings are still those the single entry was recorded with (the
// immediate draw of velScratch).
static void VelDrain(IDirect3DDevice9* dev, bool rebind, VelPend* arr, int n)
{
    if (n <= 0) return;
    RsOwnCall rsOwnGuard;   // likewise: GetViewport then SetViewport must not scale twice
    if (!velSurf || velFailed || !VelShaders(dev))
        return;                 // the caller releases; see VelDrain's note

        // Binds go through the originals, which the hooks would misread; dev->SetViewport stays hooked (see below). This guard
    // cannot fire: HookVtable captures every original before writing a slot.
    if (!origSetRenderTarget || !origSetTexture || !origSetVertexShader || !origSetVsConstF)
    {
        static bool told = false;
        if (!told)
        {
            told = true;
            LogRecord("  velocity: a hook original is null in the redraw: no objects are redrawn");
        }
        return;             // the caller releases
    }

        // --- save ---
    // The shadows match the device on the render thread only; on the worker everything is read back from the device and
    // released after the restore.
    const bool onWorker = !RsGuardHere();
    IDirect3DSurface9* oldRT[4] = { curRT0, curRT123[0], curRT123[1], curRT123[2] };
    if (onWorker)
        for (int i = 0; i < 4; i++) { oldRT[i] = nullptr; dev->GetRenderTarget((DWORD)i, &oldRT[i]); }
    if (!oldRT[0])              // the caller releases the batch; these are released here
    {
        if (onWorker) for (int i = 1; i < 4; i++) if (oldRT[i]) oldRT[i]->Release();
        return;
    }

    IDirect3DVertexShader9* oldVS = nullptr;
    IDirect3DPixelShader9*  oldPS = nullptr;
    dev->GetVertexShader(&oldVS);
    dev->GetPixelShader(&oldPS);

    D3DVIEWPORT9 oldVp;
    dev->GetViewport(&oldVp);
    RECT oldSc;                              // the rectangle; the enable is oldSC below (IvtSavedState says why)
    dev->GetScissorRect(&oldSc);

    DWORD oldZW = 0, oldCW = 0, oldAB = 0, oldAT = 0, oldZF = 0, oldST = 0;
        // ZENABLE and the scissor are saved and set: drained at the injection, the redraw would inherit a full-screen quad's
    // ZENABLE FALSE and write vectors through walls.
    DWORD oldZE = 0, oldSC = 0, oldCull = 0;
    dev->GetRenderState(D3DRS_ZENABLE,          &oldZE);
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldSC);
    dev->GetRenderState(D3DRS_CULLMODE,         &oldCull);
    dev->GetRenderState(D3DRS_ZWRITEENABLE,     &oldZW);
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &oldCW);
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAB);
    dev->GetRenderState(D3DRS_ALPHATESTENABLE,  &oldAT);
    dev->GetRenderState(D3DRS_ZFUNC,            &oldZF);
    dev->GetRenderState(D3DRS_STENCILENABLE,    &oldST);

        // --- set ---
    // Targets 1..3 are unbound, or this draw writes into the game's other G-buffer sheets.
    for (int i = 1; i < 4; i++)
        if (oldRT[i]) origSetRenderTarget(dev, i, nullptr);
    origSetRenderTarget(dev, 0, velSurf);
        // Hooked on purpose: DXVK resets its viewport on the bind above, and hkSetViewport keeps rsDxvkVp in step.
    dev->SetViewport(&oldVp);

        // Every condition must hold (round trip passed, texture exists, shader compiled, a pose stored from last frame); any one
    // missing falls back to the current-pose skinned shader.
    IDirect3DBaseTexture9* oldVTex = nullptr;
    static const D3DSAMPLERSTATETYPE kVtfSamp[5] = { D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV };
    DWORD oldVSamp[5] = {};
    bool boundBoneTex = false;
        // What the game had bound, so the drain can put it back; read only when the drain rebinds.
    IDirect3DVertexBuffer9*      gameVB   = rebind ? velCurVB   : nullptr;
    UINT                         gameVBOff = velCurVBOff, gameVBStr = velCurVBStr;
    IDirect3DIndexBuffer9*       gameIB   = rebind ? velCurIB   : nullptr;
    IDirect3DVertexDeclaration9* gameDecl = rebind ? velCurDecl : nullptr;
    const LONG                   gameDsSeq = DsBracketOpen();   // BEFORE the read below: ds_bracket.h
    IDirect3DSurface9*           gameDSv  = curDS;
        // On the worker, from the device, before anything below writes them; the whole 144-register palette, since boneMaxReg may
    // grow.
    float wkWvp[16];
    static float wkBones[144 * 4];      // the worker's alone: one worker, and the drain does not nest there
    if (onWorker)
    {
        gameVB = nullptr; gameIB = nullptr; gameDecl = nullptr; gameDSv = nullptr;
        gameVBOff = 0; gameVBStr = 0;
        if (rebind)
        {
            dev->GetStreamSource(0, &gameVB, &gameVBOff, &gameVBStr);
            dev->GetIndices(&gameIB);
            dev->GetVertexDeclaration(&gameDecl);
        }
        dev->GetDepthStencilSurface(&gameDSv);
        dev->GetVertexShaderConstantF(8,  wkWvp,   4);
        dev->GetVertexShaderConstantF(64, wkBones, 144);
    }
        // Streams 1..3 are saved lazily, the first time the drain binds over one: from the device on the worker, else from the
    // shadow.
    unsigned char                xTouched = 0;
    IDirect3DVertexBuffer9*      gameXVB[VEL_XSTREAMS]  = { nullptr };
    UINT                         gameXOff[VEL_XSTREAMS] = { 0 }, gameXStr[VEL_XSTREAMS] = { 0 };
    IDirect3DSurface9* const     gameDS   = gameDSv;
    IDirect3DSurface9*           boundDS  = gameDSv;
    D3DVIEWPORT9                 boundVp  = oldVp;
    DWORD                        boundCull = oldCull;
    IDirect3DVertexShader9*      boundVS  = nullptr;
    int                          boundCurRow = -1;      // the pose row last uploaded to c64 by this drain
    DEVSTAT(if (telemetryOn) { velStatDrains++; velStatEntries += n; })

        // Set once a drain: depth test on with LESSEQUAL so only the object's own visible surface passes, scissor off, depth write
    // off, stencil off.
    dev->SetRenderState(D3DRS_ZENABLE,           D3DZB_TRUE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE,  0x0F);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
    dev->SetRenderState(D3DRS_ZFUNC,             D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_STENCILENABLE,     FALSE);

        // ---- one entry at a time; everything above is paid once ----
    for (int velIdx = 0; velIdx < n; velIdx++)
    {
    const VelPend& e = arr[velIdx];
        // The recorded values, under the names the body uses.
    const float* const     prevWVP     = e.prevWVP;
    const D3DPRIMITIVETYPE type        = e.type;
    const INT              baseVertex  = e.base;
    const UINT             minVertex   = e.minv,  numVertices = e.numv;
    const UINT             startIndex  = e.start, primCount   = e.prim;
    const int              prevBoneRow = e.boneRow;
    const float* const     wvpOverride = e.premul ? e.wvp8 : nullptr;
    if (rebind && !e.sameAsPrev)
    {
                // Through the originals: the hooks would overwrite the shadows the restore reads.
        if (origSetVertexDecl)   origSetVertexDecl(dev, e.decl);
        if (origSetStreamSource) origSetStreamSource(dev, 0, e.vb, e.vbOff, e.vbStr);
        if (origSetIndices)      origSetIndices(dev, e.ib);
                // Streams beyond 0 this entry's declaration reads (VelPend::xMask). A later single-stream entry leaves them bound,
        // which D3D9 allows; the game's go back at the end.
        if (e.xMask && origSetStreamSource)
            for (int s = 0; s < VEL_XSTREAMS; s++)
            {
                const unsigned char bit = (unsigned char)(1u << s);
                if (!(e.xMask & bit)) continue;
                if (!(xTouched & bit))
                {
                    xTouched |= bit;
                    if (onWorker) dev->GetStreamSource((UINT)s + 1, &gameXVB[s], &gameXOff[s], &gameXStr[s]);
                    else { gameXVB[s] = velCurXVB[s]; gameXOff[s] = velCurXOff[s]; gameXStr[s] = velCurXStr[s]; }
                }
                origSetStreamSource(dev, (UINT)s + 1, e.xvb[s], e.xoff[s], e.xstr[s]);
            }
    }
        // The depth this object was drawn against, bound only when it changes (in practice once a frame). Through the original, so
    // the hook's unguarded depth creation is not reached.
    if (e.ds != boundDS && origSetDepthStencil) { origSetDepthStencil(dev, e.ds); boundDS = e.ds; }
        // The object's viewport, deduped: in practice one call a batch.
    if (memcmp(&e.vp, &boundVp, sizeof(D3DVIEWPORT9)) != 0)
    { dev->SetViewport(&e.vp); boundVp = e.vp; }

    const bool usePrevBones = e.skinned && velSkinPrevVS
                              && boneTexOK && boneTex && prevBoneRow >= 0;
    if (usePrevBones && !boundBoneTex)
    {
        dev->GetTexture(D3DVERTEXTEXTURESAMPLER0, &oldVTex);
        origSetTexture(dev, D3DVERTEXTEXTURESAMPLER0, boneTex);
                // Read first and put back: on the worker the device thread's copy would otherwise answer another writer of this sampler
        // wrongly.
        for (int k = 0; k < 5; k++) dev->GetSamplerState(D3DVERTEXTEXTURESAMPLER0, kVtfSamp[k], &oldVSamp[k]);
                // Point, no mips: a blended bone matrix is not a bone matrix. Per object, not hoisted: something else sets this sampler
        // to LINEAR between redraws (SetSamplerState is not hooked).
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

        boundBoneTex = true;
    }
        // The row constant is per object: each mover reads a different row of the same pose texture.
    if (usePrevBones)
    {
        const float bfRow[4] = {
            ((float)prevBoneRow + 0.5f) / (float)BONE_STORE_ROWS,
            1.0f / (float)BONE_REGS,
            0.0f, 1.0f
        };
        origSetVsConstF(dev, 245, bfRow, 1);
    }

        // Skinned geometry needs a skinned shader, or it is drawn at rest pose. Deduped across the batch.
    {
        IDirect3DVertexShader9* wantVS = usePrevBones ? velSkinPrevVS
                            : ((e.skinned && velSkinVS) ? velSkinVS : velVS);
        if (e.dmgTex) wantVS = VelDmgBind(dev, &e, wantVS);   // a dented vehicle: the damage shader, its map on the second vertex sampler
        if (wantVS != boundVS) { origSetVertexShader(dev, wantVS); boundVS = wantVS; }
    }
    if (velIdx == 0) dev->SetPixelShader(velPS);   // not hooked; nothing to bypass
    origSetVsConstF(dev, 240, prevWVP, 4);
        // A deferred skinned entry's pose, through the original (the hook would take it for the game's upload). One upload per
    // object.
    if (e.skinned && e.curRow >= 0 && e.curRow != boundCurRow && boneMaxReg >= 0)
    {
        origSetVsConstF(dev, 64, bonePool[e.curRow], ((UINT)(boneMaxReg + 1)));
        boundCurRow = e.curRow;
        DEVSTAT(if (telemetryOn) { velStatPalettes++; })
    }

        // near, far and the game's depth encoding (from c227): oDepth must match the buffer's encoding. 1 / log2(far / near) with
    // the shader's clamps; 0 for a degenerate camera, when the log arm is unused. From the command on the injection path
    // (velCamFromCmd).
    const bool  dpCamOk = velCamFromCmd ? velCamCmdValid : camCur.valid;
    const float dpNear  = dpCamOk ? (velCamFromCmd ? velCamCmdNear : camCur.nearC) : 0.05f;
    const float dpFar   = dpCamOk ? (velCamFromCmd ? velCamCmdFar  : camCur.farC)  : 4500.0f;
        // Memoised on its two input values; the stored value is the rounded reciprocal, so the shader gets the same bits. The
    // have-flag is separate: a zeroed key is a valid-looking pair.
    if (!velDpHave || dpNear != velDpKeyN || dpFar != velDpKeyF)
    {
        const float dpNc = (dpNear > 1e-5f) ? dpNear : 1e-5f;
        const float dpFc = (dpFar > dpNc * 2.0f) ? dpFar : dpNc * 2.0f;
        const float dpLg = log2f(dpFc / dpNc);
        velDpLgInv = (dpLg > 1e-6f) ? (1.0f / dpLg) : 0.0f;
        velDpKeyN = dpNear; velDpKeyF = dpFar; velDpHave = true;
    }
    const float dp[4] = {
        dpNear,
        dpFar,
        (shFlags & IVMV_FLAG_LOGDEPTH_CONST) ? 1.0f : 0.0f,
        velDpLgInv
    };
    const bool velConstNewFrame = (velConstFrame != frames);
    if (velConstNewFrame || memcmp(velLastDp, dp, sizeof(dp)) != 0)
    {
        origSetVsConstF(dev, 244, dp, 1);
        memcpy(velLastDp, dp, sizeof(dp));
    }

        // This frame's jitter, so the vector excludes it (c8 is jittered, prevWVP is not). From the command on the injection path.
    const float jt[4] = {
        velCamFromCmd ? velJitCmdX : ((jitterEnabled && patchedThisFrame) ? jitterNdcX : 0.0f),
        velCamFromCmd ? velJitCmdY : ((jitterEnabled && patchedThisFrame) ? jitterNdcY : 0.0f),
        0.0f, 0.0f
    };
    if (velConstNewFrame || memcmp(velLastJt, jt, sizeof(jt)) != 0)
    {
        origSetVsConstF(dev, 246, jt, 1);
        memcpy(velLastJt, jt, sizeof(jt));
    }
    velConstFrame = frames;

        // The eight states this pass needs are set once a drain, above the loop. The cull mode this object was drawn with, deduped.
    if (e.cull != boundCull) { dev->SetRenderState(D3DRS_CULLMODE, e.cull); boundCull = e.cull; }

        // c8 per object: the transform the game uploaded for this draw (for a tyre, M x c8), through the original so the constant
    // hook does not log it.
    if (origSetVsConstF)
    {
        if (wvpOverride)       origSetVsConstF(dev, 8, wvpOverride, 4);
        else if (e.haveCurWVP) origSetVsConstF(dev, 8, e.curWVP, 4);
    }

    origDrawIndexed(dev, type, baseVertex, minVertex, numVertices,
                    startIndex, primCount);
    }   // ---- end of the per-entry loop ----

        // Once, at the end: c8 back to the game's (m4bCurWVP); on the worker c8 and the palette back to what the device held
    // (wkWvp, wkBones).
    if (onWorker)
    {
        if (origSetVsConstF) origSetVsConstF(dev, 8, wkWvp, 4);
    }
    else if (origSetVsConstF && m4bHaveWVP)
        origSetVsConstF(dev, 8, m4bCurWVP, 4);
        // And the game's bone palette, if this drain overwrote it: the game may draw another sub-mesh of the character without
    // uploading again.
    if (onWorker)
    {
        if (boundCurRow >= 0) origSetVsConstF(dev, 64, wkBones, 144);
    }
    else if (boundCurRow >= 0 && boneMaxReg >= 0)
        origSetVsConstF(dev, 64, boneShadow, ((UINT)(boneMaxReg + 1)));

        // --- restore ---
    if (boundBoneTex)
    {
        origSetTexture(dev, D3DVERTEXTEXTURESAMPLER0, oldVTex);
        if (oldVTex) oldVTex->Release();
        for (int k = 0; k < 5; k++) dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, kVtfSamp[k], oldVSamp[k]);
    }
    VelDmgEnd(dev);   // the second vertex sampler as found, when a dented vehicle bound it
    if (boundCull != oldCull) dev->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, oldSC);
    dev->SetRenderState(D3DRS_ZENABLE,           oldZE);
    dev->SetRenderState(D3DRS_STENCILENABLE,    oldST);
    dev->SetRenderState(D3DRS_ZFUNC,            oldZF);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE,  oldAT);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAB);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, oldCW);
    dev->SetRenderState(D3DRS_ZWRITEENABLE,     oldZW);
    origSetVertexShader(dev, oldVS);
    dev->SetPixelShader(oldPS);
        // The game's depth stencil as it stands now: another thread's bind may have landed while this drained (ds_bracket.h).
    IDirect3DSurface9* const putBackDS = DsBracketPutBack(gameDS, gameDsSeq);
    if (boundDS != putBackDS && origSetDepthStencil) origSetDepthStencil(dev, putBackDS);
        // The game's own bindings, back through the originals; only when the drain rebound (the deferred path).
    if (rebind)
    {
        if (origSetVertexDecl)   origSetVertexDecl(dev, gameDecl);
        if (origSetStreamSource) origSetStreamSource(dev, 0, gameVB, gameVBOff, gameVBStr);
        if (origSetIndices)      origSetIndices(dev, gameIB);
        // And whichever of streams 1..3 this drain bound over, which is none unless a multi-stream entry was in it.
        if (xTouched && origSetStreamSource)
            for (int s = 0; s < VEL_XSTREAMS; s++)
                if (xTouched & (1u << s)) origSetStreamSource(dev, (UINT)s + 1, gameXVB[s], gameXOff[s], gameXStr[s]);
    }
    origSetRenderTarget(dev, 0, oldRT[0]);
    for (int i = 1; i < 4; i++)
        if (oldRT[i]) origSetRenderTarget(dev, i, oldRT[i]);
    dev->SetViewport(&oldVp);
        // The rectangle was read back from DXVK, already in render-size terms, so the hook must not scale it again. On the worker
    // the hook does nothing (DT_NOT_ON_WORKER).
    { RsOwnCall own; dev->SetScissorRect(&oldSc); }

    if (oldVS) oldVS->Release();
    if (oldPS) oldPS->Release();
        // On the render thread the targets came from the shadow, raw pointers the game owns: no release. On the worker they came
    // from the device, and every Get took a reference.
    if (onWorker)
    {
        for (int i = 0; i < 4; i++) if (oldRT[i]) oldRT[i]->Release();
        if (gameVB)   gameVB->Release();
        if (gameIB)   gameIB->Release();
        if (gameDecl) gameDecl->Release();
        if (gameDS)   gameDS->Release();
        for (int s = 0; s < VEL_XSTREAMS; s++)
            if ((xTouched & (1u << s)) && gameXVB[s]) gameXVB[s]->Release();     // GetStreamSource took one each
    }

        // Skinning layout: BLENDWEIGHT and BLENDINDICES are D3DCOLOR, the matrices are object space, three registers per bone, and
    // the registers are columns. velocity_skin_prev_vs hard-codes this.

#if DLSSIV_DEVLOG
    if (!toldVelDraw)
    {
        toldVelDraw = true;
        LogHealth("  velocity: first object redrawn into the target");
    }
#endif
}

// The velocity target's frame end: arm the next frame's clear, and the drain line.
static void VelEndFrame()
{
    velNeedClear = true;
        // What the redraw did, a frame. A drain is one render-target round trip, which is what it costs DXVK.
    if (telemetryOn && ++velStatFrames >= 600)
    {
        const double f = 1.0 / (double)velStatFrames;
        LogHealth("  velocity redraw, a frame over %ld frames: %.1f objects in %.2f drains (a drain is one render-target round trip);"
                  " skinned deferred %.1f with %.1f palette uploads, skinned drawn at once %.1f (no row to spare, or a stream this does not carry);"
                  " batch peak %ld of %d, overflowed %ld times; pose rows peak %ld of %d (the last %d are the tracker's alone),"
                  " poses the TRACKER could not store %ld (each is a previous pose lost next frame);"
                  " dropped on the device thread: batch full %ld, not deferrable %ld (both for life);"
                  " drains the worker ran after the frame's end had invalidated the live camera %ld, and of a jittered"
                  " frame after its jitter flag was down %ld (both for life; harmless since 2026-09-19, both travel in"
                  " the command)",
                  velStatFrames, velStatEntries * f, velStatDrains * f, velStatSkinDeferred * f, velStatPalettes * f,
                  velStatSkinImmediate * f, velBatchPeak, VEL_BATCH_MAX, velBatchOverflow, velStatRowsPeak, BONE_STORE_ROWS,
                  BONE_ROWS_TRACKER_ONLY, velStatTrackerLost, velStatDropFull, velStatDropNow, velDrainAfterFrameEnd,
                  velDrainJitterGone);
        if ((velStatDropFull | velStatDropNow) && !velDropSaid)
        {
            velDropSaid = true;
            LogRecord("  velocity: the device thread DROPPED a mover (batch full %ld, not deferrable %ld so far)",
                  velStatDropFull, velStatDropNow);
        }
        DEVSTAT({ LONG zk[4]; for (int k = 0; k < 4; k++) zk[k] = InterlockedExchange(&velStatZwKind[k], 0);
                  const LONG zl = InterlockedExchange(&velStatZwOut, 0);
                  LogHealth("  velocity: movers drawn with depth writes off, a frame over %ld frames: %.2f seen (blended rigid %.2f, blended skinned %.2f,"
                            " not blended rigid %.2f, not blended skinned %.2f), %.2f left out of the pass (mv_glass=%d)",
                            velStatFrames, (zk[0] + zk[1] + zk[2] + zk[3]) * f, zk[0] * f, zk[1] * f, zk[2] * f, zk[3] * f, zl * f, ivtCfg.mv_glass); })
        DEVSTAT({ LONG o3[4]; for (int k = 0; k < 4; k++) o3[k] = InterlockedExchange(&velStatOut3[k], 0);
                  if (ivtCfg.mv_glass >= 3 || o3[0] + o3[1] + o3[2] + o3[3])
                      LogHealth("  velocity: blended movers drawn with depth writes on, a frame over %ld frames: left out of the pass, their depth is not the guides' %.2f (rigid %.2f,"
                                " skinned %.2f: a vehicle's panes are among the skinned ones); %s, their depth is the guides' %.2f (rigid %.2f, skinned %.2f: hair) (mv_glass=%d)",
                                velStatFrames, (o3[0] + o3[1]) * f, o3[0] * f, o3[1] * f, ivtCfg.mv_glass == 4 ? "LEFT OUT TOO" : "redrawn", (o3[2] + o3[3]) * f, o3[2] * f, o3[3] * f, ivtCfg.mv_glass); })
#if DLSSIV_DEV
        { LONG w[6]; LONG all = 0; for (int k = 0; k < 6; k++) { w[k] = InterlockedExchange(&velStatWheel[k], 0); all += w[k]; }
          if (all)
              LogHealth("  velocity: tyres that moved, a frame over %ld frames: %.2f (mv_wheel=%d). Their spin left out: the axle was the mesh's x on %.2f,"
                        " y on %.2f, z on %.2f; no turn to see on %.2f; refused (turned too far to be a wheel on its axle) %.2f; spin kept %.2f",
                        velStatFrames, all * f, ivtCfg.mv_wheel, w[0] * f, w[1] * f, w[2] * f, w[4] * f, w[3] * f, w[5] * f); }
        VelDmgReport(velStatFrames, f);   // the dented vehicles' line
#endif
        DEVSTAT({ const LONG rp = m4dRepeatServed; m4dRepeatServed = 0;
                  LogHealth("  velocity: skinned movers drawn again within their frame (a ped's hair is six draws of one mesh), given their first draw's previous transform: %.2f a frame over %ld frames",
                            rp * f, velStatFrames); })
        velStatFrames = 0; DEVSTAT(velStatDrains = 0; velStatEntries = 0; velStatSkinDeferred = 0;)
        DEVSTAT(velStatSkinImmediate = 0; velStatPalettes = 0;) DEVSTAT(velBatchPeak = 0; velStatRowsPeak = 0; velStatTrackerLost = 0;)
        DEVSTAT(velBatchOverflow = 0;)     // a session total, like its three neighbours; fenced, so a release build is unchanged
    }
        // No layout transition and no FlushRenderingCommands: velTex is D3D9's alone, so DXVK handles the transition.
}


// The frame boundary. GTA IV never calls Present, so EndScene is the only per-frame call the mod can rely on.
typedef HRESULT(__stdcall* EndScene_t)(IDirect3DDevice9*);
static EndScene_t origEndScene = nullptr;
#if DLSSIV_DEVLOG
static bool toldFrameHook = false;
#endif


// ---------------------------------------------------------------------------
// Stage 1: can a vertex shader on this stack read a texture?
// ---------------------------------------------------------------------------
// Previous-frame bones come as a vertex texture (the constant file is full). The probe draws a triangle and reads back two
// texels.

#include "shaders/vtf_test_vs.h"
#include "shaders/vtf_test_ps.h"


static const int kBoneTexW = 144;    // 48 bones x 3 registers
static const int kBoneTexH = 4;      // probe only; the real store is taller

// The pattern, as a function so the expected value is derived the same way as the uploaded one.
static void BoneProbePattern(int x, int y, float* rgba)
{
    rgba[0] = (float)(x + y * 16) + 0.5f;
    rgba[1] = 1.0f;
    rgba[2] = 2.0f;
    rgba[3] = -(float)(x * 4 + y) - 0.25f;
}

static void VtfProbe(IDirect3DDevice9* dev)
{
    if (vtfTried)
        return;
    vtfTried = true;

    LogDetail("");
    LogDetail("=== STAGE 1: VERTEX TEXTURE FETCH, END TO END ===");

    // --- what the driver claims (logged, not trusted) -----------------------
    IDirect3D9* d3d = nullptr;
    D3DDEVICE_CREATION_PARAMETERS cp;
    D3DDISPLAYMODE dm;
    memset(&cp, 0, sizeof(cp));
    memset(&dm, 0, sizeof(dm));
    if (SUCCEEDED(dev->GetDirect3D(&d3d)) && d3d &&
        SUCCEEDED(dev->GetCreationParameters(&cp)) &&
        SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &dm)))
    {
        HRESULT hv = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType,
                                            dm.Format, D3DUSAGE_QUERY_VERTEXTEXTURE,
                                            D3DRTYPE_TEXTURE, D3DFMT_A32B32G32R32F);
        LogDetail("  driver claims A32B32G32R32F vertex-texture support: %s (0x%08X)",
            SUCCEEDED(hv) ? "yes" : "NO", hv);
        LogDetail("  (claim recorded only. The round trip below is what decides.)");
    }
    else
        LogDetail("  could not query the driver. Proceeding to the round trip anyway.");
    if (d3d) d3d->Release();

    IDirect3DTexture9*           tex     = nullptr;
    IDirect3DTexture9*           rtTex   = nullptr;
    IDirect3DSurface9*           rtSurf  = nullptr;
    IDirect3DSurface9*           sysSurf = nullptr;
    IDirect3DVertexShader9*      vs      = nullptr;
    IDirect3DPixelShader9*       ps      = nullptr;
    IDirect3DVertexDeclaration9* decl    = nullptr;

    // Saved device state. This runs inside the game's frame and every one of
    // these must go back exactly as it was found.
    IDirect3DSurface9*           oldRT[4] = { nullptr, nullptr, nullptr, nullptr };
    IDirect3DSurface9*           oldDS   = nullptr;
    LONG                         oldDsSeq = 0;         // ds_bracket.h
    IDirect3DVertexShader9*      oldVS   = nullptr;
    IDirect3DPixelShader9*       oldPS   = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DBaseTexture9*       oldVTex = nullptr;
    IDirect3DVertexBuffer9*      oldVB   = nullptr;
    UINT                         oldVBOff = 0, oldVBStride = 0;
    IDirect3DIndexBuffer9*       oldIB   = nullptr;
    D3DVIEWPORT9                 oldVp;
    DWORD oldZ = 0, oldZW = 0, oldAB = 0, oldAT = 0, oldCull = 0,
          oldCW = 0, oldScissor = 0, oldStencil = 0, oldFog = 0;
    bool  stateSaved = false;

    const char* fail = nullptr;

    do
    {
                // --- the texture, in the format bones need ---
        // DYNAMIC/DEFAULT first (rewritten every frame), MANAGED second; which worked is logged.
        DWORD  texUsage = D3DUSAGE_DYNAMIC;
        D3DPOOL texPool = D3DPOOL_DEFAULT;
        DWORD  lockFlag = D3DLOCK_DISCARD;
                // The mod's own creations: the 1x1 target would otherwise enter fpTab and dangle.
        RsOwnCreateCall guard;
        HRESULT hr = dev->CreateTexture(kBoneTexW, kBoneTexH, 1, texUsage,
                                        D3DFMT_A32B32G32R32F, texPool,
                                        &tex, nullptr);
        if (FAILED(hr) || !tex)
        {
            LogDetail("  DYNAMIC/DEFAULT A32B32G32R32F rejected (0x%08X), trying MANAGED", hr);
            texUsage = 0; texPool = D3DPOOL_MANAGED; lockFlag = 0;
            hr = dev->CreateTexture(kBoneTexW, kBoneTexH, 1, texUsage,
                                    D3DFMT_A32B32G32R32F, texPool, &tex, nullptr);
        }
        if (FAILED(hr) || !tex)
        {
            LogDetail("  CreateTexture(%dx%d A32B32G32R32F) failed both ways, 0x%08X",
                kBoneTexW, kBoneTexH, hr);
            fail = "the texture could not be created at all";
            break;
        }
        LogDetail("  bone texture created %s", texPool == D3DPOOL_DEFAULT
            ? "DYNAMIC/DEFAULT, which is what stage 3 wants"
            : "MANAGED (stage 3 will need a different upload path)");

        D3DLOCKED_RECT lr;
        if (FAILED(tex->LockRect(0, &lr, nullptr, lockFlag)))
        { fail = "LockRect on the bone texture failed"; break; }
        for (int y = 0; y < kBoneTexH; y++)
        {
            float* row = (float*)((BYTE*)lr.pBits + (size_t)y * lr.Pitch);
            for (int x = 0; x < kBoneTexW; x++)
                BoneProbePattern(x, y, row + x * 4);
        }
        tex->UnlockRect(0);

                // --- somewhere to put the answer ---
        // A32B32G32R32F so the comparison is exact; the probe values are exact in half too, so a fallback still proves
        // transport.
        D3DFORMAT rtFmt = D3DFMT_A32B32G32R32F;
        hr = dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, rtFmt,
                                D3DPOOL_DEFAULT, &rtTex, nullptr);
        if (FAILED(hr))
        {
            rtFmt = D3DFMT_A16B16G16R16F;
            hr = dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, rtFmt,
                                    D3DPOOL_DEFAULT, &rtTex, nullptr);
            LogDetail("  A32B32G32R32F render target unavailable, using A16B16G16R16F");
            LogDetail("  (the probe values are exact in half, so this still decides)");
        }
        if (FAILED(hr) || !rtTex)
        { fail = "no float render target for the readback"; break; }
        rtTex->GetSurfaceLevel(0, &rtSurf);
        if (FAILED(dev->CreateOffscreenPlainSurface(1, 1, rtFmt, D3DPOOL_SYSTEMMEM,
                                                    &sysSurf, nullptr)) || !sysSurf)
        { fail = "no system-memory surface to read back into"; break; }

                // --- shaders and declaration ---
        if (FAILED(dev->CreateVertexShader((const DWORD*)kVtfTestVS, &vs)) || !vs)
        { fail = "the vs_3_0 probe shader was rejected"; break; }
        if (FAILED(dev->CreatePixelShader((const DWORD*)kVtfTestPS, &ps)) || !ps)
        { fail = "the ps_3_0 probe shader was rejected"; break; }

        D3DVERTEXELEMENT9 el[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            D3DDECL_END()
        };
        if (FAILED(dev->CreateVertexDeclaration(el, &decl)) || !decl)
        { fail = "the probe vertex declaration was rejected"; break; }

                // --- save every piece of state the probe touches ---
        for (int i = 0; i < 4; i++) dev->GetRenderTarget(i, &oldRT[i]);
        if (!oldRT[0]) { fail = "no render target bound, cannot restore safely"; break; }
        oldDsSeq = DsBracketOpen();          // ds_bracket.h
        dev->GetDepthStencilSurface(&oldDS);
        dev->GetVertexShader(&oldVS);
        dev->GetPixelShader(&oldPS);
        dev->GetVertexDeclaration(&oldDecl);
        dev->GetTexture(D3DVERTEXTEXTURESAMPLER0, &oldVTex);
        dev->GetStreamSource(0, &oldVB, &oldVBOff, &oldVBStride);
        dev->GetIndices(&oldIB);
        dev->GetViewport(&oldVp);
        dev->GetRenderState(D3DRS_ZENABLE,           &oldZ);
        dev->GetRenderState(D3DRS_ZWRITEENABLE,      &oldZW);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE,  &oldAB);
        dev->GetRenderState(D3DRS_ALPHATESTENABLE,   &oldAT);
        dev->GetRenderState(D3DRS_CULLMODE,          &oldCull);
        dev->GetRenderState(D3DRS_COLORWRITEENABLE,  &oldCW);
        dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor);
        dev->GetRenderState(D3DRS_STENCILENABLE,     &oldStencil);
        dev->GetRenderState(D3DRS_FOGENABLE,         &oldFog);
        stateSaved = true;

                // --- set up and draw ---
        for (int i = 1; i < 4; i++) if (oldRT[i]) dev->SetRenderTarget(i, nullptr);
        dev->SetRenderTarget(0, rtSurf);
        dev->SetDepthStencilSurface(nullptr);

        D3DVIEWPORT9 vp = { 0, 0, 1, 1, 0.0f, 1.0f };
        dev->SetViewport(&vp);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 1.0f, 0);

        dev->SetRenderState(D3DRS_ZENABLE,           FALSE);
        dev->SetRenderState(D3DRS_ZWRITEENABLE,      FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE,   FALSE);
        dev->SetRenderState(D3DRS_CULLMODE,          D3DCULL_NONE);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE,  0x0F);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_STENCILENABLE,     FALSE);
        dev->SetRenderState(D3DRS_FOGENABLE,         FALSE);

        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);
        dev->SetVertexDeclaration(decl);

                // Point filtering, no mips: vertex texture fetch supports nothing else on most hardware.
        dev->SetTexture(D3DVERTEXTEXTURESAMPLER0, tex);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);
        dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);

        const int ax = 7, ay = 2, bx = 0, by = 0;
        float coords[4] = {
            ((float)ax + 0.5f) / (float)kBoneTexW, ((float)ay + 0.5f) / (float)kBoneTexH,
            ((float)bx + 0.5f) / (float)kBoneTexW, ((float)by + 0.5f) / (float)kBoneTexH
        };
        dev->SetVertexShaderConstantF(245, coords, 1);

        // One oversized triangle. Every vertex carries the same fetched value,
        // so interpolation cannot alter what lands in the single pixel.
        const float verts[3][4] = {
            { -1.0f, -1.0f, 0.0f, 1.0f },
            {  3.0f, -1.0f, 0.0f, 1.0f },
            { -1.0f,  3.0f, 0.0f, 1.0f },
        };
        hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, verts, sizeof(float) * 4);
        if (FAILED(hr)) { fail = "the probe draw itself failed"; break; }

                // --- read it back ---
        if (FAILED(dev->GetRenderTargetData(rtSurf, sysSurf)))
        { fail = "GetRenderTargetData failed, the result could not be read"; break; }

        D3DLOCKED_RECT rr;
        if (FAILED(sysSurf->LockRect(&rr, nullptr, D3DLOCK_READONLY)))
        { fail = "the readback surface could not be locked"; break; }

        float got[4];
        if (rtFmt == D3DFMT_A32B32G32R32F)
            memcpy(got, rr.pBits, sizeof(got));
        else
        {
            const unsigned short* hh = (const unsigned short*)rr.pBits;
            for (int i = 0; i < 4; i++) got[i] = HalfToFloat(hh[i]);
        }
        sysSurf->UnlockRect();

        float pa[4], pb[4];
        BoneProbePattern(ax, ay, pa);
        BoneProbePattern(bx, by, pb);
        const float want[4] = { pa[0], pa[3], pb[0], pb[3] };

        float worst = 0.0f;
        for (int i = 0; i < 4; i++)
        {
            float e = fabsf(got[i] - want[i]);
            if (e > worst) worst = e;
        }

        LogDetail("  texel (%d,%d) and (%d,%d) fetched inside the vertex shader:",
            ax, ay, bx, by);
        LogDetail("    wanted  %.4f  %.4f  %.4f  %.4f", want[0], want[1], want[2], want[3]);
        LogDetail("    got     %.4f  %.4f  %.4f  %.4f", got[0],  got[1],  got[2],  got[3]);
        LogDetail("    worst   %.9g", worst);

        vtfProven = (worst < 1e-4f);
        if (vtfProven)
        {
            LogDetail("  ROUND TRIP PASSED. A vertex shader on this stack can read a");
            LogDetail("  float texture at an address the CPU chose, so previous-frame");
            LogDetail("  bone matrices have a way in. Stage 3 is unblocked.");
        }
        else if (fabsf(got[0] - got[2]) < 1e-6f && fabsf(got[1] - got[3]) < 1e-6f)
            LogRecord("  vertex-texture self-test FAILED: both texels identical");
        else
            LogRecord("  vertex-texture self-test FAILED: the values do not match (worst %.3g)", worst);
    } while (0);

    if (fail)
        LogRecord("  ROUND TRIP FAILED: %s.", fail);

        // --- restore, in reverse ---
    if (stateSaved)
    {
        dev->SetRenderState(D3DRS_FOGENABLE,         oldFog);
        dev->SetRenderState(D3DRS_STENCILENABLE,     oldStencil);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE,  oldCW);
        dev->SetRenderState(D3DRS_CULLMODE,          oldCull);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE,   oldAT);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE,  oldAB);
        dev->SetRenderState(D3DRS_ZWRITEENABLE,      oldZW);
        dev->SetRenderState(D3DRS_ZENABLE,           oldZ);
        dev->SetTexture(D3DVERTEXTEXTURESAMPLER0, oldVTex);
        dev->SetIndices(oldIB);
        dev->SetStreamSource(0, oldVB, oldVBOff, oldVBStride);
        dev->SetVertexDeclaration(oldDecl);
        dev->SetPixelShader(oldPS);
        dev->SetVertexShader(oldVS);
        for (int i = 0; i < 4; i++) dev->SetRenderTarget(i, oldRT[i]);
        dev->SetDepthStencilSurface(DsBracketPutBack(oldDS, oldDsSeq));
        dev->SetViewport(&oldVp);

        if (oldVTex) oldVTex->Release();
        if (oldIB)   oldIB->Release();
        if (oldVB)   oldVB->Release();
        if (oldDecl) oldDecl->Release();
        if (oldPS)   oldPS->Release();
        if (oldVS)   oldVS->Release();
        if (oldDS)   oldDS->Release();
        for (int i = 0; i < 4; i++) if (oldRT[i]) oldRT[i]->Release();
    }

    if (decl)    decl->Release();
    if (ps)      ps->Release();
    if (vs)      vs->Release();
    if (sysSurf) sysSurf->Release();
    if (rtSurf)  rtSurf->Release();
    if (rtTex)   rtTex->Release();
    if (tex)     tex->Release();
}


static void HdrArmSwapchainPresent(IDirect3DDevice9* dev);   // defined with the swapchain Present hook, below

static HRESULT __stdcall hkEndScene(IDirect3DDevice9* dev)
{
    DT_NOT_ON_WORKER(origEndScene(dev));
    if (!renderTid) renderTid = GetCurrentThreadId();   // see renderTid: the hooks are this thread's
        // The mod's device slots, checked once a frame: Windows' layer can write its hooks back unseen (DtRetakeSlots,
    // dt_install.h).
    DtRetakeSlots("since the last frames", true);
        // The sharpen backstop's question is asked here: the frame-end chain below clears seamThisFrame and shpDone for the next
    // frame.
    const bool shpOwed = !dead && shpOn && !SHP_G10() && seamThisFrame && !shpDone;
    DEVSTAT(if (telemetryOn && !dead && shpOn && !SHP_G10() && seamThisFrame) shpSeamFrames++;)   // a G10 frame owes the pass nothing
    shpG10Seam = !dead && seamThisFrame;
    // Whoever presents is the render thread, by definition.
    if (!dead)
    {
        // Which surface is the backbuffer this frame (refreshed each frame so a
        // device reset cannot leave a stale pointer behind).
        {
            IDirect3DSurface9* bb = nullptr;
            if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
            {
                if (bb != backbufferSurf)   // a new swapchain: record what its buffer really is (hdr.h, hdrBbFp16)
                {
                    D3DSURFACE_DESC fd = {};
                    hdrBbFp16 = SUCCEEDED(bb->GetDesc(&fd)) && fd.Format == D3DFMT_A16B16G16R16F;
                }
                backbufferSurf = bb;
                if (!cenScreenW)
                {
                    D3DSURFACE_DESC bd = {};
                    if (SUCCEEDED(bb->GetDesc(&bd)))
                    {
                        cenScreenW = bd.Width; cenScreenH = bd.Height; RsOnScreenSize(cenScreenW, cenScreenH);
                        // Learned here when the game's CreateDevice went around the hook: targets made before this frame are
                        // unscaled, so the render scale starts after a Reset.
                        LogRecord("  screen size learned LATE, from the back buffer at frame %ld: %ux%u (the game's CreateDevice went around the hook):"
                  " the render scale starts after a Reset (change the resolution or toggle fullscreen)",
                            (long)frames, cenScreenW, cenScreenH);
                    }
                }
                bb->Release();
            }
        }
                // After the block above: its first pass sets the screen size, which LodBiasForScale derives the bias from.
        LodApplyAll(dev);
                // HDR: the swapchain Present hook sets the scRGB colour space; it needs a live device, so it is armed here.
        if (ivtCfg.hdr || latOn) HdrArmSwapchainPresent(dev);    // once, and again after each Reset (latOn: the latency stamp at threaded=0 rides this hook)

        UpdateJitterAuto();
        IvtCfgLiveTick();     // about once a second: re-read DLSS-IV.cfg if it changed (cfg_live)
        if (toldVerdict)
        {
            // Only when something consumes them: the velocity target is 63 MB at 4K FP16 in a 32-bit address space, and its one
            // reader is the transport's guide pass.
            if (ivtCfg.transport && !ivtFailed)
            {
                // Inside this gate: every reader of vtfProven is in this block or below it.
                VtfProbe(dev);      // stage 1, once, before anything depends on it
                VelCreate(dev);
                BoneTexCreate(dev);
            }
            { IvtTick(dev); }   // the bespoke transport (off unless the cfg says so)
        }
        LONG f = InterlockedIncrement((LONG*)&frames);
                // Fixed frames, so runs line up. The address space walk runs on a worker (MemCensus, MemCensusWalk).
#if DLSSIV_DEVLOG               // the memory census and its timing are development-log only
        if (f == 601 || f == 1801 || (censusOn && (f % 3600) == 1))
        {
            char lbl[32];
            _snprintf_s(lbl, sizeof(lbl), _TRUNCATE, "frame %ld", (long)f);
            LARGE_INTEGER c0 = {}, c1 = {};
            if (telemetryOn) QueryPerformanceCounter(&c0);
            MemCensus(dev, lbl);
            if (telemetryOn)
            {
                QueryPerformanceCounter(&c1);
                if (!qpcFreq.QuadPart) QueryPerformanceFrequency(&qpcFreq);
                const double us = qpcFreq.QuadPart
                    ? (double)(c1.QuadPart - c0.QuadPart) * 1e6 / (double)qpcFreq.QuadPart : 0.0;
                // This times the handover (the device call plus a CreateThread), tens of microseconds; the walk times itself on
                // the worker.
                LogHealth("  memory census at frame %ld: %.0f us on the render thread (the device call and"
                    " the handover; the walk itself runs on a worker and reports separately)%s", (long)f, us,
                    us > 2000.0 ? "   <-- STILL A HITCH: the walk is back on this thread" : "");
            }
        }
#endif
#if DLSSIV_DEVLOG
        if (!toldFrameHook)
        {
            toldFrameHook = true;
            LogHealth("  hook: EndScene fired -- the frame boundary is live");
        }
#endif
                // The published camera is the one captured at its depth-encode upload (NoteSceneC227), set before Publish reads camCur
        // against camPrev.
        if (sceneCandHave)
        {
            // [equiv_c227: elect]
            // The election: the candidate the most draws were made with wins over the one the upload rules picked live. Ties,
            // and a frame with no draws counted, keep the live one.
            int best = sceneCandLive;
            for (int i = 0; i < sceneCandN; i++)
                if (sceneCands[i].draws > sceneCands[best].draws) best = i;
            if (best != sceneCandLive)
            {
                // The increment lives in this condition and the equivalence proof depends on it (statSceneReelected): the
                // statement stays whole in every build.
                if (telemetryOn && ++statSceneReelected <= 12)
                    LogHealth("  scene camera: the live anchor (near %.3f far %.0f, %ld draws) was not the camera"
                        " the frame was drawn with; elected near %.3f far %.0f at %.1f %.1f %.1f with %ld draws"
                        " (%d candidates this frame). Next frame ranks continuity from the elected one.",
                        sceneCands[sceneCandLive].s.nearC, sceneCands[sceneCandLive].s.farC, sceneCands[sceneCandLive].draws,
                        sceneCands[best].s.nearC, sceneCands[best].s.farC,
                        sceneCands[best].vinv[12], sceneCands[best].vinv[13], sceneCands[best].vinv[14],
                        sceneCands[best].draws, sceneCandN);
                sceneCandLive = best;
                sceneCand = sceneCands[best].s;
                memcpy(sceneCandViewInv, sceneCands[best].vinv, 64);
            }
            // [/equiv_c227: elect]
            camCur           = sceneCand;
            CamMatSet(sceneViewInv, sceneCandViewInv);
            haveSceneViewInv = true;
            DEVSTAT(if (telemetryOn) { statSceneCandFrames++; })
        }
        else
        {
            camCur.valid = false;
        }
        if (telemetryOn && toldVerdict && ((f % 600) == 0 || f == 120))
            LogHealth("  scene c227 capture: uploads %ld, rejected %ld, shallow (lights) %ld, qualified"
                " frames %ld, re-elected %ld, evicted %ld; this frame %s near %.4f far %.1f res %dx%d, %d candidates, %ld draws",
                statC227Seen, statC227NoMatch, statC227Shallow, statSceneCandFrames, statSceneReelected, statSceneCandEvict,
                sceneCandHave ? "OK" : "NONE",
                sceneCandHave ? sceneCand.nearC : 0.0f,
                sceneCandHave ? sceneCand.farC  : 0.0f,
                sceneCandHave ? sceneCand.w : 0,
                sceneCandHave ? sceneCand.h : 0,
                sceneCandN, sceneCandLive >= 0 ? sceneCands[sceneCandLive].draws : 0L);

        // PUBLISH BEFORE ROLLING: Publish() reads camCur against camPrev.
        if (toldVerdict)
            { Publish(); }

                // Decides next frame's scene viewport from this frame's draw counts. While the worker owns the tracker, its EndScene
        // observer does the rest (dt_trk.h, TrkObsEndScene); the render thread stands aside, so reading its counters here is
        // safe.
        { if (!trkWorkerLive) M4dEndFrame(); }
        VelEndFrame();
        { if (!trkWorkerLive) BoneTexUpload(); }   // this frame's pose becomes next frame's previous pose
        {
            PickSceneKey();      // the signature's hysteresis: sceneKeyStable gates VelCreate
        }

        patchedThisFrame = false;   // one offset per frame, next frame re-arms

                // While jitter is on, say so periodically: this is its only readout.
        if (telemetryOn && jitterEnabled && (f % 600) == 0)
        {
            LogHealth("  jitter live @%ld: patched %ld frames, px %.4f %.4f,"
                " %d phases; c8 jittered %ld, other cameras %ld,"
                " before camera known %ld",
                (long)f, patchFrames, jitterPxX, jitterPxY, jitterPhases,
                statC8Jittered, statC8NotScene, statC8NoCamera);
        }

                // The mip LOD bias: only the faults (a scaled scene with no bias, a draw-time mismatch), each said once.
        if (rsActive && !lodActive && ivtCfg.lod_bias != 0.0f)
        {
            static bool saidOff = false;
            if (!saidOff)
            {
                saidOff = true;
                LogRecord("  lod bias: THE SCENE IS SCALED AND NO BIAS IS APPLIED (auto %+.4f, cfg %.2f, SetSamplerState hook %s)",
                    lodBiasAuto, (double)ivtCfg.lod_bias,
                    origSetSamplerState ? "live" : "NOT INSTALLED");
            }
        }
        if (haveSceneViewInv)
        {
            CamMatSet(sceneViewInvPrev, sceneViewInv);
            haveSceneViewInvPrev = true;
        }

                // A frame that captured nothing publishes valid = 0 and heals at the next c227 upload. No force-accept: it would adopt
        // the origin phantom as the scene camera.

        camPrev       = camCur;
                // One statement after the only write to camPrev, so the product and its inputs are never a frame apart. Publish() above
        // has already read the old camPrev and never reads this product.
        {
            Mat4d camPrevViewD, camPrevProjD;
            ToD(camPrev.view, &camPrevViewD);
            ToD(camPrev.proj, &camPrevProjD);
            MulMatD(camPrevViewD, camPrevProjD, &camPrevVPd);
        }
        TrkSendFrame();             // the frame note, before EndScene is recorded
        camCur.valid  = false;      // a stale capture must never be republished
        sceneCandHave = false;      // re-captured next frame at its c227 upload
        sceneCandN = 0; sceneCandLive = -1; camCandFor = 0xFFFFFFFFu;
        seamThisFrame = false; DEVSTAT(seamBbSeen = 0;) DEVSTAT(seamDrawsSeen = 0;) seamSceneWrote = false;
        c8DevJit = false;        // the rows on the device carry the frame before's jitter until the scene uploads them again
        seamArmed = false;
                // A frame that never injected still recorded movers: dropped, not drained, since nothing will read those vectors.
        // hkReset drops the batch itself on a Reset.
        if (!trkWorkerLive) VelBatchDrop();   // the device thread drops its own at its EndScene
        shpDone = false; shpBbSeen = false;   // one sharpen a frame
        chvFilled = false; chvFilledFar = -1.0f;              // the quad refills it every frame
        chvOverlayPhase = false;                              // the phase re-opens on its own evidence
        chvVpClearedN = 0;                                    // and each overlay rect is cleared once a frame

    }
        // Last, after the HUD: the whole frame is encoded. The sharpen backstop covers a frame NoteDrawRec never fired on (HUD
    // off); shpOwed was taken at the top, before the chain cleared its flags. Before HdrEncode, which copies the frame out.
    if (shpOwed && dev)
    {
                // The absorb (hdr_cga.h): a withheld curve is drawn first, and cgaRan = false keeps this rewritten frame out of the
        // comparison.
        if (hdrCgaHeld) HdrCgaHeal(dev, CGA_HEAL_STRETCH);
        cgaRan = false;
        shpDone = true;             // the mod's own quad passes the gate in NoteDrawRec; this is the second lock
        ShpSharpen(dev);
        shpDone = false;            // and the next frame starts as the chain above left it
        DEVSTAT(if (telemetryOn) { shpBackstops++; })
    }
    { NoticeFrame(dev); HdrEncode(dev); }   // the notice first (source/notice.h): the encode copies the finished frame out
    FGA(FgHudSdrFrame(dev);)                // the armed product in SDR (fg/fg_hud.h): a HUD-less picture, before the count
    // G10's latch (shpG10, above): for the next frame's sharpen sites and its encode alike. Only where the encode will sharpen:
    // HDR engaged, sharpening on, no debug view (the views draw their own shader), and neither of its encodes refused.
    shpG10 = shpOn && ivtCfg.hdr && hdrEngaged && hdrColorSpaceDone && !IVT_HDR_VIEW && !hdrG10Refused;
    hdrHooks = ivtCfg.hdr && hdrEngaged;   // a stand-down anywhere this frame reaches the draw hooks by the next (hdr.h)
    hdrFramesSeen++;              // the colour space waits until something has presented
        // HDR's own per-frame census, reset here because IvtTick returns early when transport=0. After HdrEncode, which reads
    // hdrPreHudDraws, and covering its early returns.
    hdrSceneDraws = 0; hdrBbDraws = 0; hdrPreHudDraws = 0; hdrOffscreenProbes = 0;
    HdrCgaFrameEnd(dev);   // the absorb's frame boundary (hdr_cga.h): an unabsorbed curve is drawn, the next frame is decided
        // hdrSceneOk is this frame's capture, so it ends with the frame: both capture sites test !hdrSceneOk, so without this the
    // first capture of a session would be the last.
    hdrSceneOk = false;
    HdrColorSpaceHeartbeat();     // says so, loudly, if it never happens
    return origEndScene(dev);
}

// motionBlurMatrix is a stock rage_postfx parameter at pixel shader c72 (the camera reprojection). The mod never writes it.
typedef HRESULT(__stdcall* SetPsConstF_t)(IDirect3DDevice9*, UINT, const float*, UINT);
DLSSIV_HOT static SetPsConstF_t origSetPsConstF = nullptr;

static HRESULT __stdcall hkSetPsConstF(IDirect3DDevice9* dev, UINT start,
                                       const float* data, UINT count)
{
    DT_NOT_ON_WORKER(origSetPsConstF(dev, start, data, count));
        // A pass writing to a scaled target must be told the scaled size (curRT0Scaled: the counterpart and the G-buffer, not the
    // post chain). The same rewrite as in hkSetVsConstF.
    static float rsFixedPs[16 * 4];
    if (rsActive && data && count && count <= 16 && !rsOwn && !ivtInOwnDraw
        && rsScreenTexKind == 0 && curRT0Scaled)
    {
                // Rows the mask does not cover are scanned one upload in sixteen by RsHealMaybe (RsMakeMask says why). Never runs at
        // quality 0.
        const unsigned listed = RsHealMaybe(start, count, data, RsScanRows(rsScanPS, start, count));
                // The cold shape skip: a shape that has never matched in kRsColdAfter scans is scanned one time in sixteen; one match
        // makes it hot for the session.
        bool hit = false, scan = false;
        if (listed)
        {
            RsShapeEnt& sh = rsShape[RsShapeIdx(start, count)];
            scan = true;
            if (!sh.hits && sh.scans >= kRsColdAfter)
            {
                rsColdRng = rsColdRng * 1664525u + 1013904223u;
                scan = (rsColdRng >> 28) == 0;          // one in sixteen
            }
            sh.scans++;
            if (scan)
            {
                hit = RsFixNeededRows(data, listed);
                if (hit) sh.hits++;                     // hot from here on
            }
        }
        if (hit)
        {
            memcpy(rsFixedPs, data, (size_t)count * 16);
            RsFixConstBlockRows(rsFixedPs, data, listed);
            data = rsFixedPs; DEVSTAT(if (telemetryOn) { InterlockedIncrement(&rsConstFixedPs); })
        }
    }
    return origSetPsConstF(dev, start, data, count);
}

// The three hooks M4b needs: bindings are tracked rather than queried per draw (a Get AddRefs). Their typedefs and originals
// are declared above the velocity batch.
#if DLSSIV_DEVLOG
DLSSIV_HOT static bool toldDrawHook = false;
#endif

static HRESULT __stdcall hkSetStreamSource(IDirect3DDevice9* dev, UINT stream,
                                           IDirect3DVertexBuffer9* vb,
                                           UINT offset, UINT stride)
{
    DT_NOT_ON_WORKER(origSetStreamSource(dev, stream, vb, offset, stride));
    DEVSTAT(if (stream == 0) trkApplyDirty &= ~2u;)   // the tracker's premise (dt_res.h)
    if (stream == 0)
    {
        m4bCurVB = (UINT_PTR)vb;
        velCurVB = vb; velCurVBOff = offset; velCurVBStr = stride;
    }
    else if (stream <= VEL_XSTREAMS)
    {
                // Streams 1..3 (VelPend::xMask). The game binds one for a handful of declarations only.
        velCurXVB[stream - 1] = vb; velCurXOff[stream - 1] = offset; velCurXStr[stream - 1] = stride;
    }
    return origSetStreamSource(dev, stream, vb, offset, stride);
}

static HRESULT __stdcall hkSetIndices(IDirect3DDevice9* dev,
                                      IDirect3DIndexBuffer9* ib)
{
    DT_NOT_ON_WORKER(origSetIndices(dev, ib));
    DEVSTAT(trkApplyDirty &= ~16u;)   // the tracker's premise (dt_res.h)
    m4bCurIB = (UINT_PTR)ib;
    velCurIB = ib;
    return origSetIndices(dev, ib);
}

// Tracked here rather than queried per draw: GetVertexDeclaration AddRefs. Declarations are few and long-lived, so a small
// cache answers after the first sighting.
static HRESULT __stdcall hkSetVertexDecl(IDirect3DDevice9* dev,
                                         IDirect3DVertexDeclaration9* decl)
{
    DT_NOT_ON_WORKER(origSetVertexDecl(dev, decl));
    DEVSTAT(trkApplyDirty &= ~4u;)   // the tracker's premise (dt_res.h)
    if (!dead)
    {
        velCurSkinned = false;
        velCurPosT    = false;
        velCurXMask   = 0;
        velCurQuadUv  = kNoQuadUv;
        velCurDecl = decl;
        if (decl)
        {
            int found = -1;
            for (int i = 0; i < velDeclCount; i++)
                if (velDeclPtr[i] == decl) { found = i; break; }

            if (found >= 0)
            {
                velCurSkinned = velDeclSkin[found];
                velCurPosT    = velDeclPosT[found];
                velCurXMask   = velDeclXMask[found];
                velCurQuadUv  = velDeclQuadUv[found];
            }
            else
            {
                bool skin = false, hasIdx = false, hasWt = false, posT = false, quadPos = false;
                unsigned char xmask = 0, quadUv = kNoQuadUv;
                // MAXD3DDECLLENGTH real elements plus the D3DDECL_END sentinel: GetDeclaration writes with no bound of its own
                // (as dt_trk.h, TrkObsDecl).
                D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 1];
                UINT n = 0;
                if (SUCCEEDED(decl->GetDeclaration(el, &n)))
                {
                    for (UINT e = 0; e + 1 < n; e++)
                    {
                        if (el[e].Usage == D3DDECLUSAGE_BLENDINDICES) hasIdx = true;
                        if (el[e].Usage == D3DDECLUSAGE_BLENDWEIGHT)  hasWt  = true;
                        if (el[e].Usage == D3DDECLUSAGE_POSITIONT)    posT   = true;
                        if (el[e].Stream != 0)                      xmask |= VelXMaskBit(el[e].Stream);
                        if (el[e].Stream == 0 && el[e].UsageIndex == 0)
                        {
                            if (el[e].Usage == D3DDECLUSAGE_POSITION && el[e].Offset == 0 && el[e].Type == D3DDECLTYPE_FLOAT3) quadPos = true;
                            if (el[e].Usage == D3DDECLUSAGE_TEXCOORD && el[e].Type == D3DDECLTYPE_FLOAT2 && el[e].Offset >= 12 && el[e].Offset < kNoQuadUv)
                                quadUv = (unsigned char)el[e].Offset;
                        }
                    }
                    // Skinned means blend weights and blend indices both (wheels are not indices-only declarations).
                    skin = hasIdx && hasWt;
                }
                if (!quadPos) quadUv = kNoQuadUv;
                velCurSkinned = skin;
                velCurPosT    = posT;
                velCurXMask   = xmask;
                velCurQuadUv  = quadUv;
                if (velDeclCount < VEL_DECL_CACHE)
                {
                    // Hold a reference: the cache is keyed on the pointer, and a freed address can be reused by another
                    // declaration.
                    decl->AddRef();
                    velDeclPtr[velDeclCount]  = decl;
                    velDeclSkin[velDeclCount] = skin;
                    velDeclPosT[velDeclCount] = posT;
                    velDeclXMask[velDeclCount] = xmask;
                    velDeclQuadUv[velDeclCount] = quadUv;
                    velDeclCount++;
                    if (velDeclCount == VEL_DECL_CACHE)
                        LogHealth("  velocity: declaration cache FULL at %d; later"
                            " declarations are classified per call", VEL_DECL_CACHE);
                }
                DEVSTAT(else velDeclPastCap++;)
            }
        }
    }
    return origSetVertexDecl(dev, decl);
}

// The rain draw goes into the layer (rain_layer; see ivtRainTex), cleared on the frame's first rain draw, with the game's
// viewport and scissor put back (a bind resets both). Everything is restored after.
static IDirect3DSurface9* rainWas = nullptr;      // the target the game had: tracked, not referenced (the game holds it)
static D3DVIEWPORT9       rainVp = {};
static RECT               rainSc = {};
static __declspec(noinline) bool RainBefore(IDirect3DDevice9* dev)
{
    if (dead || rsOwn || ivtInOwnDraw || seamThisFrame || !RenderThreadHere()) return false;
    if (IVT_MODE != 2 || !jitterEnabled || !patchedThisFrame || !ivtDev || !IvtResolveWillRun()) return false;   // only on a frame an upscaler takes, and its picture comes home
    if (!curRT0 || curRT0 != sceneBufSurf) return false;                                                       // only what goes into the scene buffer
        // ...and only when it is the one target bound: the layer's clear clears every bound target, and targets 1 to 3 are the
    // G-buffer's. Where one is bound, the rain goes through the upscaler.
    if (curRT123[0] || curRT123[1] || curRT123[2]) { DEVSTAT(if (telemetryOn) ivtRainMrt++;) return false; }
    D3DSURFACE_DESC d = {};
    if (FAILED(curRT0->GetDesc(&d)) || !IvtRainEnsure(dev, d.Width, d.Height, d.Format)) return false;
    RsOwnCall guard;
    if (FAILED(dev->GetViewport(&rainVp)) || FAILED(dev->GetScissorRect(&rainSc))) return false;
    IDirect3DSurface9* const was = curRT0;        // before the bind: the hook tracks the mod's target as it tracks any
    if (FAILED(dev->SetRenderTarget(0, ivtRainSurf))) return false;
    rainWas = was;
    if (ivtRainFrame != (LONG)frames) { ivtRainFrame = (LONG)frames; dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0); }
    dev->SetViewport(&rainVp);
    dev->SetScissorRect(&rainSc);
    DEVSTAT(if (telemetryOn) ivtRainTaken++;)
    return true;
}
static __declspec(noinline) void RainAfter(IDirect3DDevice9* dev)
{
    RsOwnCall guard;
    dev->SetRenderTarget(0, rainWas);
    dev->SetViewport(&rainVp);
    dev->SetScissorRect(&rainSc);
    rainWas = nullptr;
}

// The corona draw reads the held depth (corona_hold; see ivtCorTex) when the snapshot knows its transform; restored after.
static int CorFind(const IvtCorRow* list, int n, const float u[16])
{
    for (int i = 0; i < n; i++) if (CorSameRows(list[i].u, u)) return i;
    return -1;
}
#if DLSSIV_DEVLOG
// How far the nearest transform of a list is from this one (CorRowsApart), for the period's line.
static float CorNearest(const IvtCorRow* list, int n, const float u[16])
{
    float least = 1.0e9f;
    for (int i = 0; i < n; i++) { const float d = CorRowsApart(list[i].u, u); if (d < least) least = d; }
    return least;
}
#endif
static __declspec(noinline) bool CorHoldBefore(IDirect3DDevice9* dev)
{
    if (dead || rsOwn || ivtInOwnDraw || seamThisFrame || !RenderThreadHere() || !origSetTexture || !origSetVsConstF) return false;
    if (IVT_MODE != 2 || !jitterEnabled || !patchedThisFrame || !ivtDev || !IvtResolveWillRun()) return false;   // only on a frame an upscaler takes
    if (!vtf0Tex || vtf0Tex != (IDirect3DBaseTexture9*)ivtGameDsTex) return false;               // the test reads the scene's own depth
    if (!c8DevJit || !m4bHaveWVP || !c227Have) return false;                                       // and is projected with this frame's jittered rows
    const LONG f = (LONG)frames;
    if (ivtCorSeen != f)
    {   // the first corona draw of the frame: the lists turn over
        if (ivtCorSeen == f - 1) { memcpy(ivtCorWas, ivtCorNow, sizeof(IvtCorRow) * (size_t)ivtCorNowN); ivtCorWasN = ivtCorNowN; }
        else ivtCorWasN = 0;
        ivtCorNowN = 0; ivtCorServed = 0; ivtCorStillN = 0; ivtCorSeen = f;
        if (ivtCorCapFrame != f - 1)
        {   // the frame before gave the body no job: what is held is not this scene's any more
            DEVSTAT(if (telemetryOn && ivtCorSnapFrame >= 0) ivtCorEndNoJob++;)
            ivtCorSnapFrame = -1;
        }
    }
    float u[16];
    CorUnjitter(m4bCurWVP, jitterNdcX, jitterNdcY, u);
    if (CorFind(ivtCorNow, ivtCorNowN, u) < 0 && ivtCorNowN < kCorRows)
    {
        memcpy(ivtCorNow[ivtCorNowN].u, u, sizeof(u)); memcpy(ivtCorNow[ivtCorNowN].dev, m4bCurWVP, sizeof(u));
        memcpy(ivtCorNow[ivtCorNowN].lg, c227Cur, sizeof(c227Cur));
        ivtCorNowN++;
    }
    if (CorFind(ivtCorWas, ivtCorWasN, u) >= 0) ivtCorStillN++;
    DEVSTAT(else if (telemetryOn && ivtCorWasN > 0) { const float d = CorNearest(ivtCorWas, ivtCorWasN, u); if (d < ivtCorMoveMin) ivtCorMoveMin = d; if (d > ivtCorMoveMax) ivtCorMoveMax = d; })
    DEVSTAT(if (telemetryOn) { ivtCorDraws++; if (curRT0 != sceneBufSurf) ivtCorOtherRT++; })
    if (ivtCorSnapFrame < 0 || ivtCorSnapFrame >= f || !ivtCorTex) return false;
    const int s = CorFind(ivtCorSnap, ivtCorSnapN, u);
    if (s < 0)
    {
        DEVSTAT(if (telemetryOn) { ivtCorOdd++; const float d = CorNearest(ivtCorSnap, ivtCorSnapN, u); if (d < ivtCorOddMin) ivtCorOddMin = d; if (d > ivtCorOddMax) ivtCorOddMax = d; })
        return false;
    }
    origSetTexture(dev, D3DVERTEXTEXTURESAMPLER0, ivtCorTex);
    origSetVsConstF(dev, 8, ivtCorSnap[s].dev, 4);
    origSetVsConstF(dev, 227, ivtCorSnap[s].lg, 1);
    ivtCorServed++;
    DEVSTAT(if (telemetryOn) ivtCorGiven++;)
    return true;
}
static __declspec(noinline) void CorHoldAfter(IDirect3DDevice9* dev)
{
    origSetTexture(dev, D3DVERTEXTEXTURESAMPLER0, vtf0Tex);
    origSetVsConstF(dev, 8, m4bCurWVP, 4);        // the rows the game uploaded, with the jitter the upload hook put in
    origSetVsConstF(dev, 227, c227Cur, 1);        // and the depth pair it uploaded
}

static bool ChvBefore(IDirect3DDevice9* dev)
{
        // Lends the output-size depth for this draw; true when the caller must restore. chvDepth is re-tested: a null depth-stencil
    // is legal in D3D9 and would strip the depth test.
    if (!chvDepth || !rsActive || !dev) return false;
        // Not the mod's own draws, and not a stopped mod (the arm can outlive `dead`).
    if (dead || rsOwn || ivtInOwnDraw) return false;
        // Every draw: the render area is the smallest attachment, so a render-size depth cuts an output-size target.
    DWORD ze = 0, zf = D3DCMP_LESSEQUAL;
    dev->GetRenderState(D3DRS_ZENABLE, &ze);
    dev->GetRenderState(D3DRS_ZFUNC, &zf);
    RsOwnCall guard;                            // keeps the mod's own bind out of curDS
    const LONG savedSeq = DsBracketOpen();      // before the read of curDS, which another thread's hook writes first
    IDirect3DSurface9* const saved = curDS;
    if (FAILED(dev->SetDepthStencilSurface(chvDepth))) return false;
    chvSaved = saved;                           // set only once the bind has taken
    chvSavedSeq = savedSeq;
        // The far value is whatever this draw's compare treats as far: under GREATER, 1.0 is the near plane. Re-cleared within a
    // frame only if a later draw disagrees.
    const float farZ = (ze && (zf == D3DCMP_GREATER || zf == D3DCMP_GREATEREQUAL)) ? 0.0f : 1.0f;
    if (!chvFilled || (ze && chvFilledFar != farZ))
    {
        chvFilled = true; chvFilledFar = farZ;
                // The whole target, whatever draw borrows first (see chvFilled): D3D9 clears the viewport cut by the scissor, not the
        // surface, and this clear latches for the frame.
        D3DVIEWPORT9 vpWas = {}; DWORD scWas = FALSE;
        const bool partial = SUCCEEDED(dev->GetViewport(&vpWas))
                          && (vpWas.X || vpWas.Y || vpWas.Width != rsOutW || vpWas.Height != rsOutH);
        dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &scWas);
        const D3DVIEWPORT9 dxvkVpWas = rsDxvkVp; const bool haveDxvkVpWas = rsHaveDxvkVp;
        const D3DVIEWPORT9 whole = { 0, 0, rsOutW, rsOutH, 0.0f, 1.0f };
        if (partial) dev->SetViewport(&whole);
        if (scWas)   dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
                // Stencil as well as depth: GTA IV's deferred pass stencils sky against geometry.
        dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, farZ, 0);
        if (scWas)   dev->SetRenderState(D3DRS_SCISSORTESTENABLE, scWas);
        if (partial) { dev->SetViewport(&vpWas); rsDxvkVp = dxvkVpWas; rsHaveDxvkVp = haveDxvkVpWas; }
    }
        // Overlays (the phone) read the scene depth through a sampler and are drawn into the back buffer; tested against the lent
    // depth they lose, so their attachment is withheld: a draw that writes colour, samples a depth texture the mod shrank, and
    // uses the origin camera (coronas and the chevron need the test). m4bHaveViewInv: before the verdict the matrix is zeros.
    if ((rsIntzN || chvOverlayPhase) && m4bHaveViewInv
        && fabsf(m4bCurViewInv[12]) < 0.5f && fabsf(m4bCurViewInv[13]) < 0.5f
        && fabsf(m4bCurViewInv[14]) < 0.5f)
    {
        DWORD cw = 0xF;
        dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
        if (cw & 0xF)
        {
            bool hit = chvOverlayPhase;             // the phase is already open
            if (!hit)
                for (DWORD st = 0; st < 8; st++)
                {
                    IDirect3DBaseTexture9* u = nullptr;
                    if (FAILED(dev->GetTexture(st, &u)) || !u) continue;
                    for (int k = 0; k < rsIntzN; k++) if (rsIntz[k] == u) { hit = true; break; }
                    u->Release();                   // tracked only; the game holds the reference
                    if (hit) break;
                }
            if (hit)
            {
                chvOverlayPhase = true;
                // A full-target viewport (radar, HUD): withhold. A partial one (the phone's 3D model) gets its own output-size
                // depth, cleared to far and writable.
                {
                    D3DVIEWPORT9 vp = {};
                    if (SUCCEEDED(dev->GetViewport(&vp))
                        && rsOutW && rsOutH
                        && (vp.Width < rsOutW || vp.Height < rsOutH))
                    {
                        bool seen = false;
                        for (int i = 0; i < chvVpClearedN; i++)
                            if (chvVpCleared[i].x == vp.X && chvVpCleared[i].y == vp.Y
                             && chvVpCleared[i].w == vp.Width && chvVpCleared[i].h == vp.Height)
                            { seen = true; break; }
                        if (!seen)
                        {
                            if (chvVpClearedN < 4)
                            {
                                // Clear obeys the viewport (the overlay's own rect), so the world depth the coronas need
                                // survives elsewhere.
                                dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, farZ, 0);
                                chvVpCleared[chvVpClearedN].x = vp.X;
                                chvVpCleared[chvVpClearedN].y = vp.Y;
                                chvVpCleared[chvVpClearedN].w = vp.Width;
                                chvVpCleared[chvVpClearedN].h = vp.Height;
                                chvVpClearedN++;
                            }
                            else
                            {
                                // A fifth partial rect in one frame: not cleared (it tests against world depth), and the log
                                // says so once.
                                static bool said = false;
                                if (!said)
                                {
                                    said = true;
                                    LogHealth("  chevron: a fifth partial-viewport overlay in one frame (%ux%u at %u,%u); its depth is not cleared",
                                        (unsigned)vp.Width, (unsigned)vp.Height, (unsigned)vp.X, (unsigned)vp.Y);
                                }
                            }
                        }
                        // chvDepth stays bound; ChvAfter puts the game's surface back after the draw.
                        return true;
                    }
                    // Full-target viewport: fall through to the withhold, which
                    // is the right answer for a 2D screen-wide composite.
                }
                // DXVK drops the mismatched surface rather than clamping to it. No ChvAfter: this bind is the one it would
                // make.
                dev->SetDepthStencilSurface(DsBracketPutBack(chvSaved, chvSavedSeq));
                chvSaved = nullptr;
                return false;
            }
        }
    }
    return true;
}

// Puts the game's depth back (bound moments ago, so still alive). noinline: its RsOwnCall would give hkDrawIndexed an SEH
// frame.
static __declspec(noinline) void ChvAfter(IDirect3DDevice9* dev)
{
    RsOwnCall guard;
    dev->SetDepthStencilSurface(DsBracketPutBack(chvSaved, chvSavedSeq));
    chvSaved = nullptr;
}

// ===== the tracker's side, and its per-draw chain as one body =====
// TrkRender and TrkWorker (devthread/dt_trk.h) list the same inputs in the same order; dt_trk.h's verifier checks them.
struct TrkRender
{
    static constexpr bool kMayDraw = true;      // this thread owns the device: the drains stay
    static __forceinline const float*     World()        { return m4bCurWorld; }
    static __forceinline unsigned __int64 WorldSerial()  { return m4bWorldSerial; }
    static __forceinline bool             HaveWVP()      { return m4bHaveWVP; }
    static __forceinline const float*     WVP()          { return m4bCurWVP; }
    static __forceinline bool             HaveWheel()    { return m4bHaveWheel; }
    static __forceinline const float*     Wheel()        { return m4bCurWheel; }
    static __forceinline const float*     BoneShadow()   { return boneShadow; }
    static __forceinline int              BoneMaxReg()   { return boneMaxReg; }
    static __forceinline unsigned __int64 BoneSerial()   { return boneShadowSerial; }
    static __forceinline UINT_PTR         VBKey()        { return m4bCurVB; }
    static __forceinline UINT_PTR         IBKey()        { return m4bCurIB; }
    static __forceinline IDirect3DVertexBuffer9*      VB()   { return velCurVB; }
    static __forceinline UINT             VBOff()        { return velCurVBOff; }
    static __forceinline UINT             VBStr()        { return velCurVBStr; }
    static __forceinline IDirect3DIndexBuffer9*       IB()   { return velCurIB; }
    static __forceinline IDirect3DVertexDeclaration9* Decl() { return velCurDecl; }
    static __forceinline IDirect3DSurface9*           DS()   { return curDS; }
    static __forceinline bool             Skinned()      { return velCurSkinned; }
        // Multi is XMask() != 0, so the two cannot disagree.
    static __forceinline bool             Multi()        { return velCurXMask != 0; }
    static __forceinline unsigned char    XMask()        { return velCurXMask; }
    static __forceinline IDirect3DVertexBuffer9*      XVB(int s)  { return velCurXVB[s]; }
    static __forceinline UINT             XOff(int s)    { return velCurXOff[s]; }
    static __forceinline UINT             XStr(int s)    { return velCurXStr[s]; }
    static __forceinline bool             Premul()       { return velCurVSPremul; }
    static __forceinline IDirect3DVertexShader9* VS()    { return velCurVS; }          // the bound vertex shader: VelDmgNote asks whether it reads the game's damage map
    static __forceinline unsigned char    Texld0()       { return velCurVSTexld0; }    // its texldl on the first vertex sampler (4: a damage candidate)
    static __forceinline float            Window()       { return m4dWindow; }
    static __forceinline LONG             Frames()       { return frames; }
    static __forceinline bool             CamPrevValid() { return camPrev.valid; }
    static __forceinline const Mat4d&     CamPrevVP()    { return camPrevVPd; }
        // The pending mover, written by TrkOnDrawPre and read by VelRecordObject after the draw: each thread has its own block.
    static __forceinline bool&             Pend()        { return velPending; }
    static __forceinline D3DPRIMITIVETYPE& PendType()    { return velPendingType; }
    static __forceinline INT&              PendBase()    { return velPendingBase; }
    static __forceinline UINT&             PendMin()     { return velPendingMin; }
    static __forceinline UINT&             PendNum()     { return velPendingNum; }
    static __forceinline UINT&             PendStart()   { return velPendingStart; }
    static __forceinline UINT&             PendPrim()    { return velPendingPrim; }
    static __forceinline Mat4&             PendWVP()     { return velPendingWVP; }
    static __forceinline int&              PendBoneRow() { return velPendingBoneRow; }
    static __forceinline bool&             PendPremul()  { return velPendingPremul; }
    static __forceinline Mat4&             PendWVP8()    { return velPendingWVP8; }
};

// The tracker's per-draw chain, into velPending; __forceinline to match the inline form (tools/trkdiff.py). Scene draws only.
template<class S>
static __forceinline void TrkOnDrawPre(D3DPRIMITIVETYPE type, INT baseVertex, UINT minVertex,
                                       UINT numVertices, UINT startIndex, UINT primCount)
{
    float prevWorld[16];
    int   prevBoneRow = -1;
    bool  mover = false;
    // The tyre's position is (pos x M) x c8, so its world transform is
    // M x gWorld and that, not gWorld, is what identifies and places it.
    // Each of a car's four tyres shares gWorld and the mesh; only M
    // differs, so this is also what tells them apart.
    Mat4         effWorld;
    const float* trackWorld = S::World();
    bool         premul     = false;
    if (S::Premul() && !S::Skinned())
    {
        if (S::HaveWheel() && S::HaveWVP())
        {
            MulMat(*(const Mat4*)S::Wheel(), *(const Mat4*)S::World(), &effWorld);
            trackWorld = &effWorld.m[0][0];
            premul     = true;
        }
    }
    {
        mover = M4dNoteDraw<S>((UINT)baseVertex, startIndex, primCount,
                               trackWorld, prevWorld, S::Skinned(),
                               &prevBoneRow);
    }
    if (mover && S::CamPrevValid())
    {
// prevWVP in double: the camera is ~1745 units out. The camera half is camPrevVPd, formed once a frame.
#if DLSSIV_DEV
        if (premul) InterlockedIncrement(&velStatWheel[ivtCfg.mv_wheel ? VelWheelNoSpin(trackWorld, prevWorld, &velWheelAxle) : 5]);   // mv_wheel=1, the default: the tyre's spin left out; 0 keeps it
#else
        if (premul) VelWheelNoSpin(trackWorld, prevWorld, &velWheelAxle);   // a tyre is given the car's motion, its spin left out of the vectors
#endif
        Mat4d pw, res;
        ToD(*(const Mat4*)prevWorld, &pw);
        MulMatD(pw, S::CamPrevVP(), &res);
        ToF(res, &S::PendWVP());
        S::Pend() = true;
        S::PendType() = type;
        S::PendBase() = baseVertex; S::PendMin() = minVertex;
        S::PendNum()  = numVertices; S::PendStart() = startIndex;
        S::PendPrim() = primCount;
        S::PendBoneRow() = prevBoneRow;
        // The redraw's own vertex shader reads c8 for the current frame.
        // For a tyre that has to be M x c8, formed in double for the same
        // reason as prevWVP, and put back to the game's c8 afterwards.
        S::PendPremul() = premul;
        if (premul)
        {
            Mat4d md, cd, ed;
            ToD(*(const Mat4*)S::Wheel(), &md);
            ToD(*(const Mat4*)S::WVP(),   &cd);
            MulMatD(md, cd, &ed);
            ToF(ed, &S::PendWVP8());
        }
    }
}

#if DLSSIV_DEV
// The glass census (development builds, when a DLSSIV-glass folder is beside GTAIV.exe). At the file's end.
static bool gcOn = false;
static void GlassCensusInit();
static void GlassCensus(IDirect3DDevice9* dev, int kind, UINT prims);
#endif
static HRESULT __stdcall hkDrawIndexed(IDirect3DDevice9* dev,
                                       D3DPRIMITIVETYPE type, INT baseVertex,
                                       UINT minVertex, UINT numVertices,
                                       UINT startIndex, UINT primCount)
{
    DT_NOT_ON_WORKER(origDrawIndexed(dev, type, baseVertex, minVertex, numVertices, startIndex, primCount));
#if DLSSIV_DEV
    if (gcOn && curRT0 && curRT0 == sceneBufSurf && !rsOwn && !ivtInOwnDraw) GlassCensus(dev, 0, primCount);   // the glass census
#endif
        // The absorb's guard (hdr_cga.h), above the gate below, which is closed in the menus.
    if (hdrCgaHeld && curRT0 == backbufferSurf && !dead && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_DRAW);
        // No !reported test here: the tracker and the velocity pass run the whole session (if the per-frame clear stopped, the
    // target would freeze).
    if (!dead && toldVerdict && m4bHaveWorld)
    {
#if DLSSIV_DEVLOG
        if (!toldDrawHook)
        {
            toldDrawHook = true;
            // Positive, told once: a hook that only speaks on failure cannot say it ran.
            LogHealth("  hook: DrawIndexedPrimitive fired -- per-draw geometry is visible");
            GlassCensusInit();
        }
#endif
        VelClearIfNeeded(dev);

                // Only the scene viewport: other passes draw the same meshes and would claim the scene's previous-frame entries.
        bool  sigNow   = haveSceneKey && lastKey.w != 0 && KeyEq(lastKey, sceneKey);
        bool  sceneNow = sigNow;
        BYTE* drawVp   = nullptr;
        if (m4bHaveViewInv && haveSceneViewInv)
        {
            if (camCmpFor != camCmpSerial)
            {
                camCmpFor = camCmpSerial;
                // Four lanes at a time; the full maximum leaves camMatch unchanged. Memoised on camCmpSerial.
                CamDiffMax(m4bCurViewInv, sceneViewInv, &camCmpDRot, &camCmpDPos);
            }
            const float dRot = camCmpDRot, dPos = camCmpDPos;
            // Rotation and translation are split: a water reflection camera sits at nearly the same position as the scene
            // camera, flipped in pitch.
            bool camMatch = (dRot < 1e-4f && dPos < 0.05f);
            // camMatch is not diagnostic: it arms the self-proving gate below and then becomes sceneNow.

            // SELF-PROVING. A matching draw IS the proof that c12..c15 is the
            // camera; the first one arms the gate. If c12 were not the camera
            // nothing would ever match and the signature would keep control.
            if (camCur.valid && camMatch && !viewInvProven)
            {
                viewInvProven = true;
                LogHealth("  c12..c15 matched the c227-captured camera on a live"
                    " draw (dRot %.2g, dPos %.2g): per-draw camera gate ARMED",
                    dRot, dPos);
            }
            if (viewInvProven && camCur.valid)
            {
                sceneNow = camMatch;
                // [equiv_c227: count]
                // Which candidate this draw's camera is, memoised on camCmpSerial.
                if (camMatch) { if (sceneCandLive >= 0) sceneCands[sceneCandLive].draws++; }
                else if (sceneCandN > 1)
                {
                    if (camCandFor != camCmpSerial)
                    {
                        camCandFor = camCmpSerial; camCandIdx = -1;
                        for (int i = 0; i < sceneCandN; i++)
                        {
                            if (i == sceneCandLive) continue;
                            float dR = 0.0f, dP = 0.0f;
                            CamDiffMax(m4bCurViewInv, sceneCands[i].vinv, &dR, &dP);
                            if (dR < 1e-4f && dP < 0.05f) { camCandIdx = i; break; }
                        }
                    }
                    if (camCandIdx >= 0) sceneCands[camCandIdx].draws++;
                }
                // [/equiv_c227: count]
            }
            else if (viewInvProven && haveSceneViewInvPrev)
            {
                // Head of the frame, before this frame's camera is known: a
                // genuine early scene draw sits within a frame's travel of
                // LAST frame's camera; the phantom is ~1750 units away, a
                // mirror has a flipped basis, a cascade sits at the light.
                if (camCmpPrevFor != camCmpSerial)
                {
                    camCmpPrevFor = camCmpSerial;
                    // The same four lanes: the only consumer is sceneNow = (pRot < 0.6f && pPos < 5.0f).
                    CamDiffMax(m4bCurViewInv, sceneViewInvPrev, &camCmpPRot, &camCmpPPos);
                }
                const float pRot = camCmpPRot, pPos = camCmpPPos;
                sceneNow = (pRot < 0.6f && pPos < 5.0f);
            }
        }

                // The scene level to the device thread, before the draw itself is recorded below.
        TrkSendScene(sceneNow);

        if (sceneNow)
        {
            DEVSTAT(if (trkApplyDirty) { trkSceneAfterApply++; trkApplyDirtySeen |= trkApplyDirty; })   // the tracker's premise (dt_res.h)
                                                                                                        // The window is sized from this frame's interval, so a stall widens it; measured here and sent to the worker.
            if (m4dWindowFrame != frames)
            {
                m4dWindowFrame = frames;
                M4dUpdateWindow();
                TrkSendWindow();            // the window this frame uses, to the worker
            }
            if (!trkWorkerLive)         // the device thread's draw observer runs this chain while the layer is armed
                TrkOnDrawPre<TrkRender>(type, baseVertex, minVertex, numVertices, startIndex, primCount);
        }
        else if (censusOn)
            drawVp = CurrentViewportFast();   // the skip table below is its only reader
                // CurrentViewportFast carries an SEH frame, so it runs only for the census below.

#if DLSSIV_DEVLOG
        if (censusOn && !sceneNow)
        {
            // Keyed on the object's viewport, so the census names the viewport that rejected a mover.
            bool found = false;
            for (int i = 0; i < velSkipSigN; i++)
                if (velSkipSig[i].vp == drawVp)
                { velSkipSig[i].hits++; found = true; break; }
            if (!found && velSkipSigN < 12)
            {
                velSkipSig[velSkipSigN].vp    = drawVp;
                velSkipSig[velSkipSigN].w     = lastKey.w;
                velSkipSig[velSkipSigN].h     = lastKey.h;
                velSkipSig[velSkipSigN].fovQ  = lastKey.fovQ;
                velSkipSig[velSkipSigN].nearQ = lastKey.nearQ;
                velSkipSig[velSkipSigN].hits  = 1;
                velSkipSigN++;
            }
        }
#endif

        {
            DrawRec rec;
            rec.prims = primCount;
            rec.indexed = true; rec.kind = 0; rec.posT = velCurPosT;
            rec.sceneKnown = true; rec.scene = sceneNow;
            rec.vs = velCurVS;
            // An orthographic composite keeps w constant: c8[2][3] = 0, c8[3][3] = 1.
            rec.ortho = m4bHaveWVP && fabsf(m4bCurWVP[11]) < 1e-6f && fabsf(m4bCurWVP[15] - 1.0f) < 1e-3f;
            rec.bb    = (curRT0 != nullptr && (curRT0 == backbufferSurf || (rsRedirectOn && curRT0 == rsRedirectSurf)));
            NoteDrawRec(rec);
        }

    }

        // The game's draw first, so the redraw's LESSEQUAL test selects its visible surface. The rain: four vertices, two
    // triangles, a vertex shader of two texture reads (RainBefore).
    const bool rainSub = numVertices == 4 && primCount == 2 && velCurVSTexld == 2 && RainBefore(dev);
    // THE CORONAS: a vertex shader of twelve texture reads or more (CorHoldBefore)
    const bool corSub = velCurVSTexld0 >= 12 && CorHoldBefore(dev);
    DEVSTAT(if (velCurVSTexld >= 12 && telemetryOn && !ivtInOwnDraw && !rsOwn) { if (velCurVSTexld0 >= 12) ivtCorHook[0]++; else ivtCorOther12++; })
    bool chvSub = false;
    if (chvArmed) { chvSub = ChvBefore(dev); }
    HRESULT hr = origDrawIndexed(dev, type, baseVertex, minVertex, numVertices,
                                 startIndex, primCount);
    if (chvSub) { ChvAfter(dev); }
    if (rainSub) RainAfter(dev);
    if (corSub) CorHoldAfter(dev);

    if (velPending)
    {
        velPending = false;
        VelRecordObject<TrkRender>(dev, &velPendingWVP.m[0][0], velPendingType,
                        velPendingBase, velPendingMin, velPendingNum,
                        velPendingStart, velPendingPrim, velPendingBoneRow,
                        velPendingPremul ? &velPendingWVP8.m[0][0] : nullptr);
        velPendingPremul = false;
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Device discovery and hooking
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Device reset
// ---------------------------------------------------------------------------
// A Reset fails while any default-pool resource is alive: the mod's are released and rebuilt at EndScene.
typedef HRESULT(__stdcall* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static Reset_t origReset = nullptr;

// The pause path (menu, photo mode) copies the back buffer into a scaled texture and redraws it: the copy goes to a screen-size
// texture of the mod's, handed to the game's sampler for that frame. Before the injection only.
struct RsReadback { IDirect3DBaseTexture9* gameTex; IDirect3DTexture9* ours; IDirect3DSurface9* oursSurf; LONG frame; };
// Four entries: the game's pause copy, FusionFix's console gamma copy of the finished frame, and room for two more. Each is
// made when its read-back first happens.
static const int kRsRbMax = 4;
static RsReadback rsRb[kRsRbMax];
DLSSIV_HOT static int  rsRbN = 0;
// Only the first back-buffer read-back of a frame is substituted: the pause path's second one is after the HUD.
static LONG rsRbDoneFrame = -1;

static void RsReleaseReadbacks()
{
    for (int i = 0; i < rsRbN; i++)
    {
        if (rsRb[i].oursSurf) rsRb[i].oursSurf->Release();
        if (rsRb[i].ours)     rsRb[i].ours->Release();
    }
    rsRbN = 0;
}

static IDirect3DSurface9* RsReadbackTarget(IDirect3DDevice9* dev, RsSurf* e)
{
    for (int i = 0; i < rsRbN; i++)
        if (rsRb[i].gameTex == e->tex) { rsRb[i].frame = frames; return rsRb[i].oursSurf; }
    if (rsRbN >= kRsRbMax) return nullptr;
    D3DSURFACE_DESC d = {};
    if (FAILED(e->surf->GetDesc(&d))) return nullptr;
    IDirect3DTexture9* t = nullptr;
    rsOwnCreate++;
    HRESULT hr = dev->CreateTexture(e->origW, e->origH, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &t, nullptr);
    rsOwnCreate--;
        // A refusal is a log=1 line (the pause menu and photo mode then show the render-size picture for the session), capped at
    // three: the caller asks every frame.
    static int rbRefusedSaid = 0;
    if (FAILED(hr) || !t)
    { if (rbRefusedSaid++ < 3)
        LogRecord("  render scale: read-back substitute %ux%u format %u REFUSED 0x%08X: the pause menu's and Photo Mode's copies stay at render size%s", e->origW, e->origH, (unsigned)d.Format, (unsigned)hr,
                rbRefusedSaid == 3 ? " (said three times, no more)" : "");
      return nullptr; }
    IDirect3DSurface9* sf = nullptr;
    t->GetSurfaceLevel(0, &sf);
    rsRb[rsRbN].gameTex = e->tex; rsRb[rsRbN].ours = t; rsRb[rsRbN].oursSurf = sf; rsRb[rsRbN].frame = frames;
    rsRbN++;
    LogHealth("  render scale: the read-back of the backbuffer into scaled texture %p now lands in the mod's %ux%u copy (format %u)",
        (void*)e->tex, e->origW, e->origH, (unsigned)d.Format);
    return sf;
}


static HRESULT __stdcall hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    DT_NOT_ON_WORKER(origReset(dev, pp));
        // Every release below must reach DXVK before the Reset: the barrier disarms the device thread first.
    const bool  rsArmed = dtArmed;
    const bool  rsEmpty = ((DWORD)dtPub == (DWORD)dtDone);
    const long  rsFr    = (long)dtFrN;
    DWORD       rsMs    = 0;
    const char* rsHow   = DtResetBarrier(&rsMs);
        // A refused Reset is retried every frame: lines stop from the fourth identical failure, and the success says how many.
    static UINT      rsFailW = 0, rsFailH = 0; static D3DFORMAT rsFailFmt = D3DFMT_UNKNOWN; static BOOL rsFailWin = 0;
    static int       rsFailSame = 0;          // consecutive failures with these parameters
    const bool repeat = rsFailSame > 0 && pp && pp->BackBufferWidth == rsFailW && pp->BackBufferHeight == rsFailH
                        && pp->BackBufferFormat == rsFailFmt && pp->Windowed == rsFailWin;
    const bool quiet  = repeat && rsFailSame >= 3;
        // Keyed on what the game passed: HdrRewriteFormat and DXVK both change pp.
    const UINT      askedW   = pp ? pp->BackBufferWidth  : 0;
    const UINT      askedH   = pp ? pp->BackBufferHeight : 0;
    const D3DFORMAT askedFmt = pp ? pp->BackBufferFormat : D3DFMT_UNKNOWN;
    const BOOL      askedWin = pp ? pp->Windowed         : FALSE;
        // Whether HDR was running before this Reset, for the failure arm below.
    const bool hdrWasEngaged = hdrEngaged;
        // With HDR on, a Reset to exclusive fullscreen keeps the window borderless: leaving an HDR swapchain for
    // exclusive fullscreen freezes in NVIDIA's driver, and HDR cannot show in exclusive fullscreen. Not under Wine.
    const bool hdrHoldBorderless = ivtCfg.hdr && hdrWasEngaged && pp && !pp->Windowed && !HdrOnWine();
    const UINT askedHz = pp ? pp->FullScreen_RefreshRateInHz : 0;
    if (hdrHoldBorderless)
    {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;          // D3D9 wants 0 for a windowed swapchain
                // Not on a retry that keeps failing the same way (quiet).
        if (!quiet)
            LogRecord("  hdr: the game asked for exclusive fullscreen: kept borderless");
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            IvtWarn("hdr=1: Fullscreen was chosen in the game's graphics options. HDR needs Borderless, so the window stays borderless"
                    " and HDR stays on. Choose Borderless in the options to make the menu agree");
        }
    }
        // A windowed/fullscreen switch is a Reset. Frame generation's ring retires first: HdrEncodeRelease frees the target the
    // hand's raw copy into the helper reads. tools/check-fg-fence.py holds the order.
    FGX(FgpOnResetBarrier(rsHow);)
    HdrEncodeRelease();
    hdrSrgbTex0 = false;              // DXVK's Reset clears every sampler state
        // Windows HDR state, peak and SDR white are asked again for the new mode, once per distinct attempt.
    if (!repeat) hdrDisplayLogged = false;
    if (pp && pp->hDeviceWindow) hdrGameWnd = pp->hDeviceWindow;   // the mode change may have moved the window
    HdrRewriteFormat(pp, "Reset");
    hdrHooks = ivtCfg.hdr && hdrEngaged;   // the draw hooks follow the engagement (hdr.h)
    if (hdrCsScRgbSet && !hdrEngaged) HdrColorSpaceStandDown(dev, "at this Reset");   // hdr_encode.h: DXVK keeps scRGB across a Reset otherwise
        // fpTabN is not zeroed here (its references go in the loop before origReset). The last GPU submit retires before textures
    // go.
    IvtGuidesQuiesce("a device Reset, before the mod's textures go");
    FpSceneRelease();                 // the mod's own, so they must be let go before the reset
    sceneBufTex = nullptr; sceneBufSurf = nullptr; seamSceneWrote = false;
    if (velInterop) { velInterop->Release(); velInterop = nullptr; }
    if (velSurf)    { velSurf->Release();    velSurf    = nullptr; }
    if (velTex)     { velTex->Release();     velTex     = nullptr; }
    velW = 0; velH = 0;
    velTried = false; velFailed = false; velNeedClear = true;
    velConstFrame = -1;   // the reset cleared the constant file; force both back
    if (boneTex)    { boneTex->Release();    boneTex    = nullptr; }
    boneTexTried = false; boneTexOK = false;
    // The previous camera and its history no longer apply after a reset.
    camCur.valid = false; camPrev.valid = false;
    sceneCandHave = false;
    sceneCandN = 0; sceneCandLive = -1; camCandFor = 0xFFFFFFFFu;
    haveSceneViewInv = false; haveSceneViewInvPrev = false;

    IvtOnReset();                       // releases the transport's default-pool textures
        // NVIDIA's forced vsync, asked of DXVK in the game's place as at CreateDevice; the game's value goes back after the call
    const UINT gameInterval = pp ? pp->PresentationInterval : D3DPRESENT_INTERVAL_DEFAULT;
    if (pp) pp->PresentationInterval = IvtDriverVsyncInterval(gameInterval, !quiet);
    if (pp) latVsync = LatVsyncEffective(pp->PresentationInterval);   // the pacer's stand-down follows the new interval
    if (pp && pp->BackBufferWidth && pp->BackBufferHeight && !quiet)
        LogHealth("  device RESET requested: %ux%u fmt %u windowed %d interval %s (+%lu ms)", pp->BackBufferWidth, pp->BackBufferHeight,
            (unsigned)pp->BackBufferFormat, (int)pp->Windowed, PresentIntervalName(pp->PresentationInterval), (unsigned long)CenMs());
        // Every default-pool reference the mod holds goes first, or the Reset fails.
    rsRedirectOn = false; rsRedirectSurf = nullptr;
    if (chvDepth)    { chvDepth->Release();    chvDepth    = nullptr; }   // default pool: must go before the reset
    if (chvDepthTex) { chvDepthTex->Release(); chvDepthTex = nullptr; }
    dsAskedValid = false;            // a Reset frees what the engine was told about (ds_bracket.h)
    chvTried = false; chvFilled = false; chvFilledFar = -1.0f; chvSaved = nullptr;
    chvOverlayPhase = false; chvVpClearedN = 0;
    chvDsScaled = false; chvArmed = false;
    if (shpSurf) { shpSurf->Release(); shpSurf = nullptr; }      // default pool, likewise
    if (shpTex)  { shpTex->Release();  shpTex  = nullptr; }
    if (shpPS)   { shpPS->Release();   shpPS   = nullptr; }
    shpTried = false; shpDone = false; shpBbSeen = false; shpFmtUsed = D3DFMT_UNKNOWN;
    shpG10 = false;              // shpG10's latch too: the first frame after a Reset is the pass's, then the latch is taken again
    AcquireSRWLockExclusive(&rsTablesLock);      // a thread still creating must not insert into a table being emptied
        // rsIntzN is cleared under the lock every writer of rsIntz holds: an entry lives exactly as long as the rsTab reference
    // beside it.
    rsIntzN = 0;
    for (int i = 0; i < rsTabN; i++) if (rsTab[i].held && rsTab[i].tex) rsTab[i].tex->Release();
    rsTabN = 0;
    for (int i = 0; i < fpTabN; i++) if (fpTab[i].tex) fpTab[i].tex->Release();   // see fpTab's declaration
    fpTabN = 0;
    ReleaseSRWLockExclusive(&rsTablesLock);
    rsHaveDxvkVp = false; rsHaveDxvkSc = false;
    RsReleaseReadbacks();
        // Screen size and render scale are decided after a successful origReset. The batch is dropped first: it references game
    // buffers.
    if (!trkWorkerLive) VelBatchDrop();
        // Whether anything of the mod's is still alive at the Reset (DXVK logs only the count): every pointer here must be null.
    if (!quiet)
        LogHealth("  reset: thread %lu (render %lu, device-thread producer %lu); found armed %d, ring %s, %ld handed-over releases"
                  " waiting; barrier: %s, %lu ms, %lu polls; the mod's resources still held: vel %p bone %p ivtMv %p ivtDepth %p ivtIntz %p ivtRsColor %p"
                  " gameDs %p chvDepth %p shp %p hdr %p; tables fpTab %d rsTab %d fpSc %d fpScNo %d rsRb %d velBatch %d",
            (unsigned long)GetCurrentThreadId(), (unsigned long)renderTid, (unsigned long)dtProducer, rsArmed ? 1 : 0,
            rsEmpty ? "empty" : "NOT EMPTY", rsFr, rsHow, (unsigned long)rsMs, (unsigned long)dtBarrierPolls,
            (void*)velTex, (void*)boneTex, (void*)ivtMvTex, (void*)ivtDepthTex, (void*)ivtIntzTex, (void*)ivtRsColorTex,
            (void*)ivtGameDsTex, (void*)chvDepth, (void*)shpTex, (void*)hdrTex,
            fpTabN, rsTabN, fpScN, fpScNoN, rsRbN, velBatchN);
    HRESULT hr = origReset ? origReset(dev, pp) : D3DERR_INVALIDCALL;
    if (pp) pp->PresentationInterval = gameInterval;   // the game's own value back (the mod may have asked DXVK for vsync above)
        // Quiet only while it keeps failing: the success that ends a run of identical failures is logged. Decided straight after
    // the call, so the held window's fault line obeys it too.
    const bool quietNow = quiet && FAILED(hr);
    if (hdrHoldBorderless)
    {
                // The game's struct gets its request back. A game asking for exclusive fullscreen at another size may resize its
        // window: said only when it no longer covers its monitor.
        pp->Windowed = FALSE; pp->FullScreen_RefreshRateInHz = askedHz;
        RECT wr = {}; MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
        const HWND hw = pp->hDeviceWindow ? pp->hDeviceWindow : hdrGameWnd;
        if (!quietNow && hw && GetWindowRect(hw, &wr) && GetMonitorInfoA(MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST), &mi)
            && (wr.right - wr.left != mi.rcMonitor.right - mi.rcMonitor.left || wr.bottom - wr.top != mi.rcMonitor.bottom - mi.rcMonitor.top))
            LogRecord("  hdr: kept borderless, but the window %ldx%ld at %ld,%ld does not cover its monitor %ldx%ld at %ld,%ld (back buffer %ux%u)",
                wr.right - wr.left, wr.bottom - wr.top, wr.left, wr.top,
                mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, mi.rcMonitor.left, mi.rcMonitor.top,
                askedW, askedH);
    }
    statDeviceResets++;
    hdrSwapHookDirty = true;          // the swapchain may be a new object now; re-check its vtable
    const int failedBefore = rsFailSame;
    if (FAILED(hr) && pp)
    {
        if (repeat) rsFailSame++;
        else { rsFailSame = 1; rsFailW = askedW; rsFailH = askedH; rsFailFmt = askedFmt; rsFailWin = askedWin; }
    }
    else rsFailSame = 0;
    if (SUCCEEDED(hr) && pp && pp->BackBufferWidth && pp->BackBufferHeight)
    {
        cenScreenW = pp->BackBufferWidth; cenScreenH = pp->BackBufferHeight;
        RsOnScreenSize(pp->BackBufferWidth, pp->BackBufferHeight);
    }
        // Every reset is logged with its result: a mode change the game never attempts looks like one that failed. Identical
    // failures past the third are the exception (quiet).
    if (pp && !quietNow)
        LogRecord("  reset #%llu: %ux%u fmt %u bb %u %s, interval %s -> %s%s%s", (unsigned long long)statDeviceResets,   // the interval the game asked for
            pp->BackBufferWidth, pp->BackBufferHeight, (unsigned)pp->BackBufferFormat, (unsigned)pp->BackBufferCount,
            hdrHoldBorderless ? "windowed (fullscreen asked, held borderless for HDR)" : pp->Windowed ? "windowed" : "fullscreen",
            PresentIntervalName(pp->PresentationInterval),
            SUCCEEDED(hr) ? "OK" : "FAILED",
            SUCCEEDED(hr) ? "" : (hr == D3DERR_INVALIDCALL ? " (D3DERR_INVALIDCALL)"
                               : hr == D3DERR_DEVICELOST   ? " (D3DERR_DEVICELOST)"
                               : hr == D3DERR_NOTAVAILABLE ? " (D3DERR_NOTAVAILABLE)" : ""),
            rsFailSame == 3 ? "; the third identical failure in a row: further ones are not logged until the parameters change or a Reset succeeds" : "");
    if (SUCCEEDED(hr) && failedBefore >= 3)
        LogRecord("  reset #%llu succeeded after %d identical failures", (unsigned long long)statDeviceResets, failedBefore);
    if (SUCCEEDED(hr)) { LatSayRefresh(true); RgArm(); }   // the refresh may have changed, and the interval with it

        // Borrowed pointers to game surfaces are dead now (a freed pointer is not null), the majority table included. Not AddRef: a
    // reference across a Reset makes it fail.
    dsSceneMajority = nullptr;
    dsAtSceneDraws  = nullptr;
    dsCountUsed     = 0;
    for (int i = 0; i < 6; i++) { dsCountSurf[i] = nullptr; dsCountN[i] = 0; }
    curRT0 = nullptr; curDS = nullptr;
        // And the 1..3 shadow: the velocity redraw restores from these, so a stale entry is a SetRenderTarget on freed memory.
    curRT123[0] = curRT123[1] = curRT123[2] = nullptr;
        // The binding shadows are borrowed pointers too.
    velCurVB = nullptr; velCurIB = nullptr; velCurDecl = nullptr;
    velCurVBOff = velCurVBStr = 0;
        // Streams 1..3 with them; a null here also makes VelRecordObject refuse to carry a stream it has not seen bound since.
    for (int s = 0; s < VEL_XSTREAMS; s++) { velCurXVB[s] = nullptr; velCurXOff[s] = velCurXStr[s] = 0; }
    velCurXMask = 0;
        // The batch itself was dropped above origReset.
    seamArmed = false; seamThisFrame = false; c8DevJit = false; vtf0Tex = nullptr; c227Have = false;
    backbufferSurf = nullptr;
    if (SUCCEEDED(hr))
    {
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb)
        {
            D3DSURFACE_DESC fd = {};
            hdrBbFp16 = SUCCEEDED(bb->GetDesc(&fd)) && fd.Format == D3DFMT_A16B16G16R16F;   // hdr.h: the address may be reused
            backbufferSurf = bb; bb->Release();   // borrowed, exactly as hkEndScene takes it
        }
    }
    else if (hdrEngaged && !hdrWasEngaged)
    {
                // A failed Reset leaves the 8-bit back buffer, so HDR stands down until one succeeds. Only when HDR was off before:
        // otherwise the FP16 buffer survives and HdrColorSpace re-runs.
        hdrEngaged = false;
        if (!quietNow) LogRecord("  hdr: the Reset FAILED (no FP16 back buffer): HDR off until a Reset succeeds");
    }
        // The new swapchain and refresh, read after a successful Reset (an exclusive fullscreen mode change happens inside it).
    if (SUCCEEDED(hr) && pp)
    {
        LogGameSwapchain(dev, pp, hdrHoldBorderless ? TRUE : pp->Windowed, "Reset");
        HdrReadRefresh(dev, "Reset");
        FGA(FgpOnResetDone(dev, pp);)         // the armed product: its conditions asked again (it parks in exclusive fullscreen too)
    }

    if (!quietNow)
        LogHealth("  device RESET %s (0x%08X); default-pool resources were released"
            " first and rebuild lazily. Borrowed pointers invalidated; the"
            " backbuffer is now %p", SUCCEEDED(hr) ? "succeeded" : "FAILED",
            (unsigned)hr, (void*)backbufferSurf);
    return hr;
}

// hkDrawPrim: the console gamma absorb, NoteDrawRec, the chevron's depth lend, and the SMAA sRGB fix.
typedef HRESULT(__stdcall* DrawPrim_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
DLSSIV_HOT static DrawPrim_t origDrawPrim = nullptr;

// FusionFix SMAA reads as sRGB and writes with D3DRS_SRGBWRITEENABLE, which DXVK drops on FP16, so HdrEncode would decode
// twice. Its sRGB read is suppressed for that draw.

static bool RsQuadDraw(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT startVertex, HRESULT* hr);   // below the user-pointer draws it uses

static HRESULT __stdcall hkDrawPrim(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type,
                                    UINT startVertex, UINT primCount)
{
    DT_NOT_ON_WORKER(origDrawPrim(dev, type, startVertex, primCount));
#if DLSSIV_DEV
    if (gcOn && curRT0 && curRT0 == sceneBufSurf && !rsOwn && !ivtInOwnDraw) GlassCensus(dev, 1, primCount);   // the glass census
#endif
    if (!dead && toldVerdict)
    {
                // the armed product in SDR (fg/fg_hud.h): FusionFix's console gamma draw is drawn again into the HUD-less picture's
        // target
        FGA(if (fgSdrCgaArmed && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) FgHudSdrCgaDraw(dev, _ReturnAddress(), false, type, startVertex, primCount, nullptr, 0);)
        if (hdrCgaHeld && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_DRAW);   // the absorb's guard (hdr_cga.h)
        // THE ABSORB: FusionFix v5.0.1 draws its console gamma quad from a vertex buffer, here.
        // Proved and wanted, it is withheld: the encode applies the curve instead.
        else if (hdrCgaArmed && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw && HdrCgaOnDraw(dev, _ReturnAddress()))
        { return D3D_OK; }
        {
            DrawRec rec;
            rec.prims = primCount;
            rec.indexed = false; rec.kind = 1; rec.posT = velCurPosT;
            rec.sceneKnown = false; rec.scene = false;
            rec.vs = velCurVS;
            // An orthographic composite keeps w constant: c8[2][3] = 0, c8[3][3] = 1.
            rec.ortho = m4bHaveWVP && fabsf(m4bCurWVP[11]) < 1e-6f && fabsf(m4bCurWVP[15] - 1.0f) < 1e-3f;
            rec.bb    = (curRT0 != nullptr && (curRT0 == backbufferSurf || (rsRedirectOn && curRT0 == rsRedirectSurf)));
            NoteDrawRec(rec);
        }
    }
    if (!origDrawPrim) return D3DERR_INVALIDCALL;
    const bool corSub = velCurVSTexld0 >= 12 && CorHoldBefore(dev);  // the coronas, where the game draws them unindexed (CorHoldBefore)
    DEVSTAT(if (velCurVSTexld0 >= 12 && telemetryOn && !ivtInOwnDraw && !rsOwn) ivtCorHook[1]++;)
    bool chvSub = false;
    if (chvArmed) { chvSub = ChvBefore(dev); }
        // The SMAA pass under HDR (see hdrSrgbTex0): two primitives into the FP16 back buffer, or its render-scale counterpart,
    // with both sRGB flags set.
    bool srgbOff = false;
        // Keyed on the buffer (hdrBbFp16), not on HDR: DXVK drops the sRGB write because the target is FP16, and HDR can stand down
    // at the first present with the FP16 buffer still in place.
    if (hdrSrgbTex0 && hdrBbFp16 && primCount <= 2 && !ivtInOwnDraw && curRT0 && origSetSamplerState
        && (curRT0 == backbufferSurf || (rsRedirectOn && curRT0 == rsRedirectSurf)))
    {
        DWORD w = 0;
        if (SUCCEEDED(dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &w)) && w)
        {
            origSetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, 0);
            srgbOff = true;
            const bool first = !hdrSrgbSaid;
            hdrSrgbSaid = true;
            // The tail says which state this is: after a stand-down the FP16 buffer stays but no encode runs.
            if (first)
                LogRecord("  hdr: FusionFix SMAA writes the FP16 back buffer with sRGB write on (DXVK drops it on float): its sRGB read is suppressed"
                          " for that draw%s", hdrEngaged ? "; the composite goes to an 8-bit target first (named when found)"
                                                          : "; HDR is off, the FP16 buffer stays until the next Reset");
        }
    }
        // The game's full-screen quad into a scaled target (transport/quad_uv.h): RsQuadDraw redraws it with the real half texel.
    HRESULT hr;
    if (!(curRT0Scaled && primCount == 2 && velCurVSStraight && velCurQuadUv != kNoQuadUv && !rsOwn && !ivtInOwnDraw
          && RsQuadDraw(dev, type, startVertex, &hr)))
        hr = origDrawPrim(dev, type, startVertex, primCount);
    if (srgbOff) origSetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, 1);
    if (chvSub) { ChvAfter(dev); }
    if (corSub) CorHoldAfter(dev);
    return hr;
}

// Whether a vertex shader runs the position through c64..c67 before c8 (the tyres), read from its bytecode. One scratch buffer,
// capped at 1 MiB, claimed with an interlocked flag.
static DWORD* fpBuf     = nullptr;
static UINT   fpBufCap  = 0;
static LONG   fpBufBusy = 0;

struct FpScratch
{
    DWORD* p;
    bool   own;
    explicit FpScratch(UINT size) : p(nullptr), own(false)
    {
        if (InterlockedCompareExchange(&fpBufBusy, 1, 0) == 0)
        {
            own = true;
            if (size > fpBufCap)
            {
                DWORD* nb = (DWORD*)realloc(fpBuf, size);
                if (!nb) return;                  // p stays null: the caller refuses
                fpBuf = nb; fpBufCap = size;
            }
            p = fpBuf;
        }
        else p = (DWORD*)malloc(size);
    }
    ~FpScratch() { if (own) InterlockedExchange(&fpBufBusy, 0); else if (p) free(p); }
};

static bool FingerprintVS(IDirect3DVertexShader9* vs, int* instrOut, unsigned* mask64Out,
                          unsigned* mask8Out, bool* relOut, bool* posWithMOut, bool* posWithWvpOut)
{
    UINT size = 0;
    if (FAILED(vs->GetFunction(nullptr, &size)) || size < 8 || size > (1u << 20))
        return false;
    FpScratch scratch(size);
    DWORD* tok = scratch.p;
    if (!tok) return false;
    bool ok = SUCCEEDED(vs->GetFunction(tok, &size));
    UINT  n  = size / 4;
    if (!ok || n < 2 || (tok[0] & 0xFFFF0000u) != 0xFFFE0000u) return false;

    bool     rel = false, posWithM = false, posWithWvp = false;
    unsigned mask64 = 0, mask8 = 0;
    DWORD    posReg = 0;
    int      instr = 0;
    UINT i = 1;
    while (i < n)
    {
        DWORD t = tok[i];
        if (t == 0x0000FFFFu) break;
        if ((t & 0xFFFFu) == 0xFFFEu)                       // comment block
        {
            i += 1 + ((t >> 16) & 0x7FFFu);
            continue;
        }
        DWORD op  = t & 0xFFFFu;
        UINT  len = (t & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
        UINT  end = i + 1 + len;
        if (end > n) break;
        if (op == D3DSIO_DCL && len == 2)
        {
            DWORD usage = tok[i + 1] & D3DSP_DCL_USAGE_MASK;
            DWORD uidx  = (tok[i + 1] & D3DSP_DCL_USAGEINDEX_MASK) >> D3DSP_DCL_USAGEINDEX_SHIFT;
            DWORD dst   = tok[i + 2];
            DWORD rtype = ((dst & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT)
                        | ((dst & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
            if (rtype == D3DSPR_INPUT && usage == D3DDECLUSAGE_POSITION && uidx == 0)
                posReg = dst & D3DSP_REGNUM_MASK;
            i = end; continue;
        }
                // Skipped whole: `def cN` immediates are constants, not uploads. No len >= 1 guard: nothing is dereferenced, and a
        // guard would change a malformed shader's fingerprint.
        if (op == D3DSIO_DEF || op == D3DSIO_DEFB || op == D3DSIO_DEFI)
        {
            i = end; continue;
        }
        instr++;
        bool hasPos = false, hasM = false, hasWvp = false, first = true;
        UINT j = i + 1;
        if (t & D3DSHADER_INSTRUCTION_PREDICATED) j++;    // predicate token sits after the dest
        while (j < end)
        {
            DWORD pr    = tok[j];
            DWORD rtype = ((pr & D3DSP_REGTYPE_MASK) >> D3DSP_REGTYPE_SHIFT)
                        | ((pr & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
            DWORD rnum  = pr & D3DSP_REGNUM_MASK;
            bool  relative = (pr & D3DVS_ADDRESSMODE_MASK) == D3DVS_ADDRMODE_RELATIVE;
            if (!first)
            {
                if (rtype == D3DSPR_INPUT && rnum == posReg) hasPos = true;
                if (rtype == D3DSPR_CONST)
                {
                    if (relative) rel = true;
                    if (rnum >= 64 && rnum <= 67) { hasM = true;   mask64 |= 1u << (rnum - 64); }
                    if (rnum >= 8  && rnum <= 11) { hasWvp = true; mask8  |= 1u << (rnum - 8);  }
                }
            }
            first = false;
            j += relative ? 2 : 1;
        }
        if (hasPos && hasM)   posWithM   = true;
        if (hasPos && hasWvp) posWithWvp = true;
        i = end;
    }
    if (instrOut)      *instrOut      = instr;
    if (mask64Out)     *mask64Out     = mask64;
    if (mask8Out)      *mask8Out      = mask8;
    if (relOut)        *relOut        = rel;
    if (posWithMOut)   *posWithMOut   = posWithM;
    if (posWithWvpOut) *posWithWvpOut = posWithWvp;
    return !rel && posWithM && !posWithWvp && mask64 == 0xF && mask8 == 0xF;
}


// The scene phase is everything before the seam, resolve or not: the scene must render into the counterpart so the depth
// matches.
static bool FpSceneActive()
{
    return rsActive && !seamThisFrame;
}

// Gets, or makes on first bind, the render-size counterpart the scene draws into instead of a game FP16 buffer. FpSceneReject
// refusals are permanent for the session.
static FpScene* FpSceneGet(IDirect3DDevice9* dev, IDirect3DSurface9* rt)
{
    if (FpScene* e = FpSceneForSurf(rt)) return e;
    if (!dev || !rt || fpScN >= 8 || !rsW || !rsH) return nullptr;
    if (FpSceneRejected(rt)) return nullptr;
    if (frames < fpScRetryFrame) return nullptr;       // a refusal a moment ago: not yet (see fpScCreateFails)
        // Ask the surface: fpTab misses targets created before the hooks armed.
    D3DSURFACE_DESC d = {};
    if (FAILED(rt->GetDesc(&d))) { FpSceneReject(rt); return nullptr; }
    if (d.Format != D3DFMT_A16B16G16R16F || d.Width != rsOutW || d.Height != rsOutH)
    { FpSceneReject(rt); return nullptr; }
    IDirect3DTexture9* parent = nullptr;
    if (FAILED(rt->GetContainer(__uuidof(IDirect3DTexture9), (void**)&parent)) || !parent)
    { FpSceneReject(rt); return nullptr; }
        // GetContainer's reference is kept from here down: it becomes the entry's, and FpSceneRelease returns it. Every early
    // return below gives it back.
    const bool single = parent->GetLevelCount() == 1;   // a mip chain is a reflection, not the scene
    if (!single) { parent->Release(); FpSceneReject(rt); return nullptr; }
    IDirect3DTexture9* t = nullptr;
    {
        RsOwnCreateCall guard;                    // never scaled, and never entered into fpTab
        if (FAILED(dev->CreateTexture(rsW, rsH, 1, D3DUSAGE_RENDERTARGET,
                                      D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t, nullptr)) || !t)
        {
            // Retried 300 frames later, five times, then the surface is rejected. Level 1: the picture is wrong while this
            // lasts.
            fpScCreateFails++;
            fpScRetryFrame = frames + 300;
            if (fpScCreateFails >= 5) FpSceneReject(rt);
            LogRecord("  designA: scene counterpart %ux%u for %p REFUSED (%ld): the scene renders at output size%s", rsW, rsH, (void*)parent, fpScCreateFails,
                fpScCreateFails >= 5 ? "; not tried again" : "; again in 300 frames");
                        // And once in the warnings file, when the stand-down becomes permanent (== 5: the counter is global).
            if (fpScCreateFails == 5)
                IvtWarn("%s=%d is set, but the %ux%u scene buffer the render scale needs could not be created (video memory), so the picture"
                    " is wrong: set it to 0",
                        ivtCfg.custom ? "custom" : "quality", ivtCfg.custom ? ivtCfg.custom : ivtCfg.quality, rsW, rsH);   // the key in use
            parent->Release();
            return nullptr;
        }
    }
    IDirect3DSurface9* sf = nullptr;
    if (FAILED(t->GetSurfaceLevel(0, &sf)) || !sf) { FpSceneReject(rt); t->Release(); parent->Release(); return nullptr; }
    AcquireSRWLockExclusive(&rsTablesLock);
    FpScene& e = fpSc[fpScN];
    e.gameTex = parent; e.gameSurf = rt;
    e.ourTex = t; e.ourSurf = sf; e.w = rsW; e.h = rsH;
    e.wroteOurs = -1; e.wroteOrig = -1;
    _ReadWriteBarrier(); fpScN = fpScN + 1;     // the count LAST: see rsTablesLock
    ReleaseSRWLockExclusive(&rsTablesLock);
    // Registered as a scaled surface so hkSetViewport scales the game's
    // output-size rectangles down onto it, exactly as it does for the G-buffer.
    RsRegisterOwn(t, sf, rsOutW, rsOutH, rsW, rsH);
    LogHealth("  designA: scene counterpart #%d  game %p %ux%u  ->  the mod's %p %ux%u"
        "   <-- the scene rasterises here; the post chain keeps the game's own buffer",
        fpScN, (void*)e.gameTex, rsOutW, rsOutH, (void*)t, rsW, rsH);
    return &e;
}

static HRESULT __stdcall hkSetRenderTarget(IDirect3DDevice9* dev, DWORD index, IDirect3DSurface9* rt)
{
    DT_NOT_ON_WORKER(origSetRenderTarget(dev, index, rt));
        // SetRenderTarget(0, NULL) is refused by every D3D9 and changes nothing, so nothing may follow it. FusionFix makes that
    // call each frame (GTAIV.EFLC.FusionFix.asi+0x5E5CC).
    if (index == 0 && !rt) return origSetRenderTarget ? origSetRenderTarget(dev, index, rt) : D3DERR_INVALIDCALL;
    if (!dead && index == 0)
    {
                // While the redirect is on, the back buffer is the mod's render-size target, whoever binds it.
        if (rsRedirectOn && rt && rt == backbufferSurf && rsRedirectSurf) rt = rsRedirectSurf;
        drawDev = dev;
                // The seam, target side: the post chain opens by binding an FP16 target smaller than the render size.
        if (!seamThisFrame && seamSceneWrote && rt && rt != sceneBufSurf)
        {
            // Output size, not render size: the post chain is output-relative, so at Performance its half-res target equals the
            // render size.
            const UINT rw = rsOutW, rh = rsOutH;
            for (int k = 0; k < fpTabN; k++)
            {
                if (fpTab[k].surf != rt) continue;
                // Single-level only: the 1024x1024 lv4 reflection target also passes the size test, and only frame order keeps
                // it out otherwise.
                if (fpTab[k].levels != 1) break;
                if (!rw || fpTab[k].w >= rw || fpTab[k].h >= rh) break;
                seamThisFrame = true; seamArmed = true;
                // The bridge: until the transport runs, the scene counterpart is copied into the game's buffer.
                if (fpScN && dev && sceneBufSurf && !IvtResolveWillRun())
                    for (int q = 0; q < fpScN; q++)
                        if (fpSc[q].ourSurf == sceneBufSurf && fpSc[q].gameSurf)
                        {
                            RsOwnCall guard;
                            if (SUCCEEDED(dev->StretchRect(fpSc[q].ourSurf, nullptr,
                                                           fpSc[q].gameSurf, nullptr, D3DTEXF_LINEAR)))
                                { }
                            else if (telemetryOn && fpScBridgeFailed++ < 4)
                                LogRecord("  designA: the bridge blit was REFUSED: the game's buffer is stale this frame");
                            break;
                        }

#if DLSSIV_DEVLOG
                if (censusOn && seamLogged < 3)
                {
                    seamLogged++;
                    LogDetail("  seam FOUND (frame %ld): after the scene wrote %p, the post chain"
                        " opened by binding %p (%ux%u fp16) at draw %d; %d backbuffer draws so far",
                        (long)frames, (void*)sceneBufTex, (void*)rt, fpTab[k].w, fpTab[k].h,
                        seamDrawIdx, seamBbSeen);
                }
#endif
                break;
            }
        }
                // A post-seam write into the game's original: whether the post chain writes one itself decides, in hkSetTexture,
        // whether a post-seam read of it may come to the counterpart.
        if (seamThisFrame && rt)
            if (FpScene* w = FpSceneForSurf(rt)) w->wroteOrig = (long)frames;
                // Write side, after the seam test. Not for the mod's own binds (the rain layer).
        if (FpSceneActive() && !rsOwn)
            if (FpScene* e = FpSceneGet(dev, rt))
            { rt = e->ourSurf; e->wroteOurs = (long)frames; }
        curRT0 = rt;                        // tracked only; the game owns the reference
        chvArmed = (chvDepth != nullptr && chvDsScaled && curRT0 == backbufferSurf);
        RsNoteBoundTarget(rt);              // DXVK is about to reset the viewport and scissor to its size
    }
    else if (index == 0)
    {
                // A stopped mod still tracks the bind: hkSetViewport, hkSetScissorRect and hkClear have no dead guard and scale against
        // curRT0Scaled. This also keeps slot 0 out of curRT123 below.
        curRT0 = rt;
        RsNoteBoundTarget(rt);
    }
    else if (index < 4)
    {
        curRT123[index - 1] = rt;                   // the shadow the velocity redraw restores from
                // The dither's mark: FusionFix writes it to oC2.w, so target 2 is where the dither pass reads it. Noted, never
        // referenced; IvtDedMask proves it at the injection.
        if (index == 2 && rt && !seamThisFrame)
        {
            ivtDedSeenSurf = rt; ivtDedSeenFrame = (LONG)frames;
        }
    }
    return origSetRenderTarget ? origSetRenderTarget(dev, index, rt) : D3DERR_INVALIDCALL;
}

static bool IvtSceneSeenThisFrame();   // transport/DlssIvTransport.h

// The engine's question, noted for the put-back that follows (ds_bracket.h). The mod's own brackets are not noted.
typedef HRESULT(__stdcall* GetDepthStencil_t)(IDirect3DDevice9*, IDirect3DSurface9**);
static GetDepthStencil_t origGetDepthStencil = nullptr;
static HRESULT __stdcall hkGetDepthStencil(IDirect3DDevice9* dev, IDirect3DSurface9** pp)
{
    DT_NOT_ON_WORKER(origGetDepthStencil(dev, pp));
    if (!origGetDepthStencil) return D3DERR_INVALIDCALL;
    if (dead || rsOwn || !pp || !RenderThreadHere()) return origGetDepthStencil(dev, pp);
    const LONG seq = DsBracketOpen();                   // BEFORE the question
    const HRESULT hr = origGetDepthStencil(dev, pp);
    DsAskedNote(SUCCEEDED(hr) ? *pp : nullptr, SUCCEEDED(hr) || hr == D3DERR_NOTFOUND, seq);
    return hr;
}

static HRESULT __stdcall hkSetDepthStencil(IDirect3DDevice9* dev, IDirect3DSurface9* ds)
{
    DT_NOT_ON_WORKER(origSetDepthStencil(dev, ds));
    if (!dead && !rsOwn && RenderThreadHere()) ds = DsStalePutBack(ds);   // FIRST, so everything below tracks what reaches the device
    if (!dead)
    {
        ds = IvtSubstituteDepthStencil(ds);   // the mod's INTZ in place of the scene depth, when that path is on
                // DXVK does not bind a depth-stencil smaller than target 0, so a draw loses its depth test; ChvBefore repairs the 3D
        // ones.
        if (!rsOwn) chvDsScaled = (ds != nullptr && RsFind(ds) != nullptr);
        const bool rsDepthLostNow = (rsActive && chvDsScaled && curRT0 == backbufferSurf);   // the depth-stencil bound now will be dropped
                // Render thread only: RsOwnCreateCall's counter is process-wide. Bound only inside ChvBefore.
        if (rsDepthLostNow && !chvDepth && !chvTried && rsOutW && rsOutH && RenderThreadHere())
        {
            chvTried = true;
            RsOwnCreateCall guard;              // the mod's own: never scaled, never tabled
            // INTZ FIRST, so the same memory can be sampled as well as bound (see chvDepthTex).
            if (SUCCEEDED(dev->CreateTexture(rsOutW, rsOutH, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_INTZ,
                        D3DPOOL_DEFAULT, &chvDepthTex, nullptr)) && chvDepthTex
                && SUCCEEDED(chvDepthTex->GetSurfaceLevel(0, &chvDepth)) && chvDepth)
                LogHealth("  chevron: %ux%u INTZ depth created, bindable and sampleable. Bound for the"
                    " duration of a single draw on draws whose own depth DXVK is dropping, and handed"
                    " to overlay draws whose own depth DXVK drops.", rsOutW, rsOutH);
            else
            {
                if (chvDepthTex) { chvDepthTex->Release(); chvDepthTex = nullptr; }
                chvDepth = nullptr;
                if (FAILED(dev->CreateDepthStencilSurface(rsOutW, rsOutH, D3DFMT_D24S8,
                            D3DMULTISAMPLE_NONE, 0, FALSE, &chvDepth, nullptr)) || !chvDepth)
                { chvDepth = nullptr; LogRecord("  chevron: could not create the %ux%u depth", rsOutW, rsOutH); }
                else
                    LogHealth("  chevron: %ux%u depth created (INTZ was refused, so it cannot be sampled:"
                        " overlay draws keep no depth at all, and a partial-viewport overlay such as the phone"
                        " will composite its own layers unoccluded).", rsOutW, rsOutH);
            }
        }
                // The precondition, hoisted: its three inputs change only in this hook and SetRenderTarget. rsActive is checked inside
        // ChvBefore, so a stale arm costs a rejected call, never a wrong bind.
        if (!rsOwn) chvArmed = (chvDepth != nullptr && chvDsScaled && curRT0 == backbufferSurf);
        if (!rsOwn)                         // the mod's binds are not the game's state
        {
            DEVSTAT(if (telemetryOn && ds != curDS && IvtSceneSeenThisFrame()) dsChangesAfterScene++;)
            curDS = ds;                     // tracked only; the game owns the reference
            curDSKnown = true;
        }
    }
    if (RenderThreadHere()) return origSetDepthStencil ? origSetDepthStencil(dev, ds) : D3DERR_INVALIDCALL;
        // Another thread's bind (the game's main thread; the worker left at the top), marked until DXVK returns so a bracket of the
    // mod's it lands inside puts this surface back: ds_bracket.h.
    DsForeignBegin(ds);
    const HRESULT hr = origSetDepthStencil ? origSetDepthStencil(dev, ds) : D3DERR_INVALIDCALL;
    DsForeignEnd(hr);
    return hr;
}

// StretchRect census: copies whose source is the back buffer, per frame, split by whether the injection had happened. The
// transport reads and resets the counts at EndScene.
static bool IvtInjectedThisFrame();   // transport/DlssIvTransport.h
typedef HRESULT(__stdcall* StretchRect_t)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
static StretchRect_t origStretchRect = nullptr;
typedef HRESULT(__stdcall* GetRenderTargetData_t)(IDirect3DDevice9*, IDirect3DSurface9*, IDirect3DSurface9*);
static GetRenderTargetData_t origGetRenderTargetData = nullptr;

// The absorb (hdr_cga.h): a read-back after a withheld draw gets the curve first.
static HRESULT __stdcall hkGetRenderTargetData(IDirect3DDevice9* dev, IDirect3DSurface9* src, IDirect3DSurface9* dst)
{
    DT_NOT_ON_WORKER(origGetRenderTargetData(dev, src, dst));
        // The render thread's alone: the heal uses render-thread state.
    if (renderTid && GetCurrentThreadId() != renderTid)
        return origGetRenderTargetData ? origGetRenderTargetData(dev, src, dst) : D3DERR_INVALIDCALL;
    if (hdrCgaHeld && src == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_READ);
    return origGetRenderTargetData ? origGetRenderTargetData(dev, src, dst) : D3DERR_INVALIDCALL;
}

// Maps the game's output-pixel rectangle onto a render-size counterpart; false when it cannot, and the blit is left alone.
static bool FpScaleRectOnto(IDirect3DSurface9* ourSurf, const RECT* in, RECT* out)
{
    if (!in || !out) return false;
    const RsSurf* e = RsFind(ourSurf);
    if (!e || !e->origW || !e->origH || !e->newW || !e->newH) return false;
    UINT x = 0, w = 0, y = 0, h = 0;
    RsScaleSpan((UINT)in->left, (UINT)(in->right - in->left), e->origW, e->newW, &x, &w);
    RsScaleSpan((UINT)in->top,  (UINT)(in->bottom - in->top), e->origH, e->newH, &y, &h);
    out->left = (LONG)x; out->right  = (LONG)(x + w);
    out->top  = (LONG)y; out->bottom = (LONG)(y + h);
    return true;
}

static HRESULT __stdcall hkStretchRect(IDirect3DDevice9* dev, IDirect3DSurface9* src, const RECT* srcRect,
                                       IDirect3DSurface9* dst, const RECT* dstRect, D3DTEXTUREFILTERTYPE filter)
{
    DT_NOT_ON_WORKER(origStretchRect(dev, src, srcRect, dst, dstRect, filter));
    IDirect3DSurface9* rbAlsoGame = nullptr;   // a module's render-size target, written after the real copy (below)
        // Not from another thread: every clause below is about the game's blits and keeps the render thread's per-frame state. A
    // second thread goes straight to the original.
    if (renderTid && GetCurrentThreadId() != renderTid)
        return origStretchRect ? origStretchRect(dev, src, srcRect, dst, dstRect, filter) : D3DERR_INVALIDCALL;
        // The absorb (hdr_cga.h): a copy touching the back buffer after a withheld draw gets the curve first; a foreign point copy
    // of the whole back buffer arms it.
    if (!dead && !rsOwn && !ivtInOwnDraw)
    {
                // the armed product in SDR (fg/fg_hud.h): a point copy of the whole back buffer by another module (FusionFix's console
        // gamma) is noted
        FGA(if (fgSdrWatch && src && src == backbufferSurf && !srcRect && !dstRect && dst && filter == D3DTEXF_POINT) FgHudSdrCgaArm(dev, _ReturnAddress(), dst);)
        if (hdrCgaHeld && (src == backbufferSurf || dst == backbufferSurf)) HdrCgaHeal(dev, CGA_HEAL_STRETCH);
        else if (hdrCgaWant && !hdrCgaArmed && src == backbufferSurf && !srcRect && !dstRect && dst && dst != hdrSurf && dst != hdrPreHudSurf && filter == D3DTEXF_POINT)
            HdrCgaArm(dst, _ReturnAddress());
    }
        // Sharpen before the curve on a frame with no HUD: FusionFix's console gamma point-copies the whole back buffer, the first
    // event after the post chain there. A foreign caller, the whole buffer, a point filter.
    if (shpOn && !SHP_G10() && !shpDone && seamThisFrame && shpBbSeen && !dead && !rsOwn && !ivtInOwnDraw
        && src && src == backbufferSurf && !srcRect && !dstRect && dst && filter == D3DTEXF_POINT
        && ShpCallerIsForeign(_ReturnAddress()))
    {
        shpDone = true;          // BEFORE the pass: its own copy of the back buffer comes through this hook
        ShpSharpen(dev);
        DEVSTAT(if (telemetryOn) { shpAtCopy++; })
    }
    if (!dead)
    {
        if (src && src == backbufferSurf)
        {
            // Two tests: the counters are report-only, but IvtOnBackbufferReadback runs the resolve and must run at every log
            // level.
            const bool injected = IvtInjectedThisFrame();
            if (!injected) IvtOnBackbufferReadback();   // the resolve goes in before the game reads it back
                                                        // The game's read-backs only: the sharpen's and the encode's copies come through here too. The call above is not
            // gated this way; it returns for those because IvtTick has already zeroed ivtSceneDraws.
            DEVSTAT(if (telemetryOn && !rsOwn && !ivtInOwnDraw) { if (injected) srBbAfterInject++; else srBbBeforeInject++; })
            // A read-back gets a screen-size destination of the mod's, or the resolved image is shrunk again. Another module's
            // whole-frame copy (FusionFix's console gamma) gets one on any frame, its own target written too (hkSetTexture's
            // rsRb rule).
            const bool rbModuleCopy = rsActive && dst && !srcRect && !dstRect && !rsOwn && !ivtInOwnDraw && ShpCallerIsForeign(_ReturnAddress());
            const bool rbPausePath  = rsActive && dst && IvtInjectedThisFrame() && rsRbDoneFrame != frames;
            if (rbPausePath || rbModuleCopy)
            {
                RsSurf* e = RsFind(dst);
                // Does the read-back's rsTab lookup ever land on one of OURS?
                if (e && fpScN)
                    for (int q = 0; q < fpScN; q++)
                        if (fpSc[q].ourSurf == e->surf || fpSc[q].ourTex == e->tex)
                        {
                            DEVSTAT(if (telemetryOn) { fpScRbHitOurs++; })
                            LogDetail("  designA: THE READ-BACK REDIRECT RESOLVED TO THE MOD'S COUNTERPART %d"
                                "   <-- it is treating the mod's buffer as the game's screen copy", q + 1);
                        }
                IDirect3DSurface9* ours = e ? RsReadbackTarget(dev, e) : nullptr;
                if (ours)
                {
                    if (!rbPausePath)
                    {
                        rbAlsoGame = dst;                // written as well after the real copy: the module's target stays current
                        static bool saidModuleCopy = false;
                        if (!saidModuleCopy)
                        {
                            saidModuleCopy = true;
                            LogRecord("  render scale: another module copies the frame into a render-size target (%p): the mod's copy used", (void*)dst);
                        }
                    }
                    dst = ours;
                }
                // Stamped whenever the table matched, so a refused target is not retried per blit. The game's read-backs only:
                // a module's copy never takes the frame's one pause-path slot.
                if (e && rbPausePath) rsRbDoneFrame = frames;
            }
        }
        else { DEVSTAT(if (telemetryOn) srOther++;) }   // report-only: the transport's period sum is its only reader
    }
        // A copy of the back buffer into a twinned target (FusionFix's console gamma under hdr=1) is the finished frame: it stays
    // on the game's original.
    FpScene* const fpFrameCopy = (fpScN && dst && src && src == backbufferSurf && !srcRect && !dstRect && !rsOwn && !ivtInOwnDraw)
                               ? FpSceneForSurf(dst) : nullptr;
    const bool fpKeepOrig = fpFrameCopy && fpFrameCopy->ourTex != sceneBufTex && ShpCallerIsForeign(_ReturnAddress());
    if (fpKeepOrig)
    {
        fpFrameCopy->wroteOrig = (long)frames;
        static bool said = false;
        if (!said)
        {
            said = true;
            LogRecord("  designA: another module copies the finished frame into a twinned target (%p): left on the game's original", (void*)dst);
        }
    }
        // A post-seam write into the game's original by a blit, noted as in hkSetRenderTarget (see hkSetTexture). The mod's own
    // blits (the seam bridge) are not the game's writes.
    if (seamThisFrame && dst && !rsOwn)
        if (FpScene* w = FpSceneForSurf(dst)) w->wroteOrig = (long)frames;
        // The mod's counterparts, at any time in the frame.
    if (fpScN && (src || dst))
    {
        FpScene* qs = src ? FpSceneForOurSurf(src) : nullptr;
        FpScene* qd = dst ? FpSceneForOurSurf(dst) : nullptr;
        if (qs || qd)
        {
            DEVSTAT(if (telemetryOn) { fpScBlitOurs++; })
#if DLSSIV_DEVLOG
            if (fpScBlitLogged < 10)
            {
                fpScBlitLogged++;
                LogDetail("  designA: A BLIT TOUCHED ONE OF THE MOD'S COUNTERPARTS #%ld: src %p%s, dst %p%s,"
                    " seam passed %d, injected %d, frame %ld   <-- nothing should copy to or from them",
                    fpScBlitLogged, (void*)src, qs ? " (THE MOD'S)" : "", (void*)dst, qd ? " (THE MOD'S)" : "",
                    seamThisFrame ? 1 : 0, IvtInjectedThisFrame() ? 1 : 0, (long)frames);
            }
#endif
        }
    }
        // Game blits into and out of a twinned FP16 buffer go to the counterpart, before the first injection. Not the mod's own
    // blits.
    RECT fpSrcR = { 0, 0, 0, 0 }, fpDstR = { 0, 0, 0, 0 };
    if (FpSceneActive() && ivtInjections == 0 && !rsOwn && !ivtInOwnDraw)
    {
        if (dst && !fpKeepOrig)                                     // another module's copy of the finished frame stays (above)
        {
            if (FpScene* e = FpSceneForSurf(dst))
            {
                if (!dstRect)                                        { dst = e->ourSurf; e->wroteOurs = (long)frames; DEVSTAT(if (telemetryOn) { fpScBlitDstSubs++; }) }
                else if (FpScaleRectOnto(e->ourSurf, dstRect, &fpDstR)) { dst = e->ourSurf; e->wroteOurs = (long)frames; dstRect = &fpDstR; DEVSTAT(if (telemetryOn) { fpScBlitDstSubs++; }) }
                else                                                 { DEVSTAT(if (telemetryOn) { fpScBlitSkipped++; }) }
            }
        }
        if (src)
        {
            if (FpScene* e = FpSceneForSurf(src))
            {
                if (!srcRect)                                        { src = e->ourSurf; DEVSTAT(if (telemetryOn) { fpScBlitSrcSubs++; }) }
                else if (FpScaleRectOnto(e->ourSurf, srcRect, &fpSrcR)) { src = e->ourSurf; srcRect = &fpSrcR; DEVSTAT(if (telemetryOn) { fpScBlitSrcSubs++; }) }
                else                                                 { DEVSTAT(if (telemetryOn) { fpScBlitSkipped++; }) }
            }
        }
    }
        // A blit into the back buffer: a cross-format blit into an FP16 back buffer can fail silently and leave it empty, so a
    // failure says so, five times.
    const bool toBb = (!dead && dst && dst == backbufferSurf);
    const HRESULT srHr = origStretchRect ? origStretchRect(dev, src, srcRect, dst, dstRect, filter)
                                         : D3DERR_INVALIDCALL;
        // The module's own render-size target is written too, for later readers.
    if (rbAlsoGame && SUCCEEDED(srHr) && origStretchRect) origStretchRect(dev, src, nullptr, rbAlsoGame, nullptr, filter);
    if (toBb && FAILED(srHr))
    {
        {
            static int saidBlit = 0;
            if (saidBlit++ < 5)
            {
                D3DSURFACE_DESC sd = {};
                if (src) src->GetDesc(&sd);
                LogRecord("  hdr: StretchRect into the back buffer FAILED 0x%08X (source format %d %ux%u)",
                    (unsigned)srHr, (int)sd.Format, sd.Width, sd.Height);
            }
        }
    }
    return srHr;
}

// hkCreateDepthStencilSurface and hkCreateRenderTarget: pass-throughs that feed MemNote's byte census (a development-log reader
// only).
typedef HRESULT(__stdcall* CreateDepthStencilSurface_t)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
static CreateDepthStencilSurface_t origCreateDepthStencilSurface = nullptr;

static HRESULT __stdcall hkCreateDepthStencilSurface(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt,
                                                     D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL discard,
                                                     IDirect3DSurface9** out, HANDLE* sh)
{
    DT_NOT_ON_WORKER(origCreateDepthStencilSurface(dev, w, h, fmt, ms, msq, discard, out, sh));
    HRESULT hr = origCreateDepthStencilSurface ? origCreateDepthStencilSurface(dev, w, h, fmt, ms, msq, discard, out, sh) : D3DERR_INVALIDCALL;
    if (SUCCEEDED(hr)) MemNote(w, h, 1, fmt, rsOwnCreate > 0);
    return hr;
}

typedef HRESULT(__stdcall* CreateTexture_t)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
typedef HRESULT(__stdcall* CreateRenderTarget_t)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
typedef HRESULT(__stdcall* SetViewport_t)(IDirect3DDevice9*, const D3DVIEWPORT9*);
static CreateTexture_t      origCreateTexture      = nullptr;
static CreateRenderTarget_t origCreateRenderTarget = nullptr;
DLSSIV_HOT static SetViewport_t        origSetViewport        = nullptr;

static HRESULT __stdcall hkCreateTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt,
                                         D3DPOOL pool, IDirect3DTexture9** out, HANDLE* sh)
{
    DT_NOT_ON_WORKER(origCreateTexture(dev, w, h, levels, usage, fmt, pool, out, sh));
        // The scale itself: the game's screen-size (half, quarter) targets are created at render size. Every target arrives here.
    const UINT askedW = w, askedH = h;
    bool scaled = false;
    const bool fp16rt = (usage & D3DUSAGE_RENDERTARGET) && pool == D3DPOOL_DEFAULT
                        && fmt == D3DFMT_A16B16G16R16F;
    const bool target = (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0;
        // FP16 targets are never scaled: they are the post chain's, which runs at output size. The scene's FP16 target is
    // substituted.
    if (rsActive && !rsOwnCreate && !dead && target && !fp16rt && pool == D3DPOOL_DEFAULT && rsOutW && w && h
        && (ivtCfg.rs_ds || !(usage & D3DUSAGE_DEPTHSTENCIL)))
    {
        if      (w == rsOutW     && h == rsOutH)     { w = rsW;     h = rsH;     scaled = true; }
        else if (w == rsOutW / 2 && h == rsOutH / 2) { w = rsW / 2; h = rsH / 2; scaled = true; }
        else if (w == rsOutW / 4 && h == rsOutH / 4) { w = rsW / 4; h = rsH / 4; scaled = true; }
    }
    HRESULT hr = origCreateTexture ? origCreateTexture(dev, w, h, levels, usage, fmt, pool, out, sh) : D3DERR_INVALIDCALL;
        // Sized from what was created, after the scale.
    if (SUCCEEDED(hr)) MemNote(w, h, levels, fmt, rsOwnCreate > 0);
        // FP16 targets are paired, texture with surface. A full table says so once: an unpaired scene buffer never fires the seam.
    if (fp16rt && !rsOwnCreate && SUCCEEDED(hr) && out && *out && fpTabN >= 24)
    {
        static bool said = false;
        if (!said) { said = true; LogRecord("  seam: the FP16 render-target table is full (24): a %ux%u target was not paired", w, h); }
    }
    if (fp16rt && !rsOwnCreate && SUCCEEDED(hr) && out && *out && fpTabN < 24)
    {
        IDirect3DSurface9* sf = nullptr;
        if (SUCCEEDED((*out)->GetSurfaceLevel(0, &sf)) && sf)
        {
            sf->Release();      // a surface lives as long as its texture, which the table holds
            AcquireSRWLockExclusive(&rsTablesLock);
            const int fn = fpTabN;              // asked again under the lock: another thread may have filled the table since the test above
            if (fn < 24)
            {
                (*out)->AddRef();   // one per entry, returned in hkReset: see fpTab's declaration
                FpRt& e = fpTab[fn];
                e.tex = *out; e.surf = sf; e.w = w; e.h = h; e.levels = (*out)->GetLevelCount();
                _ReadWriteBarrier(); fpTabN = fn + 1;   // the count LAST: see rsTablesLock
            }
            ReleaseSRWLockExclusive(&rsTablesLock);
            LogDetail("  seam: FP16 render target #%d  tex %p surf %p  %ux%u lv%u", fn + 1,
                (void*)*out, (void*)sf, w, h, (*out)->GetLevelCount());
        }
    }
    if (scaled && SUCCEEDED(hr) && out && *out)
    {
        DEVSTAT(if (telemetryOn) { InterlockedIncrement(&rsScaledCreations); })
                // Every matching target scales, whoever asks. A shrunk depth texture is noted (chvDepthTex) only while rsTab holds it.
        AcquireSRWLockExclusive(&rsTablesLock);  // one region for this texture's rsIntz slot and its rsTab entries
        if ((usage & D3DUSAGE_DEPTHSTENCIL) && rsIntzN < 8 && rsTabN < 128)
        {
            rsIntz[rsIntzN] = *out; _ReadWriteBarrier(); rsIntzN = rsIntzN + 1;
            LogDetail("  render scale: the game's %ux%u depth texture (format %u) was shrunk to %ux%u"
                " and is tracked as #%d: a draw that samples it during the overlay phase keeps the"
                " game's own depth-stencil", askedW, askedH, (unsigned)fmt, w, h, rsIntzN);
        }
        const UINT lv = (*out)->GetLevelCount();
        bool held = false;
        for (UINT k = 0; k < lv && k < 6; k++)
        {
            IDirect3DSurface9* sf = nullptr;
            if (FAILED((*out)->GetSurfaceLevel(k, &sf)) || !sf) break;
            sf->Release();               // a surface lives as long as its texture, which the table holds
            if (rsTabN < 128)
            {
                RsSurf& e = rsTab[rsTabN];
                e.tex = *out; e.surf = sf;
                e.origW = (askedW >> k) ? (askedW >> k) : 1; e.origH = (askedH >> k) ? (askedH >> k) : 1;
                e.newW  = (w >> k) ? (w >> k) : 1;           e.newH  = (h >> k) ? (h >> k) : 1;
                e.held  = !held;
                if (!held) { (*out)->AddRef(); held = true; }   // one reference per texture, returned at the reset
                _ReadWriteBarrier(); rsTabN = rsTabN + 1;       // the count LAST: see rsTablesLock
            }
            else { DEVSTAT(if (telemetryOn) { InterlockedIncrement(&rsTabOverflow); }) }
        }
        ReleaseSRWLockExclusive(&rsTablesLock);
    }
    return hr;
}

static HRESULT __stdcall hkCreateRenderTarget(IDirect3DDevice9* dev, UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms,
                                              DWORD msq, BOOL lockable, IDirect3DSurface9** out, HANDLE* sh)
{
    DT_NOT_ON_WORKER(origCreateRenderTarget(dev, w, h, fmt, ms, msq, lockable, out, sh));
    HRESULT hr = origCreateRenderTarget ? origCreateRenderTarget(dev, w, h, fmt, ms, msq, lockable, out, sh) : D3DERR_INVALIDCALL;
    if (SUCCEEDED(hr)) MemNote(w, h, 1, fmt, rsOwnCreate > 0);
    return hr;
}

static HRESULT __stdcall hkSetViewport(IDirect3DDevice9* dev, const D3DVIEWPORT9* vp)
{
    DT_NOT_ON_WORKER(origSetViewport(dev, vp));
        // A game viewport is in screen pixels; on a scaled target DXVK would crop, so it is scaled here. Not for the mod's own
    // calls or an echo of a rectangle the mod produced.
    D3DVIEWPORT9 sv;
    if (rsActive && vp && !rsOwn && curRT0)
    {
        // The cache, not a table scan: hkSetRenderTarget already answered this.
        const RsSurf* e = curRT0Scaled ? RsFind(curRT0) : nullptr;
        // An echo of the rectangle DXVK already holds is in target pixels, not
        // screen pixels, so it must not be scaled a second time.
        const bool echo = rsHaveDxvkVp && vp->X == rsDxvkVp.X && vp->Y == rsDxvkVp.Y
                          && vp->Width == rsDxvkVp.Width && vp->Height == rsDxvkVp.Height;
        DEVSTAT(if (telemetryOn) { if (e && echo) rsVpEchoes++; })
                // What the game asks for while it believes it is on the back buffer is what the back buffer gets back when the redirect
        // ends; an echo (target pixels) must not overwrite it.
        if (rsRedirectOn && curRT0 == rsRedirectSurf && !echo) rsGameVp = *vp;
        if (e && !echo)
        {
            sv = *vp;
            UINT x = 0, w = 0, y = 0, h = 0;   // D3DVIEWPORT9 is DWORD-typed; UINT* would not bind
            RsScaleSpan(vp->X, vp->Width,  e->origW, e->newW, &x, &w);
            RsScaleSpan(vp->Y, vp->Height, e->origH, e->newH, &y, &h);
            sv.X = x; sv.Width = w; sv.Y = y; sv.Height = h;
            vp = &sv;
            DEVSTAT(if (telemetryOn) { rsVpScaled++; })
        }
    }
    // Whatever reaches DXVK is what DXVK then holds, ours and the game's alike.
    if (rsActive && vp) { rsDxvkVp = *vp; rsHaveDxvkVp = true; }
    return origSetViewport ? origSetViewport(dev, vp) : D3DERR_INVALIDCALL;
}

typedef HRESULT(__stdcall* SetScissorRect_t)(IDirect3DDevice9*, const RECT*);
DLSSIV_HOT static SetScissorRect_t origSetScissorRect = nullptr;

static HRESULT __stdcall hkSetScissorRect(IDirect3DDevice9* dev, const RECT* r)
{
    DT_NOT_ON_WORKER(origSetScissorRect(dev, r));
    RECT sr;
    if (rsActive && r && !rsOwn && curRT0 && r->left >= 0 && r->top >= 0 && r->right >= r->left && r->bottom >= r->top)
    {
        const RsSurf* e = curRT0Scaled ? RsFind(curRT0) : nullptr;
        const bool echo = rsHaveDxvkSc && r->left == rsDxvkSc.left && r->top == rsDxvkSc.top
                          && r->right == rsDxvkSc.right && r->bottom == rsDxvkSc.bottom;
        DEVSTAT(if (telemetryOn) { if (e && echo) rsScEchoes++; })
        if (e && !echo)
        {
            UINT x, w, y, h;
            RsScaleSpan((UINT)r->left, (UINT)(r->right - r->left), e->origW, e->newW, &x, &w);
            RsScaleSpan((UINT)r->top,  (UINT)(r->bottom - r->top), e->origH, e->newH, &y, &h);
            sr.left = (LONG)x; sr.right = (LONG)(x + w); sr.top = (LONG)y; sr.bottom = (LONG)(y + h);
            r = &sr;
            DEVSTAT(if (telemetryOn) { rsScissorScaled++; })
        }
    }
    if (rsActive && r) { rsDxvkSc = *r; rsHaveDxvkSc = true; }
    return origSetScissorRect ? origSetScissorRect(dev, r) : D3DERR_INVALIDCALL;
}

// The pause path's redraw quad samples the texture it just read the back buffer into; for that frame it gets the mod's
// screen-size copy instead of the scaled one.
static HRESULT __stdcall hkSetTexture(IDirect3DDevice9* dev, DWORD stage, IDirect3DBaseTexture9* tex)
{
    DT_NOT_ON_WORKER(origSetTexture(dev, stage, tex));
        // The steady depth (steady_depth; ivtSd): behind the seam, counts the post chain's binds of the scene depth textures. Never
    // the mod's own passes.
    if (seamThisFrame && tex && !ivtInOwnDraw && !rsOwn) IvtSdBind(stage, tex);
    if (stage == D3DVERTEXTEXTURESAMPLER0 && !ivtInOwnDraw && !rsOwn) vtf0Tex = tex;   // what the corona's depth samples read (corona_hold)
        // After the resolve, the scene buffer's binds get the resolved texture (render scale only). Other counterparts are read
    // from the side that wrote last (wroteOurs, wroteOrig).
    if (tex && rsActive)
    {
        if (FpScene* e = FpSceneForTex(tex))
        {
            const bool isScene  = (e->ourTex == sceneBufTex);
            const bool seamRule = !seamThisFrame;
            const bool lastRule = (e->wroteOurs > e->wroteOrig);      // equal: the post chain wrote later in the frame
            const bool useOurs  = isScene ? seamRule : lastRule;   // the read rule of design A
            if (useOurs)
            {
                tex = e->ourTex;
            }
        }
    }
    if (rsActive && stage == 0) rsScreenTexKind = 0;
    if (rsRbN && tex && rsActive)
        for (int i = 0; i < rsRbN; i++)
        {
            if (rsRb[i].gameTex == tex && rsRb[i].frame != frames)
            {
                if (stage == 0) rsScreenTexKind = 2;
                // The game is sampling its own copy of the screen, which the substitution stopped writing: non-zero means a
                // stale frame is composited over the resolve.
                DEVSTAT(if (telemetryOn) { rsStaleBinds++; })
#if DLSSIV_DEVLOG
                if (rsStaleBindLogged < 6)
                {
                    rsStaleBindLogged++;
                    LogHealth("  STALE CAPTURE BIND #%ld: the game sampled its own screen copy %ld frame(s) after the mod"
                        " last wrote its copy, stage %lu, target is %s", rsStaleBindLogged,
                        (long)(frames - rsRb[i].frame), stage,
                        curRT0 == backbufferSurf ? "the real backbuffer" : "something else");
                }
#endif
                break;
            }
            if (rsRb[i].gameTex == tex && rsRb[i].frame == frames)
            {
                if (stage == 0) rsScreenTexKind = 1;
                tex = rsRb[i].ours;
                // B. Who is sampling the screen capture, and onto what?
#if DLSSIV_DEVLOG
                if (rsTexSubLogged < 8)
                {
                    rsTexSubLogged++;
                    LogDetail("  CAPTURE SAMPLED #%ld: stage %lu, target is %s, injected %d, draw %d, frame %ld",
                        rsTexSubLogged, stage,
                        curRT0 == backbufferSurf ? "the real backbuffer" :
                        (rsRedirectSurf && curRT0 == rsRedirectSurf) ? "the mod's render-size target" : "something else",
                        IvtInjectedThisFrame() ? 1 : 0, ivtDrawIndexForLog, (long)frames);
                }
#endif
                break;
            }
        }
    return origSetTexture ? origSetTexture(dev, stage, tex) : D3DERR_INVALIDCALL;
}

// Added to the game's own bias, not substituted. No profiler bracket on so hot a hook. LodApplyAll writes through the original.
static HRESULT __stdcall hkSetSamplerState(IDirect3DDevice9* dev, DWORD stage,
                                           D3DSAMPLERSTATETYPE type, DWORD value)
{
    DT_NOT_ON_WORKER(origSetSamplerState(dev, stage, type, value));
    if (type == D3DSAMP_MIPMAPLODBIAS && lodActive && !dead && !ivtInOwnDraw && stage < 16)
    {
        float asked = 0.0f;
        memcpy(&asked, &value, sizeof(asked));
        DEVSTAT(if (telemetryOn) { lodGameSets++; })
        const float sum = asked + lodBias;
        memcpy(&value, &sum, sizeof(value));
    }
    else if (type == D3DSAMP_SRGBTEXTURE && stage == 0) hdrSrgbTex0 = (value != 0);   // see hkDrawPrim
    return origSetSamplerState ? origSetSamplerState(dev, stage, type, value) : D3DERR_INVALIDCALL;
}

// The address space census: the largest free block is what fails a 32-bit streamer. The walk runs on a worker (MemCensus).
static void MemCensusWalk(const char* when, UINT avail)
{
        // log>=2: every line below is LogHealth. MemCensus has the same gate.
    if (!DLSSIV_DEVLOG || logLevel < 2) return;
    LARGE_INTEGER w0 = {}, w1 = {};
    QueryPerformanceCounter(&w0);
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    const char* p   = (const char*)si.lpMinimumApplicationAddress;
    const char* end = (const char*)si.lpMaximumApplicationAddress;
    UINT64 freeTotal = 0, commit = 0, reserve = 0, largest = 0;
    LONG nFree = 0, n256 = 0, n64 = 0, n16 = 0, n4 = 0;
        // Attribution: committed bytes by type, the largest committed regions with their modules, and the regions either side of
    // the largest free block, to name what split the space.
    UINT64 cImage = 0, cMapped = 0, cPrivate = 0;
    const char* largestBase = nullptr;
    struct Big { const char* base; UINT64 size; DWORD type; };
    Big big[6] = {};
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        const UINT64 sz = (UINT64)mbi.RegionSize;
        if (!sz) break;                       // cannot advance; stop rather than spin
        if (mbi.State == MEM_FREE)
        {
            freeTotal += sz; nFree++;
            if (sz > largest) { largest = sz; largestBase = p; }
            if (sz >= (256ull << 20)) n256++;
            if (sz >= ( 64ull << 20)) n64++;
            if (sz >= ( 16ull << 20)) n16++;
            if (sz >= (  4ull << 20)) n4++;
        }
        else if (mbi.State == MEM_COMMIT)
        {
            commit += sz;
            if      (mbi.Type == MEM_IMAGE)  cImage  += sz;
            else if (mbi.Type == MEM_MAPPED) cMapped += sz;
            else                             cPrivate += sz;
            // keep the six largest committed regions
            int slot = -1; UINT64 smallest = ~0ull;
            for (int i = 0; i < 6; i++) if (big[i].size < smallest) { smallest = big[i].size; slot = i; }
            if (slot >= 0 && sz > smallest) { big[slot].base = p; big[slot].size = sz; big[slot].type = mbi.Type; }
        }
        else reserve += sz;
        p += sz;
    }
    // The module behind a region, when it is an image or a mapping. K32GetMappedFileNameA
    // lives in kernel32 since Windows 7, so no import library is needed.
    typedef DWORD (WINAPI* MappedName_t)(HANDLE, LPVOID, LPSTR, DWORD);
    static MappedName_t mappedName = nullptr; static bool mappedNameTried = false;
    if (!mappedNameTried) { mappedNameTried = true; HMODULE k = GetModuleHandleA("kernel32.dll"); if (k) mappedName = (MappedName_t)GetProcAddress(k, "K32GetMappedFileNameA"); }
    auto NameOf = [&](const void* base, DWORD type, char* out, size_t cap)
    {
        out[0] = 0;
        if ((type == MEM_IMAGE || type == MEM_MAPPED) && mappedName)
        {
            char full[MAX_PATH] = {};
            if (mappedName(GetCurrentProcess(), (LPVOID)base, full, MAX_PATH))
            {
                const char* fn = strrchr(full, '\\'); fn = fn ? fn + 1 : full;
                _snprintf_s(out, cap, _TRUNCATE, "%s", fn);
                return;
            }
        }
        _snprintf_s(out, cap, _TRUNCATE, type == MEM_IMAGE ? "image" : type == MEM_MAPPED ? "mapping" : "private");
    };
    char bigTxt[400] = {}; int bn = 0;
    // sort the six by size, largest first (six entries: a plain insertion sort)
    for (int i = 1; i < 6; i++) { Big t = big[i]; int j = i - 1; while (j >= 0 && big[j].size < t.size) { big[j + 1] = big[j]; j--; } big[j + 1] = t; }
    for (int i = 0; i < 6 && big[i].size && bn >= 0 && bn < (int)sizeof(bigTxt); i++)
    {
        char nm[64]; NameOf(big[i].base, big[i].type, nm, sizeof(nm));
        bn += _snprintf_s(bigTxt + bn, sizeof(bigTxt) - bn, _TRUNCATE, "%s%s %llu MB", i ? ", " : "", nm, big[i].size >> 20);
    }
    char before[64] = "?", after[64] = "?";
    if (largestBase)
    {
        MEMORY_BASIC_INFORMATION nb;
        if (largestBase > (const char*)si.lpMinimumApplicationAddress && VirtualQuery(largestBase - 1, &nb, sizeof(nb)) == sizeof(nb))
            NameOf(nb.AllocationBase ? nb.AllocationBase : nb.BaseAddress, nb.Type, before, sizeof(before));
        if (VirtualQuery(largestBase + largest, &nb, sizeof(nb)) == sizeof(nb) && nb.State != MEM_FREE)
            NameOf(nb.AllocationBase ? nb.AllocationBase : nb.BaseAddress, nb.Type, after, sizeof(after));
    }
    LogHealth("  mem (%s): address space FREE %llu MB in %ld regions, LARGEST FREE BLOCK %llu MB"
        " (blocks at or above 256/64/16/4 MB: %ld/%ld/%ld/%ld); committed %llu MB, reserved %llu MB",
        when, freeTotal >> 20, nFree, largest >> 20, n256, n64, n16, n4,
        commit >> 20, reserve >> 20);
    LogHealth("  mem (%s): committed by type: image %llu MB, mapped %llu MB, private %llu MB; the largest free block sits between"
        " %s and %s; largest committed regions: %s",
        when, cImage >> 20, cMapped >> 20, cPrivate >> 20, before, after, bigTxt);
        // Created since launch: nothing is subtracted on release, so this is a creation census, not a footprint. See MemNote for
    // what it cannot see.
    LogHealth("  mem (%s): the game's streaming budget (GetAvailableTextureMem) reads %u MB."
        " D3D resources CREATED SINCE LAUNCH (a cumulative count, never decremented on release,"
        " and blind to vertex and index buffers, cube and volume textures and offscreen surfaces):"
        " THE MOD'S %ld totalling %llu MB, the game's %ld"
        " totalling %llu MB, %ld in formats this cannot size%s",
        when, avail >> 20, memOursN, memOursBytes >> 20, memGameN, memGameBytes >> 20,
        memUnsized,
        (largest < (64ull << 20)) ? "   <-- THE LARGEST FREE BLOCK IS UNDER 64 MB: the streamer is near the wall"
                                  : "");
        // The walk's own cost, on the worker, so it can be checked that it stays out of a frame.
    QueryPerformanceCounter(&w1);
    if (!qpcFreq.QuadPart) QueryPerformanceFrequency(&qpcFreq);
    if (qpcFreq.QuadPart)
        LogHealth("  mem (%s): the walk took %.0f us on a worker thread, off the render thread", when,
            (double)(w1.QuadPart - w0.QuadPart) * 1e6 / (double)qpcFreq.QuadPart);
}

// The job the worker owns. Copied rather than referenced: the render thread
// returns to the frame the moment this is handed over.
struct MemCensusJob { char when[40]; UINT avail; };
static LONG memCensusBusy = 0;

static DWORD WINAPI MemCensusThreadProc(LPVOID p)
{
    MemCensusJob* j = (MemCensusJob*)p;
    MemCensusWalk(j->when, j->avail);
    HeapFree(GetProcessHeap(), 0, j);
    InterlockedExchange(&memCensusBusy, 0);
    return 0;
}

// Logging from the worker is safe (LogEmitV's named mutex). Detached: waiting in DLL_PROCESS_DETACH would deadlock.
static void MemCensus(IDirect3DDevice9* dev, const char* when)
{
    if (!DLSSIV_DEVLOG || logLevel < 2) return;                 // see MemCensusWalk: nothing here prints below log=2
    const UINT avail = dev ? dev->GetAvailableTextureMem() : 0;
        // Never two at once: a walk that outlives the repeat interval would otherwise stack; the skip is said.
    if (InterlockedCompareExchange(&memCensusBusy, 1, 0) != 0)
    {
        LogHealth("  mem (%s): the previous census is still walking, so this one is SKIPPED"
            " rather than stacked on top of it", when);
        return;
    }
    MemCensusJob* j = (MemCensusJob*)HeapAlloc(GetProcessHeap(), 0, sizeof(MemCensusJob));
    HANDLE t = nullptr;
    if (j)
    {
        _snprintf_s(j->when, sizeof(j->when), _TRUNCATE, "%s", when);
        j->avail = avail;
        t = CreateThread(nullptr, 0, MemCensusThreadProc, j, 0, nullptr);
    }
    if (t) { CloseHandle(t); return; }
        // Falls back to this thread rather than losing the census: a failed thread creation is a memory-pressure signal, when the
    // reading is worth most.
    if (j) HeapFree(GetProcessHeap(), 0, j);
    InterlockedExchange(&memCensusBusy, 0);
    LogHealth("  mem (%s): no worker thread could be started, so the walk runs here and costs a frame", when);
    MemCensusWalk(when, avail);
}

// ColorFill gets the blit path's rules: a fill of a twinned game buffer goes to the counterpart (before the first injection),
// and rectangles on shrunk surfaces are mapped as hkClear maps them.
static HRESULT __stdcall hkColorFill(IDirect3DDevice9* dev, IDirect3DSurface9* surf,
                                     const RECT* rect, D3DCOLOR colour)
{
    DT_NOT_ON_WORKER(origColorFill(dev, surf, rect, colour));
    if (hdrCgaHeld && surf == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_FILL);   // the absorb's guard (hdr_cga.h)
    RECT mapped = { 0, 0, 0, 0 };
    if (!dead && surf && !rsOwn && !ivtInOwnDraw)
    {
                // Said once, at log=2: whether the game calls ColorFill at all (in long sessions it has not).
        {
            static bool saidFirst = false;
            if (!saidFirst)
            {
                saidFirst = true;
                D3DSURFACE_DESC d = {};
                surf->GetDesc(&d);
                LogHealth("  ColorFill: the game uses it. First call %ux%u fmt %u onto %s,"
                    " rectangle %s. This write path was unwatched until now.",
                    d.Width, d.Height, (unsigned)d.Format,
                    (surf == backbufferSurf) ? "THE BACK BUFFER" : "one of its own surfaces",
                    rect ? "given" : "none, the whole surface");
            }
        }
                // 1. A fill of a game buffer that has a counterpart goes to the counterpart, where every read of it is sent.
        if (FpSceneActive() && ivtInjections == 0)
        {
            if (FpScene* e = FpSceneForSurf(surf))
            {
                if (!rect)                                        { surf = e->ourSurf; }
                else if (FpScaleRectOnto(e->ourSurf, rect, &mapped)) { surf = e->ourSurf; rect = &mapped; }
                else
                {
                    // A write that could not be followed: it lands on the game's buffer while every read goes to the
                    // counterpart.
                    static bool saidUnred = false;
                    if (!saidUnred)
                    {
                        saidUnred = true;
                        // log=1: a release build has no log=2.
                        LogRecord("  ColorFill: a fill of a twinned game buffer could NOT be redirected (its rectangle does not map): its reads see nothing of it");
                    }
                }
            }
        }
                // 2. A rectangle on a surface the scale shrank, in output pixels, mapped as hkClear maps its clear rectangles. Skipped
        // when the redirect above already mapped it (rect != &mapped).
        if (rsActive && rect && rect != &mapped)
        {
            if (const RsSurf* e = RsFind(surf))
            {
                UINT x = 0, w = 0, y = 0, h = 0;
                RsScaleSpan((UINT)rect->left, (UINT)(rect->right - rect->left), e->origW, e->newW, &x, &w);
                RsScaleSpan((UINT)rect->top,  (UINT)(rect->bottom - rect->top), e->origH, e->newH, &y, &h);
                mapped.left = (LONG)x; mapped.right  = (LONG)(x + w);
                mapped.top  = (LONG)y; mapped.bottom = (LONG)(y + h);
                rect = &mapped;
            }
        }
    }
    return origColorFill ? origColorFill(dev, surf, rect, colour) : D3DERR_INVALIDCALL;
}

// The game's own CreateDevice, on the IDirect3D9 vtable: its present parameters, and whether the device hooks were already in
// place (they must be, for the creations that follow).
typedef HRESULT(__stdcall* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
static CreateDevice_t origCreateDevice = nullptr;
static void** hookedD3dVtbl = nullptr;
// The module whose Direct3DCreate9 gave the hooked interface, for the not-DXVK refusal: d3d9.dll is FusionFix's proxy
// forwarding to DXVK here, but ENB's or ReShade's d3d9.dll in its place does not forward.
static char   d3d9From[64] = "";
DEVSTAT(static bool   hookedVtblLive = false;)      // set at log>=1 once the device vtable is the mod's (development log only)
static void   HookDeviceVtableFrom(IDirect3DDevice9* dev, const char* from);   // arms the device vtable from the game's device; defined with HookVtable

// The mod's own throwaway CreateDevice is passing through this hook and must not be taken for the game's. A thread id: the game
// may create its device on another thread meanwhile.
static volatile DWORD ivtOwnCreateTid = 0;

// ---- other mods' throwaway devices are not the game's either ----
// D3D9 mods read vtables from throwaway devices; taken for the game's, IvtEnsureVulkan would latch onto one. The test is the
// window size (GTA IV offers nothing under 640x480); anything unmeasured counts as the game's.
static UINT ivtThrowawayW = 0, ivtThrowawayH = 0;

static bool IvtForeignThrowaway(const D3DPRESENT_PARAMETERS* pp, HWND wnd)
{
    if (!pp || !pp->Windowed) return false;          // exclusive fullscreen is the game, always
    UINT w = pp->BackBufferWidth, h = pp->BackBufferHeight;
    if (!w || !h)
    {
        HWND hw = pp->hDeviceWindow ? pp->hDeviceWindow : wnd;   // D3D9's own fallback, so ours too
        RECT rc = {};
        if (!hw || !GetClientRect(hw, &rc)) return false;
        w = (UINT)(rc.right - rc.left); h = (UINT)(rc.bottom - rc.top);
    }
    if (!w || !h) return false;
    if (w >= 640 && h >= 480) return false;
    ivtThrowawayW = w; ivtThrowawayH = h;
    return true;
}

// Said once per distinct size and module, at most four. The module name is evidence only: a proxy can stand in front of the
// game's own call too.
static void IvtSayThrowaway(UINT w, UINT h, const void* ret)
{
        // The game itself taken for a throwaway, said at every level. It can only miss (a call through a proxy names the proxy),
    // never accuse another mod.
    {
        static bool warnedGame = false;
        HMODULE gm = nullptr;
        if (!warnedGame && ret
            && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ret, &gm)
            && gm && gm == GetModuleHandleA(nullptr))
        {
            warnedGame = true;
            IvtWarn("GTA IV made its device at %ux%u, below 640x480, so DLSS-IV took it for another mod's scratch device: HDR is off"
                    " and the render scale starts only after a Reset. Use a window of at least 640x480", (unsigned)w, (unsigned)h);
        }
    }
    if (!telemetryOn) return;
    HMODULE m = nullptr;
    const bool named = ret && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                 (LPCSTR)ret, &m) && m;

        // Keyed on module and size: every 1x1 scratch window comes back as 226x18.
    static HMODULE seenM[4] = {};
    static UINT seenW[4] = {}, seenH[4] = {};
    static int  seenN = 0;
    for (int i = 0; i < seenN; i++) if (seenM[i] == m && seenW[i] == w && seenH[i] == h) return;
    if (seenN >= (int)(sizeof(seenW) / sizeof(seenW[0]))) return;
    seenM[seenN] = m; seenW[seenN] = w; seenH[seenN] = h; seenN++;

    char who[64] = "a module that could not be named";
    if (named)
    {
        char path[MAX_PATH] = "";
        if (GetModuleFileNameA(m, path, MAX_PATH))
        {
            const char* const leaf = strrchr(path, '\\');
            strncpy_s(who, sizeof(who), leaf ? leaf + 1 : path, _TRUNCATE);
        }
    }
        // Worded for threaded=1: the device thread's install reads its resource vtables from this device too.
    LogRecord("  %s made a %ux%u windowed device: its vtables are taken, nothing else of it (not the game's device)", who, (unsigned)w, (unsigned)h);
}

// ---- the game's CPU 0 pin, undone ----
// GTA IV pins its main thread to CPU 0 at start (CE 0x59D3AC, 1.0.8.0 0x48FC58, 1.0.4.0 0x471C34). At the game's first
// CreateDevice the process mask is restored on that thread, only when its mask is exactly 1. unpin=0 leaves the pin. A process
// mask of 0 means unreadable (processor groups).
struct IvtThreadBasicInfo { LONG exitStatus; PVOID teb; ULONG_PTR pid, tid; ULONG_PTR affinity; LONG priority, basePriority; };
typedef LONG (NTAPI* IvtNtQueryInformationThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static void IvtUnpinMainThread()
{
    static bool done = false;
    if (done || !ivtCfg.unpin) return;
    DWORD_PTR proc = 0, sys = 0;
    const HMODULE nt = GetModuleHandleA("ntdll.dll");
    const IvtNtQueryInformationThread query = nt ? (IvtNtQueryInformationThread)GetProcAddress(nt, "NtQueryInformationThread") : nullptr;
    IvtThreadBasicInfo tbi = {};
        // The game's device is made on its main thread, which the CPU placement walls.
    if (!ivtMainTid) ivtMainTid = GetCurrentThreadId();
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) || !proc || !query || query(GetCurrentThread(), 0, &tbi, sizeof(tbi), nullptr) < 0)
    {
        done = true;
        LogRecord("  cpu: the main thread's processor mask could not be read, so the game's CPU 0 pin is left as it is");
        return;
    }
    const unsigned n = BitCount(proc);
    if (tbi.affinity != 1)
    {
        static bool said = false;   // once: the line is for a reader, and a mod making devices must not fill the log
        if (!said) LogRecord("  cpu: the thread creating this device is not pinned to CPU 0 (mask 0x%lX, the process 0x%lX); left as it is",
                             (unsigned long)tbi.affinity, (unsigned long)proc);
        said = true;
        return;
    }
    done = true;
    ivtMainTid = GetCurrentThreadId();   // the game's main thread, the one GTA IV pinned: the CPU placement walls it (cpu_place.h)
    if (n < 2)
        LogRecord("  cpu: the game runs on CPU 0 alone (process mask 0x%lX)", (unsigned long)proc);
    else if (!SetThreadAffinityMask(GetCurrentThread(), proc))
        LogRecord("  cpu: the game's CPU 0 pin could not be undone (error %lu); it stays", GetLastError());
    else
        LogRecord("  cpu: the game's main thread was pinned to CPU 0; it now has %u logical processors (mask 0x%lX)",
                  n, (unsigned long)proc);
}

// How many times the hook ran, for the line that says it was lost: two or three a session.
static volatile LONG ivtCreateDeviceEntries = 0;
// The hook's body. Its entry is hkCreateDevice, after it: a same-thread re-entry guard, which passes the caller's return
// address in (inside this function _ReturnAddress() would be the guard's).
static HRESULT hkCreateDeviceBody(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND wnd, DWORD flags,
                                  D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out, void* caller)
{
    InterlockedIncrement(&ivtCreateDeviceEntries);
        // The mod's own: make the device, take the vtable, touch no game state.
    if (ivtOwnCreateTid != 0 && ivtOwnCreateTid == GetCurrentThreadId())
    {
        HRESULT hrOwn = origCreateDevice ? origCreateDevice(d3d, adapter, type, wnd, flags, pp, out) : D3DERR_INVALIDCALL;
        if (SUCCEEDED(hrOwn) && out && *out) HookDeviceVtableFrom(*out, "the mod's throwaway device");
        return hrOwn;
    }
        // Somebody else's: take the vtable (one class, one table) and touch nothing else, including the fpTab wipe below, which is
    // for a device the game replaced.
    if (IvtForeignThrowaway(pp, wnd))
    {
        HRESULT hrTa = origCreateDevice ? origCreateDevice(d3d, adapter, type, wnd, flags, pp, out) : D3DERR_INVALIDCALL;
        if (SUCCEEDED(hrTa) && out && *out) HookDeviceVtableFrom(*out, "another module's throwaway device");
        IvtSayThrowaway(ivtThrowawayW, ivtThrowawayH, caller);
        return hrTa;
    }
        // A new device without a Reset invalidates fpTab.
    IvtUnpinMainThread();   // once, on the game's own thread: the only place it may run (see the function)
    for (int i = 0; i < fpTabN; i++) if (fpTab[i].tex) fpTab[i].tex->Release();
    fpTabN = 0;
    sceneBufTex = nullptr; sceneBufSurf = nullptr;   // both are read out of that table
        // And the back buffer's record: hkEndScene re-reads its format (hdrBbFp16) only when the pointer changes, and a new
    // device's buffer can land at the old one's freed address.
    backbufferSurf = nullptr;
    if (ivtCfg.hdr) HdrUnlockFormats(d3d);  // stock DXVK refuses FP16 back buffers until told otherwise; before the real call
        // Which window, and so which monitor: hDeviceWindow, or the focus window when it is null, as D3D9 itself falls back.
    if (pp && pp->hDeviceWindow) hdrGameWnd = pp->hDeviceWindow;
    else if (wnd)                hdrGameWnd = wnd;
    hdrCsScRgbSet = false;                  // a new device is a new swapchain object: nothing has been told scRGB yet
    HdrRewriteFormat(pp, "CreateDevice");   // must precede the real call
    hdrHooks = ivtCfg.hdr && hdrEngaged;    // the draw hooks follow the engagement (hdr.h)
        // NVIDIA's forced vsync, asked of DXVK in the game's place (IvtDriverVsyncInterval); the game's value goes back after the
    // call.
    const UINT gameInterval = pp ? pp->PresentationInterval : D3DPRESENT_INTERVAL_DEFAULT;
    const UINT sentInterval = pp ? IvtDriverVsyncInterval(gameInterval, true) : gameInterval;
    if (pp) pp->PresentationInterval = sentInterval;

    HRESULT hr = origCreateDevice ? origCreateDevice(d3d, adapter, type, wnd, flags, pp, out) : D3DERR_INVALIDCALL;
    if (pp) pp->PresentationInterval = gameInterval;
        // Arm the device hooks here, from the game's own device, before the game receives it. Idempotent.
    if (SUCCEEDED(hr) && out && *out) HookDeviceVtableFrom(*out, "the game's device");
        // Is this DXVK? Asked before RsOnScreenSize decides the scale, which cannot stand down later. See ivtDxvkOk.
    if (SUCCEEDED(hr) && out && *out)
    {
        ID3D9VkInteropDevice* probe = nullptr;
        if (SUCCEEDED((*out)->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&probe)) && probe)
        {
            probe->Release();
        }
        else
        {
            ivtDxvkOk = false;
            LogHealth("  device: no ID3D9VkInteropDevice, the runtime is not DXVK: render scale and resolve OFF");
        // The player is told as well as the log (DLSS-IV-DID-NOT-START.txt): that IvtFail is below, after RsOnScreenSize,
            // for the reason written there.
        }
    }   // no-op once armed
        // Proton's own DXVK (FusionFix's API setting not on Vulkan) is warned about once, under Wine only: vulkan.dll not loaded.
    if (HdrOnWine() && ivtDxvkOk && SUCCEEDED(hr) && !GetModuleHandleA("vulkan.dll"))
    {
        static bool saidProton = false;
        if (!saidProton)
        {
            saidProton = true;
            char path[MAX_PATH] = "an unknown module";
            HMODULE hm = nullptr;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)origCreateDevice, &hm) && hm)
                GetModuleFileNameA(hm, path, MAX_PATH);
            LogRecord("  dxvk: vulkan.dll is not loaded: the game runs on %s", path);
            // Says only what is known: vulkan.dll was not loaded, and whether it lies beside GTAIV.exe.
            char dll[MAX_PATH]; IvtGameDir(dll, MAX_PATH); strcat_s(dll, "vulkan.dll");
            IvtWarn("The game is not using the DXVK that comes with DLSS-IV. It is running on %s instead, because vulkan.dll was not"
                    " loaded. %s", path, GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES
                        ? "vulkan.dll is missing next to GTAIV.exe. Please copy it there from the mod's archive."
                        : "vulkan.dll is next to GTAIV.exe, but the game did not load it.");
        }
    }
        // Everything else the transport needs, before RsOnScreenSize: a failure here keeps the scale off.
    if (SUCCEEDED(hr) && out && *out && ivtDxvkOk)
    {
                // The Vulkan half: the interop device, the queue, the entry points and timeline support. The same call the first
        // injection makes; it latches.
        if (IvtEnsureVulkan(*out))
        {
            // The D3D9 half: the three guide formats and the mod's own shaders.
            if (!IvtProbeCapable(d3d, adapter, type, *out))
                IvtFail("this device cannot carry the guides (see the line above)");
        }
    }
    if (pp)
    {
        if (pp->BackBufferWidth && pp->BackBufferHeight)
        { cenScreenW = pp->BackBufferWidth; cenScreenH = pp->BackBufferHeight; RsOnScreenSize(cenScreenW, cenScreenH); }
                // Not DXVK: the player is told (DLSS-IV-DID-NOT-START.txt). Here, after RsOnScreenSize, where rsActive reflects the
        // real device (the game's first CreateDevice fails).
        if (!ivtDxvkOk)
        {
            // Another d3d9.dll (ENB, ReShade) in FusionFix's proxy's place, or FusionFix's Graphics API at DirectX 9.
            char why[400];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "the runtime is not DXVK: the game's device has no ID3D9VkInteropDevice."
                " Direct3DCreate9 came from %s%s", d3d9From[0] ? d3d9From : "an unknown module",
                _stricmp(d3d9From, "d3d9.dll") == 0
                    ? ". On this stack that is FusionFix's proxy, which forwards to DXVK (vulkan.dll) only when its Graphics API"
                      " option is Vulkan; if ENB's or ReShade's d3d9.dll has replaced it, DXVK must be the d3d9.dll that wrapper"
                      " loads, not the other way round"
                    : "");
            if (!hookedVtbl) fatalBeforeHooks = true;   // no device hook on a runtime that is not DXVK: the note says so (IvtFatalNote)
            IvtFail(why);
        }
                // What the back buffer actually became: asking for a format and being given it differ.
        if (ivtCfg.hdr && hdrEngaged && SUCCEEDED(hr) && out && *out)
        {
            IDirect3DSurface9* bb0 = nullptr;
            if (SUCCEEDED((*out)->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb0)) && bb0)
            {
                D3DSURFACE_DESC bd = {}; bb0->GetDesc(&bd);
                const bool got = bd.Format == D3DFMT_A16B16G16R16F;
                if (got) LogHealth("  hdr: the backbuffer really is %ux%u %s", bd.Width, bd.Height, HdrFmtName(bd.Format));
                else     LogRecord("  hdr: the back buffer is %ux%u format %u = %s, NOT A16B16G16R16F: HDR off",
                             bd.Width, bd.Height, (unsigned)bd.Format, HdrFmtName(bd.Format));
                if (!got)
                {
                    hdrEngaged = false;
                    IvtWarn("hdr=1 is set, but the back buffer came back as %s, not the FP16 format HDR needs, so HDR is off this session"
                    " (the runtime refused it; the DXVK 3.1 the mod ships allows it)", HdrFmtName(bd.Format));
                }
                bb0->Release();
            }
        }
        LogFusionFixOnce();
        WarnCloudFix10();                    // the one other mod known to end the game with this one (the end of this file)
                // The game's first attempt (flags 0x14) is always refused, then 0x44 succeeds: log=1 says it once the device exists,
        // and only a second refusal in a row is a fault line.
        static int refusedRun = 0;
        LogHealth("  the game's CreateDevice: %ux%u fmt %u windowed %d autoDS %d fmt %u interval %s as the GAME ASKED"
            " (DXVK's d3d9.presentInterval overrides it; its own <exe>_d3d9.log says what was applied)"
            " flags 0x%lX -> %s  +%lu ms; the mod's device hooks %s",
            pp->BackBufferWidth, pp->BackBufferHeight, (unsigned)pp->BackBufferFormat, (int)pp->Windowed,
            (int)pp->EnableAutoDepthStencil, (unsigned)pp->AutoDepthStencilFormat, PresentIntervalName(pp->PresentationInterval),
            (unsigned long)flags, SUCCEEDED(hr) ? "OK" : "FAILED", (unsigned long)CenMs(),
            hookedVtblLive ? "were already armed" : "were NOT armed yet");
        if (SUCCEEDED(hr))
        {
            // The present interval is on the log=1 line (a release build has no LogHealth). A dxvk.conf override is said by the
            // d3d9.presentInterval line (LatReadPresentIntervalCfg).
            LogRecord("  the game's device: %ux%u %s -> OK (+%lu ms%s); the game asked for present interval %s",
                pp->BackBufferWidth, pp->BackBufferHeight,
                pp->Windowed ? "windowed" : "exclusive fullscreen", (unsigned long)CenMs(),
                refusedRun == 1 ? ", after the game's usual first refusal" : refusedRun ? ", after more than one refusal" : "",
                PresentIntervalName(pp->PresentationInterval));
            refusedRun = 0;
            // DXVK runs the CPU up to min(3, BackBufferCount + 1) frames ahead (0 counts as 1); swap effect and window mode
            // decide back buffer rotation. See LogGameSwapchain.
            LogGameSwapchain(out ? *out : nullptr, pp, pp->Windowed, "CreateDevice");
            // The refresh, now that the device exists and any mode change has happened (HdrReadRefresh, last in this file).
            HdrReadRefresh(out ? *out : nullptr, "CreateDevice");
            if (latLimiterReread)   // FusionFix's cfg once more, now that FusionFix has loaded (InitThread says why)
            {
                latLimiterReread = false;
                const bool was = latLimiter; char wasNote[sizeof(latLimiterNote)]; strcpy_s(wasNote, latLimiterNote);
                LatReadLimiterCfg(true);
                if (was != latLimiter || strcmp(wasNote, latLimiterNote))
                    LogRecord("  latency: FusionFix's cfg read again at the game's device (FusionFix had not loaded at start): %s",
                              latLimiter ? latLimiterNote : "no frame limiter, the pacer is not held off");
            }
            latVsync = LatVsyncEffective(sentInterval);   // what DXVK was given: the game's, or interval one where NVIDIA's settings force vsync
            if (latPacerOn && (latVsync || !latLimiter))   // a limiter was said at start ("the pacer is off")
                LogRecord("  latency: pacer %s", latVsync ? "off: vsync (the queue cap applies)"
                     : ivtCfg.responsiveness >= 4 ? "held until the adaptive step keeps it" : "arms on live frames");
            LatSayRefresh(false);   // the display's refresh
            RgArm();                // the refresh guard, now that the interval and the refresh are known
            if (ivtWine) IvtLogWineDisplayDriver();   // Wayland or X11/XWayland
        }
        else if (++refusedRun >= 2)
            LogRecord("  the game's CreateDevice REFUSED %d times in a row (0x%08lX, %ux%u, flags 0x%lX)", refusedRun, (unsigned long)hr, pp->BackBufferWidth, pp->BackBufferHeight, (unsigned long)flags);
    }
    return hr;
}

// ---- THE CreateDevice SLOT TAKEN BACK (when another module's hook goes around this one) ----
// A module that read the slot before this hook and wrote its own after makes the game's device bypass the mod. The startup wait
// looks at the slot and judges a new holder by what its module keeps in writable data: this mod's hook (a normal chain, left);
// the pointer this mod replaced (bypass: taken back once); neither (left). The slot given its first pointer back (Windows'
// compatibility layer, at every Direct3DCreate9) is taken each time, up to kSlotRestoreMax. A same-thread re-entry goes
// straight to the first pointer, ending any mutual chain. tools/test-createdevice-retake cuts this region out and tests it.
static CreateDevice_t ivtCreateDeviceFirst = nullptr;   // what the first hook replaced: DXVK's, or a hook that was there before
static __declspec(thread) int ivtCreateDeviceDepth = 0;  // this thread is inside hkCreateDevice
static volatile LONG ivtCreateDeviceReentries = 0;       // entries the guard ended
static bool   ivtSlotRetaken = false;                    // taken back (once a session; the first pointer put back, each time)
static int    ivtSlotRestores = 0;                       // times the slot held the first pointer again and was taken back
enum { kSlotRestoreMax = 32 };
static void*  ivtSlotSeen = nullptr;                     // the holder last judged, so each is judged once
static char   ivtSlotTaker[MAX_PATH] = "";               // whom it was taken back from, for the recovery's line
static unsigned long ivtSlotTakerOff = 0;
static bool WriteSlot(void** vtbl, int slot, void* fn);  // defined with the installer, below

static HRESULT __stdcall hkCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND wnd, DWORD flags,
                                        D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    if (ivtCreateDeviceDepth > 0)
    {
        if (InterlockedIncrement(&ivtCreateDeviceReentries) == 1)
            LogRecord("  a CreateDevice call came back into this mod's hook on the same thread: passed on");
        return ivtCreateDeviceFirst ? ivtCreateDeviceFirst(d3d, adapter, type, wnd, flags, pp, out) : D3DERR_INVALIDCALL;
    }
    void* const caller = _ReturnAddress();
    HRESULT hr = D3DERR_INVALIDCALL;
    ivtCreateDeviceDepth++;
    __try { hr = hkCreateDeviceBody(d3d, adapter, type, wnd, flags, pp, out, caller); }
    __finally { ivtCreateDeviceDepth--; }
    return hr;
}

// The file name of the module an address lies in, and the module; null and "an unnamed region" for none.
static HMODULE IvtModuleName(const void* p, char* out, size_t cap)
{
    HMODULE hm = nullptr;
    strcpy_s(out, cap, "an unnamed region");
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &hm) || !hm)
        return nullptr;
    char full[MAX_PATH] = "";
    if (GetModuleFileNameA(hm, full, MAX_PATH)) { const char* leaf = strrchr(full, '\\'); strcpy_s(out, cap, leaf ? leaf + 1 : full); }
    return hm;
}

// Does a module's writable image hold this pointer, at pointer alignment? Its sections are mapped whole by the loader.
static bool IvtModuleDataHolds(HMODULE hm, const void* v)
{
    const BYTE* base = (const BYTE*)hm;
    __try
    {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
        {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_WRITE)) continue;
            const DWORD bytes = sec->Misc.VirtualSize > sec->SizeOfRawData ? sec->Misc.VirtualSize : sec->SizeOfRawData;
            const void* const* q = (const void* const*)(base + sec->VirtualAddress);
            for (DWORD k = 0; k < bytes / sizeof(void*); k++) if (q[k] == v) return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}

// One look at the slot, from the startup wait and, behind Windows' compatibility layer, from hkGameDirect3DCreate9; locked.
static SRWLOCK ivtSlotLock = SRWLOCK_INIT;
static void IvtCheckCreateDeviceSlotLocked();
static void IvtCheckCreateDeviceSlot()
{
    AcquireSRWLockExclusive(&ivtSlotLock);
    IvtCheckCreateDeviceSlotLocked();
    ReleaseSRWLockExclusive(&ivtSlotLock);
}
static void IvtCheckCreateDeviceSlotLocked()
{
    void** dv = hookedD3dVtbl;
    if (!dv || !ivtCreateDeviceFirst) return;
    void* const holder = dv[kD3dSlotCreateDevice];
    if (!holder || holder == (void*)&hkCreateDevice || holder == ivtSlotSeen) return;
    ivtSlotSeen = holder;
    char mn[MAX_PATH];
    const HMODULE hm = IvtModuleName(holder, mn, sizeof(mn));
    const unsigned long off = hm ? (unsigned long)((const BYTE*)holder - (const BYTE*)hm) : 0ul;
    const bool restored = holder == (void*)ivtCreateDeviceFirst;
    if (ivtSlotRetaken && !(restored && ivtSlotRestores < kSlotRestoreMax))
    {
        LogRecord("  IDirect3D9::CreateDevice was replaced AGAIN, now by %s (+0x%lX): left as it is", mn, off);
        return;
    }
    const bool keepsOurs  = hm && IvtModuleDataHolds(hm, (const void*)&hkCreateDevice);
    const bool keepsFirst = hm && IvtModuleDataHolds(hm, (const void*)ivtCreateDeviceFirst);
    if (!restored && (keepsOurs || !keepsFirst))
    {
        LogRecord(keepsOurs
            ? "  IDirect3D9::CreateDevice is now hooked by %s (+0x%lX) as well, and that module keeps this mod's hook as its original,"
              " so it passes the call on: left as it is"
            : "  IDirect3D9::CreateDevice is now hooked by %s (+0x%lX), and nothing in that module's data says where it passes the"
              " call: left as it is (if it goes around this mod, the recovery at the wait's end applies)", mn, off);
        return;
    }
    const CreateDevice_t was = origCreateDevice;
    origCreateDevice = (CreateDevice_t)holder;             // BEFORE the write: the game's call may come the moment it lands
    if (WriteSlot(dv, kD3dSlotCreateDevice, (void*)&hkCreateDevice))
    {
        const bool first = !ivtSlotRetaken;
        ivtSlotRetaken = true;
        ivtSlotSeen = nullptr;                             // so the same hook written again is seen again
        strcpy_s(ivtSlotTaker, mn);
        ivtSlotTakerOff = off;
        if (restored) ivtSlotRestores++;
        if (first)
            LogRecord("  THE CreateDevice HOOK WAS REPLACED before the game's device, by %s (+0x%lX), %s: TAKEN BACK%s. Passes on to %s", mn, off,
                      restored ? "which put the original pointer back" : "whose data keeps the pointer this mod replaced and not this mod's hook, so it went around this mod",
                      restored ? "" : ", once", restored ? "the original" : "that module's hook");
        else if (ivtSlotRestores <= 4 || ivtSlotRestores == kSlotRestoreMax)   // the first few and the last, not every one of a loop's
            LogRecord("  IDirect3D9::CreateDevice held the pointer this mod first replaced again (%s +0x%lX): taken back again (%d)%s", mn, off,
                      ivtSlotRestores, ivtSlotRestores == kSlotRestoreMax ? "; the last time, it is left after this" : "");
    }
    else
    {
        origCreateDevice = was;
        LogRecord("  THE CreateDevice HOOK WAS REPLACED before the game's device, by %s (+0x%lX), and could NOT be taken back", mn, off);
    }
}

// Behind Windows' compatibility layer (DWM8And16BitMitigation and the like), the layer rewrites its CreateDevice hook inside
// every Direct3DCreate9, so the game's import is wrapped and the slot taken back there. Only when the first pointer is
// apphelp.dll's.
typedef IDirect3D9* (WINAPI *Direct3DCreate9_t)(UINT);
static Direct3DCreate9_t ivtGameCreate9Next = nullptr;   // what the game's import held: the layer's wrapper
static IDirect3D9* WINAPI hkGameDirect3DCreate9(UINT sdk)
{
    IDirect3D9* const d3d = ivtGameCreate9Next(sdk);
    IvtCheckCreateDeviceSlot();   // the layer has just written its hook back: taken back before the game can call CreateDevice
    return d3d;
}
// The import slot of <dll>!<name> in a module, by its import directory; null when the module has no such import.
static void** IvtImportSlot(HMODULE mod, const char* dll, const char* name)
{
    BYTE* const base = (BYTE*)mod;
    __try
    {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
        const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dd.VirtualAddress) return nullptr;
        for (const IMAGE_IMPORT_DESCRIPTOR* d = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dd.VirtualAddress); d->Name; d++)
        {
            if (_stricmp((const char*)(base + d->Name), dll) != 0 || !d->FirstThunk) continue;
            const IMAGE_THUNK_DATA* names = (const IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
            IMAGE_THUNK_DATA* iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
            for (; names->u1.AddressOfData; names++, iat++)
            {
                if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
                if (!strcmp(((const IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, name)) return (void**)&iat->u1.Function;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}
static bool IvtWrapGameCreate9(HMODULE exe)
{
    if (ivtGameCreate9Next) return true;
    void** const slot = IvtImportSlot(exe, "d3d9.dll", "Direct3DCreate9");
    if (!slot || !*slot)
    {
        LogRecord("  compatibility layer: the game's import of Direct3DCreate9 was not found: hook watched from the start-up wait only");
        return false;
    }
    ivtGameCreate9Next = (Direct3DCreate9_t)*slot;        // BEFORE the write: the game's call may come the moment it lands
    if (!WriteSlot(slot, 0, (void*)&hkGameDirect3DCreate9))
    {
        ivtGameCreate9Next = nullptr;
        LogRecord("  Windows' compatibility layer is on, and the game's import of Direct3DCreate9 could not be wrapped (the write was"
                  " refused): its CreateDevice hook is watched from the start-up wait alone");
        return false;
    }
    LogRecord("  Windows' compatibility layer is on (IDirect3D9::CreateDevice belonged to apphelp.dll before this mod's hook): the game's own Direct3DCreate9 is wrapped");
    return true;
}
// ---- END OF THE CreateDevice SLOT TAKEN BACK ----

// ---- the swapchain's Present ----
// GTA IV presents through IDirect3DSwapChain9::Present, never the device's. This hook sets the scRGB colour space after a
// present.
static bool WriteSlot(void** vtbl, int slot, void* fn);   // defined with the installer, below
// hdrArmDev lives in hdr.h: the transport's draw hook, included after it, needs the same pointer.

// The swapchain Present hook's original; HdrColorSpace runs inside the hook. This pair is the HDR output path.
typedef HRESULT(__stdcall* SwapPresent_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
static SwapPresent_t     origSwapPresent = nullptr;

static HRESULT __stdcall hkSwapPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd,
                                       const RGNDATA* dirty, DWORD flags)
{
    DT_NOT_ON_WORKER(origSwapPresent(sc, src, dst, wnd, dirty, flags));

        // The !origSwapPresent arm stays: the hook can in principle be reached before the original is saved.
    const bool latHere = latOn && !dtArmed;                        // latched once, so the stamp, the block pair and the marker agree on the path
    LONGLONG lt0 = 0; if (latHere && telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&lt0);
    const HRESULT hr = !origSwapPresent ? D3DERR_INVALIDCALL
                     : origSwapPresent(sc, src, dst, wnd, dirty, flags);
    LONGLONG lt1 = 0; if (latHere && telemetryOn) QueryPerformanceCounter((LARGE_INTEGER*)&lt1);   // the present block ends here, before the marker's own lock and submit
        // The latency stamp when the device thread is not recording.
    if (latPacerOn && !dtArmed) LatEndSignal(frames - 1);          // the frame this Present ended, for the pacer's true end (the worker does it when recording)
    if (latGuard && !dtArmed) RgOnPresent();                       // the refresh guard (after DllMain), before the stamp: a hold is the next frame's wait, not its work
    if (latHere) LatOnPresent(0, lt1 - lt0);
    hdrPresents++;
        // The window may move to another display: checked every 600 presents, said once; a Reset reads the new display.
    if (hdrEngaged && hdrGameMon && hdrGameWnd && (hdrPresents % 600) == 0)
    {
        static bool saidMoved = false;
        const HMONITOR now = MonitorFromWindow(hdrGameWnd, MONITOR_DEFAULTTONEAREST);
        if (now && now != hdrGameMon && !saidMoved)
        {
            saidMoved = true;
            LogRecord("  hdr: the window moved to a DIFFERENT display: the encode keeps the figures read at engage until a Reset");
        }
    }
        // Every presented frame must be encoded, or SDR values reach a swapchain told scRGB. Gated on hdrEngaged, which every
    // failure clears.
    if (ivtCfg.hdr && hdrEngaged && hdrColorSpaceDone)
    {
        static UINT saidAt = 0, saidN = 0;
                // Counted since the last engage: a session that starts in exclusive fullscreen presents SDR frames with HDR refused,
        // which are not un-encoded frames.
        const UINT pres = hdrPresents - hdrPresentsAtEngage, enc = hdrEncodes - hdrEncodesAtEngage;
        const UINT behind = (pres > enc) ? pres - enc : 0;
                // Five times at 60 presents, then every 6,000, so a lasting fault does not flood the log; the count is in every line.
        if (behind > 8 && (saidAt == 0 || hdrPresents - saidAt >= (saidN < 5 ? 60u : 6000u)))
        {
            saidAt = hdrPresents; saidN++;
            LogRecord("  hdr: %u presents, %u EndScene, %u ENCODED: %u presented frames had no encode%s",
                hdrPresents, hdrFramesSeen, hdrEncodes, behind,
                saidN == 5 ? " (said five times; now once every 6,000 presents)" : "");
        }
    }
    static bool said = false; static UINT64 nFail = 0; static HRESULT lastBad = S_OK;
    if (!said) { said = true; LogHealth("  hdr: swapchain Present hook is live (first call, frame %u, returned 0x%08X)", hdrFramesSeen, (unsigned)hr); }
    if (FAILED(hr) && (hr != lastBad || nFail < 3)) { lastBad = hr; nFail++; LogRecord("  hdr: swapchain Present returned 0x%08X at frame %u", (unsigned)hr, hdrFramesSeen); }
    if (hdrArmDev && hdrCsStandDownWanted) HdrColorSpaceStandDown(hdrArmDev, "at this present, because the encode pass could not be built");
    if (hdrArmDev) HdrColorSpace(hdrArmDev);
    return hr;
}

// One vtable, hooked once: an overlay hooked above the mod (RTSS) leaves its thunk in the slot, and re-hooking would make two
// hooks call each other forever.
static void** hdrSwapHookedVt = nullptr;
static volatile LONG hookVtblBusy = 0;   // HookDeviceVtableFrom is installing the device hooks and the device thread's layer

static void HdrArmSwapchainPresent(IDirect3DDevice9* dev)
{
    if (!dev || !hdrSwapHookDirty) return;
        // Not while the device hooks go in: a Present hook in the slot before DtInstall looks refuses the device thread. The next
    // EndScene arms it.
    if (hookVtblBusy) return;
    hdrSwapHookDirty = false;
    hdrArmDev = dev;
    IDirect3DSwapChain9* sc = nullptr;
    if (FAILED(dev->GetSwapChain(0, &sc)) || !sc) { LogRecord("  hdr: GetSwapChain(0) failed: the swapchain Present is not hooked"); return; }
    void** svt = *(void***)sc;
    if (!IsReadable(svt, 4 * sizeof(void*))) { LogRecord("  hdr: swapchain vtable unreadable"); sc->Release(); return; }
    if (hdrSwapHookedVt)
    {
                // Hooked already: the slot's holder is above the mod and calls it.
        if (svt != hdrSwapHookedVt) LogHealth("  hdr: the swapchain has a different vtable after this Reset; not hooked again");
        sc->Release();
        return;
    }
    if (svt[3] != (void*)&hkSwapPresent)             // slot 3: IDirect3DSwapChain9::Present
    {
        origSwapPresent = (SwapPresent_t)IvtGateWrap(svt[3]);   // the gate before DXVK's Present where no device thread holds it (transport/present_gate.h); under the layer this is svt[3] itself
        if (WriteSlot(svt, 3, (void*)&hkSwapPresent)) { hdrSwapHookedVt = svt; LogHealth("  hdr: swapchain Present hooked (slot 3)"); }
        else LogRecord("  hdr: swapchain Present hook FAILED");
    }
    sc->Release();
}

typedef HRESULT(__stdcall* Clear_t)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
DLSSIV_HOT static Clear_t origClear = nullptr;

static HRESULT __stdcall hkClear(IDirect3DDevice9* dev, DWORD count, const D3DRECT* rects, DWORD flags,
                                 D3DCOLOR color, float z, DWORD stencil)
{
    DT_NOT_ON_WORKER(origClear(dev, count, rects, flags, color, z, stencil));
    if (hdrCgaHeld && (flags & D3DCLEAR_TARGET) && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_CLEAR);   // the absorb's guard (hdr_cga.h)
    // A clear with explicit rectangles is in screen pixels like everything
    // else the game says. A whole-target clear (count 0) needs nothing.
    D3DRECT rsRects[8];
    if (rsActive && count && count <= 8 && rects && !rsOwn && curRT0)
    {
        const RsSurf* e = curRT0Scaled ? RsFind(curRT0) : nullptr;
        if (e)
        {
            for (DWORD i = 0; i < count; i++)
            {
                UINT x, w, y, h;
                RsScaleSpan((UINT)rects[i].x1, (UINT)(rects[i].x2 - rects[i].x1), e->origW, e->newW, &x, &w);
                RsScaleSpan((UINT)rects[i].y1, (UINT)(rects[i].y2 - rects[i].y1), e->origH, e->newH, &y, &h);
                rsRects[i].x1 = (LONG)x; rsRects[i].x2 = (LONG)(x + w);
                rsRects[i].y1 = (LONG)y; rsRects[i].y2 = (LONG)(y + h);
            }
            rects = rsRects;
            DEVSTAT(if (telemetryOn) { rsClearScaled++; })
        }
    }
    if (!dead && (flags & D3DCLEAR_ZBUFFER) && curDS && curDS == (dsSceneMajority ? dsSceneMajority : dsAtSceneDraws) && IvtSceneSeenThisFrame())
    {
                // The gate is inside, not on the header, and must stay there: IvtOnSceneDepthClear is live transport work (the scene
        // depth snapshot the guide pass needs).
        DEVSTAT(if (telemetryOn) { dsClearsAfterScene++; })
        IvtOnSceneDepthClear();     // the scene depth is complete and about to go: snapshot it now
    }
    return origClear ? origClear(dev, count, rects, flags, color, z, stencil)
                     : D3DERR_INVALIDCALL;
}


// A table keyed on the shader pointer answers a last-hit miss; the cache holds a reference on each, so pointers stay unique.
static IDirect3DVertexShader9* velVSMapPtr[256];
static short                   velVSMapIdx[256];
#include "transport/quad_uv.h"     // QuvVsStraight here; the rest of it is RsQuadDraw's, below
// How many texldl instructions a vertex shader holds (velVSTexld), and whether it passes the position straight through
// (velVSStraight): two walks over its tokens, once a shader.
static BYTE VsCountTexldl(IDirect3DVertexShader9* vs, BYTE* onS0, bool* straight)
{
    *onS0 = 0; *straight = false;
    UINT size = 0;
    if (FAILED(vs->GetFunction(nullptr, &size)) || size < 8 || size > (1u << 20)) return 0;
    FpScratch scratch(size);
    DWORD* const tok = scratch.p;
    if (!tok || FAILED(vs->GetFunction(tok, &size))) return 0;
    int s0 = 0;
    const int count = CorCountTexldl(tok, size / 4, &s0);     // transport/corona_pass.h: the walk the GPU proof runs on FusionFix's own shaders
    *straight = QuvVsStraight(tok, size / 4);                 // transport/quad_uv.h: proved on FusionFix's own shaders by tools/test-quad
    *onS0 = (BYTE)(s0 > 255 ? 255 : s0);
    return (BYTE)(count > 255 ? 255 : count);
}
static HRESULT __stdcall hkSetVertexShader(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs)
{
    DT_NOT_ON_WORKER(origSetVertexShader(dev, vs));
    DEVSTAT(trkApplyDirty &= ~8u;)   // the tracker's premise (dt_res.h)
    if (!dead)
    {
        velCurVSPremul = false;
        velCurVS = vs;
        velCurVSTexld = 0; velCurVSTexld0 = 0; velCurVSStraight = false;
        if (vs)
        {
            // Consecutive draws overwhelmingly share a vertex shader: the previous hit answers most calls; on a miss the scan
            // runs.
            int found = -1;
            if (velVSLastHit >= 0 && velVSLastHit < velVSCount && velVSPtr[velVSLastHit] == vs)
                found = velVSLastHit;
            else
            {
                const unsigned slot = ((unsigned)(UINT_PTR)vs >> 4) & 255u;
                if (velVSMapPtr[slot] == vs) { found = velVSMapIdx[slot]; velVSLastHit = found; }
                else
                {
                    for (int i = 0; i < velVSCount; i++)
                        if (velVSPtr[i] == vs) { found = i; velVSLastHit = i; break; }
                    if (found >= 0) { velVSMapPtr[slot] = vs; velVSMapIdx[slot] = (short)found; }
                }
            }
            if (found >= 0)
            {
                velCurVSPremul = velVSPremul[found];
                velCurVSTexld = velVSTexld[found]; velCurVSTexld0 = velVSTexld0[found];
                velCurVSStraight = velVSStraight[found];
            }
            else
            {
                bool premul = false;
                __try { premul = FingerprintVS(vs, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }
                __except (EXCEPTION_EXECUTE_HANDLER) { premul = false; }
                velCurVSPremul = premul;
                velCurVSStraight = true;                // unclassified until the line below says otherwise
                if (velVSCount < VEL_VS_CACHE)
                {
                    vs->AddRef();                       // same reason as the declaration cache
                    velVSPtr[velVSCount]    = vs;
                    velVSPremul[velVSCount] = premul;
                    __try { velVSTexld[velVSCount] = VsCountTexldl(vs, &velVSTexld0[velVSCount], &velVSStraight[velVSCount]); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { velVSTexld[velVSCount] = 0; velVSTexld0[velVSCount] = 0; velVSStraight[velVSCount] = true; }
                    velCurVSTexld = velVSTexld[velVSCount]; velCurVSTexld0 = velVSTexld0[velVSCount];
                    velCurVSStraight = velVSStraight[velVSCount];
                    velVSCount++;
                    if (velVSCount == VEL_VS_CACHE)
                        LogHealth("  velocity: vertex shader cache FULL at %d; later shaders are"
                            " fingerprinted per call", VEL_VS_CACHE);
                }
                DEVSTAT(else velVSPastCap++;)
            }
        }
    }
    return origSetVertexShader ? origSetVertexShader(dev, vs) : D3DERR_INVALIDCALL;
}

#include "devthread/dt_trk.h"   // after every hook it mirrors and every helper it calls

// hkDrawPrimUP and hkDrawIdxUP: the console gamma absorb, NoteDrawRec and the chevron's depth lend.
typedef HRESULT(__stdcall* DrawPrimUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(__stdcall* DrawIdxUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT,
                                        const void*, D3DFORMAT, const void*, UINT);
static DrawPrimUP_t origDrawPrimUP = nullptr;
static DrawIdxUP_t  origDrawIdxUP  = nullptr;

// Redraws the game's full-screen quad with the real half texel (transport/quad_uv.h), from a corrected copy of its vertices.
// False: not the quad; the caller draws.
static bool rsQuadSaid = false, rsQuadUnreadSaid = false;
static __declspec(noinline) bool RsQuadDraw(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT startVertex, HRESULT* hr)
{
    const UINT n = QuvVerts(type), str = velCurVBStr;
    if (!n || !velCurVB || !origDrawPrimUP || !origSetStreamSource || str > kQuvMaxStride || str < (UINT)velCurQuadUv + 8u) return false;
    const RsSurf* e = RsFind(curRT0);
    if (!e) return false;
    DEVSTAT(if (telemetryOn) rsQuadSeen++;)
    BYTE v[kQuvMaxVerts * kQuvMaxStride];
    if (!QuvRead(velCurVB, velCurVBOff + startVertex * str, n * str, v))
    {
        DEVSTAT(if (telemetryOn) rsQuadUnread++;)
        if (!rsQuadUnreadSaid)
        {
            rsQuadUnreadSaid = true;
            LogRecord("  render scale: a two-triangle draw into a scaled target could not be read back from its vertex buffer (not a dynamic"
                      " default-pool buffer, or the lock was refused): if it is one of the game's full-screen quads it keeps the half texel of %ux%u", e->origW, e->origH);
        }
        return false;
    }
    if (!QuvFix(v, n, str, velCurQuadUv, e->origW, e->origH, e->newW, e->newH)) return false;
    *hr = origDrawPrimUP(dev, type, 2, v, str);
    origSetStreamSource(dev, 0, velCurVB, velCurVBOff, velCurVBStr);
    DEVSTAT(if (telemetryOn) rsQuadFixed++;)
    if (!rsQuadSaid)
    {
        rsQuadSaid = true;
        LogRecord("  render scale: full-screen quads: half a texel of %ux%u in the vertices, drawn with %ux%u (first at frame %ld)", e->origW, e->origH, e->newW, e->newH, (long)frames);
    }
    return true;
}

static HRESULT __stdcall hkDrawPrimUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type,
                                      UINT primCount, const void* vtx, UINT stride)
{
    DT_NOT_ON_WORKER(origDrawPrimUP(dev, type, primCount, vtx, stride));
#if DLSSIV_DEV
    if (gcOn && curRT0 && curRT0 == sceneBufSurf && !rsOwn && !ivtInOwnDraw) GlassCensus(dev, 2, primCount);   // the glass census
#endif
    if (!dead && toldVerdict)
    {
                // the armed product in SDR (fg/fg_hud.h): FusionFix's console gamma draw is drawn again into the HUD-less picture's
        // target
        FGA(if (fgSdrCgaArmed && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) FgHudSdrCgaDraw(dev, _ReturnAddress(), true, type, 0, primCount, vtx, stride);)
        if (hdrCgaHeld && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_DRAW);   // the absorb's guard (hdr_cga.h)
                // The absorb: FusionFix's newer console gamma draws its quad here (v5.0.1 uses hkDrawPrim).
        else if (hdrCgaArmed && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw && HdrCgaOnDraw(dev, _ReturnAddress()))
        { return D3D_OK; }
        {
            DrawRec rec;
            rec.prims = primCount;
            rec.indexed = false; rec.kind = 2; rec.posT = velCurPosT;
            rec.sceneKnown = false; rec.scene = false;
            rec.vs = velCurVS;
            rec.ortho = m4bHaveWVP && fabsf(m4bCurWVP[11]) < 1e-6f && fabsf(m4bCurWVP[15] - 1.0f) < 1e-3f;
            rec.bb    = (curRT0 != nullptr && (curRT0 == backbufferSurf || (rsRedirectOn && curRT0 == rsRedirectSurf)));
            NoteDrawRec(rec);
        }
    }
    if (!origDrawPrimUP) return D3DERR_INVALIDCALL;
    const bool corSub = velCurVSTexld0 >= 12 && CorHoldBefore(dev);  // the coronas, whichever draw call the game makes them with
    DEVSTAT(if (velCurVSTexld0 >= 12 && telemetryOn && !ivtInOwnDraw && !rsOwn) ivtCorHook[2]++;)
    bool chvSub = false;
    if (chvArmed) { chvSub = ChvBefore(dev); }
    const HRESULT hr = origDrawPrimUP(dev, type, primCount, vtx, stride);
    if (chvSub) { ChvAfter(dev); }
    if (corSub) CorHoldAfter(dev);
    return hr;
}

static HRESULT __stdcall hkDrawIdxUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minIdx,
                                     UINT numV, UINT primCount, const void* idx, D3DFORMAT fmt,
                                     const void* vtx, UINT stride)
{
    DT_NOT_ON_WORKER(origDrawIdxUP(dev, type, minIdx, numV, primCount, idx, fmt, vtx, stride));
#if DLSSIV_DEV
    if (gcOn && curRT0 && curRT0 == sceneBufSurf && !rsOwn && !ivtInOwnDraw) GlassCensus(dev, 3, primCount);   // the glass census
#endif
    if (!dead && toldVerdict)
    {
        if (hdrCgaHeld && curRT0 == backbufferSurf && !rsOwn && !ivtInOwnDraw) HdrCgaHeal(dev, CGA_HEAL_DRAW);   // the absorb's guard (hdr_cga.h)
        {
            DrawRec rec;
            rec.prims = primCount;
            rec.indexed = true; rec.kind = 3; rec.posT = velCurPosT;
            rec.sceneKnown = false; rec.scene = false;
            rec.vs = velCurVS;
            rec.ortho = m4bHaveWVP && fabsf(m4bCurWVP[11]) < 1e-6f && fabsf(m4bCurWVP[15] - 1.0f) < 1e-3f;
            rec.bb    = (curRT0 != nullptr && (curRT0 == backbufferSurf || (rsRedirectOn && curRT0 == rsRedirectSurf)));
            NoteDrawRec(rec);
        }
    }
    if (!origDrawIdxUP) return D3DERR_INVALIDCALL;
    const bool corSub = velCurVSTexld0 >= 12 && CorHoldBefore(dev);  // the coronas, whichever draw call the game makes them with
    DEVSTAT(if (velCurVSTexld0 >= 12 && telemetryOn && !ivtInOwnDraw && !rsOwn) ivtCorHook[3]++;)
    bool chvSub = false;
    if (chvArmed) { chvSub = ChvBefore(dev); }
    const HRESULT hr = origDrawIdxUP(dev, type, minIdx, numV, primCount, idx, fmt, vtx, stride);
    if (chvSub) { ChvAfter(dev); }
    if (corSub) CorHoldAfter(dev);
    return hr;
}

static bool WriteSlot(void** vtbl, int slot, void* fn)
{
    DWORD old;
    if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        return false;
    vtbl[slot] = fn;
    VirtualProtect(&vtbl[slot], sizeof(void*), old, &old);
        // Read it back: a VirtualProtect that returns TRUE is not proof the write landed.
    return vtbl[slot] == fn;
}


static void HookVtable(void** vtbl, const char* from);   // (hookedVtbl is declared beside fatalBeforeHooks, which reads it)

// Under the device thread each hook's original becomes the layer's wrapper for that slot, once every slot checks.
static bool DtRouteOriginals()
{
    int bad = 0;
#define DT_ROUTE_CHECK(slot, orig, fn) if ((void*)orig != dtDx[slot] || dtDevFn[slot] != (void*)&fn) { bad++; LogRecord("  threaded: slot %d does not line up (%s)", slot, #fn); }
    DT_ROUTE_CHECK(kSlotSetVsConstF, origSetVsConstF, dt_SetVertexShaderConstantF)
    DT_ROUTE_CHECK(kSlotSetPsConstF, origSetPsConstF, dt_SetPixelShaderConstantF)
    DT_ROUTE_CHECK(kSlotEndScene, origEndScene, dt_EndScene)
    DT_ROUTE_CHECK(kSlotDrawIndexed, origDrawIndexed, dt_DrawIndexedPrimitive)
    DT_ROUTE_CHECK(kSlotSetStreamSource, origSetStreamSource, dt_SetStreamSource)
    DT_ROUTE_CHECK(kSlotSetIndices, origSetIndices, dt_SetIndices)
    DT_ROUTE_CHECK(kSlotSetVertexDecl, origSetVertexDecl, dt_SetVertexDeclaration)
    DT_ROUTE_CHECK(kSlotReset, origReset, dt_Reset)
    DT_ROUTE_CHECK(kSlotDrawPrim, origDrawPrim, dt_DrawPrimitive)
    DT_ROUTE_CHECK(kSlotDrawPrimUP, origDrawPrimUP, dt_DrawPrimitiveUP)
    DT_ROUTE_CHECK(kSlotDrawIdxUP, origDrawIdxUP, dt_DrawIndexedPrimitiveUP)
    DT_ROUTE_CHECK(kSlotSetVertexShader, origSetVertexShader, dt_SetVertexShader)
    DT_ROUTE_CHECK(kSlotSetRenderTarget, origSetRenderTarget, dt_SetRenderTarget)
    DT_ROUTE_CHECK(kSlotSetDepthStencil, origSetDepthStencil, dt_SetDepthStencilSurface)
    DT_ROUTE_CHECK(kSlotClear, origClear, dt_Clear)
    DT_ROUTE_CHECK(kSlotCreateDepthStencilSurface, origCreateDepthStencilSurface, dt_CreateDepthStencilSurface)
    DT_ROUTE_CHECK(kSlotStretchRect, origStretchRect, dt_StretchRect)
    DT_ROUTE_CHECK(kSlotCreateTexture, origCreateTexture, dt_CreateTexture)
    DT_ROUTE_CHECK(kSlotCreateRenderTarget, origCreateRenderTarget, dt_CreateRenderTarget)
    DT_ROUTE_CHECK(kSlotSetViewport, origSetViewport, dt_SetViewport)
    DT_ROUTE_CHECK(kSlotSetTexture, origSetTexture, dt_SetTexture)
    DT_ROUTE_CHECK(kSlotSetScissorRect, origSetScissorRect, dt_SetScissorRect)
    DT_ROUTE_CHECK(kSlotSetSamplerState, origSetSamplerState, dt_SetSamplerState)
    DT_ROUTE_CHECK(kSlotColorFill, origColorFill, dt_ColorFill)
    DT_ROUTE_CHECK(kSlotGetRenderTargetData, origGetRenderTargetData, dt_GetRenderTargetData)
    DT_ROUTE_CHECK(kSlotGetDepthStencil, origGetDepthStencil, dt_GetDepthStencilSurface)
#undef DT_ROUTE_CHECK
    if (bad)
    {
        dtArmable = false;       // the wrappers stay in the other slots and pass straight through
        LogRecord("  threaded: %d hooks do not line up with the device thread's slots: it will not arm", bad);
        return false;
    }
    origSetVsConstF = dt_SetVertexShaderConstantF;  origSetPsConstF = dt_SetPixelShaderConstantF;
    origEndScene = dt_EndScene;                     origDrawIndexed = dt_DrawIndexedPrimitive;
    origSetStreamSource = dt_SetStreamSource;       origSetIndices = dt_SetIndices;
    origSetVertexDecl = dt_SetVertexDeclaration;    origReset = dt_Reset;
    origDrawPrim = dt_DrawPrimitive;                origDrawPrimUP = dt_DrawPrimitiveUP;
    origDrawIdxUP = dt_DrawIndexedPrimitiveUP;      origSetVertexShader = dt_SetVertexShader;
    origSetRenderTarget = dt_SetRenderTarget;       origSetDepthStencil = dt_SetDepthStencilSurface;
    origClear = dt_Clear;                           origCreateDepthStencilSurface = dt_CreateDepthStencilSurface;
    origStretchRect = dt_StretchRect;               origCreateTexture = dt_CreateTexture;
    origCreateRenderTarget = dt_CreateRenderTarget; origSetViewport = dt_SetViewport;
    origSetTexture = dt_SetTexture;                 origSetScissorRect = dt_SetScissorRect;
    origSetSamplerState = dt_SetSamplerState;       origColorFill = dt_ColorFill;
    origGetRenderTargetData = dt_GetRenderTargetData;
    origGetDepthStencil = dt_GetDepthStencilSurface;
    return true;
}

// The DXVK version, from the banner in DXVK's own log. The banner's version from an open log; false when the first 40 lines
// lack it.
static bool DxvkVersionFrom(FILE* f, char* ver, size_t cap)
{
    char line[256];
    ver[0] = 0;
    for (int n = 0; n < 40 && fgets(line, sizeof(line), f); n++)
    {
        const char* at = strstr(line, "DXVK: v");
        if (!at) continue;
        at += 7;
        size_t k = 0;
        while (k + 1 < cap && at[k] && at[k] != ' ' && at[k] != '\r' && at[k] != '\n' && at[k] != '\t') { ver[k] = at[k]; k++; }
        ver[k] = 0;
        return k > 0;
    }
    return false;
}
// FusionFix's Graphics API (API under [MAIN] in d3d9.cfg) decides whether the mod can run; read where FusionFix reads it.
// log=1.
static void LogD3d9CfgApi()
{
    if (!telemetryOn) return;
    static bool done = false;
    if (done) return;
    done = true;
    char exe[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exe, MAX_PATH)) return;
    char* slash = strrchr(exe, '\\');
    if (!slash) return;
    *slash = 0;
    char lad[MAX_PATH] = "", prof[MAX_PATH] = "";
    const DWORD nl = GetEnvironmentVariableA("LOCALAPPDATA", lad, MAX_PATH);
    const DWORD np = GetEnvironmentVariableA("USERPROFILE", prof, MAX_PATH);
    if (nl == 0 || nl >= MAX_PATH) lad[0] = 0;
    if (np == 0 || np >= MAX_PATH) prof[0] = 0;
    char cand[4][MAX_PATH] = {};
    _snprintf_s(cand[0], MAX_PATH, _TRUNCATE, "%s\\d3d9.cfg", exe);
    if (lad[0])  _snprintf_s(cand[1], MAX_PATH, _TRUNCATE, "%s\\Rockstar Games\\GTA IV\\d3d9.cfg", lad);
    if (lad[0])  _snprintf_s(cand[2], MAX_PATH, _TRUNCATE, "%s\\d3d9.cfg", lad);
    if (prof[0]) _snprintf_s(cand[3], MAX_PATH, _TRUNCATE, "%s\\Documents\\d3d9.cfg", prof);
    FILE* f = nullptr;
    const char* path = cand[0];
    for (int i = 0; i < 4 && !f; i++)
        if (cand[i][0] && (f = _fsopen(cand[i], "r", _SH_DENYNO)) != nullptr) path = cand[i];
    if (!f)
    {
        LogRecord("  d3d9.cfg: not found (beside GTAIV.exe, %%LOCALAPPDATA%%\\Rockstar Games\\GTA IV, %%LOCALAPPDATA%%, Documents)");
        return;
    }
        // [MAIN] only: an API key under another heading is not the one that decides.
    char line[256];
    int  api    = -1;
    bool inMain = false;
    while (fgets(line, sizeof(line), f))
    {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '[') { inMain = (_strnicmp(s, "[MAIN]", 6) == 0); continue; }
        if (!inMain || _strnicmp(s, "API", 3) != 0) continue;
        s += 3;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '=') api = atoi(s + 1);
    }
    fclose(f);
    if (api < 0)
        LogRecord("  d3d9.cfg: %s has no API line under [MAIN]: the system's d3d9.dll loads, not DXVK", path);
    else
        LogRecord("  d3d9.cfg: API = %d (%s)%s", api, path, api == 1
            ? ", Vulkan"
            : "   <-- NOT Vulkan: FusionFix loads the system's d3d9.dll (Microsoft's on Windows, Proton's own DXVK on Linux), not the"
              " DXVK 3.1.1 this mod ships. Set Graphics API to Vulkan in the game's settings");
}

// Why there is no DXVK log: under Wine DXVK writes to the terminal unless DXVK_LOG_PATH names a folder.
static const char* DxvkNoLogWhy()
{
    return ivtWine ? " (under Wine DXVK logs to the terminal unless DXVK_LOG_PATH is set)" : " (DXVK's own log is off or redirected)";
}

// The three places DXVK's log can be: beside the exe, the working directory, plugins\. Fills the stem; returns how many, 0 when
// the exe's path cannot be read.
static int DxvkLogCandidates(char (&cand)[3][MAX_PATH], char (&stem)[64])
{
    char exeDir[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exeDir, MAX_PATH)) return 0;
    char* slash = strrchr(exeDir, '\\');
    if (!slash) return 0;
    _snprintf_s(stem, sizeof(stem), _TRUNCATE, "%s", slash + 1);   // always terminated
    if (char* dot = strrchr(stem, '.')) *dot = 0;
    *slash = 0;
    char cwd[MAX_PATH] = "";
    GetCurrentDirectoryA(MAX_PATH, cwd);
    int n = 0;
    _snprintf_s(cand[n++], MAX_PATH, _TRUNCATE, "%s\\%s_d3d9.log", exeDir, stem);
    if (cwd[0] && _stricmp(cwd, exeDir) != 0) _snprintf_s(cand[n++], MAX_PATH, _TRUNCATE, "%s\\%s_d3d9.log", cwd, stem);
    _snprintf_s(cand[n++], MAX_PATH, _TRUNCATE, "%s\\plugins\\%s_d3d9.log", exeDir, stem);
    return n;
}
// When a candidate was last written; false when it is not there or is a folder.
static bool DxvkLogWrote(const char* path, ULONGLONG* wrote)
{
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad) || (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    *wrote = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
    return true;
}
// The newest candidate; with born set, only one written no more than 2 s before this process (*stale otherwise). -1 no
// candidates, 0 none there, 1 found.
static int DxvkLogNewest(char (&path)[MAX_PATH], char (&stem)[64], ULONGLONG born, bool* stale)
{
    path[0] = 0;
    char cand[3][MAX_PATH];
    const int nc = DxvkLogCandidates(cand, stem);
    if (!nc) return -1;
    ULONGLONG newest = 0;
    for (int i = 0; i < nc; i++)
    {
        ULONGLONG wrote = 0;
        if (!DxvkLogWrote(cand[i], &wrote)) continue;
        if (born && wrote + 20000000ull < born) { if (stale) *stale = true; continue; }
        if (!path[0] || wrote > newest) { newest = wrote; strcpy_s(path, cand[i]); }
    }
    return path[0] ? 1 : 0;
}

static void LogDxvkVersion(bool sayUnknown)
{
        // Only a version latches: called from the module scan (the banner may not exist yet) and at device time, which alone says
    // unknown. Interlocked: the two can overlap.
    static volatile LONG haveSaid = 0, saidUnknown = 0;
    if (InterlockedCompareExchange(&haveSaid, 0, 0) != 0) return;   // at log=0 too: the warnings file wants the answer (IvtWarn below)
        // Only a log this process wrote (2 s of slack): an older one is another session's DXVK.
    FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
    ULARGE_INTEGER born = {};
    if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser))
    { born.LowPart = ftCreate.dwLowDateTime; born.HighPart = ftCreate.dwHighDateTime; }
    char cand[3][MAX_PATH], stem[64];
    const int nc = DxvkLogCandidates(cand, stem);   // the first this session wrote, in the three places' order
    if (!nc) return;
    char path[MAX_PATH] = "", tried[3 * MAX_PATH] = "", stalePath[MAX_PATH] = "", staleVer[32] = "";
    FILE* f = nullptr;
    for (int i = 0; i < nc && !f; i++)
    {
        strcpy_s(path, cand[i]);
        f = _fsopen(path, "r", _SH_DENYNO);
        ULONGLONG wrote = 0;
        if (f && born.QuadPart && DxvkLogWrote(path, &wrote))
        {
            if (wrote + 20000000ull < born.QuadPart)   // written more than 2 s before this process existed
            {
                if (!stalePath[0]) { strcpy_s(stalePath, path); DxvkVersionFrom(f, staleVer, sizeof(staleVer)); }
                fclose(f); f = nullptr;                          // and keep looking: the live one may be in the next folder
            }
        }
        if (!f) { strncat_s(tried, tried[0] ? ", " : "", _TRUNCATE); strncat_s(tried, path, _TRUNCATE); }
    }
    if (!f)
    {
        if (!sayUnknown || InterlockedCompareExchange(&saidUnknown, 1, 0) != 0) return;
        if (stalePath[0])
            LogRecord("  dxvk: version UNKNOWN: %s (v%s) is older than this run; no newer %s_d3d9.log at %s%s", stalePath, staleVer[0] ? staleVer : "?", stem, tried,
                      DxvkNoLogWhy());
        else
            LogRecord("  dxvk: version UNKNOWN: no %s_d3d9.log at %s%s", stem, tried, DxvkNoLogWhy());
        return;
    }
    char ver[32] = "";
    const bool haveVer = DxvkVersionFrom(f, ver, sizeof(ver));
    fclose(f);
    if (!haveVer)
    {
        if (!sayUnknown || InterlockedCompareExchange(&saidUnknown, 1, 0) != 0) return;
        LogRecord("  dxvk: version UNKNOWN: no \"DXVK: v\" banner in the first 40 lines of %s", path);
        return;
    }
    if (InterlockedCompareExchange(&haveSaid, 1, 0) != 0) return;   // a version was found: this is the answer, and it latches
    const bool is31 = strncmp(ver, "3.1", 3) == 0 && (ver[3] == 0 || ver[3] == '.');
    LogRecord("  dxvk: v%s (%s)%s", ver, path, is31 ? "" : "   <-- NOT 3.1: the only DXVK DLSS-IV supports");
    if (!is31)
        IvtWarn("DXVK is v%s, but DLSS-IV supports only DXVK 3.1, which it ships as vulkan.dll beside GTAIV.exe: put that file back", ver);
}

// DXVK refusing every adapter (as on "AMD Radeon(TM) Graphics" without VK_KHR_load_store_op_none) ends the process inside
// Direct3DCreate9, so DXVK's last log is read before that call. Said once.
// The frame queue goes into DXVK_CONFIG before DXVK's first instance; a player's own d3d9.maxFrameLatency is read first.
static const int kLatQueue = 1;      // what modes 1 to 3 ask for when frame_queue is -1
static void LatSetDxvkEnv()
{
    int depth = 0; bool byHand = false;
    if (ivtCfg.frame_queue > 0)                                { depth = ivtCfg.frame_queue; byHand = true; }
    else if (ivtCfg.frame_queue < 0 && ivtCfg.responsiveness > 0)   depth = kLatQueue;
    if (!depth) return;                                       // mode 0, or frame_queue=0: nothing touched, nothing said
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char theirs[64] = ""; bool fromFile = false, fromEnv = false;
    DtDxvkValueAt(dir, "d3d9.maxFrameLatency", theirs, &fromFile, &fromEnv);
    const char* src = fromEnv ? "DXVK_CONFIG" : "dxvk.conf";
    const int theirsVal = atoi(theirs);
        // The player's own value wins only when it is as shallow or shallower: a deeper one is overridden and said; frame_queue=0
    // keeps it.
    if ((fromFile || fromEnv) && !byHand && theirsVal > 0 && theirsVal <= depth)
    {
        _snprintf_s(latQueueNote, sizeof(latQueueNote), _TRUNCATE, "yours (%s from %s), left alone", theirs, src);
        LogRecord("  latency: your %s sets d3d9.maxFrameLatency = %s: kept (responsiveness=%d)",
                  src, theirs, ivtCfg.responsiveness);
        return;
    }
    wchar_t env[512] = L"";                                     // UTF-16, as DXVK reads it; DXVK ignores the variable from 260 on, and a longer one only needs its length
    const DWORD ce = GetEnvironmentVariableW(L"DXVK_CONFIG", env, 512);   // a value that does not fit returns its length, which the test below rejects
    if (ce >= 260)
    {
        _snprintf_s(latQueueNote, sizeof(latQueueNote), _TRUNCATE, "not set: your DXVK_CONFIG is %lu characters, over the 259 DXVK reads", ce);
        LogRecord("  latency: your DXVK_CONFIG is %lu characters, over the 259 DXVK reads: the frame queue was not set", ce);
        return;
    }
    wchar_t add[64]; _snwprintf_s(add, 64, _TRUNCATE, L"%sd3d9.maxFrameLatency = %d", ce ? L";" : L"", depth);
    if (ce + wcslen(add) >= 260)
    {
        _snprintf_s(latQueueNote, sizeof(latQueueNote), _TRUNCATE, "not set: DXVK_CONFIG would pass 259 characters");
        LogRecord("  latency: d3d9.maxFrameLatency would take DXVK_CONFIG past its 259 characters: not set");
        return;
    }
    wchar_t newv[320]; _snwprintf_s(newv, 320, _TRUNCATE, L"%s%s", ce ? env : L"", add);
    if (!SetEnvironmentVariableW(L"DXVK_CONFIG", newv))
    {
        _snprintf_s(latQueueNote, sizeof(latQueueNote), _TRUNCATE, "not set: SetEnvironmentVariable failed (%lu)", GetLastError());
        LogRecord("  latency: DXVK_CONFIG could not be set (Windows error %lu): DXVK's default queue", GetLastError());
        return;
    }
    latQueueAsked = depth;
    _snprintf_s(latQueueNote, sizeof(latQueueNote), _TRUNCATE, "asked %d (%s)%s", depth, byHand ? "frame_queue by hand" : "from responsiveness",
                (fromFile || fromEnv) ? ", over yours" : "");
    LogRecord("  latency: DXVK_CONFIG gets d3d9.maxFrameLatency = %d (%s)%s%s%s", depth, byHand ? "frame_queue" : "responsiveness",
              (fromFile || fromEnv) ? ", over your " : "", (fromFile || fromEnv) ? src : "",
              !(fromFile || fromEnv) ? "" : byHand ? "'s value" : "'s deeper queue (frame_queue=0 keeps yours)");
}
// DXVK's constant path set to DXVK 2.x's (faster where the CPU paces), unless the player set it.
static bool latCbSet = false;   // LatSetDxvkConstants wrote the key: LatDxvkVerdict says at device time whether DXVK took it
static void LatSetDxvkConstants()
{
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char theirs[64] = ""; bool fromFile = false, fromEnv = false;
    DtDxvkValueAt(dir, "d3d9.deviceLocalConstantBuffers", theirs, &fromFile, &fromEnv);
    if (fromFile || fromEnv)
    {
        LogRecord("  dxvk: your %s sets d3d9.deviceLocalConstantBuffers = %s: kept", fromEnv ? "DXVK_CONFIG" : "dxvk.conf", theirs);
        return;
    }
    wchar_t env[512] = L"";
    const DWORD ce = GetEnvironmentVariableW(L"DXVK_CONFIG", env, 512);   // a value that does not fit returns its length
    const wchar_t* const add = L"d3d9.deviceLocalConstantBuffers = False";
    if (ce >= 260 || ce + 1 + wcslen(add) >= 260)
    {
        LogRecord("  dxvk: d3d9.deviceLocalConstantBuffers would take DXVK_CONFIG past its 259 characters: not set");
        return;
    }
    wchar_t newv[320]; _snwprintf_s(newv, 320, _TRUNCATE, L"%s%s%s", ce ? env : L"", ce ? L";" : L"", add);
    if (!SetEnvironmentVariableW(L"DXVK_CONFIG", newv)) { LogRecord("  dxvk: DXVK_CONFIG could not be set (Windows error %lu)", GetLastError()); return; }
    latCbSet = true;
    LogRecord("  dxvk: DXVK_CONFIG gets d3d9.deviceLocalConstantBuffers = False");
}
// dxvk.latencySleep = True is a second pacer in Present: turned off through DXVK_CONFIG where it clashes. Four costly keys are
// said.
static bool latSleepSet = false;   // LatGuardDxvkKeys wrote dxvk.latencySleep = False: LatDxvkVerdict says whether DXVK took it
static void LatGuardDxvkKeys()
{
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char v[64] = ""; bool fromFile = false, fromEnv = false;
    if (DtDxvkValueAt(dir, "dxvk.latencySleep", v, &fromFile, &fromEnv) && !_stricmp(v, "True"))
    {
        const char* src = fromEnv ? "DXVK_CONFIG" : "dxvk.conf";
        const bool onDev = ivtCfg.threaded >= 1, pacerCan = ivtCfg.pacer == 1 || (ivtCfg.pacer < 0 && ivtCfg.responsiveness >= 2);
        const bool clash = onDev || pacerCan;
        const char* where = onDev && pacerCan ? "on the mod's device thread, not the thread that reads input, and beside the mod's pacer"
                          : onDev ? "on the mod's device thread, not the thread that reads input" : "beside the mod's pacer";   // the reason that fired
        wchar_t env[512] = L"";
        const DWORD ce = clash ? GetEnvironmentVariableW(L"DXVK_CONFIG", env, 512) : 0;   // a value that does not fit returns its length
        const wchar_t* const add = L"dxvk.latencySleep = False";
        if (!clash)
            LogRecord("  dxvk: your %s sets dxvk.latencySleep = True: kept (threaded=0 and no pacer: it sleeps the game's own thread)", src);
        else if (ce >= 260 || ce + 1 + wcslen(add) >= 260)
        {
            LogRecord("  dxvk: your %s sets dxvk.latencySleep = True, and turning it off would take DXVK_CONFIG past its 259 characters: kept", src);
            IvtWarn("Your %s sets dxvk.latencySleep = True: DXVK's own sleep runs %s, and DXVK_CONFIG is too long for DLSS-IV to turn"
                    " it off. Remove the line, or set threaded=0 and responsiveness=1 (and pacer=0 if you set pacer=1) in DLSS-IV.cfg", src, where);
        }
        else
        {
            wchar_t newv[320]; _snwprintf_s(newv, 320, _TRUNCATE, L"%s%s%s", ce ? env : L"", ce ? L";" : L"", add);
            if (!SetEnvironmentVariableW(L"DXVK_CONFIG", newv))
                LogRecord("  dxvk: DXVK_CONFIG could not be set (Windows error %lu): dxvk.latencySleep = True kept", GetLastError());
            else
            {
                latSleepSet = true;
                LogRecord("  dxvk: your %s sets dxvk.latencySleep = True: DXVK_CONFIG gets False (its sleep would run %s)", src, where);
                IvtWarn("Your %s sets dxvk.latencySleep = True, which DLSS-IV turned off for this session: DXVK's own sleep would run %s."
                        " To keep it, set threaded=0 and responsiveness=1 (and pacer=0 if you set pacer=1) in DLSS-IV.cfg", src, where);
            }
        }
    }
    // THE FOUR THAT ARE ONLY SAID: the value that costs, and what it costs.
    static const char* const kSaid[4][3] = {
        { "dxvk.zeroMappedMemory",          "True",  "DXVK's own notes say it can drastically increase CPU overhead" },
        { "d3d9.allowDirectBufferMapping",  "False", "the game's dynamic buffers then go through staging copies, which costs CPU time" },
        { "d3d9.forceDrawTimeBufferUpload", "True",  "the game's dynamic buffers then go through staging copies, which costs CPU time" },
        { "d3d9.reproducibleCommandStream", "True",  "DXVK then holds a frame's commands until the mod's injection sends them, which"
                                                     " adds input delay and leaves the graphics card idle while the frame is recorded" },
    };
    for (int i = 0; i < 4; i++)
    {
        v[0] = 0; fromFile = fromEnv = false;
        if (!DtDxvkValueAt(dir, kSaid[i][0], v, &fromFile, &fromEnv) || _stricmp(v, kSaid[i][1])) continue;
        const char* src = fromEnv ? "DXVK_CONFIG" : "dxvk.conf";
        LogRecord("  dxvk: your %s sets %s = %s: kept (%s)", src, kSaid[i][0], v, kSaid[i][2]);
        IvtWarn("Your %s sets %s = %s: %s. DLSS-IV leaves it as you set it; remove the line unless you need it", src, kSaid[i][0], v, kSaid[i][2]);
    }
}
// dxvk.conf's d3d9.presentInterval overrides the game's; the pacer needs the effective one (LatVsyncEffective prefers it). Read
// the way DXVK reads it (DtDxvkValueAt).
static void LatReadPresentIntervalCfg()
{
    if (!latOn && logLevel < 1) return;   // the pacer's answer whenever the instrument runs; the line at log>=1
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char v[64] = ""; bool fromFile = false, fromEnv = false;
    if (DtDxvkValueAt(dir, "d3d9.presentInterval", v, &fromFile, &fromEnv) && v[0] >= '0' && v[0] <= '9')
    {
        const int vs = atoi(v) != 0 ? 1 : 0;
        if (latOn) latVsyncCfg = vs;
        LogRecord("  latency: your %s sets d3d9.presentInterval = %s (%s)", fromEnv ? "DXVK_CONFIG" : "dxvk.conf", v, vs ? "vsync" : "no vsync");
    }
}
// A frame limiter makes the pacer stand down: FusionFix's [FRAMELIMIT] FpsLimitPreset in plugins\GTAIV.EFLC.FusionFix.cfg (an
// index into kCaps50 or kCaps51; Custom reads FpsLimit: missing -1, 0 none, negative is the refresh minus it), or DXVK's
// maxFrameRate. again: the second read, at the first device.
static void LatReadLimiterCfg(bool again = false)
{
    if (!latOn && !telemetryOn) return;
    latLimiter = false; latLimiterNote[0] = 0;   // a second read replaces the first
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
        // dxvk.latencySleep: Auto and False mean no tracker in a 32-bit process; True sleeps inside Present, a second CPU pacer.
    // Matched as DXVK matches it (DtDxvkLine). A log line only.
    if (telemetryOn && !again)   // the second read says nothing itself
    {
        char v[64] = ""; bool vFile = false, vEnv = false;
        const bool set = DtDxvkValueAt(dir, "dxvk.latencySleep", v, &vFile, &vEnv);
        if (!set)
            LogRecord("  latency: dxvk.latencySleep is not set (DXVK's default, Auto, which in this 32-bit game makes no latency sleep,"
                      " as False does; neither dxvk.conf nor DXVK_CONFIG has it)");
        else
            LogRecord("  latency: dxvk.latencySleep is %s (%s)%s", !_stricmp(v, "true") ? "True" : !_stricmp(v, "false") ? "False"
                      : !_stricmp(v, "auto") ? "Auto" : v, vEnv ? "DXVK_CONFIG" : "dxvk.conf",
                      !_stricmp(v, "true")  ? ": DXVK sleeps inside Present and, under vsync, limits at the refresh, a second owner of CPU"
                                              " pacing beside the mod's pacer; frame generation needs it not True"
                      : !_stricmp(v, "false") ? ""
                      : !_stricmp(v, "auto")  ? ": in this 32-bit game Auto makes no latency sleep, as False does"
                      : ", which DXVK does not recognise, so it keeps its default, Auto: no latency sleep in this 32-bit game");
    }
        // FusionFix is read for a log that will be read too, not only for the pacer; only latLimiter, the pacer's stand-down, stays
    // the instrument's.
    char path[MAX_PATH];
    int preset = -1;
    const char* where = nullptr;
        // FusionFix's cfg is named after its module, in its own folder first (settings.ixx); its .asi also gives the version.
    char asi[MAX_PATH] = "";
    {
        const HMODULE ff = GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");
        if (!(ff && GetModuleFileNameA(ff, asi, MAX_PATH))) asi[0] = 0;
        const char* at[3] = { "plugins\\GTAIV.EFLC.FusionFix.asi", "GTAIV.EFLC.FusionFix.asi", "scripts\\GTAIV.EFLC.FusionFix.asi" };
        for (int i = 0; i < 3 && !asi[0]; i++)
        {
            _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", dir, at[i]);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(asi, path);
        }
    }
    char ffDir[MAX_PATH] = "", ffName[MAX_PATH] = "GTAIV.EFLC.FusionFix";   // the module's folder (with its backslash) and stem
    if (asi[0])
    {
        strcpy_s(ffDir, asi);
        if (char* s = strrchr(ffDir, '\\')) { strcpy_s(ffName, s + 1); *(s + 1) = 0; if (char* e = strrchr(ffName, '.')) *e = 0; }
        else ffDir[0] = 0;
    }
        // FusionFix's search order; the first existing writable cfg is used, with or without the key.
    char cands[5][MAX_PATH]; int nc = 0; char sh[MAX_PATH]; char cfgDir[MAX_PATH] = "";
    if (ffDir[0]) _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", ffDir, ffName);
    else          _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%splugins\\GTAIV.EFLC.FusionFix.cfg", dir);
    _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", dir, ffName);
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, sh)))
    {
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\Rockstar Games\\GTA IV\\%s.cfg", sh, ffName);
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    }
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, sh)))
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    for (int i = 0; i < nc && !where; i++)
    {
        const DWORD at = GetFileAttributesA(cands[i]);
        if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY) || (at & FILE_ATTRIBUTE_READONLY)) continue;
        preset = (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimitPreset", -1, cands[i]);
        where = cands[i];
        strcpy_s(cfgDir, cands[i]); if (char* s = strrchr(cfgDir, '\\')) *(s + 1) = 0;
    }
        // FusionFix 5.1.0 renumbered the presets ("refactor settings module"): the table follows the .asi's version.
    int ffMaj = -1, ffMin = -1;
    {
        char ver[64] = "";
        if (asi[0] && FileVersionOf(asi, ver, sizeof(ver)) && sscanf_s(ver, "%d.%d", &ffMaj, &ffMin) != 2) ffMaj = ffMin = -1;
    }
    const bool ff51 = ffMaj < 0 || ffMaj > 5 || (ffMaj == 5 && ffMin >= 1);
    static const int kCaps50[15] = { 0, 0, 0, -1, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240 };   // TOGGLE, HOLD, OFF, Custom, ...
    static const int kCaps51[13] = { 0, -1, 30, 40, 50, 60, 75, 100, 120, 144, 165, 200, 240 };          // OFF, Custom, ...
    const int* const kCaps = ff51 ? kCaps51 : kCaps50;
    const int nCaps = ff51 ? 13 : 15, kCustom = ff51 ? 1 : 3;
    if (preset >= nCaps) preset = nCaps - 1;   // as FusionFix clamps it
    if (!again && ffMaj < 0 && preset >= 0)
        LogRecord("  latency: FusionFix's version was not found: FpsLimitPreset=%d read with 5.1's numbering", preset);
    int cap = 0, rel = 0;
        // The ini the line names: the one whose FpsLimit decided, the first that exists, as FusionFix reads it.
    char iniUsed[MAX_PATH] = "";
    if (preset > kCustom && preset < nCaps) cap = kCaps[preset];
    else if (preset == kCustom)
    {
        bool ffSeen = false;   // the first ini that exists, with FusionFix's own default for a missing key
        int ffLimit = -1;      // [FRAMELIMIT] FpsLimit from its ini, with FusionFix's own default when the key is absent
        if (ffDir[0])    // FusionFix's own ini, first: framelimit.ixx reads it through CIniReader(""), the module's path with .ini
        {
            _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s.ini", ffDir, ffName);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            { ffLimit = (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimit", -1, path); ffSeen = true; strcpy_s(iniUsed, path); }
        }
        if (cfgDir[0] && !ffSeen)   // then the ini beside the cfg FusionFix chose
        {
            _snprintf_s(path, MAX_PATH, _TRUNCATE, "%sGTAIV.EFLC.FusionFix.ini", cfgDir);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            { ffLimit = (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimit", -1, path); ffSeen = true; strcpy_s(iniUsed, path); }
        }
        const char* inis[2] = { "plugins\\GTAIV.EFLC.FusionFix.ini", "GTAIV.EFLC.FusionFix.ini" };
        for (int i = 0; i < 2 && !ffSeen; i++)
        {
            _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", dir, inis[i]);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            { ffLimit = (int)GetPrivateProfileIntA("FRAMELIMIT", "FpsLimit", -1, path); ffSeen = true; strcpy_s(iniUsed, path); }
        }
        if (ffLimit < 0) { rel = -ffLimit; cap = rel; }             // refresh-relative (the refresh minus that many); still a limiter
        else cap = ffLimit;                                           // 0: FusionFix does not limit at all
    }
        // FusionFix's cap kept as a number (see latFfCap in ivt_latency.h): a fixed cap, or the size of a refresh-relative one.
    latFfRel     = rel;
    latFfCap     = rel ? 0 : cap;
        // DXVK's cap, always read (latDxvkCap), for the flip classifier.
    char v[64] = ""; bool fromFile = false, fromEnv = false; const char* key = "dxvk.maxFrameRate";   // DXVK 3.1 reads dxvk.maxFrameRate first and the d3d9 key only when it is absent
    if (!(DtDxvkValueAt(dir, key, v, &fromFile, &fromEnv) && atoi(v) > 0)) { key = "d3d9.maxFrameRate"; v[0] = 0; fromFile = fromEnv = false; DtDxvkValueAt(dir, key, v, &fromFile, &fromEnv); }
    latDxvkCap = atoi(v) > 0 ? atoi(v) : 0;
        // The line names the cap, or that it is relative to the refresh (FusionFix subtracts a negative Custom value), and the file
    // it came from.
    if (cap > 0)
    {
        if (latOn) latLimiter = true;
        if (rel) _snprintf_s(latLimiterNote, sizeof(latLimiterNote), _TRUNCATE, "FusionFix's frame limit is Custom -%d: the refresh minus %d,"
                             " unknown until the refresh is read (preset %d, %s)", rel, rel, preset, iniUsed);
        else     _snprintf_s(latLimiterNote, sizeof(latLimiterNote), _TRUNCATE, "FusionFix's frame limit is %d fps (preset %d, %s)", cap, preset,
                             preset == kCustom ? iniUsed : where ? where : "");
    }
    else
    {
                // When FusionFix caps nothing the mod counts, that is said too. Log lines only, so read only for a log that will be
        // read.
        if (telemetryOn && !again)   // said by the first read only
        {
            if (!where)
                LogRecord("  latency: FusionFix's frame limit is not read: no writable GTAIV.EFLC.FusionFix.cfg in the five places FusionFix"
                          " looks (the plugins folder, the game folder, LocalAppData twice, Documents)");
            else if (preset == kCustom && !iniUsed[0])
                LogRecord("  latency: FusionFix's frame limit is Custom, but no GTAIV.EFLC.FusionFix.ini was found to read FpsLimit from"
                          " (preset %d, %s): the mod counts no limiter", preset, where);
            else
                LogRecord("  latency: FusionFix's frame limit is off (preset %d%s, %s)", preset,
                          preset == kCustom ? " = Custom with FpsLimit 0, at which FusionFix does not limit" : preset < 0 ? ", the cfg names none"
                          : ff51 ? (preset == 0 ? " = OFF" : ", outside FusionFix's list")   // 5.1.0's list: OFF, Custom, then the caps
                          : preset == 0 ? " = TOGGLE, below Custom, where FusionFix's limiter does not run"
                          : preset == 1 ? " = HOLD, below Custom, where FusionFix's limiter does not run"
                          : preset == 2 ? " = OFF" : ", outside FusionFix's list", preset == kCustom ? iniUsed : where);
        }
        if (latDxvkCap > 0)   // read above, for the classifier as well
        {
            if (latOn) latLimiter = true;
            _snprintf_s(latLimiterNote, sizeof(latLimiterNote), _TRUNCATE, "DXVK's %s = %s (%s)", key, v, fromEnv ? "DXVK_CONFIG" : "dxvk.conf");
        }
    }
        // NVIDIA's cap, if the driver was asked before this read and nothing above is on: the second read starts the flag afresh
    // and must not lose it. Said by LatNvCapSeen when first seen.
    if (!latLimiter && latNvCapFps)
    {
        latLimiter = true;
        strcpy_s(latLimiterNote, latNvCapNote);
        return;
    }
    if (latLimiter && !again)
        LogRecord("  latency: %s: the pacer is off", latLimiterNote);
    else if (!latLimiter && latLimiterNote[0] && !again)   // latOn false (a log=1 session at responsiveness=0): the limiter is said, and that no pacer runs
        LogRecord("  latency: %s; responsiveness=0, so the mod runs no pacer (read at start)", latLimiterNote);
}
// The verdict at device time, from the last "Effective configuration" block of DXVK's newest log. Its own latch
// (LogDxvkVersion's fires on the census instance).
static void LatDxvkVerdict()
{
    static bool done = false;
    if (done || !telemetryOn || !(latOn || latCbSet || latSleepSet)) return;   // the verdict only feeds log lines; the keys the mod set are read back at any responsiveness
    done = true;
    FILETIME ftC = {}, ftE = {}, ftK = {}, ftU = {}; ULARGE_INTEGER born = {}; bool stale = false;
    if (GetProcessTimes(GetCurrentProcess(), &ftC, &ftE, &ftK, &ftU)) { born.LowPart = ftC.dwLowDateTime; born.HighPart = ftC.dwHighDateTime; }
    const bool queueCheck = latQueueAsked || latQueueNote[0] == 'y';
    if (!queueCheck && !latCbSet && !latSleepSet) return;   // nothing asked and nothing yielded to: nothing to check
    char stem[64] = "", path[MAX_PATH] = "";
    if (DxvkLogNewest(path, stem, born.QuadPart, &stale) < 0) return;   // the newest this session wrote; another session's is stale
    if (!path[0])
    {
        if (latCbSet) LogRecord("  dxvk: d3d9.deviceLocalConstantBuffers received: unknown (no %s_d3d9.log%s)%s", stem, stale ? " from this session" : "", DxvkNoLogWhy());
        if (latSleepSet) LogRecord("  dxvk: dxvk.latencySleep = False received: unknown (no %s_d3d9.log%s)%s", stem, stale ? " from this session" : "", DxvkNoLogWhy());
        if (!queueCheck) return;
        strncat_s(latQueueNote, stale ? "; DXVK's log is off this session (an older one was ignored), so whether it received the key is unknown" : "; DXVK's log is off, so whether it received the key is unknown", _TRUNCATE);
        LogRecord("  latency: d3d9.maxFrameLatency received: unknown (no %s_d3d9.log%s)%s", stem, stale ? " from this session" : "", DxvkNoLogWhy()); return;
    }
    FILE* f = _fsopen(path, "rb", _SH_DENYNO);
    if (!f) return;
    _fseeki64(f, 0, SEEK_END); const __int64 size = _ftelli64(f);
    if (size <= 0) { fclose(f); return; }                        // empty, or unreadable (a 32-bit ftell of a 2 GB log)
    const __int64 want = size > 262144 ? 262144 : size;          // the tail: the last instance is the one that matters
    _fseeki64(f, size - want, SEEK_SET);
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)want + 1);
    if (!buf) { fclose(f); return; }
    const size_t got = fread(buf, 1, (size_t)want, f); buf[got] = 0; fclose(f);
    const char* block = nullptr;                                  // the last "Effective configuration" block
    for (const char* s = buf; (s = strstr(s, "Effective configuration")) != nullptr; s++) block = s;
    int seen = -1; bool cbSeen = false; int sleepFalse = -1;   // sleepFalse: 1 listed as False, 0 listed as anything else, -1 not listed
    if (block)
    {
        const char* end = strstr(block + 1, "Game: ");           // the next instance's banner, if any
        const char* k = strstr(block, "d3d9.maxFrameLatency");
        if (k && (!end || k < end)) { const char* eq = strchr(k, '='); if (eq) seen = atoi(eq + 1); }
        const char* kc = strstr(block, "d3d9.deviceLocalConstantBuffers");
        cbSeen = kc && (!end || kc < end);
        const char* ks = strstr(block, "dxvk.latencySleep");
        if (ks && (!end || ks < end))
            if (const char* eq = strchr(ks, '=')) { eq++; while (*eq == ' ') eq++; sleepFalse = _strnicmp(eq, "False", 5) == 0 ? 1 : 0; }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    if (latCbSet)
    {
        if (block && cbSeen) LogRecord("  dxvk: DXVK received d3d9.deviceLocalConstantBuffers");
        else LogRecord(!block ? "  dxvk: d3d9.deviceLocalConstantBuffers received: unknown (no configuration block in %s)"
                              : "  dxvk: DXVK did NOT receive d3d9.deviceLocalConstantBuffers (%s): DXVK was created before it was set, or the"
                                " variable was dropped", path);
    }
    if (latSleepSet)
    {
        if (!block) LogRecord("  dxvk: dxvk.latencySleep = False received: unknown (no configuration block in %s)", path);
        else if (sleepFalse == 1) LogRecord("  dxvk: DXVK received dxvk.latencySleep = False");
        else LogRecord("  dxvk: DXVK did NOT receive dxvk.latencySleep = False (%s)", path);
    }
    if (!queueCheck) return;
    if (!block)
    {
        strncat_s(latQueueNote, "; DXVK's log carries no configuration block, unknown", _TRUNCATE);
        LogRecord("  latency: d3d9.maxFrameLatency received: unknown (no configuration block in %s)", path);
    }
    else if (seen < 0)
    {
        strncat_s(latQueueNote, "; DXVK did NOT receive it", _TRUNCATE);
        LogRecord("  latency: DXVK did NOT receive d3d9.maxFrameLatency (%s): DXVK's default queue", path);
    }
    else if (latQueueAsked && seen == latQueueAsked)
    {
        strncat_s(latQueueNote, "; DXVK received it", _TRUNCATE);
        LogRecord("  latency: DXVK received d3d9.maxFrameLatency = %d", seen);
    }
    else
    {
        char x[80]; _snprintf_s(x, sizeof(x), _TRUNCATE, "; DXVK runs %d", seen); strncat_s(latQueueNote, x, _TRUNCATE);
        LogRecord("  latency: DXVK's last instance runs d3d9.maxFrameLatency = %d%s (%s)", seen,
                  latQueueAsked ? ", not the value asked (a file or another module set it later)" : ", from your own configuration", path);
    }
}
static void DxvkWarnIfLastLaunchRefused()
{
    static bool done = false;
    if (done) return;
    done = true;
    char stem[64] = "", path[MAX_PATH] = "";
    if (DxvkLogNewest(path, stem, 0, nullptr) <= 0) return;   // the newest of any age: the last launch's is the one wanted
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) return;
    char line[512], kept[6][240];
    int  nKept = 0;
    bool refused = false;
    for (int n = 0; n < 400 && fgets(line, sizeof(line), f); n++)
    {
        if (strstr(line, "No adapters found")) { refused = true; break; }
        const char* s = strstr(line, "Skipping:");
        if (!s) s = strstr(line, "Found device:");
        if (!s || nKept >= 6) continue;
        size_t k = 0;
        while (k + 1 < sizeof(kept[0]) && s[k] && s[k] != '\r' && s[k] != '\n') { kept[nKept][k] = s[k]; k++; }
        kept[nKept][k] = 0;
        nKept++;
    }
    fclose(f);
    if (!refused) return;
    LogRecord("  dxvk: THE LAST LAUNCH, DXVK REFUSED EVERY GRAPHICS ADAPTER (%s: \"No adapters found\")", path);
    const char* why = nullptr;
    for (int i = 0; i < nKept; i++)
    {
        LogRecord("      %s", kept[i]);
        if (!why && strncmp(kept[i], "Skipping:", 9) == 0) why = kept[i];
    }
    IvtWarn("DXVK refused every graphics adapter at the last start, so the game could not render (%s). If it closes at startup"
            " again, update the graphics driver; if that fails, reinstall FusionFix's vulkan.dll (the game then starts without"
            " the mod). If the game now starts, ignore this", why ? why : path);
}

// One installer at a time: a second capture during the writes would take the mod's hooks as originals.

static void HookDeviceVtableFromLocked(IDirect3DDevice9* dev, const char* from);
static void HookDeviceVtableFrom(IDirect3DDevice9* dev, const char* from)
{
        // Another device with the hooks in: Windows' layer may have written its hooks back (DtRetakeSlots).
    if (dev && hookedVtbl) { DtRetakeSlots("when another device was made"); return; }
    if (!dev || hookedVtbl) return;
    if (InterlockedCompareExchange(&hookVtblBusy, 1, 0) != 0) return;
    HookDeviceVtableFromLocked(dev, from);      // a function of its own: it uses __try, which allows no object with a destructor
    InterlockedExchange(&hookVtblBusy, 0);
}

static void HookDeviceVtableFromLocked(IDirect3DDevice9* dev, const char* from)
{
    if (!dev || hookedVtbl) return;
        // Not on a runtime that is not DXVK (the layer's own test passes native d3d9.dll too).
    void** vt = nullptr;
    __try { vt = *(void***)dev; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
    if (!vt || !IsReadable(vt, 119 * sizeof(void*))) return;
    {
        ID3D9VkInteropDevice* probe = nullptr;
        if (FAILED(dev->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**)&probe)) || !probe)
        {
            ivtDxvkOk = false;
            LogRecord("  hook: %s has no ID3D9VkInteropDevice, the runtime is not DXVK: no device hook", from);
            return;
        }
        probe->Release();
    }
    HookVtable(vt, from);
        // The device thread, after the mod's hooks. Off unless threaded>=1.
    TrkArm();                    // stage 2a's observers, before the layer can arm
    if (vt && hookedVtbl == vt && ivtCfg.threaded
        && DtInstall(dev, vt, ivtCfg.threaded, (const DWORD*)kVtfTestVS, (const DWORD*)kVtfTestPS))
        DtRouteOriginals();
        // The gate before DXVK's Present (transport/present_gate.h) wraps the pointer the layer replays; with no layer,
    // HdrArmSwapchainPresent places it.
    if (dtScPresentO) dtScPresentO = IvtGateWrap(dtScPresentO);
        // The table as the mod left it, for DtRetakeSlots; anything Windows wrote back meanwhile goes back first.
    if (vt && hookedVtbl == vt) { DtRetakeSlots("while the layer installed"); DtSnapSlots(vt); }
    LogDxvkVersion(true);    // device time: the one call that may say the version is UNKNOWN
    LatDxvkVerdict();        // did DXVK's instance receive the frame queue the setter asked for
}

static void HookVtable(void** vtbl, const char* from)
{
    if (hookedVtbl || !IsReadable(vtbl, (kSlotSetPsConstF + 1) * sizeof(void*)))
        return;

    if (vtbl[kSlotSetVsConstF] == (void*)&hkSetVsConstF)
        return;                                  // already the mod's

        // Every slot as DXVK filled it, before any is written: the layer's wrappers call these.
    if (IsReadable(vtbl, 119 * sizeof(void*))) memcpy(dtDx, vtbl, sizeof(dtDx));

        // Capture all originals before writing any slot: reading an original back after installing is how a hook ends up calling
    // itself.
    origSetVsConstF     = (SetVsConstF_t)    vtbl[kSlotSetVsConstF];
    origSetPsConstF     = (SetPsConstF_t)    vtbl[kSlotSetPsConstF];
    origEndScene        = (EndScene_t)       vtbl[kSlotEndScene];
    origDrawIndexed     = (DrawIndexed_t)    vtbl[kSlotDrawIndexed];
    origSetStreamSource = (SetStreamSource_t)vtbl[kSlotSetStreamSource];
    origSetIndices      = (SetIndices_t)     vtbl[kSlotSetIndices];
    origSetVertexDecl   = (SetVertexDecl_t) vtbl[kSlotSetVertexDecl];
    origReset           = (Reset_t)          vtbl[kSlotReset];
    origDrawPrim        = (DrawPrim_t)       vtbl[kSlotDrawPrim];
    origDrawPrimUP      = (DrawPrimUP_t)     vtbl[kSlotDrawPrimUP];
    origDrawIdxUP       = (DrawIdxUP_t)      vtbl[kSlotDrawIdxUP];
    origSetVertexShader = (SetVertexShader_t)vtbl[kSlotSetVertexShader];
    origSetRenderTarget = (SetRenderTarget_t)vtbl[kSlotSetRenderTarget];
    origSetDepthStencil = (SetDepthStencil_t)vtbl[kSlotSetDepthStencil];
    origClear           = (Clear_t)          vtbl[kSlotClear];
    origCreateDepthStencilSurface = (CreateDepthStencilSurface_t)vtbl[kSlotCreateDepthStencilSurface];
    origStretchRect     = (StretchRect_t)    vtbl[kSlotStretchRect];
    origGetRenderTargetData = (GetRenderTargetData_t)vtbl[kSlotGetRenderTargetData];
    origGetDepthStencil     = (GetDepthStencil_t)vtbl[kSlotGetDepthStencil];
    origCreateTexture      = (CreateTexture_t)     vtbl[kSlotCreateTexture];
    origCreateRenderTarget = (CreateRenderTarget_t)vtbl[kSlotCreateRenderTarget];
    origSetViewport        = (SetViewport_t)       vtbl[kSlotSetViewport];
    origSetTexture         = (SetTexture_t)        vtbl[kSlotSetTexture];
    origSetScissorRect     = (SetScissorRect_t)    vtbl[kSlotSetScissorRect];
    origSetSamplerState    = (SetSamplerState_t)   vtbl[kSlotSetSamplerState];
    origColorFill          = (ColorFill_t)         vtbl[kSlotColorFill];

    bool okVs = WriteSlot(vtbl, kSlotSetVsConstF, (void*)&hkSetVsConstF);
    bool okPs = WriteSlot(vtbl, kSlotSetPsConstF, (void*)&hkSetPsConstF);
    bool okEs = WriteSlot(vtbl, kSlotEndScene,    (void*)&hkEndScene);
    bool okDp = WriteSlot(vtbl, kSlotDrawIndexed, (void*)&hkDrawIndexed);
    bool okSs = WriteSlot(vtbl, kSlotSetStreamSource, (void*)&hkSetStreamSource);
    bool okSi = WriteSlot(vtbl, kSlotSetIndices,  (void*)&hkSetIndices);
    bool okVd = WriteSlot(vtbl, kSlotSetVertexDecl, (void*)&hkSetVertexDecl);
    bool okRs = WriteSlot(vtbl, kSlotReset,       (void*)&hkReset);
    bool okDr = WriteSlot(vtbl, kSlotDrawPrim,    (void*)&hkDrawPrim);
    bool okDu = WriteSlot(vtbl, kSlotDrawPrimUP,  (void*)&hkDrawPrimUP);
    bool okDi = WriteSlot(vtbl, kSlotDrawIdxUP,   (void*)&hkDrawIdxUP);
    bool okVh = WriteSlot(vtbl, kSlotSetVertexShader, (void*)&hkSetVertexShader);
    bool okRt = WriteSlot(vtbl, kSlotSetRenderTarget, (void*)&hkSetRenderTarget);
    bool okDs = WriteSlot(vtbl, kSlotSetDepthStencil, (void*)&hkSetDepthStencil);
    bool okCl = WriteSlot(vtbl, kSlotClear,           (void*)&hkClear);
    bool okCd = WriteSlot(vtbl, kSlotCreateDepthStencilSurface, (void*)&hkCreateDepthStencilSurface);
    bool okSr = WriteSlot(vtbl, kSlotStretchRect, (void*)&hkStretchRect);
    bool okCt = WriteSlot(vtbl, kSlotCreateTexture,      (void*)&hkCreateTexture);
    bool okCr = WriteSlot(vtbl, kSlotCreateRenderTarget, (void*)&hkCreateRenderTarget);
    bool okVp = WriteSlot(vtbl, kSlotSetViewport,        (void*)&hkSetViewport);
    bool okTx = WriteSlot(vtbl, kSlotSetTexture,         (void*)&hkSetTexture);
    bool okSc = WriteSlot(vtbl, kSlotSetScissorRect,     (void*)&hkSetScissorRect);
    bool okSm = WriteSlot(vtbl, kSlotSetSamplerState,    (void*)&hkSetSamplerState);
    bool okCf = WriteSlot(vtbl, kSlotColorFill,          (void*)&hkColorFill);
    bool okRd = WriteSlot(vtbl, kSlotGetRenderTargetData, (void*)&hkGetRenderTargetData);
    bool okGd = WriteSlot(vtbl, kSlotGetDepthStencil,     (void*)&hkGetDepthStencil);

        // A slot that did not take is said: a silent dead hook looks like a method the game never calls. Prints only when something
    // is wrong; WriteSlot reads the slot back, so these are landed writes.
    if (!(okVs && okPs && okEs && okDp && okSs && okSi && okVd && okRs && okDr &&
          okDu && okDi && okVh && okRt && okDs && okCl && okCd && okSr && okCt &&
          okCr && okVp && okTx && okSc && okSm && okCf && okRd && okGd))
        LogRecord("  HOOK INSTALL FAILED:"
            "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
            okVs ? "" : " SetVertexShaderConstantF", okPs ? "" : " SetPixelShaderConstantF",
            okEs ? "" : " EndScene",          okDp ? "" : " DrawIndexedPrimitive",
            okSs ? "" : " SetStreamSource",   okSi ? "" : " SetIndices",
            okVd ? "" : " SetVertexDeclaration", okRs ? "" : " Reset",
            okDr ? "" : " DrawPrimitive",     okDu ? "" : " DrawPrimitiveUP",
            okDi ? "" : " DrawIndexedPrimitiveUP", okVh ? "" : " SetVertexShader",
            okRt ? "" : " SetRenderTarget",   okDs ? "" : " SetDepthStencilSurface",
            okCl ? "" : " Clear",             okCd ? "" : " CreateDepthStencilSurface",
            okSr ? "" : " StretchRect",       okCt ? "" : " CreateTexture",
            okCr ? "" : " CreateRenderTarget", okVp ? "" : " SetViewport",
            okTx ? "" : " SetTexture",        okSc ? "" : " SetScissorRect",
            okSm ? "" : " SetSamplerState",   okCf ? "" : " ColorFill",
            okRd ? "" : " GetRenderTargetData", okGd ? "" : " GetDepthStencilSurface");

    if (!okVs)
    {
                // Without the vertex constant hook, put back whatever did land rather than leave a half-install.
        if (okPs) WriteSlot(vtbl, kSlotSetPsConstF, (void*)origSetPsConstF);
        if (okEs) WriteSlot(vtbl, kSlotEndScene,    (void*)origEndScene);
        if (okDp) WriteSlot(vtbl, kSlotDrawIndexed, (void*)origDrawIndexed);
        if (okSs) WriteSlot(vtbl, kSlotSetStreamSource, (void*)origSetStreamSource);
        if (okSi) WriteSlot(vtbl, kSlotSetIndices,  (void*)origSetIndices);
        if (okVd) WriteSlot(vtbl, kSlotSetVertexDecl, (void*)origSetVertexDecl);
        if (okRs) WriteSlot(vtbl, kSlotReset,       (void*)origReset);
        if (okDr) WriteSlot(vtbl, kSlotDrawPrim,    (void*)origDrawPrim);
        if (okDu) WriteSlot(vtbl, kSlotDrawPrimUP,  (void*)origDrawPrimUP);
        if (okDi) WriteSlot(vtbl, kSlotDrawIdxUP,   (void*)origDrawIdxUP);
        if (okVh) WriteSlot(vtbl, kSlotSetVertexShader, (void*)origSetVertexShader);
        if (okRt) WriteSlot(vtbl, kSlotSetRenderTarget, (void*)origSetRenderTarget);
        if (okDs) WriteSlot(vtbl, kSlotSetDepthStencil, (void*)origSetDepthStencil);
        if (okCl) WriteSlot(vtbl, kSlotClear,           (void*)origClear);
        if (okCd) WriteSlot(vtbl, kSlotCreateDepthStencilSurface, (void*)origCreateDepthStencilSurface);
        if (okSr) WriteSlot(vtbl, kSlotStretchRect, (void*)origStretchRect);
        if (okCt) WriteSlot(vtbl, kSlotCreateTexture,      (void*)origCreateTexture);
        if (okCr) WriteSlot(vtbl, kSlotCreateRenderTarget, (void*)origCreateRenderTarget);
        if (okVp) WriteSlot(vtbl, kSlotSetViewport,        (void*)origSetViewport);
        if (okTx) WriteSlot(vtbl, kSlotSetTexture,         (void*)origSetTexture);
        if (okSc) WriteSlot(vtbl, kSlotSetScissorRect,     (void*)origSetScissorRect);
        if (okSm) WriteSlot(vtbl, kSlotSetSamplerState,    (void*)origSetSamplerState);
        if (okCf) WriteSlot(vtbl, kSlotColorFill,          (void*)origColorFill);
        if (okRd) WriteSlot(vtbl, kSlotGetRenderTargetData, (void*)origGetRenderTargetData);
        if (okGd) WriteSlot(vtbl, kSlotGetDepthStencil,     (void*)origGetDepthStencil);
        origSetVertexDecl = nullptr;   // restored and nulled like the rest
        origSetTexture = nullptr; origSetScissorRect = nullptr; origSetSamplerState = nullptr;
        origColorFill = nullptr; origGetRenderTargetData = nullptr; origGetDepthStencil = nullptr;
        origCreateTexture = nullptr; origCreateRenderTarget = nullptr; origSetViewport = nullptr;
        origStretchRect     = nullptr;
        origCreateDepthStencilSurface = nullptr;
        origSetDepthStencil = nullptr;
        origClear           = nullptr;
        origReset           = nullptr;
        origDrawPrim        = nullptr;
        origDrawPrimUP      = nullptr;
        origDrawIdxUP       = nullptr;
        origSetVertexShader = nullptr;
        origSetRenderTarget = nullptr;
        origSetVsConstF     = nullptr;
        origSetPsConstF     = nullptr;
        origEndScene        = nullptr;
        origDrawIndexed     = nullptr;
        origSetStreamSource = nullptr;
        origSetIndices      = nullptr;
        LogRecord("  %-14s vtable %s - slot %d NOT WRITABLE, rolled back",
            from, Where(vtbl), kSlotSetVsConstF);
        return;
    }

        // The table as these hooks left it, for DtRetakeSlots, retaken after the layer installs (HookDeviceVtableFromLocked).
    DtSnapSlots(vtbl);
    hookedVtbl = vtbl;
    DEVSTAT(if (telemetryOn) { hookedVtblLive = true; })
    LogDetail("  %-14s vtable %s (+%lu ms)", from, Where(vtbl), (unsigned long)CenMs());
    LogDetail("                 SetVertexShaderConstantF %s", okVs ? "hooked" : "FAILED");
    LogDetail("                 SetPixelShaderConstantF  %s", okPs ? "hooked" : "FAILED (no c72 probe)");
    LogDetail("                 EndScene                 %s", okEs ? "hooked" : "FAILED (no frame count)");
    LogDetail("                 Reset                    %s", okRs ? "hooked" : "FAILED (a device reset will wedge)");
    LogDetail("                 DrawPrimitive            %s", okDr ? "hooked" : "FAILED (probe only)");
    LogDetail("                 DrawPrimitiveUP / IdxUP  %s / %s", okDu ? "hooked" : "FAILED",
        okDi ? "hooked" : "FAILED");
    LogDetail("                 DrawIndexedPrimitive     %s", okDp ? "hooked" : "FAILED (no object identity)");
    LogDetail("                 SetStreamSource          %s", okSs ? "hooked" : "FAILED");
    LogDetail("                 SetIndices               %s", okSi ? "hooked" : "FAILED");
    LogDetail("                 SetVertexDeclaration     %s", okVd ? "hooked" : "FAILED (skinning undetectable)");
    LogDetail("                 SetVertexShader          %s", okVh ? "hooked" : "FAILED (tyres stay dark)");
    LogDetail("                 SetRenderTarget          %s", okRt ? "hooked" : "FAILED (no HUD boundary)");
    LogDetail("                 CreateTexture / RT / VP  %s / %s / %s (census, render scale)", okCt ? "hooked" : "FAILED",
        okCr ? "hooked" : "FAILED", okVp ? "hooked" : "FAILED");
    LogDetail("                 SetTexture / SetScissor  %s / %s (render scale)", okTx ? "hooked" : "FAILED", okSc ? "hooked" : "FAILED");
        // The bias itself does not need this hook (it goes through the captured original); the hook composes with a bias the game
    // sets.
    LogDetail("                 SetSamplerState          %s (mip LOD bias)", okSm ? "hooked" : "FAILED (a bias of the game's own would be replaced, not composed)");
    LogDetail("                 ColorFill                %s (the last unwatched write path)", okCf ? "hooked" : "FAILED");
    if (!okCt || !okVp || !okTx || !okSc)
        LogRecord("                 the render scale needs CreateTexture, SetViewport, SetTexture and SetScissorRect: off");
}

typedef IDirect3D9* (WINAPI* Create_t)(UINT);

// Quiet only while the wait below is retrying, so a graphics DLL that is present but not ready does not repeat lines; the first
// real attempt is loud, and the wait reports its own outcome.
static bool d3d9ScanQuiet = false;
// The throwaway device covers a late look only: normally the IDirect3D9 hook catches the game's CreateDevice. A device made
// before the hook is caught only through a device of the mod's own.
static bool d3d9LateLook   = false;   // the module turned up only on a re-look
static bool d3d9Throwaway  = false;   // TryModule may create its own device
DEVSTAT(static bool d3d9ToldAdapter = false;)  // the adapter line is once a session (development log only)

// Ask every loaded module for Direct3DCreate9, never one name: here d3d9.dll is FusionFix's proxy, whose export forwards to
// DXVK (vulkan.dll); elsewhere DXVK is d3d9.dll.
static void TryModule(HMODULE mod, const char* name, HWND wnd)
{
    Create_t create = (Create_t)GetProcAddress(mod, "Direct3DCreate9");
    if (!create)
        return;

        // Before the first call into the runtime: on a GPU DXVK refuses, the process ends inside Direct3DCreate9, so this is said
    // here or not at all.
    DxvkWarnIfLastLaunchRefused();
    {
        static bool saidCalling = false;
        if (telemetryOn && !saidCalling)
        {
            saidCalling = true;
            LogRecord("  %-14s calling Direct3DCreate9", name);
        }
    }

    IDirect3D9* d3d = nullptr;
    __try { d3d = create(D3D_SDK_VERSION); }
    __except (EXCEPTION_EXECUTE_HANDLER) { d3d = nullptr; }
    if (!d3d)
    {
        if (!d3d9ScanQuiet) LogDetail("  %-14s exports Direct3DCreate9 but returned nothing", name);
        return;
    }
        // Standalone VCBBridge: FusionFix's proxy forwards to the private bridge client, but the game's own x86 DXVK (vulkan.dll)
    // and the ASI loader also export Direct3DCreate9. With the bridge loaded, only an IDirect3D9 whose vtable lives in the bridge
    // client is the game's renderer, whatever the module order. Looked up here, uncached: FeedVkRemoteModule caches its answer.
    {
        HMODULE bridge = nullptr;
        HMODULE mods[512]; DWORD need = 0;
        if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &need))
            for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; i++)
                if (GetProcAddress(mods[i], "VCRemoteVk_IsRemote")) { bridge = mods[i]; break; }
        if (bridge)
        {
            void* vt = nullptr;
            __try { vt = *(void**)d3d; } __except (EXCEPTION_EXECUTE_HANDLER) { vt = nullptr; }
            HMODULE owner = nullptr;
            if (!vt || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)vt, &owner) || owner != bridge)
            {
                if (!d3d9ScanQuiet) LogDetail("  %-14s Direct3DCreate9 is not the bridge renderer's: skipped", name);
                __try { d3d->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) { }
                return;
            }
        }
    }

        // The adapter, once at log>=1. Not the real GPU: DXVK's GTAIV.exe profile hides NVIDIA, so an NVIDIA GeForce RTX 4090 reads
    // as an AMD Radeon RX 6700 XT (DXVK's log: "Found device: NVIDIA GeForce RTX 4090", "Hiding actual GPU"). The helper's
    // D3D12 line is the real one. The profile also makes alt-tab a device loss (d3d9.deviceLossOnFocusLoss).
#if DLSSIV_DEVLOG                // a D3D call for a development-log line: none in a release
    if (!d3d9ToldAdapter && telemetryOn)
    {
        D3DADAPTER_IDENTIFIER9 ai = {};
        HRESULT air = E_FAIL;
        __try { air = d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ai); }
        __except (EXCEPTION_EXECUTE_HANDLER) { air = E_FAIL; }
        if (SUCCEEDED(air))
        {
            d3d9ToldAdapter = true;
            LogHealth("  %-14s adapter \"%s\" (as D3D9 reports it; DXVK may spoof this, the host's D3D12 line"
                " is the real card), driver \"%s\" %u.%u.%u.%u, vendor 0x%04X device 0x%04X",
                name, ai.Description, ai.Driver,
                (unsigned)HIWORD(ai.DriverVersion.HighPart), (unsigned)LOWORD(ai.DriverVersion.HighPart),
                (unsigned)HIWORD(ai.DriverVersion.LowPart),  (unsigned)LOWORD(ai.DriverVersion.LowPart),
                (unsigned)ai.VendorId, (unsigned)ai.DeviceId);
        }
    }
#endif

        // The interface vtable first, on every start: it needs no device, and it lets hkCreateDevice arm the device hooks from the
    // game's own device. Read the original first, write second.
    if (!hookedD3dVtbl)
    {
        void** dv = *(void***)d3d;
        if (IsReadable(dv, (kD3dSlotCreateDevice + 1) * sizeof(void*)) && dv[kD3dSlotCreateDevice] != (void*)&hkCreateDevice)
        {
            origCreateDevice = (CreateDevice_t)dv[kD3dSlotCreateDevice];
            ivtCreateDeviceFirst = origCreateDevice;   // the pointer the take-back's judgement and the re-entry guard go by
            if (WriteSlot(dv, kD3dSlotCreateDevice, (void*)&hkCreateDevice))
            { hookedD3dVtbl = dv; strncpy_s(d3d9From, name, _TRUNCATE);
              // log=1: which module supplied Direct3DCreate9 (FusionFix's proxy, ENB's, ReShade's, the system d3d9) and whether
              // the hook went in.
              LogRecord("  %-14s Direct3DCreate9 OK, IDirect3D9::CreateDevice hooked", name);
              // WINDOWS' COMPATIBILITY LAYER IS ON when the slot already belonged to its apphelp.dll (Windows' own file, in a
              // system folder): the game's import of Direct3DCreate9 is wrapped then, and only then (IvtWrapGameCreate9).
              { char fm[MAX_PATH]; const HMODULE fh = IvtModuleName((const void*)ivtCreateDeviceFirst, fm, sizeof(fm));
                if (fh && !_stricmp(fm, "apphelp.dll") && DtForeignDirOk(fh)) IvtWrapGameCreate9(GetModuleHandleA(nullptr)); } }
            else
            {
                origCreateDevice = nullptr;
                ivtCreateDeviceFirst = nullptr;
                // A refused vtable write leaves the mod unable to see the game's device: said at every level.
                LogRecord("  %-14s Direct3DCreate9 OK, but IDirect3D9::CreateDevice could NOT be hooked (the vtable refused): the mod does not run", name);
            }
        }
    }

        // Preconditions said at log=1 before any device exists, from files alone.
    LogD3d9CfgApi();
    LogDxvkVersion(false);

        // A throwaway device only when the race is real (d3d9LateLook). If another hook took the slot, the device hooks are armed
    // from the mod's own device. Not on a non-DXVK runtime.
    if (d3d9Throwaway && !hookedVtbl && hookedD3dVtbl && !fatalBeforeHooks)
    {
        void** dvNow = *(void***)d3d;
        static bool saidLost = false;
                // Also when it was taken back: a wait that still ended without the game's device means the game made it through the
        // other hook before the take-back landed.
        const bool lostNow = IsReadable(dvNow, (kD3dSlotCreateDevice + 1) * sizeof(void*)) && dvNow[kD3dSlotCreateDevice] != (void*)&hkCreateDevice;
        if (!saidLost && (lostNow || ivtSlotRetaken))
        {
            saidLost = true;
            char mn[MAX_PATH] = "an unnamed region";
            unsigned long off = ivtSlotTakerOff;
            if (lostNow)
            {
                const void* holder = dvNow[kD3dSlotCreateDevice];
                const HMODULE hm = IvtModuleName(holder, mn, sizeof(mn));
                off = hm ? (unsigned long)((const BYTE*)holder - (const BYTE*)hm) : 0ul;
            }
            else strcpy_s(mn, ivtSlotTaker);
            LogRecord("  THE CreateDevice HOOK WAS REPLACED: IDirect3D9::CreateDevice %s %s (+0x%lX), hkCreateDevice ran %ld time(s): the game's device was made without this mod",
                      lostNow ? "now points into" : "was reclaimed from", mn, off, (long)ivtCreateDeviceEntries);
            IvtWarn("Another mod (%s) replaced DLSS-IV's Direct3D hook as the game started: HDR and the render scale start only after"
                " you change the resolution once or toggle fullscreen. Restarting the game usually avoids it", mn);
        }
    }
    static int ivtThrowawaysMade = 0;
    if (d3d9Throwaway && !hookedVtbl && ivtThrowawaysMade >= 4)
    {
        static bool saidCap = false;
        if (!saidCap) { saidCap = true; LogRecord("  %-14s no more throwaway devices (four made, none armed the hooks)", name); }
    }
    else if (d3d9Throwaway && !hookedVtbl && !fatalBeforeHooks)   // and no throwaway device on a runtime that is not DXVK: it could arm nothing
    {
        ivtThrowawaysMade++;
        D3DPRESENT_PARAMETERS pp = { 0 };
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = wnd;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;

        IDirect3DDevice9* dev = nullptr;
        HRESULT hr = E_FAIL;
                // The latch: this call goes through hkCreateDevice and must not be taken for the game's.
        __try
        {
            ivtOwnCreateTid = GetCurrentThreadId();
            hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                    D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_NOWINDOWCHANGES,
                    &pp, &dev);
            ivtOwnCreateTid = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { ivtOwnCreateTid = 0; hr = E_FAIL; dev = nullptr; }

        if (SUCCEEDED(hr) && dev)
        {
            // log=1: this path runs only when the game's own CreateDevice did not arrive in time, the start a "game will not
            // launch" report needs to see.
            LogRecord("  %-14s throwaway device made for the vtable (%s)", name,
                          d3d9LateLook ? "found late: a game device may exist already" : "the game's CreateDevice was late");
                        // The hook above already ran HookDeviceVtableFrom on this device, unless it did not run (the startup miss): then
            // this is the one call that arms them. Idempotent.
            if (!hookedVtbl) HookDeviceVtableFrom(dev, "the mod's throwaway device (the CreateDevice hook did not run for it)");
            __try { dev->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) { }
        }
        else
        {
            if (!d3d9ScanQuiet) LogRecord("  %-14s CreateDevice failed", name);
        }
    }
    __try { d3d->Release(); } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// One pass over every loaded module, asking each for Direct3DCreate9; repeated while the game is still loading. The caller owns
// the throwaway window.
static void ScanModulesForD3D9(HWND wnd)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me = { sizeof(me) };
    if (Module32FirstW(snap, &me))
    {
        do
        {
            char name[64];
            WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, name,
                                sizeof(name), nullptr, nullptr);
            TryModule(me.hModule, name, wnd);
            // The slot, looked at between modules too: another module's Direct3DCreate9 can be slow. Nothing when it is the
            // mod's.
            IvtCheckCreateDeviceSlot();
        } while (Module32NextW(snap, &me) && !hookedVtbl);
    }
    CloseHandle(snap);
}

// Is the entry point there at all? Asked without calling it: one poll costs a module snapshot and a GetProcAddress per module,
// with no side effect on the game.
static bool AnyModuleExportsD3D9()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    MODULEENTRY32W me = { sizeof(me) };
    if (Module32FirstW(snap, &me))
        do { if (GetProcAddress(me.hModule, "Direct3DCreate9")) { found = true; break; } }
        while (Module32NextW(snap, &me));
    CloseHandle(snap);
    return found;
}

static const unsigned kD3d9PollMs =    25;   // between looks while waiting
static const unsigned kD3d9WaitMs = 60000;   // give up after this: a D3D9 game always beats it

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------


// ---- the engine name the driver sees ----
// NVIDIA's driver sends engine "DXVK" down a composited, SDR-only windowed present path. In HDR, and in SDR on NVIDIA, the mod
// patches vulkan-1.dll's vkGetInstanceProcAddr export in memory so DXVK's instance gets another name: on NVIDIA the RTX Remix
// name, which keeps the driver's DXVK paths; elsewhere "DXVK-HDR".
static PFN_vkGetInstanceProcAddr hdrOrigGipa = nullptr;
static PFN_vkCreateInstance      hdrOrigCreateInstance = nullptr;
static const char*               hdrEngineName = "DXVK-HDR";   // the driver matches the exact string "DXVK"; this keeps the lineage readable
static bool HdrRenameDecide();                                  // at DXVK's first instance: rename or not, from NVIDIA's per-game G-Sync setting or, on a hybrid, a DXVK device filter (after DllMain)
static void HdrRenameSetup(bool sdr, bool nvPrimary, bool hybrid, const wchar_t* primary);   // what the decision needs, from the hook's install (after DllMain)
static bool IvtNvidiaDriverPresent();                 // after DllMain: NVIDIA's driver is installed (its x86 nvapi.dll is there)
// hdrInstancesRenamed, the count HdrCreateInstance keeps, is declared in hdr.h so the engage line can say when it is zero.

static VkResult VKAPI_CALL HdrCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* a, VkInstance* out)
{
    if (!hdrOrigCreateInstance) return VK_ERROR_INITIALIZATION_FAILED;
    if (!ci || !ci->pApplicationInfo) return hdrOrigCreateInstance(ci, a, out);
    if (!HdrRenameDecide()) return hdrOrigCreateInstance(ci, a, out);   // G-Sync is switched off for GTA IV: SDR keeps "DXVK" (after DllMain)
    VkApplicationInfo app = *ci->pApplicationInfo;
    const char* was = app.pEngineName ? app.pEngineName : "(none)";
    app.pEngineName = hdrEngineName;
    VkInstanceCreateInfo ci2 = *ci;
    ci2.pApplicationInfo = &app;
    const VkResult r = hdrOrigCreateInstance(&ci2, a, out);
    ++hdrInstancesRenamed;
    LogHealth("  hdr: vkCreateInstance #%d: engine \"%s\" -> \"%s\" (app \"%s\") -> %d", hdrInstancesRenamed, was, hdrEngineName,
        app.pApplicationName ? app.pApplicationName : "(none)", (int)r);
    return r;
}

static PFN_vkVoidFunction VKAPI_CALL HdrGetInstanceProcAddr(VkInstance inst, const char* name)
{
    if (name && !strcmp(name, "vkCreateInstance"))
    {
        if (!hdrOrigCreateInstance) hdrOrigCreateInstance = (PFN_vkCreateInstance)hdrOrigGipa(inst, name);
        return (PFN_vkVoidFunction)&HdrCreateInstance;
    }
    return hdrOrigGipa(inst, name);
}

// One entry of a loaded module's export address table, rewritten in memory.
static bool HdrPatchExport(HMODULE mod, const char* name, void* fn)
{
    BYTE* base = (BYTE*)mod;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const IMAGE_DATA_DIRECTORY& dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd.VirtualAddress) return false;
    const IMAGE_EXPORT_DIRECTORY* ex = (const IMAGE_EXPORT_DIRECTORY*)(base + dd.VirtualAddress);
    const DWORD* names = (const DWORD*)(base + ex->AddressOfNames);
    const WORD*  ords  = (const WORD*)(base + ex->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)(base + ex->AddressOfFunctions);
    for (DWORD i = 0; i < ex->NumberOfNames; ++i)
    {
        if (strcmp((const char*)(base + names[i]), name) != 0) continue;
        DWORD* slot = &funcs[ords[i]];
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(DWORD), PAGE_READWRITE, &old)) return false;
        *slot = (DWORD)((BYTE*)fn - base);
        VirtualProtect(slot, sizeof(DWORD), old, &old);
        return true;
    }
    return false;
}

static bool IvtNvidiaDrivesPrimary(wchar_t* name, size_t cch);   // defined after DllMain, so no hot symbol moves

static void HdrInstallEngineNameHook()
{
    if (ivtCfg.hdr_engine_name <= 0) return;
        // In SDR too, on NVIDIA (rename_sdr): renamed, a borderless window gets G-Sync. One name for both, so the driver's caches
    // stay.
    const bool sdr = ivtCfg.hdr <= 0;
        // Never under Wine: Linux tools (Mesa's driconf, gamescope, Fossilize) want exactly "DXVK".
    if (ivtWine)
    {
        if (!sdr) LogRecord("  hdr: the engine name stays \"DXVK\" under Wine");
        return;
    }
    wchar_t primary[128] = L"";
    if (sdr && !ivtCfg.rename_sdr) return;   // the cfg line says so
    // For SDR the gate; for HDR, whether NVIDIA's per-game G-Sync setting is worth asking about at DXVK's first instance.
    const bool nvPrimary = IvtNvidiaDrivesPrimary(primary, _countof(primary));
        // A hybrid laptop rendering on NVIDIA: DXVK's first instance decides (HdrDecideBody).
    const bool hybrid = sdr && !nvPrimary;
    if (hybrid && !IvtNvidiaDriverPresent())
    {
        LogHealth("  engine name: SDR, the primary display is not on an NVIDIA GPU (%ls) and no NVIDIA driver is installed: DXVK keeps \"DXVK\"", primary);
        return;
    }
    hdrEngineName = ivtCfg.hdr_engine_name == 1 ? "D3D9 on Vulkan (HDR)" : "DXVK-HDR";
        // The RTX Remix name (hdr_engine_name=3) where NVIDIA renders: any other name costs about 9 percent of GPU time.
    if (ivtCfg.hdr_engine_name == 3 && (nvPrimary || hybrid)) hdrEngineName = "DXVK_NvRemix";   // a hybrid too, rendering on NVIDIA
    HMODULE vk = LoadLibraryA("vulkan-1.dll");           // the loader DXVK will use; kept loaded
    if (!vk) { LogRecord("  hdr: vulkan-1.dll is not loadable: DXVK's engine name stays"); return; }
    hdrOrigGipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(vk, "vkGetInstanceProcAddr");
        // Decided at DXVK's first instance (HdrDecideOnce). Its inputs are written before the export is patched, since a
    // Direct3DCreate9 may reach it at once.
    HdrRenameSetup(sdr, nvPrimary, hybrid, primary);
    if (!hdrOrigGipa || !HdrPatchExport(vk, "vkGetInstanceProcAddr", (void*)&HdrGetInstanceProcAddr))
    { LogRecord("  hdr: vulkan-1.dll's vkGetInstanceProcAddr export could not be patched: DXVK's engine name stays"); return; }
    if (hybrid)
        LogHealth("  engine name: the hook is in (SDR, the primary display is on %ls); whether DXVK is renamed is decided at its first"
                  " instance, from the GPU it will render on", primary);
    else if (sdr)
        LogHealth("  engine name: the hook is in (SDR, NVIDIA drives the primary display: %ls); whether DXVK is renamed is decided"
                  " at its first instance, from NVIDIA's per-game G-Sync setting", primary);
    else
        LogHealth("  hdr: vulkan-1.dll's vkGetInstanceProcAddr export patched before any DXVK instance exists;"
            " DXVK will identify to the driver as engine \"%s\" (NVIDIA presents windows as SDR for engine \"DXVK\")", hdrEngineName);
}

static DWORD WINAPI InitThread(LPVOID)
{
    initAt = GetTickCount();
        // First, before any hook and any other thread: the shared-image lock.
    InitializeCriticalSection(&ivtSharedCs);
        // A second copy of this .asi installs nothing (see dlssivDuplicate): it waits for the first copy to clear last session's
    // files, says so where a player sees it, and is inert from here.
    if (dlssivDuplicate)
    {
                // A handshake, not a timer: the first copy sets the event once the files are cleared. The timeout is for a first copy
        // that died before that.
        if (dlssivClearedEv) WaitForSingleObject(dlssivClearedEv, 30000); else Sleep(3000);
        char me[MAX_PATH] = "";
        GetModuleFileNameA(selfModule, me, MAX_PATH);
        IvtWarn("a second copy of DLSS-IV.asi is loaded (%s). Only the first copy runs; remove this one, so that one game"
                " has one DLSS-IV.asi in one folder", me);
        NoticeTellFirstCopy();      // this copy draws nothing: the first one says it on the screen (source/notice.h)
        dead = true;
        return 0;
    }
    IvtFatalNoteClear();   // last session's note and warnings file, before anything here can write new ones
    NoticeWatchSecondCopy();                            // before a second copy is let on: it sets the event this makes
    if (dlssivClearedEv) SetEvent(dlssivClearedEv);   // a second copy may now write its line
        // Order matters: the cfg carries the log level, which decides whether a log file is created, so the cfg is read first and
    // IvtReadCfg stays silent until IvtLogCfgSource can speak.
    IvtReadCfg();   // early: quality decides sizes before the game makes a target
    IvtDetectWine(); // Wine or Windows, and the sync mode from it and the cfg
    LogInit(ivtCfg.log);
    IvtLogCfgSource();
    if (ivtCfgMissing)
    {
                // No DLSS-IV.cfg: nothing is hooked, spawned or run, and the note says so with both paths looked in (at log=0 there is
        // no log).
        char why[2 * MAX_PATH + 160];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "no DLSS-IV.cfg was found. Looked for %sDLSS-IV.cfg (beside the .asi)%s%s%s."
            " That file is the master switch: without it nothing runs. Copy it back from the download.",
            ivtCfgOwnDir,
            (ivtCfgGameDir[0] && _stricmp(ivtCfgOwnDir, ivtCfgGameDir) != 0) ? " and " : "",
            (ivtCfgGameDir[0] && _stricmp(ivtCfgOwnDir, ivtCfgGameDir) != 0) ? ivtCfgGameDir : "",
            (ivtCfgGameDir[0] && _stricmp(ivtCfgOwnDir, ivtCfgGameDir) != 0) ? "DLSS-IV.cfg (next to the game exe)" : "");
        IvtFatalNote(why, 0, false, true);   // noCfg: the note's own short form
        return 0;
    }
    IvtLogCfg();          // the cfg dump, now that there is somewhere to put it
    IvtLogPlatform();     // one line under Wine, or when sync= was set by hand
    LatSetDxvkEnv();      // the frame queue into DXVK_CONFIG before any DXVK instance exists
    LatSetDxvkConstants();   // DXVK 2.x's constant path, unless the player's dxvk.conf or DXVK_CONFIG names it
    LatGuardDxvkKeys();      // a player's dxvk.latencySleep = True turned off where it clashes; four costly keys said
        // The recorded state-block Apply on Windows only; DtSbProbe (devthread/dt_sbtab.h) can still turn it off.
    dtSbRecOn = !ivtWine;
    if (ivtWine && ivtCfg.threaded >= 1)   // only where there is a device thread to say it of
        LogRecord("  threaded: under Wine, state blocks are applied drained (no recorded Apply yet)");
    LatReadPresentIntervalCfg();   // dxvk.conf's d3d9.presentInterval, if set, decides the pacer's vsync stand-down
    LatReadLimiterCfg();           // FusionFix's FPS limit or DXVK's maxFrameRate: the pacer stands down under a limiter
        // Read again at the game's device when FusionFix had not loaded: it may create its cfg when it loads.
    latLimiterReread = latOn && !GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");
    HdrInstallEngineNameHook();   // before the census: it must precede DXVK's first instance
    shpBuilt = (ivtCfg.sharpness > 0.0f);   // the hard gate, latched once and never moved
    shpOn    = shpBuilt;                    // the run gate starts equal to it; only a live edit parts them
        // Start the helper now: its D3D12 and NGX setup (about a second) reads nothing of the mod's, so it need not wait behind the
    // viewport scan. The connect stays per frame in IvtEnsureHost.
    // Standalone VCBBridge: the bridge server starts its own paired helper (plugins\VCBBridge\dlssiv-helper-bridge.exe). The
    // game's DLSSIV\dlssiv-helper.exe belongs to the stock DLSS-IV build; spawning it here would fail the whole transport.
    if (ivtCfg.transport && IvtServerUpscaleAvailable())
        LogRecord("  transport: the private bridge's native 64-bit service runs the upscaler; the game's DLSSIV helper is not started");
    else if (ivtCfg.transport) IvtSpawnHost();
    else
        LogDetail("  jitter phases: from NVIDIA's rule once the render ratio is known (cfg jitter_phases=0)");

#if !DLSSIV_DEVLOG
    if (logLevel > 1)
        LogRecord("log=%d asked: this release build writes log=1 (2 and 3 are development levels)", logLevel);
#endif
    char ver[64] = "unknown";
    ExeVersion(ver, sizeof(ver));
    LogRecord("GTAIV.exe version %s", ver);

        // --- the device, first ---
    // Before the camera scan and the viewport wait, so the game's own CreateDevice and its boot render targets pass through the
    // hooks. Every hook early-outs until the verdict.
    {
        HWND wnd = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 1, 1,
                                   nullptr, nullptr, nullptr, nullptr);
        if (!wnd)
        {
            // Through IvtFail: the same total stop as the unrecognised-exe case below, with its note.
            char why[160];
            _snprintf_s(why, sizeof(why), _TRUNCATE, "the window the D3D9 module scan needs could not be created (Windows error %lu)", GetLastError());
            fatalBeforeHooks = true;   // the note: nothing of the mod runs (IvtFatalNote)
            IvtFail(why);
            dead = true;
            return 0;
        }

        LogDetail("");
        LogDetail("looking for Direct3DCreate9 across every loaded module (+%lu ms):", (unsigned long)CenMs());
        d3d9LateLook = false; d3d9Throwaway = false;      // the first look: hook the interface, make no device
        ScanModulesForD3D9(wnd);
        DestroyWindow(wnd);

                // The graphics DLL may not be mapped at the first look: then the scan waits.
        if (hookedD3dVtbl && !hookedVtbl)
        {
            // The normal start: wait, bounded, for hkCreateDevice to arm the device hooks; past the bound the throwaway path
            // catches a device made before the hook.
            const DWORD waitFrom = GetTickCount();
            // every 25 ms; every millisecond once the slot has been given its first pointer back (Windows' compatibility layer
            // does it at each Direct3DCreate9, and the game's CreateDevice follows its own Direct3DCreate9 closely)
            while (!hookedVtbl && GetTickCount() - waitFrom < kD3d9WaitMs) { Sleep(ivtSlotRestores ? 1 : kD3d9PollMs); IvtCheckCreateDeviceSlot(); }
            if (!hookedVtbl)
            {
                LogHealth("  the game's CreateDevice did not come within %u s of the interface hook; a device may"
                    " already exist, so a throwaway device the mod creates reaches the shared vtable", kD3d9WaitMs / 1000);
                d3d9Throwaway = true;
                HWND again = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
                if (again) { ScanModulesForD3D9(again); DestroyWindow(again); }
            }
        }
        if (!hookedVtbl)
        {
            const DWORD waitFrom = GetTickCount();
            unsigned    polls    = 0;
            bool        loud     = true;   // the first real attempt says why; repeats stay quiet
            LogHealth("  nothing exports it yet, so the graphics DLL is not mapped. Waiting for it"
                " (a look every %u ms, up to %u s); the game cannot draw before it either",
                kD3d9PollMs, kD3d9WaitMs / 1000);
                        // A late look: the DLL appeared while waiting, so the game is about to create its device or already has. That is
            // the race the throwaway device exists for, allowed here and nowhere else.
            d3d9LateLook = true; d3d9Throwaway = true;
            while (!hookedVtbl && GetTickCount() - waitFrom < kD3d9WaitMs)
            {
                Sleep(kD3d9PollMs);
                polls++;
                if (!AnyModuleExportsD3D9()) continue;
                d3d9ScanQuiet = !loud; loud = false;
                HWND again = CreateWindowExA(0, "STATIC", "", WS_OVERLAPPED, 0, 0, 1, 1,
                                             nullptr, nullptr, nullptr, nullptr);
                if (!again) break;
                ScanModulesForD3D9(again);
                DestroyWindow(again);
            }
            d3d9ScanQuiet = false;
            LogHealth("  %s after %lu ms and %u look(s)", hookedVtbl ? "it arrived" : "STILL NOTHING",
                (unsigned long)(GetTickCount() - waitFrom), polls);
        }

        if (!hookedVtbl)
        {
            // No interface hooked means nothing exports Direct3DCreate9: the session is over.
            if (!hookedD3dVtbl)
            {
                fatalBeforeHooks = true;   // the note: nothing of the mod runs (IvtFatalNote)
                IvtFail("no module in the game exported a usable Direct3DCreate9 within the wait, so nothing of this mod can hook"
                        " the device. DXVK (vulkan.dll behind FusionFix's d3d9.dll, or d3d9.dll itself) is not loaded, or was"
                        " loaded from a folder this scan does not see");
                dead = true;
                return 0;
            }
            LogRecord("IDirect3D9 hooked, the game's CreateDevice not yet come");
        // The startup miss: the game's device was made and none reached
            // hkCreateDevice. Development builds say what the slot holds now, who owns it, and how often the hook was entered.
        }
        else
            LogHealth("  device hooks live at +%lu ms; now the camera global and the viewport", (unsigned long)CenMs());
    }

        // --- the camera global ---
    // patA and patB (FusionFix's) match 1.0.8.0 only. patCE was derived from 1.0.8.0's slot 0x018CADA4 by windowing its .text
    // sites, wildcarding addresses and call targets, and matching them in CE (0x017F583C). Each matches once; ValidViewport
    // still vets it.
    static const BYTE patA[] = { 0x8B, 0x35, 0, 0, 0, 0, 0x75, 0x14 };
    static const BYTE patB[] = { 0x8B, 0x3D, 0, 0, 0, 0, 0x75, 0x14, 0x6A, 0x00 };
    static const BYTE patCE[] = {
        0xCC, 0x51, 0x8B, 0x0D, 0, 0, 0, 0, 0x56, 0xE8, 0, 0, 0, 0,
        0xF3, 0x0F, 0x10, 0x00, 0x8B, 0x0D, 0, 0, 0, 0,
        0xF3, 0x0F, 0x11, 0x44, 0x24, 0x04, 0xE8, 0, 0, 0, 0,
        0xF3, 0x0F, 0x10, 0x40, 0x08, 0x8B, 0x74, 0x24, 0x0C };
    const int hitsA  = ScanFor(patA,  "xx????xx",   2);
    const int hitsB  = ScanFor(patB,  "xx????xxxx", 2);
    const int hitsCE = ScanFor(patCE, "xxxx????xx????xxxxxx????xxxxxxx????xxxxxxxxx", 20);
    if (ivtCfg.hdr >= 1) MenuFlagFind();   // its one reader is the HDR encode's UI mask; not gated on hdr_ui, a live key that may be turned on later

    LogDetail("");
    LogDetail("scanning .text for grcViewport** (FusionFix's patterns):");
    LogDetail("  %d match(es), %d distinct candidate slot(s)%s",
        candidateHits, candidates,
        candidates >= MAX_CANDIDATES ? "  [CAP HIT - some were dropped]" : "");
    if (candidates == 0)
    {
        LogRecord("NO CANDIDATES: no grcViewport pattern matched this GTAIV.exe: not proceeding");
                // Through IvtFail, so the note and the soft-picture line appear, with the exe's version and the pattern counts.
        char why[320];
        if (hitsA < 0 || hitsB < 0 || hitsCE < 0)   // the scan never ran: not the same answer as three zeros
            _snprintf_s(why, sizeof(why), _TRUNCATE, "this GTAIV.exe (version %s) could not be scanned: its .text section could not be"
                " read, so the grcViewport patterns were never tried. That is a fault in the process, not a build this mod does not know", ver);
        else
            _snprintf_s(why, sizeof(why), _TRUNCATE, "this GTAIV.exe (version %s) is not a build this mod recognises: its grcViewport code"
                " matched none of the three patterns (1.0.8.0 pattern A %d, pattern B %d, Complete Edition %d hits in .text). Verified"
                " builds: 1.0.8.0 and the Complete Edition; 1.0.7.0, and an exe a downgrader has patched, read like this",
                ver, hitsA, hitsB, hitsCE);
        IvtFail(why);
        dead = true;
        return 0;
    }

        // The viewport does not exist at injection time: poll while the game boots, forever (dead is one-way). The deadline only
    // reports.
    char why[160];
    for (int attempt = 0; !viewportSlot; attempt++)
    {
        if (attempt == 120)
        {
            // Name the likely cause: no candidates at all is a build problem no wait fixes; candidates that keep failing
            // validation may just be a menu.
            if (candidates == 0)
                LogRecord("  NO CANDIDATES after 60 s: the viewport patterns do not match this executable");
            LogRecord("  no candidate validated in 60 s, still polling; each rejected because:");
            // The reasons reach log=1 and survive a release build (LogDetail would not). Once, at the 60 s mark, capped, behind
            // telemetryOn.
            const int shown = candidates < 8 ? candidates : 8;
            if (telemetryOn)
            {
                for (int i = 0; i < shown; i++)
                {
                    BYTE* vp = nullptr;
                    if (IsReadable(candidate[i], sizeof(void*)))
                        __try { vp = *candidate[i]; }
                        __except (EXCEPTION_EXECUTE_HANDLER) { }
                    ValidViewport(vp, why, sizeof(why));
                    LogRecord("    [%2d] %-28s %s", i, Where(candidate[i]), why);
                }
                if (candidates > shown)
                    LogRecord("    (and %d more)", candidates - shown);
            }
        }
        for (int i = 0; i < candidates && !viewportSlot; i++)
        {
            BYTE** slot = candidate[i];
            if (!IsReadable(slot, sizeof(void*)))
                continue;
            BYTE* vp = nullptr;
            bool got = false;
            __try { vp = *slot; got = true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { }
            if (!got)
                continue;
            if (ValidViewport(vp, why, sizeof(why)))
            {
                viewportSlot = slot;
                LogDetail("  ACCEPTED slot %s", Where(slot));
                LogDetail("           -> grcViewport %s", Where(vp));
                LogHealth("           %s", why);
            }
        }
        if (!viewportSlot)
            Sleep(500);
    }

    // --- the shared block --------------------------------------------------
    LogHealth("");
    OpenShared();

    armedAt = GetTickCount();
    if (!fatalBeforeHooks) LogRecord("armed (+%lu ms)", (unsigned long)CenMs());   // not after "the runtime is not DXVK"

        // Poll so the log always reaches a verdict: when the hook is live it pronounces first; this covers the case where it never
    // fires.
    for (int i = 0; i < 240 && !toldVerdict; i++)
    {
        Sleep(250);
        Verdict(false);
    }
    if (!toldVerdict)
        LogRecord("NO VERDICT after 60 s: grcViewport resolved, its projection never looked like a scene camera");
    return 0;
}

// The session's last line. Written with CreateFile/WriteFile, not the C runtime: in DLL_PROCESS_DETACH a killed thread may
// still hold the FILE lock, and an fprintf would hang the exit.
static void LogSessionOver()
{
    if (logLevel < 1 || !logPath[0] || !hookedVtbl) return;      // not a session the mod ran in
        // The latency line, formatted here and written below through the same handle, for the same reason.
    char lat[2048]; int ln = LatSummaryText(lat, sizeof(lat), true);
    if (ln > 0 && ln < (int)sizeof(lat) - 2) { lat[ln++] = '\r'; lat[ln++] = '\n'; lat[ln] = 0; } else ln = 0;
    char s[1600]; int n = 0;
    n += _snprintf_s(s + n, sizeof(s) - n, _TRUNCATE,
        "session over: %ld frames, %ld injections, %ld device reset(s), resolve %s at the end; %ld camera cut(s)",
        (long)frames, ivtInjections, statDeviceResets, resolveActive ? "ON" : "off", statCamJumps);
    if (statCamJumps) n += _snprintf_s(s + n, sizeof(s) - n, _TRUNCATE, " (worst %.0f units)", statCamJumpWorst);
    if (ivtWatchSlow > 40 || ivtFrameGaps > 40)   // the lines stop at 40 of each; the totals do not
        n += _snprintf_s(s + n, sizeof(s) - n, _TRUNCATE, "; SLOW %ld and FRAME GAP %ld in all, 40 of each listed",
                         (long)ivtWatchSlow, (long)ivtFrameGaps);
    n += _snprintf_s(s + n, sizeof(s) - n, _TRUNCATE, "; %s; %ld warning(s)%s\r\n",
        ivtFailed ? "THE TRANSPORT HAD FAILED" : "the transport was up", (long)ivtWarnCount,
        (ivtWarnCount > ivtWarnSaid && ivtWarnSaid < 8) ? ", of which these were not listed above:" :
        ivtWarnCount > ivtWarnSaid ? " (the first eight are listed above; DLSS-IV-WARNINGS.txt has them all)" : "");
    for (LONG i = ivtWarnSaid; i < ivtWarnCount && i < 8 && n > 0 && n < (int)sizeof(s) - 8; i++)
    {
                // _TRUNCATE answers -1, which is not a length: stop at the first warning that does not fit.
        const int r = _snprintf_s(s + n, sizeof(s) - n, _TRUNCATE, "    - %s\r\n", ivtWarnKept[i]);
        if (r < 0) { n = (int)strlen(s); break; }
        n += r;
    }
    if (n <= 0) return;
    const DWORD w = logMutex ? WaitForSingleObject(logMutex, 200) : WAIT_FAILED;
    const HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD wrote = 0; if (ln > 0) WriteFile(h, lat, (DWORD)ln, &wrote, nullptr); WriteFile(h, s, (DWORD)strlen(s), &wrote, nullptr); CloseHandle(h); }
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) ReleaseMutex(logMutex);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        selfModule = (HMODULE)inst;   // for the cfg lookup: see IvtOwnDir
        DisableThreadLibraryCalls(inst);
        // The single-instance mutex (see dlssivDuplicate): kernel32 only, safe under
        // the loader lock. GetLastError is read in the same expression, before
        // anything else can disturb it.
        char instanceName[64];
        _snprintf_s(instanceName, sizeof(instanceName), _TRUNCATE, "Local\\DlssIvInstance.%lu", GetCurrentProcessId());
        dlssivInstanceMutex = CreateMutexA(nullptr, FALSE, instanceName);
        dlssivDuplicate = dlssivInstanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
        // And the event the first copy sets once last session's files are cleared
        // (InitThread); a second copy waits on it before writing its warning.
        _snprintf_s(instanceName, sizeof(instanceName), _TRUNCATE, "Local\\DlssIvCleared.%lu", GetCurrentProcessId());
        dlssivClearedEv = CreateEventA(nullptr, TRUE, FALSE, instanceName);
        // Off the loader lock. Creating a D3D9 device inside DllMain deadlocks
        // as often as it works.
        HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // Stop publishing, but do NOT unmap. The render thread may be inside
        // Publish() right now, and racing it to tear down a mapping the OS is
        // about to reclaim anyway would trade a guaranteed-safe exit for a
        // crash on the way out.
        dead = true;
        LogSessionOver();
        IvtShutdown();   // close the pipe so the host exits; no Vulkan teardown
    }

    // ALWAYS TRUE. A failed init leaves an inert module; it never takes the
    // game down. There is no abort path in this file.
    return TRUE;
}

// The display's refresh, the integer mode of the game's monitor (EnumDisplaySettingsW, as DXVK reads it). Defined after DllMain
// so no hot symbol moves.
static void LatSayRefresh(bool atReset)
{
    static UINT lastHz = 0;
    const HMONITOR mon = MonitorFromWindow(hdrGameWnd ? hdrGameWnd : GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);   // hdrGameWnd: set at CreateDevice and Reset
    MONITORINFOEXW mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
    DEVMODEW dm; ZeroMemory(&dm, sizeof(dm)); dm.dmSize = sizeof(dm);
    const UINT hz = (mon && GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
                  ? dm.dmDisplayFrequency : 0;   // 0 and 1 mean "the hardware's default", which says nothing
    if (hz && hz != lastHz)
        LogRecord("  display: %u Hz (%ls)%s", hz, mi.szDevice, atReset ? ", after a Reset" : "");
    lastHz = hz;
}

// Whether the adapter Windows marks primary is NVIDIA's (vendor 10DE); read before any window exists.
static bool IvtNvidiaDrivesPrimary(wchar_t* name, size_t cch)
{
    for (DWORD i = 0; i < 16; ++i)
    {
        DISPLAY_DEVICEW dd; ZeroMemory(&dd, sizeof(dd)); dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) continue;
        wcsncpy_s(name, cch, dd.DeviceString, _TRUNCATE);
        return _wcsnicmp(dd.DeviceID, L"PCI\\VEN_10DE", 12) == 0;
    }
    wcsncpy_s(name, cch, L"none marked primary", _TRUNCATE);
    return false;
}

// NVIDIA's driver is installed: its x86 nvapi.dll in the system folder (SysWOW64 for a 32-bit process). No file, no NVIDIA GPU
// to render on.
static bool IvtNvidiaDriverPresent()
{
    wchar_t p[MAX_PATH]; const UINT n = GetSystemDirectoryW(p, MAX_PATH);
    if (!n || n >= MAX_PATH - 12) return false;
    wcscat_s(p, L"\\nvapi.dll");
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

// THE PER-GAME G-SYNC SWITCH (read-only, NVIDIA only). With G-Sync off for GTA IV alone, a renamed DXVK tears, so the SDR
// rename keeps "DXVK"; HDR keeps the rename and warns. Ids and structures from NVIDIA's public headers (github.com/NVIDIA/nvapi
// at 70d337db). To the END line: tools/test-gsync-read/gen.py cuts it out and runs it against the driver.
#pragma pack(push, 4)
struct IvtNvDrsSetting                          // NVDRS_SETTING_V1, nvapi.h:24486-24516 (declared under pack 4 there)
{
    unsigned       version;                     // MAKE_NVAPI_VERSION(NVDRS_SETTING_V1, 1), nvapi.h:24518
    unsigned short settingName[2048];           // NvAPI_UnicodeString, nvapi.h:1064-1067
    unsigned       settingId;
    int            settingType;                 // NVDRS_SETTING_TYPE, nvapi.h:24397-24404
    int            settingLocation;             // NVDRS_SETTING_LOCATION: 0 this profile, 1 global, 2 base, 3 default (24406-24412)
    unsigned       isCurrentPredefined;
    unsigned       isPredefinedValid;
    union { unsigned u32; unsigned char bin[4100]; } predefined;   // the largest member is NVDRS_BINARY_SETTING, 4 + 4096 bytes
    union { unsigned u32; unsigned char bin[4100]; } current;
};
#pragma pack(pop)
struct IvtNvDrsApp                              // NVDRS_APPLICATION_V4, nvapi.h:24557-24570
{
    unsigned       version;                     // MAKE_NVAPI_VERSION(NVDRS_APPLICATION_V4, 4), nvapi.h:24576
    unsigned       isPredefined;
    unsigned short appName[2048], userFriendlyName[2048], launcher[2048], fileInFolder[2048];
    unsigned       flags;                       // isMetro:1, isCommandLine:1, reserved:30
    unsigned short commandLine[2048];
};
static_assert(sizeof(IvtNvDrsSetting) == 12320, "NVDRS_SETTING_V1 is 12320 bytes: version word 0x00013020");
static_assert(sizeof(IvtNvDrsApp) == 20492, "NVDRS_APPLICATION_V4 is 20492 bytes: version word 0x0004500C");
#pragma pack(push, 8)
struct IvtNvVrrInfo                             // NV_GET_VRR_INFO_V1, nvapi.h:9692-9702 (under pack 8 there, nvapi.h:47)
{
    unsigned       version;                     // MAKE_NVAPI_VERSION(NV_GET_VRR_INFO_V1, 1), nvapi.h:9704
    unsigned       bIsVRREnabled          : 1;  // "Set if VRR Mode is currently active on given display" (9695)
    unsigned       bIsVRRPossible         : 1;  // "possible on the given display" (9696)
    unsigned       bIsVRRRequested        : 1;
    unsigned       bIsVRRIndicatorEnabled : 1;
    unsigned       bIsDisplayInVRRMode    : 1;  // "Set to true if the display is in VRR Mode" (9699)
    unsigned       reserved               : 27;
    unsigned       reservedEx[4];
};
#pragma pack(pop)
static_assert(sizeof(IvtNvVrrInfo) == 24, "NV_GET_VRR_INFO_V1 is 24 bytes: version word 0x00010018");
struct IvtGsyncState
{
    bool     read;          // every call the reading needs answered
    unsigned appOverride;   // VRR_APP_OVERRIDE, 0x10A879CF: 0 allow, 1 force off, 2 disallow, 3 ULMB, 4 fixed refresh (NvApiDriverSettings.h:253, 1165-1172)
    unsigned request;       // VRR_APP_OVERRIDE_REQUEST_STATE, 0x10A879AC: the same values (254, 1175-1182)
    unsigned gameMode;      // VRR_MODE, 0x1194F158, as it applies to the game: 0 off, 1 full screen only (the default), 2 windowed and full screen (255, 1185-1190)
    unsigned globalMode;    // the same setting in the current global profile: the display's own G-Sync setting
    int      locOverride;   // where appOverride came from (NVDRS_SETTING_LOCATION), -1 carried by no profile
    int      locRequest;
    bool     gameProfile;   // the executable has a driver profile of its own (NVIDIA ships "Grand Theft Auto 4")
    unsigned vsyncMode;     // VSYNCMODE, 0x00A879CF, as it applies to the game: the Control Panel's "Vertical sync" (the values at IvtDriverVsyncForced)
    int      locVsync;      // where vsyncMode came from (NVDRS_SETTING_LOCATION), -1 carried by no profile
    int      vrrPossible;   // NvAPI_Disp_GetVRRInfo for the primary display: variable refresh possible on it, 1 or 0; -1 the driver did not answer
    int      vrrInMode;     // the same call: the display is in VRR mode now, 1 or 0; -1 not answered (printed, not judged)
    unsigned ms;            // how long the reading took
    char     why[200];      // when !read: which call failed and what the driver said
    unsigned frl;           // FRL_FPS, 0x10835002, as it applies to the game: the Control Panel's "Max Frame Rate", 0 off, else frames a second up to 1023 (NvApiDriverSettings.h)
    int      locFrl;        // where frl came from (NVDRS_SETTING_LOCATION), -1 carried by no profile
};
static IvtGsyncState ivtGsync;
static void IvtGsyncRead()
{
    IvtGsyncState& g = ivtGsync;
    g = IvtGsyncState{};
    g.gameMode = g.globalMode = 1; g.locOverride = g.locRequest = -1;   // NVAPI_SETTING_NOT_FOUND leaves each at the header's default
    g.vsyncMode = 0x60925292u; g.locVsync = -1; g.vrrPossible = g.vrrInMode = -1;   // VSYNCMODE_PASSIVE, the header's default: the game decides
    g.frl = 0; g.locFrl = -1;                                                         // FRL_FPS_DISABLED, the header's default: no cap
    LARGE_INTEGER t0 = {}, t1 = {}, qf = {};
    QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&t0);
    char path[MAX_PATH + 16] = "";
    const UINT sysLen = GetSystemDirectoryA(path, MAX_PATH);
    if (!sysLen || sysLen >= MAX_PATH) { _snprintf_s(g.why, sizeof(g.why), _TRUNCATE, "GetSystemDirectoryA failed, error %lu", GetLastError()); return; }
    strcat_s(path, "\\nvapi.dll");
    const HMODULE m = LoadLibraryExA(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) { _snprintf_s(g.why, sizeof(g.why), _TRUNCATE, "%s did not load, error %lu", path, GetLastError()); return; }
    typedef void* (__cdecl *QueryInterfaceFn)(unsigned id);
    typedef int (__cdecl *VoidFn)(void);
    typedef int (__cdecl *ErrorMessageFn)(int st, char desc[64]);
    typedef int (__cdecl *CreateSessionFn)(void** session);
    typedef int (__cdecl *SessionFn)(void* session);
    typedef int (__cdecl *FindAppFn)(void* session, unsigned short* appName, void** profile, IvtNvDrsApp* app);
    typedef int (__cdecl *GetProfileFn)(void* session, void** profile);
    typedef int (__cdecl *GetSettingFn)(void* session, void* profile, unsigned settingId, IvtNvDrsSetting* setting);
    typedef int (__cdecl *DisplayIdFn)(const char* displayName, unsigned* displayId);   // nvapi.h:9025
    typedef int (__cdecl *VrrInfoFn)(unsigned displayId, IvtNvVrrInfo* info);           // nvapi.h:9726
    const QueryInterfaceFn qi = (QueryInterfaceFn)GetProcAddress(m, "nvapi_QueryInterface");
    const VoidFn          init    = qi ? (VoidFn)qi(0x0150e828u) : nullptr;            // NvAPI_Initialize, nvapi_interface.h:32
    const VoidFn          unload  = qi ? (VoidFn)qi(0xd22bdd7eu) : nullptr;            // NvAPI_Unload, :33
    const ErrorMessageFn  errMsg  = qi ? (ErrorMessageFn)qi(0x6c2d048cu) : nullptr;    // NvAPI_GetErrorMessage, :34
    const CreateSessionFn create  = qi ? (CreateSessionFn)qi(0x0694d52eu) : nullptr;   // NvAPI_DRS_CreateSession, :508
    const SessionFn       destroy = qi ? (SessionFn)qi(0xdad9cff8u) : nullptr;         // NvAPI_DRS_DestroySession, :509
    const SessionFn       load    = qi ? (SessionFn)qi(0x375dbd6bu) : nullptr;         // NvAPI_DRS_LoadSettings, :510
    const GetProfileFn    global  = qi ? (GetProfileFn)qi(0x617bff9fu) : nullptr;      // NvAPI_DRS_GetCurrentGlobalProfile, :517
    const FindAppFn       findApp = qi ? (FindAppFn)qi(0xeee566b2u) : nullptr;         // NvAPI_DRS_FindApplicationByName, :528
    const GetSettingFn    get     = qi ? (GetSettingFn)qi(0x73bf8338u) : nullptr;      // NvAPI_DRS_GetSetting, :530
    const DisplayIdFn     dispId  = qi ? (DisplayIdFn)qi(0xae457190u) : nullptr;       // NvAPI_DISP_GetDisplayIdByDisplayName, :183; may be absent: then VRR is "not answered"
    const VrrInfoFn       vrrInfo = qi ? (VrrInfoFn)qi(0xdf8fda57u) : nullptr;         // NvAPI_Disp_GetVRRInfo, :201; the same
    const char* missing = !qi ? "the export nvapi_QueryInterface" : !init ? "NvAPI_Initialize" : !unload ? "NvAPI_Unload"
                        : !create ? "NvAPI_DRS_CreateSession" : !destroy ? "NvAPI_DRS_DestroySession" : !load ? "NvAPI_DRS_LoadSettings"
                        : !global ? "NvAPI_DRS_GetCurrentGlobalProfile" : !findApp ? "NvAPI_DRS_FindApplicationByName"
                        : !get ? "NvAPI_DRS_GetSetting" : nullptr;
    if (missing) { _snprintf_s(g.why, sizeof(g.why), _TRUNCATE, "this nvapi.dll has no %s", missing); FreeLibrary(m); return; }
    static const char* const kStage[] = { "NvAPI_Initialize", "NvAPI_DRS_CreateSession", "NvAPI_DRS_LoadSettings",
        "NvAPI_DRS_GetCurrentGlobalProfile", "NvAPI_DRS_GetSetting(VRR_APP_OVERRIDE)", "NvAPI_DRS_GetSetting(VRR_APP_OVERRIDE_REQUEST_STATE)",
        "NvAPI_DRS_GetSetting(VRR_MODE)", "NvAPI_DRS_GetSetting(VSYNCMODE)", "NvAPI_DRS_GetSetting(VRR_MODE, the global profile)" };
    int stage = 0;
    int st = init();
    const bool inited = st == 0;
    void* session = nullptr;
    void* profile = nullptr;
    if (st == 0) { stage = 1; st = create(&session); }
    if (st == 0) { stage = 2; st = load(session); }
    if (st == 0)
    {
                // The game's own profile, by the executable's full path (nvapi.h:25024-25025). A game with no profile of its own (a
        // renamed executable) is governed by the global profile.
        static IvtNvDrsApp app;
        static unsigned short exe[2048];
        memset(&app, 0, sizeof(app)); app.version = (unsigned)sizeof(app) | (4u << 16);
        memset(exe, 0, sizeof(exe)); GetModuleFileNameW(nullptr, (wchar_t*)exe, 2047);
        g.gameProfile = findApp(session, exe, &profile, &app) == 0;
        if (!g.gameProfile) { stage = 3; st = global(session, &profile); }
    }
    if (st == 0)
    {
        static IvtNvDrsSetting s;
        const unsigned ids[4] = { 0x10A879CFu, 0x10A879ACu, 0x1194F158u, 0x00A879CFu };   // the last is VSYNCMODE (NvApiDriverSettings.h, VSYNCMODE_ID)
        unsigned* const values[4] = { &g.appOverride, &g.request, &g.gameMode, &g.vsyncMode };
        int* const locs[4] = { &g.locOverride, &g.locRequest, nullptr, &g.locVsync };
        for (int i = 0; i < 4 && st == 0; i++)
        {
            stage = 4 + i;
            memset(&s, 0, sizeof(s)); s.version = (unsigned)sizeof(s) | (1u << 16);
            const int r = get(session, profile, ids[i], &s);
            if (r == 0) { *values[i] = s.current.u32; if (locs[i]) *locs[i] = s.settingLocation; }
            else if (r != -160) st = r;             // NVAPI_SETTING_NOT_FOUND (nvapi_lite_common.h:344): the default stands
        }
                // NVIDIA's own frame rate cap (Max Frame Rate). Missing or refused means no cap, not a failed reading.
        if (st == 0)
        {
            memset(&s, 0, sizeof(s)); s.version = (unsigned)sizeof(s) | (1u << 16);
            if (get(session, profile, 0x10835002u, &s) == 0 && s.current.u32 <= 1023u) { g.frl = s.current.u32; g.locFrl = s.settingLocation; }
        }
                // The display's mode comes from the global profile: per-program Fixed Refresh writes VRR_MODE = 0 into the game's
        // profile.
        void* globalProfile = nullptr;
        if (st == 0) { stage = 3; st = global(session, &globalProfile); }
        if (st == 0)
        {
            stage = 8;
            memset(&s, 0, sizeof(s)); s.version = (unsigned)sizeof(s) | (1u << 16);
            const int r = get(session, globalProfile, 0x1194F158u, &s);
            if (r == 0) g.globalMode = s.current.u32;
            else if (r != -160) st = r;
        }
    }
        // Whether the display can do variable refresh at all, by its GDI name. A failure leaves -1 and the rule uses the settings
    // alone. One thread only (concurrent GetVRRInfo calls have crashed).
    if (st == 0 && dispId && vrrInfo)
    {
        MONITORINFOEXA mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
        const POINT origin = { 0, 0 };
        const HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        unsigned id = 0;
        IvtNvVrrInfo v; ZeroMemory(&v, sizeof(v)); v.version = (unsigned)sizeof(v) | (1u << 16);
        if (mon && GetMonitorInfoA(mon, &mi) && dispId(mi.szDevice, &id) == 0 && vrrInfo(id, &v) == 0)
        {
            g.vrrPossible = v.bIsVRRPossible ? 1 : 0;
            g.vrrInMode   = v.bIsDisplayInVRRMode ? 1 : 0;
        }
    }
    char msg[64] = "";
    if (st != 0 && errMsg) errMsg(st, msg);
    msg[63] = 0;
    if (session) destroy(session);
    if (inited) unload();
    FreeLibrary(m);
    g.read = st == 0;
    if (!g.read) _snprintf_s(g.why, sizeof(g.why), _TRUNCATE, "%s returned %d %s", kStage[stage], st, msg[0] ? msg : "(no message)");
    QueryPerformanceCounter(&t1);
    g.ms = (unsigned)((t1.QuadPart - t0.QuadPart) * 1000 / (qf.QuadPart ? qf.QuadPart : 1));
}
// G-Sync switched off for the game: either per-game switch set, while the display's G-Sync is on. The game's own VRR_MODE is
// not counted: it can read 0 while G-Sync works.
static bool IvtGsyncOffForGame(const IvtGsyncState& g)
{
    return (g.appOverride != 0 || g.request != 0) && g.globalMode != 0;
}
// The driver forcing vsync on a display without variable refresh: renamed, that vsync does not hold frames, so SDR keeps "DXVK"
// (rename_sdr=2 renames anyway). Values (NvApiDriverSettings.h, EValues_VSYNCMODE): 0x60925292 the application decides (the
// default; when the application decides nothing here applies), 0x08416747 off, 0x47814940 on, 0x32610244 / 0x71271021 /
// 0x13245256 a half, third and quarter of the refresh, 0x18888888 Fast Sync (not counted).
static bool IvtDriverVsyncForced(const IvtGsyncState& g)
{
    return g.vsyncMode == 0x47814940u || g.vsyncMode == 0x32610244u || g.vsyncMode == 0x71271021u || g.vsyncMode == 0x13245256u;
}
// No variable refresh in play: G-Sync unticked for the whole display (global mode 0), or the driver says it is not possible on
// the display. Not answered counts as possible: the rename stays.
static bool IvtDisplayHasNoVrr(const IvtGsyncState& g)
{
    return g.globalMode == 0 || g.vrrPossible == 0;
}
static bool IvtDriverVsyncOnFixedRefresh(const IvtGsyncState& g)
{
    return IvtDriverVsyncForced(g) && IvtDisplayHasNoVrr(g);
}
static const char* IvtVsyncModeName(unsigned v)
{
    return v == 0x60925292u ? "the application decides" : v == 0x08416747u ? "forced off" : v == 0x47814940u ? "FORCED ON"
         : v == 0x32610244u ? "FORCED ON at half the refresh" : v == 0x71271021u ? "FORCED ON at a third of the refresh"
         : v == 0x13245256u ? "FORCED ON at a quarter of the refresh" : v == 0x18888888u ? "Fast Sync" : "an unknown value";
}
// The two readings as the log says them, in one wording wherever they are said (a test kit reads this text).
static const char* IvtSettingWhereName(int loc)
{
    static const char* const kWhere[] = { "GTA IV's own profile", "the global profile", "the base profile", "the driver's default" };
    return loc >= 0 && loc < 4 ? kWhere[loc] : "no profile";
}
static const char* IvtVrrText(const IvtGsyncState& g)
{
    return g.vrrPossible < 0 ? "the driver did not say" : g.vrrPossible == 0 ? "not possible"
         : g.vrrInMode == 1 ? "possible, the display in VRR mode" : "possible, the display not in VRR mode";
}
// NVIDIA's frame rate cap counts as a limiter like FusionFix's (latLimiter). Said once. Inside the reading's cut
// (tools/test-gsync-read).
static void LatNvCapSeen()
{
    if (!ivtGsync.read || !ivtGsync.frl || latNvCapFps) return;
    latNvCapFps = ivtGsync.frl;
    _snprintf_s(latNvCapNote, sizeof(latNvCapNote), _TRUNCATE, "NVIDIA's Max Frame Rate for GTA IV is %u fps (from %s)", latNvCapFps, IvtSettingWhereName(ivtGsync.locFrl));
    if (latLimiter) return;                     // one of the other two is on already and said: the pacer is off as it is
    latLimiter = true;
    strcpy_s(latLimiterNote, latNvCapNote);
    LogRecord("  latency: %s: the pacer is off", latLimiterNote);
}
// END OF THE PER-GAME G-SYNC READ (tools/test-gsync-read/gen.py cuts from "THE PER-GAME G-SYNC SWITCH" to this line)

// Decided once, at DXVK's first instance: the 110 ms read in InitThread would let the game make its instance first. INIT_ONCE
// holds other callers.
static bool           hdrDecideSdr = false, hdrDecideNv = false, hdrDecideHybrid = false;
static wchar_t        hdrDecidePrimary[128] = L"";
static INIT_ONCE      hdrDecideOnce = INIT_ONCE_STATIC_INIT;
static bool           hdrRenameYes = true;
static void HdrRenameSetup(bool sdr, bool nvPrimary, bool hybrid, const wchar_t* primary)
{
    hdrDecideSdr = sdr; hdrDecideNv = nvPrimary; hdrDecideHybrid = hybrid;
    wcsncpy_s(hdrDecidePrimary, primary ? primary : L"", _TRUNCATE);
}
static void HdrDecideBody()
{
    hdrRenameYes = true;
        // A hybrid with NVIDIA's driver: DXVK picks a discrete GPU before an integrated one, so NVIDIA renders unless a device filter
    // says otherwise. No Vulkan instance of the mod's own here: on an Intel and RTX 4050 laptop that hung the game's start. The
    // device check in the transport warns if the game renders elsewhere. The per-game G-Sync setting is not read (a G-Sync
    // monitor on the laptop's NVIDIA port is a known gap).
    if (hdrDecideHybrid)
    {
        char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
        char conf[64] = ""; bool ff = false, fe = false;   // conf: DtDxvkValueAt writes up to 64
        DtDxvkValueAt(dir, "dxvk.deviceFilter", conf, &ff, &fe);
        if (conf[0] || GetEnvironmentVariableA("DXVK_FILTER_DEVICE_NAME", nullptr, 0) > 1 || GetEnvironmentVariableA("DXVK_FILTER_DEVICE_UUID", nullptr, 0) > 1)
        {
            hdrRenameYes = false; ivtRenamePredict = -1;
            LogRecord("  engine name: SDR, a hybrid with a DXVK device filter set: DXVK keeps \"DXVK\"");
            return;
        }
        ivtRenamePredict = 1;
        LogRecord("  engine name: SDR, a hybrid: the display is on %ls, NVIDIA's driver is installed: DXVK identifies as engine \"%s\"", hdrDecidePrimary, hdrEngineName);
        return;
    }
    if (hdrDecideSdr) ivtRenamePredict = 1;   // NVIDIA drives the display; the G-Sync read may still keep "DXVK" (not a miss)
    if (!hdrDecideNv) return;   // HDR with another vendor's display: nothing to ask NVIDIA (SDR never gets here without NVIDIA)
    IvtGsyncRead();
    LatNvCapSeen();             // NVIDIA's own frame rate cap, read with the rest: a limiter the mod sees
    const IvtGsyncState& g = ivtGsync;
    if (!g.read)
    {
        LogRecord("  engine name: NVIDIA's per-game G-Sync setting could not be read (%s, %u ms): DXVK is renamed", g.why, g.ms);
        if (hdrDecideSdr)
            LogRecord("  engine name: SDR on NVIDIA: DXVK identifies as engine \"%s\"", hdrEngineName);
        return;
    }
    static const char* const kValue[] = { "allowed", "forced off", "disallowed", "ULMB", "fixed refresh" };
    static const char* const kWhere[] = { "GTA IV's own profile", "the global profile", "the base profile", "the driver's default" };
    static const char* const kMode[] = { "off", "on for full screen only", "on for windowed and full screen" };
    const bool off = IvtGsyncOffForGame(g);
    const bool vsFixed = IvtDriverVsyncOnFixedRefresh(g);   // the driver forces vsync and the display has no variable refresh in play
        // DXVK can be asked for the vsync instead (IvtDriverVsyncInterval below): then the rename stays, and "DXVK" is kept only
    // where DXVK cannot be asked: the interval set in dxvk.conf, or a fractional rate forced.
    const bool modAsks = g.vsyncMode == 0x47814940u && IvtDxvkPinnedInterval() < 0;
    const unsigned v = g.appOverride ? g.appOverride : g.request;
    const int where = g.appOverride ? g.locOverride : g.request ? g.locRequest : g.locOverride;
        // Three lines. The phrases test kits read stay whole: "NVIDIA's G-Sync: <mode> for the display, <state> for GTA IV" and
    // "NVIDIA's vertical sync for GTA IV: ...".
    char gs[320], vs[288];
    _snprintf_s(gs, sizeof(gs), _TRUNCATE, "NVIDIA's G-Sync: %s for the display, %s for GTA IV (per-game switch %s, from %s%s; read in %u ms)",
        g.globalMode < 3 ? kMode[g.globalMode] : "an unknown mode",
                // "not in play" when the display has no G-Sync
        off ? "SWITCHED OFF" : g.globalMode == 0 ? "not in play" : "allowed", v < 5 ? kValue[v] : "an unknown value", where >= 0 && where < 4 ? kWhere[where] : "no profile",
        g.gameProfile ? "" : "; GTAIV.exe has no profile of its own", g.ms);
    _snprintf_s(vs, sizeof(vs), _TRUNCATE, "NVIDIA's vertical sync for GTA IV: %s (0x%08X, from %s); variable refresh on the display: %s",
        IvtVsyncModeName(g.vsyncMode), g.vsyncMode, IvtSettingWhereName(g.locVsync), IvtVrrText(g));
    if (hdrDecideSdr && off)
    {
        hdrRenameYes = false; ivtRenamePredict = -1;   // kept on purpose, not a prediction
        LogRecord("  engine name: NOT changed in SDR (G-Sync is switched off for GTA IV in NVIDIA's settings)");
    }
    else if (hdrDecideSdr && vsFixed && !modAsks && ivtCfg.rename_sdr != 2)
    {
        hdrRenameYes = false; ivtRenamePredict = -1;   // kept on purpose, not a prediction
        LogRecord("  engine name: NOT changed in SDR (NVIDIA's settings force vertical sync on for GTA IV, no variable refresh, DXVK cannot be asked for vsync)");
    }
    else if (hdrDecideSdr && ivtCfg.rename_sdr != 2 && IvtFfWindowedAtStart() == 0)   // exclusive fullscreen (the end of this file)
    {
        hdrRenameYes = false; ivtRenamePredict = -1;   // kept on purpose, not a prediction
        LogRecord("  engine name: NOT changed in SDR (the game starts in exclusive fullscreen, FusionFix's Windowed = 0; the rename is for a borderless window)");
    }
    else if (hdrDecideSdr)
        LogRecord("  engine name: SDR on NVIDIA: DXVK identifies as engine \"%s\"%s", hdrEngineName,
                  vsFixed && !modAsks ? "; rename_sdr=2: renamed ALTHOUGH NVIDIA's settings force vertical sync on a display without variable refresh" : "");
    if (hdrDecideSdr) LogRecord("  %s", gs); else LogRecord("  hdr: %s", gs);
    LogRecord("  %s", vs);
    if (!hdrDecideSdr)
    {
        if (off)
            // The words follow what was seen: force off tore; the Control Panel's Fixed Refresh gave no G-Sync and judder,
            // without tearing.
            IvtWarn("G-Sync is switched off for GTA IV in NVIDIA's per-program settings, so with HDR on the picture can stutter or tear."
                    " For G-Sync, set it back: NVIDIA Control Panel, Manage 3D settings, Program Settings, Grand Theft Auto IV, Monitor"
                    " Technology: Use global setting. To play without G-Sync, switch it off for the whole display instead (Set up"
                    " G-SYNC), where vsync holds");
    }
}
static BOOL CALLBACK HdrDecideOnce(PINIT_ONCE, PVOID, PVOID*)
{
    volatile bool faulted = false;
    __try { HdrDecideBody(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; }
    if (faulted)   // the NVAPI read, or a layer: a hybrid keeps "DXVK", an NVIDIA display is renamed as when the read fails
    {
        hdrRenameYes = !hdrDecideHybrid; ivtRenamePredict = -1;
        LogRecord("  engine name: an exception while deciding: DXVK %s", hdrRenameYes ? "is renamed" : "keeps \"DXVK\"");
    }
    return TRUE;
}
static bool HdrRenameDecide()
{
    InitOnceExecuteOnce(&hdrDecideOnce, HdrDecideOnce, nullptr, nullptr);
    return hdrRenameYes;
}

// THE REFRESH GUARD (renamed DXVK). There a vsync present does not wait, so a fast game passes the refresh and tears. Presents
// are held to Reflex's cap, hz - hz*hz/3600 (116 at 120 Hz). Armed renamed with vsync and no limiter (RgArm); dormant until
// RG_WIN presents span under RG_WIN - RG_SLACK refreshes, which vsync-held presents never do. Released when under one present
// in ten waits. Render thread only, after Present; sleeps at most 20 ms. Placed after DllMain to keep the hot layout. To the
// END line: tools/test-refresh-guard/gen.py drives it on a synthetic clock.
struct RgState
{
    LONGLONG qpf;            // clock ticks a second
    LONGLONG capTicks;       // engaged: the least time from one present's release to the next; 0 = not armed
    LONGLONG periodTicks;    // one refresh; dormant, RG_WIN presents spanning less than RG_WIN - RG_SLACK of these engage
    LONGLONG maxSleepTicks;  // 20 ms, the pacer's bound
    LONGLONG winStart;       // dormant: the present that opened the window
    LONGLONG last;           // engaged: when the last present was let go (its target, or its arrival when it came late)
    int      n, slept;       // presents in the window; engaged, how many of them had to wait
    LONG     winInj;         // dormant: the transport's injection count when the window opened
    bool     engaged;
    int      engagements;    // the first is said at log=1, every engage and release at log=2; kept across re-arms
    double   hz;
};
enum { RG_WIN = 240, RG_SLACK = 8 };
// Arms for a refresh of num/den Hz, dormant, or disarms (num 0). A refresh outside 20 to 1000 Hz disarms: Reflex's formula is
// for display rates, and the guard is no place to guess.
static void RgSetRefresh(RgState& s, UINT32 num, UINT32 den, LONGLONG qpf)
{
    const int engagements = s.engagements;
    s = RgState{};
    s.engagements = engagements;
    s.qpf = qpf;
    if (!num || !den || qpf <= 0) return;
    const double hz = (double)num / (double)den;
    if (hz < 20.0 || hz > 1000.0) return;
    s.hz            = hz;
    s.capTicks      = (LONGLONG)((double)qpf / (hz - hz * hz / 3600.0) + 0.5);
    s.periodTicks   = (LONGLONG)((double)qpf / hz + 0.5);
    s.maxSleepTicks = qpf / 50;
}
// One present at time now; returns when to release it, or 0. countAll: no working transport. Only windows where nine presents
// in ten ended an injected frame are judged (a loading screen can outrun vsync).
static LONGLONG RgStep(RgState& s, LONGLONG now, LONG injections, bool countAll)
{
    if (!s.capTicks) return 0;
    if (!s.engaged)
    {
        if (s.n++ == 0) { s.winStart = now; s.winInj = injections; return 0; }
        if (s.n <= RG_WIN) return 0;                              // RG_WIN intervals are needed to judge
        const LONGLONG span = now - s.winStart;
        const LONG inj = injections - s.winInj;
        s.n = 1; s.winStart = now; s.winInj = injections;         // the next window opens on this present
        if (!countAll && inj * 10 < RG_WIN * 9) return 0;         // not the game world throughout: a menu, a loading screen, the step into the world
        if (span >= s.periodTicks * (RG_WIN - RG_SLACK)) return 0;   // not faster than the refresh: vsync holds them, or they are slow
        s.engaged = true; s.n = 0; s.slept = 0; s.last = now;
        const bool first = s.engagements++ == 0;
        (first ? LogRecord : LogHealth)("  refresh guard: ENGAGED: vsync on, presents at %.1f fps on %.2f Hz: held to %.1f fps until they fall below it%s", (double)RG_WIN * (double)s.qpf / (double)(span > 0 ? span : 1),
            s.hz, (double)s.qpf / (double)s.capTicks, first ? " (said once)" : "");
        return 0;
    }
    const LONGLONG target = s.last + s.capTicks;
    LONGLONG until = 0;
    if (now < target)
    {
        until = target - now > s.maxSleepTicks ? now + s.maxSleepTicks : target;
        s.last = until;
        s.slept++;
    }
    else s.last = now;
    if (++s.n >= RG_WIN)
    {
        if (s.slept * 10 < RG_WIN)
        {
            s.engaged = false;
            LogHealth("  refresh guard: released: %d of the last %d presents had to wait, so the frames no longer reach %.1f fps on"
                      " their own", s.slept, (int)RG_WIN, (double)s.qpf / (double)s.capTicks);
        }
        s.n = 0; s.slept = 0;
    }
    return until;
}
// END OF THE REFRESH GUARD'S LOGIC (tools/test-refresh-guard/gen.py cuts from "THE REFRESH GUARD" to this line)
static RgState rg;   // the render thread's; RgArm writes it at CreateDevice and after a Reset, where no present runs beside it
// The call, from both present sites behind latGuard. The clock is read once; a present that does not wait costs RgStep alone.
static void RgOnPresent()
{
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
        // The transport's injection count, read through volatile (bumped on another thread).
    const LONG injections = *(volatile LONG*)&ivtInjections;
    const LONGLONG until = RgStep(rg, now.QuadPart, injections, !ivtCfg.transport || ivtFailed);
    if (until) LatSleepUntil(LatQpcNs(until), LatQpcNs(now.QuadPart));
}

// The guard needs the exact refresh, a rational from QueryDisplayConfig (59.94, not 59). No rational, no guard.
static bool RgRefreshOf(const wchar_t* gdiName, UINT32& num, UINT32& den)
{
    num = den = 0;
    UINT32 nPath = 0, nMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) != ERROR_SUCCESS || !nPath || !nMode) return false;
    DISPLAYCONFIG_PATH_INFO* paths = (DISPLAYCONFIG_PATH_INFO*)calloc(nPath, sizeof(DISPLAYCONFIG_PATH_INFO));
    DISPLAYCONFIG_MODE_INFO* modes = (DISPLAYCONFIG_MODE_INFO*)calloc(nMode, sizeof(DISPLAYCONFIG_MODE_INFO));
    if (paths && modes && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths, &nMode, modes, nullptr) == ERROR_SUCCESS)
        for (UINT32 i = 0; i < nPath && !num; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
            sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sn.header.size = sizeof(sn);
            sn.header.adapterId = paths[i].sourceInfo.adapterId;
            sn.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS || wcscmp(sn.viewGdiDeviceName, gdiName) != 0) continue;
            const DISPLAYCONFIG_RATIONAL r = paths[i].targetInfo.refreshRate;
            if (r.Numerator && r.Denominator) { num = r.Numerator; den = r.Denominator; }
        }
    free(paths); free(modes);
    return num != 0;
}
static void RgArm()
{
    latGuard = false;
    RgSetRefresh(rg, 0, 0, latQpf);   // disarmed until the tests below pass
    if (hdrInstancesRenamed <= 0 || !latVsync || latLimiter)
    {
        LogHealth("  refresh guard: off (%s)", hdrInstancesRenamed <= 0 ? "DXVK's engine name was not changed, so the driver's vsync waits"
                  : !latVsync ? "the effective present interval is not vsync" : "a frame limiter is on");
        return;
    }
    const HMONITOR mon = MonitorFromWindow(hdrGameWnd ? hdrGameWnd : GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi; ZeroMemory(&mi, sizeof(mi)); mi.cbSize = sizeof(mi);
    UINT32 num = 0, den = 0;
    if (!mon || !GetMonitorInfoW(mon, &mi) || !RgRefreshOf(mi.szDevice, num, den))
    {
        static bool said = false;
        if (!said) { said = true; LogRecord("  refresh guard: off (QueryDisplayConfig gave no exact refresh)"); }
        return;
    }
    RgSetRefresh(rg, num, den, latQpf);
    latGuard = rg.capTicks != 0;
    LogHealth("  refresh guard: %s at %.3f Hz (%u/%u, %ls): dormant until %d presents take under %d refreshes, then held to %.1f fps",
              latGuard ? "armed" : "off (a refresh outside 20 to 1000 Hz)", rg.hz, num, den, mi.szDevice, (int)RG_WIN, (int)(RG_WIN - RG_SLACK),
              rg.capTicks ? (double)latQpf / (double)rg.capTicks : 0.0);
}

// When NVIDIA's driver forces vsync, DXVK gets present interval ONE in the game's place, since the driver's own vsync does not
// pace DXVK's window. Not when dxvk.conf sets d3d9.presentInterval, not for fractional rates, never under Wine.
static int IvtDxvkPinnedInterval()   // d3d9.presentInterval as dxvk.conf or DXVK_CONFIG sets it, or -1: left to the application
{
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char v[64] = ""; bool fromFile = false, fromEnv = false;
    if (DtDxvkValueAt(dir, "d3d9.presentInterval", v, &fromFile, &fromEnv) && v[0] >= '0' && v[0] <= '9') return atoi(v);
    return -1;
}
static void IvtGsyncReadGuarded()    // an exception inside NVIDIA's library must not take the game's CreateDevice with it
{
    __try { IvtGsyncRead(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ivtGsync.read = false; strcpy_s(ivtGsync.why, "an exception inside nvapi.dll"); }
}
static int ivtNvVsync = -1;          // -1 not looked at yet; 0 nothing to do; 1 the driver forces vsync on and DXVK can be asked
static UINT IvtDriverVsyncInterval(UINT asked, bool say)
{
    if (ivtNvVsync < 0)
    {
        ivtNvVsync = 0;
        if (!ivtWine)
        {
            wchar_t primary[128] = L"";
            if (!ivtGsync.read && !ivtGsync.why[0] && IvtNvidiaDrivesPrimary(primary, _countof(primary)) && IvtNvidiaDriverPresent())
            {
                IvtGsyncReadGuarded();   // the engine-name hook did not read them (rename_sdr=0 or hdr_engine_name=0)
                LatNvCapSeen();
                if (ivtGsync.read)
                    LogRecord("  vsync: read at the game's device, in %u ms. NVIDIA's vertical sync for GTA IV: %s (0x%08X, from %s); variable refresh on the display: %s",
                              ivtGsync.ms, IvtVsyncModeName(ivtGsync.vsyncMode), ivtGsync.vsyncMode, IvtSettingWhereName(ivtGsync.locVsync), IvtVrrText(ivtGsync));
                else
                    LogRecord("  vsync: NVIDIA's settings could not be read at the game's device (%s, %u ms): nothing is asked of DXVK", ivtGsync.why, ivtGsync.ms);
            }
            if (ivtGsync.read && ivtGsync.vsyncMode == 0x47814940u)
            {
                const int pinned = IvtDxvkPinnedInterval();
                if (pinned < 0) ivtNvVsync = 1;
                else if (pinned == 0)
                {
                    LogRecord("  vsync: NVIDIA's settings force vertical sync on for GTA IV, and dxvk.conf (or DXVK_CONFIG) sets d3d9.presentInterval = 0: the mod cannot ask DXVK for vsync");
                    if (IvtDisplayHasNoVrr(ivtGsync))
                        IvtWarn("NVIDIA's settings force vertical sync on for GTA IV, and your dxvk.conf sets d3d9.presentInterval = 0."
                                " Together they give uneven frames (stutter). Remove that line from dxvk.conf, or set it to 1");
                }
            }
        }
    }
    if (ivtNvVsync != 1 || asked != D3DPRESENT_INTERVAL_IMMEDIATE) return asked;
    static bool said = false;
    if (say && !said)
    {
        said = true;
        LogRecord("  vsync: NVIDIA's settings force vertical sync on for GTA IV, so the mod asks DXVK for it (present interval ONE; the game asked for IMMEDIATE)");
    }
    return D3DPRESENT_INTERVAL_ONE;
}

// ---- cold log readers, last in the file: a function's place sets the DLSSIV_HOT order the layout gate checks ----

// The game display path's exact refresh (119.88, not 120) from the display configuration's vSyncFreq, matched by GDI name.
// After a successful CreateDevice or Reset, since DXVK's exclusive fullscreen changes the mode inside the call.
static void HdrReadRefresh(IDirect3DDevice9* dev, const char* where)
{
        // The armed product lifts the gate for a session that asked for frame generation: its pacer and conditions need the
    // refresh, and a player's session runs at log=0.
    if (!telemetryOn FGA(&& !FgpWantsRefresh())) return;   // the gate frame generation lifts (hdr.h, at the statics)
    // WHICH MONITOR: the one the game's window is on, as HdrLogDisplay decides it (MonitorFromWindow, not the primary).
    const HMONITOR mon = hdrGameWnd ? MonitorFromWindow(hdrGameWnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
    const bool haveMon = mon && GetMonitorInfoW(mon, (MONITORINFO*)&mi);
    const WCHAR* gdi = haveMon ? mi.szDevice : L"";
    const bool primary = haveMon && (mi.dwFlags & MONITORINFOF_PRIMARY);
        // Two integers for the line only: EnumDisplaySettingsW (what DXVK uses) and GetDisplayMode (DXVK answers for the primary).
    DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
    const int enumHz = (haveMon && EnumDisplaySettingsW(gdi, ENUM_CURRENT_SETTINGS, &dm)) ? (int)dm.dmDisplayFrequency : -1;
    D3DDISPLAYMODE dd = {};
    const HRESULT dhr = dev ? dev->GetDisplayMode(0, &dd) : E_POINTER;
    const int d3dHz = SUCCEEDED(dhr) ? (int)dd.RefreshRate : -1;

    const char* why = nullptr;          // why no refresh can be given; a string literal, so its address is its identity below
    LONG rc = ERROR_SUCCESS;
    DISPLAYCONFIG_RATIONAL vs = {}, pr = {};
    bool haveVs = false; UINT matched = 0;
    if (!hdrGameWnd)   why = "the game's window is not known";
    else if (!haveMon) why = "the game's window has no monitor";
    else
    {
        DISPLAYCONFIG_PATH_INFO* paths = nullptr; DISPLAYCONFIG_MODE_INFO* modes = nullptr;
        UINT32 nPath = 0, nMode = 0;
                // The configuration can change between the size query and the query (ERROR_INSUFFICIENT_BUFFER): up to three tries.
        for (int tries = 0; tries < 3; tries++)
        {
            free(paths); free(modes); paths = nullptr; modes = nullptr;
            rc = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode);
            if (rc != ERROR_SUCCESS || !nPath) break;
            paths = (DISPLAYCONFIG_PATH_INFO*)calloc(nPath, sizeof(DISPLAYCONFIG_PATH_INFO));
            modes = (DISPLAYCONFIG_MODE_INFO*)calloc(nMode ? nMode : 1, sizeof(DISPLAYCONFIG_MODE_INFO));
            if (!paths || !modes) { rc = ERROR_OUTOFMEMORY; break; }
            rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths, &nMode, modes, nullptr);
            if (rc != ERROR_INSUFFICIENT_BUFFER) break;
        }
        if (rc != ERROR_SUCCESS)  why = "the display configuration could not be read";
        else if (!nPath || !paths) why = "the display configuration has no active path";
        else for (UINT32 i = 0; i < nPath; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
            sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            sn.header.size = sizeof(sn);
            sn.header.adapterId = paths[i].sourceInfo.adapterId;
            sn.header.id = paths[i].sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS || wcscmp(sn.viewGdiDeviceName, gdi) != 0) continue;
            if (matched++) continue;   // a cloned source has one path per target: the first is read, and the line says so
            pr = paths[i].targetInfo.refreshRate;
            // THE UNION (wingdi.h:2944-2945, 2973-2986; Microsoft's page for DISPLAYCONFIG_PATH_TARGET_INFO). Without
            // QDC_VIRTUAL_MODE_AWARE, which this query does not ask, all 32 bits are modeInfoIdx, invalid as 0xffffffff; with
            // it Windows sets DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE on the path and the target index is the upper 16 bits,
            // invalid as 0xffff. Tested on the flag, so either form reads right.
            const bool   virt  = (paths[i].flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) != 0;
            const UINT32 ti    = virt ? (UINT32)paths[i].targetInfo.targetModeInfoIdx : paths[i].targetInfo.modeInfoIdx;
            const bool   idxOk = virt ? ti != DISPLAYCONFIG_PATH_TARGET_MODE_IDX_INVALID : ti != DISPLAYCONFIG_PATH_MODE_IDX_INVALID;
            if (idxOk && ti < nMode && modes[ti].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET)
            {
                vs = modes[ti].targetMode.targetVideoSignalInfo.vSyncFreq;
                haveVs = vs.Numerator != 0 && vs.Denominator != 0;
            }
        }
        free(paths); free(modes);
        if (!why && !matched) why = "no active path's source is the game's monitor";
    }
    const DISPLAYCONFIG_RATIONAL use = haveVs ? vs : pr;
    if (!why && !(use.Numerator && use.Denominator)) why = "the game's path carries no refresh";
    const bool ok = !why;
    __iso_volatile_store64(&hdrRefreshPacked, ok ? (LONGLONG)(((ULONGLONG)use.Numerator << 32) | (ULONGLONG)use.Denominator) : 0);
    hdrRefreshHz = ok ? (double)use.Numerator / (double)use.Denominator : 0.0;

        // Once at log=1 per distinct reading (HdrLogDisplay's rule, hdr.h): a Reset that changed nothing is log=2.
    DLSSIV_FGDATA static DWORD saidSig[8]; DLSSIV_FGDATA static int saidN;   // out of .bss (DLSSIV_FGDATA, at the top)
    DWORD sig = 2166136261u;
    for (int c = 0; c < CCHDEVICENAME && gdi[c]; c++) sig = (sig ^ (DWORD)gdi[c]) * 16777619u;
    const DWORD mix[11] = { (DWORD)ok, vs.Numerator, vs.Denominator, pr.Numerator, pr.Denominator, (DWORD)enumHz, (DWORD)d3dHz,
                            matched, (DWORD)primary, (DWORD)(UINT_PTR)why, (DWORD)rc };
    for (int m = 0; m < 11; m++) sig = (sig ^ mix[m]) * 16777619u;
    bool saidBefore = false;
    for (int s = 0; s < saidN; s++) if (saidSig[s] == sig) saidBefore = true;
    if (!saidBefore && saidN < 8) saidSig[saidN++] = sig;
        // Each number keeps its place in the line: a value not read prints 0/0 (vSyncFreq) or -1 (integer), never a plain 0 that looks
    // measured, and the words after the last number say which. No parenthesis inside the parentheses, so a reader that takes
    // everything up to the first ")" gets the whole list.
    char d3dNote[64], notes[256];
    if (d3dHz >= 0)  _snprintf_s(d3dNote, sizeof(d3dNote), _TRUNCATE, ", DXVK's answer for the primary monitor%s",
                                 !haveMon ? "" : primary ? ", which is the game's" : ", not the game's");
    else if (dev)    _snprintf_s(d3dNote, sizeof(d3dNote), _TRUNCATE, ", not read: 0x%08lX", (unsigned long)dhr);
    else             _snprintf_s(d3dNote, sizeof(d3dNote), _TRUNCATE, ", not read: no device");
    _snprintf_s(notes, sizeof(notes), _TRUNCATE, "%s%s%s",
                ok && !haveVs ? "; vSyncFreq 0/0: no target mode gave one, so the path's refreshRate stands in" : "",
                enumHz < 0 ? "; EnumDisplaySettingsW -1: not read" : "",
                matched > 1 ? "; a cloned source, the first of its targets read" : "");
    char rcTxt[48] = "";
    if (rc != ERROR_SUCCESS) _snprintf_s(rcTxt, sizeof(rcTxt), _TRUNCATE, ", Windows error %ld", (long)rc);
    if (ok)
        (saidBefore ? LogHealth : LogRecord)("  display: refresh %u/%u = %.3f Hz on the game's path %ls (target mode vSyncFreq %u/%u;"
            " EnumDisplaySettingsW %d Hz; D3D9 GetDisplayMode %d Hz%s; the path's refreshRate %u/%u%s), at %s",
            use.Numerator, use.Denominator, hdrRefreshHz, gdi, haveVs ? vs.Numerator : 0u, haveVs ? vs.Denominator : 0u,
            enumHz, d3dHz, d3dNote, pr.Numerator, pr.Denominator, notes, where);
    else
        (saidBefore ? LogHealth : LogRecord)("  display: refresh: no display path matched the game's monitor (%s%s%s%ls;"
            " EnumDisplaySettingsW %d Hz; D3D9 GetDisplayMode %d Hz%s%s), at %s",
            why, rcTxt, gdi[0] ? "; the monitor is " : "", gdi, enumHz, d3dHz, d3dNote, notes, where);
}

// Logs the game's swapchain as DXVK took it and whether DXVK rotates its back buffers. DXVK's SwapWithFrontBuffer is false for
// COPY, and for DISCARD with one back buffer in a window: the device then presents from one back buffer and rotates nothing
// (DXVK refuses COPY with more than one back buffer). Frame generation's second present goes through that rotation.
// Reads DXVK's own copy (GetSwapChain(0)->GetPresentParameters); falls back to pp, which DXVK normalises, when that fails.
// Once at log=1 per distinct tuple; a Reset that changes nothing is log=2.
static void LogGameSwapchain(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* pp, BOOL windowed, const char* where)
{
    D3DPRESENT_PARAMETERS own = {};
    HRESULT shr = E_POINTER;
    if (dev)
    {
        IDirect3DSwapChain9* sc = nullptr;
        shr = dev->GetSwapChain(0, &sc);
        if (SUCCEEDED(shr) && !sc) shr = E_POINTER;
        if (SUCCEEDED(shr)) { shr = sc->GetPresentParameters(&own); sc->Release(); }
    }
    const bool fromSc = SUCCEEDED(shr);
    const UINT se  = fromSc ? (UINT)own.SwapEffect : (UINT)pp->SwapEffect;
    const UINT cnt = fromSc ? own.BackBufferCount : pp->BackBufferCount;
    const BOOL win = fromSc ? own.Windowed : windowed;
    const UINT bbc = cnt ? cnt : 1u;   // DXVK's max(count, 1)
    const bool rotates = !(se == (UINT)D3DSWAPEFFECT_COPY || (se == (UINT)D3DSWAPEFFECT_DISCARD && bbc == 1u && win));
    const char* seName = se == 1u ? "DISCARD" : se == 2u ? "FLIP" : se == 3u ? "COPY" : se == 4u ? "OVERLAY" : se == 5u ? "FLIPEX" : "unrecognised";
    char imgs[48] = "";
    if (rotates) _snprintf_s(imgs, sizeof(imgs), _TRUNCATE, " (%u images: BackBufferCount + 1)", bbc + 1u);
    char queue[160] = "";
    if (!latQueueAsked)
        _snprintf_s(queue, sizeof(queue), _TRUNCATE, "; DXVK queues up to %u frames unless"
                    " d3d9.maxFrameLatency caps it", bbc + 1u < 3u ? bbc + 1u : 3u);
    char src[128] = "";
    if (!fromSc)
        _snprintf_s(src, sizeof(src), _TRUNCATE, "; the swapchain's own parameters were not read, %s0x%08lX, so these are the game's"
                    " after the call", dev ? "" : "no device, ", (unsigned long)shr);
    DLSSIV_FGDATA static DWORD said[4]; DLSSIV_FGDATA static int saidN;   // kept off .bss (DLSSIV_FGDATA)
    const DWORD key = (se & 0xFFu) | ((bbc & 0xFFu) << 8) | ((win ? 1u : 0u) << 16) | ((latQueueAsked ? 1u : 0u) << 17)
                    | ((fromSc ? 1u : 0u) << 18);
    bool before = false;
    for (int i = 0; i < saidN; i++) if (said[i] == key) before = true;
    if (!before && saidN < 4) said[saidN++] = key;
    (before ? LogHealth : LogRecord)("  the game's swapchain: SwapEffect %s, BackBufferCount %u, windowed %d: DXVK %s%s%s%s, at %s",
        seName, bbc, win ? 1 : 0,
        rotates ? "rotates the back buffers" : "presents from one back buffer (COPY, or DISCARD with one back buffer in a window)",
        imgs, queue, src, where);
}

// ---- THE FLIP CLASSIFIER ----
// Frame generation must know whether the screen shows each present when it is made (VRR or tearing) or on a refresh grid (a
// fixed panel under vsync, or a compositor). Under FIFO a present returns when the one before it was shown, so on a grid the
// render thread's begins sit on one phase of the refresh and on VRR they walk. Each 120-frame latency window reads FREE,
// QUANTISED, LIMITED or UNCERTAIN, and UNCERTAIN whenever two readings are possible. Defined after DllMain: a line of it among
// the hot globals reorders .data$dlhot (DlssIvTransport.h). tools/flipcls replays these very lines.
// State: written and read only by the waiter (volatile for readers elsewhere), DLSSIV_FGDATA. Values: 0 none, 1 FREE,
// 2 QUANTISED, 3 LIMITED, 4 UNCERTAIN.
// A FREE in the memory is not VRR evidence on its own: a driver's Fast Sync or Enhanced Sync with vsync on reads the same.
DLSSIV_FGDATA static volatile LONG latFlipSettled, latFlipDisplay;   // settled verdict (two windows in a row agree); display memory (0, 1 or 2)
DLSSIV_FGDATA static LONG latFlipSince, latFlipLast, latFlipDisplaySince, latFlipLatchWin, latFlipPinRun, latFlipSaidWin, latFlipHeld, latFlipPending, latFlipInfo[2], latFlipWins[5];
alignas(8) DLSSIV_FGDATA static LONGLONG latFlipKeyRefresh;          // a change clears the memory, the latch and the slots
DLSSIV_FGDATA static LONG latFlipKeyResets;
DLSSIV_FGDATA static int latFlipKeyVsync, latFlipKeyF[3];
// One window's inputs, a local of LatWaiterLoop zeroed at each resync. Per accepted frame: the begin's phase in refresh periods,
// the interval, and the anchor (the begin minus the end of the frame `lag` presents earlier: 3 when the device thread is armed,
// else 2). Integer QPC ticks. The per-frame helpers are functions so tools/flipcls compiles the same lines.
struct LatFlipWin_t
{
    LONGLONG b0, bLast, packed;   // first begin; last accepted begin (kept across windows); refresh read at the window's start
    double   tq, xLast;           // the refresh period in QPC ticks (0: unknown); the last accepted begin's phase in periods
    LONG     fLast, nB, nIv, nA, nS, lag, paceN0, paceF0;   // frame of bLast; counts; lag last used; pacer counters at the window's start
    LONG     resets0, vs0;        // at the window's first frame: a straddling window feeds neither memory nor latch
    LONG     cnt[32], stp[32];    // the begins' phase histogram, and every adjacent step's
    LONG     iv[LAT_WIN], a[LAT_WIN];   // intervals and anchors, ticks
    struct { LONG f, pad; LONGLONG tIn, tEnd; } ring[8];   // the last eight frames: f, its injection's retire, its frame end
    DEVSTAT(LONG relNext, nRel, nWait; LONG relK[16]; LONGLONG relP1[16]; float relBlk[16]; float relD[LAT_WIN]; LONG relCnt[32];)   // the worker's returns (ivt_latency.h)
};
// A development build pairs the begin with the latest worker return at or before it: the present DT_R_PACE waited for.
DEVSTAT(static __forceinline void LatFlipRel(LatFlipWin_t& fw, LONGLONG b)
{
    const LONG done = dtPresDone;
    if ((LONG)(done - fw.relNext) >= LAT_RING) fw.relNext = done - (LAT_RING - 1);   // behind by a lap: drain the last 64; the N check skips a slot torn by one in flight
    for (; (LONG)(done - fw.relNext) >= 0; fw.relNext++)
    {
        const int s = (int)(fw.relNext & (LAT_RING - 1));
        if (ReadAcquire(&latFlipRelN[s]) != fw.relNext) continue;
        const LONGLONG p1 = __iso_volatile_load64(&latFlipRelQpc[s]); const float bk = latFlipRelBlk[s];
        if (ReadAcquire(&latFlipRelN[s]) != fw.relNext) continue;
        fw.relK[fw.relNext & 15] = fw.relNext; fw.relP1[fw.relNext & 15] = p1; fw.relBlk[fw.relNext & 15] = bk;
    }
    LONGLONG best = 0; float bk = 0.0f;
    for (int i = 0; i < 16; i++) if (fw.relK[i] && fw.relP1[i] <= b && fw.relP1[i] > best) { best = fw.relP1[i]; bk = fw.relBlk[i]; }
    if (!best || fw.nRel >= LAT_WIN) return;
    fw.relD[fw.nRel++] = (float)(b - best);
    if (fw.tq > 0.0 && bk > 0.25 * fw.tq)
    {
        fw.nWait++;
        double x = (double)(best - fw.b0) / fw.tq; x -= (double)(int)x; if (x < 0.0) x += 1.0;
        fw.relCnt[(int)(x * 32.0) & 31]++;
    }
})
static __forceinline void LatFlipTake(LatFlipWin_t& fw, LONG fp, LONGLONG b, int lagNow, bool marker)
{
    if (!fw.nB)
    {
        fw.packed = __iso_volatile_load64(&hdrRefreshPacked);   // once a window, one 8-byte load (hdr.h's contract)
        const ULONG num = (ULONG)((ULONGLONG)fw.packed >> 32), den = (ULONG)fw.packed;
        fw.tq = (num && den) ? (double)ivtQpcFreq * (double)den / (double)num : 0.0;
        fw.b0 = b; fw.paceN0 = latPaceSleptN; fw.paceF0 = latPaceFrames;
        fw.resets0 = statDeviceResets; fw.vs0 = latVsync ? 1 : 0;   // can change inside a window (LatFlipStep, at the key)
    }
    if (fw.tq > 0.0)
    {
        const double x = (double)(b - fw.b0) / fw.tq;
        fw.cnt[(int)((x - (double)(int)x) * 32.0) & 31]++;
        if (fw.nB) { const double s = x - fw.xLast; fw.stp[(int)((s - (double)(int)s) * 32.0) & 31]++; fw.nS++; }
        fw.xLast = x;
    }
    if (fw.bLast && fp == fw.fLast + 1 && fw.nIv < LAT_WIN) fw.iv[fw.nIv++] = (LONG)(b - fw.bLast);
    fw.bLast = b; fw.fLast = fp; fw.nB++; fw.lag = lagNow;
    const LONG e = fp - lagNow; const int s = (int)(e & 7);
    if (fw.ring[s].f == e) { const LONGLONG E = marker ? fw.ring[s].tEnd : fw.ring[s].tIn; if (E && fw.nA < LAT_WIN) fw.a[fw.nA++] = (LONG)(b - E); }
    DEVSTAT(LatFlipRel(fw, b);)
}
static __forceinline void LatFlipKeep(LatFlipWin_t& fw, LONG f, LONGLONG tIn, LONGLONG tEnd) { const int s = (int)(f & 7); fw.ring[s].f = f; fw.ring[s].tIn = tIn; fw.ring[s].tEnd = tEnd; }
// cos(2 pi (k + 1/2) / 32) for the 32 bin centres; the sine of bin k is the cosine of bin k - 8, so no CRT trigonometry is linked.
static const float latFlipCos[32] = {
    0.995184727f, 0.956940336f, 0.881921264f, 0.773010453f, 0.634393284f, 0.471396737f, 0.290284677f, 0.098017140f,
    -0.098017140f, -0.290284677f, -0.471396737f, -0.634393284f, -0.773010453f, -0.881921264f, -0.956940336f, -0.995184727f,
    -0.995184727f, -0.956940336f, -0.881921264f, -0.773010453f, -0.634393284f, -0.471396737f, -0.290284677f, -0.098017140f,
    0.098017140f, 0.290284677f, 0.471396737f, 0.634393284f, 0.773010453f, 0.881921264f, 0.956940336f, 0.995184727f };
// LatFlipStep's helpers, forced inline so the release gains no symbol: an insertion sort, and numpy's median and linear
// percentile over a sorted array.
template <class V> static __forceinline void LatFlipSort(V* v, int n)
{
    for (int i = 1; i < n; i++) { const V x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; }
}
template <class V> static __forceinline double LatFlipPct(const V* v, int n, double q, double k)
{
    if (n <= 0) return 0.0;
    const double pos = q * (double)(n - 1); const int lo = (int)pos; const double t = pos - (double)lo;
    const double a = (double)v[lo] * k; if (lo + 1 >= n) return a;
    const double b = (double)v[lo + 1] * k;
    return t >= 0.5 ? b - (b - a) * (1.0 - t) : a + (b - a) * t;   // numpy's lerp: from the upper end at one half or more
}
template <class V> static __forceinline double LatFlipMed(const V* v, int n, double k)
{
    if (n <= 0) return 0.0;
    return (n & 1) ? (double)v[n / 2] * k : ((double)v[n / 2 - 1] * k + (double)v[n / 2] * k) / 2.0;
}
// Once a window, on the waiter (cold, noinline): features in units of the refresh period T, the decision rule (first match
// wins), the settled verdict, latch and display memory, the log lines; then empties the window. Medians and percentiles follow
// numpy's linear rule so the thresholds cut where the model's do.
static __declspec(noinline) void LatFlipStep(LatFlipWin_t& fw, float mB, LONG win)
{
        // Names come from switches, not a pointer table, which would land in .data.
    auto name = [](LONG v) -> const char* { switch (v) { case 1: return "FREE"; case 2: return "QUANTISED"; case 3: return "LIMITED"; case 4: return "UNCERTAIN"; default: return "none"; } };
    auto absd = [](double x) { return x < 0.0 ? -x : x; };
    // ---- the key: a new refresh, a Reset, vsync or a cap the mod knows clears the memory, the latch and the slots
    const bool vs = latVsync, marker = latEndOn;
    bool memChanged = false, latchChanged = false;
    if (fw.packed != latFlipKeyRefresh || statDeviceResets != latFlipKeyResets || (int)vs != latFlipKeyVsync
        || latFfCap != latFlipKeyF[0] || latFfRel != latFlipKeyF[1] || latDxvkCap != latFlipKeyF[2])
    {
        latFlipKeyRefresh = fw.packed; latFlipKeyResets = statDeviceResets; latFlipKeyVsync = (int)vs;
        latFlipKeyF[0] = latFfCap; latFlipKeyF[1] = latFfRel; latFlipKeyF[2] = latDxvkCap;
        if (latFlipDisplay) { latFlipDisplay = 0; memChanged = true; }
        if (latFlipLatchWin) { latFlipLatchWin = 0; latchChanged = true; }
        latFlipInfo[0] = latFlipInfo[1] = 0; latFlipPinRun = 0; latFlipDisplaySince = win;
    }
        // A window that straddles a Reset or a vsync change is classified but feeds neither the memory nor the pin run: part of it
    // predates the change. The refresh needs no such test (read at the window's first frame) and the caps cannot change.
    const bool straddle = fw.resets0 != statDeviceResets || fw.vs0 != (vs ? 1 : 0);
    if (!latFlipSince) latFlipSince = win;                 // first classifier window: "since" for an unsettled verdict
    if (!latFlipDisplaySince) latFlipDisplaySince = win;
        // ---- the features
    const double msT = ivtQpcFreq ? 1000.0 / (double)ivtQpcFreq : 0.0;
    const ULONG num = (ULONG)((ULONGLONG)fw.packed >> 32), den = (ULONG)fw.packed;
    const double hz = (fw.tq > 0.0 && num && den) ? (double)num / (double)den : 0.0;
    const double T  = hz > 0.0 ? 1000.0 * (double)den / (double)num : 0.0;
    const int n = fw.nIv;
    LatFlipSort(fw.iv, n);
    LONGLONG sum = 0; for (int i = 0; i < n; i++) sum += fw.iv[i];
    const double mean = n ? (double)sum * msT / (double)n : 0.0;
    const double medIv = LatFlipMed(fw.iv, n, msT), iqr = LatFlipPct(fw.iv, n, 0.75, msT) - LatFlipPct(fw.iv, n, 0.25, msT);
    double c = 0.0, s = 0.0, c2 = 0.0, s2 = 0.0, cd = 0.0, sd = 0.0;
    for (int k = 0; k < 32; k++)
    {
        const double ck = latFlipCos[k], sk = latFlipCos[(k - 8) & 31];
        c += fw.cnt[k] * ck; s += fw.cnt[k] * sk; c2 += fw.cnt[k] * (2.0 * ck * ck - 1.0); s2 += fw.cnt[k] * (2.0 * sk * ck);
        cd += fw.stp[k] * ck; sd += fw.stp[k] * sk;
    }
    const double R  = (T > 0.0 && fw.nB) ? sqrt(c * c + s * s) / (double)fw.nB : 0.0;           // phase lock
    const double R2 = (T > 0.0 && fw.nB) ? sqrt(c2 * c2 + s2 * s2) / (double)fw.nB : 0.0;       // the same at twice the angle
    const double Rd = (T > 0.0 && fw.nS) ? sqrt(cd * cd + sd * sd) / (double)fw.nS : 0.0;       // over every adjacent step
    const double S  = Rd > 1e-6 ? R / sqrt(Rd) : 0.0;                                          // stationarity
    // whole refreshes: the intervals are sorted, so k = round(iv / T) only grows and each k's on-grid intervals are one run
    int on = 0, kmode = 0, kc1 = 0, kc2 = 0, hitchN = 0;
    {
        int runK = 0, runN = 0;
        for (int i = 0; i < n; i++)
        {
            const double ms = (double)fw.iv[i] * msT;
            if (ms > 4.0 * medIv) hitchN++;
            if (T <= 0.0) continue;
            const double x = ms / T; const int k = (int)(x + 0.5);
            if (k < 1 || absd(x - (double)k) > 0.10) continue;
            on++;
            if (k == runK) { runN++; continue; }
            if (runN > kc1) { kc2 = kc1; kc1 = runN; kmode = runK; } else if (runN > kc2) kc2 = runN;
            runK = k; runN = 1;
        }
        if (runN > kc1) { kc2 = kc1; kc1 = runN; kmode = runK; } else if (runN > kc2) kc2 = runN;
    }
    const double G = n ? (double)on / n : 0.0, kfrac = n ? (double)kc1 / n : 0.0, mix2 = n ? (double)kc2 / n : 0.0;
    const double hitch = n ? (double)hitchN / n : 0.0;
    const int nA = fw.nA;
    LatFlipSort(fw.a, nA);
    const double Amed = nA ? LatFlipMed(fw.a, nA, msT) : 1e9;
    const double SA   = nA ? LatFlipPct(fw.a, nA, 0.80, msT) - LatFlipPct(fw.a, nA, 0.10, msT) : 1e9;
    const LONG paceF = latPaceFrames - fw.paceF0, paceS = latPaceSleptN - fw.paceN0;
    const double pacer = paceF > 0 ? (double)paceS / (double)paceF : 0.0;
    const bool ceiling = mean <= 1.03 * T;
    const bool periodic = kfrac >= 0.90 && mix2 < 0.08;
    const bool pinned = iqr <= 0.03 * medIv;
    int fm = 0;                                            // the known cap that binds, if one does
    auto tryF = [&](int F)
    {
        if (F <= 0) return;
        const double P = 1000.0 / (double)F;
        if (absd(medIv - P) <= (0.03 * P > 0.25 ? 0.03 * P : 0.25) && iqr <= 0.10 * P) fm = F;
    };
    tryF(latFfCap);
    if (latFfRel > 0 && hz > 0.0) { tryF((int)(hz + 0.5) - latFfRel); tryF((int)hz - latFfRel); }   // FusionFix subtracts from the game's integer refresh: rounded or truncated
    tryF(latDxvkCap);
    double aLim = marker ? 0.30 * T : 0.45 * T;            // a_lim = clamp(0.30 T, 1.5 ms, 0.60 T) on the marker, clamp(0.45 T, 3 ms, 0.70 T) on tIn
    if (aLim < (marker ? 1.5 : 3.0)) aLim = marker ? 1.5 : 3.0;
    if (aLim > (marker ? 0.60 : 0.70) * T) aLim = (marker ? 0.60 : 0.70) * T;
    const double sLim = (marker ? 0.20 : 0.25) * T;
    const bool blocked = (double)mB >= 0.25 * mean;
        // ---- the decision rule, first match wins
    enum { W_REFRESH, W_FEW, W_SLOW, W_HITCH, W_QMIX, W_PACER, W_PINSYNC, W_PINNOWAIT, W_LIMITED, W_LATCHED, W_ABOVE, W_BELOW,
           W_CEIL, W_HALF, W_FREE, W_FREELATE, W_PERIODIC, W_PINNED, W_JITTER, W_NOWAIT, W_BETWEEN };
    LONG v = 4; int why = W_BETWEEN; bool pin = false, memok = false;
    if (T <= 0.0)                            { why = W_REFRESH; }
    else if (n < 96)                         { why = W_FEW; }
    else if (1000.0 / mean < 32.0)           { why = W_SLOW; }
    else if (hitch > 0.10)                   { why = W_HITCH; }
    else if (!ceiling && (R >= 0.80 || (R >= 0.50 && S >= 0.90)) && G >= 0.50 && mix2 >= 0.08) { v = 2; why = W_QMIX; memok = vs; }
    else if (!vs && absd(mean - T) <= 0.015 * T && R >= 0.80 && !fm)
    {
        if (pacer > 0.10)                    why = W_PACER;
        else if ((double)mB >= 0.10 * T)     { why = W_PINSYNC; pin = true; }
        else                                 why = W_PINNOWAIT;
    }
    else if (fm)                             { v = 3; why = W_LIMITED; }
    else if (!vs)
    {
        if (latFlipLatchWin)                 why = W_LATCHED;
        else if (mean <= 0.97 * T && !pinned) { v = 1; why = W_ABOVE; }
        else                                 why = W_BELOW;
    }
    else if (ceiling)                        why = W_CEIL;
    else if (R < 0.40 && R2 >= 0.80)         why = W_HALF;
    else if (R <= 0.40 && nA >= 60 && (Amed <= aLim || (blocked && !pinned && Amed <= 1.25 * T)) && SA <= sLim && (marker || blocked))
                                             { v = 1; why = Amed <= aLim ? W_FREE : W_FREELATE; memok = true; }
    else if (periodic)                       why = W_PERIODIC;
    else if (pinned)                         why = W_PINNED;
    else if (R >= 0.5 && SA >= 0.40 * T)     why = W_JITTER;
    else if (Amed > aLim)                    why = W_NOWAIT;
        // ---- the latch, the memory and the settled verdict
    if (straddle) { pin = false; memok = false; }          // a straddling window feeds neither
    latFlipPinRun = pin ? latFlipPinRun + 1 : 0;
    if (latFlipPinRun >= 2 && !latFlipLatchWin) { latFlipLatchWin = win; latchChanged = true; }
    if ((v == 1 || v == 2) && memok)
    {
        latFlipInfo[0] = latFlipInfo[1]; latFlipInfo[1] = v;
        if (latFlipInfo[0])
        {
            const LONG mem = latFlipInfo[0] == latFlipInfo[1] ? v : 0;   // two that agree, or none: a contradiction is a warning
            if (mem != latFlipDisplay) { latFlipDisplay = mem; latFlipDisplaySince = win; memChanged = true; }
        }
    }
    bool settled = false;
    if (v == latFlipLast && v != latFlipSettled) { latFlipSettled = v; latFlipSince = win - 1; settled = true; }
    latFlipLast = v; latFlipWins[v]++;
        // ---- the log lines. The change line: always on a memory or latch change; a settled change at most once in 15 windows (the
    // rest held back, the latest said later); and at window 30 if nothing printed, so a log proves the classifier ran.
    bool say = memChanged || latchChanged;
    if (settled && !say)
    {
        if (!latFlipSaidWin || win - latFlipSaidWin >= 15) say = true;
        else { latFlipHeld++; latFlipPending = 1; }
    }
    else if (!say && latFlipPending && win - latFlipSaidWin >= 15) say = true;
    if (!latFlipSaidWin && win >= 30) say = true;
    bool dev = false;
    DEVSTAT(dev = logLevel >= 2;)
    if (say || dev)
    {
        char whyTxt[112];
        switch (why)
        {
        case W_REFRESH:  strcpy_s(whyTxt, "the refresh is unknown"); break;
        case W_FEW:      strcpy_s(whyTxt, "fewer than 96 consecutive intervals"); break;
        case W_SLOW:     strcpy_s(whyTxt, "below 32 fps: a load, a menu or a runtime cap"); break;
        case W_HITCH:    strcpy_s(whyTxt, "hitches: over a tenth of the intervals past four times the median"); break;
        case W_QMIX:     strcpy_s(whyTxt, "a phase-locked mix of whole refreshes"); break;
        case W_PACER:    strcpy_s(whyTxt, "the pacer slept: the begin stamps are its own clock"); break;
        case W_PINSYNC:  strcpy_s(whyTxt, "pinned at the refresh with vsync off: a sync the mod did not ask for"); break;
        case W_PINNOWAIT: strcpy_s(whyTxt, "pinned at the refresh with vsync off and no wait inside Present"); break;
        case W_LIMITED:  _snprintf_s(whyTxt, sizeof(whyTxt), _TRUNCATE, "the known cap of %d fps binds", fm); break;
        case W_LATCHED:  _snprintf_s(whyTxt, sizeof(whyTxt), _TRUNCATE, "vsync off, and a sync the mod did not ask for was seen at window %ld", latFlipLatchWin); break;
                // Rule 4's FREE says only that nothing held the presents (tearing, a compositor or Fast Sync); it never feeds the memory.
        case W_ABOVE:    strcpy_s(whyTxt, "above the refresh and unsynced at Present: tearing, or a compositor or Fast Sync showing the latest"); break;
        case W_BELOW:    strcpy_s(whyTxt, "vsync off at the game: below the refresh a synced display does not reach Present"); break;
        case W_CEIL:     strcpy_s(whyTxt, "at the refresh ceiling: VRR pins there too"); break;
        case W_HALF:     strcpy_s(whyTxt, "locked to half the refresh period read: the refresh read is wrong"); break;
        case W_FREE:     strcpy_s(whyTxt, "released a steady short time after the frame's end, off the refresh grid"); break;
        case W_FREELATE: strcpy_s(whyTxt, "released a steady time after the frame's end, off the grid (a late present wait, the worker blocked)"); break;
        case W_PERIODIC: _snprintf_s(whyTxt, sizeof(whyTxt), _TRUNCATE, "periodic at %d refreshes (a cap at Hz/%d looks the same)", kmode, kmode); break;
        case W_PINNED:   _snprintf_s(whyTxt, sizeof(whyTxt), _TRUNCATE, "pinned at %.1f fps by a limiter the mod does not know", medIv > 0.0 ? 1000.0 / medIv : 0.0); break;
        case W_JITTER:   strcpy_s(whyTxt, "near the grid, too much release jitter"); break;
        case W_NOWAIT:   strcpy_s(whyTxt, "the presents did not wait for the display"); break;
        default:         strcpy_s(whyTxt, "between the two shapes"); break;
        }
        char lim[112] = "", part[64];
        if (latFfCap > 0) { _snprintf_s(part, sizeof(part), _TRUNCATE, "limiter %d fps (FusionFix)", latFfCap); strcat_s(lim, part); }
        if (latFfRel > 0)
        {
            const int r1 = (int)hz - latFfRel, r2 = (int)(hz + 0.5) - latFfRel;
            if (hz <= 0.0) _snprintf_s(part, sizeof(part), _TRUNCATE, "limiter Custom -%d, the refresh unknown (FusionFix)", latFfRel);
            else if (r1 != r2) _snprintf_s(part, sizeof(part), _TRUNCATE, "limiter Custom -%d = %d or %d fps (FusionFix)", latFfRel, r1, r2);
            else _snprintf_s(part, sizeof(part), _TRUNCATE, "limiter Custom -%d = %d fps (FusionFix)", latFfRel, r1);
            strcat_s(lim, part);
        }
        if (latDxvkCap > 0) { _snprintf_s(part, sizeof(part), _TRUNCATE, "%slimiter %d fps (DXVK)", lim[0] ? " and " : "", latDxvkCap); strcat_s(lim, part); }
        if (!lim[0]) strcpy_s(lim, "no limiter");
        const double aShow = nA ? Amed : 0.0, sShow = nA ? SA : 0.0;
        char fx[512];
        _snprintf_s(fx, sizeof(fx), _TRUNCATE,
            "%.1f fps = %.2f of %.3f Hz; phase lock %.2f, stationarity %.2f, half-period %.2f; whole refreshes %d%% (%dT %d%%, next %d%%);"
            " anchor %.2f ms = %.2f T, spread %.2f T (%s, lag %d); block %.2f ms = %.2f of the interval; vsync %s; %s; pacer slept on %d%%;"
            " %d intervals; %s",
            mean > 0.0 ? 1000.0 / mean : 0.0, (mean > 0.0 && hz > 0.0) ? 1000.0 / mean / hz : 0.0, hz, R, S, R2,
            (int)(G * 100.0 + 0.5), kmode, (int)(kfrac * 100.0 + 0.5), (int)(mix2 * 100.0 + 0.5),
            aShow, T > 0.0 ? aShow / T : 0.0, T > 0.0 ? sShow / T : 0.0, marker ? "frame end" : "injection retire", (int)fw.lag,
            (double)mB, mean > 0.0 ? (double)mB / mean : 0.0, vs ? "on" : "off", lim, (int)(pacer * 100.0 + 0.5), n, whyTxt);
        // A development build adds the worker's own returns (the DEVSTAT ring, ivt_latency.h): how long after the release the
        // render thread began, and whether the presents that waited inside DXVK's Present sat on the grid themselves.
        DEVSTAT({
            const int nr = fw.nRel; LatFlipSort(fw.relD, nr);
            const double usT = ivtQpcFreq ? 1e6 / (double)ivtQpcFreq : 0.0;
            double wc = 0.0, ws = 0.0;
            for (int k = 0; k < 32; k++) { wc += fw.relCnt[k] * (double)latFlipCos[k]; ws += fw.relCnt[k] * (double)latFlipCos[(k - 8) & 31]; }
            const size_t used = strlen(fx);
            _snprintf_s(fx + used, sizeof(fx) - used, _TRUNCATE, "; worker returns: begin %.0f us after the release (p90 %.0f), %d of %d waited over 0.25 T, their phase lock %.2f",
                LatFlipMed(fw.relD, nr, usT), LatFlipPct(fw.relD, nr, 0.90, usT), (int)fw.nWait, nr, fw.nWait ? sqrt(wc * wc + ws * ws) / (double)fw.nWait : 0.0);
        })
        LogHealth("  flip: window %ld: %s (%s)", win, name(v), fx);
        if (say)
        {
            char latch[80] = "", held[48] = "";
            if (latFlipLatchWin) _snprintf_s(latch, sizeof(latch), _TRUNCATE, ", a sync the mod did not ask for since window %ld", latFlipLatchWin);
            if (latFlipHeld) _snprintf_s(held, sizeof(held), _TRUNCATE, "; %ld changes held back", latFlipHeld);
            // A held or window-30 line may print features beside a settled verdict they do not show, so the window's own verdict leads
            // when the two differ.
            const LONG shown = latFlipSettled ? latFlipSettled : 4;
            char cur[40] = "";
            if (v != shown) _snprintf_s(cur, sizeof(cur), _TRUNCATE, "window %ld read %s: ", win, name(v));
            LogRecord("  flip: presents read %s since window %ld (%s%s; display %s since window %ld%s%s; windows FREE %ld, QUANTISED %ld, LIMITED %ld, UNCERTAIN %ld%s%s)",
                name(shown), latFlipSince, cur, fx, name(latFlipDisplay), latFlipDisplaySince,
                latFlipDisplay == 1 ? ", VRR or a driver's Fast Sync over vsync (timing cannot tell them apart)" : "", latch,
                latFlipWins[1], latFlipWins[2], latFlipWins[3], latFlipWins[4], held,
                straddle ? "; this window straddled a Reset or a vsync change and fed neither the memory nor the latch" : "");
            latFlipSaidWin = win; latFlipHeld = 0; latFlipPending = 0;
        }
    }
    // ---- the next window starts empty; the last begin, its frame and the ring of ends carry over
    fw.nB = fw.nIv = fw.nA = fw.nS = 0;
    memset(fw.cnt, 0, sizeof(fw.cnt)); memset(fw.stp, 0, sizeof(fw.stp));
    DEVSTAT(fw.nRel = fw.nWait = 0; memset(fw.relCnt, 0, sizeof(fw.relCnt));)
}

// The waiter's calls into the armed frame generation (source/fg/fg_arm_life.h), only in a build that carries it.
#if DLSSIV_FG
static bool FgpOwnsEnd(LONG f);                   // the machine signals frame f's end itself
static int  FgpWaiterSample(LONG fe, LONG fp, bool valid, LONGLONG b, float p, float h, float q, float l,
                            float* wP, float* wH, float* wAge, float* wQ, float* wL, int* wn, int cap);   // the route and the deferral
static void FgpWaiterResync();                    // the deferred samples go with a resync
static void FgpArmWindowStep(LONG latWin, float mP, float mAge, float mH, const float* wP, int wn);   // the plan from this window's K; the conditions with its real rate and intervals
static bool FgpLawOwnsLadder();                   // the law holds the adaptive ladder at rung 1 from the wait for a cut to the stand-down
#endif
// THE LATENCY WAITER'S LOOP (DlssIvTransport.h, at its declaration, says why it is defined here).
static void LatWaiterLoop()
{
    float wP[LAT_WIN], wH[LAT_WIN], wAge[LAT_WIN], wQ[LAT_WIN], wL[LAT_WIN]; int wn = 0;
    LatBlockMark_t blk = {};
    UINT64 m = 0, mPrev = 0; LONGLONG tInPrev = 0, tOutPrev = 0, tEndPrev = 0; LONG qPrev = 0;
    LONG periodUs = latPeriodUs;                           // the period EMA, this thread's own; continues from the published value
        // The flip classifier runs in the armed product at any log level (the fixed-refresh mode reads its display memory, FgpFixQuant
    // in fg_arm_life.h), otherwise only for a log. Its window lives on this stack and is zeroed by the resync, the loop's first
    // pass. About 4 us a window; its lines stay behind the log level.
#if DLSSIV_FG
    const bool flipOn = true;
#else
    const bool flipOn = telemetryOn;
#endif
    LatFlipWin_t fw;
    for (;;)
    {
        if (latStopReq) return;                                // a plain load, not a syscall
        if (m == 0)                                            // resync: the next frame to be submitted
        {
            m = (UINT64)(ULONG)ivtWatchAwait + 1; if (m <= kIvtProbeN) m = kIvtProbeN + 1;
            tInPrev = 0; wn = 0; LatBlockMark(&blk);
            if (flipOn) memset(&fw, 0, sizeof(fw));           // the classifier's window resyncs too
            if (latPacerOn) LatPaceReset();
            FGA(FgpWaiterResync();)                           // and the armed product's
        }
        DEVSTAT(const bool lateIn = telemetryOn && FeedVkTimelineValue(&ivtVk, ivtSemIn) >= m;)   // feeds only latLate, a log figure: no driver call at log=0
        VkResult r = (VkResult)LatWaitIn(&ivtVk, ivtSemIn, m, 250);   // times out for a frame that was never sent: the end of DLSS-IV.cpp says why
        if (r == VK_TIMEOUT) { if (tInPrev) latResyncs++; m = 0; _InterlockedExchange(&latPaceArmed, 0); continue; }
        if (r != VK_SUCCESS) { latWhyNot = "the wait on the mod's own in-semaphore failed (a lost device?)"; return; }
        LARGE_INTEGER t; QueryPerformanceCounter(&t); const LONGLONG tIn = t.QuadPart;
        const LONG q = (LONG)(ULONG)ivtWatchAwait - (LONG)(ULONG)m;   // submitted and not yet retired, at this instant
        const LONG f = latOrd[m & (LAT_RING - 1)];                     // the render-thread frame this injection belongs to
        FGA(const bool fgEnd = latEndOn && FgpOwnsEnd(f);)   // while engaged, the waiter does not block on the marker
        const bool wantEnd = latEndOn FGA(&& !fgEnd);
                // The helper's answer and the frame's end come in either order: pipelined, the end first; same frame, the answer first.
        // Up to 2 s in all.
        LONGLONG tOut = 0, tEnd = 0;
        for (int tries = 0; tries < 8; tries++)
        {
            if (latStopReq) return;                            // checked before every wait, so after every timeout too
            if (tOut && (!wantEnd || tEnd)) break;
            if (!tOut && wantEnd && !tEnd) r = FeedVkWaitAny2R(&ivtVk, ivtSemOut, m, latSemEnd, (uint64_t)(ULONG)f, 250);
            else if (!tOut)                r = FeedVkWaitTimelineR(&ivtVk, ivtSemOut, m, 250);
            else                           r = FeedVkWaitTimelineR(&ivtVk, latSemEnd, (uint64_t)(ULONG)f, 250);
            if (r == VK_SUCCESS)
            {
                QueryPerformanceCounter(&t);
                if (!tOut && FeedVkTimelineValue(&ivtVk, ivtSemOut) >= m) tOut = t.QuadPart;
                if (wantEnd && !tEnd && FeedVkTimelineValue(&ivtVk, latSemEnd) >= (uint64_t)(ULONG)f) tEnd = t.QuadPart;
                tries--;                                       // a success is not a timeout
                continue;
            }
            if (r != VK_TIMEOUT) { latWhyNot = "the wait on the mod's own semaphores failed (a lost device?)"; return; }
        }
        if (!tOut) { latResyncs++; m = 0; continue; }
        latStamps++; DEVSTAT(if (lateIn) latLate++;) if (q > latQMax) latQMax = q;
        if (latPacerOn)
        {   // the estimator's frame end: the frame's own GPU end when the marker is in, else this injection's retire
            // Only where LatPace can begin frames: held, under vsync or a limiter it begins none. This thread is the hold's one runtime
            // writer, so a plain read (C28112 suppressed).
#pragma warning(suppress: 28112)
            if (!latPaceHold && !latVsync && !latLimiter)
            {
                AcquireSRWLockExclusive(&latPaceLock); latLfx.EndFrame((uint64_t)(ULONG)f, LatQpcNs(tEnd ? tEnd : tIn), nullptr, nullptr); ReleaseSRWLockExclusive(&latPaceLock);
            }
            // The period is published with one plain aligned store; the render thread reads it for the sleep cap. The armed flag is
            // written only when it changes (plain read, C28112 suppressed).
            if (tInPrev) { const LONG pUs = (LONG)(LatMs(tIn - tInPrev) * 1000.0); periodUs = periodUs ? (periodUs * 7 + pUs) / 8 : pUs; latPeriodUs = periodUs; }
            const LONG armed = (latVsync || latLimiter) ? 0 : 1;
#pragma warning(suppress: 28112)
            if (latPaceArmed != armed) _InterlockedExchange(&latPaceArmed, armed);
        }
        if (tInPrev && mPrev + 1 == m)
        {
            const LONG fp = latOrd[mPrev & (LAT_RING - 1)]; const int i = (int)(fp & (LAT_RING - 1));
            const LONG f1 = ReadAcquire(&latBeginFrame[i]);                  // value, stamp, value in plain ordered loads on x86, the stamp one 8-byte move
            const LONGLONG b = __iso_volatile_load64(&latBeginQpc[i]);        // one 8-byte load, no lock (ivt_latency.h)
            const LONG lead = latLeadAt[i];
            const LONG f2 = ReadAcquire(&latBeginFrame[i]);
            // The frame that shows injection mPrev's upscale: pipelined, the next frame; same frame, its own.
            const bool aPrev = latOrdAsync[mPrev & (LAT_RING - 1)] != 0;   // how frame mPrev ran, whatever the mode says now
            const LONGLONG endTrue  = aPrev ? (tEnd ? tEnd : tIn) : (tEndPrev ? tEndPrev : tOutPrev);
            FGA(const int fgTook = FgpWaiterSample(aPrev ? f : fp, fp, f1 == fp && f2 == fp && b != 0, b, (float)LatMs(tIn - tInPrev), (float)LatMs(tOutPrev - tInPrev), (float)qPrev, (float)lead, wP, wH, wAge, wQ, wL, &wn, LAT_WIN);)   // the armed product's route, as the rig's
            if (FGA(!fgTook &&) f1 == fp && f2 == fp && b != 0 && endTrue > b && wn < LAT_WIN)
            {
                wP[wn] = (float)LatMs(tIn - tInPrev); wH[wn] = (float)LatMs(tOutPrev - tInPrev);
                wAge[wn] = (float)LatMs(endTrue - b); wQ[wn] = (float)qPrev; wL[wn] = (float)lead; wn++;
                // The same accepted frame for the flip classifier: its phase, interval and anchor.
                if (flipOn) LatFlipTake(fw, fp, b, dtArmed ? 3 : 2, latEndOn);
            }
            if (wn == LAT_WIN)
            {
                const float mP = LatMedian(wP, wn), mH = LatMedian(wH, wn), mAge = LatMedian(wAge, wn), mQ = LatMedian(wQ, wn), mL = LatMedian(wL, wn);
                const float mB = (float)LatBlockMs(blk);
                { const int slot = latWindows % LAT_MAXWIN; latMedAge[slot] = mAge; latMedP[slot] = mP; latMedQ[slot] = mQ; latMedH[slot] = mH; latMedL[slot] = mL; latMedB[slot] = mB; }   // a ring of the last 4096 windows' medians; order does not matter to a median
                latWindows++; wn = 0; LatBlockMark(&blk);
                // The window's flip verdict, before the adaptive step so the law reads this window's verdict. Cold, once a window; clears fw.
                if (flipOn) LatFlipStep(fw, mB, latWindows);
                FGA(FgpArmWindowStep(latWindows, mP, mAge, mH, wP, LAT_WIN);)   // the armed product: planned generation time and conditions, once a window
                if (IvtAdaptiveOn() FGA(&& !FgpLawOwnsLadder())) LatAdaptiveStep(mP, mQ, mAge);   // the armed law owns the ladder from the wait for a cut to the stand-down
                if (telemetryOn && (latWindows % 5) == 0)
                    LogHealth("  latency: window %ld: picture age median %.1f ms = %.2f frames%s, GPU period %.2f ms (%.0f fps), in flight %.0f (max %ld),"
                              " helper %.2f ms, device-thread lead %.0f, present block %.2f ms; %ld stamps (late %ld, resyncs %ld);"
                              " pacer %s, slept %.2f ms a present on %ld%% of %ld%s",
                        latWindows, mAge, mP > 0.0f ? mAge / mP : 0.0f, latEndOn ? " to the frame end" : "", mP, mP > 0.0f ? 1000.0f / mP : 0.0f, mQ, latQMax, mH, mL, mB,
                        latStamps, latLate, latResyncs,
                        !latPacerOn ? "off" : latVsync ? "stood down (vsync)" : latLimiter ? "stood down (limiter)" : InterlockedCompareExchange(&latPaceHold, 0, 0) ? "held by the adaptive step" : InterlockedCompareExchange(&latPaceArmed, 0, 0) ? "armed" : "idle",
                        latPaceFrames ? LatMs(_InterlockedCompareExchange64(&latPaceSleptTicks, 0, 0)) / (double)latPaceFrames : 0.0,
                        latPaceFrames ? (long)(100 * latPaceSleptN / latPaceFrames) : 0L, latPaceFrames,
                        latEndOn ? "" : !latPacerOn ? "; no frame-end marker (pacer off), the age ends at the next injection's retire" : "; no frame-end marker (its timeline could not be created), the age ends at the next injection's retire");
                DEVSTAT(if (latEndOn && telemetryOn && (latWindows % 5) == 0) { const LONG mn = latMarkN; const LONGLONG mw = InterlockedExchange64(&latMarkWorst, 0); LogHealth("  latency: window %ld: the frame-end marker took %.1f us a present (the lock %.1f us), worst %.1f us since the last line, over %ld presents", latWindows, mn ? LatMs(_InterlockedCompareExchange64(&latMarkTicks, 0, 0)) * 1000.0 / (double)mn : 0.0, mn ? LatMs(_InterlockedCompareExchange64(&latMarkLockTicks, 0, 0)) * 1000.0 / (double)mn : 0.0, LatMs(mw) * 1000.0, mn); })
                // The pacer timer's lateness, the figure the slack is sized from (ivt_latency.h, LatSleepUntil).
                DEVSTAT(if (latPacerOn && telemetryOn && (latWindows % 5) == 0) { const LONG tn = latTimerN; const LONGLONG tw = InterlockedExchange64(&latTimerLateWorst, 0); LogHealth("  latency: window %ld: the pacer's timer woke %.1f us late on average (wake minus due), worst %.1f us since the last line, %ld of %ld waits later than its %.1f ms slack (%s)", latWindows, tn ? (double)_InterlockedCompareExchange64(&latTimerLateNs, 0, 0) / 1000.0 / (double)tn : 0.0, (double)tw / 1000.0, (long)latTimerPastSlack, (long)tn, latPaceHighRes ? 0.6 : 1.5, !latPaceTimerTried ? "no timer made yet" : latPaceHighRes ? "the high-resolution timer" : latPaceTimer ? "the fallback timer" : "no timer, Sleep"); })
                if (latWindows == 30 && !latSaidOnce && telemetryOn) { latSaidOnce = true; LatSummaryLine(false); }   // End Task never reaches the session line
            }
        }
        if (flipOn) LatFlipKeep(fw, f, tIn, tEnd);                    // this frame's ends, for a later frame's anchor
        tInPrev = tIn; tOutPrev = tOut; tEndPrev = tEnd; mPrev = m; qPrev = q; m++;
    }
}

// Frame generation's declarations, after the last hot static: a type declared among the hot statics' definitions reorders
// .data$dlhot (transport/feed_fg.h). Pipe budget: one sent injection gives at most one 'Q' and one 'G', and a build may carry
// one 'g'.
#if DLSSIV_FG
#include "transport/feed_fg.h"
static_assert((IVT_RING + 2) * (sizeof(FeedTaggedFrameIvt) + sizeof(FeedFgQIvt) + sizeof(FeedFgGIvt)) + 1 + sizeof(FeedBuild) + sizeof(FeedHello)
              + sizeof(FeedFgBuildIvt) + sizeof(FeedFgFencesIvt) <= FEED_PIPE_BYTES,   // under Wine the 'g' carries the game's three fences, 24 bytes
              "the pipe no longer holds a ring of frames with frame generation's messages: its writes could block on a stuck host");
#include "fg/fg_xport.h"     // the game's side of a set; the fake client runs these same lines
#endif

// The gate before DXVK's Present, included here in a build with frame generation because fg/fg_product.h (FgP::DxvkBare,
// FgP::PresentGate) needs it; #pragma once then makes the include at the end of this file read nothing.
#if DLSSIV_FG || DLSSIV_DEVLOG
#include "transport/present_gate.h"
#endif

// Last in this file, so it sees every global and moves no function before it.
#if DLSSIV_FG
#include "devthread/dt_bbs.h"   // whether the back buffer was written since the mark, ahead of the armed binding that asks it (fg/fg_arm.h)
#endif

// Frame generation's cold side (source/fg/fg_client.h).
#if DLSSIV_FG
#include "fg/fg_client.h"
#endif

// ---- what the vsync freeze fix adds, after the last hot static so it moves none of them ----
// The gate before DXVK's Present.
#include "transport/present_gate.h"

// DtWait (dt_core.h) says after five seconds that the worker "is stuck inside DXVK or the driver" and warns to set threaded=0.
// Under vsync the worker may instead be held in the gate before DXVK's Present (present_gate.h) by a silent helper. So while
// the gate holds, the line names the helper (once a session), the warning is not written and the reasons go back to "not
// said", so a later real stall still gets its line and warning. DtWait asks again every 20 ms while the hold lasts.
// Both functions take DtWait's own arguments in the places of LogRecord and IvtWarn: that form keeps the hot section's layout.
static bool dtStuckIsGate = false;      // the notice just said was the gate's: no warning
// When DXVK's command thread has ended on an error, DXVK logs "Exception on CS thread!" and stops taking work, and the device
// thread waits inside DXVK for good. The last 8 KB of this session's DXVK log (DxvkLogNewest) are read once the wait has lasted
// five seconds, on the waiting thread, and the notice and the warning say what that log says.
static char dtStuckCsLog[MAX_PATH] = "";   // DXVK's log when it says its command thread ended; the warning names it
static bool DtDxvkCsEnded()
{
    FILETIME ftCreate = {}, ftExit = {}, ftKernel = {}, ftUser = {};
    ULARGE_INTEGER born = {};
    if (GetProcessTimes(GetCurrentProcess(), &ftCreate, &ftExit, &ftKernel, &ftUser))
    { born.LowPart = ftCreate.dwLowDateTime; born.HighPart = ftCreate.dwHighDateTime; }
    char path[MAX_PATH] = "", stem[64] = "";
    if (DxvkLogNewest(path, stem, born.QuadPart, nullptr) != 1) return false;
    path[MAX_PATH - 1] = 0;
    FILE* f = _fsopen(path, "rb", _SH_DENYNO);
    if (!f) return false;
    char tail[8193];
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, size > 8192 ? size - 8192 : 0, SEEK_SET);
    const size_t n = fread(tail, 1, 8192, f);
    fclose(f);
    tail[n] = 0;
    for (size_t i = 0; i < n; i++) if (!tail[i]) tail[i] = ' ';      // strstr must see the whole of it
    if (!strstr(tail, "Exception on CS thread")) return false;
    strcpy_s(dtStuckCsLog, path);
    return true;
}
static void DtStuckSay(_Printf_format_string_ const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    const int seconds = va_arg(ap, int); const char* reason = va_arg(ap, const char*);
    va_end(ap);
    dtStuckIsGate = ivtGate.holding != 0;
    dtStuckCsLog[0] = 0;
    if (!dtStuckIsGate && DtDxvkCsEnded())
    {
        LogRecord("  threaded: the render thread has waited %d s for the device thread (%s): DXVK's command thread ended on an error (%s)", seconds, reason, dtStuckCsLog);
        return;
    }
    if (!dtStuckIsGate) { LogRecord(fmt, seconds, reason); return; }
    static bool said = false;
    if (!said) { said = true; LogRecord("  threaded: the render thread has waited %d s for the device thread (%s): a present is held for the helper", seconds, reason); }
    memset(dtWaitSaid, 0, sizeof(dtWaitSaid));
}
static void DtStuckWarn(_Printf_format_string_ const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    const int threaded = va_arg(ap, int), seconds = va_arg(ap, int); const char* reason = va_arg(ap, const char*);
    va_end(ap);
    if (dtStuckCsLog[0])
        IvtWarn("The game froze because DXVK crashed. Please close the game and start it again. If it keeps happening, DXVK's own log"
                " shows why: %s. Changing threaded in DLSS-IV.cfg will not fix this", dtStuckCsLog);
    else if (!dtStuckIsGate) IvtWarn(fmt, threaded, seconds, reason);
}

// THE LATENCY WAITER'S WAIT ON THE IN-FENCE, WHICH TIMES OUT for a frame that was never sent. A dead helper's fences stand at
// the top, so every wait would return at once and the loop would spin until the transport stopped. In a live session m is at
// most two past the last frame the body published (ivtWatchAwait); a third is a fence past a frame nobody sent, and gets
// "timed out" after the wait's own time, asleep in slices the stop can end, so the loop's timeout branch resyncs and stands
// the pacer down. One load and one compare a frame. It replaces the loop's call of FeedVkWaitTimelineR argument for argument,
// the one form that keeps the hot section's layout.
static int LatWaitIn(FeedVk* vk, unsigned long long sem, unsigned long long m, unsigned timeout_ms)
{
    const UINT64 sent = (UINT64)(ULONG)ivtWatchAwait;
    if (m > (sent > kIvtProbeN ? sent : kIvtProbeN) + 2)
    {
        for (unsigned i = 0; i < timeout_ms / 10 && !latStopReq; i++) Sleep(10);
        return (int)VK_TIMEOUT;
    }
    return (int)FeedVkWaitTimelineR(vk, (VkSemaphore)sem, m, timeout_ms);
}
// ---- END OF WHAT THE VSYNC FREEZE FIX ADDS (tools/test-present-gate cuts LatWaitIn from its definition to this line) ----

#if DLSSIV_DEV
// ---- THE GLASS CENSUS. Development builds only: a release has none of it. ----
// A development tool for how the game draws glass, the dirt, bullet holes and blood on it. Counts every draw into the scene
// buffer (the mod's own excluded), keyed by pixel shader, blending, its two factors and the depth write: draws and primitives a
// frame, first and last place in the frame's order, eight more states at the first draw with a mark for any that differed,
// draws with a second target, draws after the seam, and which draw call. Every 1800 frames the table is logged and restarts.
// Each shader's bytecode is written once to DLSSIV-glass\ps_<hash>_<bytes>.bin for tools\shaders\fxc_match.py.
// The switch is the folder DLSSIV-glass beside GTAIV.exe; without it the four hooks pay one bool. State reads come from the
// device thread's own copy (dt_GetRenderState, dt_GetPixelShader): no drain.
enum { GC_ROWS = 192, GC_ST = 12, GC_KEY = 4, GC_PERIOD = 1800, GC_CODE = 65536 };
static const D3DRENDERSTATETYPE kGcSt[GC_ST] = { D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ZWRITEENABLE,
                                                 D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ZENABLE, D3DRS_ZFUNC,
                                                 D3DRS_COLORWRITEENABLE, D3DRS_CULLMODE, D3DRS_SEPARATEALPHABLENDENABLE };
struct GcRow
{
    IDirect3DPixelShader9* ps;      // an address, never dereferenced after its first sight
    DWORD hash; UINT size;
    DWORD st[GC_ST]; DWORD varies;
    LONG  draws, prims, mrt, afterSeam, ordMin, ordMax, kinds[4];
};
static GcRow gcRow[GC_ROWS];
static int   gcRows = 0, gcLast = 0;
static LONG  gcFrame = -1, gcOrd = 0, gcFrames = 0, gcDraws = 0, gcLost = 0, gcTables = 0;
static char  gcDir[MAX_PATH];
static BYTE  gcCode[GC_CODE];
static void GlassCensusInit()
{
    if (!IvtSideFilePath(gcDir, false, "DLSSIV-glass")) return;
    const DWORD a = GetFileAttributesA(gcDir);
    if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return;
    gcOn = true;
    LogRecord("  glass census: ON (the folder DLSSIV-glass is beside GTAIV.exe): every draw into the scene buffer is counted by pixel"
              " shader and blend state, a table every %d frames; each shader's bytecode is written into that folder once", GC_PERIOD);
}
static const char* GcBlend(DWORD b)
{
    static const char* const k[] = { "?", "ZERO", "ONE", "SRCCOLOR", "INVSRCCOLOR", "SRCALPHA", "INVSRCALPHA", "DESTALPHA", "INVDESTALPHA",
                                     "DESTCOLOR", "INVDESTCOLOR", "SRCALPHASAT" };
    return b < _countof(k) ? k[b] : "?";
}
static void GcShader(GcRow* r)
{
    r->hash = 0; r->size = 0;
    if (!r->ps) return;
    for (int i = 0; i < gcRows; i++)        // the same shader under another state: its name is known
        if (&gcRow[i] != r && gcRow[i].ps == r->ps && gcRow[i].size) { r->hash = gcRow[i].hash; r->size = gcRow[i].size; return; }
    UINT n = 0;
    if (FAILED(r->ps->GetFunction(nullptr, &n)) || !n || n > GC_CODE || FAILED(r->ps->GetFunction(gcCode, &n))) return;
    DWORD h = 2166136261u;
    for (UINT i = 0; i < n; i++) { h ^= gcCode[i]; h *= 16777619u; }
    r->hash = h; r->size = n;
    char path[MAX_PATH];
    if (_snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\ps_%08lX_%u.bin", gcDir, h, n) < 0) return;
    const HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);   // once: a file that is there stays
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0; WriteFile(f, gcCode, n, &w, nullptr); CloseHandle(f);
}
static void GcPrint()
{
    const float f = gcFrames > 0 ? 1.0f / (float)gcFrames : 0.0f;
    int live = 0;
    for (int i = 0; i < gcRows; i++) if (gcRow[i].draws) live++;
    LogRecord("  glass census: table %ld, %ld frames, %.1f draws a frame into the scene buffer, %d kinds (%ld draws found the table full)",
              ++gcTables, gcFrames, gcDraws * f, live, gcLost);
    bool done[GC_ROWS] = {};
    for (int n = 0; n < live; n++)
    {
        int best = -1;                       // in the frame's own order: the kind whose first draw comes earliest
        for (int i = 0; i < gcRows; i++)
            if (gcRow[i].draws && !done[i] && (best < 0 || gcRow[i].ordMin < gcRow[best].ordMin)) best = i;
        if (best < 0) break;
        done[best] = true;
        const GcRow& r = gcRow[best];
        LogRecord("  glass: ps %08lX %5u  %7.2f draws %9.1f prims a frame  order %ld..%ld  blend %lu %s/%s  zwrite %lu ztest %lu zfunc %lu"
                  "  alphatest %lu ref %lu func %lu  mask %lX cull %lu sep %lu  varies %03lX  mrt %ld afterseam %ld  calls %ld/%ld/%ld/%ld",
                  r.hash, r.size, r.draws * f, r.prims * f, r.ordMin, r.ordMax, r.st[0], GcBlend(r.st[1]), GcBlend(r.st[2]), r.st[3], r.st[7], r.st[8],
                  r.st[4], r.st[5], r.st[6], r.st[9], r.st[10], r.st[11], r.varies, r.mrt, r.afterSeam, r.kinds[0], r.kinds[1], r.kinds[2], r.kinds[3]);
    }
    for (int i = 0; i < gcRows; i++)
    {
        GcRow& r = gcRow[i];
        r.draws = r.prims = r.mrt = r.afterSeam = 0; r.ordMin = 0x7FFFFFFF; r.ordMax = -1; r.varies = 0;
        r.kinds[0] = r.kinds[1] = r.kinds[2] = r.kinds[3] = 0;
    }
    gcFrames = 0; gcDraws = 0; gcLost = 0;
}
static void GlassCensus(IDirect3DDevice9* dev, int kind, UINT prims)
{
    const LONG fr = (LONG)frames;
    if (fr != gcFrame)
    {
        gcFrame = fr; gcOrd = 0;
        if (++gcFrames > GC_PERIOD) { gcFrames--; GcPrint(); gcFrames = 1; }
    }
    const LONG ord = gcOrd++;
    gcDraws++;
    DWORD st[GC_ST];
    for (int i = 0; i < GC_ST; i++) { st[i] = 0xFFFFFFFFu; dev->GetRenderState(kGcSt[i], &st[i]); }
    IDirect3DPixelShader9* ps = nullptr;
    if (SUCCEEDED(dev->GetPixelShader(&ps)) && ps) ps->Release();     // the address is the key; the game holds the shader
    GcRow* r = nullptr;
    if (gcLast < gcRows && gcRow[gcLast].ps == ps && !memcmp(gcRow[gcLast].st, st, GC_KEY * sizeof(DWORD))) r = &gcRow[gcLast];
    else
        for (int i = 0; i < gcRows; i++)
            if (gcRow[i].ps == ps && !memcmp(gcRow[i].st, st, GC_KEY * sizeof(DWORD))) { r = &gcRow[i]; gcLast = i; break; }
    if (!r)
    {
        if (gcRows >= GC_ROWS) { gcLost++; return; }
        gcLast = gcRows; r = &gcRow[gcRows++];
        memset(r, 0, sizeof(*r));
        r->ps = ps; r->ordMin = 0x7FFFFFFF; r->ordMax = -1;
        memcpy(r->st, st, sizeof(st));
        GcShader(r);
    }
    if (!r->draws) memcpy(r->st, st, sizeof(st));                     // the states of the period's first draw of this kind
    else for (int i = GC_KEY; i < GC_ST; i++) if (r->st[i] != st[i]) r->varies |= 1u << i;
    r->draws++; r->prims += (LONG)prims; r->kinds[kind & 3]++;
    if (ord < r->ordMin) r->ordMin = ord;
    if (ord > r->ordMax) r->ordMax = ord;
    if (curRT123[0] || curRT123[1] || curRT123[2]) r->mrt++;
    if (seamThisFrame) r->afterSeam++;
}
// ---- END OF THE GLASS CENSUS ----
#endif

// ---- THE LADDER'S HELD FRAME RATE START (tools\test-ladder cuts from this line to the END line) ----
// A frame rate held from outside (RTSS, another vendor's cap, another mod) cannot be read; the mod reads only FusionFix's,
// DXVK's and NVIDIA's limiters. When every pair of the ladder's measurement is within half a percent and under a quarter of a
// frame is in flight, the period did not answer to the rung: something outside holds it. AdVerdictKept (the verdict's log call,
// same arguments) marks the rate held and says so. AdOutOfBandHeld, the resting state's band test, then keeps the 300-window
// re-check clock standing, and lets go when the rate rises two percent over the held one for three windows, which no cap
// allows. AdMeasure clears the mark. Both read the ladder's statics (DlssIvTransport.h); LatAdaptiveStep is the only caller.
static void AdVerdictKept(const char* fmt, ...)
{
    char b[512];
    va_list ap; va_start(ap, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, ap); va_end(ap);
    bool held = adQ1 < 0.25f && adPairs >= AD_PAIRS_MIN && adPairs <= AD_PAIRS_MAX;
    for (int i = 0; held && i < adPairs; i++)
    {
        const float r = adSumA[i] > 0.0f ? adSumB[i] / adSumA[i] : 0.0f;
        if (!(r >= 0.995f && r <= 1.005f)) held = false;
    }
    adHeld = held; adHeldUp = 0;
    if (!held) { LogRecord("%s", b); return; }
    if (char* tail = strstr(b, " for 300 windows")) *tail = 0;      // the caller's words for a verdict that will be re-checked
    LogRecord("%s; the frame rate is HELD", b);
}
static bool AdOutOfBandHeld(float mP, bool lighterOnly)
{
    if (adHeld && adState == AD_STAY1)
    {
        adHeldUp = (adBase > 0.0f && mP < adBase * 0.98f) ? adHeldUp + 1 : 0;
        if (adHeldUp >= 3)
        {
            adHeld = false; adHeldUp = 0;        // the count stays: the band rule below may speak in this same window
            LogRecord("  latency: ladder: the frame rate rose: not held any more");
        }
        if (adCount > 6) adCount = 6;           // the clock stands: past the band rule's five windows, short of the re-check's 300
    }
    return AdOutOfBand(mP, lighterOnly);
}
// ---- THE LADDER'S HELD FRAME RATE END ----

// ---- WHEEL NO SPIN START (tools\test-wheel-nospin cuts from this line to the END line)
// A release runs this on every tyre that moved; a development build has a key that puts the spin back (TrkOnDrawPre). Defined
// at the file's end, its declaration and memory at the top: a function or static among the hot statics moves them.
// A tyre is given the car's motion and its spin is left out of the vectors: a wheel's true vectors turn round its hub, up to
// 120 render pixels a frame, and the interpolated frame, stepping half of it along a straight line, tears the wheel.
// cur and prev are the tyre's transform this frame and last (wheel matrix times car's, row vectors): rows 0 to 2 the mesh's
// axes, row 3 the hub. A spin turns two rows round the third, the axle. So prev keeps its axle row and hub, and its other two
// rows become this frame's, carried by the smallest rotation that takes this frame's axle to last frame's.
// The axle is seen and remembered: on a clear frame (one row turned at most half as far as the next, and that one at least
// three degrees) the stillest row is the axle and *axleSeen keeps it; otherwise the remembered one, or with none the stillest.
// One memory for every tyre (the game's wheel meshes share their axle). Pitching about the axle is taken for spin too.
// Returns the axle row (0 to 2) when the wheel visibly turned; 4 when no row turned three degrees (prev still rewritten);
// 3 when the axle row turned over sixty degrees or a row is empty (not a wheel on its axle: prev left as it was).
static __declspec(noinline) int VelWheelNoSpin(const float* cur, float* prev, volatile long* axleSeen)
{
    float u[3][3], v[3][3], dot[3];
    for (int r = 0; r < 3; r++)
    {
        const float* c = cur + 4 * r; const float* p = prev + 4 * r;
        const float lc = sqrtf(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]), lp = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (!(lc > 1e-6f) || !(lp > 1e-6f)) return 3;
        for (int k = 0; k < 3; k++) { u[r][k] = c[k] / lc; v[r][k] = p[k] / lp; }
        dot[r] = u[r][0] * v[r][0] + u[r][1] * v[r][1] + u[r][2] * v[r][2];
    }
    int         a = (dot[0] >= dot[1] && dot[0] >= dot[2]) ? 0 : (dot[1] >= dot[2] ? 1 : 2);
    const float d1 = dot[(a + 1) % 3], d2 = dot[(a + 2) % 3], next = d1 > d2 ? d1 : d2, lo = d1 < d2 ? d1 : d2;   // a: the stillest row; of the other two, the stiller and the one that turned most
    if (next < 0.99863f && (1.0f - dot[a]) * 4.0f <= 1.0f - next) *axleSeen = a;      // clear (1 - cos is the angle squared: a quarter of it is half the angle)
    else if (*axleSeen >= 0) a = (int)*axleSeen;
    const float c = dot[a];
    if (!(c > 0.5f)) return 3;
    const float x[3] = { u[a][1] * v[a][2] - u[a][2] * v[a][1], u[a][2] * v[a][0] - u[a][0] * v[a][2], u[a][0] * v[a][1] - u[a][1] * v[a][0] };   // this frame's axle x last frame's
    for (int r = 0; r < 3; r++)
    {
        if (r == a) continue;
        const float* w = cur + 4 * r; float* o = prev + 4 * r;
        const float k = (x[0] * w[0] + x[1] * w[1] + x[2] * w[2]) / (1.0f + c);
        const float o0 = w[0] * c + (x[1] * w[2] - x[2] * w[1]) + x[0] * k;
        const float o1 = w[1] * c + (x[2] * w[0] - x[0] * w[2]) + x[1] * k;
        const float o2 = w[2] * c + (x[0] * w[1] - x[1] * w[0]) + x[2] * k;
        o[0] = o0; o[1] = o1; o[2] = o2;
    }
    return lo < 0.99863f ? a : 4;      // cos of three degrees
}
// ---- WHEEL NO SPIN END ----

// ---- THE DENTED VEHICLE'S REDRAW START ----
// GTA IV dents a vehicle in its vertex shader: every vehicle shader but the tyre's, rims', disc's and "basic" (132 vertex
// shaders) reads a damage map on vertex sampler 0, BoundRadius at c208 and a switch at b8, and moves each vertex up to half a
// metre before the world, bones and camera. The velocity redraw's own shaders read only position and bones, so a dented car's
// vectors would come from the undamaged body. Each of the three shaders has a variant with the game's damage first
// (shaders/velocity_dmg.hlsli, the game's arithmetic in its order), used for a mover whose draw had damage on:
// - VelDmgNote, from VelRecordObject on whichever side records: only for a shader that reads the map (four texldl on vertex
//   sampler 0 and c208 read: exactly the 132), with b8 on, a map bound and a usable radius. The entry holds its own reference
//   to the map (VelDropArr releases it). Otherwise dmgTex is null and the drain is unchanged.
// - VelDmgBind, from VelDrain's shader choice: the variant, the map on vertex sampler 1 (sampler 0 carries the previous-pose
//   texture), every one of the game's sampler states, BoundRadius in c247 (the mod's range, above c237 where the game and
//   FusionFix stop). Deduped across a drain. Sampler 1 is read once a drain before its first bind and VelDmgEnd restores it;
//   devthread/dt_trk.h takes the slot off dtBodySampFree.
// A wrong variant fails the depth test where it is off and leaves the camera's vector there. Every call used is plain
// Direct3D 9 that DXVK implements, so Wine runs it the same.
#include "shaders/velocity_dmg_vs.h"
#include "shaders/velocity_skin_dmg_vs.h"
#include "shaders/velocity_skin_prev_dmg_vs.h"
#include "vel_dmg.h"   // the shader test the desk proof runs too, and VEL_DMG_TOK_MAX
// Every sampler state from ADDRESSU (1) to DMAPOFFSET (13), copied from the game's vertex sampler 0 with the map and restored
// on sampler 1 after the drain.
static const D3DSAMPLERSTATETYPE kVelDmgSamp[VEL_DMG_SAMP_N] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_BORDERCOLOR,
    D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MAXANISOTROPY,
    D3DSAMP_SRGBTEXTURE, D3DSAMP_ELEMENTINDEX, D3DSAMP_DMAPOFFSET };
// Shaders asked about. Only candidates are asked (the tracker counted four texldl on vertex sampler 0), which over the game's
// shaders is exactly the 132 damage readers, so 256 holds them. Lookup: one compare against the last shader, else a 64-entry
// index, scanning only when two share a slot.
static const int VEL_DMG_VS_CACHE = 256;
static IDirect3DVertexShader9* velDmgVSPtr[VEL_DMG_VS_CACHE];   // every candidate asked about, a reference held
static bool                    velDmgVSReads[VEL_DMG_VS_CACHE];
DLSSIV_FGDATA static int                     velDmgVSN = 0;
static int                     velDmgVSIdx[64];                 // pointer bits to index + 1, in front of the scan
DLSSIV_FGDATA static IDirect3DVertexShader9* velDmgLastVS = nullptr;           // a vehicle is many draws of one shader in a row
DLSSIV_FGDATA static bool                    velDmgLastReads = false;
static DWORD                   velDmgTok[VEL_DMG_TOK_MAX];       // one shader's tokens: only the live tracker side asks, one at a time
DLSSIV_FGDATA static IDirect3DVertexShader9* velDmgVS[3] = { nullptr, nullptr, nullptr };   // the variants of velVS, velSkinVS, velSkinPrevVS
DLSSIV_FGDATA static bool                    velDmgVSTried = false;
DLSSIV_FGDATA static bool                    velDmgHeld = false;               // this drain has bound vertex sampler 1
DLSSIV_FGDATA static IDirect3DBaseTexture9*  velDmgOldTex = nullptr;           // what it held before, and every one of its sampler states
DLSSIV_FGDATA static DWORD                   velDmgOldSamp[VEL_DMG_SAMP_N];
DLSSIV_FGDATA static IDirect3DBaseTexture9*  velDmgBoundTex = nullptr;
DLSSIV_FGDATA static DWORD                   velDmgBoundSamp[VEL_DMG_SAMP_N];
DLSSIV_FGDATA static bool                    velDmgSampKnown = false;          // velDmgBoundSamp holds sampler 1's states (no value can mean unknown)
static float                   velDmgBoundRadius = -1.0f;

// Four texldl on vertex sampler 0 and c208 read as a source (vel_dmg.h; tools\test-vel-damage runs it on every game shader):
// the game's damage-reading vertex shaders and nothing else. A shader larger than the buffer is refused; the game's largest
// is under it.
static bool VelDmgFingerprint(IDirect3DVertexShader9* vs)
{
    UINT size = 0;
    if (FAILED(vs->GetFunction(nullptr, &size)) || size < 8 || size > sizeof(velDmgTok)) return false;
    if (FAILED(vs->GetFunction(velDmgTok, &size))) return false;
    return VelDmgPrintTok(velDmgTok, size / 4);
}

static bool VelDmgReads(IDirect3DVertexShader9* vs)
{
    if (vs == velDmgLastVS) return velDmgLastReads;
    bool reads = false, known = false;
    const unsigned slot = ((unsigned)(UINT_PTR)vs >> 4) & 63u;
    const int k = velDmgVSIdx[slot] - 1;
    if (k >= 0 && velDmgVSPtr[k] == vs) { reads = velDmgVSReads[k]; known = true; }
    for (int i = 0; !known && i < velDmgVSN; i++)
        if (velDmgVSPtr[i] == vs) { reads = velDmgVSReads[i]; velDmgVSIdx[slot] = i + 1; known = true; }
    if (!known)
    {
        if (velDmgVSN >= VEL_DMG_VS_CACHE) return false;   // past the cap: the plain redraw
        __try { reads = VelDmgFingerprint(vs); }
        __except (EXCEPTION_EXECUTE_HANDLER) { reads = false; }
        vs->AddRef();
        velDmgVSPtr[velDmgVSN] = vs; velDmgVSReads[velDmgVSN] = reads; velDmgVSIdx[slot] = velDmgVSN + 1; velDmgVSN++;
    }
    velDmgLastVS = vs; velDmgLastReads = reads;
    return reads;
}

#if DLSSIV_DEV
DLSSIV_FGDATA static volatile LONG velDmgStat[3] = { 0, 0, 0 };   // movers drawn with a damage-reading shader; of them with the damage on; entries drawn through a variant
#endif

static void VelDmgNote(IDirect3DDevice9* dev, IDirect3DVertexShader9* vs, VelPend* e)
{
    e->dmgTex = nullptr;
    if (!vs || !VelDmgReads(vs)) return;
#if DLSSIV_DEV
    InterlockedIncrement(&velDmgStat[0]);
#endif
    BOOL on = FALSE;
    if (FAILED(dev->GetVertexShaderConstantB(8, &on, 1)) || !on) return;          // the game's switch: an undamaged vehicle draws the plain way
    float br[4] = {};
    if (FAILED(dev->GetVertexShaderConstantF(208, br, 1)) || !(br[0] > 1e-3f && br[0] < 1e4f)) return;   // a radius the game divides by
    IDirect3DBaseTexture9* t = nullptr;
    if (FAILED(dev->GetTexture(D3DVERTEXTEXTURESAMPLER0, &t)) || !t) return;      // Get takes a reference: the entry's own
    for (int k = 0; k < VEL_DMG_SAMP_N; k++)
    {
        DWORD s = 0;
        dev->GetSamplerState(D3DVERTEXTEXTURESAMPLER0, kVelDmgSamp[k], &s);
        e->dmgSamp[k] = s;
    }
    e->dmgTex = t;
    e->dmgRadius = br[0];
#if DLSSIV_DEV
    InterlockedIncrement(&velDmgStat[1]);
#endif
}

static IDirect3DVertexShader9* VelDmgBind(IDirect3DDevice9* dev, const VelPend* e, IDirect3DVertexShader9* plain)
{
    if (!velDmgVSTried)
    {
        velDmgVSTried = true;
        if (FAILED(dev->CreateVertexShader((const DWORD*)kVelocityDmgVS, &velDmgVS[0])))         velDmgVS[0] = nullptr;
        if (FAILED(dev->CreateVertexShader((const DWORD*)kVelocitySkinDmgVS, &velDmgVS[1])))     velDmgVS[1] = nullptr;
        if (FAILED(dev->CreateVertexShader((const DWORD*)kVelocitySkinPrevDmgVS, &velDmgVS[2]))) velDmgVS[2] = nullptr;
#if DLSSIV_DEV      // a development build's line only
        if (!velDmgVS[0] || !velDmgVS[1] || !velDmgVS[2])
            LogRecord("  velocity: a dented vehicle's shader could not be made: such a vehicle is redrawn as before");
#endif
    }
    IDirect3DVertexShader9* const v = plain == velVS ? velDmgVS[0] : plain == velSkinVS ? velDmgVS[1] : plain == velSkinPrevVS ? velDmgVS[2] : nullptr;
    if (!v || !e->dmgTex) return plain;
    if (!velDmgHeld)
    {
        velDmgHeld = true;
        velDmgOldTex = nullptr;
        dev->GetTexture(D3DVERTEXTEXTURESAMPLER1, &velDmgOldTex);
        for (int k = 0; k < VEL_DMG_SAMP_N; k++) dev->GetSamplerState(D3DVERTEXTEXTURESAMPLER1, kVelDmgSamp[k], &velDmgOldSamp[k]);
        velDmgBoundTex = nullptr; velDmgSampKnown = false; velDmgBoundRadius = -1.0f;
    }
    if (e->dmgTex != velDmgBoundTex) { origSetTexture(dev, D3DVERTEXTEXTURESAMPLER1, e->dmgTex); velDmgBoundTex = e->dmgTex; }
    for (int k = 0; k < VEL_DMG_SAMP_N; k++)
        if (!velDmgSampKnown || e->dmgSamp[k] != velDmgBoundSamp[k]) { dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, kVelDmgSamp[k], e->dmgSamp[k]); velDmgBoundSamp[k] = e->dmgSamp[k]; }
    velDmgSampKnown = true;
    if (e->dmgRadius != velDmgBoundRadius)
    {
        const float c[4] = { e->dmgRadius, 0.0f, 0.0f, 0.0f };
        origSetVsConstF(dev, 247, c, 1);
        velDmgBoundRadius = e->dmgRadius;
    }
#if DLSSIV_DEV
    InterlockedIncrement(&velDmgStat[2]);
#endif
    return v;
}

static void VelDmgEnd(IDirect3DDevice9* dev)
{
    if (!velDmgHeld) return;
    velDmgHeld = false;
    origSetTexture(dev, D3DVERTEXTEXTURESAMPLER1, velDmgOldTex);
    if (velDmgOldTex) { velDmgOldTex->Release(); velDmgOldTex = nullptr; }
    for (int k = 0; k < VEL_DMG_SAMP_N; k++) dev->SetSamplerState(D3DVERTEXTEXTURESAMPLER1, kVelDmgSamp[k], velDmgOldSamp[k]);
    velDmgBoundTex = nullptr; velDmgSampKnown = false;
}

#if DLSSIV_DEV
// The development log's line beside the tyres' (VelEndFrame's report, every 600 frames at log=2): said while any vehicle drew.
static void VelDmgReport(LONG nFrames, double f)
{
    LONG s[3]; for (int k = 0; k < 3; k++) s[k] = InterlockedExchange(&velDmgStat[k], 0);
    if (s[0])
        LogHealth("  velocity: moving vehicle draws with the game's damage shader, a frame over %ld frames: %.2f; dented (the switch on, a map bound) %.2f;"
                  " redrawn with the damage %.2f", nFrames, s[0] * f, s[1] * f, s[2] * f);
}
#endif
// ---- THE DENTED VEHICLE'S REDRAW END ----

// ---- THE DID-NOT-START NOTE'S CAUSE START (tools\test-warn-takeback cuts to the END line) ----
// Which of the note's three causes a reason belongs to, by its words: 1 the game is not on DXVK; 2 the helper's folder is
// missing or another build's; 3 the upscaler's library is missing. 0 for every other reason, which carries its own advice or
// needs the log. The words are IvtFail's and hkCreateDevice's; tools\test-warn-takeback prints each reason's cause.
static int IvtFatalCause(const char* why)
{
    if (!why) return 0;
    if (strstr(why, "not DXVK") || strstr(why, "not running on DXVK")) return 1;
    if (strstr(why, "no host exe") || strstr(why, "protocol version mismatch") || strstr(why, "from a different build")) return 2;
    if (strstr(why, "FidelityFX files") || strstr(why, "nvngx_dlss.dll")) return 3;
    return 0;
}
// ---- THE DID-NOT-START NOTE'S CAUSE END ----

// ---- THE CONSOLE GAMMA PROBE THAT COULD NOT DECIDE START (tools\test-warn-takeback cuts to the END line) ----
// With hdr_console_gamma=-1 and no proof from FusionFix's shader, the probe decides the HDR model's console gamma curve. This
// says why an undecided probe was refused: one line a reason, at most four a session, only until the first decision.
// why: 0 too dark (said at the fifth such probe), 1 no curve fits, 2 a curve is on but not which, 3 no clear winner.
// Called from HdrCgProbeAnalyse (hdr_encode.h) when a probe is read back.
static void HdrCgSayUndecided(int why, const double* e, int n)
{
    static unsigned said = 0;
    static int dark = 0;
    if (hdrCgDecided || why < 0 || why > 3 || (said & (1u << why))) return;
    if (why == 0)
    {
        if (++dark < 5) return;
        said |= 1u;
        LogRecord("  hdr: console gamma probe: undecided so far, the picture was too dark to judge on five probes; FusionFix's default stays until it can tell");
        return;
    }
    said |= 1u << why;
    LogRecord("  hdr: console gamma probe: undecided so far, %s (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d samples); FusionFix's default stays until it can tell",
        why == 1 ? "no curve fits this frame" : why == 2 ? "a curve is on but this frame cannot tell which" : "no clear winner", e[0], e[1], e[2], n);
}
// ---- THE CONSOLE GAMMA PROBE THAT COULD NOT DECIDE END ----

// ---- CLOUDFIXIV 1.0 IS NAMED START (tools\test-warn-takeback cuts to the END line) ----
// CloudFixIV 1.0's watchdog rewrites the device's EndScene slot in its first minute, storing what is there as its original;
// with this mod's hook above it the two hooks call each other until the stack overflows. CloudFixIV 1.1 leaves the slot alone.
// The mod cannot undo it, so it names the cause in the warnings file before the first frame. 1.0 is known by the word
// "re-asserting" in its image, in 1.0 and no later build. Asked once at CreateDevice; any other module costs one
// GetModuleHandle.
static bool CloudFix10Image(const BYTE* base, size_t size)
{
    static const char kMark[] = "re-asserting";
    const size_t n = sizeof(kMark) - 1;
    if (!base || size < n) return false;
    for (size_t i = 0; i + n <= size; i++)
        if (base[i] == 'r' && !memcmp(base + i, kMark, n)) return true;
    return false;
}
static void WarnCloudFix10()
{
    static bool done = false;
    if (done) return;
    done = true;
    const BYTE* const base = (const BYTE*)GetModuleHandleA("CloudFixIV.asi");
    if (!base || !IsReadable(base, 0x400)) return;
    const IMAGE_DOS_HEADER* const dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x300) return;
    const IMAGE_NT_HEADERS* const nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const size_t size = nt->OptionalHeader.SizeOfImage;
    if (size > 0x1000000 || !IsReadable(base, size) || !CloudFix10Image(base, size)) return;
    LogRecord("  CloudFixIV.asi is its 1.0: its watchdog takes the EndScene slot back from a hook above its own, which with this mod's hook is a call cycle that ends the game at a first frame; 1.1 does not");
    IvtWarn("CloudFixIV 1.0 is installed. It can crash the game at start together with DLSS-IV. Please update CloudFixIV to 1.1.");
}
// ---- CLOUDFIXIV 1.0 IS NAMED END ----

// ---- THE SDR RENAME IN EXCLUSIVE FULLSCREEN START (tools\test-warn-takeback cuts to the END line) ----
// With the game's vsync off in exclusive fullscreen, the SDR rename ran some players at half their refresh (NVIDIA's cap acting
// at half). The rename exists for G-Sync in a borderless window; in exclusive fullscreen G-Sync works without it. The engine
// name is fixed when DXVK makes its instance, before the device says how it is shown, so the mode is read from FusionFix's
// cfg: [MAIN] Windowed (PREF_WINDOWED, default 0 from 5.0.0 to 5.1.2), which replaces the game's -windowed. FusionFix writes
// only keys that differ from its defaults, so a missing key or cfg means exclusive fullscreen. The cfg is found as FusionFix
// finds it (LatReadLimiterCfg's search, repeated here since that one runs only with the latency step). Asked once; Alt+Enter
// later does not change the name.
static int IvtFfWindowedAtStart()
{
    static int answer = -2;
    if (answer != -2) return answer;
    answer = -1;
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char path[MAX_PATH], asi[MAX_PATH] = "";
    const HMODULE ff = GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");
    if (!(ff && GetModuleFileNameA(ff, asi, MAX_PATH))) asi[0] = 0;
    static const char* const at[3] = { "plugins\\GTAIV.EFLC.FusionFix.asi", "GTAIV.EFLC.FusionFix.asi", "scripts\\GTAIV.EFLC.FusionFix.asi" };
    for (int i = 0; i < 3 && !asi[0]; i++)
    {
        _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", dir, at[i]);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(asi, path);
    }
    if (!asi[0]) return answer;   // no FusionFix: unknown, the rename stays
    char ffDir[MAX_PATH], ffName[MAX_PATH] = "GTAIV.EFLC.FusionFix";
    strcpy_s(ffDir, asi);
    if (char* s = strrchr(ffDir, '\\')) { strcpy_s(ffName, s + 1); *(s + 1) = 0; if (char* e = strrchr(ffName, '.')) *e = 0; }
    else ffDir[0] = 0;
    char cands[5][MAX_PATH]; int nc = 0; char sh[MAX_PATH];
    if (ffDir[0]) _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", ffDir, ffName);
    else          _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%splugins\\GTAIV.EFLC.FusionFix.cfg", dir);
    _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", dir, ffName);
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, sh)))
    {
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\Rockstar Games\\GTA IV\\%s.cfg", sh, ffName);
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    }
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, sh)))
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    answer = 0;   // FusionFix's default, where no cfg holds the key
    for (int i = 0; i < nc; i++)
    {
        const DWORD a = GetFileAttributesA(cands[i]);
        if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_READONLY)) continue;
        answer = GetPrivateProfileIntA("MAIN", "Windowed", 0, cands[i]) != 0 ? 1 : 0;
        break;
    }
    return answer;
}
// ---- THE SDR RENAME IN EXCLUSIVE FULLSCREEN END ----

// ---- a switch that is off is said ----
// The cfg's echo prints responsiveness only when it is not 0, and its test cannot change without moving the release's hot
// data, so the line for 0 is written here, once, by the function that lists the cfg's unusable lines. threaded, unpin and
// cpu_place have their lines at 0 already.
static void CfgSayOff()
{
    static bool said = false;
    if (said) return;
    said = true;
    if (ivtCfg.responsiveness == 0) LogRecord("  cfg: responsiveness=0");
}

// ---- Intel XeSS texts ----
// At the handshake, from the helper's caps (transport/feed_ipc.h): PROBED, the helper asked libxess.dll; XESS, it can run;
// NOFILE, UNSUPPORTED and NORUNTIME say why not (at most one; with none, the log's "host:" lines say). Here and called in one
// line from IvtEnsureHost because the same lines inside it moved the release's hot data. Above the stale HDR warning's section,
// which tools/test-warn-takeback cuts and compiles alone. Under Proton the helper's Direct3D 12 is vkd3d-proton, where XeSS is
// not tested, so the refusal gives other advice. tools/test-xess-texts runs the lines between the START and END markers.
// XESS TEXTS START
// Why XeSS is not running, as the end of a sentence (after "and" in the refusal, after a colon in the warning).
static const char* IvtXessWhy(unsigned long long caps, bool wine)
{
    if (caps & FEED_CAP_XESS_NOFILE)
        return "Intel's " DLSSIV_HOST_DIR "\\libxess.dll is not there. It ships with this mod, so copy the " DLSSIV_HOST_DIR
               " folder over again";
    if (caps & FEED_CAP_XESS_UNSUPPORTED)
        return "Intel's library says this graphics card or driver cannot run XeSS";
    if (caps & FEED_CAP_XESS_NORUNTIME)
        return wine ? "libxess.dll would not load. It needs the Microsoft Visual C++ runtime 14.40 or later"
                    : "libxess.dll would not load. It needs the Microsoft Visual C++ runtime 14.40 or later, which Microsoft's"
                      " latest Visual C++ Redistributable for x64 installs";
    return "Intel's library could not start XeSS on this graphics card (the \"host:\" lines in DLSS-IV.log say why)";
}
// What to do instead, after the refusal's reason: nothing more for the missing file, whose reason already says it.
static const char* IvtXessFix(unsigned long long caps, bool wine)
{
    if (caps & FEED_CAP_XESS_NOFILE) return "";
    if (wine) return ". Under Proton the upscaler host runs on vkd3d-proton; if XeSS will not start there, upscaler=-1 runs FSR";
    return ". Set upscaler=-1 and the mod picks for this card";
}
// False when the session stops here (upscaler=2 and XeSS cannot run); otherwise true, with a warning when automatic fell to FSR
// on an Intel card (automatic tries XeSS only on vendor 0x8086). Not in transport-test mode (mode=1). No on-screen notice.
static bool IvtXessHandshake(unsigned long long caps)
{
    if (caps & FEED_CAP_XESS) ivtXessFell |= 1;   // the helper chose XeSS: a later refused build naming FSR is its fall (IvtXessFell)
    if (IVT_MODE == 1 || !(caps & FEED_CAP_XESS_PROBED) || (caps & FEED_CAP_XESS)) return true;
    if (ivtCfg.backend == 2)
    {
        char why[512];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "upscaler=2 asks for XeSS and %s%s", IvtXessWhy(caps, ivtWine), IvtXessFix(caps, ivtWine));
        IvtFail(why);
        return false;
    }
    if (ivtCfg.backend < 0 && (caps & FEED_CAP_FSR))
        IvtWarn("upscaler=-1 (automatic) is running FSR, not XeSS: %s", IvtXessWhy(caps, ivtWine));
    return true;
}
// Automatic fell from XeSS to FSR at a build: XeSS's context failed, the helper refused that build with an ack naming FSR and
// loads FSR for the next (host/xess_host.h, CreateXessContext). Warns once with the general reason; the helper's "host:" lines
// name the real one. Called from IvtBuild at every refused ack.
static void IvtXessFell(unsigned flags)
{
    if (ivtXessFell != 1 || ivtCfg.backend >= 0 || !(flags & FEED_ACK_BACKEND_FSR)) return;
    ivtXessFell = 3;
    IvtWarn("upscaler=-1 (automatic) is running FSR, not XeSS: %s", IvtXessWhy(0, ivtWine));
}
// The third refused build's stop (IvtBuild): which back end refused, NGX's missing mode, and with upscaler=2 on XeSS what to do
// instead (IvtXessFix's words again, so IvtXessFix keeps one caller; tools/test-xess-texts holds them equal).
static void IvtBuildRefusedWhy(char* why, size_t cap, unsigned flags)
{
    const bool xess = (flags & FEED_ACK_BACKEND_XESS) != 0;
    _snprintf_s(why, cap, _TRUNCATE,
        "three host builds were refused by the %s back end%s. The \"host:\" lines in"
        " DLSS-IV.log name the reason, which is normally a back end library this GPU refused or a shared texture"
        " the driver would not create%s",
        xess ? "XeSS" : (flags & FEED_ACK_BACKEND_FSR) ? "FSR" : "DLSS",
        (flags & FEED_ACK_SR_UNAVAILABLE) ? " because NGX could not report a DLSS mode for this output size" : "",
        !(xess && ivtCfg.backend == 2) ? "."
        : ivtWine ? ". Under Proton the upscaler host runs on vkd3d-proton; if XeSS will not start there, upscaler=-1 runs FSR"
                  : ". Set upscaler=-1 and the mod picks for this card");
}
// XESS TEXTS END

// ---- THE STALE "HDR IS NOT ON" WARNING (2026-10-11, the log list) ----
// A player who switches to Borderless after starting in exclusive fullscreen gets HDR at that Reset, but the warnings file
// still said "hdr=1 is set, but HDR is not on", which reads as the mod still seeing exclusive fullscreen. When HDR engages and
// such a warning is kept, a line says HDR came on later and the kept line is replaced by what was true of the session, both
// worded as history: the last-warned reason is a static inside HdrRewriteFormat, so a return to the same refusal is not warned
// again. Called from HdrRewriteFormat (hdr_encode.h) on the device call's thread; appends as IvtWarn does but is no warning.
static void IvtWarnHdrCameOn()
{
    static const char kOld[] = "hdr=1 is set, but HDR is not on";
    const LONG n = ivtWarnCount < 8 ? ivtWarnCount : 8;
    bool had = false;
    for (LONG i = 0; i < n; i++)
        if (!strncmp(ivtWarnKept[i], kOld, sizeof(kOld) - 1))
        {
            strcpy_s(ivtWarnKept[i], "hdr=1: HDR was off when the game started and switched on later");
            had = true;
        }
    if (!had) return;
    char path[MAX_PATH];
    FILE* f = nullptr;
    if (IvtWarnPath(path, false)) f = _fsopen(path, "a", _SH_DENYNO);
    if (!f && IvtWarnPath(path, true)) f = _fsopen(path, "a", _SH_DENYNO);
    if (!f) return;
    fprintf(f, "- Update: HDR switched on later in this session, so the HDR message above no longer applies.\n");
    fclose(f);
}

// ---- THE ON-SCREEN NOTICE (2026-10-11): everything of it but the names at the top of this file ----
#include "notice.h"
