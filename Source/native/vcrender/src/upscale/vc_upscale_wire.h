// BBridge standalone high-level upscale RPC. MIT; project integration, 2026-11-11.
// Only VCBInput is local-process data. Everything carried over the bridge is
// packed, fixed-width and pointer-free. Bump VCB_ABI_VERSION for any layout change.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>

#define VCB_ABI_VERSION 1u
#define VCB_PATH_BYTES 1024u
#define VCB_MESSAGE_BYTES 192u

enum VCBFrameFlags {
  VCB_FRAME_RESET = 1u,
  VCB_FRAME_VALID = 2u,
  VCB_FRAME_PREVIOUS_VALID = 4u,
  VCB_FRAME_GUIDES_VALID = 8u,
  VCB_FRAME_HDR = 16u
};
enum VCBResultFlags {
  VCB_RESULT_READY = 1u,
  VCB_RESULT_COPIED_HOME = 2u,
  VCB_RESULT_SHUTDOWN = 4u
};
enum VCBWireOperation {
  VCB_OP_FRAME = 1u,
  VCB_OP_STATUS = 2u,
  VCB_OP_SHUTDOWN = 3u
};

#pragma pack(push, 1)
struct VCBConfig {
  uint32_t size, version;
  int32_t backend;             // -1 automatic, 0 DLSS, 1 FSR
  int32_t fsr_version;         // 0 automatic, 3 FSR3, 4 FSR4
  int32_t preset;
  float sharpness;
  uint32_t flags;              // reserved; must be zero in ABI 1
  uint32_t timeout_ms;         // 1..30000 service budget; RPC adds 5s communication grace
  int32_t hdr, depth_inverted, flags_override, transport;
  char helper_path[VCB_PATH_BYTES]; // absolute Windows UTF-8 exe path, NUL terminated
  char vendor_dir[VCB_PATH_BYTES];  // absolute Windows UTF-8 folder, or empty
};
struct VCBFrame {
  uint64_t sequence;
  uint32_t render_width, render_height, output_width, output_height, flags;
  float jitter_x, jitter_y, frame_time_ms, camera_near, fov_y_rad;
  float mv_scale_x, mv_scale_y;
};
struct VCBResult {
  uint32_t size, version;
  int32_t hr, backend;
  uint32_t flags, detail;
  uint64_t sequence;
  char message[VCB_MESSAGE_BYTES];
};
struct VCBWireRequest {
  uint32_t size, version, operation;
  uint32_t device_id, scene_id, home_id, depth_id, mv_id;
  VCBConfig config;
  VCBFrame frame;
};
#pragma pack(pop)

// Never serialize this structure. The bridge converts these local interfaces
// into its monotonically allocated object IDs while holding the device lock.
struct VCBInput {
  uint32_t size, version;
  void* device;                // IDirect3DDevice9* from this bridge client
  void* scene;                 // IDirect3DTexture9*, rendered input
  void* home;                  // IDirect3DTexture9*, completed output destination
  void* depth;                 // IDirect3DTexture9*, prepared depth guide, optional
  void* mv;                    // IDirect3DTexture9*, prepared motion guide, optional
  VCBFrame frame;
};

static_assert(sizeof(VCBConfig) == 2096, "VCBConfig ABI changed");
static_assert(sizeof(VCBFrame) == 56, "VCBFrame ABI changed");
static_assert(sizeof(VCBResult) == 224, "VCBResult ABI changed");
static_assert(sizeof(VCBWireRequest) == 2184, "VCBWireRequest ABI changed");
static_assert(offsetof(VCBWireRequest, frame) == 2128, "VCB frame offset changed");

inline void VCB_InitResult(VCBResult& r, int32_t hr, const char* message = nullptr) {
  memset(&r, 0, sizeof(r));
  r.size = sizeof(r); r.version = VCB_ABI_VERSION; r.hr = hr; r.backend = -1;
  if (message) {
    const size_t n = strlen(message);
    memcpy(r.message, message, n < sizeof(r.message) - 1 ? n : sizeof(r.message) - 1);
  }
}
inline bool VCB_ValidConfig(const VCBConfig& c) {
  return c.size == sizeof(c) && c.version == VCB_ABI_VERSION &&
    c.backend >= -1 && c.backend <= 1 && (c.fsr_version == 0 || c.fsr_version == 3 || c.fsr_version == 4) &&
    isfinite(c.sharpness) && c.sharpness >= 0.0f && c.sharpness <= 1.0f &&
    !c.flags && c.timeout_ms >= 1 && c.timeout_ms <= 30000 &&
    c.hdr >= 0 && c.hdr <= 1 && c.depth_inverted >= 0 && c.depth_inverted <= 1 &&
    c.transport >= 0 && c.transport <= 1 &&
    memchr(c.helper_path, 0, sizeof(c.helper_path)) && memchr(c.vendor_dir, 0, sizeof(c.vendor_dir));
}
inline bool VCB_ValidFrame(const VCBFrame& f) {
  return f.sequence && !(f.flags & ~31u) &&
    f.render_width && f.render_width <= 16384 && f.render_height && f.render_height <= 16384 &&
    f.output_width && f.output_width <= 16384 && f.output_height && f.output_height <= 16384 &&
    isfinite(f.jitter_x) && isfinite(f.jitter_y) &&
    isfinite(f.frame_time_ms) && f.frame_time_ms >= 0.0f &&
    isfinite(f.camera_near) && f.camera_near >= 0.0f &&
    isfinite(f.fov_y_rad) && f.fov_y_rad >= 0.0f && f.fov_y_rad <= 3.141593f &&
    isfinite(f.mv_scale_x) && isfinite(f.mv_scale_y);
}

#if defined(_WIN32)
#include <windows.h>
// VCRender (MinGW x86): export the plain names DLSS-IV's GetProcAddress asks for, not stdcall-decorated ones.
#if defined(__MINGW32__) && defined(__i386__)
#define VCB_EXPORT_NAME(n) __asm__("_" #n)
#else
#define VCB_EXPORT_NAME(n)
#endif
extern "C" HRESULT WINAPI VCB_UpscaleFrame(const VCBConfig*, const VCBInput*, VCBResult*) VCB_EXPORT_NAME(VCB_UpscaleFrame);
extern "C" HRESULT WINAPI VCB_UpscaleStatus(void* device, VCBResult*) VCB_EXPORT_NAME(VCB_UpscaleStatus);
extern "C" HRESULT WINAPI VCB_UpscaleShutdown(void* device, VCBResult*) VCB_EXPORT_NAME(VCB_UpscaleShutdown);
#endif
