// The depth the post chain reads, steadied (steady_depth). The upscaler takes the jitter out of the picture; the depth is
// never upscaled, so the post chain reads it at a new sub-pixel offset every frame. FusionFix's depth of field and the game's
// distance blur choose sharp or blurred per pixel from one depth sample, so at depth edges the choice flips each frame.
// Where an edge lies inside a pixel is only in several frames' samples, so the depth is resolved over time.
// Writes only where it holds something: its target is cleared and written on depth-edge pixels and remembered things;
// everything else is discarded. ivt_steady_home_ps.hlsl writes the held values back into the texture the post chain reads.
// The nearer surface wins and is remembered: a nearer sample is taken at once, and a held value moves towards a farther one
// by a small share a frame (c0.z). A mean would make thin things (wires) thinner and dashed.
// What is held is read from where the point was (s2, the motion vectors) and clamped to the eight neighbours' range, so it
// is dropped at once behind something that moved away. The vector is read at the neighbour with the nearest depth, which
// follows a mover on every frame (the pixel's own sample would alternate between two vectors). Nearer is the smaller value
// in both of the game's depth encodings.
// Only where there is an edge (the neighbourhood's range above c0.w); inside a surface nothing is held or written.
// Except a thing remembered: a thing thinner than a pixel is in no sample on some frames. g carries freshness: 1 when written
// on an edge whose vector was zero. On a flat frame with the camera still (c1.x), a nearer, fresh held value is kept, moves
// by the same share, and loses c1.z of freshness; anything else is dropped. A thing that is there is found again within a
// jitter cycle; one that has gone is let go after two cycles. While the camera moves nothing is remembered on a flat frame.
// All eight neighbours decide the edge: a slanted edge can touch a pixel by a corner alone.
// The target is G16R16F: one step is 0.0005 at these values, so a held depth that close to the frame's own is not written
// back. Under the stock depth encoding that is everything beyond about 600 metres, where the pass does nothing.
// The values are the game's own encoding: the pass only compares and blends them, and a blend of two depths is a depth.
//   s0  the depth the post chain reads, as this frame drew it; POINT
//   s1  what the frame before held (this pass's own target); POINT: it is written in places only
//   s2  this frame's motion vectors, in pixels, to where the point was a frame ago (the guides' target); POINT
//   c0  xy = one texel; z = the share by which a held value moves towards a farther sample (1 and above: history not read,
//       which seeds it); w = how far apart the neighbourhood's nearest and farthest must be for an edge (0: any difference)
//   c1  x = 1 while the camera stands still; y = the squared vector, in pixels, below which a point stood still;
//       z = how much freshness a remembered thing loses on each frame no sample finds it
//   COLOR0 (G16R16F, cleared to zero before the draw)
//       r  the depth held, positive where it differs from the frame's own sample (the home pass writes those back),
//          negative where it is the frame's own to within a half-float step
//       g  0: nothing is held here; else 1 plus how fresh it is
sampler2D sNow  : register(s0);
sampler2D sHist : register(s1);
sampler2D sMv   : register(s2);
float4    gSd   : register(c0);
float4    gSd2  : register(c1);

// the frame's depth at a neighbour
float Now(float2 uv, float2 o)
{
    return tex2Dlod(sNow, float4(uv + o * gSd.xy, 0.0, 0.0)).r;
}
// one more tap of the neighbourhood: the range grows, and the nearest so far and where it is are kept
void Tap(float2 uv, float2 o, inout float lo, inout float hi, inout float2 at)
{
    const float v = Now(uv, o);
    if (v < lo) { lo = v; at = o; }
    hi = max(hi, v);
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float2 uv = (vpos + 0.5) * gSd.xy;
    const float d = Now(uv, float2(0.0, 0.0));
    float lo = d, hi = d;
    float2 at = float2(0.0, 0.0);
    Tap(uv, float2(-1.0, 0.0), lo, hi, at);  Tap(uv, float2(1.0, 0.0), lo, hi, at);
    Tap(uv, float2(0.0, -1.0), lo, hi, at);  Tap(uv, float2(0.0, 1.0), lo, hi, at);
    Tap(uv, float2(-1.0, -1.0), lo, hi, at); Tap(uv, float2(1.0, -1.0), lo, hi, at);
    Tap(uv, float2(-1.0, 1.0), lo, hi, at);  Tap(uv, float2(1.0, 1.0), lo, hi, at);
    float s = d;
    float g = 0.0;
    [branch] if (hi - lo > gSd.w)
    {
                // An edge: what was held where this point was, kept inside what the frame's neighbourhood contains.
        const float2 mv = tex2Dlod(sMv, float4(uv + at * gSd.xy, 0.0, 0.0)).rg;
        const float2 hv = tex2Dlod(sHist, float4(uv + mv * gSd.xy, 0.0, 0.0)).rg;
        if (hv.g > 0.5 && gSd.z < 1.0)
        {
            const float h = clamp(abs(hv.r), lo, hi);
            s = (d < h) ? d : lerp(h, d, gSd.z);
        }
        g = (dot(mv, mv) < gSd2.y) ? 2.0 : 1.0;
    }
    else
    {
        [branch] if (gSd2.x > 0.5 && gSd.z < 1.0)
        {
            // A flat frame, the camera still: a nearer thing remembered here is kept while it is fresh.
            const float2 hv = tex2Dlod(sHist, float4(uv, 0.0, 0.0)).rg;
            const float h = abs(hv.r);
            if (hv.g > 1.5 && h < d - gSd.w) { s = lerp(h, d, gSd.z); g = hv.g - gSd2.z; }
        }
    }
    clip(g - 0.5);                                // nothing held here: nothing written
    return float4((abs(s - d) > 0.0005) ? s : -s, g, 0.0, 1.0);
}
