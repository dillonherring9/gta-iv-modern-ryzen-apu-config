// The device: state mirror with DXVK semantics, command encoding, Reset/Present, window handling.
#include "vcr_client.h"
#include <math.h>

namespace vcr {

void setFullscreenDevice(VDevice* d, bool on);
static inline DWORD fbits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }
static const D3DMATRIX kIdentity = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
static int samplerIndex(DWORD s) {
  if (s < 16) return (int) s;
  if (s == D3DDMAPSAMPLER) return 16;
  if (s >= D3DVERTEXTEXTURESAMPLER0 && s <= D3DVERTEXTEXTURESAMPLER3) return 17 + (int) (s - D3DVERTEXTEXTURESAMPLER0);
  return -1;
}

static void initStateVectors(VState& s, VDevice* d) {
  s.vsF.assign((size_t) d->vsFloatCount * 4, 0.f); s.vsI.assign((size_t) d->vsIntCount * 4, 0); s.vsB.assign(d->vsBoolCount, FALSE);
  for (auto& m : s.xf) m = kIdentity;
}
static void initMask(SBMask& m, VDevice* d) {
  m.vsF.assign(d->vsFloatCount, false); m.vsI.assign(d->vsIntCount, false); m.vsB.assign(d->vsBoolCount, false);
}

// ================================================================ device lifetime
VDevice::~VDevice() {}

void VDevice::finalRelease() {
  Guard g;
  leaveFullscreen();
  if (recording) { recording->ownedByDevice = false; delete recording; recording = nullptr; }
  releaseStateRefs(st);
  for (auto& r : rt) setRef<VBase>(r, nullptr);
  setRef<VBase>(ds, nullptr);
  for (auto& kv : fvfDecls) { VDecl* d = kv.second; if (d->id) freeId(d->id); d->id = 0; d->ownedByDevice = false; if (d->pub <= 0 && d->priv <= 0) delete d; }
  fvfDecls.clear();
  if (implicitSc) {
    presenterUnregister(implicitSc->id);
    implicitSc->destroyBackBuffers();
    if (implicitSc->id) freeId(implicitSc->id);
    implicitSc->id = 0;
    if (implicitSc->pub <= 0) delete implicitSc;   // a game reference left over keeps an inert object
    implicitSc = nullptr;
  }
  if (autoDS) { if (autoDS->id) freeId(autoDS->id); autoDS->id = 0; autoDS->own.addr.id = 0; autoDS = nullptr; }
  interopDeviceGone(this);
  if (id && ready()) { *rec<uint32_t>(OP_DESTROY) = id; flush(); freeId(id); }
  id = 0;
  log("device destroyed");
  interopFreeDevice(this);
  VD3D* p = parent; parent = nullptr;
  delete this;
  if (p) p->Release();
}

HRESULT STDMETHODCALLTYPE VDevice::QueryInterface(REFIID riid, void** ppv) {
  if (!ppv) return E_POINTER; *ppv = nullptr;
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IDirect3DDevice9) || (isEx && riid == __uuidof(IDirect3DDevice9Ex))) {
    *ppv = static_cast<IDirect3DDevice9Ex*>(this); AddRef(); return S_OK;
  }
  return interopQueryDevice(this, riid, ppv);
}

void VDevice::resetState(const D3DPRESENT_PARAMETERS& pp) {
  releaseStateRefs(st);
  st = VState();
  initStateVectors(st, this);
  DWORD* rs = st.rs;
  rs[D3DRS_SEPARATEALPHABLENDENABLE] = FALSE; rs[D3DRS_ALPHABLENDENABLE] = FALSE; rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
  rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD; rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO; rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
  rs[D3DRS_COLORWRITEENABLE] = rs[D3DRS_COLORWRITEENABLE1] = rs[D3DRS_COLORWRITEENABLE2] = rs[D3DRS_COLORWRITEENABLE3] = 0xF;
  rs[D3DRS_SRCBLEND] = D3DBLEND_ONE; rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE; rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
  rs[D3DRS_ZENABLE] = pp.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE; rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
  rs[D3DRS_TWOSIDEDSTENCILMODE] = FALSE; rs[D3DRS_ZWRITEENABLE] = TRUE; rs[D3DRS_STENCILENABLE] = FALSE;
  rs[D3DRS_STENCILFAIL] = rs[D3DRS_STENCILZFAIL] = rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP; rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
  rs[D3DRS_CCW_STENCILFAIL] = rs[D3DRS_CCW_STENCILZFAIL] = rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP; rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
  rs[D3DRS_STENCILMASK] = rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF; rs[D3DRS_STENCILREF] = 0;
  rs[D3DRS_FILLMODE] = D3DFILL_SOLID; rs[D3DRS_CULLMODE] = D3DCULL_CCW; rs[D3DRS_DEPTHBIAS] = fbits(0.f); rs[D3DRS_SLOPESCALEDEPTHBIAS] = fbits(0.f);
  rs[D3DRS_SCISSORTESTENABLE] = FALSE; rs[D3DRS_ALPHATESTENABLE] = FALSE; rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS; rs[D3DRS_ALPHAREF] = 0;
  rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF; rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
  rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1; rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
  rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL; rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
  rs[D3DRS_LIGHTING] = TRUE; rs[D3DRS_COLORVERTEX] = TRUE; rs[D3DRS_LOCALVIEWER] = TRUE; rs[D3DRS_RANGEFOGENABLE] = FALSE;
  rs[D3DRS_NORMALIZENORMALS] = FALSE; rs[D3DRS_SPECULARENABLE] = FALSE; rs[D3DRS_AMBIENT] = 0;
  rs[D3DRS_FOGENABLE] = FALSE; rs[D3DRS_FOGCOLOR] = 0; rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE; rs[D3DRS_FOGSTART] = fbits(0.f);
  rs[D3DRS_FOGEND] = fbits(1.f); rs[D3DRS_FOGDENSITY] = fbits(1.f); rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE; rs[D3DRS_CLIPPLANEENABLE] = 0;
  rs[D3DRS_POINTSPRITEENABLE] = FALSE; rs[D3DRS_POINTSCALEENABLE] = FALSE; rs[D3DRS_POINTSCALE_A] = fbits(1.f);
  rs[D3DRS_POINTSCALE_B] = fbits(0.f); rs[D3DRS_POINTSCALE_C] = fbits(0.f); rs[D3DRS_POINTSIZE] = fbits(1.f);
  rs[D3DRS_POINTSIZE_MIN] = fbits(1.f); rs[D3DRS_POINTSIZE_MAX] = fbits(caps.MaxPointSize > 0 ? caps.MaxPointSize : 256.f);
  rs[D3DRS_SRGBWRITEENABLE] = 0; rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD; rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
  rs[D3DRS_INDEXEDVERTEXBLENDENABLE] = FALSE; rs[D3DRS_TWEENFACTOR] = fbits(0.f); rs[D3DRS_LASTPIXEL] = TRUE; rs[D3DRS_DITHERENABLE] = FALSE;
  rs[D3DRS_CLIPPING] = TRUE; rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE; rs[D3DRS_PATCHEDGESTYLE] = D3DPATCHEDGE_DISCRETE;
  rs[D3DRS_DEBUGMONITORTOKEN] = D3DDMT_ENABLE; rs[D3DRS_POSITIONDEGREE] = D3DDEGREE_CUBIC; rs[D3DRS_NORMALDEGREE] = D3DDEGREE_LINEAR;
  rs[D3DRS_ANTIALIASEDLINEENABLE] = FALSE; rs[D3DRS_MINTESSELLATIONLEVEL] = fbits(1.f); rs[D3DRS_MAXTESSELLATIONLEVEL] = fbits(1.f);
  rs[D3DRS_ADAPTIVETESS_X] = fbits(0.f); rs[D3DRS_ADAPTIVETESS_Y] = fbits(0.f); rs[D3DRS_ADAPTIVETESS_Z] = fbits(1.f);
  rs[D3DRS_ADAPTIVETESS_W] = fbits(0.f); rs[D3DRS_ENABLEADAPTIVETESSELLATION] = FALSE;
  for (uint32_t i = 0; i < kStages; i++) {
    DWORD* t = st.tss[i];
    t[D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE; t[D3DTSS_COLORARG1] = D3DTA_TEXTURE; t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
    t[D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE; t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE; t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
    t[D3DTSS_BUMPENVMAT00] = t[D3DTSS_BUMPENVMAT01] = t[D3DTSS_BUMPENVMAT10] = t[D3DTSS_BUMPENVMAT11] = fbits(0.f);
    t[D3DTSS_TEXCOORDINDEX] = i; t[D3DTSS_BUMPENVLSCALE] = fbits(0.f); t[D3DTSS_BUMPENVLOFFSET] = fbits(0.f);
    t[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE; t[D3DTSS_COLORARG0] = t[D3DTSS_ALPHAARG0] = t[D3DTSS_RESULTARG] = D3DTA_CURRENT; t[D3DTSS_CONSTANT] = 0;
  }
  for (uint32_t i = 0; i < kStreams; i++) st.freq[i] = 1;
  for (uint32_t i = 0; i < kSamplers; i++) {
    DWORD* s = st.ss[i];
    s[D3DSAMP_ADDRESSU] = s[D3DSAMP_ADDRESSV] = s[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP; s[D3DSAMP_BORDERCOLOR] = 0;
    s[D3DSAMP_MAGFILTER] = s[D3DSAMP_MINFILTER] = D3DTEXF_POINT; s[D3DSAMP_MIPFILTER] = D3DTEXF_NONE; s[D3DSAMP_MIPMAPLODBIAS] = fbits(0.f);
    s[D3DSAMP_MAXMIPLEVEL] = 0; s[D3DSAMP_MAXANISOTROPY] = 1; s[D3DSAMP_SRGBTEXTURE] = 0; s[D3DSAMP_ELEMENTINDEX] = 0; s[D3DSAMP_DMAPOFFSET] = 0;
  }
  npatch = 0.f; swvp = (cp.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) ? TRUE : FALSE;
  clipStatus = {};
}

void VDevice::bindImplicitTargets() {
  for (auto& r : rt) setRef<VBase>(r, nullptr);
  setRef<VBase>(ds, nullptr);
  if (implicitSc && !implicitSc->bb.empty()) {
    setRef<VBase>(rt[0], implicitSc->bb[0]);
    implicitSc->bb[0]->own.gpuWritten = true;
    uint32_t w = implicitSc->bb[0]->own.w, h = implicitSc->bb[0]->own.h;
    st.vp = {0, 0, w, h, 0.f, 1.f}; st.scissor = {0, 0, (LONG) w, (LONG) h};
  }
  if (autoDS) setRef<VBase>(ds, autoDS);
}

static VcrPP toWire(const D3DPRESENT_PARAMETERS& p) {
  VcrPP v = {}; v.w = p.BackBufferWidth; v.h = p.BackBufferHeight; v.fmt = p.BackBufferFormat; v.count = p.BackBufferCount;
  v.ms = p.MultiSampleType; v.msq = p.MultiSampleQuality; v.swap = p.SwapEffect; v.windowed = p.Windowed; v.autoDS = p.EnableAutoDepthStencil;
  v.dsFmt = p.AutoDepthStencilFormat; v.flags = p.Flags; v.refresh = p.FullScreen_RefreshRateInHz; v.interval = p.PresentationInterval; return v;
}

static VSurface* makeStandaloneSurface(VDevice* d, uint32_t id, uint32_t w, uint32_t h, D3DFORMAT f, D3DPOOL pool, DWORD usage,
                                       D3DMULTISAMPLE_TYPE ms, DWORD msq, bool lockable) {
  VSurface* s = new VSurface();
  s->kind = K_SURFACE; s->dev = d; s->id = id; s->registered = true; s->img = &s->own;
  ImgSub& i = s->own; i.owner = s; i.addr = VcrSub{id, K_SURFACE, 0, 0}; i.w = w; i.h = h; i.d = 1; i.fmt = f; i.pool = pool; i.usage = usage;
  i.ms = ms; i.msq = msq; i.lockable = lockable && fmtInfo(f).known; i.resident = pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_SCRATCH;
  if (i.resident) i.mem.assign((size_t) i.slicePitch(), 0);
  return s;
}

// sanitize the game's parameters the way the runtime would before handing them to the host
static void sanitizePP(D3DPRESENT_PARAMETERS& p, HWND focus) {
  if (!p.hDeviceWindow) p.hDeviceWindow = focus;
  if (p.BackBufferCount == 0) p.BackBufferCount = 1;
  if (p.Windowed && (p.BackBufferWidth == 0 || p.BackBufferHeight == 0)) {
    RECT r = {0, 0, 640, 480}; if (p.hDeviceWindow) GetClientRect(p.hDeviceWindow, &r);
    if (!p.BackBufferWidth) p.BackBufferWidth = r.right > 0 ? r.right : 1;
    if (!p.BackBufferHeight) p.BackBufferHeight = r.bottom > 0 ? r.bottom : 1;
  }
  if (p.Windowed && p.BackBufferFormat == D3DFMT_UNKNOWN) p.BackBufferFormat = D3DFMT_X8R8G8B8;
}

// Match the runtime's validation before releasing implicit resources. The host
// renders windowed, so it cannot validate the original fullscreen arguments.
static HRESULT validatePP(const D3DPRESENT_PARAMETERS& p, bool ex, const D3DDISPLAYMODEEX* mode, bool exCall) {
  const UINT swap = (UINT) p.SwapEffect;
  if (swap == 0 || swap > (UINT) (ex ? D3DSWAPEFFECT_FLIPEX : D3DSWAPEFFECT_COPY)) return D3DERR_INVALIDCALL;
  if (p.BackBufferCount > (ex ? D3DPRESENT_BACK_BUFFERS_MAX_EX : D3DPRESENT_BACK_BUFFERS_MAX)) return D3DERR_INVALIDCALL;
  if (p.SwapEffect == D3DSWAPEFFECT_COPY && p.BackBufferCount > 1) return D3DERR_INVALIDCALL;
  const UINT interval = p.PresentationInterval;
  if (interval != D3DPRESENT_INTERVAL_DEFAULT && interval != D3DPRESENT_INTERVAL_ONE && interval != D3DPRESENT_INTERVAL_IMMEDIATE &&
      (p.Windowed || (interval != D3DPRESENT_INTERVAL_TWO && interval != D3DPRESENT_INTERVAL_THREE && interval != D3DPRESENT_INTERVAL_FOUR))) return D3DERR_INVALIDCALL;
  if (!p.Windowed && (!p.BackBufferWidth || !p.BackBufferHeight || p.BackBufferFormat == D3DFMT_UNKNOWN)) return D3DERR_INVALIDCALL;
  if (exCall) {
    if ((p.Windowed && mode) || (!p.Windowed && !mode)) return D3DERR_INVALIDCALL;
    if (mode && (mode->Width != p.BackBufferWidth || mode->Height != p.BackBufferHeight)) return D3DERR_INVALIDCALL;
  }
  return D3D_OK;
}

HRESULT VDevice::createImplicit(const D3DPRESENT_PARAMETERS& pp, const D3DDISPLAYMODEEX*, bool) {
  (void) 0; return D3D_OK;
}


static void setupFpu() {
  uint16_t control;
  __asm__ __volatile__("fnstcw %0" : "=m"(control));
  control &= 0xF0C0; control |= 0x003F;   // all exceptions masked, 24-bit precision, round to nearest (as DXVK/D3D9)
  __asm__ __volatile__("fldcw %0" : : "m"(control));
}

// build the client objects for an implicit swapchain the host already created
static void buildImplicit(VDevice* d, const D3DPRESENT_PARAMETERS& pp, const VcrPP& actual, uint32_t scId, const uint32_t* bbIds, uint32_t dsId) {
  // GetSwapChain's COM identity survives Reset, as it does in DXVK. The game
  // and overlays can keep that interface while its host resources are rebuilt.
  VSwapChain* sc = d->implicitSc ? d->implicitSc : new VSwapChain();
  sc->kind = K_SWAPCHAIN; sc->dev = d; sc->id = scId; sc->implicit = true; sc->ownedByDevice = true; sc->pp = pp;
  sc->pp.BackBufferWidth = actual.w; sc->pp.BackBufferHeight = actual.h;
  sc->pp.BackBufferFormat = (D3DFORMAT) actual.fmt; sc->pp.BackBufferCount = actual.count; sc->fullscreen = !pp.Windowed;
  sc->frameNo = 0; // completion counters belong to the new host frame buffer
  uint32_t n = actual.count ? actual.count : 1; if (n > 4) n = 4;
  for (uint32_t i = 0; i < n; i++) {
    VSurface* b = makeStandaloneSurface(d, bbIds[i], actual.w, actual.h, (D3DFORMAT) actual.fmt, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET,
                                        (D3DMULTISAMPLE_TYPE) actual.ms, actual.msq, (pp.Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0);
    b->container = sc; b->containerIface = static_cast<IDirect3DSwapChain9Ex*>(sc); b->ownedByDevice = true;
    sc->bb.push_back(b);
  }
  d->implicitSc = sc;
  d->autoDS = nullptr;
  if (pp.EnableAutoDepthStencil && dsId) {
    d->autoDS = makeStandaloneSurface(d, dsId, actual.w, actual.h, pp.AutoDepthStencilFormat, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL,
                                      (D3DMULTISAMPLE_TYPE) actual.ms, actual.msq, isLockableDepth(pp.AutoDepthStencilFormat));
    d->autoDS->ownedByDevice = true;
  }
  presenterRegister(scId, pp.hDeviceWindow);
  presenterSetWindow(scId, pp.hDeviceWindow, !pp.Windowed, nullptr, nullptr);
  if (sc->gammaSet) presenterSetGamma(scId, &sc->gamma);
}

HRESULT VD3D::createDevice(UINT adapter, D3DDEVTYPE type, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* ppIn, D3DDISPLAYMODEEX* fs, bool ex, VDevice** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  if (!ppIn) return D3DERR_INVALIDCALL;
  if (!focus && !ppIn->hDeviceWindow) return D3DERR_INVALIDCALL;
  HRESULT valid = validatePP(*ppIn, ex, fs, ex);
  if (FAILED(valid)) return valid;
  Guard g;
  D3DPRESENT_PARAMETERS pp = *ppIn;
  sanitizePP(pp, focus);
  VDevice* d = new VDevice();
  d->kind = K_DEVICE; d->dev = nullptr; d->parent = this; AddRef(); d->isEx = ex;
  d->cp = {adapter, type, focus, beh};
  bool canSwvp = (beh & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0;
  d->vsFloatCount = canSwvp ? 8192 : 256; d->vsIntCount = canSwvp ? 2048 : 16; d->vsBoolCount = canSwvp ? 2048 : 16;
  d->id = newId();
  PCreateDevice c = {};
  c.devId = d->id; c.adapter = adapter; c.devType = type; c.behavior = beh; c.isEx = ex; c.pp = toWire(pp); c.pp.windowed = 1;
  c.scId = newId(); uint32_t n = pp.BackBufferCount > 4 ? 4 : pp.BackBufferCount;
  for (uint32_t i = 0; i < n; i++) c.bbIds[i] = newId();
  c.dsId = pp.EnableAutoDepthStencil ? newId() : 0;
  if (fs) { c.fsW = fs->Width; c.fsH = fs->Height; c.fsRefresh = fs->RefreshRate; c.fsFmt = fs->Format; c.fsScan = fs->ScanLineOrdering; }
  if (!pp.Windowed) d->applyWindowMode(pp, fs);
  RCreateDevice resp = {}; uint32_t got = 0;
  HRESULT hr = call(OP_CREATE_DEVICE, &c, sizeof c, &resp, sizeof resp, &got);
  log("CreateDevice%s %ux%u fmt %u %s count %u autoDS %u/%u behavior 0x%lx -> 0x%08lx", ex ? "Ex" : "", pp.BackBufferWidth, pp.BackBufferHeight,
      pp.BackBufferFormat, pp.Windowed ? "windowed" : "fullscreen", pp.BackBufferCount, pp.EnableAutoDepthStencil, pp.AutoDepthStencilFormat,
      beh, (unsigned long) hr);
  if (FAILED(hr)) {
    d->leaveFullscreen();
    freeId(c.scId); for (uint32_t i = 0; i < n; i++) freeId(c.bbIds[i]); if (c.dsId) freeId(c.dsId); freeId(d->id);
    d->id = 0; d->parent = nullptr; delete d; Release();
    return hr;
  }
  memcpy(&d->caps, resp.caps, sizeof d->caps);
  if (!(beh & D3DCREATE_FPU_PRESERVE)) setupFpu();
  buildImplicit(d, pp, resp.pp, c.scId, c.bbIds, c.dsId);
  d->resetState(pp);
  d->bindImplicitTargets();
  d->restoreStartupWindow(pp);
  d->addRef();
  *out = d;
  *ppIn = d->implicitSc->pp;
  return D3D_OK;
}

HRESULT VDevice::resetImpl(D3DPRESENT_PARAMETERS* ppIn, D3DDISPLAYMODEEX* mode, bool ex) {
  if (!ppIn) return D3DERR_INVALIDCALL;
  Guard g;
  HRESULT valid = validatePP(*ppIn, isEx, mode, ex);
  if (FAILED(valid)) return valid;
  if (!ready()) return D3DERR_DEVICELOST;
  const LONG resetCoop = InterlockedCompareExchange(&coopState, 0, 0);
  D3DPRESENT_PARAMETERS pp = *ppIn;
  sanitizePP(pp, cp.hFocusWindow);
  const float minZ = st.vp.MinZ, maxZ = st.vp.MaxZ;
  if (!isEx) {
    if (recording) { recording->ownedByDevice = false; recording->finalRelease(); recording = nullptr; }
    resetState(pp);
    inScene = false;
  }
  for (auto& r : rt) setRef<VBase>(r, nullptr);
  setRef<VBase>(ds, nullptr);
  if (!isEx) {
    bool held = autoDS && autoDS->pub > 0;
    if (implicitSc) for (VSurface* s : implicitSc->bb) held = held || s->forwardedPub > 0;
    if (held) {
      LONG observed = InterlockedCompareExchange(&coopState, 0, 0);
      for (;;) {
        const LONG next = (observed & ~3L) | ((observed & 3) == 1 ? 1 : 2);
        const LONG previous = InterlockedCompareExchange(&coopState, next, observed);
        if (previous == observed) break;
        observed = previous;
      }
      log("Reset refused: application still holds an implicit surface");
      return D3DERR_INVALIDCALL;
    }
  }
  if (implicitSc) { startupPresented = presenterUnregister(implicitSc->id) || startupPresented; implicitSc->destroyBackBuffers(isEx);
                    if (implicitSc->id) freeId(implicitSc->id); implicitSc->id = 0; }
  if (autoDS) {
    if (!isEx) { freeId(autoDS->id); autoDS->id = 0; autoDS->own.addr.id = 0; }
    autoDS->ownedByDevice = false; autoDS->maybeDelete(); autoDS = nullptr;
  }
  applyWindowMode(pp, mode);
  PReset c = {};
  c.devId = id; c.isEx = ex; c.pp = toWire(pp); c.pp.windowed = 1;
  c.scId = newId(); uint32_t n = pp.BackBufferCount > 4 ? 4 : pp.BackBufferCount;
  for (uint32_t i = 0; i < n; i++) c.bbIds[i] = newId();
  c.dsId = pp.EnableAutoDepthStencil ? newId() : 0;
  if (mode) { c.fsW = mode->Width; c.fsH = mode->Height; c.fsRefresh = mode->RefreshRate; c.fsFmt = mode->Format; c.fsScan = mode->ScanLineOrdering; }
  RCreateDevice resp = {}; uint32_t got = 0;
  HRESULT hr = call(OP_RESET, &c, sizeof c, &resp, sizeof resp, &got);
  log("Reset %ux%u fmt %u %s -> 0x%08lx (swapchain %p, retained refs %ld, iconic %d)", pp.BackBufferWidth, pp.BackBufferHeight, pp.BackBufferFormat, pp.Windowed ? "windowed" : "fullscreen", (unsigned long) hr,
      implicitSc, implicitSc ? implicitSc->pub : 0, IsIconic(pp.hDeviceWindow));
  if (FAILED(hr)) {
    freeId(c.scId); for (uint32_t i = 0; i < n; i++) freeId(c.bbIds[i]); if (c.dsId) freeId(c.dsId);
    if (!isEx) {
      LONG observed = InterlockedCompareExchange(&coopState, 0, 0);
      for (;;) {
        const LONG next = (observed & ~3L) | ((observed & 3) == 1 ? 1 : 2);
        const LONG previous = InterlockedCompareExchange(&coopState, next, observed);
        if (previous == observed) break;
        observed = previous;
      }
    }
    return hr;
  }
  buildImplicit(this, pp, resp.pp, c.scId, c.bbIds, c.dsId);
  bindImplicitTargets();
  if (isEx) { st.vp.MinZ = minZ; st.vp.MaxZ = maxZ; }
  // Window messages run without the device lock. Do not erase a newer loss
  // while the host was resetting; the generation prevents a lose/gain ABA.
  InterlockedCompareExchange(&coopState, resetCoop & ~3L, resetCoop);
  restoreStartupWindow(pp);
  *ppIn = implicitSc->pp;
  return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::Reset(D3DPRESENT_PARAMETERS* pp) { return resetImpl(pp, nullptr, false); }
HRESULT STDMETHODCALLTYPE VDevice::ResetEx(D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* m) { return resetImpl(pp, m, true); }

HRESULT VDevice::presentImpl(VSwapChain* sc, const RECT* src, const RECT* dst, HWND wnd, DWORD flags) {
  Guard g;
  // Present reports DEVICELOST until a successful reset, including NOTRESET state.
  if (FAILED(TestCooperativeLevel())) return D3DERR_DEVICELOST;
  if (!sc || !sc->id) return D3DERR_INVALIDCALL;
  sc->frameNo++; sc->presentCount++;
  budgetPoll();
  presenterSetWindow(sc->id, wnd ? wnd : sc->pp.hDeviceWindow, sc->fullscreen, src, dst);
  auto* p = rec<PPresent>(OP_PRESENT); p->devId = id; p->scId = sc->id; p->frameNo = sc->frameNo; p->flags = flags;
  flush();
  if (sc == implicitSc) presenterNotePresent(sc->frameNo);
  // How far the game may run ahead of the picture: Present N waits until frame N - ahead has been copied for the screen.
  // 1 keeps one frame of CPU/GPU overlap with the least input latency (the game is GPU-bound on this PC).
  const uint32_t ahead = (uint32_t) framesAheadSetting();
  if (sc->frameNo > ahead) waitFrameCompleted(sc->id, (int32_t) (sc->frameNo - ahead));
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::Present(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA*) { return presentImpl(implicitSc, src, dst, wnd, 0); }
HRESULT STDMETHODCALLTYPE VDevice::PresentEx(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA*, DWORD flags) { return presentImpl(implicitSc, src, dst, wnd, flags); }

// ================================================================ simple queries
void VDevice::windowActivated(bool activated) {
  if (isEx) return;
  LONG observed = InterlockedCompareExchange(&coopState, 0, 0);
  for (;;) {
    const LONG state = observed & 3;
    const LONG nextState = activated ? (state == 1 ? 2 : state) : 1;
    const LONG generation = (LONG) (((uint32_t) observed + 4u) & ~3u);
    const LONG previous = InterlockedCompareExchange(&coopState, generation | nextState, observed);
    if (previous == observed) break;
    observed = previous;
  }
}
HRESULT STDMETHODCALLTYPE VDevice::TestCooperativeLevel() {
  if (!ready()) return D3DERR_DEVICELOST;
  const LONG state = InterlockedCompareExchange(&coopState, 0, 0) & 3;
  return state == 0 ? D3D_OK : state == 2 ? D3DERR_DEVICENOTRESET : D3DERR_DEVICELOST;
}
HRESULT STDMETHODCALLTYPE VDevice::CheckDeviceState(HWND) { return ready() ? D3D_OK : D3DERR_DEVICELOST; }
UINT STDMETHODCALLTYPE VDevice::GetAvailableTextureMem() {
  Guard g; PGeneric q = {id}; uint32_t v = 0; call(OP_AVAILABLE_TEXMEM, &q, sizeof q, &v, 4);
  static int logged; if (logged++ < 3) log("GetAvailableTextureMem -> %u MiB", v >> 20);
  return v;
}
HRESULT STDMETHODCALLTYPE VDevice::EvictManagedResources() { Guard g; auto* r = rec<PGeneric>(OP_EVICT_MANAGED); r->devId = id; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetDirect3D(IDirect3D9** out) { if (!out) return D3DERR_INVALIDCALL; *out = parent; parent->AddRef(); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetDeviceCaps(D3DCAPS9* c) { if (!c) return D3DERR_INVALIDCALL; *c = caps; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetDisplayMode(UINT sc, D3DDISPLAYMODE* m) {
  if (!m) return D3DERR_INVALIDCALL;
  if (sc != 0 && sc > extraSc.size()) return D3DERR_INVALIDCALL;
  if (fsActive) { *m = fsMode; return D3D_OK; }
  return parent->GetAdapterDisplayMode(cp.AdapterOrdinal, m);
}
HRESULT STDMETHODCALLTYPE VDevice::GetDisplayModeEx(UINT sc, D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) {
  if (!m) return D3DERR_INVALIDCALL;
  D3DDISPLAYMODE d; HRESULT hr = GetDisplayMode(sc, &d); if (FAILED(hr)) return hr;
  m->Width = d.Width; m->Height = d.Height; m->RefreshRate = d.RefreshRate; m->Format = d.Format; m->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
  if (r) *r = D3DDISPLAYROTATION_IDENTITY; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) { if (!p) return D3DERR_INVALIDCALL; *p = cp; return D3D_OK; }

// ---- cursor (handled in this process; the host has no visible window)
HRESULT STDMETHODCALLTYPE VDevice::SetCursorProperties(UINT x, UINT y, IDirect3DSurface9* bmp) {
  Guard g;
  VSurface* s = asSurface(bmp);
  if (!s) return D3DERR_INVALIDCALL;
  ImgSub& i = *s->img;
  if (i.fmt != D3DFMT_A8R8G8B8 || i.w > 256 || i.h > 256) return D3DERR_INVALIDCALL;
  std::vector<uint8_t> px((size_t) i.w * i.h * 4);
  if (i.resident) { for (uint32_t r = 0; r < i.h; r++) memcpy(px.data() + (size_t) r * i.w * 4, i.mem.data() + (size_t) r * i.pitch(), i.w * 4); }
  else if (i.addr.id) readImg(i, px.data(), i.w * 4, 0, 0, 0, i.w, i.h, 0, 1, true);
  ICONINFO ii = {}; ii.fIcon = FALSE; ii.xHotspot = x; ii.yHotspot = y;
  BITMAPV5HEADER bh = {}; bh.bV5Size = sizeof bh; bh.bV5Width = (LONG) i.w; bh.bV5Height = -(LONG) i.h; bh.bV5Planes = 1; bh.bV5BitCount = 32;
  bh.bV5Compression = BI_BITFIELDS; bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
  HDC dc = GetDC(nullptr); void* bits = nullptr;
  ii.hbmColor = CreateDIBSection(dc, (BITMAPINFO*) &bh, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!ii.hbmColor) return D3DERR_INVALIDCALL;
  memcpy(bits, px.data(), px.size());
  ii.hbmMask = CreateBitmap(i.w, i.h, 1, 1, nullptr);
  HCURSOR c = (HCURSOR) CreateIconIndirect(&ii);
  DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask);
  if (!c) return D3DERR_INVALIDCALL;
  if (cursor) DestroyCursor(cursor);
  cursor = c;
  if (cursorShown) SetCursor(cursor);
  return D3D_OK;
}
void STDMETHODCALLTYPE VDevice::SetCursorPosition(int x, int y, DWORD) {
  POINT p; if (GetCursorPos(&p) && p.x == x && p.y == y) return;
  SetCursorPos(x, y);
}
BOOL STDMETHODCALLTYPE VDevice::ShowCursor(BOOL show) {
  BOOL prev = cursorShown; cursorShown = show;
  if (cursor) SetCursor(show ? cursor : nullptr);
  return prev;
}

// ---- swapchains
HRESULT STDMETHODCALLTYPE VDevice::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* ppIn, IDirect3DSwapChain9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  if (!ppIn) return D3DERR_INVALIDCALL;
  Guard g;
  if (!ppIn->Windowed || !implicitSc || !implicitSc->pp.Windowed) return D3DERR_INVALIDCALL;
  HRESULT valid = validatePP(*ppIn, isEx, nullptr, false);
  if (FAILED(valid)) return valid;
  if (TestCooperativeLevel() != D3D_OK) return D3DERR_DEVICELOST;
  D3DPRESENT_PARAMETERS pp = *ppIn; sanitizePP(pp, cp.hFocusWindow);
  PCreateSwapchain c = {}; c.devId = id; c.pp = toWire(pp); c.pp.windowed = 1; c.scId = newId();
  uint32_t n = pp.BackBufferCount > 4 ? 4 : pp.BackBufferCount;
  for (uint32_t i = 0; i < n; i++) c.bbIds[i] = newId();
  RCreateDevice resp = {}; uint32_t got = 0;
  HRESULT hr = call(OP_CREATE_SWAPCHAIN, &c, sizeof c, &resp, sizeof resp, &got);
  if (FAILED(hr)) { freeId(c.scId); for (uint32_t i = 0; i < n; i++) freeId(c.bbIds[i]); return hr; }
  VSwapChain* sc = new VSwapChain();
  sc->kind = K_SWAPCHAIN; sc->dev = this; sc->id = c.scId; sc->pp = pp;
  sc->pp.BackBufferWidth = resp.pp.w; sc->pp.BackBufferHeight = resp.pp.h;
  sc->pp.BackBufferCount = resp.pp.count; sc->pp.BackBufferFormat = (D3DFORMAT) resp.pp.fmt; sc->fullscreen = false;
  for (uint32_t i = 0; i < n; i++) {
    VSurface* b = makeStandaloneSurface(this, c.bbIds[i], resp.pp.w, resp.pp.h, (D3DFORMAT) resp.pp.fmt, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET,
                                        (D3DMULTISAMPLE_TYPE) resp.pp.ms, resp.pp.msq, (pp.Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0);
    b->container = sc; b->containerIface = static_cast<IDirect3DSwapChain9Ex*>(sc); sc->bb.push_back(b);
  }
  extraSc.push_back(sc);
  presenterRegister(sc->id, pp.hDeviceWindow);
  presenterSetWindow(sc->id, pp.hDeviceWindow, false, nullptr, nullptr);
  sc->addRef(); *out = sc; *ppIn = sc->pp;
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetSwapChain(UINT i, IDirect3DSwapChain9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g;
  VSwapChain* sc = i == 0 ? implicitSc : (i - 1 < extraSc.size() ? extraSc[i - 1] : nullptr);
  *out = sc; if (!sc) return D3DERR_INVALIDCALL; sc->AddRef(); return D3D_OK;
}
UINT STDMETHODCALLTYPE VDevice::GetNumberOfSwapChains() { return 1 + (UINT) extraSc.size(); }
HRESULT STDMETHODCALLTYPE VDevice::GetBackBuffer(UINT sc, UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  IDirect3DSwapChain9* s = nullptr; HRESULT hr = GetSwapChain(sc, &s); if (FAILED(hr)) { *out = nullptr; return hr; }
  hr = s->GetBackBuffer(i, t, out); s->Release(); return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::GetRasterStatus(UINT sc, D3DRASTER_STATUS* rs) {
  IDirect3DSwapChain9* s = nullptr; HRESULT hr = GetSwapChain(sc, &s); if (FAILED(hr)) return hr;
  hr = s->GetRasterStatus(rs); s->Release(); return hr;
}
void STDMETHODCALLTYPE VDevice::SetGammaRamp(UINT sc, DWORD, const D3DGAMMARAMP* ramp) {
  if (!ramp) return; Guard g;
  VSwapChain* s = sc == 0 ? implicitSc : (sc - 1 < extraSc.size() ? extraSc[sc - 1] : nullptr);
  if (!s) return;
  s->gamma = *ramp; s->gammaSet = true; presenterSetGamma(s->id, ramp);
}
void STDMETHODCALLTYPE VDevice::GetGammaRamp(UINT sc, D3DGAMMARAMP* ramp) {
  if (!ramp) return; Guard g;
  VSwapChain* s = sc == 0 ? implicitSc : (sc - 1 < extraSc.size() ? extraSc[sc - 1] : nullptr);
  if (s && s->gammaSet) { *ramp = s->gamma; return; }
  for (int i = 0; i < 256; i++) ramp->red[i] = ramp->green[i] = ramp->blue[i] = (WORD) (i * 257);
}

// ================================================================ resource creation
static HRESULT checkFormat(VDevice* d, DWORD usage, D3DRESOURCETYPE rt, D3DFORMAT f) {
  D3DDISPLAYMODE m = {}; d->parent->GetAdapterDisplayMode(d->cp.AdapterOrdinal, &m);
  D3DFORMAT af = m.Format ? m.Format : D3DFMT_X8R8G8B8;
  DWORD u = usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC | D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_DMAP);
  return d->parent->CheckDeviceFormat(d->cp.AdapterOrdinal, d->cp.DeviceType, af, u & ~D3DUSAGE_AUTOGENMIPMAP, rt, f);
}

template<class T> static HRESULT makeTexture(VDevice* d, T* t, uint32_t kind, UINT w, UINT h, UINT depth, UINT faces, UINT levels, DWORD usage,
                                              D3DFORMAT fmt, D3DPOOL pool, uint16_t op, D3DRESOURCETYPE rt) {
  if (!w || !h || !depth) return D3DERR_INVALIDCALL;
  if (d->isEx && pool == D3DPOOL_MANAGED) return D3DERR_INVALIDCALL;
  if ((usage & D3DUSAGE_AUTOGENMIPMAP) && levels > 1) return D3DERR_INVALIDCALL;
  if (pool != D3DPOOL_SCRATCH && FAILED(checkFormat(d, usage, rt, fmt))) return D3DERR_INVALIDCALL;
  uint32_t maxLevels = mipLevels(w, h, depth);
  if ((usage & D3DUSAGE_AUTOGENMIPMAP)) levels = maxLevels;
  else if (levels == 0 || levels > maxLevels) levels = maxLevels;
  t->kind = kind; t->dev = d; t->fmt = fmt; t->pool = pool; t->usage = usage; t->levels = levels; t->faces = faces;
  if (pool != D3DPOOL_SCRATCH) t->id = newId();
  initTexture(t, w, h, depth);
  if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) t->markGpuWritten();
  HRESULT result = D3D_OK;
  if (t->id) {
    // Creation must report the real backend HRESULT so the game can recover
    // from allocation/parameter failures. Draw and state commands stay async.
    PCreateTex r = {}; r.devId = d->id; r.id = t->id; r.w = w; r.h = h; r.d = depth;
    r.levels = (usage & D3DUSAGE_AUTOGENMIPMAP) ? 0 : levels; r.usage = usage; r.fmt = fmt; r.pool = pool;
    result = call(op, &r, sizeof r);
    if (FAILED(result)) { freeId(t->id); t->id = 0; return result; }
  }
  t->addRef();
  return result;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE* shared) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  if (shared && pool != D3DPOOL_SYSTEMMEM) return D3DERR_INVALIDCALL;
  Guard g; VTexture* t = new VTexture();
  HRESULT hr = makeTexture(this, t, K_TEXTURE, w, h, 1, 1, levels, usage, fmt, pool, OP_CREATE_TEXTURE, D3DRTYPE_TEXTURE);
  if (FAILED(hr)) { delete t; return hr; }
  *out = t; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture9** out, HANDLE*) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VVolumeTexture* t = new VVolumeTexture();
  HRESULT hr = makeTexture(this, t, K_VOLTEX, w, h, d, 1, levels, usage, fmt, pool, OP_CREATE_VOLUME_TEXTURE, D3DRTYPE_VOLUMETEXTURE);
  if (FAILED(hr)) { delete t; return hr; }
  *out = t; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateCubeTexture(UINT e, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9** out, HANDLE*) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VCubeTexture* t = new VCubeTexture();
  HRESULT hr = makeTexture(this, t, K_CUBE, e, e, 1, 6, levels, usage, fmt, pool, OP_CREATE_CUBE_TEXTURE, D3DRTYPE_CUBETEXTURE);
  if (FAILED(hr)) { delete t; return hr; }
  *out = t; return hr;
}
template<class T> static HRESULT makeBuffer(VDevice* d, T* b, uint32_t kind, UINT len, DWORD usage, DWORD fvfOrFmt, D3DPOOL pool, uint16_t op) {
  if (!len) return D3DERR_INVALIDCALL;
  if (d->isEx && pool == D3DPOOL_MANAGED) return D3DERR_INVALIDCALL;
  b->kind = kind; b->dev = d; b->length = len; b->usage = usage; b->pool = pool; b->fvfOrFmt = fvfOrFmt;
  b->shadowed = (usage & D3DUSAGE_DYNAMIC) || pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_SCRATCH;
  if (b->shadowed) b->shadow.assign(len, 0);
  HRESULT result = D3D_OK;
  if (pool != D3DPOOL_SCRATCH) {
    b->id = newId();
    PCreateBuf r = {}; r.devId = d->id; r.id = b->id; r.length = len; r.usage = usage; r.fvfOrFmt = fvfOrFmt; r.pool = pool;
    result = call(op, &r, sizeof r);
    if (FAILED(result)) { freeId(b->id); b->id = 0; return result; }
  }
  b->addRef();
  return result;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE*) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VVertexBuffer* b = new VVertexBuffer();
  HRESULT hr = makeBuffer(this, b, K_VB, len, usage, fvf, pool, OP_CREATE_VB); if (FAILED(hr)) { delete b; return hr; }
  *out = b; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE*) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  if (fmt != D3DFMT_INDEX16 && fmt != D3DFMT_INDEX32) return D3DERR_INVALIDCALL;
  Guard g; VIndexBuffer* b = new VIndexBuffer();
  HRESULT hr = makeBuffer(this, b, K_IB, len, usage, fmt, pool, OP_CREATE_IB); if (FAILED(hr)) { delete b; return hr; }
  *out = b; return hr;
}
static HRESULT makeSurface(VDevice* d, uint16_t op, UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL flag, D3DPOOL pool, DWORD usage, bool lockable, IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  if (!w || !h) return D3DERR_INVALIDCALL;
  Guard g;
  if (pool != D3DPOOL_SCRATCH && op != OP_CREATE_OFFSCREEN && FAILED(checkFormat(d, usage, D3DRTYPE_SURFACE, f))) return D3DERR_INVALIDCALL;
  uint32_t id = pool == D3DPOOL_SCRATCH ? 0 : newId();
  VSurface* s = makeStandaloneSurface(d, id, w, h, f, pool, usage, ms, msq, lockable);
  if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) s->own.gpuWritten = true;
  HRESULT result = D3D_OK;
  if (id) {
    PCreateSurf r = {}; r.devId = d->id; r.id = id; r.w = w; r.h = h; r.fmt = f; r.ms = ms; r.msq = msq; r.flag = flag; r.usage = usage; r.pool = pool;
    result = call(op, &r, sizeof r);
    if (FAILED(result)) { freeId(id); s->id = 0; delete s; return result; }
  }
  s->addRef(); *out = s; return result;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateRenderTarget(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL lockable, IDirect3DSurface9** out, HANDLE*) {
  return makeSurface(this, OP_CREATE_RT, w, h, f, ms, msq, lockable, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET, lockable && ms == D3DMULTISAMPLE_NONE, out);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateRenderTargetEx(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL lockable, IDirect3DSurface9** out, HANDLE* sh, DWORD) {
  return CreateRenderTarget(w, h, f, ms, msq, lockable, out, sh);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL discard, IDirect3DSurface9** out, HANDLE*) {
  return makeSurface(this, OP_CREATE_DS, w, h, f, ms, msq, discard, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, isLockableDepth(f), out);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateDepthStencilSurfaceEx(UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL discard, IDirect3DSurface9** out, HANDLE* sh, DWORD) {
  return CreateDepthStencilSurface(w, h, f, ms, msq, discard, out, sh);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT f, D3DPOOL pool, IDirect3DSurface9** out, HANDLE*) {
  if (pool == D3DPOOL_MANAGED) return D3DERR_INVALIDCALL;
  return makeSurface(this, OP_CREATE_OFFSCREEN, w, h, f, D3DMULTISAMPLE_NONE, 0, FALSE, pool, 0, true, out);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateOffscreenPlainSurfaceEx(UINT w, UINT h, D3DFORMAT f, D3DPOOL pool, IDirect3DSurface9** out, HANDLE* sh, DWORD) {
  return CreateOffscreenPlainSurface(w, h, f, pool, out, sh);
}

// ================================================================ copies between resources
HRESULT STDMETHODCALLTYPE VDevice::UpdateSurface(IDirect3DSurface9* srcIf, const RECT* sr, IDirect3DSurface9* dstIf, const POINT* pt) {
  Guard g; VSurface* s = asSurface(srcIf); VSurface* d = asSurface(dstIf);
  if (!s || !d || s->img->pool != D3DPOOL_SYSTEMMEM || d->img->pool != D3DPOOL_DEFAULT) return D3DERR_INVALIDCALL;
  VcrSub sa = subAddr(s), da = subAddr(d);   // register subresources before this record
  auto* r = rec<PUpdateSurface>(OP_UPDATE_SURFACE); r->devId = id; r->src = sa; r->dst = da;
  r->hasSrcRect = sr != nullptr; if (sr) { r->sl = sr->left; r->st = sr->top; r->sr = sr->right; r->sb = sr->bottom; }
  r->hasPt = pt != nullptr; if (pt) { r->px = pt->x; r->py = pt->y; }
  d->img->gpuWritten = true; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::UpdateTexture(IDirect3DBaseTexture9* srcIf, IDirect3DBaseTexture9* dstIf) {
  Guard g; VBaseTexture* s = asTexture(srcIf); VBaseTexture* d = asTexture(dstIf);
  if (!s || !d || s->pool != D3DPOOL_SYSTEMMEM || d->pool != D3DPOOL_DEFAULT || s->kind != d->kind) return D3DERR_INVALIDCALL;
  auto* r = rec<PUpdateTexture>(OP_UPDATE_TEXTURE); r->devId = id; r->src = s->id; r->dst = d->id;
  d->markGpuWritten(); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetRenderTargetData(IDirect3DSurface9* rtIf, IDirect3DSurface9* dstIf) {
  Guard g; VSurface* s = asSurface(rtIf); VSurface* d = asSurface(dstIf);
  if (!s || !d || d->img->pool != D3DPOOL_SYSTEMMEM) return D3DERR_INVALIDCALL;
  PGetRTData q = {}; q.devId = id; q.rt = subAddr(s); q.dst = subAddr(d);
  HRESULT hr = call(OP_GET_RT_DATA, &q, sizeof q);
  if (FAILED(hr)) return hr;
  ImgSub& i = *d->img;   // the destination lives in this process too: refresh it
  if (i.resident && i.addr.id) readImg(i, i.mem.data(), i.pitch(), i.slicePitch(), 0, 0, i.w, i.h, 0, 1, true);
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetFrontBufferData(UINT sc, IDirect3DSurface9* dst) {
  IDirect3DSwapChain9* s = nullptr; HRESULT hr = GetSwapChain(sc, &s); if (FAILED(hr)) return hr;
  hr = s->GetFrontBufferData(dst); s->Release(); return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::StretchRect(IDirect3DSurface9* srcIf, const RECT* sr, IDirect3DSurface9* dstIf, const RECT* dr, D3DTEXTUREFILTERTYPE f) {
  Guard g; VSurface* s = asSurface(srcIf); VSurface* d = asSurface(dstIf);
  if (!s || !d) return D3DERR_INVALIDCALL;
  VcrSub sa = subAddr(s), da = subAddr(d);
  auto* r = rec<PStretchRect>(OP_STRETCH_RECT); r->devId = id; r->src = sa; r->dst = da; r->filter = f;
  r->hasSrc = sr != nullptr; if (sr) { r->sl = sr->left; r->st = sr->top; r->sr = sr->right; r->sb = sr->bottom; }
  r->hasDst = dr != nullptr; if (dr) { r->dl = dr->left; r->dt = dr->top; r->dr = dr->right; r->db = dr->bottom; }
  d->img->gpuWritten = true; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::ColorFill(IDirect3DSurface9* sIf, const RECT* rc, D3DCOLOR c) {
  Guard g; VSurface* s = asSurface(sIf); if (!s) return D3DERR_INVALIDCALL;
  VcrSub da = subAddr(s);
  auto* r = rec<PColorFill>(OP_COLOR_FILL); r->devId = id; r->dst = da; r->color = c;
  r->hasRect = rc != nullptr; if (rc) { r->l = rc->left; r->t = rc->top; r->r = rc->right; r->b = rc->bottom; }
  s->img->gpuWritten = true; return D3D_OK;
}

// ================================================================ render targets and scene
HRESULT STDMETHODCALLTYPE VDevice::SetRenderTarget(DWORD i, IDirect3DSurface9* sIf) {
  if (i >= 4) return D3DERR_INVALIDCALL;
  Guard g;
  VSurface* s = asSurface(sIf);
  if (i == 0 && !s) return D3DERR_INVALIDCALL;
  if (sIf && !s) return D3DERR_INVALIDCALL;
  if (s && !(s->img->usage & D3DUSAGE_RENDERTARGET)) return D3DERR_INVALIDCALL;
  setRef<VBase>(rt[i], s);
  if (s) s->img->gpuWritten = true;
  if (i == 0) {
    uint32_t w = s->img->w, h = s->img->h;
    st.vp = {0, 0, w, h, 0.f, 1.f}; st.scissor = {0, 0, (LONG) w, (LONG) h};
  }
  VcrSub sa = s ? subAddr(s) : VcrSub{0, K_SURFACE, 0, 0};
  auto* r = rec<PSetRT>(OP_SET_RT); r->devId = id; r->index = i; r->surf = sa;
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetRenderTarget(DWORD i, IDirect3DSurface9** out) {
  if (!out || i >= 4) return D3DERR_INVALIDCALL;
  Guard g; VSurface* s = static_cast<VSurface*>(rt[i]);
  *out = s; if (!s) return D3DERR_NOTFOUND; s->AddRef(); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetDepthStencilSurface(IDirect3DSurface9* sIf) {
  Guard g; VSurface* s = asSurface(sIf);
  if (sIf && !s) return D3DERR_INVALIDCALL;
  setRef<VBase>(ds, s);
  if (s) s->img->gpuWritten = true;
  VcrSub sa = s ? subAddr(s) : VcrSub{0, K_SURFACE, 0, 0};
  auto* r = rec<PSetDS>(OP_SET_DS); r->devId = id; r->surf = sa;
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetDepthStencilSurface(IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  Guard g; VSurface* s = static_cast<VSurface*>(ds);
  *out = s; if (!s) return D3DERR_NOTFOUND; s->AddRef(); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::BeginScene() {
  Guard g; if (inScene) return D3DERR_INVALIDCALL; inScene = true;
  auto* r = rec<PGeneric>(OP_BEGIN_SCENE); r->devId = id; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::EndScene() {
  Guard g; if (!inScene) return D3DERR_INVALIDCALL; inScene = false;
  auto* r = rec<PGeneric>(OP_END_SCENE); r->devId = id;
  flush();   // give the host the frame's work as early as possible
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::Clear(DWORD n, const D3DRECT* rects, DWORD flags, D3DCOLOR c, float z, DWORD s) {
  Guard g;
  if (n && !rects) n = 0;
  if (n > 4096) n = 4096;
  auto* r = rec<PClear>(OP_CLEAR, n * sizeof(D3DRECT)); r->devId = id; r->count = n; r->flags = flags; r->color = c; r->z = z; r->stencil = s;
  if (n) memcpy(r + 1, rects, n * sizeof(D3DRECT));
  return D3D_OK;
}

// ================================================================ fixed-function state
HRESULT STDMETHODCALLTYPE VDevice::SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) {
  if (!m || (UINT) t >= kXforms) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.xf[t] = *m; if (mk) { mk->f.set(SB_XF); mk->xf.set(t); }
  auto* r = rec<PSetTransform>(OP_SET_TRANSFORM); r->devId = id; r->state = t; memcpy(r->m, m, 64); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) {
  if (!m || (UINT) t >= kXforms) return D3DERR_INVALIDCALL; Guard g; *m = st.xf[t]; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) {
  if (!m || (UINT) t >= kXforms) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk);
  D3DMATRIX a = *m, b = ts.xf[t], o;
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float v = 0; for (int k = 0; k < 4; k++) v += a.m[i][k] * b.m[k][j]; o.m[i][j] = v; }
  ts.xf[t] = o; if (mk) { mk->f.set(SB_XF); mk->xf.set(t); }
  auto* r = rec<PSetTransform>(OP_MULTIPLY_TRANSFORM); r->devId = id; r->state = t; memcpy(r->m, m, 64); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetViewport(const D3DVIEWPORT9* v) {
  if (!v) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.vp = *v; if (mk) mk->f.set(SB_VIEWPORT);
  if (!mk) liftViewport(ts.vp);   // DXVK on the device: MaxZ = MinZ + 0.001 when MinZ >= MaxZ (a block records it raw)
  auto* r = rec<PViewport>(OP_SET_VIEWPORT); r->devId = id; r->x = v->X; r->y = v->Y; r->w = v->Width; r->h = v->Height; r->mn = v->MinZ; r->mx = v->MaxZ; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetViewport(D3DVIEWPORT9* v) { if (!v) return D3DERR_INVALIDCALL; Guard g; *v = st.vp; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetMaterial(const D3DMATERIAL9* m) {
  if (!m) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.mat = *m; if (mk) mk->f.set(SB_MATERIAL);
  auto* r = rec<PMaterial>(OP_SET_MATERIAL); r->devId = id; memcpy(r->v, m, sizeof(D3DMATERIAL9)); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetMaterial(D3DMATERIAL9* m) { if (!m) return D3DERR_INVALIDCALL; Guard g; *m = st.mat; return D3D_OK; }
static D3DLIGHT9 defaultLight() { D3DLIGHT9 l = {}; l.Type = D3DLIGHT_DIRECTIONAL; l.Diffuse = {1, 1, 1, 0}; l.Direction = {0, 0, 1}; return l; }
HRESULT STDMETHODCALLTYPE VDevice::SetLight(DWORD i, const D3DLIGHT9* l) {
  if (!l || i > 65535) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk);
  if (i >= ts.lights.size()) ts.lights.resize(i + 1);
  ts.lights[i].valid = true; ts.lights[i].light = *l; if (mk) mk->f.set(SB_LIGHTS);
  auto* r = rec<PLight>(OP_SET_LIGHT); r->devId = id; r->index = i; r->type = l->Type; memcpy(r->v, &l->Diffuse, sizeof(float) * 25); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetLight(DWORD i, D3DLIGHT9* l) {
  if (!l) return D3DERR_INVALIDCALL; Guard g;
  if (i >= st.lights.size() || !st.lights[i].valid) return D3DERR_INVALIDCALL;
  *l = st.lights[i].light; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::LightEnable(DWORD i, BOOL e) {
  if (i > 65535) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk);
  if (i >= ts.lights.size()) ts.lights.resize(i + 1);
  if (!ts.lights[i].valid) { ts.lights[i].valid = true; ts.lights[i].light = defaultLight(); }
  ts.lights[i].enabled = e != FALSE;
  if (mk) { mk->f.set(SB_LIGHTS); if (i >= mk->lightEnable.size()) mk->lightEnable.resize(i + 1); mk->lightEnable[i] = true; }
  auto* r = rec<PLightEnable>(OP_LIGHT_ENABLE); r->devId = id; r->index = i; r->enable = e; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetLightEnable(DWORD i, BOOL* e) {
  if (!e) return D3DERR_INVALIDCALL; Guard g;
  if (i >= st.lights.size() || !st.lights[i].valid) return D3DERR_INVALIDCALL;
  *e = st.lights[i].enabled ? 128 : 0; return D3D_OK;   // D3D9 reports 128 for enabled lights
}
HRESULT STDMETHODCALLTYPE VDevice::SetClipPlane(DWORD i, const float* p) {
  if (!p || i >= 6) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); memcpy(ts.clip[i], p, 16); if (mk) { mk->f.set(SB_CLIP); mk->clip.set(i); }
  auto* r = rec<PClipPlane>(OP_SET_CLIP_PLANE); r->devId = id; r->index = i; memcpy(r->p, p, 16); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetClipPlane(DWORD i, float* p) { if (!p || i >= 6) return D3DERR_INVALIDCALL; Guard g; memcpy(p, st.clip[i], 16); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetRenderState(D3DRENDERSTATETYPE s, DWORD v) {
  if ((UINT) s >= 256) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.rs[s] = v; if (mk) { mk->f.set(SB_RS); mk->rs.set(s); }
  auto* r = rec<PRS>(OP_SET_RS); r->devId = id; r->state = s; r->value = v; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) {
  if (!v) return D3DERR_INVALIDCALL; if ((UINT) s >= 256) return D3DERR_INVALIDCALL; Guard g; *v = st.rs[s]; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetClipStatus(const D3DCLIPSTATUS9* s) { if (!s) return D3DERR_INVALIDCALL; clipStatus = *s; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetClipStatus(D3DCLIPSTATUS9* s) { if (!s) return D3DERR_INVALIDCALL; *s = clipStatus; return D3D_OK; }

// ================================================================ textures and samplers
HRESULT STDMETHODCALLTYPE VDevice::GetTexture(DWORD stage, IDirect3DBaseTexture9** out) {
  if (!out) return D3DERR_INVALIDCALL; int i = samplerIndex(stage); if (i < 0) return D3DERR_INVALIDCALL;
  Guard g; IDirect3DBaseTexture9* t = texIface(st.tex[i]); *out = t; if (t) t->AddRef(); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetTexture(DWORD stage, IDirect3DBaseTexture9* tIf) {
  int i = samplerIndex(stage); if (i < 0) return D3DERR_INVALIDCALL;
  Guard g; VBaseTexture* t = asTexture(tIf); if (tIf && !t) return D3DERR_INVALIDCALL;
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.tex[i], t); if (mk) { mk->f.set(SB_TEX); mk->tex.set(i); }
  auto* r = rec<PSetTexture>(OP_SET_TEXTURE); r->devId = id; r->stage = stage; r->id = t ? t->id : 0; r->kind = t ? t->kind : 0; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD* v) {
  if (!v) return D3DERR_INVALIDCALL; if (stage >= kStages || (UINT) t >= kStageStates) return D3DERR_INVALIDCALL;
  Guard g; *v = st.tss[stage][t]; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v) {
  if (stage >= kStages || (UINT) t >= kStageStates) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.tss[stage][t] = v; if (mk) { mk->f.set(SB_TSS); mk->stages.set(stage); mk->stageStates[stage].set(t); }
  auto* r = rec<PTSS>(OP_SET_TSS); r->devId = id; r->stage = stage; r->type = t; r->value = v; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) {
  if (!v) return D3DERR_INVALIDCALL; int i = samplerIndex(s); if (i < 0 || (UINT) t >= kSamplerStates) return D3DERR_INVALIDCALL;
  Guard g; *v = st.ss[i][t]; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) {
  int i = samplerIndex(s); if (i < 0 || (UINT) t >= kSamplerStates) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.ss[i][t] = v; if (mk) { mk->f.set(SB_SS); mk->samplers.set(i); mk->samplerStates[i].set(t); }
  auto* r = rec<PSS>(OP_SET_SS); r->devId = id; r->sampler = s; r->type = t; r->value = v; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::ValidateDevice(DWORD* passes) {
  Guard g; PGeneric q = {id}; uint32_t n = 0; HRESULT hr = call(OP_VALIDATE, &q, sizeof q, &n, 4); if (passes) *passes = n; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::SetPaletteEntries(UINT n, const PALETTEENTRY* e) {
  if (!e || n > 0xFFFF) return D3DERR_INVALIDCALL;
  Guard g; palettes[n].assign(e, e + 256);
  auto* r = rec<PPalette>(OP_SET_PALETTE); r->devId = id; r->number = n; memcpy(r->entries, e, 1024); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetPaletteEntries(UINT n, PALETTEENTRY* e) {
  if (!e) return D3DERR_INVALIDCALL; Guard g; auto it = palettes.find(n); if (it == palettes.end()) return D3DERR_INVALIDCALL;
  memcpy(e, it->second.data(), 1024); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetCurrentTexturePalette(UINT n) { Guard g; curPalette = n; auto* r = rec<PGeneric>(OP_SET_CUR_PALETTE); r->devId = id; r->a = n; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetCurrentTexturePalette(UINT* n) { if (!n) return D3DERR_INVALIDCALL; *n = curPalette; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetScissorRect(const RECT* rc) {
  if (!rc) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.scissor = *rc; if (mk) mk->f.set(SB_SCISSOR);
  auto* r = rec<PScissor>(OP_SET_SCISSOR); r->devId = id; r->l = rc->left; r->t = rc->top; r->r = rc->right; r->b = rc->bottom; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetScissorRect(RECT* rc) { if (!rc) return D3DERR_INVALIDCALL; Guard g; *rc = st.scissor; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetSoftwareVertexProcessing(BOOL b) {
  Guard g; swvp = b; auto* r = rec<PGeneric>(OP_SET_SWVP); r->devId = id; r->a = b; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetNPatchMode(float n) {
  Guard g; npatch = n; auto* r = rec<PGeneric>(OP_SET_NPATCH); r->devId = id; memcpy(&r->a, &n, 4); return D3D_OK;
}

// ================================================================ draws
HRESULT STDMETHODCALLTYPE VDevice::DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count) {
  Guard g; if (!st.decl) return D3DERR_INVALIDCALL; if (!count) return D3D_OK;
  auto* r = rec<PDraw>(OP_DRAW); r->devId = id; r->prim = t; r->a = start; r->b = count; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT minIdx, UINT numV, UINT startIdx, UINT count) {
  Guard g; if (!st.decl) return D3DERR_INVALIDCALL; if (!count) return D3D_OK;
  auto* r = rec<PDraw>(OP_DRAW_INDEXED); r->devId = id; r->prim = t; r->a = (uint32_t) base; r->b = minIdx; r->c = numV; r->d = startIdx; r->e = count; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT count, const void* data, UINT stride) {
  Guard g;
  if (!stride || !data) return D3DERR_INVALIDCALL;
  if (!st.decl) return D3DERR_INVALIDCALL;
  if (!count) return D3D_OK;
  uint32_t bytes = vertexCount(t, count) * stride;
  if (bytes + 64 > maxInline()) { log("DrawPrimitiveUP with %u bytes of vertices is larger than one record; skipped", bytes); }
  else {
    auto* r = rec<PDrawUP>(OP_DRAW_UP, bytes); r->devId = id; r->prim = t; r->count = count; r->stride = stride; r->vbytes = bytes;
    memcpy(r + 1, data, bytes);
  }
  setRef<VBase>(st.streams[0].vb, nullptr); st.streams[0].off = 0; st.streams[0].stride = 0;   // as DXVK/D3D9
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT minIdx, UINT numV, UINT count, const void* idx, D3DFORMAT fmt, const void* data, UINT stride) {
  Guard g;
  if (!stride || !data || !idx) return D3DERR_INVALIDCALL;
  if (!st.decl) return D3DERR_INVALIDCALL;
  if (!count) return D3D_OK;
  uint32_t ib = vertexCount(t, count) * (fmt == D3DFMT_INDEX32 ? 4 : 2), ibAligned = (ib + 7) & ~7u;
  uint32_t vb = (minIdx + numV) * stride;
  if (ibAligned + vb + 64 > maxInline()) { log("DrawIndexedPrimitiveUP with %u bytes is larger than one record; skipped", ibAligned + vb); }
  else {
    auto* r = rec<PDrawIndexedUP>(OP_DRAW_INDEXED_UP, ibAligned + vb);
    r->devId = id; r->prim = t; r->minIdx = minIdx; r->numV = numV; r->count = count; r->idxFmt = fmt; r->stride = stride; r->ibytes = ib; r->vbytes = vb;
    memcpy(r + 1, idx, ib); memcpy((uint8_t*) (r + 1) + ibAligned, data, vb);
  }
  setRef<VBase>(st.streams[0].vb, nullptr); st.streams[0].off = 0; st.streams[0].stride = 0;
  setRef<VBase>(st.ib, nullptr);
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::ProcessVertices(UINT src, UINT dst, UINT count, IDirect3DVertexBuffer9* vb, IDirect3DVertexDeclaration9* decl, DWORD flags) {
  Guard g; VVertexBuffer* b = static_cast<VVertexBuffer*>(vb); if (!b || static_cast<VBase*>(b)->magic != 0x4F524356) return D3DERR_INVALIDCALL;
  VDecl* d = decl ? static_cast<VDecl*>(decl) : nullptr;
  if (d) { HRESULT hr = d->ensureHost(); if (FAILED(hr)) return hr; }
  auto* r = rec<PProcessVertices>(OP_PROCESS_VERTICES); r->devId = id; r->src = src; r->dst = dst; r->count = count; r->destVB = b->id; r->decl = d ? d->id : 0; r->flags = flags;
  return D3D_OK;
}

// ================================================================ declarations and shaders
HRESULT STDMETHODCALLTYPE VDevice::CreateVertexDeclaration(const D3DVERTEXELEMENT9* el, IDirect3DVertexDeclaration9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  if (!el) return D3DERR_INVALIDCALL;
  Guard g;
  VDecl* d = new VDecl(); d->kind = K_DECL; d->dev = this;
  for (uint32_t i = 0; i < 65; i++) { d->el.push_back(el[i]); if (el[i].Stream == 0xFF) break; }
  if (d->el.back().Stream != 0xFF) { delete d; return D3DERR_INVALIDCALL; }
  d->fvf = declToFvf(d->el);
  HRESULT hr = d->ensureHost();
  if (FAILED(hr)) { delete d; return hr; }
  d->addRef(); *out = d; return hr;
}
VDecl* VDevice::declForFvf(DWORD fvf) {
  auto it = fvfDecls.find(fvf);
  if (it != fvfDecls.end()) return it->second;
  VDecl* d = new VDecl(); d->kind = K_DECL; d->dev = this; d->el = fvfToDecl(fvf); d->fvf = fvf; d->ownedByDevice = true;
  fvfDecls[fvf] = d; return d;
}
HRESULT STDMETHODCALLTYPE VDevice::SetVertexDeclaration(IDirect3DVertexDeclaration9* dIf) {
  Guard g; VDecl* d = dIf ? static_cast<VDecl*>(dIf) : nullptr;
  if (d && static_cast<VBase*>(d)->magic != 0x4F524356) return D3DERR_INVALIDCALL;
  if (d) { HRESULT hr = d->ensureHost(); if (FAILED(hr)) return hr; }
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.decl, d); if (mk) mk->f.set(SB_DECL);
  auto* r = rec<PSetObj>(OP_SET_DECL); r->devId = id; r->id = d ? d->id : 0; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetVertexDeclaration(IDirect3DVertexDeclaration9** out) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VDecl* d = static_cast<VDecl*>(st.decl);
  if (d) { HRESULT hr = d->ensureHost(); if (FAILED(hr)) return hr; d->AddRef(); }
  *out = d; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetFVF(DWORD fvf) {
  if (fvf == 0) return D3D_OK;   // DXVK ignores FVF 0
  Guard g; VDecl* d = declForFvf(fvf);
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.decl, d); if (mk) mk->f.set(SB_DECL);
  auto* r = rec<PSetObj>(OP_SET_FVF); r->devId = id; r->id = fvf; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetFVF(DWORD* fvf) { if (!fvf) return D3DERR_INVALIDCALL; Guard g; *fvf = st.decl ? static_cast<VDecl*>(st.decl)->fvf : 0; return D3D_OK; }

template<class T> static HRESULT makeShader(VDevice* d, const DWORD* code, uint16_t op, uint32_t kind, T** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  if (!code) return D3DERR_INVALIDCALL;
  uint32_t bytes = shaderBytes(code); if (!bytes || bytes > (1u << 20)) return D3DERR_INVALIDCALL;
  T* s = new T(); s->kind = kind; s->dev = d; s->code.assign(code, code + bytes / 4); s->id = newId();
  std::vector<uint8_t> payload(sizeof(PCreateShader) + bytes);
  auto* r = (PCreateShader*) payload.data(); r->devId = d->id; r->id = s->id; r->bytes = bytes; memcpy(r + 1, code, bytes);
  HRESULT hr = call(op, r, (uint32_t) payload.size());
  if (FAILED(hr)) { freeId(s->id); s->id = 0; delete s; return hr; }
  s->addRef(); *out = s; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::CreateVertexShader(const DWORD* code, IDirect3DVertexShader9** out) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VVertexShader* s = nullptr; HRESULT hr = makeShader(this, code, OP_CREATE_VS, K_VS, &s); *out = s; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::CreatePixelShader(const DWORD* code, IDirect3DPixelShader9** out) {
  if (!out) return D3DERR_INVALIDCALL; *out = nullptr;
  Guard g; VPixelShader* s = nullptr; HRESULT hr = makeShader(this, code, OP_CREATE_PS, K_PS, &s); *out = s; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::SetVertexShader(IDirect3DVertexShader9* sIf) {
  Guard g; VVertexShader* s = sIf ? static_cast<VVertexShader*>(sIf) : nullptr;
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.vs, s); if (mk) mk->f.set(SB_VS);
  auto* r = rec<PSetObj>(OP_SET_VS); r->devId = id; r->id = s ? s->id : 0; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetVertexShader(IDirect3DVertexShader9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g; VVertexShader* s = static_cast<VVertexShader*>(st.vs); *out = s; if (s) s->AddRef(); return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetPixelShader(IDirect3DPixelShader9* sIf) {
  Guard g; VPixelShader* s = sIf ? static_cast<VPixelShader*>(sIf) : nullptr;
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.ps, s); if (mk) mk->f.set(SB_PS);
  auto* r = rec<PSetObj>(OP_SET_PS); r->devId = id; r->id = s ? s->id : 0; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetPixelShader(IDirect3DPixelShader9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g; VPixelShader* s = static_cast<VPixelShader*>(st.ps); *out = s; if (s) s->AddRef(); return D3D_OK;
}

// shader constants (mirror + forward)
template<class V> static HRESULT setConsts(VDevice* d, uint16_t op, UINT start, const void* data, UINT n, uint32_t limit, uint32_t elem,
                                           V* mirror, std::vector<bool>* maskVec, size_t maskBitsFixed, void (*mark)(SBMask*, UINT)) {
  if (!data || start + n > limit || start + n < start) return D3DERR_INVALIDCALL;
  (void) maskVec; (void) maskBitsFixed;
  memcpy(mirror + (size_t) start * elem, data, (size_t) n * elem * sizeof(V));
  SBMask* mk = d->recording ? &d->recording->m : nullptr;
  if (mk) for (UINT i = 0; i < n; i++) mark(mk, start + i);
  auto* r = rec<PConst>(op, n * elem * (uint32_t) sizeof(V)); r->devId = d->id; r->start = start; r->count = n;
  memcpy(r + 1, data, (size_t) n * elem * sizeof(V));
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetVertexShaderConstantF(UINT r, const float* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<float>(this, OP_VS_CONST_F, r, d, n, vsFloatCount, 4, ts.vsF.data(), nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_VSC); m->vsF[i] = true; });
}
HRESULT STDMETHODCALLTYPE VDevice::GetVertexShaderConstantF(UINT r, float* d, UINT n) { if (!d || r + n > vsFloatCount) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.vsF.data() + (size_t) r * 4, (size_t) n * 16); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetVertexShaderConstantI(UINT r, const int* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<int>(this, OP_VS_CONST_I, r, d, n, vsIntCount, 4, ts.vsI.data(), nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_VSC); m->vsI[i] = true; });
}
HRESULT STDMETHODCALLTYPE VDevice::GetVertexShaderConstantI(UINT r, int* d, UINT n) { if (!d || r + n > vsIntCount) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.vsI.data() + (size_t) r * 4, (size_t) n * 16); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetVertexShaderConstantB(UINT r, const BOOL* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<BOOL>(this, OP_VS_CONST_B, r, d, n, vsBoolCount, 1, ts.vsB.data(), nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_VSC); m->vsB[i] = true; });
}
HRESULT STDMETHODCALLTYPE VDevice::GetVertexShaderConstantB(UINT r, BOOL* d, UINT n) { if (!d || r + n > vsBoolCount) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.vsB.data() + r, (size_t) n * 4); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetPixelShaderConstantF(UINT r, const float* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<float>(this, OP_PS_CONST_F, r, d, n, 224, 4, ts.psF, nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_PSC); m->psF.set(i); });
}
HRESULT STDMETHODCALLTYPE VDevice::GetPixelShaderConstantF(UINT r, float* d, UINT n) { if (!d || r + n > 224) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.psF + (size_t) r * 4, (size_t) n * 16); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetPixelShaderConstantI(UINT r, const int* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<int>(this, OP_PS_CONST_I, r, d, n, 16, 4, ts.psI, nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_PSC); m->psI.set(i); });
}
HRESULT STDMETHODCALLTYPE VDevice::GetPixelShaderConstantI(UINT r, int* d, UINT n) { if (!d || r + n > 16) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.psI + (size_t) r * 4, (size_t) n * 16); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetPixelShaderConstantB(UINT r, const BOOL* d, UINT n) {
  Guard g; SBMask* mk; VState& ts = target(mk);
  return setConsts<BOOL>(this, OP_PS_CONST_B, r, d, n, 16, 1, ts.psB, nullptr, 0, [](SBMask* m, UINT i) { m->f.set(SB_PSC); m->psB.set(i); });
}
HRESULT STDMETHODCALLTYPE VDevice::GetPixelShaderConstantB(UINT r, BOOL* d, UINT n) { if (!d || r + n > 16) return D3DERR_INVALIDCALL; Guard g; memcpy(d, st.psB + r, (size_t) n * 4); return D3D_OK; }

// ================================================================ streams
HRESULT STDMETHODCALLTYPE VDevice::SetStreamSource(UINT i, IDirect3DVertexBuffer9* vbIf, UINT off, UINT stride) {
  if (i >= kStreams) return D3DERR_INVALIDCALL;
  Guard g; VVertexBuffer* vb = vbIf ? static_cast<VVertexBuffer*>(vbIf) : nullptr;
  if (vb && static_cast<VBase*>(vb)->magic != 0x4F524356) return D3DERR_INVALIDCALL;
  SBMask* mk; VState& ts = target(mk);
  if (!mk && !vb) { off = 0; stride = 0; }   // DXVK: a null stream on the device keeps neither offset nor stride (a block records them raw)
  setRef<VBase>(ts.streams[i].vb, vb); ts.streams[i].off = off; ts.streams[i].stride = stride;
  if (mk) { mk->f.set(SB_VB); mk->vbs.set(i); }
  auto* r = rec<PStream>(OP_SET_STREAM); r->devId = id; r->stream = i; r->id = vb ? vb->id : 0; r->offset = off; r->stride = stride; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetStreamSource(UINT i, IDirect3DVertexBuffer9** vb, UINT* off, UINT* stride) {
  if (!vb || !off || !stride || i >= kStreams) return D3DERR_INVALIDCALL;
  Guard g; VVertexBuffer* b = static_cast<VVertexBuffer*>(st.streams[i].vb); *vb = b; if (b) b->AddRef();
  *off = st.streams[i].off; *stride = st.streams[i].stride; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::SetStreamSourceFreq(UINT i, UINT div) {
  if (i >= kStreams) return D3DERR_INVALIDCALL;
  Guard g; SBMask* mk; VState& ts = target(mk); ts.freq[i] = div; if (mk) { mk->f.set(SB_FREQ); mk->freq.set(i); }
  auto* r = rec<PStreamFreq>(OP_SET_STREAM_FREQ); r->devId = id; r->stream = i; r->setting = div; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetStreamSourceFreq(UINT i, UINT* div) { if (!div || i >= kStreams) return D3DERR_INVALIDCALL; Guard g; *div = st.freq[i]; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetIndices(IDirect3DIndexBuffer9* ibIf) {
  Guard g; VIndexBuffer* ib = ibIf ? static_cast<VIndexBuffer*>(ibIf) : nullptr;
  if (ib && static_cast<VBase*>(ib)->magic != 0x4F524356) return D3DERR_INVALIDCALL;
  SBMask* mk; VState& ts = target(mk); setRef<VBase>(ts.ib, ib); if (mk) mk->f.set(SB_INDICES);
  auto* r = rec<PSetObj>(OP_SET_INDICES); r->devId = id; r->id = ib ? ib->id : 0; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VDevice::GetIndices(IDirect3DIndexBuffer9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g; VIndexBuffer* b = static_cast<VIndexBuffer*>(st.ib); *out = b; if (b) b->AddRef(); return D3D_OK;
}

// ================================================================ state blocks (DXVK semantics)
static void maskForType(SBMask& m, D3DSTATEBLOCKTYPE t, VDevice* d) {
  bool px = t == D3DSBT_PIXELSTATE || t == D3DSBT_ALL, vx = t == D3DSBT_VERTEXSTATE || t == D3DSBT_ALL;
  if (px) {
    static const D3DRENDERSTATETYPE prs[] = {D3DRS_ZENABLE, D3DRS_FILLMODE, D3DRS_SHADEMODE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_LASTPIXEL,
      D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_ZFUNC, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_DITHERENABLE, D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY,
      D3DRS_ALPHABLENDENABLE, D3DRS_DEPTHBIAS, D3DRS_STENCILENABLE, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
      D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK, D3DRS_TEXTUREFACTOR, D3DRS_WRAP0, D3DRS_WRAP1, D3DRS_WRAP2, D3DRS_WRAP3,
      D3DRS_WRAP4, D3DRS_WRAP5, D3DRS_WRAP6, D3DRS_WRAP7, D3DRS_WRAP8, D3DRS_WRAP9, D3DRS_WRAP10, D3DRS_WRAP11, D3DRS_WRAP12, D3DRS_WRAP13,
      D3DRS_WRAP14, D3DRS_WRAP15, D3DRS_COLORWRITEENABLE, D3DRS_BLENDOP, D3DRS_SCISSORTESTENABLE, D3DRS_SLOPESCALEDEPTHBIAS,
      D3DRS_ANTIALIASEDLINEENABLE, D3DRS_TWOSIDEDSTENCILMODE, D3DRS_CCW_STENCILFAIL, D3DRS_CCW_STENCILZFAIL, D3DRS_CCW_STENCILPASS,
      D3DRS_CCW_STENCILFUNC, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2, D3DRS_COLORWRITEENABLE3, D3DRS_BLENDFACTOR,
      D3DRS_SRGBWRITEENABLE, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA};
    m.f.set(SB_RS); for (auto r : prs) m.rs.set(r);
    m.f.set(SB_SS);
    for (uint32_t i = 0; i < 17; i++) {   // pixel samplers and the displacement-map sampler (DXVK: FirstVSSamplerSlot = 17)
      m.samplers.set(i);
      for (auto s : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_BORDERCOLOR, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER,
                     D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MAXANISOTROPY, D3DSAMP_SRGBTEXTURE, D3DSAMP_ELEMENTINDEX}) m.samplerStates[i].set(s);
    }
    m.f.set(SB_PS); m.f.set(SB_PSC); m.psF.set(); m.psI.set(); m.psB.set();
    m.f.set(SB_TSS); m.stages.set(); for (auto& s : m.stageStates) s.set();
  }
  if (vx) {
    static const D3DRENDERSTATETYPE vrs[] = {D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_FOGCOLOR, D3DRS_FOGTABLEMODE, D3DRS_FOGSTART, D3DRS_FOGEND,
      D3DRS_FOGDENSITY, D3DRS_RANGEFOGENABLE, D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_FOGVERTEXMODE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_LOCALVIEWER,
      D3DRS_EMISSIVEMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE, D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_SPECULARMATERIALSOURCE, D3DRS_VERTEXBLEND,
      D3DRS_CLIPPLANEENABLE, D3DRS_POINTSIZE, D3DRS_POINTSIZE_MIN, D3DRS_POINTSPRITEENABLE, D3DRS_POINTSCALEENABLE, D3DRS_POINTSCALE_A,
      D3DRS_POINTSCALE_B, D3DRS_POINTSCALE_C, D3DRS_MULTISAMPLEANTIALIAS, D3DRS_MULTISAMPLEMASK, D3DRS_PATCHEDGESTYLE, D3DRS_POINTSIZE_MAX,
      D3DRS_INDEXEDVERTEXBLENDENABLE, D3DRS_TWEENFACTOR, D3DRS_POSITIONDEGREE, D3DRS_NORMALDEGREE, D3DRS_MINTESSELLATIONLEVEL,
      D3DRS_MAXTESSELLATIONLEVEL, D3DRS_ADAPTIVETESS_X, D3DRS_ADAPTIVETESS_Y, D3DRS_ADAPTIVETESS_Z, D3DRS_ADAPTIVETESS_W,
      D3DRS_ENABLEADAPTIVETESSELLATION, D3DRS_NORMALIZENORMALS, D3DRS_SPECULARENABLE, D3DRS_SHADEMODE};
    m.f.set(SB_RS); for (auto r : vrs) m.rs.set(r);
    m.f.set(SB_SS);
    for (uint32_t i = 17; i < kSamplers; i++) { m.samplers.set(i); m.samplerStates[i].set(D3DSAMP_DMAPOFFSET); }
    m.f.set(SB_VS); m.f.set(SB_VSC);
    for (uint32_t i = 0; i < d->vsFloatCount; i++) m.vsF[i] = true;
    for (uint32_t i = 0; i < d->vsIntCount; i++) m.vsI[i] = true;
    for (uint32_t i = 0; i < d->vsBoolCount; i++) m.vsB[i] = true;
    m.f.set(SB_DECL); m.f.set(SB_FREQ); m.freq.set(); m.f.set(SB_LIGHTS);
    m.lightEnable.assign(d->st.lights.size(), true);
  }
  if (t == D3DSBT_ALL) {
    m.f.set(SB_TEX); m.tex.set(); m.f.set(SB_VB); m.vbs.set(); m.f.set(SB_INDICES); m.f.set(SB_VIEWPORT); m.f.set(SB_SCISSOR);
    m.f.set(SB_CLIP); m.clip.set(); m.f.set(SB_XF); m.xf.set(); m.f.set(SB_MATERIAL);
  }
}

void liftViewport(D3DVIEWPORT9& v) { if (!(v.MinZ < v.MaxZ)) v.MaxZ = v.MinZ + 0.001f; }
// copy masked state src -> dst; ignoreOffset follows DXVK (Capture() keeps the block's stream offsets)
static void transfer(const SBMask& m, const VState& src, VState& dst, bool ignoreOffset, VDevice* d, bool toDevice = false) {
  if (m.f[SB_FREQ]) for (uint32_t i = 0; i < kStreams; i++) if (m.freq[i]) dst.freq[i] = src.freq[i];
  if (m.f[SB_INDICES]) setRef<VBase>(dst.ib, src.ib);
  if (m.f[SB_RS]) for (uint32_t i = 0; i < 256; i++) if (m.rs[i]) dst.rs[i] = src.rs[i];
  if (m.f[SB_SS]) for (uint32_t s = 0; s < kSamplers; s++) if (m.samplers[s]) for (uint32_t t = 0; t < kSamplerStates; t++) if (m.samplerStates[s][t]) dst.ss[s][t] = src.ss[s][t];
  if (m.f[SB_VB]) for (uint32_t i = 0; i < kStreams; i++) if (m.vbs[i]) {
    setRef<VBase>(dst.streams[i].vb, src.streams[i].vb); dst.streams[i].stride = src.streams[i].stride;
    if (!ignoreOffset) dst.streams[i].off = src.streams[i].off;
    if (toDevice && !src.streams[i].vb) { dst.streams[i].off = 0; dst.streams[i].stride = 0; }   // Apply goes through the device setter
  }
  if (m.f[SB_MATERIAL]) dst.mat = src.mat;
  if (m.f[SB_TEX]) for (uint32_t i = 0; i < kSamplers; i++) if (m.tex[i]) setRef<VBase>(dst.tex[i], src.tex[i]);
  if (m.f[SB_VS]) setRef<VBase>(dst.vs, src.vs);
  if (m.f[SB_PS]) setRef<VBase>(dst.ps, src.ps);
  if (m.f[SB_XF]) for (uint32_t i = 0; i < kXforms; i++) if (m.xf[i]) dst.xf[i] = src.xf[i];
  if (m.f[SB_TSS]) for (uint32_t s = 0; s < kStages; s++) if (m.stages[s]) for (uint32_t t = 0; t < kStageStates; t++) if (m.stageStates[s][t]) dst.tss[s][t] = src.tss[s][t];
  if (m.f[SB_VIEWPORT]) { dst.vp = src.vp; if (toDevice) liftViewport(dst.vp); }
  if (m.f[SB_SCISSOR]) dst.scissor = src.scissor;
  if (m.f[SB_CLIP]) for (uint32_t i = 0; i < 6; i++) if (m.clip[i]) memcpy(dst.clip[i], src.clip[i], 16);
  if (m.f[SB_VSC]) {
    for (uint32_t i = 0; i < d->vsFloatCount && i < m.vsF.size(); i++) if (m.vsF[i]) memcpy(&dst.vsF[i * 4], &src.vsF[i * 4], 16);
    for (uint32_t i = 0; i < d->vsIntCount && i < m.vsI.size(); i++) if (m.vsI[i]) memcpy(&dst.vsI[i * 4], &src.vsI[i * 4], 16);
    for (uint32_t i = 0; i < d->vsBoolCount && i < m.vsB.size(); i++) if (m.vsB[i]) dst.vsB[i] = src.vsB[i];
  }
  if (m.f[SB_PSC]) {
    for (uint32_t i = 0; i < 224; i++) if (m.psF[i]) memcpy(&dst.psF[i * 4], &src.psF[i * 4], 16);
    for (uint32_t i = 0; i < 16; i++) { if (m.psI[i]) memcpy(&dst.psI[i * 4], &src.psI[i * 4], 16); if (m.psB[i]) dst.psB[i] = src.psB[i]; }
  }
  if (m.f[SB_LIGHTS]) {
    for (size_t i = 0; i < src.lights.size(); i++) if (src.lights[i].valid) {
      if (i >= dst.lights.size()) dst.lights.resize(i + 1);
      dst.lights[i].valid = true; dst.lights[i].light = src.lights[i].light;
    }
    for (size_t i = 0; i < m.lightEnable.size(); i++) if (m.lightEnable[i] && i < src.lights.size()) {
      if (i >= dst.lights.size()) dst.lights.resize(i + 1);
      if (!dst.lights[i].valid) { dst.lights[i].valid = true; dst.lights[i].light = defaultLight(); }
      dst.lights[i].enabled = src.lights[i].enabled;
    }
  }
}
void VDevice::capture(VStateBlock* sb, bool creation) {
  if (sb->m.f[SB_DECL]) setRef<VBase>(sb->st.decl, st.decl);
  transfer(sb->m, st, sb->st, !creation, this);
}
void VDevice::apply(VStateBlock* sb) {
  if (sb->m.f[SB_DECL] && sb->st.decl) setRef<VBase>(st.decl, sb->st.decl);
  transfer(sb->m, sb->st, st, false, this, true);
}
HRESULT STDMETHODCALLTYPE VDevice::CreateStateBlock(D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  if (t != D3DSBT_ALL && t != D3DSBT_PIXELSTATE && t != D3DSBT_VERTEXSTATE) return D3DERR_INVALIDCALL;
  Guard g;
  if (recording) return D3DERR_INVALIDCALL;
  VStateBlock* sb = new VStateBlock(); sb->kind = K_SB; sb->dev = this; sb->id = newId();
  initStateVectors(sb->st, this); initMask(sb->m, this);
  maskForType(sb->m, t, this);
  capture(sb, true);
  PSB r = {id, sb->id, (uint32_t) t};
  HRESULT hr = call(OP_CREATE_SB, &r, sizeof r);
  if (FAILED(hr)) { freeId(sb->id); sb->id = 0; delete sb; return hr; }
  sb->addRef(); *out = sb; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::BeginStateBlock() {
  Guard g; if (recording) return D3DERR_INVALIDCALL;
  recording = new VStateBlock(); recording->kind = K_SB; recording->dev = this; recording->ownedByDevice = true;
  initStateVectors(recording->st, this); initMask(recording->m, this);
  // the block starts from the current state so unrecorded slots compare equal (only masked slots are ever used)
  PSB r = {id, 0, 0}; HRESULT hr = call(OP_BEGIN_SB, &r, sizeof r);
  if (FAILED(hr)) { delete recording; recording = nullptr; }
  return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::EndStateBlock(IDirect3DStateBlock9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = nullptr;
  Guard g; if (!recording) return D3DERR_INVALIDCALL;
  VStateBlock* sb = recording; recording = nullptr; sb->ownedByDevice = false; sb->id = newId();
  PSB r = {id, sb->id, 0}; HRESULT hr = call(OP_END_SB, &r, sizeof r);
  if (FAILED(hr)) { freeId(sb->id); sb->id = 0; delete sb; return hr; }
  sb->addRef(); *out = sb; return hr;
}

// ================================================================ queries and Ex
HRESULT STDMETHODCALLTYPE VDevice::CreateQuery(D3DQUERYTYPE t, IDirect3DQuery9** out) {
  if (out) *out = nullptr;
  Guard g;
  static std::unordered_map<uint32_t, HRESULT> support;
  auto it = support.find(t);
  HRESULT hr;
  if (it != support.end()) hr = it->second;
  else { PCreateQuery q = {id, 0, (uint32_t) t}; hr = call(OP_QUERY_SUPPORT, &q, sizeof q); support[t] = hr; }
  if (!out || FAILED(hr)) return hr;
  VQuery* q = new VQuery(); q->kind = K_QUERY; q->dev = this; q->type = t; q->id = newId();
  PCreateQuery r = {id, q->id, (uint32_t) t}; hr = call(OP_CREATE_QUERY, &r, sizeof r);
  if (FAILED(hr)) { freeId(q->id); q->id = 0; delete q; return hr; }
  q->addRef(); *out = q; return hr;
}
HRESULT STDMETHODCALLTYPE VDevice::SetConvolutionMonoKernel(UINT w, UINT h, float*, float*) { Guard g; auto* r = rec<PGeneric>(OP_SET_CONVOLUTION); r->devId = id; r->a = w; r->b = h; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetGPUThreadPriority(INT* p) { if (!p) return D3DERR_INVALIDCALL; *p = gpuPriority; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetGPUThreadPriority(INT p) { if (p < -7 || p > 7) return D3DERR_INVALIDCALL; gpuPriority = p; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::WaitForVBlank(UINT) { Sleep(1); return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::CheckResourceResidency(IDirect3DResource9**, UINT32) { return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::SetMaximumFrameLatency(UINT n) { maxLatency = n ? n : 3; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VDevice::GetMaximumFrameLatency(UINT* n) { if (!n) return D3DERR_INVALIDCALL; *n = maxLatency; return D3D_OK; }

}  // namespace vcr
