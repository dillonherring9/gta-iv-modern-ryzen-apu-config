// THE HUD-LESS PICTURE at hdr=1, the game's side.
// AMD's interpolation takes the presented picture and one with no interface, and keeps the real frame's pixel wherever the two
// differ; with one picture the HUD would be interpolated with the scene.
// The HUD-less picture is the presented frame with only the pixels the interface changed replaced by the encode of the frame
// before the interface drew (shaders/hdr_encode_ps.hlsl). Interface pixels are told by pixels, not draws: the copy at the first
// interface draw (hdr.h, HdrPreHudCapture) against the frame's end (HdrUiCoverage).
// The phone is a 3D model with its own projection drawn into the back buffer before the 2D interface, which the boundary rule
// misses. While engaged, its frame's HUD-less picture comes from a copy taken ahead of it in frame generation's own texture
// (FgHud::ovTex), never the encode's copy, so the real picture still treats the phone as scene. Inside the overlay's rect every
// pixel where the two copies differ in any of the four channels is the phone: there the HUD-less picture is the frame ahead and
// its alpha is 0, and the mod's FSR library takes the presented pixel in full; alpha is 1 elsewhere. An overlay is told by
// its phase and by a viewport that is not the whole target, never by its model.
// Where it is drawn: by the real encode itself as a second output (HDR_FGMRT) into a target of its own, copied to the helper's
// HUDLESS in the worker's one span, so no HUD-less picture ever stands in the back buffer. The target is made at the first
// engaged encode and given back FGHUD_T2_IDLE encodes after frame generation was last wanted, and before a Reset. Its Vulkan
// image is asked fresh at every hand: DXVK's defragmenter can move the texture to a new image (FgHudSrcTake).
// If the target or shader is missing, the game holds D3DRS_COLORWRITEENABLE1 off (read, never set), or a debug view ran, the
// frame has no HUD-less picture and its generated frame is not shown (FG_HUD_NONE). The back buffer is always the real encode's.
// Linux: only D3D9 calls DXVK serves, and ID3D9VkInteropTexture as the transport uses it.
#pragma once
#include "fg_hud_rule.h"
#include "shaders/hdr_fgmrt_ps.h"
#include "shaders/hdr_fgmrt_cga_ps.h"
#include "shaders/hdr_fgmrt_g10_ps.h"
#include "shaders/hdr_fgmrt_cga_g10_ps.h"
#include "shaders/hdr_fgmrt_ov_ps.h"
#include "shaders/hdr_fgmrt_ov_cga_ps.h"
#include "shaders/hdr_fgmrt_ov_g10_ps.h"
#include "shaders/hdr_fgmrt_ov_cga_g10_ps.h"

// What a frame has for a HUD-less picture (fg_machine.h's FG_HUD_*).
enum { FGHUD_NONE_ENCODE = 0, FGHUD_NONE_COPY = 1, FGHUD_NONE_MEANS = 2, FGHUD_NONE_N };
// FGHUD_T2_IDLE: encodes after frame generation was last wanted before the second target is given back (10 s at 60)
enum { FGHUD_WANT_N = 5, FGHUD_T2_IDLE = 600 };
static_assert(FGHUD_T2_IDLE == FG_HUDT2_IDLE, "the idle time is the rule's (fg_hud_rule.h): the number stays on the line above for tools/test-fg-hud-notice's cut");
// The sampler the frame ahead of a 3D overlay is bound on (hdr_encode_ps.hlsl, sPreFg); saved and restored round the draw.
enum { FGHUD_OV_SLOT = 5 };
// The pixel constant row the overlay's rect is handed in (gFgOv), the first beyond the nine the encode's bracket saves;
// saved and restored here.
enum { FGHUD_OV_ROW = 9 };
static_assert(FG_HUD_SAME == 0 && FG_HUD_PASS == 1 && FG_HUD_NONE == 2 && FG_HUD_WIDE == 3 && FG_HUD_TEX == 4, "FgHud::want is indexed by the machine's FG_HUD_* (fg_machine.h)");

struct FgHud
{
        // ---- the capture: what the real encode of the last frame said of that frame
    bool   have;
    bool   held, g10;                    // the absorbed encode; it sharpened
    bool   hudDrew;                      // the 2D pass drew at or after the interface boundary
    bool   preOk;                        // and the boundary copy is that frame's
    bool   second;                       // drawn with two targets: the HUD-less picture is in the second
    UINT   frame;                        // hdrFramesSeen at that encode: the capture is this present's only while the count is one past it
        // ---- the second target and the four encodes that write it (index: held | g10 << 1)
    IDirect3DTexture9*     t2Tex;
    IDirect3DSurface9*     t2Surf;
    UINT   t2W, t2H;
    bool   t2Tried;                      // refused for good (multisampled back buffer) until the next Reset
    UINT   t2Idle;                       // encodes in a row with frame generation not wanted
        // As read at the make, for its own check only: no copy names them, since DXVK may move the texture at any flush.
    // Written by the render thread with the ring drained; t2Live any thread may take down.
    VkImage       t2Img;
    VkImageLayout t2Layout;
    VkFormat      t2Fmt;
    volatile LONG t2Live;
        // The worker's own, inside one hand's span: the image and layout asked after the span's flush (FgHudSrcOk) and named by that
    // span's copy.
    VkImage       srcImg;
    VkImageLayout srcLayout;
    IDirect3DPixelShader9* mrt[4];
    bool   mrtTried[4];
    UINT   mrtWant;                      // variants asked for and not found (made after the encode's bracket)
        // ---- counts, render thread
    volatile LONG want[FGHUD_WANT_N];
    volatile LONG none[FGHUD_NONE_N];
    volatile LONG seconds;
    volatile LONG t2Made, t2Given, t2Refused;
        // ---- the copy ahead of a 3D overlay (the phone): frame generation's own, never the encode's
    IDirect3DTexture9*     ovTex;
    IDirect3DSurface9*     ovSurf;
    UINT   ovW, ovH;
    UINT   ovRetryAt;                    // after a refused create, the frame to ask again (300 on)
    UINT   t2RetryAt;                    // after a failed make, the frame to ask again (FG_HUDT2_RETRY on)
    UINT   ovIdle;
        // stamps are hdrFramesSeen + 1 of their frame; 0 none
    UINT   ovOpenAt;                     // an overlay drew before the interface's first draw
    UINT   ovCopyAt;                     // and ovTex holds the copy ahead of it
        // The overlay's rect in back-buffer pixels: the union of its draws' viewports before the interface. The exact cover is asked
    // inside it alone.
    UINT   ovX0, ovY0, ovX1, ovY1;
    float  ovOldC[4];                    // FGHUD_OV_ROW as it stood, restored with the sampler
    IDirect3DPixelShader9* mrtOv[4];
    bool   mrtOvTried[4];
    UINT   mrtOvWant;
    bool   ovBound;                      // between FgHudSecond and FgHudCapture
    IDirect3DBaseTexture9* ovOldTex;
    DWORD  ovOldSt[6];
    volatile LONG ovMade, ovGiven, ovRefused;
        // ---- SDR: what the sequence's binding (fg_hud_sdr.h) keeps of Direct3D's; its own state is fgHudSdr.
    const BYTE* sdrCgaLo; const BYTE* sdrCgaHi;   // told by the draw's return address
    IDirect3DBaseTexture9* sdrCgaTex;    // referenced from the copy to the frame's end: sampler 0 must hold it
    IDirect3DTexture9*     sdrPreTex;    // the curve's copy: D3D9's alone, no raw copy of the worker's names it
    IDirect3DSurface9*     sdrPreSurf;
    UINT   sdrPreW, sdrPreH, sdrPreIdle, sdrPreRetryAt;
        // Padding: keeps fgHud's size so no static behind it moves.
    char   keep[32];
};
static FgHud fgHud;

// Is the three-image set asked for (FgxAfterBuild's build flags)?
static bool FgHudSetWanted()
{
    return true;
}
// Which form draws the picture: 1 the real encode's second target.
static int FgHudForm()
{
    return 1;
}
// The machine is installed, its driver wants a pair, and the set has the image. Two loads and a compare in standby.
static bool FgHudOn()
{
    const FgState* s = fgp.s;
    if (!s || !fgx.c || !fgx.live) return false;
    const LONG want = s->wt.want;
    if (want != FG_WANT_ON) return false;
    return fgx.c->set.img[FEED_FG_HUDLESS] != VK_NULL_HANDLE;
}
// The boundary copy is taken for frame generation too: with hdr_ui = -1 the encode itself does not need it.
static bool FgHudWantsCopy() { return FgHudOn(); }
// Before a Reset, and once no frame has had an overlay for FGHUD_T2_IDLE encodes. Only D3D9 draws of this thread read it, so its
// Release (the hooked one, recorded after the draws that sampled it) needs no wait.
static void FgHudOverlayGive()
{
    FgHud& h = fgHud;
    if (h.ovSurf) { h.ovSurf->Release(); h.ovSurf = nullptr; }
    if (h.ovTex)  { h.ovTex->Release();  h.ovTex  = nullptr; h.ovGiven++; }
    h.ovW = 0; h.ovH = 0; h.ovIdle = 0; h.ovCopyAt = 0;
}
// The back buffer into frame generation's own texture, by the same StretchRect HdrPreHudCapture uses. A refused create is asked
// again 300 frames on. False: no copy, and the frame is handed with no HUD-less picture.
static bool FgHudOverlayCopy(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    D3DSURFACE_DESC d = {};
    if (!backbufferSurf || FAILED(backbufferSurf->GetDesc(&d)) || !d.Width || !d.Height) return false;
    if (!h.ovTex || h.ovW != d.Width || h.ovH != d.Height)
    {
        if (!h.ovTex && hdrFramesSeen < h.ovRetryAt) return false;
        FgHudOverlayGive();
        RsOwnCreateCall guard;
        if (FAILED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &h.ovTex, nullptr)) || !h.ovTex
            || FAILED(h.ovTex->GetSurfaceLevel(0, &h.ovSurf)) || !h.ovSurf)
        {
            if (h.ovTex) { h.ovTex->Release(); h.ovTex = nullptr; }
            h.ovSurf = nullptr; h.ovRetryAt = hdrFramesSeen + 300;
            if (h.ovRefused++ < 3)
                LogRecord("  frame generation: the %ux%u copy of the frame ahead of a 3D overlay (the phone) could not be made: such frames are handed with no HUD-less picture (asked again in 300 frames)", d.Width, d.Height);
            return false;
        }
        h.ovW = d.Width; h.ovH = d.Height; h.ovIdle = 0;
        h.ovMade++;
        LogHealth("  frame generation: the copy of the frame ahead of a 3D overlay (the phone) made for this back buffer: %ux%u D3DFORMAT %d (made %ld time(s), given back %ld)",
                  d.Width, d.Height, (int)d.Format, (long)h.ovMade, (long)h.ovGiven);
    }
    if (FAILED(dev->StretchRect(backbufferSurf, nullptr, h.ovSurf, nullptr, D3DTEXF_NONE))) return false;
    return true;
}
// From IvtOnDraw, for a back-buffer draw the boundary rule did not take: is this a 3D overlay ahead of the interface (the phone)?
// At the frame's first such draw frame generation's copy is taken here, ahead of it; nothing of the encode's is touched. Judged by
// FgHudOverlay3dRule. Cost: a handful of draws a frame; GetViewport and GetRenderState come from the device thread's copy.
// Not a draw of the mod's, not into the render scale's stand-in, not ahead of the mod's resolve, and not while the sharpening
// pass is owed (a copy ahead of it would differ everywhere).
static bool FgHudOverlay3d(const DrawRec& rec, IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    const UINT stamp = hdrFramesSeen + 1u;
    if (rec.ortho || !rec.bb || !dev || rsOwn || !FgHudOn()) return false;
    if (rec.sceneKnown && rec.scene)
    {
        return false;
    }
    FgHudDrawFacts f = {};
    f.backBuffer = curRT0 != nullptr && curRT0 == backbufferSurf;
    f.resolved = ivtInjected;
    f.passOwed = shpOn && !SHP_G10() && !shpDone;
    f.bbBefore = hdrBbDraws; f.sceneBefore = hdrSceneDraws;
    f.outW = rsOutW; f.outH = rsOutH;
    if (!f.backBuffer || !f.resolved || f.passOwed) return false;
    D3DVIEWPORT9 vp = {};
    if (FAILED(dev->GetViewport(&vp))) return false;
    f.vpX = vp.X; f.vpY = vp.Y; f.vpW = vp.Width; f.vpH = vp.Height;
    f.colour = true;
    if (!FgHudOverlay3dRule(f))                    // the whole target's viewport, mostly: no render state read
    {
        return false;
    }
    DWORD cw = 0;
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
    f.colour = (cw & 0xFu) != 0;
    if (!FgHudOverlay3dRule(f)) return false;
        // The copy, once a frame. An overlay after the interface's first draw is already in the encode's own copy.
    if (FgHudOverlayCopyRule(h.ovOpenAt == stamp, hdrPreHudDraws))
    {
        h.ovOpenAt = stamp;
        h.ovX0 = f.vpX; h.ovY0 = f.vpY; h.ovX1 = f.vpX + f.vpW; h.ovY1 = f.vpY + f.vpH;
        if (FgHudOverlayCopy(dev)) h.ovCopyAt = stamp;
    }
    else if (h.ovOpenAt == stamp && hdrPreHudDraws == 0)
    {   // a later overlay draw still ahead of the encode's copy: its viewport joins the rect
        if (f.vpX < h.ovX0) h.ovX0 = f.vpX;
        if (f.vpY < h.ovY0) h.ovY0 = f.vpY;
        if (f.vpX + f.vpW > h.ovX1) h.ovX1 = f.vpX + f.vpW;
        if (f.vpY + f.vpH > h.ovY1) h.ovY1 = f.vpY + f.vpH;
    }
    return true;
}
// Asked at this frame's encode, while hdrFramesSeen is still this frame's.
static bool FgHudOverlayNow()
{
    const FgHud& h = fgHud;
    return h.ovTex != nullptr && h.ovCopyAt == hdrFramesSeen + 1u && h.ovW == rsOutW && h.ovH == rsOutH;
}
static bool FgHudOverlayOpened() { return fgHud.ovOpenAt == hdrFramesSeen + 1u; }
// as hdr_encode.h, c[1][1], asks it
static bool FgHudEncodeCopyNow() { return hdrPreHudDraws > 0 && hdrPreHudValid && hdrPreHudFrame == hdrFramesSeen && hdrPreHudTex != nullptr; }

static void FgHudDrop()
{
    FgHud& h = fgHud;
    h.have = false;
}

// ---- the second target
static bool FgHudRetireWait(DWORD ms);
// Render thread, after an encode's bracket. The ring is drained (a create drains it anyway), so the worker reads the new facts
// whole.
static void FgHudTargetMake(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    if (h.t2Tex || h.t2Tried || !dev || !backbufferSurf) return;
        // t2Tried is set only where the make fails (FgHudT2FailedAfter), so a target given back for idleness is made again.
    D3DSURFACE_DESC d = {};
    if (FAILED(backbufferSurf->GetDesc(&d)) || d.MultiSampleType != D3DMULTISAMPLE_NONE || !d.Width || !d.Height)
    {   // multisampled: refused until the next Reset; unreadable: asked again in FG_HUDT2_RETRY frames
        const bool forGood = d.MultiSampleType != D3DMULTISAMPLE_NONE;
        h.t2Tried = FgHudT2FailedAfter(false, forGood);
        if (!h.t2Tried) h.t2RetryAt = hdrFramesSeen + FG_HUDT2_RETRY;
        if (h.t2Refused++ < 3)
            LogRecord("  frame generation: the encode's second target for the HUD-less picture is not made (%s): frames with an interface are handed with no HUD-less picture%s",
                      forGood ? "the back buffer is multisampled" : "the back buffer could not be read", forGood ? " until the next Reset" : " (asked again in 300 frames)");
        return;
    }
    DT_DIRECT();
    RsOwnCreateCall guard;
    IDirect3DTexture9* t = nullptr; IDirect3DSurface9* s = nullptr;
    VkImage img = VK_NULL_HANDLE; VkImageLayout lay = VK_IMAGE_LAYOUT_UNDEFINED; VkFormat fmt = VK_FORMAT_UNDEFINED; VkExtent3D ext = {};
    const bool ok = SUCCEEDED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &t, nullptr)) && t
                 && SUCCEEDED(t->GetSurfaceLevel(0, &s)) && s
                 && IvtSurfaceImage(s, &img, &lay, &fmt, &ext) && ext.width == d.Width && ext.height == d.Height;
    if (!ok)
    {
        if (s) s->Release();
        if (t) t->Release();
        h.t2Tried = FgHudT2FailedAfter(false, false);   // a failed create is asked again
        if (!h.t2Tried) h.t2RetryAt = hdrFramesSeen + FG_HUDT2_RETRY;
        if (h.t2Refused++ < 3)
            LogRecord("  frame generation: the encode's second target for the HUD-less picture (%ux%u D3DFORMAT %d) could not be made: frames with an interface are handed with no HUD-less picture (asked again in 300 frames)",
                      d.Width, d.Height, (int)d.Format);
        return;
    }
    h.t2Tex = t; h.t2Surf = s; h.t2W = d.Width; h.t2H = d.Height; h.t2Idle = 0; h.t2Tried = FgHudT2FailedAfter(true, false); h.t2RetryAt = 0;
    h.t2Img = img; h.t2Layout = lay; h.t2Fmt = fmt;
    _InterlockedExchange(&h.t2Live, 1);
    h.t2Made++;
    LogHealth("  frame generation: the encode's second target for the HUD-less picture made for this back buffer: %ux%u D3DFORMAT %d, VkFormat %d (made %ld time(s), given back %ld)",
              d.Width, d.Height, (int)d.Format, (int)fmt, (long)h.t2Made, (long)h.t2Given);
}
// A D3D9 target that outlives a Reset fails the Reset. The worker's view goes first; the ring is then drained so every copy naming
// the target is done; one not retired leaves it for another round. At a Reset the layer is disarmed and the machine's ring
// retire runs ahead of HdrEncodeRelease (tools/check-fg-fence.py holds that order).
static void FgHudTargetGive(bool reset)
{
    FgHud& h = fgHud;
    if (!h.t2Tex) return;
    _InterlockedExchange(&h.t2Live, 0);
    {
        DtSyncScope direct(DT_R_DIRECT);
        if (direct.on)
        {
            if (!FgHudRetireWait(reset ? 2000 : 250) && !reset) { h.t2Idle = 0; _InterlockedExchange(&h.t2Live, 1); return; }
            h.t2Img = VK_NULL_HANDLE;              // worker idle and ring empty: the one place it is taken down
        }
    }
    if (h.t2Surf) { h.t2Surf->Release(); h.t2Surf = nullptr; }
    h.t2Tex->Release(); h.t2Tex = nullptr;
    h.t2W = 0; h.t2H = 0; h.t2Idle = 0;
    h.t2Given++;
}
// Made on first need; one refused is tried again after the next Reset.
static IDirect3DPixelShader9* FgHudMrtShader(IDirect3DDevice9* dev, int k)
{
    FgHud& h = fgHud;
    if (!h.mrt[k] && !h.mrtTried[k])
    {
        h.mrtTried[k] = true;
        RsOwnCreateCall guard;
        const BYTE* const code = k == 0 ? kHdrFgMrtPS : k == 1 ? kHdrFgMrtCgaPS : k == 2 ? kHdrFgMrtG10PS : kHdrFgMrtCgaG10PS;
        if (FAILED(dev->CreatePixelShader((const DWORD*)code, &h.mrt[k]))) h.mrt[k] = nullptr;
        LogRecord("  frame generation: the encode with the HUD-less picture as its second output (%s%s) %s", k & 1 ? "absorbed" : "plain", k & 2 ? ", sharpening" : "",
                  h.mrt[k] ? "created" : "was REFUSED: frames with an interface are handed with no HUD-less picture");
    }
    return h.mrt[k];
}
// HDR_FGOV, made on first need once an overlay's copy stands; one refused is tried again after the next Reset.
static IDirect3DPixelShader9* FgHudMrtOvShader(IDirect3DDevice9* dev, int k)
{
    FgHud& h = fgHud;
    if (!h.mrtOv[k] && !h.mrtOvTried[k])
    {
        h.mrtOvTried[k] = true;
        RsOwnCreateCall guard;
        const BYTE* const code = k == 0 ? kHdrFgMrtOvPS : k == 1 ? kHdrFgMrtOvCgaPS : k == 2 ? kHdrFgMrtOvG10PS : kHdrFgMrtOvCgaG10PS;
        if (FAILED(dev->CreatePixelShader((const DWORD*)code, &h.mrtOv[k]))) h.mrtOv[k] = nullptr;
        LogRecord("  frame generation: the encode of a frame with a 3D overlay ahead of the interface (the phone; %s%s) %s", k & 1 ? "absorbed" : "plain", k & 2 ? ", sharpening" : "",
                  h.mrtOv[k] ? "created" : "was REFUSED: frames with the phone up are handed with no HUD-less picture");
    }
    return h.mrtOv[k];
}
// Once an encode, after its bracket: made while engaged, given back after FGHUD_T2_IDLE unwanted encodes. Not engaged: two loads
// and one more.
static void FgHudTargetTick(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    if (h.ovTex) { if (h.ovCopyAt == hdrFramesSeen + 1u) h.ovIdle = 0; else if (++h.ovIdle >= FGHUD_T2_IDLE) FgHudOverlayGive(); }
    const bool engaged = FgHudOn() && FgHudForm() == 1;
        // While the armed driver can engage again it waits FG_HUDT2_IDLE_DRIVEN encodes, so the law's REST does not remake it (a create
    // drains the ring) at every turn.
    const bool driven = fgp.s != nullptr && fgx.live;
    const int act = FgHudT2Tick(engaged, h.t2Tex != nullptr, h.t2Tried, hdrFramesSeen, h.t2RetryAt, &h.t2Idle, driven ? (unsigned)FG_HUDT2_IDLE_DRIVEN : (unsigned)FGHUD_T2_IDLE);   // fg_hud_rule.h; the harness runs the same
    if (act == FG_HUDT2_MAKE) FgHudTargetMake(dev);
    if (engaged)
    {
        if (h.t2Tex && dev)
        {
            const UINT wantNow = h.mrtWant | 3u | (shpG10 ? 12u : 0u);
            for (int k = 0; k < 4; k++) if ((wantNow & (1u << k)) && !h.mrt[k] && !h.mrtTried[k]) FgHudMrtShader(dev, k);
            h.mrtWant = 0;
            if (h.ovTex)
            {
                const UINT wantOv = h.mrtOvWant | 3u | (shpG10 ? 12u : 0u);
                for (int k = 0; k < 4; k++) if ((wantOv & (1u << k)) && !h.mrtOv[k] && !h.mrtOvTried[k]) FgHudMrtOvShader(dev, k);
                h.mrtOvWant = 0;
            }
        }
        return;
    }
    if (act == FG_HUDT2_GIVE) FgHudTargetGive(false);
}
// From HdrEncode, between its SetPixelShader and its draw, on a frame with an interface while engaged: the second target is
// bound and the encode with THE HUD-LESS PICTURE as its second output takes the real one's place; IvtRestoreState binds target 1
// back. Not under a debug view, not where the boundary copy is not this frame's, not where the game holds target 1's colour
// writes off (read, never set): then the frame has no HUD-less picture.
// With a 3D overlay ahead of the interface: both copies, an HDR_FGOV encode with the overlay copy on FGHUD_OV_SLOT; the overlay's
// copy alone, the plain encode with it in the encode copy's place (the real picture's mask is off then); no overlay copy, no
// HUD-less picture. Not engaged: one store and one load.
static void FgHudSecond(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps, bool held, bool g10, bool view)
{
    FgHud& h = fgHud;
    h.second = false;
    if (!h.t2Surf) return;
    if (view || !ps || !FgHudOn() || FgHudForm() != 1) return;
    const int src = FgHudSecondRule(FgHudEncodeCopyNow(), FgHudOverlayOpened(), FgHudOverlayNow());
    if (src == FG_HUDSRC_NONE) return;
    if (h.t2W != rsOutW || h.t2H != rsOutH) return;
    const int k = (held ? 1 : 0) | (g10 ? 2 : 0);
    const bool two = src == FG_HUDSRC_TWO;
    IDirect3DPixelShader9* const m = two ? h.mrtOv[k] : h.mrt[k];
    if (!m) { (two ? h.mrtOvWant : h.mrtWant) |= 1u << k; return; }
    DWORD cw1 = 0;
    dev->GetRenderState(D3DRS_COLORWRITEENABLE1, &cw1);
    if ((cw1 & 0xFu) != 0xFu) return;
    if (two)
    {
        const DWORD s = FGHUD_OV_SLOT;
        h.ovOldTex = nullptr; dev->GetTexture(s, &h.ovOldTex);
        for (int j = 0; j < 6; ++j) dev->GetSamplerState(s, kHdrExtraSamp[j], &h.ovOldSt[j]);
        dev->SetTexture(s, h.ovTex);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT); dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
                // the overlay's rect, for the exact cover (gFgOv)
        const float rect[4] = { (float)h.ovX0, (float)h.ovY0, (float)h.ovX1, (float)h.ovY1 };
        memset(h.ovOldC, 0, sizeof(h.ovOldC));
        dev->GetPixelShaderConstantF(FGHUD_OV_ROW, h.ovOldC, 1);
        dev->SetPixelShaderConstantF(FGHUD_OV_ROW, rect, 1);
        h.ovBound = true;
    }
    else if (src == FG_HUDSRC_OVERLAY)
    {
        dev->SetTexture(kHdrExtraSlot[5], h.ovTex);    // point and clamp by HdrBindExtra, which also restores it
    }
    dev->SetRenderTarget(1, h.t2Surf);
    dev->SetPixelShader(m);
    h.second = true; h.seconds++;
}
// After the encode's own restore: the overlay copy's sampler, as it stood.
static void FgHudOverlayUnbind(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    if (!h.ovBound) return;
    h.ovBound = false;
    const DWORD s = FGHUD_OV_SLOT;
    if (dev)
    {
        dev->SetTexture(s, h.ovOldTex);
        for (int j = 0; j < 6; ++j) dev->SetSamplerState(s, kHdrExtraSamp[j], h.ovOldSt[j]);
        dev->SetPixelShaderConstantF(FGHUD_OV_ROW, h.ovOldC, 1);
    }
    if (h.ovOldTex) { h.ovOldTex->Release(); h.ovOldTex = nullptr; }
}


// ==================================================================================
// SDR: THE HUD-LESS PICTURE of an 8-bit back buffer (rules: fg_hud_rule.h; sequence: fg_hud_sdr.h).
// In SDR no encode of the mod's runs, so THE HUD-LESS PICTURE is a copy of the back buffer at the interface's first draw, by the
// boundary rule, into the same second target; the worker copies it as for HDR. Pixels the interface did not touch are then the
// presented picture's bit for bit.
// FusionFix's console gamma is a full-screen pass after the interface, so a boundary copy would differ everywhere. Where the
// curve was told on the frame before (the latch), the copy goes into sdrPreTex and FusionFix's draw is replayed ahead of itself
// into the second target with that copy on sampler 0 (fg_hud_sdr_d3d.h). The curve is told only inside the frame: another
// module's point copy of the whole back buffer followed by its first back-buffer draw with sampler 0 holding that copy.
// A frame where latch and curve disagree, or the replay did not run, has no HUD-less picture. With the phone up in SDR no
// generated frame is shown (the exact cover needs 16-bit alpha).
// Cost while engaged, on a frame with an interface: one StretchRect (about 49 us at 3840x2160 on an RTX 4090), about 105 us with
// console gamma. Linux: only calls DXVK serves and the game or FusionFix already make.
#include "fg_hud_sdr.h"
#include "fg_hud_sdr_d3d.h"
static FgHudSdrState fgHudSdr;

// D3D9's alone, so its Release is the hooked one, recorded after the draw that read it.
static void FgHudSdrPreGive()
{
    FgHud& h = fgHud;
    if (h.sdrPreSurf) { h.sdrPreSurf->Release(); h.sdrPreSurf = nullptr; }
    if (h.sdrPreTex)  { h.sdrPreTex->Release();  h.sdrPreTex  = nullptr; }
    h.sdrPreW = 0; h.sdrPreH = 0; h.sdrPreIdle = 0;
}
static void FgHudSdrPreMake(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    D3DSURFACE_DESC d = {};
    if (!dev || !h.t2Surf || FAILED(h.t2Surf->GetDesc(&d))) return;
    RsOwnCreateCall guard;
    if (FAILED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &h.sdrPreTex, nullptr)) || !h.sdrPreTex
        || FAILED(h.sdrPreTex->GetSurfaceLevel(0, &h.sdrPreSurf)) || !h.sdrPreSurf)
    {
        if (h.sdrPreTex) { h.sdrPreTex->Release(); h.sdrPreTex = nullptr; }
        h.sdrPreSurf = nullptr; h.sdrPreRetryAt = hdrFramesSeen + FG_HUDT2_RETRY;   // asked again as the target is; until then the curve's frames have no picture
        return;
    }
    h.sdrPreW = d.Width; h.sdrPreH = d.Height; h.sdrPreIdle = 0;
}
// Where the copy the curve reads was given back: the mod's full-size stand-in under a render scale.
static IDirect3DBaseTexture9* FgHudSdrStandIn(IDirect3DBaseTexture9* game)
{
    for (int i = 0; i < rsRbN; i++) if ((IDirect3DBaseTexture9*)rsRb[i].gameTex == game) return (IDirect3DBaseTexture9*)rsRb[i].ours;
    return nullptr;
}
// The sequence's binding (fg_hud_sdr.h says what each answers). t0: sampler 0's texture, referenced by CurveFacts, released by
// the caller.
struct FgHudSdrDrawCtx { const DrawRec* rec; IDirect3DDevice9* dev; };
struct FgHudSdrCopyCtx { IDirect3DDevice9* dev; void* ret; IDirect3DSurface9* dst; };
struct FgHudSdrModCtx { IDirect3DDevice9* dev; void* ret; bool up; D3DPRIMITIVETYPE type; UINT start, prims; const void* vtx; UINT stride; mutable IDirect3DBaseTexture9* t0; };
struct FgHudSdrPB
{
    static FgHudSdrState& St() { return fgHudSdr; }
    static unsigned Stamp() { return hdrFramesSeen + 1u; }
    static bool SceneBefore() { return ivtInjected || ivtSceneDraws > 0; }
    static bool Resolved() { return ivtInjected; }
    static bool RtIsBackBuffer() { return curRT0 != nullptr && curRT0 == backbufferSurf; }
    static bool OpaqueQuad(const FgHudSdrDrawCtx& d) { return IvtIsOpaqueQuad(*d.rec, d.dev); }
        // GetViewport and one GetRenderState, both from the device thread's copy
    static bool Overlay3d(const FgHudSdrDrawCtx& d, unsigned bbBefore, bool sceneBefore)
    {
        FgHudDrawFacts f = {};
        f.backBuffer = RtIsBackBuffer(); f.resolved = ivtInjected; f.passOwed = false;
        f.bbBefore = bbBefore; f.sceneBefore = sceneBefore ? 1u : 0u; f.outW = rsOutW; f.outH = rsOutH;
        if (!f.backBuffer || !f.resolved || !bbBefore) return false;
        D3DVIEWPORT9 vp = {};
        if (FAILED(d.dev->GetViewport(&vp))) return false;
        f.vpX = vp.X; f.vpY = vp.Y; f.vpW = vp.Width; f.vpH = vp.Height;
        f.colour = true;
        if (!FgHudOverlay3dRule(f)) return false;
        DWORD cw = 0;
        d.dev->GetRenderState(D3DRS_COLORWRITEENABLE, &cw);
        f.colour = (cw & 0xFu) != 0;
        return FgHudOverlay3dRule(f);
    }
        // HdrPreHudCapture's StretchRect, as the mod's own (the game's read-back books do not count it)
    static int Copy(const FgHudSdrDrawCtx& d, bool intoPre)
    {
        FgHud& h = fgHud;
        if (!h.t2Surf || h.t2W != rsOutW || h.t2H != rsOutH) return 0;
        if (intoPre && (!h.sdrPreSurf || h.sdrPreW != h.t2W || h.sdrPreH != h.t2H)) return -1;
        RsOwnCall guard;
        return SUCCEEDED(d.dev->StretchRect(backbufferSurf, nullptr, intoPre ? h.sdrPreSurf : h.t2Surf, nullptr, D3DTEXF_NONE)) ? 1 : 0;
    }
        // inside the frame: after the resolve, before hkEndScene's IvtTick
    static bool InFrame() { return ivtInjected; }
    static bool Arm(const FgHudSdrCopyCtx& c)
    {
        FgHud& h = fgHud;
        if (!ShpCallerIsForeign(c.ret)) return false;                  // the game's read-backs and the mod's own copies are not another module's
        IDirect3DTexture9* cont = nullptr;
        if (FAILED(c.dst->GetContainer(__uuidof(IDirect3DTexture9), (void**)&cont)) || !cont) return false;   // a copy no sampler can read
        const BYTE* const a = (const BYTE*)c.ret;
        if (a < h.sdrCgaLo || a >= h.sdrCgaHi)
        {
            HMODULE m = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)c.ret, &m) || !m) { cont->Release(); return false; }
            const IMAGE_DOS_HEADER* const dos = (const IMAGE_DOS_HEADER*)m;
            const IMAGE_NT_HEADERS* const nt  = (const IMAGE_NT_HEADERS*)((const BYTE*)m + dos->e_lfanew);
            h.sdrCgaLo = (const BYTE*)m; h.sdrCgaHi = h.sdrCgaLo + nt->OptionalHeader.SizeOfImage;
        }
        if (h.sdrCgaTex) h.sdrCgaTex->Release();
        h.sdrCgaTex = cont;                                             // referenced to the frame's end: FgHudSdrFrame releases it
        return true;
    }
    static void SetArmed(bool a) { fgSdrCgaArmed = a; }
    static bool SameModule(const FgHudSdrModCtx& m) { const BYTE* const a = (const BYTE*)m.ret; return a >= fgHud.sdrCgaLo && a < fgHud.sdrCgaHi; }
        // The absorb's facts (hdr_cga.h, HdrCgaOnDraw), from the device thread's copy, under the caller's RsOwnCall.
    static FgHudSdrCurveFacts CurveFacts(const FgHudSdrModCtx& m)
    {
        FgHud& h = fgHud;
        IDirect3DDevice9* const dev = m.dev;
        FgHudSdrCurveFacts f = {};
        f.sameModule = true;
        DWORD v = 0, w = 0;
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &v);  f.blendOff = v == 0;
        dev->GetRenderState(D3DRS_COLORWRITEENABLE, &v);  f.allChannels = (v & 0xFu) == 0xFu;
        dev->GetRenderState(D3DRS_ALPHATESTENABLE, &v);   f.alphaTestOff = v == 0;
        dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &v); f.scissorOff = v == 0;
        dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &w); dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &v); f.srgbOff = w == 0 && v == 0;
        f.depthOff = true;
        IDirect3DSurface9* ds = nullptr;
        dev->GetDepthStencilSurface(&ds);
        if (ds) { ds->Release(); DWORD z = 0, st = 0; dev->GetRenderState(D3DRS_ZENABLE, &z); dev->GetRenderState(D3DRS_STENCILENABLE, &st); f.depthOff = z == D3DZB_FALSE && st == 0; }
        D3DVIEWPORT9 vp = {};
        dev->GetViewport(&vp);
        f.fullViewport = vp.X == 0 && vp.Y == 0 && vp.Width == rsOutW && vp.Height == rsOutH;
        f.oneTarget = !curRT123[0] && !curRT123[1] && !curRT123[2];
        dev->GetTexture(0, &m.t0);
        f.t0Copy = m.t0 && h.sdrCgaTex && (m.t0 == h.sdrCgaTex || (rsActive && FgHudSdrStandIn(h.sdrCgaTex) == m.t0));
        return f;
    }
        // Through the original entry point (no hook of the mod's sees it). rsScreenTexKind is restored as FusionFix's bind left it.
    static bool Replay(const FgHudSdrModCtx& m)
    {
        FgHud& h = fgHud;
        if (!h.sdrPreTex || !h.t2Surf || (m.up ? origDrawPrimUP == nullptr : origDrawPrim == nullptr)) return false;
        const bool wasOwn = ivtInOwnDraw;
        const int kind = rsScreenTexKind;
        ivtInOwnDraw = true;
        const HRESULT hr = FgHudSdrReplayD3D(m.dev, h.t2Surf, backbufferSurf, h.sdrPreTex, m.t0,
            [&]() { return m.up ? origDrawPrimUP(m.dev, m.type, m.prims, m.vtx, m.stride) : origDrawPrim(m.dev, m.type, m.start, m.prims); });
        rsScreenTexKind = kind;
        ivtInOwnDraw = wasOwn;
        return SUCCEEDED(hr);
    }
        // In HDR's own capture fields, which FgHudWant reads at the Present.
    static void Publish(bool hudDrew, bool pic)
    {
        FgHud& h = fgHud;
        FgHudDrop();
        h.held = false; h.g10 = false;
        h.frame = hdrFramesSeen;             // hkEndScene counts the frame after this: at its Present the count is one more
        h.hudDrew = hudDrew;
        h.preOk = pic;
        h.second = pic;
        h.have = true;
        if (pic) h.seconds++;
    }
};
// From IvtOnDraw, after HDR's block, for a back-buffer draw while fgSdrHooks stands. After the interface's first draw: a count and
// one compare.
static void FgHudSdrOnDraw(const DrawRec& rec)
{
    IDirect3DDevice9* const dev = ivtDev ? ivtDev : hdrArmDev;
    if (!rec.bb || !dev || rsOwn) return;
    const FgHudSdrDrawCtx d = { &rec, dev };
    FgHudSdr<FgHudSdrPB>::OnDraw(d, rec.ortho, rec.sceneKnown && rec.scene);
}
// From hkStretchRect while fgSdrWatch stands: a point copy of the whole back buffer.
static void FgHudSdrCgaArm(IDirect3DDevice9* dev, void* ret, IDirect3DSurface9* dst)
{
    const FgHudSdrCopyCtx c = { dev, ret, dst };
    FgHudSdr<FgHudSdrPB>::OnCopy(c);
}
// From hkDrawPrim and hkDrawPrimUP after that copy, into the back buffer. The guard is raised before the first read.
static void FgHudSdrCgaDraw(IDirect3DDevice9* dev, void* ret, bool up, D3DPRIMITIVETYPE type, UINT start, UINT prims, const void* vtx, UINT stride)
{
    RsOwnCall guard;
    const FgHudSdrModCtx m = { dev, ret, up, type, start, prims, vtx, stride, nullptr };
    FgHudSdr<FgHudSdrPB>::OnModuleDraw(m);
    if (m.t0) { m.t0->Release(); m.t0 = nullptr; }
}
// Once a frame at its end: HDR's rule for the target, and the curve's copy beside it while a curve is latched (made in the same
// tick, given back with the target or after the target's idle time with no curve).
static void FgHudSdrTick(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    const bool engaged = FgHudOn() && FgHudForm() == 1;
    const bool driven = fgp.s != nullptr && fgx.live;
    const unsigned idleMax = driven ? (unsigned)FG_HUDT2_IDLE_DRIVEN : (unsigned)FGHUD_T2_IDLE;
    const int act = FgHudT2Tick(engaged, h.t2Tex != nullptr, h.t2Tried, hdrFramesSeen, h.t2RetryAt, &h.t2Idle, idleMax);
    if (act == FG_HUDT2_MAKE) FgHudTargetMake(dev);
    else if (act == FG_HUDT2_GIVE) FgHudTargetGive(false);
    if (h.sdrPreTex && (!h.t2Tex || h.sdrPreW != h.t2W || h.sdrPreH != h.t2H)) FgHudSdrPreGive();
    if (h.sdrPreTex) { if (fgHudSdr.latch) h.sdrPreIdle = 0; else if (++h.sdrPreIdle >= idleMax) FgHudSdrPreGive(); }
    else if (h.t2Tex && fgHudSdr.latch && hdrFramesSeen >= h.sdrPreRetryAt) FgHudSdrPreMake(dev);
}
// From hkEndScene after HdrEncode, before the count: the frame's verdict, the latch, the copy's texture released, the target's
// life, and what the next frame follows (fgSdrWatch, fgSdrHooks). An HDR frame is not touched here.
static void FgHudSdrFrame(IDirect3DDevice9* dev)
{
    FgHud& h = fgHud;
    const bool asked = fgSdrHooks && FgHudOn();
    if (hdrHooks)
    {
        fgSdrHooks = false; fgSdrWatch = false; fgSdrCgaArmed = false;
        if (h.sdrCgaTex) { h.sdrCgaTex->Release(); h.sdrCgaTex = nullptr; }
        return;
    }
    FgHudSdr<FgHudSdrPB>::Frame(asked);
    if (h.sdrCgaTex) { h.sdrCgaTex->Release(); h.sdrCgaTex = nullptr; }
    if (dev) FgHudSdrTick(dev);
    const bool sdr = !(ivtCfg.hdr && hdrEngaged);
    fgSdrWatch = sdr && fgp.s != nullptr && fgx.live;
    fgSdrHooks = sdr && FgHudOn() && FgHudForm() == 1 && h.t2Tex != nullptr;
}

// Before a Reset (HdrEncodeRelease): everything of this file's on the device.
static void FgHudRelease()
{
    FgHud& h = fgHud;
    FgHudDrop();
    FgHudTargetGive(true);
    h.t2Tried = false; h.t2RetryAt = 0; h.mrtWant = 0; h.second = false;
    for (int i = 0; i < 4; i++) { if (h.mrt[i]) { h.mrt[i]->Release(); h.mrt[i] = nullptr; } h.mrtTried[i] = false; }
    FgHudOverlayUnbind(nullptr);         // a reference left standing would fail the Reset
    FgHudOverlayGive();
    h.ovRetryAt = 0; h.ovOpenAt = 0; h.mrtOvWant = 0;
    for (int i = 0; i < 4; i++) { if (h.mrtOv[i]) { h.mrtOv[i]->Release(); h.mrtOv[i] = nullptr; } h.mrtOvTried[i] = false; }
        // SDR: the frame's facts start over; the latch is FusionFix's setting and stands.
    FgHudSdrPreGive();
    if (h.sdrCgaTex) { h.sdrCgaTex->Release(); h.sdrCgaTex = nullptr; }
    h.sdrPreRetryAt = 0; fgHudSdr.f = FgHudSdrFrameFacts();
    fgSdrHooks = false; fgSdrWatch = false; fgSdrCgaArmed = false;
}

// From HdrEncode after its draw and count. The second target's life comes first. frame: what the encode bound on sampler 0.
static void FgHudCapture(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps, IDirect3DBaseTexture9* frame, const float c[9][4], bool held, bool g10)
{
    FgHud& h = fgHud;
    FgHudOverlayUnbind(dev);
    FgHudDrop();
        // asked before the tick: the tick may give the overlay's copy back
    const bool ovNow = FgHudOverlayNow(), ovOpened = FgHudOverlayOpened(), encNow = FgHudEncodeCopyNow();
    FgHudTargetTick(dev);
    if (!FgHudOn() || !ps || !frame) { h.second = false; return; }
    (void)c;
    h.held = held; h.g10 = g10;
    h.frame = hdrFramesSeen;             // hkEndScene counts the frame after HdrEncode: at this frame's Present the count is one more
        // A 3D overlay ahead of the interface makes the frame one with an interface, whatever the 2D pass did.
    h.hudDrew = FgHudDrewRule(hdrPreHudDraws, ovOpened);
    h.preOk = FgHudPreOkRule(encNow, ovOpened, ovNow);
    h.have = true;
    (void)dev;
}


// At an engaged present, before the hand. FG_HUD_SAME: no interface and no notice drew, so the presented picture is its own
// HUD-less picture. FG_HUD_TEX: the encode wrote it into the second target. FG_HUD_NONE: an interface drew and there is no
// HUD-less picture. FG_HUD_WIDE: the check says the copy point is wrong.
static int FgHudWant(IDirect3DDevice9* dev, bool store)
{
    FgHud& h = fgHud;
    int r, why = -1;
        // the stamp, not the flag: a frame whose encode did not run left no capture of its own
    if (!h.have || h.frame + 1 != hdrFramesSeen || !dev || !backbufferSurf) { r = FG_HUD_NONE; why = FGHUD_NONE_ENCODE; }
        // A notice of the mod's on a frame with no game interface (source/notice.h): with no boundary copy the interpolation would carry
    // its text with the scene, so such a frame has no HUD-less picture and its generated frame is not shown.
    // tools/test-fg-hud-notice holds the rule.
    else if (!h.hudDrew) { if (NoticeDrewThisFrame()) { r = FG_HUD_NONE; why = FGHUD_NONE_COPY; } else r = FG_HUD_SAME; }
    else if (!h.preOk) { r = FG_HUD_NONE; why = FGHUD_NONE_COPY; }
    else if (!h.second) { r = FG_HUD_NONE; why = FGHUD_NONE_MEANS; }
    else r = FG_HUD_TEX;
    (void)store;
    h.want[r]++;
    if (why >= 0) h.none[why]++;
    return r;
}
// On the worker inside the hand's span, after its flush (FgPA::HudSrc): the target stands (a give-back drains the worker first),
// and the image DXVK holds it in now (FgHudSrcTake, never the make's) has the set's size and format. That image and layout are
// what this span's copy names.
struct FgHudVkQ
{
    static bool Query(IDirect3DSurface9* s, unsigned long long* i, int* l, unsigned* w, unsigned* h, int* f)
    {
        VkImage vi = VK_NULL_HANDLE; VkImageLayout vl = VK_IMAGE_LAYOUT_UNDEFINED; UINT ww = 0, hh = 0; VkFormat vf = VK_FORMAT_UNDEFINED;
        if (!FgvBbQuery(s, &vi, &vl, &ww, &hh, &vf)) return false;
        *i = (unsigned long long)vi; *l = (int)vl; *w = ww; *h = hh; *f = (int)vf;
        return true;
    }
};
static bool FgHudSrcOk(UINT w, UINT hgt, VkFormat fmt)
{
    DT_DIRECT();
    FgHud& h = fgHud;
    FgHudSrcNow n = {};
    const bool live = h.t2Live != 0 && h.t2W == w && h.t2H == hgt;
    const bool ok = FgHudSrcTake<FgHudVkQ>(live, h.t2Surf, w, hgt, (int)fmt, (unsigned long long)h.t2Img, (int)h.t2Layout, &n);
    h.srcImg = ok ? (VkImage)n.img : VK_NULL_HANDLE; h.srcLayout = ok ? (VkImageLayout)n.layout : VK_IMAGE_LAYOUT_UNDEFINED;
    return ok;
}

// FgHudWant never answers PASS here.
static void FgHudDraw(IDirect3DDevice9*) {}
static void FgHudUndo(IDirect3DDevice9*) {}
