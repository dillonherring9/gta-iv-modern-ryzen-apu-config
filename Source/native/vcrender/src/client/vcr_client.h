// VCRender game-side client (32-bit): Direct3D 9 objects whose calls are carried to the 64-bit host.
// Design rules learned from b-bridge (see research/claude-bbridge-followup-2026-11-12):
//  * every setter is forwarded; nothing is skipped as "redundant";
//  * getters read a mirror kept with DXVK's exact semantics (state blocks, UP draws, render target 0);
//  * no permanent copies of texture or static buffer data live in this process;
//  * one global lock serializes all devices and the channel.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <bitset>
#include <unordered_map>
#include "../shared/vcr_proto.h"

namespace vcr {

// ---------------------------------------------------------------- core services (vcr_channel.cpp)
void log(const char* fmt, ...);
void lock();
void unlock();
struct Guard { Guard() { lock(); } ~Guard() { unlock(); } Guard(const Guard&) = delete; Guard& operator=(const Guard&) = delete; };

bool ready();                                   // host connected
void* rec(uint16_t op, uint32_t payload, uint16_t flags = 0);   // reserve a record (lock held); returns payload
template<class T> inline T* rec(uint16_t op, uint32_t extra = 0, uint16_t flags = 0) { return (T*) rec(op, sizeof(T) + extra, flags); }
void flush();
uint32_t pushedSeq();                           // non-PAD records pushed so far
HRESULT call(uint16_t op, const void* payload, uint32_t bytes, void* out = nullptr, uint32_t outCap = 0, uint32_t* outBytes = nullptr);
uint32_t respCap();
uint32_t maxInline();                           // largest payload a single record may carry

struct Region { uint8_t* p = nullptr; uint32_t off = 0; uint32_t bytes = 0; bool arena = false; };
bool regionAlloc(uint32_t bytes, Region& r);    // arena memory (or heap fallback) the game can write into
void regionSubmit(Region& r);                   // the record that reads it has been pushed
void regionDrop(Region& r);                     // never sent
void sendChunks(const uint8_t* data, uint32_t bytes);   // fills the host's chunk buffer (OP_UPLOAD_CHUNK)

uint32_t newId();
void freeId(uint32_t id);

VcrControl* control();
int fullscreenSetting();
int framesAheadSetting();   // VCRender.ini MaxFramesAhead: frames the game may run ahead of the picture on screen (1-3, default 1)
void presenterNotePresent(uint32_t frameNo);   // the game's Present time of a frame, for the latency line   // VCRender.ini FullscreenMode: 0 borderless + scale (default), 1 change display mode like DXVK
void waitFrameCompleted(uint32_t scId, int32_t frame);  // throttle
const wchar_t* channelBase();
const wchar_t* pluginDir();
void budgetStart();                             // phase 2 texture budget (vcr_budget.cpp)
void budgetPoll();
int budgetExtraMiB();
void streamDiagStart();                          // failed-load diagnostics (vcr_streamdiag.cpp)
void streamDiagSummary(char* out, size_t cap);
void physPoolStart();                            // separate memory for big physical resource blocks (vcr_physpool.cpp)
void physPoolSummary(char* out, size_t cap);
class VDevice; class VBase;
const wchar_t* privateDir();
void liftViewport(D3DVIEWPORT9& v);
HRESULT interopQueryDevice(VDevice* d, REFIID riid, void** out);      // phase 3 (vcr_interop.cpp)
HRESULT interopQueryTexture(VBase* b, IUnknown* owner, REFIID riid, void** out);
void interopDeviceGone(VDevice* d);
void interopFreeDevice(VDevice* d);
void companionStart();

// presenter (vcr_present.cpp)
struct PresentTarget;
void presenterRegister(uint32_t scId, HWND hwnd);
bool presenterUnregister(uint32_t scId); // true if this target accepted any real GDI frame
void presenterSetWindow(uint32_t scId, HWND hwnd, bool fullscreen, const RECT* src, const RECT* dst);
void presenterSetGamma(uint32_t scId, const D3DGAMMARAMP* ramp);
bool presenterLastFrame(uint32_t scId, std::vector<uint8_t>& bgra, uint32_t& w, uint32_t& h);

// ---------------------------------------------------------------- utilities (vcr_util.cpp)
struct FmtInfo { uint32_t bw, bh, bytes; bool known; };
FmtInfo fmtInfo(D3DFORMAT f);
uint32_t rowPitch(D3DFORMAT f, uint32_t width);         // locked pitch reported to the game
uint32_t blockRows(D3DFORMAT f, uint32_t height);
uint32_t mipDim(uint32_t d, uint32_t level);
uint32_t mipLevels(uint32_t w, uint32_t h, uint32_t d);
uint32_t shaderBytes(const DWORD* code);
uint32_t fvfStride(DWORD fvf);
std::vector<D3DVERTEXELEMENT9> fvfToDecl(DWORD fvf);
DWORD declToFvf(const std::vector<D3DVERTEXELEMENT9>& el);
uint32_t vertexCount(D3DPRIMITIVETYPE t, uint32_t prims);
bool isDepthFormat(D3DFORMAT f);
bool isLockableDepth(D3DFORMAT f);

// ---------------------------------------------------------------- object model
class VDevice;
class VD3D;

class VBase {
public:
  uint32_t magic = 0x4F524356;         // 'VCRO': recognises our objects behind interface pointers
  uint32_t id = 0;
  uint32_t kind = K_NONE;
  VDevice* dev = nullptr;
  VBase* container = nullptr;          // refcounts forward here (subresources)
  bool ownedByDevice = false;          // implicit swapchain objects live until device destruction/reset
  volatile LONG pub = 0, priv = 0;
  volatile LONG forwardedPub = 0, forwardedPriv = 0;
  bool retiredImplicit = false;
  std::unordered_map<GUID, std::vector<uint8_t>, struct GuidHash>* pdata = nullptr;
  std::unordered_map<GUID, IUnknown*, struct GuidHash>* pdataObj = nullptr;
  DWORD priority = 0;
  virtual ~VBase();
  virtual IUnknown* unk() = 0;
  ULONG addRef();
  ULONG release();
  void addPriv();
  void releasePriv();
  void maybeDelete();
  virtual void finalRelease();         // sends OP_DESTROY and deletes this
  HRESULT setPrivate(REFGUID g, const void* data, DWORD size, DWORD flags);
  HRESULT getPrivate(REFGUID g, void* data, DWORD* size);
  HRESULT freePrivate(REFGUID g);
};
struct GuidHash { size_t operator()(const GUID& g) const { const uint32_t* p = (const uint32_t*) &g; return p[0] ^ (p[1] * 31) ^ (p[2] * 131) ^ (p[3] * 1031); } };

template<class T> inline void setRef(T*& slot, T* v) {   // private (binding) reference swap
  if (slot == v) return;
  if (v) static_cast<VBase*>(v)->addPriv();
  T* old = slot; slot = v;
  if (old) static_cast<VBase*>(old)->releasePriv();
}

// one image (surface level / volume level) of any image object
struct ImgSub {
  VBase* owner = nullptr;              // object that owns the storage (texture/cube/voltex or standalone surface/volume)
  VcrSub addr = {};                    // how the host addresses it
  uint32_t w = 0, h = 0, d = 1;
  D3DFORMAT fmt = D3DFMT_UNKNOWN;
  D3DPOOL pool = D3DPOOL_DEFAULT;
  DWORD usage = 0;
  D3DMULTISAMPLE_TYPE ms = D3DMULTISAMPLE_NONE; DWORD msq = 0;
  bool lockable = false;
  bool resident = false;               // client memory exists (SYSTEMMEM / SCRATCH)
  std::vector<uint8_t> mem;            // resident storage, pitch = rowPitch()
  bool gpuWritten = false;             // the host has content the client cannot reproduce
  bool uploaded = false;               // the client has written it before
  bool locked = false;
  DWORD lockFlags = 0;
  RECT lockRect = {}; D3DBOX lockBox = {}; bool lockWhole = true;
  Region region;
  uint32_t lkBl = 0, lkBt = 0, lkRowBytes = 0, lkRows = 0, lkSlices = 1, lkSlicePitch = 0;   // geometry of the current lock
  uint32_t pitch() const { return rowPitch(fmt, w); }
  uint32_t rows() const { return blockRows(fmt, h); }
  uint32_t slicePitch() const { return pitch() * rows(); }
};

// ---- state mirror (DXVK semantics)
enum SBFlag { SB_RS, SB_SS, SB_VB, SB_MATERIAL, SB_TEX, SB_VS, SB_PS, SB_XF, SB_TSS, SB_VIEWPORT, SB_SCISSOR, SB_CLIP,
              SB_VSC, SB_PSC, SB_FREQ, SB_INDICES, SB_DECL, SB_LIGHTS, SB_COUNT };
constexpr uint32_t kSamplers = 21, kSamplerStates = 14, kStages = 8, kStageStates = 33, kStreams = 16, kXforms = 512;

class VShaderBase; class VDecl; class VBufferBase; class VBaseTexture;

struct LightSlot { bool valid = false; bool enabled = false; D3DLIGHT9 light = {}; };
struct VState {
  DWORD rs[256] = {};
  DWORD tss[kStages][kStageStates] = {};
  DWORD ss[kSamplers][kSamplerStates] = {};
  VBase* tex[kSamplers] = {};
  struct Stream { VBase* vb = nullptr; UINT off = 0, stride = 0; } streams[kStreams];
  UINT freq[kStreams] = {};
  VBase* ib = nullptr;
  VBase* decl = nullptr;
  VBase* vs = nullptr; VBase* ps = nullptr;
  std::vector<float> vsF; std::vector<int> vsI; std::vector<BOOL> vsB;
  float psF[224 * 4] = {}; int psI[16 * 4] = {}; BOOL psB[16] = {};
  D3DMATRIX xf[kXforms];
  D3DVIEWPORT9 vp = {};
  D3DMATERIAL9 mat = {};
  std::vector<LightSlot> lights;
  float clip[6][4] = {};
  RECT scissor = {};
};
void releaseStateRefs(VState& s);

struct SBMask {
  std::bitset<SB_COUNT> f;
  std::bitset<256> rs;
  std::bitset<kSamplers> samplers; std::bitset<kSamplerStates> samplerStates[kSamplers];
  std::bitset<kStreams> vbs;
  std::bitset<kSamplers> tex;
  std::bitset<kXforms> xf;
  std::bitset<kStages> stages; std::bitset<kStageStates> stageStates[kStages];
  std::bitset<6> clip;
  std::vector<bool> vsF, vsI, vsB; std::bitset<224> psF; std::bitset<16> psI, psB;
  std::bitset<kStreams> freq;
  std::vector<bool> lightEnable;
};

// ---------------------------------------------------------------- COM objects
#define VCR_UNK_IMPL(IFACE)                                                               \
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;             \
  ULONG STDMETHODCALLTYPE AddRef() override { return addRef(); }                          \
  ULONG STDMETHODCALLTYPE Release() override { return release(); }                        \
  IUnknown* unk() override { return (IUnknown*) static_cast<IFACE*>(this); }

#define VCR_RESOURCE_IMPL(TYPE)                                                                                      \
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;                                          \
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void* d, DWORD s, DWORD f) override { return setPrivate(g, d, s, f); } \
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void* d, DWORD* s) override { return getPrivate(g, d, s); }   \
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return freePrivate(g); }                          \
  DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override;                                                             \
  DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }                                                \
  void STDMETHODCALLTYPE PreLoad() override;                                                                         \
  D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return TYPE; }

class VSurface;
class VVolume;

class VBaseTexture : public VBase {
public:
  D3DFORMAT fmt = D3DFMT_UNKNOWN; D3DPOOL pool = D3DPOOL_DEFAULT; DWORD usage = 0;
  uint32_t levels = 1, faces = 1;
  DWORD lod = 0; D3DTEXTUREFILTERTYPE autoGenFilter = D3DTEXF_LINEAR;
  std::vector<ImgSub> subs;            // faces * levels
  std::vector<VBase*> children;        // VSurface / VVolume per sub (lazy)
  ImgSub* sub(uint32_t face, uint32_t level) { return (face < faces && level < levels) ? &subs[face * levels + level] : nullptr; }
  void finalRelease() override;
  void markGpuWritten() { for (auto& s : subs) s.gpuWritten = true; }
};

class VTexture final : public IDirect3DTexture9, public VBaseTexture {
public:
  VCR_UNK_IMPL(IDirect3DTexture9)
  VCR_RESOURCE_IMPL(D3DRTYPE_TEXTURE)
  DWORD STDMETHODCALLTYPE SetLOD(DWORD) override;
  DWORD STDMETHODCALLTYPE GetLOD() override { return lod; }
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return autoGenFilter; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
  HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9** surf) override;
  HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT* rc) override;
};
class VCubeTexture final : public IDirect3DCubeTexture9, public VBaseTexture {
public:
  VCR_UNK_IMPL(IDirect3DCubeTexture9)
  VCR_RESOURCE_IMPL(D3DRTYPE_CUBETEXTURE)
  DWORD STDMETHODCALLTYPE SetLOD(DWORD) override;
  DWORD STDMETHODCALLTYPE GetLOD() override { return lod; }
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return autoGenFilter; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
  HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** surf) override;
  HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES face, const RECT* rc) override;
};
class VVolumeTexture final : public IDirect3DVolumeTexture9, public VBaseTexture {
public:
  VCR_UNK_IMPL(IDirect3DVolumeTexture9)
  VCR_RESOURCE_IMPL(D3DRTYPE_VOLUMETEXTURE)
  DWORD STDMETHODCALLTYPE SetLOD(DWORD) override;
  DWORD STDMETHODCALLTYPE GetLOD() override { return lod; }
  DWORD STDMETHODCALLTYPE GetLevelCount() override;
  HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override;
  D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return autoGenFilter; }
  void STDMETHODCALLTYPE GenerateMipSubLevels() override;
  HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DVOLUME_DESC* desc) override;
  HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level, IDirect3DVolume9** vol) override;
  HRESULT STDMETHODCALLTYPE LockBox(UINT level, D3DLOCKED_BOX* lb, const D3DBOX* box, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override;
  HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX* box) override;
};

class VSurface final : public IDirect3DSurface9, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DSurface9)
  VCR_RESOURCE_IMPL(D3DRTYPE_SURFACE)
  ImgSub own;                          // used when standalone
  ImgSub* img = nullptr;               // own or the container's sub
  IUnknown* containerIface = nullptr;  // texture / cube / swapchain for GetContainer
  bool registered = false;             // host knows this id
  HDC dc = nullptr; HBITMAP dib = nullptr; void* dibBits = nullptr; HGDIOBJ oldBmp = nullptr;
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void** ppv) override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* desc) override;
  HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* lr, const RECT* rc, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE UnlockRect() override;
  HRESULT STDMETHODCALLTYPE GetDC(HDC* phdc) override;
  HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hdc) override;
  VcrSub addr() const { return registered || !container ? VcrSub{id, K_SURFACE, 0, 0} : img->addr; }
  void finalRelease() override;
};
class VVolume final : public IDirect3DVolume9, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DVolume9)
  ImgSub* img = nullptr;
  IUnknown* containerIface = nullptr;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void* d, DWORD s, DWORD f) override { return setPrivate(g, d, s, f); }
  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void* d, DWORD* s) override { return getPrivate(g, d, s); }
  HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return freePrivate(g); }
  HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void** ppv) override;
  HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC* desc) override;
  HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX* lb, const D3DBOX* box, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE UnlockBox() override;
};

class VBufferBase : public VBase {
public:
  UINT length = 0; DWORD usage = 0; D3DPOOL pool = D3DPOOL_DEFAULT; DWORD fvfOrFmt = 0;
  bool shadowed = false;               // dynamic buffers keep an exact client copy
  std::vector<uint8_t> shadow;
  bool uploaded = false;
  struct LockRec { UINT off, size; DWORD flags; Region region; uint8_t* ptr; };
  uint32_t lockDepth = 0;
  std::vector<LockRec> locks;
  HRESULT lockImpl(UINT off, UINT size, void** pp, DWORD flags);
  HRESULT unlockImpl();
};
class VVertexBuffer final : public IDirect3DVertexBuffer9, public VBufferBase {
public:
  VCR_UNK_IMPL(IDirect3DVertexBuffer9)
  VCR_RESOURCE_IMPL(D3DRTYPE_VERTEXBUFFER)
  HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void** pp, DWORD flags) override { return lockImpl(off, size, pp, flags); }
  HRESULT STDMETHODCALLTYPE Unlock() override { return unlockImpl(); }
  HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* desc) override;
};
class VIndexBuffer final : public IDirect3DIndexBuffer9, public VBufferBase {
public:
  VCR_UNK_IMPL(IDirect3DIndexBuffer9)
  VCR_RESOURCE_IMPL(D3DRTYPE_INDEXBUFFER)
  HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void** pp, DWORD flags) override { return lockImpl(off, size, pp, flags); }
  HRESULT STDMETHODCALLTYPE Unlock() override { return unlockImpl(); }
  HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* desc) override;
};

class VShaderBase : public VBase { public: std::vector<DWORD> code; };
class VVertexShader final : public IDirect3DVertexShader9, public VShaderBase {
public:
  VCR_UNK_IMPL(IDirect3DVertexShader9)
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE GetFunction(void* data, UINT* size) override;
};
class VPixelShader final : public IDirect3DPixelShader9, public VShaderBase {
public:
  VCR_UNK_IMPL(IDirect3DPixelShader9)
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE GetFunction(void* data, UINT* size) override;
};
class VDecl final : public IDirect3DVertexDeclaration9, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DVertexDeclaration9)
  std::vector<D3DVERTEXELEMENT9> el;   // including D3DDECL_END
  DWORD fvf = 0;
  bool hostCreated = false;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* el, UINT* count) override;
  HRESULT ensureHost();
};
class VQuery final : public IDirect3DQuery9, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DQuery9)
  D3DQUERYTYPE type = D3DQUERYTYPE_EVENT;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  D3DQUERYTYPE STDMETHODCALLTYPE GetType() override { return type; }
  DWORD STDMETHODCALLTYPE GetDataSize() override;
  HRESULT STDMETHODCALLTYPE Issue(DWORD flags) override;
  HRESULT STDMETHODCALLTYPE GetData(void* data, DWORD size, DWORD flags) override;
};
class VStateBlock final : public IDirect3DStateBlock9, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DStateBlock9)
  VState st; SBMask m;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE Capture() override;
  HRESULT STDMETHODCALLTYPE Apply() override;
  ~VStateBlock() override { releaseStateRefs(st); }
};

class VSwapChain final : public IDirect3DSwapChain9Ex, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DSwapChain9Ex)
  D3DPRESENT_PARAMETERS pp = {};       // as the game asked (hDeviceWindow = game window)
  std::vector<VSurface*> bb;
  bool implicit = false;
  bool fullscreen = false;
  uint32_t frameNo = 0, presentCount = 0;
  D3DGAMMARAMP gamma = {}; bool gammaSet = false;
  HRESULT STDMETHODCALLTYPE Present(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE GetFrontBufferData(IDirect3DSurface9* dst) override;
  HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** out) override;
  HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* rs) override;
  HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* mode) override;
  HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** ppDevice) override;
  HRESULT STDMETHODCALLTYPE GetPresentParameters(D3DPRESENT_PARAMETERS* p) override;
  HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* c) override;
  HRESULT STDMETHODCALLTYPE GetPresentStats(D3DPRESENTSTATS* s) override;
  HRESULT STDMETHODCALLTYPE GetDisplayModeEx(D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) override;
  void destroyBackBuffers(bool preserveHost = false);
  void finalRelease() override;
};

// ---------------------------------------------------------------- the device
class VDevice final : public IDirect3DDevice9Ex, public VBase {
public:
  VCR_UNK_IMPL(IDirect3DDevice9Ex)
  VD3D* parent = nullptr;
  bool isEx = false;
  D3DDEVICE_CREATION_PARAMETERS cp = {};
  volatile LONG coopState = 0;        // low 2 bits: 0 ready, 1 focus lost, 2 needs Reset; upper bits: activation generation
  D3DCAPS9 caps = {};
  VSwapChain* implicitSc = nullptr;
  DWORD startupWindowTick = GetTickCount();
  bool startupPresented = false, startupRestoreAttempted = false;
  std::vector<VSwapChain*> extraSc;
  VSurface* autoDS = nullptr;
  void* interop = nullptr;             // RemoteInteropDevice (vcr_interop.cpp)
  VState st;
  VBase* rt[4] = {};
  VBase* ds = nullptr;
  VStateBlock* recording = nullptr;
  BOOL swvp = FALSE; float npatch = 0.f; UINT curPalette = 0xFFFF;
  std::unordered_map<UINT, std::vector<PALETTEENTRY>> palettes;
  D3DCLIPSTATUS9 clipStatus = {};
  std::unordered_map<DWORD, VDecl*> fvfDecls;
  uint32_t vsFloatCount = 256, vsIntCount = 16, vsBoolCount = 16;
  INT gpuPriority = 0; UINT maxLatency = 3;
  HCURSOR cursor = nullptr; BOOL cursorShown = FALSE;
  bool inScene = false;
  // fullscreen emulation
  bool fsActive = false, modeChanged = false, fsWindowManaged = false, savedDisplayValid = false, fsPresentRect = false;
  HWND fsWindow = nullptr;  // independent of implicitSc, which Reset replaces before window cleanup
  LONG savedStyle = 0, savedExStyle = 0; RECT savedRect = {}; D3DDISPLAYMODE fsMode = {};
  wchar_t fsDisplay[CCHDEVICENAME] = {}; DEVMODEW savedDisplayMode = {};

  ~VDevice() override;
  void finalRelease() override;
  void resetState(const D3DPRESENT_PARAMETERS& pp);
  void windowActivated(bool activated); // called by the window hook; takes no device lock
  void bindImplicitTargets();
  HRESULT createImplicit(const D3DPRESENT_PARAMETERS& pp, const D3DDISPLAYMODEEX* fsMode, bool reset);
  void applyWindowMode(const D3DPRESENT_PARAMETERS& pp, const D3DDISPLAYMODEEX* fsMode);
  void restoreStartupWindow(const D3DPRESENT_PARAMETERS& pp);
  void leaveFullscreen();
  void capture(VStateBlock* sb, bool creation);
  void apply(VStateBlock* sb);
  VDecl* declForFvf(DWORD fvf);
  // state helpers (respect recording)
  VState& target(SBMask*& m) { if (recording) { m = &recording->m; return recording->st; } m = nullptr; return st; }

  // IDirect3DDevice9
  HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
  UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
  HRESULT STDMETHODCALLTYPE EvictManagedResources() override;
  HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) override;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* c) override;
  HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT sc, D3DDISPLAYMODE* m) override;
  HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override;
  HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT x, UINT y, IDirect3DSurface9* bmp) override;
  void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) override;
  BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override;
  HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pp, IDirect3DSwapChain9** out) override;
  HRESULT STDMETHODCALLTYPE GetSwapChain(UINT i, IDirect3DSwapChain9** out) override;
  UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override;
  HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override;
  HRESULT STDMETHODCALLTYPE Present(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty) override;
  HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT sc, UINT i, D3DBACKBUFFER_TYPE t, IDirect3DSurface9** out) override;
  HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT sc, D3DRASTER_STATUS* rs) override;
  HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL) override { return D3D_OK; }
  void STDMETHODCALLTYPE SetGammaRamp(UINT sc, DWORD flags, const D3DGAMMARAMP* ramp) override;
  void STDMETHODCALLTYPE GetGammaRamp(UINT sc, D3DGAMMARAMP* ramp) override;
  HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DVolumeTexture9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT e, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL lockable, IDirect3DSurface9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL discard, IDirect3DSurface9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* src, const RECT* srcRect, IDirect3DSurface9* dst, const POINT* pt) override;
  HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* src, IDirect3DBaseTexture9* dst) override;
  HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* rt, IDirect3DSurface9* dst) override;
  HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT sc, IDirect3DSurface9* dst) override;
  HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* src, const RECT* sr, IDirect3DSurface9* dst, const RECT* dr, D3DTEXTUREFILTERTYPE f) override;
  HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* s, const RECT* r, D3DCOLOR c) override;
  HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9** out, HANDLE* shared) override;
  HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD i, IDirect3DSurface9* s) override;
  HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD i, IDirect3DSurface9** s) override;
  HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* s) override;
  HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** s) override;
  HRESULT STDMETHODCALLTYPE BeginScene() override;
  HRESULT STDMETHODCALLTYPE EndScene() override;
  HRESULT STDMETHODCALLTYPE Clear(DWORD n, const D3DRECT* r, DWORD flags, D3DCOLOR c, float z, DWORD s) override;
  HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) override;
  HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) override;
  HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) override;
  HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* v) override;
  HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* v) override;
  HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* m) override;
  HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* m) override;
  HRESULT STDMETHODCALLTYPE SetLight(DWORD i, const D3DLIGHT9* l) override;
  HRESULT STDMETHODCALLTYPE GetLight(DWORD i, D3DLIGHT9* l) override;
  HRESULT STDMETHODCALLTYPE LightEnable(DWORD i, BOOL e) override;
  HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD i, BOOL* e) override;
  HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD i, const float* p) override;
  HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD i, float* p) override;
  HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override;
  HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) override;
  HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9** out) override;
  HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
  HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override;
  HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9* s) override;
  HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* s) override;
  HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9** t) override;
  HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9* t) override;
  HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD* v) override;
  HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v) override;
  HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) override;
  HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override;
  HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override;
  HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT n, const PALETTEENTRY* e) override;
  HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT n, PALETTEENTRY* e) override;
  HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT n) override;
  HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* n) override;
  HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* r) override;
  HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* r) override;
  HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL b) override;
  BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return swvp; }
  HRESULT STDMETHODCALLTYPE SetNPatchMode(float n) override;
  float STDMETHODCALLTYPE GetNPatchMode() override { return npatch; }
  HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE t, UINT start, UINT count) override;
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t, INT base, UINT minIdx, UINT numV, UINT startIdx, UINT count) override;
  HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t, UINT count, const void* data, UINT stride) override;
  HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t, UINT minIdx, UINT numV, UINT count, const void* idx, D3DFORMAT fmt, const void* data, UINT stride) override;
  HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src, UINT dst, UINT count, IDirect3DVertexBuffer9* vb, IDirect3DVertexDeclaration9* decl, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* el, IDirect3DVertexDeclaration9** out) override;
  HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* d) override;
  HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** d) override;
  HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override;
  HRESULT STDMETHODCALLTYPE GetFVF(DWORD* fvf) override;
  HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code, IDirect3DVertexShader9** out) override;
  HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* s) override;
  HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** s) override;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT r, const float* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT r, float* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT r, const int* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT r, int* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT r, const BOOL* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT r, BOOL* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE SetStreamSource(UINT i, IDirect3DVertexBuffer9* vb, UINT off, UINT stride) override;
  HRESULT STDMETHODCALLTYPE GetStreamSource(UINT i, IDirect3DVertexBuffer9** vb, UINT* off, UINT* stride) override;
  HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT i, UINT div) override;
  HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT i, UINT* div) override;
  HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* ib) override;
  HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** ib) override;
  HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code, IDirect3DPixelShader9** out) override;
  HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* s) override;
  HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** s) override;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT r, const float* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT r, float* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT r, const int* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT r, int* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT r, const BOOL* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT r, BOOL* d, UINT n) override;
  HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return D3DERR_INVALIDCALL; }
  HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE t, IDirect3DQuery9** out) override;
  // IDirect3DDevice9Ex
  HRESULT STDMETHODCALLTYPE SetConvolutionMonoKernel(UINT w, UINT h, float* rows, float* cols) override;
  HRESULT STDMETHODCALLTYPE ComposeRects(IDirect3DSurface9*, IDirect3DSurface9*, IDirect3DVertexBuffer9*, UINT, IDirect3DVertexBuffer9*, D3DCOMPOSERECTSOP, INT, INT) override { return D3D_OK; }
  HRESULT STDMETHODCALLTYPE PresentEx(const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags) override;
  HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* p) override;
  HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT p) override;
  HRESULT STDMETHODCALLTYPE WaitForVBlank(UINT sc) override;
  HRESULT STDMETHODCALLTYPE CheckResourceResidency(IDirect3DResource9** r, UINT32 n) override;
  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT n) override;
  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* n) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceState(HWND w) override;
  HRESULT STDMETHODCALLTYPE CreateRenderTargetEx(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL lockable, IDirect3DSurface9** out, HANDLE* shared, DWORD usage) override;
  HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurfaceEx(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9** out, HANDLE* shared, DWORD usage) override;
  HRESULT STDMETHODCALLTYPE CreateDepthStencilSurfaceEx(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD msq, BOOL discard, IDirect3DSurface9** out, HANDLE* shared, DWORD usage) override;
  HRESULT STDMETHODCALLTYPE ResetEx(D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode) override;
  HRESULT STDMETHODCALLTYPE GetDisplayModeEx(UINT sc, D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) override;

  HRESULT resetImpl(D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* mode, bool ex);
  HRESULT presentImpl(VSwapChain* sc, const RECT* src, const RECT* dst, HWND wnd, DWORD flags);
};

class VD3D final : public IDirect3D9Ex {
public:
  volatile LONG refs = 1;
  bool isEx = false;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }
  HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3D_OK; }
  UINT STDMETHODCALLTYPE GetAdapterCount() override;
  HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT a, DWORD flags, D3DADAPTER_IDENTIFIER9* id) override;
  UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT f) override;
  HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT f, UINT i, D3DDISPLAYMODE* m) override;
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* m) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT bf, BOOL win) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT af, DWORD usage, D3DRESOURCETYPE rt, D3DFORMAT cf) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE t, D3DFORMAT f, BOOL win, D3DMULTISAMPLE_TYPE ms, DWORD* q) override;
  HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT af, D3DFORMAT rf, D3DFORMAT df) override;
  HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT a, D3DDEVTYPE t, D3DFORMAT s, D3DFORMAT d) override;
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) override;
  HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT a) override;
  HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) override;
  UINT STDMETHODCALLTYPE GetAdapterModeCountEx(UINT a, const D3DDISPLAYMODEFILTER* f) override;
  HRESULT STDMETHODCALLTYPE EnumAdapterModesEx(UINT a, const D3DDISPLAYMODEFILTER* f, UINT i, D3DDISPLAYMODEEX* m) override;
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayModeEx(UINT a, D3DDISPLAYMODEEX* m, D3DDISPLAYROTATION* r) override;
  HRESULT STDMETHODCALLTYPE CreateDeviceEx(UINT a, D3DDEVTYPE t, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fs, IDirect3DDevice9Ex** out) override;
  HRESULT STDMETHODCALLTYPE GetAdapterLUID(UINT a, LUID* l) override;
  HRESULT createDevice(UINT a, D3DDEVTYPE t, HWND focus, DWORD beh, D3DPRESENT_PARAMETERS* pp, D3DDISPLAYMODEEX* fs, bool ex, VDevice** out);
};

// helpers shared between translation units
VSurface* asSurface(IDirect3DSurface9* s);
VBaseTexture* asTexture(IDirect3DBaseTexture9* t);
VBase* baseOf(IDirect3DBaseTexture9* t);
IDirect3DBaseTexture9* texIface(VBase* b);
uint32_t texKind(VBase* b);
VcrSub subAddr(VSurface* s);
void registerSurface(VSurface* s);
HRESULT lockImg(ImgSub& s, const RECT* rc, const D3DBOX* box, DWORD flags, bool volume, D3DLOCKED_RECT* lr, D3DLOCKED_BOX* lb);
HRESULT unlockImg(ImgSub& s, bool volume);
HRESULT readImg(ImgSub& s, uint8_t* dst, uint32_t dstPitch, uint32_t dstSlicePitch, int32_t l, int32_t t, int32_t r, int32_t b, int32_t front, int32_t back, bool whole);
void uploadWholeResident(ImgSub& s);
VSurface* surfaceFor(VBaseTexture* t, uint32_t face, uint32_t level, IUnknown* containerIface);
void initTexture(VBaseTexture* t, uint32_t w, uint32_t h, uint32_t d);
HRESULT moduleQuery(uint16_t op, const PAdapterQ& q, void* out, uint32_t cap);
bool fullscreenMode(D3DDISPLAYMODE* m);


}  // namespace vcr
