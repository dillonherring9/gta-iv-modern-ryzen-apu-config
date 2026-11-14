// Streaming diagnostics (vcr_streamdiag.cpp) against stand-ins of GTA IV's loader 0xD5AD70, resource allocator 0x6734A0
// and raw allocator 0x673110, mapped with the game's layout in a fresh allocation (game address A at R + A - 0x670000).
// Each call site holds "call target" followed by the stack cleanup its real caller does, so the test can enter a site the
// way the engine does. usage: streamdiag_test.exe VCRender.asi results.txt
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uintptr_t R;
static uintptr_t at(uintptr_t a) { return R + (a - 0x670000); }
static FILE* o; static int fails;
static void check(bool ok, const char* what, long long got, long long want) {
  fprintf(o, "%s %s (got %lld, want %lld)\n", ok ? "PASS" : "FAIL", what, got, want); if (!ok) fails++;
}

// ---- stand-in behaviour
static int rscFailsLeft, rawFailsLeft, rscCalls, rawCalls;
static bool __cdecl standRsc(void* a, void* b, uint32_t c) { rscCalls++; if (a != (void*) 0x11 || b != (void*) 0x22 || c != 0x33) return false; return rscFailsLeft-- <= 0; }
static void* __cdecl standRaw(uint32_t a, uint32_t b) { rawCalls++; if (a != 0x44 || b != 0) return nullptr; return rawFailsLeft-- <= 0 ? (void*) 0x1234 : nullptr; }

// Enter a cdecl call site: push the return label and the arguments, jump to the site (call + caller's cleanup + ret).
static uint32_t g_espB, g_espA, g_regsA[4], g_lespB, g_lespA; static int g_innerUnbalanced;
static uint32_t siteRsc(uintptr_t site) {
  uint32_t r;
  asm volatile(
    "movl %%esp, %[eb]\n\t"
    "pushl $1f\n\t pushl $0x33\n\t pushl $0x22\n\t pushl $0x11\n\t jmpl *%[s]\n\t"
    "1:\n\t movl %%esp, %[ea]\n\t"
    : "=&a"(r), [eb]"=m"(g_espB), [ea]"=m"(g_espA) : [s]"r"(site) : "ecx", "edx", "memory", "cc");
  if (g_espA != g_espB) g_innerUnbalanced++;
  return r & 0xFF;
}
static uint32_t siteRaw(uintptr_t site) {
  uint32_t r;
  asm volatile(
    "movl %%esp, %[eb]\n\t"
    "pushl $1f\n\t pushl $0\n\t pushl $0x44\n\t jmpl *%[s]\n\t"
    "1:\n\t movl %%esp, %[ea]\n\t"
    : "=&a"(r), [eb]"=m"(g_espB), [ea]"=m"(g_espA) : [s]"r"(site) : "ecx", "edx", "memory", "cc");
  if (g_espA != g_espB) g_innerUnbalanced++;
  return r;
}
// The loader stand-in, like 0xD5AD70: first try at the first-try site, then evict-and-retry at the retry site.
static int g_maxRetries = 3; static bool g_useRaw = false;
static bool __attribute__((thiscall)) standLoad(void* self, void* req, void* entry) {
  if (self != (void*) 0x5E1F || !req || !entry) return false;
  if (g_useRaw) {
    if (siteRaw(at(0xD5AE8E))) return true;
    for (int i = 0; i < g_maxRetries; i++) if (siteRaw(at(0xD5AED7))) return true;
    return false;
  }
  if (siteRsc(at(0xD5ADA3)) & 0xFF) return true;
  for (int i = 0; i < g_maxRetries; i++) if (siteRsc(at(0xD5ADD8)) & 0xFF) return true;
  return false;
}
// Enter the loader call site 0xD5B500 like 0xD5B480 does: ecx = this, two arguments, callee pops them (ret 8).
// ebx/esi/edi carry known values across the call and must come back unchanged (results go to globals: no frame use).
static uint32_t siteLoad(void* req, void* entry) {
  uint32_t r; uintptr_t site = at(0xD5B500);
  asm volatile(
    "pushl %%ebx\n\t pushl %%esi\n\t pushl %%edi\n\t"
    "movl %%esp, %[eb]\n\t"
    "pushl $1f\n\t pushl %%ecx\n\t pushl %%edx\n\t"
    "movl $0x5E1F, %%ecx\n\t movl $0x0B0B0B0B, %%ebx\n\t movl $0x05050505, %%esi\n\t movl $0x0D0D0D0D, %%edi\n\t"
    "jmpl *%%eax\n\t"
    "1:\n\t movl %%esp, %[ea]\n\t"
    "movl %%ebx, %[rb]\n\t movl %%esi, %[rs]\n\t movl %%edi, %[rd]\n\t"
    "popl %%edi\n\t popl %%esi\n\t popl %%ebx\n\t"
    : "=a"(r), [eb]"=m"(g_lespB), [ea]"=m"(g_lespA), [rb]"=m"(g_regsA[0]), [rs]"=m"(g_regsA[1]), [rd]"=m"(g_regsA[2])
    : "a"(site), "d"(req), "c"(entry) : "memory", "cc");
  return r & 0xFF;
}

static void trampoline(uintptr_t a, void* fn) {   // jmp dword ptr [a + 6]
  uint8_t* p = (uint8_t*) at(a); p[0] = 0xFF; p[1] = 0x25; uint32_t slot = (uint32_t) at(a) + 6; memcpy(p + 2, &slot, 4); memcpy(p + 6, &fn, 4);
}
static void callSite(uintptr_t a, uintptr_t target, const uint8_t* tail, size_t n) {
  uint8_t* p = (uint8_t*) at(a); p[0] = 0xE8; int32_t rel = (int32_t) (at(target) - (at(a) + 5)); memcpy(p + 1, &rel, 4); memcpy(p + 5, tail, n);
}

int main(int argc, char** argv) {
  o = fopen(argc > 2 ? argv[2] : "results.txt", "w"); setvbuf(o, nullptr, _IONBF, 0);
  R = (uintptr_t) VirtualAlloc(nullptr, 0x700000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
  fprintf(o, "stand-in image at %p (0x670000-0xD70000 layout)\n", (void*) R);
  if (!R) return 1;
  memset((void*) R, 0xCC, 0x700000);
  trampoline(0x6734A0, (void*) &standRsc); trampoline(0x673110, (void*) &standRaw); trampoline(0xD5AD70, (void*) &standLoad);
  const uint8_t pop12[] = {0x83, 0xC4, 0x0C, 0xC3}, pop8[] = {0x83, 0xC4, 0x08, 0xC3}, ret[] = {0xC3};
  callSite(0xD5ADA3, 0x6734A0, pop12, 4); callSite(0xD5ADD8, 0x6734A0, pop12, 4);
  callSite(0xD5AE8E, 0x673110, pop8, 4); callSite(0xD5AED7, 0x673110, pop8, 4);
  callSite(0xD5B500, 0xD5AD70, ret, 1);
  uint32_t req[8] = {0, 0, 0x1234, 0}, entry[4] = {0x00150123u | (2u << 11), 0, 0, 0}; ((uint16_t*) entry)[7] = 1u << 13;   // index 0x1234, resource flag

  HMODULE v = LoadLibraryA(argc > 1 ? argv[1] : "VCRender.asi");
  auto install = (int (__cdecl*)(uintptr_t)) GetProcAddress(v, "VCR_TestStreamDiag");
  auto summary = (void (__cdecl*)(char*, size_t)) GetProcAddress(v, "VCR_TestStreamDiagSummary");
  fprintf(o, "plugin %s\n", install && summary ? "loaded" : "MISSING");
  if (!install || !summary) return 1;

  // unpatched baseline through the real layout
  rscFailsLeft = 0; check(siteLoad(req, entry) == 1, "before install: load succeeds through the stand-ins", 0, 1);
  check(g_lespA == g_lespB, "before install: stack balanced", g_lespA, g_lespB);

  // a changed site refuses the whole install and patches nothing
  uint8_t keep[5]; memcpy(keep, (void*) at(0xD5AED7), 5); ((uint8_t*) at(0xD5AED7))[0] = 0x90;
  int r = install(R);
  check(r < 0, "install refused when one call site differs", r, -1);
  int32_t rel; memcpy(&rel, (void*) (at(0xD5B500) + 1), 4);
  check(at(0xD5B500) + 5 + rel == at(0xD5AD70), "refused install left the loader site unpatched", 0, 0);
  memcpy((void*) at(0xD5AED7), keep, 5);

  r = install(R);
  check(r == 1, "install", r, 1);
  int redirected = 0; const uintptr_t sites[5] = {0xD5B500, 0xD5ADA3, 0xD5ADD8, 0xD5AE8E, 0xD5AED7}, targets[5] = {0xD5AD70, 0x6734A0, 0x6734A0, 0x673110, 0x673110};
  for (int i = 0; i < 5; i++) { memcpy(&rel, (void*) (at(sites[i]) + 1), 4); if (at(sites[i]) + 5 + rel != at(targets[i])) redirected++; }
  check(redirected == 5, "all five call sites redirected", redirected, 5);

  char s[256];
  // 1. first try succeeds
  rscFailsLeft = 0; rscCalls = 0;
  uint32_t res = siteLoad(req, entry);
  check(res == 1 && rscCalls == 1, "load succeeds on the first allocation", res, 1);
  check(g_lespA == g_lespB, "stack balanced through the loader wrapper (ret 8)", g_lespA, g_lespB);
  check(g_regsA[0] == 0x0B0B0B0B && g_regsA[1] == 0x05050505 && g_regsA[2] == 0x0D0D0D0D, "ebx/esi/edi preserved", g_regsA[0], 0x0B0B0B0B);
  // 2. first try fails, second retry succeeds
  rscFailsLeft = 2; rscCalls = 0;
  res = siteLoad(req, entry);
  check(res == 1 && rscCalls == 3, "load succeeds after two evictions", rscCalls, 3);
  // 3. allocation never succeeds: the loader gives up
  rscFailsLeft = 1000; rscCalls = 0;
  res = siteLoad(req, entry);
  check(res == 0 && rscCalls == 1 + g_maxRetries, "a load that never gets memory returns false", res, 0);
  check(g_lespA == g_lespB, "stack balanced on the failure path", g_lespA, g_lespB);
  // 4. raw file: first fails, retry succeeds; then a raw give-up
  g_useRaw = true; rawFailsLeft = 1; rawCalls = 0;
  res = siteLoad(req, entry);
  check(res == 1 && rawCalls == 2, "raw-file load succeeds after one eviction", rawCalls, 2);
  rawFailsLeft = 1000;
  res = siteLoad(req, entry);
  check(res == 0, "raw-file load that never gets memory returns false", res, 0);
  g_useRaw = false;
  // counters: loads 5 (+0 before install), failed 2; first-try failures rsc 2 (cases 2,3), raw 2 (cases 4a,4b);
  // eviction retries rsc 2 + 3 = 5, raw 1 + 3 = 4
  summary(s, sizeof s);
  fprintf(o, "summary:%s\n", s);
  check(strstr(s, "loads 5, failed 2") != nullptr, "loads and failures counted", 0, 0);
  check(strstr(s, "first-try failures 2 (raw 2), eviction retries 5 (raw 4)") != nullptr, "first-try failures and eviction retries counted", 0, 0);
  check(g_innerUnbalanced == 0, "every allocator call site left the stack balanced (cdecl)", g_innerUnbalanced, 0);
  fprintf(o, "%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
  fclose(o);
  return fails ? 1 : 0;
}
