// HDR debug views: a separate shader so the shipping encode carries none of this; created only while hdr_view is set. The
// same output signal (linear scRGB, nits / 80), and the pixel under every view is the shipping maths of hdr_common.hlsli.
//   gHdr3.y  the view:
//     6  False colour by presented luminance: < 1 nit dark blue; up to paper white a grey ramp; to 1.5x paper green; to 400
//        yellow; to 600 orange; to 1000 red; above magenta; at or above 0.98 x peak white.
//    14  The UI mask: green where the mask is full (the UI level applies and the scene's restoration is withdrawn; a whole
//        green screen is a game menu); the picture, dimmed, where it is empty; a dark-red-through-orange ramp where it is
//        partial, which is a fault (orange is the worst reading). Magenta over the whole screen: no mask this frame (the
//        game's HUD is off).
//    17  The highlight map: below paper white the real shipping pixel. Above it one continuous scale, green through yellow,
//        orange and red to magenta, spaced in stops between paper white and this display's peak, emitted from 1.25x to 2.5x
//        paper white. Dark contours at 400, 600 and 1000 nits; zebra at or above 0.98 of the peak. The top 3 percent of
//        the screen is the legend, paper white to the peak with the same ticks.

#include "hdr_common.hlsli"

sampler2D sColor : register(s0);
sampler2D sScene : register(s1);
sampler2D sLum   : register(s2);
sampler2D sBloom : register(s3);
sampler2D sLut   : register(s4);
sampler2D sXbox  : register(s9);
sampler2D sProf  : register(s10);
sampler2D sPreHud: register(s11);
float4    gHdr   : register(c0);
float4    gHdr2  : register(c1);
float4    gHdr3  : register(c2);
float4    gGame0 : register(c3);
float4    gGame1 : register(c4);
float4    gGame2 : register(c5);
float4    gGame3 : register(c6);
float4    gFlags : register(c8);

float3 Out(float3 nits) { return nits / 80.0; }

// View 17's scale. t is 0 at paper white and 1 at the peak, in stops.
float3 HighlightScale(float t)
{
    const float3 c0 = float3(0.0, 0.8, 0.0);    // view 6's green
    const float3 c1 = float3(0.9, 0.9, 0.0);
    const float3 c2 = float3(1.0, 0.5, 0.0);
    const float3 c3 = float3(1.0, 0.0, 0.0);
    const float3 c4 = float3(1.0, 0.0, 1.0);
    const float  x  = saturate(t) * 4.0;
    if (x < 1.0) return lerp(c0, c1, x);
    if (x < 2.0) return lerp(c1, c2, x - 1.0);
    if (x < 3.0) return lerp(c2, c3, x - 2.0);
    return lerp(c3, c4, x - 3.0);
}

float4 main(float2 vpos : VPOS) : COLOR
{
    const float2 uv    = (vpos + 0.5) * gHdr2.zw;
    const float  view  = gHdr3.y;
    const float  paper = gHdr.x;
    const float  peak  = max(gHdr.y, gHdr.x);

        // ---- The shipping pixel, as the encode would produce it ----
    const float3 s    = tex2D(sColor, uv).rgb;
    const float3 u    = max(tex2D(sScene, uv).rgb, 0.0);
    const float3 b    = max(tex2D(sBloom, uv).rgb, 0.0);
    const float  L    = tex2Dlod(sLum, float4(0.5, 0.5, 0.0, 0.0)).r;
    const float3 lin  = HdrDecode(s, gHdr.z);                 // graded, 1 = white
    const float  uiOn  = gHdr2.y;                             // 0 = no mask this frame; negative = a menu is up
    const float3 pre   = tex2D(sPreHud, uv).rgb;
    const float  uiCov = HdrUiCoverage(s, pre, uiOn, sXbox, gFlags);
    const float3 out_ = HdrShip(s, u, b, L, sLut, sXbox, sProf, gHdr, gHdr2, gHdr3, gGame0, gGame1, gGame2, gGame3, gFlags,
                                uiCov, abs(uiOn));

    if (view > 13.5 && view < 14.5)
    {
                // The UI mask: green where the game's 2D pass moved the pixel (the UI level applies, the scene's restoration is withdrawn).
        // Magenta means no mask was made at all, a different state from an empty mask (which shows the picture at quarter brightness).
        if (uiOn == 0.0) return float4(Out(float3(0.35, 0.0, 0.35) * paper), 1.0);   // no mask this frame (a menu frame reads full, green)
                // Green is a full mask, red a partial one: a partial mask over the scene tints the picture once the UI level differs from
        // paper white, so anything red here is a fault.
        if (uiCov < 0.002) return float4(Out(lin * paper * 0.25), 1.0);
        if (uiCov > 0.98)  return float4(Out(float3(0.0, 1.0, 0.2) * paper), 1.0);
        return float4(Out(lerp(float3(0.25, 0.0, 0.0), float3(1.0, 0.15, 0.0), uiCov) * paper), 1.0);
    }

    if (view > 5.5 && view < 6.5)
    {
        const float nits = Luma709(out_) * paper;
        float3 fc;
        if      (nits < 1.0)          fc = float3(0.0, 0.0, 0.35);
        else if (nits < paper)        fc = (0.15 + 0.6 * nits / paper).xxx;
        else if (nits < 1.5 * paper)  fc = float3(0.0, 0.8, 0.0);
        else if (nits < 400.0)        fc = float3(0.9, 0.9, 0.0);
        else if (nits < 600.0)        fc = float3(1.0, 0.5, 0.0);
        else if (nits < 1000.0)       fc = float3(1.0, 0.0, 0.0);
        else                          fc = float3(1.0, 0.0, 1.0);
        if (nits >= 0.98 * peak)      fc = float3(1.0, 1.0, 1.0);
        return float4(Out(fc * paper), 1.0);
    }
        // The gradient is taken outside the branch: ps_3_0 wants screen derivatives outside per-pixel flow control.
    const bool  legend17 = uv.y < 0.03;
    const float nits17   = legend17 ? paper * pow(max(peak / paper, 1.0001), uv.x) : Luma709(out_) * paper;
    const float fw17     = max(fwidth(nits17), 1e-3);
    if (view > 16.5 && view < 17.5)
    {
        float3 fc = out_;                                                      // below paper white: the real picture, as view 0
        if (nits17 >= paper)
        {
            const float t = log2(nits17 / paper) / max(log2(peak / paper), 1e-3);
            fc = HighlightScale(t) * lerp(1.25, 2.5, saturate(t));             // emitted above paper white, so it glows
            const float d = min(abs(nits17 - 400.0), min(abs(nits17 - 600.0), abs(nits17 - 1000.0)));
            if (d < 0.75 * fw17) fc = (0.0).xxx;                               // contour lines at 400, 600 and 1000 nits
            if (nits17 >= 0.98 * peak)                                         // the zebra: at the display's ceiling
                fc = (frac((vpos.x + vpos.y) * 0.125) < 0.5) ? (2.5).xxx : (0.0).xxx;
        }
        return float4(Out(fc * paper), 1.0);
    }
    return float4(Out(out_ * paper), 1.0);
}
