// hdr.h: HDR state, the display, and the pre-tonemap capture.
// Deliberately absent, so nobody puts it back:
// no exclusive fullscreen: under DXVK a D3D9 fullscreen swapchain performs a legacy display-mode change and the HDMI link
// drops to SDR. HDR is borderless, and HdrRewriteFormat refuses the rest.
// no HDR10 / PQ output: a windowed HDR10 Vulkan swapchain never reaches the screen here; scRGB FP16 does, and DWM composes
// it natively. The encode emits linear scRGB, nits / 80, BT.709 primaries.
// The NVIDIA driver presents a window as SDR when the Vulkan engine string is exactly "DXVK", so DXVK's instance is renamed
// (DLSS-IV.cpp, HdrInstallEngineNameHook: on Windows when hdr_engine_name > 0, with HDR on, or in SDR with rename_sdr=1,
// the default, on an NVIDIA primary display or a hybrid where NVIDIA renders; never under Wine; decided at DXVK's first
// instance by HdrRenameDecide).

#pragma once

static bool  hdrDisplayLogged = false;
static bool  hdrWindowsHdrOn  = false;
// Screens whose description the last DXGI walk read. Zero means "nothing was read", not "HDR is off": HdrRewriteFormat
// refuses the two in different words and, under Wine, by a different rule.
static UINT  hdrDxgiScreens   = 0;
// The back buffer really is A16B16G16R16F, recorded where it is (re)acquired: the EndScene hook when its pointer changes,
// and a successful Reset. FusionFix's SMAA fix in hkDrawPrim needs this, not hdrEngaged: HDR stands down at the first
// present when the surface refuses scRGB, but the FP16 buffer stays until the next Reset, and an SMAA frame would land
// linearised in a buffer presented as sRGB.
static bool hdrBbFp16 = false;   // not on the hot line: hkDrawPrim tests hdrSrgbTex0 (which is) first
static float hdrOsSdrWhite    = 0.0f;    // nits, from the OS; the paper-white default
static bool  hdrEngaged       = false;   // the single authority: false means the mod behaves as it always did
static bool  hdrRefusedLogged = false;
// FusionFix SMAA under the FP16 back buffer: see hkDrawPrim in DLSS-IV.cpp.
DLSSIV_HOT static bool hdrSrgbTex0 = false;   // sampler 0 has D3DSAMP_SRGBTEXTURE set (the game's own writes, kept by hkSetSamplerState)
static bool  hdrSrgbSaid      = false;   // the suppression's one line has been written
static float hdrPaperWhite    = 0.0f;    // nits, resolved at engage
// Counts hkEndScene calls, not presents (the name is historical). The HDR encode is driven from EndScene alone, so a
// presented frame with no EndScene behind it goes out with raw SDR values in a buffer the swapchain calls linear scRGB:
// dark, and dark text vanishes.
static UINT  hdrFramesSeen    = 0;       // hkEndScene calls; the colour space cannot be set before the first
// Presents, counted separately so the EndScene-per-present assumption can be checked.
static UINT  hdrPresents      = 0;
static UINT  hdrEncodes       = 0;       // frames the encode actually ran on
static float hdrOsPeak        = 0.0f;    // nits, DXGI MaxLuminance of the HDR output; the Windows HDR Calibration app writes it
static float hdrOsPeakFull    = 0.0f;    // nits, DXGI MaxFullFrameLuminance
static float hdrPeak          = 0.0f;    // nits, resolved with paper white (hdr_encode.h, HdrResolveLevels)
// The composite's registers are hard-coded below (Exposure c66.x, ToneMapParams c81, deSatContrastGamma c82, ColorCorrect
// c83, ColorShift c84, FusionFix c217.z and c217.w). A game patch or a FusionFix update can move them silently; if so,
// read the shaders' constant tables again.

static const char* HdrFmtName(D3DFORMAT f)
{
    switch ((unsigned)f)
    {
    case 21:  return "A8R8G8B8";
    case 22:  return "X8R8G8B8";
    case 35:  return "A2R10G10B10";
    case 111: return "R16F";
    case 112: return "G16R16F";
    case 113: return "A16B16G16R16F";
    case 114: return "R32F";
    case 115: return "G32R32F";
    case 116: return "A32B32G32R32F";
    case 50:  return "L8";
    case 81:  return "L16";
    default:  return "?";
    }
}

// ---- the display ----
// The game's own window, set at CreateDevice and at every Reset. Without it the display walk would take whichever HDR
// output it saw last, on any adapter, and could engage HDR on a second monitor that is not the game's.
static HWND hdrGameWnd = nullptr;
// The monitor those numbers describe when the game's own was matched; null when the fallback answered. hkSwapPresent
// compares it with the window's monitor every 600 presents and says once when the game has moved.
static HMONITOR hdrGameMon = nullptr;
// vkCreateInstance calls whose engine name the hdr_engine_name hook rewrote.
// Counted in DLSS-IV.cpp's HdrCreateInstance; declared here, before the encode
// header, so HdrRewriteFormat can say when HDR engages with none renamed.
static int hdrInstancesRenamed = 0;
// The no-rename warning is NVIDIA's alone: only NVIDIA composites a window whose engine is exactly "DXVK" as SDR. The vendor
// is not known when HdrRewriteFormat first runs (the D3D9 adapter is DXVK's spoof here, hideNvidiaGpu; the Vulkan device
// is read later in IvtEnsureVulkan), so HdrRewriteFormat records that it saw no rename, IvtEnsureVulkan records the
// vendor, and whichever comes second prints, once. A session where HDR can engage always reaches the vendor read.
static int   hdrNoRenameCfg = 0;   // hdr_engine_name when HdrRewriteFormat saw none renamed; 0 = not seen
static DWORD hdrVkVendor    = 0;   // the Vulkan vendor ID, from IvtEnsureVulkan; 0 = not read yet
static void HdrSayNoRename()
{
    static bool said = false;
    if (said || hdrNoRenameCfg == 0 || hdrVkVendor != 0x10DE) return;
    said = true;
    LogRecord("  hdr: engaging, but NO vkCreateInstance was renamed (hdr_engine_name=%d)", hdrNoRenameCfg);
}

// DXGI is reached by name rather than by linking, so the .asi gains no new
// import and still starts on a machine where the call is unavailable.
static void HdrLogDisplay()
{
    if (hdrDisplayLogged) return;
    hdrDisplayLogged = true;
    hdrGameMon = nullptr;
    hdrDxgiScreens = 0;                    // before any early return below: a walk that cannot start reads nothing too

        // Which monitor the game is actually on: MONITORFROMWINDOW rather than the primary, because they differ exactly when it matters.
    HMONITOR gameMon = hdrGameWnd ? MonitorFromWindow(hdrGameWnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    WCHAR gameDev[32] = {};
    bool  gameMonFound = false, otherHdr = false;
        // Fallback: the old any-display answer. If matching the game's monitor fails for any reason, the result must be what it
    // was before, not "no HDR display". Applied after the walk only.
    bool  anyHdr = false;
    float anyPeak = 0.0f, anyPeakFull = 0.0f;

    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    typedef HRESULT (WINAPI *PFN_CreateFactory1)(REFIID, void**);
    PFN_CreateFactory1 mk = dxgi ? (PFN_CreateFactory1)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    if (!mk) { LogRecord("  hdr: dxgi.dll unavailable: the display state is not read"); return; }

    struct DxgiOutputDesc1Min {
        WCHAR DeviceName[32]; RECT DesktopCoordinates; BOOL AttachedToDesktop; UINT Rotation; void* Monitor;
        UINT BitsPerColor; UINT ColorSpace; float RedPrimary[2], GreenPrimary[2], BluePrimary[2], WhitePoint[2];
        float MinLuminance, MaxLuminance, MaxFullFrameLuminance;
    };
    struct IDXGIOutput6Min : public IUnknown {
        virtual HRESULT __stdcall SetPrivateData(REFGUID, UINT, const void*) = 0;
        virtual HRESULT __stdcall SetPrivateDataInterface(REFGUID, const IUnknown*) = 0;
        virtual HRESULT __stdcall GetPrivateData(REFGUID, UINT*, void*) = 0;
        virtual HRESULT __stdcall GetParent(REFIID, void**) = 0;
        virtual HRESULT __stdcall GetDesc(void*) = 0;
        virtual HRESULT __stdcall GetDisplayModeList(UINT, UINT, UINT*, void*) = 0;
        virtual HRESULT __stdcall FindClosestMatchingMode(const void*, void*, IUnknown*) = 0;
        virtual HRESULT __stdcall WaitForVBlank() = 0;
        virtual HRESULT __stdcall TakeOwnership(IUnknown*, BOOL) = 0;
        virtual void    __stdcall ReleaseOwnership() = 0;
        virtual HRESULT __stdcall GetGammaControlCapabilities(void*) = 0;
        virtual HRESULT __stdcall SetGammaControl(const void*) = 0;
        virtual HRESULT __stdcall GetGammaControl(void*) = 0;
        virtual HRESULT __stdcall SetDisplaySurface(IUnknown*) = 0;
        virtual HRESULT __stdcall GetDisplaySurfaceData(IUnknown*) = 0;
        virtual HRESULT __stdcall GetFrameStatistics(void*) = 0;
        virtual HRESULT __stdcall GetDisplayModeList1(UINT, UINT, UINT*, void*) = 0;
        virtual HRESULT __stdcall FindClosestMatchingMode1(const void*, void*, IUnknown*) = 0;
        virtual HRESULT __stdcall GetDisplaySurfaceData1(IUnknown*) = 0;
        virtual HRESULT __stdcall DuplicateOutput(IUnknown*, void**) = 0;
        virtual BOOL    __stdcall SupportsOverlays() = 0;
        virtual HRESULT __stdcall CheckOverlaySupport(UINT, IUnknown*, UINT*) = 0;
        virtual HRESULT __stdcall CheckOverlayColorSpaceSupport(UINT, UINT, IUnknown*, UINT*) = 0;
        virtual HRESULT __stdcall DuplicateOutput1(IUnknown*, UINT, UINT, const UINT*, void**) = 0;
        virtual HRESULT __stdcall GetDesc1(DxgiOutputDesc1Min*) = 0;
        virtual HRESULT __stdcall CheckHardwareCompositionSupport(UINT*) = 0;
    };
    struct IDXGIAdapterMin : public IUnknown {
        virtual HRESULT __stdcall SetPrivateData(REFGUID, UINT, const void*) = 0;
        virtual HRESULT __stdcall SetPrivateDataInterface(REFGUID, const IUnknown*) = 0;
        virtual HRESULT __stdcall GetPrivateData(REFGUID, UINT*, void*) = 0;
        virtual HRESULT __stdcall GetParent(REFIID, void**) = 0;
        virtual HRESULT __stdcall EnumOutputs(UINT, IUnknown**) = 0;
    };
    struct IDXGIFactory1Min : public IUnknown {
        virtual HRESULT __stdcall SetPrivateData(REFGUID, UINT, const void*) = 0;
        virtual HRESULT __stdcall SetPrivateDataInterface(REFGUID, const IUnknown*) = 0;
        virtual HRESULT __stdcall GetPrivateData(REFGUID, UINT*, void*) = 0;
        virtual HRESULT __stdcall GetParent(REFIID, void**) = 0;
        virtual HRESULT __stdcall EnumAdapters(UINT, IUnknown**) = 0;
        virtual HRESULT __stdcall MakeWindowAssociation(HWND, UINT) = 0;
        virtual HRESULT __stdcall GetWindowAssociation(HWND*) = 0;
        virtual HRESULT __stdcall CreateSwapChain(IUnknown*, void*, void**) = 0;
        virtual HRESULT __stdcall CreateSoftwareAdapter(HMODULE, IUnknown**) = 0;
        virtual HRESULT __stdcall EnumAdapters1(UINT, IDXGIAdapterMin**) = 0;
        virtual BOOL    __stdcall IsCurrent() = 0;
    };
    const GUID iidFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    const GUID iidOutput6  = { 0x068346e8, 0xaaec, 0x4b84, { 0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1 } };

        // Which dxgi.dll answers: LoadLibrary by name searches the game's folder before the system's, on Windows and under Wine,
    // so a stray dxgi.dll there (ReShade, an old DXVK copy) answers every question below for the whole process.
    char dxgiPath[MAX_PATH] = "", exePath[MAX_PATH] = "";
    GetModuleFileNameA(dxgi, dxgiPath, MAX_PATH);
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    bool dxgiInGameDir = false;
    {
        const char* ds = strrchr(dxgiPath, '\\'); const char* es = strrchr(exePath, '\\');
        if (ds && es && (ds - dxgiPath) == (es - exePath) && _strnicmp(dxgiPath, exePath, (size_t)(ds - dxgiPath)) == 0) dxgiInGameDir = true;
    }

        // The walk, counted: every step before a screen's description can fail silently, and a walk that read nothing must say so
    // rather than blame Windows HDR.
    UINT nAd = 0, nOut = 0, nNo6 = 0, nDescFail = 0;
    HRESULT descHr = S_OK;
        // What ended each list: the EnumAdapters1 result that stopped the walk, and the first EnumOutputs that stopped on an
    // adapter with no screen. DXGI_ERROR_NOT_FOUND (0x887A0002) is an empty list; anything else is a failure.
    HRESULT adEndHr = S_OK, outEndHr = S_OK;
    hdrDxgiScreens = 0;
    IDXGIFactory1Min* factory = nullptr;
    if (FAILED(mk(iidFactory1, (void**)&factory)) || !factory) { LogRecord("  hdr: no DXGI factory (dxgi.dll is %s)", dxgiPath); return; }
    for (UINT ai = 0; ; ++ai)
    {
        IDXGIAdapterMin* ad = nullptr;
        const HRESULT ah = factory->EnumAdapters1(ai, &ad);
        if (ah != S_OK || !ad) { adEndHr = ah; break; }
        nAd++;
        for (UINT oi = 0; ; ++oi)
        {
            IUnknown* out = nullptr;
            const HRESULT oh = ad->EnumOutputs(oi, &out);
            if (oh != S_OK || !out) { if (oi == 0 && outEndHr == S_OK) outEndHr = oh; break; }
            nOut++;
            IDXGIOutput6Min* o6 = nullptr;
            if (!(SUCCEEDED(out->QueryInterface(iidOutput6, (void**)&o6)) && o6)) nNo6++;
            else
            {
                DxgiOutputDesc1Min d = {};
                const HRESULT dh = o6->GetDesc1(&d);
                if (FAILED(dh)) { nDescFail++; descHr = dh; }
                else
                {
                    hdrDxgiScreens++;
                    // 12 is DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: Windows HDR on.
                    // This walk runs at CreateDevice and at every Reset: a reading already said goes to log=2; a changed one stays at log=1.
                    static DWORD saidSig[8]; static int saidN = 0;
                    DWORD sig = 2166136261u;
                    for (int ci = 0; ci < 32 && d.DeviceName[ci]; ci++) sig = (sig ^ (DWORD)d.DeviceName[ci]) * 16777619u;
                    const DWORD mix[6] = { (DWORD)(d.DesktopCoordinates.right - d.DesktopCoordinates.left), (DWORD)(d.DesktopCoordinates.bottom - d.DesktopCoordinates.top),
                                           (DWORD)d.BitsPerColor, (DWORD)d.ColorSpace, (DWORD)(d.MaxLuminance * 16.0f), (DWORD)(d.MaxFullFrameLuminance * 16.0f) };
                    for (int mi = 0; mi < 6; mi++) sig = (sig ^ mix[mi]) * 16777619u;
                    bool saidBefore = false;
                    for (int si = 0; si < saidN; si++) if (saidSig[si] == sig) saidBefore = true;
                    if (!saidBefore && saidN < 8) saidSig[saidN++] = sig;
                    (saidBefore ? LogHealth : LogRecord)("  hdr: display %ls %ldx%ld, %u bpc, colour space %u -> Windows HDR is %s; luminance min %.4f max %.1f max full frame %.1f nits",
                        d.DeviceName, d.DesktopCoordinates.right - d.DesktopCoordinates.left,
                        d.DesktopCoordinates.bottom - d.DesktopCoordinates.top, d.BitsPerColor, d.ColorSpace,
                        d.ColorSpace == 12 ? "ON" : "OFF", d.MinLuminance, d.MaxLuminance, d.MaxFullFrameLuminance);
                    // The game's monitor decides. When the game's window cannot be resolved the any-display fallback stands, and the line
                    // below says which happened.
                    const bool isGameMon = (gameMon && d.Monitor == (void*)gameMon);
                    if (d.ColorSpace == 12)
                    {
                        anyHdr = true; anyPeak = d.MaxLuminance; anyPeakFull = d.MaxFullFrameLuminance;
                        if (!isGameMon) otherHdr = true;
                    }
                    if (isGameMon)
                    {
                        gameMonFound = true;
                        hdrGameMon = gameMon;
                        wcsncpy_s(gameDev, d.DeviceName, _TRUNCATE);
                        hdrWindowsHdrOn = (d.ColorSpace == 12);
                        if (d.ColorSpace == 12) { hdrOsPeak = d.MaxLuminance; hdrOsPeakFull = d.MaxFullFrameLuminance; }
                    }
                }
                o6->Release();
            }
            out->Release();
        }
        ad->Release();
    }
    factory->Release();

        // The fallback, only if the game's own output was never found: the last HDR output wins.
    if (!gameMonFound && anyHdr) { hdrWindowsHdrOn = true; hdrOsPeak = anyPeak; hdrOsPeakFull = anyPeakFull; }

        // Nothing read is its own fact, said with where the walk stopped and which dxgi.dll answered. A dxgi.dll from the game's
    // folder is said even when screens were read. Once at log=1 per distinct reading, as above.
    if (hdrDxgiScreens == 0)
    {
        static DWORD saidNone = 0;
        const DWORD sig = ((DWORD)nAd * 16777619u) ^ ((DWORD)nOut * 2166136261u) ^ ((DWORD)nNo6 << 8) ^ ((DWORD)nDescFail << 16)
                        ^ (DWORD)adEndHr ^ ((DWORD)outEndHr * 31u) ^ (dxgiInGameDir ? 0x80000000u : 0u) ^ 1u;
        char descTxt[64] = "";
        if (nDescFail) _snprintf_s(descTxt, sizeof(descTxt), _TRUNCATE, " (the last answered 0x%08X)", (unsigned)descHr);
        (saidNone == sig ? LogHealth : LogRecord)("  hdr: DXGI read NO screen: %u adapter(s) (end 0x%08X), %u screen(s) (first 0x%08X), %u without IDXGIOutput6, %u description failed%s; dxgi.dll is %s%s", nAd, (unsigned)adEndHr, nOut, (unsigned)outEndHr,
                  nNo6, nDescFail, descTxt, dxgiPath,
                  dxgiInGameDir ? ", IN THE GAME'S OWN FOLDER (ReShade, an old DXVK?)" : "");
        saidNone = sig;
    }
    else if (dxgiInGameDir)
    {
        static bool saidDir = false;
        (saidDir ? LogHealth : LogRecord)("  hdr: the dxgi.dll that answered is in the game's own folder (%s)", dxgiPath);
        saidDir = true;
    }

        // Say which display the numbers came from: on two monitors the user needs to know.
    if (gameMonFound)
        LogHealth("  hdr: the game is on display %ls, and that is the one these numbers describe", gameDev);
    else if (hdrDxgiScreens == 0)
        ;                                  // said above, in its own words
    else if (hdrGameWnd)
        LogRecord("  hdr: the game's window is on a display DXGI did not enumerate: the figures above are the last HDR display found");
    else
        LogRecord("  hdr: the game's window is not known yet: the figures above are the last HDR display found");
    if (gameMonFound && !hdrWindowsHdrOn && otherHdr)
        LogRecord("  hdr: WINDOWS HDR IS OFF ON THE GAME'S DISPLAY %ls (ON for another): HDR will not engage", gameDev);

        // The OS SDR white level is the paper-white default: a brightness the user already chose.
    UINT32 nPath = 0, nMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) == ERROR_SUCCESS && nPath && nMode)
    {
        DISPLAYCONFIG_PATH_INFO* paths = (DISPLAYCONFIG_PATH_INFO*)calloc(nPath, sizeof(DISPLAYCONFIG_PATH_INFO));
        DISPLAYCONFIG_MODE_INFO* modes = (DISPLAYCONFIG_MODE_INFO*)calloc(nMode, sizeof(DISPLAYCONFIG_MODE_INFO));
        if (paths && modes && QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths, &nMode, modes, nullptr) == ERROR_SUCCESS)
        {
            for (UINT32 i = 0; i < nPath; ++i)
            {
                DISPLAYCONFIG_SDR_WHITE_LEVEL w = {};
                w.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
                w.header.size = sizeof(w);
                w.header.adapterId = paths[i].targetInfo.adapterId;
                w.header.id = paths[i].targetInfo.id;
                // Matched to the game's panel by name, so on two monitors the default comes from the game's.
                if (gameDev[0])
                {
                    DISPLAYCONFIG_SOURCE_DEVICE_NAME sn = {};
                    sn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                    sn.header.size = sizeof(sn);
                    sn.header.adapterId = paths[i].sourceInfo.adapterId;
                    sn.header.id = paths[i].sourceInfo.id;
                    if (DisplayConfigGetDeviceInfo(&sn.header) != ERROR_SUCCESS) continue;
                    if (wcscmp(sn.viewGdiDeviceName, gameDev) != 0) continue;
                }
                if (DisplayConfigGetDeviceInfo(&w.header) == ERROR_SUCCESS && w.SDRWhiteLevel)
                {
                    hdrOsSdrWhite = w.SDRWhiteLevel / 1000.0f * 80.0f;
                    static float saidWhite = -1.0f;          // the same rule as the display line above
                    const bool whiteSaid = saidWhite == hdrOsSdrWhite; saidWhite = hdrOsSdrWhite;
                    (whiteSaid ? LogHealth : LogRecord)("  hdr: OS SDR white level %.1f nits%s", hdrOsSdrWhite,
                              gameDev[0] ? ", from the game's own display" : "");
                }
            }
        }
        free(paths); free(modes);
    }
}

// ---- the game's display refresh ----
// The game's display path refresh as a fraction (119.88 vs 120.00), for the pacing law; written by HdrReadRefresh
// (DLSS-IV.cpp) after CreateDevice or Reset. Read on another thread, so one aligned 8-byte store (__iso_volatile_store64:
// winnt.h's 64-bit helpers are two 32-bit moves on x86). 0: no path matched. DLSSIV_FGDATA keeps .data and .bss unmoved.
alignas(8) DLSSIV_FGDATA static volatile LONGLONG hdrRefreshPacked;   // ((LONGLONG)numerator << 32) | denominator of the game's path; 0 when none matched
DLSSIV_FGDATA static double hdrRefreshHz;               // numerator / denominator, for log lines only

// ---- the FP16 back buffer, on stock DXVK ----
// Stock DXVK refuses an A16B16G16R16F back buffer at device creation ("ResetSwapChain: Unsupported backbuffer format") and
// the game shows a fatal D3D error. The supported way to allow it is UnlockAdditionalFormats() on ID3D9VkExtInterface,
// queried from the IDirect3D9 object before CreateDevice: the device caches the flag when it is constructed. (The
// swapchain-level UnlockAdditionalFormats is a deprecated no-op on 3.0.x.)
static bool hdrUnlockDone = false;       // an attempt has been made at least once this session
static bool hdrExtInterfaceOk = false;   // the query succeeded on the IDirect3D9 most recently seen
static IDirect3D9* hdrUnlockLastSeen = nullptr;   // for the log only: a reused address costs a line, never the unlock
static void HdrUnlockFormats(IDirect3D9* d3d)
{
    if (!d3d) return;                       // gated on ivtCfg.hdr at the call site: this header precedes the cfg
        // Not latched once per process, nor keyed on the pointer: the CreateDevice hook is on the shared vtable, so an overlay's
    // or trainer's interface reaches it too, and a released interface's address can come back as the game's. Every call
    // unlocks. ID3D9VkExtInterfaceMin lives in dxvk/dxvk_interop_min.h.
    const bool again = hdrUnlockDone;
    hdrUnlockDone = true;
    hdrExtInterfaceOk = false;
    ID3D9VkExtInterfaceMin* ext = nullptr;
    if (SUCCEEDED(d3d->QueryInterface(__uuidof(ID3D9VkExtInterfaceMin), (void**)&ext)) && ext)
    {
        hdrExtInterfaceOk = true;
        ext->UnlockAdditionalFormats();
        ext->Release();
        if (!again)
            LogHealth("  hdr: UnlockAdditionalFormats() called on the game's IDirect3D9 before CreateDevice, so an A16B16G16R16F back buffer is allowed");
        else if (d3d != hdrUnlockLastSeen)
            LogRecord("  hdr: UnlockAdditionalFormats() again, on a second IDirect3D9 (%p) at CreateDevice", (void*)d3d);
    }
    else LogRecord("  hdr: no ID3D9VkExtInterface (this IDirect3D9 is not DXVK's): HDR will not engage");
    hdrUnlockLastSeen = d3d;
}

// ---- the pre-tonemap image, for restoration ----
// Highlight restoration needs the picture before the tonemap clipped it: sampler 2 of the composite draw (screen-size
// A16B16G16R16F, the game's). The AddRef is dropped every frame and in HdrEncodeRelease: a D3DPOOL_DEFAULT resource still
// referenced at Reset fails the Reset and loses the device.
static IDirect3DTexture9* hdrSceneTex    = nullptr;
static bool               hdrSceneOk     = false;
// The rest of the composite's inputs, taken at the same instant, so the encode can rebuild it exactly (shaders/
// hdr_common.hlsli, GtaComposite): the 1x1 adapted luminance, the bloom, FusionFix's tone-map LUT when on, and the
// constants (Exposure c66, ToneMapParams c81, deSatContrastGamma c82, ColorCorrect c83, ColorShift c84, FusionFix c217).
// Found by what they are rather than by slot: the slots differ between shader variants (full: luminance s5, bloom s4;
// reduced: luminance s4).
static IDirect3DTexture9* hdrLumTex      = nullptr;
static IDirect3DTexture9* hdrBloomTex    = nullptr;
static IDirect3DTexture9* hdrLutTex      = nullptr;
static float              hdrGame[4][4]  = {};      // gGame0..3 exactly as the shaders read them
static bool               hdrGameOk      = false;   // constants read and sane this frame
static bool               hdrFfToneMap   = false;   // FusionFix tone mapping on (c217.z != 0)
static bool               hdrFfToneMapLogged = false, hdrFfToneMapLast = false;
static UINT64             hdrLumMisses   = 0;
DEVSTAT(static bool       hdrSceneLogged = false;)   // its one reader is the LogHealth in HdrCaptureScene
static UINT64             hdrSceneMisses = 0;
// With FusionFix's post AA on, the composite draws into an 8-bit screen-size target and the first back-buffer draw is
// SMAA. hdrAaDivert: look for the composite among the earlier 2-primitive draws. hdrCompositeSurf: that target once found.
// hdrCompositeOffscreen: nothing above white in the frame, so HdrResolveSource uses the rebuilt composite (source 1).
// All three clear when a back-buffer capture succeeds again and at Reset.
static bool               hdrAaDivert = false;
static IDirect3DSurface9* hdrCompositeSurf = nullptr;    // tracked, not referenced
static UINT               hdrCompositeW = 0, hdrCompositeH = 0;   // that target's size, for the log line that names it
static bool               hdrCompositeOffscreen = false;
static UINT               hdrOffscreenProbes = 0;        // candidate draws examined this frame
static LONG               hdrOffscreenFrames = 0;        // frames searched without a find
static bool               hdrOffscreenSaid = false;
static UINT               hdrBbCaptureRun = 0;           // back-buffer captures in a row while the composite was being taken off-screen
static void HdrOffscreenForget()
{
    hdrAaDivert = false; hdrCompositeSurf = nullptr; hdrCompositeOffscreen = false;
    hdrOffscreenProbes = 0; hdrOffscreenFrames = 0; hdrOffscreenSaid = false; hdrBbCaptureRun = 0;
}

static void HdrSceneDrop()
{
    if (hdrSceneTex) { hdrSceneTex->Release(); hdrSceneTex = nullptr; }
    if (hdrLumTex)   { hdrLumTex->Release();   hdrLumTex   = nullptr; }
    if (hdrBloomTex) { hdrBloomTex->Release(); hdrBloomTex = nullptr; }
    if (hdrLutTex)   { hdrLutTex->Release();   hdrLutTex   = nullptr; }
    hdrGameOk = false;
}


// The constants and the other textures the composite reads: three constant reads and up to sixteen GetTexture calls a frame.
static void HdrCaptureCompositeInputs(IDirect3DDevice9* dev, UINT sceneW)
{
    float c66[4] = {}, c81[16] = {}, c217[4] = {};
    dev->GetPixelShaderConstantF(66, c66, 1);
    dev->GetPixelShaderConstantF(81, c81, 4);     // c81 ToneMapParams, c82 deSatContrastGamma, c83 ColorCorrect, c84 ColorShift
    dev->GetPixelShaderConstantF(217, c217, 1);   // FusionFix: z tone mapping on, w NoBloomColorShift
    hdrFfToneMap = c217[2] != 0.0f;
    if (!hdrFfToneMapLogged || hdrFfToneMap != hdrFfToneMapLast)
    {
        hdrFfToneMapLogged = true; hdrFfToneMapLast = hdrFfToneMap;
        LogRecord("  hdr: FusionFix tone mapping is %s (c217.z = %g); the game's clip is %s", hdrFfToneMap ? "ON" : "OFF", c217[2],
            hdrFfToneMap ? "its LUT's shoulder" : "the 8-bit target");
    }
    hdrGame[0][0] = c66[0];  hdrGame[0][1] = c81[0];  hdrGame[0][2] = c81[1];  hdrGame[0][3] = c81[2];
    hdrGame[1][0] = c81[4];  hdrGame[1][1] = c81[6];  hdrGame[1][2] = c81[15]; hdrGame[1][3] = c217[3] != 0.0f ? 1.0f : 0.0f;
    hdrGame[2][0] = c81[8];  hdrGame[2][1] = c81[9];  hdrGame[2][2] = c81[10]; hdrGame[2][3] = 0.0f;
    hdrGame[3][0] = c81[12]; hdrGame[3][1] = c81[13]; hdrGame[3][2] = c81[14]; hdrGame[3][3] = 0.0f;

    IDirect3DTexture9* lum = nullptr; IDirect3DTexture9* bloom = nullptr; IDirect3DTexture9* lut = nullptr;
    UINT lumArea = ~0u;
    for (int i = 0; i < 16; ++i)
    {
        IDirect3DBaseTexture9* bt = nullptr;
        if (FAILED(dev->GetTexture(i, &bt)) || !bt) continue;
        if (bt->GetType() != D3DRTYPE_TEXTURE) { bt->Release(); continue; }
        IDirect3DTexture9* tx = static_cast<IDirect3DTexture9*>(bt);
        D3DSURFACE_DESC d = {};
        if (FAILED(tx->GetLevelDesc(0, &d))) { tx->Release(); continue; }
        const unsigned f = (unsigned)d.Format;
        const bool isFloat = f == 111 || f == 112 || f == 113 || f == 114 || f == 115 || f == 116;
        const UINT area = d.Width * d.Height;
        bool keep = false;
        if (i != 2 && area <= 16 && isFloat && area < lumArea)
        { if (lum) lum->Release(); lum = tx; lumArea = area; keep = true; }
        else if (i != 2 && d.Width == 1024 && d.Height == 32 && !lut)
        { lut = tx; keep = true; }
        else if (i >= 3 && i <= 8 && isFloat && !bloom && d.Width * 4 >= sceneW - 4 && d.Width * 4 <= sceneW + 4)
        { bloom = tx; keep = true; }   // the bloom: a float target at a quarter of the scene, whatever slot it sits in
        else if ((i == 4 || i == 3) && !bloom && !isFloat && d.Width >= 64 && d.Width < sceneW && !(d.Width == 256 && d.Height == 256))
        { bloom = tx; keep = true; }   // an 8-bit bloom, by slot, if there is no float one
        if (!keep) tx->Release();
    }
    hdrLumTex = lum; hdrBloomTex = bloom; hdrLutTex = lut;
    hdrGame[2][3] = bloom ? 1.0f : 0.0f;
    hdrGame[3][3] = (lut && hdrFfToneMap) ? 1.0f : 0.0f;
    hdrGameOk = lum && c66[0] > 0.0f && c81[1] > 0.0f;
    if (telemetryOn && !hdrGameOk && hdrLumMisses++ < 3)
        LogRecord("  hdr: composite inputs incomplete this frame (luminance %s, Exposure %g, ToneMapParams.y %g): exact restoration off this frame",
            lum ? "found" : "MISSING", c66[0], c81[1]);
}

// ---- the pre-HUD frame ----
// The HUD must stay at reference white (BT.2408), not ride restored highlights, so the back buffer is copied at the HUD
// boundary and the encode compares per pixel. The boundary: the first orthographic back-buffer draw after a scene-camera
// draw, skipping opaque full-screen quads (FusionFix's console gamma is one). No draws after the composite: not armed.
static IDirect3DTexture9* hdrPreHudTex  = nullptr;
static IDirect3DSurface9* hdrPreHudSurf = nullptr;
static UINT  hdrPreHudW = 0, hdrPreHudH = 0;
static bool  hdrPreHudValid   = false;   // a copy was taken...
static UINT  hdrPreHudFrame   = 0xFFFFFFFFu;   // ...for this frame, and the stamp is what proves it
static bool  hdrPreHudArmed   = true;    // the HUD drew last frame, so take it again
DLSSIV_HOT static bool  hdrPreHudPending = false;   // this frame's copy is still to be taken
DLSSIV_HOT static UINT  hdrPreHudDraws   = 0;       // draws seen at and after the interface boundary this frame

// HDR's own per-frame draw census and device: the transport's ivtSceneDraws and ivtDev are not maintained when transport=0
// or before it starts. Reset in the EndScene hook, which always runs.
DLSSIV_HOT static UINT  hdrSceneDraws    = 0;       // scene-camera draws this frame, HDR's own count
// HDR's own count of back-buffer draws; the pre-tonemap capture fires on the first (the composite). Not volatile: one
// thread owns it, and it is bumped on every back-buffer draw in the hottest hook.
DLSSIV_HOT static UINT hdrBbDraws = 0;              // backbuffer draws this frame, HDR's own count
// The draw hooks' HDR gate, ivtCfg.hdr && hdrEngaged in one hot byte, so no HDR draw work runs once HDR was refused or
// stood down. Written after each HdrRewriteFormat and once a frame in hkEndScene.
DLSSIV_HOT static bool hdrHooks = false;

// ---- the absorb: the hot state (the design is in hdr_cga.h) ----
// FusionFix's console gamma draw is withheld once its pixel shader is proved, and the encode samples FusionFix's own copy
// of the frame and applies the identical curve and dither.
DLSSIV_HOT static bool hdrCgaWant  = false;   // this frame may: the key on, HDR encoding, no view, the mod's shaders made (HdrCgaFrameEnd's `want`)
DLSSIV_HOT static bool hdrCgaArmed = false;   // the point copy of the whole back buffer was seen this frame; the draw that samples it is next
DLSSIV_HOT static bool hdrCgaHeld  = false;   // that draw was withheld: the back buffer holds the frame without the curve until the encode
// Which curve FusionFix drew this frame, when its draw was the proved shader: 1 the Xbox 360 curve, 2 the PS3 curve, 0 not
// known. Set at FusionFix's draw (HdrCgaOnDraw), read by the encode's constants (HdrConsoleGammaMode), cleared at
// HdrCgaFrameEnd after the encode. The proof is exact to half-float rounding, so this is the answer, not an estimate.
static int hdrCgaCurve = 0;
// The proved curve stopped being drawn: set by HdrCgaFrameEnd when FusionFix draws with another shader (the player switched
// curves) or its pass is absent for kCgaNoPassEnds frames; read once by HdrCgProbeTick, which then ends the teach window
// so the probe may speak at once.
static bool hdrCgaTeachEnd = false;
enum { CGA_HEAL_DRAW = 0, CGA_HEAL_STRETCH, CGA_HEAL_CLEAR, CGA_HEAL_FILL, CGA_HEAL_READ, CGA_HEAL_NOENCODE, CGA_HEAL_N };
static void HdrCgaHeal(IDirect3DDevice9* dev, int why);   // hdr_cga.h: the curve drawn by the mod after all, because the back buffer is wanted first
static void HdrCgaRelease();                              // hdr_cga.h
// ---- end of the absorb's hot state ----
DLSSIV_HOT static IDirect3DDevice9* hdrArmDev = nullptr;   // set at EndScene; stands in for ivtDev
static int   hdrPreHudFails   = 0;
static UINT  hdrPreHudRetryAt = 0;   // after a refused create: the frame to ask again, 300 on, not every frame

static void HdrPreHudRelease()
{
    if (hdrPreHudSurf) { hdrPreHudSurf->Release(); hdrPreHudSurf = nullptr; }
    if (hdrPreHudTex)  { hdrPreHudTex->Release();  hdrPreHudTex  = nullptr; }
    hdrPreHudW = hdrPreHudH = 0;
    hdrPreHudValid = hdrPreHudPending = false; hdrPreHudFrame = 0xFFFFFFFFu;
    hdrPreHudRetryAt = 0;                // a new device state (Reset) gets its first attempt at once
}

// Called at the HUD boundary, and only there.
static void HdrPreHudCapture(IDirect3DDevice9* dev)
{
    hdrPreHudPending = false;
    if (!dev || !hdrPreHudArmed || !backbufferSurf) return;
    D3DSURFACE_DESC d = {};
    if (FAILED(backbufferSurf->GetDesc(&d))) return;
    if (!hdrPreHudTex || hdrPreHudW != d.Width || hdrPreHudH != d.Height)
    {
                // A refused create is asked again every 300 frames, not every frame (a screen-size FP16 target refused for lack of video
        // memory would otherwise be a failing allocation a frame).
        if (!hdrPreHudTex && hdrFramesSeen < hdrPreHudRetryAt) return;
        HdrPreHudRelease();
                // The mod's own allocation, so it goes through the guard: rsOwnCreate keeps the game's streaming budget figures free of
        // the mod's targets, and without it this back-buffer-format target would enter fpTab (the game's FP16 render targets,
        // raw pointers, no reference) and dangle once HdrPreHudRelease frees it. The guard covers CreateTexture and
        // GetSurfaceLevel and nothing else.
        RsOwnCreateCall guard;
        if (FAILED(dev->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format,
                                      D3DPOOL_DEFAULT, &hdrPreHudTex, nullptr)) || !hdrPreHudTex)
        {
            hdrPreHudTex = nullptr; hdrPreHudRetryAt = hdrFramesSeen + 300;
            if (telemetryOn && hdrPreHudFails++ < 3)
                LogRecord("  hdr: the %ux%u UI copy target could not be created: the interface rides the scene's highlights (again in 300"
                              " frames)", d.Width, d.Height);
            return;
        }
        if (FAILED(hdrPreHudTex->GetSurfaceLevel(0, &hdrPreHudSurf)) || !hdrPreHudSurf) { HdrPreHudRelease(); hdrPreHudRetryAt = hdrFramesSeen + 300; return; }
        hdrPreHudW = d.Width; hdrPreHudH = d.Height;
    }
    if (SUCCEEDED(dev->StretchRect(backbufferSurf, nullptr, hdrPreHudSurf, nullptr, D3DTEXF_NONE)))
    {
        hdrPreHudValid = true;
        hdrPreHudFrame = hdrFramesSeen;   // stamped: a frame that never reached the encode cannot pass its copy on
    }
    else if (telemetryOn && hdrPreHudFails++ < 3)
        LogRecord("  hdr: the UI copy failed this frame: the interface rides the scene's highlights");
}

// Called at the composite draw, while its textures are still bound. `offscreen`
// says the caller found the composite drawing into an 8-bit target rather than
// the back buffer (hdrAaDivert): the frame then holds nothing above white.
static void HdrCaptureScene(IDirect3DDevice9* dev, bool offscreen = false)
{
    if (!dev) return;
    HdrSceneDrop();                         // one reference at a time, never a leak
    hdrSceneOk = false;

    IDirect3DBaseTexture9* bt = nullptr;
    if (FAILED(dev->GetTexture(2, &bt)) || !bt)
    {
        if (telemetryOn && hdrSceneMisses++ < 3) LogRecord("  hdr: composite sampler 2 was empty: no highlight source this frame");
        return;
    }
    if (bt->GetType() != D3DRTYPE_TEXTURE) { bt->Release(); return; }

    IDirect3DTexture9* t = static_cast<IDirect3DTexture9*>(bt);
    D3DSURFACE_DESC d = {};
    if (FAILED(t->GetLevelDesc(0, &d))) { t->Release(); return; }

        // Only a float format holds values above white; restoring from anything else would invent highlights.
    if (d.Format != D3DFMT_A16B16G16R16F && d.Format != D3DFMT_A32B32G32R32F)
    {
                // The first back-buffer draw is a post-process AA pass, not the composite: from the next frame the composite is looked
        // for where it went (hdrAaDivert). An off-screen candidate that fails here (its target's pointer was reused) only says
        // so; the caller relearns.
        if (!offscreen)
        {
            const bool first = !hdrAaDivert;
            hdrAaDivert = true;
            if (telemetryOn && (first || hdrSceneMisses < 3) && hdrSceneMisses++ < 6)
                LogRecord("  hdr: the first back-buffer draw's sampler 2 is %ux%u %s, not the pre-tonemap image%s: looked for off-screen from the next frame",
                    d.Width, d.Height, HdrFmtName(d.Format),
                    (d.Width == 160 && d.Height == 560) ? " (SMAA's area texture)" : "");
        }
        t->Release();
        return;
    }

    hdrSceneTex = t;                        // the reference from GetTexture is now the mod's
    hdrSceneOk = true;
        // A back-buffer capture that worked means the composite is back on the back buffer: stop searching. Not on one frame's
    // word: with SMAA on, a paused game can show the full-size float scene on the first back-buffer draw's sampler 2, so the
    // capture is kept and only the forgetting waits for thirty such frames in a row (FusionFix's AA really off); any
    // off-screen capture in between restarts the count.
    if (offscreen) hdrBbCaptureRun = 0;
    else if ((hdrAaDivert || hdrCompositeOffscreen) && ++hdrBbCaptureRun >= 30) { hdrBbCaptureRun = 0; HdrOffscreenForget(); }
    if (offscreen) hdrCompositeOffscreen = true;
    HdrCaptureCompositeInputs(dev, d.Width);
#if DLSSIV_DEVLOG                       // the once-latch and its log line together: a release build carries neither
    if (!hdrSceneLogged)
    {
        hdrSceneLogged = true;
        LogHealth("  hdr: captured the pre-tonemap image, %ux%u %s. This is the highlight source.",
            d.Width, d.Height, HdrFmtName(d.Format));
    }
#endif
    if (offscreen && !hdrOffscreenSaid)
    {
        hdrOffscreenSaid = true;
        LogRecord("  hdr: the composite draws into an off-screen 8-bit target, %ux%u: captured there", hdrCompositeW, hdrCompositeH);
    }
}

// The off-screen composite, one candidate 2-primitive draw at a time (hdrAaDivert): an 8-bit target at the screen or the
// render size (hkCreateTexture scales those) with a screen-size float texture on sampler 2 (FP16 targets are never
// scaled). Once known, a pointer compare. True when captured here.
static bool HdrOffscreenComposite(IDirect3DDevice9* dev, IDirect3DSurface9* rt0, UINT screenW, UINT screenH,
                                  UINT scaledW, UINT scaledH)
{
    if (!dev || !rt0 || !screenW || hdrSceneOk) return false;
    if (hdrCompositeSurf)
    {
        if (rt0 != hdrCompositeSurf) return false;
    }
    else
    {
                // Bounded twice: 1024 candidates a frame (the scene phase draws its own 2-primitive things, coronas and decals), and none
        // at all once 600 frames have failed and the log has said so.
        if (hdrOffscreenFrames >= 600 || hdrOffscreenProbes++ >= 1024) return false;
        D3DSURFACE_DESC rd = {};
        if (FAILED(rt0->GetDesc(&rd))) return false;
        if (!(rd.Width == screenW && rd.Height == screenH)
            && !(scaledW && rd.Width == scaledW && rd.Height == scaledH)) return false;
        if (rd.Format != D3DFMT_A8R8G8B8 && rd.Format != D3DFMT_X8R8G8B8 && rd.Format != D3DFMT_A8B8G8R8) return false;
        IDirect3DBaseTexture9* bt = nullptr;
        if (FAILED(dev->GetTexture(2, &bt)) || !bt) return false;
        bool ok = false;
        if (bt->GetType() == D3DRTYPE_TEXTURE)
        {
            D3DSURFACE_DESC td = {};
            if (SUCCEEDED(static_cast<IDirect3DTexture9*>(bt)->GetLevelDesc(0, &td)))
                ok = (td.Format == D3DFMT_A16B16G16R16F || td.Format == D3DFMT_A32B32G32R32F)
                  && td.Width == screenW && td.Height == screenH;
        }
        bt->Release();
        if (!ok) return false;
        hdrCompositeSurf = rt0;
        hdrCompositeW = rd.Width; hdrCompositeH = rd.Height;
    }
    HdrCaptureScene(dev, true);
    if (!hdrSceneOk) hdrCompositeSurf = nullptr;   // the pointer no longer names the composite's target: learn again
    else             hdrOffscreenFrames = 0;       // found: the 600 is consecutive failed frames, not a session's total
    return hdrSceneOk;
}
