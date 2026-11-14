// Per-object velocity, skinned vertex stage. The game skins its vehicles as well as its pedestrians; a position-only redraw
// would put hidden optional parts and doors, wheels and panels at their rest pose.
// Declaration, measured from the game:
//     POSITION      FLOAT3     offset 0
//     BLENDWEIGHT   D3DCOLOR   offset 12
//     BLENDINDICES  D3DCOLOR   offset 16
// D3DCOLOR arrives normalised to [0,1], so indices need x255; it also swizzles BGRA to RGBA, but weights and indices share
// the type, so pairing .x with .x is consistent.
// Bone matrices at c64..c207: 48 bones of three registers, a 4x3 affine transform, in object space (their translations are
// tiny), so gWVP still applies afterwards. The registers are the columns. velocity_skin_prev_vs.hlsl reads columns too.

row_major float4x4 gWVP     : register(c8);
float4             gBone[144] : register(c64);
row_major float4x4 gPrevWVP : register(c240);
float4             gDepthParams : register(c244);  // x=near y=far z=logDepth w=1/log2(far/near), from the CPU
float4             gJitterNdc   : register(c246);  // xy = current jitter in NDC; see velocity_vs.hlsl
#ifdef VEL_DAMAGE
#include "velocity_dmg.hlsli"   // the dented vehicle's variant (velocity_skin_dmg_vs.hlsl): the game's damage, in bone space
#endif

struct VSIn {
    float4 pos : POSITION;
    float4 wgt : BLENDWEIGHT;
    float4 idx : BLENDINDICES;
};
struct VSOut {
    float4 pos : POSITION;
    float4 cur : TEXCOORD0;
    float4 prv : TEXCOORD1;
    float4 dp  : TEXCOORD2;
};

// The three registers are the columns of the 4x3 (HLSL's float4x3 under default packing), so mul(v, M) is three dot products.
float3 SkinCols(float4 p, int b)
{
    int o = b * 3;
    return float3(dot(p, gBone[o + 0]), dot(p, gBone[o + 1]), dot(p, gBone[o + 2]));
}

VSOut main(VSIn i)
{
    float4 p = float4(i.pos.xyz, 1.0);
#ifdef VEL_DAMAGE
    p.xyz = VelDamage(p.xyz);   // before the bones, as the game's skinned vehicle shader does it
#endif

    // D3DCOLOR arrives in [0,1]; bone numbers are those bytes.
    float4 bi = i.idx * 255.0 + 0.5;
    float4 w  = i.wgt;

    float3 sk;
    sk  = w.x * SkinCols(p, (int)bi.x);
    sk += w.y * SkinCols(p, (int)bi.y);
    sk += w.z * SkinCols(p, (int)bi.z);
    sk += w.w * SkinCols(p, (int)bi.w);

        // An unweighted vertex would collapse to the origin and drag a triangle across the screen: fall back to unskinned.
    float wsum = w.x + w.y + w.z + w.w;
    float4 sp  = (wsum > 0.01) ? float4(sk, 1.0) : p;

    VSOut o;
    o.pos = mul(sp, gWVP);
    o.cur = o.pos;
    o.cur.xy -= gJitterNdc.xy * o.pos.w;   // rasterise jittered, measure unjittered
        // The same bones for the previous position, a different world matrix: exact rigid motion for the body, limbs treated as
    // rigid. The prev variant uses the previous frame's bones.
    o.prv = mul(sp, gPrevWVP);
    o.dp  = gDepthParams;
    return o;
}
