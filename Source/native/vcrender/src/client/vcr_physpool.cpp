// Physical resource pool (Claude, 2026-11-13): big texture/model blocks no longer need a hole in the 160 MiB engine heap.
//
// Step 263's log of the user's helicopter jump (2026-11-13 06:41-06:52): northbuild.wtd (31.5 MiB physical) and
// police.wtd (25 MiB) failed to load 3,829 times. Each failure followed ~1,000 evictions and ended in the engine's
// "flush 128 MiB virtual / 1 GiB physical" call, so the world was emptied again and again (texture budget down to 7%).
//
// GTA IV CE 1.2.0.59 (decrypted .text of 2026-11-11, .rdata from GTAIV.exe):
//  * The current allocator is a multi-allocator at 0x1BB68D8 (vtable 0xFC9944): heaps[0..3] at +4, count at +0x24.
//    Static init (0x401190-0x4011F4) sets heap 0 = 0x1BB6908 (fixed 100 MiB general heap) and heaps 1, 2 and 3 =
//    0x1BB8A08, the 160 MiB resource heap (vtable 0xFC99C4, 16 KiB pages).
//  * That heap is a buddy allocator: 0x4108F0 rounds every request up to a power-of-two page count. A 31.5 MiB block
//    therefore needs a completely free, aligned 32 MiB block; 160 MiB holds five. Resident pieces that cannot be
//    evicted in each of them make the load impossible however much is evicted.
//  * Resources take their virtual part from heap 1 and their physical part (texture/vertex data) from heap 3:
//    0x6734A0/0x673640 call Allocate(size, 0x1000, 3); 0x673110 raw files likewise. They are freed through
//    GetAllocator(3)->Free (0x673150, 0x5EF2A0, 0x6736AD) or through the multi-allocator's Free (0x401E90), which asks
//    heaps 0..3 in order who owns the pointer (vtable +0x48; heap 0 answers by its own range, 0x403110).
//
// This module puts its own allocator object in heaps[3]. Requests above PoolLargeMiB, and any request the engine heap
// cannot place, get their own VirtualAlloc block (up to PoolMaxMiB in total, and never below LowAddressMiB of free
// address space). Everything else is passed to the engine heap unchanged: every vtable entry this object does not
// handle itself forwards to the original with the engine heap as `this`. Pointers this pool did not allocate are
// always handed to the engine heap. Heaps 1 and 2 are not touched.
#include "vcr_client.h"
#include <stdio.h>
#include <map>

namespace vcr {

void budgetAddress(uint32_t& unmappedMiB, uint32_t& gapMiB);

#define TC __attribute__((thiscall))
static const uintptr_t kMulti = 0x1BB68D8, kMultiVt = 0xFC9944, kGeneral = 0x1BB6908, kHeap = 0x1BB8A08, kHeapVt = 0xFC99C4;
static const int kSlots = 29;
static const uint32_t kHeapSlots[kSlots] = {
  0x401730, 0x401710, 0x402280, 0x402310, 0x401670, 0x401670, 0x402440, 0x4023A0, 0x4023D0, 0x4023E0,
  0x401690, 0x4016A0, 0x4016B0, 0x4016C0, 0x4016A0, 0x4016D0, 0x4016D0, 0x4016A0, 0x402420, 0x4016F0,
  0x4021B0, 0x4021C0, 0x4021D0, 0x4016D0, 0x402690, 0x402710, 0x4024B0, 0x402570, 0x402600 };
enum { S_DTOR = 0, S_ALLOC = 2, S_FREE = 3, S_GETA = 4, S_GETA2 = 5, S_SIZE = 6, S_AVAIL = 8, S_LARGEST = 9,
       S_VALID = 18, S_SIZE2 = 19, S_MARK = 24, S_UNMARK = 25 };

typedef void* (TC *AllocFn)(void*, uint32_t, uint32_t, uint32_t);
typedef void (TC *PtrFn)(void*, void*);
typedef uint32_t (TC *SizeFn)(void*, void*);
typedef bool (TC *ValidFn)(void*, void*);
typedef uint32_t (TC *StatFn)(void*);

static uintptr_t g_pd;                  // 0 in the game; the isolated test lays stand-ins out elsewhere
static void* g_heap;                    // the engine's resource heap object
static const uint32_t* g_heapVt;        // its vtable (in .rdata)
static struct PoolObject { void* const* vt; } g_obj;
static void* g_vt[kSlots];
static volatile LONG g_state;           // 0 off, 1 starting, 2 on
static uint32_t g_largeBytes = 8u << 20, g_maxBytes = 512u << 20, g_lowAddrMiB = 600;

static SRWLOCK g_lock = SRWLOCK_INIT;
static std::map<uintptr_t, uint32_t>* g_blocks;   // base -> requested size
static volatile LONG g_used, g_peak, g_count, g_large, g_fallback, g_refused, g_frees, g_logged;

static uint32_t reserveBytes(uint32_t size) { return (size + 0xFFFFu) & ~0xFFFFu; }   // VirtualAlloc granularity

static void* poolAlloc(uint32_t size, bool large) {
  if (!size || size > g_maxBytes) return nullptr;
  const LONG r = (LONG) reserveBytes(size);
  if ((uint32_t) InterlockedExchangeAdd(&g_used, r) + (uint32_t) r > g_maxBytes) {
    InterlockedExchangeAdd(&g_used, -r); InterlockedIncrement(&g_refused); return nullptr;
  }
  if (!g_pd) {   // keep the address-space floor the texture budget uses
    uint32_t un = 0, gap = 0; budgetAddress(un, gap);
    if (un && (un < g_lowAddrMiB + ((uint32_t) r >> 20) || gap < ((uint32_t) r >> 20) + 64)) {
      InterlockedExchangeAdd(&g_used, -r); InterlockedIncrement(&g_refused); return nullptr;
    }
  }
  void* p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!p) { InterlockedExchangeAdd(&g_used, -r); InterlockedIncrement(&g_refused); return nullptr; }
  AcquireSRWLockExclusive(&g_lock);
  if (!g_blocks) g_blocks = new std::map<uintptr_t, uint32_t>();
  (*g_blocks)[(uintptr_t) p] = size;
  ReleaseSRWLockExclusive(&g_lock);
  const LONG n = InterlockedIncrement(&g_count);
  InterlockedIncrement(large ? &g_large : &g_fallback);
  for (LONG pk = g_peak, u = g_used; u > pk; pk = g_peak) if (InterlockedCompareExchange(&g_peak, u, pk) == pk) break;
  const LONG k = InterlockedIncrement(&g_logged);
  if (!g_pd && (k <= 20 || k % 200 == 0))
    log("[physpool] block %ld: %.1f MiB %s; pool now %ld blocks, %u MiB", k, size / 1048576.0,
        large ? "(large physical resource)" : "(engine heap could not place it)", n, (uint32_t) g_used >> 20);
  return p;
}
// Size of the pool block containing p, or 0 when p is not the pool's.
static uint32_t poolFind(const void* p, uintptr_t* base) {
  uint32_t size = 0;
  AcquireSRWLockShared(&g_lock);
  if (g_blocks && !g_blocks->empty()) {
    auto it = g_blocks->upper_bound((uintptr_t) p);
    if (it != g_blocks->begin()) {
      --it;
      if ((uintptr_t) p < it->first + it->second) { size = it->second; if (base) *base = it->first; }
    }
  }
  ReleaseSRWLockShared(&g_lock);
  return size;
}
static bool poolFree(void* p) {
  uint32_t size = 0;
  AcquireSRWLockExclusive(&g_lock);
  if (g_blocks) {
    auto it = g_blocks->find((uintptr_t) p);
    if (it != g_blocks->end()) { size = it->second; g_blocks->erase(it); }
  }
  ReleaseSRWLockExclusive(&g_lock);
  if (!size) return false;
  VirtualFree(p, 0, MEM_RELEASE);
  InterlockedExchangeAdd(&g_used, -(LONG) reserveBytes(size));
  InterlockedDecrement(&g_count); InterlockedIncrement(&g_frees);
  return true;
}

// ---- the allocator object's own entries (thiscall, same arguments and stack cleanup as the engine heap's)
static void* TC oDtor(void* self, uint32_t) { return self; }   // static object: never destroyed
static void* TC oAlloc(void*, uint32_t size, uint32_t align, uint32_t heap) {
  const bool poolable = align <= 0x10000, large = poolable && size > g_largeBytes;
  if (large) if (void* p = poolAlloc(size, true)) return p;
  if (void* p = ((AllocFn) g_heapVt[S_ALLOC])(g_heap, size, align, heap)) return p;
  return poolable && !large ? poolAlloc(size, false) : nullptr;   // a refused large block was already tried
}
static void TC oFree(void*, void* p) {
  if (!p) return;
  if (!poolFree(p)) ((PtrFn) g_heapVt[S_FREE])(g_heap, p);
}
static void* TC oGetAllocator(void* self, uint32_t) { return self; }
static uint32_t TC oSize(void*, void* p) {
  if (uint32_t s = p ? poolFind(p, nullptr) : 0) return s;
  return ((SizeFn) g_heapVt[S_SIZE])(g_heap, p);
}
static uint32_t poolRoom() {
  uint32_t room = g_maxBytes > (uint32_t) g_used ? g_maxBytes - (uint32_t) g_used : 0;
  if (!g_pd) {
    uint32_t un = 0, gap = 0; budgetAddress(un, gap);
    if (un) {
      const uint32_t a = un > g_lowAddrMiB ? un - g_lowAddrMiB : 0, g = gap > 64 ? gap - 64 : 0;
      const uint32_t lim = (a < g ? a : g); if (lim < 4096 && (lim << 20) < room) room = lim << 20;
    }
  }
  return room;
}
static uint32_t TC oLargest(void*) {
  const uint32_t heap = ((StatFn) g_heapVt[S_LARGEST])(g_heap), room = poolRoom();
  return heap > room ? heap : room;
}
static bool TC oValid(void*, void* p) {
  if (p && poolFind(p, nullptr)) return true;
  return ((ValidFn) g_heapVt[S_VALID])(g_heap, p);
}
static void TC oMark(void*, void* p) { if (!p || !poolFind(p, nullptr)) ((PtrFn) g_heapVt[S_MARK])(g_heap, p); }
static void TC oUnmark(void*, void* p) { if (!p || !poolFind(p, nullptr)) ((PtrFn) g_heapVt[S_UNMARK])(g_heap, p); }

static bool readable(uintptr_t a, size_t n) {
  MEMORY_BASIC_INFORMATION mi;
  return VirtualQuery((void*) a, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
         a + n <= (uintptr_t) mi.BaseAddress + mi.RegionSize;
}
#define AT(a) ((a) + g_pd)

static int install() {
  const uintptr_t multi = AT(kMulti), heap = AT(kHeap);
  if (!readable(multi, 0x28) || !readable(heap, 0x20) || !readable(AT(kHeapVt), kSlots * 4)) return -1;
  const uint32_t* m = (const uint32_t*) multi;
  if (m[0] != AT(kMultiVt) || m[9] != 4 || m[1] != AT(kGeneral) || m[2] != heap || m[3] != heap || m[4] != heap) return -2;
  if (*(const uint32_t*) heap != AT(kHeapVt)) return -3;
  const uint32_t* vt = (const uint32_t*) AT(kHeapVt);
  for (int i = 0; i < kSlots; i++) if (vt[i] != AT(kHeapSlots[i])) return -4;
  g_heap = (void*) heap; g_heapVt = vt;

  // Forwarders: mov ecx, <engine heap>; jmp dword ptr [<engine vtable entry>]
  uint8_t* code = (uint8_t*) VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!code) return -5;
  for (int i = 0; i < kSlots; i++) {
    uint8_t* t = code + i * 16; const uint32_t self = (uint32_t) heap, slot = (uint32_t) (uintptr_t) &vt[i];
    t[0] = 0xB9; memcpy(t + 1, &self, 4); t[5] = 0xFF; t[6] = 0x25; memcpy(t + 7, &slot, 4);
    g_vt[i] = t;
  }
  DWORD old; if (!VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old)) return -6;
  FlushInstructionCache(GetCurrentProcess(), code, 4096);
  g_vt[S_DTOR] = (void*) &oDtor; g_vt[S_ALLOC] = (void*) &oAlloc; g_vt[S_FREE] = (void*) &oFree;
  g_vt[S_GETA] = g_vt[S_GETA2] = (void*) &oGetAllocator; g_vt[S_SIZE] = g_vt[S_SIZE2] = (void*) &oSize;
  g_vt[S_LARGEST] = (void*) &oLargest; g_vt[S_VALID] = (void*) &oValid; g_vt[S_MARK] = (void*) &oMark; g_vt[S_UNMARK] = (void*) &oUnmark;
  g_obj.vt = g_vt;
  // heaps[3]: only if it is still the engine heap (one locked write; allocations made before keep going to the heap
  // through the forwarding Free)
  if ((uintptr_t) InterlockedCompareExchange((volatile LONG*) (multi + 4 + 3 * 4), (LONG) (uintptr_t) &g_obj, (LONG) heap) != heap) return -7;
  return 1;
}

void physPoolStart() {
  if (InterlockedCompareExchange(&g_state, 1, 0) != 0) return;
  wchar_t ini[MAX_PATH]; swprintf(ini, MAX_PATH, L"%ls\\VCRender.ini", pluginDir());
  if (!GetPrivateProfileIntW(L"Streaming", L"PhysicalPool", 1, ini)) { log("[physpool] off (VCRender.ini [Streaming] PhysicalPool=0)"); return; }
  int large = (int) GetPrivateProfileIntW(L"Streaming", L"PoolLargeMiB", 8, ini), cap = (int) GetPrivateProfileIntW(L"Streaming", L"PoolMaxMiB", 512, ini);
  large = large < 1 ? 1 : large > 64 ? 64 : large; cap = cap < 64 ? 64 : cap > 1024 ? 1024 : cap;
  g_largeBytes = (uint32_t) large << 20; g_maxBytes = (uint32_t) cap << 20;
  g_lowAddrMiB = GetPrivateProfileIntW(L"Streaming", L"LowAddressMiB", 600, ini);
  wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
  const wchar_t* nm = wcsrchr(exe, L'\\'); nm = nm ? nm + 1 : exe;
  if (_wcsicmp(nm, L"GTAIV.exe") != 0 || (uintptr_t) GetModuleHandleW(nullptr) != 0x400000) return;
  const int r = install();
  if (r != 1) { log("[physpool] not installed (%d): the engine allocators are not the expected 1.2.0.59 layout", r); return; }
  InterlockedExchange(&g_state, 2);
  log("[physpool] on: physical resource blocks over %d MiB, and any the 160 MiB engine heap cannot place, get their own memory "
      "(up to %d MiB, keeping %u MiB of address space free); engine heap now %u MiB free, largest block %u MiB",
      large, cap, g_lowAddrMiB, ((StatFn) g_heapVt[S_AVAIL])(g_heap) >> 20, ((StatFn) g_heapVt[S_LARGEST])(g_heap) >> 20);
}

void physPoolSummary(char* out, size_t cap) {
  if (g_state != 2) { if (cap) out[0] = 0; return; }
  snprintf(out, cap, "pool %ld blocks, %u MiB (peak %u MiB); %ld large, %ld engine-heap fallbacks, %ld freed, %ld refused; "
           "engine heap %u MiB free, largest block %u MiB",
           g_count, (uint32_t) g_used >> 20, (uint32_t) g_peak >> 20, g_large, g_fallback, g_frees, g_refused,
           ((StatFn) g_heapVt[S_AVAIL])(g_heap) >> 20, ((StatFn) g_heapVt[S_LARGEST])(g_heap) >> 20);
}

}  // namespace vcr

// Isolated test hooks (tests/physpool_test.cpp lays stand-ins of the two allocators out with the game's addresses).
extern "C" __declspec(dllexport) int __cdecl VCR_TestPhysPool(uintptr_t imageAt400000, uint32_t largeMiB, uint32_t maxMiB) {
  vcr::g_pd = imageAt400000 - 0x400000;
  vcr::g_largeBytes = largeMiB << 20; vcr::g_maxBytes = maxMiB << 20;
  int r = vcr::install();
  if (r == 1) vcr::g_state = 2;
  return r;
}
extern "C" __declspec(dllexport) void __cdecl VCR_TestPhysPoolSummary(char* out, size_t cap) { vcr::physPoolSummary(out, cap); }
