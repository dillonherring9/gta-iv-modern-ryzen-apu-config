// DXGI answered from the host. GTAIV.exe (and FusionFix's fallback) load dxgi.dll to read the adapter's video memory;
// under Proton that DXGI is DXVK's, and creating a factory starts Vulkan inside the game process, which maps the
// 32-bit graphics driver and LLVM (~195 MiB). With VCRender the factory functions in dxgi.dll's export table point
// here instead: adapter description and memory come from the host's 64-bit DXGI, outputs from this process's monitors.
#include "vcr_client.h"
#include <dxgi1_4.h>
#include <stdio.h>

namespace vcr {
bool hostAvailable();

static const GUID kIID_IUnknown = __uuidof(IUnknown);

static RDxgiInfo g_info; static bool g_haveInfo;
static bool fetchInfo(bool refresh) {
  Guard g;
  if (g_haveInfo && !refresh) return true;
  if (!hostAvailable()) return false;
  RDxgiInfo r = {}; uint32_t got = 0;
  if (FAILED(call(OP_DXGI_INFO, nullptr, 0, &r, sizeof r, &got)) || got < sizeof r) return false;
  g_info = r; g_haveInfo = true; return true;
}
static uint64_t u64(uint32_t lo, uint32_t hi) { return ((uint64_t) hi << 32) | lo; }
static SIZE_T clamp32(uint64_t v) { return (SIZE_T) (v > 0xC0000000ull ? 0xC0000000ull : v); }   // as 32-bit DXVK reports

template<class Base> struct DxgiObj : public Base {
  volatile LONG refs = 1;
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return DXGI_ERROR_NOT_FOUND; }
  virtual ~DxgiObj() {}
};

struct VDxgiFactory;
struct VDxgiAdapter;

struct VDxgiOutput final : public DxgiObj<IDXGIOutput> {
  IDXGIAdapter* adapter; HMONITOR mon;
  VDxgiOutput(IDXGIAdapter* a, HMONITOR m) : adapter(a), mon(m) { adapter->AddRef(); }
  ~VDxgiOutput() override { adapter->Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER; *ppv = nullptr;
    if (riid == kIID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutput)) { *ppv = this; AddRef(); return S_OK; }
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return adapter->QueryInterface(riid, pp); }
  HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC* d) override {
    if (!d) return E_INVALIDARG;
    MONITORINFOEXW mi = {}; mi.cbSize = sizeof mi; GetMonitorInfoW(mon, &mi);
    memset(d, 0, sizeof *d);
    wcsncpy(d->DeviceName, mi.szDevice, 31); d->DesktopCoordinates = mi.rcMonitor; d->AttachedToDesktop = TRUE;
    d->Rotation = DXGI_MODE_ROTATION_IDENTITY; d->Monitor = mon; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDisplayModeList(DXGI_FORMAT fmt, UINT, UINT* num, DXGI_MODE_DESC* desc) override {
    if (!num) return DXGI_ERROR_INVALID_CALL;
    bool rgba8 = fmt == DXGI_FORMAT_R8G8B8A8_UNORM || fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || fmt == DXGI_FORMAT_B8G8R8A8_UNORM || fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    std::vector<DXGI_MODE_DESC> modes;
    if (rgba8 && hostAvailable()) {
      PAdapterQ q = {0, 0, (uint32_t) D3DFMT_X8R8G8B8}; uint32_t n = 0;
      moduleQuery(OP_ADAPTER_MODE_COUNT, q, &n, 4);
      for (uint32_t i = 0; i < n && i < 1024; i++) {
        PAdapterQ e = {0, 0, (uint32_t) D3DFMT_X8R8G8B8, i}; RMode m = {};
        if (FAILED(moduleQuery(OP_ENUM_ADAPTER_MODES, e, &m, sizeof m))) continue;
        DXGI_MODE_DESC md = {}; md.Width = m.w; md.Height = m.h; md.RefreshRate = {m.refresh, 1}; md.Format = fmt;
        md.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE; md.Scaling = DXGI_MODE_SCALING_UNSPECIFIED; modes.push_back(md);
      }
    }
    if (!desc) { *num = (UINT) modes.size(); return S_OK; }
    UINT c = *num < modes.size() ? *num : (UINT) modes.size();
    memcpy(desc, modes.data(), c * sizeof(DXGI_MODE_DESC)); *num = c;
    return c < modes.size() ? DXGI_ERROR_MORE_DATA : S_OK;
  }
  HRESULT STDMETHODCALLTYPE FindClosestMatchingMode(const DXGI_MODE_DESC* want, DXGI_MODE_DESC* out, IUnknown*) override {
    if (!want || !out) return DXGI_ERROR_INVALID_CALL;
    UINT n = 0; GetDisplayModeList(want->Format == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R8G8B8A8_UNORM : want->Format, 0, &n, nullptr);
    if (!n) return DXGI_ERROR_NOT_FOUND;
    std::vector<DXGI_MODE_DESC> m(n); GetDisplayModeList(want->Format == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_R8G8B8A8_UNORM : want->Format, 0, &n, m.data());
    const DXGI_MODE_DESC* best = &m[0]; uint64_t bestScore = ~0ull;
    for (auto& x : m) {
      uint64_t s = (uint64_t) ((int64_t) x.Width - want->Width) * ((int64_t) x.Width - want->Width) + (uint64_t) ((int64_t) x.Height - want->Height) * ((int64_t) x.Height - want->Height);
      if (want->RefreshRate.Numerator && want->RefreshRate.Denominator) s += (uint64_t) llabs((int64_t) x.RefreshRate.Numerator - (int64_t) (want->RefreshRate.Numerator / want->RefreshRate.Denominator));
      if (s < bestScore) { bestScore = s; best = &x; }
    }
    *out = *best; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE WaitForVBlank() override { Sleep(1); return S_OK; }
  HRESULT STDMETHODCALLTYPE TakeOwnership(IUnknown*, BOOL) override { return E_NOTIMPL; }
  void STDMETHODCALLTYPE ReleaseOwnership() override {}
  HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE SetGammaControl(const DXGI_GAMMA_CONTROL*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE GetGammaControl(DXGI_GAMMA_CONTROL*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE SetDisplaySurface(IDXGISurface*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData(IDXGISurface*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS*) override { return DXGI_ERROR_UNSUPPORTED; }
};

static BOOL CALLBACK collectMonitor(HMONITOR m, HDC, LPRECT, LPARAM p) { ((std::vector<HMONITOR>*) p)->push_back(m); return TRUE; }

struct VDxgiAdapter final : public DxgiObj<IDXGIAdapter3> {
  IDXGIFactory1* factory;
  explicit VDxgiAdapter(IDXGIFactory1* f) : factory(f) { factory->AddRef(); }
  ~VDxgiAdapter() override { factory->Release(); }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER; *ppv = nullptr;
    if (riid == kIID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) || riid == __uuidof(IDXGIAdapter1) ||
        riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3)) { *ppv = static_cast<IDXGIAdapter3*>(this); AddRef(); return S_OK; }
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return factory->QueryInterface(riid, pp); }
  HRESULT STDMETHODCALLTYPE EnumOutputs(UINT i, IDXGIOutput** out) override {
    if (!out) return E_INVALIDARG; *out = nullptr;
    std::vector<HMONITOR> mons; POINT o = {0, 0}; HMONITOR primary = MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY);
    mons.push_back(primary); EnumDisplayMonitors(nullptr, nullptr, collectMonitor, (LPARAM) &mons);
    std::vector<HMONITOR> uniq; for (HMONITOR m : mons) { bool seen = false; for (HMONITOR u : uniq) if (u == m) seen = true; if (!seen) uniq.push_back(m); }
    if (i >= uniq.size()) return DXGI_ERROR_NOT_FOUND;
    *out = new VDxgiOutput(this, uniq[i]); return S_OK;
  }
  void fill(DXGI_ADAPTER_DESC2& d) {
    memset(&d, 0, sizeof d);
    memcpy(d.Description, g_info.description, sizeof d.Description);
    d.VendorId = g_info.vendorId; d.DeviceId = g_info.deviceId; d.SubSysId = g_info.subSysId; d.Revision = g_info.revision;
    d.DedicatedVideoMemory = clamp32(u64(g_info.dedicatedVideoLo, g_info.dedicatedVideoHi));
    d.DedicatedSystemMemory = clamp32(u64(g_info.dedicatedSystemLo, g_info.dedicatedSystemHi));
    d.SharedSystemMemory = clamp32(u64(g_info.sharedSystemLo, g_info.sharedSystemHi));
    d.AdapterLuid.LowPart = g_info.luidLow; d.AdapterLuid.HighPart = (LONG) g_info.luidHigh; d.Flags = g_info.flags;
    d.GraphicsPreemptionGranularity = DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY; d.ComputePreemptionGranularity = DXGI_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
  }
  HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* d) override {
    if (!d) return E_INVALIDARG; DXGI_ADAPTER_DESC2 f; fill(f);
    memcpy(d->Description, f.Description, sizeof d->Description); d->VendorId = f.VendorId; d->DeviceId = f.DeviceId; d->SubSysId = f.SubSysId;
    d->Revision = f.Revision; d->DedicatedVideoMemory = f.DedicatedVideoMemory; d->DedicatedSystemMemory = f.DedicatedSystemMemory;
    d->SharedSystemMemory = f.SharedSystemMemory; d->AdapterLuid = f.AdapterLuid;
    static int logged; if (logged++ < 4) log("DXGI GetDesc answered by VCRender: dedicated video %lu MiB, shared system %lu MiB", (unsigned long) (d->DedicatedVideoMemory >> 20), (unsigned long) (d->SharedSystemMemory >> 20));
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* d) override {
    if (!d) return E_INVALIDARG; DXGI_ADAPTER_DESC2 f; fill(f); memcpy(d, &f, sizeof *d); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDesc2(DXGI_ADAPTER_DESC2* d) override { if (!d) return E_INVALIDARG; fill(*d); return S_OK; }
  HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID, LARGE_INTEGER*) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE RegisterHardwareContentProtectionTeardownStatusEvent(HANDLE, DWORD*) override { return DXGI_ERROR_UNSUPPORTED; }
  void STDMETHODCALLTYPE UnregisterHardwareContentProtectionTeardownStatus(DWORD) override {}
  HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(UINT node, DXGI_MEMORY_SEGMENT_GROUP g, DXGI_QUERY_VIDEO_MEMORY_INFO* mi) override {
    if (node > 0 || !mi || (g != DXGI_MEMORY_SEGMENT_GROUP_LOCAL && g != DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL)) return E_INVALIDARG;
    fetchInfo(true);
    int s = g == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? 0 : 1;
    mi->Budget = u64(g_info.mem[s][0][0], g_info.mem[s][0][1]); mi->CurrentUsage = u64(g_info.mem[s][1][0], g_info.mem[s][1][1]);
    mi->AvailableForReservation = u64(g_info.mem[s][2][0], g_info.mem[s][2][1]); mi->CurrentReservation = u64(g_info.mem[s][3][0], g_info.mem[s][3][1]);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetVideoMemoryReservation(UINT, DXGI_MEMORY_SEGMENT_GROUP, UINT64) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE RegisterVideoMemoryBudgetChangeNotificationEvent(HANDLE, DWORD*) override { return DXGI_ERROR_UNSUPPORTED; }
  void STDMETHODCALLTYPE UnregisterVideoMemoryBudgetChangeNotification(DWORD) override {}
};

struct VDxgiFactory final : public DxgiObj<IDXGIFactory1> {
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER; *ppv = nullptr;
    if (riid == kIID_IUnknown || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1)) { *ppv = this; AddRef(); return S_OK; }
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE GetParent(REFIID, void** pp) override { if (pp) *pp = nullptr; return E_NOINTERFACE; }
  HRESULT STDMETHODCALLTYPE EnumAdapters(UINT i, IDXGIAdapter** out) override {
    if (!out) return E_INVALIDARG; *out = nullptr;
    if (i > 0) return DXGI_ERROR_NOT_FOUND;
    *out = new VDxgiAdapter(this); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT i, IDXGIAdapter1** out) override { return EnumAdapters(i, (IDXGIAdapter**) out); }
  HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND, UINT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* w) override { if (w) *w = nullptr; return S_OK; }
  HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**) override { return DXGI_ERROR_UNSUPPORTED; }
  HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE, IDXGIAdapter**) override { return DXGI_ERROR_UNSUPPORTED; }
  BOOL STDMETHODCALLTYPE IsCurrent() override { return TRUE; }
};

static void* g_orig[3];
static HRESULT makeFactory(int which, UINT flags, REFIID riid, void** pp) {
  if (!pp) return E_INVALIDARG;
  *pp = nullptr;
  if (!fetchInfo(false)) {   // no host: let the real DXGI answer
    log("DXGI: host unavailable, using the original dxgi.dll");
    if (which == 2 && g_orig[2]) return ((HRESULT (WINAPI*)(UINT, REFIID, void**)) g_orig[2])(flags, riid, pp);
    if (g_orig[which]) return ((HRESULT (WINAPI*)(REFIID, void**)) g_orig[which])(riid, pp);
    return E_FAIL;
  }
  VDxgiFactory* f = new VDxgiFactory();
  HRESULT hr = f->QueryInterface(riid, pp);
  f->Release();
  static int logged; if (logged++ < 4) log("DXGI factory %d created by VCRender (%s)", which, SUCCEEDED(hr) ? "ok" : "interface not offered");
  return hr;
}
static HRESULT WINAPI fakeCreate(REFIID riid, void** pp) { return makeFactory(0, 0, riid, pp); }
static HRESULT WINAPI fakeCreate1(REFIID riid, void** pp) { return makeFactory(1, 0, riid, pp); }
static HRESULT WINAPI fakeCreate2(UINT flags, REFIID riid, void** pp) { return makeFactory(2, flags, riid, pp); }

// point dxgi.dll's exports at the functions above; GTAIV.exe and FusionFix look them up with GetProcAddress
void installDxgiRedirect() {
  HMODULE m = LoadLibraryW(L"dxgi.dll");   // kept loaded for the life of the process so the redirect stays
  if (!m) { log("DXGI: dxgi.dll not found; nothing to redirect"); return; }
  auto* dos = (IMAGE_DOS_HEADER*) m; auto* nt = (IMAGE_NT_HEADERS32*) ((BYTE*) m + dos->e_lfanew);
  auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dir.VirtualAddress) { log("DXGI: dxgi.dll has no exports"); return; }
  auto* ex = (IMAGE_EXPORT_DIRECTORY*) ((BYTE*) m + dir.VirtualAddress);
  DWORD* funcs = (DWORD*) ((BYTE*) m + ex->AddressOfFunctions);
  DWORD* names = (DWORD*) ((BYTE*) m + ex->AddressOfNames);
  WORD* ords = (WORD*) ((BYTE*) m + ex->AddressOfNameOrdinals);
  const char* want[3] = {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2"};
  void* repl[3] = {(void*) &fakeCreate, (void*) &fakeCreate1, (void*) &fakeCreate2};
  int done = 0;
  for (DWORD i = 0; i < ex->NumberOfNames; i++) {
    const char* n = (const char*) m + names[i];
    for (int k = 0; k < 3; k++) if (!strcmp(n, want[k])) {
      DWORD* slot = &funcs[ords[i]];
      g_orig[k] = (BYTE*) m + *slot;
      DWORD rva = (DWORD) ((uintptr_t) repl[k] - (uintptr_t) m);   // wraps modulo 2^32 like the loader's own arithmetic
      DWORD old;
      if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { *slot = rva; VirtualProtect(slot, 4, old, &old); done++; }
    }
  }
  bool ok = GetProcAddress(m, "CreateDXGIFactory") == (FARPROC) &fakeCreate;
  log("DXGI: %d factory exports of dxgi.dll now answered by VCRender (%s)", done, ok ? "verified" : "NOT verified");
}

}  // namespace vcr
