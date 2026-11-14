// The dither pass's counter (development builds only): drawn with colour writes off inside an occlusion query, so the log
// can say whether the pass had anything to work on.
//   c0.w = 0   the control: pixels where the mark's texture holds anything in rgb (the G-buffer's specular target, so most
//              of a world frame); zero means the pass is not reading the texture it thinks it is.
//   c0.w = 1   pixels the pass looks at: the mark as ivt_dedither_ps.hlsl reads it.
//   c0.w = 2   pixels the pass moves: the view's rule (odd one out, and the write differs by more than a hundredth).
// The decision is written out again here so the shipping shader does not change for an instrument. tools/test-dedither
// holds the three counts to the truth of its pictures.
sampler2D sScene : register(s0);
sampler2D sMask  : register(s1);
float4    gDed   : register(c0);

static const float3 kLuma = float3(0.2125, 0.7154, 0.0721);

float3 Tap(float2 uv) { return tex2Dlod(sScene, float4(uv, 0.0, 0.0)).rgb; }

void main(float2 vpos : VPOS, out float4 outColor : COLOR0)
{
    const float2 inv = gDed.xy;
    const float2 uv  = (vpos + 0.5) * inv;
    bool keep = false;
    if (gDed.w < 0.5)
    {
        const float4 m = tex2Dlod(sMask, float4(uv, 0.0, 0.0));
        keep = (m.r + m.g + m.b) > 0.0;
    }
    else
    {
        const float mark = tex2Dlod(sMask, float4(uv + 0.5 * inv, 0.0, 0.0)).a;
        if (gDed.w < 1.5) keep = mark > 0.0;
        else if (mark > 0.0)
        {
            const float3 c  = tex2Dlod(sScene, float4(uv, 0.0, 0.0)).rgb;
            const float3 t0 = Tap(uv + float2(-0.5, -1.5) * inv);
            const float3 t1 = Tap(uv + float2( 1.5, -0.5) * inv);
            const float3 t2 = Tap(uv + float2( 0.5,  1.5) * inv);
            const float3 t3 = Tap(uv + float2(-1.5,  0.5) * inv);
            const float4 lt = float4(dot(t0, kLuma), dot(t1, kLuma), dot(t2, kLuma), dot(t3, kLuma));
            const float  mean = dot(lt, 0.25);
            const float4 d  = lt - mean;
            const float  dc = dot(c, kLuma) - mean;
            const float3 gap = abs((t0 + t1 + t2 + t3) * 0.25 - c) - 0.01 * (abs(c) + 0.01);
            keep = dc * dc >= dot(d, d) && max(gap.r, max(gap.g, gap.b)) > 0.0;
        }
    }
    clip(keep ? 1.0 : -1.0);
    outColor = 0.0;
}
