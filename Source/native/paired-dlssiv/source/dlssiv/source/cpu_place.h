// CPU placement, once the game world has run a few seconds (CpuPlaceTick, from IvtTick on the render thread, any log level).
// 1. The pin: on a CPU with two or more last-level caches where the chosen one holds at least six cores (a two-CCD Ryzen), the
//    game and the helper get that cache's processors as their default CPU set: the larger cache on an X3D part, else the one
//    holding the lowest-numbered processors. Smaller caches (Zen 2's CCX, a laptop's efficient cluster) are not pinned to.
// 2. The wall: the threads that set the frame rate each get one logical processor nothing else of the game or helper uses:
//    the render thread, the device thread, DXVK's command thread ("dxvk-cs") and the game's main thread (IvtUnpinMainThread).
//    One logical processor each, its SMT sibling left to the rest. Two fastest-kind cores are always kept for everything else,
//    and nothing is walled under six logical processors. Fastest-kind cores (EfficiencyClass), in one cache where they fit,
//    never processor 0's core, which takes the interrupts.
// Stands down, and says so once: under Wine (CPU sets are stubs that report success), without CPU sets, on more than 32 logical
// processors, with unpin=0 (the switch for all of the mod's CPU handling), and when another program set the game's or the
// helper's CPU set or narrowed their affinity: checked before writing and every few seconds after, when the mod takes back
// what it wrote. Needs the transport (the world is counted by its injections). Group 0 only.
// The wall has its own key, cpu_place, off by default; the pin stays with unpin. Stutter was reported with the wall on at a
// frame cap the machine reaches: a thread held to one logical processor cannot move when something else lands there.
#pragma once
#include <tlhelp32.h>

// CPU PLAN START (tools/test-cpu-place/gen.py cuts from here to CPU PLAN END and holds the plan against made-up CPUs)
struct CpuPlSet { ULONG id; BYTE lp, core, llc, eff; bool usable; };
struct CpuPlLlc { BYTE llc; DWORD kb; };
struct CpuPlPlan
{
    int pin;                 // the pinned cache's index in llcs[], or -1
    int pinCores;
    int domCores;            // cores in the domain (the pinned cache, or every usable one)
    int fastCores;           // of those, the cores of the fastest kind
    int domLps;              // logical processors in the domain
    int nwall;               // processors walled, one per thread, in the threads' order
    ULONG wallId[4]; BYTE wallLp[4];
    ULONG proc[64]; ULONG procN;   // the default CPU set: the domain less the walled processors
};
// A core's key: its cache and its index (CoreIndex numbers cores within the system, the cache keeps two caches' cores apart).
static inline bool CpuPlSameCore(const CpuPlSet& a, const CpuPlSet& b) { return a.core == b.core && a.llc == b.llc; }
static void CpuPlPlanFor(const CpuPlSet* s, int n, const CpuPlLlc* llcs, int nl, int threads, CpuPlPlan* p)
{
    p->pin = -1; p->pinCores = 0; p->domCores = 0; p->fastCores = 0; p->domLps = 0; p->nwall = 0; p->procN = 0;
    if (threads > 4) threads = 4;
    // each cache's cores and lowest processor
    int cores[8] = {}; BYTE lp0[8]; for (int k = 0; k < 8; k++) lp0[k] = 255;
    if (nl > 8) nl = 8;
    for (int i = 0; i < n; i++)
    {
        if (!s[i].usable) continue;
        int k = 0; while (k < nl && llcs[k].llc != s[i].llc) k++;
        if (k == nl) continue;
        bool seen = false; for (int j = 0; j < i; j++) if (s[j].usable && CpuPlSameCore(s[j], s[i])) { seen = true; break; }
        if (!seen) cores[k]++;
        if (s[i].lp < lp0[k]) lp0[k] = s[i].lp;
    }
        // The pin: two caches or more, the larger (on a tie the lower-numbered), only if it holds six cores
    if (nl >= 2)
    {
        int best = -1;
        for (int k = 0; k < nl; k++)
            if (cores[k] && (best < 0 || llcs[k].kb > llcs[best].kb || (llcs[k].kb == llcs[best].kb && lp0[k] < lp0[best]))) best = k;
        if (best >= 0 && cores[best] >= 6) { p->pin = best; p->pinCores = cores[best]; }
    }
    const auto inDom = [&](const CpuPlSet& c) { return c.usable && (p->pin < 0 || c.llc == llcs[p->pin].llc); };
    BYTE effTop = 0;
    for (int i = 0; i < n; i++)
    {
        if (!inDom(s[i])) continue;
        p->domLps++;
        if (s[i].eff > effTop) effTop = s[i].eff;
        bool seen = false; for (int j = 0; j < i; j++) if (inDom(s[j]) && CpuPlSameCore(s[j], s[i])) { seen = true; break; }
        if (!seen) p->domCores++;
    }
    for (int i = 0; i < n; i++)
    {
        if (!inDom(s[i]) || s[i].eff != effTop) continue;
        bool seen = false; for (int j = 0; j < i; j++) if (inDom(s[j]) && CpuPlSameCore(s[j], s[i])) { seen = true; break; }
        if (!seen) p->fastCores++;
    }
        // The reserve: two fast cores kept for the rest, and no wall under six logical processors
    int slots = p->domLps < 6 ? 0 : p->fastCores - 2;
    if (slots > threads) slots = threads;
    if (slots < 0) slots = 0;
        // The wall: the first processor of each fastest-kind core, not processor 0's core, from the cache with the most candidates
    // first (so the walled threads share a cache where they fit), then the others
    const auto cand = [&](int i) -> bool
    {
        if (!inDom(s[i]) || s[i].eff != effTop) return false;
        for (int j = 0; j < n; j++) if (CpuPlSameCore(s[j], s[i]) && (j < i || s[j].lp == 0)) return false;   // its first set, not processor 0's core
        return true;
    };
    int candN[8] = {};
    for (int i = 0; i < n; i++) if (cand(i)) { int k = 0; while (k < nl && llcs[k].llc != s[i].llc) k++; if (k < nl) candN[k]++; }
    int order[8], no = 0; bool used[8] = {};
    for (int r = 0; r < nl; r++)
    {
        int best = -1;
        for (int k = 0; k < nl; k++) if (!used[k] && (best < 0 || candN[k] > candN[best])) best = k;
        if (best < 0) break;
        used[best] = true; order[no++] = best;
    }
    for (int o = 0; o < no && p->nwall < slots; o++)
        for (int i = 0; i < n && p->nwall < slots; i++)
        {
            const CpuPlSet& c = s[i];
            if (c.llc != llcs[order[o]].llc || !cand(i)) continue;
            p->wallId[p->nwall] = c.id; p->wallLp[p->nwall] = c.lp; p->nwall++;
        }
    for (int i = 0; i < n && p->procN < 64; i++)
    {
        if (!inDom(s[i])) continue;
        bool walled = false; for (int w = 0; w < p->nwall; w++) if (p->wallId[w] == s[i].id) walled = true;
        if (!walled) p->proc[p->procN++] = s[i].id;
    }
}
// CPU PLAN END

static CpuPlSet   cpuPl[64];
static int        cpuPlN = 0;
static CpuPlLlc   cpuPlLlc[8];
static int        cpuPlNl = 0;
static CpuPlPlan  cpuPlP;
static int        cpuPlState = 0;          // 0 waiting, 1 applied, 2 stood down for the session, 3 withdrawn
static volatile LONG cpuPlBusy = 0;        // 1 while CpuPlApply runs on its worker; the state and the plan are the worker's until 0
                                           // Plain volatile stores and loads (x86, /volatile:ms), never an Interlocked call beside them (C28112)
static int        cpuPlRun = 0, cpuPlDry = 0, cpuPlCheck = 0;
static LONG       cpuPlLastInj = -1;
static HANDLE     cpuPlHost = nullptr;     // the helper process the default set was last given to (ivtHost's handle, not a copy)
struct CpuPlThread { DWORD tid; HANDLE h; ULONG id; BYTE lp; bool dxvk; const char* who; };
static CpuPlThread cpuPlT[4];
static int        cpuPlTN = 0;
typedef BOOL (WINAPI* PFN_CpuPlSys)(void*, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI* PFN_CpuPlGet)(HANDLE, PULONG, ULONG, PULONG);
typedef BOOL (WINAPI* PFN_CpuPlSet)(HANDLE, const ULONG*, ULONG);
typedef HRESULT (WINAPI* PFN_CpuPlDesc)(HANDLE, PWSTR*);
static PFN_CpuPlGet cpuPlGetProc = nullptr;
static PFN_CpuPlSet cpuPlSetProc = nullptr, cpuPlSetThr = nullptr;

static void CpuPlStandDown(const char* why) { cpuPlState = 2; LogRecord("  cpu: the mod leaves the game's CPU placement alone: %s", why); }

// The size of the level 3 cache (KB) a logical processor sits under, or 0 where none is reported.
static DWORD CpuPlL3KB(BYTE lp)
{
    DWORD kb = 0, cl = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &cl);
    BYTE* const b = cl ? (BYTE*)HeapAlloc(GetProcessHeap(), 0, cl) : nullptr;
    if (b && GetLogicalProcessorInformationEx(RelationCache, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)b, &cl))
        for (DWORD off = 0; off < cl; )
        {
            const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* const e = (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(b + off);
            if (e->Relationship == RelationCache && e->Cache.Level == 3 && e->Cache.GroupMask.Group == 0 && lp < 32 && ((e->Cache.GroupMask.Mask >> lp) & 1))
                kb = e->Cache.CacheSize / 1024;
            if (!e->Size) break;
            off += e->Size;
        }
    if (b) HeapFree(GetProcessHeap(), 0, b);
    return kb;
}

// Every CPU set of group 0, and each last-level cache's size, read once. SYSTEM_CPU_SET_INFORMATION by offset (winnt.h): Size
// 0, Type 4 (0 is a CPU set), Id 8, Group 12, LogicalProcessorIndex 14, CoreIndex 15, LastLevelCacheIndex 16, EfficiencyClass
// 18, AllFlags 19 (2 allocated, 4 to this process). A set allocated to another process is left out of everything. Returns
// why it cannot place, or nullptr.
static const char* CpuPlRead()
{
    const HMODULE k = GetModuleHandleA("kernel32.dll");
    const PFN_CpuPlSys sys = k ? (PFN_CpuPlSys)GetProcAddress(k, "GetSystemCpuSetInformation") : nullptr;
    cpuPlGetProc = k ? (PFN_CpuPlGet)GetProcAddress(k, "GetProcessDefaultCpuSets") : nullptr;
    cpuPlSetProc = k ? (PFN_CpuPlSet)GetProcAddress(k, "SetProcessDefaultCpuSets") : nullptr;
    cpuPlSetThr  = k ? (PFN_CpuPlSet)GetProcAddress(k, "SetThreadSelectedCpuSets") : nullptr;
    if (!sys || !cpuPlGetProc || !cpuPlSetProc || !cpuPlSetThr) return "this Windows has no CPU sets";
    ULONG len = 0; sys(nullptr, 0, &len, GetCurrentProcess(), 0);
    if (!len || len > 65536) return "Windows did not report its CPU sets";
    BYTE* const b = (BYTE*)HeapAlloc(GetProcessHeap(), 0, len);
    if (!b) return "Windows did not report its CPU sets";
    bool over32 = false;
    if (sys(b, len, &len, GetCurrentProcess(), 0))
        for (ULONG off = 0; off + 20 <= len && cpuPlN < 64; )
        {
            const BYTE* const s = b + off; const ULONG size = *(const ULONG*)s;
            if (size < 20) break;
            if (*(const ULONG*)(s + 4) == 0 && *(const WORD*)(s + 12) == 0 && s[14] >= 32) over32 = true;
            if (*(const ULONG*)(s + 4) == 0 && *(const WORD*)(s + 12) == 0 && s[14] < 32)
            {
                CpuPlSet& c = cpuPl[cpuPlN++];
                c.id = *(const ULONG*)(s + 8); c.lp = s[14]; c.core = s[15]; c.llc = s[16]; c.eff = s[18];
                c.usable = !((s[19] & 2) && !(s[19] & 4));
                int i = 0; while (i < cpuPlNl && cpuPlLlc[i].llc != c.llc) i++;
                if (i == cpuPlNl && cpuPlNl < 8) { cpuPlLlc[cpuPlNl].llc = c.llc; cpuPlLlc[cpuPlNl].kb = CpuPlL3KB(c.lp); cpuPlNl++; }
            }
            off += size;
        }
    HeapFree(GetProcessHeap(), 0, b);
    if (over32) return "more than 32 logical processors, which a 32-bit game cannot all be given";
    return cpuPlN > 0 && cpuPlNl > 0 ? nullptr : "Windows did not report its CPU sets";
}

// Another program's hand on the placement: a default CPU set, or an affinity narrower than the system's.
static const char* CpuPlForeign(HANDLE p)
{
    ULONG ids[64]; ULONG n = 0;
    const BOOL ok = cpuPlGetProc(p, ids, 64, &n);
    if (ok ? n > 0 : GetLastError() == ERROR_INSUFFICIENT_BUFFER) return "a default CPU set";
    DWORD_PTR pm = 0, sm = 0;
    if (GetProcessAffinityMask(p, &pm, &sm) && pm != sm) return "an affinity narrower than the system's";
    return nullptr;
}

// DXVK's command-stream thread, by the name DXVK gives it (SetThreadDescription, "dxvk-cs"; Windows 10 1607 and later). A
// system-wide thread snapshot: on the apply's worker, and on the render thread only when a walled dxvk-cs ended (its device
// went, a load screen).
static DWORD CpuPlFindDxvkCs()
{
    static const PFN_CpuPlDesc desc = (PFN_CpuPlDesc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
    if (!desc) return 0;
    const DWORD pid = GetCurrentProcessId(); DWORD found = 0;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok && !found; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != pid) continue;
        const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        PWSTR w = nullptr;
        if (SUCCEEDED(desc(h, &w)) && w && !wcscmp(w, L"dxvk-cs")) found = te.th32ThreadID;
        if (w) LocalFree(w);
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

// Does this process still hold the default set the mod wrote (in any order)?
static bool CpuPlIsOurs(HANDLE p)
{
    ULONG ids[64]; ULONG n = 0;
    if (!cpuPlGetProc(p, ids, 64, &n) || n != cpuPlP.procN || !n) return false;
    for (ULONG i = 0; i < n; i++) { ULONG j = 0; while (j < cpuPlP.procN && cpuPlP.proc[j] != ids[i]) j++; if (j == cpuPlP.procN) return false; }
    return true;
}

// Is the game's default CPU set still the one written, and its affinity untouched?
static bool CpuPlStillOurs()
{
    if (!CpuPlIsOurs(GetCurrentProcess())) return false;
    DWORD_PTR pm = 0, sm = 0;
    return !(GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm) && pm != sm);
}

// Undo the placement: every walled thread's set (its handle closed), and the game's and the helper's default sets where they
// still hold what was written (never another program's).
static void CpuPlClear()
{
    for (int i = 0; i < cpuPlTN; i++) if (cpuPlT[i].h) { cpuPlSetThr(cpuPlT[i].h, nullptr, 0); CloseHandle(cpuPlT[i].h); cpuPlT[i].h = nullptr; }
    cpuPlTN = 0;
    if (CpuPlIsOurs(GetCurrentProcess())) cpuPlSetProc(GetCurrentProcess(), nullptr, 0);
    if (cpuPlHost && cpuPlHost == ivtHost && CpuPlIsOurs(cpuPlHost)) cpuPlSetProc(cpuPlHost, nullptr, 0);
}

static HANDLE CpuPlOpen(DWORD tid) { return OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, tid); }

// The decision and the writes, once the identities are known: on a worker of its own (CpuPlStart), so the thread snapshot and
// the reads never land on a frame.
static void CpuPlApply()
{
    if (!cpuPlN) if (const char* why = CpuPlRead()) { CpuPlStandDown(why); return; }
    if (const char* w = CpuPlForeign(GetCurrentProcess()))
    {
        char why[200]; _snprintf_s(why, sizeof(why), _TRUNCATE, "another program gave the game %s (XOS Toolkit, Process Lasso or a rule of"
                                   " your own); turn it off for GTA IV to let the mod place it", w);
        CpuPlStandDown(why); return;
    }
    const HANDLE host = ivtHost;
    if (host && CpuPlForeign(host)) { CpuPlStandDown("another program placed the host, dlssiv-helper.exe"); return; }
        // The threads, in the order they get a processor when fewer than four are free
    struct W { DWORD tid; const char* who; bool dxvk; } want[4] = {}; int nw = 0;
    const DWORD dxvkCs = CpuPlFindDxvkCs();
    if (renderTid) want[nw++] = { renderTid, "the render thread", false };
    if (dtWorkerTid && dtWorkerTid != renderTid) want[nw++] = { dtWorkerTid, "DLSS-IV's device thread", false };
    if (dxvkCs && dxvkCs != renderTid) want[nw++] = { dxvkCs, "DXVK's command thread", true };
    if (ivtMainTid && ivtMainTid != renderTid && ivtMainTid != dxvkCs) want[nw++] = { ivtMainTid, "the game's main thread", false };
        // At cpu_place=0 the plan gets no thread, so only the pin is left where there is a cache to pin to.
    CpuPlPlanFor(cpuPl, cpuPlN, cpuPlLlc, cpuPlNl, ivtCfg.cpu_place ? nw : 0, &cpuPlP);
    if (cpuPlP.pin < 0 && !cpuPlP.nwall)
    {
        CpuPlStandDown(!ivtCfg.cpu_place ? "cpu_place=0 gives no thread a processor of its own, and this CPU has no second cache of six cores or more to keep the game on"
                       : !nw ? "no thread to wall was found" : "too few fast cores to spare one for a thread, and no cache of six cores or more to pin to");
        return;
    }
    if (!cpuPlSetProc(GetCurrentProcess(), cpuPlP.proc, cpuPlP.procN)) { CpuPlStandDown("Windows refused the game's default CPU set"); return; }
    const bool hostOk = host && cpuPlSetProc(host, cpuPlP.proc, cpuPlP.procN);
    cpuPlHost = host;
    cpuPlTN = 0;
    char walled[400] = ""; size_t at = 0;
    for (int w = 0; w < cpuPlP.nwall && w < nw; w++)   // the plan walls no more processors than it was given threads
    {
        CpuPlThread& t = cpuPlT[cpuPlTN];
        t.tid = want[w].tid; t.who = want[w].who; t.dxvk = want[w].dxvk; t.id = cpuPlP.wallId[w]; t.lp = cpuPlP.wallLp[w];
        t.h = CpuPlOpen(t.tid);
        if (!t.h || !cpuPlSetThr(t.h, &t.id, 1)) { if (t.h) CloseHandle(t.h); t.h = nullptr; continue; }
        cpuPlTN++;
        const int k = _snprintf_s(walled + at, sizeof(walled) - at, _TRUNCATE, "%s%s on %u", at ? ", " : "", t.who, (unsigned)t.lp);
        at = k < 0 ? strlen(walled) : at + k;
    }
        // Nothing walled and no pin: the default set would only keep processors idle, so it is taken back
    if (!cpuPlTN && cpuPlP.pin < 0) { CpuPlClear(); CpuPlStandDown("Windows refused every thread's CPU set"); return; }
    cpuPlState = 1;
    const char* const hostSay = !host ? "" : hostOk ? "" : " (the host's set was refused)";
    if (cpuPlP.pin >= 0)
        LogRecord("  cpu: the game and the host on one last-level cache (%lu KB, %d cores)%s; %s%s", (unsigned long)cpuPlLlc[cpuPlP.pin].kb,
                  cpuPlP.pinCores, hostSay, cpuPlTN ? "a processor of its own for " : ivtCfg.cpu_place ? "no thread walled" : "no thread on a processor of its own (cpu_place=0)", walled);
    else
        LogRecord("  cpu: a processor of its own for %s; the rest of the game on the other %lu%s", walled, cpuPlP.procN, hostSay);
}

static DWORD WINAPI CpuPlWorker(void*) { CpuPlApply(); cpuPlBusy = 0; return 0; }
static void CpuPlStart()
{
    cpuPlBusy = 1;
    const HANDLE h = CreateThread(nullptr, 64 * 1024, CpuPlWorker, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (h) CloseHandle(h);
    else { CpuPlApply(); cpuPlBusy = 0; }
}

// Once a frame on the render thread (IvtTick). Nothing after it has stood down; while applied, a look every 600 frames.
static void CpuPlaceTick()
{
    if (cpuPlBusy) return;
    if (cpuPlState == 2 || cpuPlState == 3) return;
    if (cpuPlState == 0)
    {
        if (!ivtCfg.unpin) { CpuPlStandDown("unpin=0"); return; }
        if (ivtWine) { CpuPlStandDown("under Wine CPU sets do nothing"); return; }
                // The game world, counted with slack: 600 frames with injections; 60 in a row without one (a loading screen, a menu) restart
        // the count.
        const bool moved = cpuPlLastInj >= 0 && ivtInjections != cpuPlLastInj;
        cpuPlLastInj = ivtInjections;
        if (moved) cpuPlDry = 0; else if (++cpuPlDry >= 60) { cpuPlRun = 0; return; }
        if (!cpuPlRun && !moved) return;
        if (++cpuPlRun < 600 || !renderTid) return;
        CpuPlStart();
        return;
    }
        // The helper started or restarted: the same default set, unless another program gave it one
    if (ivtHost && ivtHost != cpuPlHost)
    {
        if (!CpuPlForeign(ivtHost)) cpuPlSetProc(ivtHost, cpuPlP.proc, cpuPlP.procN);
        cpuPlHost = ivtHost;
    }
    if (++cpuPlCheck < 600) return;
    cpuPlCheck = 0;
        // Another program took over: take back the threads' sets and the default sets where still ours (after an affinity change they
    // would keep the walled processors idle); leave its rule alone
    if (!CpuPlStillOurs())
    {
        CpuPlClear(); cpuPlState = 3;
        LogRecord("  cpu: another program changed the game's CPU set or affinity: the mod's placement withdrew and leaves it to that program");
        return;
    }
        // A walled thread that ended (DXVK's command thread goes with its device): the new one takes the same processor
    for (int i = 0; i < cpuPlTN; i++)
        if (cpuPlT[i].h && WaitForSingleObject(cpuPlT[i].h, 0) == WAIT_OBJECT_0)
        {
            CloseHandle(cpuPlT[i].h); cpuPlT[i].h = nullptr;
            if (!cpuPlT[i].dxvk) continue;
            const DWORD tid = CpuPlFindDxvkCs();
            if (!tid) continue;
            cpuPlT[i].tid = tid; cpuPlT[i].h = CpuPlOpen(tid);
            if (cpuPlT[i].h) cpuPlSetThr(cpuPlT[i].h, &cpuPlT[i].id, 1);
        }
}
