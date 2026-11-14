// Per-object velocity, pixel stage.
// RG = delta-UV, matching prev_uv = uv + mv.
// B  = clip.w, the view-space distance (not z/w: the depth buffer is logarithmic, distances compare whatever the encoding).
// A  = validity: the target is cleared to zero, so alpha separates "no object" from "an object that did not move".
// It also writes oDepth: FusionShaders makes the game's pixel shaders emit logarithmic depth, log2(viewZ/near)/log2(far/near),
// and a redraw emitting plain z/w would fail every depth test. gDepthParams.z says which encoding is in use, set by the .asi
// from the c227 traffic it observed.

struct PSIn {
    float4 cur : TEXCOORD0;
    float4 prv : TEXCOORD1;
    float4 dp  : TEXCOORD2;      // x=near y=far z=logDepth w=1/log2(far/near), from the CPU
};

void main(PSIn i, out float4 col : COLOR0, out float depth : DEPTH)
{
        // nearC is needed by the guard below.
    float nearC = max(i.dp.x, 1e-5);
        // 1/log2(far/near) is a per-draw constant sent by the CPU in i.dp.w. The clamp on near stays here (on the CPU it buys nothing).
    // i.dp.y is left unread rather than repurposed to 1/near: `dp` is interpolated, and another independent CPU-side component
    // would break the bit-for-bit match this shader's oDepth needs against the game's depth (LESSEQUAL with a 1e-5 bias).

    float2 curNdc = i.cur.xy / i.cur.w;

        // The previous position is not clipped: i.cur.w is positive for every fragment, but i.prv = mul(pos, gPrevWVP) can have w at
    // or below zero, or small, where the previous position sat at or behind the previous camera plane. The divisor is clamped
    // so no non-finite number is produced at all: a select on ps_3_0 is not a branch, and inf times zero is NaN.
    float2 prvNdc = i.prv.xy / max(i.prv.w, nearC);

    // uv.y = 0 is the top of the screen; NDC y = +1 is the top.
    float2 curUV = float2(curNdc.x * 0.5 + 0.5, 0.5 - curNdc.y * 0.5);
    float2 prvUV = float2(prvNdc.x * 0.5 + 0.5, 0.5 - prvNdc.y * 0.5);
    float2 mvUV  = prvUV - curUV;

        // The guard is on the produced vector: a small positive prv.w still yields a huge delta-UV, and two full screens of movement
    // in one frame is not something the game produces. Failing writes a zero vector with alpha 0 ("no object here"), so the
    // guide pass falls back to the camera vector.
    bool prvOk = (i.prv.w > nearC) && (max(abs(mvUV.x), abs(mvUV.y)) < 2.0);

    col = float4(prvOk ? mvUV : float2(0.0, 0.0), i.cur.w, prvOk ? 1.0 : 0.0);

    float logD  = log2(max(i.cur.w, nearC) / nearC) * i.dp.w;
        // A small bias toward the camera: the redraw tests LESSEQUAL against geometry already in the depth buffer, so any rounding
    // difference fails about half the fragments (a shape striped red and green). 1e-5 is far below the gap to any real occluder.
    depth = ((i.dp.z > 0.5) ? logD : (i.cur.z / i.cur.w)) - 1e-5;
}
