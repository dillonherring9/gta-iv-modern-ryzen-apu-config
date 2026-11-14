// Phase 3: what DLSS-IV's paired game side needs from VCRender.
//
//  1. DXVK's D3D9 Vulkan interop (ID3D9VkInteropDevice / ID3D9VkInteropTexture) and the small Vulkan subset its depth
//     snapshot uses. Every call is carried to the 64-bit host (OP_REMOTEVK) and run there on the real DXVK device, in
//     order with the game's D3D9 calls. Ported from b-bridge's client/vc_remotevk_client.cpp (Claude, 2026-11-01), which
//     DLSS-IV's paired build (60f29688-vcbridge1) was tested against; its present ring is not needed here.
//  2. The bulk upscale: VCB_UpscaleFrame / VCB_UpscaleStatus / VCB_UpscaleShutdown (ABI in vc_upscale_wire.h). One
//     ordered synchronous call (OP_UPSCALE); the host's native service (vc_upscale_server.cpp) copies scene, depth and
//     motion into the FSR helper's shared images and the result back into the game's "home" texture.
//
// Handles: non-dispatchable Vulkan handles are the host's own 64-bit values; dispatchable ones are tokens DLSS-IV only
// hands back. Command buffers are recorded here and replayed by the host at vkQueueSubmit. Semaphore waits and counter
// reads go over a named pipe to a host worker, so a wait never stalls the D3D9 stream.
#include "vcr_client.h"
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include "../upscale/dxvk_interop_min.h"
#include "../upscale/vc_upscale_wire.h"
#include "../shared/vcr_vk_wire.h"
#include <stdio.h>
#include <wchar.h>
#include <string>
#include <tlhelp32.h>


using namespace vcrvk;

namespace vcr {

static VDevice* gBound;               // the device whose Vulkan session the host holds
static uint32_t gBoundId;
static bool gHelloOk;
static uint32_t gQueueIndex, gQueueFamily;
static int gInstTok, gPhysTok, gDevTok, gQueueTok;
static VkInstance kInst = (VkInstance) &gInstTok;
static VkPhysicalDevice kPhys = (VkPhysicalDevice) &gPhysTok;
static VkDevice kDev = (VkDevice) &gDevTok;
static VkQueue kQueue = (VkQueue) &gQueueTok;

struct ClientCb { uint64_t token = 0; uint32_t beginFlags = 0; bool recording = false; Writer rec; };

static Writer Begin(Op op) { Writer w; w.u32(op); return w; }

// One ordered message. With a reply it is a synchronous call; without, a one-way record in the stream.
static bool Call(const Writer& w, std::vector<uint8_t>* reply) {
  Guard g;
  if (!ready()) return false;
  const uint32_t op = *(const uint32_t*) w.b.data();
  if (!hasReply(op)) {
    void* p = rec(OP_REMOTEVK, (uint32_t) w.b.size());
    memcpy(p, w.b.data(), w.b.size());
    return true;
  }
  uint8_t buf[4096]; uint32_t got = 0;
  HRESULT hr = call(OP_REMOTEVK, w.b.data(), (uint32_t) w.b.size(), buf, sizeof buf, &got);
  if (FAILED(hr)) { log("[interop] op %u: host refused (0x%08lx)", op, (unsigned long) hr); return false; }
  if (reply) reply->assign(buf, buf + (got < sizeof buf ? got : sizeof buf));
  return true;
}

// Side channel: waits and counter reads. The stream is published first, so a wait for a value an earlier one-way
// submit signals sees that submit reach the host.
static bool SideCall(const Writer& w, std::vector<uint8_t>* reply) {
  static thread_local HANDLE pipe = INVALID_HANDLE_VALUE;
  { Guard g; flush(); }
  for (int attempt = 0; attempt < 2; attempt++) {
    if (pipe == INVALID_HANDLE_VALUE) {
      wchar_t name[200]; swprintf(name, 200, L"\\\\.\\pipe\\%ls_vk", wcsrchr(channelBase(), L'\\') ? wcsrchr(channelBase(), L'\\') + 1 : channelBase());
      for (int i = 0; i < 50 && pipe == INVALID_HANDLE_VALUE; i++) {
        pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) { if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeW(name, 100); else Sleep(10); }
      }
      if (pipe == INVALID_HANDLE_VALUE) { log("[interop] side pipe: cannot connect"); return false; }
    }
    const uint32_t len = (uint32_t) w.b.size();
    DWORD io = 0;
    bool ok = WriteFile(pipe, &len, 4, &io, nullptr) && io == 4 && WriteFile(pipe, w.b.data(), len, &io, nullptr) && io == len;
    uint32_t rl = 0;
    if (ok) ok = ReadFile(pipe, &rl, 4, &io, nullptr) && io == 4 && rl <= 4096;
    if (ok) {
      reply->resize(rl);
      DWORD at = 0;
      while (ok && at < rl) { ok = ReadFile(pipe, reply->data() + at, rl - at, &io, nullptr) && io; at += io; }
      if (ok) return true;
    }
    CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE;
  }
  return false;
}

// Binds the host's Vulkan session to this device (the first one that asks for its Vulkan handles).
static bool Hello(VDevice* d) {
  Guard g;
  if (gBound == d && gBoundId == d->id) return gHelloOk;
  gBound = d; gBoundId = d->id; gHelloOk = false;
  Writer w = Begin(OpHello); w.u32(kWireVersion); w.u32(GetCurrentProcessId()); w.u32(d->id);
  std::vector<uint8_t> r;
  if (!Call(w, &r)) return false;
  Reader rd(r.data(), r.size());
  gHelloOk = rd.i32() == 1;
  gQueueIndex = rd.u32(); gQueueFamily = rd.u32();
  gHelloOk = gHelloOk && !rd.bad;
  log("[interop] Vulkan session bound to device %u: %s (queue %u, family %u)", d->id, gHelloOk ? "ready" : "FAILED", gQueueIndex, gQueueFamily);
  return gHelloOk;
}
void interopDeviceGone(VDevice* d) {   // called with the lock held, before the device sends OP_DESTROY
  if (gBound == d) { gBound = nullptr; gBoundId = 0; gHelloOk = false; }
}

static const void* FindNext(const void* chain, VkStructureType type) {
  for (const VkBaseInStructure* s = (const VkBaseInStructure*) chain; s; s = s->pNext) if (s->sType == type) return s;
  return nullptr;
}
static void* FindNextOut(void* chain, VkStructureType type) {
  for (VkBaseOutStructure* s = (VkBaseOutStructure*) chain; s; s = s->pNext) if (s->sType == type) return s;
  return nullptr;
}
static VkResult Result(const Writer& w, std::vector<uint8_t>& r, Reader& rd) {
  if (!Call(w, &r)) return VK_ERROR_DEVICE_LOST;
  rd = Reader(r.data(), r.size());
  return (VkResult) rd.i32();
}

// ---- Vulkan functions
static void VKAPI_CALL rGetPhysicalDeviceProperties2(VkPhysicalDevice, VkPhysicalDeviceProperties2* p) {
  if (!p) return;
  memset(&p->properties, 0, sizeof(p->properties));
  std::vector<uint8_t> r;
  if (!Call(Begin(OpPhysProps), &r)) return;
  Reader rd(r.data(), r.size());
  rd.raw(p->properties.deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
  p->properties.vendorID = rd.u32(); p->properties.deviceID = rd.u32();
  p->properties.driverVersion = rd.u32(); p->properties.apiVersion = rd.u32();
  p->properties.deviceType = (VkPhysicalDeviceType) rd.u32();
  VkPhysicalDeviceIDProperties id = {};
  rd.raw(id.deviceUUID, VK_UUID_SIZE); rd.raw(id.driverUUID, VK_UUID_SIZE); rd.raw(id.deviceLUID, VK_LUID_SIZE);
  id.deviceNodeMask = rd.u32(); id.deviceLUIDValid = rd.u32();
  if (auto* out = (VkPhysicalDeviceIDProperties*) FindNextOut(p->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES)) {
    memcpy(out->deviceUUID, id.deviceUUID, VK_UUID_SIZE); memcpy(out->driverUUID, id.driverUUID, VK_UUID_SIZE);
    memcpy(out->deviceLUID, id.deviceLUID, VK_LUID_SIZE); out->deviceNodeMask = id.deviceNodeMask; out->deviceLUIDValid = id.deviceLUIDValid;
  }
}
static VkResult VKAPI_CALL rGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice, const VkPhysicalDeviceImageFormatInfo2* in, VkImageFormatProperties2* out) {
  Writer w = Begin(OpImageFormatProps2);
  w.u32(in->format); w.u32(in->type); w.u32(in->tiling); w.u32(in->usage); w.u32(in->flags);
  auto* ext = (const VkPhysicalDeviceExternalImageFormatInfo*) FindNext(in->pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
  w.u32(ext ? ext->handleType : 0);
  std::vector<uint8_t> r; Reader rd(nullptr, 0);
  const VkResult res = Result(w, r, rd);
  VkImageFormatProperties& f = out->imageFormatProperties;
  f.maxExtent.width = rd.u32(); f.maxExtent.height = rd.u32(); f.maxExtent.depth = rd.u32();
  f.maxMipLevels = rd.u32(); f.maxArrayLayers = rd.u32(); f.sampleCounts = rd.u32(); f.maxResourceSize = rd.u64();
  const uint32_t feat = rd.u32(), exp = rd.u32(), compat = rd.u32();
  if (auto* e = (VkExternalImageFormatProperties*) FindNextOut(out->pNext, VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES)) {
    e->externalMemoryProperties.externalMemoryFeatures = feat;
    e->externalMemoryProperties.exportFromImportedHandleTypes = exp;
    e->externalMemoryProperties.compatibleHandleTypes = compat;
  }
  return res;
}
static VkResult VKAPI_CALL rCreateSemaphore(VkDevice, const VkSemaphoreCreateInfo* ci, const VkAllocationCallbacks*, VkSemaphore* out) {
  auto* t = (const VkSemaphoreTypeCreateInfo*) FindNext(ci->pNext, VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
  if (FindNext(ci->pNext, VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO)) { log("[interop] exported semaphores are not carried"); *out = VK_NULL_HANDLE; return VK_ERROR_FEATURE_NOT_PRESENT; }
  Writer w = Begin(OpCreateSemaphore); w.u32(t ? t->semaphoreType : 0); w.u64(t ? t->initialValue : 0);
  std::vector<uint8_t> r; Reader rd(nullptr, 0);
  const VkResult res = Result(w, r, rd);
  *out = (VkSemaphore) rd.u64();
  return res;
}
static void VKAPI_CALL rDestroySemaphore(VkDevice, VkSemaphore s, const VkAllocationCallbacks*) {
  if (!s) return; Writer w = Begin(OpDestroySemaphore); w.u64((uint64_t) s); Call(w, nullptr);
}
static VkResult VKAPI_CALL rWaitSemaphores(VkDevice, const VkSemaphoreWaitInfo* wi, uint64_t timeout) {
  Writer w = Begin(OpWaitSemaphores); w.u32(wi->flags); w.u32(wi->semaphoreCount);
  for (uint32_t i = 0; i < wi->semaphoreCount; i++) { w.u64((uint64_t) wi->pSemaphores[i]); w.u64(wi->pValues[i]); }
  w.u64(timeout);
  std::vector<uint8_t> r;
  if (!SideCall(w, &r)) return VK_ERROR_DEVICE_LOST;
  Reader rd(r.data(), r.size());
  return (VkResult) rd.i32();
}
static VkResult VKAPI_CALL rGetSemaphoreCounterValue(VkDevice, VkSemaphore s, uint64_t* v) {
  Writer w = Begin(OpSemaphoreCounter); w.u64((uint64_t) s);
  std::vector<uint8_t> r;
  if (!SideCall(w, &r)) return VK_ERROR_DEVICE_LOST;
  Reader rd(r.data(), r.size());
  const VkResult res = (VkResult) rd.i32();
  *v = rd.u64();
  return res;
}
static VkResult VKAPI_CALL rSignalSemaphore(VkDevice, const VkSemaphoreSignalInfo* si) {
  Writer w = Begin(OpSignalSemaphore); w.u64((uint64_t) si->semaphore); w.u64(si->value);
  std::vector<uint8_t> r; Reader rd(nullptr, 0);
  return Result(w, r, rd);
}
static VkResult VKAPI_CALL rCreateCommandPool(VkDevice, const VkCommandPoolCreateInfo* ci, const VkAllocationCallbacks*, VkCommandPool* out) {
  Writer w = Begin(OpCreateCommandPool); w.u32(ci->flags); w.u32(ci->queueFamilyIndex);
  std::vector<uint8_t> r; Reader rd(nullptr, 0);
  const VkResult res = Result(w, r, rd);
  *out = (VkCommandPool) rd.u64();
  return res;
}
static void VKAPI_CALL rDestroyCommandPool(VkDevice, VkCommandPool p, const VkAllocationCallbacks*) {
  if (!p) return; Writer w = Begin(OpDestroyCommandPool); w.u64((uint64_t) p); Call(w, nullptr);
}
static VkResult VKAPI_CALL rAllocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo* ai, VkCommandBuffer* out) {
  Writer w = Begin(OpAllocCommandBuffers); w.u64((uint64_t) ai->commandPool); w.u32(ai->level); w.u32(ai->commandBufferCount);
  std::vector<uint8_t> r; Reader rd(nullptr, 0);
  const VkResult res = Result(w, r, rd);
  for (uint32_t i = 0; i < ai->commandBufferCount; i++) {
    const uint64_t tok = rd.u64();
    if (res != VK_SUCCESS) { out[i] = nullptr; continue; }
    ClientCb* cb = new ClientCb; cb->token = tok;
    out[i] = (VkCommandBuffer) cb;
  }
  return res;
}
static VkResult VKAPI_CALL rBeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo* bi) {
  ClientCb* cb = (ClientCb*) c; cb->rec.b.clear(); cb->beginFlags = bi ? bi->flags : 0; cb->recording = true; return VK_SUCCESS;
}
static VkResult VKAPI_CALL rEndCommandBuffer(VkCommandBuffer c) { ((ClientCb*) c)->recording = false; return VK_SUCCESS; }
static VkResult VKAPI_CALL rResetCommandBuffer(VkCommandBuffer c, VkCommandBufferResetFlags) {
  ClientCb* cb = (ClientCb*) c; cb->rec.b.clear(); cb->recording = false; return VK_SUCCESS;
}
static void WriteRange(Writer& w, const VkImageSubresourceRange& r) {
  w.u32(r.aspectMask); w.u32(r.baseMipLevel); w.u32(r.levelCount); w.u32(r.baseArrayLayer); w.u32(r.layerCount);
}
static void WriteLayers(Writer& w, const VkImageSubresourceLayers& l) { w.u32(l.aspectMask); w.u32(l.mipLevel); w.u32(l.baseArrayLayer); w.u32(l.layerCount); }
static void WriteOffset(Writer& w, const VkOffset3D& o) { w.i32(o.x); w.i32(o.y); w.i32(o.z); }
static void VKAPI_CALL rCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags src, VkPipelineStageFlags dst, VkDependencyFlags dep,
                                           uint32_t memN, const VkMemoryBarrier* mem, uint32_t bufN, const VkBufferMemoryBarrier*,
                                           uint32_t imgN, const VkImageMemoryBarrier* img) {
  Writer& w = ((ClientCb*) c)->rec;
  w.u32(CmdPipelineBarrier); w.u32(src); w.u32(dst); w.u32(dep);
  w.u32(memN); for (uint32_t i = 0; i < memN; i++) { w.u32(mem[i].srcAccessMask); w.u32(mem[i].dstAccessMask); }
  if (bufN) log("[interop] buffer barriers are not carried");
  w.u32(imgN);
  for (uint32_t i = 0; i < imgN; i++) {
    const VkImageMemoryBarrier& b = img[i];
    w.u32(b.srcAccessMask); w.u32(b.dstAccessMask); w.u32(b.oldLayout); w.u32(b.newLayout);
    w.u32(b.srcQueueFamilyIndex); w.u32(b.dstQueueFamilyIndex); w.u64((uint64_t) b.image); WriteRange(w, b.subresourceRange);
  }
}
static void VKAPI_CALL rCmdCopyImage(VkCommandBuffer c, VkImage src, VkImageLayout srcL, VkImage dst, VkImageLayout dstL, uint32_t n, const VkImageCopy* reg) {
  Writer& w = ((ClientCb*) c)->rec;
  w.u32(CmdCopyImage); w.u64((uint64_t) src); w.u32(srcL); w.u64((uint64_t) dst); w.u32(dstL); w.u32(n);
  for (uint32_t i = 0; i < n; i++) {
    WriteLayers(w, reg[i].srcSubresource); WriteOffset(w, reg[i].srcOffset);
    WriteLayers(w, reg[i].dstSubresource); WriteOffset(w, reg[i].dstOffset);
    w.u32(reg[i].extent.width); w.u32(reg[i].extent.height); w.u32(reg[i].extent.depth);
  }
}
static void VKAPI_CALL rCmdBlitImage(VkCommandBuffer c, VkImage src, VkImageLayout srcL, VkImage dst, VkImageLayout dstL, uint32_t n, const VkImageBlit* reg, VkFilter filter) {
  Writer& w = ((ClientCb*) c)->rec;
  w.u32(CmdBlitImage); w.u64((uint64_t) src); w.u32(srcL); w.u64((uint64_t) dst); w.u32(dstL); w.u32(filter); w.u32(n);
  for (uint32_t i = 0; i < n; i++) {
    WriteLayers(w, reg[i].srcSubresource); WriteOffset(w, reg[i].srcOffsets[0]); WriteOffset(w, reg[i].srcOffsets[1]);
    WriteLayers(w, reg[i].dstSubresource); WriteOffset(w, reg[i].dstOffsets[0]); WriteOffset(w, reg[i].dstOffsets[1]);
  }
}
static VkResult VKAPI_CALL rQueueSubmit(VkQueue, uint32_t n, const VkSubmitInfo* si, VkFence fence) {
  if (fence) return VK_ERROR_FEATURE_NOT_PRESENT;   // DLSS-IV synchronises with timeline semaphores only
  Writer w = Begin(OpQueueSubmit); w.u32(n);
  for (uint32_t s = 0; s < n; s++) {
    const VkSubmitInfo& x = si[s];
    auto* tl = (const VkTimelineSemaphoreSubmitInfo*) FindNext(x.pNext, VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
    w.u32(x.waitSemaphoreCount);
    for (uint32_t i = 0; i < x.waitSemaphoreCount; i++) {
      w.u64((uint64_t) x.pWaitSemaphores[i]); w.u32(x.pWaitDstStageMask[i]);
      w.u64(tl && i < tl->waitSemaphoreValueCount && tl->pWaitSemaphoreValues ? tl->pWaitSemaphoreValues[i] : 0);
    }
    w.u32(x.commandBufferCount);
    for (uint32_t i = 0; i < x.commandBufferCount; i++) {
      ClientCb* cb = (ClientCb*) x.pCommandBuffers[i];
      w.u64(cb->token); w.u32(cb->beginFlags); w.u32((uint32_t) cb->rec.b.size()); w.raw(cb->rec.b.data(), cb->rec.b.size());
    }
    w.u32(x.signalSemaphoreCount);
    for (uint32_t i = 0; i < x.signalSemaphoreCount; i++) {
      w.u64((uint64_t) x.pSignalSemaphores[i]);
      w.u64(tl && i < tl->signalSemaphoreValueCount && tl->pSignalSemaphoreValues ? tl->pSignalSemaphoreValues[i] : 0);
    }
  }
  return Call(w, nullptr) ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
}

// FeedVkLoad requires these to resolve; the upscale in the host never needs them on the game side. A call is logged.
static void notCarried(const char* n) { static int said; if (said++ < 20) log("[interop] DLSS-IV called %s, which VCRender does not carry", n); }
static VkResult VKAPI_CALL rImportSemaphoreWin32HandleKHR(VkDevice, const VkImportSemaphoreWin32HandleInfoKHR*) { notCarried("vkImportSemaphoreWin32HandleKHR"); return VK_ERROR_FEATURE_NOT_PRESENT; }
static VkResult VKAPI_CALL rCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* out) { notCarried("vkCreateImage"); *out = VK_NULL_HANDLE; return VK_ERROR_FEATURE_NOT_PRESENT; }
static void VKAPI_CALL rDestroyImage(VkDevice, VkImage i, const VkAllocationCallbacks*) { if (i) notCarried("vkDestroyImage"); }
static void VKAPI_CALL rGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* out) { notCarried("vkGetImageMemoryRequirements"); memset(out, 0, sizeof *out); }
static VkResult VKAPI_CALL rAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* out) { notCarried("vkAllocateMemory"); *out = VK_NULL_HANDLE; return VK_ERROR_FEATURE_NOT_PRESENT; }
static void VKAPI_CALL rFreeMemory(VkDevice, VkDeviceMemory m, const VkAllocationCallbacks*) { if (m) notCarried("vkFreeMemory"); }
static VkResult VKAPI_CALL rBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) { notCarried("vkBindImageMemory"); return VK_ERROR_FEATURE_NOT_PRESENT; }

static PFN_vkVoidFunction VKAPI_CALL rGetDeviceProcAddr(VkDevice, const char* name) {
  if (!name) return nullptr;
  struct E { const char* n; PFN_vkVoidFunction f; };
  static const E table[] = {
    { "vkGetDeviceProcAddr", (PFN_vkVoidFunction) rGetDeviceProcAddr },
    { "vkCreateSemaphore", (PFN_vkVoidFunction) rCreateSemaphore }, { "vkDestroySemaphore", (PFN_vkVoidFunction) rDestroySemaphore },
    { "vkImportSemaphoreWin32HandleKHR", (PFN_vkVoidFunction) rImportSemaphoreWin32HandleKHR },
    { "vkCreateImage", (PFN_vkVoidFunction) rCreateImage }, { "vkDestroyImage", (PFN_vkVoidFunction) rDestroyImage },
    { "vkGetImageMemoryRequirements", (PFN_vkVoidFunction) rGetImageMemoryRequirements },
    { "vkAllocateMemory", (PFN_vkVoidFunction) rAllocateMemory }, { "vkFreeMemory", (PFN_vkVoidFunction) rFreeMemory },
    { "vkBindImageMemory", (PFN_vkVoidFunction) rBindImageMemory },
    { "vkWaitSemaphores", (PFN_vkVoidFunction) rWaitSemaphores }, { "vkWaitSemaphoresKHR", (PFN_vkVoidFunction) rWaitSemaphores },
    { "vkGetSemaphoreCounterValue", (PFN_vkVoidFunction) rGetSemaphoreCounterValue },
    { "vkGetSemaphoreCounterValueKHR", (PFN_vkVoidFunction) rGetSemaphoreCounterValue },
    { "vkSignalSemaphore", (PFN_vkVoidFunction) rSignalSemaphore }, { "vkSignalSemaphoreKHR", (PFN_vkVoidFunction) rSignalSemaphore },
    { "vkCreateCommandPool", (PFN_vkVoidFunction) rCreateCommandPool }, { "vkDestroyCommandPool", (PFN_vkVoidFunction) rDestroyCommandPool },
    { "vkAllocateCommandBuffers", (PFN_vkVoidFunction) rAllocateCommandBuffers },
    { "vkBeginCommandBuffer", (PFN_vkVoidFunction) rBeginCommandBuffer }, { "vkEndCommandBuffer", (PFN_vkVoidFunction) rEndCommandBuffer },
    { "vkResetCommandBuffer", (PFN_vkVoidFunction) rResetCommandBuffer },
    { "vkCmdPipelineBarrier", (PFN_vkVoidFunction) rCmdPipelineBarrier }, { "vkCmdCopyImage", (PFN_vkVoidFunction) rCmdCopyImage },
    { "vkCmdBlitImage", (PFN_vkVoidFunction) rCmdBlitImage }, { "vkQueueSubmit", (PFN_vkVoidFunction) rQueueSubmit },
  };
  for (const E& e : table) if (!strcmp(e.n, name)) return e.f;
  static int said; if (said++ < 40) log("[interop] not carried: %s", name);
  return nullptr;
}
static PFN_vkVoidFunction VKAPI_CALL rGetInstanceProcAddr(VkInstance, const char* name) {
  if (!name) return nullptr;
  if (!strcmp(name, "vkGetPhysicalDeviceProperties2") || !strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) return (PFN_vkVoidFunction) rGetPhysicalDeviceProperties2;
  if (!strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2") || !strcmp(name, "vkGetPhysicalDeviceImageFormatProperties2KHR"))
    return (PFN_vkVoidFunction) rGetPhysicalDeviceImageFormatProperties2;
  if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction) rGetInstanceProcAddr;
  return rGetDeviceProcAddr(kDev, name);
}

// ---- COM interop objects
static void writeSub(Writer& w, const VcrSub& s) { w.u32(s.id); w.u32(s.kind); w.u32(s.face); w.u32(s.level); }

class RemoteInteropTexture final : public ID3D9VkInteropTexture {
public:
  RemoteInteropTexture(VcrSub s, IUnknown* owner): sub(s), owner(owner) { owner->AddRef(); }
  VcrSub sub;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == __uuidof(ID3D9VkInteropTexture)) { AddRef(); *out = this; return S_OK; }
    return owner->QueryInterface(riid, out);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&refs); if (!r) { owner->Release(); delete this; } return r; }
  HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(VkImage* pHandle, VkImageLayout* pLayout, VkImageCreateInfo* pInfo) override {
    Writer w = Begin(OpImageInfo); writeSub(w, sub);
    std::vector<uint8_t> r;
    if (!Call(w, &r)) return E_FAIL;
    Reader rd(r.data(), r.size());
    const HRESULT hr = rd.i32();
    const uint64_t img = rd.u64(); const int32_t layout = rd.i32();
    VkImageCreateInfo ci = {}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.flags = rd.u32(); ci.imageType = (VkImageType) rd.u32(); ci.format = (VkFormat) rd.u32();
    ci.extent.width = rd.u32(); ci.extent.height = rd.u32(); ci.extent.depth = rd.u32();
    ci.mipLevels = rd.u32(); ci.arrayLayers = rd.u32(); ci.samples = (VkSampleCountFlagBits) rd.u32();
    ci.tiling = (VkImageTiling) rd.u32(); ci.usage = rd.u32(); ci.sharingMode = (VkSharingMode) rd.u32();
    ci.initialLayout = (VkImageLayout) rd.u32();
    if (pHandle) *pHandle = (VkImage) img;
    if (pLayout) *pLayout = (VkImageLayout) layout;
    if (pInfo) { const void* next = pInfo->pNext; *pInfo = ci; pInfo->pNext = next; }
    return rd.bad ? E_FAIL : hr;
  }
private:
  volatile LONG refs = 1;
  IUnknown* owner;
};

class RemoteInteropDevice final : public ID3D9VkInteropDevice {
public:
  explicit RemoteInteropDevice(VDevice* d): dev(d) {}
  VDevice* dev;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == __uuidof(ID3D9VkInteropDevice)) { AddRef(); *out = this; return S_OK; }
    return dev->QueryInterface(riid, out);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return dev->AddRef(); }     // lives as long as its device
  ULONG STDMETHODCALLTYPE Release() override { return dev->Release(); }
  void STDMETHODCALLTYPE GetVulkanHandles(VkInstance* i, VkPhysicalDevice* p, VkDevice* d) override {
    bool ok = Hello(dev);
    if (i) *i = ok ? kInst : nullptr; if (p) *p = ok ? kPhys : nullptr; if (d) *d = ok ? kDev : nullptr;
  }
  void STDMETHODCALLTYPE GetSubmissionQueue(VkQueue* q, uint32_t* qi, uint32_t* qf) override {
    bool ok = Hello(dev);
    if (q) *q = ok ? kQueue : nullptr; if (qi) *qi = gQueueIndex; if (qf) *qf = gQueueFamily;
  }
  void STDMETHODCALLTYPE TransitionTextureLayout(ID3D9VkInteropTexture* t, const VkImageSubresourceRange* r, VkImageLayout from, VkImageLayout to) override {
    if (!t || !r) return;
    Writer w = Begin(OpTransition); writeSub(w, static_cast<RemoteInteropTexture*>(t)->sub);
    WriteRange(w, *r); w.u32(from); w.u32(to);
    Call(w, nullptr);
  }
  void STDMETHODCALLTYPE FlushRenderingCommands() override { Call(Begin(OpFlush), nullptr); }
  void STDMETHODCALLTYPE LockSubmissionQueue() override { Call(Begin(OpLockQueue), nullptr); }
  void STDMETHODCALLTYPE ReleaseSubmissionQueue() override { Call(Begin(OpReleaseQueue), nullptr); }
  void STDMETHODCALLTYPE LockDevice() override { Call(Begin(OpLockDevice), nullptr); }
  void STDMETHODCALLTYPE UnlockDevice() override { Call(Begin(OpUnlockDevice), nullptr); }
  bool STDMETHODCALLTYPE WaitForResource(IDirect3DResource9*, DWORD) override { return false; }   // not used by DLSS-IV
  HRESULT STDMETHODCALLTYPE CreateImage(const D3D9VkExtImageDesc*, IDirect3DResource9**) override { return E_NOTIMPL; }
};

HRESULT interopQueryDevice(VDevice* d, REFIID riid, void** out) {
  if (riid != __uuidof(ID3D9VkInteropDevice)) return E_NOINTERFACE;
  if (!ready()) return E_NOINTERFACE;
  Guard g;
  if (!d->interop) d->interop = new RemoteInteropDevice(d);
  d->AddRef();
  *out = static_cast<ID3D9VkInteropDevice*>((RemoteInteropDevice*) d->interop);
  return S_OK;
}
void interopFreeDevice(VDevice* d) { delete (RemoteInteropDevice*) d->interop; d->interop = nullptr; }
HRESULT interopQueryTexture(VBase* b, IUnknown* owner, REFIID riid, void** out) {
  if (riid != __uuidof(ID3D9VkInteropTexture)) return E_NOINTERFACE;
  if (!ready() || !b) return E_NOINTERFACE;
  Guard g;
  VcrSub s = {};
  if (b->kind == K_SURFACE) s = subAddr(static_cast<VSurface*>(b));
  else if (b->kind == K_TEXTURE || b->kind == K_CUBE) s = VcrSub{b->id, b->kind, 0, 0};
  else return E_NOINTERFACE;
  if (!s.id) return E_NOINTERFACE;
  *out = static_cast<ID3D9VkInteropTexture*>(new RemoteInteropTexture(s, owner));
  return S_OK;
}

// ---- bulk upscale (vc_upscale_wire.h ABI 1)
// A pointer handed in is only used once its vtable is known to live in this module (b-bridge did the same).
static bool vtableOurs(void* p) {
  if (!p) return false;
  MEMORY_BASIC_INFORMATION mi;
  if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
      (uintptr_t) p + sizeof(void*) > (uintptr_t) mi.BaseAddress + mi.RegionSize) return false;
  void* vt = *(void**) p;
  HMODULE owner = nullptr, self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &vtableOurs, &self);
  return vt && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) vt, &owner) &&
         owner && owner == self;
}
static VDevice* ourDevice(void* p) {
  if (!vtableOurs(p)) return nullptr;
  VDevice* d = static_cast<VDevice*>((IDirect3DDevice9Ex*) p);
  return static_cast<VBase*>(d)->magic == 0x4F524356 && d->kind == K_DEVICE ? d : nullptr;
}
static uint32_t ourTexture(void* p, VDevice* d) {
  if (!vtableOurs(p)) return 0;
  VBaseTexture* t = asTexture((IDirect3DBaseTexture9*) p);
  return t && t->kind == K_TEXTURE && t->dev == d ? t->id : 0;
}
static HRESULT upscaleCall(uint32_t operation, VDevice* d, const VCBConfig* c, const VCBInput* in, VCBResult* out) {
  VCBWireRequest q = {};
  q.size = sizeof q; q.version = VCB_ABI_VERSION; q.operation = operation; q.device_id = d->id;
  if (in) {
    q.scene_id = ourTexture(in->scene, d); q.home_id = ourTexture(in->home, d);
    q.depth_id = ourTexture(in->depth, d); q.mv_id = ourTexture(in->mv, d);
    if (!q.scene_id || !q.home_id || (in->depth && !q.depth_id) || (in->mv && !q.mv_id)) {
      VCB_InitResult(*out, E_INVALIDARG, "scene/home/guide textures do not belong to this VCRender device"); return E_INVALIDARG;
    }
    q.frame = in->frame;
  }
  if (c) q.config = *c;
  VCBResult r; VCB_InitResult(r, E_FAIL, "VCRender host did not answer");
  uint32_t got = 0;
  HRESULT hr;
  { Guard g; hr = call(OP_UPSCALE, &q, sizeof q, &r, sizeof r, &got); }
  if (got != sizeof r || r.size != sizeof r || r.version != VCB_ABI_VERSION) {
    VCB_InitResult(*out, FAILED(hr) ? hr : E_FAIL, "malformed upscale reply"); return FAILED(hr) ? hr : E_FAIL;
  }
  *out = r;
  return hr;
}

}  // namespace vcr

using namespace vcr;

extern "C" __declspec(dllexport) int __stdcall VCRemoteVk_IsRemote() __asm__("_VCRemoteVk_IsRemote");
extern "C" __declspec(dllexport) PFN_vkVoidFunction __stdcall VCRemoteVk_GetInstanceProcAddr(VkInstance i, const char* n) __asm__("_VCRemoteVk_GetInstanceProcAddr");
extern "C" __declspec(dllexport) PFN_vkVoidFunction __stdcall VCRemoteVk_GetDeviceProcAddr(VkDevice d, const char* n) __asm__("_VCRemoteVk_GetDeviceProcAddr");
extern "C" __declspec(dllexport) int __stdcall VCRemoteVk_IsRemote() { return (int) kWireVersion; }
extern "C" __declspec(dllexport) PFN_vkVoidFunction __stdcall VCRemoteVk_GetInstanceProcAddr(VkInstance i, const char* n) { return rGetInstanceProcAddr(i, n); }
extern "C" __declspec(dllexport) PFN_vkVoidFunction __stdcall VCRemoteVk_GetDeviceProcAddr(VkDevice d, const char* n) { return rGetDeviceProcAddr(d, n); }

extern "C" __declspec(dllexport) HRESULT WINAPI VCB_UpscaleFrame(const VCBConfig* c, const VCBInput* in, VCBResult* out) {
  if (!out) return E_POINTER;
  if (!c || !in || in->size != sizeof *in || in->version != VCB_ABI_VERSION || !VCB_ValidConfig(*c) || !VCB_ValidFrame(in->frame)) {
    VCB_InitResult(*out, E_INVALIDARG, "invalid upscale config or frame"); return E_INVALIDARG;
  }
  VDevice* d = ourDevice(in->device);
  if (!d || !ready()) { VCB_InitResult(*out, E_INVALIDARG, "not a VCRender device"); return E_INVALIDARG; }
  return upscaleCall(VCB_OP_FRAME, d, c, in, out);
}
extern "C" __declspec(dllexport) HRESULT WINAPI VCB_UpscaleStatus(void* device, VCBResult* out) {
  if (!out) return E_POINTER;
  VDevice* d = ourDevice(device);
  if (!d || !ready()) { VCB_InitResult(*out, E_INVALIDARG, "not a VCRender device"); return E_INVALIDARG; }
  return upscaleCall(VCB_OP_STATUS, d, nullptr, nullptr, out);
}
extern "C" __declspec(dllexport) HRESULT WINAPI VCB_UpscaleShutdown(void* device, VCBResult* out) {
  if (!out) return E_POINTER;
  VDevice* d = ourDevice(device);
  if (!d || !ready()) { VCB_InitResult(*out, E_INVALIDARG, "not a VCRender device"); return E_INVALIDARG; }
  return upscaleCall(VCB_OP_SHUTDOWN, d, nullptr, nullptr, out);
}

// ---- every other Direct3DCreate9 in the game process answers with VCRender
// DLSS-IV finds the game's renderer by calling Direct3DCreate9 on each loaded module that exports it until it has hooked
// a device. Before the game makes its device that includes the game folder's x86 DXVK (vulkan.dll) and the ASI loader
// (dinput8.dll, which forwards to Proton's DXVK): either creates a Vulkan instance and maps the 32-bit driver and LLVM
// (195 MiB, isolated test 2026-11-13) into the game. Their export-table entries are pointed at VCRender instead, as
// step 257 did for dxgi.dll; FusionFix's d3d9.dll already forwards here through its slots.
extern "C" IDirect3D9* WINAPI VCR_Direct3DCreate9(UINT sdk);
extern "C" HRESULT WINAPI VCR_Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out);
static int guardExports(HMODULE m) {
  auto* dos = (IMAGE_DOS_HEADER*) m; auto* nt = (IMAGE_NT_HEADERS32*) ((BYTE*) m + dos->e_lfanew);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dir.VirtualAddress) return 0;
  auto* ex = (IMAGE_EXPORT_DIRECTORY*) ((BYTE*) m + dir.VirtualAddress);
  DWORD* funcs = (DWORD*) ((BYTE*) m + ex->AddressOfFunctions);
  DWORD* names = (DWORD*) ((BYTE*) m + ex->AddressOfNames);
  WORD* ords = (WORD*) ((BYTE*) m + ex->AddressOfNameOrdinals);
  int done = 0;
  for (DWORD i = 0; i < ex->NumberOfNames; i++) {
    const char* n = (const char*) m + names[i];
    void* repl = !strcmp(n, "Direct3DCreate9") ? (void*) &VCR_Direct3DCreate9 : !strcmp(n, "Direct3DCreate9Ex") ? (void*) &VCR_Direct3DCreate9Ex : nullptr;
    if (!repl) continue;
    DWORD* slot = &funcs[ords[i]];
    DWORD rva = (DWORD) ((uintptr_t) repl - (uintptr_t) m);   // wraps modulo 2^32 like the loader's own arithmetic
    DWORD old;
    if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { *slot = rva; VirtualProtect(slot, 4, old, &old); done++; }
  }
  return done;
}
static void guardDirect3DCreate9() {
  HMODULE self = nullptr, proxy = GetModuleHandleW(L"d3d9.dll");
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) &guardExports, &self);
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE) { log("[upscaler] module list unavailable (%lu); other Direct3DCreate9 exports left as they are", GetLastError()); return; }
  MODULEENTRY32W me = { sizeof me };
  std::string said;
  for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
    if (me.hModule == self || me.hModule == proxy) continue;
    if (!GetProcAddress(me.hModule, "Direct3DCreate9") && !GetProcAddress(me.hModule, "Direct3DCreate9Ex")) continue;
    int n = guardExports(me.hModule);
    bool verified = (void*) GetProcAddress(me.hModule, "Direct3DCreate9") == (void*) &VCR_Direct3DCreate9 || !GetProcAddress(me.hModule, "Direct3DCreate9");
    char nm[96]; WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, nm, sizeof nm, nullptr, nullptr);
    char line[160]; snprintf(line, sizeof line, "%s%s (%d%s)", said.empty() ? "" : ", ", nm, n, verified ? "" : ", NOT verified");
    said += line;
  }
  CloseHandle(snap);
  log("[upscaler] other Direct3DCreate9 exports now answered by VCRender: %s", said.empty() ? "none found" : said.c_str());
}

// ---- the paired DLSS-IV (plugins\VCRender\dlssiv-client.dll): scene detection, render scale, jitter and guide passes in
// the game; its bulk upscale comes back here through VCB_UpscaleFrame. Loaded once VCRender owns Direct3DCreate9, before
// the game creates its device. The root DLSS-IV.asi stays off (plugins\global.ini) because it needs Direct3D in-process.
void vcr::companionStart() {
  wchar_t ini[MAX_PATH], dll[MAX_PATH];
  swprintf(ini, MAX_PATH, L"%ls\\VCRender.ini", pluginDir());
  if (!GetPrivateProfileIntW(L"Upscaler", L"Enabled", 1, ini)) { log("[upscaler] off in VCRender.ini ([Upscaler] Enabled=0)"); return; }
  swprintf(dll, MAX_PATH, L"%ls\\dlssiv-client.dll", privateDir());
  if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) { log("[upscaler] no paired DLSS-IV (%ls): upscaler off", dll); return; }
  if (GetModuleHandleW(L"DLSS-IV.asi")) { log("[upscaler] the root DLSS-IV.asi is loaded (needs Direct3D in the game process); paired DLSS-IV not started"); return; }
  guardDirect3DCreate9();
  HMODULE m = LoadLibraryExW(dll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!m) { log("[upscaler] paired DLSS-IV failed to load (error %lu): upscaler off", GetLastError()); return; }
  auto init = (void (__cdecl*)()) GetProcAddress(m, "InitializeASI");
  if (init) init();
  log("[upscaler] paired DLSS-IV loaded: FSR runs in the 64-bit host; settings from DLSS-IV.cfg");
}
