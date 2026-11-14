// Per-object velocity, vertex stage.
// Position-only by design: a D3D9 vertex shader declares only the inputs it uses, so this binds against whatever vertex
// declaration the game has set, and redraws the game's geometry without knowing its formats.
// gWVP at c8 is already set by the game for this draw. gPrevWVP at c240 and gDepthParams at c244 are the mod's: FusionFix's
// register map stops at c237.
// row_major: RAGE stores these row-major and the shader multiplies row-vector first; HLSL's default would transpose them.

row_major float4x4 gWVP     : register(c8);
row_major float4x4 gPrevWVP : register(c240);
float4 gDepthParams         : register(c244);   // x=near y=far z=logDepth w=1/log2(far/near), from the CPU
// The jitter in NDC, so the vector can be formed without it. gWVP carries the game's jittered projection (right for
// rasterising: the depth test must land on the game's samples), but DLSS expects vectors free of jitter, and gPrevWVP is
// already unjittered. Undo: clip.x = clip.x - jx * w.
float4 gJitterNdc           : register(c246);   // xy = (jx_ndc, jy_ndc), zero when jitter is off
#ifdef VEL_DAMAGE
#include "velocity_dmg.hlsli"   // the dented vehicle's variant (velocity_dmg_vs.hlsl): the game's damage, before the camera
#endif

struct VSIn  { float4 pos : POSITION; };
struct VSOut {
    float4 pos : POSITION;
    float4 cur : TEXCOORD0;
    float4 prv : TEXCOORD1;
    float4 dp  : TEXCOORD2;
};

VSOut main(VSIn i)
{
    VSOut o;
#ifdef VEL_DAMAGE
    i.pos.xyz = VelDamage(i.pos.xyz);   // the same dent this frame and the last
#endif
    o.pos = mul(i.pos, gWVP);
    o.cur = o.pos;
    o.cur.xy -= gJitterNdc.xy * o.pos.w;
    o.prv = mul(i.pos, gPrevWVP);
    o.dp  = gDepthParams;
    return o;
}
