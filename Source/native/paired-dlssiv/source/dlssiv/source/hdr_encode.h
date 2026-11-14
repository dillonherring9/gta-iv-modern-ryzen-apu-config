// hdr_encode.h: the HDR display encode pass, at the very end of the frame after the HUD (uses the transport header's
// full-screen pass machinery). One copy out, one draw back, emitting linear scRGB into the FP16 back buffer requested at
// CreateDevice and Reset (DXVK picks the surface format from that request). The whole chain:
// borderless window                  (fullscreen drops the HDMI link to SDR)
// A16B16G16R16F back buffer          (requested here)
// EXTENDED_SRGB_LINEAR swapchain     (set here, on DXVK's ID3D9VkExtSwapchain)
// Vulkan engine name not "DXVK"      (DLSS-IV.cpp; the driver's SDR path otherwise)
// this pass, nits / 80, no clamp anywhere downstream

#pragma once
#include "shaders/hdr_encode_ps.h"
#include "shaders/hdr_encode_g10_ps.h"          // G10: the encode that sharpens at its sample
#include "shaders/hdr_encode_cga_g10_ps.h"      // the same, absorbed
#include "shaders/hdr_debug_ps.h"
#include "hdr_xbox_gamma.h"
#include "shaders/hdr_lutprofile_ps.h"
#include "shaders/hdr_cgprobe_ps.h"

static bool hdrColorSpaceDone = false;
static UINT hdrPresentsAtEngage = 0, hdrEncodesAtEngage = 0;   // the divergence check counts from the last engage too
static UINT hdrCsEngagedFrame = 0;   // hdrFramesSeen at the last engage: the heartbeat counts from here, not from the session's start
// The surface cannot do scRGB, and asking again will not change that: set when the colour space failed for a reason of
// the driver or surface. Without it every Reset would rewrite the back buffer to FP16 and fail again, leaving linear
// values in a buffer nothing was told about. Not set for a missing swapchain, which can differ next time.
static bool hdrColorSpaceImpossible = false;

// The knee: the smoothstep in HdrApplyRatio starts restoration at this fraction of graded white. Live on one path only:
// the exact ratio (hdr_source=1 with no usable LUT profile, or forced with the LUT off), where the rebuilt composite does
// not reproduce the game's blurs and restoration must stay on pixels that clipped (by the brightest channel). The LUT path
// uses the LUT's measured pivot; source 0 needs no knee (its ratio is exactly 1 wherever nothing clipped).
static const float kHdrKnee = 0.90f;

// ---- the levels ----
// Paper white and peak, resolved from the cfg or the OS at engage and on every live cfg change; logged when the pair
// changes. Peak 0: what Windows reports (DXGI MaxLuminance, written by the Windows HDR Calibration app), or 1000 when DXGI
// reports nothing (HGiG's default). Paper white 0: the OS SDR white level, Microsoft's documented default.
static float hdrLoggedPaper = -1.0f, hdrLoggedPeak = -1.0f;
static void HdrResolveLevels()
{
    hdrPaperWhite = ivtCfg.hdr_paper_white > 0 ? (float)ivtCfg.hdr_paper_white : (hdrOsSdrWhite > 0.0f ? hdrOsSdrWhite : 203.0f);
    hdrPeak       = ivtCfg.hdr_peak > 0 ? (float)ivtCfg.hdr_peak : (hdrOsPeak > 0.0f ? hdrOsPeak : 1000.0f);
    if (hdrPaperWhite == hdrLoggedPaper && hdrPeak == hdrLoggedPeak) return;
    hdrLoggedPaper = hdrPaperWhite; hdrLoggedPeak = hdrPeak;
    LogRecord("  hdr: paper white %.0f nits (%s), peak %.0f nits (%s), ceiling %.2fx paper white",
        hdrPaperWhite, ivtCfg.hdr_paper_white > 0 ? "from the cfg" : hdrOsSdrWhite > 0.0f ? "the OS SDR white level" : "fallback 203",
        hdrPeak, ivtCfg.hdr_peak > 0 ? "from the cfg" : hdrOsPeak > 0.0f ? "DXGI MaxLuminance" : "fallback 1000: DXGI reported nothing",
        hdrPeak / hdrPaperWhite);
        // No headroom is no HDR: the shader's only room above white is peak / paper, and at or under 1.0 restoration and the
    // roll-off reduce to a clip at paper white, so say it.
    if (hdrPeak <= hdrPaperWhite * 1.05f)
        LogRecord("  hdr: the peak (%.0f nits) is not above paper white (%.0f nits): no headroom", hdrPeak, hdrPaperWhite);
    if (ivtCfg.hdr_peak <= 0 && hdrOsPeak > 0.0f && hdrOsPeakFull > 0.0f && hdrOsPeakFull < hdrOsPeak * 0.6f)
        LogRecord("  hdr: Windows reports full-frame %.0f, peak %.0f: an EDID-shaped pair (never calibrated?)", hdrOsPeakFull, hdrOsPeak);
}

// ---- engaging ----

// Wine, for the fullscreen gate only. The transport's IvtDetectWine is in a header included later, so this asks ntdll
// itself, once, at the first CreateDevice.
static bool hdrWineGateSaid = false;
static bool HdrOnWine()
{
    static int cached = -1;
    if (cached < 0)
    {
        const HMODULE nt = GetModuleHandleA("ntdll.dll");
        cached = (nt && GetProcAddress(nt, "wine_get_version")) ? 1 : 0;
    }
    return cached == 1;
}

// On Windows DXGI's colour space is the display's HDR state and gates the rewrite (a surface accepts scRGB even with
// Windows HDR off). Under Wine DXGI is DXVK's and reports the session's switch (DXVK_HDR=1 or dxgi.enableHDR), which may
// also be read directly; the surface check at the first present then stands HDR down if the screen cannot show it.
// Checked at compile time: an environment variable can never engage HDR on Windows.
static constexpr bool HdrSessionWantsHdr(bool wine, bool dxgiSaysHdr, bool wineSwitchSet)
{
    return dxgiSaysHdr || (wine && wineSwitchSet);
}
static_assert( HdrSessionWantsHdr(false, true,  false), "Windows: DXGI's HDR engages");
static_assert( HdrSessionWantsHdr(false, true,  true ), "Windows: DXGI's HDR engages, the variable is irrelevant");
static_assert(!HdrSessionWantsHdr(false, false, true ), "Windows: an environment variable never engages");
static_assert(!HdrSessionWantsHdr(false, false, false), "Windows: HDR off stays off");
static_assert( HdrSessionWantsHdr(true,  true,  false), "Wine: DXGI's HDR engages");
static_assert( HdrSessionWantsHdr(true,  false, true ), "Wine: the session's switch engages where DXGI read nothing or said SDR");
static_assert( HdrSessionWantsHdr(true,  true,  true ), "Wine: both agree");
static_assert(!HdrSessionWantsHdr(true,  false, false), "Wine: no switch anywhere, no HDR");

// DXVK_HDR=1, or dxgi.enableHDR inside DXVK_CONFIG, in DXVK's own precedence: an option the config names, true or false,
// wins over the variable, compared without case. A dxvk.conf file is not read here; where it switches HDR on, DXGI's own
// answer carries it. Asked under Wine only, once.
static bool HdrWineSessionSwitch()
{
    static int cached = -1;
    if (cached < 0)
    {
        int named = -1;                             // what DXVK_CONFIG says, when it names the option (the last one wins)
        static char cfg[2048];                      // static: this runs once, and 2 KB is not for a hook's stack
        const DWORD n = GetEnvironmentVariableA("DXVK_CONFIG", cfg, sizeof cfg);
        if (n > 0 && n < sizeof cfg)
        {
            for (char* p = cfg; *p; ++p) *p = (char)tolower((unsigned char)*p);
            for (const char* k = strstr(cfg, "dxgi.enablehdr"); k; k = strstr(k + 1, "dxgi.enablehdr"))
            {
                const char* e = strchr(k, '=');
                const char* semi = strchr(k, ';');
                if (!e || (semi && semi < e)) continue;
                ++e;
                while (*e == ' ' || *e == '\t') ++e;
                if      (strncmp(e, "true", 4) == 0)  named = 1;
                else if (strncmp(e, "false", 5) == 0) named = 0;
            }
        }
        if (named >= 0) cached = named;
        else
        {
            char v[16] = "";
            const DWORD nv = GetEnvironmentVariableA("DXVK_HDR", v, sizeof v);
            cached = (nv > 0 && nv < sizeof v && strcmp(v, "1") == 0) ? 1 : 0;
        }
    }
    return cached == 1;
}
static bool hdrWineSwitchSaid = false;
// The desktop HDR advice for Linux, one text for the stand-down warning and the refusals.
#define DESKTOP_HDR_ADVICE "on a desktop, add DXVK_HDR=1 to the launch options and run it in gamescope --hdr-enabled or through Wine's Wayland driver in an HDR session (PROTON_ENABLE_WAYLAND=1, Mesa 25.1 or newer): X11 and XWayland cannot show HDR"

static bool HdrRewriteFormat(D3DPRESENT_PARAMETERS* pp, const char* where)
{
    if (!ivtCfg.hdr || !pp) return false;
    HdrLogDisplay();                       // idempotent; fills in the display state

    const bool wine       = HdrOnWine();
    const bool wineSwitch = wine && HdrWineSessionSwitch();
    const char* refuse = nullptr;
    if (!HdrSessionWantsHdr(wine, hdrWindowsHdrOn, wineSwitch))   // the Linux rule, above
        refuse = !wine
               ? (hdrDxgiScreens ? "Windows HDR is off. Turn it on in Settings, System, Display, HDR"
                                 // Nothing read on Windows is not "HDR off" (hdrDxgiScreens).
                                 : "no screen could be read through DXGI, so nothing says Windows HDR is on. A dxgi.dll in the"
                                   " game's own folder (ReShade, an old DXVK copy) is the usual cause; DLSS-IV.log says"
                                   " which dxgi.dll answered and where the search stopped")
               : hdrDxgiScreens
                 ? "DXVK reports this screen as SDR. On a Steam Deck, play in Game Mode with HDR switched on in its display"
                   " settings; " DESKTOP_HDR_ADVICE
                 : "no screen was found through DXGI and DXVK_HDR is not set. On a Steam Deck, play in Game Mode with HDR"
                   " switched on in its display settings; " DESKTOP_HDR_ADVICE;
        // Not on DXVK: native D3D9 refuses an FP16 back buffer at CreateDevice with a fatal D3D error in the game. This must be
    // decided before the device exists, so the transport's own DXVK check comes too late.
    else if (hdrColorSpaceImpossible)
                            refuse = "the colour space could not be set earlier in this session, and rewriting the back"
                                     " buffer to FP16 without it is what produces a dark picture. The line above says why";
    else if (hdrUnlockDone && !hdrExtInterfaceOk)
                            refuse = HdrOnWine()
                                   ? "this D3D9 runtime has no ID3D9VkExtInterface: under Proton that is Proton's own DXVK, older than"
                                     " 3.0. Set Graphics API to Vulkan in the game's settings (FusionFix) so the DXVK 3.1.1 shipped"
                                     " with DLSS-IV runs"
                                   : "this D3D9 runtime is not DXVK's (ID3D9VkExtInterface absent); an FP16 back buffer would make"
                                     " the game's own CreateDevice fail. DLSS-IV requires DXVK";
    else if (!pp->Windowed && !HdrOnWine())
                            refuse = "the game is in exclusive fullscreen. Under DXVK that is a legacy display-mode change and the"
                                     " HDMI link drops to SDR whatever the swapchain carries; set Borderless in the graphics options";
        // THE FULLSCREEN GATE IS A WINDOWS FACT. Under Wine there is no legacy display-mode change and no HDMI link to drop:
    // Wine's Wayland driver and Gamescope give DXVK's "fullscreen" a borderless surface, so under Wine the gate is lifted and said.
    if (!pp->Windowed && HdrOnWine() && !refuse && telemetryOn && !hdrWineGateSaid)   // not 'engaging' when another gate refused on the same call
    {
        hdrWineGateSaid = true;
        LogRecord("  hdr: exclusive fullscreen under Wine (a borderless surface there): engaging");
    }
        // Engaged by the session's switch rather than by DXGI (the Linux rule): said once, so a player's log shows which rule let
    // it through.
    if (!refuse && wine && !hdrWindowsHdrOn && telemetryOn && !hdrWineSwitchSaid)
    {
        hdrWineSwitchSaid = true;
        LogRecord("  hdr: %s, but the session's HDR switch is on (DXVK_HDR or dxgi.enableHDR): engaging, the display decides"
                      " at the first frame", hdrDxgiScreens ? "DXVK reports this screen as SDR" : "DXGI read no screen");
    }
    if (refuse)
    {
        hdrEngaged = false;
                // And the colour-space flag with it: hdrColorSpaceDone is set before the attempt, so leaving it through a refusal would
        // tell the divergence check the swapchain had been told scRGB.
        hdrColorSpaceDone = false;
        if (telemetryOn && !hdrRefusedLogged) { hdrRefusedLogged = true; LogRecord("  hdr: NOT engaging at %s, because %s", where, refuse); }
                // Also in the warnings file, which log=0 shows, once per distinct reason (a Reset can change it, e.g. borderless to
        // exclusive fullscreen). The reasons are string literals, so the pointer is the identity.
        static const char* warnedFor = nullptr;
        if (warnedFor != refuse)
        {
            warnedFor = refuse;
            IvtWarn("hdr=1 is set, but HDR is not on (decided at %s): %s", where, refuse);
        }
        return false;
    }

    HdrResolveLevels();                    // paper white and peak; logged when they change
    const D3DFORMAT was = pp->BackBufferFormat;
    pp->BackBufferFormat = D3DFMT_A16B16G16R16F;
    hdrEngaged = true;
    IvtWarnHdrCameOn();                    // takes back an earlier "HDR is not on" warning (the end of DLSS-IV.cpp)
    hdrColorSpaceDone = false;             // a new device or reset needs it again
    hdrCsEngagedFrame = hdrFramesSeen;     // the heartbeat's 600 frames start here (a mid-session Reset re-engages)
    hdrPresentsAtEngage = hdrPresents; hdrEncodesAtEngage = hdrEncodes;
    if (telemetryOn) { hdrRefusedLogged = false; }
        // Once at log=1: a normal start comes through here three times (CreateDevice twice, then a Reset); repeats go to log=2.
    // A refusal is a different line and is never demoted.
    static bool saidEngaging = false;
    (saidEngaging ? LogHealth : LogRecord)("  hdr: engaging at %s. back buffer %u = %s -> A16B16G16R16F, borderless", where, (unsigned)was, HdrFmtName(was));
    saidEngaging = true;
        // The engine rename must have happened: NVIDIA composites a window whose Vulkan engine is exactly "DXVK" as SDR, so zero
    // instances renamed at engage means a dark picture. The count includes the mod's own scan's instance, so zero is sure and
    // nonzero is not proof. Printed by HdrSayNoRename on NVIDIA only. Not under Wine, where the rename is skipped on purpose.
    if (ivtCfg.hdr_engine_name > 0 && hdrInstancesRenamed == 0 && !HdrOnWine())
    { hdrNoRenameCfg = ivtCfg.hdr_engine_name; HdrSayNoRename(); }
    return true;
}

// ---- The colour space ----
// EXTENDED_SRGB_LINEAR is set on DXVK's ID3D9VkExtSwapchain once, after the first present (DXVK creates its presenter lazily
// and the call dereferences it), from the swapchain Present hook, the one moment outside any scene. Stock DXVK stamps the
// back buffer with that colour space and its blitter passes the values through.
static bool hdrCsHeartbeatDone = false;

// DXVK's ID3D9VkExtSwapchain, as far down as this mod calls it.
struct HdrExtSwapchain : public IUnknown {
    virtual BOOL    __stdcall CheckColorSpaceSupport(int) = 0;
    virtual HRESULT __stdcall SetColorSpace(int) = 0;
    virtual HRESULT __stdcall SetHDRMetaData(const VkHdrMetadataEXT*) = 0;
    virtual HRESULT __stdcall GetCurrentOutputDesc(float*) = 0;
    virtual void    __stdcall UnlockAdditionalFormats() = 0;
};
// What the swapchain was last told (hdrEngaged is not that: HdrEncodeEnsure's failure arm clears it after scRGB was set).
// Set where SetColorSpace(scRGB) succeeds, cleared where SetColorSpace(sRGB) succeeds and at CreateDevice (a new swapchain).
// Not cleared at a Reset: DXVK keeps the colour space across one.
static bool hdrCsScRgbSet = false;
// The encode pass could not be built after scRGB was set: the swapchain is told sRGB at the next present. The colour space
// is only touched from the Present hook, outside any scene, so back-buffer pointers borrowed for the frame never go stale.
static bool hdrCsStandDownWanted = false;
static const GUID kHdrIidExtSwapchain = { 0x13776e93, 0x4aa9, 0x430a, { 0xa4, 0xec, 0xfe, 0x9e, 0x28, 0x11, 0x81, 0xd5 } };

// HDR stands down at a Reset, so the swapchain is told sRGB again. DXVK keeps the colour space across a Reset; going from
// borderless HDR to exclusive fullscreen refuses HDR and hands the game's A8R8G8B8 through, which DXVK cannot pair with
// scRGB, so it falls back to an FP16 swapchain carrying 8-bit SDR values and the game freezes on its first frame in
// exclusive fullscreen. A session that starts in exclusive fullscreen never set scRGB and runs.
// Called from hkReset before DXVK's Reset, on the refusal of a session that had engaged; the layer is unarmed by then.
static void HdrColorSpaceStandDown(IDirect3DDevice9* dev, const char* when)
{
    if (!dev) return;
        // The layer is disarmed when hkReset calls this, so the scope is a load and a compare; every road to the extended
    // swapchain takes it (tools/check-dt.py).
    DT_DIRECT();
    IDirect3DSwapChain9* sc = nullptr;
    if (FAILED(dev->GetSwapChain(0, &sc)) || !sc) return;
    HdrExtSwapchain* xsc = nullptr;
    if (SUCCEEDED(sc->QueryInterface(kHdrIidExtSwapchain, (void**)&xsc)) && xsc)
    {
        const HRESULT hr = xsc->SetColorSpace(0);      // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                // A failure keeps the want up so the next present retries; said once per outcome.
        static HRESULT saidHr = (HRESULT)1;
        if (SUCCEEDED(hr)) { hdrCsScRgbSet = false; hdrCsStandDownWanted = false; }
        if (hr != saidHr || SUCCEEDED(hr))
        {
            saidHr = hr;
            LogRecord("  hdr: HDR stands down %s: the swapchain's colour space back to sRGB (0x%08X)", when, (unsigned)hr);
        }
        xsc->Release();
    }
    sc->Release();
}

static void HdrColorSpace(IDirect3DDevice9* dev)
{
    if (!ivtCfg.hdr || hdrColorSpaceDone || !dev || !hdrEngaged) return;
    if (hdrFramesSeen < 1) return;         // the presenter does not exist before the first present
    hdrColorSpaceDone = true;              // one attempt per device, success or not
        // After the one-shot latch, never above it: this is called on every present.
    // DXVK's extended swapchain must see the present it follows (devthread/dt_core.h).
    DT_DIRECT();

    IDirect3DSwapChain9* sc = nullptr;
    if (FAILED(dev->GetSwapChain(0, &sc)) || !sc) { hdrEngaged = false; LogRecord("  hdr: no swapchain; HDR off"); return; }
    HdrExtSwapchain* xsc = nullptr;
    if (FAILED(sc->QueryInterface(kHdrIidExtSwapchain, (void**)&xsc)) || !xsc)
    {
        hdrEngaged = false; hdrColorSpaceImpossible = true;
        LogRecord("  hdr: no ID3D9VkExtSwapchain in this DXVK: the colour space cannot be set, HDR off for the session");
        sc->Release(); return;
    }
    const int kScRgb = 1000104002;         // VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
        // Set true to answer "not offered" without asking, to exercise the stand-down below (the Linux path) on Windows.
    const bool csTest = false;
    if (csTest || !xsc->CheckColorSpaceSupport(kScRgb))
    {
        hdrEngaged = false; hdrColorSpaceImpossible = true;
        LogRecord("  hdr: the surface does not offer scRGB%s: HDR off for the session (the back buffer stays FP16 until the next Reset, SDR picture)",
                  csTest ? " (hdr_cs_test=1 answered, the surface was not asked)" : "");
                // Said at log=0 too: under Wine it answers "why is there no HDR".
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            // Which Wine display driver: X11, and XWayland on a Wayland desktop, cannot show HDR outside gamescope, and desktop Proton
            // runs the game there unless PROTON_ENABLE_WAYLAND=1. gamescope (the Steam Deck's Game Mode among them) also runs the game
            // on the X11 driver and does show HDR, so the X11 sentence names it.
            if (HdrOnWine())
            {
                const bool wl  = GetModuleHandleA("winewayland.drv") != nullptr;
                const bool x11 = GetModuleHandleA("winex11.drv") != nullptr;
                IvtWarn("hdr=1 and the HDR switch are on, but the display does not accept an HDR picture: SDR. %s",
                        x11 && !wl ? "The game runs on Wine's X11 driver, which shows HDR only inside gamescope with HDR on (a Steam"
                                     " Deck's Game Mode with HDR switched on in its display settings, or gamescope --hdr-enabled on a"
                                     " desktop); on a desktop without gamescope add PROTON_ENABLE_WAYLAND=1 to the launch options, in"
                                     " an HDR session with Mesa 25.1 or newer"
                        : wl        ? "The game runs on Wine's Wayland driver, so the desktop session or its graphics driver does"
                                     " not offer HDR to this window: an HDR session on KDE Plasma with Mesa 25.1 or newer does"
                                     " (or gamescope --hdr-enabled)"
                                   : "On a Steam Deck, HDR is off in its display settings or the game is not in Game Mode; " DESKTOP_HDR_ADVICE);
            }
            else
                IvtWarn("hdr=1 is set and Windows HDR is on, but the display surface does not accept an HDR picture (scRGB), so the"
                        " picture stays SDR");
        }
    }
    else
    {
        const HRESULT hr = xsc->SetColorSpace(kScRgb);
        if (FAILED(hr))
        {
            hdrEngaged = false; hdrColorSpaceImpossible = true;
            LogRecord("  hdr: SetColorSpace(scRGB) failed 0x%08X; HDR off for the rest of this session,"
                      " and the back buffer will not be rewritten again", (unsigned)hr);
        }
        else
        {
            hdrCsScRgbSet = true;
            LogRecord("  hdr: SetColorSpace(scRGB) OK at frame %u", hdrFramesSeen);
        }
    }
    xsc->Release();
    sc->Release();
}

// Said once, late, if HDR engaged and the colour space still is not set. Not when HDR was refused: that was said at
// CreateDevice with its reason. Counted from the engage, since a Reset re-engages and the colour space is set only at
// the end of the next Present.
static void HdrColorSpaceHeartbeat()
{
    if (!ivtCfg.hdr || !hdrEngaged || hdrCsHeartbeatDone || hdrColorSpaceDone || hdrFramesSeen - hdrCsEngagedFrame < 600) return;
    hdrCsHeartbeatDone = true;
    LogRecord("  hdr: WARNING: %u frames presented since HDR engaged and the colour space is still not set: plain SDR", hdrFramesSeen - hdrCsEngagedFrame);
    IvtWarn("hdr=1: HDR engaged, but after %u frames the swapchain's colour space was never set, so the picture is plain SDR."
            " This is a fault in the mod, not in your settings. Please send me the DLSS-IV.log file", hdrFramesSeen - hdrCsEngagedFrame);
}

// ---- The pass ----
static IDirect3DPixelShader9* hdrPS     = nullptr;
static IDirect3DPixelShader9* hdrDbgPS  = nullptr;   // created only when asked for
static IDirect3DPixelShader9* hdrG10PS[2] = {};      // G10: the encode that sharpens at its sample, plain [0] and absorbed [1]
static bool                   hdrG10Tried[2] = {};
static bool                   hdrG10Refused = false;  // one was refused: DLSS-IV.cpp's latch stops, the sharpen pass is back
#if DLSSIV_DEV
static LONG                   hdrG10Frames = 0;       // frames the encode sharpened (development line)
#endif
static IDirect3DTexture9*     hdrTex    = nullptr;
static IDirect3DTexture9*     hdrXboxTex = nullptr;   // FusionFix's console gamma table, 256x1 R32F, managed; released before every Reset
static IDirect3DSurface9*     hdrSurf   = nullptr;
// Does a level-0 texture have exactly this shape? Used by both scratch owners to share one target.
static bool HdrEncodeScratchMatchesTex(IDirect3DTexture9* t, UINT w, UINT h, D3DFORMAT f)
{
    D3DSURFACE_DESC d = {};
    return t && SUCCEEDED(t->GetLevelDesc(0, &d)) && d.Width == w && d.Height == h && d.Format == f
        && (d.Usage & D3DUSAGE_RENDERTARGET);
}
static bool HdrEncodeScratchMatches(UINT w, UINT h, D3DFORMAT f) { return HdrEncodeScratchMatchesTex(hdrTex, w, h, f); }
static D3DFORMAT              hdrScratchFmt = D3DFMT_UNKNOWN;
static bool   hdrPassTried   = false;
static bool   hdrDbgTried    = false;
static UINT64 hdrPassFailed  = 0;

static void HdrProfileRelease();
static void HdrCgProbeRelease();

// Declared here because HdrEncodeRelease clears it and HdrEncodeEnsure calls it, both above its definition.
// The lifetime is once per device.
static bool hdrGammaChecked = false;
static void HdrGammaRampCheck(IDirect3DDevice9* dev);

static void HdrEncodeRelease()
{
    HdrSceneDrop();                        // the game's texture first: this runs before Reset
    FGA(FgHudRelease();)                   // the HUD-less picture: the encode's second target and its shaders
    HdrOffscreenForget();                  // FusionFix recreates its targets across a Reset; the pointer is stale
    if (hdrSurf)  { hdrSurf->Release();  hdrSurf  = nullptr; }
    if (hdrTex)   { hdrTex->Release();   hdrTex   = nullptr; }
    HdrCgaRelease();                       // all start over after a Reset
    if (hdrPS)    { hdrPS->Release();    hdrPS    = nullptr; }
    if (hdrDbgPS) { hdrDbgPS->Release(); hdrDbgPS = nullptr; }
    for (int i = 0; i < 2; ++i) { if (hdrG10PS[i]) { hdrG10PS[i]->Release(); hdrG10PS[i] = nullptr; } hdrG10Tried[i] = false; }
    hdrG10Refused = false;                 // tried again on the next device
    if (hdrXboxTex) { hdrXboxTex->Release(); hdrXboxTex = nullptr; }   // managed, but a device teardown must not leak it
    HdrPreHudRelease();
    HdrProfileRelease();
    HdrCgProbeRelease();
    hdrPassTried = false; hdrDbgTried = false;
        // Re-arm the gamma ramp check: a Reset restores the driver's default ramp, and HDR needs identity (DXVK's present LUT
    // clamps at 1.0 and flattens everything above white). Once per device is the lifetime, and this is where a device goes.
    hdrGammaChecked = false;
}

static bool HdrEncodeEnsure(IDirect3DDevice9* dev)
{
    if (hdrPS && hdrTex) return true;
    if (hdrPassTried) return false;
    hdrPassTried = true;
    if (!rsOutW || !rsOutH) { hdrPassTried = false; return false; }   // retry once the size is known
    RsOwnCreateCall guard;            // the mod's own: never scaled, never tabled

    bool ok = IvtEnsureFsQuad(dev);
    if (!ok) LogRecord("  hdr: the full-screen quad was refused");
    if (ok) ok = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrEncodePS, &hdrPS)) && hdrPS;
        // The scratch matches the back buffer as it is, so the copy out is a plain blit.
    D3DFORMAT bbFmt = D3DFMT_A16B16G16R16F;
    if (backbufferSurf) { D3DSURFACE_DESC bd = {}; if (SUCCEEDED(backbufferSurf->GetDesc(&bd))) bbFmt = bd.Format; }
    hdrScratchFmt = bbFmt;
        // One scratch for two passes (see ShpEnsure): if sharpen's target matches, reference it.
    if (ok && shpTex && shpSurf && HdrEncodeScratchMatchesTex(shpTex, rsOutW, rsOutH, bbFmt))
    {
        hdrTex = shpTex;  hdrTex->AddRef();
        hdrSurf = shpSurf; hdrSurf->AddRef();
    }
    else
    {
        if (ok) ok = SUCCEEDED(dev->CreateTexture(rsOutW, rsOutH, 1, D3DUSAGE_RENDERTARGET, bbFmt, D3DPOOL_DEFAULT, &hdrTex, nullptr)) && hdrTex;
        if (ok) ok = SUCCEEDED(hdrTex->GetSurfaceLevel(0, &hdrSurf)) && hdrSurf;
    }
        // The console gamma table, filled once a device.
    if (ok && !hdrXboxTex)
    {
        if (SUCCEEDED(dev->CreateTexture(256, 1, 1, 0, D3DFMT_R32F, D3DPOOL_MANAGED, &hdrXboxTex, nullptr)) && hdrXboxTex)
        {
            D3DLOCKED_RECT lr = {};
            if (SUCCEEDED(hdrXboxTex->LockRect(0, &lr, nullptr, 0))) { memcpy(lr.pBits, kHdrXboxGamma, sizeof kHdrXboxGamma); hdrXboxTex->UnlockRect(0); }
            else { hdrXboxTex->Release(); hdrXboxTex = nullptr; }
        }
        if (!hdrXboxTex) LogRecord("  hdr: the console gamma table could not be created: rebuild without it");
    }
    if (!ok)
    {
        HdrEncodeRelease();
        hdrPassTried = true;
        hdrEngaged = false;           // no half-applied state: without the pass the format rewrite must not stand alone
        hdrCsStandDownWanted = true;  // nor the colour space: stood down at the next present, outside the scene
        LogRecord("  hdr: the %ux%u encode pass could not be created: HDR off", rsOutW, rsOutH);
        return false;
    }
    LogRecord("  hdr: encode pass ready at %ux%u, scratch %s, restoration %s, roll-off %s",
        rsOutW, rsOutH, HdrFmtName(hdrScratchFmt),
        ivtCfg.hdr_restore > 0 ? "ON" : "off",
        ivtCfg.hdr_rolloff > 0 ? "ON" : "off (hard clip)");
        // The gamma ramp, once per device: a Reset comes back through here and restores the driver's default ramp.
    // HdrEncodeRelease clears the latch on the way down.
    HdrGammaRampCheck(dev);
    return true;
}

// The gamma ramp, read rather than watched. A set ramp goes through a LUT in DXVK's present shader whose coordinate clamps
// at 1.0, and a ramp means nothing on an HDR signal, so identity is forced while HDR is on (as RenoDX and Special K do).
// hdrGammaChecked is cleared by HdrEncodeRelease on every device teardown, so this runs again after a Reset.
static void HdrGammaRampCheck(IDirect3DDevice9* dev)
{
    if (hdrGammaChecked || !dev) return;
    hdrGammaChecked = true;
    D3DGAMMARAMP r = {};
    dev->GetGammaRamp(0, &r);
        // The comparison is the log line's alone. GetGammaRamp stays in both builds on purpose: dt_device_sync.h makes it a drain
    // of the device thread, so removing it from the release would change more than a log line.
#if DLSSIV_DEVLOG
    bool identity = true;
    for (int i = 0; i < 256 && identity; ++i)
    {
        const WORD want = (WORD)(i * 257);
        if (r.red[i] != want || r.green[i] != want || r.blue[i] != want) identity = false;
    }
#endif
    D3DGAMMARAMP id = {};
    for (int i = 0; i < 256; ++i) id.red[i] = id.green[i] = id.blue[i] = (WORD)(i * 257);
    dev->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &id);
    LogHealth("  hdr: gamma ramp was %s; identity applied for HDR", identity ? "identity" : "NOT identity");
}

// FusionFix's console gamma: 0 none, 1 the Xbox 360 curve, 2 the PS3 curve (FusionFix's ConsoleGamma setting). Sources, in order:
//   1. hdr_console_gamma 0, 1 or 2, forced by the player;
//   2. the absorb's proof (hdr_cga.h), when FusionFix's pass drew with the proved shader this frame: exact;
//   3. the probe below (hdrCgDetected), an estimate from the rebuilt composite; the Xbox 360 curve until it decides.
// The two curves are close, so the probe separates them only on a clean frame. The Xbox 360 curve needs its table on s9.
static int  hdrCgDetected = 1;
static bool hdrCgDecided  = false;
// The proof teaches the probe: a proved curve becomes the probe's answer too, and the probe stands down for kHdrCgTaughtFor
// frames after the last proof, so frames without the proof do not alternate between two curves and re-profile the LUT.
static const UINT kHdrCgTaughtFor = 1200;       // frames: twenty seconds at 60 fps
static int  hdrCgStreak = 0, hdrCgCandidate = -1;
static bool hdrCgDisagree = false;         // the last probe disagreed with the answer: probe again soon
static bool hdrCgTaught   = false;
static UINT hdrCgTaughtAt = 0;
static bool HdrCgTaughtLately() { return hdrCgTaught && hdrFramesSeen - hdrCgTaughtAt < kHdrCgTaughtFor; }
static const char* HdrConsoleGammaName(int m) { return m == 2 ? "the PS3 curve" : m == 1 ? "the Xbox 360 curve" : "none"; }
static int  HdrConsoleGammaMode()
{
    int m, from;
    if (ivtCfg.hdr_console_gamma >= 0) { m = ivtCfg.hdr_console_gamma; from = 0; }
    else if (hdrCgaCurve > 0)
    {
        m = hdrCgaCurve; from = 1;
        hdrCgDetected = m; hdrCgDecided = true; hdrCgTaught = true; hdrCgTaughtAt = hdrFramesSeen;
        hdrCgStreak = 0; hdrCgCandidate = -1; hdrCgDisagree = false;   // a half-flip from before the proof does not survive it
    }
    else                               { m = hdrCgDetected;            from = 2; }
    if (m == 1 && !hdrXboxTex) m = 0;
        // Said on every change of the answer or of its source; proof and probe alternating on the same curve is not said.
    static int saidM = -1, saidFrom = -1;
    if (m != saidM || (from != saidFrom && !(from >= 1 && saidFrom >= 1)))
    {
        saidM = m; saidFrom = from;
        LogRecord("  hdr: console gamma in the HDR model: %s (%s)", HdrConsoleGammaName(m),
            from == 0 ? "hdr_console_gamma" : from == 1 ? "proved from FusionFix's shader" : hdrCgDecided ? "the probe's estimate" : "FusionFix's default until measured");
    }
    return m;
}
static bool  hdrLutProfileOk = false;         // filled by HdrLutProfileAnalyse below
static float hdrLutPivot = 0.0f, hdrLutSlope = 0.0f;
// The restoration source, in one function, since the console-gamma probe needs the same answer as HdrFillConstants.
// Auto: the frame itself while FusionFix's tone mapping is off (the composite draws into the FP16 back buffer, which does
// not clip); the rebuilt composite while it is on (its LUT compressed the frame) and while the composite draws into an
// off-screen 8-bit target (FusionFix's post AA, hdrCompositeOffscreen), which clipped it. A source whose inputs are
// missing this frame falls back to the frame itself.
static int HdrResolveSource()
{
    int source = ivtCfg.hdr_source;
    if (source < 0) source = (hdrGame[3][3] > 0.5f || hdrCompositeOffscreen) ? 1 : 0;
    if (source == 1 && !(hdrSceneOk && hdrSceneTex && hdrGameOk && hdrLumTex)) source = 0;
    return source;
}

// The constants every HDR pass reads (layout in shaders/hdr_common.hlsli), from one function so no pass is fed different numbers.
static ULONGLONG hdrT0 = 0;
// Do values above white in the frame reach the display? Written by the encode, read by the sharpener (ShpCeiling,
// DLSS-IV.cpp), which runs earlier in the frame. True only on source 0 with hdr_restore above 0. False until the first
// encode, and always with hdr=0.
static bool hdrAboveWhiteShown = false;
static void HdrFillConstants(float c[9][4], float view)
{
    if (!hdrT0) hdrT0 = GetTickCount64();
        // Restoration is on whenever the cfg asks and a capture exists, for the shipping pass and the debug views alike.
    // The source of the lost luminance: gHdr3.w in shaders/hdr_common.hlsli.
    const int source = HdrResolveSource();
    const bool restore = ivtCfg.hdr_restore > 0;
    hdrAboveWhiteShown = restore && source == 0;
#if DLSSIV_DEVLOG                   // the latch and its log line: a release build carries neither
    static int saidSource = -9;
    if (restore && source != saidSource)
    {
        saidSource = source;
        LogHealth("  hdr: restoration source: %s%s%s",
            source == 0 ? "the frame's own values above white (the FP16 back buffer does not clip)"
                        : "the composite rebuilt from its own inputs (GtaComposite)",
            ivtCfg.hdr_source < 0 ? "   (auto)" : "",
            (source == 1 && hdrCompositeOffscreen && hdrGame[3][3] <= 0.5f) ? ", because the composite draws off-screen into 8 bits" : "");
    }
#endif
    static bool saidFfNote = false;
    if (hdrGame[3][3] > 0.5f && !saidFfNote)
    {
        saidFfNote = true;
        LogRecord("  hdr: FusionFix tone mapping ON: restored through its LUT");
    }
    c[0][0] = hdrPaperWhite;                                                    // SDR white becomes this many nits
    c[0][1] = hdrPeak;                                                          // the display's ceiling
    c[0][2] = ivtCfg.hdr_eotf == 1 ? 0.0f : (ivtCfg.hdr_eotf == 2 ? 2.4f : 2.2f);   // 0 = sRGB piecewise, else exponent
    c[0][3] = restore ? (float)ivtCfg.hdr_restore * 0.01f : 0.0f;
    c[1][0] = kHdrKnee;                                                         // the exact-ratio path's knee; the LUT path uses its pivot instead
        // The UI level in units of paper white, or 0 when there is no pre-HUD copy for this frame (hdr_ui = -1, HUD off, copy
    // failed). 0 in the cfg means paper white, which still withdraws the scene's restoration from HUD pixels.
    // The stamp, not just the flag: HdrEncode's early returns precede the per-frame reset, which would leave hdrPreHudValid
    // set and compare frame N+1 against frame N's pre-UI image.
    // Negative while a game menu is up: the whole frame is interface, so the coverage is 1 everywhere. The pause menu is a
    // translucent layer, and differencing it prints the scene's outlines into the mask. GameMenuUp is false for good on a
    // build where the flag was not found.
    const float uiLevel = ivtCfg.hdr_ui > 0 ? (float)ivtCfg.hdr_ui / max(hdrPaperWhite, 1e-3f) : 1.0f;
    c[1][1] = ivtCfg.hdr_ui < 0 ? 0.0f
            : GameMenuUp() ? -uiLevel
            : (hdrPreHudValid && hdrPreHudFrame == hdrFramesSeen) ? uiLevel : 0.0f;
    c[1][2] = 1.0f / (float)rsOutW;                                             // UV from VPOS
    c[1][3] = 1.0f / (float)rsOutH;
    c[2][0] = (float)ivtCfg.hdr_rolloff * 0.01f;                                // 0 = hard clip at the ceiling
    c[2][1] = view;
    c[2][2] = (float)((double)(GetTickCount64() - hdrT0) * 0.001);              // seconds, for the cycling patterns
    c[2][3] = (float)source;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) c[3 + i][j] = hdrGame[i][j];   // gGame0..3
    c[8][0] = (float)HdrConsoleGammaMode();                                 // gFlags: which console gamma is in the frame, 0 none, 1 Xbox 360, 2 PS3
    c[8][1] = kHdrXboxGammaTopSlope;
    c[8][2] = hdrLutProfileOk ? hdrLutPivot : 0.0f;                        // the LUT path: 0 = no profile, use the exact ratio
    c[8][3] = hdrLutProfileOk ? hdrLutSlope : 0.0f;
}

// Samplers 2 to 4 and 9 to 11 for the HDR passes: the adapted luminance, the bloom, the LUT, the console gamma table, the
// LUT profile and the pre-HUD frame. IvtSetupPass and IvtSavedState cover 0 to 2 only. Anything not captured is stood in
// for by the frame copy, so no sampler is read unbound. Saved and restored in full.
struct HdrExtraBind { IDirect3DBaseTexture9* tex[6]; DWORD st[6][6]; };
static const DWORD kHdrExtraSlot[6] = { 2, 3, 4, 9, 10, 11 };
static IDirect3DTexture9* hdrProfTex = nullptr;   // defined with the profile pass below
static const D3DSAMPLERSTATETYPE kHdrExtraSamp[6] = { D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE };
static void HdrBindExtra(IDirect3DDevice9* dev, HdrExtraBind* o)
{
    IDirect3DBaseTexture9* want[6] = { hdrLumTex ? (IDirect3DBaseTexture9*)hdrLumTex : (IDirect3DBaseTexture9*)hdrTex,
                                       hdrBloomTex ? (IDirect3DBaseTexture9*)hdrBloomTex : (IDirect3DBaseTexture9*)hdrTex,
                                       hdrLutTex ? (IDirect3DBaseTexture9*)hdrLutTex : (IDirect3DBaseTexture9*)hdrTex,
                                       hdrXboxTex ? (IDirect3DBaseTexture9*)hdrXboxTex : (IDirect3DBaseTexture9*)hdrTex,
                                       hdrProfTex ? (IDirect3DBaseTexture9*)hdrProfTex : (IDirect3DBaseTexture9*)hdrTex,
                                       hdrPreHudTex ? (IDirect3DBaseTexture9*)hdrPreHudTex : (IDirect3DBaseTexture9*)hdrTex };
    const DWORD filt[6] = { D3DTEXF_POINT, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT };
    for (int k = 0; k < 6; ++k)
    {
        const DWORD s = kHdrExtraSlot[k];
        o->tex[k] = nullptr; dev->GetTexture(s, &o->tex[k]);
        for (int j = 0; j < 6; ++j) dev->GetSamplerState(s, kHdrExtraSamp[j], &o->st[k][j]);
        dev->SetTexture(s, want[k]);
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, filt[k]); dev->SetSamplerState(s, D3DSAMP_MINFILTER, filt[k]);
        dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
}
static void HdrUnbindExtra(IDirect3DDevice9* dev, HdrExtraBind* o)
{
    for (int k = 5; k >= 0; --k)
    {
        const DWORD s = kHdrExtraSlot[k];
        dev->SetTexture(s, o->tex[k]); if (o->tex[k]) o->tex[k]->Release();
        for (int j = 0; j < 6; ++j) dev->SetSamplerState(s, kHdrExtraSamp[j], o->st[k][j]);
    }
}


// ---- The LUT profile ----
// For the LUT path (source 1 with FusionFix's tone mapping on). Once per LUT texture and console-gamma setting, a 256x1 pass
// samples the LUT's grey axis through the same lookup and console gamma the rebuild uses (shaders/hdr_lutprofile_ps.hlsl),
// read back three frames later (never a stall), and the CPU finds where the curve's slope starts falling. From that pivot the
// encode continues the LUT's own curve straight to the roll-off at the display peak, so the LUT's look stands exact below it.
// hdr_lut_pivot (percent of white) overrides the measured pivot. Nothing here runs unless the LUT path is in use.
static IDirect3DPixelShader9* hdrProfPS   = nullptr;
static IDirect3DSurface9*     hdrProfSurf = nullptr;
static IDirect3DSurface9*     hdrProfSys  = nullptr;
static bool   hdrProfTried = false, hdrProfPending = false;
static UINT   hdrProfIssued = 0;
static void*  hdrProfKeyLut = nullptr;
static int    hdrProfKeyXg = -1;
// The decode is part of the key: the profile measures T(x) through HdrDecode(sdr, gHdr.z), and gHdr.z is hdr_eotf, a live key.
static int    hdrProfKeyEotf = -1;

static void HdrProfileRelease()
{
    if (hdrProfSys)  { hdrProfSys->Release();  hdrProfSys  = nullptr; }
    if (hdrProfSurf) { hdrProfSurf->Release(); hdrProfSurf = nullptr; }
    if (hdrProfTex)  { hdrProfTex->Release();  hdrProfTex  = nullptr; }
    if (hdrProfPS)   { hdrProfPS->Release();   hdrProfPS   = nullptr; }
    hdrProfTried = false; hdrProfPending = false; hdrProfKeyLut = nullptr; hdrProfKeyXg = -1; hdrProfKeyEotf = -1;
    hdrLutProfileOk = false;
}

// The pivot finder. The profile is T(x): the LUT's grey-axis output luminance against linear input x in white units. Below the
// pivot the LUT stands untouched; above it the encode continues the LUT's straight line with the slope it had there. So the
// pivot is where the straight part ends: lower regrades the midtones, higher leaves the shoulder compressed.
// The log-log slope d(log T)/d(log x) is the local power-law exponent: above 1 a toe, 1 faithful up to a gain, below 1
// compressing. So the threshold is absolute (0.85), not relative to a measured plateau (which a LUT with a toe misreads).
// The black lift is removed first (a pedestal flattens the bottom and is not compression), and the pivot is the last upcrossing
// of the threshold (a non-monotonic LUT dips and recovers, and a dip is not a shoulder). Guards:
//   low     start where T >= 0.01 and x >= 0.01 (below that the readback's quantisation is noise);
//   high    run past a hard clip, so a LUT that saturates gets its pivot at the clip;
//   floor   never below the input the LUT maps to middle grey (that would be a regrade); said when it acts.
// The slope is median filtered over five samples and the fall must hold for three, so no single noisy texel moves the answer.
static void HdrLutProfileAnalyse(const float* px)   // 256 pairs (T, x)
{
    float T[256], X[256];
    for (int i = 0; i < 256; ++i) { T[i] = px[2 * i]; X[i] = px[2 * i + 1]; }

        // White, for the report: the first x where T reaches 98 percent of its max.
    float Tmax = 0.0f; for (int i = 0; i < 256; ++i) if (T[i] > Tmax) Tmax = T[i];
    int iWhite = 255; for (int i = 0; i < 256; ++i) if (T[i] >= 0.98f * Tmax) { iWhite = i; break; }
    const float white = X[iWhite];

    int i0 = 1; while (i0 < 250 && (X[i0] < 0.01f || T[i0] < 0.01f)) ++i0;      // low guard
    int i1 = 253; while (i1 > i0 && T[i1] >= 0.999f * Tmax) --i1;               // high guard
    i1 = min(i1 + 6, 253);
    if (i1 <= i0 + 4)
    {
        hdrLutProfileOk = false;
        LogHealth("  hdr: LUT profile: only %d usable samples (T reaches 0.01 at x %.3f, stops moving at %.2fx);"
            " the LUT path falls back to the exact ratio with the knee", i1 - i0, X[i0], white);
        return;
    }

        // The black lift (T(0) > 0 on a faded or film-look LUT), removed before the exponent is taken.
    float lift = T[1];
    for (int i = 1; i <= i0 && i < 256; ++i) if (T[i] < lift) lift = T[i];
    if (lift < 0.004f) lift = 0.0f;                     // quantisation, not a pedestal

        // The log-log slope of (T - lift), central differences on the measured spacing (PQ-uniform in x), then a five-sample median.
    float s[256] = {}, sm[256] = {};
    for (int i = 1; i < 255; ++i)
    {
        const float u1 = T[i + 1] - lift, u0 = T[i - 1] - lift;
        if (u1 > 1e-6f && u0 > 1e-6f && X[i + 1] > X[i - 1])
            s[i] = (logf(u1) - logf(u0)) / (logf(X[i + 1]) - logf(X[i - 1]));
    }
    s[0] = s[1]; s[255] = s[254];
    for (int i = 0; i < 256; ++i)
    {
        float w[5]; int n = 0;
        for (int k = -2; k <= 2; ++k) { const int j = i + k; if (j >= 0 && j < 256) w[n++] = s[j]; }
        for (int aa = 1; aa < n; ++aa) { const float v = w[aa]; int bb = aa - 1; while (bb >= 0 && w[bb] > v) { w[bb + 1] = w[bb]; --bb; } w[bb + 1] = v; }
        sm[i] = w[n / 2];
    }

        // The pivot: the last upcrossing of 0.85, after which the exponent never comes back.
    int ipRaw = i0;
    for (int i = i0; i <= i1; ++i) if (sm[i] >= 0.85f) ipRaw = i + 1;
    if (ipRaw > i1 - 2)
    {
        hdrLutProfileOk = false;
        LogHealth("  hdr: LUT profile: no shoulder (the exponent never settles below 0.85 between x %.3f and %.2fx, so this"
            " LUT does not compress); the LUT path falls back to the exact ratio with the knee", X[i0], X[i1]);
        return;
    }

    int ip = ipRaw; bool floored = false;
    int iMid = -1; for (int i = i0; i <= i1 && i < 256; ++i) if (T[i] >= 0.18f) { iMid = i; break; }
    if (iMid >= 0 && ip < iMid) { ip = iMid; floored = true; }

        // The slope the extension continues is the linear dT/dx at the pivot, since HdrLutRatio adds H = Tp + slope * (x - xp).
    float ls[256] = {};
    for (int i = 1; i < 255; ++i) { const float dx = X[i + 1] - X[i - 1]; ls[i] = dx > 1e-9f ? (T[i + 1] - T[i - 1]) / dx : 0.0f; }
    float w[5]; int n = 0;
    for (int k = -2; k <= 2; ++k) { const int j = ip + k; if (j >= 1 && j < 255) w[n++] = ls[j]; }
    for (int aa = 1; aa < n; ++aa) { const float v = w[aa]; int bb = aa - 1; while (bb >= 0 && w[bb] > v) { w[bb + 1] = w[bb]; --bb; } w[bb + 1] = v; }
    const float slope = n ? w[n / 2] : ls[ip];

    hdrLutPivot = X[ip]; hdrLutSlope = slope;
    hdrLutProfileOk = slope > 0.0f;
    if (!hdrLutProfileOk)
    {
        LogHealth("  hdr: LUT profile: the slope at the pivot is not positive (%.4f at %.3fx); the LUT path falls back to the exact ratio with the knee", slope, X[ip]);
        return;
    }
    LogHealth("  hdr: LUT profile (FusionFix tone mapping ON, console gamma %s): the curve stops being faithful at %.3fx white"
        " (exponent %.2f there, T %.3f, dT/dx %.3f, black lift %.3f)%s, white at %.2fx. The LUT stands below %.3f,"
        " bit-exact, and its own straight line continues above it.",
        HdrConsoleGammaName(hdrProfKeyXg), X[ip], sm[ip], T[ip], slope, lift,   // the curve the profile was keyed on: pure, as a LogHealth argument must be
        floored ? ", RAISED to middle grey because the geometric answer was below it" : ", measured",
        white, X[ip]);
}

static void HdrLutProfileTick(IDirect3DDevice9* dev)
{
        // Harvest a pending readback, three frames after it was issued.
    if (hdrProfPending && hdrFramesSeen - hdrProfIssued >= 3)
    {
        D3DLOCKED_RECT lr = {};
        if (SUCCEEDED(hdrProfSys->LockRect(&lr, nullptr, D3DLOCK_READONLY)))
        {
            HdrLutProfileAnalyse((const float*)lr.pBits);
            hdrProfSys->UnlockRect();
        }
        hdrProfPending = false;
    }
        // Wanted only on the LUT path.
    const bool lutPath = hdrLutTex && hdrFfToneMap && (ivtCfg.hdr_source == 1 || ivtCfg.hdr_source < 0);
    if (!lutPath) { if (hdrLutProfileOk && !hdrLutTex) hdrLutProfileOk = false; return; }
    const int xg = HdrConsoleGammaMode();   // which curve, 0 to 2: a switch between curves re-profiles too
    if ((void*)hdrLutTex == hdrProfKeyLut && xg == hdrProfKeyXg && ivtCfg.hdr_eotf == hdrProfKeyEotf) return;
    if (hdrProfPending) return;
    if (hdrProfTried && !hdrProfPS) return;
    if (!hdrProfPS)
    {
        hdrProfTried = true;
        RsOwnCreateCall guard;
        bool ok = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrLutProfilePS, &hdrProfPS)) && hdrProfPS;
        if (ok) ok = SUCCEEDED(dev->CreateTexture(256, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G32R32F, D3DPOOL_DEFAULT, &hdrProfTex, nullptr)) && hdrProfTex
                  && SUCCEEDED(hdrProfTex->GetSurfaceLevel(0, &hdrProfSurf)) && hdrProfSurf;
        if (ok) ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 1, D3DFMT_G32R32F, D3DPOOL_SYSTEMMEM, &hdrProfSys, nullptr)) && hdrProfSys;
        if (!ok) { HdrProfileRelease(); hdrProfTried = true; LogHealth("  hdr: the LUT profile pass could not be created; the LUT path uses the exact ratio with the knee"); return; }
    }
    hdrProfKeyLut = hdrLutTex; hdrProfKeyXg = xg; hdrProfKeyEotf = ivtCfg.hdr_eotf;
    hdrLutProfileOk = false;                    // until the new one is analysed, the exact ratio stands in
    RsOwnCall rsOwnGuard;
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    HdrFillConstants(c, 0.0f);
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
    HdrExtraBind xb;
    HdrBindExtra(dev, &xb);
    dev->SetRenderTarget(0, hdrProfSurf);
    const D3DVIEWPORT9 vp = { 0, 0, 256, 1, 0.0f, 1.0f };
    dev->SetViewport(&vp);
    dev->SetPixelShader(hdrProfPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));
    HdrUnbindExtra(dev, &xb);
    ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
    if (SUCCEEDED(dev->GetRenderTargetData(hdrProfSurf, hdrProfSys))) { hdrProfPending = true; hdrProfIssued = hdrFramesSeen; }
    else LogHealth("  hdr: the LUT profile readback could not be issued");
}

// ---- The console gamma probe ----
// Is FusionFix's console gamma in the frame, and which curve? The setting is not in FusionFix's ini, so it is measured: every
// kHdrCgEvery frames a 256x1 pass compares the frame with the composite rebuilt with no curve, the Xbox 360 curve and the PS3
// curve (shaders/hdr_cgprobe_ps.hlsl), read back three frames later. The best fit wins with a margin (runner-up at least 1.5x
// worse) and two agreeing probes, so one odd frame cannot flip it. Off when hdr_console_gamma is forced or nothing reads the
// answer (HdrCgProbeTick). Cost when it runs: one 256-pixel draw and a 4 KB readback.
static const UINT kHdrCgEvery = 600;
static IDirect3DPixelShader9* hdrCgPS   = nullptr;
static IDirect3DTexture9*     hdrCgTex  = nullptr;
static IDirect3DSurface9*     hdrCgSurf = nullptr;
static IDirect3DSurface9*     hdrCgSys  = nullptr;
static bool hdrCgTried = false, hdrCgPending = false;
static UINT hdrCgIssued = 0, hdrCgLast = 0;
// The model must fit before its answer means anything: the margin test is relative and names a winner however wrong both
// models are. 0.5 sits in a wide gap between the error of every correct decision seen and that of a bad flip.
// Refusing is the cheap direction: with no decision the probe keeps what it has (FusionFix's default until the first answer).
static const double kHdrCgFit = 0.5;
DEVSTAT(static int  hdrCgSaid = -1;)                // 0 fits but undecided, 1 does not fit, 2 a curve but not which: logged on change only (development log)
DEVSTAT(static UINT64 hdrCgProbes = 0;)

static void HdrCgProbeRelease()
{
    if (hdrCgSys)  { hdrCgSys->Release();  hdrCgSys  = nullptr; }
    if (hdrCgSurf) { hdrCgSurf->Release(); hdrCgSurf = nullptr; }
    if (hdrCgTex)  { hdrCgTex->Release();  hdrCgTex  = nullptr; }
    if (hdrCgPS)   { hdrCgPS->Release();   hdrCgPS   = nullptr; }
    hdrCgTried = false; hdrCgPending = false;
}

// Mid-range samples accumulated across probes, one sum of log errors per hypothesis: [0] no curve, [1] the Xbox 360 curve,
// [2] the PS3 curve (the probe's B, G and A channels).
static double hdrCgAcc[3] = { 0.0, 0.0, 0.0 }; static int hdrCgAccN = 0;
DEVSTAT(static int    hdrCgStarved = 0;)                                                  // consecutive probes that added almost nothing

// The three-way decision: the best must fit (kHdrCgFit), win by the margin, and a change needs two agreeing decisions.
// The two curves are much closer to each other than either is to none, so a frame can rule "none" out long before it can
// tell the curves apart. When "none" loses to the better curve but the curves tie: a decided curve stands, a decided none
// moves to the closer curve through the usual two decisions, and before the first decision nothing is decided.
static void HdrCgProbeAnalyse(const float* px)   // 256 texels of (real, Xbox 360, none, PS3)
{
        // The proof spoke lately (HdrConsoleGammaMode): its answer is exact, so a probe already in flight is dropped with its samples.
    if (HdrCgTaughtLately()) { hdrCgAcc[0] = hdrCgAcc[1] = hdrCgAcc[2] = 0.0; hdrCgAccN = 0; return; }
    int added = 0;
    for (int i = 0; i < 256; ++i)
    {
        const float real = px[4 * i], xb = px[4 * i + 1], off = px[4 * i + 2], ps3 = px[4 * i + 3];
        if (real < 0.1f || real > 0.95f || xb < 0.01f || off < 0.01f || ps3 < 0.01f) continue;
        hdrCgAcc[0] += fabs(log((double)real / off));
        hdrCgAcc[1] += fabs(log((double)real / xb));
        hdrCgAcc[2] += fabs(log((double)real / ps3));
        hdrCgAccN++; added++;
    }
    if (added < 4)
    {
                // A dark or empty frame: said once per streak, so a detector that cannot see is not mistaken for one that has decided.
        HdrCgSayUndecided(0, nullptr, hdrCgAccN);             // a release's log too, until the first decision
        DEVSTAT(if (++hdrCgStarved == 5)
            LogHealth("  hdr: console gamma probe: too dark to judge for five probes in a row (%d mid-range samples so far); keeping %s until a brighter scene",
                hdrCgAccN, HdrConsoleGammaName(hdrCgDetected));)
        return;
    }
    DEVSTAT(hdrCgStarved = 0;)
    if (hdrCgAccN < 32) return;                           // not enough yet: the next probe adds to it
    const double e[3] = { hdrCgAcc[0] / hdrCgAccN, hdrCgAcc[1] / hdrCgAccN, hdrCgAcc[2] / hdrCgAccN };
    const int n = hdrCgAccN;
    hdrCgAcc[0] = hdrCgAcc[1] = hdrCgAcc[2] = 0.0; hdrCgAccN = 0;
    int best = 0;
    for (int k = 1; k < 3; ++k) if (e[k] < e[best]) best = k;
    double second = 1e30;
    for (int k = 0; k < 3; ++k) if (k != best && e[k] < second) second = e[k];
    if (e[best] >= kHdrCgFit)
    {
                // No model describes this frame: the best of three bad fits is not an answer.
        HdrCgSayUndecided(1, e, n);
        DEVSTAT(if (hdrCgSaid != 1)
        {
            hdrCgSaid = 1;
            LogHealth("  hdr: console gamma probe: NO assumption fits this frame (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d samples,"
                " and %.2f is the most any may be off by); not deciding from it, keeping %s", e[0], e[1], e[2], n, kHdrCgFit, HdrConsoleGammaName(hdrCgDetected));
        })
        return;
    }
    int cand;
    if (second >= 1.5 * e[best]) cand = best;
    else if (best != 0 && e[0] >= 1.5 * e[best])
    {
                // The curves tie and none is ruled out: see the rule above.
        if (!hdrCgDecided || hdrCgDetected != 0)
        {
            HdrCgSayUndecided(2, e, n);
            DEVSTAT(if (hdrCgSaid != 2)
            {
                hdrCgSaid = 2;
                LogHealth("  hdr: console gamma probe: a curve is on, but this frame cannot tell which (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f,"
                    " %d samples); keeping %s", e[0], e[1], e[2], n, HdrConsoleGammaName(hdrCgDetected));
            })
            if (hdrCgDecided) { hdrCgStreak = 0; hdrCgCandidate = -1; hdrCgDisagree = false; }   // it agrees with the current curve being on
            return;
        }
        cand = best;                                      // decided none: the closer curve, through the two-decision rule below
    }
    else
    {
        HdrCgSayUndecided(3, e, n);
        DEVSTAT(if (hdrCgSaid != 0)
        {
            hdrCgSaid = 0;
            LogHealth("  hdr: console gamma probe: no clear winner (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d samples); keeping %s",
                e[0], e[1], e[2], n, HdrConsoleGammaName(hdrCgDetected));
        })
        return;
    }
    DEVSTAT(hdrCgSaid = -1;)                                       // it fits and it decided: the next indecision is news again
    if (!hdrCgDecided)
    {
        hdrCgDecided = true; hdrCgDetected = cand; hdrCgStreak = 0; hdrCgCandidate = -1; hdrCgDisagree = false;
        LogRecord("  hdr: console gamma detected: %s (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d mid-range samples)",
            HdrConsoleGammaName(cand), e[0], e[1], e[2], n);
        return;
    }
    if (cand == hdrCgDetected) { hdrCgStreak = 0; hdrCgCandidate = -1; hdrCgDisagree = false; return; }
    hdrCgDisagree = true;
    if (hdrCgCandidate == cand) hdrCgStreak++; else { hdrCgCandidate = cand; hdrCgStreak = 1; }
        // A flip needs two agreeing decisions: the first is said too.
    if (hdrCgStreak == 1)
        LogHealth("  hdr: console gamma probe DISAGREES: %s fits better (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d samples);"
            " one more agreeing measurement switches it", HdrConsoleGammaName(cand), e[0], e[1], e[2], n);
    if (hdrCgStreak >= 2)
    {
        hdrCgDetected = cand; hdrCgStreak = 0; hdrCgCandidate = -1; hdrCgDisagree = false;
        LogRecord("  hdr: console gamma is now %s (mean log error: none %.3f, Xbox 360 %.3f, PS3 %.3f, %d samples)",
            HdrConsoleGammaName(cand), e[0], e[1], e[2], n);
    }
}

// Called from HdrEncode after the frame is copied out and before the encode draw, so s0 holds this frame's SDR picture.
static void HdrCgProbeTick(IDirect3DDevice9* dev)
{
    if (ivtCfg.hdr_console_gamma >= 0) return;            // forced: nothing to measure
    if (hdrCgPending && hdrFramesSeen - hdrCgIssued >= 3)
    {
        D3DLOCKED_RECT lr = {};
        if (SUCCEEDED(hdrCgSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { HdrCgProbeAnalyse((const float*)lr.pBits); hdrCgSys->UnlockRect(); }
        hdrCgPending = false;
    }
    if (hdrCgPending) return;
        // Off while the absorb's proof is speaking: its answer is exact and has taught hdrCgDetected. Unless the absorb says the proved
    // curve stopped being drawn (switched in FusionFix's menu, or off): then the window ends now.
    if (hdrCgaTeachEnd) { hdrCgaTeachEnd = false; hdrCgTaught = false; }
    if (HdrCgTaughtLately()) return;
        // Off when nothing reads the answer. Below the harvest, so a probe in flight is always collected.
    // The answer reaches the picture only through gFlags.x (HdrFillConstants, c[8][0]), read by ConsoleGamma in
    // shaders/hdr_common.hlsli. Readers: the rebuilt composite (source 1); the UI mask (hdr_ui >= 0), which applies the curve to
    // the pre-HUD copy before differencing, since the copy is taken before FusionFix's console gamma pass; and the debug views.
    // The LUT profile needs no clause: its path implies source 1.
    // Everything the probe owns is created lazily below this gate; the release stays in HdrApplyLiveCfg on the static key,
    // because a predicate that can change per frame must not drive create and destroy.
    if (!(HdrResolveSource() == 1 || ivtCfg.hdr_ui >= 0 || IVT_HDR_VIEW != 0)) return;
    if (!(hdrSceneOk && hdrSceneTex && hdrGameOk && hdrLumTex && hdrXboxTex)) return;
    const UINT every = (hdrCgDecided && !hdrCgDisagree && hdrCgAccN == 0) ? kHdrCgEvery : 120;   // quick until the first answer, while accumulating and while confirming a change
    if (hdrFramesSeen - hdrCgLast < every) return;
    hdrCgLast = hdrFramesSeen;
    if (hdrCgTried && !hdrCgPS) return;
    if (!hdrCgPS)
    {
        hdrCgTried = true;
        RsOwnCreateCall guard;
        bool ok = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrCgProbePS, &hdrCgPS)) && hdrCgPS;
        if (ok) ok = SUCCEEDED(dev->CreateTexture(256, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &hdrCgTex, nullptr)) && hdrCgTex
                  && SUCCEEDED(hdrCgTex->GetSurfaceLevel(0, &hdrCgSurf)) && hdrCgSurf;
        if (ok) ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 1, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &hdrCgSys, nullptr)) && hdrCgSys;
        if (!ok) { HdrCgProbeRelease(); hdrCgTried = true; LogRecord("  hdr: the console gamma probe could not be created: Xbox 360 assumed"); return; }
    }
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    HdrFillConstants(c, 0.0f);
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
    DWORD oldSrgb0 = 0, oldSrgb1 = 0; IDirect3DBaseTexture9* oldTex1 = nullptr;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb0); dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->GetTexture(1, &oldTex1); dev->GetSamplerState(1, D3DSAMP_SRGBTEXTURE, &oldSrgb1);
    dev->SetTexture(1, hdrSceneTex); dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
    HdrExtraBind xb;
    HdrBindExtra(dev, &xb);
    dev->SetRenderTarget(0, hdrCgSurf);
    const D3DVIEWPORT9 vp = { 0, 0, 256, 1, 0.0f, 1.0f };
    dev->SetViewport(&vp);
    dev->SetTexture(0, hdrTex);
    dev->SetPixelShader(hdrCgPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));
    HdrUnbindExtra(dev, &xb);
    dev->SetTexture(1, oldTex1); if (oldTex1) oldTex1->Release();
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, oldSrgb1);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb0);
    ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
    if (SUCCEEDED(dev->GetRenderTargetData(hdrCgSurf, hdrCgSys))) { hdrCgPending = true; hdrCgIssued = hdrFramesSeen; DEVSTAT(if (telemetryOn && hdrCgProbes++ == 0) LogHealth("  hdr: console gamma probe issued at frame %u (hdr_console_gamma=-1); first answer in a few frames", hdrFramesSeen);) }
}

#include "hdr_cga.h"                     // the absorb: FusionFix's console gamma pass folded into the encode

// the armed product's alone (fg/fg_hud.h, the encode's second target)
FGA(static void FgHudSecond(IDirect3DDevice9* dev, IDirect3DPixelShader9* ps, bool held, bool g10, bool view);)
static void HdrEncode(IDirect3DDevice9* dev)
{
        // Every way out before the tail drops the game's borrowed textures: HdrCaptureScene holds one reference at a time.
    // The absorb (hdr_cga.h): when FusionFix's console gamma draw was withheld, the frame is in FusionFix's copy (cgaTex) without
    // the curve, and the encode samples it through the curve. A frame the encode cannot run for must have the curve drawn first.
    const bool held = hdrCgaHeld && cgaTex && cgaEncPS;
    if (!ivtCfg.hdr || !hdrEngaged || !hdrColorSpaceDone) { HdrCgaHeal(dev, CGA_HEAL_NOENCODE); if (hdrSceneTex) HdrSceneDrop(); return; }
    if (!dev || !backbufferSurf || !HdrEncodeEnsure(dev)) { HdrCgaHeal(dev, CGA_HEAL_NOENCODE); if (hdrSceneTex) HdrSceneDrop(); return; }
    if (!held) HdrCgaHeal(dev, CGA_HEAL_NOENCODE);   // withheld, yet not absorbable after all: the draw is owed
    RsOwnCall rsOwnGuard;             // the mod's own binds are not the game's state

    HdrLutProfileTick(dev);           // once per LUT; nothing otherwise
    if (!held && FAILED(dev->StretchRect(backbufferSurf, nullptr, hdrSurf, nullptr, D3DTEXF_NONE)))
    {
        if (telemetryOn && hdrPassFailed++ < 5) LogRecord("  hdr: the copy out of the back buffer failed (this frame unchanged)");
        HdrSceneDrop();
        return;
    }
        // The debug shader exists only while a view is asked for.
    IDirect3DPixelShader9* ps = hdrPS;
    float view = 0.0f;
    if (IVT_HDR_VIEW)
    {
        if (!hdrDbgPS && !hdrDbgTried)
        {
            hdrDbgTried = true;
            // LogRecord: a development build only; a release clamps hdr_view to 0 and its test here is a constant.
            if (FAILED(dev->CreatePixelShader((const DWORD*)DEVSTAT(kHdrDebugPS) RELSTAT(nullptr), &hdrDbgPS))) { hdrDbgPS = nullptr; LogRecord("  hdr: the debug shader was refused: hdr_view off"); }
        }
        if (hdrDbgPS) { ps = hdrDbgPS; view = (float)IVT_HDR_VIEW; }
    }

    if (held) ps = cgaEncPS;          // the same encode with the curve at its one sample; never under a view
        // G10: on a frame DLSS-IV.cpp latched for it (shpG10) the sharpen pass did not run and the encode that sharpens at its own
    // sample draws instead. One that cannot be created is said once and stops the latch (hdrG10Refused); that frame goes unsharpened.
    bool g10 = false;
    if (shpG10 && shpG10Seam && (ps == hdrPS || ps == cgaEncPS))   // a frame without a seam is not sharpened in either arm
    {
        const int k = (ps == cgaEncPS) ? 1 : 0;
        if (!hdrG10PS[k] && !hdrG10Tried[k])
        {
            hdrG10Tried[k] = true;
            if (FAILED(dev->CreatePixelShader((const DWORD*)(k ? kHdrEncodeCgaG10PS : kHdrEncodeG10PS), &hdrG10PS[k]))) hdrG10PS[k] = nullptr;
            LogRecord("  hdr: the sharpening encode (%s) %s", k ? "absorbed" : "plain", hdrG10PS[k] ? "created" : "was REFUSED: the sharpen pass stays");
            if (!hdrG10PS[k]) hdrG10Refused = true;
        }
        if (hdrG10PS[k]) { ps = hdrG10PS[k]; g10 = true; }
    }
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    HdrFillConstants(c, view);
    if (held) memcpy(c[7], cgaConst, sizeof(cgaConst));   // c7 is the absorb's alone: HdrFillConstants leaves it zero
        // G10's constants, in the gHdr3 lanes only the debug views read: y the ceiling by ShpCeiling's rule (the frame is FP16 here),
    // z AMD's attenuation from the cfg's sharpness (ShpEnsure may never run under G10). See hdr_encode_ps.hlsl, HDR_G10.
    if (g10)
    {
        const float lin = 6.0f;
        c[2][1] = !hdrAboveWhiteShown ? 1.0f : ivtCfg.hdr_eotf == 1 ? 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f
                                                                   : powf(lin, 1.0f / (ivtCfg.hdr_eotf == 2 ? 2.4f : 2.2f));
        c[2][2] = powf(2.0f, -(1.0f - ivtCfg.sharpness) * 2.0f);
#if DLSSIV_DEV
        hdrG10Frames++;
#endif
    }
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
        // Sampler sRGB conversion is not in the shared save set; left on by the game, reads would come back linearised.
    DWORD oldSrgb0 = 0, oldSrgb1 = 0; IDirect3DBaseTexture9* oldTex1 = nullptr;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb0);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        // Sampler 1 (the pre-tonemap image, or the frame copy as a stand-in) is always bound: the debug shader fetches it unconditionally.
    dev->GetTexture(1, &oldTex1);
    dev->GetSamplerState(1, D3DSAMP_SRGBTEXTURE, &oldSrgb1);
    dev->SetTexture(1, (hdrSceneOk && hdrSceneTex) ? (IDirect3DBaseTexture9*)hdrSceneTex : (IDirect3DBaseTexture9*)hdrTex);
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);   // IvtSetupPass already set point and clamp on samplers 0..2

    HdrExtraBind xb;
    HdrBindExtra(dev, &xb);   // s2 luminance, s3 bloom, s4 LUT

    const D3DVIEWPORT9 vp = { 0, 0, rsOutW, rsOutH, 0.0f, 1.0f };
    dev->SetRenderTarget(0, backbufferSurf);
    dev->SetViewport(&vp);
    dev->SetTexture(0, held ? (IDirect3DBaseTexture9*)cgaTex : (IDirect3DBaseTexture9*)hdrTex);
    dev->SetPixelShader(ps);
        // The armed product's HUD-less picture (fg/fg_hud.h, FgHudSecond): with frame generation engaged and an interface on the frame,
    // the same encode with the HUD-less picture as a second output takes this shader's place. The back buffer is unchanged.
    FGA(FgHudSecond(dev, ps, held, g10, view != 0.0f);)
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));

    HdrUnbindExtra(dev, &xb);
    dev->SetTexture(1, oldTex1); if (oldTex1) oldTex1->Release();
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, oldSrgb1);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb0);
    ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
    hdrEncodes++;     // this frame really was encoded
    HdrCgaAfterEncode(dev, held);          // the absorb's frame is closed, a due compare is issued, a finished one is read
        // The HUD-less picture (fg/fg_hud.h): what this encode says of its frame, for the Present, and the second target's life.
    // Before the scene's textures are dropped below. Not a frame under a debug view.
    FGA(FgHudCapture(dev, view == 0.0f ? ps : nullptr, held ? (IDirect3DBaseTexture9*)cgaTex : (IDirect3DBaseTexture9*)hdrTex, c, held, g10);)
        // Outside the encode, so its pass and readback are not charged to it; the probe saves and restores its own state.
    // It reads hdrTex, which an absorbed frame never fills, so with a proved absorb it runs only on the frames the absorb leaves
    // alone and its quick cadence slows to 600 frames. Tolerable because hdrCgDetected starts at FusionFix's default.
    if (!held) HdrCgProbeTick(dev);
    HdrSceneDrop();                        // the frame is done with the game's texture

        // The pre-HUD copy, decided for the next frame by what this one did. The draws after the composite are counted whether or not
    // a copy was taken. With the HUD off nothing draws after the composite and the copy is not armed; it re-arms by itself.
    {
        static int saidHud = -1;
        const int drawing = hdrPreHudDraws > 0 ? 1 : 0;
        if (drawing != saidHud)
        {
            saidHud = drawing;
            LogHealth("  hdr: the game's 2D pass %s (%u draw(s) at and after the interface boundary)."
                " The UI level and its restoration guard %s.",
                drawing ? "is drawing over the frame" : "drew nothing this frame", hdrPreHudDraws,
                drawing ? "apply" : "do nothing, and no copy of the frame is taken");
        }
        hdrPreHudArmed   = (ivtCfg.hdr_ui >= 0 FGA(|| FgHudWantsCopy())) && drawing;   // an engaged frame generation needs the copy at hdr_ui = -1 too
        hdrPreHudValid   = false; hdrPreHudFrame = 0xFFFFFFFFu;
        hdrPreHudPending = hdrPreHudArmed;   // one copy next frame, at its boundary
                // hdrPreHudDraws is zeroed in the EndScene hook, so the early returns above cannot leave a count standing.
    }
}

// Live cfg: what has to follow a change of the live keys (DlssIvTransport.h, IvtCfgLiveTick), on the render thread.
// Turning a feature off live gives its memory back. Safe here: every consumer stands in the frame copy for a missing
// texture (HdrBindExtra's want[]), so releasing cannot leave a sampler unbound.
static void HdrApplyLiveCfg()
{
    if (hdrEngaged) HdrResolveLevels();          // paper white and peak
    hdrDbgTried = false;     // a view may be tried again


        // The UI copy: the largest allocation in the HDR path.
    if (ivtCfg.hdr_ui < 0 FGA(&& !FgHudWantsCopy()) && hdrPreHudTex)   // not while frame generation is using the copy
    {
        HdrPreHudRelease();
        hdrPreHudArmed = false;
        LogRecord("  hdr: UI brightness off (hdr_ui=-1), its copy released");
    }

        // The debug views (6, 14 and 17): a shader no shipping frame selects. hdrDbgTried is cleared above, so a later view rebuilds it.
    if (IVT_HDR_VIEW == 0 && hdrDbgPS)
    {
        hdrDbgPS->Release(); hdrDbgPS = nullptr;
        LogHealth("  hdr: views off; the debug shader is released");
    }

        // The console-gamma probe: not needed once the answer is forced.
    if (ivtCfg.hdr_console_gamma >= 0 && hdrCgPS)
    {
        HdrCgProbeRelease();
        LogRecord("  hdr: console gamma forced to %s: the probe released",
            HdrConsoleGammaName(ivtCfg.hdr_console_gamma));
    }
}
