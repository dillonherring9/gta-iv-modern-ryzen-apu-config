// The absorb's proof (hdr_cga.h). On a frame where FusionFix's console gamma pass ran untouched, its input (its own copy of
// the frame) and output (the back buffer, just copied out) both exist. 256 pixels on a 16x16 grid, jittered inside their
// cells per check, drawn into a 256x1 target and read back three frames later. Each texel is one pixel's worst-channel error
// under each candidate, in half-float steps (the output went through a 16-bit float target):
//   R  the Xbox 360 curve                 G  the Xbox 360 curve and the dither
//   B  the PS3 curve                      A  the PS3 curve and the dither
// HdrCgaAnalyse asks for exactly one candidate within one step on every sample while the others are further on many.
// One step, not half: the conversion into a half-float target may round to either neighbour (Vulkan allows it).
// The dither's wrap: FusionFix's phase comes from an interpolated coordinate, so near the wrap the two can differ by a whole
// 8-bit step. A pixel within 0.02 of the wrap is replaced by the one to its right (phase 0.7549 further on).

#include "hdr_cga.hlsli"

sampler2D sOut  : register(s0);   // FusionFix's output, as the encode copied it out
sampler2D sIn   : register(s1);   // FusionFix's input, its own copy of the frame
float4    gSize : register(c0);   // W, H, 1/W, 1/H
float4    gCga  : register(c7);   // zw: FusionFix's c44.xy
float4    gJit  : register(c1);   // xy: this check's offset inside each grid cell, in pixels

// One half-float step at each channel's value; below 2^-14 (the smallest normal half) the step stays 2^-24. The 1e-4 keeps
// an exact power of two, whose log2 may come back a hair low, in its own binade.
float3 HalfStep(float3 t) { return exp2(floor(log2(max(abs(t), 6.103515625e-5)) + 1e-4) - 10.0); }
// The dither's phase differs from FusionFix's by up to about 1e-3 (interpolated uv against (pixel + 0.5) / size), a few
// steps' worth near zero. So 0.004 of phase is allowed for the two dithered candidates only.
static const float kPhaseSlack = 0.004f / 255.0f;
float  Steps(float3 cand, float3 t, float3 step, float slack) { const float3 e = max(abs(cand - t) - slack, 0.0) / step; return max(max(e.r, e.g), e.b); }

float4 main(float2 vpos : VPOS) : COLOR
{
    const float i   = floor(vpos.x);
    const float col = fmod(i, 16.0);
    const float row = floor(i / 16.0);
    float2 p  = min(floor(float2(col, row) * gSize.xy / 16.0 + gJit.xy), gSize.xy - 2.0);
    float2 uv = (p + 0.5) * gSize.zw;
    const float ph = frac(dot(gCga.zw * uv, float2(0.754877666247f, 0.569840290998f)));
    if (ph < 0.02 || ph > 0.98) { p.x += 1.0; uv = (p + 0.5) * gSize.zw; }

    const float3 t = tex2Dlod(sOut, float4(uv, 0.0, 0.0)).rgb;
    const float3 r = tex2Dlod(sIn,  float4(uv, 0.0, 0.0)).rgb;
    const float  n = CgaNoise(uv, gCga.zw);
    const float3 x = CgaXbox(r);
    const float3 q = CgaPs3(r);
    const float3 h = HalfStep(t);
    return float4(Steps(x, t, h, 0.0), Steps(x + n, t, h, kPhaseSlack), Steps(q, t, h, 0.0), Steps(q + n, t, h, kPhaseSlack));
}
