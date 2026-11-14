// The game's vehicle damage, applied to the position before anything else.
// GTA IV dents a vehicle in its vertex shader: every vehicle shader but the tyre's, the rims', the disc's and "basic" reads a
// damage map on its first vertex sampler and moves each vertex by up to half a metre in object (or bone) space, before the
// world, bones or camera. Without it a dented car's vectors come from the undamaged body.
// The game's own arithmetic in the game's order (gta_vehicle_paint1VS1.asm of FusionShaders' published text; VS2 the same in
// bone space): a place on the map from the vertex's direction, four reads and a bilinear by hand, three bytes in each read's
// x decoded to an offset in -1..1 per axis, times min(|pos| / BoundRadius, 1), half of it added to the position. The decode
// is whole numbers in floats below 2^24, so exact.
// Inputs: the map on the second vertex sampler (the first carries the previous-pose texture), BoundRadius in c247.x (above
// the game's and FusionFix's range), both set by the drain from the game's draw (VelPend's dmgTex and dmgRadius). The
// game's switch (b8) is the drain's: a variant with this file is bound only for a draw whose switch was on.
// Included only under VEL_DAMAGE (velocity_dmg_vs.hlsl, velocity_skin_dmg_vs.hlsl, velocity_skin_prev_dmg_vs.hlsl); the
// other three shaders compile unchanged (tools\compile-shaders.ps1 -Verify).

sampler2D sDamage : register(s1);
float4    gDamage : register(c247);   // x = the draw's BoundRadius (the game's c208.x)

// Three bytes in one float, z * 65536 + y * 256 + x, as the game takes them apart: each part truncated toward zero.
float3 VelDmgDecode(float x)
{
    const float a  = x * 1.52587891e-005;
    const float bz = trunc(a);
    const float r  = bz * -65536.0 + x;
    const float b  = r * 0.00390625;
    const float by = trunc(b);
    const float bx = by * -256.0 + r;
    return float3(bx, by, bz);
}

float3 VelDamage(float3 v)
{
    const float  len = length(v);
    const float  inv = 1.0 / (len + 9.99999975e-006);
    const float  h   = (1.0 - v.z * inv) * 0.5;
    float2       d   = v.xy * inv + 9.99999975e-006;
    d *= rsqrt(dot(d, d));
    const float2 uv  = (h * d) * 0.5 + 0.5;
    const float2 t   = uv * 126.732674;
    const float2 fa  = frac(abs(t));
    const float2 fr  = (t >= -t) ? fa : -fa;                 // the game's signed fraction (sge, then lrp on it)
    const float4 t00 = tex2Dlod(sDamage, float4(uv, 0.0, 0.0));
    const float2 o   = fr * -0.00789062493 + uv;
    const float4 t10 = tex2Dlod(sDamage, float4(o + float2(0.00789062493, 0.0), 0.0, 0.0));
    const float4 t01 = tex2Dlod(sDamage, float4(o + float2(0.0, 0.00789062493), 0.0, 0.0));
    const float4 t11 = tex2Dlod(sDamage, float4(o + 0.00789062493, 0.0, 0.0));
    const float3 d00 = VelDmgDecode(t00.x), d10 = VelDmgDecode(t10.x), d01 = VelDmgDecode(t01.x), d11 = VelDmgDecode(t11.x);
    const float2 g   = 1.0 - fr;
    float3 off = (d00 * g.x) * g.y + (fr.x * d10) * g.y;
    off = (d01 * g.x) * fr.y + off;
    off = (fr.x * d11) * fr.y + off;
    off = (off + -128.0) * 0.0078125;
    const float s = min(len * (1.0 / gDamage.x), 1.0);
    return (off * s) * 0.5 + v;
}
