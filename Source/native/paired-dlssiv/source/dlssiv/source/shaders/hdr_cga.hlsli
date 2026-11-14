#ifndef HDR_CGA_HLSLI
#define HDR_CGA_HLSLI   // included by hdr_common.hlsli and by the absorbed variants
// The absorb (hdr_cga.h): FusionFix's console gamma pass, reproduced from the standards in the order FusionFix evaluates
// them: the sRGB decode of IEC 61966-2-1, the Rec.709 encode of ITU-R BT.709, a plain power of 1.2, and Roberts' R2 sequence.
// The order is what makes the output agree to the last rounding (FusionFix's rewrite emits the same bytecode as before,
// compile-shaders -Verify). Do not tidy these: a reassociated expression rounds differently, and the proof pass
// (hdr_cgaproof_ps.hlsl) compares against FusionFix's real output before anything relies on them.
// Two constants are one ulp from FusionFix's (another fxc folds 1/1.055 and 0.055/1.055 to the adjacent float32): worst
// output difference 1.4e-7, invisible, but not zero for anyone tightening kCgaTol toward 1.00.
// Many Xbox 360 games decode with sRGB and encode with Rec.709, a contrast boost: "the Xbox 360 curve". The PS3 curve is a
// plain power. The dither is half an 8-bit step either way on an R2 grid keyed on the pixel through FusionFix's c44
// (the screen size) and the texel-centre coordinate.

// IEC 61966-2-1: a straight line below the knee at 0.04045, the offset 2.4 power above it.
float3 CgaSrgbDecode(float3 v)
{
    float3 belowKnee = v / 12.92f;
    float3 nonNeg    = max(v, 0.0f);
    float3 aboveKnee = pow((nonNeg + 0.055f) / 1.055f, 2.4f);

    return (v >= 0.04045f) ? aboveKnee : belowKnee;
}

// ITU-R BT.709: a slope of 4.5 below 0.018, the 0.45 power with its offset above.
float3 CgaRec709Encode(float3 v)
{
    float3 belowKnee = v * 4.5f;
    float3 aboveKnee = 1.099f * pow(max(v, 0.0f), 0.45f) - 0.099f;

    return (v >= 0.018f) ? aboveKnee : belowKnee;
}

float3 CgaXbox(float3 v)
{
    return CgaRec709Encode(CgaSrgbDecode(v));
}

float3 CgaPs3(float3 v)
{
    float3 nonNeg = max(v, 0.0f);
    return pow(nonNeg, 1.2f);
}

// R2 (Roberts 2018): frac of the pixel position dotted with 1/g and 1/g^2, g = 1.32471795724... (the plastic number).
// Centred and scaled to half an 8-bit step either way. screen is FusionFix's c44.xy, read at its own draw.
float CgaNoise(float2 uv, float2 screen)
{
    float grid = frac(dot(screen * uv, float2(0.754877666247f, 0.569840290998f)));
    grid -= 0.5f;
    return grid * (1.0f / 255.0f);
}

// cga.x: 1 = the Xbox 360 curve, 2 = the PS3 curve. cga.y: 1 = with the dither. cga.zw: FusionFix's c44.xy.
// Both tests are on constants, so every pixel takes the same side.
float3 CgaCurve(float3 color, float2 uv, float4 cga)
{
    float3 o;
    [branch] if (cga.x > 1.5f) o = CgaPs3(color);
    else                       o = CgaXbox(color);
    [branch] if (cga.y > 0.5f) o += CgaNoise(uv, cga.zw);
    return o;
}
#endif
