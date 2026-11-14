// The depth the game's corona test reads, held (corona_hold).
// The game decides a corona's brightness in its vertex shader (FusionFix's deferred_lighting, technique corona): twelve
// point samples of the scene's depth within a pixel of the projected centre; the brightness is the share farther than the
// centre. Under the jitter those samples land differently every frame and a far or half-covered corona blinks.
// A filter over time does not fix it (what it holds drifts); the same inputs every frame do. While the camera stands still
// the mod keeps one frame's depth in its own texture and gives the corona draws that texture and that frame's transform
// (DLSS-IV.cpp, CorHoldBefore).
// This pass writes that texture, after the vectors and before the steady depth:
//   The snapshot (c0.z = 1): every pixel; on the first still frame and on the smallest-jitter frame of each jitter cycle.
//   The movers (c0.z = 0), on other frames of a hold: only pixels with a vector (with the camera still only a mover has one).
// A static pixel is the depth, bit for bit. A mover's is pushed farther by c1.z metres: the corona draws read last frame's
// write, so a mover's depth is a frame old, and a car driving away would hide its own tail lights. The price: a corona less
// than that far behind a mover's surface shows through it.
// The depth is FusionFix's logarithmic one, d = log2(w / near) / log2(far / near); without those constants the mod does not hold.
//   s0  the scene's depth as this frame drew it (the game's depth texture); POINT
//   s1  this frame's motion vectors, in pixels (the guides' target); POINT
//   c0  xy = one texel; z = 1 the snapshot, 0 the movers alone; w = the squared vector, in pixels, below which a point stood still
//   c1  x = 1 / near; y = 1 / log2(far / near); z = the push, in metres; w = 1 where the vectors are this frame's and trusted
//   COLOR0 (R32F)  the depth the corona test will read
sampler2D sNow : register(s0);
sampler2D sMv  : register(s1);
float4    gCor : register(c0);
float4    gCo2 : register(c1);

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float2 uv = (vpos + 0.5) * gCor.xy;
    const float2 mv = tex2Dlod(sMv, float4(uv, 0.0, 0.0)).rg;
    const float mover = (gCo2.w > 0.5 && dot(mv, mv) >= gCor.w) ? 1.0 : 0.0;
    clip(gCor.z + mover - 0.5);                   // the movers' frames: a static pixel is not written...
    float d = 0.0;
    [branch] if (gCor.z + mover > 0.5)            // ...and costs the one tap above: the depth is read only where it is written
    {
        d = tex2Dlod(sNow, float4(uv, 0.0, 0.0)).r;
        [branch] if (mover > 0.5)
        {
            const float w = exp2(d / gCo2.y) / gCo2.x;    // the view distance, in metres
            d = log2((w + gCo2.z) * gCo2.x) * gCo2.y;
        }
    }
    return float4(d, 0.0, 0.0, 1.0);
}
