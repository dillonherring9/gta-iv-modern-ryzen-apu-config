// Phase 2: a larger texture budget for GTA IV's streaming while VCRender renders.
//
// GTA IV CE 1.2.0.59 (GTAIV.exe, image base 0x400000), read from the decrypted .text captured on 2026-11-10
// (research/codex-streaming-memory-2026-11-10/runtime/resample-20261111T042502Z/GTAIV-live-text.bin):
//  * 0x427E40 (thiscall, ecx = grcResourceCache, xmm2 = multiplier, one stack argument, ret 4, result edx:eax)
//    returns the video memory resources may use: min(video memory, quality-table limit) x multiplier minus what the
//    cache already holds. Argument 0 is the normal path (FusionFix forces its -nomemrestrict branch); 1 is a
//    low-system-RAM path the engine only takes below 2.5 GiB of RAM.
//  * Its 13 callers are GetAvailableFreeMemory (0x4276B0, multiplier 0.9 from the streaming manager) and the
//    resource cache's own over-budget checks (multiplier 1.0). The quality-table limit is what holds the streaming
//    budget near 1,070 MiB at High texture quality.
//  * Texture data the engine streams is kept in the game's own (32-bit) memory, so in-process the limit protected the
//    address space that Vulkan, the driver and LLVM also needed. With VCRender those live in the 64-bit host.
//
// The 13 call instructions are pointed at vcrAvailWrap, which calls the original and, for argument 0, adds
// ExtraTextureMiB x multiplier: the same as a quality-table limit ExtraTextureMiB higher, so both checks move
// together. Only the 4-byte call displacement changes (one locked write inside a cache line), so a thread running
// the code at that moment calls either the original or the wrapper; both are correct.
// A watcher switches the extra off while the 32-bit address space runs low and back on with hysteresis.
#include "vcr_client.h"
#include "../shared/vcr_maps.h"
#include <stdio.h>
#include <wchar.h>

extern "C" {
float vcrBudgetExtra = 0.0f;   // bytes added to the engine's limit (as a float, multiplied by the caller's xmm2)
uintptr_t vcrAvailTarget = 0x427E40;   // the engine routine (moved only by the isolated test)
void vcrAvailWrap();
}

// eax/ecx/edx/xmm0/xmm1 are scratch in the original as well; xmm2 (the multiplier) is passed through unchanged.
asm(R"(
  .text
  .globl _vcrAvailWrap
  .p2align 4
_vcrAvailWrap:
  cmpl $0, 4(%esp)
  jne 1f
  subl $8, %esp
  movss %xmm2, (%esp)
  pushl $0
  calll *_vcrAvailTarget
  movss (%esp), %xmm0
  mulss _vcrBudgetExtra, %xmm0
  xorps %xmm1, %xmm1
  maxss %xmm1, %xmm0
  cvttss2si %xmm0, %ecx
  addl %ecx, %eax
  adcl $0, %edx
  addl $8, %esp
  retl $4
1:
  jmpl *_vcrAvailTarget
)");

namespace vcr {

const wchar_t* pluginDir();

static const uintptr_t kAvail = 0x427E40;
static const uint8_t kAvailHead[12] = {0x83, 0xEC, 0x60, 0x83, 0x7C, 0x24, 0x64, 0x00, 0x53, 0x55, 0x56, 0x57};
static const uintptr_t kSites[13] = {0x4276C5, 0x427762, 0x428B63, 0x429153, 0x4291AF, 0x4294B1, 0x429536,
                                     0x429750, 0x429797, 0x42984E, 0x42989A, 0x429D6A, 0x429DB9};

struct BudgetCfg { int extraMiB = 512; int lowMiB = 600; int lowGapMiB = 200; };
static BudgetCfg g_bc;
static volatile LONG g_bstate;      // 0 not tried, 1 waiting for the game code, 2 installed, -1 not installed
static volatile LONG g_open;        // extra currently applied
static int g_tries;
static uintptr_t g_delta;           // 0 in the game; the isolated test maps its stand-in elsewhere

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void readBudgetCfg() {
  wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\VCRender.ini", pluginDir());
  g_bc.extraMiB = clampi((int) GetPrivateProfileIntW(L"Streaming", L"ExtraTextureMiB", 512, path), 0, 1024);
  g_bc.lowMiB = clampi((int) GetPrivateProfileIntW(L"Streaming", L"LowAddressMiB", 600, path), 256, 2048);
  g_bc.lowGapMiB = clampi((int) GetPrivateProfileIntW(L"Streaming", L"LowGapMiB", 200, path), 64, 1024);
}

static bool readable(uintptr_t a, size_t n, bool exec) {
  MEMORY_BASIC_INFORMATION mi;
  if (!VirtualQuery((void*) a, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
  if (exec && !(mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
  return a + n <= (uintptr_t) mi.BaseAddress + mi.RegionSize;
}
static uintptr_t callTarget(uintptr_t site) { int32_t rel; memcpy(&rel, (const void*) (site + 1), 4); return site + 5 + rel; }

// 1 = installed, 0 = game code not there yet (retry later), negative = will never match
static int patchSites(char* why, size_t whyLen) {
  const uintptr_t avail = kAvail + g_delta;
  if (!readable(avail, sizeof kAvailHead, true) || memcmp((const void*) avail, kAvailHead, sizeof kAvailHead) != 0) {
    snprintf(why, whyLen, "routine at 0x%X not (yet) as in GTA IV 1.2.0.59", (unsigned) kAvail); return 0;
  }
  const uintptr_t wrap = (uintptr_t) &vcrAvailWrap;
  uintptr_t sites[13];
  for (int i = 0; i < 13; i++) sites[i] = kSites[i] + g_delta;
  for (uintptr_t s : sites) {
    if (!readable(s, 5, true) || *(const uint8_t*) s != 0xE8) { snprintf(why, whyLen, "0x%X is not a call", (unsigned) s); return -1; }
    uintptr_t t = callTarget(s);
    if (t != avail && t != wrap) { snprintf(why, whyLen, "call at 0x%X goes to 0x%X (changed by another mod?)", (unsigned) s, (unsigned) t); return -2; }
    if (((s + 1) & 63) > 60) { snprintf(why, whyLen, "call at 0x%X spans a cache line", (unsigned) s); return -3; }
  }
  vcrAvailTarget = avail;
  for (uintptr_t s : sites) {
    DWORD old;
    if (!VirtualProtect((void*) (s + 1), 4, PAGE_EXECUTE_READWRITE, &old)) { snprintf(why, whyLen, "VirtualProtect 0x%X failed %lu", (unsigned) s, GetLastError()); return -4; }
    InterlockedExchange((volatile LONG*) (s + 1), (LONG) (wrap - (s + 5)));
    DWORD ignored; VirtualProtect((void*) (s + 1), 4, old, &ignored);
  }
  FlushInstructionCache(GetCurrentProcess(), (void*) sites[0], sites[12] + 5 - sites[0]);
  return 1;
}

static void setOpen(bool on) {
  InterlockedExchange(&g_open, on ? 1 : 0);
  vcrBudgetExtra = on ? (float) ((double) g_bc.extraMiB * 1048576.0) : 0.0f;
}

static bool addressRoom(uint64_t& unmapped, uint64_t& gap) {
  VcrMaps m;
  if (vcrReadMaps(m)) { unmapped = m.unmapped; gap = m.largestGap; return true; }
  MEMORY_BASIC_INFORMATION mi; uintptr_t p = 0x10000; unmapped = gap = 0;
  while (p < 0xFFFF0000u && VirtualQuery((void*) p, &mi, sizeof mi)) {
    if (mi.State == MEM_FREE) { unmapped += mi.RegionSize; if (mi.RegionSize > gap) gap = mi.RegionSize; }
    p = (uintptr_t) mi.BaseAddress + mi.RegionSize;
  }
  return false;
}

static volatile LONG g_lastUnMiB = -1, g_lastGapMiB = -1;   // last sample of the watcher (for the streaming diagnostics)
void budgetAddress(uint32_t& unmappedMiB, uint32_t& gapMiB) {
  if (g_lastUnMiB >= 0) { unmappedMiB = (uint32_t) g_lastUnMiB; gapMiB = (uint32_t) g_lastGapMiB; return; }
  uint64_t un = 0, gap = 0; addressRoom(un, gap); unmappedMiB = (uint32_t) (un >> 20); gapMiB = (uint32_t) (gap >> 20);
}
static DWORD WINAPI watchThread(void*) {
  const uint64_t lowB = (uint64_t) g_bc.lowMiB << 20, gapB = (uint64_t) g_bc.lowGapMiB << 20;
  const uint64_t reopenB = lowB + (256ull << 20), reopenGapB = gapB + (128ull << 20);
  for (;;) {
    Sleep(2000);
    uint64_t un = 0, gap = 0; bool linux = addressRoom(un, gap);
    InterlockedExchange(&g_lastUnMiB, (LONG) (un >> 20)); InterlockedExchange(&g_lastGapMiB, (LONG) (gap >> 20));
    if (g_open && (un < lowB || gap < gapB)) {
      setOpen(false);
      log("[budget] address space low (%s view: %llu MiB unmapped, largest gap %llu MiB): extra texture budget OFF, the engine's own budget applies",
          linux ? "Linux" : "Windows", un >> 20, gap >> 20);
    } else if (!g_open && un >= reopenB && gap >= reopenGapB) {
      setOpen(true);
      log("[budget] address space recovered (%llu MiB unmapped, largest gap %llu MiB): extra texture budget ON (+%d MiB)", un >> 20, gap >> 20, g_bc.extraMiB);
    }
  }
  return 0;
}

static void tryInstall(bool lastChance) {
  char why[160] = "";
  int r = patchSites(why, sizeof why);
  if (r == 1) {
    InterlockedExchange(&g_bstate, 2);
    setOpen(true);
    uint64_t un = 0, gap = 0; addressRoom(un, gap);
    log("[budget] texture budget: engine limit raised by %d MiB (13 engine calls redirected; streaming budget +%d MiB at its 0.9 factor). "
        "Off below %d MiB unmapped or a %d MiB largest gap; now %llu MiB unmapped, largest gap %llu MiB",
        g_bc.extraMiB, g_bc.extraMiB * 9 / 10, g_bc.lowMiB, g_bc.lowGapMiB, un >> 20, gap >> 20);
    CloseHandle(CreateThread(nullptr, 0, watchThread, nullptr, 0, nullptr));
  } else if (r < 0 || lastChance) {
    InterlockedExchange(&g_bstate, -1);
    log("[budget] texture budget left at the game's own value: %s", why);
  } else if (g_bstate == 0) {
    InterlockedExchange(&g_bstate, 1);
    log("[budget] waiting for the game code: %s", why);
  }
}

static bool isGame() {
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const wchar_t* n = wcsrchr(exe, L'\\'); n = n ? n + 1 : exe;
  return _wcsicmp(n, L"GTAIV.exe") == 0 && (uintptr_t) GetModuleHandleW(nullptr) == 0x400000;
}

// Called once VCRender is the game's renderer (first Direct3DCreate9 with the host connected).
void budgetStart() {
  if (InterlockedCompareExchange(&g_bstate, 1, 0) != 0) return;
  if (!isGame()) { InterlockedExchange(&g_bstate, -1); return; }
  readBudgetCfg();
  if (g_bc.extraMiB <= 0) { InterlockedExchange(&g_bstate, -1); log("[budget] ExtraTextureMiB=0: the game's own texture budget applies"); return; }
  InterlockedExchange(&g_bstate, 0);
  tryInstall(false);
}
// Present: retries for a while if the code was not ready at device creation.
void budgetPoll() {
  if (g_bstate != 1) return;
  if (++g_tries % 60) return;
  tryInstall(g_tries >= 60 * 50);
}
// For the [streaming] log line: -1 not installed, else the extra currently applied in MiB.
int budgetExtraMiB() { return g_bstate == 2 ? (g_open ? g_bc.extraMiB : 0) : -1; }

}  // namespace vcr

// Isolated test hooks (tests/budget_test.cpp maps a stand-in of the engine routine with the game's layout).
extern "C" __declspec(dllexport) int __cdecl VCR_TestBudgetInstall(int extraMiB, uintptr_t standInImage) {
  vcr::g_bc = vcr::BudgetCfg(); vcr::g_bc.extraMiB = extraMiB;
  vcr::g_delta = standInImage - 0x427000;
  char why[160] = "";
  int r = vcr::patchSites(why, sizeof why);
  if (r == 1) { vcr::g_bstate = 2; vcr::setOpen(true); }
  return r;
}
extern "C" __declspec(dllexport) void __cdecl VCR_TestBudgetOpen(int on) { vcr::setOpen(on != 0); }
