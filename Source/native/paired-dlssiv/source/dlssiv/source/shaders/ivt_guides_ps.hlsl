// Both guides in one pass, two render targets: the motion vectors (COLOR0, G16R16F, pixels) and the depth (COLOR1, R32F,
// reverse-Z); the depth decode is shared.
//   s0  the depth snapshot (INTZ): the hardware depth, raw, in .r
//   s1  the object velocity target: rg = delta-UV (prev = cur + mv), b = view distance, a = validity
//   c0..c3  reproj: current view space -> previous view space, row-major, row vector times matrix
//   c4      proj:     a = P[0][0], b = P[1][1], c = P[2][2], d_p = P[3][2]
//   c5      projPrev: a = P[0][0], b = P[1][1] for the previous camera;
//             z  log2(far/near), so the logarithmic decode is one exp2
//             w  1 when near and far are usable, else 0
//   c6      misc:     near, far, valid (1/0), logDepth (1/0)
//   c7      size:     width, height, 1/width, 1/height
// Vectors: (prevPx - curPx), pointing from this frame's position to the previous one's, as NGX wants with InMVScale = 1.
// Zero wherever the vector cannot be trusted: a temporal filter trusts motion vectors completely, so a guess is worse than
// nothing. Depth: near / Z, 1 at the near plane towards 0 at the far plane (the host is told depth_inverted = 1).

sampler2D sDepth  : register(s0);
sampler2D sObjVel : register(s1);

float4 gReproj0  : register(c0);
float4 gReproj1  : register(c1);
float4 gReproj2  : register(c2);
float4 gReproj3  : register(c3);
float4 gProj     : register(c4);
float4 gProjPrev : register(c5);
float4 gMisc     : register(c6);
float4 gSize     : register(c7);
// c8: z and w (x and y unused).
//   z  mv_glass: keep the object vector when its surface is nearer than the visible one. See the test at the bottom.
//   w  far * 1.0001, the far-plane slack, from the CPU.
float4 gFlags    : register(c8);

// Called only where gProjPrev.w says near and far are usable.
float ViewZ(float raw)
{
        // near * pow(far/near, raw) == near * exp2(raw * log2(far/near)); the CPU has taken the logarithm into gProjPrev.z.
    if (gMisc.w > 0.5)
        return gMisc.x * exp2(raw * gProjPrev.z);          // logarithmic (FusionShaders)
    float denom = raw + gProj.z;                           // standard: Z = d_p / (d + c)
    if (abs(denom) < 1e-9) return gMisc.y;
    return gProj.w / denom;
}

void main(float2 vpos : VPOS, out float4 mvOut : COLOR0, out float4 depthOut : COLOR1)
{
    // VPOS is the pixel's integer position; its centre is the sample point.
    const float2 uv  = (vpos + 0.5) * gSize.zw;
    const float raw  = tex2D(sDepth, uv).r;
    const float nearC = gMisc.x;
    const float farC  = gMisc.y;

    // --- depth guide ---------------------------------------------------------
    float Z = 0.0;
    float d;
    if (gProjPrev.w < 0.5)                    // the CPU's "near and far are usable" bit
        d = 1.0 - saturate(raw);              // nothing to decode with yet: keep the ordering
    else
    {
        Z = ViewZ(raw);
        d = (Z > 0.0) ? saturate(nearC / Z) : 0.0;
    }
    depthOut = float4(d, d, d, 1.0);

    // --- motion-vector guide -------------------------------------------------
    float2 mv = 0.0;
    bool   camOk = false;
        // gMisc.z carries the projection test too (|P00| and |P11| folded in on the CPU).
    if (gMisc.z > 0.5 && Z > 0.0 && Z <= gFlags.w)
    {
        // uv -> NDC (uv.y = 0 is the top; NDC y = +1 is the top), then
        // NDC + Z -> view space with the diagonal projection.
        float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        float3 vpos3;
        vpos3.x = ndc.x * Z / gProj.x;
        vpos3.y = ndc.y * Z / gProj.y;
        vpos3.z = -Z;
        float4 p  = float4(vpos3, 1.0);
        float4 pp = p.x * gReproj0 + p.y * gReproj1 + p.z * gReproj2 + p.w * gReproj3;
        float prevZ = -pp.z;
        if (prevZ > nearC)
        {
            float2 prevNdc = float2(pp.x * gProjPrev.x, pp.y * gProjPrev.y) / prevZ;
            float2 prevUV  = float2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
            mv    = prevUV - uv;
            camOk = true;
        }
    }
        // Object velocity wins where it is valid and its stored distance agrees with the scene; disagreement means something was
    // drawn over it later. The sign matters:
    //   ov.z > Z   the stored surface is behind what is visible: ordinary occlusion, the camera vector is right. Rejected.
    //   ov.z < Z   the stored surface is nearer: a blended mover such as vehicle glass. Kept only under mv_glass=1, since it is
    //              right for a car seen from outside and wrong for everything else seen through glass.
    // A vehicle's pane is drawn blended with depth writes into the scene's depth, after the G-buffer depth read here. Under
    // mv_glass=3 the velocity pass does not redraw a blended mover whose depth differs (VelRecordObject, DLSS-IV.cpp), so a
    // pane's vector does not land here.
    float4 ov = tex2D(sObjVel, uv);
    if (ov.a >= 0.5 && camOk && Z > 0.0)
    {
        float rel = abs(ov.z - Z) / max(Z, 1e-3);
        if (rel <= 0.02 || (gFlags.z > 0.5 && ov.z < Z)) mv = ov.xy;
    }
    mvOut = float4(mv * gSize.xy, 0.0, 1.0);              // delta-UV -> pixels (RG16F keeps rg)
}
