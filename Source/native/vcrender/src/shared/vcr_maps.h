// Address-space census from Linux's view of this process (/proc/self/maps through Wine's Z: drive).
// Wine's VirtualQuery does not see mappings made by Linux-side libraries (graphics driver, LLVM), so this is
// the measurement that matters for a 32-bit game under Proton.
#pragma once
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct VcrMaps { bool ok; uint64_t mapped, unmapped, largestGap, graphics, gpuBuffers, llvm; uint32_t regions; };

// Wine opens /proc files through wineserver, so /proc/self is the server: find this process by its command line.
static inline bool vcrReadFile(const char* path, char*& buf, size_t& len) {
  HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  size_t cap = 1 << 16; len = 0; buf = (char*) malloc(cap + 1);
  for (;;) { DWORD n = 0; if (len + 65536 > cap) { cap *= 2; buf = (char*) realloc(buf, cap + 1); }
    if (!ReadFile(f, buf + len, 65536, &n, nullptr) || n == 0) break; len += n; }
  CloseHandle(f); buf[len] = 0; return true;
}
static inline unsigned long vcrUnixPid() {
  static unsigned long cached = 0;
  if (cached) return cached;
  char exe[MAX_PATH]; GetModuleFileNameA(nullptr, exe, MAX_PATH);
  const char* name = strrchr(exe, '\\'); name = name ? name + 1 : exe;
  WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA("Z:\\proc\\*", &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  unsigned long best = 0; int matches = 0;
  do {
    if (fd.cFileName[0] < '0' || fd.cFileName[0] > '9') continue;
    char path[64]; snprintf(path, sizeof path, "Z:\\proc\\%s\\cmdline", fd.cFileName);
    char* b = nullptr; size_t n = 0;
    if (!vcrReadFile(path, b, n)) continue;
    for (size_t i = 0; i < n; i++) if (!b[i]) b[i] = ' ';
    if (n && strstr(b, name) && !strstr(b, "wineserver") && !strstr(b, "umu") && !strstr(b, "proton") && !strstr(b, "pressure-vessel")) {
      unsigned long pid = strtoul(fd.cFileName, nullptr, 10);
      if (pid > best) best = pid;   // the newest matching process
      matches++;
    }
    free(b);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  cached = best;
  return best;
}

static inline bool vcrReadMaps(VcrMaps& m) {
  memset(&m, 0, sizeof m);
  unsigned long pid = vcrUnixPid();
  if (!pid) return false;
  char mp[64]; snprintf(mp, sizeof mp, "Z:\\proc\\%lu\\maps", pid);
  HANDLE f = CreateFileA(mp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  size_t cap = 1 << 20, len = 0; char* buf = (char*) malloc(cap);
  for (;;) { DWORD n = 0; if (len + 65536 > cap) { cap *= 2; buf = (char*) realloc(buf, cap); }
    if (!ReadFile(f, buf + len, 65536, &n, nullptr) || n == 0) break; len += n; }
  CloseHandle(f);
  buf[len < cap ? len : cap - 1] = 0;
  const uint64_t top = 0x100000000ull; uint64_t prevEnd = 0x10000;
  for (char* line = buf; line && *line;) {
    char* next = strchr(line, '\n'); if (next) *next++ = 0;
    char* dash = strchr(line, '-');
    if (dash) {
      uint64_t a = strtoull(line, nullptr, 16), b = strtoull(dash + 1, nullptr, 16);
      if (a < top) {
        if (b > top) b = top;
        if (a > prevEnd) { uint64_t g = a - prevEnd; m.unmapped += g; if (g > m.largestGap) m.largestGap = g; }
        if (b > prevEnd) prevEnd = b;
        uint64_t sz = b - a; m.mapped += sz; m.regions++;
        const char* path = strchr(line, '/');
        if (path) {
          if (strstr(path, "/dev/dri") || strstr(path, "renderD")) { m.gpuBuffers += sz; m.graphics += sz; }
          else if (strstr(path, "libLLVM")) { m.llvm += sz; m.graphics += sz; }
          else if (strstr(path, "vulkan_") || strstr(path, "libvulkan") || strstr(path, "libdrm") || strstr(path, "winevulkan")) m.graphics += sz;
        }
      }
    }
    line = next;
  }
  if (prevEnd < top) { uint64_t g = top - prevEnd; m.unmapped += g; if (g > m.largestGap) m.largestGap = g; }
  free(buf);
  m.ok = m.regions > 0;
  return m.ok;
}
