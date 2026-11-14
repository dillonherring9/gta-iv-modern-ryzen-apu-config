// OUR OWN FILE (not AMD's): THE INTERFACE OF dlssiv-fg.dll, the mod's own frame generation library (2026-10-07, the own
// build's stage 2; docs/DECISIONS.md, "PHASE 3: OUR OWN BUILD OF AMD'S FRAME GENERATION", sections 3 and 4).
//
// WHAT THE LIBRARY IS. AMD's frame interpolation, optical flow and Direct3D 12 backend, compiled from AMD's public source
// as vendored in external\ffx\ffx_api_2.3.0, unmodified, behind THIS interface instead of AMD's ffx-api. The file that
// implements it (dlssiv_fg.cpp, beside this one) does what AMD's provider layer does for the path dlssiv-helper.exe
// takes, and nothing else. The library exports the seven functions below and none of AMD's five entry points.
//
// PLAIN C: fixed-size structs, each beginning with its own size, no type of AMD's and none of Direct3D's (a pointer to a
// Direct3D object is a void*, and its comment says which interface it is). The caller needs this header and nothing else.
//
// THREE CALLS A PAIR where AMD's provider has one dispatch: PREPARE (the guides: depth and motion vectors), OPTICAL FLOW
// and INTERPOLATE. Each records into the command list it is handed and returns when it has recorded; none of them
// executes, waits or signals anything.
//
// THE RULES THE LIBRARY KEEPS (DECISIONS, section 5, rule 5):
//   - It takes only the caller's own textures. EVERY TEXTURE IS HANDED IN D3D12_RESOURCE_STATE_COMMON and is in that state
//     again when the call's commands have run.
//   - It records only into the list of the call. It creates no command queue, no thread and no fence.
//   - One context is used from one thread at a time.
//
// THE FRAME ID is the caller's count of pairs: exactly one more for each prepare. A dispatch (optical flow, interpolate)
// whose id is not the id of the last prepare, or is not one more than the last interpolated id, RESETS, whatever its
// flags say: AMD's provider's rule, kept.
//
// WHAT COMES BACK: 0, or a number that is never 0. 0x8000000x is AMD's own error code as its code returned it
// (ffx_error.h); 0x0F0000xx is this library's own refusal (the DLSSIV_FG_E_ values).
//
// INTERFACE 2 (2026-10-17, frame generation phase 5, package F3): THE POSITION. A generated frame stood in the middle of
// its two real frames, as AMD's code writes it; a library built with the cut fi-position (ffx-own\cuts\fi-position.cut)
// takes WHERE it stands, DlssivFgFrameDesc::position (0 the previous frame, 1 the current), and says interface 2:
//   - position 0 is AMD's middle (0.5), so a description zeroed and filled as before asks for exactly the frame it got;
//   - any other value is held within DLSSIV_FG_POSITION_LEAST and DLSSIV_FG_POSITION_MOST (the library does not say
//     that it did: a caller that wants to count it compares its own value with the two); one that is not a number is
//     refused with DLSSIV_FG_E_ARGUMENT before anything is recorded;
//   - a library of interface 2 takes a frame description of either size: the whole struct, or interface 1's, which
//     ends before `position` (DLSSIV_FG_FRAME_SIZE_1) and asks for the middle. A library of interface 1 (the reference
//     with no cut, and every library built before this day) takes only the shorter. SO A CALLER THAT SPEAKS TO BOTH
//     sends DLSSIV_FG_FRAME_SIZE_1 whenever its position is 0, and the whole size only to a library of interface 2.
//
// INTERFACE 3 (2026-10-18, frame generation phase 5, the true fill's package G1): THE STORE APART. An interpolate keeps the
// pair's picture (the HUD-less one where it is handed in, else the presented one) for the next pair's interpolation, and
// counts the pair (AMD's count of dispatches since a reset, which the interpolation reads). A library built with the cut
// fi-store-apart (ffx-own\cuts\fi-store-apart.cut) says interface 3 and takes two more flags of an interpolate call:
//   - DLSSIV_FG_NO_STORE: the generated frame is made and the pair's picture is NOT kept. THE PAIR STAYS OPEN: the next
//     interpolate of the same id (another position) is a later frame of the same pair; it does not count the pair again,
//     and the one that is not told NO_STORE keeps the picture and closes it. Two generated frames of one pair are
//     interpolate(1/3, NO_STORE), interpolate(2/3), and each is, pixel for pixel, a single interpolate at its position;
//   - DLSSIV_FG_STORE_ONLY: NO generated frame (output may be null): the pair is counted and its picture kept, as a
//     whole interpolate keeps them, and no pass that makes a picture runs. A pair with no generated frame is prepare,
//     optical flow and interpolate(STORE_ONLY), and the next pair's picture is the one it has after a whole interpolate.
//     It also closes a pair left open (interpolate(1/3, NO_STORE), then interpolate(STORE_ONLY): one generated frame);
//   - both together are refused (DLSSIV_FG_E_ARGUMENT), as is a null output without STORE_ONLY;
//   - A PAIR LEFT OPEN AND NOT CLOSED (an interpolate of another id comes): that interpolate RESETS, because the picture
//     kept for it is not its previous pair's. Optical flow is not reset by it (its own history is whole);
//   - a second interpolate of one id does not reset (AMD's rule: the id is not one more than the last one, but not more
//     either), with these flags or without;
//   - with neither flag a call is exactly what it was (interfaces 1 and 2), and the frame description is the same size.
// A library of interface 2 or 1 does not read these bits: a caller asks for them only of a library that says 3.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLSSIV_FG_INTERFACE 3u            // the number of this header: a caller refuses a library that says a number above it or 0 (interface 2 is this header without NO_STORE and STORE_ONLY, interface 1 also without the position)
#define DLSSIV_FG_STAGE     2u            // the own build's stage this library is of

#define DLSSIV_FG_OK              0u
#define DLSSIV_FG_E_ARGUMENT      0x0F000001u   // a null pointer, a size that is not this header's, a value out of range
#define DLSSIV_FG_E_MEMORY        0x0F000002u
#define DLSSIV_FG_E_PIPELINE      0x0F000003u   // the device refused one of AMD's pipelines (AMD's own code would not have said)
#define DLSSIV_FG_E_STAMPS        0x0F000004u   // the stamps were asked for and could not be made, or are not on
#define DLSSIV_FG_E_CONTEXT       0x0F000005u   // not a context of this library

// dlssivFgInfo's caps
#define DLSSIV_FG_CAP_STAMPS      1u            // this build can stamp its passes (DLSSIV_FG_CREATE_STAMPS)

// DlssivFgCreateDesc::flags
#define DLSSIV_FG_CREATE_DEPTH_INVERTED     1u
#define DLSSIV_FG_CREATE_DEPTH_INFINITE     2u
#define DLSSIV_FG_CREATE_HDR                4u    // the presented image holds high dynamic range values
#define DLSSIV_FG_CREATE_CHECK              8u    // AMD's own checking of what it is handed
#define DLSSIV_FG_CREATE_STAMPS             16u   // DEVELOPMENT: a GPU timestamp after every job AMD's backend executes
#define DLSSIV_FG_CREATE_STAMPS_NO_BARRIER  32u   // with STAMPS: no barrier before a stamp (the barrier's control)

// the per-pair calls' flags
#define DLSSIV_FG_RESET                     1u    // the picture changed discontinuously
#define DLSSIV_FG_NO_STORE                  2u    // INTERFACE 3, interpolate: the pair's picture is not kept, the pair stays open for a later frame of it
#define DLSSIV_FG_STORE_ONLY                4u    // INTERFACE 3, interpolate: no generated frame, the pair counted and its picture kept
#define DLSSIV_FG_DEBUG_TEAR_LINES          0x100u    // interpolate: AMD's debug drawing into the generated image
#define DLSSIV_FG_DEBUG_RESET_INDICATORS    0x200u
#define DLSSIV_FG_DEBUG_VIEW                0x400u

// the presented image's transfer function (the numbers are AMD's)
#define DLSSIV_FG_TRANSFER_SRGB   0u
#define DLSSIV_FG_TRANSFER_PQ     1u
#define DLSSIV_FG_TRANSFER_SCRGB  2u

typedef struct DlssivFgContext DlssivFgContext;       // opaque

typedef void (*DlssivFgMessage)(uint32_t type, const wchar_t* text);   // AMD's own messages (0 an error, 1 a warning)

typedef struct DlssivFgCreateDesc
{
    uint32_t size;                         // sizeof(DlssivFgCreateDesc)
    uint32_t flags;                        // DLSSIV_FG_CREATE_
    void*    device;                       // ID3D12Device*
    uint32_t displayWidth, displayHeight;  // the presented and the generated image
    uint32_t maxRenderWidth, maxRenderHeight;   // the guides at their largest
    uint32_t backBufferFormat;             // DXGI_FORMAT of the presented image
    uint32_t hudlessFormat;                // DXGI_FORMAT of a HUD-less image where it has another format; 0: the same, or none
    uint32_t stampSlots;                   // with STAMPS: how many lists may be in flight at once, 1 to 64
    uint32_t reserved;
    DlssivFgMessage message;               // or null: where AMD's messages go (with CHECK)
} DlssivFgCreateDesc;

typedef struct DlssivFgInfo
{
    uint32_t size;                         // in: sizeof(DlssivFgInfo)
    uint32_t iface;                        // DLSSIV_FG_INTERFACE as the library was built
    uint32_t stage;                        // DLSSIV_FG_STAGE
    uint32_t caps;                         // DLSSIV_FG_CAP_
    uint64_t amdId;                        // AMD's id of the frame interpolation version this source is (0xF600000000C01006)
    char     amdVersion[16];               // "3.1.6"
    char     build[80];                    // what the library says it is
    uint64_t contextBytes, aliasableBytes; // with a description given: what a context of it takes, by AMD's own arithmetic
} DlssivFgInfo;

typedef struct DlssivFgCreated
{
    uint32_t size;                         // in: sizeof(DlssivFgCreated)
    uint32_t pipelinesAsked, pipelinesRefused;   // AMD's code asks the device for 18 and reads no answer: read here
    uint32_t shaderModel;                  // what AMD's backend reads of the device: 0x51, 0x60 .. 0x67
    uint32_t waveLaneMin, waveLaneMax;
    uint32_t fp16;                         // AMD's 16-bit flag (min precision and native 16-bit operations)
    uint32_t wave64;                       // AMD's code took its cs_6_6 containers (shader model 6.6, wave lanes 32 to 64)
    uint32_t stamps;                       // 0 off, 1 on, 2 on with no barrier
    uint64_t contextBytes, aliasableBytes; // what the context's three backend contexts hold, by AMD's own count
} DlssivFgCreated;

typedef struct DlssivFgPrepareDesc
{
    uint32_t size;
    uint32_t flags;                        // DLSSIV_FG_RESET
    void*    commandList;                  // ID3D12GraphicsCommandList*
    void*    depth;                        // ID3D12Resource*
    void*    motionVectors;                // ID3D12Resource*
    uint64_t frameId;
    uint32_t renderWidth, renderHeight;
    float    jitterX, jitterY;
    float    motionVectorScaleX, motionVectorScaleY;
    float    frameTimeMs;
    float    cameraNear, cameraFar, cameraFovY, viewSpaceToMeters;
    float    cameraPosition[3], cameraUp[3], cameraRight[3], cameraForward[3];
    uint32_t stampSlot;                    // with STAMPS: the slot of the list this is recorded into
} DlssivFgPrepareDesc;

typedef struct DlssivFgFrameDesc           // optical flow and interpolate take the same description
{
    uint32_t size;
    uint32_t flags;                        // DLSSIV_FG_RESET; interpolate: DLSSIV_FG_DEBUG_, and (interface 3) DLSSIV_FG_NO_STORE or DLSSIV_FG_STORE_ONLY
    void*    commandList;                  // ID3D12GraphicsCommandList*
    void*    present;                      // ID3D12Resource*: the presented image
    void*    hudless;                      // ID3D12Resource*, or null: the same image without the HUD
    void*    output;                       // ID3D12Resource*: the generated image (interpolate; optical flow does not read it; null with STORE_ONLY)
    uint64_t frameId;
    int32_t  rectLeft, rectTop, rectWidth, rectHeight;   // interpolate: the part to generate; width and height 0: all of it
    uint32_t transfer;                     // DLSSIV_FG_TRANSFER_
    float    minLuminance, maxLuminance;
    uint32_t stampSlot;
    float    position;                     // INTERFACE 2, interpolate: where the generated frame stands, 0 the previous frame, 1 the current; 0 is the middle (optical flow does not read it)
} DlssivFgFrameDesc;

#define DLSSIV_FG_FRAME_SIZE_1      offsetof(DlssivFgFrameDesc, position)   // interface 1's frame description: this one up to `position`
#define DLSSIV_FG_POSITION_LEAST    0.2f   // a position other than 0 is held within these two (AMD's inpainting is tuned for the middle)
#define DLSSIV_FG_POSITION_MOST     0.8f

// THE STAMPS (DEVELOPMENT; only on a context created with DLSSIV_FG_CREATE_STAMPS). One GPU timestamp at the head of each
// of the three calls, one behind every job AMD's backend executes for it, and one behind the call's last barriers. A
// stamp's span is the time since the stamp before it in the same list (the caller's arithmetic). A
// SLOT is one command list of the caller's: BEGIN before the first call recorded into it, RESOLVE behind the last, and
// READ when that list has run on the GPU (the caller knows when; the library waits for nothing).
#define DLSSIV_FG_STAMPS_BEGIN    1u
#define DLSSIV_FG_STAMPS_RESOLVE  2u
#define DLSSIV_FG_STAMPS_READ     3u

#define DLSSIV_FG_STAMPS_A_SLOT   256u     // the most one slot holds; more are counted as lost

#define DLSSIV_FG_CALL_PREPARE      1u
#define DLSSIV_FG_CALL_OPTICAL_FLOW 2u
#define DLSSIV_FG_CALL_INTERPOLATE  3u

#define DLSSIV_FG_MARK_HEAD  0u            // the call's head, before AMD records anything (behind a barrier of its own, as every stamp that follows work)
#define DLSSIV_FG_MARK_JOB   1u            // behind one job
#define DLSSIV_FG_MARK_END   2u            // behind AMD's last barriers, which put every texture back in its state

typedef struct DlssivFgMark
{
    uint8_t call;                          // DLSSIV_FG_CALL_
    uint8_t kind;                          // DLSSIV_FG_MARK_
    uint8_t job;                           // AMD's kind of job: 0 a clear, 1 a copy, 2 a compute pass, 3 a barrier, 4 a discard
    uint8_t waited;                        // a barrier stands before this stamp, so it waits for the work before it
    char    name[44];                      // the job's name as AMD's code names it, or what kind it is
} DlssivFgMark;

typedef struct DlssivFgStampsDesc
{
    uint32_t size;
    uint32_t op;                           // DLSSIV_FG_STAMPS_
    void*    commandList;                  // RESOLVE: ID3D12GraphicsCommandList*
    uint32_t slot;
    uint32_t capacity;                     // READ: how many the two arrays hold
    uint32_t count;                        // READ, out: the stamps of the slot's last list
    uint32_t lost;                         // READ, out: stamps that did not fit
    uint64_t*     ticks;                   // READ: [capacity], the GPU's ticks (the queue's timestamp frequency is the caller's to ask)
    DlssivFgMark* marks;                   // READ: [capacity]
} DlssivFgStampsDesc;

// The library's functions, by these names. A caller that loads the library by its full path resolves all seven and
// refuses a library that lacks one.
typedef uint32_t (*DlssivFgInfoFn)(DlssivFgInfo* out, const DlssivFgCreateDesc* forMemory);
typedef uint32_t (*DlssivFgCreateFn)(const DlssivFgCreateDesc* desc, DlssivFgContext** out, DlssivFgCreated* created);
typedef uint32_t (*DlssivFgDestroyFn)(DlssivFgContext* context);
typedef uint32_t (*DlssivFgPrepareFn)(DlssivFgContext* context, const DlssivFgPrepareDesc* desc);
typedef uint32_t (*DlssivFgOpticalFlowFn)(DlssivFgContext* context, const DlssivFgFrameDesc* desc);
typedef uint32_t (*DlssivFgInterpolateFn)(DlssivFgContext* context, const DlssivFgFrameDesc* desc);
typedef uint32_t (*DlssivFgStampsFn)(DlssivFgContext* context, DlssivFgStampsDesc* desc);

#define DLSSIV_FG_NAME_INFO         "dlssivFgInfo"
#define DLSSIV_FG_NAME_CREATE       "dlssivFgCreate"
#define DLSSIV_FG_NAME_DESTROY      "dlssivFgDestroy"
#define DLSSIV_FG_NAME_PREPARE      "dlssivFgPrepare"
#define DLSSIV_FG_NAME_OPTICAL_FLOW "dlssivFgOpticalFlow"
#define DLSSIV_FG_NAME_INTERPOLATE  "dlssivFgInterpolate"
#define DLSSIV_FG_NAME_STAMPS       "dlssivFgStamps"

#ifdef __cplusplus
}
#endif
