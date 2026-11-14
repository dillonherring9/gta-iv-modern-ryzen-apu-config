// ---- the absorb: FusionFix's console gamma pass, folded into the HDR encode (hot state and key in hdr.h) ----
// FusionFix's console gamma pass copies the back buffer into a texture T and draws T through its curve back into the back
// buffer; the encode would then copy and draw again. So FusionFix's draw is withheld once its pixel shader is proved, and
// the encode samples T and applies the identical curve and dither (hdr_encode_cga_ps.hlsl).
// The pass is recognised by structure (a point copy of the whole back buffer into a same-size render target texture, then
// a full-screen draw from the same module sampling it), and the shader by proof: on an untouched frame a 256-pixel pass
// compares each of the four candidate curves with the real output (hdr_cgaproof_ps.hlsl). One candidate fitting every
// pixel and the others failing on sixteen or more, three checks running, proves that shader object. One frame in 600 is
// checked again; a failed check withdraws the proof.
// Anything that touches the back buffer between the withheld draw and the encode first gets the curve drawn from T
// (HdrCgaHeal); four such remedies with no absorbed frame between them turn the absorb off for 600 frames.

#include "shaders/hdr_encode_cga_ps.h"
#include "shaders/hdr_cgaproof_ps.h"
#include "shaders/hdr_cgablit_ps.h"

static const UINT  kCgaProofEveryUnproved = 120;     // frames between checks until a shader is proved
static const UINT  kCgaProofEveryProved   = 600;     // and after: one frame in 600 takes the old path and is compared
static const int   kCgaAgreeNeeded        = 3;       // checks that must name the same candidate, running
static const int   kCgaNoPassEnds         = 60;      // frames in a row without FusionFix's pass that end the probe's teach window
static const float kCgaTol                = 1.02f;   // half-float steps a channel: the write may land on either neighbour (the dither alone is up to 8)
static const int   kCgaOthersMustFail     = 16;      // of 256: a dark frame fits everything, and proves nothing

// ---- this frame ----
// Referenced, not borrowed: under threaded=1 a Release made by the render thread is recorded and run by the worker later,
// so FusionFix could drop its target between the arm and the draw and leave HdrCgaOnDraw with freed memory.
static IDirect3DSurface9*     cgaDst      = nullptr;   // the arming copy's destination, referenced until the frame ends
static IDirect3DTexture9*     cgaTex      = nullptr;   // its texture; referenced from the recognised draw to HdrCgaFrameEnd
static bool                   cgaRan      = false;     // FusionFix's draw was recognised and issued this frame
static bool                   cgaSawPass  = false;     // its pass arrived at all this frame, withheld or not
static bool                   cgaProofDue = false;     // decided at the last frame's end: this frame runs untouched and is compared
static bool                   cgaSawUnproved = false;  // this frame the module drew with a shader that is not the proved one
static int                    cgaNoPassRun   = 0;      // frames in a row the armed module's pass did not arrive
// ---- the module that armed ----
static const BYTE*            cgaModLo = nullptr;
static const BYTE*            cgaModHi = nullptr;
DEVSTAT(static char           cgaModName[64] = "";)   // only the name is for the log: cgaModLo and cgaModHi above are the
                                                      // range test that decides which draw is FusionFix's.
// ---- the proof ----
static IDirect3DPixelShader9* cgaProvedPS = nullptr;   // referenced
static IDirect3DPixelShader9* cgaIssuedPS = nullptr;   // referenced, from a check's issue to its analysis
// Compared, never dereferenced: this holds no reference, so it may name freed memory or another shader at the same address.
// It is only tested for equality against the current cgaIssuedPS, which is referenced, so a stale value can at worst
// restart the three-check run: it can lose a proof, never grant one. cgaProvedPS is referenced because it decides withholding.
static IDirect3DPixelShader9* cgaCandPS   = nullptr;   // an identity to compare, not an object to use
static int   cgaCand = -1, cgaAgree = 0;
static float cgaC44[4]   = { 0, 0, 0, 0 };             // FusionFix's c44 as read at the last compared draw
static float cgaConst[4] = { 0, 0, 0, 0 };             // what the shaders read: curve, dither, c44.xy
static bool  cgaPending = false;
static UINT  cgaIssuedAt = 0, cgaLastProof = 0, cgaProofN = 0;
// ---- the mod's passes ----
static IDirect3DPixelShader9* cgaEncPS = nullptr;
static IDirect3DPixelShader9* cgaProofPS = nullptr;
static IDirect3DPixelShader9* cgaBlitPS = nullptr;
static IDirect3DTexture9*     cgaProofTex = nullptr;
static IDirect3DSurface9*     cgaProofSurf = nullptr;
static IDirect3DSurface9*     cgaProofSys = nullptr;
static bool  cgaTried = false;
static UINT  cgaRetryAt = 0;        // after a refused create: the frame to ask again, never every frame
static int   cgaProofLostRun = 0;   // and how many in a row, so a lost re-check cannot hold the absorb off for ever
static int  cgaHealStreak = 0, cgaHold = 0;

static bool HdrCgaWithholds() { return ivtCfg.hdr_cg_absorb == 1; }

DEVSTAT(static const char* const kCgaCandName[4] = { "the Xbox 360 curve", "the Xbox 360 curve with the dither", "the PS3 curve", "the PS3 curve with the dither" };)   // named in the proof's log line only

static void HdrCgaUnprove(const char* why)
{
    if (cgaProvedPS)
    {
                // LogRecord, not LogHealth: this is the absorb's one fault line (FusionFix's real output disagreed with the curve the mod
        // applies in its place), and a player's log=1 report must show it.
        LogRecord("  hdr absorb: THE PROOF IS WITHDRAWN (%s): FusionFix's console gamma pass runs", why);
        cgaProvedPS->Release(); cgaProvedPS = nullptr;
    }
    cgaConst[0] = cgaConst[1] = 0.0f;
    cgaCand = -1; cgaAgree = 0; cgaCandPS = nullptr;
}

static void HdrCgaRelease()
{
    if (cgaTex)       { cgaTex->Release();       cgaTex = nullptr; }
    if (cgaProvedPS)  { cgaProvedPS->Release();  cgaProvedPS = nullptr; }
    if (cgaIssuedPS)  { cgaIssuedPS->Release();  cgaIssuedPS = nullptr; }
    if (cgaProofSys)  { cgaProofSys->Release();  cgaProofSys = nullptr; }
    if (cgaProofSurf) { cgaProofSurf->Release(); cgaProofSurf = nullptr; }
    if (cgaProofTex)  { cgaProofTex->Release();  cgaProofTex = nullptr; }
    if (cgaEncPS)     { cgaEncPS->Release();     cgaEncPS = nullptr; }
    if (cgaProofPS)   { cgaProofPS->Release();   cgaProofPS = nullptr; }
    if (cgaBlitPS)    { cgaBlitPS->Release();    cgaBlitPS = nullptr; }
    cgaTried = false; cgaPending = false; cgaProofDue = false; cgaRan = false;
    if (cgaDst) { cgaDst->Release(); cgaDst = nullptr; }
    cgaCand = -1; cgaAgree = 0; cgaCandPS = nullptr; cgaConst[0] = cgaConst[1] = 0.0f;
    cgaHealStreak = 0; cgaHold = 0;
    hdrCgaWant = false; hdrCgaArmed = false; hdrCgaHeld = false;
}

// The three shaders, the 256x1 target and its system-memory twin. At the mod's EndScene, never inside a draw hook.
// Without all of them nothing is withheld and nothing is compared.
static bool HdrCgaEnsure(IDirect3DDevice9* dev)
{
    if (cgaEncPS && cgaProofPS && cgaBlitPS && cgaProofSys) return true;
        // A refused create is asked again 300 frames later (a 4K scratch may not fit a 32-bit address space at one moment), as
    // hdrPreHudRetryAt does, never every frame.
    if (cgaTried && hdrFramesSeen < cgaRetryAt) return false;
    cgaTried = true;
    cgaRetryAt = hdrFramesSeen + 300;
    RsOwnCreateCall guard;
    bool ok = SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrEncodeCgaPS, &cgaEncPS)) && cgaEncPS
           && SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrCgaProofPS, &cgaProofPS)) && cgaProofPS
           && SUCCEEDED(dev->CreatePixelShader((const DWORD*)kHdrCgaBlitPS, &cgaBlitPS)) && cgaBlitPS;
    if (ok) ok = SUCCEEDED(dev->CreateTexture(256, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &cgaProofTex, nullptr)) && cgaProofTex
              && SUCCEEDED(cgaProofTex->GetSurfaceLevel(0, &cgaProofSurf)) && cgaProofSurf;
    if (ok) ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(256, 1, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &cgaProofSys, nullptr)) && cgaProofSys;
    if (!ok)
    {
        if (cgaProofSys)  { cgaProofSys->Release();  cgaProofSys = nullptr; }
        if (cgaProofSurf) { cgaProofSurf->Release(); cgaProofSurf = nullptr; }
        if (cgaProofTex)  { cgaProofTex->Release();  cgaProofTex = nullptr; }
        if (cgaEncPS)     { cgaEncPS->Release();     cgaEncPS = nullptr; }
        if (cgaProofPS)   { cgaProofPS->Release();   cgaProofPS = nullptr; }
        if (cgaBlitPS)    { cgaBlitPS->Release();    cgaBlitPS = nullptr; }
                // Said three times at most, like the pre-HUD copy's line (hdrPreHudFails).
        static int cgaCreateFails = 0;
        if (cgaCreateFails++ < 3)
            LogRecord("  hdr absorb: its shaders or proof target could not be created: FusionFix's console gamma pass runs (again in 300"
                  " frames%s)", cgaCreateFails == 3 ? ", not said again" : "");
    }
    return ok;
}

// The arm, from hkStretchRect: a point copy of the whole back buffer, by someone else, into something not the mod's. It arms
// only if the destination could be a full-screen pass's texture: the back buffer's size and format, a render target, not
// multisampled.
static void HdrCgaArm(IDirect3DSurface9* dst, void* ret)
{
    D3DSURFACE_DESC d = {}, b = {};
    if (FAILED(dst->GetDesc(&d)) || FAILED(backbufferSurf->GetDesc(&b))) return;
    if (d.Width != b.Width || d.Height != b.Height || d.Format != b.Format || !(d.Usage & D3DUSAGE_RENDERTARGET) || d.MultiSampleType != D3DMULTISAMPLE_NONE) return;
    const BYTE* const a = (const BYTE*)ret;
    if (a < cgaModLo || a >= cgaModHi)
    {
        HMODULE m = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ret, &m) || !m) return;
        const IMAGE_DOS_HEADER* const dos = (const IMAGE_DOS_HEADER*)m;
        const IMAGE_NT_HEADERS* const nt  = (const IMAGE_NT_HEADERS*)((const BYTE*)m + dos->e_lfanew);
        cgaModLo = (const BYTE*)m; cgaModHi = cgaModLo + nt->OptionalHeader.SizeOfImage;
#if DLSSIV_DEVLOG                       // the name only: the range above and the unprove below are load-bearing;
        char path[MAX_PATH] = "";       // a release build makes no MAX_PATH buffer and asks Windows for no module file name.
        GetModuleFileNameA(m, path, MAX_PATH);
        const char* const leaf = strrchr(path, '\\');
        strncpy_s(cgaModName, sizeof(cgaModName), leaf ? leaf + 1 : path, _TRUNCATE);
#endif
        HdrCgaUnprove("the copy now comes from another module");
        if (telemetryOn) LogHealth("  hdr absorb: a point copy of the whole back buffer into a %ux%u format %u render target, from %s: watching for the draw that samples it", d.Width, d.Height, (unsigned)d.Format, cgaModName);
    }
    cgaSawPass = true;
    if (cgaDst) cgaDst->Release();
    cgaDst = dst;
    cgaDst->AddRef();
    hdrCgaArmed = true;
}

// The draw, from hkDrawPrim and hkDrawPrimUP while armed, the tracked target being the back buffer. true = withhold it.
// The reads below come from the device thread's shadow (dt_device_sync.h) and drain nothing; c44 is a drain, read on
// compared frames only.
static bool HdrCgaOnDraw(IDirect3DDevice9* dev, void* ret)
{
    const BYTE* const a = (const BYTE*)ret;
    if (a < cgaModLo || a >= cgaModHi) return false;         // someone else's draw; the arm stands
    hdrCgaArmed = false;                                     // one draw per arm: the module's first into the back buffer
    IDirect3DBaseTexture9* t0 = nullptr;
    IDirect3DTexture9* cont = nullptr;
    dev->GetTexture(0, &t0);
    const bool samplesTheCopy = t0 && cgaDst && SUCCEEDED(cgaDst->GetContainer(__uuidof(IDirect3DTexture9), (void**)&cont)) && cont
                             && (IDirect3DBaseTexture9*)cont == t0;
    if (t0) t0->Release();
    if (!samplesTheCopy) { if (cont) cont->Release(); return false; }

        // Nothing that could leave a pixel unwritten, or write it differently into another target.
    DWORD v = 0; bool st = true;
    dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &v);  st = st && v == 0;
    dev->GetRenderState(D3DRS_COLORWRITEENABLE, &v);  st = st && (v & 0xF) == 0xF;
    dev->GetRenderState(D3DRS_ALPHATESTENABLE, &v);   st = st && v == 0;
    dev->GetRenderState(D3DRS_SCISSORTESTENABLE, &v); st = st && v == 0;
    dev->GetRenderState(D3DRS_SRGBWRITEENABLE, &v);   st = st && v == 0;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &v); st = st && v == 0;
    IDirect3DSurface9* ds = nullptr;
    dev->GetDepthStencilSurface(&ds);
    if (ds)
    {
        ds->Release();
        dev->GetRenderState(D3DRS_ZENABLE, &v);       st = st && v == D3DZB_FALSE;
        dev->GetRenderState(D3DRS_STENCILENABLE, &v); st = st && v == 0;
    }
    D3DVIEWPORT9 vp = {};
    dev->GetViewport(&vp);
    st = st && vp.X == 0 && vp.Y == 0 && vp.Width == rsOutW && vp.Height == rsOutH;
    if (!st) { cont->Release(); return false; }

    IDirect3DPixelShader9* ps = nullptr;
    dev->GetPixelShader(&ps);
    if (!ps) { cont->Release(); return false; }
    const bool proved = ps == cgaProvedPS;
        // The proved shader names its curve, withheld or not: the encode's model of console gamma takes it from here instead of
    // the probe's estimate. After the player switches curves the new shader proves on its own; while a proved shader exists
    // and the module draws another, HdrCgaFrameEnd schedules checks at the unproved cadence (120 frames) and ends the probe's
    // teach window.
    if (proved) hdrCgaCurve = (int)cgaConst[0];
    else if (cgaProvedPS) cgaSawUnproved = true;

    if (proved && HdrCgaWithholds() && !cgaProofDue && cgaHold == 0 && cgaEncPS && cgaBlitPS)
    {
        ps->Release();
        if (cgaTex) cgaTex->Release();
        cgaTex = cont;                                       // kept until HdrCgaFrameEnd
        hdrCgaHeld = true;
        return true;
    }

    cgaRan = true;
    if (cgaProofDue && cgaProofPS && !cgaPending)
    {
        if (cgaTex) cgaTex->Release();
        cgaTex = cont;                                       // the compare samples it at the mod's EndScene
        if (cgaIssuedPS) cgaIssuedPS->Release();
        cgaIssuedPS = ps;                                    // kept until the analysis
        dev->GetPixelShaderConstantF(44, cgaC44, 1);         // exactly what FusionFix's shader is about to read
    }
    else { cont->Release(); ps->Release(); }
    return false;
}

// The remedy's draw: what FusionFix's withheld draw would have done, T through the curve into the back buffer. Runs inside
// whichever hook found the back buffer wanted; every state goes back as found.
static void HdrCgaBlit(IDirect3DDevice9* dev)
{
    RsOwnCall rsOwnGuard;
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    c[0][0] = (float)rsOutW; c[0][1] = (float)rsOutH; c[0][2] = 1.0f / (float)rsOutW; c[0][3] = 1.0f / (float)rsOutH;
    memcpy(c[7], cgaConst, sizeof(cgaConst));
    const bool wasOwn = ivtInOwnDraw;
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);
    DWORD oldSrgb0 = 0;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb0);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    const D3DVIEWPORT9 vp = { 0, 0, rsOutW, rsOutH, 0.0f, 1.0f };
    dev->SetRenderTarget(0, backbufferSurf);
    dev->SetViewport(&vp);
    dev->SetTexture(0, cgaTex);
    dev->SetPixelShader(cgaBlitPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb0);
    ivtInOwnDraw = wasOwn;
    IvtRestoreState(dev, &o);
}

// The debt stands until it is really paid: on the path where it cannot draw (a Reset between the withheld draw and the
// guard that wants the back buffer), HdrCgaFrameEnd forces it clear at the frame boundary and counts that separately.
static void HdrCgaHeal(IDirect3DDevice9* dev, int why)
{
    if (!hdrCgaHeld) return;
    if (!dev || !cgaTex || !cgaBlitPS || !backbufferSurf) return;   // still owed; the frame end settles it
    hdrCgaHeld = false;
    if (++cgaHealStreak >= 4) { cgaHealStreak = 0; cgaHold = 600; }
    HdrCgaBlit(dev);
}

static void HdrCgaAnalyse(const float* px)   // 256 texels of four errors, in half-float steps
{
        // fail[] is the verdict. worst[] is for the log line alone, fenced statement by statement.
    int fail[4] = { 0, 0, 0, 0 };
    DEVSTAT(float worst[4] = { 0, 0, 0, 0 };)
    for (int i = 0; i < 256; i++)
        for (int k = 0; k < 4; k++)
        {
            const float e = px[i * 4 + k];
            if (!(e <= kCgaTol)) fail[k]++;                  // a NaN is a failure
            DEVSTAT(if (e > worst[k] || e != e) worst[k] = (e != e) ? 9999.0f : e;)
        }
    int win = -1, fits = 0;
    for (int k = 0; k < 4; k++) if (fail[k] == 0) { win = k; fits++; }
    bool decisive = fits == 1;
    for (int k = 0; decisive && k < 4; k++) if (k != win && fail[k] < kCgaOthersMustFail) decisive = false;

    const char* verdict;
    char buf[160];
    DEVSTAT(static int saidKind = -1;)                      // 0 proved or standing, 1 fits and counting, 2 fits nothing, 3 undecided.
                                                            // The suppression rule's memory, read by the log line only.
    int kind;
    if (decisive)
    {
        if (cgaIssuedPS == cgaProvedPS && cgaProvedPS)
        {
            const int have = ((int)cgaConst[0] - 1) * 2 + (int)cgaConst[1];
            if (have == win) { cgaConst[2] = cgaC44[0]; cgaConst[3] = cgaC44[1]; verdict = "still exact, the proof stands"; kind = 0; }
            else { HdrCgaUnprove("the proved shader now fits another candidate"); verdict = "DISAGREES with what was proved"; kind = 2; }
        }
        else
        {
            if (cgaCand == win && cgaCandPS == cgaIssuedPS) cgaAgree++;
            else { cgaCand = win; cgaCandPS = cgaIssuedPS; cgaAgree = 1; }
            if (cgaAgree >= kCgaAgreeNeeded)
            {
                HdrCgaUnprove("another pixel shader has proved");
                cgaProvedPS = cgaIssuedPS; cgaProvedPS->AddRef();
                cgaConst[0] = win < 2 ? 1.0f : 2.0f; cgaConst[1] = (win & 1) ? 1.0f : 0.0f;
                cgaConst[2] = cgaC44[0]; cgaConst[3] = cgaC44[1];
                verdict = ivtCfg.hdr_cg_absorb != 2 ? "PROVED: from here FusionFix's draw is withheld and the encode applies this"
                                                    : "PROVED (hdr_cg_absorb=2: compared only, nothing is withheld)";
                kind = 0; DEVSTAT(saidKind = -1;)          // a proof is always said
            }
            else { sprintf_s(buf, "fits, %d of %d agreeing checks", cgaAgree, kCgaAgreeNeeded); verdict = buf; kind = 1; }
        }
    }
    else if (fits == 0)
    {
        if (cgaIssuedPS == cgaProvedPS && cgaProvedPS) HdrCgaUnprove("a compared frame fitted no candidate");
        cgaCand = -1; cgaAgree = 0; cgaCandPS = nullptr;
        verdict = "NO candidate fits: not a shader this build knows, or something else wrote the frame in between";
        kind = 2;
    }
    else { verdict = "undecided: more than one candidate fits, or the others do not fail on enough pixels (a dark or flat frame); nothing changes"; kind = 3; }

        // Logged: every change of kind, every step of the run-up, and one standing compare in eight (about every eighty seconds).
    // cgaProofN is the proof's sample-grid cursor, not a log counter, and stays in every build.
#if DLSSIV_DEVLOG
    const bool say = kind != saidKind || kind == 1 || (kind == 0 && (cgaProofN % 8u) == 0u);
    saidKind = kind;
    if (telemetryOn && say)
        LogHealth("  hdr absorb proof #%u: FusionFix's draw from %s (pixel shader %p, its c44 %.0f x %.0f), its real output against four candidates computed from its input,"
                  " 256 pixels, worst error in half-float steps and pixels beyond %.2f: Xbox %.2f/%d, Xbox+dither %.2f/%d, PS3 %.2f/%d, PS3+dither %.2f/%d. %s%s%s",
                  cgaProofN, cgaModName, (void*)cgaIssuedPS, cgaC44[0], cgaC44[1], (double)kCgaTol,
                  worst[0], fail[0], worst[1], fail[1], worst[2], fail[2], worst[3], fail[3],
                  decisive ? kCgaCandName[win] : "", decisive ? ": " : "", verdict);
#endif
    if (cgaIssuedPS) { cgaIssuedPS->Release(); cgaIssuedPS = nullptr; }
}

// The compare, issued at the tail of the encode on a frame FusionFix's pass ran untouched:
// hdrTex holds its output (the encode's own copy out of the back buffer), cgaTex its input.
static void HdrCgaIssueProof(IDirect3DDevice9* dev)
{
    IvtSavedState o;
    IvtSaveState(dev, &o);
    float c[9][4] = {};
    c[0][0] = (float)rsOutW; c[0][1] = (float)rsOutH; c[0][2] = 1.0f / (float)rsOutW; c[0][3] = 1.0f / (float)rsOutH;
    // a different pixel of each grid cell every check: 37 and 23 walk a 240 by 135 cell without repeating soon
    c[1][0] = (float)((cgaProofN * 37u) % ((rsOutW / 16u) ? rsOutW / 16u : 1u));
    c[1][1] = (float)((cgaProofN * 23u) % ((rsOutH / 16u) ? rsOutH / 16u : 1u));
    c[7][2] = cgaC44[0]; c[7][3] = cgaC44[1];
    ivtInOwnDraw = true;
    IvtSetupPass(dev, &o, c);                                // point, clamp and no mips on samplers 0..2
    DWORD oldSrgb0 = 0, oldSrgb1 = 0; IDirect3DBaseTexture9* oldTex1 = nullptr;
    dev->GetSamplerState(0, D3DSAMP_SRGBTEXTURE, &oldSrgb0); dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->GetTexture(1, &oldTex1); dev->GetSamplerState(1, D3DSAMP_SRGBTEXTURE, &oldSrgb1);
    dev->SetTexture(1, cgaTex); dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, FALSE);
    dev->SetRenderTarget(0, cgaProofSurf);
    const D3DVIEWPORT9 vp = { 0, 0, 256, 1, 0.0f, 1.0f };
    dev->SetViewport(&vp);
    dev->SetTexture(0, hdrTex);
    dev->SetPixelShader(cgaProofPS);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, kIvtQuad, sizeof(kIvtQuad[0]));
    dev->SetTexture(1, oldTex1); if (oldTex1) oldTex1->Release();
    dev->SetSamplerState(1, D3DSAMP_SRGBTEXTURE, oldSrgb1);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, oldSrgb0);
    ivtInOwnDraw = false;
    IvtRestoreState(dev, &o);
        // The budget is spent only by a check that really went out: a refused GetRenderTargetData must not use up the
    // 600-frame re-check interval silently.
    if (SUCCEEDED(dev->GetRenderTargetData(cgaProofSurf, cgaProofSys)))
    {
        cgaLastProof = hdrFramesSeen; cgaPending = true; cgaIssuedAt = hdrFramesSeen; cgaProofN++;
    }
    else
    {
        if (cgaIssuedPS) { cgaIssuedPS->Release(); cgaIssuedPS = nullptr; }
    }
}

// The tail of HdrEncode, inside its RsOwnCall. `held` is what the encode entered with.
static void HdrCgaAfterEncode(IDirect3DDevice9* dev, bool held)
{
    if (held) { hdrCgaHeld = false; cgaHealStreak = 0; }     // a frame absorbed whole ends any run of remedies
    if (cgaPending && hdrFramesSeen - cgaIssuedAt >= 3)
    {
        D3DLOCKED_RECT lr = {};
        if (SUCCEEDED(cgaProofSys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { HdrCgaAnalyse((const float*)lr.pBits); cgaProofSys->UnlockRect(); }
        else { if (cgaIssuedPS) { cgaIssuedPS->Release(); cgaIssuedPS = nullptr; } }   // the other silent drop
        cgaPending = false;
    }
        // A due compare that cannot be taken must not hold the absorb off for ever (while it is due nothing is withheld). After
    // four lost in a row the re-check is given up: only a failed check weakens a proof.
    const bool issued = !held && cgaProofDue && cgaRan && cgaTex && cgaIssuedPS && cgaProofPS && !cgaPending;
    if (issued) { HdrCgaIssueProof(dev); cgaProofLostRun = 0; }
        // Counted when a re-check is given up, not on every frame it was owed (menus and loading screens run no pass).
    else if (cgaProofDue && ++cgaProofLostRun >= 4)
    {
        cgaProofLostRun = 0; cgaLastProof = hdrFramesSeen;
    }

}

// The frame boundary, in hkEndScene after the encode: a withheld draw nobody absorbed is drawn after all, this frame's
// borrowed things go back, and the next frame is decided from settled state.
static void HdrCgaFrameEnd(IDirect3DDevice9* dev)
{
    if (hdrCgaHeld) HdrCgaHeal(dev, CGA_HEAL_NOENCODE);
    if (hdrCgaHeld) { hdrCgaHeld = false; }   // it could not be drawn at all: this frame lost the curve
        // A flag the arm sets, cleared here (hdrCgaArmed is already cleared by the draw that consumes the arm).
    // The pass stopped: console gamma switched off in FusionFix's menu stops its pass. Sixty frames in a row without it, while
    // the absorb wants it, end the probe's teach window (a frame or two without it is not a switch-off).
    cgaNoPassRun = (hdrCgaWant && !cgaSawPass) ? cgaNoPassRun + 1 : 0;
    if (cgaNoPassRun == kCgaNoPassEnds) hdrCgaTeachEnd = true;
    const bool sawUnproved = cgaSawUnproved;
    if (sawUnproved) hdrCgaTeachEnd = true;
    cgaSawUnproved = false;
    cgaSawPass = false;
    hdrCgaArmed = false; cgaRan = false;
    hdrCgaCurve = 0;                     // read by this frame's encode, which has run: the next frame names its own
    if (cgaDst) { cgaDst->Release(); cgaDst = nullptr; }
    if (cgaTex) { cgaTex->Release(); cgaTex = nullptr; }
    if (cgaHold > 0) { cgaHold--; }
        // `want` ignores the sharpen: it runs before FusionFix's copy, so the proof compares like with like. If the EndScene
    // backstop meets a withheld draw, it draws the curve first and takes the frame out of the proof.
    const bool want = ivtCfg.hdr_cg_absorb != 0 && ivtCfg.hdr && hdrEngaged && hdrColorSpaceDone && hdrSurf != nullptr
                   && IVT_HDR_VIEW == 0;
    if (!want)
    {
        if (hdrCgaWant || cgaProvedPS || cgaEncPS || cgaIssuedPS) HdrCgaRelease();   // the key went off, or HDR did: start over when it returns
        return;
    }
    hdrCgaWant  = HdrCgaEnsure(dev);
    cgaProofDue = hdrCgaWant && !cgaPending && (hdrFramesSeen - cgaLastProof >= ((cgaProvedPS && !sawUnproved) ? kCgaProofEveryProved : kCgaProofEveryUnproved));
}
// ---- end of the absorb ----
