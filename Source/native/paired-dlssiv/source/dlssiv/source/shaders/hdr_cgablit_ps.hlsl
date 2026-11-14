// The absorb's remedy (hdr_cga.h): FusionFix's console gamma draw was withheld for the encode, then something wanted the
// back buffer first. This is that draw: FusionFix's copy in, curve and dither applied, alpha passed through, back buffer out.

#include "hdr_cga.hlsli"

sampler2D sIn   : register(s0);   // FusionFix's own copy of the frame
float4    gSize : register(c0);   // W, H, 1/W, 1/H
float4    gCga  : register(c7);   // x curve, y dither, zw FusionFix's c44.xy

float4 main(float2 vpos : VPOS) : COLOR
{
    const float2 uv = (vpos + 0.5) * gSize.zw;
    float4 color = tex2D(sIn, uv);
    color.rgb = CgaCurve(color.rgb, uv, gCga);
    return color;
}
