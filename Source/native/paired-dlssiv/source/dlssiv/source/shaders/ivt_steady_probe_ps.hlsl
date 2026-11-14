// The steady depth's control (development builds only): inside an occlusion query with colour writes off, it counts the
// pixels of the depth texture that hold a depth at all. A small number means the texture is not a depth buffer.
//   s0  the depth the post chain reads, as this frame drew it; POINT
//   c0  xy = one texel
sampler2D sNow : register(s0);
float4    gSd  : register(c0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    clip(tex2Dlod(sNow, float4((vpos + 0.5) * gSd.xy, 0.0, 0.0)).r != 0.0 ? 1.0 : -1.0);
    return 0.0;
}
