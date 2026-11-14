// Phase 2 texture-budget redirect, tested against a stand-in of GTA IV's 0x427E40 in a fresh allocation laid out
// like the game's 0x427000-0x42A000. usage: budget_test.exe VCRender.asi results.txt
//
// Stand-in routine (game 0x427E40): same first 12 bytes as the game; stores xmm2 at image+0xF00 and returns
// ecx + 1,000,000 for argument 0, ecx + 7 otherwise. Each of the 13 call sites holds "call routine; ret" so the test
// can enter a site with its own return address and argument on the stack, as the engine's callers do.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uintptr_t R;   // stand-in image; game address A maps to R + (A - 0x427000)
static uintptr_t at(uintptr_t gameAddr) { return R + (gameAddr - 0x427000); }
static const uintptr_t kAvail = 0x427E40;
static const uintptr_t kSites[13] = {0x4276C5, 0x427762, 0x428B63, 0x429153, 0x4291AF, 0x4294B1, 0x429536,
                                     0x429750, 0x429797, 0x42984E, 0x42989A, 0x429D6A, 0x429DB9};
static const uint8_t kStandIn[] = {
  0x83, 0xEC, 0x60,                          // sub esp, 0x60
  0x83, 0x7C, 0x24, 0x64, 0x00,              // cmp dword [esp+0x64], 0
  0x53, 0x55, 0x56, 0x57,                    // push ebx, ebp, esi, edi
  0xF3, 0x0F, 0x11, 0x15, 0x00, 0x7F, 0x42, 0x00,   // movss [0x427F00], xmm2
  0x8B, 0x44, 0x24, 0x74,                    // mov eax, [esp+0x74]   (the argument)
  0x85, 0xC0,                                // test eax, eax
  0x75, 0x08,                                // jnz +8
  0x8D, 0x81, 0x40, 0x42, 0x0F, 0x00,        // lea eax, [ecx+1000000]
  0xEB, 0x03,                                // jmp +3
  0x8D, 0x41, 0x07,                          // lea eax, [ecx+7]
  0x31, 0xD2,                                // xor edx, edx
  0x5F, 0x5E, 0x5D, 0x5B,                    // pop edi, esi, ebp, ebx
  0x83, 0xC4, 0x60,                          // add esp, 0x60
  0xC2, 0x04, 0x00,                          // ret 4
};

static uint32_t g_espBefore, g_espAfter;
// Enter a call site like an engine caller: ecx = this, xmm2 = multiplier, argument pushed, return address below it.
static __attribute__((noinline)) int64_t callSite(uintptr_t site, uint32_t self, uint32_t arg, float mult) {
  uint32_t lo, hi;
  asm volatile(
    "movl %%esp, %[eb]\n\t"
    "movss %[m], %%xmm2\n\t"
    "pushl $1f\n\t"
    "pushl %[arg]\n\t"
    "jmpl *%[site]\n\t"
    "1:\n\t"
    "movl %%esp, %[ea]\n\t"
    : "=&a"(lo), "=&d"(hi), "+c"(self), [eb]"=m"(g_espBefore), [ea]"=m"(g_espAfter)
    : [m]"m"(mult), [arg]"r"(arg), [site]"r"(site)
    : "xmm0", "xmm1", "xmm2", "memory", "cc");
  return (int64_t) (((uint64_t) hi << 32) | lo);
}

static FILE* o; static int fails;
static void check(bool ok, const char* what, long long got, long long want) {
  fprintf(o, "%s %s (got %lld, want %lld)\n", ok ? "PASS" : "FAIL", what, got, want);
  if (!ok) fails++;
}
static float seenXmm2() { return *(volatile float*) at(0x427F00); }

static void buildImage() {
  uint8_t* p = (uint8_t*) at(kAvail);
  memset((void*) R, 0xCC, 0x3000);
  memcpy(p, kStandIn, sizeof kStandIn);
  uint32_t slot = (uint32_t) at(0x427F00); memcpy(p + 16, &slot, 4);   // movss [image+0xF00], xmm2
  for (uintptr_t g : kSites) {
    uintptr_t s = at(g);
    uint8_t* q = (uint8_t*) s; q[0] = 0xE8; int32_t rel = (int32_t) (at(kAvail) - (s + 5)); memcpy(q + 1, &rel, 4); q[5] = 0xC3;
  }
}
static void sweep(const char* phase, float mult, uint32_t arg, int64_t addWant) {
  char what[160];
  for (uintptr_t s : kSites) {
    *(volatile float*) at(0x427F00) = -123.0f;
    int64_t got = callSite(at(s), 0x1000, arg, mult);
    int64_t want = 0x1000 + (arg ? 7 : 1000000) + addWant;
    snprintf(what, sizeof what, "%s site 0x%X arg %u mult %.2f result", phase, (unsigned) s, arg, mult);
    check(got == want, what, got, want);
    snprintf(what, sizeof what, "%s site 0x%X multiplier reaches the engine routine unchanged", phase, (unsigned) s);
    check(seenXmm2() == mult, what, (long long) (seenXmm2() * 1000), (long long) (mult * 1000));
    snprintf(what, sizeof what, "%s site 0x%X stack balanced", phase, (unsigned) s);
    check(g_espBefore == g_espAfter, what, g_espAfter, g_espBefore);
  }
}

int main(int argc, char** argv) {
  o = fopen(argc > 2 ? argv[2] : "results.txt", "w");
  // 64-byte alignment keeps every call displacement inside one cache line, as at the game's addresses
  void* img = VirtualAlloc(nullptr, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  R = (uintptr_t) img;
  fprintf(o, "stand-in image: %s at %p\n", img ? "fresh allocation" : "FAILED", img);
  if (!img || (R & 0xFFF)) return 1;
  buildImage();
  HMODULE v = LoadLibraryA(argc > 1 ? argv[1] : "VCRender.asi");
  auto installAt = (int (__cdecl*)(int, uintptr_t)) GetProcAddress(v, "VCR_TestBudgetInstall");
  auto install = [&](int mib) { return installAt ? installAt(mib, R) : -99; };
  auto open = (void (__cdecl*)(int)) GetProcAddress(v, "VCR_TestBudgetOpen");
  fprintf(o, "plugin %s\n", installAt && open ? "loaded" : "MISSING");
  if (!installAt || !open) return 1;

  sweep("before install", 0.9f, 0, 0);

  // A changed call site (another mod) must refuse the whole install and leave every site untouched.
  uint8_t saved[5]; memcpy(saved, (void*) at(kSites[7]), 5);
  int32_t other = (int32_t) (at(0x428000) - (at(kSites[7]) + 5)); memcpy((void*) (at(kSites[7]) + 1), &other, 4);
  int r = install(512);
  check(r < 0, "install refused when one call site was changed", r, -2);
  memcpy((void*) at(kSites[7]), saved, 5);
  sweep("after refused install", 0.9f, 0, 0);

  // Different first bytes at 0x427E40 (code not decrypted yet): wait, change nothing.
  uint8_t b0 = *(uint8_t*) at(kAvail); *(uint8_t*) at(kAvail) = 0x90;
  r = install(512);
  check(r == 0, "install waits when 0x427E40 does not match", r, 0);
  *(uint8_t*) at(kAvail) = b0;
  sweep("after waiting install", 0.9f, 0, 0);

  r = install(512);
  check(r == 1, "install with ExtraTextureMiB=512", r, 1);
  int redirected = 0;
  for (uintptr_t g : kSites) { uintptr_t s = at(g); int32_t rel; memcpy(&rel, (void*) (s + 1), 4); if (s + 5 + rel != at(kAvail)) redirected++; }
  check(redirected == 13, "call sites redirected", redirected, 13);
  const float e = 512.0f * 1048576.0f;
  sweep("installed", 0.9f, 0, (int64_t) (0.9f * e));
  sweep("installed", 1.0f, 0, (int64_t) (1.0f * e));
  sweep("installed (low-RAM path untouched)", 0.9f, 1, 0);
  sweep("installed (negative multiplier adds nothing)", -1.0f, 0, 0);

  open(0);
  sweep("extra off (address space low)", 0.9f, 0, 0);
  open(1);
  sweep("extra back on", 0.9f, 0, (int64_t) (0.9f * e));

  r = install(512);
  check(r == 1, "second install is harmless", r, 1);
  sweep("after second install", 1.0f, 0, (int64_t) e);

  // Result near the engine's own 0x7FFFFFFF clamp: the 64-bit sum carries into edx correctly.
  int64_t got = callSite(at(kSites[0]), 0x7FFFFFFF - 1000000 - 5, 0, 1.0f);
  check(got == (int64_t) 0x7FFFFFFF - 5 + (int64_t) e, "64-bit carry", got, (int64_t) 0x7FFFFFFF - 5 + (int64_t) e);
  got = callSite(at(kSites[0]), 0xFFFFFFFF - 1000000, 0, 1.0f);
  check(got == (int64_t) 0xFFFFFFFFll + (int64_t) e, "carry out of the low dword", got, (int64_t) 0xFFFFFFFFll + (int64_t) e);

  fprintf(o, "%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
  fclose(o);
  return fails ? 1 : 0;
}
