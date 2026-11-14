// RCAS: Robust Contrast Adaptive Sharpening, the post-resolve sharpener.
//
// Ported from AMD FidelityFX Super Resolution [FSR 1] v1.20210629,
// ffx_fsr1.h, FsrRcasF, with FSR_RCAS_DENOISE and FSR_RCAS_PASSTHROUGH_ALPHA.
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Licensed under the MIT licence; see docs/THIRD-PARTY.md for the full text.
//
// WHY RCAS AND NOT CAS. RCAS solves for how much headroom a neighbourhood has
// before it clips, so by construction it cannot ring past the signal range;
// CAS uses a fixed contrast heuristic and can. RCAS is also the only one of the
// two with a noise gate, which is the mechanism for residual temporal shimmer
// on thin geometry. And CAS demands LINEAR input, where this pass runs after
// the game's tonemap on a perceptual buffer. FSR2, FSR3, FSR4 and OptiScaler
// all use RCAS for exactly this job, on exactly this kind of image.
//
//   s0  the copy of the backbuffer, post-tonemap, sRGB-encoded, output size
//   c0  x = the attenuation constant, exp2(-stops); y = THE CEILING; zw = 1/width, 1/height
//
// THE CEILING (2026-09-19, audit SHADER-L44; the one change to AMD's arithmetic). AMD's peak is the literal 1: the
// signal cannot go higher, so headroom is measured against it. Under hdr=1 this pass reads the FP16 back buffer, which
// is OPEN above white, and with the literal 1 every neighbourhood holding a tap above white had NEGATIVE headroom, the
// guard flipped the divisor and min(lobe, 0) switched the sharpening off there: street lamps, neon, the sun and the
// pixels around them were the only unsharpened part of the picture. The mod passes the ceiling in. It is exactly 1.0
// in SDR and whenever values above white cannot reach the display (ShpCeiling in DLSS-IV.cpp says when), so those
// pictures are the ones they were; otherwise it is six times paper white in this buffer's own encoding, the top of the
// night emitters' measured range and NOT above everything the game produces (headlight interiors reach 25 times
// white; ShpCeiling in DLSS-IV.cpp has the numbers). A tap above the ceiling still switches the lobe off, as before.
//
// The source is the same size as the target and sampling is POINT, so the tap
// offsets below are exact texels rather than an interpolation of them.

sampler2D sColor : register(s0);
float4    gRcas  : register(c0);

// AMD: "set at the limit of providing unnatural results for sharpening".
#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

void main(float2 vpos : VPOS, out float4 outColor : COLOR0)
{
    const float2 inv = gRcas.zw;
    const float2 uv  = (vpos + 0.5) * inv;      // VPOS is the integer position

    //    b
    //  d e f
    //    h
    const float4 eS = tex2D(sColor, uv);
    // max against zero: RCAS returns NaN on negative input, and a wide gamut or
    // a stray negative from the tonemap would poison the whole neighbourhood.
    const float3 b = max(tex2D(sColor, uv + float2( 0.0, -1.0) * inv).rgb, 0.0);
    const float3 d = max(tex2D(sColor, uv + float2(-1.0,  0.0) * inv).rgb, 0.0);
    const float3 e = max(eS.rgb, 0.0);
    const float3 f = max(tex2D(sColor, uv + float2( 1.0,  0.0) * inv).rgb, 0.0);
    const float3 h = max(tex2D(sColor, uv + float2( 0.0,  1.0) * inv).rgb, 0.0);

    // Luma times two, in AMD's weighting: 0.5*B + (0.5*R + G).
    const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    // THE NOISE GATE. The range spans all FIVE taps, the CENTRE included. A
    // secondary description of this algorithm had it as the four neighbours
    // only, which reads as plausible and is wrong: on a lone bright pixel the
    // centre is the whole signal, and excluding it makes the range collapse and
    // the gate misfire exactly where it matters most.
    float nz = 0.25 * (bL + dL + fL + hL) - eL;
    const float mxL = max(max(max(bL, dL), max(eL, fL)), hL);
    const float mnL = min(min(min(bL, dL), min(eL, fL)), hL);
    nz = saturate(abs(nz) / max(mxL - mnL, 1e-6));   // flat neighbourhood: no noise
    nz = -0.5 * nz + 1.0;

    const float3 mn4 = min(min(b, d), min(f, h));
    const float3 mx4 = max(max(b, d), max(f, h));

    // THE HEADROOM, and the two guards AMD's own version does not carry.
    // FSR1 runs RCAS on an upscaled render target, where an exactly-black or
    // exactly-white neighbourhood is vanishingly rare. This pass runs on the
    // FINAL LDR image, where both are common: night sky and shadow clip to 0,
    // headlights and the sun clip to 1. Unguarded, 4*mx4 is then 0 and
    // 4*mn4-4 is 0, and the divide produces NaN across the whole tile.
    const float3 hitMin = min(mn4, e)        / max(4.0 * mx4,        1e-6);
    const float3 hitMax = (gRcas.y - max(mx4, e)) / min(4.0 * mn4 - 4.0 * gRcas.y, -1e-6);

    // The lobe takes the max ACROSS CHANNELS before it is clamped, not per
    // channel: one channel about to clip has to restrain the other two, or the
    // sharpening pulls a saturated edge off-hue.
    const float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-FSR_RCAS_LIMIT,
                     min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * gRcas.x;
    lobe *= nz;                                  // FSR_RCAS_DENOISE

    // 4*lobe+1 lies in [0.25, 1]: lobe is clamped at or below zero above, so
    // this cannot divide by zero.
    const float rcpL = 1.0 / (4.0 * lobe + 1.0);
    // Alpha passes through untouched: the game owns whatever it means here.
    outColor = float4((lobe * (b + d + f + h) + e) * rcpL, eS.a);
}
