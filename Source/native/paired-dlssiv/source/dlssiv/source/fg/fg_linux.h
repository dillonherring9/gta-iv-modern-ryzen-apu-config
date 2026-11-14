// Frame generation under Wine and Proton: the game's own fences and their probe.
// On Windows the helper makes the three fences shared and the game imports them. Valve's Wine breaks that across two processes
// without an error: the import succeeds, the values never connect, and a wait never ends. So, as the upscaler does under Wine,
// the game makes its own timelines exported as OPAQUE_WIN32, the helper opens them, and a probe proves both ways first. It fails
// closed: on any doubt frame generation stands by for the session with one warnings line, and no wait is queued on an unproved
// fence. Every call used is one the upscaler already makes under Wine. Windows never takes this path; all of it is cold.
// The decisions are fg_linux_rule.h's pure functions.
#pragma once
#include "fg_linux_rule.h"
#include <type_traits>
static_assert(FGX_WINE_PROBE_N == FEED_FG_WINE_PROBE_N && FGX_WINE_PROBE_MS == FEED_FG_WINE_PROBE_MS,
              "the probe's value and bound are the wire's (feed_fg.h): the game and the helper must look for the same signal");

// Not a static of its own: in the game it is the last field of the client's heap state (FgxClient::wine), so no static of the
// release's moves. The semaphores are made once and never destroyed (a set may hold them, a batch may wait on them).
struct FgxWineSt
{
    VkSemaphore sem[3];                     // in, prep, out
    uint64_t    inHelper[3];                // as duplicated into the helper: what the 'g' sends and the ack echoes
    LONG        state;
    LONG        why;                        // the session's reason, once it failed
    LONG        local;                      // the last acquire was refused here, with no exchange
};
// Null where the last acquire reached the helper.
static const char* FgxWineLocalWhy(const FgxWineSt& w)
{
    if (!w.local) return nullptr;
    switch (w.why)
    {
    case FGX_WHY_BRIDGE: return "under Wine or Proton the upscaler runs on its CPU bridge, which frame generation cannot use";
    case FGX_WHY_MAKE:   return "under Wine or Proton the fences it shares with the helper could not be made";
    case FGX_WHY_HELPER: return "under Wine or Proton the helper did not see the game's test signal earlier this session";
    case FGX_WHY_GAME:   return "under Wine or Proton the game did not see the helper's test signal earlier this session";
    case FGX_WHY_ECHO:   return "under Wine or Proton the helper answered with other fences than the game made, earlier this session";
    default:             return "under Wine or Proton its shared fences failed earlier this session";
    }
}

// WineFences() true: the game's own fences; Host(): the helper's process; Bridge(): the CPU bridge; Warn(text); WineState().
// A binding without WineFences is a Windows binding and none of this runs.
template <class B, class = void> struct FgxWineOf
{
    static bool On() { return false; }
    static HANDLE Host() { return nullptr; }
    static bool Bridge() { return false; }
    static void Warn(const char*) {}
    static FgxWineSt* St() { return nullptr; }   // never asked: On() is false
};
template <class B> struct FgxWineOf<B, std::void_t<decltype(&B::WineFences)>>
{
    static bool On() { return B::WineFences(); }
    static HANDLE Host() { return B::Host(); }
    static bool Bridge() { return B::Bridge(); }
    static void Warn(const char* s) { B::Warn(s); }
    static FgxWineSt* St() { return B::WineState(); }
};

// Failed for good, the reason said once.
template <class B> static void FgxWineFail(int why)
{
    FgxWineSt& fgxWine = *FgxWineOf<B>::St();
    if (fgxWine.state == FGX_WINE_FAILED) return;
    fgxWine.state = FgxWineNext(fgxWine.state, why, false);
    fgxWine.why = why;
    const char* t = FgxWineWarnText(why);
    if (t) { B::Line("%s", t); FgxWineOf<B>::Warn(t); }
}

// Once a session: the three timelines made, duplicated into the helper, and the test signal given on the in-fence.
// 0: the 'g' goes with *fx; FGX_REFUSED: no exchange (status FEED_FG_ST_FENCES).
template <class B> static int FgxWineBefore(FgxSet* s, FeedFgFencesIvt* fx)
{
    FgxWineSt& fgxWine = *FgxWineOf<B>::St();
    fgxWine.local = 0;
    const int g = FgxWineGate(fgxWine.state, FgxWineOf<B>::Bridge());
    if (g > 0) FgxWineFail<B>(g);
    if (g) { s->status = FEED_FG_ST_FENCES; fgxWine.local = 1; return FGX_REFUSED; }
    if (fgxWine.state == FGX_WINE_NONE)
    {
        FeedVk* vk = B::Vk();
        const HANDLE host = FgxWineOf<B>::Host();
        VkSemaphore sem[3] = {}; uint64_t remote[3] = {}; bool ok = host != nullptr;
        for (int i = 0; i < 3 && ok; i++)
        {
            HANDLE local = nullptr, inHost = nullptr;
            ok = FeedVkCreateTimeline(vk, true, &sem[i], &local) == 1 && local
              && DuplicateHandle(GetCurrentProcess(), local, host, &inHost, 0, FALSE, DUPLICATE_SAME_ACCESS) && inHost;
            if (local) CloseHandle(local);
            remote[i] = (uint64_t)(uintptr_t)inHost;
        }
                // The test signal, from the CPU, by the device's own vkSignalSemaphore: a second caller of the binding's Signal would take it out
        // of line.
        PFN_vkSignalSemaphore sig = ok && vk->GetDeviceProcAddr ? (PFN_vkSignalSemaphore)vk->GetDeviceProcAddr(vk->dev, "vkSignalSemaphore") : nullptr;
        if (ok && !sig && vk->GetDeviceProcAddr) sig = (PFN_vkSignalSemaphore)vk->GetDeviceProcAddr(vk->dev, "vkSignalSemaphoreKHR");
        VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO }; si.semaphore = sem[0]; si.value = FGX_WINE_PROBE_N;
        ok = ok && sig && sig(vk->dev, &si) == VK_SUCCESS;
        if (!ok)
        {   // nothing half made is kept: the helper's handles closed, the semaphores destroyed (no batch has seen them)
            for (int i = 0; i < 3; i++)
            {
                if (remote[i] && host) DuplicateHandle(host, (HANDLE)(uintptr_t)remote[i], nullptr, nullptr, 0, FALSE, DUPLICATE_CLOSE_SOURCE);
                if (sem[i]) vk->DestroySemaphore(vk->dev, sem[i], nullptr);
            }
            FgxWineFail<B>(FGX_WHY_MAKE);
            s->status = FEED_FG_ST_FENCES; fgxWine.local = 1;
            return FGX_REFUSED;
        }
        for (int i = 0; i < 3; i++) { fgxWine.sem[i] = sem[i]; fgxWine.inHelper[i] = remote[i]; }
        fgxWine.state = FGX_WINE_MADE;
    }
    fx->in = fgxWine.inHelper[0]; fx->prep = fgxWine.inHelper[1]; fx->out = fgxWine.inHelper[2];
    return 0;
}

template <class B> static void FgxWineOnStatus(int status)
{
    const int why = FgxWineAckWhy(status == FEED_FG_ST_FENCES);
    if (why) FgxWineFail<B>(why);
}

// The game's half of the probe, on the first set the helper created with them: the ack must echo the three handles and the
// helper's test signal must be on fgPrep and fgOut. Otherwise the set holds none of them (no wait can be queued), the caller gives
// the set back, and the session is done.
template <class B> static bool FgxWineTake(FgxSet* s, const FeedFgAckIvt& a)
{
    FgxWineSt& fgxWine = *FgxWineOf<B>::St();
    FeedVk* vk = B::Vk();
    const bool echo = a.fence_in == fgxWine.inHelper[0] && a.fence_prep == fgxWine.inHelper[1] && a.fence_out == fgxWine.inHelper[2];
        // Bounded: the helper signalled both before it answered, so where they connect the waits return at once. fgOut is waited for
    // only once fgPrep was seen.
    if (echo && FeedVkWaitTimeline(vk, fgxWine.sem[1], FGX_WINE_PROBE_N, FGX_WINE_PROBE_MS)) (void)FeedVkWaitTimeline(vk, fgxWine.sem[2], FGX_WINE_PROBE_N, FGX_WINE_PROBE_MS);
    const uint64_t prep = echo ? FeedVkTimelineValue(vk, fgxWine.sem[1]) : 0, out = echo ? FeedVkTimelineValue(vk, fgxWine.sem[2]) : 0;
    const int why = FgxWineProbeWhy(echo, prep, out);
    if (why) { FgxWineFail<B>(why); return false; }
    fgxWine.state = FgxWineNext(fgxWine.state, FGX_WHY_OK, true);
    s->semIn = fgxWine.sem[0]; s->semPrep = fgxWine.sem[1]; s->semOut = fgxWine.sem[2];
    s->fence[0] = a.fence_in; s->fence[1] = a.fence_prep; s->fence[2] = a.fence_out;
        // no log line of its own: the caller's "set 1 acquired" line says the probe passed
    return true;
}
