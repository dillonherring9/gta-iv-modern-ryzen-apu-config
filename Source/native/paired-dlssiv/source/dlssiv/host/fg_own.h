// The own arm: frame generation through the mod's own library and interface (dlssiv-fg.dll; ffx-own/glue/dlssiv_fg.h), built from
// AMD's public source, where a pair is three calls (prepare, optical flow, interpolate) and a GPU timestamp can follow every pass.
// Each entry point is one call added where fg_host.h's stock arm calls ffx-api (FgOwnLoad, FgOwnAtProbe, FgOwnMemory, FgOwnCreate,
// FgOwnConfigure, FgOwnPrepare, FgOwnDispatch, FgOwnDestroy, FgOwnInList, FgOwnAfterG). AMD's loader is never touched: FgHost::fn
// stays empty and FgHost::ctx holds this arm's context. Calls run in this file's own SEH frames (FgoSafe*); a raise sets
// FgHost::raised, never g_ffxRaised. Textures are handed in COMMON and left in COMMON; the library makes no queue, thread or fence.
// Development arguments (with --fg=1): --fgown=<file> (--fglib's two forms; any refusal is one line ending ": off", never a
// fallback), --fgown_sha=<64 hex>, --fgown_stamps=N (1 a timestamp behind every backend job with a barrier, 2 without), and
// --fgown_csv=PATH. Stamps are read once their list retires, without waiting.
// What it says:
//     frame generation: fgown <full path>, <bytes> bytes, sha256 <hex>: loaded direct
//     frame generation: fgown: interface <n>, stage <n>, "<what the library says it is>", AMD "<version>" id 0x<id>
//     frame generation: fgown <as given>: not a full path: off              (and the other refusals, in --fglib's words)
//     frame generation: fgown: set: pipelines <asked> asked, <refused> refused; shader model <m>.<n>, wave lanes <a> to <b>,
//                       16-bit <set|clear>: AMD's <base variants (cs_6_2)|WAVE64 variants (cs_6_6)>; context <bytes> bytes; stamps <word>
//     frame generation: fgown: set end: lists <n>, read <n>, not read <n>, stamps lost <n>, rows lost <n>
// The shipped helper (build-host.ps1 -Release -Fg) runs frame generation on this arm alone, for DLSSIV\dlssiv-fg.dll held to its
// record (tools\ffx-own\RECORD-dlssiv-fg-cut.txt, passed to the compiler as DLSSIV_FG_OWN_SIZE and DLSSIV_FG_OWN_SHA). A file that
// is missing or of another size or hash is refused: frame generation off, one log line, FEED_CAP_FG_NOLIB in the handshake. The
// stamps, rows, CPU clocks, module record, message hook and raise lever are DLSSIV_DEV only.
// fg_host.h's rules hold: one pointer (FgHost::own), everything on the heap, no statics, no entry point inlined or called from Serve.
#pragma once
#include "../ffx-own/glue/dlssiv_fg.h"
#include "fg_sha.h"

#if !DLSSIV_DEV
// The record, from the build (build-host.ps1 -Release -Fg): a shipped helper is never built without it.
#if !defined(DLSSIV_FG_OWN_SIZE) || !defined(DLSSIV_FG_OWN_SHA)
#error "a shipped helper holds dlssiv-fg.dll to its record: build it with build-host.ps1 -Release -Fg, which defines DLSSIV_FG_OWN_SIZE and DLSSIV_FG_OWN_SHA from tools\\ffx-own\\RECORD-dlssiv-fg-cut.txt"
#endif
static_assert(sizeof(DLSSIV_FG_OWN_SHA) == 65, "DLSSIV_FG_OWN_SHA is the record's SHA-256, 64 hex digits");
#define DLSSIV_FG_OWN_FILE "dlssiv-fg.dll"
#endif

// Wire 3, the counts of a pair. Interface 3 (dlssiv_fg.h) has two interpolate flags: DLSSIV_FG_NO_STORE (the first of two
// interpolates records no "store current buffer" copy, so the second reads the same previous source) and DLSSIV_FG_STORE_ONLY (the
// copy alone, for a flow-only pair). By the count the 'G' asked (FgHost::genNow): 0 the optical flow and the store alone; 1 the two
// calls; 2 the optical flow, interpolate(1/3, NO_STORE) into GEN, then interpolate(2/3) into GEN2, in one list.
// Where the header is not interface 3 the flags get stand-in values so this compiles, and the arm makes no count but one
// (FgOwnGenCounts says why); when the header defines them these lines go.
#if DLSSIV_FG_INTERFACE >= 3
#define FGO_GEN_STANDIN 0
#else
#define FGO_GEN_STANDIN 1                   // stand-in: the header is interface 2; counts 0 and 2 are never made
#ifndef DLSSIV_FG_NO_STORE
#define DLSSIV_FG_NO_STORE   2u             // stand-in: interface 3's flag, until the header has it
#endif
#ifndef DLSSIV_FG_STORE_ONLY
#define DLSSIV_FG_STORE_ONLY 4u             // stand-in: likewise
#endif
#endif

enum { FGO_ROWS_BYTES = 1 << 20 };   // the rows wait here for the next write (about a hundred dispatches' worth with the stamps on)
enum { FGO_ROWS_EVERY = 16 };        // pairs between two writes of the rows

// What a slot of the ring remembers of the list recorded from it, until that list has retired and is read.
struct FgoSlot
{
    bool   open;                     // a list of this arm's was recorded from this slot and is not read yet
    BYTE   kind, reset;              // FGC_PREPARE or FGC_DISPATCH; it was told to reset
    BYTE   calls;                    // which of the three calls returned OK in it: bit 0 prepare, 1 optical flow, 2 interpolate
    UINT64 n;                        // the pair
    UINT64 fenceWas;                 // the slot's retiring value when the list began: another one says the list went to the queue
    float  cpu[3];                   // the CPU's microseconds round each recording: prepare, optical flow, interpolate
};
struct FgOwn
{
    char   path[MAX_PATH];           // --fgown, as a full path
    BYTE   want[32]; bool haveWant;  // --fgown_sha
    bool   refused;                  // THE REFUSAL: said once, final for the process
#if DLSSIV_DEV
    int    stampsAsk;                // --fgown_stamps
#endif
    // the library
    HMODULE mod;                     // this arm's own reference, kept for the process (FgHost::dll holds a second one, which FgUnload gives back)
    char   modPath[MAX_PATH]; UINT64 modSize; BYTE modSha[32];
    DlssivFgInfoFn        info;
    DlssivFgCreateFn      create;
    DlssivFgDestroyFn     destroy;
    DlssivFgPrepareFn     prepare;
    DlssivFgOpticalFlowFn flow;
    DlssivFgInterpolateFn interpolate;
    DlssivFgStampsFn      stamps;
    DlssivFgInfo          said;      // what the library said it is, at its load
    // the set
    DlssivFgContext* ctx;            // FgHost::ctx holds the same pointer
    DlssivFgCreated  made;
    void*    hud;                    // the configure's HUD-less image for the pair's two calls (ID3D12Resource*), or null
    uint32_t dbg;                    // and its debug drawing, in our interface's bits
#if DLSSIV_DEV
    // the stamps and the rows
    bool     stampsOn;               // this set's context was created with the stamps
    UINT64   gpuFreq; LONGLONG qpcFreq;
    FgoSlot  slot[FG_RING];
    UINT64   lists, read, notRead, stampsLost, rowsLost, pairs;
    HANDLE   csv; char* rows; int rowsAt;
    UINT64*  ticks; DlssivFgMark* marks;   // DLSSIV_FG_STAMPS_A_SLOT each: where a slot is read into
#endif
    float    position;               // where the generated frame stands, 0 the previous frame, 1 the current; 0 means the middle (nothing sets it yet)
        // The size the file must have: the record's in a shipped helper, none in a development one.
    UINT64   wantSize; bool haveSize;
};

// ---- The seven calls into the library ----
// Each in a frame of this arm's own. A raise is noted, sets frame generation's latch, and returns 0x7FFFFFFF with the code in *code.
static uint32_t FgoSafeInfo(FgOwn* O, DlssivFgInfo* out, const DlssivFgCreateDesc* forMemory, DWORD* code)
{
    *code = 0;
    __try { return O->info(out, forMemory); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return 0x7FFFFFFFu; }
}
static uint32_t FgoSafeCreate(FgOwn* O, const DlssivFgCreateDesc* d, DlssivFgContext** out, DlssivFgCreated* made, DWORD* code)
{
    *code = 0;
    __try { return O->create(d, out, made); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return 0x7FFFFFFFu; }
}
static uint32_t FgoSafePrepare(FgOwn* O, const DlssivFgPrepareDesc* d, DWORD* code)
{
    *code = 0;
    __try { return O->prepare(O->ctx, d); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return 0x7FFFFFFFu; }
}
static uint32_t FgoSafeFlow(FgOwn* O, const DlssivFgFrameDesc* d, DWORD* code)
{
    *code = 0;
    __try
    {
#if DLSSIV_DEV
        if (g_fg->forceRaise & 1) RaiseException(0xE0F60001u, 0, 0, nullptr);   // the raise lever, as the stock arm's dispatch has it
#endif
        return O->flow(O->ctx, d);
    }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return 0x7FFFFFFFu; }
}
static uint32_t FgoSafeInterpolate(FgOwn* O, const DlssivFgFrameDesc* d, DWORD* code)
{
    *code = 0;
    __try { return O->interpolate(O->ctx, d); }
    __except (RaiseNote(GetExceptionInformation())) { *code = GetExceptionCode(); g_fg->raised = true; return 0x7FFFFFFFu; }
}
static void FgoSafeDestroy(FgOwn* O)
{
    __try { O->destroy(O->ctx); }
    __except (RaiseNote(GetExceptionInformation())) { g_fg->raised = true; }
}

#if DLSSIV_DEV
static uint32_t FgoSafeStamps(FgOwn* O, DlssivFgStampsDesc* d)
{
    __try { return O->stamps(O->ctx, d); }
    __except (RaiseNote(GetExceptionInformation())) { g_fg->raised = true; return 0x7FFFFFFFu; }
}

// ---- The rows ----
// Kept in memory and written behind a pair's answer: no file is written between two of a list's clocks. A row that finds the
// buffer full is lost and counted.
static void FgoRowsWrite(FgOwn* O)
{
    if (!O->csv || !O->rowsAt) return;
    DWORD w = 0;
    WriteFile(O->csv, O->rows, (DWORD)O->rowsAt, &w, nullptr);
    O->rowsAt = 0;
}
static void FgoRow(FgOwn* O, const FgtLine* l)
{
    if (!O->csv || !O->rows) return;
    if (l->at <= 0 || O->rowsAt + l->at > FGO_ROWS_BYTES) { O->rowsLost++; return; }
    memcpy(O->rows + O->rowsAt, l->b, (size_t)l->at);
    O->rowsAt += l->at;
}

// A slot's list, read; the caller knows it has retired. Without the stamps: one row a call, the CPU's clock alone. With them: one
// row a stamp, with the GPU time since the previous stamp. A list whose stamps are out of order is not read, and is counted.
static void FgoCollect(FgOwn* O, int slot)
{
    FgoSlot& r = O->slot[slot];
    if (!r.open) return;
    r.open = false;
    const char list = r.kind == FGC_DISPATCH ? 'D' : 'P';
    FgtLine l;
    if (!O->stampsOn)
    {
        for (int k = 0; k < 3; k++)
        {
            if (!(r.calls & (1 << k))) continue;
            l.at = 0; l.b[0] = 0;
            FgtAdd(&l, "%llu,%c,%d,%d,-1,0,0,0,0,0.000,%.2f,(no stamps)\r\n", (unsigned long long)r.n, list, (int)r.reset, k + 1, (double)r.cpu[k]);
            FgoRow(O, &l);
        }
        O->read++;
        return;
    }
    DlssivFgStampsDesc s = {};
    s.size = sizeof(s); s.op = DLSSIV_FG_STAMPS_READ; s.slot = (uint32_t)slot; s.capacity = DLSSIV_FG_STAMPS_A_SLOT; s.ticks = O->ticks; s.marks = O->marks;
    if (g_fg->raised || !O->ctx || FgoSafeStamps(O, &s) != DLSSIV_FG_OK || !s.count) { O->notRead++; return; }
    O->stampsLost += s.lost;
    for (uint32_t i = 1; i < s.count; i++) if (O->ticks[i] < O->ticks[i - 1]) { O->notRead++; return; }
    const double k = 1e6 / (double)O->gpuFreq;
    for (uint32_t i = 0; i < s.count; i++)
    {
        const DlssivFgMark& m = O->marks[i];
        const UINT64 dt = i ? O->ticks[i] - O->ticks[i - 1] : 0;
        const int call = (int)m.call;
        const double cpu = (m.kind == DLSSIV_FG_MARK_HEAD && call >= 1 && call <= 3) ? (double)r.cpu[call - 1] : 0.0;
        l.at = 0; l.b[0] = 0;
        FgtAdd(&l, "%llu,%c,%d,%d,%u,%d,%d,%d,%llu,%.3f,%.2f,%.43s\r\n", (unsigned long long)r.n, list, (int)r.reset, call, (unsigned)i, (int)m.kind, (int)m.job, (int)m.waited,
               (unsigned long long)dt, (double)dt * k, cpu, m.name);
        FgoRow(O, &l);
    }
    O->read++;
}
#endif

// ---- Asked for ----
// From main, once, after FgLibAsk and before the first device; with no argument nothing is made or said. The module's identity is
// said in fg_lib.h's lines, so that file's state is made here with only its lines on. A shipped helper asks at --fg=1 only, with
// DLSSIV_FG_OWN_FILE and the record's SHA-256 and size; no stamps, no csv, no fg_lib.h.
__declspec(noinline) static void FgOwnAsk(int fg, const char* path, const char* sha, int stamps, const char* csv, bool withLib)
{
    const bool hasPath = path && path[0], hasSha = sha && sha[0], hasCsv = csv && csv[0];
#if DLSSIV_DEV
    if (!hasPath && !hasSha && !stamps && !hasCsv) return;
    if (fg != 1) { LogRecord("frame generation: --fgown, --fgown_sha, --fgown_stamps and --fgown_csv need --fg=1: none of them is on"); return; }
#else
    (void)fg; (void)hasCsv;                          // (a shipped helper's main asks at --fg=1 alone, with the file and the hash)
#endif
    FgOwn* const O = FgHostMake() ? (FgOwn*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgOwn)) : nullptr;
#if DLSSIV_DEV
    if (O && !g_fg->lib) g_fg->lib = (FgLib*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(FgLib));
    if (!O || !g_fg->lib)
    {   // NEVER A FALLBACK: without this state FgLoad would ask AMD's loader, which is not what was asked for
        LogRecord("frame generation: no memory for the own arm's state (%lu bytes): this helper ends", (unsigned long)sizeof(FgOwn));
        TerminateProcess(GetCurrentProcess(), 2);
        return;
    }
    g_fg->own = O;
    g_fg->lib->ident = true;
    O->stampsAsk = stamps < 0 ? 0 : stamps > 2 ? 2 : stamps;
#else
        // A shipped helper has no other arm: with no memory for this state frame generation is off, said once, and the upscaler goes on.
    if (!O) { LogRecord("frame generation: no memory for the own arm's state (%lu bytes): off for this process", (unsigned long)sizeof(FgOwn)); return; }
    g_fg->own = O;
    O->wantSize = DLSSIV_FG_OWN_SIZE; O->haveSize = true;
    (void)stamps;
#endif
    O->path[0] = '?'; O->path[1] = 0;                     // never empty: FgLoad must come to this arm's load or to its refusal
#if DLSSIV_DEV
    if (withLib) { O->refused = true; LogRecord("frame generation: fgown: --fglib is given beside it: off"); }
    if (!hasPath && !O->refused) { O->refused = true; LogRecord("frame generation: fgown: --fgown_sha, --fgown_stamps or --fgown_csv without --fgown: off"); }
#else
    (void)withLib;
#endif
    if (hasSha)
    {
        int n = 0;
        for (; n < 64; n++)
        {
            const char c = sha[n];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) break;
            O->want[n / 2] = (BYTE)((O->want[n / 2] << 4) | v);
        }
        O->haveWant = n == 64 && !sha[64];
        if (!O->haveWant && !O->refused) { O->refused = true; LogRecord("frame generation: fgown_sha: not 64 hex digits: off"); }
    }
    if (hasPath)
    {   // (the two forms, made a full path exactly as FgLibAsk makes --fglib's)
        bool bare = true;
        for (const char* p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') bare = false;
        char beside[MAX_PATH] = "", full[MAX_PATH] = "";
        const bool fits = !bare || _snprintf_s(beside, MAX_PATH, _TRUNCATE, "%s%s", g_exeDir, path) > 0;   // g_exeDir ends with its backslash
        const char* const src = bare ? beside : path;
        const bool isFull = fits && ((src[0] && src[1] == ':' && (src[2] == '\\' || src[2] == '/')) || (src[0] == '\\' && src[1] == '\\'));
        const DWORD n = isFull ? GetFullPathNameA(src, MAX_PATH, full, nullptr) : 0;
        size_t dir = 0, name = 0;
        while (g_exeDir[dir]) dir++;
        for (size_t i = 0; n && n < MAX_PATH && full[i]; i++) if (full[i] == '\\') name = i + 1;
        const char* why = nullptr;
        if (!n || n >= MAX_PATH) why = bare ? "not a file beside the helper" : "not a full path";
        else if (bare && (!FglSameStart(full, g_exeDir, dir) || name != dir)) why = "not a file beside the helper";
        else if (FglSameStart(full + name, "amd_fidelityfx", 14)) why = "an AMD name";
        if (why) { if (!O->refused) { O->refused = true; LogRecord("frame generation: fgown %s: %s: off", path, why); } }
        else memcpy(O->path, full, MAX_PATH);
    }
#if DLSSIV_DEV
    if (hasCsv && !O->refused)
    {
        O->rows = (char*)HeapAlloc(GetProcessHeap(), 0, FGO_ROWS_BYTES);
        const HANDLE f = O->rows ? CreateFileA(csv, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
        if (f == INVALID_HANDLE_VALUE) LogRecord("frame generation: fgown: the rows' file would not open (%lu): no rows are written", GetLastError());
        else
        {
            O->csv = f;
            LARGE_INTEGER size = {};
            if (GetFileSizeEx(f, &size) && size.QuadPart == 0)
            {
                FgtLine l; l.at = 0; l.b[0] = 0;
                FgtAdd(&l, "n,list,reset,call,idx,kind,job,waited,ticks,us,cpu_us,name\r\n");
                FgoRow(O, &l);
            }
            FgoRowsWrite(O);
        }
    }
#endif
}

// ---- The load, from FgLoad ----
// false refused (said once; frame generation off for the process), true loaded; FgLoad goes nowhere else. A file that could not be
// read, or of another size or hash, also sets FEED_CAP_FG_NOLIB for the handshake (FgOffer).
__declspec(noinline) static bool FgOwnLoad()
{
    FgOwn* const O = g_fg->own;
    if (O->refused) return false;
    if (O->mod)
    {   // after a rebind: FgUnload gave back FgHost's reference, this arm's own kept the module mapped
        g_fg->dll = LoadLibraryA(O->modPath);
        return g_fg->dll != nullptr;
    }
    BYTE sha[32]; UINT64 size = 0; HANDLE held = nullptr; DWORD err = 0; char hex[65], want[65];
    if (!FglHashFile(O->path, true, &size, sha, &held, &err))
    {
        O->refused = true; g_fg->offerWhy = FEED_CAP_FG_NOLIB;
        LogRecord("frame generation: fgown %s: not read (%lu): off", O->path, (unsigned long)err);
        return false;
    }
    FglHex(sha, hex);
    if (O->haveSize && size != O->wantSize)
    {
        CloseHandle(held); O->refused = true; g_fg->offerWhy = FEED_CAP_FG_NOLIB;
        LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: not the expected %llu bytes: off", O->path, (unsigned long long)size, hex, (unsigned long long)O->wantSize);
        return false;
    }
    if (O->haveWant && !FglSameSha(sha, O->want))
    {
        CloseHandle(held); O->refused = true; g_fg->offerWhy = FEED_CAP_FG_NOLIB;
        LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: not the expected sha256 %s: off", O->path, (unsigned long long)size, hex, FglHex(O->want, want));
        return false;
    }
    const HMODULE m = LoadLibraryA(O->path);
    err = m ? 0 : GetLastError();
    CloseHandle(held);                              // held to here: nobody wrote the file between its hash and its load
    if (!m)
    {
        O->refused = true;
        LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: would not load (%lu): off", O->path, (unsigned long long)size, hex, (unsigned long)err);
        return false;
    }
    char mod[MAX_PATH] = "";
    if (!GetModuleFileNameA(m, mod, MAX_PATH)) mod[0] = 0;
    O->info        = (DlssivFgInfoFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_INFO);
    O->create      = (DlssivFgCreateFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_CREATE);
    O->destroy     = (DlssivFgDestroyFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_DESTROY);
    O->prepare     = (DlssivFgPrepareFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_PREPARE);
    O->flow        = (DlssivFgOpticalFlowFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_OPTICAL_FLOW);
    O->interpolate = (DlssivFgInterpolateFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_INTERPOLATE);
    O->stamps      = (DlssivFgStampsFn)(void*)GetProcAddress(m, DLSSIV_FG_NAME_STAMPS);
    const bool entries = O->info && O->create && O->destroy && O->prepare && O->flow && O->interpolate && O->stamps;
    const bool same = FglSameStart(mod, O->path, MAX_PATH);
    DWORD raised = 0;
    DlssivFgInfo said = {}; said.size = sizeof(said);
    const uint32_t rc = (entries && same) ? FgoSafeInfo(O, &said, nullptr, &raised) : 0u;
    if (!entries || !same || rc != DLSSIV_FG_OK || said.iface < 1u || said.iface > DLSSIV_FG_INTERFACE)   // (interface 1 is interface 2 without the position: dlssiv_fg.h)
    {
        O->refused = true;
        if (!entries)   LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: lacks a function of the mod's interface: off", O->path, (unsigned long long)size, hex);
        else if (!same) LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: the module mapped is %s: off", O->path, (unsigned long long)size, hex, mod);
        else if (rc)    LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: asked what it is, it answered 0x%08X%s: off", O->path, (unsigned long long)size, hex, (unsigned)rc, raised ? " (it RAISED)" : "");
        else            LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: it speaks interface %u, this helper %u: off", O->path, (unsigned long long)size, hex, (unsigned)said.iface, (unsigned)DLSSIV_FG_INTERFACE);
        if (!raised) FreeLibrary(m);                // a library that raised is not unloaded: its detach code is its code
        return false;
    }
    said.amdVersion[sizeof(said.amdVersion) - 1] = 0; said.build[sizeof(said.build) - 1] = 0;
    O->mod = m; O->said = said;
    memcpy(O->modPath, mod, MAX_PATH); O->modSize = size; memcpy(O->modSha, sha, 32);
    g_fg->dll = LoadLibraryA(mod);                  // FgHost's own reference: FgUnload gives it back at a rebind, and the module stays
#if DLSSIV_DEV
    FgLib* const L = g_fg->lib;                     // fg_lib.h's record of the module, so that its lines name this one
    L->direct = true; L->modKnown = true; L->saidFrames = false;
    memcpy(L->modPath, mod, MAX_PATH); L->modSize = size; memcpy(L->modSha, sha, 32);
#endif
    LogRecord("frame generation: fgown %s, %llu bytes, sha256 %s: loaded direct", mod, (unsigned long long)size, hex);
#if DLSSIV_DEV
    LogRecord("frame generation: fgown: interface %u, stage %u, \"%s\", AMD \"%s\" id 0x%llX", (unsigned)said.iface, (unsigned)said.stage, said.build, said.amdVersion, (unsigned long long)said.amdId);
#endif
    return g_fg->dll != nullptr;
}

// ---- At the probe ----
// From FgProbe after a successful load: no version query. What the library said at its load is offered: AMD's version of the
// source and its id, which the ack carries.
__declspec(noinline) static void FgOwnAtProbe()
{
    FgOwn* const O = g_fg->own;
    g_fg->provider = O->said.amdId;
    size_t i = 0;
    for (; i + 1 < sizeof(g_fg->providerName) && O->said.amdVersion[i]; i++) g_fg->providerName[i] = O->said.amdVersion[i];
    g_fg->providerName[i] = 0;
    LogRecord("frame generation: available, provider \"%s\" id 0x%llX by the mod's own interface %u; the upscaler's library is %s in this process",
              g_fg->providerName, (unsigned long long)g_fg->provider, (unsigned)O->said.iface, GetModuleHandleA("amd_fidelityfx_upscaler_dx12.dll") ? "mapped" : "not mapped");
}

// AMD's context flags, as FgBuild made them for the stock create, in our interface's bits.
static uint32_t FgoCreateFlags(uint32_t ctxFlags)
{
    return ((ctxFlags & FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED) ? DLSSIV_FG_CREATE_DEPTH_INVERTED : 0u) | ((ctxFlags & FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE) ? DLSSIV_FG_CREATE_DEPTH_INFINITE : 0u)
         | ((ctxFlags & FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE) ? DLSSIV_FG_CREATE_HDR : 0u) | ((ctxFlags & FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING) ? DLSSIV_FG_CREATE_CHECK : 0u);
}
static void FgoCreateDesc(DlssivFgCreateDesc* c, UINT dispW, UINT dispH, DXGI_FORMAT fmt, uint32_t ctxFlags)
{
    memset(c, 0, sizeof(*c));
    c->size = sizeof(*c); c->flags = FgoCreateFlags(ctxFlags); c->device = h.dev;
    c->displayWidth = dispW; c->displayHeight = dispH; c->maxRenderWidth = h.width; c->maxRenderHeight = h.height;
    c->backBufferFormat = (uint32_t)fmt;
}

// ---- In FgBuild: a context's memory, and the create ----
__declspec(noinline) static void FgOwnMemory(UINT dispW, UINT dispH, DXGI_FORMAT fmt, uint32_t ctxFlags, FfxApiEffectMemoryUsage* mu, DWORD* raised)
{
    FgOwn* const O = g_fg->own;
    mu->totalUsageInBytes = 0; mu->aliasableUsageInBytes = 0;
    *raised = 0;
    if (FgRefusedAfterRaise()) return;
    DlssivFgCreateDesc c; FgoCreateDesc(&c, dispW, dispH, fmt, ctxFlags);
    DlssivFgInfo info = {}; info.size = sizeof(info);
    if (FgoSafeInfo(O, &info, &c, raised) != DLSSIV_FG_OK) return;
    mu->totalUsageInBytes = info.contextBytes; mu->aliasableUsageInBytes = info.aliasableBytes;
}
__declspec(noinline) static ffxReturnCode_t FgOwnCreate(UINT dispW, UINT dispH, DXGI_FORMAT fmt, uint32_t ctxFlags, DWORD* raised)
{
    FgOwn* const O = g_fg->own;
    *raised = 0;
    if (FgRefusedAfterRaise()) return (ffxReturnCode_t)0x7FFFFFFF;
    DlssivFgCreateDesc c; FgoCreateDesc(&c, dispW, dispH, fmt, ctxFlags);
#if DLSSIV_DEV
    if (O->stampsAsk) { c.flags |= DLSSIV_FG_CREATE_STAMPS | (O->stampsAsk == 2 ? DLSSIV_FG_CREATE_STAMPS_NO_BARRIER : 0u); c.stampSlots = FG_RING; }
    if (g_fg->check) c.message = FgCheckMessage;   // --fgcheck=1: AMD's own messages, to the check's counter (fg_lib.h)
#endif
    DlssivFgCreated made = {}; made.size = sizeof(made);
    DlssivFgContext* ctx = nullptr;
    const uint32_t rc = FgoSafeCreate(O, &c, &ctx, &made, raised);
    O->made = made; O->hud = nullptr; O->dbg = 0;
#if DLSSIV_DEV
    O->stampsOn = false;
    memset(O->slot, 0, sizeof(O->slot));
    O->lists = O->read = O->notRead = O->stampsLost = O->pairs = 0;
#endif
    if (rc == DLSSIV_FG_OK && ctx)
    {
        O->ctx = ctx; g_fg->ctx = (ffxContext)ctx;
#if DLSSIV_DEV
        LARGE_INTEGER qf = {}; QueryPerformanceFrequency(&qf); O->qpcFreq = qf.QuadPart;
        if (made.stamps)
        {
            if (!O->ticks) O->ticks = (UINT64*)HeapAlloc(GetProcessHeap(), 0, DLSSIV_FG_STAMPS_A_SLOT * sizeof(UINT64));
            if (!O->marks) O->marks = (DlssivFgMark*)HeapAlloc(GetProcessHeap(), 0, DLSSIV_FG_STAMPS_A_SLOT * sizeof(DlssivFgMark));
            O->stampsOn = O->ticks && O->marks && SUCCEEDED(h.queue->GetTimestampFrequency(&O->gpuFreq)) && O->gpuFreq != 0;
        }
#endif
    }
#if DLSSIV_DEV
        // A shipped helper says neither this line nor the load's interface line: its identity line stands where the stock arm's provider
    // line stood, and FgBuild's own lines say each set and a refused create.
    LogRecord("frame generation: fgown: set: create 0x%08X; pipelines %u asked, %u refused; shader model %u.%u, wave lanes %u to %u, 16-bit %s: AMD's %s; context %llu bytes; stamps %s",
              (unsigned)rc, (unsigned)made.pipelinesAsked, (unsigned)made.pipelinesRefused, (unsigned)(made.shaderModel >> 4), (unsigned)(made.shaderModel & 15u), (unsigned)made.waveLaneMin, (unsigned)made.waveLaneMax,
              made.fp16 ? "set" : "clear", made.wave64 ? "WAVE64 variants (cs_6_6)" : "base variants (cs_6_2)", (unsigned long long)made.contextBytes,
              !O->stampsAsk ? "off" : !O->stampsOn ? "ASKED AND NOT ON" : made.stamps == 2 ? "on, NO barrier (the barrier's control)" : "on, a barrier before each");
#endif
    return (ffxReturnCode_t)rc;
}

// ---- In FgConfigureNow ----
// Nothing is called; what AMD's configure would have been told is kept for the pair's two calls.
__declspec(noinline) static ffxReturnCode_t FgOwnConfigure(const ffxConfigureDescFrameGeneration* cfg)
{
    FgOwn* const O = g_fg->own;
    O->hud = cfg->HUDLessColor.resource;
    O->dbg = ((cfg->flags & FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES) ? DLSSIV_FG_DEBUG_TEAR_LINES : 0u) | ((cfg->flags & FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS) ? DLSSIV_FG_DEBUG_RESET_INDICATORS : 0u)
           | ((cfg->flags & FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW) ? DLSSIV_FG_DEBUG_VIEW : 0u);
    return FFX_API_RETURN_OK;
}

#if DLSSIV_DEV
// A list of this arm's begins in the slot FgBeginCommands just took (its last list has retired): that list is read first if still
// waiting, the slot's record is made new, and with the stamps on the library's slot begins.
static void FgoListBegins(FgOwn* O, int kind, bool reset, UINT64 n)
{
    const int slot = g_fg->slot;
    FgoSlot& r = O->slot[slot];
    if (r.open)
    {
        if (g_fg->allocFence[slot] != r.fenceWas) FgoCollect(O, slot);
        else { r.open = false; O->notRead++; }      // it never went to the queue (AMD's call failed, the list did not close)
    }
    memset(&r, 0, sizeof(r));
    r.open = true; r.kind = (BYTE)kind; r.reset = reset ? 1 : 0; r.n = n; r.fenceWas = g_fg->allocFence[slot];
    O->lists++;
    if (O->stampsOn)
    {
        DlssivFgStampsDesc s = {};
        s.size = sizeof(s); s.op = DLSSIV_FG_STAMPS_BEGIN; s.slot = (uint32_t)slot;
        FgoSafeStamps(O, &s);
    }
}
static float FgoUs(const FgOwn* O, LONGLONG ticks) { return O->qpcFreq ? (float)((double)ticks * 1e6 / (double)O->qpcFreq) : 0.0f; }
#endif

// ---- In FgPrepareSubmit ----
// This arm's prepare where the stock arm calls AMD's, from the very description the stock arm filled, into the same list.
__declspec(noinline) static ffxReturnCode_t FgOwnPrepare(const ffxDispatchDescFrameGenerationPrepareV2* p, DWORD* raised)
{
    FgOwn* const O = g_fg->own;
    *raised = 0;
    if (FgRefusedAfterRaise() || !O->ctx) return (ffxReturnCode_t)0x7FFFFFFF;
    DlssivFgPrepareDesc d = {};
    d.size = sizeof(d); d.flags = p->reset ? DLSSIV_FG_RESET : 0u;
    d.commandList = p->commandList; d.depth = p->depth.resource; d.motionVectors = p->motionVectors.resource;
    d.frameId = p->frameID;
    d.renderWidth = p->renderSize.width; d.renderHeight = p->renderSize.height;
    d.jitterX = p->jitterOffset.x; d.jitterY = p->jitterOffset.y;
    d.motionVectorScaleX = p->motionVectorScale.x; d.motionVectorScaleY = p->motionVectorScale.y;
    d.frameTimeMs = p->frameTimeDelta;
    d.cameraNear = p->cameraNear; d.cameraFar = p->cameraFar; d.cameraFovY = p->cameraFovAngleVertical; d.viewSpaceToMeters = p->viewSpaceToMetersFactor;
    for (int k = 0; k < 3; k++) { d.cameraPosition[k] = p->cameraPosition[k]; d.cameraUp[k] = p->cameraUp[k]; d.cameraRight[k] = p->cameraRight[k]; d.cameraForward[k] = p->cameraForward[k]; }
    d.stampSlot = (uint32_t)g_fg->slot;
#if DLSSIV_DEV
    FgoListBegins(O, FGC_PREPARE, p->reset, g_fg->qN);
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
#endif
    const uint32_t rc = FgoSafePrepare(O, &d, raised);
#if DLSSIV_DEV
    QueryPerformanceCounter(&q1);
    FgoSlot& r = O->slot[g_fg->slot];
    r.cpu[0] = FgoUs(O, q1.QuadPart - q0.QuadPart);
    if (rc == DLSSIV_FG_OK) r.calls |= 1;
#endif
    return (ffxReturnCode_t)rc;
}

// ---- In FgDispatchSubmit ----
// Optical flow, then interpolate, where the stock arm calls AMD's one dispatch, from the same description and configure.
// Wire 3, by the count (FgHost::genNow): 1 below; 0 and 2 in FgoDispatchCounts.
__declspec(noinline) static ffxReturnCode_t FgoDispatchCounts(const ffxDispatchDescFrameGeneration* d, DWORD* raised);
__declspec(noinline) static ffxReturnCode_t FgOwnDispatch(const ffxDispatchDescFrameGeneration* d, DWORD* raised)
{
    FgOwn* const O = g_fg->own;
    *raised = 0;
    if (FgRefusedAfterRaise() || !O->ctx) return (ffxReturnCode_t)0x7FFFFFFF;
    if (g_fg->genNow != FEED_FG_G_GEN_ONE) return FgoDispatchCounts(d, raised);
    DlssivFgFrameDesc f = {};
        // The middle goes as interface 1's description, which either interface takes; another position only to an interface 2 library.
    const bool placed = O->position != 0.0f && O->said.iface >= 2u;
    f.size = placed ? (uint32_t)sizeof(f) : (uint32_t)DLSSIV_FG_FRAME_SIZE_1; f.position = placed ? O->position : 0.0f;
    f.flags = (d->reset ? DLSSIV_FG_RESET : 0u) | O->dbg;
    f.commandList = d->commandList; f.present = d->presentColor.resource; f.hudless = O->hud; f.output = d->outputs[0].resource;
    f.frameId = d->frameID;
    f.rectLeft = d->generationRect.left; f.rectTop = d->generationRect.top; f.rectWidth = d->generationRect.width; f.rectHeight = d->generationRect.height;
    f.transfer = d->backbufferTransferFunction; f.minLuminance = d->minMaxLuminance[0]; f.maxLuminance = d->minMaxLuminance[1];
    f.stampSlot = (uint32_t)g_fg->slot;
#if DLSSIV_DEV
    FgoListBegins(O, FGC_DISPATCH, d->reset, g_fg->gN);
    FgoSlot& r = O->slot[g_fg->slot];
    LARGE_INTEGER q0, q1, q2; QueryPerformanceCounter(&q0);
#endif
    uint32_t rc = FgoSafeFlow(O, &f, raised);
#if DLSSIV_DEV
    QueryPerformanceCounter(&q1);
    r.cpu[1] = FgoUs(O, q1.QuadPart - q0.QuadPart);
#endif
    if (rc != DLSSIV_FG_OK) return (ffxReturnCode_t)rc;
#if DLSSIV_DEV
    r.calls |= 2;
#endif
    rc = FgoSafeInterpolate(O, &f, raised);
#if DLSSIV_DEV
    QueryPerformanceCounter(&q2);
    r.cpu[2] = FgoUs(O, q2.QuadPart - q1.QuadPart);
    if (rc == DLSSIV_FG_OK) r.calls |= 4;
#endif
    return (ffxReturnCode_t)rc;
}

// ---- Wire 3: which counts this library makes ----
// Asked by FgBuild at a set of wire 3 (it keeps what the set's images allow): one always; flow-only and two at the thirds only on an
// interface 3 library, with this arm compiled against it. *why (may be null) says why when it is one alone.
__declspec(noinline) static uint32_t FgOwnGenCounts(const char** why)
{
    const FgOwn* const O = g_fg->own;
    if (!O) return FEED_FG_GENS_ONE;
#if FGO_GEN_STANDIN
    if (why) *why = "on the own arm built against interface 2's header (THE STAND-IN for interface 3: counts 0 and 2 are never made)";
    return FEED_FG_GENS_ONE;
#else
    if (O->said.iface < 3u)
    {
        if (why) *why = "on the own arm, whose library speaks an interface before 3 (no store apart: one frame a pair)";
        return FEED_FG_GENS_ONE;
    }
    return FEED_FG_GENS_ONE | FEED_FG_GENS_FLOW | FEED_FG_GENS_TWO;
#endif
}

// ---- In FgDispatchSubmit, for a count of 0 or 2 ----
// One list, one ring slot, one answer.
//   0  flow-only: the optical flow, then interpolate with DLSSIV_FG_STORE_ONLY: no picture, and the next pair's previous source
//      is this pair's
//   2  two at the thirds: the optical flow, interpolate(1/3, DLSSIV_FG_NO_STORE) into GEN, then interpolate(2/3) into GEN2,
//      which stores. One frame id: the second interpolate does not reset; each gets the pair's own flags.
// A count this library does not make is never asked (FgOnG checks gen_counts first) and is refused here all the same.
__declspec(noinline) static ffxReturnCode_t FgoDispatchCounts(const ffxDispatchDescFrameGeneration* d, DWORD* raised)
{
    FgOwn* const O = g_fg->own;
    const uint32_t gen = g_fg->genNow;
    if (gen > FEED_FG_G_GEN_TWO || !(FgOwnGenCounts(nullptr) & (1u << gen)) || (gen == FEED_FG_G_GEN_TWO && !g_fg->img[FEED_FG_GEN2])) return (ffxReturnCode_t)DLSSIV_FG_E_ARGUMENT;
    DlssivFgFrameDesc f = {};
    f.size = (uint32_t)sizeof(f);
    f.flags = (d->reset ? DLSSIV_FG_RESET : 0u) | O->dbg;
    f.commandList = d->commandList; f.present = d->presentColor.resource; f.hudless = O->hud; f.output = d->outputs[0].resource;
    f.frameId = d->frameID;
    f.rectLeft = d->generationRect.left; f.rectTop = d->generationRect.top; f.rectWidth = d->generationRect.width; f.rectHeight = d->generationRect.height;
    f.transfer = d->backbufferTransferFunction; f.minLuminance = d->minMaxLuminance[0]; f.maxLuminance = d->minMaxLuminance[1];
    f.stampSlot = (uint32_t)g_fg->slot;
#if DLSSIV_DEV
    FgoListBegins(O, FGC_DISPATCH, d->reset, g_fg->gN);
    FgoSlot& r = O->slot[g_fg->slot];
    LARGE_INTEGER q0, q1, q2; QueryPerformanceCounter(&q0);
#endif
    uint32_t rc = FgoSafeFlow(O, &f, raised);
#if DLSSIV_DEV
    QueryPerformanceCounter(&q1);
    r.cpu[1] = FgoUs(O, q1.QuadPart - q0.QuadPart);
#endif
    if (rc != DLSSIV_FG_OK) return (ffxReturnCode_t)rc;
#if DLSSIV_DEV
    r.calls |= 2;
#endif
    if (gen == FEED_FG_G_GEN_FLOW)
    {
        DlssivFgFrameDesc s = f; s.flags |= DLSSIV_FG_STORE_ONLY;
        rc = FgoSafeInterpolate(O, &s, raised);
    }
    else
    {
        DlssivFgFrameDesc a = f; a.flags |= DLSSIV_FG_NO_STORE; a.position = 1.0f / 3.0f;
        rc = FgoSafeInterpolate(O, &a, raised);
        if (rc == DLSSIV_FG_OK)
        {
            DlssivFgFrameDesc b = f; b.position = 2.0f / 3.0f; b.output = g_fg->img[FEED_FG_GEN2];
            rc = FgoSafeInterpolate(O, &b, raised);
        }
    }
#if DLSSIV_DEV
    QueryPerformanceCounter(&q2);
    r.cpu[2] = FgoUs(O, q2.QuadPart - q1.QuadPart);   // (both interpolates of a count of two, together)
    if (rc == DLSSIV_FG_OK) r.calls |= 4;
#endif
    return (ffxReturnCode_t)rc;
}

#if DLSSIV_DEV
// ---- Last in a list ----
// From FgPrepareSubmit and FgDispatchSubmit, behind the cost marks' closing stamps: this list's stamps resolved into their slot.
// Nothing without the stamps.
__declspec(noinline) static void FgOwnInList()
{
    FgOwn* const O = g_fg->own;
    if (!O || !O->stampsOn || !O->ctx || g_fg->raised || !O->slot[g_fg->slot].open) return;
    DlssivFgStampsDesc s = {};
    s.size = sizeof(s); s.op = DLSSIV_FG_STAMPS_RESOLVE; s.slot = (uint32_t)g_fg->slot; s.commandList = g_fg->list;
    FgoSafeStamps(O, &s);
}

// ---- After 'G' ----
// Behind the pair's answer: every list of this arm's that the fence says has retired is read, and every FGO_ROWS_EVERY pairs the
// rows are written. Nothing waits.
__declspec(noinline) static void FgOwnAfterG()
{
    FgOwn* const O = g_fg->own;
    if (!O || !O->ctx) return;
    O->pairs++;
    const UINT64 done = h.fence ? h.fence->GetCompletedValue() : 0;
    for (int i = 0; i < FG_RING; i++)
    {
        const FgoSlot& r = O->slot[i];
        if (r.open && g_fg->allocFence[i] != r.fenceWas && g_fg->allocFence[i] <= done) FgoCollect(O, i);
    }
    if (O->pairs % FGO_ROWS_EVERY == 0 || O->rowsAt > FGO_ROWS_BYTES / 2) FgoRowsWrite(O);
}
#endif

// ---- The set's end ----
// From FgTeardown, before the stock destroy, with the queue drained: every list is read, then the destroy in this arm's own frame.
// FgHost::ctx is null after, so the stock destroy finds nothing. Not after a raise. A shipped helper reads no lists here.
__declspec(noinline) static void FgOwnDestroy()
{
    FgOwn* const O = g_fg ? g_fg->own : nullptr;
    if (!O || !O->ctx) return;
#if DLSSIV_DEV
    for (int i = 0; i < FG_RING; i++)
    {
        FgoSlot& r = O->slot[i];
        if (!r.open) continue;
        if (g_fg->allocFence[i] != r.fenceWas) FgoCollect(O, i);
        else { r.open = false; O->notRead++; }
    }
    FgoRowsWrite(O);
    LogRecord("frame generation: fgown: set end: lists %llu, read %llu, not read %llu, stamps lost %llu, rows lost %llu", (unsigned long long)O->lists, (unsigned long long)O->read,
              (unsigned long long)O->notRead, (unsigned long long)O->stampsLost, (unsigned long long)O->rowsLost);
#endif
    if (!g_fg->raised) FgoSafeDestroy(O);
    O->ctx = nullptr; g_fg->ctx = nullptr;
#if DLSSIV_DEV
    O->stampsOn = false;
#endif
}
