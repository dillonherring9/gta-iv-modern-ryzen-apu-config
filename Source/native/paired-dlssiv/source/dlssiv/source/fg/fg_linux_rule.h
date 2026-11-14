// Frame generation under Wine and Proton, the pure rules: plain integers, so the device-thread harness (dt_fg_linux.h) and the
// platform bench (tools/test-fg-eligible/gen.py) run these lines with Wine's answers faked. fg_linux.h is the half that acts.
#pragma once
#include <stdint.h>

// The probe's value on all three fences. Under Wine with sync 1 the transport's frame numbers start above its own probe's value
// (kIvtProbeN, 4), so a pair's first signal (at least 5) is always above it.
#define FGX_WINE_PROBE_N 1ull
// Each side's wait for the other's signal, once a session; the upscaler's probe allows 1.5 s too.
#define FGX_WINE_PROBE_MS 1500u

// Under Wine: sync 1 (the game's own semaphores, the default there) and never the CPU bridge. sync 0 (the helper's D3D12 fences)
// is what Wine breaks silently across processes; sync 2 is the bridge forced.
static bool FgxWinePlatformOk(int syncMode, bool bridge) { return syncMode == 1 && !bridge; }

// Once a session. MADE: made, handed to the helper, test signal given, no set probed yet. LIVE: the probe saw both ways.
// FAILED: never undone; no later build asks the helper again.
enum { FGX_WINE_NONE = 0, FGX_WINE_MADE = 1, FGX_WINE_LIVE = 2, FGX_WINE_FAILED = 3 };
enum { FGX_WHY_OK = 0, FGX_WHY_BRIDGE = 1, FGX_WHY_MAKE = 2, FGX_WHY_HELPER = 3, FGX_WHY_GAME = 4, FGX_WHY_ECHO = 5, FGX_WHY_N = 6 };

// Before a 'g': -1 refuse silently (the verdict was said), a reason (refuse, say it once, it becomes the verdict), or 0 ask. The
// bridge is known only after the static verdict was latched, so it is asked here at every build.
static int FgxWineGate(int state, bool bridge)
{
    if (state == FGX_WINE_FAILED) return -1;
    if (bridge) return FGX_WHY_BRIDGE;
    return FGX_WHY_OK;
}
// The helper says FEED_FG_ST_FENCES when it could not open them or did not see the game's test signal in time.
static int FgxWineAckWhy(bool fencesStatus) { return fencesStatus ? FGX_WHY_HELPER : FGX_WHY_OK; }
// The ack must name the three handles the game sent, and fgPrep and fgOut must read at the probe's value or above. An unreadable
// value is 0, below it: Wine's silent fence reads 0.
static int FgxWineProbeWhy(bool echoOk, uint64_t prepAt, uint64_t outAt)
{
    if (!echoOk) return FGX_WHY_ECHO;
    if (prepAt < FGX_WINE_PROBE_N || outAt < FGX_WINE_PROBE_N) return FGX_WHY_GAME;
    return FGX_WHY_OK;
}
// A reason fails the session for good; LIVE only from MADE with no reason; nothing leaves FAILED.
static int FgxWineNext(int state, int why, bool probed)
{
    if (state == FGX_WINE_FAILED || why != FGX_WHY_OK) return FGX_WINE_FAILED;
    if (probed && state == FGX_WINE_MADE) return FGX_WINE_LIVE;
    return state;
}
// The one warnings line by reason (tools\check-we.py holds its words): frame generation off for the session, the upscaler running.
static const char* FgxWineWarnText(int why)
{
    switch (why)
    {
    case FGX_WHY_BRIDGE: return "frame generation: off for this session: under Wine or Proton the upscaler runs on its CPU bridge (the shared semaphores did not connect), and frame generation cannot. The upscaler keeps running";
    case FGX_WHY_MAKE:   return "frame generation: off for this session: under Wine or Proton the fences it shares with the helper could not be made. The upscaler keeps running";
    case FGX_WHY_HELPER: return "frame generation: off for this session: under Wine or Proton the helper did not see the game's test signal on the fences frame generation shares with it. The upscaler keeps running";
    case FGX_WHY_GAME:   return "frame generation: off for this session: under Wine or Proton the game did not see the helper's test signal on the fences frame generation shares with it. The upscaler keeps running";
    case FGX_WHY_ECHO:   return "frame generation: off for this session: under Wine or Proton the helper answered with other fences than the game made. The upscaler keeps running";
    default:             return nullptr;
    }
}
