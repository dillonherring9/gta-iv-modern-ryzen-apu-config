// The LUT profile pass: 256 pixels, once per LUT or console gamma setting: the LUT's grey-axis response through the same
// lookup and console gamma the rebuild uses, against a PQ-uniform input axis.
//   x   linear input in white units (FusionFix: 1.0 = 100 nits), from the PQ code (vpos.x + 0.5) / 256, so the samples sit
//       where the LUT's own texels sit
//   g   the composite output that produces it: FusionFix decodes with 2.2 before the LUT, so g = x ^ (1 / 2.2)
//   T   the resulting output luminance, console gamma applied, decoded
// Output: R = T(x), G = x. HdrLutProfileAnalyse (hdr_encode.h) finds the pivot and slope; the encode samples T on s10 at
// PqEncode(x * 0.01).

#include "hdr_common.hlsli"

sampler2D sLut   : register(s4);
sampler2D sXbox  : register(s9);
float4    gHdr   : register(c0);
float4    gGame3 : register(c6);
float4    gFlags : register(c8);

float4 main(float2 vpos : VPOS) : COLOR
{
    const float pq = (vpos.x + 0.5) / 256.0;
    const float x  = PqDecode1(pq) * 100.0;
    const float3 g = pow(x, 1.0 / 2.2).xxx;
    const float3 sdr = GtaSdr(g, sLut, sXbox, gGame3, gFlags);
    return float4(Luma709(HdrDecode(sdr, gHdr.z)), x, 0.0, 1.0);
}
