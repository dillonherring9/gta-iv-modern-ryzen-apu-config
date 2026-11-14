// The dither pass's second draw: what the first draw changed, put into the scene buffer itself.
// The first draw's target is never cleared, so this draw asks the mark first with the same tap (no mark: discarded after one
// fetch, nearly every pixel). Only where the first draw wrote this frame is its target read, and only a changed pixel is
// written. RGB alone: the scene buffer's alpha stays (the caller sets the write mask).
//   s0  the first draw's target; POINT
//   s1  the third G-buffer target, LINEAR, as in the first draw: alpha is the mark
//   c0  xy = one texel
sampler2D sHeld : register(s0);
sampler2D sMask : register(s1);
float4    gDed  : register(c0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float2 uv = (vpos + 0.5) * gDed.xy;
    const float mark = tex2Dlod(sMask, float4(uv + 0.5 * gDed.xy, 0.0, 0.0)).a;
    float4 c = float4(0.0, 0.0, 0.0, 0.0);
    [branch] if (mark > 0.0) c = tex2Dlod(sHeld, float4(uv, 0.0, 0.0));
    clip(c.a - 0.5);
    return float4(c.rgb, 0.0);
}
