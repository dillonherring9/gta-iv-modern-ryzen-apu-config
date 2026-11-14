// Wire format of OP_REMOTEVK: DXVK's D3D9 Vulkan interop and the small Vulkan subset DLSS-IV's game side uses, carried
// from the 32-bit game to the 64-bit host and run there on the real DXVK device.
// Adapted from b-bridge's vc_remotevk_wire.h (Vice City NextGen, Claude 2026-11-01), without its present ring:
// VCRender presents its own frames. Every field is fixed width; both sides are little-endian.
#pragma once
#include <stdint.h>
#include <string.h>
#include <vector>

namespace vcrvk {

constexpr uint32_t kWireVersion = 2;   // DLSS-IV's paired build reads VCRemoteVk_IsRemote() and expects 2

enum Op : uint32_t {
  OpHello = 1,            // u32 version, u32 clientPid -> i32 ok, u32 queueIndex, u32 queueFamily
  OpPhysProps,            // -> props subset + ID props
  OpImageFormatProps2,    // -> i32 VkResult + limits + external props
  OpImageInfo,            // VcrSub -> i32 hr, u64 image, i32 layout, create-info scalars
  OpTransition,           // VcrSub, range, old, new                           (no reply)
  OpFlush,                //                                                    (no reply)
  OpLockQueue,            //                                                    (no reply)
  OpReleaseQueue,         //                                                    (no reply)
  OpLockDevice,           //                                                    (no reply)
  OpUnlockDevice,         //                                                    (no reply)
  OpCreateSemaphore,      // u32 type, u64 initial -> i32, u64 sem
  OpDestroySemaphore,     // u64 sem                                            (no reply)
  OpWaitSemaphores,       // u32 flags, u32 n, n*(u64 sem, u64 value), u64 timeout -> i32   (side channel)
  OpSemaphoreCounter,     // u64 sem -> i32, u64 value                                     (side channel)
  OpSignalSemaphore,      // u64 sem, u64 value -> i32
  OpCreateCommandPool,    // u32 flags, u32 family -> i32, u64 pool
  OpDestroyCommandPool,   // u64 pool                                           (no reply)
  OpAllocCommandBuffers,  // u64 pool, u32 level, u32 n -> i32, n*u64 token
  OpQueueSubmit,          // recorded command streams                           (no reply; the host logs a failure)
  OpCount
};

// Recorded command-buffer stream entries (inside OpQueueSubmit).
enum Cmd : uint32_t { CmdPipelineBarrier = 1, CmdCopyImage, CmdBlitImage, CmdEnd };

inline bool hasReply(uint32_t op) {
  switch (op) {
  case OpTransition: case OpFlush: case OpLockQueue: case OpReleaseQueue: case OpLockDevice: case OpUnlockDevice:
  case OpDestroySemaphore: case OpDestroyCommandPool: case OpQueueSubmit:
    return false;
  default:
    return true;
  }
}

struct Writer {
  std::vector<uint8_t> b;
  void raw(const void* p, size_t n) { const uint8_t* c = (const uint8_t*) p; b.insert(b.end(), c, c + n); }
  void u32(uint32_t v) { raw(&v, 4); }
  void i32(int32_t v) { raw(&v, 4); }
  void u64(uint64_t v) { raw(&v, 8); }
};

struct Reader {
  const uint8_t* p; size_t n; size_t at = 0; bool bad = false;
  Reader(const void* data, size_t size): p((const uint8_t*) data), n(size) {}
  bool raw(void* out, size_t k) {
    if (bad || at + k > n) { bad = true; memset(out, 0, k); return false; }
    memcpy(out, p + at, k); at += k; return true;
  }
  uint32_t u32() { uint32_t v; raw(&v, 4); return v; }
  int32_t  i32() { int32_t v;  raw(&v, 4); return v; }
  uint64_t u64() { uint64_t v; raw(&v, 8); return v; }
};

}  // namespace vcrvk
