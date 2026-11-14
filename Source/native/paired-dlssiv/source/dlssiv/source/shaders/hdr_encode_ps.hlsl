// HDR display encode: the game's finished, graded frame re-encoded as linear scRGB for an FP16 swapchain, with highlight
// restoration from the game's own pre-tonemap data. The maths is in hdr_common.hlsli.
// scRGB is linear BT.709 with 1.0 = 80 nits, so SDR white leaves at paper white. No gamut matrix, no PQ, no dither (FP16
// carries the encode without banding).
// UV comes from VPOS: the shared full-screen vertex shader outputs position only. Every sampler is still bound every frame
// (the debug shader fetches unconditionally); only this shader's fetches of scene, bloom and luminance are conditional.

#include "hdr_common.hlsli"

sampler2D sColor : register(s0);   // the finished frame, copied out
sampler2D sScene : register(s1);   // the pre-tonemap scene (the composite's HDRSampler)
sampler2D sLum   : register(s2);   // the 1x1 adapted luminance (AdapLumSampler)
sampler2D sBloom : register(s3);   // the bloom (BloomSampler)
sampler2D sLut   : register(s4);   // FusionFix's tone-map LUT, when its tone mapping is on
sampler2D sXbox  : register(s9);   // FusionFix's console gamma table, 256x1
sampler2D sProf  : register(s10);  // the LUT profile, 256x1
sampler2D sPreHud: register(s11);  // the frame before the HUD drew (gHdr2.y > 0)
float4    gHdr   : register(c0);
float4    gHdr2  : register(c1);
float4    gHdr3  : register(c2);
float4    gGame0 : register(c3);
float4    gGame1 : register(c4);
float4    gGame2 : register(c5);
float4    gGame3 : register(c6);
float4    gFlags : register(c8);
#ifdef HDR_CGA
// hdr_encode_cga_ps.hlsl defines this: the variant for frames whose console gamma draw was withheld (hdr_cga.h).
#include "hdr_cga.hlsli"
// c7: x curve, y dither, zw FusionFix's c44.xy. Other shaders declare c7 too; it is safe because c7 is inside the nine rows
// IvtSaveState and IvtRestoreState keep, and the encode reads it as gCga only on a frame the absorb held (never under
// hdr_view). Two passes wanting c7 for different things in one frame is the fault to prevent.
float4    gCga   : register(c7);
#endif

#ifdef HDR_G10
// G10, the sharpening inside the encode: hdr_encode_g10_ps.hlsl and hdr_encode_cga_g10_ps.hlsl define this and include this
// file; without it the shipping header is unchanged. On these frames the mod's sharpen pass (ShpSharpen) does not run: RCAS
// runs here on the sampled frame, so the HUD, menus and FusionFix's overlays are sharpened too, and one full-screen copy and
// pass go. The mask stays exact: HdrUiCoverage compares the unsharpened sample (sM) with the pre-HUD copy, taken unsharpened.
// Under the absorb, RCAS runs on FusionFix's input before its curve.
// The constants use the two gHdr3 lanes only the debug views read (this encode never draws under a view): gHdr3.y the
// ceiling as the CPU computes it (ShpCeiling, DLSS-IV.cpp; computing it here drifted from the CPU's powf), gHdr3.z AMD's
// attenuation. The arithmetic is rcas_ps.hlsl's, line for line, with gRcas.x and gRcas.y replaced by those two.
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))
float3 HdrG10Rcas(float3 eIn, float2 uv)
{
    const float  att  = gHdr3.z;
    const float  top  = gHdr3.y;
    const float2 inv  = gHdr2.zw;
    const float3 b = max(tex2D(sColor, uv + float2( 0.0, -1.0) * inv).rgb, 0.0);
    const float3 d = max(tex2D(sColor, uv + float2(-1.0,  0.0) * inv).rgb, 0.0);
    const float3 e = max(eIn, 0.0);
    const float3 f = max(tex2D(sColor, uv + float2( 1.0,  0.0) * inv).rgb, 0.0);
    const float3 h = max(tex2D(sColor, uv + float2( 0.0,  1.0) * inv).rgb, 0.0);
    const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    const float mxL = max(max(max(bL, dL), max(eL, fL)), hL);
    const float mnL = min(min(min(bL, dL), min(eL, fL)), hL);
    nz = saturate(abs(nz) / max(mxL - mnL, 1e-6));
    nz = -0.5 * nz + 1.0;
    const float3 mn4 = min(min(b, d), min(f, h));
    const float3 mx4 = max(max(b, d), max(f, h));
    const float3 hitMin = min(mn4, e)        / max(4.0 * mx4,        1e-6);
    const float3 hitMax = (top - max(mx4, e)) / min(4.0 * mn4 - 4.0 * top, -1e-6);
    const float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT,
                     min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * att;
    lobe *= nz;
    const float rcpL = 1.0 / (4.0 * lobe + 1.0);
    return (lobe * (b + d + f + h) + e) * rcpL;
}
#define HDR_MASK_S sM
#else
#define HDR_MASK_S s
#endif

#ifdef HDR_FGOV
// Frame generation's alone (HDR_FGOV, the second-target encode of a frame with a 3D overlay ahead of the interface, the phone):
// the frame ahead of the overlay, frame generation's own copy (source/fg/fg_hud.h, FgHudOverlayCopy). sPreHud stays the
// encode's own copy.
sampler2D sPreFg : register(s5);
// The overlay's rect in back-buffer pixels (x0, y0, x1, y1: the union of the viewports of its draws). A row beyond the nine
// the encode's bracket keeps: fg_hud.h saves and restores it round the draw.
float4    gFgOv  : register(c9);
#define HDR_FGHUD_PRE sPreFg
#else
#define HDR_FGHUD_PRE sPreHud
#endif

#if defined(HDR_G10) && defined(HDR_FGMRT)
// For frame generation's HUD-less picture below (HDR_FGMRT): HdrG10Rcas's arithmetic on the frame before the interface, by
// explicit LOD (it runs inside a branch). HDR_FGHUD_PRE is the copy read: sPreHud, or under HDR_FGOV the frame ahead of the overlay.
// Its first two lines are spelled apart from HdrG10Rcas's on purpose: tools\test-hdr-sharpen\build.ps1 patches each by its text.
float3 HdrFgHudRcas(float3 eIn, float2 uv)
{
    const float att = gHdr3.z;
    const float top = gHdr3.y;
    const float2 inv  = gHdr2.zw;
    const float3 b = max(tex2Dlod(HDR_FGHUD_PRE, float4(uv + float2( 0.0, -1.0) * inv, 0.0, 0.0)).rgb, 0.0);
    const float3 d = max(tex2Dlod(HDR_FGHUD_PRE, float4(uv + float2(-1.0,  0.0) * inv, 0.0, 0.0)).rgb, 0.0);
    const float3 e = max(eIn, 0.0);
    const float3 f = max(tex2Dlod(HDR_FGHUD_PRE, float4(uv + float2( 1.0,  0.0) * inv, 0.0, 0.0)).rgb, 0.0);
    const float3 h = max(tex2Dlod(HDR_FGHUD_PRE, float4(uv + float2( 0.0,  1.0) * inv, 0.0, 0.0)).rgb, 0.0);
    const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    const float mxL = max(max(max(bL, dL), max(eL, fL)), hL);
    const float mnL = min(min(min(bL, dL), min(eL, fL)), hL);
    nz = saturate(abs(nz) / max(mxL - mnL, 1e-6));
    nz = -0.5 * nz + 1.0;
    const float3 mn4 = min(min(b, d), min(f, h));
    const float3 mx4 = max(max(b, d), max(f, h));
    const float3 hitMin = min(mn4, e)        / max(4.0 * mx4,        1e-6);
    const float3 hitMax = (top - max(mx4, e)) / min(4.0 * mn4 - 4.0 * top, -1e-6);
    const float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT,
                     min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * att;
    lobe *= nz;
    const float rcpL = 1.0 / (4.0 * lobe + 1.0);
    return (lobe * (b + d + f + h) + e) * rcpL;
}
#endif

#ifdef HDR_FGMRT
// The real encode with frame generation's HUD-less picture as its second output (source/fg/fg_hud.h). The four files
// hdr_fgmrt_*.hlsl under source/fg/shaders define this and include this file; without the define nothing of it exists and
// the shipping headers are unchanged (tools\compile-shaders.ps1 -Verify).
// COLOR0 is the real encode, from the same lines as the shipping shader. COLOR1 is THE HUD-LESS PICTURE: AMD's interpolation
// finds the interface by the difference of two pictures (ffx_frameinterpolation_inpainting.h), so where the interface
// changed nothing COLOR1 is COLOR0's own value, equal by construction; where it did, it is the frame before the interface
// (s11) through the same sharpening, curve and HdrShip, with no UI coverage and UI level 1. "Changed" is HdrUiCoverage above
// zero, by difference. No HUD-less picture ever stands in the back buffer.
struct HdrFgMrtOut { float4 c0 : COLOR0; float4 c1 : COLOR1; };
HdrFgMrtOut main(float2 vpos : VPOS)
#else
float4 main(float2 vpos : VPOS) : COLOR
#endif
{
    const float2 uv  = (vpos + 0.5) * gHdr2.zw;
#ifdef HDR_G10
    const float3 e0  = tex2D(sColor, uv).rgb;                       // the unsharpened sample: the HUD mask compares this
  #ifdef HDR_CGA
    const float3 s   = CgaCurve(HdrG10Rcas(e0, uv), uv, gCga);      // sharpened on FusionFix's input, then its curve
    const float3 sM  = CgaCurve(e0, uv, gCga);
  #else
    const float3 s   = HdrG10Rcas(e0, uv);
    const float3 sM  = e0;
  #endif
#elif defined(HDR_CGA)
    const float3 s   = CgaCurve(tex2D(sColor, uv).rgb, uv, gCga);   // sColor is FusionFix's input on these frames
#else
    const float3 s   = tex2D(sColor, uv).rgb;
#endif

        // The scene and the bloom are fetched only when something reads them: source 1 with restoration on. Both tests are on
    // constants, so the branch is uniform. The saving is traffic (sScene is a full-screen A16B16G16R16F), not instructions.
    // tex2Dlod, since a gradient instruction may not sit inside dynamic flow control in ps_3_0; LOD 0 reads the same texel under
    // the point, no-mip sampling both setup paths set (IvtSetupPass, HdrBindExtra).
    float3 u = float3(0.0, 0.0, 0.0);
    float3 b = float3(0.0, 0.0, 0.0);
    float  L = 0.0;
    [branch] if (gHdr.w > 0.0 && gHdr3.w > 0.5)
    {
        u = max(tex2Dlod(sScene, float4(uv, 0.0, 0.0)).rgb, 0.0);
        b = max(tex2Dlod(sBloom, float4(uv, 0.0, 0.0)).rgb, 0.0);
        L = tex2Dlod(sLum, float4(0.5, 0.5, 0.0, 0.0)).r;
    }
    const float  uiOn  = gHdr2.y;                                   // 0 = no mask this frame; negative = a menu is up
        // Unconditional on purpose: branching this fetch on uiOn > 0 costs slots (it forces texldl) and the branch is taken every
    // frame at the shipped hdr_ui=0 with a HUD on screen, so it would only save on configurations that do not ship.
    const float3 pre   = tex2D(sPreHud, uv).rgb;
    const float  uiCov = HdrUiCoverage(HDR_MASK_S, pre, uiOn, sXbox, gFlags);   // s, or under G10 the unsharpened sM
    const float3 lin = HdrShip(s, u, b, L, sLut, sXbox, sProf, gHdr, gHdr2, gHdr3, gGame0, gGame1, gGame2, gGame3, gFlags,
                               uiCov, abs(uiOn));
#ifdef HDR_FGMRT
    HdrFgMrtOut o;
    o.c0 = float4(lin * (gHdr.x / 80.0), 1.0);
    o.c1 = o.c0;                                                    // not the interface: the presented value itself, bit for bit
  #ifdef HDR_FGOV
        // A frame with a 3D overlay ahead of the interface (the phone in HDR; source/fg/fg_hud.h; hdr_fgmrt_ov_*.hlsl define HDR_FGOV).
    // COLOR0 is a release's: its mask reads the encode's own copy (sPreHud, behind the overlay), so the overlay is scene to the
    // real picture. COLOR1 is made from the frame ahead of the overlay (sPreFg), through the same sharpening, curve and HdrShip:
    //   The overlay's cover, exact: every pixel inside the overlay's rect where the two copies differ at all. Only the overlay
    //   draws between the two copy points there, and there is no dead zone, so an overlay pixel close in colour to the scene
    //   behind it is not interpolated as scene. Compared in any of the four channels: an overlay pixel whose colour equals the
    //   scene's leaves rgb equal, but a blended draw that writes alpha moves it (the back buffer's alpha is not 1 behind the
    //   composite). A draw that writes no alpha is told by its colour alone: that limit stands.
    //   The cover travels in COLOR1's alpha: 0 on the cover, 1 everywhere else. The mod's own build of the interpolation takes
    //   the presented pixel in full where it reads 0 (ffx-own/cuts/fi-cover-in-alpha.cut); another library ignores it.
    //   The 2D interface, by its usual rule: the mask above zero against the encode's own copy.
    const float4 preF4 = tex2D(sPreFg, uv);                         // the frame ahead of the overlay, all four channels
    const float3 preF = preF4.rgb;
    const bool ovCover = vpos.x >= gFgOv.x && vpos.x < gFgOv.z && vpos.y >= gFgOv.y && vpos.y < gFgOv.w
                         && any(preF4 != float4(pre, tex2D(sPreHud, uv).a));
    #define HDR_FG_PRE preF
    #define HDR_FG_ALPHA (ovCover ? 0.0 : 1.0)
    [branch] if (ovCover || HdrUiCoverage(HDR_MASK_S, pre, 1.0, sXbox, gFlags) > 0.0)
  #else
        // the text of the four encodes every other frame draws with, token for token
    #define HDR_FG_PRE pre
    #define HDR_FG_ALPHA 1.0
    [branch] if (HdrUiCoverage(HDR_MASK_S, pre, 1.0, sXbox, gFlags) > 0.0)
  #endif
    {
  #ifdef HDR_G10
        const float3 eH = HdrFgHudRcas(HDR_FG_PRE, uv);
  #else
        const float3 eH = HDR_FG_PRE;
  #endif
  #ifdef HDR_CGA
        const float3 sH = CgaCurve(eH, uv, gCga);                   // the absorb's own curve and dither, at this pixel
  #else
        const float3 sH = ConsoleGammaExact(eH, sXbox, gFlags);     // the curve FusionFix drew into the frame (none when it draws none)
  #endif
        o.c1 = float4(HdrShip(sH, u, b, L, sLut, sXbox, sProf, gHdr, gHdr2, gHdr3, gGame0, gGame1, gGame2, gGame3, gFlags, 0.0, 1.0)
                      * (gHdr.x / 80.0), HDR_FG_ALPHA);
    }
    return o;
#else
    return float4(lin * (gHdr.x / 80.0), 1.0);
#endif
}
