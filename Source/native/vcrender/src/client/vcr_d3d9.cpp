// IDirect3D9(Ex): adapter queries answered by the host's DXVK, cached because they do not change.
#include "vcr_client.h"
#include <string>

namespace vcr {

static std::unordered_map<std::string, std::vector<uint8_t>>* g_cache;
static std::string key(uint16_t op, const PAdapterQ& q) { std::string k((const char*) &op, 2); k.append((const char*) &q, sizeof q); return k; }

// returns hr; payload copied into out (cap bytes)
HRESULT moduleQuery(uint16_t op, const PAdapterQ& q, void* out, uint32_t cap) {
  Guard g;
  if (!g_cache) g_cache = new std::unordered_map<std::string, std::vector<uint8_t>>();
  bool cacheable = op != OP_ADAPTER_DISPLAY_MODE && op != OP_ADAPTER_DISPLAY_MODE_EX;
  std::string k = key(op, q);
  if (cacheable) {
    auto it = g_cache->find(k);
    if (it != g_cache->end()) {
      HRESULT hr; memcpy(&hr, it->second.data(), 4);
      uint32_t n = (uint32_t) it->second.size() - 4; if (n > cap) n = cap;
      if (out && n) memcpy(out, it->second.data() + 4, n);
      return hr;
    }
  }
  uint8_t buf[2048]; uint32_t got = 0;
  HRESULT hr = call(op, &q, sizeof q, buf, sizeof buf, &got);
  if (got > sizeof buf) got = sizeof buf;
  if (cacheable) { std::vector<uint8_t> v(4 + got); memcpy(v.data(), &hr, 4); memcpy(v.data() + 4, buf, got); (*g_cache)[k] = std::move(v); }
  uint32_t n = got < cap ? got : cap;
  if (out && n) memcpy(out, buf, n);
  return hr;
}

static VDevice* g_fsDevice;   // device currently emulating exclusive fullscreen
bool fullscreenMode(D3DDISPLAYMODE* m);

HRESULT STDMETHODCALLTYPE VD3D::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER; *ppv = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3D9) || (isEx && riid == __uuidof(IDirect3D9Ex))) { *ppv = this; AddRef(); return S_OK; }
  return E_NOINTERFACE;
}
UINT STDMETHODCALLTYPE VD3D::GetAdapterCount() { PAdapterQ q = {}; uint32_t n = 0; moduleQuery(OP_ADAPTER_COUNT, q, &n, 4); return n; }
HRESULT STDMETHODCALLTYPE VD3D::GetAdapterIdentifier(UINT a, DWORD flags, D3DADAPTER_IDENTIFIER9* id) {
  if (!id) return D3DERR_INVALIDCALL;
  PAdapterQ q = {a, 0, flags}; RAdapterId r = {};
  HRESULT hr = moduleQuery(OP_ADAPTER_IDENTIFIER, q, &r, sizeof r);
  if (FAILED(hr)) return hr;
  memcpy(id->Driver, r.driver, 512); memcpy(id->Description, r.description, 512); memcpy(id->DeviceName, r.deviceName, 32);
  id->DriverVersion.LowPart = r.versionLow; id->DriverVersion.HighPart = (LONG) r.versionHigh; id->VendorId = r.vendorId; id->DeviceId = r.deviceId;
  id->SubSysId = r.subSysId; id->Revision = r.revision; memcpy(&id->DeviceIdentifier, r.guid, 16); id->WHQLLevel = r.whql;
  return hr;
}
UINT STDMETHODCALLTYPE VD3D::GetAdapterModeCount(UINT a, D3DFORMAT f) { PAdapterQ q = {a, 0, (uint32_t) f}; uint32_t n = 0; moduleQuery(OP_ADAPTER_MODE_COUNT, q, &n, 4); return n; }
HRESULT STDMETHODCALLTYPE VD3D::EnumAdapterModes(UINT a, D3DFORMAT f, UINT i, D3DDISPLAYMODE* m) {
  if (!m) return D3DERR_INVALIDCALL;
  PAdapterQ q = {a, 0, (uint32_t) f, i}; RMode r = {};
  HRESULT hr = moduleQuery(OP_ENUM_ADAPTER_MODES, q, &r, sizeof r);
  m->Width = r.w; m->Height = r.h; m->RefreshRate = r.refresh; m->Format = (D3DFORMAT) r.fmt; return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* m) {
  if (!m) return D3DERR_INVALIDCALL;
  if (a == 0 && fullscreenMode(m)) return D3D_OK;
  PAdapterQ q = {a}; RMode r = {};
  HRESULT hr = moduleQuery(OP_ADAPTER_DISPLAY_MODE, q, &r, sizeof r);
  m->Width = r.w; m->Height = r.h; m->RefreshRate = r.refresh; m->Format = (D3DFORMAT) r.fmt; return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::CheckDeviceType(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT bf, BOOL win) {
  PAdapterQ q = {a, (uint32_t) t, (uint32_t) af, (uint32_t) bf, (uint32_t) win}; return moduleQuery(OP_CHECK_DEVICE_TYPE, q, nullptr, 0);
}
HRESULT STDMETHODCALLTYPE VD3D::CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD usage, D3DRESOURCETYPE rt, D3DFORMAT cf) {
  PAdapterQ q = {a, (uint32_t) t, (uint32_t) af, usage, (uint32_t) rt, (uint32_t) cf}; return moduleQuery(OP_CHECK_DEVICE_FORMAT, q, nullptr, 0);
}
HRESULT STDMETHODCALLTYPE VD3D::CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE t, D3DFORMAT f, BOOL win, D3DMULTISAMPLE_TYPE ms, DWORD* qual) {
  PAdapterQ q = {a, (uint32_t) t, (uint32_t) f, (uint32_t) win, (uint32_t) ms}; uint32_t v = 0;
  HRESULT hr = moduleQuery(OP_CHECK_MULTISAMPLE, q, &v, 4); if (qual) *qual = v; return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) {
  PAdapterQ q = {a, (uint32_t) t, (uint32_t) af, (uint32_t) rf, (uint32_t) df}; return moduleQuery(OP_CHECK_DEPTH_MATCH, q, nullptr, 0);
}
HRESULT STDMETHODCALLTYPE VD3D::CheckDeviceFormatConversion(UINT a, D3DDEVTYPE t, D3DFORMAT s, D3DFORMAT d) {
  PAdapterQ q = {a, (uint32_t) t, (uint32_t) s, (uint32_t) d}; return moduleQuery(OP_CHECK_FORMAT_CONVERSION, q, nullptr, 0);
}
HRESULT STDMETHODCALLTYPE VD3D::GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) {
  if (!c) return D3DERR_INVALIDCALL; PAdapterQ q = {a, (uint32_t) t}; return moduleQuery(OP_ADAPTER_CAPS, q, c, sizeof *c);
}
HMONITOR STDMETHODCALLTYPE VD3D::GetAdapterMonitor(UINT a) {
  if (a != 0) return nullptr;
  POINT p = {0, 0}; return MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
}
HRESULT STDMETHODCALLTYPE VD3D::CreateDevice(UINT a, D3DDEVTYPE t, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
  VDevice* d = nullptr; HRESULT hr = createDevice(a, t, focus, beh, pp, nullptr, false, &d);
  if (out) *out = d;
  return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::CreateDeviceEx(UINT a, D3DDEVTYPE t, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fs, IDirect3DDevice9Ex** out) {
  if (!isEx) return D3DERR_INVALIDCALL;
  VDevice* d = nullptr; HRESULT hr = createDevice(a, t, focus, beh, pp, fs, true, &d);
  if (out) *out = d;
  return hr;
}
UINT STDMETHODCALLTYPE VD3D::GetAdapterModeCountEx(UINT a, const D3DDISPLAYMODEFILTER* f) {
  PAdapterQ q = {a, 0, f ? (uint32_t) f->Format : 0u, f ? (uint32_t) f->ScanLineOrdering : 0u}; uint32_t n = 0;
  moduleQuery(OP_ADAPTER_MODE_COUNT_EX, q, &n, 4); return n;
}
HRESULT STDMETHODCALLTYPE VD3D::EnumAdapterModesEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT i, D3DDISPLAYMODEEX* m) {
  if (!m) return D3DERR_INVALIDCALL;
  PAdapterQ q = {a, 0, f ? (uint32_t) f->Format : 0u, f ? (uint32_t) f->ScanLineOrdering : 0u, i}; RModeEx r = {};
  HRESULT hr = moduleQuery(OP_ENUM_ADAPTER_MODES_EX, q, &r, sizeof r);
  m->Width = r.w; m->Height = r.h; m->RefreshRate = r.refresh; m->Format = (D3DFORMAT) r.fmt; m->ScanLineOrdering = (D3DSCANLINEORDERING) r.scan; return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::GetAdapterDisplayModeEx(UINT a, D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* rot) {
  if (!m) return D3DERR_INVALIDCALL;
  D3DDISPLAYMODE d;
  if (a == 0 && fullscreenMode(&d)) { m->Width = d.Width; m->Height = d.Height; m->RefreshRate = d.RefreshRate; m->Format = d.Format; m->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE; if (rot) *rot = D3DDISPLAYROTATION_IDENTITY; return D3D_OK; }
  PAdapterQ q = {a}; RModeEx r = {};
  HRESULT hr = moduleQuery(OP_ADAPTER_DISPLAY_MODE_EX, q, &r, sizeof r);
  m->Width = r.w; m->Height = r.h; m->RefreshRate = r.refresh; m->Format = (D3DFORMAT) r.fmt; m->ScanLineOrdering = (D3DSCANLINEORDERING) r.scan;
  if (rot) *rot = (D3DDISPLAYROTATION) (r.size ? r.size : D3DDISPLAYROTATION_IDENTITY);
  return hr;
}
HRESULT STDMETHODCALLTYPE VD3D::GetAdapterLUID(UINT a, LUID* l) {
  if (!l) return D3DERR_INVALIDCALL; PAdapterQ q = {a}; return moduleQuery(OP_ADAPTER_LUID, q, l, sizeof *l);
}

bool fullscreenMode(D3DDISPLAYMODE* m) {
  VDevice* d = g_fsDevice;
  if (!d || !d->fsActive) return false;
  *m = d->fsMode; return true;
}
void setFullscreenDevice(VDevice* d, bool on) { if (on) g_fsDevice = d; else if (g_fsDevice == d) g_fsDevice = nullptr; }

}  // namespace vcr
