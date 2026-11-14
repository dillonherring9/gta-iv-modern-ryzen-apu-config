// The capture: short runs of consecutive frames with everything the library was given, saved in the game and replayed at a desk
// (tools\fg-replay): the presented picture, THE HUD-LESS PICTURE when the set has one, the prepare's depth and vectors, every
// number of the prepare and dispatch, and the generated image. Development builds only, with:
//     --fgcap=N          frames a run (2 to 32)          --fgcap_runs=R    runs a connection (1, at most 12)
//     --fgcap_gap=P      pairs between runs (600)        --fgcap_go=1      wait for go.txt in the capture's folder
//     --fgcap_dir=PATH   a full path, or one relative to the game's folder; default fg-capture beside the log
//     --fgcap_refuse=N   a desk control: 1 the video memory rule refuses, 2 the disk's
// A run begins with the helper's own reset, so a replay from a fresh context meets the same history; its first 12 frames are not
// steady. The serving thread only records copies and fills a record; the capture's own thread makes the textures, reads a whole
// run back on its own copy queue and writes the files, one run at a time.
// Bounds: video memory for one run, refused by the video memory rule unless the budget has it plus 512 MiB; the disk for R runs
// plus 1 GiB. A refusal is one line beginning "frame generation capture: OFF" and turns off only the capture.
// Layout: <folder>\<YYYYMMDD-HHMMSS>-<arm>\run-NN\ with fNN-present/gen/depth/mv/hud.raw (rows top first, no padding) and
// index.txt, written last.
// fg_host.h's rules hold: one pointer (FgHost::cap), nothing static, entry points not inlined, none called from Serve.
#pragma once

// A line of the index into the index's buffer, and a whole file written.
static void FgsIxPut(char* buf, size_t cap, size_t* at, FgtLine* l)
{
    if (l->at > 0 && *at + (size_t)l->at < cap) { memcpy(buf + *at, l->b, (size_t)l->at); *at += (size_t)l->at; }
    else if (l->at > 0) *at = cap;                                       // it did not fit: the writer says so and writes no index
    l->at = 0; l->b[0] = 0;
}
static bool FgsWriteAll(const char* path, const BYTE* data, size_t n, DWORD* err)
{
    const HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = GetLastError(); return false; }
    bool ok = true;
    for (size_t at = 0; at < n && ok; )
    {
        const DWORD want = (DWORD)(n - at < (16u << 20) ? n - at : (16u << 20)); DWORD w = 0;
        ok = WriteFile(f, data + at, want, &w, nullptr) && w == want;
        at += w;
    }
    if (!ok) *err = GetLastError();
    CloseHandle(f);
    return ok;
}

enum { FGCAP_FRAMES_MIN = 2, FGCAP_FRAMES_MAX = 32 };
enum { FGCAP_RUNS_MAX = 12 };
enum { FGCAP_GAP = 600 };
enum { FGCAP_PRESENT, FGCAP_GEN, FGCAP_HUD, FGCAP_DEPTH, FGCAP_MV, FGCAP_KINDS };
enum { FGCAP_STAMPS = 4 };           // GPU timestamps a frame: round the prepare's copies, round the dispatch's
enum { FGCAP_IX = 1 << 17 };         // the index's buffer
#define FGCAP_VRAM_MARGIN (512ull << 20)
#define FGCAP_DISK_MARGIN (1ull << 30)

struct FgCapKind
{
    bool   on;
    UINT   w, hh, fmt;               // the texture
    UINT   rowBytes, pitch;          // a row as it is written, and as a read-back buffer holds it
    UINT64 bytes, foot;              // the file, and the read-back buffer
};
// Everything one frame of a run was, beside its five pictures.
struct FgCapFrame
{
    UINT64   n, id, fence;           // the pair, AMD's frame id, the allocator fence's value behind its dispatch
    int      since;                  // dispatches since the last one that reset (0: this one reset)
    bool     prepared, dispatched;
    // what 'Q' and 'G' carried
    uint32_t qFlags, gFlags;
    float    wireJitter[2], wireFrameMs, wireNear, wireFov, wirePeak;
    // the prepare, as the library was handed it
    uint32_t prepReset, renderW, renderH;
    float    jitter[2], mvScale[2], frameMs, camNear, camFar, fovY, v2m, cam[12];
    // the dispatch
    uint32_t dispReset, hud, dbg, transfer;
    float    lum[2];
    int32_t  rect[4];
    float    usServe;                // this file's own time on the serving thread for the pair, microseconds
};
struct FgCap
{
    // ---- asked (never changes)
    int    frames, runs, gap, goWait, refuse;
    char   base[MAX_PATH];           // the capture's folder (the go file lies in it)
    char   dir[MAX_PATH];            // this process's folder under it
    char   goFile[MAX_PATH];
    char   arm[48];
    // ---- the two threads' words to each other
    volatile LONG off;               // the capture gave up for this process (a refusal said why)
    volatile LONG goSeen;            // the go file was seen (the worker's)
    volatile LONG ready;             // the textures in video memory are the armed set's (the worker's; the serving thread clears it)
    volatile LONG busy;              // a whole run is the worker's until it is written
    volatile LONG quit;
    volatile LONG written, failed;   // runs on the disk, and runs that could not be written
    CRITICAL_SECTION cs;             // round `want` and `allocSeq` alone: a set's build and the worker's allocation, never a frame
    HANDLE wake, idle, thread;
    LONG   allocSeq, allocDone;
    FgCapKind want[FGCAP_KINDS];     // the armed set's five pictures
    // ---- the textures (the worker makes and frees them; the serving thread records copies into them while `ready`)
    FgCapKind have[FGCAP_KINDS];
    ID3D12Resource* stg[FGCAP_FRAMES_MAX][FGCAP_KINDS];
    ID3D12QueryHeap* qh; ID3D12Resource* qrb; UINT64 gpuFreq;
    // ---- the worker's own
    ID3D12CommandQueue* cq; ID3D12CommandAllocator* ca; ID3D12GraphicsCommandList* cl; ID3D12Fence* cf; UINT64 cfv; HANDLE cev;
    ID3D12Resource* rb; UINT64 rbBytes;
    BYTE*  row;                      // one row, for a picture whose read-back rows are padded
    // ---- the serving thread's own
    bool   armed;                    // a set is built and its pictures are `want`
    bool   startNext;                // the next pair that is prepared with a reset is a run's frame 0
    bool   open;                     // a run is being gathered
    bool   lastReset;                // the last dispatch recorded was told to reset
    bool   exiting, saidEnd;
    int    got;                      // frames of the open run that are whole
    int    since;                    // dispatches since the last reset
    int    cool;                     // pairs to let pass before the next run may begin
    int    handed;                   // runs handed to the worker
    int    saidBroken;
    UINT64 broken;
    FgCapFrame q;                    // what the open 'Q' carried, for the frame its prepare begins
    FgCapFrame fr[FGCAP_FRAMES_MAX];
    int    jobRun; UINT64 jobFence;  // the run that is the worker's now
    double usSum, usMax; UINT64 usN; // the serving thread's time a pair of a run
    UINT   dispW, dispH, fmt; bool hdr;
};

static double FgCapUs(LONGLONG a, LONGLONG b) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)(b - a) * 1e6 / (double)f.QuadPart; }
static uint32_t FgCapBits(float v) { uint32_t u; memcpy(&u, &v, sizeof(u)); return u; }
static const char* FgCapKindName(int k) { return k == FGCAP_PRESENT ? "present" : k == FGCAP_GEN ? "gen" : k == FGCAP_HUD ? "hud" : k == FGCAP_DEPTH ? "depth" : "mv"; }
static void FgCapOff(FgCap* c) { InterlockedExchange(&c->off, 1); }
static void FgCapBarrier(ID3D12GraphicsCommandList* l, ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from; b.Transition.StateAfter = to;
    l->ResourceBarrier(1, &b);
}
// What a texture is, for a copy of the whole of it. False for one this file does not copy.
static bool FgCapDescribe(ID3D12Resource* t, FgCapKind* k)
{
    memset(k, 0, sizeof(*k));
    if (!t) return false;
    const D3D12_RESOURCE_DESC d = t->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    h.dev->GetCopyableFootprints(&d, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    if (!rows || !rowBytes || rows != d.Height || rowBytes > 0x7FFFFFFFull) return false;
    k->on = true; k->w = (UINT)d.Width; k->hh = d.Height; k->fmt = (UINT)d.Format;
    k->rowBytes = (UINT)rowBytes; k->pitch = fp.Footprint.RowPitch; k->bytes = rowBytes * rows; k->foot = total;
    return true;
}
static UINT64 FgCapFrameBytes(const FgCapKind* k) { UINT64 b = 0; for (int i = 0; i < FGCAP_KINDS; i++) if (k[i].on) b += k[i].bytes; return b; }
static bool FgCapSameKinds(const FgCapKind* a, const FgCapKind* b)
{
    for (int i = 0; i < FGCAP_KINDS; i++)
        if (a[i].on != b[i].on || (a[i].on && (a[i].w != b[i].w || a[i].hh != b[i].hh || a[i].fmt != b[i].fmt))) return false;
    return true;
}
// A folder and every folder above it that is not there yet.
static bool FgCapMakeDirs(const char* path)
{
    char p[MAX_PATH]; strcpy_s(p, MAX_PATH, path);
    for (char* s = p + 3; *s; s++)
        if (*s == '\\' || *s == '/') { const char keep = *s; *s = 0; CreateDirectoryA(p, nullptr); *s = keep; }
    return CreateDirectoryA(p, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
static bool FgCapFileThere(const char* path) { const DWORD a = GetFileAttributesA(path); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

// ---- The worker (the capture's own thread): textures, the read-back of a whole run, the files ----
static void FgCapFreeStaging(FgCap* c)
{
    for (int i = 0; i < FGCAP_FRAMES_MAX; i++) for (int k = 0; k < FGCAP_KINDS; k++) if (c->stg[i][k]) { c->stg[i][k]->Release(); c->stg[i][k] = nullptr; }
    if (c->qh) { c->qh->Release(); c->qh = nullptr; }
    if (c->qrb) { c->qrb->Release(); c->qrb = nullptr; }
    if (c->rb) { c->rb->Release(); c->rb = nullptr; }
    if (c->row) { HeapFree(GetProcessHeap(), 0, c->row); c->row = nullptr; }
    memset(c->have, 0, sizeof(c->have));
}
static ID3D12Resource* FgCapMakeBuffer(UINT64 bytes)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
    bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* b = nullptr;
    if (FAILED(h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&b))) return nullptr;
    return b;
}
// The worker's own queue, list and fence, once a process. A COPY queue: a read-back is all it ever does.
static bool FgCapMakeQueue(FgCap* c)
{
    if (c->cq) return true;
    D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    HRESULT hr = h.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&c->cq);
    if (SUCCEEDED(hr)) hr = h.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, __uuidof(ID3D12CommandAllocator), (void**)&c->ca);
    if (SUCCEEDED(hr)) hr = h.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, c->ca, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&c->cl);
    if (SUCCEEDED(hr)) { c->cl->Close(); hr = h.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&c->cf); }
    if (SUCCEEDED(hr) && !c->cev) c->cev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (FAILED(hr) || !c->cev)
    {
        if (c->cl) { c->cl->Release(); c->cl = nullptr; } if (c->ca) { c->ca->Release(); c->ca = nullptr; }
        if (c->cf) { c->cf->Release(); c->cf = nullptr; } if (c->cq) { c->cq->Release(); c->cq = nullptr; }
        return false;
    }
    return true;
}
// A fence waited for on the worker's own event (the serving thread's waits have theirs: fg_host.h, FgWaitFence, says why
// two waits must not share one).
static bool FgCapWait(FgCap* c, ID3D12Fence* f, UINT64 v, DWORD ms)
{
    const ULONGLONG t0 = GetTickCount64();
    for (;;)
    {
        if (f->GetCompletedValue() >= v) return true;
        const ULONGLONG gone = GetTickCount64() - t0;
        if (gone >= ms) return false;
        ResetEvent(c->cev);
        if (FAILED(f->SetEventOnCompletion(v, c->cev))) return f->GetCompletedValue() >= v;
        WaitForSingleObject(c->cev, (DWORD)(ms - gone));
    }
}
static UINT64 FgCapDiskFree(const char* dir)
{
    char root[MAX_PATH]; strcpy_s(root, MAX_PATH, dir);
    for (;;)
    {   // the deepest folder of the path that exists
        ULARGE_INTEGER fr = {};
        if (GetDiskFreeSpaceExA(root, &fr, nullptr, nullptr)) return fr.QuadPart;
        char* cut = strrchr(root, '\\');
        if (!cut || cut == root) return 0;
        *cut = 0;
    }
}
// The textures for one run, made when the go file was seen and a set is armed, and again when a later set's pictures differ in
// size. All or none. First the video memory rule and the disk's.
static void FgCapAlloc(FgCap* c)
{
    FgCapKind want[FGCAP_KINDS]; LONG seq;
    EnterCriticalSection(&c->cs); memcpy(want, c->want, sizeof(want)); seq = c->allocSeq; LeaveCriticalSection(&c->cs);
    FgCapFreeStaging(c);             // (`ready` is clear: the serving thread cleared it before it asked, and the queue was drained before that set was built)
    const ULONGLONG t0 = GetTickCount64();
    const UINT64 frame = FgCapFrameBytes(want), run = frame * (UINT64)c->frames, files = run * (UINT64)(c->runs - (int)c->written);
    const char* why = nullptr; char num[200] = "";
    FgVram v; const bool vRead = FgVramRead(&v);
    const UINT64 room = (vRead && v.budget > v.usage) ? v.budget - v.usage : 0;
    const UINT64 freeDisk = FgCapDiskFree(c->base);
    if ((vRead && room < run + FGCAP_VRAM_MARGIN) || c->refuse == 1)
    {
        why = c->refuse == 1 ? "the video memory rule (FORCED by --fgcap_refuse)" : "the video memory rule";
        _snprintf_s(num, sizeof(num), _TRUNCATE, "a run needs %.1f MiB and a margin of %.1f, and this process may still take %.1f MiB (a budget of %.1f, %.1f in use)",
                    run / 1048576.0, FGCAP_VRAM_MARGIN / 1048576.0, room / 1048576.0, v.budget / 1048576.0, v.usage / 1048576.0);
    }
    else if (freeDisk < files + FGCAP_DISK_MARGIN || c->refuse == 2)
    {
        why = c->refuse == 2 ? "the disk (FORCED by --fgcap_refuse)" : "the disk";
        _snprintf_s(num, sizeof(num), _TRUNCATE, "the %d run(s) still asked need %.1f MiB and a margin of %.1f, and the disk has %.1f MiB free",
                    c->runs - (int)c->written, files / 1048576.0, FGCAP_DISK_MARGIN / 1048576.0, freeDisk / 1048576.0);
    }
    else if (!FgCapMakeQueue(c)) { why = "its own copy queue could not be made"; }
    int made = 0; UINT64 largest = 0; UINT maxRow = 0;
    for (int i = 0; i < c->frames && !why; i++)
        for (int k = 0; k < FGCAP_KINDS && !why; k++)
        {
            if (!want[k].on) continue;
            D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = want[k].w; rd.Height = want[k].hh; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.Format = (DXGI_FORMAT)want[k].fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            if (FAILED(h.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&c->stg[i][k])) || !c->stg[i][k])
            {
                why = "a texture could not be made"; _snprintf_s(num, sizeof(num), _TRUNCATE, "texture %d of a run of %.1f MiB", made + 1, run / 1048576.0);
            }
            else { made++; if (want[k].foot > largest) largest = want[k].foot; if (want[k].rowBytes > maxRow) maxRow = want[k].rowBytes; }
        }
    if (!why)
    {
        D3D12_QUERY_HEAP_DESC qd = {}; qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qd.Count = (UINT)(c->frames * FGCAP_STAMPS);
        c->qrb = FgCapMakeBuffer((UINT64)c->frames * FGCAP_STAMPS * sizeof(UINT64));
        c->rb = FgCapMakeBuffer(largest); c->rbBytes = largest;
        c->row = (BYTE*)HeapAlloc(GetProcessHeap(), 0, maxRow ? maxRow : 1);
        if (FAILED(h.dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&c->qh)) || !c->qh || !c->qrb || !c->rb || !c->row || FAILED(h.queue->GetTimestampFrequency(&c->gpuFreq)) || !c->gpuFreq)
            why = "its read-back buffers or its timestamps could not be had";
    }
    if (why)
    {
        FgCapFreeStaging(c); FgCapOff(c);
        LogRecord("frame generation capture: OFF: %s%s%s: nothing is captured, nothing else is affected", why, num[0] ? ": " : "", num);
    }
    else
    {
        memcpy(c->have, want, sizeof(want));
        FgVram v1; const bool v1Read = FgVramRead(&v1);
        LogRecord("frame generation capture: textures: %d of them for a run of %d frames, %.1f MiB of video memory, made in %lu ms; this process's video memory %.1f -> %.1f MiB of a budget of %.1f%s; one read-back buffer of %.1f MiB; the disk has %.1f MiB free",
                  made, c->frames, run / 1048576.0, (unsigned long)(GetTickCount64() - t0), vRead ? v.usage / 1048576.0 : 0.0, v1Read ? v1.usage / 1048576.0 : 0.0, v1Read ? v1.budget / 1048576.0 : 0.0,
                  (vRead && v1Read) ? "" : " (not read)", largest / 1048576.0, freeDisk / 1048576.0);
    }
    EnterCriticalSection(&c->cs);
    c->allocDone = seq;
    if (!why && c->allocSeq == seq) InterlockedExchange(&c->ready, 1);   // (a set built meanwhile asks again: this one's textures are not that set's)
    LeaveCriticalSection(&c->cs);
}
// The mod's own setting lines from the shared log: the last line holding `mark`, without its line end; empty when none.
// The log is read whole, shared with its two writers.
static void FgCapLogLine(const BYTE* text, size_t n, const char* mark, char* out, size_t cap)
{
    out[0] = 0;
    size_t m = 0; while (mark[m]) m++;
    if (!text || n < m || !m) return;
    for (size_t at = n - m + 1; at-- > 0; )
    {
        if (text[at] != (BYTE)mark[0] || memcmp(text + at, mark, m) != 0) continue;
        size_t a = at, b = at;
        while (a > 0 && text[a - 1] != '\n' && text[a - 1] != '\r') a--;
        while (b < n && text[b] != '\n' && text[b] != '\r') b++;
        while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
        size_t len = b - a; if (len > cap - 1) len = cap - 1;
        for (size_t i = 0; i < len; i++) { const BYTE ch = text[a + i]; out[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?'; }
        out[len] = 0;
        return;
    }
}
// The value behind "key=" in a line, up to the next blank or comma. Empty when the key is not there.
static void FgCapValue(const char* line, const char* key, char* out, size_t cap)
{
    out[0] = 0;
    size_t m = 0; while (key[m]) m++;
    for (const char* p = line; *p; p++)
    {
        if (strncmp(p, key, m) != 0) continue;
        size_t i = 0;
        for (p += m; *p && *p != ' ' && *p != ',' && *p != ')' && i + 1 < cap; p++) out[i++] = *p;
        out[i] = 0;
        return;
    }
}
static BYTE* FgCapReadLog(size_t* n)
{
    *n = 0;
    if (!g_logPath[0]) return nullptr;
    const HANDLE f = CreateFileA(g_logPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER size = {};
    BYTE* buf = nullptr;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (256ll << 20)) buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart);
    if (buf)
    {
        size_t at = 0;
        for (;;)
        {
            const DWORD want = (DWORD)((size_t)size.QuadPart - at < (8u << 20) ? (size_t)size.QuadPart - at : (8u << 20)); DWORD got = 0;
            if (!want || !ReadFile(f, buf + at, want, &got, nullptr) || !got) break;
            at += got;
        }
        *n = at;
    }
    CloseHandle(f);
    return buf;
}
// One picture of the run out of video memory into its file: a copy on the worker's own queue into the one read-back
// buffer, waited for, mapped, written. Returns what went wrong, or null.
static const char* FgCapSavePicture(FgCap* c, ID3D12Resource* tex, const FgCapKind& k, const char* path, DWORD* err, double* msRead, double* msWrite)
{
    LARGE_INTEGER q0, q1, q2; QueryPerformanceCounter(&q0);
    if (FAILED(c->ca->Reset()) || FAILED(c->cl->Reset(c->ca, nullptr))) return "its command list would not reset";
    D3D12_RESOURCE_DESC d = tex->GetDesc();
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = c->rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    h.dev->GetCopyableFootprints(&d, 0, 1, 0, &dst.PlacedFootprint, nullptr, nullptr, nullptr);
    src.pResource = tex; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    c->cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    if (FAILED(c->cl->Close())) return "its command list would not close";
    ID3D12CommandList* lists[] = { c->cl };
    c->cq->ExecuteCommandLists(1, lists);
    const UINT64 v = ++c->cfv;
    if (FAILED(c->cq->Signal(c->cf, v)) || !FgCapWait(c, c->cf, v, 10000)) return "a read-back did not retire in 10 s";
    void* p = nullptr; const D3D12_RANGE all = { 0, (SIZE_T)k.foot };
    if (FAILED(c->rb->Map(0, &all, &p)) || !p) return "the read-back buffer would not map";
    QueryPerformanceCounter(&q1);
    bool ok;
    if (k.pitch == k.rowBytes) ok = FgsWriteAll(path, (const BYTE*)p, (size_t)k.bytes, err);
    else
    {   // padded rows: the file holds none of the padding
        const HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ok = f != INVALID_HANDLE_VALUE;
        for (UINT y = 0; y < k.hh && ok; y++)
        {
            DWORD w = 0;
            memcpy(c->row, (const BYTE*)p + (size_t)y * k.pitch, k.rowBytes);
            ok = WriteFile(f, c->row, k.rowBytes, &w, nullptr) && w == k.rowBytes;
        }
        if (!ok) *err = GetLastError();
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
    const D3D12_RANGE none = { 0, 0 }; c->rb->Unmap(0, &none);
    QueryPerformanceCounter(&q2);
    *msRead += FgCapUs(q0.QuadPart, q1.QuadPart) / 1000.0; *msWrite += FgCapUs(q1.QuadPart, q2.QuadPart) / 1000.0;
    return ok ? nullptr : "a file could not be written";
}
// A WHOLE RUN, ON THE DISK. The serving thread handed it over and touches neither its records nor its textures until
// `busy` is clear again. The index is written LAST.
static void FgCapWriteRun(FgCap* c)
{
    const ULONGLONG t0 = GetTickCount64();
    const int N = c->frames, runNo = c->jobRun;
    const char* why = nullptr; DWORD err = 0; int files = 0; UINT64 bytes = 0; double msRead = 0.0, msWrite = 0.0;
    char dir[MAX_PATH] = "";
    FgtLine* ln = (FgtLine*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgtLine));
    char* ix = (char*)HeapAlloc(GetProcessHeap(), 0, FGCAP_IX); size_t ixAt = 0;
    UINT64 ticks[FGCAP_FRAMES_MAX * FGCAP_STAMPS] = {};
    const UINT64 run = FgCapFrameBytes(c->have) * (UINT64)N;
    if (!ln || !ix) why = "no memory for the index";
    else if (!FgCapWait(c, h.fence, c->jobFence, 10000)) why = "the run's last list did not retire in 10 s";
    else if (_snprintf_s(dir, MAX_PATH, _TRUNCATE, "%s\\run-%02d", c->dir, runNo) < 0) why = "its folder's path is too long";
    else if (!FgCapMakeDirs(dir)) { err = GetLastError(); why = "its folder could not be made"; }
    else if (FgCapDiskFree(dir) < run + FGCAP_DISK_MARGIN) why = "the disk has not room for it and a margin of 1 GiB";
    if (!why)
    {   // the stamps round this run's copies, as the queue wrote them
        void* p = nullptr; const D3D12_RANGE all = { 0, (SIZE_T)N * FGCAP_STAMPS * sizeof(UINT64) };
        if (SUCCEEDED(c->qrb->Map(0, &all, &p)) && p) { memcpy(ticks, p, all.End); const D3D12_RANGE none = { 0, 0 }; c->qrb->Unmap(0, &none); }
    }
    for (int i = 0; i < N && !why; i++)
        for (int k = 0; k < FGCAP_KINDS && !why; k++)
        {
            if (!c->have[k].on) continue;
            char path[MAX_PATH];
            if (_snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\f%02d-%s.raw", dir, i, FgCapKindName(k)) < 0) { why = "a file's path is too long"; break; }
            why = FgCapSavePicture(c, c->stg[i][k], c->have[k], path, &err, &msRead, &msWrite);
            if (!why) { files++; bytes += c->have[k].bytes; }
        }
    int steady = 0;
    if (!why)
    {
        size_t logN = 0; BYTE* log = FgCapReadLog(&logN);
        char cfgLine[400], movers[400], sharp[24], glass[24];
        FgCapLogLine(log, logN, "transport: cfg sharpness=", cfgLine, sizeof(cfgLine));
        FgCapLogLine(log, logN, "left out of the pass (mv_glass=", movers, sizeof(movers));
        FgCapValue(cfgLine, "sharpness=", sharp, sizeof(sharp)); FgCapValue(cfgLine, "mv_glass=", glass, sizeof(glass));
        if (log) HeapFree(GetProcessHeap(), 0, log);
        const FgOwn* O = g_fg->own; const FgLib* L = g_fg->lib; char hex[65] = "";
        FgtAdd(ln, "DLSS-IV frame generation capture\r\nformat: 1\r\nhelper_build: " IVT_BUILD_ID " " DLSSIV_FLAVOUR "\r\n");
        FgtAdd(ln, "arm: %s\r\nlevers: %s\r\n", O ? "own (the mod's own interface)" : "stock (AMD's ffx-api)", c->arm);
        if (O && O->mod) FgtAdd(ln, "library: %s\r\nlibrary_bytes: %llu\r\nlibrary_sha256: %s\r\nlibrary_says: interface %u, stage %u, \"%s\", AMD \"%s\" id 0x%llX\r\n", O->modPath, (unsigned long long)O->modSize,
                                FglHex(O->modSha, hex), (unsigned)O->said.iface, (unsigned)O->said.stage, O->said.build, O->said.amdVersion, (unsigned long long)O->said.amdId);
        else if (L && L->modKnown) FgtAdd(ln, "library: %s\r\nlibrary_bytes: %llu\r\nlibrary_sha256: %s\r\nlibrary_says: provider \"%s\" id 0x%llX\r\n", L->modPath, (unsigned long long)L->modSize, FglHex(L->modSha, hex),
                                          g_fg->providerName, (unsigned long long)g_fg->provider);
        else FgtAdd(ln, "library: (not named: start the helper with --fgid=1)\r\nlibrary_says: provider \"%s\" id 0x%llX\r\n", g_fg->providerName, (unsigned long long)g_fg->provider);
        FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
        FgtAdd(ln, "run: %d\r\nruns_asked: %d\r\nframes: %d\r\nsteady_from: %d\r\ndisplay: %ux%u\r\nrender: %ux%u\r\npresent_format: %u\r\nhdr: %d\r\nhudless: %d\r\n", runNo, c->runs, N, (int)FGC_STEADY,
               c->have[FGCAP_PRESENT].w, c->have[FGCAP_PRESENT].hh, c->have[FGCAP_DEPTH].w, c->have[FGCAP_DEPTH].hh, c->have[FGCAP_PRESENT].fmt, c->hdr ? 1 : 0, c->have[FGCAP_HUD].on ? 1 : 0);
        FgtAdd(ln, "create_flags: 0x%X\r\nstamps_asked: %d\r\ngpu_ticks_a_second: %llu\r\n",
               (unsigned)(DLSSIV_FG_CREATE_DEPTH_INVERTED | DLSSIV_FG_CREATE_DEPTH_INFINITE | (c->hdr ? DLSSIV_FG_CREATE_HDR : 0u) | (g_fg->check ? DLSSIV_FG_CREATE_CHECK : 0u)), O ? O->stampsAsk : 0,
               (unsigned long long)c->gpuFreq);
        FgtAdd(ln, "mod_cfg_line: %s\r\nsharpness: %s\r\nmv_glass: %s\r\nmod_movers_line: %s\r\n", cfgLine[0] ? cfgLine : "(none in the helper's log)", sharp[0] ? sharp : "?", glass[0] ? glass : "?",
               movers[0] ? movers : "(none in the helper's log)");
        FgtAdd(ln, "raw: a texture's rows, top row first, no padding, little endian, as the card holds them\r\n"
                   "floats: every float of a frame line is its 32 bits in hex; the frame_plain line under it says the same in decimal and is never parsed\r\n");
        FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
        for (int i = 0; i < N; i++)
        {
            const FgCapFrame& f = c->fr[i];
            if (f.since >= (int)FGC_STEADY) steady++;
            const UINT64* t = ticks + (size_t)i * FGCAP_STAMPS;
            const double k = c->gpuFreq ? 1e6 / (double)c->gpuFreq : 0.0;
            const double gp = t[1] > t[0] ? (double)(t[1] - t[0]) * k : 0.0, gd = t[3] > t[2] ? (double)(t[3] - t[2]) * k : 0.0;
            FgtAdd(ln, "frame: i=%d n=%llu id=%llu since=%d q_flags=0x%X g_flags=0x%X prep_reset=%u disp_reset=%u render=%ux%u jitter=%08X,%08X mvscale=%08X,%08X frame_ms=%08X near=%08X far=%08X fov=%08X v2m=%08X cam=",
                   i, (unsigned long long)f.n, (unsigned long long)f.id, f.since, (unsigned)f.qFlags, (unsigned)f.gFlags, (unsigned)f.prepReset, (unsigned)f.dispReset, (unsigned)f.renderW, (unsigned)f.renderH,
                   (unsigned)FgCapBits(f.jitter[0]), (unsigned)FgCapBits(f.jitter[1]), (unsigned)FgCapBits(f.mvScale[0]), (unsigned)FgCapBits(f.mvScale[1]), (unsigned)FgCapBits(f.frameMs),
                   (unsigned)FgCapBits(f.camNear), (unsigned)FgCapBits(f.camFar), (unsigned)FgCapBits(f.fovY), (unsigned)FgCapBits(f.v2m));
            for (int j = 0; j < 12; j++) FgtAdd(ln, "%s%08X", j ? "," : "", (unsigned)FgCapBits(f.cam[j]));
            FgtAdd(ln, " hud=%u dbg=0x%X transfer=%u lum=%08X,%08X rect=%d,%d,%d,%d wire_jitter=%08X,%08X wire_frame_ms=%08X wire_near=%08X wire_fov=%08X wire_peak=%08X serve_us=%.1f gpu_us=%.1f,%.1f\r\n",
                   (unsigned)f.hud, (unsigned)f.dbg, (unsigned)f.transfer, (unsigned)FgCapBits(f.lum[0]), (unsigned)FgCapBits(f.lum[1]), f.rect[0], f.rect[1], f.rect[2], f.rect[3],
                   (unsigned)FgCapBits(f.wireJitter[0]), (unsigned)FgCapBits(f.wireJitter[1]), (unsigned)FgCapBits(f.wireFrameMs), (unsigned)FgCapBits(f.wireNear), (unsigned)FgCapBits(f.wireFov),
                   (unsigned)FgCapBits(f.wirePeak), (double)f.usServe, gp, gd);
            FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
            FgtAdd(ln, "frame_plain: i=%d pair %llu, %s%s, %d since the reset, frame time %.3f ms, vectors' scale %.6g x %.6g, jitter %.4f %.4f (the wire's %.4f %.4f), far %.4g, fov %.5f, luminance %.1f to %.1f, the capture's copies %.0f and %.0f us of the card's time\r\n",
                   i, (unsigned long long)f.n, f.dispReset ? "RESET" : "no reset", f.since >= (int)FGC_STEADY ? ", steady" : "", f.since, (double)f.frameMs, (double)f.mvScale[0], (double)f.mvScale[1],
                   (double)f.jitter[0], (double)f.jitter[1], (double)f.wireJitter[0], (double)f.wireJitter[1], (double)f.camFar, (double)f.fovY, (double)f.lum[0], (double)f.lum[1], gp, gd);
            FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
        }
        for (int i = 0; i < N; i++)
            for (int k = 0; k < FGCAP_KINDS; k++)
            {
                if (!c->have[k].on) continue;
                FgtAdd(ln, "file: f%02d-%s.raw %s %d %u %u %u %u %llu\r\n", i, FgCapKindName(k), FgCapKindName(k), i, c->have[k].w, c->have[k].hh, c->have[k].fmt, c->have[k].rowBytes / c->have[k].w,
                       (unsigned long long)c->have[k].bytes);
                FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
            }
        FgtAdd(ln, "end\r\n");
        FgsIxPut(ix, FGCAP_IX, &ixAt, ln);
        char path[MAX_PATH];
        _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\index.txt", dir);
        if (ixAt >= FGCAP_IX) why = "the index did not fit its buffer";
        else if (!FgsWriteAll(path, (const BYTE*)ix, ixAt, &err)) why = "the index could not be written";
    }
    if (why)
    {
        InterlockedIncrement(&c->failed);
        LogRecord("frame generation capture: run %d of %d NOT written: %s (%lu); %d file(s) of it were", runNo, c->runs, why, (unsigned long)err, files);
    }
    else
    {
        InterlockedIncrement(&c->written);
        LogRecord("frame generation capture: run %d of %d written: %d frames, pairs %llu to %llu, %d of them steady, %d files and 1 index, %.1f MiB, %lu ms (read back %.0f ms, written %.0f ms), in %s",
                  runNo, c->runs, N, (unsigned long long)c->fr[0].n, (unsigned long long)c->fr[N - 1].n, steady, files, (double)bytes / 1048576.0, (unsigned long)(GetTickCount64() - t0), msRead, msWrite, dir);
    }
    if (ix) HeapFree(GetProcessHeap(), 0, ix);
    if (ln) HeapFree(GetProcessHeap(), 0, ln);
}
static DWORD WINAPI FgCapWorker(void* arg)
{
    FgCap* c = (FgCap*)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;)
    {
        const bool waiting = c->goWait && !c->goSeen && !c->off;
        WaitForSingleObject(c->wake, waiting ? 250 : INFINITE);
        if (c->quit) break;
        if (waiting && FgCapFileThere(c->goFile))
        {
            DeleteFileA(c->goFile);
            InterlockedExchange(&c->goSeen, 1);
            LogRecord("frame generation capture: go: the go file was seen and removed: the runs begin");
        }
        if ((c->goSeen || !c->goWait) && !c->off && c->allocSeq != c->allocDone && !c->busy) FgCapAlloc(c);
        if (c->busy)
        {
            FgCapWriteRun(c);
            InterlockedExchange(&c->busy, 0);
            SetEvent(c->idle);
        }
    }
    return 0;
}

// ---- The serving thread's side ----
// Asked for from main, once, after the probe. With no argument of its own nothing is made or said.
__declspec(noinline) static void FgCapAsk(int frames, int runs, int gap, int go, const char* dir, int refuse)
{
    if (frames <= 0 && runs <= 0 && gap < 0 && !go && !refuse && !(dir && dir[0])) return;
    if (frames <= 0) { LogRecord("frame generation capture: OFF: --fgcap_runs, --fgcap_gap, --fgcap_go, --fgcap_dir or --fgcap_refuse without --fgcap=N: nothing is captured"); return; }
    if (!g_fg) { LogRecord("frame generation capture: OFF: asked for (--fgcap=%d) with no frame generation state (--fg=1 was not given, or no memory)", frames); return; }
    if (g_fg->cap) return;
    FgCap* c = (FgCap*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgCap));
    if (!c) { LogRecord("frame generation capture: OFF: no memory for its state (%lu bytes)", (unsigned long)sizeof(FgCap)); return; }
    c->frames = frames < FGCAP_FRAMES_MIN ? FGCAP_FRAMES_MIN : frames > FGCAP_FRAMES_MAX ? FGCAP_FRAMES_MAX : frames;
    c->runs = runs <= 0 ? 1 : runs > FGCAP_RUNS_MAX ? FGCAP_RUNS_MAX : runs;
    c->gap = gap >= 0 ? gap : FGCAP_GAP;
    c->goWait = go != 0; c->refuse = refuse;
        // The arm's name, from the levers that change what the library is handed: "mvright", with "-hudempty" for that lever.
    strcpy_s(c->arm, sizeof(c->arm), g_fg->hudEmpty ? "mvright-hudempty" : "mvright");
    // WHERE: a full path as it is; a path with no drive and no ".." from the folder above the helper's; else beside the log
    const char* why = nullptr;
    if (dir && dir[0])
    {
        const bool full = (dir[1] == ':' && (dir[2] == '\\' || dir[2] == '/')) || (dir[0] == '\\' && dir[1] == '\\');
        bool plain = dir[0] != '\\' && dir[0] != '/';
        for (const char* p = dir; *p; p++) if (*p == ':' || (p[0] == '.' && p[1] == '.')) plain = false;
        if (full) strcpy_s(c->base, MAX_PATH, dir);
        else if (plain)
        {
            char up[MAX_PATH]; strcpy_s(up, MAX_PATH, g_exeDir);       // g_exeDir ends with its backslash
            size_t n = 0; while (up[n]) n++;
            if (n) up[n - 1] = 0;
            if (char* cut = strrchr(up, '\\')) cut[1] = 0; else why = "the helper's folder has no folder above it";
            if (!why && _snprintf_s(c->base, MAX_PATH, _TRUNCATE, "%s%s", up, dir) < 0) why = "its folder's path is too long";
        }
        else why = "--fgcap_dir is neither a full path nor a plain path with no drive and no \"..\"";
    }
    else
    {
        char at[MAX_PATH]; strcpy_s(at, MAX_PATH, g_logPath[0] ? g_logPath : g_exeDir);
        if (g_logPath[0]) { char* cut = strrchr(at, '\\'); char* cut2 = strrchr(at, '/'); if (cut2 > cut) cut = cut2; if (cut) cut[1] = 0; else at[0] = 0; }
        if (_snprintf_s(c->base, MAX_PATH, _TRUNCATE, "%sfg-capture", at) < 0) why = "its folder's path is too long";
    }
    for (char* p = c->base; *p; p++) if (*p == '/') *p = '\\';
    { size_t n = 0; while (c->base[n]) n++; while (n > 3 && c->base[n - 1] == '\\') c->base[--n] = 0; }
    SYSTEMTIME t; GetLocalTime(&t);
    if (!why && (_snprintf_s(c->dir, MAX_PATH, _TRUNCATE, "%s\\%04u%02u%02u-%02u%02u%02u-%s", c->base, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, c->arm) < 0
              || _snprintf_s(c->goFile, MAX_PATH, _TRUNCATE, "%s\\go.txt", c->base) < 0)) why = "its folder's path is too long";
    { size_t n = 0; while (c->dir[n]) n++; if (!why && n > MAX_PATH - 40) why = "its folder's path is too long"; }   // written out: a first strlen reference would reorder the C runtime's data, which prove-fg-off --dev holds
    if (!why && !FgCapMakeDirs(c->base)) why = "its folder could not be made";
    if (!why)
    {
        InitializeCriticalSection(&c->cs);
        c->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr); c->idle = CreateEventW(nullptr, TRUE, TRUE, nullptr);
        bool stale = false;
        if (c->goWait && FgCapFileThere(c->goFile)) { DeleteFileA(c->goFile); stale = true; }   // before the worker starts: a go file already there is stale
        if (c->wake && c->idle) c->thread = CreateThread(nullptr, 0, FgCapWorker, c, 0, nullptr);
        if (!c->thread) why = "its thread could not be started";
        else if (stale) LogRecord("frame generation capture: go: a go file older than this helper was removed: it is not this leg's word");
    }
    if (why)
    {
        LogRecord("frame generation capture: OFF: %s (--fgcap_dir=%s): nothing is captured", why, dir && dir[0] ? dir : "");
        HeapFree(GetProcessHeap(), 0, c);
        return;
    }
    g_fg->cap = c;
    LogRecord("frame generation capture: asked: %d frames a run, %d run(s), %d pairs between runs; a run begins with a reset of the helper's own, at a steady pair (the %dth dispatch after a reset or later); %s; folder %s",
              c->frames, c->runs, c->gap, (int)FGC_STEADY, c->goWait ? "no run before the go file (go.txt in the capture's folder)" : "the first run at once (no go file is waited for)", c->base);
}

static void FgCapBroken(FgCap* c, UINT64 n, const char* why)
{
    c->broken++;
    if (c->saidBroken++ < 5) LogRecord("frame generation capture: run broken at pair %llu after %d of %d frames: %s: it starts again", (unsigned long long)n, c->got, c->frames, why);
    c->open = false; c->got = 0;
}
// The closing line, once: what was asked, what is on the disk, what the serving thread paid.
static void FgCapSayEnd(FgCap* c, const char* when)
{
    if (c->saidEnd) return;
    c->saidEnd = true;
    LogRecord("frame generation capture: %s: %d run(s) written of %d asked (%d handed to the writer, %d not written), %llu broken, %d frame(s) of an open run dropped; go %s; the serving thread: %.1f us a pair of a run, %.1f at most, over %llu pair(s)%s",
              when, (int)c->written, c->runs, c->handed, (int)c->failed, (unsigned long long)c->broken, c->open ? c->got : 0,
              !c->goWait ? "not waited for" : c->goSeen ? "seen" : "NEVER SEEN: no run could begin", c->usN ? c->usSum / (double)c->usN : 0.0, c->usMax, (unsigned long long)c->usN,
              c->off ? "; THE CAPTURE GAVE UP (its OFF line above says why)" : "");
}

// A set was built (FgBuild): its five pictures are described, and when they differ from the textures', the worker is asked for new
// ones. One line says what a frame and a run take.
__declspec(noinline) static void FgCapArm()
{
    FgCap* c = g_fg->cap;
    if (!c || c->off || !g_fg->built) return;
    c->armed = false; c->open = false; c->got = 0; c->startNext = false; c->since = 0;
    FgCapKind w[FGCAP_KINDS]; memset(w, 0, sizeof(w));
    const bool hud = g_fg->img[FEED_FG_HUDLESS] != nullptr && !g_fg->hudEmpty;
    const char* why = nullptr;
    if (!FgCapDescribe(g_fg->img[FEED_FG_PRESENT], &w[FGCAP_PRESENT]) || !FgCapDescribe(g_fg->img[FEED_FG_GEN], &w[FGCAP_GEN]) || !FgCapDescribe(h.tex[FEED_DEPTH], &w[FGCAP_DEPTH])
        || !FgCapDescribe(h.tex[FEED_MV], &w[FGCAP_MV]) || (hud && !FgCapDescribe(g_fg->img[FEED_FG_HUDLESS], &w[FGCAP_HUD]))) why = "one of its pictures is missing or is not one it can copy whole";
    if (why) { LogRecord("frame generation capture: NOT ARMED for this set: %s; nothing else is affected", why); return; }
    EnterCriticalSection(&c->cs);
    const bool same = FgCapSameKinds(w, c->want) && c->allocSeq != 0;
    if (!same) { InterlockedExchange(&c->ready, 0); memcpy(c->want, w, sizeof(w)); c->allocSeq++; }
    LeaveCriticalSection(&c->cs);
    c->dispW = g_fg->dispW; c->dispH = g_fg->dispH; c->fmt = (UINT)g_fg->fmt; c->hdr = g_fg->hdr;
    c->armed = true;
    if (!same) SetEvent(c->wake);
    const double frame = (double)FgCapFrameBytes(w) / 1048576.0;
    LogRecord("frame generation capture: ARMED: the presented and the generated image %ux%u format %u%s, depth %ux%u format %u, vectors format %u; a frame is %.1f MiB, a run of %d frames %.1f MiB of video memory and of files, %d run(s) %.1f MiB of files; run %d of %d is next%s",
              w[FGCAP_PRESENT].w, w[FGCAP_PRESENT].hh, w[FGCAP_PRESENT].fmt, hud ? ", the HUD-less image too" : ", no HUD-less image", w[FGCAP_DEPTH].w, w[FGCAP_DEPTH].hh, w[FGCAP_DEPTH].fmt, w[FGCAP_MV].fmt,
              frame, c->frames, frame * c->frames, c->runs, frame * c->frames * c->runs, c->handed + 1, c->runs,
              (c->goWait && !c->goSeen) ? "; waiting for the go file" : "");
}
// The set's end (FgTeardown; the queue is drained): an open run is dropped. After the connection has ended the worker gets its
// time: nothing is served any more, so this is the one place that waits.
__declspec(noinline) static void FgCapDisarm()
{
    FgCap* c = g_fg->cap;
    if (!c) return;
    if (c->open) FgCapBroken(c, g_fg->gN, "the set ended");
    c->armed = false; c->startNext = false;
    if (!c->exiting) return;
    if (c->busy && WaitForSingleObject(c->idle, 120000) != WAIT_OBJECT_0)
        LogRecord("frame generation capture: the writer had not finished 120 s after the connection's end: its run may lack files (a run without its index is not whole)");
    FgCapSayEnd(c, "at the connection's end");
}
// At the connection's end (FgDevAtExit calls this, so nothing new stands in Serve): the teardown that follows gives the
// worker its time; where that teardown is skipped (a library raised, a drain failed) the closing line is said here.
__declspec(noinline) static void FgCapAtExit()
{
    FgCap* c = g_fg ? g_fg->cap : nullptr;
    if (!c) return;
    c->exiting = true;
    if (g_fg->raised) FgCapSayEnd(c, "at the connection's end, before a teardown that is skipped");
}

// 'Q' WAS READ (FgOnQ, before its prepare): what the wire carried for the frame, kept until the prepare takes it.
__declspec(noinline) static void FgCapOnQ(const FeedFgQIvt* q)
{
    FgCap* c = g_fg->cap;
    if (!c || !c->armed) return;
    c->q.n = q->n; c->q.qFlags = q->flags; c->q.wireJitter[0] = q->jitter_x; c->q.wireJitter[1] = q->jitter_y;
    c->q.wireFrameMs = q->frame_time_ms; c->q.wireNear = q->camera_near; c->q.wireFov = q->fov_y_rad;
}
static bool FgCapLive(FgCap* c) { return c && c->armed && g_fg->built && c->ready && !c->busy && !c->off; }
// One texture into the capture's own, on frame generation's open list. The source is simultaneous-access and left in COMMON, so it
// needs no barrier; the capture's texture goes through COPY_DEST and back.
static void FgCapCopy(ID3D12Resource* dst, ID3D12Resource* src)
{
    D3D12_TEXTURE_COPY_LOCATION d = {}, s = {};
    d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    g_fg->list->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
}
// In the prepare's list (FgPrepareSubmit, behind AMD's prepare): a run's frame begins here, with the description the library was
// handed, and the guides are copied where the prepare read them.
__declspec(noinline) static void FgCapInPrepare(const ffxDispatchDescFrameGenerationPrepareV2* p)
{
    FgCap* c = g_fg->cap;
    if (!FgCapLive(c)) return;
    if (!c->open)
    {
        if (!c->startNext || !p->reset) return;
        c->startNext = false; c->open = true; c->got = 0;
    }
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
    const int i = c->got;
    FgCapFrame& f = c->fr[i];
    const FgCapFrame wire = c->q;
    memset(&f, 0, sizeof(f));
    f.n = g_fg->qN; f.id = p->frameID; f.prepared = true;
    if (wire.n == f.n) { f.qFlags = wire.qFlags; f.wireJitter[0] = wire.wireJitter[0]; f.wireJitter[1] = wire.wireJitter[1]; f.wireFrameMs = wire.wireFrameMs; f.wireNear = wire.wireNear; f.wireFov = wire.wireFov; }
    f.prepReset = p->reset ? 1u : 0u; f.renderW = p->renderSize.width; f.renderH = p->renderSize.height;
    f.jitter[0] = p->jitterOffset.x; f.jitter[1] = p->jitterOffset.y; f.mvScale[0] = p->motionVectorScale.x; f.mvScale[1] = p->motionVectorScale.y;
    f.frameMs = p->frameTimeDelta; f.camNear = p->cameraNear; f.camFar = p->cameraFar; f.fovY = p->cameraFovAngleVertical; f.v2m = p->viewSpaceToMetersFactor;
    for (int k = 0; k < 3; k++) { f.cam[k] = p->cameraPosition[k]; f.cam[3 + k] = p->cameraUp[k]; f.cam[6 + k] = p->cameraRight[k]; f.cam[9 + k] = p->cameraForward[k]; }
    ID3D12GraphicsCommandList* const l = g_fg->list;
    FgCapBarrier(l, c->stg[i][FGCAP_DEPTH], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    FgCapBarrier(l, c->stg[i][FGCAP_MV], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    l->EndQuery(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS));
    FgCapCopy(c->stg[i][FGCAP_DEPTH], h.tex[FEED_DEPTH]);
    FgCapCopy(c->stg[i][FGCAP_MV], h.tex[FEED_MV]);
    FgCapBarrier(l, c->stg[i][FGCAP_DEPTH], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    FgCapBarrier(l, c->stg[i][FGCAP_MV], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    l->EndQuery(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS + 1));
    l->ResolveQueryData(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS), 2, c->qrb, (UINT64)i * FGCAP_STAMPS * sizeof(UINT64));
    QueryPerformanceCounter(&q1);
    f.usServe = (float)FgCapUs(q0.QuadPart, q1.QuadPart);
}
// IN THE DISPATCH'S LIST (FgDispatchSubmit, behind AMD's dispatch): the presented image the library read, the HUD-less one
// where it was bound, and the image it wrote.
__declspec(noinline) static void FgCapInDispatch(const ffxDispatchDescFrameGeneration* d)
{
    FgCap* c = g_fg->cap;
    if (!c || !c->armed || !g_fg->built) return;
    c->lastReset = d->reset;
    if (!FgCapLive(c) || !c->open) return;
    const int i = c->got;
    FgCapFrame& f = c->fr[i];
    if (!f.prepared || f.n != g_fg->gN || f.id != d->frameID) return;   // (not this run's frame: the 'G' that follows breaks the run)
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
    const bool hud = c->have[FGCAP_HUD].on && g_fg->img[FEED_FG_HUDLESS] != nullptr && !g_fg->hudEmpty;
    f.dispReset = d->reset ? 1u : 0u; f.hud = hud ? 1u : 0u; f.dbg = 0u; f.transfer = d->backbufferTransferFunction;   // dbg: no lever asks for AMD's debug drawing; the index keeps the field for tools\fg-replay
    f.lum[0] = d->minMaxLuminance[0]; f.lum[1] = d->minMaxLuminance[1];
    f.rect[0] = d->generationRect.left; f.rect[1] = d->generationRect.top; f.rect[2] = d->generationRect.width; f.rect[3] = d->generationRect.height;
    ID3D12GraphicsCommandList* const l = g_fg->list;
    FgCapBarrier(l, c->stg[i][FGCAP_PRESENT], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    FgCapBarrier(l, c->stg[i][FGCAP_GEN], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    if (hud) FgCapBarrier(l, c->stg[i][FGCAP_HUD], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    l->EndQuery(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS + 2));
    FgCapCopy(c->stg[i][FGCAP_PRESENT], FgIn(FEED_FG_PRESENT));     // (the second input set: the pair's own, FgIn)
    FgCapCopy(c->stg[i][FGCAP_GEN], g_fg->img[FEED_FG_GEN]);
    if (hud) FgCapCopy(c->stg[i][FGCAP_HUD], FgIn(FEED_FG_HUDLESS));
    FgCapBarrier(l, c->stg[i][FGCAP_PRESENT], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    FgCapBarrier(l, c->stg[i][FGCAP_GEN], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    if (hud) FgCapBarrier(l, c->stg[i][FGCAP_HUD], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    l->EndQuery(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS + 3));
    l->ResolveQueryData(c->qh, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(i * FGCAP_STAMPS + 2), 2, c->qrb, ((UINT64)i * FGCAP_STAMPS + 2) * sizeof(UINT64));
    f.dispatched = true;
    QueryPerformanceCounter(&q1);
    f.usServe += (float)FgCapUs(q0.QuadPart, q1.QuadPart);
}
// After 'G', behind the pair's answer: the open run's frame is whole or the run is broken; a whole run goes to the worker; where a
// run may begin, the next pair is told to reset and is its frame 0. No wait: two flags are read, an event is set.
__declspec(noinline) static void FgCapAfterG(UINT64 n, bool generated, uint32_t gFlags, float peak)
{
    FgCap* c = g_fg->cap;
    if (!c || !c->armed) return;
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
    if (generated) c->since = c->lastReset ? 0 : (c->since < 60000 ? c->since + 1 : 60000);
    if (c->open)
    {
        FgCapFrame& f = c->fr[c->got];
        if (generated && f.dispatched && f.n == n && (c->got > 0 || f.dispReset))
        {
            f.gFlags = gFlags; f.wirePeak = peak; f.since = c->since; f.fence = h.fenceValue;
            const bool whole = c->got + 1 == c->frames;
            if (whole) { c->jobRun = c->handed + 1; c->jobFence = h.fenceValue; ResetEvent(c->idle); }
            // THE FRAME'S OWN COST ON THIS THREAD, closed here: its three calls, with the hand-over's two flags when it is the last
            QueryPerformanceCounter(&q1);
            f.usServe += (float)FgCapUs(q0.QuadPart, q1.QuadPart);
            const double us = (double)f.usServe;
            c->usSum += us; c->usN++; if (us > c->usMax) c->usMax = us;
            c->got++;
            if (whole)
            {   // the run is whole: records and textures are the worker's until `busy` clears
                InterlockedExchange(&c->busy, 1);
                SetEvent(c->wake);
                c->handed++; c->open = false; c->got = 0; c->cool = c->gap;
            }
        }
        else FgCapBroken(c, n, !generated ? "a pair had no frame" : !f.dispatched ? "a pair's dispatch was not this run's" : "its first frame was not told to reset");
    }
    else if (c->cool > 0) c->cool--;
    if (!c->open && !c->startNext && c->cool == 0 && c->handed < c->runs && generated && c->since >= (int)FGC_STEADY && c->ready && !c->busy && !c->off)
    {
        c->startNext = true;
        g_fg->resetNext = true;      // the next pair's prepare and dispatch reset, by the helper's own account: the run's frame 0
    }
    else if (c->startNext && !c->open) g_fg->resetNext = true;   // (the pair that should have begun the run was not prepared: the next one is asked again)
}

// ---- The connection's end for the development arm ----
// FgAtExit calls this (declared in fg_host.h), before the tail's teardown, which is skipped when a library raised or its drain
// failed. No pair is served after this.
__declspec(noinline) static void FgDevAtExit()
{
    FgCapAtExit();           // the capture: the connection has ended, so the teardown that follows may wait for its writer
        // The second input set's counts (fg_host.h), here and not in FgAtExit, which Serve inlines; only when one is not zero. The mixes
    // must read 0; a 'Q' naming the second set in a set without it is a planted fault.
    if (g_fg && (g_fg->cSet1 || g_fg->cSetBad || g_fg->cSetMix))
        LogRecord("frame generation: this connection, the second input set: pairs in it %llu; pairs that named it in a set without it %llu; dispatches whose 'G' named the other set %llu",
                  (unsigned long long)g_fg->cSet1, (unsigned long long)g_fg->cSetBad, (unsigned long long)g_fg->cSetMix);
}
