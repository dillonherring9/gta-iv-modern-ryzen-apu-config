// Per-object velocity, skinned, with true previous-frame bones. velocity_skin_vs.hlsl takes the previous position from the
// current pose, which treats every limb as rigid; idle animation always plays, so that is wrong for every character.
// Why a texture: both bone sets (144 registers each) must be live in one vertex shader and the game fills the constant file
// to c237, so the previous set arrives by vertex texture fetch (proven exact by vtf_test_vs.hlsl).
// A separate shader rather than a branch: fxc may flatten a runtime `if` and pay for the twelve fetches anyway; this keeps
// the fallback the original file and selecting costs one SetVertexShader.

row_major float4x4 gWVP        : register(c8);
float4             gBone[144]  : register(c64);    // current pose, from the game
row_major float4x4 gPrevWVP    : register(c240);
float4             gDepthParams : register(c244);  // x=near y=far z=logDepth w=1/log2(far/near), from the CPU

// x = this object's row in the bone texture, in v coordinates; y = 1/width, the step from one bone register to the next
float4             gBoneFetch  : register(c245);
float4             gJitterNdc  : register(c246);    // xy = current jitter in NDC; see velocity_vs.hlsl

// Previous pose. Point sampled, no mips: a bone matrix blended with its neighbour is not a bone matrix.
sampler2D sPrevBone : register(s0);
#ifdef VEL_DAMAGE
#include "velocity_dmg.hlsli"   // the dented vehicle's variant (velocity_skin_prev_dmg_vs.hlsl): the map on s1, beside this pose on s0
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

// Columns: the three registers are the columns of the 4x3 (an orthonormal matrix reads the same either way, so this was
// settled by the picture).
float3 SkinCols(float4 p, int b)
{
    int o = b * 3;
    return float3(dot(p, gBone[o + 0]), dot(p, gBone[o + 1]), dot(p, gBone[o + 2]));
}

// Same 4x3, same column reading, from the texture: three fetches walk along the row as three registers walk along gBone.
float3 SkinPrevCols(float4 p, int b)
{
    float u = ((float)(b * 3) + 0.5) * gBoneFetch.y;
    float v = gBoneFetch.x;
    float4 r0 = tex2Dlod(sPrevBone, float4(u,                      v, 0.0, 0.0));
    float4 r1 = tex2Dlod(sPrevBone, float4(u + gBoneFetch.y,       v, 0.0, 0.0));
    float4 r2 = tex2Dlod(sPrevBone, float4(u + gBoneFetch.y * 2.0, v, 0.0, 0.0));
    return float3(dot(p, r0), dot(p, r1), dot(p, r2));
}

VSOut main(VSIn i)
{
    float4 p = float4(i.pos.xyz, 1.0);
#ifdef VEL_DAMAGE
    p.xyz = VelDamage(p.xyz);   // before both poses: the dent is the same in the frame before
#endif

        // D3DCOLOR, not UBYTE4: it arrives normalised to [0,1], so the bone numbers are those bytes scaled back up.
    float4 bi = i.idx * 255.0 + 0.5;
    float4 w  = i.wgt;

    float3 sk;
    sk  = w.x * SkinCols(p, (int)bi.x);
    sk += w.y * SkinCols(p, (int)bi.y);
    sk += w.z * SkinCols(p, (int)bi.z);
    sk += w.w * SkinCols(p, (int)bi.w);

    float3 skp;
    skp  = w.x * SkinPrevCols(p, (int)bi.x);
    skp += w.y * SkinPrevCols(p, (int)bi.y);
    skp += w.z * SkinPrevCols(p, (int)bi.z);
    skp += w.w * SkinPrevCols(p, (int)bi.w);

        // An unweighted vertex would collapse to the origin and drag a triangle across the screen. Both poses fall back together,
    // so the vertex's motion vector stays consistent.
    float wsum = w.x + w.y + w.z + w.w;
    float4 sp  = (wsum > 0.01) ? float4(sk,  1.0) : p;
    float4 spp = (wsum > 0.01) ? float4(skp, 1.0) : p;

    VSOut o;
    o.pos = mul(sp,  gWVP);
    o.cur = o.pos;
    o.cur.xy -= gJitterNdc.xy * o.pos.w;   // rasterise jittered, measure unjittered
    o.prv = mul(spp, gPrevWVP);
    o.dp  = gDepthParams;
    return o;
}
