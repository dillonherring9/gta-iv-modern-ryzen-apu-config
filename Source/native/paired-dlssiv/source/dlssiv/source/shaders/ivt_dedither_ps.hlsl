// The game's dither, smoothed before the upscaler.
// The game draws a see-through or fading object into the G-buffer with pixels left out by a 2x2 pattern of screen position
// (FusionFix's deferred passes: thresholds 0.2, 0.4, 0.6, 0.8 by the parity of x and y) and marks the pixels it drew in the
// alpha of the third G-buffer target; FusionFix's post chain opens with a filter that blends the pattern away. The mod runs
// the upscaler before the post chain, and a pattern tied to the pixel grid under the jitter comes out crawling. So the same
// decision is made here at the render size, where the pattern is exact. Written from the rule, not from FusionFix's text:
//   The mark   the mask's alpha at this pixel and its right, lower and diagonal neighbours, read in one tap on their shared
//              corner. No mark, no change: nearly every pixel, one fetch.
//   The test   four taps on shared corners (each the mean of a 2x2 block) in a pinwheel around this pixel, none holding it;
//              each block holds every phase once. The pixel is the odd one out when its luma is further from the taps' mean
//              than the taps are from each other, squared and summed.
//   The write  mode 1: the mean of the four taps (sixteen pixels; FusionFix's footprint).
//              mode 2: the mean of the four 2x2 blocks that hold this pixel, the 3x3 tent 1-2-1: the same blend, less blur.
// It writes only where a mark reaches: the new colour and a = 1 where the pixel changes, a = 0 where not. A second draw
// (ivt_dedither_home_ps.hlsl) puts the changed pixels into the scene buffer, which the upscaler is given; the target is never
// cleared and never read where stale. tools/test-dedither (proof 9) holds the result equal to a full-copy reference.
//   s0  the scene, the render size, LINEAR filtering (the taps on shared corners rely on it)
//   s1  the third G-buffer target, the same size, LINEAR: alpha is the mark
//   c0  xy = one texel; z = the mode (1 or 2)
//   COLOR0 (the scene buffer's format, not cleared): rgb the new colour and a = 1 where the pass changes the pixel, a = 0
//   where it does not; no other pixel is written
// The view variant (ivt_dedither_view_ps.hlsl, development builds) paints the decision instead: magenta where a pixel would
// move, a green tint where the mark was set and nothing moves. It writes every pixel, into a copy the upscaler is given.
sampler2D sScene : register(s0);
sampler2D sMask  : register(s1);
float4    gDed   : register(c0);

static const float3 kLuma = float3(0.2125, 0.7154, 0.0721);

float3 Tap(float2 uv) { return tex2Dlod(sScene, float4(uv, 0.0, 0.0)).rgb; }

void main(float2 vpos : VPOS, out float4 outColor : COLOR0)
{
    const float2 inv = gDed.xy;
    const float2 uv  = (vpos + 0.5) * inv;      // VPOS is the integer position
    const float mark = tex2Dlod(sMask, float4(uv + 0.5 * inv, 0.0, 0.0)).a;
#ifdef DED_VIEW
    const float4 c   = tex2Dlod(sScene, float4(uv, 0.0, 0.0));
    outColor = c;
#else
    outColor = float4(0.0, 0.0, 0.0, 0.0);
    float keep = 0.0;                           // 1 where this pixel is written: wherever a mark reaches
#endif
    [branch] if (mark > 0.0)
    {
#ifndef DED_VIEW
        const float4 c  = tex2Dlod(sScene, float4(uv, 0.0, 0.0));
#endif
        const float3 t0 = Tap(uv + float2(-0.5, -1.5) * inv);
        const float3 t1 = Tap(uv + float2( 1.5, -0.5) * inv);
        const float3 t2 = Tap(uv + float2( 0.5,  1.5) * inv);
        const float3 t3 = Tap(uv + float2(-1.5,  0.5) * inv);
        const float4 lt = float4(dot(t0, kLuma), dot(t1, kLuma), dot(t2, kLuma), dot(t3, kLuma));
        const float  mean = dot(lt, 0.25);
        const float4 d  = lt - mean;
        const float  dc = dot(c.rgb, kLuma) - mean;
        const bool odd = dc * dc >= dot(d, d);
#ifdef DED_VIEW
                // Magenta only where the write would move the pixel by more than a hundredth in some channel: a flat marked surface passes
        // the test with nothing to change. Judged on mode 1's write.
        const float3 gap = abs((t0 + t1 + t2 + t3) * 0.25 - c.rgb) - 0.01 * (abs(c.rgb) + 0.01);
        const bool moves = odd && max(gap.r, max(gap.g, gap.b)) > 0.0;
        outColor.rgb = moves ? float3(4.0, 0.0, 4.0) : lerp(c.rgb, float3(0.0, 2.0, 0.0), 0.5);
#else
        [branch] if (odd)
        {
            float3 blend = (t0 + t1 + t2 + t3) * 0.25;
            [branch] if (gDed.z > 1.5)
                blend = (Tap(uv + float2(-0.5, -0.5) * inv) + Tap(uv + float2( 0.5, -0.5) * inv)
                       + Tap(uv + float2(-0.5,  0.5) * inv) + Tap(uv + float2( 0.5,  0.5) * inv)) * 0.25;
            outColor = float4(blend, 1.0);
        }
        keep = 1.0;                             // written either way: the second draw must find this frame's answer
#endif
    }
#ifndef DED_VIEW
    clip(keep - 0.5);                           // no mark reaches this pixel: nothing is written
#endif
}
