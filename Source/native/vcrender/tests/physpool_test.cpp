// Physical resource pool (vcr_physpool.cpp) against stand-ins of GTA IV's multi-allocator (0x1BB68D8) and 160 MiB
// buddy resource heap (0x1BB8A08, vtable 0xFC99C4), laid out with the game's addresses in a fresh allocation
// (game address A at R + A - 0x400000). Each of the heap's 29 vtable targets is a trampoline to a stand-in that records
// which entry ran, with which `this` and arguments, and pops the same number of arguments as the game's code.
// usage: physpool_test.exe VCRender.asi results.txt
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#define TC __attribute__((thiscall))
static uintptr_t R;
static uintptr_t at(uintptr_t a) { return R + (a - 0x400000); }
static FILE* o; static int fails;
static void check(bool ok, const char* what, long long got, long long want) {
  fprintf(o, "%s %s (got %lld, want %lld)\n", ok ? "PASS" : "FAIL", what, got, want); if (!ok) fails++;
}

static const uintptr_t kMulti = 0x1BB68D8, kMultiVt = 0xFC9944, kGeneral = 0x1BB6908, kHeap = 0x1BB8A08, kHeapVt = 0xFC99C4;
static const uint32_t kSlots[29] = {
  0x401730, 0x401710, 0x402280, 0x402310, 0x401670, 0x401670, 0x402440, 0x4023A0, 0x4023D0, 0x4023E0,
  0x401690, 0x4016A0, 0x4016B0, 0x4016C0, 0x4016A0, 0x4016D0, 0x4016D0, 0x4016A0, 0x402420, 0x4016F0,
  0x4021B0, 0x4021C0, 0x4021D0, 0x4016D0, 0x402690, 0x402710, 0x4024B0, 0x402570, 0x402600 };

// ---- stand-in buddy heap: a 48 MiB bump arena; object fields as the game's (+4 base, +0xC size)
static uintptr_t g_arena; static const uint32_t kArena = 48u << 20; static uint32_t g_bump;
static bool g_heapFull; static uint32_t g_heapMax = 0xFFFFFFFF;
static volatile LONG g_hits[0x500000 >> 4]; static volatile LONG g_badThis, g_foreignFree, g_heapFrees, g_heapAllocs;
static CRITICAL_SECTION g_hl;
static void hit(uintptr_t fn, void* self) { InterlockedIncrement(&g_hits[(fn - 0x400000) >> 4]); if ((uintptr_t) self != at(kHeap)) InterlockedIncrement(&g_badThis); }
static long hits(uintptr_t fn) { return g_hits[(fn - 0x400000) >> 4]; }
static bool inArena(const void* p) { return (uintptr_t) p >= g_arena && (uintptr_t) p < g_arena + kArena; }

static void* TC h401730(void* s, uint32_t) { hit(0x401730, s); return s; }
static uint32_t TC h401710(void* s, uint32_t) { hit(0x401710, s); return 0; }
static void* TC h402280(void* s, uint32_t size, uint32_t align, uint32_t heap) {   // Allocate
  hit(0x402280, s);
  if (g_heapFull || size > g_heapMax || heap != 3) return nullptr;
  EnterCriticalSection(&g_hl);
  uint32_t need = (size + 0x3FFF) & ~0x3FFFu; void* p = nullptr;
  if (g_bump + need > kArena) g_bump = 0;   // wraps: a bump arena is enough for these checks
  if (g_bump + need <= kArena) { p = (void*) (g_arena + g_bump); g_bump += need; }
  LeaveCriticalSection(&g_hl);
  if (p) InterlockedIncrement(&g_heapAllocs);
  return p;
}
static void TC h402310(void* s, void* p) { hit(0x402310, s); if (p && !inArena(p)) InterlockedIncrement(&g_foreignFree); else if (p) InterlockedIncrement(&g_heapFrees); }
static void* TC h401670(void* s, uint32_t) { hit(0x401670, s); return s; }
static uint32_t TC h402440(void* s, void* p) { hit(0x402440, s); if (p && !inArena(p)) InterlockedIncrement(&g_foreignFree); return 0x4000; }
static uint32_t TC h4023A0(void* s, uint32_t i) { hit(0x4023A0, s); return 0x1000 + i; }
static uint32_t TC h4023D0(void* s) { hit(0x4023D0, s); return 7u << 20; }
static uint32_t TC h4023E0(void* s) { hit(0x4023E0, s); return 4u << 20; }   // largest block 4 MiB
static uint32_t TC h401690(void* s) { hit(0x401690, s); return 1; }
static uint32_t TC h4016A0(void* s) { hit(0x4016A0, s); return 0xA0; }
static uint32_t TC h4016B0(void* s, uint32_t, uint32_t) { hit(0x4016B0, s); return 0; }
static uint32_t TC h4016C0(void* s, uint32_t a, uint32_t b) { hit(0x4016C0, s); return a ^ b; }
static uint32_t TC h4016D0(void* s) { hit(0x4016D0, s); return 0; }
static bool TC h402420(void* s, void* p) { hit(0x402420, s); return inArena(p); }   // IsValidPointer: range check
static uint32_t TC h4021B0(void* s) { hit(0x4021B0, s); return kArena; }
static uint32_t TC h4021C0(void* s) { hit(0x4021C0, s); return (uint32_t) g_arena; }
static void TC h4021D0(void* s, uint32_t) { hit(0x4021D0, s); }
static void TC h402690(void* s, void* p) { hit(0x402690, s); if (p && !inArena(p)) InterlockedIncrement(&g_foreignFree); }
static void TC h402710(void* s, void* p) { hit(0x402710, s); if (p && !inArena(p)) InterlockedIncrement(&g_foreignFree); }
static uint32_t TC h4024B0(void* s, uint32_t a) { hit(0x4024B0, s); return a + 1; }
static uint32_t TC h402570(void* s, uint32_t a) { hit(0x402570, s); return a + 2; }
static uint32_t TC h402600(void* s) { hit(0x402600, s); return 0x600; }

// ---- stand-in general heap (heap 0): owns nothing the tests allocate
static bool TC g402420(void*, void*) { return false; }

static void trampoline(uintptr_t a, void* fn) {   // jmp dword ptr [a + 6]
  uint8_t* p = (uint8_t*) at(a); p[0] = 0xFF; p[1] = 0x25; uint32_t slot = (uint32_t) at(a) + 6; memcpy(p + 2, &slot, 4); memcpy(p + 6, &fn, 4);
}

// Thiscall through a vtable entry: `this` in ecx, n stack arguments, the callee pops them. Everything is in registers
// (no stack-relative operands while pushing, safe on several threads); ebx comes back as the stack difference, and
// esp is restored either way.
static int g_unbalanced;
static void** vtOf(void* obj) { return *(void***) obj; }
static void* heaps3() { return *(void**) (at(kMulti) + 4 + 12); }
static uint32_t callN(void* obj, int off, int n, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
  void* fn = vtOf(obj)[off / 4]; uint32_t r, diff; void* t = obj;
  switch (n) {
    case 0: asm volatile("movl %%esp, %%ebx\n\t call *%%eax\n\t subl %%esp, %%ebx\n\t addl %%ebx, %%esp"
                         : "=a"(r), "=b"(diff), "+c"(t), "+S"(b), "+D"(c) : "0"(fn), "1"(a) : "edx", "memory", "cc"); break;
    case 1: asm volatile("pushl %%ebx\n\t leal 4(%%esp), %%ebx\n\t call *%%eax\n\t subl %%esp, %%ebx\n\t addl %%ebx, %%esp"
                         : "=a"(r), "=b"(diff), "+c"(t), "+S"(b), "+D"(c) : "0"(fn), "1"(a) : "edx", "memory", "cc"); break;
    case 2: asm volatile("pushl %%esi\n\t pushl %%ebx\n\t leal 8(%%esp), %%ebx\n\t call *%%eax\n\t subl %%esp, %%ebx\n\t addl %%ebx, %%esp"
                         : "=a"(r), "=b"(diff), "+c"(t), "+S"(b), "+D"(c) : "0"(fn), "1"(a) : "edx", "memory", "cc"); break;
    default: asm volatile("pushl %%edi\n\t pushl %%esi\n\t pushl %%ebx\n\t leal 12(%%esp), %%ebx\n\t call *%%eax\n\t subl %%esp, %%ebx\n\t addl %%ebx, %%esp"
                         : "=a"(r), "=b"(diff), "+c"(t), "+S"(b), "+D"(c) : "0"(fn), "1"(a) : "edx", "memory", "cc"); break;
  }
  if (diff) InterlockedIncrement((volatile LONG*) &g_unbalanced);
  return r;
}
// The engine's multi-allocator Free (0x401E90): the first of heaps 0..3 that owns the pointer frees it.
static int multiFree(void* p) {
  for (int i = 0; i < 4; i++) {
    void* h = *(void**) (at(kMulti) + 4 + 4 * i);
    if (callN(h, 0x48, 1, (uint32_t) (uintptr_t) p) & 0xFF) { callN(h, 0x0C, 1, (uint32_t) (uintptr_t) p); return i; }
  }
  return -1;
}
static bool released(void* p) { MEMORY_BASIC_INFORMATION mi; return VirtualQuery(p, &mi, sizeof mi) && mi.State == MEM_FREE; }

static void* g_obj; static volatile LONG g_threadErrors;
static DWORD WINAPI churn(void* arg) {
  const int t = (int) (intptr_t) arg; std::vector<uint32_t> live;
  for (int i = 0; i < 400; i++) {
    const uint32_t size = (i + t) % 5 == 0 ? (9u << 20) + (i << 12) : (64u << 10) + (i << 10);
    uint32_t p = callN(g_obj, 0x08, 3, size, 0x1000, 3);
    if (!p) { InterlockedIncrement(&g_threadErrors); continue; }
    live.push_back(p);
    if (live.size() > 3) { uint32_t q = live.front(); live.erase(live.begin()); if ((i & 1) ? callN(g_obj, 0x0C, 1, q), 0 : multiFree((void*) q) < 0) InterlockedIncrement(&g_threadErrors); }
  }
  for (uint32_t q : live) callN(g_obj, 0x0C, 1, q);
  return 0;
}

int main(int argc, char** argv) {
  o = fopen(argc > 2 ? argv[2] : "results.txt", "w"); setvbuf(o, nullptr, _IONBF, 0);
  InitializeCriticalSection(&g_hl);
  R = (uintptr_t) VirtualAlloc(nullptr, 0x1800000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);   // 0x400000-0x1C00000
  g_arena = (uintptr_t) VirtualAlloc(nullptr, kArena, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  fprintf(o, "stand-in image at %p (0x400000-0x1C00000 layout), heap arena %p\n", (void*) R, (void*) g_arena);
  if (!R || !g_arena) return 1;
  memset((void*) R, 0xCC, 0x1800000);
  void* fns[] = { (void*) h401730, (void*) h401710, (void*) h402280, (void*) h402310, (void*) h401670, (void*) h401670, (void*) h402440,
    (void*) h4023A0, (void*) h4023D0, (void*) h4023E0, (void*) h401690, (void*) h4016A0, (void*) h4016B0, (void*) h4016C0, (void*) h4016A0,
    (void*) h4016D0, (void*) h4016D0, (void*) h4016A0, (void*) h402420, nullptr, (void*) h4021B0, (void*) h4021C0, (void*) h4021D0,
    (void*) h4016D0, (void*) h402690, (void*) h402710, (void*) h4024B0, (void*) h402570, (void*) h402600 };
  for (int i = 0; i < 29; i++) if (fns[i]) trampoline(kSlots[i], fns[i]);
  { uint8_t* p = (uint8_t*) at(0x4016F0); p[0] = 0x8B; p[1] = 0x01; p[2] = 0xFF; p[3] = 0x60; p[4] = 0x18; }   // the game's own +0x4C: mov eax,[ecx]; jmp [eax+0x18]
  uint32_t* hv = (uint32_t*) at(kHeapVt); for (int i = 0; i < 29; i++) hv[i] = (uint32_t) at(kSlots[i]);
  uint32_t* heap = (uint32_t*) at(kHeap); heap[0] = (uint32_t) at(kHeapVt); heap[1] = (uint32_t) g_arena; heap[3] = kArena; heap[4] = 0x4000;
  static void* genVt[29]; for (auto& v : genVt) v = (void*) h4016D0; genVt[0x48 / 4] = (void*) g402420;
  uint32_t* gen = (uint32_t*) at(kGeneral); gen[0] = (uint32_t) (uintptr_t) genVt;
  uint32_t* multi = (uint32_t*) at(kMulti); multi[0] = (uint32_t) at(kMultiVt);
  multi[1] = (uint32_t) at(kGeneral); multi[2] = multi[3] = multi[4] = (uint32_t) at(kHeap); multi[9] = 4;

  HMODULE v = LoadLibraryA(argc > 1 ? argv[1] : "VCRender.asi");
  auto install = (int (__cdecl*)(uintptr_t, uint32_t, uint32_t)) GetProcAddress(v, "VCR_TestPhysPool");
  auto summary = (void (__cdecl*)(char*, size_t)) GetProcAddress(v, "VCR_TestPhysPoolSummary");
  fprintf(o, "plugin %s\n", install && summary ? "loaded" : "MISSING");
  if (!install || !summary) return 1;

  // An allocation made before install, freed afterwards through heaps[3], must reach the engine heap.
  void* early = (void*) (uintptr_t) callN((void*) at(kHeap), 0x08, 3, 1u << 20, 0x1000, 3);
  check(inArena(early), "before install: heap 3 is the engine heap", inArena(early), 1);

  // refused when the layout differs; nothing changes
  hv[7] ^= 1; int r = install(R, 8, 64); hv[7] ^= 1;
  check(r < 0 && heaps3() == (void*) at(kHeap), "install refused when one heap vtable entry differs; heaps[3] unchanged", r, -4);
  multi[9] = 5; r = install(R, 8, 64); multi[9] = 4;
  check(r < 0 && heaps3() == (void*) at(kHeap), "install refused when the heap count differs", r, -2);

  r = install(R, 8, 64);
  g_obj = heaps3();
  check(r == 1 && g_obj != (void*) at(kHeap), "install: heaps[3] is the pool's allocator", r, 1);
  check(multi[1] == at(kGeneral) && multi[2] == at(kHeap) && multi[3] == at(kHeap), "heaps 0, 1 and 2 untouched", 0, 0);

  // small physical request -> engine heap
  long a0 = g_heapAllocs;
  void* small = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 2u << 20, 0x1000, 3);
  check(inArena(small) && g_heapAllocs == a0 + 1, "2 MiB physical request served by the engine heap", inArena(small), 1);
  check(callN(g_obj, 0x48, 1, (uint32_t) (uintptr_t) small) & 0xFF, "engine-heap pointer valid through the pool object (forwarded)", 0, 0);
  // large -> pool, engine heap not asked
  a0 = g_heapAllocs; long c0 = hits(0x402280);
  void* big = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 33030144u, 0x1000, 3);   // northbuild.wtd's 31.5 MiB
  check(big && !inArena(big) && hits(0x402280) == c0, "31.5 MiB physical request served by the pool, engine heap not asked", big != nullptr, 1);
  check(((uintptr_t) big & 0xFFF) == 0, "pool block aligned for the requested 0x1000", (uintptr_t) big & 0xFFF, 0);
  memset(big, 0x5A, 33030144u);   // the whole block is usable
  check(callN(g_obj, 0x48, 1, (uint32_t) (uintptr_t) big + 1000) & 0xFF, "pool pointer (inside the block) valid through the pool object", 0, 0);
  check(!(callN((void*) at(kHeap), 0x48, 1, (uint32_t) (uintptr_t) big) & 0xFF), "pool pointer not claimed by the engine heap", 0, 0);
  check(callN(g_obj, 0x18, 1, (uint32_t) (uintptr_t) big) == 33030144u, "GetSize of a pool block", callN(g_obj, 0x18, 1, (uint32_t) (uintptr_t) big), 33030144);
  check(callN(g_obj, 0x4C, 1, (uint32_t) (uintptr_t) big) == 33030144u, "entry +0x4C (GetSize via own vtable) of a pool block", 0, 0);
  long m0 = hits(0x402690), u0 = hits(0x402710);
  callN(g_obj, 0x60, 1, (uint32_t) (uintptr_t) big); callN(g_obj, 0x64, 1, (uint32_t) (uintptr_t) big);
  check(hits(0x402690) == m0 && hits(0x402710) == u0, "entries +0x60/+0x64 on a pool pointer do not reach the engine heap", 0, 0);
  callN(g_obj, 0x60, 1, (uint32_t) (uintptr_t) small); callN(g_obj, 0x64, 1, (uint32_t) (uintptr_t) small);
  check(hits(0x402690) == m0 + 1 && hits(0x402710) == u0 + 1, "entries +0x60/+0x64 on an engine-heap pointer are forwarded", 0, 0);
  check(callN(g_obj, 0x10, 1, 3) == (uint32_t) (uintptr_t) g_obj && callN(g_obj, 0x14, 1, 1) == (uint32_t) (uintptr_t) g_obj, "GetAllocator on the pool object returns itself", 0, 0);
  check(callN(g_obj, 0x24, 0) >= (16u << 20), "largest block reports the pool's room (engine heap stand-in says 4 MiB)", callN(g_obj, 0x24, 0) >> 20, 32);
  // frees: through the engine's ownership loop and through GetAllocator(3)->Free
  check(multiFree(big) == 3 && released(big), "multi-allocator Free: heap 3 owns the pool block and it is released", 0, 0);
  long f0 = g_heapFrees;
  check(multiFree(small) == 1 && g_heapFrees == f0 + 1, "multi-allocator Free: engine-heap block still freed by heap 1", 0, 0);
  f0 = g_heapFrees; callN(g_obj, 0x0C, 1, (uint32_t) (uintptr_t) early);
  check(g_heapFrees == f0 + 1, "block allocated before install, freed through heaps[3], reaches the engine heap", g_heapFrees - f0, 1);
  // the engine heap cannot place a small request -> pool fallback
  g_heapFull = true; c0 = hits(0x402280);
  void* fb = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 3u << 20, 0x1000, 3);
  check(fb && !inArena(fb) && hits(0x402280) == c0 + 1, "3 MiB request the engine heap refuses: engine heap asked, then the pool serves it", fb != nullptr, 1);
  callN(g_obj, 0x0C, 1, (uint32_t) (uintptr_t) fb);
  check(released(fb), "fallback block released through Free", 0, 0);
  // the pool's cap: 64 MiB in this test; a third 31.5 MiB block does not fit
  void* b1 = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 33030144u, 0x1000, 3), *b2 = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 26214400u, 0x1000, 3);
  c0 = hits(0x402280);
  void* b3 = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 33030144u, 0x1000, 3);
  check(b1 && b2 && !b3 && hits(0x402280) == c0 + 1, "over the cap: the engine heap is asked and its answer (none) returned", b3 != nullptr, 0);
  g_heapFull = false;
  void* b4 = (void*) (uintptr_t) callN(g_obj, 0x08, 3, 33030144u, 0x1000, 3);
  check(b4 && inArena(b4), "over the cap with room in the engine heap: the engine heap serves it", inArena(b4), 1);
  callN(g_obj, 0x0C, 1, (uint32_t) (uintptr_t) b1); callN(g_obj, 0x0C, 1, (uint32_t) (uintptr_t) b2); callN(g_obj, 0x0C, 1, (uint32_t) (uintptr_t) b4);
  check(released(b1) && released(b2), "pool blocks released", 0, 0);
  callN(g_obj, 0x0C, 1, 0);
  check(true, "Free(NULL) returns", 0, 0);
  // alignment above 64 KiB is never pooled
  g_heapFull = true;
  check(callN(g_obj, 0x08, 3, 33030144u, 0x20000, 3) == 0, "alignment above 64 KiB: not pooled (engine heap's answer)", 0, 0);
  g_heapFull = false;

  // every other entry forwards with the engine heap as `this`, same arguments, same stack cleanup
  long bad0 = g_badThis; int fwd = 0, fwdOk = 0;
  struct F { int off, n; uint32_t a, b, want; uintptr_t fn; } fw[] = {
    {0x04, 1, 9, 0, 0, 0x401710}, {0x1C, 1, 5, 0, 0x1005, 0x4023A0}, {0x20, 0, 0, 0, 7u << 20, 0x4023D0}, {0x28, 0, 0, 0, 1, 0x401690},
    {0x2C, 0, 0, 0, 0xA0, 0x4016A0}, {0x30, 2, 1, 2, 0, 0x4016B0}, {0x34, 2, 6, 3, 5, 0x4016C0}, {0x38, 0, 0, 0, 0xA0, 0x4016A0},
    {0x3C, 0, 0, 0, 0, 0x4016D0}, {0x40, 0, 0, 0, 0, 0x4016D0}, {0x44, 0, 0, 0, 0xA0, 0x4016A0}, {0x50, 0, 0, 0, kArena, 0x4021B0},
    {0x54, 0, 0, 0, 0, 0x4021C0}, {0x58, 1, 7, 0, 0, 0x4021D0}, {0x5C, 0, 0, 0, 0, 0x4016D0}, {0x68, 1, 10, 0, 11, 0x4024B0},
    {0x6C, 1, 10, 0, 12, 0x402570}, {0x70, 0, 0, 0, 0x600, 0x402600} };
  for (auto& f : fw) {
    long h0 = hits(f.fn); int ub = g_unbalanced;
    uint32_t got = callN(g_obj, f.off, f.n, f.a, f.b);
    fwd++;
    bool ok = hits(f.fn) == h0 + 1 && g_unbalanced == ub && (f.off == 0x54 ? got == (uint32_t) g_arena : f.off == 0x58 || f.off == 0x30 || f.off == 0x3C || f.off == 0x40 || f.off == 0x5C ? true : got == f.want);
    if (ok) fwdOk++; else fprintf(o, "  forward +0x%02X: got 0x%X, hits %ld\n", f.off, got, hits(f.fn) - h0);
  }
  check(fwdOk == fwd, "18 forwarded entries reach the engine heap's code with the same arguments and return value", fwdOk, fwd);
  check(g_badThis == bad0, "every forwarded call ran with the engine heap as `this`", g_badThis - bad0, 0);
  check(g_unbalanced == 0, "stack balanced after every call through the pool object", g_unbalanced, 0);
  check(callN(g_obj, 0x00, 1, 1) == (uint32_t) (uintptr_t) g_obj && hits(0x401730) == 0, "destructor entry is inert (the engine heap is not destroyed)", 0, 0);

  // 4 threads, 400 allocations each (one in five 9+ MiB), frees through both paths
  HANDLE th[4]; for (int i = 0; i < 4; i++) th[i] = CreateThread(nullptr, 0, churn, (void*) (intptr_t) i, 0, nullptr);
  WaitForMultipleObjects(4, th, TRUE, 60000);
  char s[300]; summary(s, sizeof s);
  fprintf(o, "summary: %s\n", s);
  check(g_threadErrors == 0, "4 threads x 400 mixed allocations and frees: no failure", g_threadErrors, 0);
  check(strstr(s, "pool 0 blocks, 0 MiB") != nullptr, "pool empty after everything was freed", 0, 0);
  check(g_foreignFree == 0, "the engine heap never received a pool pointer", g_foreignFree, 0);
  fprintf(o, "%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
  fclose(o);
  return fails ? 1 : 0;
}
