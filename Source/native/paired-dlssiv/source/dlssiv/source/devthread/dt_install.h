// The device thread, part 5b of 5: install, arm, disarm, report.
// Install (at device creation, threaded >= 1): every device slot the mod's hooks do not hold gets its wrapper; slots they do
// hold keep the hook, and DLSS-IV.cpp points that hook's original at the wrapper (DtRouteOriginals). The resource classes'
// vtables are found from throwaway objects and hooked. Nothing records until the layer is armed.
// Refused, with the reason in the log, when anything below the mod in a vtable is not DXVK: a recorded call would run that
// module's code on the worker. It also refuses to arm a device that is not MULTITHREADED (DtMaybeArm says why).
// Arm (on the render thread, after a Present, 300 presents after install or a Reset): the shadow is loaded from DXVK and
// recording starts. Disarm (Reset, the device released): drain, stop, arm again later. A disarmed layer is the mod without
// it, call for call.
#pragma once

static bool  dtInstalled = false, dtArmable = false;
static LONG  dtArmPresents = 0, dtFrames = 0, dtArmCount = 0, dtDisarms = 0;
static char  dtWhyNot[280] = "";
static unsigned dtCpuCount = 0;                // logical processors the process may run on; the verdict prints it
// The report's window; the counters themselves stay monotonic. Here because DtMaybeArm re-bases the idle pair at every arm.
static LONGLONG dtIdleBase = 0, dtPresBase = 0;
DEVSTAT(static LONGLONG dtNRecBase = 0, dtNDwBase = 0;)   // the development report's line alone reads these three
DEVSTAT(static LONG     dtIdleSleepsBase = 0;)

static HMODULE DtModOf(const void* p)
{
    HMODULE m = nullptr;
    if (!p || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR)p, &m)) return nullptr;
    return m;
}
static const char* DtModName(const void* p, char* buf, size_t cap)
{
    HMODULE m = DtModOf(p);
    if (!m || !GetModuleFileNameA(m, buf, (DWORD)cap)) return "no module";
    const char* s = strrchr(buf, '\\');
    return s ? s + 1 : buf;
}
static void DtRefuse(const char* fmt, ...)
{
    if (!dtArmable) return;                 // keep the first reason
    dtArmable = false;
    va_list a; va_start(a, fmt);
    _vsnprintf_s(dtWhyNot, sizeof(dtWhyNot), _TRUNCATE, fmt, a);
    va_end(a);
}

// Windows' own compatibility hooks on the device are left where they are. apphelp.dll's 8 and 16-bit colour mitigation
// (DWM8And16BitMitigation) hooks a Direct3D 9 device's QueryInterface, Release, CreateAdditionalSwapChain and Reset (slots 0,
// 2, 13 and 16) and nothing else (tools/apphelp-probe). Windows applies it to a program with no Windows 8 or later manifest,
// as GTA IV is, in compatibility mode for Windows 7 or 8, in reduced colour mode, and on the first run of an executable from a
// new path; its hooks are then in DXVK's vtable before the mod's. They are let stand because of where the layer calls them:
// - QueryInterface, CreateAdditionalSwapChain and Reset only on the calling thread, never on the worker (a PASS, a drained
//   SYNC, and dt_Reset, which drains, unarms and calls directly);
// - Release on whichever thread releases, the worker among them: a MULTITHREADED device may be released from any thread, and
//   the probe ran the real hook from several threads during Resets without fault.
// Anything else (another module, or apphelp on another slot) refuses as before. tools/perfproof/dt_test.cpp holds the rule
// (its `twin` mode). A call of one of these made on the worker by other code (an overlay inside a replayed Present) goes to
// the slot's original, as every wrapper does there. Only Windows' own file: a module of that name outside the system folders
// is refused (DtForeignDirOk).
static bool DtForeignSlotOk(int slot, const char* mod)   // mod: the file name of the module that owned the slot's function
{
    return (slot == 0 || slot == 2 || slot == 13 || slot == 16) && mod && !_stricmp(mod, "apphelp.dll");
}
// Is this module's file in a Windows system folder? A 32-bit process is told System32 for a file that is in SysWOW64, so
// both are asked for. DT_TEST_FOREIGN_DIR is the harness's: the folder of its stand-in, which is no system file.
static bool DtForeignDirOk(HMODULE m)
{
    char full[MAX_PATH], sys[MAX_PATH];
    if (!m || !GetModuleFileNameA(m, full, sizeof(full))) return false;
    char* cut = strrchr(full, '\\');
    if (!cut) return false;
    *cut = 0;
    if (GetSystemDirectoryA(sys, sizeof(sys)) && !_stricmp(full, sys)) return true;
    if (GetSystemWow64DirectoryA(sys, sizeof(sys)) && !_stricmp(full, sys)) return true;
#ifdef DT_TEST_FOREIGN_DIR
    if (DT_TEST_FOREIGN_DIR(full)) return true;
#endif
    return false;
}

// The mod's slots, taken back when Windows puts its own back. Windows' layer writes its four hooks into the device's vtable
// again every time another device is made, and DXVK's devices share one vtable: they land over the mod's hooks and wrappers,
// so those calls then go around the mod (a Reset that skips hkReset and dt_Reset leaves the worker replaying). Another mod's
// scratch device (ENB, ReShade, IV-SDK .NET's ImGui pass) is such a second device.
// The rule is the CreateDevice slot's (IvtCheckCreateDeviceSlot, "which put the original pointer back"): a slot that holds
// again exactly what it held before the mod's hooks (dtDx) was put back by that function's module, and writing the mod's
// function over it again rebuilds the chain. A hook someone wrote over the mod's later is not that value and is left alone.
// DtSnapSlots runs once every hook and wrapper is written; DtRetakeSlots after every CreateDevice through the mod and once a
// frame (hkEndScene), where one compare of the table is the cost.
// - One at a time: two threads inside WriteSlot's VirtualProtect pair on one page would undo each other; dtVtBusy is a
//   try-lock, and the loser looks again next frame.
// - The frame's look waits a frame: Windows writes its four slots one after another, so a difference is acted on at the next
//   look. The look after a CreateDevice through the mod's own hook does not wait: Windows has returned on that thread.
// - The table is published last and read under the same lock, so no look reads it half copied.
static void*  dtVtSnap[119];
static void** volatile dtVtSnapOf = nullptr;
static volatile LONG dtVtBusy = 0;
static bool   dtVtMoved = false;         // the frame's look found the table different last time (that thread's alone)
static LONG   dtRetakes = 0;             // slots taken back this session
static void DtSnapSlots(void** vt)
{
    while (InterlockedCompareExchange(&dtVtBusy, 1, 0) != 0) YieldProcessor();
    InterlockedExchangePointer((void* volatile*)&dtVtSnapOf, nullptr);
    memcpy(dtVtSnap, vt, sizeof(dtVtSnap));
    InterlockedExchangePointer((void* volatile*)&dtVtSnapOf, vt);
    InterlockedExchange(&dtVtBusy, 0);
}
static int DtRetakeSlots(const char* when, bool settle = false)
{
    void** const vt = dtVtSnapOf;
    if (!vt) return 0;
    if (!memcmp(vt, dtVtSnap, sizeof(dtVtSnap))) { if (settle) dtVtMoved = false; return 0; }
    if (settle && !dtVtMoved) { dtVtMoved = true; return 0; }
    if (InterlockedCompareExchange(&dtVtBusy, 1, 0) != 0) return 0;
    int n = 0; char said[48] = "", mb[MAX_PATH]; const char* mod = "";
    if (dtVtSnapOf == vt)
        for (int i = 0; i < 119; i++)
        {
            void* const now = vt[i];
            if (!dtVtSnap[i] || now == dtVtSnap[i] || now != dtDx[i]) continue;   // still the mod's; or someone's hook over the mod's, which is theirs to keep
            if (!WriteSlot(vt, i, dtVtSnap[i])) continue;
            if (!n) mod = DtModName(now, mb, sizeof(mb));
            const size_t at = strlen(said);
            if (at + 6 < sizeof(said)) _snprintf_s(said + at, sizeof(said) - at, _TRUNCATE, "%s%d", at ? ", " : "", i);
            n++;
        }
    InterlockedExchange(&dtVtBusy, 0);
    if (settle) dtVtMoved = false;
    if (n)
    {
        const LONG total = InterlockedExchangeAdd(&dtRetakes, n) + n;
        if (total <= 16)      // a session's first few, by name; a module that does it every frame is not narrated
            LogRecord("  device slot%s %s held again what stood there before the mod's hooks (%s's, written back %s): TAKEN BACK", n > 1 ? "s" : "", said, mod, when);
    }
    return n;
}

// One resource slot: remember DXVK's function, put the wrapper in. A slot whose function is not DXVK's refuses the layer.
static void DtHookRes(void** vt, int slot, void* fn, void** orig, HMODULE dxvk, const char* cls)
{
    if (!vt) { DtRefuse("no %s object to find its vtable", cls); return; }
    if (vt[slot] == fn) return;             // a second install: already ours
    if (DtModOf(vt[slot]) != dxvk)
    {
        char b[MAX_PATH];
        DtRefuse("%s slot %d belongs to %s, not DXVK", cls, slot, DtModName(vt[slot], b, sizeof(b)));
        return;
    }
    *orig = vt[slot];
    if (!WriteSlot(vt, slot, fn)) DtRefuse("%s slot %d would not take the hook", cls, slot);
}
static void** DtVtOf(IUnknown* o) { return o ? *(void***)o : nullptr; }
// Every path that makes a surface must give the same class, or one Release hook
// would call another class's Release. Checked, not assumed.
static void DtSameClass(void** want, IUnknown* o, const char* what)
{
    if (o && want && DtVtOf(o) != want) DtRefuse("a %s is a different class from a texture level", what);
}

// ---- a DXVK option, read the way DXVK 3.1 reads it ----
// util/config/config.cpp: $DXVK_CONFIG_FILE, else dxvk.conf in the working directory, then $DXVK_CONFIG split at ';'. A
// [name] line scopes what follows to the exe of exactly that name; each source starts unscoped. A key is [0-9A-Za-z._]; a
// value runs to the next whitespace, quotes dropped; the last one set wins; a bool is "true" or "false" in any case, anything
// else the default. DXVK's GTAIV profile sets neither option the layer asks about.
static __forceinline bool DtCfgWs(char c)  { return c == ' ' || c == '\t' || c == '\r'; }
static __forceinline bool DtCfgKey(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '.' || c == '_'; }
static void DtDxvkLine(const char* s, size_t len, const char* key, const char* exe, bool* active, char* val, bool* have)
{
    size_t n = 0;
    while (n < len && DtCfgWs(s[n])) n++;
    if (n < len && s[n] == '[')
    {
        n++;
        size_t e = len - 1;
        while (e > n && s[e] != ']') e--;
        const size_t kl = e > n ? e - n : 0;
        *active = kl == strlen(exe) && !memcmp(s + n, exe, kl);
        return;
    }
    const size_t k0 = n;
    while (n < len && DtCfgKey(s[n])) n++;
    const size_t kl = n - k0;
    while (n < len && DtCfgWs(s[n])) n++;
    if (n >= len || s[n] != '=') return;
    n++;
    while (n < len && DtCfgWs(s[n])) n++;
    char v[64]; size_t vl = 0; bool q = false, junk = false;
    for (; n < len; n++)
    {
        if (!q && DtCfgWs(s[n])) break;
        if (s[n] == '"') q = !q;
        else if (vl < sizeof(v) - 1) v[vl++] = s[n];
        else junk = true;                                 // too long to be a bool: stored, and not one
    }
    v[vl] = 0;
    if (!*active || kl != strlen(key) || memcmp(s + k0, key, kl)) return;
    strcpy_s(val, 64, junk ? "?" : v);
    *have = true;
}
// One source: the file split at '\n', or the environment's line split at ';'.
static void DtDxvkParse(const char* t, size_t len, char sep, const char* key, const char* exe, char* val, bool* have)
{
    bool active = true;
    for (size_t a = 0; a <= len; )
    {
        size_t b = a;
        while (b < len && t[b] != sep) b++;
        DtDxvkLine(t + a, b - a, key, exe, &active, val, have);
        a = b + 1;
    }
}
static bool DtDxvkBool(const char* key, bool def)
{
    char exe[MAX_PATH] = "", full[MAX_PATH], path[MAX_PATH];
    const DWORD fn = GetModuleFileNameA(nullptr, full, MAX_PATH);
    if (fn && fn < MAX_PATH) { const char* b = strrchr(full, '\\'); strcpy_s(exe, b ? b + 1 : full); }
    const DWORD pe = GetEnvironmentVariableA("DXVK_CONFIG_FILE", path, MAX_PATH);
    if (!pe || pe >= MAX_PATH) strcpy_s(path, "dxvk.conf");
    char val[64] = ""; bool have = false;
    const HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        const DWORD sz = GetFileSize(h, nullptr);
        if (sz != INVALID_FILE_SIZE && sz <= (4u << 20))
            if (char* t = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1))
            {
                DWORD got = 0;
                if (ReadFile(h, t, sz, &got, nullptr)) DtDxvkParse(t, got, '\n', key, exe, val, &have);
                HeapFree(GetProcessHeap(), 0, t);
            }
        CloseHandle(h);
    }
    char env[8192];
    const DWORD ce = GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
    if (ce && ce < sizeof(env)) DtDxvkParse(env, ce, ';', key, exe, val, &have);
    if (!have) return def;
    return !_stricmp(val, "true") ? true : !_stricmp(val, "false") ? false : def;
}

// The same reader with the folder as an argument: at load the ASI loader leaves the working directory at plugins\, but the
// game's DXVK instance is made with the game folder current, so a setter that runs at load reads from the exe's folder.
// A DXVK_CONFIG of 260 characters or more is ignored whole, as DXVK does (util_env.cpp). Says which source carried the value;
// the environment is parsed after the file and wins. DXVK_CONFIG_FILE is opened as given when absolute or rooted (a Proton
// Unix path is rooted), else from the game folder, as DXVK does.
static bool DtDxvkValueAt(const char* dir, const char* key, char* val, bool* fromFile, bool* fromEnv)
{
    *fromFile = false; *fromEnv = false; val[0] = 0;
    char exe[MAX_PATH] = "", full[MAX_PATH], path[MAX_PATH] = "";
    const DWORD fn = GetModuleFileNameA(nullptr, full, MAX_PATH);
    if (fn && fn < MAX_PATH) { const char* b = strrchr(full, '\\'); strcpy_s(exe, b ? b + 1 : full); }
    char given[MAX_PATH] = "";
    const DWORD pe = GetEnvironmentVariableA("DXVK_CONFIG_FILE", given, MAX_PATH);
    const bool absolute = pe && pe < MAX_PATH && (given[0] == '\\' || given[0] == '/' || given[1] == ':');
    if (pe && pe < MAX_PATH && absolute) strcpy_s(path, given);
    else _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s%s%s", dir ? dir : "", (dir && dir[0] && dir[strlen(dir) - 1] != '\\') ? "\\" : "",
                     (pe && pe < MAX_PATH) ? given : "dxvk.conf");
    bool have = false;
    const HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        const DWORD sz = GetFileSize(h, nullptr);
        if (sz != INVALID_FILE_SIZE && sz <= (4u << 20))
            if (char* tt = (char*)HeapAlloc(GetProcessHeap(), 0, sz + 1))
            {
                DWORD got = 0;
                if (ReadFile(h, tt, sz, &got, nullptr)) DtDxvkParse(tt, got, '\n', key, exe, val, &have);
                HeapFree(GetProcessHeap(), 0, tt);
            }
        CloseHandle(h);
    }
    *fromFile = have;
    char env[8192]; bool haveEnv = false;
    const DWORD ce = GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
    if (ce && ce < 260) { char v2[64] = ""; DtDxvkParse(env, ce, ';', key, exe, v2, &haveEnv); if (haveEnv) { strcpy_s(val, 64, v2); *fromEnv = true; } }
    return have || haveEnv;
}
// With every argument it needs, so tools/perfproof/dt_test.cpp can call it too.
// Returns true when the device slots were wrapped, which is when the caller must
// route its own hooks' originals to the wrappers.
static bool DtInstall(IDirect3DDevice9* dev, void** vtbl, int cfg, const DWORD* vsCode, const DWORD* psCode)
{
    if (dtInstalled || cfg < 1 || !dev || !vtbl) return false;
    dtCfg = cfg; DEVSTAT(dtValidate = cfg >= 2;) dtArmable = true;
    // The two DXVK options the buffer locks depend on: see DtBufNoOverwriteOk (dt_res.h).
    dtDrawTimeUploads = DtDxvkBool("d3d9.forceDrawTimeBufferUpload", false);
    dtDirectMapping   = DtDxvkBool("d3d9.allowDirectBufferMapping", true);
    if (!dtDx[0] || !dtDx[118]) { LogRecord("  threaded: the device's own functions were not captured: off"); return false; }
        // A core of its own: the worker spins before it sleeps, so on a process allowed two logical processors it shares a core with
    // the game's threads. The count on the install line is the first thing to read when the 2 ms verdict (DtReport) fires.
    // Nothing is installed when it refuses, so such a machine pays nothing. A failed read (a process spanning processor groups
    // answers zero masks) is not "0 processors".
    DWORD_PTR dtProcMask = 0, dtSysMask = 0;
    const BOOL affOk = GetProcessAffinityMask(GetCurrentProcess(), &dtProcMask, &dtSysMask) && dtProcMask;
    unsigned dtCpus = 0;
    for (DWORD_PTR m = dtProcMask; m; m &= m - 1) dtCpus++;
    dtCpuCount = dtCpus;
    if (!affOk)
        LogRecord("  threaded: the process affinity could not be read (more than one processor group?): the core count is not checked");
    else if (dtCpus < 4)
    {
        DtRefuse("the process may run on only %u logical processor(s) (affinity 0x%lX); the worker would share a core with the game's"
                 " own threads and cost more than it saves", dtCpus, (unsigned long)dtProcMask);
        LogRecord("  threaded: REFUSED, nothing is installed: %s", dtWhyNot);
        IvtWarn("threaded=%d is set, but the device thread is not running: %s", cfg, dtWhyNot);
        return false;                          // the caller routes nothing; dtInstalled stays false, so nothing here is ever entered
    }
    const HMODULE dxvk = DtModOf(vtbl);
    char dtKept[96] = ""; int dtKeptN = 0;     // the slots Windows' compatibility layer holds, left as they are (DtForeignSlotOk)
        // Every foreign slot is named in a refusal, not the first alone: a player's log is the only view of another module's hooks.
    // Whole numbers only: an entry that does not fit ends the list with "...", and the count beside it is always true.
    char dtAll[48] = "", dtBadMod[48] = ""; int dtAllN = 0, dtBad = -1; bool dtAllCut = false, dtBadDir = false;
    for (int i = 0; i < 119; i++)
        if (DtModOf(dtDx[i]) != dxvk)
        {
            char b[MAX_PATH];
            const char* mod = DtModName(dtDx[i], b, sizeof(b));
            const size_t all = strlen(dtAll);
            if (!dtAllCut)
            {
                if (all + 10 < sizeof(dtAll)) _snprintf_s(dtAll + all, sizeof(dtAll) - all, _TRUNCATE, "%s%d", all ? ", " : "", i);
                else { strcat_s(dtAll, ", ..."); dtAllCut = true; }
            }
            dtAllN++;
            const bool nameOk = DtForeignSlotOk(i, mod), dirOk = nameOk && DtForeignDirOk(DtModOf(dtDx[i]));
            if (!nameOk || !dirOk)
            {
                if (dtBad < 0) { dtBad = i; strncpy_s(dtBadMod, mod, _TRUNCATE); dtBadDir = nameOk && !dirOk; }
            }
            else
            {
                const size_t at = strlen(dtKept);
                _snprintf_s(dtKept + at, sizeof(dtKept) - at, _TRUNCATE, "%s%d (%s)", at ? ", " : "", i, dtDevName[i]);
                dtKeptN++;
            }
        }
    const char* const dtBadWhere = dtBadDir ? " (a file of that name outside Windows' own folders)" : "";
    if (dtBad >= 0 && dtAllN > 1)
        DtRefuse("device slot %d (%s) belonged to %s%s before the mod's hooks, not DXVK (%d slots were another module's: %s)",
                 dtBad, dtDevName[dtBad], dtBadMod, dtBadWhere, dtAllN, dtAll);
    else if (dtBad >= 0)
        DtRefuse("device slot %d (%s) belonged to %s%s before the mod's hooks, not DXVK", dtBad, dtDevName[dtBad], dtBadMod, dtBadWhere);
    if (dtArmable && dtKeptN)
        LogRecord("  threaded: device slot%s %s belonged to apphelp.dll before the mod's hooks (Windows' 8 and 16-bit colour compatibility layer): installed over them",
                  dtKeptN > 1 ? "s" : "", dtKept);
        // A refusal found before anything is installed installs nothing (no wrappers, resource hooks, ring or worker): the caller
    // routes nothing, which is threaded=0.
    if (!dtArmable)
    {
        LogRecord("  threaded: REFUSED, nothing is installed: %s", dtWhyNot);
        IvtWarn("threaded=%d is set, but the device thread is not running: %s", cfg, dtWhyNot);
        return false;
    }

    // ---- throwaway objects, one of every class ------------------------------------
    IDirect3DVertexBuffer9 *vb = nullptr, *vbDyn = nullptr; IDirect3DIndexBuffer9 *ib = nullptr, *ibDyn = nullptr;
    IDirect3DTexture9 *tx = nullptr, *txRt = nullptr; IDirect3DCubeTexture9* cu = nullptr; IDirect3DVolumeTexture9* vo = nullptr;
    IDirect3DSurface9 *sf = nullptr, *sfRt = nullptr, *sfCube = nullptr, *sfOff = nullptr, *sfRtS = nullptr, *sfDs = nullptr, *sfBb = nullptr;
    IDirect3DVolume9* vl = nullptr; IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    IDirect3DVertexDeclaration9* de = nullptr; IDirect3DQuery9* q = nullptr; IDirect3DStateBlock9* sb = nullptr;
    IDirect3DSwapChain9* sc = nullptr;
    const D3DVERTEXELEMENT9 el[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 }, D3DDECL_END() };
    rsOwnCreate++;
    dev->CreateVertexBuffer(64, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr);
    dev->CreateVertexBuffer(64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vbDyn, nullptr);
    dev->CreateIndexBuffer(64, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, nullptr);
    dev->CreateIndexBuffer(64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ibDyn, nullptr);
    dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tx, nullptr);
    dev->CreateTexture(4, 4, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &txRt, nullptr);
    dev->CreateCubeTexture(4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cu, nullptr);
    dev->CreateVolumeTexture(4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vo, nullptr);
    if (tx)   tx->GetSurfaceLevel(0, &sf);
    if (txRt) txRt->GetSurfaceLevel(0, &sfRt);
    if (cu)   cu->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 0, &sfCube);
    if (vo)   vo->GetVolumeLevel(0, &vl);
    dev->CreateOffscreenPlainSurface(4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sfOff, nullptr);
    dev->CreateRenderTarget(4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &sfRtS, nullptr);
    dev->CreateDepthStencilSurface(4, 4, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &sfDs, nullptr);
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &sfBb);
    if (vsCode) dev->CreateVertexShader(vsCode, &vs);
    if (psCode) dev->CreatePixelShader(psCode, &ps);
    dev->CreateVertexDeclaration(el, &de);
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &q);
    dev->CreateStateBlock(D3DSBT_ALL, &sb);
    dev->GetSwapChain(0, &sc);
    rsOwnCreate--;

    dtVt[DTC_VB] = DtVtOf(vb); dtVt[DTC_IB] = DtVtOf(ib); dtVt[DTC_TEX] = DtVtOf(tx); dtVt[DTC_CUBE] = DtVtOf(cu);
    dtVt[DTC_VTEX] = DtVtOf(vo); dtVt[DTC_SURF] = DtVtOf(sf); dtVt[DTC_VOL] = DtVtOf(vl); dtVt[DTC_VS] = DtVtOf(vs);
    dtVt[DTC_PS] = DtVtOf(ps); dtVt[DTC_DECL] = DtVtOf(de); dtVt[DTC_QUERY] = DtVtOf(q); dtVt[DTC_SB] = DtVtOf(sb);
    dtVt[DTC_SC] = DtVtOf(sc);
    DtSameClass(dtVt[DTC_VB], vbDyn, "dynamic vertex buffer");
    DtSameClass(dtVt[DTC_IB], ibDyn, "dynamic index buffer");
    DtSameClass(dtVt[DTC_TEX], txRt, "render-target texture");
    DtSameClass(dtVt[DTC_SURF], sfRt, "render-target texture level");
    DtSameClass(dtVt[DTC_SURF], sfCube, "cube face");
    DtSameClass(dtVt[DTC_SURF], sfOff, "offscreen plain surface");
    DtSameClass(dtVt[DTC_SURF], sfRtS, "render target surface");
    DtSameClass(dtVt[DTC_SURF], sfDs, "depth-stencil surface");
    DtSameClass(dtVt[DTC_SURF], sfBb, "back buffer");
    for (int c = 0; c < DTC_N; c++)
        for (int k = 0; k < c; k++)
            if (dtVt[c] && dtVt[c] == dtVt[k]) DtRefuse("%s and %s share a vtable", kDtClass[c], kDtClass[k]);
    DT_SB_PROBE(dev, vb, de)                  // DXVK's state-block rules, probed before any hook (dt_sbtab.h)

    // ---- the hooks: Release everywhere, then each class's ordered methods ------------
    DtHookRes(dtVt[DTC_VB],   2, (void*)&dtRelease<DTC_VB>,   &dtRel[DTC_VB],   dxvk, kDtClass[DTC_VB]);
    DtHookRes(dtVt[DTC_IB],   2, (void*)&dtRelease<DTC_IB>,   &dtRel[DTC_IB],   dxvk, kDtClass[DTC_IB]);
    DtHookRes(dtVt[DTC_TEX],  2, (void*)&dtRelease<DTC_TEX>,  &dtRel[DTC_TEX],  dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_CUBE], 2, (void*)&dtRelease<DTC_CUBE>, &dtRel[DTC_CUBE], dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_VTEX], 2, (void*)&dtRelease<DTC_VTEX>, &dtRel[DTC_VTEX], dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_SURF], 2, (void*)&dtRelease<DTC_SURF>, &dtRel[DTC_SURF], dxvk, kDtClass[DTC_SURF]);
    DtHookRes(dtVt[DTC_VOL],  2, (void*)&dtRelease<DTC_VOL>,  &dtRel[DTC_VOL],  dxvk, kDtClass[DTC_VOL]);
    DtHookRes(dtVt[DTC_VS],   2, (void*)&dtRelease<DTC_VS>,   &dtRel[DTC_VS],   dxvk, kDtClass[DTC_VS]);
    DtHookRes(dtVt[DTC_PS],   2, (void*)&dtRelease<DTC_PS>,   &dtRel[DTC_PS],   dxvk, kDtClass[DTC_PS]);
    DtHookRes(dtVt[DTC_DECL], 2, (void*)&dtRelease<DTC_DECL>, &dtRel[DTC_DECL], dxvk, kDtClass[DTC_DECL]);
    DtHookRes(dtVt[DTC_QUERY],2, (void*)&dtRelease<DTC_QUERY>,&dtRel[DTC_QUERY],dxvk, kDtClass[DTC_QUERY]);
    DtHookRes(dtVt[DTC_SB],   2, (void*)&dtRelease<DTC_SB>,   &dtRel[DTC_SB],   dxvk, kDtClass[DTC_SB]);
    DtHookRes(dtVt[DTC_SC],   2, (void*)&dtRelease<DTC_SC>,   &dtRel[DTC_SC],   dxvk, kDtClass[DTC_SC]);
    DtHookRes(dtVt[DTC_VB], 11, (void*)&dtVbLock,   &dtVbLockO,   dxvk, kDtClass[DTC_VB]);
    DtHookRes(dtVt[DTC_VB], 12, (void*)&dtVbUnlock, &dtVbUnlockO, dxvk, kDtClass[DTC_VB]);
    DtHookRes(dtVt[DTC_IB], 11, (void*)&dtIbLock,   &dtIbLockO,   dxvk, kDtClass[DTC_IB]);
    DtHookRes(dtVt[DTC_IB], 12, (void*)&dtIbUnlock, &dtIbUnlockO, dxvk, kDtClass[DTC_IB]);
    DtHookRes(dtVt[DTC_TEX], 11, (void*)&dtTexSetLOD,  &dtTexSetLODO,  dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_TEX], 14, (void*)&dtTexAutoGen, &dtTexAutoGenO, dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_TEX], 16, (void*)&dtTexGenMips, &dtTexGenMipsO, dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_TEX], 19, (void*)&dtTexLock,    &dtTexLockO,    dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_TEX], 20, (void*)&dtTexUnlock,  &dtTexUnlockO,  dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_TEX], 21, (void*)&dtTexDirty,   &dtTexDirtyO,   dxvk, kDtClass[DTC_TEX]);
    DtHookRes(dtVt[DTC_CUBE], 11, (void*)&dtCubeSetLOD,  &dtCubeSetLODO,  dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_CUBE], 14, (void*)&dtCubeAutoGen, &dtCubeAutoGenO, dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_CUBE], 16, (void*)&dtCubeGenMips, &dtCubeGenMipsO, dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_CUBE], 19, (void*)&dtCubeLock,    &dtCubeLockO,    dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_CUBE], 20, (void*)&dtCubeUnlock,  &dtCubeUnlockO,  dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_CUBE], 21, (void*)&dtCubeDirty,   &dtCubeDirtyO,   dxvk, kDtClass[DTC_CUBE]);
    DtHookRes(dtVt[DTC_VTEX], 11, (void*)&dtVTexSetLOD,  &dtVTexSetLODO,  dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_VTEX], 14, (void*)&dtVTexAutoGen, &dtVTexAutoGenO, dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_VTEX], 16, (void*)&dtVTexGenMips, &dtVTexGenMipsO, dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_VTEX], 19, (void*)&dtVTexLock,    &dtVTexLockO,    dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_VTEX], 20, (void*)&dtVTexUnlock,  &dtVTexUnlockO,  dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_VTEX], 21, (void*)&dtVTexDirty,   &dtVTexDirtyO,   dxvk, kDtClass[DTC_VTEX]);
    DtHookRes(dtVt[DTC_SURF], 13, (void*)&dtSurfLock,   &dtSurfLockO,   dxvk, kDtClass[DTC_SURF]);
    DtHookRes(dtVt[DTC_SURF], 14, (void*)&dtSurfUnlock, &dtSurfUnlockO, dxvk, kDtClass[DTC_SURF]);
    DtHookRes(dtVt[DTC_SURF], 15, (void*)&dtSurfGetDC,  &dtSurfGetDCO,  dxvk, kDtClass[DTC_SURF]);
    DtHookRes(dtVt[DTC_SURF], 16, (void*)&dtSurfRelDC,  &dtSurfRelDCO,  dxvk, kDtClass[DTC_SURF]);
    DtHookRes(dtVt[DTC_VOL],  9, (void*)&dtVolLock,    &dtVolLockO,    dxvk, kDtClass[DTC_VOL]);
    DtHookRes(dtVt[DTC_VOL], 10, (void*)&dtVolUnlock,  &dtVolUnlockO,  dxvk, kDtClass[DTC_VOL]);
    DtHookRes(dtVt[DTC_QUERY], 6, (void*)&dtQIssue,   &dtQIssueO,   dxvk, kDtClass[DTC_QUERY]);
    DtHookRes(dtVt[DTC_QUERY], 7, (void*)&dtQGetData, &dtQGetDataO, dxvk, kDtClass[DTC_QUERY]);
    DtHookRes(dtVt[DTC_SB], 4, (void*)&dtSbCapture, &dtSbCaptureO, dxvk, kDtClass[DTC_SB]);
    DtHookRes(dtVt[DTC_SB], 5, (void*)&dtSbApply,   &dtSbApplyO,   dxvk, kDtClass[DTC_SB]);
    DtHookRes(dtVt[DTC_SC], 3, (void*)&dtScPresent, &dtScPresentO, dxvk, kDtClass[DTC_SC]);
    DtHookRes(dtVt[DTC_SC], 4, (void*)&dtScFront,   &dtScFrontO,   dxvk, kDtClass[DTC_SC]);
    DtHookRes(dtVt[DTC_SC], 5, (void*)&dtScBack,    &dtScBackO,    dxvk, kDtClass[DTC_SC]);

    // The throwaways go back through the hooks just installed: not armed, so direct.
    IUnknown* const tmp[] = { sc, sb, q, de, ps, vs, sfBb, sfDs, sfRtS, sfOff, vl, sfCube, sfRt, sf, vo, cu, txRt, tx, ibDyn, ib, vbDyn, vb };
    for (IUnknown* o : tmp) if (o) o->Release();

        // ---- the device: every slot the mod's hooks do not hold ----
    // A slot that differs from dtDx is taken as the mod's own hook. Windows' layer may have written its hook back over one since
    // HookVtable (another thread's device), which would then get a wrapper and drop hkReset from the chain: put back first what
    // was put back.
    DtRetakeSlots("before the layer's wrappers");
    int wrapped = 0, behindOurs = 0;
    for (int i = 0; i < 119; i++)
    {
        if (vtbl[i] != dtDx[i]) { behindOurs++; continue; }   // the mod's: DLSS-IV.cpp routes its original
        if (WriteSlot(vtbl, i, dtDevFn[i])) wrapped++;
        else DtRefuse("device slot %d (%s) would not take the wrapper", i, dtDevName[i]);
    }
    InitializeCriticalSection(&dtFrLock);
    dtFrInit = true;
        // A refusal found after the resource vtables were hooked cannot unwind them, but need not make the ring or the worker. The
    // timebase is set regardless, so dtQpf is never left 0.
    DtTimebase();
    if (dtArmable && !DtStart(16u << 20)) DtRefuse("the ring or the worker could not be created");
    dtInstalled = true;
    if (dtArmable)
        LogHealth("  threaded: installed (threaded=%d): %d device slots wrapped and %d behind the mod's own hooks, %d resource"
            " classes, a %lu KB ring, worker thread %lu, %u logical processors (affinity 0x%lX); DXVK's forceDrawTimeBufferUpload=%d"
            " allowDirectBufferMapping=%d. Arms on the render thread 300 presents from now%s%s.",
            cfg, wrapped, behindOurs, (int)DTC_N, (unsigned long)(dtRingDw / 256), (unsigned long)dtWorkerTid, dtCpus, (unsigned long)dtProcMask,
            dtDrawTimeUploads ? 1 : 0, dtDirectMapping ? 1 : 0,
            dtValidate ? ", and every shadow answer is checked against DXVK (validation)" : "",
            dtDrawTimeUploads ? ". DXVK's config sets d3d9.forceDrawTimeBufferUpload, so a NOOVERWRITE lock of a buffer"
                                " DXVK uploads at draw time drains first" : "");
    else
    {
        LogRecord("  threaded: REFUSED, every call passes straight through: %s", dtWhyNot);
        IvtWarn("threaded=%d is set, but the device thread is not running: %s", cfg, dtWhyNot);
    }
    return true;
}

static void DtMaybeArm(IDirect3DSwapChain9* sc, HRESULT hr)
{
    if (!dtInstalled || !dtArmable || dtArmed) return;
    if (__readfsdword(0x24) != renderTid) return;          // the thread that runs EndScene
        // 300 presents that worked, in a row: DXVK's GTAIV profile turns on d3d9.deviceLossOnFocusLoss, so a player in
    // exclusive fullscreen who switches away has a lost device and failing Presents until the game Resets; arming then would
    // record for a device about to be Reset.
    if (FAILED(hr)) { dtArmPresents = 0; return; }
    if (++dtArmPresents < 300) return;
    IDirect3DDevice9* dev = nullptr;
    if (FAILED(sc->GetDevice(&dev)) || !dev) return;
    dev->Release();                                        // not armed: goes straight to DXVK
    DT_SB_ARM_DEFER(dev)                                   // not while a state-block recording is open (dt_sbtab.h)
        // Only a MULTITHREADED device: a PASS call, and every call from another thread, reaches DXVK while the worker may be inside
    // it, which is safe only because DXVK then takes its device lock on every entry (d3d9_multithread.h). GTA IV creates its
    // device with it (flags 0x44). And no software vertex processing, under which DXVK ignores the NOOVERWRITE the buffer locks
    // rely on (dt_res.h).
    D3DDEVICE_CREATION_PARAMETERS cp = {};
    typedef HRESULT(__stdcall* DtCP_t)(IDirect3DDevice9*, D3DDEVICE_CREATION_PARAMETERS*);
    if (FAILED(((DtCP_t)dtDx[9])(dev, &cp)) || !(cp.BehaviorFlags & D3DCREATE_MULTITHREADED)
        || (cp.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING))
    {
        DtRefuse("the device's behaviour flags are 0x%08lX, and the layer needs D3DCREATE_MULTITHREADED"
            " without software vertex processing", (unsigned long)cp.BehaviorFlags);
        LogRecord("  threaded: REFUSED to arm, every call passes straight through: %s", dtWhyNot);
        IvtWarn("threaded=%d is set, but the device thread is not running: %s", dtCfg, dtWhyNot);
        return;
    }
    dtDev = dev;
    dtProducer = __readfsdword(0x24);
    DT_SB_AT_ARM()
    dtSbRec = false;
    memset(dtQStamp, 0, sizeof(dtQStamp));           // ring indices do not survive an arm (dt_res.h, a query's GetData)
    DtShadowLoad(dev);                                     // direct: the ring is empty and idle
        // The worker runs at the render thread's priority, and the line says which: a game raised by a launcher, or a boosted render
    // thread, would leave a NORMAL worker behind; the set can fail.
    const int  prRender = GetThreadPriority(GetCurrentThread());
    const BOOL prSet    = SetThreadPriority(dtWorker, prRender);
    dtArmCount++;
    _InterlockedExchange(&dtDisarmAsk, 0);                 // this thread has passed 300 wrapper entries since any ask; belt and braces
        // The idle window starts here: idle time before this arm is not headroom against the armed frames the report divides by.
    dtIdleBase = InterlockedCompareExchange64(&dtIdleTicksSum, 0, 0);
    DEVSTAT(dtIdleSleepsBase = dtIdleSleeps;)
#if DLSSIV_DEV   // the handoff is the harness's alone
        // No handoff from before this arm answers an ask after it (ordinals keep counting across a disarm). Safe here: ring empty,
    // worker idle.
    dtHand[0].present = dtHand[1].present = -1;
#endif
    if (dtArmObs) dtArmObs();                              // the tracker seeds its worker-side copies here
    DTBBS(DtBbsAtArm(sc);)                                 // the save's question (dt_bbs.h): its own table; no mark until a pair is armed
    dtArmed = true;
    dtRecTid = dtProducer;
    LogRecord("  threaded: ARMED (#%ld) on the render thread %lu; DXVK now runs on thread %lu at priority %d (render %d, class 0x%lX)%s%s",
        dtArmCount, (unsigned long)dtProducer, (unsigned long)dtWorkerTid, GetThreadPriority(dtWorker), prRender,
        (unsigned long)GetPriorityClass(GetCurrentProcess()),
        prSet ? "" : "; the worker's priority could NOT be matched to the render thread's",
        dtValidate ? "; validation on" : "");
}

// ---- disarming ------------------------------------------------------------------------
// The tail every disarm shares, once dtArmed is off. Any thread. The hand-over list
// first, complete (see dtFrBusy), then the counters and the line.
static void DtDisarmTail(const char* why, const char* how)
{
        // The tracker goes back first, mirroring the arm: from the moment dtArmed was taken no observer runs on the worker, so draws
    // during the bounded wait below would be tracked by neither side. TrkOnDisarm only sets two bools.
    // Bounded: the pusher this waits for holds dtFrLock and can be preempted, so past the spin budget it yields a millisecond at a
    // time; a wait over 50 ms is said.
    if (dtDisarmObs) dtDisarmObs();                        // the render thread owns the tracker again from here
    LARGE_INTEGER t0, t; QueryPerformanceCounter(&t0);
    for (unsigned i = 1; dtFrBusy; i++)                    // whoever is between the flag's test and the push
    {
        _mm_pause();
        if ((i & 63) == 0) { QueryPerformanceCounter(&t); if (t.QuadPart - t0.QuadPart > dtSpinTicks) Sleep(1); }
    }
    QueryPerformanceCounter(&t);
    if ((t.QuadPart - t0.QuadPart) * 1000 >= 50 * dtQpf)
        LogRecord("  threaded: the disarm waited %ld ms for a Release on another thread to finish its hand-over",
            (long)((t.QuadPart - t0.QuadPart) * 1000 / dtQpf));
    DtFlushForeignReleasesDirect();
        // A failed Present the drain just replayed belongs to this device state, which a Reset is about to replace: left in dtPresHr
    // it would reach the game hundreds of frames later as a D3DERR_DEVICELOST.
    _InterlockedExchange(&dtPresHr, 0);
    dtArmPresents = 0;
    dtDisarms++;
    DT_SB_AT_DISARM()
    if (dtDisarms <= 20)
        LogRecord("  threaded: disarmed (%s) by thread %lu%s%s", why,
            (unsigned long)__readfsdword(0x24), __readfsdword(0x24) == dtProducer ? "" : " (NOT the producer)", how);
}

// THE PRODUCER'S OWN DISARM: its ring drains on its thread, then the flag, then the
// tail. Only the producer calls this (dt_Release, dt_Reset, DtRecAsked, and
// DtResetBarrier when the Reset is its own).
static void DtDisarm(const char* why)
{
    if (!dtArmed) return;
    DtDrain(DT_R_DIRECT);                                  // everything queued reaches DXVK first
        // The flag is taken, not stored: whoever turns it off runs the tail, once (DtResetBarrier may take it as this drain ends).
    // The exchange is a full fence, so the flag is off before the tail looks at dtFrBusy: Dekker with the push.
    const bool mine = _InterlockedExchange8((volatile char*)&dtArmed, 0) != 0;
    dtRecTid = 0;
    if (mine) DtDisarmTail(why, "");
}

// The producer answers the ask (dt_core.h) at its first wrapper entry after it went up. Still armed: a full DtDisarm. Already
// unarmed by DtResetBarrier's idle path: only the switch is left, and anything this thread recorded after the flip (should
// the settle window have judged wrong) is drained now and said, rather than published later onto a Reset device. Either way
// the call that asked goes direct and the ask comes down.
// dtRecAtFlip: dtNRec when the idle path took the flag, so DtRecAsked can tell whether anything was recorded after it, even
// if it was published since.
static LONGLONG dtRecAtFlip = 0;
static char     dtAskWhy[112] = "a device Reset from another thread";   // what the ask is for: written by the asker before the ask goes up
DEVSTAT(static volatile LONG     dtForeignOrdered = 0;)   // another thread's calls that took their place behind the ring (reported)
DEVSTAT(static volatile LONG     dtForeignWaited  = 0;)   // ...of which the worker had not yet got that far
DEVSTAT(static volatile LONGLONG dtForeignWaitT   = 0;)   // QPC ticks those waited, in all
static bool DtRecAsked()
{
    if (dtArmed) DtDisarm(dtAskWhy);
    else
    {
        dtRecTid = 0;
        const LONGLONG since = dtNRec - dtRecAtFlip;
        const DWORD w = dtW, p = (DWORD)dtPub;
        const DWORD unpublished = w >= p ? w - p : dtRingDw - p + w;   // wrap-aware, as the barrier counts it
                // What this thread published itself is waited for too: published is not run, so DtDrain waits for the worker to reach the
        // end of what is published before the asking call reaches DXVK.
        if (unpublished || since > 0)
        {
            LogRecord("  threaded: after %s found the render thread idle and disarmed, this thread recorded %lld more commands, %lu"
                          " dwords unpublished: they run now, before the call that asked (the settle window was too short)", dtAskWhy, since, (unsigned long)unpublished);
#if DLSSIV_DEV
            if (unpublished || !DT_SBM_FAULT(35))               // negative control NC35: drain only the unpublished
#endif
            DtDrain(DT_R_DIRECT);
        }
    }
    _InterlockedExchange(&dtDisarmAsk, 0);
    return false;
}

// A Reset is about to reach DXVK, and the caller may not be the producer. Everything recorded and handed over must run first:
// a live D3DPOOL_DEFAULT object makes a D3D9 Reset fail (DXVK d3d9_device.cpp:513). hkReset calls this before its first
// Release, so from there on its releases run direct, in order.
// On the producer it is DtDisarm. Another thread must not drain (dtPub, dtBatch and dtRecTid are the producer's), so it
// raises the ask, and then:
//   answered: the producer is running and takes the ask at its next wrapper entry with a full DtDisarm; this thread waits.
//   idle: no entry comes (the game's render thread waits for a frame the resetting main thread will not hand it). Once
//     nothing has been recorded for kDtSettleMs, the producer is outside its wrappers (DtRecAsked covers one preempted inside
//     them). This thread publishes what the producer committed and never published, by compare-exchange so dtPub only moves
//     forward, waits for the worker, turns dtArmed off and runs the tail. The switch stays; the producer's first entry
//     clears it.
//   stood aside: neither within kDtAskMs, so the worker is stuck inside DXVK. The layer stays armed with the ask up, the
//     line says so, and the Reset goes ahead; the producer disarms at its first entry, so the game's retry succeeds.
// Unarmed it is two loads and a flush of an empty list. The word it returns and its time go on hkReset's line.
// The same barrier stands in front of another thread's ordered call (DtOrderForeign, dt_core.h). `mine` is the reason when
// the caller is the producer, `foreign` when not; the latter is copied to dtAskWhy before the ask goes up.
enum { kDtSettleMs = 25, kDtAskMs = 1000 };
static DWORD dtBarrierPolls = 0;      // Sleep(1) rounds the last barrier took; hkReset prints it beside the ms
static const char* DtBarrier(const char* mine, const char* foreign, DWORD* msOut)
{
    if (msOut) *msOut = 0;
    dtBarrierPolls = 0;
    if (!dtArmed) { DtFlushForeignReleasesDirect(); return "unarmed"; }
    const DWORD me = __readfsdword(0x24);
    if (me == dtProducer) { DtDisarm(mine); return "the producer's own"; }
    if (me == dtWorkerTid) return "on the worker";            // a Reset is never recorded, so this cannot happen; never wait on this thread itself
        // Answered means the tail has run, not just the flag off: dtDisarms moves at the end of DtDisarmTail, after the hand-over
    // list (which may hold this thread's own releases) is flushed. Read before the ask goes up, so any disarm between counts.
    const LONG disarmsBefore = *(volatile LONG*)&dtDisarms;
    strncpy_s(dtAskWhy, sizeof(dtAskWhy), foreign, _TRUNCATE);
    _InterlockedExchange(&dtDisarmAsk, 1);
    LARGE_INTEGER t0, t; QueryPerformanceCounter(&t0);
    LONGLONG quietSince = 0, quietRec = 0; DWORD quietW = 0, published = 0;
    const char* how = "stood aside";
    for (;;)
    {
        if (*(volatile LONG*)&dtDisarms != disarmsBefore)
        { how = published ? "answered by the producer after a publish from here" : "answered by the producer"; break; }
        QueryPerformanceCounter(&t);
        const DWORD    w   = *(volatile DWORD*)&dtW;            // the producer's, read and never written here
        const LONGLONG rec = *(volatile LONGLONG*)&dtNRec;
        if (!quietSince || w != quietW || rec != quietRec) { quietSince = t.QuadPart; quietW = w; quietRec = rec; }
        else if ((t.QuadPart - quietSince) * 1000 >= (LONGLONG)kDtSettleMs * dtQpf)
        {
            // dtPub first, then dtW again, and only while the producer is still quiet: in the other order a producer that woke between
            // the reads could make the exchange move dtPub back and send the worker round the whole ring again. A stale pub can only make
            // the exchange fail. dtW at dtRingDw (between DtCommit's two stores) is taken as 0, since the worker wraps before reaching it.
            const LONG pub = dtPub;
            DWORD wq = *(volatile DWORD*)&dtW;
            if (wq != quietW) { Sleep(1); dtBarrierPolls++; continue; }        // it moved after all: the quiet starts again
            if (wq == dtRingDw) wq = 0;
            if ((DWORD)pub != wq)
            {
                if (_InterlockedCompareExchange(&dtPub, (LONG)wq, pub) == pub)
                {
                    published += wq >= (DWORD)pub ? wq - (DWORD)pub : dtRingDw - (DWORD)pub + wq;
                    _mm_mfence();                              // Dekker with the worker's exchange on dtWorkerSleeping
                    if (dtWorkerSleeping) SetEvent(dtWorkerEv);
                }
            }
            else if ((DWORD)dtDone == wq)
            {
                // Taken, not stored (see DtDisarm): if the producer took it first, its tail is running and the loop's check sees it finish.
                // dtRecAtFlip is stored before the exchange: a producer that sees the flag down reads it next, and x86 keeps store order.
                dtRecAtFlip = rec;                             // what the producer had recorded when the flag was taken
                if (_InterlockedExchange8((volatile char*)&dtArmed, 0))
                {
                    DtDisarmTail(foreign, ", the render thread idle");
                    how = published ? "idle render thread, published and disarmed here" : "idle render thread, disarmed here";
                    break;
                }
            }
        }
        if ((t.QuadPart - t0.QuadPart) * 1000 >= (LONGLONG)kDtAskMs * dtQpf)
        {
            LogRecord("  threaded: %s (thread %lu) waited %d ms, armed, and the ring never finished: the call goes AHEAD of what is queued", foreign, (unsigned long)me, (int)kDtAskMs);
            break;
        }
        Sleep(1);
        dtBarrierPolls++;
    }
    QueryPerformanceCounter(&t);
    if (msOut) *msOut = (DWORD)((t.QuadPart - t0.QuadPart) * 1000 / dtQpf);
    if (published && dtDisarms <= 20)
        LogRecord("  threaded: the render thread was idle with %lu dwords recorded past its last Present: the other thread published them", (unsigned long)published);
    return how;
}
static const char* DtResetBarrier(DWORD* msOut) { return DtBarrier("a device Reset", "a device Reset from another thread", msOut); }
// Another thread's ordered call (DtOrderForeign in dt_core.h says why, and why not a disarm). Never the producer or the
// worker; it waits for the worker alone.
//   w        how far the producer has committed; every command below it is complete and must run before this call.
//   publish  it if the producer has not, by compare-exchange and only when w is ahead of dtPub in ring order, so dtPub never
//            moves back.
//   wait     until the worker has run as many dwords as the producer had committed (a count that does not wrap). Spin, then
//            yield: the usual wait is microseconds, the longest an injection body, under a millisecond.
//   1 s      then go ahead and say so once: a stuck worker is the render thread's stuck line's business, and this thread
//            must not hang the game's loader.
static __forceinline bool DtRingIn(DWORD x, DWORD a, DWORD b) { return a <= b ? (x >= a && x < b) : (x >= a || x < b); }
// Has the worker passed the call's point? By count: with the ring full the producer refills each freed slot at once, so a
// ring-position test can see w queued again after the worker passed it. The producer's committed dwords (nDw) and the
// worker's run dwords (dtDoneDw) do not wrap; a signed difference of their low dwords is exact while the worker is less than
// 2^31 dwords behind, and it is never more than a ring behind.
static __forceinline bool DtForeignPassedCount(DWORD wDw, DWORD doneDw) { return (LONG)(doneDw - wDw) >= 0; }
// The ring-position test, kept for dt_test's controls (DT_BARRIER_BY_COUNT, ForeignBarrierLap, BarrierRule).
static __forceinline bool DtForeignPassedRing(DWORD w, DWORD done, DWORD cur) { return done == w || !(DtRingIn(w, done, cur) || w == cur); }
static const char* volatile dtForeignNamed[16];     // each method named once at log=1: a detector for a caller not seen before
DtForeignScope::DtForeignScope(const char* what, bool count) : on(DtForeignThread())
{ if (on) { if (count) DtForeignOrdered(what); else DtForeignBarrier(what); } }
// DXVK has returned. The count is raised again, so an entry a settle fetched while the call was in flight is fetched once
// more; only then is the call taken out of flight, which a settle on the producer may be waiting for.
static void DtForeignLanded()
{
    _InterlockedIncrement(&dtForeign);
    _InterlockedDecrement(&dtForeignInFlight);
}
// The barrier's two reads, for the harness: in the mod the first is the constant true and the second nothing; dt_test widens
// the gap and swaps the order as its control (ForeignBarrierLap). DT_BARRIER_NORM_CUR, true in the mod, is ForeignAtRingEnd's
// control: without it a producer parked between DtCommit's two stores reads as dtRingDw.
#ifndef DT_BARRIER_CUR_FIRST
#define DT_BARRIER_CUR_FIRST() true
#endif
#ifndef DT_BARRIER_NORM_CUR
#define DT_BARRIER_NORM_CUR() true
#endif
#ifndef DT_BARRIER_BETWEEN_READS
#define DT_BARRIER_BETWEEN_READS()
#endif
// DT_BARRIER_BY_COUNT, true in the mod: false puts back the ring-position wait, the harness's control.
#ifndef DT_BARRIER_BY_COUNT
#define DT_BARRIER_BY_COUNT() true
#endif
static __declspec(noinline) void DtForeignBarrier(const char* what)
{
        // In flight from the first instruction, with the count raised (dt_core.h): from here the producer's next Get is a settle,
    // which waits this call out.
    _InterlockedIncrement(&dtForeignInFlight);
    _InterlockedIncrement(&dtForeign);
    DEVSTAT(_InterlockedIncrement(&dtForeignOrdered);)
    for (int i = 0; i < 16; i++)
    {
        const char* const seen = dtForeignNamed[i];
        if (seen == what) break;
        if (!seen && !InterlockedCompareExchangePointer((PVOID volatile*)&dtForeignNamed[i], (PVOID)what, nullptr))
        {
            // One line a session in a release: the first method names the thread (the game's own main thread); the later methods are
            // development lines.
            (i ? LogHealth : LogRecord)("  threaded: another thread (%lu) calls %s on the device", (unsigned long)__readfsdword(0x24), what);
            break;
        }
    }
    DWORD w = *(volatile DWORD*)&dtW;
    if (w == dtRingDw) w = 0;                       // DtCommit's two stores, seen between them: the end of the ring IS its start
        // The count, read after w: DtCommit stores nDw before dtW, so it covers every command w covers.
    const DWORD wDw = *(volatile DWORD*)&dtNDw;     // the low dword of a LONGLONG the producer adds to: one aligned load
        // Publish cur, the producer's place read after dtPub, not w: cur is a command boundary of the lap it was read in, while a w
    // read before a stall of a whole lap could fall inside a later lap's command, which the worker would run past. cur at
    // dtRingDw is taken as 0, as w is.
    for (;;)
    {
        const LONG pub = dtPub; DWORD cur = *(volatile DWORD*)&dtW;
        if (DT_BARRIER_NORM_CUR() && cur == dtRingDw) cur = 0;
        if ((DWORD)pub == w || !(DtRingIn(w, (DWORD)pub, cur) || w == cur)) break;          // w is published already
        if (_InterlockedCompareExchange(&dtPub, (LONG)cur, pub) == pub)
        { _mm_mfence(); if (dtWorkerSleeping) SetEvent(dtWorkerEv); break; }                // Dekker with the worker's exchange, as DtPublishFenced
    }
    LARGE_INTEGER t0, t; t0.QuadPart = 0;
    for (unsigned i = 1;; i++)
    {
                // Where the producer is, then where the worker is: in the other order, with the ring full, a `cur` read just after `done`
        // could already lie past it, and the call could go ahead of up to a ring of older calls. In this order a stale value only
        // makes w look unreached for one more turn.
        if (DT_BARRIER_BY_COUNT())
        {
            DT_BARRIER_BETWEEN_READS();
            if (DtForeignPassedCount(wDw, (DWORD)dtDoneDw)) break;                           // the worker has run everything committed before the call
                                                                                             // Publish what the count waits for: the count can include a command whose dtW this thread did not see, and a producer that
            // then records under 128 commands and settles a Get would wait for this call while this call waits for it. So each turn
            // publishes the producer's place again, read after dtPub.
            const LONG p2 = dtPub; DWORD c2 = *(volatile DWORD*)&dtW;
            if (DT_BARRIER_NORM_CUR() && c2 == dtRingDw) c2 = 0;
            if ((DWORD)p2 != c2 && _InterlockedCompareExchange(&dtPub, (LONG)c2, p2) == p2) { _mm_mfence(); if (dtWorkerSleeping) SetEvent(dtWorkerEv); }
        }
        else
        {
            DWORD done, cur;
            if (DT_BARRIER_CUR_FIRST()) { cur = *(volatile DWORD*)&dtW; DT_BARRIER_BETWEEN_READS(); done = (DWORD)dtDone; }
            else                        { done = (DWORD)dtDone; DT_BARRIER_BETWEEN_READS(); cur = *(volatile DWORD*)&dtW; }
            if (DT_BARRIER_NORM_CUR() && cur == dtRingDw) cur = 0;                            // as the publish above; the worker never reports dtRingDw
            if (DtForeignPassedRing(w, done, cur)) break;                                    // the worker is at w or past it
        }
        if (!dtArmed) break;                                                                // a Reset's barrier drained and unarmed meanwhile: nothing is queued
        if (!t0.QuadPart) { QueryPerformanceCounter(&t0); DEVSTAT(_InterlockedIncrement(&dtForeignWaited);) }
        _mm_pause();
        if ((i & 63) == 0)
        {
            QueryPerformanceCounter(&t);
            const LONGLONG dt = t.QuadPart - t0.QuadPart;
            if (dt > dtSpinTicks) { if (dtWorkerSleeping) SetEvent(dtWorkerEv); Sleep(dt * 1000 > 2 * dtQpf ? 1 : 0); }
            if (dt * 1000 >= (LONGLONG)kDtAskMs * dtQpf)
            {
                static volatile LONG said = 0;
                if (_InterlockedExchange(&said, 1) == 0)
                    LogRecord("  threaded: another thread's %s waited %d ms for the device thread: the call goes AHEAD of what is still queued", what, (int)kDtAskMs);
                DT_SB_GAVE_UP()                                                               // the state-block records (dt_sbtab.h)
                break;
            }
        }
    }
    DEVSTAT(if (t0.QuadPart) { QueryPerformanceCounter(&t); InterlockedExchangeAdd64(&dtForeignWaitT, t.QuadPart - t0.QuadPart); })
}

// The report's window bases are at the top of this file, and the shadow's two here: the harness reads the session totals at
// its end, so a report must not zero them.
DEVSTAT(static LONG dtShFetchedBase = 0, dtShDirtiedBase = 0;)
// The threaded=0 advice's rule, on its own so the harness can hold it to labelled runs (tools\perfproof\dt_test.cpp).
// DtReport says why each term is there.
static bool DtSayThreaded0(bool whole, double costMs, double overlapMs)
{
    return whole && costMs > 2.0 && overlapMs < 1.0;
}

#if DLSSIV_DEVLOG
static void TrkCacheReport(double n);   // the velocity caches against their caps (dt_trk.h, included after this header)
#endif
#ifndef FGX
#define FGX(...)   // a program that compiles the device thread without the mod (the harness) has no frame generation product
#endif
static void DtReport()
{
    const double n = dtFrames ? (double)dtFrames : 1.0, ms = 1000.0 / (double)dtQpf;
    DEVSTAT(LONG waits = 0;) LONGLONG ticks = 0;
    for (int r = 0; r < DT_R_N; r++) { DEVSTAT(waits += dtWaitHit[r];) ticks += dtWaitT[r]; }
    const double   waitMs = ticks * ms / n;
    const LONGLONG idleNow = InterlockedCompareExchange64(&dtIdleTicksSum, 0, 0);
    const LONGLONG presNow = InterlockedCompareExchange64(&dtPresTicksSum, 0, 0);
    DEVSTAT(const LONG sleepsNow = dtIdleSleeps;)
    const double   presMs = (presNow - dtPresBase) * ms / n;
    const double   idleMs = (idleNow - dtIdleBase) * ms / n;
        // The frame and the overlap, for the verdict below. The frame is the clock between two reports over the frames between them,
    // used only when no arm fell between (dtFrames counts armed frames only), so the first window after every arm is skipped.
    // The overlap is the worker's busy time the render thread did not spend waiting for it: the time both threads worked at
    // once, which is what threaded=1 is for. One clock read every 600 frames, log=1 and up.
    static LONG     repArm = -1;
    static LONGLONG repQpc = 0;
    LARGE_INTEGER   nowQpc;
    QueryPerformanceCounter(&nowQpc);
    const bool      whole     = repQpc != 0 && repArm == dtArmCount;
    const double    frameMs   = whole ? (nowQpc.QuadPart - repQpc) * ms / n : 0.0;
    const double    overlapMs = frameMs - idleMs - waitMs;
    repArm = dtArmCount; repQpc = nowQpc.QuadPart;
        // The development lines' text is built inside DEVSTAT: a release builds none of it; the harnesses build all of it.
    DEVSTAT(char side[96] = "";
    if (whole)
        _snprintf_s(side, sizeof(side), _TRUNCATE, "; frame %.3f ms, the two threads side by side %.3f ms a frame", frameMs, overlapMs);)
    LogHealth("  threaded: %ld frames armed: %.0f calls a frame recorded (%.0f KB), the render thread waited %.1f times a frame"
        " for %.3f ms a frame; the worker was idle %.3f ms a frame (%.1f sleeps a frame) and inside Present %.3f ms a frame%s", dtFrames,
        (dtNRec - dtNRecBase) / n, (dtNDw - dtNDwBase) * 4.0 / 1024.0 / n, waits / n, waitMs, idleMs,
        (sleepsNow - dtIdleSleepsBase) / n, presMs, side);
        // The verdict, at log=1. The layer's gain is DXVK's time off the render thread; a render thread that waits longer than that
    // for the worker pays more than it saves. Less the worker's time inside Present, which is the frame limiter, clamped at zero
    // (under-counting is the safe direction for a line that says to turn the layer off).
    // And only when the two threads ran in series: waits alone cannot decide it, since under vsync the block lands in whichever
    // DXVK call meets the full queue. At threaded=0 the render thread does the worker's work itself, blocks included, so a wait
    // is a loss only where recording added cost, and threaded=1's gain is the overlap; overlap below the recording cost means
    // the threads ran in series. That cost is not measured on its own, so the line suggests a comparison and does not rule.
    // The 1 ms threshold is under every overlap seen where the layer really did cost time.
    static int saidCostly = 0;
    const double costMs = waitMs - presMs > 0.0 ? waitMs - presMs : 0.0;
        // Not for a window in which frame generation was engaged: the worker ran its notes too, and this advice is about the device
    // thread alone. The report says how many presents were engaged. One declaration, with the line said inside the call: a
    // statement of its own would renumber the scopes and rename this function's local statics, which the binary compare holds.
    FGX(const LONG fgEngaged = FgpDtWindow();)
    if (DtSayThreaded0(whole FGX(&& !fgEngaged), costMs, overlapMs) && saidCostly < 10)
    {
        saidCostly++;
        LogRecord("  threaded: the render thread waited %.2f ms a frame for the device thread over %ld frames (%.2f ms with the limiter), side by side %.2f ms of %.2f ms: threaded=0 may be better (%u logical processors%s)", costMs, dtFrames, waitMs, overlapMs > 0.0 ? overlapMs : 0.0, frameMs, dtCpuCount,
            dtCpuCount && dtCpuCount < 6 ? ", the worker may share a core with the game" : "");
    }
        // The six costliest reasons, each as: came up / had to wait / went to the kernel / ms a frame. The third separates a spin
    // from a sleep.
    DEVSTAT({
    bool used[DT_R_N] = {};
    char line[600]; size_t at = 0; line[0] = 0;
    for (int k = 0; k < 6; k++)
    {
        int best = -1;
        for (int r = 0; r < DT_R_N; r++)
            if (!used[r] && dtWaitN[r] && (best < 0 || dtWaitT[r] > dtWaitT[best] || (dtWaitT[r] == dtWaitT[best] && dtWaitN[r] > dtWaitN[best]))) best = r;
        if (best < 0) break;
        used[best] = true;
        at += _snprintf_s(line + at, sizeof(line) - at, _TRUNCATE, "%s%s %.1f/%.1f/%.2f/%.3f", at ? ", " : "",
            DtReasonName(best), dtWaitN[best] / n, dtWaitHit[best] / n, dtWaitBlocked[best] / n, dtWaitT[best] * ms / n);
    }
    if (at) LogHealth("    waits by reason (a frame: came up / waited / slept / ms): %s", line);
    })
#if DLSSIV_DEV
    {   // wakes: signals and wakes a frame, and the wakes that found the wait not over
        static LONG sigB = 0, wakeB = 0, futB = 0;
        const LONG sig = dtProdSignals, wk = dtProdWakes, fut = dtProdWakesFutile;
        LogHealth("    wakes: the worker signalled the render thread %.1f times a frame; the render thread woke from a sleeping wait %.1f"
            " times a frame, %.1f of them before the wait was over (a drain and the one-frame-ahead wait ask for kind %ld)",
            (sig - sigB) / n, (wk - wakeB) / n, (fut - futB) / n, DT_WAKE_KIND());
        sigB = sig; wakeB = wk; futB = fut;
    }
#endif
    LogHealth("    shadow: %ld settles (a drain; %ld of them first waited out another thread's call in flight), %.1f entries fetched on demand a frame, marked all unknown %.1f times a frame;"
        " pixel constants %s, %ld FVF asks went to DXVK", dtShReloads, dtShWaitedOut, (dtShFetched - dtShFetchedBase) / n,
        (dtShDirtied - dtShDirtiedBase) / n, dtShConsts ? "followed (somebody asked)" : "not followed (nobody has asked)", dtShFvfAsked);
    DEVSTAT(LogHealth("    state blocks: %ld Applies so far; %ld scene draws came after one before the game set the tracked state again%s%s%s%s%s%s"
        " (the tracker's premise is 0: devthread/dt_trk.h)", trkApplies, trkSceneAfterApply, trkApplyDirtySeen ? ", still stale:" : "",
        (trkApplyDirtySeen & 1u) ? " world" : "", (trkApplyDirtySeen & 2u) ? " stream-0" : "", (trkApplyDirtySeen & 4u) ? " declaration" : "",
        (trkApplyDirtySeen & 8u) ? " vertex-shader" : "", (trkApplyDirtySeen & 16u) ? " index-buffer" : "");)
#if DLSSIV_DEVLOG
    unsigned __int64 errKey = 0;          // what errs is built from, folded for the hold below
    for (int i = 0; i < 8; i++) if (dtReplayErrCnt[i]) errKey = LogKeyMix(errKey, dtReplayErrCnt[i], dtReplayErrWho[i], dtReplayErrHr[i]);
#endif
    DEVSTAT(char errs[300] = ""; size_t ea = 0;
    for (int i = 0; i < 8; i++)
    {
        if (!dtReplayErrCnt[i]) continue;
        const DWORD who = dtReplayErrWho[i];                  // the slot names the method; no reverse lookup
        const char* nm = who < 119 ? dtDevName[who] : who == DT_WHO_PRESENT ? "a swapchain Present"
                       : who == DT_WHO_RES ? "a resource method" : who == DT_WHO_SBAPPLY ? "a recorded state block's Apply" : "?";
        ea += _snprintf_s(errs + ea, sizeof(errs) - ea, _TRUNCATE, "%s%s 0x%08lX x%ld", ea ? ", " : "", nm,
            (unsigned long)dtReplayErrHr[i], dtReplayErrCnt[i]);
    })
        // Held while unchanged: once a session settles this line repeats every 600 frames. errs is not hashed as a string; what it
    // is built from is, so a new failure or a changed HRESULT always prints. The alarm: any failed replayed call, and the two
    // release faults. The fence is around the if header, not the call: an extra `if (!(false))` in a release build moved two
    // globals in .data and lost byte identity.
#if DLSSIV_DEVLOG
    static LogHold holdReplay;
    if (!LOGHELD(holdReplay, dtReplayErrN != 0 || dtPendMiss || dtFrOom,
                 errKey, (long)dtReplayErrN, ea != 0, (long)dtForeign, dtForeignLocks, dtForeignRels, dtForeignPresents,
                 (long)dtForeignOrdered, (long)dtForeignWaited, InterlockedCompareExchange64(&dtForeignWaitT, 0, 0),
                 dtShReloads, dtDisarms, dtPendPeak, dtPendMiss != 0, dtFrOom != 0))
#endif
        LogHealth("    replayed calls that failed %ld%s%s; other threads: %ld raises of the foreign count (three a Set or a state-block Apply, two for any other ordered call), %ld locks, %ld releases handed over,"
            " %ld presents, %ld calls put behind the ring (%ld waited for the worker, %.3f ms in all this session); shadow reloads %ld; disarms %ld;"
            " pair table peak %d live of %d%s%s%s", (long)dtReplayErrN, ea ? ": " : "", errs, (long)dtForeign,
            dtForeignLocks, dtForeignRels, dtForeignPresents, (long)dtForeignOrdered, (long)dtForeignWaited,
            (double)InterlockedCompareExchange64(&dtForeignWaitT, 0, 0) * ms, dtShReloads, dtDisarms, dtPendPeak, DT_PEND_CAP * 3 / 4,
            dtPendMiss ? "; RELEASES DEFERRED WITHOUT AN EXACT COUNT (the pair table was full)" : "",
            dtFrOom ? "; RELEASES RUN EARLY (the hand-over list could not grow: out of memory)" : "", LOGMARK);
#if DLSSIV_DEVLOG
    TrkCacheReport(n);     // the velocity caches against their caps (dt_trk.h)
    {
                // the pair table's compactions (dt_res.h, DtPendCompact), held while there is no new one
        static LogHold holdCompact;
        if (!LOGHELD(holdCompact, false, dtPendCompacts))
            LogHealth("    pair table compactions (X6): %ld this session, %.3f ms in all under the pair lock, the longest %.3f ms%s",
                      (long)dtPendCompacts, (double)dtPendCompactT * ms, (double)dtPendCompactMax * ms, LOGMARK);
    }
    DtSbReport(n);         // the state-block table (dt_sbtab.h): what would take the recorded path, and why the rest would not
    {
        static const char* const kKind[4] = { "", "render state", "sampler state on slot", "texture on slot" };
        const LONG f = dtBodyUndeclFirst;
        LogHealth("    the injection body's declarations (dt_core.h, DtBodyDecl): %ld Sets checked this session, %ld outside them%s%s%ld%s",
            (long)dtBodySets, (long)dtBodyUndeclared, dtBodyUndeclared ? "   <-- dt_trk.h's masks are wrong: the first was the " : "",
            dtBodyUndeclared ? kKind[(f >> 16) & 3] : "", dtBodyUndeclared ? (long)(f & 0xFFFF) : 0L,
            dtBodyUndeclared ? "" : (dtBodySets ? " (every one declared)" : " (no body has run on the worker)"));
    }
#endif
    if (dtValidate)
        LogHealth("    validation: %ld Gets and %ld whole-shadow compares checked (%ld more thrown away: another thread's Set landed during the read), %ld MISMATCHES%s", dtValGets, dtValChecks, dtValRaced, dtValMis,
            dtValMis ? "   <-- the shadow is wrong somewhere: the lines above name it" : "   (the shadow matched DXVK every time)");
    DEVSTAT(memset(dtWaitN, 0, sizeof(dtWaitN)); memset(dtWaitHit, 0, sizeof(dtWaitHit));) memset(dtWaitT, 0, sizeof(dtWaitT));
    DEVSTAT(memset(dtWaitBlocked, 0, sizeof(dtWaitBlocked));)
    DEVSTAT(dtNRecBase = dtNRec; dtNDwBase = dtNDw;) dtIdleBase = idleNow; dtPresBase = presNow; DEVSTAT(dtIdleSleepsBase = sleepsNow;) dtFrames = 0;
    DEVSTAT(dtShFetchedBase = dtShFetched; dtShDirtiedBase = dtShDirtied;)
}

// After every recorded Present, on the producer.
static void DtFrameEnd()
{
    dtFrames++;
    if (dtValidate && dtDev)
    {
        DtSyncScope s_(DT_R_VALIDATE);                     // drained: DXVK now holds what the shadow says
        if (dtShNeedDrain || dtForeign != dtForeignSeen) DtShadowLoad(dtDev);
        else DtShadowCompare(dtDev);
    }
    if (telemetryOn && dtFrames >= 600) DtReport();
}
