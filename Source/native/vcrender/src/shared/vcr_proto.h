// VCRender wire protocol, shared by the 32-bit game-side client and the 64-bit host.
// Every struct here contains only 32-bit fields so x86 and x64 layouts are identical.
#pragma once
#include <stdint.h>

#define VCR_MAGIC     0x52435656u   // "VVCR"
#define VCR_PROTOCOL  4u   // 4: persistent Ex surfaces + quarantine-safe lifecycle; requires paired host
#define VCR_MAX_SC    16u           // swapchains with a frame entry
#define VCR_FRAME_SLOTS 3u

// ---- shared memory layout: [control][ring][arena][response] -------------------------------
#define VCR_CONTROL_BYTES (64u * 1024u)

struct VcrFrameEntry {              // one per presenting swapchain (written by host)
  volatile int32_t scId;            // 0 = free
  volatile int32_t gen;             // bumps when the frame mapping is recreated
  volatile int32_t width, height;
  volatile int32_t latestSlot;      // slot holding the newest complete frame, -1 none
  volatile int32_t latestFrame;     // frame number in latestSlot
  volatile int32_t readingSlot;     // set by the client presenter while it reads, -1 none
  volatile int32_t completedFrame;  // newest frame number fully copied (throttle)
};

struct alignas(64) VcrControl {
  uint32_t magic, protocol, controlBytes, ringBytes, arenaBytes, respBytes;
  uint32_t clientPid;
  volatile int32_t hostPid;
  volatile int32_t hostReady;       // 1 ready, <0 failed
  volatile int32_t hostError;
  char hostMessage[512];
  alignas(64) volatile int32_t ringHead;      // client: published write position (free running)
  alignas(64) volatile int32_t ringTail;      // host: consumed position
  alignas(64) volatile int32_t hostSleeping;  // host: 1 while waiting on the wake event
  alignas(64) volatile int32_t processedSeq;  // host: records processed (PAD excluded)
  alignas(64) volatile int32_t clientWaiting; // client: 1 while waiting for host progress
  alignas(64) volatile int32_t respSeq;       // host: number of sync records answered
  alignas(64) VcrFrameEntry frames[VCR_MAX_SC];
  alignas(64) volatile int32_t hostFatal;     // host hit an unrecoverable error
  volatile int32_t stats[16];
};

struct VcrRec { uint16_t op; uint16_t flags; uint32_t size; };  // size includes header, multiple of 8
#define VCR_F_SYNC 1u

struct VcrResp { int32_t hr; uint32_t bytes; };  // followed by payload

// ---- opcodes ---------------------------------------------------------------------------------
enum VcrOp : uint16_t {
  OP_PAD = 0, OP_NOP, OP_HELLO, OP_SHUTDOWN,
  // IDirect3D9(Ex) queries (sync)
  OP_ADAPTER_COUNT, OP_ADAPTER_IDENTIFIER, OP_ADAPTER_MODE_COUNT, OP_ENUM_ADAPTER_MODES,
  OP_ADAPTER_DISPLAY_MODE, OP_CHECK_DEVICE_TYPE, OP_CHECK_DEVICE_FORMAT, OP_CHECK_MULTISAMPLE,
  OP_CHECK_DEPTH_MATCH, OP_CHECK_FORMAT_CONVERSION, OP_ADAPTER_CAPS, OP_ADAPTER_MODE_COUNT_EX,
  OP_ENUM_ADAPTER_MODES_EX, OP_ADAPTER_DISPLAY_MODE_EX, OP_ADAPTER_LUID,
  // device lifetime
  OP_CREATE_DEVICE, OP_RESET, OP_DESTROY, OP_AVAILABLE_TEXMEM, OP_EVICT_MANAGED, OP_DISPLAY_MODE,
  OP_CREATE_SWAPCHAIN, OP_PRESENT,
  // resources
  OP_CREATE_TEXTURE, OP_CREATE_VOLUME_TEXTURE, OP_CREATE_CUBE_TEXTURE, OP_CREATE_VB, OP_CREATE_IB,
  OP_CREATE_RT, OP_CREATE_DS, OP_CREATE_OFFSCREEN, OP_REGISTER_SUB,
  OP_UPDATE_SURFACE, OP_UPDATE_TEXTURE, OP_GET_RT_DATA, OP_STRETCH_RECT, OP_COLOR_FILL,
  OP_SET_RT, OP_SET_DS, OP_BEGIN_SCENE, OP_END_SCENE, OP_CLEAR,
  OP_SET_TRANSFORM, OP_MULTIPLY_TRANSFORM, OP_SET_VIEWPORT, OP_SET_MATERIAL, OP_SET_LIGHT,
  OP_LIGHT_ENABLE, OP_SET_CLIP_PLANE, OP_SET_RS,
  OP_CREATE_SB, OP_BEGIN_SB, OP_END_SB, OP_SB_CAPTURE, OP_SB_APPLY,
  OP_SET_TEXTURE, OP_SET_TSS, OP_SET_SS, OP_VALIDATE, OP_SET_PALETTE, OP_SET_CUR_PALETTE,
  OP_SET_SCISSOR, OP_SET_SWVP, OP_SET_NPATCH,
  OP_DRAW, OP_DRAW_INDEXED, OP_DRAW_UP, OP_DRAW_INDEXED_UP, OP_PROCESS_VERTICES,
  OP_CREATE_DECL, OP_SET_DECL, OP_SET_FVF,
  OP_CREATE_VS, OP_SET_VS, OP_VS_CONST_F, OP_VS_CONST_I, OP_VS_CONST_B,
  OP_SET_STREAM, OP_SET_STREAM_FREQ, OP_SET_INDICES,
  OP_CREATE_PS, OP_SET_PS, OP_PS_CONST_F, OP_PS_CONST_I, OP_PS_CONST_B,
  OP_QUERY_SUPPORT, OP_CREATE_QUERY, OP_QUERY_ISSUE, OP_QUERY_GETDATA,
  OP_TEX_SET_LOD, OP_TEX_AUTOGEN_FILTER, OP_TEX_GENMIPS, OP_TEX_DIRTY, OP_RES_PRELOAD, OP_RES_PRIORITY,
  OP_UPLOAD_IMG, OP_UPLOAD_BUF, OP_UPLOAD_CHUNK, OP_READ_IMG, OP_READ_BUF,
  OP_SET_CONVOLUTION, OP_COMPOSE_RECTS, OP_SET_GPU_PRIORITY, OP_SET_MAX_LATENCY,
  OP_DXGI_INFO,
  OP_REMOTEVK,          // vcr_vk_wire.h payload; synchronous when its op has a reply
  OP_UPSCALE,           // VCBWireRequest (vc_upscale_wire.h) -> VCBResult (synchronous)
  OP_COUNT
};

// object kinds (host type checks)
enum VcrKind : uint32_t {
  K_NONE = 0, K_DEVICE, K_SWAPCHAIN, K_SURFACE, K_TEXTURE, K_CUBE, K_VOLTEX, K_VOLUME,
  K_VB, K_IB, K_VS, K_PS, K_DECL, K_QUERY, K_SB
};

// subresource address: an image inside an object
struct VcrSub { uint32_t id, kind, face, level; };   // kind K_SURFACE/K_TEXTURE/K_CUBE/K_VOLTEX/K_VOLUME

struct VcrPP {   // D3DPRESENT_PARAMETERS without the HWND
  uint32_t w, h, fmt, count, ms, msq, swap, windowed, autoDS, dsFmt, flags, refresh, interval;
};

// ---- fixed payloads --------------------------------------------------------------------------
struct PCreateDevice {
  uint32_t devId, adapter, devType, behavior, isEx;
  VcrPP pp;
  uint32_t scId, bbIds[4], dsId;
  uint32_t fsW, fsH, fsRefresh, fsFmt, fsScan;
};
struct RCreateDevice { VcrPP pp; uint8_t caps[304]; };   // response payload after hr
struct PReset { uint32_t devId, isEx; VcrPP pp; uint32_t scId, bbIds[4], dsId; uint32_t fsW, fsH, fsRefresh, fsFmt, fsScan; };
struct PCreateSwapchain { uint32_t devId; VcrPP pp; uint32_t scId, bbIds[4]; };
struct PPresent { uint32_t devId, scId, frameNo, flags; };
struct PCreateTex { uint32_t devId, id, w, h, d, levels, usage, fmt, pool; };
struct PCreateBuf { uint32_t devId, id, length, usage, fvfOrFmt, pool; };
struct PCreateSurf { uint32_t devId, id, w, h, fmt, ms, msq, flag, usage, pool; };  // flag: lockable/discard
struct PRegisterSub { uint32_t parent, parentKind, face, level, id, kind; };
struct PUpdateSurface { uint32_t devId; VcrSub src; uint32_t hasSrcRect; int32_t sl, st, sr, sb; VcrSub dst; uint32_t hasPt; int32_t px, py; };
struct PUpdateTexture { uint32_t devId, src, dst; };
struct PGetRTData { uint32_t devId; VcrSub rt; VcrSub dst; };
struct PStretchRect { uint32_t devId; VcrSub src; uint32_t hasSrc; int32_t sl, st, sr, sb; VcrSub dst; uint32_t hasDst; int32_t dl, dt, dr, db; uint32_t filter; };
struct PColorFill { uint32_t devId; VcrSub dst; uint32_t hasRect; int32_t l, t, r, b; uint32_t color; };
struct PSetRT { uint32_t devId, index; VcrSub surf; };
struct PSetDS { uint32_t devId; VcrSub surf; };
struct PClear { uint32_t devId, count, flags, color; float z; uint32_t stencil; };   // + count RECTs
struct PSetTransform { uint32_t devId, state; float m[16]; };
struct PViewport { uint32_t devId, x, y, w, h; float mn, mx; };
struct PMaterial { uint32_t devId; float v[17]; };
struct PLight { uint32_t devId, index; uint32_t type; float v[25]; };
struct PLightEnable { uint32_t devId, index, enable; };
struct PClipPlane { uint32_t devId, index; float p[4]; };
struct PRS { uint32_t devId, state, value; };
struct PSB { uint32_t devId, id, type; };
struct PSetTexture { uint32_t devId, stage, id, kind; };
struct PTSS { uint32_t devId, stage, type, value; };
struct PSS { uint32_t devId, sampler, type, value; };
struct PPalette { uint32_t devId, number; uint8_t entries[1024]; };
struct PScissor { uint32_t devId; int32_t l, t, r, b; };
struct PDraw { uint32_t devId, prim, a, b, c, d, e; };   // DrawPrimitive: a=start b=count; Indexed: a=base b=minIdx c=numV d=startIdx e=count
struct PDrawUP { uint32_t devId, prim, count, stride, vbytes; };                        // + vertex data
struct PDrawIndexedUP { uint32_t devId, prim, minIdx, numV, count, idxFmt, stride, ibytes, vbytes; }; // + indices + vertices
struct PProcessVertices { uint32_t devId, src, dst, count, destVB, decl, flags; };
struct PCreateDecl { uint32_t devId, id, count; };   // + D3DVERTEXELEMENT9[count]
struct PCreateShader { uint32_t devId, id, bytes; }; // + bytecode
struct PSetObj { uint32_t devId, id; };              // decl / vs / ps / indices
struct PConst { uint32_t devId, start, count; };     // + data (F: 16*count bytes, I: 16*count, B: 4*count)
struct PStream { uint32_t devId, stream, id, offset, stride; };
struct PStreamFreq { uint32_t devId, stream, setting; };
struct PCreateQuery { uint32_t devId, id, type; };
struct PQueryIssue { uint32_t id, flags; };
struct PQueryGetData { uint32_t id, size, flags; };
struct PTexOp { uint32_t id, kind, value; };
struct PTexDirty { uint32_t id, kind, face, hasBox; int32_t l, t, r, b, front, back; };
struct PUploadImg {
  VcrSub sub; int32_t l, t, r, b, front, back;  // rect (pixels) and depth range; whole image when r==0 && b==0
  uint32_t whole;                               // lock with NULL rect/box
  uint32_t rowBytes, rows, slices, srcPitch, srcSlicePitch, lockFlags;
  uint32_t src, arenaOff;                       // src: 0 inline (follows), 1 arena, 2 chunk buffer
};
struct PUploadBuf { uint32_t id, kind, offset, size, lockFlags, src, arenaOff; };
struct PUploadChunk { uint32_t total, offset, bytes; };   // + data
struct PReadImg { VcrSub sub; int32_t l, t, r, b, front, back; uint32_t whole, rowBytes, rowStart, rowCount, slice; };
struct PReadBuf { uint32_t id, kind, offset, size; };
struct PGeneric { uint32_t devId, a, b, c, d; };

// module query payloads
struct PAdapterQ { uint32_t adapter, devType, a, b, c, d, e; };
struct RAdapterId {   // D3DADAPTER_IDENTIFIER9 field by field (struct padding differs x86/x64)
  char driver[512], description[512], deviceName[32];
  uint32_t versionLow, versionHigh, vendorId, deviceId, subSysId, revision;
  uint8_t guid[16]; uint32_t whql;
};
struct RMode { uint32_t w, h, refresh, fmt; };
struct RModeEx { uint32_t size, w, h, refresh, fmt, scan; };

// DXGI adapter facts from the host's 64-bit DXGI (answers the game's DXGI queries without Vulkan in the game)
struct RDxgiInfo {
  uint16_t description[128];
  uint32_t vendorId, deviceId, subSysId, revision, flags;
  uint32_t luidLow, luidHigh;
  uint32_t dedicatedVideoLo, dedicatedVideoHi, dedicatedSystemLo, dedicatedSystemHi, sharedSystemLo, sharedSystemHi;
  uint32_t mem[2][4][2];   // [local, non-local][budget, usage, availableForReservation, reservation][lo, hi]
};
