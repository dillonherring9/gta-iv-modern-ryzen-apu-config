// Vice City BBridge: the bulk upscale stays on the bridge's native x64 server.
// The game still owns scene detection, render scaling, jitter and D3D9 guide passes.
// No pointers or Vulkan handles from this process are sent to the x64 helper.
#pragma once
#include "../../bridge/src/vc_upscale_wire.h"

using IvtServerFrameFn = HRESULT (WINAPI*)(const VCBConfig*, const VCBInput*, VCBResult*);
using IvtServerShutdownFn = HRESULT (WINAPI*)(void*, VCBResult*);
static IvtServerFrameFn ivtServerFrame = nullptr;
static IvtServerShutdownFn ivtServerShutdown = nullptr;
static bool ivtServerIssued = false;
static IDirect3DTexture9* ivtServerPendingHome = nullptr;
static UINT64 ivtServerPendingHomeN = 0;

static void IvtServerClearHome()
{
    DT_DIRECT();
    if (ivtServerPendingHome) { ivtServerPendingHome->Release(); ivtServerPendingHome = nullptr; }
    ivtServerPendingHomeN = 0;
}

static void IvtServerRememberHome(IDirect3DTexture9* tex, UINT64 sequence)
{
    DT_DIRECT();
    IvtServerClearHome();
    ivtServerPendingHome = tex; ivtServerPendingHomeN = sequence;
    if (tex) tex->AddRef();
}

static bool IvtServerCopyPendingHome(IDirect3DDevice9* dev, IDirect3DSurface9* backbuffer, UINT64 sequence)
{
    DT_DIRECT();
    if (!dev || !backbuffer || !ivtServerPendingHome || ivtServerPendingHomeN != sequence || ivtSentN != sequence)
    {
        IvtServerClearHome();
        IvtFail("the split native upscale has no completed result for this exact frame");
        return false;
    }
    IDirect3DSurface9* src = nullptr;
    HRESULT hr = ivtServerPendingHome->GetSurfaceLevel(0, &src);
    if (SUCCEEDED(hr) && !src) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr))
    {
        // This is the legacy pause fallback after its opaque composite, not the
        // scene seam. Both surfaces are output-size; DXVK converts formats just
        // as the old raw Vulkan blit did. The own-call guard prevents redirection.
        RsOwnCall own;
        hr = dev->StretchRect(src, nullptr, backbuffer, nullptr, D3DTEXF_NONE);
        src->Release();
    }
    IvtServerClearHome();
    if (FAILED(hr))
    {
        char why[192];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "the completed native upscale could not be copied after the pause composite (HRESULT 0x%08X)", static_cast<unsigned>(hr));
        IvtFail(why);
        return false;
    }
    return true;
}

static bool IvtServerUpscaleAvailable()
{
    // The private bridge is loaded before this companion. Its remote Vulkan marker
    // also proves these exports belong to the D3D9 device's own bridge client.
    HMODULE bridge = FeedVkRemoteModule();
    if (!bridge) return false;
    if (!ivtServerFrame)
        ivtServerFrame = reinterpret_cast<IvtServerFrameFn>(GetProcAddress(bridge, "VCB_UpscaleFrame"));
    if (!ivtServerShutdown)
        ivtServerShutdown = reinterpret_cast<IvtServerShutdownFn>(GetProcAddress(bridge, "VCB_UpscaleShutdown"));
    return ivtServerFrame && ivtServerShutdown;
}

static bool IvtServerPath(char* out, size_t cap, HMODULE module, const wchar_t* tail)
{
    wchar_t path[32768];
    const DWORD n = GetModuleFileNameW(module, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (!n || n >= sizeof(path) / sizeof(path[0])) return false;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return false;
    const size_t base = static_cast<size_t>(slash + 1 - path);
    const size_t tailN = wcslen(tail);
    if (base + tailN >= sizeof(path) / sizeof(path[0])) return false;
    memcpy(path + base, tail, (tailN + 1) * sizeof(wchar_t));
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, out, static_cast<int>(cap), nullptr, nullptr);
    return bytes > 0;
}

static bool IvtServerConfig(VCBConfig* out)
{
    *out = {};
    out->size = sizeof(*out); out->version = VCB_ABI_VERSION;
    out->backend = ivtCfg.backend;
    out->fsr_version = ivtCfg.fsr_version;
    out->preset = ivtCfg.preset;
    out->sharpness = ivtCfg.sharpness;
    out->timeout_ms = 30000;
    // The captured scene is pre-tonemap FP16 even with display HDR disabled.
    out->hdr = ivtColorFmt == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1 : 0;
    out->depth_inverted = 1;
    out->flags_override = ivtCfg.flags;
    out->transport = IVT_MODE == 1 ? 1 : 0;
    if (!selfModule || !IvtServerPath(out->helper_path, sizeof(out->helper_path), selfModule, L"dlssiv-helper-bridge.exe") ||
        !IvtServerPath(out->vendor_dir, sizeof(out->vendor_dir), nullptr, L"DLSSIV"))
    {
        IvtFail("the native bridge helper or vendor path could not be formed as an absolute UTF-8 path");
        return false;
    }
    if (!VCB_ValidConfig(*out))
    {
        IvtFail("the native bridge upscaler configuration is unsupported by its paired ABI");
        return false;
    }
    return true;
}

static bool IvtServerUpscale(const VCBInput& input)
{
    VCBConfig config = {};
    if (!IvtServerConfig(&config)) return false;
    VCBResult result = {};
    result.size = sizeof(result); result.version = VCB_ABI_VERSION;
    ivtServerIssued = true;
    const HRESULT hr = ivtServerFrame(&config, &input, &result);
    const uint32_t completed = VCB_RESULT_READY | VCB_RESULT_COPIED_HOME;
    if (FAILED(hr) || result.size != sizeof(result) || result.version != VCB_ABI_VERSION ||
        FAILED(static_cast<HRESULT>(result.hr)) || result.sequence != input.frame.sequence ||
        (result.flags & completed) != completed || result.backend < 0 || result.backend > 1)
    {
        char why[512];
        result.message[sizeof(result.message) - 1] = 0;
        _snprintf_s(why, sizeof(why), _TRUNCATE,
            "native 64-bit upscale failed for frame %llu (HRESULT 0x%08X, service 0x%08X, flags 0x%X): %s",
            static_cast<unsigned long long>(input.frame.sequence), static_cast<unsigned>(hr),
            static_cast<unsigned>(result.hr), result.flags, result.message);
        IvtFail(why);
        return false;
    }
    const bool sr = input.frame.render_width != input.frame.output_width || input.frame.render_height != input.frame.output_height;
    if (sr && !(result.detail & FEED_ACK_SR_ACTIVE))
    {
        IvtFail("the native 64-bit helper returned an image but did not acknowledge the requested Super Resolution mode");
        return false;
    }
    const bool backendFsr = result.backend == 1;
    if (backendFsr != ((result.detail & FEED_ACK_BACKEND_FSR) != 0) || (result.detail & FEED_ACK_BACKEND_XESS))
    {
        IvtFail("the native bridge helper returned inconsistent upscaler backend metadata");
        return false;
    }
    static int reportedBackend = -1;
    static uint32_t reportedDetail = ~0u;
    if (reportedBackend != result.backend || reportedDetail != result.detail)
    {
        LogRecord("  bridge upscale: native 64-bit %s, %ux%u -> %ux%u, same frame, helper flags 0x%X",
            backendFsr ? "FSR" : "DLSS", input.frame.render_width, input.frame.render_height,
            input.frame.output_width, input.frame.output_height, result.detail);
        reportedBackend = result.backend; reportedDetail = result.detail;
    }
    ivtBackendIsFsr = backendFsr;
    ivtBackendId = result.backend;
    if ((result.detail & FEED_ACK_FSR4_SKIPPED) && !ivtFsr4SkipSaid)
    {
        ivtFsr4SkipSaid = true;
        const unsigned sm = (result.detail & FEED_ACK_SM_MASK) >> FEED_ACK_SM_SHIFT;
        IvtWarn("FSR 3.1 is running: Direct3D 12 offers shader model %u.%u. FOR FSR 4: %s",
            sm >> 4, sm & 0xFu, IvtAgilityAdvice((result.detail & FEED_ACK_AGILITY_REFUSED) != 0));
    }
    return true;
}

static bool IvtServerReset(IDirect3DDevice9* dev)
{
    IvtServerClearHome();
    if (!ivtServerIssued) return true;
    if (!IvtServerUpscaleAvailable() || !dev)
    {
        IvtFail("the native bridge upscale session could not be shut down before a device Reset");
        return false;
    }
    VCBResult result = {};
    result.size = sizeof(result); result.version = VCB_ABI_VERSION;
    const HRESULT hr = ivtServerShutdown(dev, &result);
    if (FAILED(hr) || result.size != sizeof(result) || result.version != VCB_ABI_VERSION ||
        FAILED(static_cast<HRESULT>(result.hr)) || !(result.flags & VCB_RESULT_SHUTDOWN))
    {
        IvtFail("the native bridge upscaler did not confirm shutdown before a device Reset");
        return false;
    }
    ivtServerIssued = false;
    return true;
}
