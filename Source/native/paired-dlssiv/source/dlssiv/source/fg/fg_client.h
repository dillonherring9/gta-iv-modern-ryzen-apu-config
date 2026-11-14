// Frame generation, the game's cold side: the key, the static conditions, the helper's flag, the handshake, and a set's life
// around the build. Nothing per frame. Included at the end of DLSS-IV.cpp so nothing of it comes before the last hot static.
// Frame generation may be refused, lost or absent and the upscaler goes on. Only two things stop the transport: the helper not
// answering 'g' within the build's minute, and the pipe breaking.
#pragma once

// ---- the conditions
// FG PLATFORM START (tools/test-fg-eligible/gen.py cuts from this line to FG PLATFORM END and runs every case)
// Windows: the native fences. Wine and Proton: the game's own semaphores (sync 1), never the bridge. The bridge is known only
// after the first build, so the set's acquire asks again there.
static bool FgxPlatformOk(bool wine, int syncMode, bool bridge)
{
    if (wine) return FgxWinePlatformOk(syncMode, bridge);
    return syncMode == 0 && !bridge;
}
// The first that fails, or null. All are known at load, so they decide the helper's flag.
static const char* FgxStaticRefusal(int keyFg, bool platformOk, int threaded, int injectWorker, int responsiveness, int pacer)
{
    if (keyFg != 1)          return "fg is not 1";
    if (!platformOk)         return "Windows native path only in this version";   // a tool reads these words
    if (threaded != 1)       return "threaded is not 1";
    if (injectWorker != 2)   return "inject_worker is not 2";
    if (responsiveness != 4) return "responsiveness is not 4";
    if (pacer == 0)          return "pacer is 0";
    return nullptr;
}
// FG PLATFORM END

struct FgxClient
{
    FgxSet set;
    bool   laid;
    bool   inFlight;            // a sample's submit did not retire: the images may be in use
    UINT   acquired, refused;
    FgxWaits   waits;
    FgxWatchSt watch;
        // Here and not a static, so no static of the release's moves.
    FgxWineSt  wine;
};
struct FgxCold
{
    int        keyFg;
    int        keyBad;          // out-of-range value, said once the log exists; 0 none
    LONG       latched;
    LONG       argDropped;
    LONG       caps;
    LONG       live;            // cleared wherever the transport's own "built" is
    LONG       said;
    FgxClient* c;
    const char* staticWhy;      // latched at the helper's spawn: null, or why not
        // fg_min_fps: 0 the floor by display, else 30 to 240 on every display
    LONG       minFps;
};
DLSSIV_FGDATA static FgxCold fgx;
enum { FGX_SAID_KEY = 1, FGX_SAID_LIVE = 2, FGX_SAID_VERDICT = 4, FGX_SAID_SESSION = 8, FGX_SAID_ARG = 16, FGX_SAID_MEM = 32, FGX_SAID_FLIGHT = 64 };
static bool FgxSayOnce(LONG bit) { return (_InterlockedOr(&fgx.said, bit) & bit) == 0; }
// A second said-once word: fgx.said is full, and beside fgx no struct grows. A refused set says once per status (0 to 15).
DLSSIV_FGDATA static LONG fgxSaid2;
DLSSIV_FGDATA static LONG fgxMinFpsBad;
enum { FGX_SAID2_KEY = 1, FGX_SAID2_MIN = 2, FGX_SAID2_STATIC = 4, FGX_SAID2_SESSION = 8, FGX_SAID2_FLOOR = 16, FGX_SAID2_LONG = 32, FGX_SAID2_HOST = 64,
       FGX_SAID2_DT = 128, FGX_SAID2_SET = 256 };
static_assert(FGX_SAID2_SET > FGX_SAID2_HOST && (FGX_SAID2_SET << 15) > 0, "a refused set's said-once bit is FGX_SAID2_SET << status, status 0 to 15");
static bool FgxSayOnce2(LONG bit) { return (_InterlockedOr(&fgxSaid2, bit) & bit) == 0; }

static const char* FgxWhyNotStaticNow()
{
    return FgxStaticRefusal(fgx.keyFg, FgxPlatformOk(ivtWine, ivtSyncMode, ivtBridge), ivtCfg.threaded, ivtCfg.inject_worker, ivtCfg.responsiveness, ivtCfg.pacer);
}
// Latched at the helper's spawn: inject_worker is a live key, and a later edit is a session condition (FgxSessionRefusal).
static const char* FgxWhyNotStatic()
{
    return fgx.latched ? fgx.staticWhy : FgxWhyNotStaticNow();
}

// ---- the key
// fg: 0 or 1, read once at load: the helper is started with or without its flag before the game has a device.
static bool FgxParseKey(const char* k, const char* v)
{
        // fg_min_fps: the floor, 30 to 240, on every display; 0 the floor by display (FgCondMinX10). Read once at load. A whole number
    // out of range is said in the warnings file.
    if (_stricmp(k, "fg_min_fps") == 0)
    {
        int fv = 0;
        if (IvtWholeInt(v, &fv) == 0 && !fgx.latched)
        {
            if (fv == 0 || (fv >= 30 && fv <= 240)) { fgx.minFps = fv; fgxMinFpsBad = 0; }
            else fgxMinFpsBad = fv;                                // 0 is in range
        }
        return true;
    }
    if (_stricmp(k, "fg") != 0) return false;
    int iv = 0;
    if (IvtWholeInt(v, &iv) != 0) return true;
    if (fgx.latched)
    {
        if (iv != fgx.keyFg && iv != fgx.keyBad && FgxSayOnce(FGX_SAID_LIVE))
        {
            if (iv == 0 || iv == 1) LogRecord("  frame generation: fg=%d in the file is not applied: the key is read once at load (a relaunch applies it)", iv);
            else                    LogRecord("  frame generation: fg=%d in the file is out of range (0 or 1), and the key is read once at load: nothing changes", iv);
        }
        return true;
    }
    if (iv == 0 || iv == 1) { fgx.keyFg = iv; fgx.keyBad = 0; }
    else { fgx.keyFg = 0; fgx.keyBad = iv; }
    return true;
}


// ---- the helper's flag
// Only under the static conditions: a helper not asked loads nothing of frame generation. The first reader latches the key.
static bool FgxHelperArg()
{
    if (!fgx.latched) { fgx.staticWhy = FgxWhyNotStaticNow(); _InterlockedExchange(&fgx.latched, 1); }
    return fgx.staticWhy == nullptr;
}
// Frame generation is off for the session, with a line; it never costs the spawn.
static void FgxArgDropped()
{
    _InterlockedExchange(&fgx.argDropped, 1);
}

// ---- the handshake
// A session at fg=0 prints nothing here.
static_assert(FEED_CAP_FG == 1024ull && FEED_CAP_FG_WIRE1 == 512ull && FEED_CAP_FG_WIRE3 == 2048ull && FEED_FG_WIRE == 3,
              "the handshake's lines say \"caps 1024, wire 2\", \"caps 2048\" and \"wire 1\" in words: a new wire revision rewords them");
// Once, in one plain sentence with its remedy. The player's sentence is chosen by the first word of the latched verdict, whose
// literals a tool reads.
static void FgxWarnHandshake(unsigned long long caps, const char* why)
{
    if (fgx.keyBad && FgxSayOnce2(FGX_SAID2_KEY))
        IvtWarn("fg=%d in DLSS-IV.cfg is not a value it takes (0 or 1), so frame generation is off. Set fg=1 to turn it on", fgx.keyBad);
    if (fgxMinFpsBad && FgxSayOnce2(FGX_SAID2_MIN))
        IvtWarn("fg_min_fps=%ld in DLSS-IV.cfg is not a value it takes (0, or 30 to 240), so the default is used: 30 fps on a 60 Hz screen and 38 on every other",
                (long)fgxMinFpsBad);
    if (fgx.keyFg != 1 || !FgxSayOnce2(FGX_SAID2_STATIC)) return;
    if (why && !FgxPlatformOk(ivtWine, ivtSyncMode, ivtBridge))
        IvtWarn(ivtWine ? "frame generation: off for this session: under Wine or Proton it needs sync=-1 or sync=1. Take the sync line out of DLSS-IV.cfg"
                        : "frame generation: off for this session: on Windows it needs the sync setting at its default. Take the sync line out of DLSS-IV.cfg");
    else if (why && !strncmp(why, "threaded", 8))
        IvtWarn("frame generation: off for this session: it needs threaded=1, and DLSS-IV.cfg has threaded=%d", ivtCfg.threaded);
    else if (why && !strncmp(why, "responsiveness", 14))
        IvtWarn("frame generation: off for this session: it needs responsiveness=4, and DLSS-IV.cfg has responsiveness=%d", ivtCfg.responsiveness);
    else if (why && !strncmp(why, "inject_worker", 13))
        IvtWarn("frame generation: off for this session: it needs inject_worker at its default. Take the inject_worker line out of DLSS-IV.cfg");
    else if (why && !strncmp(why, "pacer", 5))
        IvtWarn("frame generation: off for this session: it needs the pacer. Take the pacer line out of DLSS-IV.cfg");
    else if (why)
        IvtWarn("frame generation: off for this session: it could not start; please send me DLSS-IV.log (%s)", why);
    else if (fgx.argDropped)
        IvtWarn("frame generation: off for this session: it could not start; please send me DLSS-IV.log (its flag did not fit on the helper's command line)");
    else if (!(caps & FEED_CAP_FG) && !((caps & FEED_CAP_FG_PROBED) && (caps & FEED_CAP_FG_NOLIB)))
        IvtWarn("frame generation: off for this session: it could not start on this PC; please send me DLSS-IV.log (the helper offered no frame generation)");
}
static void FgxOnCaps(unsigned long long caps)
{
        // 2048 and 4096 are kept only with 1024. A wire 2 host offers 1024 alone and is spoken to at wire 2.
    _InterlockedExchange(&fgx.caps, (LONG)(caps & (FEED_CAP_FG_PROBED | FEED_CAP_FG | ((caps & FEED_CAP_FG) ? FEED_CAP_FG_WIRE3 | FEED_CAP_FG_TWIN : 0ull))));
    if (fgx.keyBad && FgxSayOnce(FGX_SAID_KEY)) LogRecord("  frame generation: fg=%d is out of range (0 or 1): 0 stands", fgx.keyBad);
    FgxWarnHandshake(caps, FgxWhyNotStatic());
    if (fgx.keyFg != 1 || !FgxSayOnce(FGX_SAID_VERDICT)) return;
    const char* why = FgxWhyNotStatic();
        // a tool reads the Windows line word for word
    if (why && !FgxPlatformOk(ivtWine, ivtSyncMode, ivtBridge)) LogRecord(ivtWine ? "  frame generation: off for this session: under Wine or Proton it runs only at sync=-1 or 1, on the game's own semaphores"
                                                                                    : "  frame generation: Windows native path only in this version");
    else if (why)             LogRecord("  frame generation: off for this session: %s", why);
    else if (fgx.argDropped)  LogRecord("  frame generation: off for this session: its flag did not fit on the host's command line and was left out");
        // An older host offers 512 and not 1024: its 'Q' is 73 bytes where this build writes 81, so nothing of frame generation is sent.
    else if (!(caps & FEED_CAP_FG) && (caps & FEED_CAP_FG_WIRE1))
        LogRecord("  frame generation: off for this session: the host speaks frame generation's wire 1, this build wire %d", (int)FEED_FG_WIRE);
    else
    {
        LogRecord("  transport: host offers frame generation: %s",
                  (caps & FEED_CAP_FG) ? "available (caps 1024, wire 2)"
                  : (caps & FEED_CAP_FG_PROBED) ? "NOT available (caps 256 without 1024: the host asked "
                                                  DLSSIV_HOST_DIR "\\dlssiv-fg.dll,"
                                                  " and its own lines say why)"
                                                : "not asked (neither caps bit: the host is not this build's, or was started without its flag)");
                // The one line a log=0 player reads.
        if ((caps & FEED_CAP_FG_PROBED) && !(caps & FEED_CAP_FG) && (caps & FEED_CAP_FG_NOLIB))
            IvtWarn("frame generation: off for this session: " DLSSIV_HOST_DIR "\\dlssiv-fg.dll is missing, or it is not the file this version of DLSS-IV"
                    " was built with. It ships with this mod, so copy the " DLSSIV_HOST_DIR " folder over again. The upscaler is not affected");
                // The line above stays word for word (a tool reads it).
        if (caps & FEED_CAP_FG)
            LogRecord("  transport: frame generation's wire 3 %s", (caps & FEED_CAP_FG_WIRE3)
                      ? "is offered as well (caps 2048): this session asks its sets at wire 3"
                      : "is NOT offered (no caps 2048: a host of wire 2): this session asks its sets at wire 2, one generated frame a pair");
    }
}

// Must the frame-end marker exist under a limiter too? Only where the helper offers frame generation: a session whose helper
// offers nothing pays no submission lock and empty submit at every present.
static bool FgxMarkerWanted()
{
    return FgxWhyNotStatic() == nullptr && !fgx.argDropped && (fgx.caps & FEED_CAP_FG) != 0;
}

// ---- the set's life
struct FgxB
{
    static bool PipeWrite(const void* p, DWORD n) { return IvtPipeWrite(p, n); }
    static bool PipeReadWithin(void* p, DWORD n, DWORD ms, bool* late) { return IvtPipeReadWithin(p, n, ms, late); }
    static FeedVk* Vk() { return &ivtVk; }
    static VkExternalMemoryHandleTypeFlagBits HandleType() { return ivtImportType ? ivtImportType : VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT; }
    static bool Narrow() { return !ivtWine && HandleType() == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT; }
    static bool WineFences() { return ivtWine; }
    static HANDLE Host() { return ivtHost; }
    static bool Bridge() { return ivtBridge; }
    static void Warn(const char* s) { IvtWarn("%s", s); }
    static FgxWineSt* WineState() { return &fgx.c->wine; }
    static VkResult Signal(VkSemaphore s, uint64_t v)
    {
        if (!ivtSignalSemaphore || !ivtDevice) return VK_ERROR_FEATURE_NOT_PRESENT;
        VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
        si.semaphore = s; si.value = v;
        return ivtSignalSemaphore(ivtDevice, &si);
    }
        // Under DXVK's submission lock, as every raw submit of the mod's.
    static VkResult QueueSubmit(const VkSubmitInfo* si)
    {
        DT_DIRECT();
        ivtIDev->LockSubmissionQueue();
        const VkResult r = ivtQueueSubmit(ivtQueue, 1, si, VK_NULL_HANDLE);
        ivtIDev->ReleaseSubmissionQueue();
        return r;
    }
    static void Line(const char* fmt, ...)
    {
        char b[512]; va_list a; va_start(a, fmt); _vsnprintf_s(b, sizeof(b), _TRUNCATE, fmt, a); va_end(a);
        LogRecord("  %s", b);
    }
        // Only the mod going down ends a held present at once, not the transport's stop: IvtFail sets ivtFailed before it releases
    // the fences, so a hold ending at that flag would queue a second present behind a wait that still stands. A held present
    // waits the stop out by the fences' own values.
    static const volatile bool* GateDown() { return &dead; }
};

#include "fg_machine.h"
#include "fg_product.h"

// A store, from any thread; the images go at the next build's head.
static void FgxStandDown()
{
    _InterlockedExchange(&fgx.live, 0);
}

// The game's imports go before the main set's release; the helper frees its side in the build message that follows.
static void FgxBeforeRelease()
{
    _InterlockedExchange(&fgx.live, 0);
        // No pair stays open across the build message, and the queue's copies have retired, before the images go.
    const bool retired = FgpBeforeRelease();
    if (!fgx.c || !fgx.c->set.have) return;
    IvtSharedGuard sharedGuard;
    if (!retired) fgx.c->inFlight = true;
    if (fgx.c->inFlight)
    {
                // A submit naming these images did not retire: they are forgotten, not destroyed (the memory stays until the game ends).
        FgxSet& s = fgx.c->set;
        for (int k = 0; k < FEED_FG_PLACES; k++) { s.img[k] = VK_NULL_HANDLE; s.mem[k] = VK_NULL_HANDLE; s.all[k] = VK_NULL_HANDLE; if (s.nt[k]) { CloseHandle(s.nt[k]); s.nt[k] = nullptr; } }
        s.nAll = 0; s.genCounts = 0;
        s.have = false; fgx.c->inFlight = false; fgx.c->laid = false;
        if (FgxSayOnce(FGX_SAID_FLIGHT)) LogRecord("  frame generation: a set whose sample did not retire is left to the device, not destroyed (its images stay until the game ends)");
        return;
    }
    FgxDrop<FgxB>(&fgx.c->set);
    fgx.c->laid = false;
}

// ---- the queued wait's release, the watch
// THE RELEASE OF THE GAME'S QUEUED WAIT ON THE HELPER'S PREPARE. Any thread; nothing is waited for first.
static void FgxReleasePrepWait()
{
    FgxClient* c = fgx.c;
    if (!c || !c->set.semPrep || !c->waits.prepTop) return;
    bool did = false;
    const VkResult r = FgxPrepRelease<FgxB>(&c->set, &c->waits, &did);
    if (did) LogHealth("  frame generation: released the queued GPU wait on the host's prepare, signalled its fence to %lu (%s)",
                       (unsigned long)(ULONG)c->waits.prepTop + 1, r == VK_SUCCESS ? "ok" : "FAILED");
}
// Twice a second. True: the helper is to be condemned for a prepare that has stood still for 4 s.
static bool FgxWatchLook()
{
    FgxClient* c = fgx.c;
    if (!c || !c->set.semPrep) return false;
    const uint64_t lastQ = (uint64_t)(ULONG)c->waits.lastQ;
    if (!lastQ) return false;
    ULONGLONG now = 0;
    QueryUnbiasedInterruptTime(&now);
    return FgxWatchStep(&c->watch, lastQ, (uint64_t)(ULONG)c->waits.waitLow, FeedVkTimelineValue(&ivtVk, c->set.semPrep), FeedVkTimelineValue(&ivtVk, ivtSemIn), now);
}
// The helper is ended first, because the kernel will not move a fence shared with a frozen process; then both waits are
// released; then the flag the render thread turns into the stop.
static void FgxWatchFire(unsigned long long want)
{
    FgxClient* c = fgx.c;
    if (!c) return;
    const uint64_t lastQ = (uint64_t)(ULONG)c->waits.lastQ;
    const uint64_t prep = FeedVkTimelineValue(&ivtVk, c->set.semPrep), mine = FeedVkTimelineValue(&ivtVk, ivtSemIn);
    bool ended = false;
    if (ivtWatchHost)
    {
        ended = TerminateProcess(ivtWatchHost, 0) != 0;
        WaitForSingleObject(ivtWatchHost, 2000);
    }
    bool did = false;
    const VkResult r = IvtSignalOutTo(want + 1, &did);
    FgxReleasePrepWait();
    _InterlockedCompareExchange(&ivtWatchFired, 1, 0);
    LogRecord("  transport: THE HOST HAS NOT FINISHED FRAME GENERATION'S PREPARE FOR 4 s (the prepare of pair %llu asked, its fence at %llu,"
              " the input of %llu retired here): ended %s, the GPU waits released (the frame's %s; the prepare's fence now %llu); the transport stops at the next frame",
              (unsigned long long)lastQ, (unsigned long long)prep, (unsigned long long)mine, ended ? "ok" : "NOT (TerminateProcess failed, or no handle)",
              !did ? "already released" : r == VK_SUCCESS ? "ok" : "FAILED", (unsigned long long)FeedVkTimelineValue(&ivtVk, c->set.semPrep));
}

static const char* FgxStatusName(int st)
{
    switch (st)
    {
    case FEED_FG_ST_PLATFORM:    return "not on this sync path";
    case FEED_FG_ST_NO_SET:      return "no shared set to prepare from";
    case FEED_FG_ST_NO_PROVIDER: return "no provider";
    case FEED_FG_ST_RAISED:      return "AMD's library raised earlier";
    case FEED_FG_ST_FORMAT:      return "a presented format it does not know";
    case FEED_FG_ST_SIZE:        return "a size out of range or not the set's";
    case FEED_FG_ST_VRAM:        return "the video memory rule";
    case FEED_FG_ST_BUSY:        return "its queue did not drain";
    case FEED_FG_ST_CREATE:      return "AMD's create failed";
    case FEED_FG_ST_WARMUP:      return "the warm-up failed or was late";
    case FEED_FG_ST_RUNTIME:     return "an Agility runtime beside the host";
    case FEED_FG_ST_OBJECTS:     return "a fence, an image or its command ring could not be made";
    case FEED_FG_ST_FENCES:      return "under Wine its shared fences did not connect, off for the session";
    default:                     return "a status this build does not know";
    }
}


// Known once the device exists: the first that fails, or null. inject_worker is a live key, so it is asked here again.
static const char* FgxSessionRefusal()
{
    if (ivtCfg.inject_worker != 2)     return "inject_worker is not 2 any more (a live edit; frame generation's sets come back when it is 2 again)";
    if (!dtInstalled || !dtScPresentO) return "the device thread did not install";
    if (!latOn || !latPacerOn)         return "the latency instrument or its pacer is off";
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char v[64] = ""; bool vFile = false, vEnv = false;
    if (DtDxvkValueAt(dir, "dxvk.latencySleep", v, &vFile, &vEnv) && !_stricmp(v, "true")) return "dxvk.latencySleep is True";
    return nullptr;
}
// Once a session; the sentence chosen by the verdict's first word.
static void FgxWarnSession(const char* why)
{
    if (!why || !FgxSayOnce2(FGX_SAID2_SESSION)) return;
    if (!strncmp(why, "inject_worker", 13))
        IvtWarn("frame generation: off: inject_worker in DLSS-IV.cfg was changed while the game ran. Take the inject_worker line out of DLSS-IV.cfg");
    else if (!strncmp(why, "dxvk.latencySleep", 17))
        IvtWarn("frame generation: off for this session: it does not run with DXVK's latency sleep. Take dxvk.latencySleep = True out of dxvk.conf, or out of DXVK_CONFIG");
    else
        IvtWarn("frame generation: off for this session: it could not start; please send me DLSS-IV.log (%s)", why);
}

// Never asked: their only user, the true fill, is gone. GEN2 is still numbered apart from SAVE and HOLD.
static_assert(FG_IMG_SAVE != FEED_FG_GEN2 && FG_IMG_HOLD != FEED_FG_GEN2 && FG_IMG_GEN2 == FEED_FG_GEN2,
              "the machine's GEN2 is the set's fourth place, and SAVE and HOLD are the mod's own (feed_ipc.h, fg_machine.h)");

// False only when the transport was stopped; otherwise the build goes on, with a set or without one and a line.
static bool FgxAfterBuild(unsigned w, unsigned h, int bbVkFormat, unsigned rw, unsigned rh)
{
    if (FgxWhyNotStatic() || fgx.argDropped || !(fgx.caps & FEED_CAP_FG)) return true;
    if (const char* why = FgxSessionRefusal())
    {
        if (FgxSayOnce(FGX_SAID_SESSION)) { LogRecord("  frame generation: off for this session: %s", why); FgxWarnSession(why); }
        return true;
    }
        // A session a row refuses makes no exchange, so the helper holds no context for it.
    FGA(if (FgpNoSetNow(bbVkFormat)) return true;)
    if (!fgx.c)
    {
        fgx.c = (FgxClient*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgxClient));
        if (!fgx.c) { if (FgxSayOnce(FGX_SAID_MEM)) LogRecord("  frame generation: no memory for its state: off"); return true; }
    }
    FgxClient& c = *fgx.c;
        // Its own table, not IvtColorFormatFor: a second caller would take that inlined function out of line.
    const DXGI_FORMAT pf = bbVkFormat == VK_FORMAT_B8G8R8A8_UNORM || bbVkFormat == VK_FORMAT_B8G8R8A8_SRGB ? DXGI_FORMAT_B8G8R8A8_UNORM
                         : bbVkFormat == VK_FORMAT_R8G8B8A8_UNORM || bbVkFormat == VK_FORMAT_R8G8B8A8_SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM
                         : bbVkFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ? DXGI_FORMAT_R10G10B10A2_UNORM
                         : bbVkFormat == VK_FORMAT_R16G16B16A16_SFLOAT ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_UNKNOWN;
    if (pf == DXGI_FORMAT_UNKNOWN)
    {
        LogRecord("  frame generation: the back buffer's format (VkFormat %d) has no shared-texture equivalent: no set for this build", bbVkFormat);
        return true;
    }
    LONGLONG t0 = 0, t1 = 0; QueryPerformanceCounter((LARGE_INTEGER*)&t0);
    int x;
    {
        IvtWatch wFg("frame generation's build exchange", true);
        IvtSharedGuard sharedGuard;
                // No HUD-less image unless the HUD-less pass wants it: AMD given an empty HUD-less colour makes the same generated image.
        // Never the fourth image or the second input set: nothing reads either.
        x = FgxAcquire<FgxB>(&c.set, w, h, pf, rw, rh, 0, FEED_FG_BUILD_DEADLINE_MS, (FGA(FgHudSetWanted() ? 0u :) FEED_FG_BUILD_NO_HUDLESS)
                             | ((fgx.caps & FEED_CAP_FG_WIRE3) ? FEED_FG_BUILD_WIRE3 : 0u));
    }
    QueryPerformanceCounter((LARGE_INTEGER*)&t1);
    c.laid = false;
    if (x == FGX_LATE)
    {
        IvtFail("the host did not answer frame generation's build within 60 s; its own lines in this log (\"host:\") say where it stopped");
        return false;
    }
    if (x == FGX_PIPE) { IvtFail("frame generation's build exchange failed"); return false; }
    if (x != FGX_CREATED)
    {
        c.refused++;
        FGA(if (x == FGX_REFUSED) FgpSetRefused(c.set.status);)
                // a set the game refused under Wine says why
        if (x == FGX_REFUSED && FgxWineLocalWhy(c.wine)) LogRecord("  frame generation: the game refused this build's set (%s): none this session", FgxWineLocalWhy(c.wine));
        else if (x == FGX_REFUSED) LogRecord("  frame generation: the host refused this build's set (status %d, %s): none until the next build", c.set.status, FgxStatusName(c.set.status));
        else if (c.set.status == FEED_FG_ST_RELEASED) LogRecord("  frame generation: this build's set could not be imported and was given back: none until the next build");
        else LogRecord("  frame generation: this build's set could not be imported; the host keeps its side until the next build (status %d, %s): none until then", c.set.status, FgxStatusName(c.set.status));
        return true;
    }
    c.acquired++;
    _InterlockedExchange(&fgx.live, 1);
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
    LogRecord("  frame generation: set %u acquired: %ux%u dxgi %d, guides %ux%u; the host's context %llu MiB, provider 0x%llX, its warm-up %d, its create and warm-up %.1f ms, the whole exchange %.1f ms",
              c.acquired, w, h, (int)pf, rw, rh, (unsigned long long)(c.set.ctxBytes >> 20), (unsigned long long)c.set.provider, (int)c.set.warmRc,
              c.set.warmUs / 1000.0, (double)(t1 - t0) * 1000.0 / (double)qf.QuadPart);
        // Installed once, where a set is first held. Never fails the build.
    FgpAfterAcquire(c, w, h, bbVkFormat);
    return true;
}

