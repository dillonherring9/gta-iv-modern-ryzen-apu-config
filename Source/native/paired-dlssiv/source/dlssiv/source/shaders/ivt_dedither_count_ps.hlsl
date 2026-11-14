// Is anything on screen dithered at all? The dither pass (ivt_dedither_ps.hlsl) is usually needed on a tiny share of the
// screen, or none. This is the cheap question first: drawn over a quarter-size viewport with colour writes off inside an
// occlusion query, it keeps a pixel only where the mark is set at texel (4x, 4y). While the count (a frame or two later) is
// zero the mod leaves the pass out and asks again every fourth frame.
// One texel in sixteen is enough: the game's pattern draws the even-even pixels at every dither level and marks each pixel
// it draws, so any dithered surface at least four pixels across is seen. Thinner ones gain nothing from the pass.
//   s1  the third G-buffer target (the mark in its alpha); the tap sits on a texel's centre
//   c0  xy = one texel of the mark
sampler2D sMask : register(s1);
float4    gDed  : register(c0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    clip(tex2Dlod(sMask, float4((vpos * 4.0 + 0.5) * gDed.xy, 0.0, 0.0)).a > 0.0 ? 1.0 : -1.0);
    return 0.0;
}
