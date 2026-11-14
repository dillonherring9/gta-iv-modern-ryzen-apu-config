// Channel to the 64-bit host, host launch, plugin entry and diagnostics.
#include "vcr_client.h"
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <deque>
#include <string>
#include "../shared/vcr_maps.h"

namespace vcr {

static HMODULE g_self;
static CRITICAL_SECTION g_lock, g_logLock;
static wchar_t g_pluginDir[MAX_PATH], g_privateDir[MAX_PATH], g_logPath[MAX_PATH], g_base[128];
static FILE* g_logFile;

void log(const char* fmt, ...) {
  EnterCriticalSection(&g_logLock);
  if (!g_logFile && g_logPath[0]) g_logFile = _wfopen(g_logPath, L"wb");
  if (g_logFile) {
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(g_logFile, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_logFile, fmt, ap); va_end(ap);
    fputc('\n', g_logFile); fflush(g_logFile);
  }
  LeaveCriticalSection(&g_logLock);
}
void lock() { EnterCriticalSection(&g_lock); }
void unlock() { LeaveCriticalSection(&g_lock); }
const wchar_t* channelBase() { return g_base; }
const wchar_t* pluginDir() { return g_pluginDir; }
const wchar_t* privateDir() { return g_privateDir; }

// ---------------------------------------------------------------- settings
struct Settings { int enabled = 1; int ringMB = 16; int arenaMB = 48; int respMB = 8; int monitorSeconds = 30; int fullscreenMode = 0; int framesAhead = 1; };
static Settings g_cfg;
static void readSettings() {
  wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\VCRender.ini", g_pluginDir);
  g_cfg.enabled = GetPrivateProfileIntW(L"VCRender", L"Enabled", 1, path);
  g_cfg.ringMB = GetPrivateProfileIntW(L"VCRender", L"RingMB", 16, path);
  g_cfg.arenaMB = GetPrivateProfileIntW(L"VCRender", L"ArenaMB", 48, path);
  g_cfg.respMB = GetPrivateProfileIntW(L"VCRender", L"ResponseMB", 8, path);
  g_cfg.monitorSeconds = GetPrivateProfileIntW(L"VCRender", L"AddressLogSeconds", 30, path);
  g_cfg.fullscreenMode = GetPrivateProfileIntW(L"VCRender", L"FullscreenMode", 0, path);
  g_cfg.framesAhead = GetPrivateProfileIntW(L"VCRender", L"MaxFramesAhead", 1, path);
  if (g_cfg.framesAhead < 1) g_cfg.framesAhead = 1; if (g_cfg.framesAhead > 3) g_cfg.framesAhead = 3;
  auto pow2 = [](int mb, int lo, int hi) { if (mb < lo) mb = lo; if (mb > hi) mb = hi; int p = 1; while (p < mb) p <<= 1; return p; };
  g_cfg.ringMB = pow2(g_cfg.ringMB, 4, 64);
  if (g_cfg.arenaMB < 16) g_cfg.arenaMB = 16; if (g_cfg.arenaMB > 256) g_cfg.arenaMB = 256;
  if (g_cfg.respMB < 4) g_cfg.respMB = 4; if (g_cfg.respMB > 64) g_cfg.respMB = 64;
}

// ---------------------------------------------------------------- shared memory and host process
static VcrControl* C;
static uint8_t* g_ring; static uint32_t g_ringBytes, g_ringMask;
static uint8_t* g_arena; static uint32_t g_arenaBytes;
static uint8_t* g_resp; static uint32_t g_respBytes;
static HANDLE g_map, evWake, evResp, evProgress, evFrame, g_hostProc;
static uint32_t g_head, g_pubHead, g_pending, g_seq, g_syncSent;
static volatile LONG g_state;          // 0 not started, 1 starting, 2 ready, -1 failed
static HANDLE g_startedEvent;
static bool g_shuttingDown;

VcrControl* control() { return C; }
int fullscreenSetting() { return g_cfg.fullscreenMode; }
int framesAheadSetting() { return g_cfg.framesAhead; }
bool ready() { return g_state == 2; }
uint32_t pushedSeq() { return g_seq; }
uint32_t respCap() { return g_respBytes - sizeof(VcrResp); }
uint32_t maxInline() { return g_ringBytes / 4 - 64; }
HANDLE frameEvent() { return evFrame; }

static void fatal(const char* what) {
  log("FATAL: %s", what);
  if (g_shuttingDown) return;
  g_shuttingDown = true;
  char msg[600]; snprintf(msg, sizeof msg, "VCRender: the 64-bit renderer stopped (%s).\n\nThe game cannot continue. Details are in plugins\\VCRender.log.", what);
  MessageBoxA(nullptr, msg, "VCRender", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  ExitProcess(1);
}
static void checkHost() {
  if (g_hostProc && WaitForSingleObject(g_hostProc, 0) == WAIT_OBJECT_0) {
    DWORD code = 0; GetExitCodeProcess(g_hostProc, &code);
    char m[128]; snprintf(m, sizeof m, "host process exited with code %lu", code); fatal(m);
  }
  if (C && C->hostFatal) { char m[128]; snprintf(m, sizeof m, "host reported fatal error %d", C->hostFatal); fatal(m); }
}

static DWORD WINAPI hostWatch(void*) {
  WaitForSingleObject(g_hostProc, INFINITE);
  if (!g_shuttingDown) checkHost();
  return 0;
}

template<class Pred> static void waitUntil(Pred pred, LONG bit, HANDLE ev) {
  for (int i = 0; i < 4000; i++) { if (pred()) return; YieldProcessor(); if ((i & 63) == 63) SwitchToThread(); }
  for (;;) {
    InterlockedOr((volatile LONG*) &C->clientWaiting, bit);
    if (pred()) break;
    WaitForSingleObject(ev, 2);
    checkHost();
  }
  InterlockedAnd((volatile LONG*) &C->clientWaiting, ~bit);
}

void flush() {
  if (g_head == g_pubHead) return;
  MemoryBarrier();
  InterlockedExchange((volatile LONG*) &C->ringHead, (LONG) g_head);
  g_pubHead = g_head; g_pending = 0;
  MemoryBarrier();
  if (C->hostSleeping) SetEvent(evWake);
}

static void waitRingSpace(uint32_t need) {
  auto freeBytes = [&] { return g_ringBytes - (g_head - (uint32_t) C->ringTail); };
  if (freeBytes() >= need) return;
  flush();
  waitUntil([&] { return freeBytes() >= need; }, 2, evProgress);
}

void* rec(uint16_t op, uint32_t payload, uint16_t flags) {
  uint32_t size = (uint32_t) ((sizeof(VcrRec) + payload + 7) & ~7u);
  if (size > g_ringBytes / 4) { log("record op %u of %u bytes exceeds the ring limit", op, size); fatal("oversized record"); }
  if (g_pending >= 64 || g_head - g_pubHead >= (256u << 10)) flush();   // publish completed records in batches
  uint32_t off = g_head & g_ringMask, toEnd = g_ringBytes - off;
  if (size > toEnd) {
    waitRingSpace(toEnd + size);
    VcrRec* pad = (VcrRec*) (g_ring + off); pad->op = OP_PAD; pad->flags = 0; pad->size = toEnd;
    g_head += toEnd; off = 0;
  } else {
    waitRingSpace(size);
  }
  VcrRec* r = (VcrRec*) (g_ring + off);
  r->op = op; r->flags = flags; r->size = size;
  g_head += size; g_seq++; g_pending++;
  return r + 1;
}

HRESULT call(uint16_t op, const void* payload, uint32_t bytes, void* out, uint32_t outCap, uint32_t* outBytes) {
  if (!ready()) return D3DERR_NOTAVAILABLE;
  void* p = rec(op, bytes, VCR_F_SYNC);
  if (bytes) memcpy(p, payload, bytes);
  uint32_t want = ++g_syncSent;
  flush();
  waitUntil([&] { return (int32_t) ((uint32_t) C->respSeq - want) >= 0; }, 1, evResp);
  MemoryBarrier();
  const VcrResp* r = (const VcrResp*) g_resp;
  uint32_t n = r->bytes < outCap ? r->bytes : outCap;
  if (out && n) memcpy(out, r + 1, n);
  if (outBytes) *outBytes = r->bytes;
  return r->hr;
}

void sendChunks(const uint8_t* data, uint32_t bytes) {
  const uint32_t step = 1u << 20;
  for (uint32_t off = 0; off < bytes; off += step) {
    uint32_t n = bytes - off < step ? bytes - off : step;
    auto* c = rec<PUploadChunk>(OP_UPLOAD_CHUNK, n);
    c->total = bytes; c->offset = off; c->bytes = n;
    memcpy(c + 1, data + off, n);
  }
}

// ---------------------------------------------------------------- upload arena
struct ArenaReg { uint32_t off, bytes, seq; bool submitted; };
static std::deque<ArenaReg> g_regs;
static uint32_t g_arenaHead;
static void arenaReclaim() {
  uint32_t done = (uint32_t) C->processedSeq;
  while (!g_regs.empty() && g_regs.front().submitted && (int32_t) (done - g_regs.front().seq) >= 0) g_regs.pop_front();
  if (g_regs.empty()) g_arenaHead = 0;
}
static bool arenaTry(uint32_t bytes, uint32_t& off) {
  if (g_regs.empty()) { off = 0; return bytes <= g_arenaBytes; }
  uint32_t oldest = g_regs.front().off;
  if (g_arenaHead > oldest) {
    if (bytes <= g_arenaBytes - g_arenaHead) { off = g_arenaHead; return true; }
    if (bytes <= oldest) { off = 0; return true; }
    return false;
  }
  if (g_arenaHead < oldest && bytes <= oldest - g_arenaHead) { off = g_arenaHead; return true; }
  return false;
}
bool regionAlloc(uint32_t bytes, Region& r) {
  r = Region();
  bytes = (bytes + 63) & ~63u;
  if (bytes == 0) bytes = 64;
  if (bytes <= g_arenaBytes / 2) {
    uint32_t off = 0;
    arenaReclaim();
    bool ok = arenaTry(bytes, off);
    if (!ok && !g_regs.empty() && g_regs.front().submitted) {
      flush();
      for (int tries = 0; tries < 2000 && !ok; tries++) {   // up to ~4 s: the host is consuming earlier uploads
        uint32_t want = g_regs.front().seq;
        waitUntil([&] { return (int32_t) ((uint32_t) C->processedSeq - want) >= 0; }, 2, evProgress);
        arenaReclaim();
        ok = arenaTry(bytes, off);
        if (!ok && (g_regs.empty() || !g_regs.front().submitted)) break;
      }
    }
    if (ok) {
      g_regs.push_back({off, bytes, 0, false});
      g_arenaHead = off + bytes;
      r.p = g_arena + off; r.off = off; r.bytes = bytes; r.arena = true;
      return true;
    }
    static int warned; if (warned++ < 10) log("arena full (%u bytes requested, %zu regions held); using heap", bytes, g_regs.size());
  }
  r.p = (uint8_t*) VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  r.bytes = bytes; r.arena = false;
  return r.p != nullptr;
}
void regionSubmit(Region& r) {
  if (r.arena) {
    for (auto& g : g_regs) if (g.off == r.off && !g.submitted) { g.submitted = true; g.seq = g_seq; break; }
  } else if (r.p) {
    VirtualFree(r.p, 0, MEM_RELEASE);
  }
  r = Region();
}
void regionDrop(Region& r) {
  if (r.arena) {
    for (auto& g : g_regs) if (g.off == r.off && !g.submitted) { g.submitted = true; g.seq = (uint32_t) C->processedSeq; break; }   // nothing reads it
    arenaReclaim();
  } else if (r.p) VirtualFree(r.p, 0, MEM_RELEASE);
  r = Region();
}

// ---------------------------------------------------------------- ids
static std::vector<uint32_t> g_freeIds; static uint32_t g_nextId = 1;
uint32_t newId() { if (!g_freeIds.empty()) { uint32_t i = g_freeIds.back(); g_freeIds.pop_back(); return i; } return g_nextId++; }
void freeId(uint32_t id) { if (id) g_freeIds.push_back(id); }

// ---------------------------------------------------------------- frame throttle
void waitFrameCompleted(uint32_t scId, int32_t frame) {
  int e = -1;
  for (uint32_t i = 0; i < VCR_MAX_SC; i++) if (C->frames[i].scId == (int32_t) scId) { e = (int) i; break; }
  if (e < 0) return;
  flush();
  DWORD start = GetTickCount();
  waitUntil([&] { return C->frames[e].scId != (int32_t) scId || (int32_t) (C->frames[e].completedFrame - frame) >= 0 || GetTickCount() - start > 2000; }, 2, evProgress);
}

// ---------------------------------------------------------------- address-space monitor
// GTA IV 1.2.0.59 CStreamingInfoManager counters (0x103E8D0 + 0x20, nine DWORDs); see codex-streaming-memory-2026-11-10
static void streamingReport() {
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const wchar_t* n = wcsrchr(exe, L'\\'); n = n ? n + 1 : exe;
  if (_wcsicmp(n, L"GTAIV.exe") != 0) return;
  const uintptr_t at = 0x103E8F0;
  MEMORY_BASIC_INFORMATION mi;
  if (!VirtualQuery((void*) at, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return;
  uint32_t v[9]; memcpy(v, (const void*) at, sizeof v);
  int32_t avail = (int32_t) v[3];
  int extra = budgetExtraMiB();
  char tail[64] = "";
  if (extra > 0) snprintf(tail, sizeof tail, "; VCRender extra +%d MiB on", extra);
  else if (extra == 0) snprintf(tail, sizeof tail, "; VCRender extra OFF (address space low)");
  char diag[200]; streamDiagSummary(diag, sizeof diag);
  log("[streaming] texture budget used %u MiB, available %d MiB (%.1f%% full); virtual %u of %u MiB; requests %u%s%s",
      v[4] >> 20, avail / (1 << 20), 100.0 * v[4] / (double) ((int64_t) v[4] + avail), v[1] >> 20, v[0] >> 20, v[6], tail, diag);
  char pool[300]; physPoolSummary(pool, sizeof pool);
  if (pool[0]) log("[physpool] %s", pool);
}
static void addressReport(const char* why) {
  streamingReport();
  bool vk = GetModuleHandleW(L"winevulkan.dll") || GetModuleHandleW(L"vulkan-1.dll");
  VcrMaps m;
  if (vcrReadMaps(m)) {
    log("[address] %s: Linux view below 4 GiB: mapped %llu MiB, unmapped %llu MiB, largest gap %llu MiB; graphics libraries/buffers %llu MiB (GPU buffers %llu, LLVM %llu); Vulkan DLL in game process: %s",
        why, m.mapped >> 20, m.unmapped >> 20, m.largestGap >> 20, m.graphics >> 20, m.gpuBuffers >> 20, m.llvm >> 20, vk ? "YES" : "no");
    return;
  }
  MEMORY_BASIC_INFORMATION mi; uintptr_t p = 0x10000; uint64_t freeBytes = 0, largest = 0;
  while (p < 0xFFFF0000u && VirtualQuery((void*) p, &mi, sizeof mi)) {
    if (mi.State == MEM_FREE) { freeBytes += mi.RegionSize; if (mi.RegionSize > largest) largest = mi.RegionSize; }
    p = (uintptr_t) mi.BaseAddress + mi.RegionSize;
  }
  log("[address] %s: Windows view (Linux maps unavailable): free %llu MiB, largest free %llu MiB; Vulkan DLL in game process: %s",
      why, freeBytes >> 20, largest >> 20, vk ? "YES" : "no");
}
static DWORD WINAPI monitorThread(void*) {
  for (;;) { Sleep(g_cfg.monitorSeconds * 1000); addressReport("periodic"); }
  return 0;
}

// ---------------------------------------------------------------- host launch
static bool startHost() {
  const uint32_t ctl = VCR_CONTROL_BYTES;
  g_ringBytes = (uint32_t) g_cfg.ringMB << 20; g_ringMask = g_ringBytes - 1;
  g_arenaBytes = (uint32_t) g_cfg.arenaMB << 20; g_respBytes = (uint32_t) g_cfg.respMB << 20;
  uint64_t total = (uint64_t) ctl + g_ringBytes + g_arenaBytes + g_respBytes;
  swprintf(g_base, 128, L"Local\\VCRender_%lu_%lu", GetCurrentProcessId(), GetTickCount());
  wchar_t name[200];
  swprintf(name, 200, L"%ls_mem", g_base);
  g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD) (total >> 32), (DWORD) total, name);
  if (!g_map) { log("CreateFileMapping failed %lu", GetLastError()); return false; }
  uint8_t* base = (uint8_t*) MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!base) { log("MapViewOfFile (%llu MiB) failed %lu", total >> 20, GetLastError()); return false; }
  C = (VcrControl*) base;
  memset(C, 0, sizeof(VcrControl));
  C->magic = VCR_MAGIC; C->protocol = VCR_PROTOCOL; C->controlBytes = ctl; C->ringBytes = g_ringBytes;
  C->arenaBytes = g_arenaBytes; C->respBytes = g_respBytes; C->clientPid = GetCurrentProcessId();
  for (auto& f : C->frames) { f.latestSlot = -1; f.readingSlot = -1; }
  g_ring = base + ctl; g_arena = g_ring + g_ringBytes; g_resp = g_arena + g_arenaBytes;
  auto mk = [&](const wchar_t* tag) { swprintf(name, 200, L"%ls_%ls", g_base, tag); return CreateEventW(nullptr, FALSE, FALSE, name); };
  evWake = mk(L"wake"); evResp = mk(L"resp"); evProgress = mk(L"progress"); evFrame = mk(L"frame");
  if (!evWake || !evResp || !evProgress || !evFrame) { log("CreateEvent failed %lu", GetLastError()); return false; }

  wchar_t exe[MAX_PATH], cmd[1024], logs[MAX_PATH], hostLog[MAX_PATH], conf[MAX_PATH];
  swprintf(exe, MAX_PATH, L"%ls\\VCRenderHost.exe", g_privateDir);
  swprintf(logs, MAX_PATH, L"%ls\\logs", g_privateDir);
  CreateDirectoryW(logs, nullptr);
  swprintf(hostLog, MAX_PATH, L"%ls\\VCRenderHost.log", logs);
  swprintf(conf, MAX_PATH, L"%ls\\dxvk.conf", g_privateDir);
  swprintf(cmd, 1024, L"\"%ls\" %ls %lu \"%ls\"", exe, g_base, GetCurrentProcessId(), hostLog);
  // environment: inherit, but point DXVK at our config and log folder
  std::vector<wchar_t> env;
  wchar_t* cur = GetEnvironmentStringsW();
  for (wchar_t* p = cur; *p; p += wcslen(p) + 1) {
    if (!_wcsnicmp(p, L"DXVK_CONFIG_FILE=", 17) || !_wcsnicmp(p, L"DXVK_LOG_PATH=", 14) || !_wcsnicmp(p, L"DXVK_CONFIG=", 12)) continue;
    env.insert(env.end(), p, p + wcslen(p) + 1);
  }
  FreeEnvironmentStringsW(cur);
  auto addEnv = [&](const wchar_t* k, const wchar_t* v) { std::wstring s = std::wstring(k) + L"=" + v; env.insert(env.end(), s.c_str(), s.c_str() + s.size() + 1); };
  addEnv(L"DXVK_CONFIG_FILE", conf);
  addEnv(L"DXVK_LOG_PATH", logs);
  env.push_back(0);
  STARTUPINFOW si = {}; si.cb = sizeof si; PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(exe, cmd, nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT | ABOVE_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW,
                      env.data(), g_privateDir, &si, &pi)) {
    log("CreateProcess %ls failed %lu", exe, GetLastError());
    return false;
  }
  CloseHandle(pi.hThread);
  g_hostProc = pi.hProcess;
  log("host started: pid %lu", pi.dwProcessId);
  DWORD t0 = GetTickCount();
  while (C->hostReady == 0) {
    if (WaitForSingleObject(g_hostProc, 20) == WAIT_OBJECT_0) {
      DWORD code = 0; GetExitCodeProcess(g_hostProc, &code);
      log("host exited during startup with code %lu: %s", code, C->hostMessage);
      return false;
    }
    if (GetTickCount() - t0 > 30000) { log("host did not become ready within 30 s"); TerminateProcess(g_hostProc, 1); return false; }
  }
  if (C->hostReady < 0) { log("host failed: %s", C->hostMessage); return false; }
  log("host ready after %lu ms", GetTickCount() - t0);
  g_state = 2;
  uint32_t proto = 0;
  { Guard g; HRESULT hr = call(OP_HELLO, nullptr, 0, &proto, 4); if (FAILED(hr) || proto != VCR_PROTOCOL) { log("handshake failed (hr 0x%08lx, protocol %u)", hr, proto); g_state = -1; return false; } }
  CloseHandle(CreateThread(nullptr, 0, hostWatch, nullptr, 0, nullptr));
  extern void presenterStart(HANDLE frameEv);
  presenterStart(evFrame);
  return true;
}

static DWORD WINAPI startThread(void*) {
  bool ok = startHost();
  if (!ok) g_state = -1;
  SetEvent(g_startedEvent);
  addressReport(ok ? "host connected" : "host failed");
  if (g_cfg.monitorSeconds > 0) CloseHandle(CreateThread(nullptr, 0, monitorThread, nullptr, 0, nullptr));
  return 0;
}

static void beginStart() {
  if (InterlockedCompareExchange(&g_state, 1, 0) != 0) return;
  g_startedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CloseHandle(CreateThread(nullptr, 0, startThread, nullptr, 0, nullptr));
}
static bool waitStarted() {
  beginStart();
  if (g_state == 1) WaitForSingleObject(g_startedEvent, 60000);
  return g_state == 2;
}
bool hostAvailable() { return waitStarted(); }
void installDxgiRedirect();
void budgetStart();

// ---------------------------------------------------------------- FusionFix proxy redirection
static PVOID* g_slots[2]; static PVOID g_previous[2];
static PVOID* dispatchSlot(HMODULE mod, const char* name) {
  BYTE* code = (BYTE*) GetProcAddress(mod, name);
  if (!code || code[0] != 0xFF || code[1] != 0x25) return nullptr;   // jmp dword ptr [slot]
  IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*) mod;
  IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*) ((BYTE*) mod + dos->e_lfanew);
  uintptr_t lo = (uintptr_t) mod, hi = lo + nt->OptionalHeader.SizeOfImage, slot = *(uint32_t*) (code + 2);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE || slot < lo || slot > hi - sizeof(PVOID) || slot % sizeof(PVOID)) return nullptr;
  MEMORY_BASIC_INFORMATION mi;
  if (!VirtualQuery((void*) slot, &mi, sizeof mi) || mi.State != MEM_COMMIT || mi.AllocationBase != mod || (mi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
  return (PVOID*) slot;
}
static bool setSlot(PVOID* slot, PVOID value) {
  DWORD old; if (!VirtualProtect(slot, sizeof(PVOID), PAGE_READWRITE, &old)) return false;
  InterlockedExchangePointer((PVOID volatile*) slot, value);
  DWORD ignored; VirtualProtect(slot, sizeof(PVOID), old, &ignored); return true;
}

}  // namespace vcr

using namespace vcr;

extern "C" IDirect3D9* WINAPI VCR_Direct3DCreate9(UINT sdk);
extern "C" HRESULT WINAPI VCR_Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out);

// undecorated export names (as the system d3d9.dll) for test programs that load this DLL directly
extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) __asm__("_Direct3DCreate9");
extern "C" __declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) __asm__("_Direct3DCreate9Ex");
extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) { return VCR_Direct3DCreate9(sdk); }
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) { return VCR_Direct3DCreate9Ex(sdk, out); }

static void initPaths();
static void ensureConfigured() { static LONG once; if (!InterlockedExchange(&once, 1)) { initPaths(); vcr::readSettings(); } }

extern "C" IDirect3D9* WINAPI VCR_Direct3DCreate9(UINT sdk) {
  ensureConfigured();
  if (!waitStarted()) {
    log("Direct3DCreate9: renderer unavailable; %s", g_previous[0] ? "using the original Direct3D" : "no fallback");
    if (g_previous[0]) return ((IDirect3D9* (WINAPI*)(UINT)) g_previous[0])(sdk);
    return nullptr;
  }
  VD3D* d = new VD3D(); d->isEx = false;
  log("Direct3DCreate9(%u)", sdk);
  budgetStart();
  streamDiagStart();
  physPoolStart();
  return d;
}
extern "C" HRESULT WINAPI VCR_Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
  if (!out) return D3DERR_INVALIDCALL;
  ensureConfigured();
  if (!waitStarted()) {
    if (g_previous[1]) return ((HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)) g_previous[1])(sdk, out);
    *out = nullptr; return D3DERR_NOTAVAILABLE;
  }
  VD3D* d = new VD3D(); d->isEx = true; *out = d;
  log("Direct3DCreate9Ex(%u)", sdk);
  budgetStart();
  streamDiagStart();
  physPoolStart();
  return S_OK;
}

static void initPaths() {
  if (g_pluginDir[0]) return;
  GetModuleFileNameW(g_self, g_pluginDir, MAX_PATH);
  wchar_t* s = wcsrchr(g_pluginDir, L'\\'); if (s) *s = 0;
  swprintf(g_privateDir, MAX_PATH, L"%ls\\VCRender", g_pluginDir);
  swprintf(g_logPath, MAX_PATH, L"%ls\\VCRender.log", g_pluginDir);
}

// Ultimate ASI Loader calls this after loading the plugin.
extern "C" __declspec(dllexport) void __cdecl InitializeASI() {
  static LONG once; if (InterlockedExchange(&once, 1)) return;
  initPaths(); readSettings();
  log("VCRender (protocol %u) loaded from %ls", VCR_PROTOCOL, g_pluginDir);
  if (!g_cfg.enabled) { log("disabled in VCRender.ini; the game uses its normal renderer"); return; }
  if (GetModuleHandleW(L"DLSS-IV.asi")) log("warning: DLSS-IV.asi is loaded; it cannot work with VCRender and should be disabled");
  HMODULE proxy = GetModuleHandleW(L"d3d9.dll");
  if (!proxy) { log("no d3d9.dll in the process; inactive"); return; }
  g_slots[0] = dispatchSlot(proxy, "Direct3DCreate9"); g_slots[1] = dispatchSlot(proxy, "Direct3DCreate9Ex");
  if (!g_slots[0] || !g_slots[1]) { log("d3d9.dll is not FusionFix's proxy layout (no forwarding slots); inactive"); return; }
  g_previous[0] = *g_slots[0]; g_previous[1] = *g_slots[1];
  readSettings();
  beginStart();
  if (!setSlot(g_slots[0], (PVOID) &VCR_Direct3DCreate9) || !setSlot(g_slots[1], (PVOID) &VCR_Direct3DCreate9Ex)) {
    setSlot(g_slots[0], g_previous[0]); setSlot(g_slots[1], g_previous[1]);
    log("could not redirect d3d9.dll; inactive"); return;
  }
  log("d3d9.dll (FusionFix proxy) now creates devices through VCRender; host starting in the background");
  installDxgiRedirect();
  companionStart();
}

// Lets a test program load this DLL directly as its d3d9.dll replacement.
extern "C" __declspec(dllexport) void __cdecl VCR_InitStandalone() {
  initPaths(); readSettings();
  log("VCRender standalone init from %ls", g_pluginDir);
  beginStart();
  installDxgiRedirect();
}

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_self = inst; DisableThreadLibraryCalls(inst);
    InitializeCriticalSectionAndSpinCount(&vcr::g_lock, 4000);
    InitializeCriticalSection(&vcr::g_logLock);
  } else if (reason == DLL_PROCESS_DETACH) {
    vcr::g_shuttingDown = true;
  }
  return TRUE;
}
