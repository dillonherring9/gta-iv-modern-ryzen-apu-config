// The mask for transparent things: behind a pane a pixel shows two things and an upscaler takes one motion a pixel, so both
// take a per-pixel "lean on this frame" image (DLSS: the bias-current-colour mask; FSR: the reactive mask).
// No pane is marked. The game draws its distance fog as one full-screen quad, then its panes, particles and other blended
// things. The mod keeps the scene right after that quad (the reference) and this pass compares the finished scene with it.
// Luminance is squeezed through x / (1 + x), so a dark street counts like a bright sky. The mask is the difference times a
// strength, held to a ceiling. A pixel nothing was laid over gives exactly 0.
//   s0  the finished scene (the scene buffer at the injection), POINT
//   s1  the reference (the scene right after the fog quad), the same size, POINT
//   c0  xy = one texel of either, z = the strength, w = the ceiling
sampler2D sNow  : register(s0);
sampler2D sRef  : register(s1);
float4    gMask : register(c0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float4 uv = float4((vpos + 0.5) * gMask.xy, 0.0, 0.0);
    const float3 k = float3(0.2126, 0.7152, 0.0722);
        // held to 0..65504 first: a negative or a NaN in the scene buffer must not become a mask
    float a = clamp(dot(tex2Dlod(sNow, uv).rgb, k), 0.0, 65504.0);
    float b = clamp(dot(tex2Dlod(sRef, uv).rgb, k), 0.0, 65504.0);
    a = a / (1.0 + a);
    b = b / (1.0 + b);
    const float m = min(abs(a - b) * gMask.z, gMask.w);
    return float4(m, m, m, m);
}
