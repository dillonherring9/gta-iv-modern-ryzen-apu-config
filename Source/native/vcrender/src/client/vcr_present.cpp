// Presenter: draws frames the host copied into shared memory onto the game window with GDI.
// It runs on its own thread and never takes the global device lock, so the game is not stalled by drawing.
#include "vcr_client.h"
#include <algorithm>
#include <stdio.h>

namespace vcr {

struct Target {
  HWND hwnd = nullptr;
  bool fullscreen = false;
  bool hasSrc = false, hasDst = false; RECT src = {}, dst = {};
  bool gamma = false; uint8_t lut[3][256] = {};
  int gen = 0; HANDLE map = nullptr; uint8_t* view = nullptr; uint32_t w = 0, h = 0;
  int32_t shownFrame = -1;
  int lastBlit = -1;
  bool acceptedFrame = false;
  int failedMapGen = -1; DWORD lastMapTry = 0;
  std::vector<uint8_t> scratch;
};
static CRITICAL_SECTION g_pl;
static std::unordered_map<uint32_t, Target>* g_targets;
static HANDLE g_frameEv;
static volatile LONG g_started;

void presenterRegister(uint32_t scId, HWND hwnd) {
  EnterCriticalSection(&g_pl); (*g_targets)[scId].hwnd = hwnd; LeaveCriticalSection(&g_pl);
}
bool presenterUnregister(uint32_t scId) {
  EnterCriticalSection(&g_pl);
  auto it = g_targets->find(scId);
  const bool accepted = it != g_targets->end() && it->second.acceptedFrame;
  if (it != g_targets->end()) { if (it->second.view) UnmapViewOfFile(it->second.view); if (it->second.map) CloseHandle(it->second.map); g_targets->erase(it); }
  LeaveCriticalSection(&g_pl);
  return accepted;
}
void presenterSetWindow(uint32_t scId, HWND hwnd, bool fullscreen, const RECT* src, const RECT* dst) {
  EnterCriticalSection(&g_pl);
  Target& t = (*g_targets)[scId];
  if (hwnd) t.hwnd = hwnd;
  t.fullscreen = fullscreen;
  t.hasSrc = src != nullptr; if (src) t.src = *src;
  t.hasDst = dst != nullptr; if (dst) t.dst = *dst;
  LeaveCriticalSection(&g_pl);
}
void presenterSetGamma(uint32_t scId, const D3DGAMMARAMP* ramp) {
  EnterCriticalSection(&g_pl);
  Target& t = (*g_targets)[scId];
  bool identity = true;
  for (int i = 0; i < 256; i++) {
    t.lut[0][i] = (uint8_t) (ramp->blue[i] >> 8); t.lut[1][i] = (uint8_t) (ramp->green[i] >> 8); t.lut[2][i] = (uint8_t) (ramp->red[i] >> 8);
    for (int c = 0; c < 3; c++) if (t.lut[c][i] != i) identity = false;
  }
  t.gamma = !identity;
  LeaveCriticalSection(&g_pl);
}
bool presenterLastFrame(uint32_t scId, std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h) {
  VcrControl* C = control(); bool ok = false;
  EnterCriticalSection(&g_pl);
  auto it = g_targets->find(scId);
  if (it != g_targets->end() && it->second.view) {
    for (uint32_t i = 0; i < VCR_MAX_SC; i++) if (C->frames[i].scId == (int32_t) scId && C->frames[i].gen == it->second.gen) {
      VcrFrameEntry& fe = C->frames[i];
      int slot = -1;
      for (int tries = 0; tries < 4; tries++) {
        slot = fe.latestSlot;
        if (slot < 0) break;
        InterlockedExchange((volatile LONG*) &fe.readingSlot, slot);
        if (fe.latestSlot == slot) break;
        slot = -1;
      }
      if (slot >= 0) { w = it->second.w; h = it->second.h; bgra.assign(it->second.view + (size_t) w * h * 4 * slot, it->second.view + (size_t) w * h * 4 * (slot + 1)); ok = true; }
      InterlockedExchange((volatile LONG*) &fe.readingSlot, -1);
      break;
    }
  }
  LeaveCriticalSection(&g_pl);
  return ok;
}

enum BlitResult { BLIT_OK, BLIT_NO_WINDOW, BLIT_MINIMIZED, BLIT_EMPTY, BLIT_NO_DC, BLIT_FAILED, BLIT_RESULT_COUNT };
static const char* blitName(BlitResult r) {
  static const char* names[] = {"GDI accepted", "invalid window", "minimized", "empty rectangle", "no DC", "GDI failed"};
  return names[r];
}
static BlitResult blit(Target& t, const uint8_t* pixels, uint32_t w, uint32_t h) {
  if (!t.hwnd || !IsWindow(t.hwnd)) return BLIT_NO_WINDOW;
  if (IsIconic(t.hwnd)) return BLIT_MINIMIZED;
  RECT cr; if (!GetClientRect(t.hwnd, &cr) || cr.right <= 0 || cr.bottom <= 0) return BLIT_EMPTY;
  const uint8_t* src = pixels;
  if (t.gamma) {
    t.scratch.resize((size_t) w * h * 4);
    uint8_t* o = t.scratch.data();
    for (size_t i = 0, n = (size_t) w * h; i < n; i++) { o[i*4] = t.lut[0][pixels[i*4]]; o[i*4+1] = t.lut[1][pixels[i*4+1]]; o[i*4+2] = t.lut[2][pixels[i*4+2]]; o[i*4+3] = 255; }
    src = o;
  }
  BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = (LONG) w; bmi.bmiHeader.biHeight = -(LONG) h;
  bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
  RECT s = {0, 0, (LONG) w, (LONG) h};
  if (t.hasSrc) { s = t.src; if (s.left < 0) s.left = 0; if (s.top < 0) s.top = 0; if (s.right > (LONG) w) s.right = w; if (s.bottom > (LONG) h) s.bottom = h; }
  if (s.right <= s.left || s.bottom <= s.top) return BLIT_EMPTY;
  RECT d = cr;
  if (t.hasDst) d = t.dst;
  else if (t.fullscreen) {   // keep the frame's aspect inside the monitor-sized window
    LONG cw = cr.right, ch = cr.bottom, sw = s.right - s.left, sh = s.bottom - s.top;
    if ((int64_t) cw * sh > (int64_t) ch * sw) { LONG nw = (LONG) ((int64_t) ch * sw / sh); d.left = (cw - nw) / 2; d.right = d.left + nw; }
    else { LONG nh = (LONG) ((int64_t) cw * sh / sw); d.top = (ch - nh) / 2; d.bottom = d.top + nh; }
  }
  if (d.right <= d.left || d.bottom <= d.top) return BLIT_EMPTY;
  HDC dc = GetDC(t.hwnd);
  if (!dc) return BLIT_NO_DC;
  LONG dw = d.right - d.left, dh = d.bottom - d.top, sw = s.right - s.left, sh = s.bottom - s.top;
  int copied = 0;
  if (dw == sw && dh == sh && s.left == 0 && s.top == 0 && sw == (LONG) w && sh == (LONG) h) {
    copied = (int) SetDIBitsToDevice(dc, d.left, d.top, w, h, 0, 0, 0, h, src, &bmi, DIB_RGB_COLORS);
  } else {
    SetStretchBltMode(dc, COLORONCOLOR);
    copied = StretchDIBits(dc, d.left, d.top, dw, dh, s.left, s.top, sw, sh, src, &bmi, DIB_RGB_COLORS, SRCCOPY);
  }
  if (t.fullscreen && !t.hasDst) {   // black bars
    HBRUSH b = (HBRUSH) GetStockObject(BLACK_BRUSH);
    RECT r1 = {0, 0, cr.right, d.top}, r2 = {0, d.bottom, cr.right, cr.bottom}, r3 = {0, d.top, d.left, d.bottom}, r4 = {d.right, d.top, cr.right, d.bottom};
    if (r1.bottom > r1.top) FillRect(dc, &r1, b); if (r2.bottom > r2.top) FillRect(dc, &r2, b);
    if (r3.right > r3.left) FillRect(dc, &r3, b); if (r4.right > r4.left) FillRect(dc, &r4, b);
  }
  ReleaseDC(t.hwnd, dc);
  return copied > 0 ? BLIT_OK : BLIT_FAILED;
}

// Present-to-GDI latency (this does not measure compositor scanout): the game's Present time per frame number (implicit swapchain), read when that frame is shown.
struct PresentStamp { volatile int32_t frame; volatile LONGLONG qpc; };
static PresentStamp g_stamps[256];
void presenterNotePresent(uint32_t frameNo) {
  LARGE_INTEGER t; QueryPerformanceCounter(&t);
  PresentStamp& s = g_stamps[frameNo & 255];
  s.frame = -1; MemoryBarrier(); s.qpc = t.QuadPart; MemoryBarrier(); s.frame = (int32_t) frameNo;
}
static DWORD WINAPI presenterThread(void*) {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  VcrControl* C = control();
  uint32_t presented = 0, skipped[BLIT_RESULT_COUNT] = {}; DWORD lastStat = GetTickCount(); double blitMs = 0;
  LARGE_INTEGER qf; QueryPerformanceFrequency(&qf);
  std::vector<double> ages; ages.reserve(1024);
  for (;;) {
    WaitForSingleObject(g_frameEv, 50);
    EnterCriticalSection(&g_pl);
    for (auto& kv : *g_targets) {
      uint32_t scId = kv.first; Target& t = kv.second;
      int e = -1;
      for (uint32_t i = 0; i < VCR_MAX_SC; i++) if (C->frames[i].scId == (int32_t) scId) { e = (int) i; break; }
      if (e < 0) continue;
      VcrFrameEntry& fe = C->frames[e];
      if (fe.gen != t.gen) {
        if (t.failedMapGen == fe.gen && GetTickCount() - t.lastMapTry < 250) continue;
        t.lastMapTry = GetTickCount();
        if (t.view) { UnmapViewOfFile(t.view); t.view = nullptr; } if (t.map) { CloseHandle(t.map); t.map = nullptr; }
        wchar_t name[200]; swprintf(name, 200, L"%ls_F%u_%d", channelBase(), scId, fe.gen);
        t.map = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        t.view = t.map ? (uint8_t*) MapViewOfFile(t.map, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (!t.view) {
          const DWORD error = GetLastError();
          if (t.map) { CloseHandle(t.map); t.map = nullptr; }
          if (t.failedMapGen != fe.gen) log("presenter: cannot open frame buffer %ls (%lu); retrying", name, error);
          t.failedMapGen = fe.gen; continue;
        }
        t.failedMapGen = -1;
        t.w = fe.width; t.h = fe.height; t.gen = fe.gen; t.shownFrame = -1;
        log("presenter: swapchain %u frame buffer %ux%u (gen %d)", scId, t.w, t.h, t.gen);
      }
      if (!t.view) continue;
      int slot = -1;
      for (int tries = 0; tries < 4; tries++) {
        slot = fe.latestSlot;
        if (slot < 0) break;
        InterlockedExchange((volatile LONG*) &fe.readingSlot, slot);
        if (fe.latestSlot == slot) break;
        slot = -1;
      }
      if (slot >= 0 && fe.latestFrame != t.shownFrame) {
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
        const uint8_t* px = t.view + (size_t) t.w * t.h * 4 * slot;
        int32_t frame = fe.latestFrame;
        BlitResult result = blit(t, px, t.w, t.h);
        t.shownFrame = frame;
        QueryPerformanceCounter(&b);
        if ((int) result != t.lastBlit) {
          RECT cr = {}; GetClientRect(t.hwnd, &cr);
          log("[window] swapchain %u HWND %p: %s; visible %d, iconic %d, foreground %p, client %ldx%ld, style 0x%08lx, exstyle 0x%08lx",
              scId, t.hwnd, blitName(result), IsWindowVisible(t.hwnd), IsIconic(t.hwnd), GetForegroundWindow(),
              cr.right, cr.bottom, (unsigned long) GetWindowLongW(t.hwnd, GWL_STYLE), (unsigned long) GetWindowLongW(t.hwnd, GWL_EXSTYLE));
          t.lastBlit = (int) result;
        }
        if (result == BLIT_OK) {
          t.acceptedFrame = true;
          blitMs += (b.QuadPart - a.QuadPart) * 1000.0 / qf.QuadPart; presented++;
          const PresentStamp& st = g_stamps[frame & 255];
          const LONGLONG q = st.qpc; MemoryBarrier();
          if (st.frame == frame && ages.size() < 4096) ages.push_back((b.QuadPart - q) * 1000.0 / qf.QuadPart);
        } else skipped[result]++;
      }
      InterlockedExchange((volatile LONG*) &fe.readingSlot, -1);
    }
    LeaveCriticalSection(&g_pl);
    DWORD now = GetTickCount();
    if (now - lastStat > 10000) {
      if (presented) {
        double median = 0, p90 = 0;
        if (!ages.empty()) {
          std::sort(ages.begin(), ages.end());
          median = ages[ages.size() / 2]; p90 = ages[(ages.size() * 9) / 10];
        }
        log("[present] %u frames accepted by GDI in %.1f s, %.2f ms per blit; game Present to GDI %.0f ms median, %.0f ms 90th (frames ahead %d)",
            presented, (now - lastStat) / 1000.0, blitMs / presented, median, p90, framesAheadSetting());
      }
      uint32_t missed = 0; for (int i = 1; i < BLIT_RESULT_COUNT; i++) missed += skipped[i];
      if (missed) log("[present] %u frames skipped: invalid window %u, minimized %u, empty rectangle %u, no DC %u, GDI failed %u",
          missed, skipped[BLIT_NO_WINDOW], skipped[BLIT_MINIMIZED], skipped[BLIT_EMPTY], skipped[BLIT_NO_DC], skipped[BLIT_FAILED]);
      presented = 0; memset(skipped, 0, sizeof skipped); blitMs = 0; lastStat = now; ages.clear();
    }
  }
  return 0;
}

void presenterStart(HANDLE frameEv) {
  if (InterlockedExchange(&g_started, 1)) return;
  g_frameEv = frameEv;
  CloseHandle(CreateThread(nullptr, 0, presenterThread, nullptr, 0, nullptr));
}

struct PresenterInit { PresenterInit() { InitializeCriticalSection(&g_pl); g_targets = new std::unordered_map<uint32_t, Target>(); } };
static PresenterInit g_presenterInit;

}  // namespace vcr
