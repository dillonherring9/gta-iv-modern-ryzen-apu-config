// present_gate.h: the gate before DXVK's Present. With vsync on, NVIDIA's driver never completes two presents queued behind a
// silent helper, and DXVK waits for that in its device lock forever. So before each Present, on the CPU, wait until the helper
// reaches what the batches before the previous present wait for: at most one present stands behind a wait. Not on a failed
// read, past IVT_GATE_MS, or with vsync off. Not ended by ivtFailed, which precedes the fence's release.
// The core touches no transport global, so tools/test-present-gate proves it and frame generation reuses it with its prepare
// fence. IVT_GATE_CORE_ONLY compiles it alone (windows.h and feed_vk.h first).
#pragma once

// Between presents: the needs (presenting thread only) and the counts (read by log lines).
struct IvtGate
{
    volatile LONG prevOut, prevPrep;        // what the queued batches waited for at the last present:
                                            // the frame's out-fence value, and the prepare fence's top (0 in a release)
    volatile LONG held, gaveUp, unread;     // presents held; holds that ended at the cap; looks at which a
                                            // counter's read (or its wait) FAILED: nothing was held on it
    volatile LONG holding;                  // 1 while a present is held; written at a hold's start and
                                            // end; read by DtStuckSay on another thread to name the cause
    volatile LONGLONG sumT, maxT;           // every hold together, and the longest, in QueryPerformanceCounter's ticks
};
enum { IVT_GATE_MS = 8000, IVT_GATE_SLICE_MS = 5 };

// One counter with its result: VK_SUCCESS means *v is the value, 0 included; anything else is a failed read. (FeedVkTimelineValue
// cannot tell the two apart.)
static VkResult IvtGateCounterR(FeedVk* vk, VkSemaphore sem, uint64_t* v)
{
    *v = 0;
    if (!FeedVkHasTimelineQueries(vk) || sem == VK_NULL_HANDLE) return VK_ERROR_UNKNOWN;
    const VkResult r = vk->GetSemaphoreCounterValue(vk->dev, sem, v);
    if (r != VK_SUCCESS) *v = 0;
    return r;
}

// The term. A need of 0 never holds; a read counter short of its need holds (out-fence first); a failed read holds nothing.
// 0 pass; 1 hold on the out-fence; 2 hold on the prepare fence; 3 unread.
static int IvtGateStep(uint64_t needOut, uint64_t needPrep, uint64_t out, bool outRead, uint64_t prep, bool prepRead)
{
    if (needOut && outRead && out < needOut) return 1;
    if (needPrep && prepRead && prep < needPrep) return 2;
    return ((needOut && !outRead) || (needPrep && !prepRead)) ? 3 : 0;
}

// The core, just before DXVK's Present, no lock held. g's needs are what stood queued at the previous present; sentOut and
// prepTop (0: none) become the next ones. `down` ends a hold. Holds in slices until passed, `down`, an error, or IVT_GATE_MS.
// Returns the milliseconds held.
static DWORD IvtGateCore(IvtGate* g, FeedVk* vk, VkSemaphore semOut, uint64_t sentOut, VkSemaphore semPrep, uint64_t prepTop, const volatile bool* down)
{
    const uint64_t needOut = (uint64_t)(ULONG)g->prevOut, needPrep = (uint64_t)(ULONG)g->prevPrep;
    LARGE_INTEGER qf, q0, q1;
    qf.QuadPart = 0; q0.QuadPart = 0; q1.QuadPart = 0;
    bool holding = false, waited = false, cap = false;
    int st;
    for (;;)
    {
                // The only place the counters are read.
        uint64_t out = 0, prep = 0;
        const bool outRead = needOut && IvtGateCounterR(vk, semOut, &out) == VK_SUCCESS;
        const bool prepRead = needPrep && IvtGateCounterR(vk, semPrep, &prep) == VK_SUCCESS;
        st = IvtGateStep(needOut, needPrep, out, outRead, prep, prepRead);
        if ((st != 1 && st != 2) || (down && *down)) break;
        if (!holding) { QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0); holding = true; g->holding = 1; }   // the clock, only once a hold begins
        else { QueryPerformanceCounter(&q1); if (q1.QuadPart - q0.QuadPart >= qf.QuadPart * IVT_GATE_MS / 1000) { cap = true; break; } }
        const VkResult r = st == 1 ? FeedVkWaitTimelineR(vk, semOut, needOut, IVT_GATE_SLICE_MS) : FeedVkWaitTimelineR(vk, semPrep, needPrep, IVT_GATE_SLICE_MS);
        if (r != VK_SUCCESS && r != VK_TIMEOUT) { st = 3; if (!waited) g->holding = 0; break; }   // a fence that cannot be waited on: unread, and the hold ends
        waited = true;
    }
    DWORD ms = 0;
    if (waited)
    {
        g->holding = 0;
        QueryPerformanceCounter(&q1);
        const LONGLONG d = q1.QuadPart - q0.QuadPart;
        ms = qf.QuadPart > 0 ? (DWORD)(d * 1000 / qf.QuadPart) : 0;
        _InterlockedIncrement(&g->held);
        if (cap) _InterlockedIncrement(&g->gaveUp);
        InterlockedExchangeAdd64(&g->sumT, d);
        if (d > InterlockedCompareExchange64(&g->maxT, 0, 0)) InterlockedExchange64(&g->maxT, d);
    }
    if (st == 3) _InterlockedIncrement(&g->unread);
    g->prevOut = (LONG)sentOut;
    g->prevPrep = (LONG)prepTop;
    return ms;
}

#ifndef IVT_GATE_CORE_ONLY
// ---- the transport's use: the out-fence alone (included at the end of DLSS-IV.cpp, so no hot line moves) ----

// The state and DXVK's bare Present on a 64-byte line of their own. ivtGateDxvkPresent is null until IvtGateWrap runs.
struct __declspec(align(64)) IvtGateLine { IvtGate gate; void* dxvkPresent; };
static IvtGateLine ivtGateLine;               // zero-initialised: no need, no pointer
#define ivtGate            (ivtGateLine.gate)
#define ivtGateDxvkPresent (ivtGateLine.dxvkPresent)

// Once per present, on the presenting thread, under vsync only (the freeze needs FIFO). The need is the last frame sent, less
// one if it ran pipelined (it waits on the frame before). Nothing after ivtFailed.
static void IvtPresentGate()
{
    if (!latVsync) { if (ivtGate.prevOut) ivtGate.prevOut = 0; return; }
    if (ivtSemOut == VK_NULL_HANDLE || !FeedVkHasTimelineQueries(&ivtVk)) return;
    const uint64_t sent = ivtFailed ? 0 : (uint64_t)(ULONG)ivtWatchAwait;
    const uint64_t sentOut = sent ? sent - (latOrdAsync[sent & (LAT_RING - 1)] ? 1u : 0u) : 0;
    const uint64_t needOut = (uint64_t)(ULONG)ivtGate.prevOut;
    const DWORD ms = IvtGateCore(&ivtGate, &ivtVk, ivtSemOut, sentOut, VK_NULL_HANDLE, 0, nullptr);
    if (ms >= 250)   // a cap give-up is IVT_GATE_MS or more
        LogRecord("  transport: present held %lu ms (fence %llu of %llu)%s", (unsigned long)ms,
                  (unsigned long long)FeedVkTimelineValue(&ivtVk, ivtSemOut), (unsigned long long)needOut, ms >= IVT_GATE_MS ? " GAVE UP" : "");
        // The give-up, said once in the warnings file: DtStuckSay stays quiet while the gate holds.
    static bool gaveUpSaid = false;
    if (ms >= IVT_GATE_MS && !gaveUpSaid)
    {
        gaveUpSaid = true;
        IvtWarn("With vertical sync on, the game waited %d seconds for dlssiv-helper.exe and then went on without it. If the game"
                " is frozen now, end it and start it again. If this happens again, turn vertical sync off and report it with"
                " DLSS-IV.log.", IVT_GATE_MS / 1000);
    }
}

// The mod reaches DXVK's Present through one of two pointers (dtScPresentO or origSwapPresent); IvtGateWrap puts this in its
// place, so every present passes the gate once. Frame generation's machine calls the bare pointer and gates itself.
typedef HRESULT(__stdcall* IvtGatePresent_t)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
static HRESULT __stdcall IvtGatedPresent(IDirect3DSwapChain9* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags)
{
    IvtPresentGate();
    return ((IvtGatePresent_t)ivtGateDxvkPresent)(sc, src, dst, wnd, dirty, flags);
}
// Once a session: a later call or a null gets its argument back, so wraps never chain.
static void* IvtGateWrap(void* present)
{
    if (!present || ivtGateDxvkPresent) return present;
    ivtGateDxvkPresent = present;
    return (void*)&IvtGatedPresent;
}
#endif
