// Frame generation: the armed product's life (fg_arm.h is the binding): install, HOLD and SAVE, conditions, plan, the want.
// Included at the end of fg_product.h, after every other function, so a shared literal keeps its place in .rdata.
#pragma once
static_assert(7 + FGC_N <= 31, "a condition's said-once bit is (1 << (7 + FGC_*)) of fgx.said, above FGX_SAID_FLIGHT");
#include "fg_ship.h"
// The fixed mode's warnings line is said once by bit 1 << 7, which no condition takes; the floor's line uses bit 31.
enum { FGP_SAID_FIXED = 1 << 7 };
static_assert(FGP_SAID_FIXED > FGX_SAID_FLIGHT && FGC_OK == 0, "the fixed mode's said-once bit is the one FgpCondSay never takes");
static FgShipKeys FgpShipKeys()
{
    FgShipKeys f = {};
    f.minFps = (int)fgx.minFps;
    return f;
}
// Every function here that reaches FgpArmOf() asks this first.
static __forceinline bool FgpArmHas() { return fgp.side != nullptr; }

// ---- the install's part
// Render thread, in the build's drained scope.
static bool FgpArmVkMake(char* why, size_t cap)
{
    DT_DIRECT();
    FgpArm& m = *FgpArmOf();
    HANDLE none = nullptr;
    if (FeedVkCreateTimeline(&ivtVk, false, &m.ring.semLocal, &none) < 0 || m.ring.semLocal == VK_NULL_HANDLE) { strcpy_s(why, cap, "its timeline could not be created"); return false; }
    VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = ivtQueueFamily;
    if (!ivtCreateCommandPool || ivtCreateCommandPool(ivtDevice, &pci, nullptr, &m.pool) != VK_SUCCESS || ivtCreateCommandPool(ivtDevice, &pci, nullptr, &m.poolOnce) != VK_SUCCESS)
    { strcpy_s(why, cap, "its command pools could not be created"); return false; }
    VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool = m.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = FGV_RING;
    if (ivtAllocateCommandBuffers(ivtDevice, &cai, m.ring.cb) != VK_SUCCESS) { strcpy_s(why, cap, "its command ring could not be allocated"); return false; }
    cai.commandPool = m.poolOnce; cai.commandBufferCount = 1;
    if (ivtAllocateCommandBuffers(ivtDevice, &cai, &m.cbOnce) != VK_SUCCESS) { strcpy_s(why, cap, "its one-shot command buffer could not be allocated"); return false; }
        // without it no order wait is queued; not a reason to refuse the install
    FgxOrdMake<FgxB>(&m.ord);
        // the stamps' query pool; without it the machine places by the clock due
    {
        HMODULE vkLib = GetModuleHandleA("vulkan-1.dll");
        if (!vkLib) vkLib = GetModuleHandleA("winevulkan.dll");
        PFN_vkGetInstanceProcAddr gipa = vkLib ? (PFN_vkGetInstanceProcAddr)GetProcAddress(vkLib, "vkGetInstanceProcAddr") : nullptr;
        PFN_vkGetPhysicalDeviceProperties props = gipa && ivtVkInst ? (PFN_vkGetPhysicalDeviceProperties)gipa(ivtVkInst, "vkGetPhysicalDeviceProperties") : nullptr;
        PFN_vkGetPhysicalDeviceQueueFamilyProperties qfp = gipa && ivtVkInst ? (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa(ivtVkInst, "vkGetPhysicalDeviceQueueFamilyProperties") : nullptr;
        if (props && qfp && ivtVkPhys != VK_NULL_HANDLE)
        {
            VkPhysicalDeviceProperties p = {};
            props(ivtVkPhys, &p);
            VkQueueFamilyProperties fam[16] = {}; uint32_t nf = 16;
            qfp(ivtVkPhys, &nf, fam);
            FgvTsMake(&m.ts, &ivtVk, (double)p.limits.timestampPeriod, ivtQueueFamily < nf ? fam[ivtQueueFamily].timestampValidBits : 0u);
        }
    }
    return true;
}
// The keys, before the first pair. Defaults are fg_cond.h's and fg_plan.h's.
static void FgpArmKeys(FgState* s)
{
    FgpArm& m = *FgpArmOf();
    m.condK = FgCondDefaults();
        // The fixed mode's key goes into condIn, which every reading starts from.
    {
        const FgShipKeys f = FgpShipKeys();
        FgShipSeed(m.condIn, f);
    }
    m.planK = FgPlanDefaults();
    m.lateFireUs = 0;
        // 3000 us: above the highest K 99th percentile measured, so the first pairs are not late; 1000: half the least median.
    m.planK.startUs = 3000;
    m.planK.minUs = 1000;
        // 4500 us, the ride's cap: an answer slower than that is a hitch anyway.
    m.planK.maxUs = 4500;
        // At 950 about one pair in 25 was late by the law's own choice.
    m.planK.quantilePm = 990;
    m.a.k.kPlanUs = m.planK.startUs;
    m.a.k.lateUs = 0;
    m.a.k.minLeadUs = m.lateFireUs;
        // The room floor (fg_plan.h, ArmedRoom): a clamp the law lets go.
    m.a.k.room = 1; m.a.k.roomGuardUs = 0;
        // 1250 us: a 240 Hz panel at 100 fps needs it. Decides only whether a pair fits or rides.
    m.a.k.roomGuardUs = 1250;
        // Two late answers under 100 pairs apart raise the plan one bin; it goes down only on the four-window quantile.
    m.planK.downWindows = FG_PLAN_WINDOWS;
    m.planMem.last = m.planK.startUs; m.planMem.hold = 0;
    m.a.k.planAns = 1;
    m.a.k.planUpPairs = FgPlanLateSpan(m.planK.quantilePm); m.a.k.planUpUs = FG_K_BIN_US; m.a.k.planMaxUs = m.planK.maxUs;
        // The ride: on a card that is the limit the room floor alone held the game at half its rate. Margin 0.5 ms.
    m.a.k.ride = 1; m.a.k.rideGuardUs = 500;
        // Capped at 4500 us so the ride's floor stays bounded whatever the plan does.
    m.a.k.rideKMaxUs = 4500;
        // One late answer costs one frame where the home waits behind the helper on the queue.
    m.a.k.over = 1;
        // Not asked: it showed no gain and the order wait does its work. fg_pace bit 256 asks for it.
    m.a.k.card = 0;
    m.a.k.rideEarly = 1; m.a.k.armTeach = 1;
        // fg_pace bit 4096 takes the ride's wait floor out (ArmedRideFloors has the trade).
    m.a.k.rideNoWait = 0; m.a.k.ordAtStamp = 1;
    m.a.k.rideWaitAns = 1;
        // The order wait on every sourced ride pair, at most the ride's deadline. 2: the helper line says "on every ride pair".
    m.a.k.ord = 2; m.a.k.ordMaxUs = m.a.k.rideKMaxUs; m.a.k.roomSeed = 1;
    m.a.k.prepWait = 1;
        // Placed by its position or its clock due, whichever first; with no timestamps the clock due.
    m.a.k.place = 1;
        // fg_machine.h says why not.
    m.a.k.place2 = 0;
        // The switch-on (fg_machine.h, RampWant): an engage's first pair never rides, and on a card that is the limit its first
    // presents wait for the queue to drain.
    m.a.k.ramp = FG_RAMP_KEY_ON | FG_RAMP_KEY_DRAIN | FG_RAMP_KEY_QREAD; m.a.k.rampFirst = 1;
    (void)s;
}
// The five threads: the placer and four stampers. FgPace's timer is its own: two waits on one timer cancel each other.
static DWORD WINAPI FgpArmPlacerMain(void*) { return FgThreadPlacer<FgPA, FgpThr>(); }
static DWORD WINAPI FgpArmStamperMain(void* which) { return FgThreadStamper<FgPA, FgpThr>((int)(INT_PTR)which); }
static HANDLE FgpArmTimer()
{
    HANDLE t = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!t) t = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    return t;
}
static bool FgpArmThreadsStart()
{
    if (fgp.thr[0]) return false;
    FgpArm& m = *FgpArmOf();
    // The placer and the stampers wait on timers and fences and run briefly, so they run at THREAD_PRIORITY_HIGHEST (as AMD's
    // own frame generation pacer does), or at the game's render priority if that is higher: a busy CPU then delays their wakes less.
    const int prioGame = GetThreadPriority(GetCurrentThread());
    const int prio = prioGame > THREAD_PRIORITY_HIGHEST && prioGame != THREAD_PRIORITY_ERROR_RETURN ? prioGame : THREAD_PRIORITY_HIGHEST;
    fgp.placerEv = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    fgp.runEv    = CreateEventA(nullptr, TRUE, FALSE, nullptr);     // manual reset
    fgp.quitEv   = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m.placerTimer = FgpArmTimer();
    m.paceEv = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    m.paceTimer = FgpArmTimer();
    if (!fgp.placerEv || !fgp.runEv || !fgp.quitEv || !m.placerTimer || !m.paceEv || !m.paceTimer) return false;
    for (int i = 0; i < 5; i++)
    {
                // reserved, not committed: the 32-bit process counts its address space
        const HANDLE t = i == 0 ? CreateThread(nullptr, 64 * 1024, FgpArmPlacerMain, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)
                                : CreateThread(nullptr, 64 * 1024, FgpArmStamperMain, (void*)(INT_PTR)(i == 1 ? FG_TL_IN : i == 2 ? FG_TL_PREP : i == 3 ? FG_TL_OUT : FG_TL_END),
                                               STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (i < 4) fgp.thr[i] = t; else m.thrEnd = t;
        if (!t) return false;
        SetThreadPriority(t, prio);
    }
    return true;
}
// Once a session. False: frame generation is off for the session, with one line.
static bool FgpArmInstall(FgState* s)
{
    char why[160] = "";
    if (!FgpArmVkMake(why, sizeof(why))) { LogRecord("  frame generation: off for this session: %s", why); return false; }
    FgpArmKeys(s);
    FgWebFind();
    fgp.s = s;                                                   // the threads read it
    if (!FgpArmThreadsStart()) { FgpThreadsStop(true); fgp.s = nullptr; LogRecord("  frame generation: off for this session: its threads could not be started (error %lu)", GetLastError()); return false; }
    if (!FgPM::Install()) { FgpThreadsStop(true); fgp.s = nullptr; LogRecord("  frame generation: off for this session: its machine was installed already"); return false; }
    const FgPtrLine& p = s->ptr;
    LogRecord("  frame generation: installed on build %lu, in standby, and it can show frames: its present, its three notes, the injection's, the present's, the arm's and the disarm's observers;"
              " a placer and four stampers, asleep until it is engaged; chained ahead of the injection %s, the frame end %s, the tracker %s",
              (unsigned long)(ivtBuildFails + 1), p.prevInject == &IvtInjectNoteW ? "yes" : "NO", p.prevPresent == &LatPresentObs ? "yes" : "NO",
              (p.prevArm == &TrkOnArm && p.prevDisarm == &TrkOnDisarm) ? "yes" : "NO");
    return true;
}
// 600 ms, never on the worker.
static void FgpArmThreadsJoin()
{
    HANDLE live[5]; DWORD n = 0;
    for (int i = 0; i < 4; i++) if (fgp.thr[i]) live[n++] = fgp.thr[i];
    if (FgpArmHas() && FgpArmOf()->thrEnd) live[n++] = FgpArmOf()->thrEnd;
    if (n) WaitForMultipleObjects(n, live, TRUE, 600);
}

// ---- the conditions' reading
// A D3D9 call: only in a build's drained scope or the tail of a good Reset.
static bool FgpSwapParams(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* own)
{
    memset(own, 0, sizeof(*own));
    if (!dev) return false;
    IDirect3DSwapChain9* sc = nullptr;
    HRESULT hr = dev->GetSwapChain(0, &sc);
    if (FAILED(hr) || !sc) return false;
    hr = sc->GetPresentParameters(own);
    sc->Release();
    return SUCCEEDED(hr);
}
// NVIDIA's answer on variable refresh: 1 yes, 0 no, -1 unknown (other vendors). It is the driver's setting, not the panel's
// state, so the table takes it checked against the panel (FgpVrrRead).
static int FgpVrrNv()
{
    if (ivtWine) return -1;                                        // Wine and Proton: the classifier alone
    int v = -1;
    const IvtGsyncState& g = ivtGsync;
    if (g.read)
    {
        if (g.vrrPossible == 0 || g.globalMode == 0 || IvtGsyncOffForGame(g)) v = 0;
        else if (g.vrrPossible == 1) v = 1;
    }
    return v;
}
// Fast Sync (VSYNCMODE 0x18888888) looks like variable refresh on a fixed screen, so the panel's FREE does not overrule NVIDIA.
static int FgpVrrFastSync() { return (!ivtWine && ivtGsync.read && ivtGsync.vsyncMode == 0x18888888u) ? 1 : 0; }
// Under the conditions' lock.
DLSSIV_FGDATA static FgShipVrr fgpVrr;
DLSSIV_FGDATA static int fgpVrrSaid;                               // signature of the last said; 0 none
// 1 said, 2 taken back since the answer formed later
DLSSIV_FGDATA static int fgpVrrUnknownSaid, fgpVrrWaitSaid, fgpVrrAskedInWait;
static bool FgpVrrMayForm(int vsync);
// Taken only from a window the mod's pacer did not hold; cleared by a new refresh, a Reset and NVIDIA's yes. Under the
// conditions' lock.
DLSSIV_FGDATA static FgFixLatch fgpFixLatch;
static int FgpFixQuant(bool held, int vrr)
{
    const unsigned long long key = (unsigned long long)FgP::RefreshPacked() ^ ((unsigned long long)(ULONG)statDeviceResets << 1);
    return FgCondFixLatchStep(fgpFixLatch, (int)latFlipDisplay, held ? 1 : 0, key, vrr == 1 ? 1 : 0);
}
// The fixed mode's word for the machine: 0 off, 1 on, 2 uncorrected. Lines are never said under the lock (a line waits on the
// log's lock): steps mark them, the waiter copies the marks, and FgpFixSay says the copy after the lock.
struct FgpFixLinesT { LONG modeSay, modeKey, vrr, hz, why, depth, baseUs, winSay, win, perUs, ppm, medUs, lastUs, wins, held, stalls, lUs, over, sharePm, shareN, shareRun; };
DLSSIV_FGDATA static FgpFixLinesT fgpFixMarks, fgpFixSaid;
DLSSIV_FGDATA static struct { int overRun, base; LONG presDone, presWin; LONGLONG presTicks; unsigned long long key; int keyed, lUs, over, entryX10, wasEngaged; FgFixEntry entry;
    int sharePm, shareN, lRun, dipRun, dipFirst;
} fgpFixSt;
static bool FgpLawDownBySlow();
static void FgpFixPublish(FgCondIn& in, bool window)
{
    {
        const unsigned long long key = (unsigned long long)FgP::RefreshPacked() ^ ((unsigned long long)(ULONG)statDeviceResets << 1);
        if (!fgpFixSt.keyed || key != fgpFixSt.key) { fgpFixSt.key = key; fgpFixSt.keyed = 1; fgpFixSt.base = -1; fgpFixSt.overRun = 0; fgpFixSt.over = 0; fgpFixSt.entryX10 = 0; fgpFixSt.entry.x10 = 0; fgpFixSt.entry.n = 0; fgpFixSt.entry.win = 0; fgpFixSt.entry.age = 0; }
    }
    FgState* s = fgp.s;
    const bool fixedNow = FgCondFixedMode(in);
    if (window)
    {
                // the worker's mean time inside DXVK's Present
        const LONGLONG pt = InterlockedCompareExchange64(&dtPresTicksSum, 0, 0);
        const LONG pd = (LONG)dtPresDone;
        int meanUs = 0;
        if (fgpFixSt.presWin && pd > fgpFixSt.presDone && pt >= fgpFixSt.presTicks)
            meanUs = (int)(LatMs(pt - fgpFixSt.presTicks) * 1000.0 / (double)(pd - fgpFixSt.presDone));
        fgpFixSt.presTicks = pt; fgpFixSt.presDone = pd; fgpFixSt.presWin = 1;
        FgCondFixBaseStep(fgpFixSt.base, meanUs, in.realFpsX10, FgCondHzX1000(in), (!in.engaged && in.vsync && FgCondFixedScreen(in)) ? 1 : 0);
        const int lUs = (s && in.engaged && fixedNow) ? (int)ReadAcquire(&s->r.lawLUs) : 0;
        fgpFixSt.lUs = lUs;
        const int wasOver = fgpFixSt.over;
        const int overUs = FgCondFloorUs(in);
                // Read without a lock, as the median is.
        int shN = 0;
        fgpFixSt.sharePm = (s && s->law && in.engaged && fixedNow) ? FgCondRingOverPm(s->law->slow, overUs + FG_COND_FIX_OVER_US, FG_COND_FIX_SHARE_MIN_N, &shN) : -1;
        fgpFixSt.shareN = shN;
        fgpFixSt.lRun = (in.engaged && fixedNow && lUs > overUs + FG_COND_FIX_OVER_US) ? (fgpFixSt.lRun < 1000 ? fgpFixSt.lRun + 1 : fgpFixSt.lRun) : 0;
        const int eUs = (s && in.engaged && fixedNow) ? (int)ReadAcquire(&s->r.lawEUs) : 0;
        fgpFixSt.over = FgCondFixShareStep(fgpFixSt.overRun, fgpFixSt.sharePm, eUs, overUs + FG_COND_FIX_OVER_US, (in.engaged && fixedNow) ? 1 : 0);
                // The way back in: set where the overrun first stands, cleared by the next engage.
        const int setX10 = (fgpFixSt.over && !wasOver && s && s->law) ? FgCondFixEntryX10(lUs, FgCondRingMedUs(s->law->fast), FgCondFloorUs(in)) : 0;
        if (in.engaged && !fgpFixSt.wasEngaged && !fgpFixSt.over) { fgpFixSt.entry.x10 = 0; fgpFixSt.entry.n = 0; }
        if (!in.engaged && fgpFixSt.wasEngaged && fixedNow && !setX10 && fgpFixSt.entry.x10 == 0 && FgpLawDownBySlow()) FgCondFixEntrySlowSet(fgpFixSt.entry, FgCondFloorX10(in, FgCondDefaults()));
        fgpFixSt.entryX10 = FgCondFixEntryStep(fgpFixSt.entry, setX10, (!in.engaged && fixedNow) ? 1 : 0, FgCondFloorX10(in, FgCondDefaults()));
        fgpFixSt.wasEngaged = in.engaged;
    }
    in.fixLUs = fgpFixSt.lUs; in.fixOver = fgpFixSt.over; in.fixEntryX10 = fgpFixSt.entryX10; in.fixSharePm = fgpFixSt.sharePm;
    if (window) fgpFixSt.dipFirst = FgCondFixDipStep(fgpFixSt.dipRun, in);
    in.fixDipFirst = fgpFixSt.dipFirst;
    if (!s) return;
    FgRenderLine& r = s->r;
    const LONG base = fgpFixSt.base > 0 ? (LONG)fgpFixSt.base : 0;
    if (ReadAcquire(&r.fixBaseUs) != base) WriteRelease(&r.fixBaseUs, base);
    const LONG depth = latQueueAsked > 0 ? (LONG)latQueueAsked : latQueueNote[0] == 'y' ? 1 : 0;
    const LONG want = fixedNow ? (LONG)in.fixedKey : 0;
    const LONG was = ReadAcquire(&r.fixOn);
    if (was != want)
    {
        WriteRelease(&r.fixOn, want);
        fgpFixMarks.modeSay = 1; fgpFixMarks.modeKey = want; fgpFixMarks.vrr = in.vrr; fgpFixMarks.hz = FgCondHzX1000(in);
        fgpFixMarks.why = (!in.vsync && FgCondFixedScreen(in)) ? 1 : FgCondFixedSeen(in) ? 2 : 3;
        fgpFixMarks.depth = depth; fgpFixMarks.baseUs = base;
    }
    if (!window) return;
    if (want)
    {
        fgpFixMarks.winSay = 1; fgpFixMarks.win = latWindows; fgpFixMarks.perUs = (LONG)(*(const volatile double*)&r.period * 1000.0);
        fgpFixMarks.ppm = r.fixPpm; fgpFixMarks.medUs = r.fixMedUs; fgpFixMarks.lastUs = r.fixLastBlkUs; fgpFixMarks.wins = r.fixWins;
        fgpFixMarks.held = r.fixHeld; fgpFixMarks.stalls = r.fixStalls; fgpFixMarks.lUs = fgpFixSt.lUs; fgpFixMarks.over = fgpFixSt.lRun;
        fgpFixMarks.sharePm = fgpFixSt.sharePm; fgpFixMarks.shareN = fgpFixSt.shareN; fgpFixMarks.shareRun = fgpFixSt.overRun;
    }
    fgpFixSaid = fgpFixMarks;
    fgpFixMarks.modeSay = 0; fgpFixMarks.winSay = 0;
}
// On the waiter, after the conditions' lock. A tool reads the literal to its first colon.
static void FgpVrrSay()
{
    const int nv = FgpVrrNv(), fast = FgpVrrFastSync(), ev = fgpVrr.ev, a = FgShipVrrAnswer(fgpVrr, nv, fast);
        // The wait for the panel ended with no answer: the set asked for in the wait stays until the next build.
    if (!fgpVrrWaitSaid && fgpVrrAskedInWait && a < 0 && !FgpVrrMayForm(1) && FgpArmHas() && FgpArmOf()->refusal == FGC_VRR_UNKNOWN)
    {
        fgpVrrWaitSaid = 1;
        LogRecord("  frame generation: variable refresh: still NOT KNOWN after %d windows: the next build asks the helper for no set, and the set of this build is given back there (a set goes only at a build)",
                  (int)FG_SHIP_VRR_WAIT_WIN);
    }
        // The answer formed after FGC_VRR_UNKNOWN's line was written: take it back.
    if (fgpVrrUnknownSaid == 1 && a >= 0)
    {
        fgpVrrUnknownSaid = 2;
        static const char kOld[] = "frame generation: off: the mod could not tell";
        const LONG n = ivtWarnCount < 8 ? ivtWarnCount : 8;
        for (LONG i = 0; i < n; i++)
            if (!strncmp(ivtWarnKept[i], kOld, sizeof(kOld) - 1)) strcpy_s(ivtWarnKept[i], "frame generation: variable refresh was read later in this session");
        char path[MAX_PATH];
        FILE* f = nullptr;
        if (IvtWarnPath(path, false)) f = _fsopen(path, "a", _SH_DENYNO);
        if (!f && IvtWarnPath(path, true)) f = _fsopen(path, "a", _SH_DENYNO);
        if (f)
        {
            fprintf(f, "- Update: later in this session the mod could tell that the display %s variable refresh for the game, so the frame generation message above about it no longer applies.\n",
                    a > 0 ? "runs" : "does not run");
            fclose(f);
        }
        LogRecord("  frame generation: variable refresh: the answer formed after the warnings file said it could not be told: the file is updated");
    }
    const int sig = 1 + (a + 1) + 3 * ev + 9 * (nv + 1) + 27 * fast;
    if (sig == fgpVrrSaid) return;
    fgpVrrSaid = sig;
    LogRecord("  frame generation: variable refresh: %s for the game, read at window %ld (NVIDIA's driver %s; %s)%s",
              a > 0 ? "ON" : a == 0 ? "OFF" : "NOT KNOWN", (long)latWindows,
              nv > 0 ? "says on" : nv == 0 ? "says off" : ivtWine ? "not asked under Wine" : ivtGsync.read ? "did not say for this display" : "not read: another vendor's card, or its reading failed",
              ev == 1 ? "the presents follow the game" : ev == 2 ? "the presents land on a fixed refresh grid" : "the presents have not shown it yet",
              (ev == 1 && nv == 0 && fast) ? "; NVIDIA's vertical sync for the game is Fast Sync, which also shows frames off the grid, so the driver's answer stands"
              : (ev == 1 && nv < 0) ? "; a driver's Enhanced Sync or Fast Sync over vsync would read the same on a fixed screen" : "");
}
// Mode and window lines, on the waiter after the conditions' lock; a tool reads each literal. The shipped build says the window
// line only when it carries news (fg_fixsay.h); the development build says every window.
#if !DLSSIV_DEV
#include "fg_fixsay.h"
DLSSIV_FGDATA static FgFixSaySt fgpFixShip;
#endif
static void FgpFixSay()
{
    FgpVrrSay();
    FgpFixLinesT& l = fgpFixSaid;
    if (l.modeSay)
    {
        l.modeSay = 0;
#if !DLSSIV_DEV
        if (l.modeKey) FgFixSayModeOn(fgpFixShip);
#endif
        const int hz = (int)l.hz;
                // vrr 0 by the presents' fixed grid is not NVIDIA's word
        if (l.modeKey) LogRecord("  frame generation: fixed refresh: the mode is on (fg_fixed %ld): the display reads %d.%03d Hz and is fixed by %s, vsync is on; the game is held to half the refresh, every frame on its own refresh, the period's correction %s; DXVK's frame queue %ld%s, the Present's own time %ld us",
                                 (long)l.modeKey, hz / 1000, hz % 1000, (l.vrr == 0 && fgpVrr.ev != 2) ? "NVIDIA's driver" : "the presents read on a fixed grid while standing by", l.modeKey == 2 ? "off" : "on",
                                 (long)l.depth, l.depth == 0 ? " (unknown: DXVK's own)" : l.depth > 1 ? " (each above 1 a refresh of picture age more)" : "", (long)l.baseUs);
                // the warnings line is FgpFixWarnStep's
        else LogRecord("  frame generation: fixed refresh: the mode is off (%s)",
                       l.why == 1 ? "vsync is off" : l.why == 2 ? "the display is not one the mode may run on" : "the display is no longer known fixed");
    }
    if (l.winSay)
    {
        l.winSay = 0;
#if !DLSSIV_DEV
        const FgFixSayW w = { (long)l.win, (long)l.perUs, (long)l.ppm, (long)l.medUs, (long)l.lastUs, (long)l.wins, (long)l.held, (long)l.stalls,
                              (long)l.lUs, (long)l.over, (long)l.sharePm, (long)l.shareN, (long)l.shareRun };
        const int why = FgFixSayStep(fgpFixShip, w, GetTickCount64(), (long)FG_COND_FIX_SHARE_PM);
        if (!why) return;
        char b[768];
        if (FgFixSayLine(b, sizeof(b), w, why, fgpFixShip, (int)FG_FIX_TARGET_US, (int)FG_COND_FIX_SHARE_PM) > 0) LogRecord("%s", b);
        FgFixSaySaid(fgpFixShip);
#else
        LogRecord("  frame generation: fixed refresh: window %ld: the period %ld us (the correction %+ld ppm), the block a present median %ld us (target %d), the last %ld us, windows corrected %ld, held for the game %ld, stalls %ld; the law's L %ld us (over two refreshes %ld window(s) running); the share over two refreshes %ld of 1000 of %ld sample(s) of the last 12 s (over %d of 1000 %ld window(s) running)",
                  (long)l.win, (long)l.perUs, (long)l.ppm, (long)l.medUs, (int)FG_FIX_TARGET_US, (long)l.lastUs, (long)l.wins, (long)l.held, (long)l.stalls, (long)l.lUs, (long)l.over,
                  (long)l.sharePm, (long)l.shareN, (int)FG_COND_FIX_SHARE_PM, (long)l.shareRun);
#endif
    }
}
#if !DLSSIV_DEV
// At exit (DllMain's detach, every other thread gone). Through the raw handle: nothing goes through the C runtime here.
static void FgpFixSayExit()
{
    if (!fgp.s || logLevel < 1 || !logPath[0]) return;
    FgpFixLinesT& l = fgpFixSaid;
    const FgFixSayW w = { (long)l.win, (long)l.perUs, (long)l.ppm, (long)l.medUs, (long)l.lastUs, (long)l.wins, (long)l.held, (long)l.stalls,
                          (long)l.lUs, (long)l.over, (long)l.sharePm, (long)l.shareN, (long)l.shareRun };
    int why = l.winSay ? FgFixSayStep(fgpFixShip, w, GetTickCount64(), (long)FG_COND_FIX_SHARE_PM) : 0;
    l.winSay = 0;
    if (!why) why = FgFixSayEnd(fgpFixShip);
    if (!why) return;
    char b[768];
    int n = FgFixSayLine(b, sizeof(b) - 2, w, why, fgpFixShip, (int)FG_FIX_TARGET_US, (int)FG_COND_FIX_SHARE_PM);
    FgFixSaySaid(fgpFixShip);
    if (n <= 0) return;
    b[n++] = '\r'; b[n++] = '\n';
    const DWORD wt = logMutex ? WaitForSingleObject(logMutex, 200) : WAIT_FAILED;
    const HANDLE h = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) { DWORD done = 0; WriteFile(h, b, (DWORD)n, &done, nullptr); CloseHandle(h); }
    if (wt == WAIT_OBJECT_0 || wt == WAIT_ABANDONED) ReleaseMutex(logMutex);
}
#endif
static int FgpLawFreeX10(const FgState& s, float mPms, int floorUs, bool engaged, bool window);
static int FgpLawPredX10(int aloneX10);
static int FgpLawPredOnX10(int aloneX10, int fixedMode);
static void FgpLawFfApply(FgCondIn& in);
static unsigned FgpLawWarmLift();
static int FgpLawEngagedNow(bool wantOn, LONG refusal);
static int FgpLawEngagingNow();
static bool FgpLawRampWin();
// FgCondIn::vrr: NVIDIA's answer checked against the flip classifier's display memory, the classifier alone without NVIDIA.
// Under the conditions' lock, never at a present. The classifier's two statics are the waiter's, read as two aligned 32-bit
// loads: a torn read sees the window as older, which takes less, never more.
static int FgpVrrRead()
{
    const FgState* s = fgp.s;
    const unsigned long long key = (unsigned long long)FgP::RefreshPacked() ^ ((unsigned long long)(ULONG)statDeviceResets << 1);
    const int win = (int)latWindows;
    FgShipVrrKey(fgpVrr, key, (unsigned long long)FgP::RefreshPacked(), win);   // restarts at a new refresh only, not at a Reset
    const bool engaged = (s && FgpArmHas()) ? FgpLawEngagedNow(s->wt.want != FG_WANT_PASS, FgpArmOf()->refusal) != 0 : false;
    FgShipVrrTake(fgpVrr, (int)latFlipDisplay, (int)latFlipDisplaySince, win, (engaged || FgpLawRampWin()) ? 1 : 0);
    return FgShipVrrAnswer(fgpVrr, FgpVrrNv(), FgpVrrFastSync());
}
static bool FgpVrrMayForm(int vsync) { return FgShipVrrMayForm(fgpVrr, FgpVrrNv(), vsync, (int)latWindows) != 0; }

// The better refresh: said once a session in the warnings file; the mod never switches the display.
// The offered refreshes are read at most once a display key: about 25 ms for a TV with 1001 modes, on the waiter.
DLSSIV_FGDATA static FgShipBetter fgpBetter;
DLSSIV_FGDATA static struct { unsigned long long key; int keyed, n, hz[16]; } fgpOffer;
static void FgpOfferedHz()
{
    const unsigned long long key = (unsigned long long)FgP::RefreshPacked() ^ ((unsigned long long)(ULONG)statDeviceResets << 1);
    if (fgpOffer.keyed && fgpOffer.key == key) return;
    fgpOffer.key = key; fgpOffer.keyed = 1; fgpOffer.n = 0;
    const HMONITOR mon = hdrGameWnd ? MonitorFromWindow(hdrGameWnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, (MONITORINFO*)&mi)) return;
    DEVMODEW cur = {}; cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &cur)) return;
    for (DWORD i = 0; i < 4096 && fgpOffer.n < 16; i++)
    {
        DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(mi.szDevice, i, &dm)) break;
        if (dm.dmPelsWidth != cur.dmPelsWidth || dm.dmPelsHeight != cur.dmPelsHeight || (dm.dmDisplayFlags & DM_INTERLACED) || dm.dmDisplayFrequency <= 1) continue;
        bool seen = false;
        for (int j = 0; j < fgpOffer.n; j++) if (fgpOffer.hz[j] == (int)dm.dmDisplayFrequency) seen = true;
        if (!seen) fgpOffer.hz[fgpOffer.n++] = (int)dm.dmDisplayFrequency;
    }
}
static void FgpBetterSay(const FgCondIn& in, const FgCondK& k, int row, bool on)
{
    const int g = FgShipBetterStep(fgpBetter, in, row, on ? 1 : 0);
    if (!g) return;
    FgpOfferedHz();
    const int r = FgShipBetterHz(in, k, g, fgpOffer.hz, fgpOffer.n);
    if (!r) return;
    fgpBetter.said = 1;
    const int hzW = (FgCondHzX1000(in) + 500) / 1000;
    IvtWarn("frame generation: off at this display's fixed %d Hz: the game runs at about %d fps, and frame generation needs about %d fps there (a little over half the refresh rate). The display also offers %d Hz, where frame generation would run and show %d fps, every frame on its own refresh. The mod never changes the refresh rate. To use %d Hz, set it in Windows' display settings",
            hzW, (g + 5) / 10, (FgCondFloorX10(in, k) + 5) / 10, r, r, r);
}
// dev: non-null where a D3D9 call may be made; null (the waiter) keeps what the last build or Reset read. mPms: the window's
// median real frame time. Engaged the median is the pacer's own period, so the game's rate is R_free.
// fgpFixWin: [0] n, [1] the window's mean interval in us, capped at three refreshes. A reader on another thread may pair one
// window's mean with the next's n. In DLSSIV_FGDATA so no .bss static moves.
DLSSIV_FGDATA static int fgpFixWin[2];
static void FgpCondRead(FgCondIn* out, IDirect3DDevice9* dev, int bbVkFormat, float mPms)
{
    const FgState* s = fgp.s;
    FgCondIn in = {};
    if (FgpArmHas()) in = FgpArmOf()->condIn;
    else { in.windowed = 1; }
    in.machineDead = (s && s->sh.state == FG_DEAD) ? 1 : 0;
    in.restoreLost = FgpArmHas() ? (int)FgpArmOf()->a.w.restoreLost : 0;
        // the machine's copy once installed, the global before
    in.presentOurs = ((s && s->ptr.installed) ? s->ptr.origSwapPresent : (void*)origSwapPresent) == (void*)&dtScPresent ? 1 : 0;
    in.markerOk = (latEndOn && latSemEnd != VK_NULL_HANDLE) ? 1 : 0;
    in.agility = (fgx.c && fgx.c->set.status == FEED_FG_ST_RUNTIME) ? 1 : 0;
        // DXVK's version is not kept: -1 refuses nothing
    in.dxvk31 = -1;
    in.asyncNamed = ivtCfgNamedAsync ? 1 : 0; in.asyncHome = ivtCfg.async_home;
    const ULONGLONG rp = FgP::RefreshPacked();
    in.refreshNum = (int)(ULONG)(rp >> 32); in.refreshDen = (int)(ULONG)rp;
    in.lockPm = FG_LOCK_PM;
    if (dev)
    {
        D3DPRESENT_PARAMETERS own;
        if (FgpSwapParams(dev, &own)) { in.swapEffect = (int)own.SwapEffect; in.backBuffers = own.BackBufferCount ? (int)own.BackBufferCount : 1; in.windowed = own.Windowed ? 1 : 0;
                                        FGA(in.sdrMsaa = own.MultiSampleType != D3DMULTISAMPLE_NONE ? 1 : 0;) }
        else { in.swapEffect = 0; in.backBuffers = 0; in.windowed = 1; }   // not read: the swapchain row refuses
    }
    if (bbVkFormat) in.bbFp16 = bbVkFormat == (int)VK_FORMAT_R16G16B16A16_SFLOAT ? 1 : 0;
    in.hdrOn = hdrEngaged ? 1 : 0;
    in.vrr = FgpVrrRead();
    in.ffCap = latFfCap; in.ffRel = latFfRel; in.dxvkCap = latDxvkCap;
    in.menu = 0;                                                   // a menu is MayEngage's, never a standing row
        // where the law drives, its state, never the want word
    in.engaged = (s && FgpArmHas()) ? FgpLawEngagedNow(s->wt.want != FG_WANT_PASS, FgpArmOf()->refusal) : 0;
    in.vsync = latVsync ? 1 : 0;
        // A ramp's paced presents are no memory of the screen.
    (void)FgpFixQuant(in.engaged != 0 || FgpLawRampWin(), in.vrr);
    in.flipQuant = FgCondFixQuantIn(fgpFixLatch);
    in.fixedKey = 1;
    const LONG pUs = latPeriodUs;
    in.realFpsX10 = FgCondRateX10((double)mPms, (int)pUs);         // standing by the game's own; engaged R_on
    in.freeFpsX10 = s ? FgpLawFreeX10(*s, mPms, FgCondFloorUs(in), in.engaged != 0, dev == nullptr) : 0;   // R_free; 0 standing by
    in.freeFpsX10 = FgCondFixedFreeX10(in, fgpFixWin[0], fgpFixWin[1]);   // fixed mode standing by: the screen's rate where under the median's
    in.predFpsX10 = in.engaged ? 0 : FgpLawPredOnX10(in.realFpsX10, FgCondFixedMode(in) ? 1 : 0);
    in.engaging = FgpLawEngagingNow();
    FgpLawFfApply(in);
    in.lift = FgpLawWarmLift();
    in.nvCap = (int)latNvCapFps;                                   // read once; 0 none
    FgpFixPublish(in, dev == nullptr);
    *out = in;
}
// Once a session: the log, and the warnings file where a player would otherwise have a key that does nothing.
static void FgpCondSay(int c, const FgCondIn& in, const FgCondK& k)
{
    if (c == FGC_VRR_UNKNOWN && FgpVrrMayForm(in.vsync)) return;   // not yet: the panel may still answer
    if (c <= FGC_OK || c >= FGC_N || !FgxSayOnce((LONG)(1u << (7 + c)))) return;
    char b[320];
    if (FgCondLine(c, in, k, b, sizeof(b)) <= 0) return;
    if (c == FGC_VRR_UNKNOWN) fgpVrrUnknownSaid = 1;
    LogHealth("  %s", b);
    if (FgCondWarns(c)) IvtWarn("%s", b);
}
static void FgpLawCondKeys(FgCondK& k);
// At a build's tail, after a good Reset, and once a window on the latency waiter. Returns the standing row (FGC_OK: none).
static int FgpCondStep(IDirect3DDevice9* dev, int bbVkFormat, float mPms)
{
    if (!FgpArmHas()) return FGC_OK;
    FgpArm& m = *FgpArmOf();
        // One step at a time, else a Reset's fresh facts could be overwritten by a window's step begun before it.
    AcquireSRWLockExclusive(&m.condLock);
    if (!dev && !m.condRead) { ReleaseSRWLockExclusive(&m.condLock); return FGC_OK; }   // the install's own build reads the device next
    FgpLawCondKeys(m.condK);
    FgCondIn in; FgpCondRead(&in, dev, bbVkFormat, mPms);
    m.condIn = in;
    if (dev) m.condRead = 1;
    const int c = FgCondFirst(in, m.condK);
    if (m.refusal != c) _InterlockedExchange(&m.refusal, c);
    ReleaseSRWLockExclusive(&m.condLock);
    if (!dev) FgpFixSay();
    if (c) FgpCondSay(c, in, m.condK);
    if (m.menuSeen) { FgCondIn mi = in; mi.menu = 1; FgpCondSay(FGC_MENU, mi, m.condK); }
    return c;
}
// True: ask no set. A SESSION or RESET row means no frame before the next build, and the helper's side costs hundreds of MiB.
static bool FgpNoSetNow(int bbVkFormat)
{
    if (fgp.tried && !fgp.s) return true;
    FgCondIn in; FgCondK k = FgCondDefaults(); int c;
    if (FgpArmHas())
    {
        FgpArm& m = *FgpArmOf();
        AcquireSRWLockExclusive(&m.condLock);
        FgpLawCondKeys(m.condK);
        FgpCondRead(&in, ivtDev, bbVkFormat, 0.0f);
        k = m.condK; m.condIn = in; m.condRead = 1;
        c = FgCondFirst(in, k);
        if (m.refusal != c) _InterlockedExchange(&m.refusal, c);
        ReleaseSRWLockExclusive(&m.condLock);
    }
    else { FgpCondRead(&in, ivtDev, bbVkFormat, 0.0f); c = FgCondFirst(in, k); }
    if (!c || FgCondClassOf(c) == FGCC_STANDBY) return false;
        // While the panel may still answer, ask for the set as a STANDBY row does, so the answer can be used before the next build.
    if (c == FGC_VRR_UNKNOWN && FgpVrrMayForm(in.vsync)) { fgpVrrAskedInWait = 1; return false; }
    FgpCondSay(c, in, k);
    return true;
}
static const char* FgxStatusName(int st);
// Every reason but a busy queue (which passes by itself) says one plain warnings line, once a session per status.
static void FgpSetRefused(int status)
{
    if (status == FEED_FG_ST_RUNTIME)
    {
        FgCondIn in = {}; in.agility = 1;
        FgpCondSay(FGC_AGILITY, in, FgCondDefaults());
        return;
    }
    if (status == FEED_FG_ST_BUSY || status < 0 || status > 15 || !FgxSayOnce2((LONG)FGX_SAID2_SET << status)) return;
    switch (status)
    {
    case FEED_FG_ST_VRAM:
        IvtWarn("frame generation: off: the graphics card did not have enough free video memory for it. A lower resolution or texture quality, or closing other programs that use the graphics card, frees some");
        break;
    case FEED_FG_ST_CREATE:
        IvtWarn("frame generation: off: it could not be set up on this graphics card; please send me DLSS-IV.log");
        break;
    case FEED_FG_ST_WARMUP:
        IvtWarn("frame generation: off: its first test frame failed on this graphics card; please send me DLSS-IV.log");
        break;
    case FEED_FG_ST_RAISED:
        IvtWarn("frame generation: off for this session: its library crashed while it started; please send me DLSS-IV.log");
        break;
    default:
        IvtWarn("frame generation: off: it could not start; please send me DLSS-IV.log (the helper refused its set: %s)", FgxStatusName(status));
        break;
    }
}

// ---- HOLD and SAVE
// All or none. 191.4 MiB at 3840x2160 FP16, 95.7 at 8 bits, none of it in DXVK's streaming budget.
static bool FgpArmImagesMake(FgpArm& m, UINT w, UINT h, VkFormat fmt)
{
    DT_DIRECT();
    if (!m.mpRead)
    {
        const PFN_vkGetInstanceProcAddr gipa = ivtVk.lib ? (PFN_vkGetInstanceProcAddr)GetProcAddress(ivtVk.lib, "vkGetInstanceProcAddr") : nullptr;
        const PFN_vkGetPhysicalDeviceMemoryProperties gmp = (gipa && ivtVkInst) ? (PFN_vkGetPhysicalDeviceMemoryProperties)gipa(ivtVkInst, "vkGetPhysicalDeviceMemoryProperties") : nullptr;
        if (!gmp || ivtVkPhys == VK_NULL_HANDLE) return false;
        gmp(ivtVkPhys, &m.mp); m.mpRead = 1;
    }
    for (int i = 0; i < FG_HOLD_SLOTS; i++)
        if (!FgvImageMake(&ivtVk, m.mp, w, h, fmt, &m.hold[i])) { while (i-- > 0) FgvImageFree(&ivtVk, &m.hold[i]); return false; }
    if (!FgvImageMake(&ivtVk, m.mp, w, h, fmt, &m.save)) { for (int i = FG_HOLD_SLOTS; i-- > 0; ) FgvImageFree(&ivtVk, &m.hold[i]); return false; }
    m.iw = w; m.ih = h; m.ifmt = fmt; m.imgMade++;
    return true;
}
// Behind the retire wait. Not retired in 2 s: not destroyed but forgotten and left to the device.
static void FgpArmImagesGive(FgpArm& m)
{
    DT_DIRECT();
    bool any = m.save.img != VK_NULL_HANDLE;
    for (int i = 0; i < FG_HOLD_SLOTS; i++) if (m.hold[i].img) any = true;
    if (!any) return;
    if (FgPA::RetireWait(2000)) { for (int i = 0; i < FG_HOLD_SLOTS; i++) FgvImageFree(&ivtVk, &m.hold[i]); FgvImageFree(&ivtVk, &m.save); }
    else { for (int i = 0; i < FG_HOLD_SLOTS; i++) m.hold[i] = FgvImage(); m.save = FgvImage(); m.imgLeft++; }
    m.iw = 0; m.ih = 0; m.ifmt = VK_FORMAT_UNDEFINED;
}
// In its own submit that retires before anything is copied: every image to GENERAL, then the set's images released, so every
// later acquire has a release before it. 1 retired; 0 not submitted; -1 not retired in 5 s (its images are then in flight).
static int FgpArmFirstLayout(FgxClient& c, bool freshOwn)
{
    DT_DIRECT();
    FgpArm& m = *FgpArmOf(); FgvRing& g = m.ring;
    ivtResetCommandBuffer(m.cbOnce, 0);
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (ivtBeginCommandBuffer(m.cbOnce, &bi) != VK_SUCCESS) return 0;
    VkImage own[1 + FG_HOLD_SLOTS] = { m.save.img };
    for (int i = 0; i < FG_HOLD_SLOTS; i++) own[1 + i] = m.hold[i].img;
    const uint32_t nOwn = freshOwn ? (uint32_t)(1 + FG_HOLD_SLOTS) : 0u;
        // a set may lack HUDLESS and GEN2
    const uint32_t nSet = c.set.nAll;
    if (!c.laid) FgvRecFirstLayout(&ivtVk, m.cbOnce, own, nOwn, c.set.all, nSet, ivtQueueFamily);
    else
    {
        FgvRecFirstLayout(&ivtVk, m.cbOnce, own, nOwn, nullptr, 0, ivtQueueFamily);
        FeedVkExternalTransferN(&ivtVk, m.cbOnce, c.set.all, nSet, ivtQueueFamily, true /*release*/);
    }
    if (ivtEndCommandBuffer(m.cbOnce) != VK_SUCCESS) return 0;
    const UINT64 val = (UINT64)InterlockedIncrement64(&g.askedN);
    VkTimelineSemaphoreSubmitInfo tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    tl.signalSemaphoreValueCount = 1; tl.pSignalSemaphoreValues = &val;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext = &tl; si.commandBufferCount = 1; si.pCommandBuffers = &m.cbOnce; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &g.semLocal;
    ivtIDev->LockSubmissionQueue();
    const VkResult r = ivtQueueSubmit(ivtQueue, 1, &si, VK_NULL_HANDLE);
    if (r == VK_SUCCESS) InterlockedExchange64(&g.tookN, (LONGLONG)val);
    ivtIDev->ReleaseSubmissionQueue();
    if (r != VK_SUCCESS) return 0;
    return FeedVkWaitTimeline(&ivtVk, g.semLocal, val, 5000) ? 1 : -1;
}
// The set acquired and the worker drained. Never fails the build: a failure shows nothing until the next build, with a line.
static void FgpArmAtBuild(FgxClient& c, unsigned w, unsigned h, int bbVkFormat)
{
    FgpArm& m = *FgpArmOf();
    m.homeN = 0;
    const int row = FgpCondStep(ivtDev, bbVkFormat, 0.0f);
    if (c.inFlight) return;
    const VkFormat fmt = (VkFormat)bbVkFormat;
    bool fresh = false;
    if (!(row && FgCondClassOf(row) == FGCC_SESSION) && (!m.hold[0].img || !m.save.img || m.iw != w || m.ih != h || m.ifmt != fmt))
    {
        FgpArmImagesGive(m);
        fresh = FgpArmImagesMake(m, w, h, fmt);
        if (fresh) LogHealth("  frame generation: its two images of its own (the hold and the save) made for this back buffer: %ux%u VkFormat %d, %.1f MiB each (made %ld time(s), left to the device %ld)",
                             w, h, bbVkFormat, (double)m.hold[0].bytes / (1024.0 * 1024.0), (long)m.imgMade, (long)m.imgLeft);
        else LogRecord("  frame generation: its two images of its own (%ux%u VkFormat %d) could not be made: nothing generated is shown until the next build", w, h, bbVkFormat);
    }
    const int r = FgpArmFirstLayout(c, fresh);
    if (r > 0) { c.laid = true; return; }
        // Submitted and not retired: the set and images are forgotten unless the next build's retire wait finds it done.
    if (r < 0) { c.inFlight = true; m.layoutFlight = 1; }
    m.iw = 0; m.ih = 0;
    LogRecord("  frame generation: this build's set is not used: its images' first layout %s", r < 0 ? "did not retire in 5 s" : "could not be submitted");
}
// A first layout that had not retired has now, so its set may be destroyed like any other.
static void FgpArmRetired()
{
    FgpArm& m = *FgpArmOf();
    if (!m.layoutFlight) return;
    m.layoutFlight = 0;
    if (fgx.c) fgx.c->inFlight = false;
}

// ---- the plan
// Once a window on the latency waiter. The placer raises the plan too (ArmedPlanLate), so this stores by compare and exchange
// and a rise that lands meanwhile is not undone.
static void FgpPlanStep()
{
    FgpArm& m = *FgpArmOf(); const FgShadowPlacer& sp = fgp.x->pl;
    LONG n = m.planK.windows; if (n < 1) n = 1; if (n > FG_PLAN_WINDOWS) n = FG_PLAN_WINDOWS;
    LONG nd = m.planK.downWindows; if (nd < n) nd = n; if (nd > FG_PLAN_WINDOWS) nd = FG_PLAN_WINDOWS;
    LONG* win = m.planWin[m.planAt % FG_PLAN_WINDOWS];
    for (int i = 0; i < FG_K_BINS; i++) { const LONG now = sp.kHist[i]; win[i] = now - m.planSeen[i]; m.planSeen[i] = now; }
    m.planAt++;
    LONG up[FG_K_BINS], down[FG_K_BINS];
    for (int i = 0; i < FG_K_BINS; i++)
    {
        LONG su = 0, sd = 0;
        for (LONG back = 0; back < nd; back++) { const LONG c = m.planWin[(m.planAt - 1 - back) & (FG_PLAN_WINDOWS - 1)][i]; sd += c; if (back < n) su += c; }
        up[i] = su; down[i] = sd;
    }
    const LONG cur = ReadAcquire(&m.a.k.kPlanUs);
    const LONG next = FgPlanStep(m.planMem, up, down, FG_K_BINS, FG_K_BIN_US, m.planK, cur);
    if (next != cur) { if (InterlockedCompareExchange(&m.a.k.kPlanUs, next, cur) != cur) m.planMem.last = cur; }
}
static_assert((FG_PLAN_WINDOWS & (FG_PLAN_WINDOWS - 1)) == 0, "FgpPlanStep's ring of windows is indexed by a mask");

// ---- the driver's seam
// The one writer of the want, once a window on the waiter. The stampers' run event first, then the want.
static void FgpArmWant(LONG want)
{
    FgState* s = fgp.s;
    if (!s || !FgpArmHas()) return;
    FgpArm& m = *FgpArmOf();
    if (FgThreadsRun<FgPA>(&m.passWindows, want)) SetEvent(fgp.runEv); else ResetEvent(fgp.runEv);
    if (s->wt.want != want) WriteRelease(&s->wt.want, want);
}
// True where the switch-on's floor raised a period in the window just ended: the law learns nothing from it. The first call
// in a window takes the count.
static LONG fgpRampWinAt = -1, fgpRampWin, fgpRampSeen;
static bool FgpRampWindow()
{
    if (!FgpArmHas()) return false;
    const LONG w = latWindows;
    if (w != fgpRampWinAt)
    {
        const LONG seen = ReadAcquire(&FgpArmOf()->a.r.rampHeld);
        fgpRampWin = seen != fgpRampSeen ? 1 : 0; fgpRampSeen = seen; fgpRampWinAt = w;
    }
    return fgpRampWin != 0;
}

// ---- the entry points (FGA)
// A pair found armed is brought to its real frame by the worker inside the build's drain; never presented from the render thread.
static void FgpBeforeBuild()
{
    if (fgp.s) FgPM::BeforeBuild();
}
// Needed even at log=0: FgPace's floor and the conditions' ceiling are the refresh.
static bool FgpWantsRefresh()
{
    return FgxWhyNotStatic() == nullptr && !fgx.argDropped;
}
// The RESET rows asked again at once (exclusive fullscreen parks here), ahead of the build.
static void FgpOnResetDone(IDirect3DDevice9* dev, const D3DPRESENT_PARAMETERS* pp)
{
    (void)pp;
    if (!fgp.s) return;
    FgpCondStep(dev, 0, 0.0f);
}
// An inline injection runs on the drained render thread, where no observer runs and the body would write guides the last prepare
// may still read: a pair found armed is only counted here, and its real frame goes out at the next drain note.
static void FgpHomeWaited(LONG n) { if (FgpArmHas()) FgpArmOf()->homeN = n; }
static void FgpInlineGuard()
{
    const FgState* s = fgp.s;
    if (!s || !FgpArmHas()) return;
    FgPM::ShadowBeforeInject();
}
// ---- the latency waiter's three terms. The waiter does not block on the marker of a frame whose end the machine owns.
static bool FgpOwnsEnd(LONG f) { return fgp.s && FgPM::OwnsEnd(f); }
// 1: taken or deferred, and the waiter's own push stands aside.
struct FgpSink
{
    float *wP, *wH, *wAge, *wQ, *wL; int* wn; int cap;
    void Take(const FgPend& e, LONGLONG te)
    {
        if (*wn >= cap) return;
        const int i = *wn;
        wP[i] = e.p; wH[i] = e.h; wAge[i] = (float)LatMs(te - e.b); wQ[i] = e.q; wL[i] = e.l;
        *wn = i + 1;
    }
};
static void FgpLawCutPoll();
static int FgpWaiterSample(LONG fe, LONG fp, bool valid, LONGLONG b, float p, float h, float q, float l,
                           float* wP, float* wH, float* wAge, float* wQ, float* wL, int* wn, int cap)
{
    if (!fgp.s || !FgpArmHas()) return 0;
    FgpLawCutPoll();                                               // one load outside the wait
    FgpSink sink = { wP, wH, wAge, wQ, wL, wn, cap };
    return FgPM::Route(FgpArmOf()->route, fe, fp, valid, b, p, h, q, l, sink);
}
// The deferred samples go, counted "not back".
static void FgpWaiterResync()
{
    if (!fgp.s || !FgpArmHas()) return;
    FgRoute& r = FgpArmOf()->route;
    DEVSTAT(r.notBack += r.pendN;) r.pendN = 0;
}
// Once a window on the waiter: the plan, the conditions, then the law (which writes the want).
static void FgpLawWindowStep(LONG latWin, float mP, float mAge);
static void FgpArmWindowStep(LONG latWin, float mP, float mAge, float mH, const float* wP, int wn)
{
    (void)latWin; (void)mAge; (void)mH;
    if (!fgp.s || !FgpArmHas()) return;
    FgpPlanStep();
    {
        FgCondIn r = {}; const ULONGLONG rp = FgP::RefreshPacked();
        r.refreshNum = (int)(ULONG)(rp >> 32); r.refreshDen = (int)(ULONG)rp;
        const int hz = FgCondHzX1000(r);
        fgpFixWin[1] = FgCondFixedMeanUs(wP, wn, hz); fgpFixWin[0] = hz > 0 ? wn : 0;
    }
    FgpCondStep(nullptr, 0, mP);
    FgpLawWindowStep(latWin, mP, mAge);
}

// ---- the law
// The armed product's driver; the law itself is the pure FgDrive* in fg_cond.h. On the latency waiter only: once a window, and
// once a frame for the wait for a cut. It reads the mechanism's state, never a mode word; nothing runs per present. From the wait
// for a cut to the stand-down it holds the latency ladder at rung 1 and keeps the refresh guard parked.
// A tool reads its log lines literally.
#include "fg_ffwatch.h"
struct FgpLawSt
{
    FgDriveMem mem; FgDriveK k;
    LONG started;
    LONG keyed;
    LONG armSeen;                   // a change is a Reset or a build
    LONG presISeen, presNSeen;
    LONG placeSeen;
    LONG ladderHeld, guardHeld;
    LONG ffSaid;
    LONG rOnX10, rFreeX10, row, band, shownGen, shownN, cardMed, cardN;
    LONG eX10;                      // E leaves out the time in Present
    LONG periodUs;
    LONG medUs, lawPerUs;
    volatile LONG freeX10;
    volatile LONG card;             // is the card the limit: 1, 0, -1
    LONG inFlightX10;               // tenths of a frame
    LONG pacedSeen, sleptSeen, pacedW, sleptW;
    FgFfWatch ffw;
    FgDriveCardH cardH;
        // ---- the window's readings
    LONG cpuHave;                   // dtArmCount + 1 when the baselines were taken
    LONGLONG qpc0, presT0;
    LONG blockUs, presDone0;        // -1 none
    LONG cardWin;
        // Padding: keeps the struct's size so no .bss static behind it moves.
    char lineKeep[36];
};
static FgpLawSt fgpLaw;             // keyed under the conditions' lock
// K over a stint: the placer's kFull counts less those at the last window not ON. 0 standing by, after a Reset or a build, or
// under FG_DRIVE_K_PAIRS_MIN pairs.
DLSSIV_FGDATA static LONG fgpLawKAt[FG_K_BINS];
static int FgpLawKUs(const FgArmedPlacer& pl, bool on, bool cleared)
{
    long d[FG_K_BINS];
    for (int i = 0; i < FG_K_BINS; i++) { const LONG v = ReadAcquire(&pl.kFull[i]); d[i] = v - fgpLawKAt[i]; if (!on || cleared) fgpLawKAt[i] = v; }
    return (on && !cleared) ? FgDriveKMedUs(d, FG_K_BINS, FG_K_BIN_US) : 0;
}
// c, the band and the card are not read from a window that ramped.
static bool FgpLawRampWin() { return FgpRampWindow(); }
static int FgpLawEngagedNow(bool wantOn, LONG refusal)
{
    const FgpLawSt& L = fgpLaw;
    return FgCondEngagedOf(L.started ? 1 : 0, FgDriveWantsOn(L.mem) ? 1 : 0, wantOn ? 1 : 0, (int)refusal);
}
static int FgpLawEngagingNow()
{
    const FgpLawSt& L = fgpLaw;
    return (L.started && L.mem.state == FGDR_ENGAGING) ? 1 : 0;
}
// R_free while engaged, and whether the card is the limit, from counts kept anyway. A window an arm or a disarm fell in reads
// nothing.
static void FgpLawBlockRead()
{
    FgpLawSt& L = fgpLaw;
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    const LONG arm = dtArmCount;
    const LONGLONG pt = InterlockedCompareExchange64(&dtPresTicksSum, 0, 0);
    const LONG pd = (LONG)dtPresDone;
    L.blockUs = -1;
    if (L.cpuHave == arm + 1 && t.QuadPart > L.qpc0 && (long long)(LatMs(t.QuadPart - L.qpc0) * 1000.0) > 0)
        L.blockUs = FgDriveBlockUs(pt >= L.presT0 ? (long long)(LatMs(pt - L.presT0) * 1000.0) : -1, (int)(pd - L.presDone0));
    L.cpuHave = arm + 1; L.qpc0 = t.QuadPart; L.presT0 = pt; L.presDone0 = pd;
}
static int FgpLawFreeX10(const FgState& s, float mPms, int floorUs, bool engaged, bool window)
{
    FgpLawSt& L = fgpLaw;
    if (!window) return engaged ? (int)L.freeX10 : 0;
    FgpLawBlockRead();
    const FgRenderLine& r = s.r;
    {
        const float q = latMedQ[(ULONG)(latWindows - 1) % LAT_MAXWIN];
        const LONG paced = latPaceFrames, slept = latPaceSleptN;
        L.inFlightX10 = latWindows > 0 ? (LONG)(q * 10.0f + 0.5f) : -1;
        L.pacedW = paced - L.pacedSeen; L.sleptW = slept - L.sleptSeen; L.pacedSeen = paced; L.sleptSeen = slept;
                // only from a window the law held nothing in
        L.cardWin = FgDriveCardOf((int)L.inFlightX10, (int)L.sleptW, (int)L.pacedW, (int)L.blockUs, (int)(mPms * 1000.0f));
        L.card = FgDriveCardTake(L.cardH, (int)L.cardWin, (FgDriveHolds(L.mem) || FgpLawRampWin()) ? 1 : 0);
    }
    if (!engaged) return 0;
        // FgPace's own period: FgRenderLine::periodUs is raised to the game's where the game is slower
    L.periodUs = (LONG)(*(const volatile double*)&r.period * 1000.0);
    const FgLaw* lw = s.law;
    L.medUs = lw ? FgCondRingMedUs(lw->fast) : 0;
    L.lawPerUs = lw ? (LONG)(*(const volatile double*)&lw->per * 1000.0) : 0;
    int f = FgCondFreeX10((int)L.medUs, (int)L.periodUs, floorUs, (double)mPms, L.mem.aloneX10);
    L.freeX10 = f;
    return f;
}
// Standing by (FGC_ONSLOW), with c as the card stands.
static int FgpLawPredX10(int aloneX10)
{
    const FgpLawSt& L = fgpLaw;
    if (!L.started) return 0;
    return FgDrivePredHiX10(L.mem, aloneX10, (int)L.card);
}
// K the least c where the card is the limit or cannot be read, outside the fixed mode. The ramp keeps FgpLawPredX10.
static int FgpLawPredOnX10(int aloneX10, int fixedMode)
{
    const FgpLawSt& L = fgpLaw;
    if (!L.started) return 0;
    return FgDrivePredOnX10(L.mem, aloneX10, (int)L.card, fixedMode);
}
static unsigned FgpLawWarmLift()
{
    const FgpLawSt& L = fgpLaw;
    if (!L.started) return 0u;
    return FgDriveWarmLift(L.mem);
}
// The fixed mode's way back in.
static bool FgpLawDownBySlow()
{
    const FgpLawSt& L = fgpLaw;
    return L.started && L.mem.state == FGDR_REST && L.mem.prev == FGDR_ON && L.mem.why == FGDRW_ROW && L.mem.whyRow == FGC_SLOW;
}
// Found once, as LatReadLimiterCfg finds them: the module, the first writable cfg of five, the version, the Custom limit's ini.
static void FgpLawFfSetup()
{
    FgFfWatch& w = fgpLaw.ffw;
    memset(&w, 0, sizeof(w));
    char dir[MAX_PATH]; IvtGameDir(dir, MAX_PATH);
    char path[MAX_PATH], asi[MAX_PATH] = "";
    {
        const HMODULE ff = GetModuleHandleA("GTAIV.EFLC.FusionFix.asi");
        if (!(ff && GetModuleFileNameA(ff, asi, MAX_PATH))) asi[0] = 0;
        const char* at[3] = { "plugins\\GTAIV.EFLC.FusionFix.asi", "GTAIV.EFLC.FusionFix.asi", "scripts\\GTAIV.EFLC.FusionFix.asi" };
        for (int i = 0; i < 3 && !asi[0]; i++) { _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", dir, at[i]); if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(asi, path); }
    }
    char ffDir[MAX_PATH] = "", ffName[MAX_PATH] = "GTAIV.EFLC.FusionFix";
    if (asi[0]) { strcpy_s(ffDir, asi); if (char* s = strrchr(ffDir, '\\')) { strcpy_s(ffName, s + 1); *(s + 1) = 0; if (char* e = strrchr(ffName, '.')) *e = 0; } else ffDir[0] = 0; }
    char cands[5][MAX_PATH]; int nc = 0; char sh[MAX_PATH]; char cfgDir[MAX_PATH] = "";
    if (ffDir[0]) _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", ffDir, ffName);
    else          _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%splugins\\GTAIV.EFLC.FusionFix.cfg", dir);
    _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s%s.cfg", dir, ffName);
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, sh)))
    {
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\Rockstar Games\\GTA IV\\%s.cfg", sh, ffName);
        _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    }
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, sh))) _snprintf_s(cands[nc++], MAX_PATH, _TRUNCATE, "%s\\%s\\%s.cfg", sh, ffName, ffName);
    for (int i = 0; i < nc && !w.cfg[0]; i++)
    {
        const DWORD at = GetFileAttributesA(cands[i]);
        if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY) || (at & FILE_ATTRIBUTE_READONLY)) continue;
        strcpy_s(w.cfg, cands[i]); strcpy_s(cfgDir, cands[i]); if (char* s = strrchr(cfgDir, '\\')) *(s + 1) = 0;
    }
    int maj = -1, mnr = -1;
    { char ver[64] = ""; if (asi[0] && FileVersionOf(asi, ver, sizeof(ver)) && sscanf_s(ver, "%d.%d", &maj, &mnr) != 2) maj = mnr = -1; }
    w.ff51 = (maj < 0 || maj > 5 || (maj == 5 && mnr >= 1)) ? 1 : 0;
    if (ffDir[0]) { _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s.ini", ffDir, ffName); if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(w.ini, path); }
    if (!w.ini[0] && cfgDir[0]) { _snprintf_s(path, MAX_PATH, _TRUNCATE, "%sGTAIV.EFLC.FusionFix.ini", cfgDir); if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(w.ini, path); }
    const char* inis[2] = { "plugins\\GTAIV.EFLC.FusionFix.ini", "GTAIV.EFLC.FusionFix.ini" };
    for (int i = 0; i < 2 && !w.ini[0]; i++) { _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s", dir, inis[i]); if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) strcpy_s(w.ini, path); }
    char now[48]; const int on = FgFfWatchStart(w, latFfCap, latFfRel); FgFfText(w.cap, w.rel, now, sizeof(now));
    if (!w.cfg[0]) LogRecord("  frame generation law: FusionFix's cfg is not watched: none was found in the five places FusionFix looks");
    else if (!on) LogRecord("  frame generation law: FusionFix's cfg is not watched: it reads the limiter as %s where the start read %d fps, Custom -%d (%s), and a second reader that disagrees at the start is not trusted later",
                            now, latFfCap, latFfRel, w.cfg);
    else LogRecord("  frame generation law: FusionFix's cfg is watched for its limiter, once a window by its write time: %s (the ini %s), the limiter %s",
                   w.cfg, w.ini[0] ? w.ini : "none", now);
}
// Except where the start's limiter stood frame generation by (FGC_FFCAP): FgPace's owner was decided from that read, so lifting
// it would leave nobody pacing until a restart.
static void FgpLawFfApply(FgCondIn& in)
{
    const FgFfWatch& w = fgpLaw.ffw;
    if (!w.on) return;
    const FgCondK k = FgCondDefaults();
    if (FgCondHolds(FGC_FFCAP, in, k)) return;
    const LONG p = *(const volatile LONG*)&w.packed;
    in.ffCap = (int)(p & 0xFFFF); in.ffRel = (int)((ULONG)p >> 16);
}
// Once a window: one or two GetFileAttributesExA.
static void FgpLawFfStep()
{
    FgpLawSt& L = fgpLaw;
    const int was = L.ffw.cap, wasRel = L.ffw.rel;
    if (!FgFfWatchStep(L.ffw)) return;
    char a[48], b[48]; FgFfText(L.ffw.cap, L.ffw.rel, a, sizeof(a)); FgFfText(was, wasRel, b, sizeof(b));
    FgCondIn s = {}; s.refreshNum = 1; { const ULONGLONG rp = FgP::RefreshPacked(); s.refreshNum = (int)(ULONG)(rp >> 32); s.refreshDen = (int)(ULONG)rp; }
    s.lockPm = FG_LOCK_PM; s.ffCap = latFfCap; s.ffRel = latFfRel;
    const bool held = FgCondHolds(FGC_FFCAP, s, FgCondDefaults());
    LogRecord("  frame generation law: FusionFix's frame limiter changed in its own menu: %s, was %s%s", a, b,
              held ? "; THE LIMITER OF THE START BOUND, SO FRAME GENERATION STAYS STANDING BY UNTIL THE GAME IS STARTED AGAIN (its pacing was handed to FusionFix then)" : "");
}
// Once, under the conditions' lock.
static void FgpLawCondKeys(FgCondK& k)
{
    if (fgpLaw.keyed) return;
    fgpLaw.keyed = 1;
    FgShipCondKeys(k, FgpShipKeys());
}
// The law owns the ladder from the wait for a cut to the stand-down.
static bool FgpLawOwnsLadder()
{
    return fgp.s != nullptr && FgpArmHas() && fgpLaw.started && FgDriveHolds(fgpLaw.mem);
}
static void FgpLawHold(bool hold)
{
    FgpLawSt& L = fgpLaw;
    if (hold)
    {
        if (!L.ladderHeld) { L.ladderHeld = 1; AdRung(1, "frame generation's law holds the ladder from the wait for a cut to the stand-down: rung 1 (same frame)"); }
        if (latGuard) { latGuard = false; L.guardHeld = 1; }
    }
    else
    {
        L.ladderHeld = 0;
        if (L.guardHeld) { L.guardHeld = 0; latGuard = rg.capTicks != 0; }
    }
}
// After the law's reading and before its step: the target published while nothing is generated, and the window reader for c.
static void FgpRampStep(LONG latWin, const FgCondIn& in)
{
    if (!FgpArmHas()) return;
    FgpArm& m = *FgpArmOf(); FgArmedState& a = m.a;
    (void)FgpRampWindow();
    if (!in.engaged && in.realFpsX10 > 0)
    {
        const FgCondK& ck = m.condK;
        const int outX10 = FgCondFloorX10(in, ck) - ck.rateHystX10;
        WriteRelease(&a.k.rampCapUs, (LONG)FgRampCapUs(outX10));
        int which = 0; (void)FgDriveCUs(fgpLaw.mem, (int)fgpLaw.card, &which);
        WriteRelease(&a.k.rampAskUs, (LONG)FgRampTargetUs(in.realFpsX10, FgpLawPredX10(in.realFpsX10), which != 0 ? 1 : 0, (int)fgpLaw.card, outX10));
        WriteRelease(&a.k.rampDrainOk, fgpLaw.card > 0 ? 1 : 0);
    }
    (void)latWin;
}
static const char* FgpLawCardWord() { return fgpLaw.card > 0 ? "is the limit" : fgpLaw.card == 0 ? "has idle room" : "cannot be read"; }
static void FgpLawRate(char* b, size_t cap, LONG x10) { if (x10 > 0) _snprintf_s(b, cap, _TRUNCATE, "%ld.%ld", (long)(x10 / 10), (long)(x10 % 10)); else strcpy_s(b, cap, "none"); }
// One line at log=1. at: the window after which it changed.
static void FgpLawSay(LONG at, unsigned now)
{
    FgpLawSt& L = fgpLaw; const FgDriveMem& d = L.mem;
    char on[16], fr[16]; FgpLawRate(on, sizeof(on), L.rOnX10); FgpLawRate(fr, sizeof(fr), L.rFreeX10);
    int cw = 0; const int cUs = FgDriveCUs(d, (int)L.card, &cw);
    LogRecord("  frame generation law: %s -> %s behind window %ld: %s (%s); R_on %s, R_free %s, shown %ld of %ld, on the card %ld (%ld);"
              " rest %d, back-off %d (the next %d), the cut wait %d ms; engages %d, stand-downs %d; the added time %d us a real frame (%s), the card %s;"
              " its margin %d us (the spread of its stint's samples: an engage asks the rate with both);"
              " the come-back's hold %d s (short stints stood down by FGC_SLOW or FGC_FAST running: %d)",
              FgDriveStateName(d.prev), FgDriveStateName(d.state), (long)at, FgDriveWhyName(d.why), FgCondEnumName(d.whyRow), on, fr,
              (long)L.shownGen, (long)L.shownN, (long)L.cardMed, (long)L.cardN, d.restLeft, d.backoffLeft, d.backoffNext,
              d.state == FGDR_ENGAGING ? L.k.cutWaitMs : d.state == FGDR_ON ? (int)(now - d.engT0) : 0, d.engages, d.downs,
              cUs, FgDriveCWhy(cw, (int)L.card, FgDriveCBound(d, (int)L.card)), FgpLawCardWord(), FgDriveCErrUs(d, (int)L.card),
              (FgDriveHoldLeftMs(d, now) + 999) / 1000, d.flapRun);
        // FusionFix's limiter above 1.03 C: said once.
    if (d.state == FGDR_ON && !L.ffSaid && FgpArmHas())
    {
        const FgCondIn& in = FgpArmOf()->condIn; const int ff = FgCondFfFpsX10(in), cf = FgCondCFpsX10(in);
        if (ff > 0 && cf > 0)
        {
            L.ffSaid = 1;
            LogRecord("  frame generation law: FusionFix's frame limiter is at %d fps, above 1.03 C (%d.%d): while engaged frame generation's own pacer holds the game under it, and the limiter does not bind",
                      ff / 10, (int)((long long)cf * 103 / 100) / 10, (int)((long long)cf * 103 / 100) % 10);
        }
    }
}
// Warnings lines, once a session each: the fixed mode's first engage, or the floor refusing the fixed screen; the long stretch
// with no engage; fg_min_fps over what this screen allows. Refreshes in whole numbers (59.94 reads 60), the half with its tenth
// (75 Hz: 37.5).
DLSSIV_FGDATA static FgShipLong fgpLong;
static void FgpLongSay(const FgCondIn& in, const FgCondK& k, int row, bool on)
{
    const int most = FgShipFloorUnreachable(in, k);
    if (most > 0 && FgxSayOnce2(FGX_SAID2_FLOOR))
        IvtWarn("fg_min_fps=%d in DLSS-IV.cfg is more than this display allows: at %d Hz frame generation runs the game at up to %d fps, so it never switches on. Set fg_min_fps to %d or less, or to 0 for the default",
                k.minRealFpsX10 / 10, (FgCondHzX1000(in) + 500) / 1000, most, most);
    const int say = FgShipLongStep(fgpLong, in, k, row, on ? 1 : 0, GameMenuUp() ? 1 : 0, GetTickCount());
    if (!say || !FgxSayOnce2(FGX_SAID2_LONG)) return;
    char b[400];
    if (FgShipLongLine(say, in, k, b, sizeof(b)) > 0) IvtWarn("%s", b);
}
static void FgpFixWarnStep(const FgCondIn& in, const FgCondK& k, int row, bool on)
{
    FgpLongSay(in, k, row, on);
    FgpBetterSay(in, k, row, on);
    const int kind = FgShipFixWarn(in, k, row, on ? 1 : 0);
    if (!kind) return;
    const int hz = FgCondHzX1000(in), hzW = (hz + 500) / 1000;
    if (kind == 1 && FgxSayOnce(FGP_SAID_FIXED))
        IvtWarn("frame generation: the display runs at a fixed %d Hz, so the game is held to %d fps, half the refresh rate, and shown at %d fps with a generated frame between every two real frames",
                hzW, (hz + 1000) / 2000, hzW);
    if (kind == 2 && FgxSayOnce((LONG)0x80000000u))
    {
        const int h = FgCondHalfRoundX10(hz), mn = FgCondMinX10(in, k);
        char half[16];
        if (h % 10) _snprintf_s(half, sizeof(half), _TRUNCATE, "%d.%d", h / 10, h % 10); else _snprintf_s(half, sizeof(half), _TRUNCATE, "%d", h / 10);
        IvtWarn("frame generation: off on this display: it runs at a fixed %d Hz, so the game would be held to %s fps, half the refresh rate, under the %d fps frame generation needs%s",
                hzW, half, mn / 10, (h >= 300 && mn > 300) ? " (fg_min_fps=30 in DLSS-IV.cfg lowers that to 30)" : "");
    }
}
// After the plan's and the conditions' steps. mP: the window's median real frame time.
static void FgpLawWindowStep(LONG latWin, float mP, float mAge)
{
    FgpLawSt& L = fgpLaw; FgpArm& m = *FgpArmOf(); const FgState& s = *fgp.s;
    const unsigned now = GetTickCount();
    if (!L.started)
    {
        L.started = 1;
        L.k = FgDriveDefaults();
        FgDriveInit(L.mem, L.k);
        FgpLawFfSetup();
        L.armSeen = dtArmCount; L.presISeen = s.c.presentsI; L.presNSeen = s.c.presentsN; L.placeSeen = ReadAcquire(&m.a.w.placeN);
        const FgCondK& ck = m.condK;
        char held[96] = "";
        if (FgCondFloorX10(m.condIn, ck) != FgCondMinX10(m.condIn, ck)) _snprintf_s(held, sizeof(held), _TRUNCATE, "; THE FLOOR IS HELD under this display's top: fg_min_fps asked for %d", FgCondMinX10(m.condIn, ck) / 10);
        LogRecord("  frame generation law: the windows start: the law drives this session; the floor %d (out under %d), the top %d (engaged over %d), the dwell %d window(s),"
                  " the cut wait %d ms, the rest %d window(s), the back-off %d doubling to %d behind a health exit;"
                  " behind a stand-down by FGC_SLOW or FGC_FAST the rest %d window(s) and no back-off (the prediction and the dwell bring it back), the dwell held %d s behind the second short stint running stood down so, %d s from the third;"
                  " on a fixed screen with vsync the switch-on comes at once behind the dwell (no wait for a cut) and the first does not wait for the CPU placement;"
                  " the health exit under %d of 100 shown or the card's median outside %d to %d, %d windows running;"
                  " a stint's first %d window(s) its warm-up (no health exit, no rate row stands it down);"
                  " the added time on the card c, R_on = 1 / (1/R + c): measured by every stint over its windows %d to %d and remembered until a Reset or a build, 0 where none is (the first engage is optimistic)%s",
                  FgCondFloorX10(m.condIn, ck) / 10, (FgCondFloorX10(m.condIn, ck) - ck.rateHystX10) / 10, FgCondTopX10(m.condIn, ck) / 10, (FgCondTopX10(m.condIn, ck) + ck.rateHystX10) / 10,
                  L.k.dwellWin, L.k.cutWaitMs, L.k.restWin, L.k.backoffWin, L.k.backoffMaxWin, L.k.quickRestWin, L.k.flapHoldMs / 1000, L.k.flapHoldMaxMs / 1000, L.k.shownMinPc, L.k.cardLoPm, L.k.cardHiPm, L.k.sickWin, L.k.warmWin, L.k.warmWin + 1, L.k.warmWin + L.k.costWin, held);
    }
    FgpLawFfStep();
        // ---- the reading (copied under the lock: a build may store the facts at any time)
    FgCondIn in; AcquireSRWLockShared(&m.condLock); in = m.condIn; ReleaseSRWLockShared(&m.condLock);
    L.rOnX10 = mP > 0.1f ? (LONG)(10000.0f / mP) : 0;
    L.rFreeX10 = in.engaged ? L.freeX10 : 0;
    { const int eUs = (int)*(const volatile LONG*)&s.r.lawEUs; L.eX10 = (in.engaged && eUs > 0) ? (LONG)(10000000 / eUs) : 0; }
    FgDriveIn di = {};
    di.row = (int)ReadAcquire(&m.refusal);
    di.band = FgDriveBandOf(in, m.condK);
    {
        const LONG pi = s.c.presentsI, pn = s.c.presentsN;
        di.shownGen = (int)(pi - L.presISeen); di.shownN = (int)(pn - L.presNSeen);
        L.presISeen = pi; L.presNSeen = pn;
    }
    {
        const LONG pn = ReadAcquire(&m.a.w.placeN);
        di.cardN = FgDrivePlaceMed(m.a.w.placeV, pn, L.placeSeen, &di.cardMed);
        L.placeSeen = pn;
    }
    { const LONG arm = dtArmCount; di.cleared = arm != L.armSeen ? 1 : 0; L.armSeen = arm; }
    if (di.cleared) { const FgDriveCardH z = {}; L.cardH = z; }   // a Reset or a build restarts the card's readings, as c does
    di.kUs = FgpLawKUs(m.a.pl, L.mem.state == FGDR_ON && L.mem.warmLeft == 0, di.cleared != 0);
    di.cpuReady = (cpuPlBusy ? 0 : *(const volatile int*)&cpuPlState) != 0 ? 1 : 0;
    di.realX10 = FgCondAloneX10(in); di.card = (int)L.card; di.lawPerUs = in.engaged ? (int)L.lawPerUs : 0;
    di.floorUs = in.engaged ? FgCondFloorUs(in) : 0; di.ramp = FgpLawRampWin() ? 1 : 0;   // the pacer's floor is never c
    di.perUs = in.engaged ? (int)L.periodUs : 0;
    di.fixed = FgCondFixedMode(in) ? 1 : 0;
    if (in.engaged)
    {
        di.heldUpX10 = FgDriveHeldUpX10((int)L.medUs, (int)L.periodUs, FgCondFloorUs(in), (double)mP);
        di.fastX10 = FgDriveFastX10(in, m.condK);
    }
    L.row = di.row; L.band = di.band; L.shownGen = di.shownGen; L.shownN = di.shownN; L.cardMed = di.cardMed; L.cardN = di.cardN;
    FgpRampStep(latWin, in);
    (void)mAge;
        // ---- the law, its line, what it holds, and the want
    if (FgDriveStep(L.mem, di, L.k, now, (int)statCamJumps, GameMenuUp() ? 1 : 0)) FgpLawSay(latWin, now);
    FgpLawHold(FgDriveHolds(L.mem));
    FgpArmWant(FgDriveWantsOn(L.mem) ? FG_WANT_ON : FG_WANT_PASS);
    FgpFixWarnStep(in, m.condK, di.row, FgDriveWantsOn(L.mem));
}
// Once a frame: outside the wait one load and a test.
static void FgpLawCutPoll()
{
    FgpLawSt& L = fgpLaw;
    if (L.mem.state != FGDR_ENGAGING) return;
    const unsigned now = GetTickCount();
    if (!FgDriveCut(L.mem, L.k, now, (int)statCamJumps, GameMenuUp() ? 1 : 0)) return;
    FgpArmWant(FG_WANT_ON);
    FgpLawSay(latWindows, now);
}
// On the worker: a real frame's write with its generated frame's and the one before gives the machine that pair's place.
static void FgpTsTake(int kind, LONG frame, UINT64 head, UINT64 tail)
{
    if (!FgpArmHas()) return;
    FgpArm& m = *FgpArmOf();
    double d1 = 0.0, d2 = 0.0; LONG place = 0; bool both = false;
    const bool said = FgvTsPaceTake(m.tsPace, m.ts, kind, frame, tail, &d1, &d2, &place, &both);
    if (said && both) FgPM::ArmedPlaceTake(frame, place, (LONG)(d1 + d2 + 0.5));
    (void)head;
}
