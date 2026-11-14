// The HUD-less picture's rules, pure, so the mod and the device-thread harness (tools/perfproof/dt_fg_hud.h) run the same lines.
// fg_hud.h's head says why. No type of Windows' or Direct3D's.
#pragma once

// Frame generation's copy is taken at the frame's first overlay draw, and only before the interface's own first draw (after it,
// the encode's copy already holds the overlay). ifaceDrawsBefore: hdrPreHudDraws, which this rule never adds to.
static inline bool FgHudOverlayCopyRule(bool openedThisFrame, unsigned ifaceDrawsBefore) { return !openedThisFrame && ifaceDrawsBefore == 0; }

// What the encode's second output is made from.
// ENCODE: the encode's own copy (no overlay). TWO: the real mask reads the encode's copy, the HUD-less picture the overlay's.
// OVERLAY: the overlay's copy alone; the real mask is off. NONE: no copy, or an overlay whose copy could not be taken (the
// encode's copy would hold the overlay as scene).
enum { FG_HUDSRC_NONE = 0, FG_HUDSRC_ENCODE = 1, FG_HUDSRC_TWO = 2, FG_HUDSRC_OVERLAY = 3 };
static inline int FgHudSecondRule(bool encCopy, bool ovOpened, bool ovCopy)
{
    if (ovOpened && !ovCopy) return FG_HUDSRC_NONE;
    if (ovCopy) return encCopy ? FG_HUDSRC_TWO : FG_HUDSRC_OVERLAY;
    return encCopy ? FG_HUDSRC_ENCODE : FG_HUDSRC_NONE;
}

// "An interface drew": the 2D pass did, or a 3D overlay came ahead of it. "Its copy is this frame's": the overlay's where one
// opened, else the encode's.
static inline bool FgHudDrewRule(unsigned ifaceDraws, bool ovOpened) { return ifaceDraws > 0 || ovOpened; }
static inline bool FgHudPreOkRule(bool encCopy, bool ovOpened, bool ovCopy) { return ovOpened ? ovCopy : encCopy; }

// Once an encode. Engaged: make the target unless refused for good since the last Reset (multisampled) or a retry is pending
// (FG_HUDT2_RETRY). Not engaged: give it back after idleMax unwanted encodes: FG_HUDT2_IDLE, or FG_HUDT2_IDLE_DRIVEN (5 minutes at
// 60) while the armed driver can engage again, so the law's rest and back-off do not remake it. now, retryAt: the encode count.
enum { FG_HUDT2_NONE = 0, FG_HUDT2_MAKE = 1, FG_HUDT2_GIVE = 2, FG_HUDT2_IDLE = 600, FG_HUDT2_IDLE_DRIVEN = 18000, FG_HUDT2_RETRY = 300 };
static inline int FgHudT2Tick(bool engaged, bool stands, bool refused, unsigned now, unsigned retryAt, unsigned* idle, unsigned idleMax)
{
    if (engaged) { *idle = 0; return (!stands && !refused && now >= retryAt) ? FG_HUDT2_MAKE : FG_HUDT2_NONE; }
    if (stands && ++*idle >= idleMax) return FG_HUDT2_GIVE;
    return FG_HUDT2_NONE;
}
// Refused for good only where no retry can change it (permanent); a failed make is asked again after FG_HUDT2_RETRY encodes.
// FGHUD_T2_TRIED_AT_TRY and FGHUD_T2_FAIL_LATCHED are harness controls that bring back older faults.
static inline bool FgHudT2FailedAfter(bool made, bool permanent)
{
#if defined(FGHUD_T2_TRIED_AT_TRY)
    (void)made; (void)permanent; return true;
#elif defined(FGHUD_T2_FAIL_LATCHED)
    (void)permanent; return !made;
#else
    return !made && permanent;
#endif
}

// ==================================================================================
// SDR (fg_hud.h, "SDR"): THE HUD-LESS PICTURE of an 8-bit back buffer is a copy at the interface's first draw. With FusionFix's
// console gamma the copy goes into a texture of its own and the curve's draw is replayed from it into the second target. A frame
// with the phone has no HUD-less picture in SDR. The mod and the harness (dt_fg_hud.h) run these lines.
// The boundary rule as HDR has it: 2D, after a scene draw or the mod's resolve, not an opaque full-screen quad, not opened before.
static inline bool FgHudSdrBoundaryRule(bool ortho, bool opaqueQuad, bool sceneBefore, bool openedThisFrame)
{
    return ortho && !opaqueQuad && sceneBefore && !openedThisFrame;
}
// Only out of the real back buffer after the mod's resolve, once a frame. Into the curve's copy where the curve was latched: a
// draw cannot read the target it writes.
static inline bool FgHudSdrCopyRule(bool resolved, bool backBuffer, bool copiedThisFrame) { return resolved && backBuffer && !copiedThisFrame; }
static inline bool FgHudSdrIntoPreRule(bool curveLatch) { return curveLatch; }
// Is FusionFix's draw after its point copy the curve? Its module's first back-buffer draw after the copy, with nothing that could
// leave a pixel unwritten or write it differently, and sampler 0 holding the copy's texture (or the render scale's stand-in). The
// shader is not inspected: it is replayed as it stands. The copy counts only between the mod's resolve and EndScene.
struct FgHudSdrCurveFacts { bool sameModule, blendOff, allChannels, alphaTestOff, scissorOff, srgbOff, depthOff, fullViewport, oneTarget, t0Copy; };
static inline bool FgHudSdrCurveRule(const FgHudSdrCurveFacts& f)
{
    return f.sameModule && f.blendOff && f.allChannels && f.alphaTestOff && f.scissorOff && f.srgbOff && f.depthOff && f.fullViewport && f.oneTarget && f.t0Copy;
}
// Replayed where this frame's copy went into the curve's copy.
static inline bool FgHudSdrReplayRule(bool copiedThisFrame, bool intoPre, bool isCurve) { return copiedThisFrame && intoPre && isCurve; }
// At the frame's end, does the second target hold the HUD-less picture? Yes for a copy into the target with no curve after it, or
// a copy into the curve's copy with the curve replayed. Anything else is no picture. odd: another module's whole-frame copy came
// with no curve told after it.
static inline bool FgHudSdrPicRule(bool copied, bool intoPre, bool curve, bool replayed, bool odd)
{
    if (!copied || odd) return false;
    return intoPre ? (curve && replayed) : !curve;
}
// The latch: a curve was told this frame. A change of FusionFix's setting costs one frame with no HUD-less picture.
static inline bool FgHudSdrLatchRule(bool curve) { return curve; }

// ==================================================================================
// The source of the hand's copy out of the second target. DXVK's memory defragmenter can move a D3D9 texture to a new VkImage at
// any time (dxvk_image.cpp, relocateStorage) and free the old one, and GetVulkanImageInfo pins nothing. So the image and layout are
// asked at the hand, on the worker, inside the span after its flush (the worker alone feeds DXVK while threaded=1, so nothing moves
// it before the submit), and the copy names that answer. FGHUD_SRC_CACHED is a harness control that reads the make's image.
struct FgHudSrcNow { unsigned long long img; int layout; };
template <class Q, class S> static inline bool FgHudSrcTake(bool live, S surf, unsigned w, unsigned h, int fmt, unsigned long long madeImg, int madeLayout, FgHudSrcNow* out)
{
    out->img = 0; out->layout = 0;
    if (!live || !surf) return false;
#if defined(FGHUD_SRC_CACHED)
    (void)w; (void)h; (void)fmt;
    out->img = madeImg; out->layout = madeLayout;
    return madeImg != 0;
#else
    (void)madeImg; (void)madeLayout;
    unsigned long long i = 0; int l = 0; unsigned iw = 0, ih = 0; int f = 0;
    if (!Q::Query(surf, &i, &l, &iw, &ih, &f) || !i || iw != w || ih != h || f != fmt) return false;
    out->img = i; out->layout = l;
    return true;
#endif
}
