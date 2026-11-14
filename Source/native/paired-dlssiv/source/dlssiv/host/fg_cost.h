// The cost marks: GPU time of a frame generation pair split into AMD's prepare and dispatch, on frame generation's own lists; the
// helper's half of tools\test-fg-cost\run.py. Development builds only, on when the serve timer is (--log=2 or --gpu_probe=1) with
// --fg=1. Arguments: --fgcost_pad=K (K full-screen copies between stamps of their own; 1, 2, 4, 8 must lie on a line through
// zero), --fgcost_bar=0 (no barrier before the closing stamp), --fgcost_csv=PATH, --fgcost_head=1 (a barrier before the head
// stamp too), --fgcost_pre=K (a control: copies in a list right before each prepare).
// A bracket does not hold a stranger's work: with work in flight before the prepare, even with
// the card as the limit, the wait shows in the execute-to-first-stamp gap instead.
// Four stamps a list: 0 the head; 1 behind AMD's work, after a UAV barrier (without one a stamp behind a compute dispatch on a
// simultaneous-access texture does not wait for it); 2 behind the pad; 3 at once behind 2, the empty bracket (about zero).
// A dispatch is named by its count since the last reset, since AMD's library does different work by it (v2.3.0): 0 copies only,
// 1 uses the game's vectors alone, 2 to 11 bring optical flow in, 12 or more steady (FGC_STEADY).
// CPU times: AMD's configure, recording, and Close plus ExecuteCommandLists, and the gap to the first stamp via the queue's clock
// calibration. Stamps are read when their ring slot is reused, so GPU figures trail by the ring's depth; nothing waits.
// Lines: ON, ARMED (or NOT ARMED and why), one every 64 pairs and one at a set's end, each starting "frame generation cost:",
// said from FgOnG after the pair's answer. The closing line is the teardown's alone.
// fg_host.h's rules hold: one pointer (FgHost::cost), nothing static, entry points not inlined. The line builder (FgtLine, FgtAdd)
// shared with fg_own.h and fg_cap.h is at this file's top.
#pragma once

// One line, built in pieces.
struct FgtLine { char b[4096]; int at; };
static void FgtAdd(FgtLine* l, _Printf_format_string_ const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    const int w = _vsnprintf_s(l->b + l->at, sizeof(l->b) - (size_t)l->at, _TRUNCATE, fmt, ap);
    va_end(ap);
    l->at = w < 0 ? (int)sizeof(l->b) - 1 : l->at + w;
}

enum { FGC_STAMPS = 4 };             // a list's stamps: its head, behind AMD's work, behind the pad, and one more at once
enum { FGC_KEEP = 2048 };            // what a series keeps of a set for its lines: the last so many samples
enum { FGC_EVERY = 64 };             // pairs between two lines
enum { FGC_CSV_BYTES = 1 << 16 };    // the rows wait here for the next line (about 250 pairs' worth)
enum { FGC_STEADY = 12 };            // a dispatch is steady from the 12th after one that reset

// THE SERIES. One number a list each, in microseconds.
enum
{
    FGS_PREP, FGS_PREP_RESET,                       // GPU: AMD's prepare, and one that was told to reset
    FGS_DISP, FGS_DISP_RESET, FGS_DISP_FIRST,       // GPU: AMD's dispatch: steady (the 12th after a reset or later), told to reset, the first after one that reset,
    FGS_DISP_EARLY,                                 //      and the 2nd to the 11th after one
    FGS_PAD, FGS_EMPTY,                             // GPU: the pad's copies, and two stamps with nothing between them
    FGS_GAP_PREP, FGS_GAP_DISP,                     // ExecuteCommandLists returning to the list's first stamp
    FGS_CPU_CFG,                                    // CPU: AMD's configure
    FGS_CPU_PBEGIN, FGS_CPU_PREC, FGS_CPU_PEXEC,    // CPU, the prepare's list: the ring's slot and the resets, AMD's recording, Close and Execute
    FGS_CPU_DBEGIN, FGS_CPU_DREC, FGS_CPU_DEXEC,    // CPU, the dispatch's list: the same three
    FGS_N_
};
struct FgcSeries { UINT64 n, atLine; float v[FGC_KEEP]; };   // samples of this set so far; that count at the last line; a ring of the last ones

// What a slot of the ring remembers of the list that went to the queue from it, until its stamps are read.
struct FgcSlot
{
    bool     timed;                  // a list of an armed set was executed from this slot and its stamps are not read yet
    BYTE     kind, reset;            // FGC_PREPARE or FGC_DISPATCH; it was told to reset
    int      since;                  // a dispatch: how many dispatches after the last one that reset it is (0: it reset itself)
    int      pad;                    // the pad's copies recorded in it
    UINT64   n;                      // the pair
    LONGLONG exec;                   // the clock when ExecuteCommandLists returned
    float    cpuCfg, cpuBegin, cpuRec, cpuExec;
};
// What a set's marks hold. Made by FgCostArm, zeroed whole by FgCostDisarm: nothing of one set's numbers reaches the next.
struct FgCostSet
{
    bool   armed;
    ID3D12QueryHeap* heap;
    ID3D12Resource*  readback;       // FGC_STAMPS timestamps for each slot of the ring
    ID3D12Resource*  padTex;         // the pad's own texture: the presented image is copied into it
    UINT64   gpuFreq;
    LONGLONG qpcFreq;
    bool     calOk; UINT64 calGpu, calCpu;   // the queue's clock calibration, taken at the arm and at every line
    // the list being recorded
    bool     open;                   // its head is stamped
    int      cur;                    // its slot
    LONGLONG clk[FGC_CLK_N_];        // FgCostClock's three
    LONGLONG tHead, tClose;          // the clock when the head's stamp was recorded, and when the resolve was
    int      sinceReset;             // dispatches of this set since the last one that was told to reset (FGC_STEADY and more: steady)
    FgcSlot  slot[FG_RING];
    // what the lines count
    UINT64   pairs, pairsAtLine, generated, noFrame;
    UINT64   lists, notTimed, rowsLost;
    FgcSeries s[FGS_N_];
        // The stranger (--fgcost_pre=K), last: its own list, allocators, unread texture and fence, so a control touches nothing of the
    // helper's ring or allocator fence.
    ID3D12Resource*            preTex;
    ID3D12GraphicsCommandList* preList;
    ID3D12CommandAllocator*    preAlloc[FG_RING];
    ID3D12Fence*               preFence;
    UINT64   preValue, preSlot[FG_RING];     // the last value signalled, and the one that retires each allocator's last list
    UINT64   preLists;
};
struct FgCost
{
    int    pad;                      // --fgcost_pad=K; never changes
    bool   bar;                      // a barrier before the stamp that closes AMD's work (off only with --fgcost_bar=0)
    bool   headBar;                  // a barrier before a list's head stamp too (only with --fgcost_head=1)
    int    pre;                      // --fgcost_pre=K: the stranger's copies before each prepare's list; never changes
    HANDLE csv;                      // --fgcost_csv=PATH, opened once, or null
    char*  rows; int rowsAt;         // the rows not yet written
    float  sort[FGC_KEEP];           // where a series is put in order for its three numbers
    FgCostSet s;
};

static double FgcUs(const FgCostSet& s, LONGLONG ticks) { return (double)ticks * 1e6 / (double)s.qpcFreq; }
static void FgcAdd(FgCostSet& s, int which, double us)
{
    FgcSeries& r = s.s[which];
    r.v[r.n % FGC_KEEP] = (float)us; r.n++;
}
static void FgcCalibrate(FgCostSet& s)
{
    s.calOk = h.queue && SUCCEEDED(h.queue->GetClockCalibration(&s.calGpu, &s.calCpu));
}
// A series' middle, least and 99th percentile, in words: of the samples since the last line, or of the set's (the last
// FGC_KEEP of them, and the words say so when there were more). "none" where there are none.
static const char* FgcStat(FgCost* c, int which, bool whole, char* buf, size_t cap)
{
    const FgcSeries& r = c->s.s[which];
    const UINT64 all = whole ? r.n : r.n - r.atLine;
    const UINT64 count = all > FGC_KEEP ? FGC_KEEP : all;
    if (!count) { strcpy_s(buf, cap, "none"); return buf; }
    float* v = c->sort;
    for (UINT64 i = 0; i < count; i++) v[i] = r.v[(r.n - 1 - i) % FGC_KEEP];
    for (UINT64 gap = count / 2; gap > 0; gap /= 2)          // a shell sort: 2048 numbers at most, once a line
        for (UINT64 i = gap; i < count; i++)
        {
            const float x = v[i]; UINT64 j = i;
            for (; j >= gap && v[j - gap] > x; j -= gap) v[j] = v[j - gap];
            v[j] = x;
        }
    char of[48] = "";
    if (all > count) _snprintf_s(of, sizeof(of), _TRUNCATE, "the last %llu of ", (unsigned long long)count);
    _snprintf_s(buf, cap, _TRUNCATE, "p50 %.1f min %.1f p99 %.1f (%s%llu)", v[count / 2], v[0], v[(size_t)(0.99 * (double)(count - 1))], of, (unsigned long long)all);
    return buf;
}

// ---- The rows ----
// One a list, kept in memory and written when a line is said: no file is written between two of a list's clocks. A row that finds
// the buffer full is lost and counted, never written late.
static void FgcRowsWrite(FgCost* c)
{
    if (!c->csv || !c->rowsAt) return;
    DWORD w = 0;
    WriteFile(c->csv, c->rows, (DWORD)c->rowsAt, &w, nullptr);
    c->rowsAt = 0;
}
// Built with the shared line builder and put here as bytes: a formatter of the marks' own pulled _vsnprintf_s out of line into
// the development helper.
static void FgcRow(FgCost* c, const FgtLine* l)
{
    if (!c->csv || !c->rows) return;
    if (l->at <= 0 || c->rowsAt + l->at > FGC_CSV_BYTES) { c->s.rowsLost++; return; }
    memcpy(c->rows + c->rowsAt, l->b, (size_t)l->at);
    c->rowsAt += l->at;
}

// ---- A slot's stamps, read ----
// The caller knows the slot has retired. A set of four not in order (unwritten or disjoint) is not timed and counted, never averaged.
static void FgcCollect(FgCost* c, int slot)
{
    FgCostSet& s = c->s;
    FgcSlot& r = s.slot[slot];
    if (!r.timed) return;
    r.timed = false;
    UINT64 t[FGC_STAMPS] = {};
    const SIZE_T at = (SIZE_T)slot * FGC_STAMPS * sizeof(UINT64);
    const D3D12_RANGE rr = { at, at + sizeof(t) };
    void* p = nullptr;
    if (!s.readback || FAILED(s.readback->Map(0, &rr, &p)) || !p) { s.notTimed++; return; }
    memcpy(t, (const BYTE*)p + at, sizeof(t));
    const D3D12_RANGE none = { 0, 0 };
    s.readback->Unmap(0, &none);
    if (t[1] <= t[0] || t[2] < t[1] || t[3] < t[2]) { s.notTimed++; return; }
    const double k = 1e6 / (double)s.gpuFreq;
    const double gpuUs = (double)(t[1] - t[0]) * k, midUs = (double)(t[2] - t[1]) * k, emptyUs = (double)(t[3] - t[2]) * k;
    const bool disp = r.kind == FGC_DISPATCH;
    FgcAdd(s, disp ? (r.since == 0 ? FGS_DISP_RESET : r.since == 1 ? FGS_DISP_FIRST : r.since < FGC_STEADY ? FGS_DISP_EARLY : FGS_DISP) : (r.reset ? FGS_PREP_RESET : FGS_PREP), gpuUs);
    if (r.pad > 0) FgcAdd(s, FGS_PAD, midUs); else FgcAdd(s, FGS_EMPTY, midUs);   // with no pad, nothing is between stamps 1 and 2 either
    FgcAdd(s, FGS_EMPTY, emptyUs);
    double gapUs = -1.0; LONGLONG headQpc = 0;
    if (s.calOk)
    {
        headQpc = (LONGLONG)s.calCpu + (LONGLONG)((double)(LONGLONG)(t[0] - s.calGpu) * (double)s.qpcFreq / (double)s.gpuFreq);
        gapUs = FgcUs(s, headQpc - r.exec);
        FgcAdd(s, disp ? FGS_GAP_DISP : FGS_GAP_PREP, gapUs);
    }
    if (!disp) FgcAdd(s, FGS_CPU_CFG, r.cpuCfg);
    FgcAdd(s, disp ? FGS_CPU_DBEGIN : FGS_CPU_PBEGIN, r.cpuBegin);
    FgcAdd(s, disp ? FGS_CPU_DREC : FGS_CPU_PREC, r.cpuRec);
    FgcAdd(s, disp ? FGS_CPU_DEXEC : FGS_CPU_PEXEC, r.cpuExec);
    if (c->csv)
    {
        FgtLine l; l.at = 0; l.b[0] = 0;
        FgtAdd(&l, "%llu,%c,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%lld,%lld\r\n", (unsigned long long)r.n, disp ? 'D' : 'P', (int)r.reset, disp ? r.since : -1, r.pad,
               gpuUs, r.pad > 0 ? midUs : 0.0, emptyUs, gapUs, (double)r.cpuCfg, (double)r.cpuBegin, (double)r.cpuRec, (double)r.cpuExec, (long long)r.exec, (long long)headQpc);
        FgcRow(c, &l);
    }
}

// ---- The line ----
// Every 64 pairs (of those pairs) and at the set's end (of the set), with sample counts and the empty bracket beside the numbers.
static void FgcSay(FgCost* c, bool whole)
{
    FgCostSet& s = c->s;
    char a[96], b[96], d[96], e[96], f[96], g[96];
    FgtLine l; l.at = 0; l.b[0] = 0;
    if (whole) FgtAdd(&l, "frame generation cost: at the set's end, its %llu pairs", (unsigned long long)s.pairs);
    else       FgtAdd(&l, "frame generation cost: the last %llu pairs (%llu of this set so far)", (unsigned long long)(s.pairs - s.pairsAtLine), (unsigned long long)s.pairs);
    FgtAdd(&l, ", %ux%u format %u, guides %ux%u, HUDLESS %s; %llu dispatched and %llu with no frame in this set", g_fg->dispW, g_fg->dispH, (unsigned)g_fg->fmt, h.width, h.height,
           !g_fg->img[FEED_FG_HUDLESS] ? "none" : g_fg->hudEmpty ? "empty" : "own", (unsigned long long)s.generated, (unsigned long long)s.noFrame);
    if (!s.pairs) FgtAdd(&l, ": NO NUMBER (no pair was seen).");
    else
    {
        FgtAdd(&l, ": GPU us: prepare %s; reset prepare %s; steady dispatch (the %dth after a reset or later) %s; reset dispatch %s; first dispatch after a reset (no optical flow search) %s;"
                   " dispatches 2 to %d after a reset %s", FgcStat(c, FGS_PREP, whole, a, sizeof(a)), FgcStat(c, FGS_PREP_RESET, whole, b, sizeof(b)), FGC_STEADY, FgcStat(c, FGS_DISP, whole, d, sizeof(d)),
               FgcStat(c, FGS_DISP_RESET, whole, e, sizeof(e)), FgcStat(c, FGS_DISP_FIRST, whole, f, sizeof(f)), FGC_STEADY - 1, FgcStat(c, FGS_DISP_EARLY, whole, g, sizeof(g)));
        FgtAdd(&l, "; execute to first stamp us: prepare %s, dispatch %s%s", FgcStat(c, FGS_GAP_PREP, whole, a, sizeof(a)), FgcStat(c, FGS_GAP_DISP, whole, b, sizeof(b)),
               s.calOk ? "" : " (the queue's clock calibration was refused)");
        FgtAdd(&l, "; CPU us: configure %s; prepare's begin %s, record %s, close and execute %s", FgcStat(c, FGS_CPU_CFG, whole, a, sizeof(a)), FgcStat(c, FGS_CPU_PBEGIN, whole, b, sizeof(b)),
               FgcStat(c, FGS_CPU_PREC, whole, d, sizeof(d)), FgcStat(c, FGS_CPU_PEXEC, whole, e, sizeof(e)));
        FgtAdd(&l, "; dispatch's begin %s, record %s, close and execute %s", FgcStat(c, FGS_CPU_DBEGIN, whole, a, sizeof(a)), FgcStat(c, FGS_CPU_DREC, whole, b, sizeof(b)),
               FgcStat(c, FGS_CPU_DEXEC, whole, d, sizeof(d)));
        FgtAdd(&l, "; not timed %llu of %llu lists; empty bracket us %s", (unsigned long long)s.notTimed, (unsigned long long)s.lists, FgcStat(c, FGS_EMPTY, whole, a, sizeof(a)));
        if (c->pad > 0) FgtAdd(&l, "; pad of %d copies us %s", c->pad, FgcStat(c, FGS_PAD, whole, a, sizeof(a)));
        if (!c->bar) FgtAdd(&l, "; NO BARRIER before the closing stamps (--fgcost_bar=0: the barrier's control)");
        if (c->headBar) FgtAdd(&l, "; a barrier before the head stamps too (--fgcost_head=1)");
        if (c->pre > 0) FgtAdd(&l, "; stranger: %d copies before each prepare's list, %llu lists (--fgcost_pre: a control)", c->pre, (unsigned long long)s.preLists);
        if (s.rowsLost) FgtAdd(&l, "; %llu rows LOST to a full buffer", (unsigned long long)s.rowsLost);
    }
    LogRecord("%s", l.b);
    for (int k = 0; k < FGS_N_; k++) s.s[k].atLine = s.s[k].n;
    s.pairsAtLine = s.pairs;
}

// ---- Asked for, armed, disarmed ----
// From main, once. The state is made only when the marks are on and frame generation's state exists; never freed. Off with no
// argument of theirs, nothing is said; an argument given where they cannot run is said.
__declspec(noinline) static void FgCostAsk(int pad, int bar, const char* csv, int head, int pre)
{
    const bool on = g_logLevel >= 2 || g_gpuProbe;
    const bool asked = pad > 0 || !bar || (csv && csv[0]) || head || pre > 0;
    if (!g_fg || !on)
    {
        if (asked) LogRecord("frame generation cost: asked for (--fgcost_pad=%d, --fgcost_bar=%d, --fgcost_csv %s) and OFF: %s", pad, bar, (csv && csv[0]) ? "given" : "not given",
                             !g_fg ? "this helper has no frame generation state (it was not started with --fg=1, or there was no memory for it): the marks live on frame generation's lists"
                                   : "the marks are on with --log=2 or more, or with --gpu_probe=1, as the serve timer is");
        return;
    }
    FgCost* c = (FgCost*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgCost));
    if (!c) { LogRecord("frame generation cost: no memory for the marks' state (%lu bytes): OFF for this process", (unsigned long)sizeof(FgCost)); return; }
    c->pad = pad > 0 ? (pad > 64 ? 64 : pad) : 0; c->bar = bar != 0;
    c->headBar = head != 0; c->pre = pre > 0 ? (pre > 256 ? 256 : pre) : 0;
    if (csv && csv[0])
    {
        c->rows = (char*)HeapAlloc(GetProcessHeap(), 0, FGC_CSV_BYTES);
        const HANDLE f = c->rows ? CreateFileA(csv, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
        if (f == INVALID_HANDLE_VALUE) LogRecord("frame generation cost: the rows' file would not open (%lu): no rows are written, the lines are", GetLastError());
        else
        {
            c->csv = f;
            LARGE_INTEGER size = {};
            if (GetFileSizeEx(f, &size) && size.QuadPart == 0)
            {
                FgtLine l; l.at = 0; l.b[0] = 0;
                FgtAdd(&l, "n,kind,reset,since,pad,gpu_us,pad_us,empty_us,gap_us,cpu_cfg_us,cpu_begin_us,cpu_rec_us,cpu_exec_us,exec_qpc,head_qpc\r\n");
                FgcRow(c, &l);
            }
            FgcRowsWrite(c);
        }
    }
    g_fg->cost = c;
    LogRecord("frame generation cost: the marks are ON (this helper's log level %d, --gpu_probe=%d): four GPU stamps on each of frame generation's own lists and the CPU's clock round AMD's calls;"
              " one line every %d pairs and one at a set's end; a barrier before the stamp that closes AMD's work: %s; the pad: %d full-screen copies in each dispatch's list; rows: %s."
              " DEVELOPMENT MARKS: each list carries four timestamp queries, one resolve%s more than a product list",
              g_logLevel, g_gpuProbe, FGC_EVERY, c->bar ? "yes" : "NO (--fgcost_bar=0, the barrier's control)", c->pad, c->csv ? "written" : "none",
              c->bar ? " and one barrier" : "");
        // Two lines of their own, each only when its argument was given.
    if (c->headBar) LogRecord("frame generation cost: head barrier: yes (--fgcost_head=1)");
    if (c->pre > 0) LogRecord("frame generation cost: stranger: %d copies before each prepare's list (--fgcost_pre: a control)", c->pre);
}

// ---- The stranger (--fgcost_pre=K): a control ----
// K full-screen copies of the presented image into a texture nothing reads, in a list of the marks' own, executed right before a
// prepare's list, with nothing that makes them complete. The prepare's bracket does not hold them. Its allocators follow the ring's
// slots and are held to a fence of its own: a prepare that failed behind a stranger leaves no helper fence value behind it.
static void FgcStrangerRelease(FgCostSet& s)
{
    if (s.preFence && s.preValue) FgWaitFence(s.preFence, s.preValue, 2000);     // its last list retires before its allocators go
    if (s.preList)  s.preList->Release();
    for (int i = 0; i < FG_RING; i++) if (s.preAlloc[i]) s.preAlloc[i]->Release();
    if (s.preFence) s.preFence->Release();
    if (s.preTex)   s.preTex->Release();
}
static HRESULT FgcStrangerMake(FgCostSet& s)
{
    D3D12_HEAP_PROPERTIES tp = {}; tp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = g_fg->dispW; td.Height = g_fg->dispH; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = g_fg->fmt; td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    HRESULT hr = h.dev->CreateCommittedResource(&tp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&s.preTex);
    for (int i = 0; i < FG_RING && SUCCEEDED(hr); i++) hr = h.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&s.preAlloc[i]);
    if (SUCCEEDED(hr)) hr = h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.preAlloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&s.preList);
    if (SUCCEEDED(hr)) hr = s.preList->Close();
    if (SUCCEEDED(hr)) hr = h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&s.preFence);
    return hr;
}
// One stranger, recorded and executed NOW: the caller is at a prepare's head, so the prepare's own list follows it on the queue.
static void FgcStranger(FgCost* c, int slot)
{
    FgCostSet& s = c->s;
    if (!s.preList || !g_fg->img[FEED_FG_PRESENT]) return;
    if (s.preSlot[slot] && !FgWaitFence(s.preFence, s.preSlot[slot], 2000)) return;
    if (FAILED(s.preAlloc[slot]->Reset()) || FAILED(s.preList->Reset(s.preAlloc[slot], nullptr))) return;
    for (int k = 0; k < c->pre; k++) s.preList->CopyResource(s.preTex, g_fg->img[FEED_FG_PRESENT]);
    if (FAILED(s.preList->Close())) return;
    ID3D12CommandList* lists[] = { s.preList };
    h.queue->ExecuteCommandLists(1, lists);
    s.preSlot[slot] = ++s.preValue;
    h.queue->Signal(s.preFence, s.preValue);
    s.preLists++;
}

// The set's end, from FgTeardown: the caller has drained the queue, so every slot's stamps are final and read here, the closing
// line is said, and everything goes behind the same drain. Safe when never armed: every pointer is null.
__declspec(noinline) static void FgCostDisarm()
{
    FgCost* c = g_fg->cost;
    if (!c) return;
    FgCostSet& s = c->s;
    if (s.armed)
    {
        for (int i = 0; i < FG_RING; i++) FgcCollect(c, i);
        FgcSay(c, true);
        FgcRowsWrite(c);
    }
    FgcStrangerRelease(s);           // the stranger's own objects, its last list waited for first
    if (s.padTex)   s.padTex->Release();
    if (s.readback) s.readback->Release();
    if (s.heap)     s.heap->Release();
    memset(&s, 0, sizeof(s));
}

// A set was built ('g' answered CREATED): arm all objects or none, and say so in one line; marks that could not arm say why, once
// a set.
__declspec(noinline) static void FgCostArm()
{
    FgCost* c = g_fg->cost;
    if (!c || !g_fg->built) return;
    FgCostDisarm();                  // nothing is armed here (the teardown before this create saw to it); kept so a second arm cannot leak
    FgCostSet& s = c->s;
    D3D12_QUERY_HEAP_DESC qd = {};
    qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = FG_RING * FGC_STAMPS;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = FG_RING * FGC_STAMPS * sizeof(UINT64); rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    const HRESULT hq = h.dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&s.heap);
    const HRESULT hb = SUCCEEDED(hq) ? h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&s.readback) : E_ABORT;
    const HRESULT hf = SUCCEEDED(hb) ? h.queue->GetTimestampFrequency(&s.gpuFreq) : E_ABORT;
    HRESULT hpad = S_OK;
    if (SUCCEEDED(hf) && c->pad > 0)
    {   // the pad's own texture: plain, the presented image's size and format; leaving its copy state by a barrier completes the copies
        D3D12_HEAP_PROPERTIES tp = {}; tp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td = {};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = g_fg->dispW; td.Height = g_fg->dispH; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = g_fg->fmt; td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        hpad = h.dev->CreateCommittedResource(&tp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&s.padTex);
    }
    if (SUCCEEDED(hf) && SUCCEEDED(hpad) && c->pre > 0) hpad = FgcStrangerMake(s);   // (the stranger's objects, said with the pad's: the line below names one result for both)
    LARGE_INTEGER qf = {}; QueryPerformanceFrequency(&qf); s.qpcFreq = qf.QuadPart;
    if (FAILED(hq) || FAILED(hb) || FAILED(hf) || FAILED(hpad) || !s.gpuFreq || !s.qpcFreq)
    {
        LogRecord("frame generation cost: NOT ARMED for this set (query heap 0x%08X, read-back 0x%08X, timestamp frequency 0x%08X, the pad's texture 0x%08X): nothing else is affected",
                  (unsigned)hq, (unsigned)hb, (unsigned)hf, (unsigned)hpad);
        FgCostDisarm();
        return;
    }
    FgcCalibrate(s);
    s.armed = true;
    LogRecord("frame generation cost: ARMED on a %ux%u set of format %u over guides of %ux%u, HUDLESS %s: %d stamps on each of the ring's %d lists, GPU timestamps at %llu Hz, the CPU's clock at %lld Hz%s%s",
              g_fg->dispW, g_fg->dispH, (unsigned)g_fg->fmt, h.width, h.height, !g_fg->img[FEED_FG_HUDLESS] ? "none" : g_fg->hudEmpty ? "EMPTY (--fghud_empty)" : "own", FGC_STAMPS, FG_RING,
              (unsigned long long)s.gpuFreq, (long long)s.qpcFreq, s.padTex ? "; the pad's texture was had" : "", s.calOk ? "" : "; the queue's clock calibration was refused: no gap is said");
}

// ---- In FgOnQ and FgBeginCommands: the CPU's clock ----
// Three places (FGC_CLK_*). The one at FgBeginCommands' first line also drops a list whose recording was abandoned: its head was
// stamped and nothing may close it.
__declspec(noinline) static void FgCostClock(int which)
{
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed) return;
    if (which == FGC_CLK_BEGIN) c->s.open = false;
    QueryPerformanceCounter((LARGE_INTEGER*)&c->s.clk[which]);
}

// ---- In the two lists (FgPrepareSubmit, FgDispatchSubmit) and at their end (FgEndCommands) ----
// The head: FgBeginCommands succeeded, so this slot's last list has retired and its stamps are read first. The clock is read on the
// way in and on the way out, so this function's own work is in neither.
__declspec(noinline) static void FgCostHead(int kind, bool reset)
{
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed) return;
    FgCostSet& s = c->s;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    const int slot = g_fg->slot;
    FgcCollect(c, slot);
    FgcSlot& r = s.slot[slot];
    memset(&r, 0, sizeof(r));
    r.kind = (BYTE)kind; r.reset = reset ? 1 : 0;
        // A set's first dispatch always resets (the helper sees to it).
    r.since = kind != FGC_DISPATCH ? 0 : reset ? 0 : s.sinceReset + 1;
    r.n = kind == FGC_DISPATCH ? g_fg->gN : g_fg->qN;
    r.cpuBegin = (float)FgcUs(s, q.QuadPart - s.clk[FGC_CLK_BEGIN]);
    r.cpuCfg = kind == FGC_PREPARE ? (float)FgcUs(s, s.clk[FGC_CLK_CFG1] - s.clk[FGC_CLK_CFG0]) : 0.0f;
    s.cur = slot; s.open = true;
    if (kind == FGC_PREPARE && c->pre > 0) FgcStranger(c, slot);   // the stranger (a control): its own list, on the queue before this one
    if (c->headBar)
    {   // --fgcost_head=1: the closing stamp's barrier before the head stamp too; at a desk it changed nothing
        D3D12_RESOURCE_BARRIER br = {};
        br.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        g_fg->list->ResourceBarrier(1, &br);
    }
    g_fg->list->EndQuery(s.heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(slot * FGC_STAMPS));
    QueryPerformanceCounter(&q); s.tHead = q.QuadPart;
}
// Behind AMD's call, which returned OK: the clock (end of AMD's recording), then the barrier and the stamp closing AMD's work, the
// pad between two stamps of its own, and the empty bracket.
__declspec(noinline) static void FgCostAfterAmd()
{
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed || !c->s.open) return;
    FgCostSet& s = c->s;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    FgcSlot& r = s.slot[s.cur];
    r.cpuRec = (float)FgcUs(s, q.QuadPart - s.tHead);
    ID3D12GraphicsCommandList* const l = g_fg->list;
    const UINT base = (UINT)(s.cur * FGC_STAMPS);
    if (c->bar)
    {   // every unordered access before this point completes before anything behind it: the stamp waits for AMD's last pass
        D3D12_RESOURCE_BARRIER br = {};
        br.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        l->ResourceBarrier(1, &br);
    }
    l->EndQuery(s.heap, D3D12_QUERY_TYPE_TIMESTAMP, base + 1u);
    if (r.kind == FGC_DISPATCH && c->pad > 0 && s.padTex)
    {
        D3D12_RESOURCE_BARRIER br = {};
        br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = s.padTex; br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        br.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON; br.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        l->ResourceBarrier(1, &br);
                // The presented image is simultaneous-access and left in COMMON by AMD's passes: a copy source with no barrier (as in fg_cap.h).
        for (int k = 0; k < c->pad; k++) l->CopyResource(s.padTex, g_fg->img[FEED_FG_PRESENT]);
        br.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; br.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        l->ResourceBarrier(1, &br);
        r.pad = c->pad;
    }
    l->EndQuery(s.heap, D3D12_QUERY_TYPE_TIMESTAMP, base + 2u);
    l->EndQuery(s.heap, D3D12_QUERY_TYPE_TIMESTAMP, base + 3u);
}
// BEFORE Close: the four stamps resolved into this slot's place, then the clock (the start of "close and execute").
__declspec(noinline) static void FgCostBeforeClose()
{
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed || !c->s.open) return;
    FgCostSet& s = c->s;
    g_fg->list->ResolveQueryData(s.heap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(s.cur * FGC_STAMPS), FGC_STAMPS, s.readback, (UINT64)s.cur * FGC_STAMPS * sizeof(UINT64));
    QueryPerformanceCounter((LARGE_INTEGER*)&s.tClose);
}
// BEHIND ExecuteCommandLists: the list went to the queue. Only now is the slot "timed": a list that failed to close
// never comes here, and its slot is not read.
__declspec(noinline) static void FgCostAfterExecute()
{
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed || !c->s.open) return;
    FgCostSet& s = c->s;
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    FgcSlot& r = s.slot[s.cur];
    r.exec = q.QuadPart; r.cpuExec = (float)FgcUs(s, q.QuadPart - s.tClose);
    r.timed = true; s.lists++;
    if (r.kind == FGC_DISPATCH) s.sinceReset = r.since > 60000 ? 60000 : r.since;
    s.open = false;
}

// ---- After 'G' ----
// The pair is counted, and every 64 pairs the line is said and the rows written. Called by FgOnG once fgOut is queued, so the game
// is released before this thread writes a file.
__declspec(noinline) static void FgCostAfterG(bool generated)
{
    // (a pair is one 'G': every 'Q' has exactly one, and a pair the game closed with no frame is a pair all the same)
    FgCost* c = g_fg->cost;
    if (!c || !c->s.armed) return;
    FgCostSet& s = c->s;
    s.pairs++;
    if (generated) s.generated++; else s.noFrame++;
    if (s.pairs % FGC_EVERY != 0) return;
    FgcSay(c, false);
    FgcRowsWrite(c);
    FgcCalibrate(s);
}
