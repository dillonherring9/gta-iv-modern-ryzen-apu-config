// Phase 3, host half: OP_REMOTEVK runs DXVK's interop and DLSS-IV's Vulkan subset on the real DXVK x64 device, in order
// with the game's D3D9 calls; OP_UPSCALE runs the native FSR service (../upscale/vc_upscale_server.cpp).
// Ported from b-bridge's server/vc_remotevk_server.cpp and vc_upscale_dispatch.cpp (Vice City NextGen, 2026-11-01/11),
// without b-bridge's present ring and NT-handle import/export, which VCRender does not need.
#include <windows.h>
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <d3d9.h>
#include <stdio.h>
#include <wchar.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "../upscale/dxvk_interop_min.h"
#include "../upscale/vc_upscale_server.h"
#include "../shared/vcr_vk_wire.h"
#include "../shared/vcr_proto.h"
#include "vcr_host_vk.h"


using namespace vcrvk;

void hostLog(const char* fmt, ...);

namespace {

struct State {
  IDirect3DDevice9* d3d = nullptr;
  ID3D9VkInteropDevice* idev = nullptr;
  VkInstance inst = VK_NULL_HANDLE; VkPhysicalDevice phys = VK_NULL_HANDLE; VkDevice dev = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE; uint32_t queueIndex = 0, queueFamily = 0;
  bool queueLocked = false, deviceLocked = false;
  std::unordered_set<uint64_t> cbs;   // VkCommandBuffer tokens this host allocated
  PFN_vkGetInstanceProcAddr gipa = nullptr;
  PFN_vkGetDeviceProcAddr gdpa = nullptr;
#define VCF(name) PFN_vk##name name = nullptr;
  VCF(GetPhysicalDeviceProperties2) VCF(GetPhysicalDeviceImageFormatProperties2)
  VCF(CreateSemaphore) VCF(DestroySemaphore) VCF(WaitSemaphores) VCF(GetSemaphoreCounterValue) VCF(SignalSemaphore)
  VCF(CreateCommandPool) VCF(DestroyCommandPool) VCF(AllocateCommandBuffers) VCF(BeginCommandBuffer) VCF(EndCommandBuffer)
  VCF(CmdPipelineBarrier) VCF(CmdCopyImage) VCF(CmdBlitImage) VCF(QueueSubmit) VCF(DeviceWaitIdle)
#undef VCF
} S;
SRWLOCK gStateLock = SRWLOCK_INIT;

bool LoadFunctions() {
  HMODULE m = GetModuleHandleA("winevulkan.dll");
  if (!m) m = GetModuleHandleA("vulkan-1.dll");
  if (!m) { hostLog("[interop] no Vulkan loader module in the host"); return false; }
  S.gipa = (PFN_vkGetInstanceProcAddr) GetProcAddress(m, "vkGetInstanceProcAddr");
  S.gdpa = (PFN_vkGetDeviceProcAddr) GetProcAddress(m, "vkGetDeviceProcAddr");
  if (!S.gipa || !S.gdpa) return false;
  bool ok = true;
#define VCI(name) S.name = (PFN_vk##name) S.gipa(S.inst, "vk" #name); \
  if (!S.name) S.name = (PFN_vk##name) S.gipa(S.inst, "vk" #name "KHR"); ok &= S.name != nullptr;
#define VCD(name) S.name = (PFN_vk##name) S.gdpa(S.dev, "vk" #name); \
  if (!S.name) S.name = (PFN_vk##name) S.gdpa(S.dev, "vk" #name "KHR"); \
  if (!S.name) { hostLog("[interop] missing vk" #name); ok = false; }
  VCI(GetPhysicalDeviceProperties2) VCI(GetPhysicalDeviceImageFormatProperties2)
  VCD(CreateSemaphore) VCD(DestroySemaphore) VCD(WaitSemaphores) VCD(GetSemaphoreCounterValue) VCD(SignalSemaphore)
  VCD(CreateCommandPool) VCD(DestroyCommandPool) VCD(AllocateCommandBuffers) VCD(BeginCommandBuffer) VCD(EndCommandBuffer)
  VCD(CmdPipelineBarrier) VCD(CmdCopyImage) VCD(CmdBlitImage) VCD(QueueSubmit) VCD(DeviceWaitIdle)
#undef VCI
#undef VCD
  return ok;
}

void ReadRange(Reader& r, VkImageSubresourceRange& x) {
  x.aspectMask = r.u32(); x.baseMipLevel = r.u32(); x.levelCount = r.u32(); x.baseArrayLayer = r.u32(); x.layerCount = r.u32();
}
void ReadLayers(Reader& r, VkImageSubresourceLayers& x) { x.aspectMask = r.u32(); x.mipLevel = r.u32(); x.baseArrayLayer = r.u32(); x.layerCount = r.u32(); }
void ReadOffset(Reader& r, VkOffset3D& o) { o.x = r.i32(); o.y = r.i32(); o.z = r.i32(); }
VcrSub ReadSub(Reader& r) { VcrSub s; s.id = r.u32(); s.kind = r.u32(); s.face = r.u32(); s.level = r.u32(); return s; }

// Replays one recorded client stream into a real command buffer. Returns false on a malformed stream.
bool Replay(VkCommandBuffer cb, Reader& r, uint32_t len) {
  const size_t end = r.at + len;
  if (end > r.n) return false;
  while (r.at < end && !r.bad) {
    const uint32_t cmd = r.u32();
    if (cmd == CmdPipelineBarrier) {
      const uint32_t src = r.u32(), dst = r.u32(), dep = r.u32();
      const uint32_t memN = r.u32();
      if (memN > 64) return false;
      std::vector<VkMemoryBarrier> mem(memN);
      for (auto& m : mem) { m = { VK_STRUCTURE_TYPE_MEMORY_BARRIER }; m.srcAccessMask = r.u32(); m.dstAccessMask = r.u32(); }
      const uint32_t imgN = r.u32();
      if (imgN > 64) return false;
      std::vector<VkImageMemoryBarrier> img(imgN);
      for (auto& b : img) {
        b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = r.u32(); b.dstAccessMask = r.u32();
        b.oldLayout = (VkImageLayout) r.u32(); b.newLayout = (VkImageLayout) r.u32();
        b.srcQueueFamilyIndex = r.u32(); b.dstQueueFamilyIndex = r.u32();
        b.image = (VkImage) r.u64(); ReadRange(r, b.subresourceRange);
      }
      if (r.bad) return false;
      S.CmdPipelineBarrier(cb, src, dst, dep, memN, mem.data(), 0, nullptr, imgN, img.data());
    } else if (cmd == CmdCopyImage) {
      const VkImage src = (VkImage) r.u64(); const VkImageLayout sl = (VkImageLayout) r.u32();
      const VkImage dst = (VkImage) r.u64(); const VkImageLayout dl = (VkImageLayout) r.u32();
      const uint32_t n = r.u32();
      if (n > 64) return false;
      std::vector<VkImageCopy> reg(n);
      for (auto& g : reg) {
        ReadLayers(r, g.srcSubresource); ReadOffset(r, g.srcOffset);
        ReadLayers(r, g.dstSubresource); ReadOffset(r, g.dstOffset);
        g.extent.width = r.u32(); g.extent.height = r.u32(); g.extent.depth = r.u32();
      }
      if (r.bad) return false;
      S.CmdCopyImage(cb, src, sl, dst, dl, n, reg.data());
    } else if (cmd == CmdBlitImage) {
      const VkImage src = (VkImage) r.u64(); const VkImageLayout sl = (VkImageLayout) r.u32();
      const VkImage dst = (VkImage) r.u64(); const VkImageLayout dl = (VkImageLayout) r.u32();
      const VkFilter filter = (VkFilter) r.u32();
      const uint32_t n = r.u32();
      if (n > 64) return false;
      std::vector<VkImageBlit> reg(n);
      for (auto& g : reg) {
        ReadLayers(r, g.srcSubresource); ReadOffset(r, g.srcOffsets[0]); ReadOffset(r, g.srcOffsets[1]);
        ReadLayers(r, g.dstSubresource); ReadOffset(r, g.dstOffsets[0]); ReadOffset(r, g.dstOffsets[1]);
      }
      if (r.bad) return false;
      S.CmdBlitImage(cb, src, sl, dst, dl, n, reg.data(), filter);
    } else {
      return false;
    }
  }
  return !r.bad && r.at == end;
}

// DXVK's interop calls take no device lock, and the host's frame-copier thread uses the device too: interop calls run
// inside LockDevice/UnlockDevice (unless DLSS-IV holds it itself through OpLockDevice).
struct DevLock {
  bool held = false;
  DevLock() { if (S.idev && !S.deviceLocked) { S.idev->LockDevice(); held = true; } }
  ~DevLock() { if (held) S.idev->UnlockDevice(); }
};
bool GetInteropTexture(IUnknown* res, ID3D9VkInteropTexture** out) {
  *out = nullptr;
  return res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D9VkInteropTexture), (void**) out)) && *out;
}

// ---- side channel: semaphore waits and counter reads, off the ordered stream
bool ReadAll(HANDLE h, void* p, DWORD n) {
  uint8_t* c = (uint8_t*) p;
  while (n) { DWORD got = 0; if (!ReadFile(h, c, n, &got, nullptr) || !got) return false; c += got; n -= got; }
  return true;
}
bool WriteAll(HANDLE h, const void* p, DWORD n) {
  const uint8_t* c = (const uint8_t*) p;
  while (n) { DWORD put = 0; if (!WriteFile(h, c, n, &put, nullptr) || !put) return false; c += put; n -= put; }
  return true;
}
void SideOp(const uint8_t* data, uint32_t len, Writer& reply) {
  AcquireSRWLockShared(&gStateLock);
  Reader r(data, len);
  const uint32_t op = r.u32();
  if (op == OpWaitSemaphores) {
    VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    wi.flags = r.u32();
    const uint32_t n = r.u32();
    if (n > 16 || !S.WaitSemaphores || !S.dev) reply.i32(VK_ERROR_UNKNOWN);
    else {
      VkSemaphore sems[16]; uint64_t vals[16];
      for (uint32_t i = 0; i < n; i++) { sems[i] = (VkSemaphore) r.u64(); vals[i] = r.u64(); }
      const uint64_t requested = r.u64();
      const uint64_t timeout = requested < 5000000000ull ? requested : 5000000000ull;
      wi.semaphoreCount = n; wi.pSemaphores = sems; wi.pValues = vals;
      reply.i32(r.bad ? VK_ERROR_UNKNOWN : S.WaitSemaphores(S.dev, &wi, timeout));
    }
  } else if (op == OpSemaphoreCounter) {
    uint64_t v = 0;
    const VkSemaphore sem = (VkSemaphore) r.u64();
    reply.i32(r.bad || !S.GetSemaphoreCounterValue || !S.dev ? VK_ERROR_UNKNOWN : S.GetSemaphoreCounterValue(S.dev, sem, &v));
    reply.u64(v);
  } else {
    reply.i32(VK_ERROR_UNKNOWN);
  }
  ReleaseSRWLockShared(&gStateLock);
}
DWORD WINAPI SideWorker(void* arg) {
  HANDLE pipe = (HANDLE) arg;
  std::vector<uint8_t> buf;
  for (;;) {
    uint32_t len = 0;
    if (!ReadAll(pipe, &len, 4) || len > 4096) break;
    buf.resize(len);
    if (!ReadAll(pipe, buf.data(), len)) break;
    Writer reply; SideOp(buf.data(), len, reply);
    const uint32_t rl = (uint32_t) reply.b.size();
    if (!WriteAll(pipe, &rl, 4) || !WriteAll(pipe, reply.b.data(), rl)) break;
  }
  DisconnectNamedPipe(pipe); CloseHandle(pipe);
  return 0;
}
wchar_t gPipeName[200];
DWORD WINAPI SideListener(void*) {
  for (;;) {
    HANDLE p = CreateNamedPipeW(gPipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
    if (p == INVALID_HANDLE_VALUE) { hostLog("[interop] side pipe: CreateNamedPipe failed %lu", GetLastError()); return 1; }
    if (!ConnectNamedPipe(p, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) { CloseHandle(p); continue; }
    HANDLE t = CreateThread(nullptr, 0, SideWorker, p, 0, nullptr);
    if (t) CloseHandle(t); else CloseHandle(p);
  }
}

bool ResetLocked() {
  if (S.d3d && VCB_ServerUpscaleDeviceQuarantined(S.d3d)) {
    hostLog("[interop] quarantined session pinned; idle drain skipped until process exit");
    return false;
  }
  if (S.queueLocked && S.idev) { S.idev->ReleaseSubmissionQueue(); S.queueLocked = false; }
  if (S.deviceLocked && S.idev) { S.idev->UnlockDevice(); S.deviceLocked = false; }
  if (S.idev) {
    { S.idev->LockDevice(); S.idev->FlushRenderingCommands(); S.idev->UnlockDevice(); }
    if (S.DeviceWaitIdle) S.DeviceWaitIdle(S.dev);
    S.idev->Release();
  }
  S = State();
  return true;
}

// upscale results per device, for VCB_OP_STATUS
std::unordered_map<uint32_t, VCBResult> gLast;
int gUpscaleLogged;

}  // namespace

static void serviceLog(const char* m) { hostLog("[upscale] %s", m); }
void vkStartSideChannel(const wchar_t* base) {
  VCB_ServerUpscaleSetLog(serviceLog);
  const wchar_t* tail = wcsrchr(base, L'\\'); tail = tail ? tail + 1 : base;
  swprintf(gPipeName, 200, L"\\\\.\\pipe\\%ls_vk", tail);
  HANDLE t = CreateThread(nullptr, 0, SideListener, nullptr, 0, nullptr);
  if (t) CloseHandle(t);
}

// Before a device Reset or its destruction: the FSR service lets go of the device; the Vulkan session ends with it.
bool vkDeviceQuarantined(IDirect3DDevice9* d) { return VCB_ServerUpscaleDeviceQuarantined(d); }
bool vkDeviceReset(IDirect3DDevice9* d, uint32_t devId) {
  const bool safe = VCB_ServerUpscaleShutdown(d);
  gLast.erase(devId);
  return safe && !vkDeviceQuarantined(d);
}
bool vkDeviceGone(IDirect3DDevice9* d, uint32_t devId) {
  if (!vkDeviceReset(d, devId)) return false;
  AcquireSRWLockExclusive(&gStateLock);
  if (S.d3d == d) { ResetLocked(); hostLog("[interop] Vulkan session ended with device %u", devId); }
  ReleaseSRWLockExclusive(&gStateLock);
  return true;
}

bool vkHandle(const VkLookup& lk, const uint8_t* data, uint32_t len, Writer& reply) {
  Reader r(data, len);
  const uint32_t op = r.u32();
  const bool needReply = hasReply(op);
  if (op == OpHello) {
    const uint32_t ver = r.u32(), pid = r.u32(), devId = r.u32();
    IDirect3DDevice9* device = lk.device(devId);
    AcquireSRWLockExclusive(&gStateLock);
    const bool detached = S.d3d == device || ResetLocked();
    bool ok = detached && ver == kWireVersion && device && !vkDeviceQuarantined(device);
    if (ok && !S.idev) ok = SUCCEEDED(device->QueryInterface(__uuidof(ID3D9VkInteropDevice), (void**) &S.idev)) && S.idev;
    if (ok) {
      S.d3d = device;
      S.idev->GetVulkanHandles(&S.inst, &S.phys, &S.dev);
      S.idev->GetSubmissionQueue(&S.queue, &S.queueIndex, &S.queueFamily);
      ok = S.dev && S.queue && LoadFunctions();
    }
    hostLog("[interop] Vulkan session for device %u (game pid %u, wire %u): %s", devId, pid, ver, ok ? "ready" : "FAILED");
    reply.i32(ok ? 1 : 0); reply.u32(S.queueIndex); reply.u32(S.queueFamily);
    if (!ok) ResetLocked();
    ReleaseSRWLockExclusive(&gStateLock);
    return true;
  }
  if (S.d3d && vkDeviceQuarantined(S.d3d)) {
    if (needReply) reply.i32(VK_ERROR_DEVICE_LOST);
    return needReply;
  }
  if (!S.idev) {
    static int said; if (said++ < 20) hostLog("[interop] op %u before the session started", op);
    if (needReply) reply.i32(VK_ERROR_INITIALIZATION_FAILED);
    return needReply;
  }
  switch (op) {
  case OpPhysProps: {
    VkPhysicalDeviceIDProperties id = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
    VkPhysicalDeviceProperties2 p = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id };
    S.GetPhysicalDeviceProperties2(S.phys, &p);
    reply.raw(p.properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
    reply.u32(p.properties.vendorID); reply.u32(p.properties.deviceID); reply.u32(p.properties.driverVersion);
    reply.u32(p.properties.apiVersion); reply.u32(p.properties.deviceType);
    reply.raw(id.deviceUUID, VK_UUID_SIZE); reply.raw(id.driverUUID, VK_UUID_SIZE); reply.raw(id.deviceLUID, VK_LUID_SIZE);
    reply.u32(id.deviceNodeMask); reply.u32(id.deviceLUIDValid);
    break;
  }
  case OpImageFormatProps2: {
    VkPhysicalDeviceExternalImageFormatInfo ext = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO };
    VkPhysicalDeviceImageFormatInfo2 in = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2 };
    in.format = (VkFormat) r.u32(); in.type = (VkImageType) r.u32(); in.tiling = (VkImageTiling) r.u32();
    in.usage = r.u32(); in.flags = r.u32();
    ext.handleType = (VkExternalMemoryHandleTypeFlagBits) r.u32();
    if (ext.handleType) in.pNext = &ext;
    VkExternalImageFormatProperties ep = { VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES };
    VkImageFormatProperties2 out = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, ext.handleType ? &ep : nullptr };
    const VkResult res = S.GetPhysicalDeviceImageFormatProperties2(S.phys, &in, &out);
    const VkImageFormatProperties& f = out.imageFormatProperties;
    reply.i32(res);
    reply.u32(f.maxExtent.width); reply.u32(f.maxExtent.height); reply.u32(f.maxExtent.depth);
    reply.u32(f.maxMipLevels); reply.u32(f.maxArrayLayers); reply.u32(f.sampleCounts); reply.u64(f.maxResourceSize);
    reply.u32(ep.externalMemoryProperties.externalMemoryFeatures);
    reply.u32(ep.externalMemoryProperties.exportFromImportedHandleTypes);
    reply.u32(ep.externalMemoryProperties.compatibleHandleTypes);
    break;
  }
  case OpImageInfo: {
    const VcrSub sub = ReadSub(r);
    ID3D9VkInteropTexture* t = nullptr;
    VkImage img = VK_NULL_HANDLE; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    HRESULT hr = E_FAIL;
    bool have = false;
    { DevLock dl; if ((have = GetInteropTexture(lk.resource(sub), &t))) { hr = t->GetVulkanImageInfo(&img, &layout, &ci); t->Release(); } }
    if (!have) hostLog("[interop] image info: resource %u (kind %u) has no interop texture", sub.id, sub.kind);
    static int said = 0;
    if (said < 24) {
      said++;
      hostLog("[interop] image info #%d: resource %u kind %u -> hr 0x%x image 0x%llx %ux%u fmt %d usage 0x%x layout %d", said, sub.id, sub.kind,
              (unsigned) hr, (unsigned long long) img, ci.extent.width, ci.extent.height, (int) ci.format, (unsigned) ci.usage, (int) layout);
    }
    reply.i32(hr); reply.u64((uint64_t) img); reply.i32(layout);
    reply.u32(ci.flags); reply.u32(ci.imageType); reply.u32(ci.format);
    reply.u32(ci.extent.width); reply.u32(ci.extent.height); reply.u32(ci.extent.depth);
    reply.u32(ci.mipLevels); reply.u32(ci.arrayLayers); reply.u32(ci.samples); reply.u32(ci.tiling); reply.u32(ci.usage);
    reply.u32(ci.sharingMode); reply.u32(ci.initialLayout);
    break;
  }
  case OpTransition: {
    const VcrSub sub = ReadSub(r);
    VkImageSubresourceRange range; ReadRange(r, range);
    const VkImageLayout from = (VkImageLayout) r.u32(), to = (VkImageLayout) r.u32();
    ID3D9VkInteropTexture* t = nullptr;
    if (!r.bad) { DevLock dl; if (GetInteropTexture(lk.resource(sub), &t)) { S.idev->TransitionTextureLayout(t, &range, from, to); t->Release(); } }
    break;
  }
  case OpFlush: { DevLock dl; S.idev->FlushRenderingCommands(); break; }
  case OpLockQueue: if (!S.queueLocked) { S.idev->LockSubmissionQueue(); S.queueLocked = true; } break;
  case OpReleaseQueue: if (S.queueLocked) { S.idev->ReleaseSubmissionQueue(); S.queueLocked = false; } break;
  case OpLockDevice: if (!S.deviceLocked) { S.idev->LockDevice(); S.deviceLocked = true; } break;
  case OpUnlockDevice: if (S.deviceLocked) { S.idev->UnlockDevice(); S.deviceLocked = false; } break;
  case OpCreateSemaphore: {
    VkSemaphoreTypeCreateInfo tci = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    VkSemaphoreCreateInfo ci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    tci.semaphoreType = (VkSemaphoreType) r.u32(); tci.initialValue = r.u64();
    if (tci.semaphoreType || tci.initialValue) ci.pNext = &tci;
    VkSemaphore s = VK_NULL_HANDLE;
    reply.i32(r.bad ? VK_ERROR_UNKNOWN : S.CreateSemaphore(S.dev, &ci, nullptr, &s)); reply.u64((uint64_t) s);
    break;
  }
  case OpDestroySemaphore: { const uint64_t s = r.u64(); if (!r.bad && s) S.DestroySemaphore(S.dev, (VkSemaphore) s, nullptr); break; }
  case OpWaitSemaphores: case OpSemaphoreCounter: { Writer w; SideOp(data, len, w); reply.b = w.b; break; }
  case OpSignalSemaphore: {
    VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO };
    si.semaphore = (VkSemaphore) r.u64(); si.value = r.u64();
    reply.i32(r.bad ? VK_ERROR_UNKNOWN : S.SignalSemaphore(S.dev, &si));
    break;
  }
  case OpCreateCommandPool: {
    VkCommandPoolCreateInfo ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    // always resettable per buffer: every submit re-records the client's stream with a fresh vkBeginCommandBuffer
    ci.flags = r.u32() | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; ci.queueFamilyIndex = r.u32();
    VkCommandPool p = VK_NULL_HANDLE;
    reply.i32(r.bad ? VK_ERROR_UNKNOWN : S.CreateCommandPool(S.dev, &ci, nullptr, &p)); reply.u64((uint64_t) p);
    break;
  }
  case OpDestroyCommandPool: { const uint64_t p = r.u64(); if (!r.bad && p) S.DestroyCommandPool(S.dev, (VkCommandPool) p, nullptr); break; }
  case OpAllocCommandBuffers: {
    VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    ai.commandPool = (VkCommandPool) r.u64(); ai.level = (VkCommandBufferLevel) r.u32(); ai.commandBufferCount = r.u32();
    if (r.bad || ai.commandBufferCount > 64) { reply.i32(VK_ERROR_UNKNOWN); break; }
    std::vector<VkCommandBuffer> cbs(ai.commandBufferCount);
    const VkResult res = S.AllocateCommandBuffers(S.dev, &ai, cbs.data());
    reply.i32(res);
    for (VkCommandBuffer c : cbs) { reply.u64((uint64_t) (uintptr_t) c); if (res == VK_SUCCESS) S.cbs.insert((uint64_t) (uintptr_t) c); }
    break;
  }
  case OpQueueSubmit: {
    const uint32_t n = r.u32();
    if (n > 8) { hostLog("[interop] submit with %u batches refused", n); break; }
    struct Sub { std::vector<VkSemaphore> ws; std::vector<VkPipelineStageFlags> wst; std::vector<uint64_t> wv;
                 std::vector<VkCommandBuffer> cbs; std::vector<VkSemaphore> ss; std::vector<uint64_t> sv; VkTimelineSemaphoreSubmitInfo tl; };
    std::vector<Sub> subs(n);
    VkResult res = VK_SUCCESS;
    for (Sub& s : subs) {
      const uint32_t wn = r.u32();
      if (wn > 16) { r.bad = true; break; }
      for (uint32_t i = 0; i < wn && !r.bad; i++) { s.ws.push_back((VkSemaphore) r.u64()); s.wst.push_back(r.u32()); s.wv.push_back(r.u64()); }
      const uint32_t cn = r.u32();
      if (cn > 16) { r.bad = true; break; }
      for (uint32_t i = 0; i < cn && !r.bad; i++) {
        const uint64_t tok = r.u64(); const uint32_t flags = r.u32(); const uint32_t clen = r.u32();
        if (!S.cbs.count(tok)) { r.bad = true; break; }
        VkCommandBuffer cb = (VkCommandBuffer) (uintptr_t) tok;
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = flags;
        S.BeginCommandBuffer(cb, &bi);
        if (!Replay(cb, r, clen)) r.bad = true;
        S.EndCommandBuffer(cb);
        s.cbs.push_back(cb);
      }
      const uint32_t sn = r.u32();
      if (sn > 16) { r.bad = true; break; }
      for (uint32_t i = 0; i < sn && !r.bad; i++) { s.ss.push_back((VkSemaphore) r.u64()); s.sv.push_back(r.u64()); }
    }
    if (r.bad) { hostLog("[interop] malformed submit refused"); break; }
    std::vector<VkSubmitInfo> si(n);
    for (uint32_t i = 0; i < n; i++) {
      Sub& s = subs[i];
      s.tl = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
      s.tl.waitSemaphoreValueCount = (uint32_t) s.wv.size(); s.tl.pWaitSemaphoreValues = s.wv.data();
      s.tl.signalSemaphoreValueCount = (uint32_t) s.sv.size(); s.tl.pSignalSemaphoreValues = s.sv.data();
      si[i] = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &s.tl };
      si[i].waitSemaphoreCount = (uint32_t) s.ws.size(); si[i].pWaitSemaphores = s.ws.data(); si[i].pWaitDstStageMask = s.wst.data();
      si[i].commandBufferCount = (uint32_t) s.cbs.size(); si[i].pCommandBuffers = s.cbs.data();
      si[i].signalSemaphoreCount = (uint32_t) s.ss.size(); si[i].pSignalSemaphores = s.ss.data();
    }
    // DXVK's queue lock: DLSS-IV normally holds it (OpLockQueue) around its submit; taken here if it did not
    const bool take = !S.queueLocked;
    if (take) S.idev->LockSubmissionQueue();
    res = S.QueueSubmit(S.queue, n, si.data(), VK_NULL_HANDLE);
    if (take) S.idev->ReleaseSubmissionQueue();
    if (res != VK_SUCCESS) hostLog("[interop] vkQueueSubmit failed: %d", (int) res);
    break;
  }
  default:
    hostLog("[interop] unknown op %u", op);
    if (needReply) reply.i32(VK_ERROR_UNKNOWN);
  }
  if (r.bad) hostLog("[interop] op %u: truncated payload", op);
  return needReply;
}

// OP_UPSCALE: decode, resolve the device's own textures, run the native service between the scene and the HUD.
void upscaleHandle(const VkLookup& lk, const uint8_t* payload, uint32_t bytes, VCBResult& result) {
  VCB_InitResult(result, E_INVALIDARG, "invalid upscale wire request");
  if (!payload || bytes < sizeof(VCBWireRequest)) return;
  VCBWireRequest q; memcpy(&q, payload, sizeof q);
  result.sequence = q.frame.sequence;
  if (q.size != sizeof q || q.version != VCB_ABI_VERSION) return;
  IDirect3DDevice9* device = lk.device(q.device_id);
  if (!device) { result.hr = D3DERR_DEVICELOST; return; }
  if (q.operation == VCB_OP_SHUTDOWN) {
    if (!vkDeviceReset(device, q.device_id)) {
      VCB_InitResult(result, D3DERR_DEVICELOST, "upscale teardown quarantined; restart required"); return;
    }
    VCB_InitResult(result, S_OK, "upscale service shut down"); result.flags = VCB_RESULT_SHUTDOWN; return;
  }
  if (q.operation == VCB_OP_STATUS) {
    auto it = gLast.find(q.device_id);
    if (it != gLast.end()) result = it->second; else VCB_InitResult(result, S_FALSE, "upscale service has not received a frame");
    return;
  }
  if (q.operation != VCB_OP_FRAME || !VCB_ValidConfig(q.config) || !VCB_ValidFrame(q.frame) || !q.scene_id || !q.home_id) return;
  auto it = gLast.find(q.device_id);
  if (it != gLast.end() && q.frame.sequence <= it->second.sequence && !(q.frame.flags & VCB_FRAME_RESET)) {
    VCB_InitResult(result, E_INVALIDARG, "stale upscale frame sequence"); result.sequence = q.frame.sequence; return;
  }
  IDirect3DTexture9* scene = lk.texture(q.scene_id); IDirect3DTexture9* home = lk.texture(q.home_id);
  IDirect3DTexture9* depth = q.depth_id ? lk.texture(q.depth_id) : nullptr; IDirect3DTexture9* mv = q.mv_id ? lk.texture(q.mv_id) : nullptr;
  if (!scene || !home || (q.depth_id && !depth) || (q.mv_id && !mv) ||
      lk.deviceOf(q.scene_id) != q.device_id || lk.deviceOf(q.home_id) != q.device_id) {
    VCB_InitResult(result, D3DERR_INVALIDCALL, "upscale texture is absent, not a texture, or from another device");
    result.sequence = q.frame.sequence; return;
  }
  result.hr = VCB_ServerUpscaleFrame(q.config, q.frame, device, scene, home, depth, mv, result);
  result.size = sizeof result; result.version = VCB_ABI_VERSION; result.sequence = q.frame.sequence;
  gLast[q.device_id] = result;
  if (gUpscaleLogged < 3 || (FAILED(result.hr) && gUpscaleLogged < 40)) {
    gUpscaleLogged++;
    hostLog("[upscale] frame %llu %ux%u -> %ux%u: hr 0x%08lx backend %d: %s", (unsigned long long) q.frame.sequence,
            q.frame.render_width, q.frame.render_height, q.frame.output_width, q.frame.output_height, (unsigned long) result.hr, result.backend, result.message);
  }
}
