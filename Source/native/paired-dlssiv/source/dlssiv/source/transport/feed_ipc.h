// STARTED as DLSS5-Feeder v0.12.0's header (https://github.com/jlrouzies-fr/DLSS5-Feeder)
// and is NOT verbatim any more: it said "verbatim" until 2026-09-09. Versions 8 to 16 (FEED_IPC_VERSION) are DLSS-IV's
// own; v1 to v7 are the Feeder's and are unchanged.
// Copyright (c) 2026 Jean-Laurent ROUZIES. MIT License: see LICENSE-DLSS5-Feeder.txt
// in this folder. Used by DLSS-IV as the resolve host's protocol / interop.

// dlss5-feed IPC: the 32-bit in-game client <-> 64-bit host protocol.
// The host creates the shared textures and two fences on D3D12 and duplicates them into the game (D3D12 cannot open
// Vulkan-exported memory). Per frame n the host waits fence_in >= n, evaluates, signals fence_out = n. The game either waits for
// n in the same frame (async_home=0) or homes the previous frame's result before signalling n (async_home=1, the default).
// A pipe break means "stop". v1 to v7 are the Feeder's (v7's panel for exclusive fullscreen is gone); 8 and up DLSS-IV's.
// The LUID comes from Vulkan: DXVK's GTA IV profile sets d3d9.hideNvidiaGpu.
// The host reads the hello incrementally by version, so an unknown version is refused cleanly. FeedHelloAck must stay 16 bytes.

#pragma once
#include <cstdint>

#define FEED_IPC_MAGIC   0x35534C44u  // 'DLS5'
#define FEED_IPC_VERSION 16u   // 16: the game's two semaphores, and the 'P', 'R'
                               // and 'M' messages (the Linux sync path).

// FeedBuild::client_flags (v5+) has no bits; its old values stay unassigned.
// FeedHelloAck::caps (v14+): what the host can do. PROBED is separate because "not probed" is a real state (no NGX on a
// non-NVIDIA adapter, no AMD DLL under DLSS).
#define FEED_CAP_DLSS_PROBED   1ull
#define FEED_CAP_DLSS          2ull
#define FEED_CAP_FSR_PROBED    4ull
#define FEED_CAP_FSR           8ull
// The host refused: the game and the host could not share one GPU.
#define FEED_CAP_REFUSED      16ull
// The client's build id is not this host's; the host refuses after the ack.
#define FEED_CAP_BUILD_MISMATCH 32ull
// DLSS unavailable: the NVIDIA driver is too old. Only with FEED_CAP_DLSS_PROBED set and FEED_CAP_DLSS clear.
#define FEED_CAP_DLSS_DRIVER  64ull
// DLSS unavailable: no nvngx_dlss.dll beside the host. Only with FEED_CAP_DLSS_PROBED set and FEED_CAP_DLSS clear.
#define FEED_CAP_DLSS_NOFILE 128ull
// Frame generation. PROBED: the helper ran with --fg=1 and asked AMD's library; FG: a provider answered. Nothing of frame
// generation is sent without FEED_CAP_FG, which is 1024, "frame generation, wire 2", so mixed revisions refuse each other;
// 512 is retired. WIRE3 (2048) and TWIN (4096) are offered beside it. NOLIB: DLSSIV\dlssiv-fg.dll is missing or not this
// build's (host/fg_own.h).
#define FEED_CAP_FG_PROBED  256ull
#define FEED_CAP_FG_WIRE1   512ull
#define FEED_CAP_FG        1024ull
#define FEED_CAP_FG_WIRE3  2048ull
#define FEED_CAP_FG_TWIN   4096ull
#define FEED_CAP_FG_NOLIB  8192ull
#define FEED_FG_WIRE          3

// Intel XeSS, as NVIDIA's pair. Reasons, with FEED_CAP_XESS_PROBED set and FEED_CAP_XESS clear: NOFILE, libxess.dll is not there
// (in DLSSIV\); UNSUPPORTED, the card or driver was refused; NORUNTIME, no Visual C++ runtime 14.40 or later.
#define FEED_CAP_XESS_PROBED       16384ull
#define FEED_CAP_XESS              32768ull
#define FEED_CAP_XESS_NOFILE       65536ull
#define FEED_CAP_XESS_UNSUPPORTED 131072ull
#define FEED_CAP_XESS_NORUNTIME   262144ull

// FeedBuildAck::flags (v6+)
#define FEED_ACK_SR_ACTIVE       1u   // the feature is Super Resolution work -> target (the build line says which mode)
// v10+: the host built FSR (clear: DLSS, or see FEED_ACK_BACKEND_XESS); backend=-1 is decided in the host.
#define FEED_ACK_BACKEND_FSR     4u
#define FEED_ACK_SR_UNAVAILABLE  2u   // SR was asked for and NGX could not report a DLSS mode for this output: the build
                                      // was refused, and the client counts it toward its three strikes
// AMD's FSR library raised (it is never called again): the build is refused for the session. Typically a community
// amd_fidelityfx_upscaler_dx12.dll the card cannot run (the build's warm-up is its first dispatch).
#define FEED_ACK_FSR_RAISED      8u
// Automatic chose FSR 3: the host's shader model is below FEED_FSR4_MIN_SM. The warnings file names Microsoft's Agility SDK.
#define FEED_ACK_FSR4_SKIPPED   16u
// A D3D12Core.dll in DLSSIV\D3D12\ was not usable. With FSR only.
#define FEED_ACK_AGILITY_REFUSED 32u
// v16: the build runs XeSS (with FEED_ACK_BACKEND_FSR clear; neither means DLSS).
#define FEED_ACK_BACKEND_XESS   64u
// The host's D3D12 shader model in bits 8..15 (0x65 for 6.5; 0 unread). With FSR only.
#define FEED_ACK_SM_SHIFT        8
#define FEED_ACK_SM_MASK    0xFF00u
#define FEED_FSR4_MIN_SM      0x66   // below this, automatic does not run FSR 4
#define FEED_PIPE_FMT    "\\\\.\\pipe\\dlssiv-feed.%lu"   // %lu = game PID
// The pipe's buffer each way. Client writes need no deadline while unread data stays under it (asserted beside IvtPipeWrite).
#define FEED_PIPE_BYTES  1024u

// The bytes a version-1 client sends as its hello: magic, version, pid.
#define FEED_HELLO_V1_SIZE (3u * sizeof(uint32_t))

// Four slots. A slot the host does not create is 0 in tex[], so anything walking the array must test the handle.
enum FeedSlot { FEED_COLOR = 0, FEED_OUTPUT, FEED_DEPTH, FEED_MV, FEED_SLOTS };

enum FeedClientKind { FEED_CLIENT_D3D11 = 0, FEED_CLIENT_GL = 1, FEED_CLIENT_VULKAN = 2 };

// True for every client whose API cannot hand D3D12 an importable texture, so the
// host creates the shared set and duplicates the handles in.
static inline bool FeedHostCreatesTextures(uint32_t client_kind)
{
    return client_kind == FEED_CLIENT_GL || client_kind == FEED_CLIENT_VULKAN;
}

#pragma pack(push, 1)

struct FeedHello        // game -> host, once
{
    uint32_t magic;
    uint32_t version;
    uint32_t pid;
    uint32_t client_kind;   // v2+: FeedClientKind. Absent (and 0) from a v1 client.
    uint64_t self_process;  // v4+: the game process's handle, valid in the host (no OpenProcess, which a DACL can deny); 0: the host falls back to OpenProcess
    uint8_t  luid[8];       // v14+: the game's adapter LUID from Vulkan's
                            // VkPhysicalDeviceIDProperties, never from D3D9. All zero when deviceLUIDValid is false, and the host then does not check.
                            // The host reads this struct incrementally by version, so fields must stay in the order the versions added them.
    char     build[48];     // v15+: the client's IVT_BUILD_ID (the git sha, "-dirty" if the
                            // tree was, or the unknown-source form), NUL-padded. A mismatch is refused with FEED_CAP_BUILD_MISMATCH in the ack. Last.
};

struct FeedHelloAck     // host -> game, once
{
    uint32_t magic;
    uint32_t version;
    uint64_t caps;                        // v14+: FEED_CAP_*
                                          // 8 bytes, so FeedHelloAck stays 16 and the client reads it in one go.
};

struct FeedBuild        // game -> host, on every resolution/format change
{
    uint32_t width, height;
    uint32_t color_fmt;          // DXGI_FORMAT of the shared Color/Output pair
    uint32_t output_fmt;
    int32_t  hdr;                // resolved flags, not cfg values
    int32_t  depth_inverted;
    int32_t  flags_override;     // -1 = none
    int32_t  transport;          // 1 = no NGX: host copies Color -> Output (cross-process transport test)
    float    mv_scale_x, mv_scale_y;
    uint32_t client_flags;       // v5+: always 0; kept for the layout
    uint32_t target_width, target_height;   // v6+: 0 = DLAA (target == width/height). Otherwise the Output
                                            // slot is this size and the feature is Super Resolution; Color, Depth and MV stay at width/height.
    uint64_t sem_in, sem_out;    // v16+ (the Linux sync path): handle values valid in the host
                                 // of the game's two OPAQUE_WIN32 timeline semaphores, opened as the host's fences. 0 on the native path (sync=0).
};

struct FeedBuildAck     // host -> game
{
    int32_t  ok;                 // 1 = feature ready
    uint32_t ngx_result;         // NVSDK_NGX_Result of the create (0x1 = success)
    uint64_t fence_in;           // handle values valid in the GAME process (host duplicated them in)
    uint64_t fence_out;
    uint64_t tex[FEED_SLOTS];    // host-creating clients only: handle values valid in the GAME process
    uint32_t output_fmt;         // the DXGI_FORMAT the host created the Output slot with:
                                 // it can differ from the request where the host's device has no typed UAV store to B8G8R8A8 (then R8G8B8A8).
    uint32_t flags;              // v6+: FEED_ACK_*
        // Which DLSS preset an SR feature picked is in the host's log.
};

struct FeedFrameMsg     // game -> host, per frame
{
    uint64_t n;                  // fence value for this frame
    uint32_t reset;              // 1 = reset temporal history
    float    jitter_x, jitter_y; // v6+: the sub-pixel shift applied to this frame's downsample grid, in
                                 // work pixels; 0 under DLAA. Handed to DLSS as the jitter offset.
    float    frame_time_ms;      // v8+: milliseconds since the previous frame was handed over, measured
                                 // on the game's render thread. Goes straight into
                                 // NVSDK_NGX_D3D12_DLSS_Eval_Params::InFrameTimeDeltaInMsec.
                                 // 0 on the first frame and whenever the clock is not readable.
        // v9+: camera intrinsics, which FSR needs on every dispatch. In band: with async_home the host runs a frame behind. No far
    // plane: the depth guide is reverse-Z with an infinite far plane, so FSR gets FLT_MAX, never the game's moving far plane.
    float    camera_near;        // v9+: 0 when no perspective frame has landed yet
    float    fov_y_rad;          // v9+: vertical field of view, radians, from the projection
};

#pragma pack(pop)

// The tag and the frame message in one pipe write.
#pragma pack(push, 1)
struct FeedTaggedFrameIvt { uint8_t tag; FeedFrameMsg fm; };
// The Linux sync path (v16; design in DlssIvTransport.h): Wine cannot import the host's fences, so the game makes two timeline
// semaphores the host opens (FeedBuild::sem_in/sem_out). 'P' probes them with a CPU signal, 'R' answers (and in bridge mode
// answers each frame), 'M' sets FAST (GPU waits, as on Windows) or BRIDGE (the game's relay signals from the CPU). 'M' may carry
// an answer event (FeedModeEventIvt) when the probe's answer had FEED_ANS_EVENT.
struct FeedProbeIvt  { uint8_t tag; uint64_t n; };
struct FeedAnswerIvt { uint8_t tag; uint64_t n; uint32_t status; };
struct FeedModeIvt   { uint8_t tag; uint32_t mode; };
struct FeedModeEventIvt { uint8_t tag; uint32_t mode; uint64_t answer_event; };   // only with FEED_MODE_ANSWER_EVENT set in mode
#pragma pack(pop)
#define FEED_ANS_IN_OK   1u
#define FEED_ANS_OPENED  2u
#define FEED_ANS_EVENT   4u          // in the probe's answer: this host sets an answer event when given one
#define FEED_MODE_ANSWER_EVENT 0x100u   // in FeedModeIvt::mode: a handle follows; the mode is `mode` without this bit
static_assert(sizeof(FeedProbeIvt)  == 9,  "FeedProbeIvt layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedAnswerIvt) == 13, "FeedAnswerIvt layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedModeIvt)   == 5,  "FeedModeIvt layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedModeEventIvt) == 13, "FeedModeEventIvt layout changed: it is FeedModeIvt and one handle, and only a host that set FEED_ANS_EVENT reads it");

// ===========================================================================
// Frame generation's wire, negotiated by FEED_CAP_FG.
//   n is the injection's number. It only rises and is sparse (an unsent frame gets no 'Q' or 'G'), so compare fences with the
//   last number this side wrote, never n-1. No fence is signalled at or below its last value.
//   Fences, made at the first 'g': fgIn (the game, in the submit of pair n's copies), fgPrep (the helper, behind 'Q'(n)'s
//   prepare), fgOut (the helper: FEED_FG_OUT_GEN(n) behind a dispatch, FEED_FG_OUT_NONE(n) otherwise).
//   'g' game -> helper, 29 bytes, inside the build: asks for a set, answered by the ack. A refusal never fails the build.
//   'Q' worker -> helper, 81 bytes, at the hand: configure and prepare pair n.
//   'G' worker -> helper, 17 bytes, after the pair's submit: one per 'Q'. Only a DISPATCH waits on fgIn.
//   Order: 'F'(n), 'Q'(n), 'G'(n), 'G'(n) before the next 'Q'. 131 bytes a frame (asserted in DLSS-IV.cpp against
//   FEED_PIPE_BYTES). Writers: the worker, or the render thread with the ring drained.
//   'S' (development builds) stamps each image with a distinct colour, to check which handle is which.
//   Wire 3: 0, 1 or 2 generated frames a pair (FEED_FG_G_GEN_*). Second input set (FEED_FG_BUILD_TWIN): inputs and guides twice,
//   used by turns (FEED_FG_Q_SET1 / FEED_FG_G_SET1 must agree); the game's side is in fg_machine_state.h.
// ===========================================================================
// The images of a set, in the machine's order (source/fg/fg_machine.h, FgImg). Macros, not an enum (see feed_fg.h). A set may
// lack HUDLESS and GEN2: count from what it holds (FgxSet::all).
#define FEED_FG_PRESENT          0
#define FEED_FG_GEN              1
#define FEED_FG_HUDLESS          2
#define FEED_FG_GEN2             3    // (wire 3) the second generated frame of a pair, at two thirds: only in a set asked with FEED_FG_BUILD_GEN2
#define FEED_FG_IMAGES           4    // the places of the ack's img[] (wire 3's ack, and the first 124 bytes of FeedFgAck4Ivt)
// The second input set: two places above the ack's img[], in the same arrays sized FEED_FG_PLACES; the ack carries them in
// FeedFgAck4Ivt::in2 (place k at in2[k - FEED_FG_IMAGES]). Only with FEED_FG_BUILD_TWIN; HUDLESS2 only where the set has HUDLESS.
#define FEED_FG_PRESENT2         4
#define FEED_FG_HUDLESS2         5
// and each set's guides, at the render size: depth R32_FLOAT, vectors R16G16_FLOAT
#define FEED_FG_GDEPTH0          6
#define FEED_FG_GMV0             7
#define FEED_FG_GDEPTH1          8
#define FEED_FG_GMV1             9
#define FEED_FG_GDEPTH(s)        (FEED_FG_GDEPTH0 + 2 * (s))
#define FEED_FG_GMV(s)           (FEED_FG_GMV0 + 2 * (s))
#define FEED_FG_IS_GUIDE(k)      ((k) >= FEED_FG_GDEPTH0 && (k) <= FEED_FG_GMV1)
#define FEED_FG_PLACES          10
#define FEED_FG_BUILD_RELEASE   1u    // FeedFgBuildIvt::flags: release the set and the context, create nothing
#define FEED_FG_BUILD_NO_HUDLESS 2u   // FeedFgBuildIvt::flags (wire 2): this set has no HUD-less image: two images, the ack's third all zero
#define FEED_FG_BUILD_WIRE3     4u    // FeedFgBuildIvt::flags (wire 3): this 'g' speaks wire 3: its ack is FeedFgAckIvt (124 bytes), and its set's 'G' carry the count
#define FEED_FG_BUILD_GEN2      8u    // FeedFgBuildIvt::flags (wire 3, with FEED_FG_BUILD_WIRE3 only): this set has the fourth image, GEN2; without it the ack's fourth is all zero
#define FEED_FG_BUILD_TWIN     16u    // FeedFgBuildIvt::flags (with FEED_FG_BUILD_WIRE3 only, beside FEED_CAP_FG_TWIN): the second input set and the sets' guides; the ack is FeedFgAck4Ivt (220 bytes)
#define FEED_FG_IMG_UAV         1u    // FeedFgImageIvt::flags: made with unordered access, so the game imports it with storage usage
// FeedFgAckIvt::gen_counts (wire 3): bit k set, this set makes count k (a 'G' asking for another is answered with no frame)
#define FEED_FG_GENS_FLOW       1u    // count 0: the flow-only pair
#define FEED_FG_GENS_ONE        2u    // count 1: one frame at the middle (every set of either wire)
#define FEED_FG_GENS_TWO        4u    // count 2: two frames at the thirds (a set with GEN2, on an arm that can)
// FeedFgAckIvt::status. Only CREATED carries images; every other value is a refusal (or the answer to a release) with one
// line in the helper's log, and none of them touches the upscaler.
#define FEED_FG_ST_CREATED       1    // the context exists and its warm-up dispatch ran (warm_rc says how)
#define FEED_FG_ST_RELEASED      2    // the answer to FEED_FG_BUILD_RELEASE
#define FEED_FG_ST_PLATFORM      3    // not Windows native (the second line of defence: the game never asks there)
#define FEED_FG_ST_NO_SET        4    // no shared set with depth and vectors to prepare from
#define FEED_FG_ST_NO_PROVIDER   5    // FEED_CAP_FG was never set
#define FEED_FG_ST_RAISED        6    // AMD's frame generation library raised earlier in this process: never called again
#define FEED_FG_ST_FORMAT        7    // a presented format outside the five the helper knows
#define FEED_FG_ST_SIZE          8    // a size out of bounds, or a render size that is not the set's
#define FEED_FG_ST_VRAM          9    // the video memory rule refused it (the helper's line has the numbers)
#define FEED_FG_ST_BUSY         10    // the helper's own queue did not drain within FEED_FG_DRAIN_MS: nothing was freed
#define FEED_FG_ST_CREATE       11    // AMD's create failed (ffx_rc)
#define FEED_FG_ST_WARMUP       12    // the warm-up dispatch failed or outran FEED_FG_WARMUP_MS (warm_rc, warm_us)
#define FEED_FG_ST_RUNTIME      13    // the helper runs on an Agility runtime frame generation has not been proved on
#define FEED_FG_ST_OBJECTS      14    // a fence, an image or the command ring could not be made (the helper's line says which)
// FeedFgQIvt::flags. Bits 0..3 are why this pair resets the interpolator's history (any means reset), as known when 'Q' was
// written; the helper counts resets by reason, once a pair.
#define FEED_FG_Q_RESET_CUT      1u   // a camera cut
#define FEED_FG_Q_RESET_BUILD    2u   // the first pair after a 'g'
#define FEED_FG_Q_RESET_DEVICE   4u   // the first pair after a device Reset
#define FEED_FG_Q_RESET_GAP      8u   // the first pair after a gap: standby, a menu, back-off, a run of FEED_FG_OUT_NONE
#define FEED_FG_Q_RESET_MASK    15u
#define FEED_FG_Q_CAMERA        16u   // cam[] holds the real camera; clear means the floats are the identity
#define FEED_FG_Q_PIPELINED     32u   // (wire 2) this pair's frame ran pipelined (its injection waited for the previous frame's
                                      // answer, not its own): counted by the helper
#define FEED_FG_Q_SET1          64u   // (the second input set) this pair's inputs are PRESENT2 and HUDLESS2
// FeedFgGIvt::flags, bits 0..1: the kind. Bits 2..3: the gap's generated count (FEED_FG_G_GEN_*, read only in a wire 3 set).
// Bits 4..7: the reset's reasons as known at the 'G', the FEED_FG_Q_RESET_* values shifted by FEED_FG_G_RESET_SHIFT. A CLOSED
// 'G', or one owed and written later, carries none.
#define FEED_FG_G_RESET_SHIFT    4
#define FEED_FG_G_RESET_MASK  0xF0u
#define FEED_FG_G_DISPATCH       0u   // both copies made and the submit that signals fgIn was taken: wait on fgIn, dispatch
#define FEED_FG_G_CLOSED         1u   // the pair was closed before its 'G' was due: no input, no fgIn
#define FEED_FG_G_NOSOURCE       2u   // no copy (the predecessor unanswered, no buffer, a refused submit): no fgIn
#define FEED_FG_G_WITHHELD       3u   // inputs valid and fgIn signalled, but no dispatch is wanted (a cut, a hitch)
#define FEED_FG_G_KIND_MASK      3u
#define FEED_FG_G_GEN_SHIFT      2    // (wire 3) bits 2..3: how many generated frames the pair's gap carries
#define FEED_FG_G_GEN_MASK    0x0Cu
#define FEED_FG_G_GEN_FLOW       0u   //   0 flow-only: the optical flow and the store, no picture, answered FEED_FG_OUT_NONE(n)
#define FEED_FG_G_GEN_ONE        1u   //   1 one frame at the middle into GEN: the only count of wire 2
#define FEED_FG_G_GEN_TWO        2u   //   2 two frames, at 1/3 into GEN and at 2/3 into GEN2, one list and one answer
#define FEED_FG_G_SET1       0x200u   // (the second input set) bit 9: a DISPATCH reads PRESENT2; it must be its 'Q''s FEED_FG_Q_SET1 (bit 8 is
                                      // the machine's own mark, FG_G_COUNTED, never on the wire)
// fgOut's two values for pair n
#define FEED_FG_OUT_GEN(n)   (2ull * (uint64_t)(n))
#define FEED_FG_OUT_NONE(n)  (2ull * (uint64_t)(n) + 1ull)
// The 'g' exchange has the build's one-minute deadline; the helper bounds its drain and warm-up well inside it, so a slow create
// refuses frame generation, never the transport.
#define FEED_FG_BUILD_DEADLINE_MS 60000u
#define FEED_FG_DRAIN_MS           2000u
#define FEED_FG_WARMUP_MS         10000u

// The sizes each version promises: on a failure, bump FEED_IPC_VERSION rather than editing the number.
static_assert(sizeof(FeedBuild)     == 68, "FeedBuild layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedBuildAck)  == 64, "FeedBuildAck layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedFrameMsg)  == 32, "FeedFrameMsg layout changed: bump FEED_IPC_VERSION");
static_assert(sizeof(FeedHello)     == 80, "FeedHello layout changed: the host reads it incrementally by version");
static_assert(sizeof(FeedHelloAck)  == 16, "FeedHelloAck MUST stay 16: the client reads it in one go");
