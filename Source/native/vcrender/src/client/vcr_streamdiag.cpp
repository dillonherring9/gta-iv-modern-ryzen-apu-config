// Streaming diagnostics (Claude, 2026-11-13): count and log the engine's failed resource loads.
//
// User report: after driving fast for a long time, roads/buildings went missing and stayed missing until a reload,
// while the texture budget was 71% full and the 32-bit address space had 1.3 GB free. Which limit fails is unknown,
// so this records it. GTA IV CE 1.2.0.59, from the decrypted .text captured on 2026-11-10
// (research/codex-streaming-memory-2026-11-10/texture-pressure-virtual-findings.md):
//  * 0xD5B480 loads one streaming request. At 0xD5B500 it calls 0xD5AD70(request, entry) (thiscall, 2 stack args,
//    ret 8, result in AL). A false result means the allocation still failed after up to 1,000 evictions; the caller then
//    asks for a large flush and drops the request.
//  * Inside 0xD5AD70 the resource allocator 0x6734A0 (cdecl, 3 args, AL) is called first at 0xD5ADA3 and again after each
//    eviction at 0xD5ADD8; the raw-file allocator 0x673110 (cdecl, 2 args, pointer) at 0xD5AE8E and 0xD5AED7.
// The five call instructions are pointed at counting wrappers that call the originals unchanged (only the rel32 of each
// call changes, one locked write inside a cache line, as for the texture budget). Nothing about loading changes.
#include "vcr_client.h"
#include <stdio.h>
#include <unordered_map>

namespace vcr {

void budgetAddress(uint32_t& unmappedMiB, uint32_t& gapMiB);

static const uintptr_t kLoad = 0xD5AD70, kLoadSite = 0xD5B500;
static const uintptr_t kRsc = 0x6734A0, kRscFirst = 0xD5ADA3, kRscRetry = 0xD5ADD8;
static const uintptr_t kRaw = 0x673110, kRawFirst = 0xD5AE8E, kRawRetry = 0xD5AED7;
static const uintptr_t kManager = 0x103E8D0;   // CStreamingInfoManager: entries at +0, counters at +0x20

typedef bool (__attribute__((thiscall)) *LoadFn)(void* self, void* req, void* entry);
typedef bool (__cdecl *RscFn)(void* a, void* b, uint32_t c);
typedef void* (__cdecl *RawFn)(uint32_t a, uint32_t b);

static uintptr_t g_dd;   // 0 in the game; the isolated test maps stand-ins elsewhere (address A lives at A + g_dd)
static volatile LONG g_rscFirstFail, g_rscRetries, g_rawFirstFail, g_rawRetries, g_loadFail, g_loads, g_diagState;
static std::unordered_map<uint32_t, uint32_t>* g_failsByIndex;
static CRITICAL_SECTION g_diagLock;

static bool __cdecl rscFirst(void* a, void* b, uint32_t c) { bool ok = ((RscFn) (kRsc + g_dd))(a, b, c); if (!ok) InterlockedIncrement(&g_rscFirstFail); return ok; }
static bool __cdecl rscRetry(void* a, void* b, uint32_t c) { InterlockedIncrement(&g_rscRetries); return ((RscFn) (kRsc + g_dd))(a, b, c); }
static void* __cdecl rawFirst(uint32_t a, uint32_t b) { void* p = ((RawFn) (kRaw + g_dd))(a, b); if (!p) InterlockedIncrement(&g_rawFirstFail); return p; }
static void* __cdecl rawRetry(uint32_t a, uint32_t b) { InterlockedIncrement(&g_rawRetries); return ((RawFn) (kRaw + g_dd))(a, b); }

static bool readable(uintptr_t a, size_t n) {
  MEMORY_BASIC_INFORMATION mi;
  return VirtualQuery((void*) a, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
         a + n <= (uintptr_t) mi.BaseAddress + mi.RegionSize;
}

static bool __attribute__((fastcall)) loadWrap(void* self, void*, void* req, void* entry) {
  bool ok = ((LoadFn) (kLoad + g_dd))(self, req, entry);
  InterlockedIncrement(&g_loads);
  if (ok) return ok;
  LONG n = InterlockedIncrement(&g_loadFail);
  uint32_t index = 0, dw = 0, fl = 0;
  if (readable((uintptr_t) req + 8, 2)) index = *(const uint16_t*) ((const uint8_t*) req + 8);
  if (readable((uintptr_t) entry, 16)) { dw = *(const uint32_t*) entry; fl = *(const uint16_t*) ((const uint8_t*) entry + 0xE); }
  const bool resource = (fl >> 13) & 1;
  const uint32_t virt = resource ? (dw & 0x7FF) << (((dw >> 11) & 0xF) + 8) : dw;
  const uint32_t phys = resource ? ((dw >> 15) & 0x7FF) << (((dw >> 26) & 0xF) + 8) : 0;
  uint32_t same = 0;
  EnterCriticalSection(&g_diagLock);
  if (!g_failsByIndex) g_failsByIndex = new std::unordered_map<uint32_t, uint32_t>();
  same = ++(*g_failsByIndex)[index];
  const size_t distinct = g_failsByIndex->size();
  LeaveCriticalSection(&g_diagLock);
  if (n <= 60 || n % 100 == 0 || same == 10 || same == 100) {
    uint32_t c[9] = {}; uint32_t un = 0, gap = 0;
    if (!g_dd && readable(kManager + 0x20, sizeof c)) memcpy(c, (const void*) (kManager + 0x20), sizeof c);
    budgetAddress(un, gap);
    log("[streaming] LOAD FAILED #%ld: resource #%u (%s, virtual %u KiB, physical %u KiB), its failure %u; "
        "virtual heap %u of %u MiB used, physical %u MiB used, %d MiB available; address %u MiB unmapped, gap %u MiB; "
        "allocator first-try failures %ld (raw %ld), eviction retries %ld (raw %ld); %u resources have failed",
        n, index, resource ? "resource" : "raw file", virt >> 10, phys >> 10, same, c[1] >> 20, c[0] >> 20, c[4] >> 20,
        (int32_t) c[3] / (1 << 20), un, gap, g_rscFirstFail, g_rawFirstFail, g_rscRetries, g_rawRetries, (unsigned) distinct);
  }
  return ok;
}

static uintptr_t target(uintptr_t site) { int32_t rel; memcpy(&rel, (const void*) (site + 1), 4); return site + 5 + rel; }

static int patchAll();
void streamDiagStart() {
  if (InterlockedCompareExchange(&g_diagState, 1, 0) != 0) return;
  wchar_t ini[MAX_PATH]; swprintf(ini, MAX_PATH, L"%ls\\VCRender.ini", pluginDir());
  if (!GetPrivateProfileIntW(L"Streaming", L"Diagnostics", 1, ini)) { log("[streaming] diagnostics off (VCRender.ini [Streaming] Diagnostics=0)"); return; }
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const wchar_t* nm = wcsrchr(exe, L'\\'); nm = nm ? nm + 1 : exe;
  if (_wcsicmp(nm, L"GTAIV.exe") != 0 || (uintptr_t) GetModuleHandleW(nullptr) != 0x400000) return;
  InitializeCriticalSection(&g_diagLock);
  if (patchAll() == 1) {
    InterlockedExchange(&g_diagState, 2);
    log("[streaming] diagnostics on: failed resource loads and allocator retries are counted (5 engine calls redirected; loading unchanged)");
  }
}
static int patchAll() {
  struct Site { uintptr_t at, want; void* wrap; } sites[5] = {
    { kLoadSite, kLoad, (void*) &loadWrap }, { kRscFirst, kRsc, (void*) &rscFirst }, { kRscRetry, kRsc, (void*) &rscRetry },
    { kRawFirst, kRaw, (void*) &rawFirst }, { kRawRetry, kRaw, (void*) &rawRetry } };
  for (auto& s : sites) {
    const uintptr_t at = s.at + g_dd;
    if (!readable(at, 5) || *(const uint8_t*) at != 0xE8 || target(at) != s.want + g_dd || ((at + 1) & 63) > 60) {
      log("[streaming] diagnostics not installed: the call at 0x%X is not the expected 1.2.0.59 code", (unsigned) s.at);
      return -1;
    }
  }
  for (auto& s : sites) {
    const uintptr_t at = s.at + g_dd;
    DWORD old;
    if (!VirtualProtect((void*) (at + 1), 4, PAGE_EXECUTE_READWRITE, &old)) { log("[streaming] diagnostics: VirtualProtect failed %lu", GetLastError()); return -2; }
    InterlockedExchange((volatile LONG*) (at + 1), (LONG) ((uintptr_t) s.wrap - (at + 5)));
    DWORD ignored; VirtualProtect((void*) (at + 1), 4, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), (void*) at, 5);
  }
  return 1;
}

// For the periodic [streaming] line; empty when not installed.
void streamDiagSummary(char* out, size_t cap) {
  if (g_diagState != 2) { if (cap) out[0] = 0; return; }
  snprintf(out, cap, "; loads %ld, failed %ld; allocator first-try failures %ld (raw %ld), eviction retries %ld (raw %ld)",
           g_loads, g_loadFail, g_rscFirstFail, g_rawFirstFail, g_rscRetries, g_rawRetries);
}

}  // namespace vcr

// Isolated test hooks (tests/streamdiag_test.cpp maps stand-ins of the three engine routines with the game's layout).
extern "C" __declspec(dllexport) int __cdecl VCR_TestStreamDiag(uintptr_t imageAt670000) {
  vcr::g_dd = imageAt670000 - 0x670000;
  InitializeCriticalSection(&vcr::g_diagLock);
  int r = vcr::patchAll();
  if (r == 1) vcr::g_diagState = 2;
  return r;
}
extern "C" __declspec(dllexport) void __cdecl VCR_TestStreamDiagSummary(char* out, size_t cap) { vcr::streamDiagSummary(out, cap); }
