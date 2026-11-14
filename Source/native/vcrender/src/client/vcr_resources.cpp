// Resource objects: lifetimes, image and buffer locks, shaders, declarations, queries, state blocks, swapchains.
#include "vcr_client.h"
#include <stdio.h>

namespace vcr {

// ================================================================ object lifetime
VBase::~VBase() {
  delete pdata;
  if (pdataObj) { for (auto& kv : *pdataObj) kv.second->Release(); delete pdataObj; }
  magic = 0;
}
ULONG VBase::addRef() {
  if (container) { InterlockedIncrement(&forwardedPub); container->addRef(); return 1; }
  LONG r = InterlockedIncrement(&pub);
  if (r == 1 && dev && (VBase*) dev != this) dev->AddRef();
  return (ULONG) r;
}
ULONG VBase::release() {
  if (container) {
    VBase* parent = container;
    InterlockedDecrement(&forwardedPub);
    if (retiredImplicit) maybeDelete();
    parent->release(); return 0;
  }
  LONG r = InterlockedDecrement(&pub);
  if (r == 0) {
    VDevice* d = (dev && (VBase*) dev != this) ? dev : nullptr;
    maybeDelete();
    if (d) d->Release();
  }
  return (ULONG) (r < 0 ? 0 : r);
}
void VBase::addPriv() { if (container) { InterlockedIncrement(&forwardedPriv); container->addPriv(); return; } InterlockedIncrement(&priv); }
void VBase::releasePriv() {
  if (container) {
    VBase* parent = container;
    InterlockedDecrement(&forwardedPriv);
    if (retiredImplicit) maybeDelete();
    parent->releasePriv(); return;
  }
  if (InterlockedDecrement(&priv) == 0) maybeDelete();
}
void VBase::maybeDelete() { if (pub <= 0 && priv <= 0 && forwardedPub <= 0 && forwardedPriv <= 0 && !ownedByDevice) finalRelease(); }
void VBase::finalRelease() {
  Guard g;
  if (id && ready()) { *rec<uint32_t>(OP_DESTROY) = id; freeId(id); }
  id = 0;
  delete this;
}
HRESULT VBase::setPrivate(REFGUID g, const void* data, DWORD size, DWORD flags) {
  Guard lk;
  freePrivate(g);
  if (flags & D3DSPD_IUNKNOWN) {
    if (size != sizeof(IUnknown*) || !data) return D3DERR_INVALIDCALL;
    IUnknown* u = *(IUnknown* const*) data; if (u) u->AddRef();
    if (!pdataObj) pdataObj = new std::unordered_map<GUID, IUnknown*, GuidHash>();
    (*pdataObj)[g] = u; return D3D_OK;
  }
  if (!pdata) pdata = new std::unordered_map<GUID, std::vector<uint8_t>, GuidHash>();
  (*pdata)[g].assign((const uint8_t*) data, (const uint8_t*) data + size);
  return D3D_OK;
}
HRESULT VBase::getPrivate(REFGUID g, void* data, DWORD* size) {
  Guard lk;
  if (!size) return D3DERR_INVALIDCALL;
  if (pdataObj) { auto it = pdataObj->find(g); if (it != pdataObj->end()) {
    if (!data) { *size = sizeof(IUnknown*); return D3D_OK; }
    if (*size < sizeof(IUnknown*)) { *size = sizeof(IUnknown*); return D3DERR_MOREDATA; }
    *(IUnknown**) data = it->second; if (it->second) it->second->AddRef(); *size = sizeof(IUnknown*); return D3D_OK; } }
  if (pdata) { auto it = pdata->find(g); if (it != pdata->end()) {
    DWORD n = (DWORD) it->second.size();
    if (!data) { *size = n; return D3D_OK; }
    if (*size < n) { *size = n; return D3DERR_MOREDATA; }
    memcpy(data, it->second.data(), n); *size = n; return D3D_OK; } }
  return D3DERR_NOTFOUND;
}
HRESULT VBase::freePrivate(REFGUID g) {
  if (pdataObj) { auto it = pdataObj->find(g); if (it != pdataObj->end()) { if (it->second) it->second->Release(); pdataObj->erase(it); return D3D_OK; } }
  if (pdata && pdata->erase(g)) return D3D_OK;
  return D3DERR_NOTFOUND;
}

#define QI_BEGIN(CLS) HRESULT STDMETHODCALLTYPE CLS::QueryInterface(REFIID riid, void** ppv) { \
    if (!ppv) return E_POINTER; *ppv = nullptr;
#define QI_IF(IFACE) if (riid == __uuidof(IFACE)) { *ppv = static_cast<IFACE*>(this); AddRef(); return S_OK; }
#define QI_END return E_NOINTERFACE; }

#define QI_END_INTEROP return interopQueryTexture(this, unk(), riid, ppv); }
QI_BEGIN(VTexture) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DBaseTexture9) QI_IF(IDirect3DTexture9) QI_END_INTEROP
QI_BEGIN(VCubeTexture) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DBaseTexture9) QI_IF(IDirect3DCubeTexture9) QI_END_INTEROP
QI_BEGIN(VVolumeTexture) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DBaseTexture9) QI_IF(IDirect3DVolumeTexture9) QI_END
QI_BEGIN(VSurface) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DSurface9) QI_END_INTEROP
QI_BEGIN(VVolume) QI_IF(IUnknown) QI_IF(IDirect3DVolume9) QI_END
QI_BEGIN(VVertexBuffer) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DVertexBuffer9) QI_END
QI_BEGIN(VIndexBuffer) QI_IF(IUnknown) QI_IF(IDirect3DResource9) QI_IF(IDirect3DIndexBuffer9) QI_END
QI_BEGIN(VVertexShader) QI_IF(IUnknown) QI_IF(IDirect3DVertexShader9) QI_END
QI_BEGIN(VPixelShader) QI_IF(IUnknown) QI_IF(IDirect3DPixelShader9) QI_END
QI_BEGIN(VDecl) QI_IF(IUnknown) QI_IF(IDirect3DVertexDeclaration9) QI_END
QI_BEGIN(VQuery) QI_IF(IUnknown) QI_IF(IDirect3DQuery9) QI_END
QI_BEGIN(VStateBlock) QI_IF(IUnknown) QI_IF(IDirect3DStateBlock9) QI_END
QI_BEGIN(VSwapChain) QI_IF(IUnknown) QI_IF(IDirect3DSwapChain9) if (dev && dev->isEx) { QI_IF(IDirect3DSwapChain9Ex) } QI_END

static HRESULT getDev(VBase* b, IDirect3DDevice9** out) {
  if (!out) return D3DERR_INVALIDCALL;
  *out = b->dev; if (b->dev) b->dev->AddRef();
  return b->dev ? D3D_OK : D3DERR_INVALIDCALL;
}
#define RES_COMMON(CLS)                                                                                          \
  HRESULT STDMETHODCALLTYPE CLS::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }                \
  DWORD STDMETHODCALLTYPE CLS::SetPriority(DWORD p) { Guard g; DWORD o = priority; priority = p;                  \
    if (id && ready()) { auto* r = rec<PTexOp>(OP_RES_PRIORITY); r->id = id; r->kind = kind; r->value = p; } return o; } \
  void STDMETHODCALLTYPE CLS::PreLoad() { Guard g; if (id && ready()) { auto* r = rec<PTexOp>(OP_RES_PRELOAD); r->id = id; r->kind = kind; r->value = 0; } }
RES_COMMON(VTexture) RES_COMMON(VCubeTexture) RES_COMMON(VVolumeTexture) RES_COMMON(VSurface)
RES_COMMON(VVertexBuffer) RES_COMMON(VIndexBuffer)
HRESULT STDMETHODCALLTYPE VVolume::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VVertexShader::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VPixelShader::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VDecl::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VQuery::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VStateBlock::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }
HRESULT STDMETHODCALLTYPE VSwapChain::GetDevice(IDirect3DDevice9** out) { return getDev(this, out); }

// ================================================================ casting helpers
template<class T, class I> static T* ours(I* p) {
  if (!p) return nullptr;
  T* t = static_cast<T*>(p);
  return static_cast<VBase*>(t)->magic == 0x4F524356 ? t : nullptr;
}
VSurface* asSurface(IDirect3DSurface9* s) { return ours<VSurface>(s); }
VBaseTexture* asTexture(IDirect3DBaseTexture9* t) {
  if (!t) return nullptr;
  switch (t->GetType()) {
  case D3DRTYPE_TEXTURE: return ours<VTexture>(static_cast<IDirect3DTexture9*>(t));
  case D3DRTYPE_CUBETEXTURE: return ours<VCubeTexture>(static_cast<IDirect3DCubeTexture9*>(t));
  case D3DRTYPE_VOLUMETEXTURE: return ours<VVolumeTexture>(static_cast<IDirect3DVolumeTexture9*>(t));
  default: return nullptr;
  }
}
VBase* baseOf(IDirect3DBaseTexture9* t) { return asTexture(t); }
IDirect3DBaseTexture9* texIface(VBase* b) {
  if (!b) return nullptr;
  if (b->kind == K_TEXTURE) return static_cast<VTexture*>(b);
  if (b->kind == K_CUBE) return static_cast<VCubeTexture*>(b);
  if (b->kind == K_VOLTEX) return static_cast<VVolumeTexture*>(b);
  return nullptr;
}
uint32_t texKind(VBase* b) { return b ? b->kind : K_NONE; }

void registerSurface(VSurface* s) {
  if (!s || s->registered || !s->container) return;
  if (!s->id) s->id = newId();
  auto* r = rec<PRegisterSub>(OP_REGISTER_SUB);
  r->parent = s->img->addr.id; r->parentKind = s->img->addr.kind; r->face = s->img->addr.face; r->level = s->img->addr.level;
  r->id = s->id; r->kind = K_SURFACE;
  s->registered = true;
}
VcrSub subAddr(VSurface* s) {
  if (!s) return VcrSub{0, K_SURFACE, 0, 0};
  if (s->container && s->container->kind != K_SWAPCHAIN) registerSurface(s);
  return VcrSub{s->id, K_SURFACE, 0, 0};
}

// ================================================================ image locks
static bool needReadback(const ImgSub& s, DWORD flags) {
  if (flags & D3DLOCK_READONLY) return true;
  if (flags & D3DLOCK_DISCARD) return false;
  return s.gpuWritten || s.uploaded;
}
static uint32_t g_readbacks, g_readbackBytes; static DWORD g_readbackLog;

HRESULT readImg(ImgSub& s, uint8_t* dst, uint32_t dstPitch, uint32_t dstSlicePitch, int32_t l, int32_t t, int32_t r, int32_t b, int32_t front, int32_t back, bool whole) {
  FmtInfo fi = fmtInfo(s.fmt);
  uint32_t bl = l / fi.bw, bt = t / fi.bh, br = (r + fi.bw - 1) / fi.bw, bb = (b + fi.bh - 1) / fi.bh;
  uint32_t rowBytes = (br - bl) * fi.bytes, rows = bb - bt, slices = back - front;
  if (!rowBytes || !rows) return D3D_OK;
  uint32_t per = respCap() / rowBytes; if (!per) return E_OUTOFMEMORY;
  g_readbacks++;
  for (uint32_t z = 0; z < (slices ? slices : 1); z++) {
    for (uint32_t y = 0; y < rows; y += per) {
      PReadImg q = {}; q.sub = s.addr; q.l = l; q.t = t; q.r = r; q.b = b; q.front = front; q.back = back; q.whole = whole;
      q.rowBytes = rowBytes; q.rowStart = y; q.rowCount = rows - y < per ? rows - y : per; q.slice = z;
      std::vector<uint8_t> tmp((size_t) q.rowBytes * q.rowCount);
      uint32_t got = 0;
      HRESULT hr = call(OP_READ_IMG, &q, sizeof q, tmp.data(), (uint32_t) tmp.size(), &got);
      if (FAILED(hr)) return hr;
      for (uint32_t k = 0; k < q.rowCount; k++) memcpy(dst + (size_t) z * dstSlicePitch + (size_t) (y + k) * dstPitch, tmp.data() + (size_t) k * rowBytes, rowBytes);
      g_readbackBytes += got;
    }
  }
  DWORD now = GetTickCount();
  if (now - g_readbackLog > 10000) { log("[readback] %u image readbacks, %u KiB in the last period", g_readbacks, g_readbackBytes >> 10); g_readbacks = g_readbackBytes = 0; g_readbackLog = now; }
  return D3D_OK;
}

static void sendImage(ImgSub& s, const uint8_t* src, uint32_t srcPitch, uint32_t srcSlicePitch, bool arena, uint32_t arenaOff, DWORD flags) {
  uint32_t bytes = s.lkRowBytes * s.lkRows * (s.lkSlices ? s.lkSlices : 1);
  auto fill = [&](PUploadImg* u) {
    u->sub = s.addr; u->l = s.lockRect.left; u->t = s.lockRect.top; u->r = s.lockRect.right; u->b = s.lockRect.bottom;
    u->front = s.lockBox.Front; u->back = s.lockBox.Back; u->whole = s.lockWhole; u->rowBytes = s.lkRowBytes; u->rows = s.lkRows;
    u->slices = s.lkSlices; u->lockFlags = flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE | D3DLOCK_NO_DIRTY_UPDATE | D3DLOCK_NOSYSLOCK);
  };
  if (arena) {
    auto* u = rec<PUploadImg>(OP_UPLOAD_IMG); fill(u);
    u->srcPitch = srcPitch; u->srcSlicePitch = srcSlicePitch; u->src = 1; u->arenaOff = arenaOff;
    return;
  }
  // compact copy: inline when small, chunked otherwise
  if (bytes <= 64u * 1024) {
    auto* u = rec<PUploadImg>(OP_UPLOAD_IMG, bytes); fill(u);
    u->srcPitch = s.lkRowBytes; u->srcSlicePitch = s.lkRowBytes * s.lkRows; u->src = 0;
    uint8_t* o = (uint8_t*) (u + 1);
    for (uint32_t z = 0; z < (s.lkSlices ? s.lkSlices : 1); z++)
      for (uint32_t y = 0; y < s.lkRows; y++) memcpy(o + ((size_t) z * s.lkRows + y) * s.lkRowBytes, src + (size_t) z * srcSlicePitch + (size_t) y * srcPitch, s.lkRowBytes);
    return;
  }
  std::vector<uint8_t> tmp(bytes);
  for (uint32_t z = 0; z < (s.lkSlices ? s.lkSlices : 1); z++)
    for (uint32_t y = 0; y < s.lkRows; y++) memcpy(tmp.data() + ((size_t) z * s.lkRows + y) * s.lkRowBytes, src + (size_t) z * srcSlicePitch + (size_t) y * srcPitch, s.lkRowBytes);
  sendChunks(tmp.data(), bytes);
  auto* u = rec<PUploadImg>(OP_UPLOAD_IMG); fill(u);
  u->srcPitch = s.lkRowBytes; u->srcSlicePitch = s.lkRowBytes * s.lkRows; u->src = 2;
}

HRESULT lockImg(ImgSub& s, const RECT* rc, const D3DBOX* box, DWORD flags, bool volume, D3DLOCKED_RECT* lr, D3DLOCKED_BOX* lb) {
  if (volume ? !lb : !lr) return D3DERR_INVALIDCALL;
  if (!s.lockable || s.locked) return D3DERR_INVALIDCALL;
  FmtInfo fi = fmtInfo(s.fmt);
  int32_t l = 0, t = 0, r = (int32_t) s.w, b = (int32_t) s.h, f = 0, bk = (int32_t) s.d; bool whole = true;
  if (!volume && rc) { l = rc->left; t = rc->top; r = rc->right; b = rc->bottom; whole = false; }
  if (volume && box) { l = box->Left; t = box->Top; r = box->Right; b = box->Bottom; f = box->Front; bk = box->Back; whole = false; }
  if (l < 0 || t < 0 || f < 0 || r > (int32_t) s.w || b > (int32_t) s.h || bk > (int32_t) s.d || l >= r || t >= b || f >= bk) return D3DERR_INVALIDCALL;
  uint32_t bl = l / fi.bw, bt = t / fi.bh, br = (r + fi.bw - 1) / fi.bw, bb = (b + fi.bh - 1) / fi.bh;
  uint32_t pitch = s.pitch();
  s.lkBl = bl; s.lkBt = bt; s.lkRowBytes = (br - bl) * fi.bytes; s.lkRows = bb - bt; s.lkSlices = volume ? (uint32_t) (bk - f) : 1;
  s.lockRect = {l, t, r, b}; s.lockBox = {(UINT) l, (UINT) t, (UINT) r, (UINT) b, (UINT) f, (UINT) bk}; s.lockWhole = whole; s.lockFlags = flags;
  uint8_t* ptr;
  uint32_t slicePitch;
  if (s.resident) {
    slicePitch = s.slicePitch();
    ptr = s.mem.data() + (size_t) f * slicePitch + (size_t) bt * pitch + (size_t) bl * fi.bytes;
  } else {
    slicePitch = s.lkRows * pitch;
    uint32_t bytes = slicePitch * s.lkSlices;
    if (!regionAlloc(bytes, s.region)) return E_OUTOFMEMORY;
    ptr = s.region.p + (size_t) bl * fi.bytes;
    if (needReadback(s, flags) && s.addr.id) {
      HRESULT hr = readImg(s, ptr, pitch, slicePitch, l, t, r, b, f, bk, whole);
      if (FAILED(hr)) log("lock readback failed 0x%08lx (fmt %u %ux%u)", (unsigned long) hr, s.fmt, s.w, s.h);
    } else if (!s.uploaded && !s.gpuWritten) {
      memset(s.region.p, 0, bytes);
    }
  }
  if (volume) { lb->pBits = ptr; lb->RowPitch = (INT) pitch; lb->SlicePitch = (INT) slicePitch; }
  else { lr->pBits = ptr; lr->Pitch = (INT) pitch; }
  s.lkSlicePitch = slicePitch;
  s.locked = true;
  return D3D_OK;
}

HRESULT unlockImg(ImgSub& s, bool volume) {
  (void) volume;
  if (!s.locked) return D3DERR_INVALIDCALL;
  s.locked = false;
  FmtInfo fi = fmtInfo(s.fmt);
  bool write = !(s.lockFlags & D3DLOCK_READONLY);
  if (write && s.addr.id && ready()) {
    if (s.resident) {
      const uint8_t* src = s.mem.data() + (size_t) s.lockBox.Front * s.slicePitch() + (size_t) s.lkBt * s.pitch() + (size_t) s.lkBl * fi.bytes;
      sendImage(s, src, s.pitch(), s.slicePitch(), false, 0, s.lockFlags);
    } else if (s.region.arena) {
      sendImage(s, nullptr, s.pitch(), s.lkSlicePitch, true, s.region.off + s.lkBl * fi.bytes, s.lockFlags);
      regionSubmit(s.region);
    } else {
      sendImage(s, s.region.p + (size_t) s.lkBl * fi.bytes, s.pitch(), s.lkSlicePitch, false, 0, s.lockFlags);
      regionSubmit(s.region);
    }
    s.uploaded = true;
  }
  if (s.region.p) regionDrop(s.region);
  return D3D_OK;
}

// SYSTEMMEM data the host must see before it reads the resource
void uploadWholeResident(ImgSub& s) {
  (void) s;   // resident images upload on every unlock, so the host copy is always current
}

// ================================================================ textures
static void initSubs(VBaseTexture* t, uint32_t w, uint32_t h, uint32_t d, bool volume) {
  t->subs.resize((size_t) t->faces * t->levels);
  t->children.assign(t->subs.size(), nullptr);
  bool resident = t->pool == D3DPOOL_SYSTEMMEM || t->pool == D3DPOOL_SCRATCH;
  bool lockable = t->pool != D3DPOOL_DEFAULT || (t->usage & D3DUSAGE_DYNAMIC);
  if (isDepthFormat(t->fmt) && !isLockableDepth(t->fmt)) lockable = false;
  if (!fmtInfo(t->fmt).known) lockable = false;
  for (uint32_t f = 0; f < t->faces; f++) for (uint32_t l = 0; l < t->levels; l++) {
    ImgSub& s = t->subs[f * t->levels + l];
    s.owner = t; s.addr = VcrSub{t->id, t->kind, f, l};
    s.w = mipDim(w, l); s.h = mipDim(h, l); s.d = volume ? mipDim(d, l) : 1;
    s.fmt = t->fmt; s.pool = t->pool; s.usage = t->usage; s.lockable = lockable; s.resident = resident;
    if (resident) s.mem.assign((size_t) s.slicePitch() * s.d, 0);
  }
}
void initTexture(VBaseTexture* t, uint32_t w, uint32_t h, uint32_t d) { initSubs(t, w, h, d, t->kind == K_VOLTEX); }

VSurface* surfaceFor(VBaseTexture* t, uint32_t face, uint32_t level, IUnknown* containerIface) {
  size_t i = (size_t) face * t->levels + level;
  if (!t->children[i]) {
    VSurface* s = new VSurface();
    s->kind = K_SURFACE; s->dev = t->dev; s->container = t; s->img = &t->subs[i]; s->containerIface = containerIface;
    t->children[i] = s;
  }
  return static_cast<VSurface*>(t->children[i]);
}
void VBaseTexture::finalRelease() {
  Guard g;
  for (auto*& c : children) if (c) {
    if (c->kind == K_SURFACE) { VSurface* s = static_cast<VSurface*>(c); if (s->id) freeId(s->id); s->id = 0; s->container = nullptr; delete s; }
    else { VVolume* v = static_cast<VVolume*>(c); if (v->id) freeId(v->id); v->id = 0; v->container = nullptr; delete v; }
    c = nullptr;
  }
  for (auto& s : subs) if (s.region.p) regionDrop(s.region);
  VBase::finalRelease();
}

static void texSetLod(VBaseTexture* t, DWORD lod) { Guard g; t->lod = lod; if (t->id && ready()) { auto* r = rec<PTexOp>(OP_TEX_SET_LOD); r->id = t->id; r->kind = t->kind; r->value = lod; } }
static void texFilter(VBaseTexture* t, D3DTEXTUREFILTERTYPE f) { Guard g; t->autoGenFilter = f; if (t->id && ready()) { auto* r = rec<PTexOp>(OP_TEX_AUTOGEN_FILTER); r->id = t->id; r->kind = t->kind; r->value = f; } }
static void texGenMips(VBaseTexture* t) { Guard g; if (t->id && ready()) { auto* r = rec<PTexOp>(OP_TEX_GENMIPS); r->id = t->id; r->kind = t->kind; r->value = 0; } for (auto& s : t->subs) s.gpuWritten = true; }

DWORD STDMETHODCALLTYPE VTexture::SetLOD(DWORD l) { DWORD o = lod; if (pool == D3DPOOL_MANAGED) texSetLod(this, l < levels ? l : levels - 1); return o; }
DWORD STDMETHODCALLTYPE VTexture::GetLevelCount() { return (usage & D3DUSAGE_AUTOGENMIPMAP) ? 1 : levels; }
HRESULT STDMETHODCALLTYPE VTexture::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) { if (f == D3DTEXF_NONE) return D3DERR_INVALIDCALL; texFilter(this, f); return D3D_OK; }
void STDMETHODCALLTYPE VTexture::GenerateMipSubLevels() { texGenMips(this); }
HRESULT STDMETHODCALLTYPE VTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* d) {
  ImgSub* s = sub(0, level); if (!s || !d) return D3DERR_INVALIDCALL;
  d->Format = fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = usage; d->Pool = pool; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->MultiSampleQuality = 0;
  d->Width = s->w; d->Height = s->h; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VTexture::GetSurfaceLevel(UINT level, IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g;
  if (level >= levels) return D3DERR_INVALIDCALL;
  VSurface* s = surfaceFor(this, 0, level, static_cast<IDirect3DTexture9*>(this));
  s->AddRef(); *out = s; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VTexture::LockRect(UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) {
  Guard g; ImgSub* s = sub(0, level); if (!s) return D3DERR_INVALIDCALL; return lockImg(*s, rc, nullptr, flags, false, lr, nullptr);
}
HRESULT STDMETHODCALLTYPE VTexture::UnlockRect(UINT level) { Guard g; ImgSub* s = sub(0, level); if (!s) return D3DERR_INVALIDCALL; return unlockImg(*s, false); }
HRESULT STDMETHODCALLTYPE VTexture::AddDirtyRect(const RECT* rc) {
  Guard g; if (id && ready()) { auto* r = rec<PTexDirty>(OP_TEX_DIRTY); r->id = id; r->kind = K_TEXTURE; r->face = 0; r->hasBox = rc != nullptr;
    if (rc) { r->l = rc->left; r->t = rc->top; r->r = rc->right; r->b = rc->bottom; } } return D3D_OK;
}

DWORD STDMETHODCALLTYPE VCubeTexture::SetLOD(DWORD l) { DWORD o = lod; if (pool == D3DPOOL_MANAGED) texSetLod(this, l < levels ? l : levels - 1); return o; }
DWORD STDMETHODCALLTYPE VCubeTexture::GetLevelCount() { return (usage & D3DUSAGE_AUTOGENMIPMAP) ? 1 : levels; }
HRESULT STDMETHODCALLTYPE VCubeTexture::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) { if (f == D3DTEXF_NONE) return D3DERR_INVALIDCALL; texFilter(this, f); return D3D_OK; }
void STDMETHODCALLTYPE VCubeTexture::GenerateMipSubLevels() { texGenMips(this); }
HRESULT STDMETHODCALLTYPE VCubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* d) {
  ImgSub* s = sub(0, level); if (!s || !d) return D3DERR_INVALIDCALL;
  d->Format = fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = usage; d->Pool = pool; d->MultiSampleType = D3DMULTISAMPLE_NONE; d->MultiSampleQuality = 0;
  d->Width = s->w; d->Height = s->h; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VCubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g;
  if ((UINT) face >= 6 || level >= levels) return D3DERR_INVALIDCALL;
  VSurface* s = surfaceFor(this, face, level, static_cast<IDirect3DCubeTexture9*>(this));
  s->AddRef(); *out = s; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VCubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) {
  Guard g; ImgSub* s = sub(face, level); if (!s) return D3DERR_INVALIDCALL; return lockImg(*s, rc, nullptr, flags, false, lr, nullptr);
}
HRESULT STDMETHODCALLTYPE VCubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT level) { Guard g; ImgSub* s = sub(face, level); if (!s) return D3DERR_INVALIDCALL; return unlockImg(*s, false); }
HRESULT STDMETHODCALLTYPE VCubeTexture::AddDirtyRect(D3DCUBEMAP_FACES face, const RECT* rc) {
  Guard g; if (id && ready()) { auto* r = rec<PTexDirty>(OP_TEX_DIRTY); r->id = id; r->kind = K_CUBE; r->face = face; r->hasBox = rc != nullptr;
    if (rc) { r->l = rc->left; r->t = rc->top; r->r = rc->right; r->b = rc->bottom; } } return D3D_OK;
}

DWORD STDMETHODCALLTYPE VVolumeTexture::SetLOD(DWORD l) { DWORD o = lod; if (pool == D3DPOOL_MANAGED) texSetLod(this, l < levels ? l : levels - 1); return o; }
DWORD STDMETHODCALLTYPE VVolumeTexture::GetLevelCount() { return levels; }
HRESULT STDMETHODCALLTYPE VVolumeTexture::SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) { if (f == D3DTEXF_NONE) return D3DERR_INVALIDCALL; texFilter(this, f); return D3D_OK; }
void STDMETHODCALLTYPE VVolumeTexture::GenerateMipSubLevels() { texGenMips(this); }
HRESULT STDMETHODCALLTYPE VVolumeTexture::GetLevelDesc(UINT level, D3DVOLUME_DESC* d) {
  ImgSub* s = sub(0, level); if (!s || !d) return D3DERR_INVALIDCALL;
  d->Format = fmt; d->Type = D3DRTYPE_VOLUME; d->Usage = usage; d->Pool = pool; d->Width = s->w; d->Height = s->h; d->Depth = s->d; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VVolumeTexture::GetVolumeLevel(UINT level, IDirect3DVolume9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g;
  if (level >= levels) return D3DERR_INVALIDCALL;
  if (!children[level]) {
    VVolume* v = new VVolume(); v->kind = K_VOLUME; v->dev = dev; v->container = this; v->img = &subs[level];
    v->containerIface = static_cast<IDirect3DVolumeTexture9*>(this); children[level] = v;
  }
  VVolume* v = static_cast<VVolume*>(children[level]); v->AddRef(); *out = v; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VVolumeTexture::LockBox(UINT level, D3DLOCKED_BOX* lb, const D3DBOX* box, DWORD flags) {
  Guard g; ImgSub* s = sub(0, level); if (!s) return D3DERR_INVALIDCALL; return lockImg(*s, nullptr, box, flags, true, nullptr, lb);
}
HRESULT STDMETHODCALLTYPE VVolumeTexture::UnlockBox(UINT level) { Guard g; ImgSub* s = sub(0, level); if (!s) return D3DERR_INVALIDCALL; return unlockImg(*s, true); }
HRESULT STDMETHODCALLTYPE VVolumeTexture::AddDirtyBox(const D3DBOX* b) {
  Guard g; if (id && ready()) { auto* r = rec<PTexDirty>(OP_TEX_DIRTY); r->id = id; r->kind = K_VOLTEX; r->face = 0; r->hasBox = b != nullptr;
    if (b) { r->l = b->Left; r->t = b->Top; r->r = b->Right; r->b = b->Bottom; r->front = b->Front; r->back = b->Back; } } return D3D_OK;
}

// ================================================================ surfaces and volumes
HRESULT STDMETHODCALLTYPE VSurface::GetContainer(REFIID riid, void** ppv) {
  if (!ppv) return D3DERR_INVALIDCALL;
  if (containerIface) return containerIface->QueryInterface(riid, ppv);
  if (dev) return dev->QueryInterface(riid, ppv);
  *ppv = nullptr; return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE VSurface::GetDesc(D3DSURFACE_DESC* d) {
  if (!d) return D3DERR_INVALIDCALL;
  d->Format = img->fmt; d->Type = D3DRTYPE_SURFACE; d->Usage = img->usage; d->Pool = img->pool;
  d->MultiSampleType = img->ms; d->MultiSampleQuality = img->msq; d->Width = img->w; d->Height = img->h; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VSurface::LockRect(D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) {
  Guard g;
  if (!container || container->kind == K_SWAPCHAIN) {   // standalone/backbuffer surfaces are addressed by their own id
    img->addr = VcrSub{id, K_SURFACE, 0, 0};
  }
  return lockImg(*img, rc, nullptr, flags, false, lr, nullptr);
}
HRESULT STDMETHODCALLTYPE VSurface::UnlockRect() { Guard g; return unlockImg(*img, false); }
HRESULT STDMETHODCALLTYPE VSurface::GetDC(HDC* phdc) {
  if (!phdc) return D3DERR_INVALIDCALL;
  Guard g;
  if (dc) return D3DERR_INVALIDCALL;
  D3DFORMAT f = img->fmt;
  if (f != D3DFMT_X8R8G8B8 && f != D3DFMT_A8R8G8B8 && f != D3DFMT_R5G6B5 && f != D3DFMT_X1R5G5B5 && f != D3DFMT_A1R5G5B5 && f != D3DFMT_R8G8B8) return D3DERR_INVALIDCALL;
  D3DLOCKED_RECT lr; HRESULT hr = LockRect(&lr, nullptr, 0); if (FAILED(hr)) return hr;
  BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = (LONG) img->w; bi.bmiHeader.biHeight = -(LONG) img->h;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = (WORD) (fmtInfo(f).bytes * 8); bi.bmiHeader.biCompression = BI_RGB;
  dc = CreateCompatibleDC(nullptr);
  dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
  if (!dib) { DeleteDC(dc); dc = nullptr; UnlockRect(); return D3DERR_INVALIDCALL; }
  uint32_t row = img->w * fmtInfo(f).bytes, dibPitch = (row + 3) & ~3u;
  for (uint32_t y = 0; y < img->h; y++) memcpy((uint8_t*) dibBits + (size_t) y * dibPitch, (uint8_t*) lr.pBits + (size_t) y * lr.Pitch, row);
  oldBmp = SelectObject(dc, dib);
  *phdc = dc; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VSurface::ReleaseDC(HDC hdc) {
  Guard g;
  if (!dc || hdc != dc) return D3DERR_INVALIDCALL;
  GdiFlush();
  uint32_t bytes = fmtInfo(img->fmt).bytes, row = img->w * bytes, dibPitch = (row + 3) & ~3u;
  uint8_t* dst = img->resident ? img->mem.data() : img->region.p;
  uint32_t pitch = img->pitch();
  if (dst) for (uint32_t y = 0; y < img->h; y++) memcpy(dst + (size_t) y * pitch, (uint8_t*) dibBits + (size_t) y * dibPitch, row);
  SelectObject(dc, oldBmp); DeleteObject(dib); DeleteDC(dc); dc = nullptr; dib = nullptr; dibBits = nullptr;
  return UnlockRect();
}
void VSurface::finalRelease() {
  if (dc) { SelectObject(dc, oldBmp); DeleteObject(dib); DeleteDC(dc); dc = nullptr; }
  if (own.region.p) { Guard g; regionDrop(own.region); }
  VBase::finalRelease();
}

HRESULT STDMETHODCALLTYPE VVolume::GetContainer(REFIID riid, void** ppv) {
  if (!ppv) return D3DERR_INVALIDCALL;
  if (containerIface) return containerIface->QueryInterface(riid, ppv);
  *ppv = nullptr; return E_NOINTERFACE;
}
HRESULT STDMETHODCALLTYPE VVolume::GetDesc(D3DVOLUME_DESC* d) {
  if (!d) return D3DERR_INVALIDCALL;
  d->Format = img->fmt; d->Type = D3DRTYPE_VOLUME; d->Usage = img->usage; d->Pool = img->pool; d->Width = img->w; d->Height = img->h; d->Depth = img->d; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VVolume::LockBox(D3DLOCKED_BOX* lb, const D3DBOX* box, DWORD flags) { Guard g; return lockImg(*img, nullptr, box, flags, true, nullptr, lb); }
HRESULT STDMETHODCALLTYPE VVolume::UnlockBox() { Guard g; return unlockImg(*img, true); }

// ================================================================ vertex and index buffers
HRESULT VBufferBase::lockImpl(UINT off, UINT size, void** pp, DWORD flags) {
  if (!pp) return D3DERR_INVALIDCALL;
  Guard g;
  if (off >= length && !(off == 0 && length == 0)) { *pp = nullptr; return D3DERR_INVALIDCALL; }
  if (size == 0 || off + size > length) size = length - off;
  LockRec L = {}; L.off = off; L.size = size; L.flags = flags;
  if (shadowed) {
    L.ptr = shadow.data() + off;
  } else {
    if (!regionAlloc(size ? size : 4, L.region)) return E_OUTOFMEMORY;
    L.ptr = L.region.p;
    bool readback = (flags & D3DLOCK_READONLY) || (uploaded && !(flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)));
    if (readback && id && size) {
      for (uint32_t done = 0; done < size;) {
        PReadBuf q = {id, kind, off + done, size - done < respCap() ? size - done : respCap()};
        uint32_t got = 0;
        HRESULT hr = call(OP_READ_BUF, &q, sizeof q, L.ptr + done, q.size, &got);
        if (FAILED(hr)) { log("buffer readback failed 0x%08lx", (unsigned long) hr); break; }
        done += q.size;
      }
    } else if (!uploaded) {
      memset(L.ptr, 0, size);
    }
  }
  locks.push_back(L);
  lockDepth++;
  *pp = L.ptr;
  return D3D_OK;
}
HRESULT VBufferBase::unlockImpl() {
  Guard g;
  if (!lockDepth) return D3DERR_INVALIDCALL;
  if (--lockDepth) return D3D_OK;   // D3D9 unlocks when the count reaches zero
  for (auto& L : locks) {
    bool write = !(L.flags & D3DLOCK_READONLY);
    if (write && L.size && id && ready()) {
      DWORD fl = L.flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE);
      if (shadowed || !L.region.arena) {
        const uint8_t* src = shadowed ? shadow.data() + L.off : L.region.p;
        if (L.size <= 64u * 1024) {
          auto* u = rec<PUploadBuf>(OP_UPLOAD_BUF, L.size); u->id = id; u->kind = kind; u->offset = L.off; u->size = L.size; u->lockFlags = fl; u->src = 0; u->arenaOff = 0;
          memcpy(u + 1, src, L.size);
        } else {
          sendChunks(src, L.size);
          auto* u = rec<PUploadBuf>(OP_UPLOAD_BUF); u->id = id; u->kind = kind; u->offset = L.off; u->size = L.size; u->lockFlags = fl; u->src = 2; u->arenaOff = 0;
        }
        if (L.region.p) regionSubmit(L.region);
      } else {
        auto* u = rec<PUploadBuf>(OP_UPLOAD_BUF); u->id = id; u->kind = kind; u->offset = L.off; u->size = L.size; u->lockFlags = fl; u->src = 1; u->arenaOff = L.region.off;
        regionSubmit(L.region);
      }
      uploaded = true;
    }
    if (L.region.p) regionDrop(L.region);
  }
  locks.clear();
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VVertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC* d) {
  if (!d) return D3DERR_INVALIDCALL;
  d->Format = D3DFMT_VERTEXDATA; d->Type = D3DRTYPE_VERTEXBUFFER; d->Usage = usage; d->Pool = pool; d->Size = length; d->FVF = fvfOrFmt; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VIndexBuffer::GetDesc(D3DINDEXBUFFER_DESC* d) {
  if (!d) return D3DERR_INVALIDCALL;
  d->Format = (D3DFORMAT) fvfOrFmt; d->Type = D3DRTYPE_INDEXBUFFER; d->Usage = usage; d->Pool = pool; d->Size = length; return D3D_OK;
}

// ================================================================ shaders, declarations, queries
static HRESULT getFunction(const std::vector<DWORD>& code, void* data, UINT* size) {
  if (!size) return D3DERR_INVALIDCALL;
  UINT n = (UINT) (code.size() * 4);
  if (!data) { *size = n; return D3D_OK; }
  if (*size < n) return D3DERR_INVALIDCALL;
  memcpy(data, code.data(), n); *size = n; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VVertexShader::GetFunction(void* data, UINT* size) { return getFunction(code, data, size); }
HRESULT STDMETHODCALLTYPE VPixelShader::GetFunction(void* data, UINT* size) { return getFunction(code, data, size); }

HRESULT STDMETHODCALLTYPE VDecl::GetDeclaration(D3DVERTEXELEMENT9* out, UINT* count) {
  if (!count) return D3DERR_INVALIDCALL;
  if (out) memcpy(out, el.data(), el.size() * sizeof(D3DVERTEXELEMENT9));
  *count = (UINT) el.size(); return D3D_OK;
}
HRESULT VDecl::ensureHost() {
  if (hostCreated) return D3D_OK;
  if (!ready()) return D3DERR_NOTAVAILABLE;
  if (!id) id = newId();
  std::vector<uint8_t> payload(sizeof(PCreateDecl) + el.size() * sizeof(D3DVERTEXELEMENT9));
  auto* r = (PCreateDecl*) payload.data();
  r->devId = dev->id; r->id = id; r->count = (uint32_t) el.size();
  memcpy(r + 1, el.data(), el.size() * sizeof(D3DVERTEXELEMENT9));
  HRESULT hr = call(OP_CREATE_DECL, r, (uint32_t) payload.size());
  if (FAILED(hr)) { freeId(id); id = 0; return hr; }
  hostCreated = true;
  return hr;
}

DWORD STDMETHODCALLTYPE VQuery::GetDataSize() {
  switch (type) {
  case D3DQUERYTYPE_EVENT: return sizeof(BOOL);
  case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
  case D3DQUERYTYPE_TIMESTAMP: return sizeof(UINT64);
  case D3DQUERYTYPE_TIMESTAMPDISJOINT: return sizeof(BOOL);
  case D3DQUERYTYPE_TIMESTAMPFREQ: return sizeof(UINT64);
  case D3DQUERYTYPE_VCACHE: return sizeof(D3DDEVINFO_VCACHE);
  case D3DQUERYTYPE_RESOURCEMANAGER: return sizeof(D3DDEVINFO_RESOURCEMANAGER);
  case D3DQUERYTYPE_VERTEXSTATS: return sizeof(D3DDEVINFO_D3DVERTEXSTATS);
  case D3DQUERYTYPE_PIPELINETIMINGS: return sizeof(D3DDEVINFO_D3D9PIPELINETIMINGS);
  case D3DQUERYTYPE_INTERFACETIMINGS: return sizeof(D3DDEVINFO_D3D9INTERFACETIMINGS);
  case D3DQUERYTYPE_VERTEXTIMINGS: case D3DQUERYTYPE_PIXELTIMINGS: return sizeof(D3DDEVINFO_D3D9STAGETIMINGS);
  case D3DQUERYTYPE_BANDWIDTHTIMINGS: return sizeof(D3DDEVINFO_D3D9BANDWIDTHTIMINGS);
  case D3DQUERYTYPE_CACHEUTILIZATION: return sizeof(D3DDEVINFO_D3D9CACHEUTILIZATION);
  default: return 0;
  }
}
HRESULT STDMETHODCALLTYPE VQuery::Issue(DWORD flags) {
  Guard g; if (!id || !ready()) return D3DERR_INVALIDCALL;
  auto* r = rec<PQueryIssue>(OP_QUERY_ISSUE); r->id = id; r->flags = flags; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VQuery::GetData(void* data, DWORD size, DWORD flags) {
  Guard g; if (!id || !ready()) return D3DERR_DEVICELOST;
  PQueryGetData q = {id, data ? size : 0, flags};
  uint8_t buf[64]; uint32_t got = 0;
  HRESULT hr = call(OP_QUERY_GETDATA, &q, sizeof q, buf, sizeof buf, &got);
  if (hr == S_OK && data && got) memcpy(data, buf, got < size ? got : size);
  return hr;
}

HRESULT STDMETHODCALLTYPE VStateBlock::Capture() {
  Guard g;
  if (dev->recording) return D3DERR_INVALIDCALL;
  dev->capture(this, false);
  if (id && ready()) { auto* r = rec<PSB>(OP_SB_CAPTURE); r->devId = dev->id; r->id = id; r->type = 0; }
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VStateBlock::Apply() {
  Guard g;
  if (dev->recording) return D3DERR_INVALIDCALL;
  dev->apply(this);
  if (id && ready()) { auto* r = rec<PSB>(OP_SB_APPLY); r->devId = dev->id; r->id = id; r->type = 0; }
  return D3D_OK;
}

// ================================================================ swapchains
HRESULT STDMETHODCALLTYPE VSwapChain::Present(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA*, DWORD flags) {
  return dev->presentImpl(this, src, dst, wnd, flags);
}
HRESULT STDMETHODCALLTYPE VSwapChain::GetFrontBufferData(IDirect3DSurface9* dstIf) {
  Guard g;
  VSurface* d = asSurface(dstIf);
  if (!d || !d->img->resident) return D3DERR_INVALIDCALL;
  std::vector<uint8_t> px; uint32_t w = 0, h = 0;
  if (!presenterLastFrame(id, px, w, h)) return D3DERR_INVALIDCALL;
  ImgSub& s = *d->img;
  if (s.fmt != D3DFMT_A8R8G8B8 && s.fmt != D3DFMT_X8R8G8B8) return D3DERR_INVALIDCALL;
  uint32_t cw = s.w < w ? s.w : w, ch = s.h < h ? s.h : h;
  for (uint32_t y = 0; y < ch; y++) memcpy(s.mem.data() + (size_t) y * s.pitch(), px.data() + (size_t) y * w * 4, cw * 4);
  // keep the host copy of the destination identical
  s.lockRect = {0, 0, (LONG) s.w, (LONG) s.h}; s.lockBox = {0, 0, s.w, s.h, 0, 1}; s.lockWhole = true; s.lockFlags = 0;
  D3DLOCKED_RECT lr; if (SUCCEEDED(lockImg(s, nullptr, nullptr, 0, false, &lr, nullptr))) unlockImg(s, false);
  return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VSwapChain::GetBackBuffer(UINT i, D3DBACKBUFFER_TYPE, IDirect3DSurface9** out) {
  if (!out) return D3DERR_INVALIDCALL; Guard g;
  if (i >= bb.size()) { *out = nullptr; return D3DERR_INVALIDCALL; }
  bb[i]->AddRef(); *out = bb[i]; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VSwapChain::GetRasterStatus(D3DRASTER_STATUS* rs) {
  if (!rs) return D3DERR_INVALIDCALL;
  // estimate from time, as DXVK does (no scanline query under Wine)
  DEVMODEW dm = {}; dm.dmSize = sizeof dm; EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm);
  uint32_t hz = dm.dmDisplayFrequency ? dm.dmDisplayFrequency : 60, lines = (pp.BackBufferHeight ? pp.BackBufferHeight : 1080) + 20;
  LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
  uint64_t period = f.QuadPart / hz; uint64_t pos = (uint64_t) c.QuadPart % period;
  uint32_t line = (uint32_t) (pos * lines / period);
  rs->InVBlank = line >= lines - 20; rs->ScanLine = rs->InVBlank ? 0 : line; return D3D_OK;
}
HRESULT STDMETHODCALLTYPE VSwapChain::GetDisplayMode(D3DDISPLAYMODE* m) { return dev->GetDisplayMode(0, m); }
HRESULT STDMETHODCALLTYPE VSwapChain::GetPresentParameters(D3DPRESENT_PARAMETERS* p) { if (!p) return D3DERR_INVALIDCALL; *p = pp; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VSwapChain::GetLastPresentCount(UINT* c) { if (!c) return D3DERR_INVALIDCALL; *c = presentCount; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VSwapChain::GetPresentStats(D3DPRESENTSTATS* s) { if (!s) return D3DERR_INVALIDCALL; memset(s, 0, sizeof *s); s->PresentCount = presentCount; return D3D_OK; }
HRESULT STDMETHODCALLTYPE VSwapChain::GetDisplayModeEx(D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) { return dev->GetDisplayModeEx(0, m, r); }
void VSwapChain::destroyBackBuffers(bool preserveHost) {
  // Ex surfaces remain usable until their last reference. Keep forwarding to
  // the live chain so every earlier public/private reference stays balanced.
  for (VSurface* s : bb) {
    if (!preserveHost) { if (s->id) freeId(s->id); s->id = 0; s->own.addr.id = 0; }
    s->retiredImplicit = true; s->ownedByDevice = false; s->maybeDelete();
  }
  bb.clear();
}
void VSwapChain::finalRelease() {
  Guard g;
  presenterUnregister(id);
  if (dev) { auto& v = dev->extraSc; for (size_t i = 0; i < v.size(); i++) if (v[i] == this) { v.erase(v.begin() + i); break; } }
  destroyBackBuffers();
  VBase::finalRelease();
}

void releaseStateRefs(VState& s) {
  for (auto& t : s.tex) setRef<VBase>(t, nullptr);
  for (auto& st : s.streams) setRef<VBase>(st.vb, nullptr);
  setRef<VBase>(s.ib, nullptr); setRef<VBase>(s.decl, nullptr); setRef<VBase>(s.vs, nullptr); setRef<VBase>(s.ps, nullptr);
}

}  // namespace vcr
