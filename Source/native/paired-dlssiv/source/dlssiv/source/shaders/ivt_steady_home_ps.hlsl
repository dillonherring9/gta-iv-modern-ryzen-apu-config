// The steadied depth goes home: held values that differ from the frame's own are written back into the texture the post
// chain reads. Every pixel the resolve had nothing to say about is discarded and keeps what the game drew.
// One shader for both kinds of home (transport/steady_depth_pass.h, SdHome):
//   a colour texture (FusionFix's R32F copy of the depth): drawn as the target; only COLOR0 lands, no depth-stencil bound;
//   a depth texture (the scene's INTZ): drawn as the depth-stencil, test always, depth writes on, colour writes off; only
//   depth lands. The stencil is left as it is.
//   s0  what the resolve holds this frame (ivt_steady_ps.hlsl's target: r the depth, positive where it is to be written
//       back; g above zero where anything is held); POINT
//   c0  xy = one texel
sampler2D sHeld : register(s0);
float4    gSd   : register(c0);

void main(float2 vpos : VPOS, out float4 col : COLOR0, out float dep : DEPTH)
{
    const float2 hv = tex2Dlod(sHeld, float4((vpos + 0.5) * gSd.xy, 0.0, 0.0)).rg;
    clip((hv.g > 0.5 && hv.r > 0.0) ? 1.0 : -1.0);
    col = float4(hv.r, 0.0, 0.0, 1.0);
    dep = hv.r;
}
