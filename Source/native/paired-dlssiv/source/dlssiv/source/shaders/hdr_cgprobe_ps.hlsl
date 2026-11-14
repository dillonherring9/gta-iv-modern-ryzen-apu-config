// The console gamma probe: 256 pixels on a 16x16 grid into a 256x1 target, read back three frames later. Each texel compares
// the frame against the composite rebuilt from the same inputs, with each curve and without one. The CPU
// (hdr_encode.h, HdrCgProbeAnalyse) accumulates mid-range samples and picks the assumption that fits.
//   R  the frame's luminance, decoded                        (1 = white)
//   G  the rebuild's luminance with the Xbox 360 curve        (decoded)
//   B  the rebuild's luminance without any curve              (decoded)
//   A  the rebuild's luminance with the PS3 curve             (decoded)

#include "hdr_common.hlsli"

sampler2D sColor : register(s0);
sampler2D sScene : register(s1);
sampler2D sLum   : register(s2);
sampler2D sBloom : register(s3);
sampler2D sLut   : register(s4);
sampler2D sXbox  : register(s9);
float4    gHdr   : register(c0);
float4    gGame0 : register(c3);
float4    gGame1 : register(c4);
float4    gGame2 : register(c5);
float4    gGame3 : register(c6);
float4    gFlags : register(c8);

float4 main(float2 vpos : VPOS) : COLOR
{
    const float  i   = floor(vpos.x);
    const float  col = fmod(i, 16.0);
    const float  row = floor(i / 16.0);
    const float4 uv  = float4((col + 0.5) / 16.0, (row + 0.5) / 16.0, 0.0, 0.0);
    const float3 s   = tex2Dlod(sColor, uv).rgb;
    const float3 u   = max(tex2Dlod(sScene, uv).rgb, 0.0);
    const float3 b   = max(tex2Dlod(sBloom, uv).rgb, 0.0);
    const float  L   = tex2Dlod(sLum, float4(0.5, 0.5, 0.0, 0.0)).r;
    const float3 g   = GtaComposite(u, b, L, gGame0, gGame1, gGame2, gGame3);
    const float3 on  = GtaSdr(g, sLut, sXbox, gGame3, float4(1.0, gFlags.y, 0.0, 0.0));
    const float3 off = GtaSdr(g, sLut, sXbox, gGame3, float4(0.0, gFlags.y, 0.0, 0.0));
    const float3 ps3 = GtaSdr(g, sLut, sXbox, gGame3, float4(2.0, gFlags.y, 0.0, 0.0));
    return float4(Luma709(HdrDecode(s, gHdr.z)), Luma709(HdrDecode(on, gHdr.z)), Luma709(HdrDecode(off, gHdr.z)),
                  Luma709(HdrDecode(ps3, gHdr.z)));
}
