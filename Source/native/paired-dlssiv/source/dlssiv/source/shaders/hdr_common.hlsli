// HDR: the maths shared by the shipping encode, the debug views and the LUT profile pass. One copy, so the views show the
// shipping maths.
// Constant layout, uploaded by IvtSetupPass as c0..c8 (nine float4):
//   gHdr   c0  x paper white nits   y peak nits   z decode (0 = sRGB piecewise, else the gamma exponent)
//              w restoration strength, 0 off .. 1 full
//   gHdr2  c1  x knee (graded luminance where restoration may start; < 0 = none; kHdrKnee in hdr_encode.h). Used only on
//                the exact ratio (source 1, no LUT profile): HdrShip substitutes -1 on source 0 and the LUT path.
//              y the UI level, in units of paper white, or 0 when there is no pre-HUD copy this frame
//              zw 1 / output size (UV from VPOS)
//   gHdr3  c2  x roll-off start as a fraction of the ceiling, 0 = hard clip   y debug view   z seconds (cycling patterns)
//              w the source of the lost luminance, resolved on the CPU:
//                0 the frame itself: the composite draws into the FP16 back buffer, which does not clip, so values above
//                  1 are the game's own unclipped output
//                1 the composite rebuilt from its own inputs (GtaComposite), for when FusionFix's LUT compressed the frame
//   gGame0 c3  x Exposure (rage_postfx c66.x)   yzw ToneMapParams.xyz (c81)
//   gGame1 c4  x deSatContrastGamma.x (c82.x)   y deSatContrastGamma.z, the gamma
//              z ColorShift.w (c84.w)   w bloom mode: FusionFix NoBloomColorShift (c217.w != 0)
//   gGame2 c5  xyz ColorCorrect (c83)   w 1 = a bloom texture is bound on s3
//   gGame3 c6  xyz ColorShift (c84)     w 1 = FusionFix tone mapping is on and its LUT is bound on s4
//   gCga   c7  x curve, y dither, zw FusionFix's c44.xy (the absorb's encodes, the remedy blit and the proof only)
//   gFlags c8  x which console gamma is in the frame (HdrConsoleGammaMode): 0 none, 1 the Xbox 360 curve, 2 the PS3 curve
//              y the Xbox 360 table's slope at white, for its extension above 1
//              z the LUT profile's pivot, in white units; 0 = no usable profile
//              w the LUT profile's slope dT/dx at the pivot
// Samplers: s0 the graded frame (copied out), s1 the pre-tonemap scene (HDRSampler), s2 the 1x1 adapted luminance
// (AdapLumSampler), s3 the bloom, s4 FusionFix's tone-map LUT (1024x32, 32 tiles of 32x32), s9 FusionFix's console gamma
// table (256x1), s10 the LUT profile (256x1, PQ-uniform input), s11 the frame before the HUD drew (raw: without the console
// gamma, which HdrUiCoverage applies). The game's textures are the composite's own, captured while bound. s5 to s8 are
// unused, left as a hole so s9 to s11 keep their numbers.
// Units: "lin" is the game's finished frame in linear light with 1.0 = SDR white. Display-referred values are in units of
// paper white until the last multiply by (paper / 80), which makes scRGB. The ceiling is peak / paper.

#ifndef HDR_COMMON_HLSLI
#define HDR_COMMON_HLSLI

static const float3 kLuma709 = float3(0.2126, 0.7152, 0.0722);
static const float3 kLumaGta = float3(0.2125, 0.7154, 0.0721);   // the game's own weights (rage_postfx def c1)

float Luma709(float3 c) { return dot(c, kLuma709); }

// IEC 61966-2-1 sRGB EOTF, per channel.
float3 SrgbToLinear(float3 c)
{
    c = saturate(c);
    return (c <= 0.04045) ? (c / 12.92) : pow((c + 0.055) / 1.055, 2.4);
}

// The game's 8-bit output to linear light. decode 0 = sRGB piecewise, else that gamma (2.2 ships).
// decode is a per-draw uniform, so only one curve is live in a session: an `if` with [branch] lets the hardware skip the
// dead half, where a ternary makes fxc compute both curves and select (twelve dead slots per site). [branch] cannot sit on
// `?:`. One local and one exit, not a return inside the `if`: the early-return form keeps more registers live to the join.
float3 HdrDecode(float3 s, float decode)
{
    float3 lin;
    [branch] if (decode < 0.5) lin = SrgbToLinear(s);
    else                       lin = pow(saturate(s), decode);
    return lin;
}

// The same decode without the clamp, for the reproduced unclipped output. Branched for the same reason. The inner select
// on 0.04045 stays a ternary: that one varies per channel inside the draw.
float3 HdrDecodeOpen(float3 s, float decode)
{
    s = max(s, 0.0);
    float3 lin;
    [branch] if (decode < 0.5) lin = (s <= 0.04045) ? (s / 12.92) : pow((s + 0.055) / 1.055, 2.4);
    else                       lin = pow(s, decode);
    return lin;
}

// ---- The game ----
// The game's composite, reproduced: rage_postfx.fxc, GTACompositePostFx (stock and FusionFix's agree). Returns what the
// game's shader hands to its target before the clip, the LUT and the console gamma: gamma-domain, 1.0 = SDR white.
//   E      = ToneMapParams.y / adaptedLum            (the per-frame exposure)
//   c      = E * (scene * Exposure + bloomTerm)
//   Y      = dot(c, kLumaGta)
//   d      = deSat * (c - Y) + Y                     (desaturation)
//   w      = Y * ColorShift.w
//   d      = sat(w) * (d - ColorShift.rgb * w) + w * ColorShift.rgb
//   o      = 2 * ColorCorrect * d                    (0.5 = neutral)
//   out    = o * sat(Y) ^ (gamma - 1)                (the game's own gamma)
// Not reproduced: depth of field, motion blur, the stipple filter and the film grain, which act on the scene before this.
// That is why a ratio from this is applied to the real pixel, never substituted.
float3 GtaComposite(float3 s, float3 b, float L, float4 g0, float4 g1, float4 g2, float4 g3)
{
    const float E = g0.z / max(L, 1e-6);
    float3 bloom = 0.0;
    if (g2.w > 0.5)
    {
        const float thr = g0.y / max(E, 1e-6);                  // ToneMapParams.x / E
        if (g1.w > 0.5)
        {   // FusionFix NoBloomColorShift: one scale for the whole bloom colour
            const float n = sqrt(max(dot(b * b, kLumaGta), 1e-12));
            bloom = b * max(0.0, (n * g0.x - thr) / n * g0.w * 0.25);
        }
        else
        {   // stock: a per-channel threshold
            const float3 t = b * g0.x - thr;
            bloom = (t >= 0.0) ? t * g0.w * 0.25 : 0.0;
        }
    }
    const float3 c = E * (s * g0.x + bloom);
    const float  Y = dot(c, kLumaGta);
    float3 d = g1.x * (c - Y) + Y;
    const float w = Y * g1.z;
    d = saturate(w) * (d - g3.xyz * w) + w * g3.xyz;
    float3 o = 2.0 * g2.xyz * d;
    o *= pow(max(saturate(Y), 1e-6), g1.y - 1.0);
    return max(o, 0.0);
}

// SMPTE ST 2084, normalised so that 1.0 = 10000 nits. FusionFix indexes its
// LUT with the OETF of linear colour at 1.0 = 100 nits (so x = lin * 0.01).
float3 PqEncode(float3 x)
{
    x = max(x, 0.0);
    const float3 p = pow(x, 0.1593017578125);
    return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);
}
float PqEncode1(float x) { return PqEncode(x.xxx).x; }
float PqDecode1(float p)
{
    const float q = pow(saturate(p), 1.0 / 78.84375);
    return pow(max(q - 0.8359375, 0.0) / (18.8515625 - 18.6875 * q), 1.0 / 0.1593017578125);
}

// FusionFix's 32x32x32 LUT, stored as 32 tiles of 32x32 in a 1024x32 texture,
// blue interpolated between two tiles: exactly the lookup in its shader.
float3 GtaLut(sampler2D lut, float3 pq)
{
    const float z = pq.b * 31.0;
    const float f = frac(z);
    float2 uv = (pq.rg * 31.0 + 0.5) * float2(1.0 / 1024.0, 1.0 / 32.0);
    uv.x += (z - f) * (1.0 / 32.0);
    const float3 a = tex2Dlod(lut, float4(uv, 0.0, 0.0)).rgb;
    const float3 c = tex2Dlod(lut, float4(uv.x + 1.0 / 32.0, uv.y, 0.0, 0.0)).rgb;
    return lerp(a, c, f);
}

// FusionFix's console gamma, applied to the composite's output after the LUT or the clip. flags.x names the curve.
// 1, the Xbox 360 curve: per channel from the 256-entry table on s9; above white it continues with the table's top slope.
// 2, the PS3 curve: FusionFix's ConsoleGamma.fx, PSMain_BlitCellGamma, pow(max(color, 0), 1.2), exact at every value.
// Above white it climbs faster than the Xbox 360 extension, so one cannot stand in for the other in HdrUiCoverage.
// One local, one exit, [branch] on both tests: written as early returns, fxc computed both curves at every call site.
// flags.x is a constant, so every pixel takes the same arm.
float3 ConsoleGamma(float3 x, sampler2D t, float4 flags)
{
    float3 y = x;
    [branch] if (flags.x > 1.5)
    {
        y = pow(max(x, 0.0), 1.2);
    }
    else [branch] if (flags.x > 0.5)
    {
        [unroll] for (int c = 0; c < 3; ++c)
        {
            const float v = x[c];
            y[c] = (v <= 1.0) ? tex2Dlod(t, float4((saturate(v) * 255.0 + 0.5) / 256.0, 0.5, 0.0, 0.0)).r
                              : 1.0 + flags.y * (v - 1.0);
        }
    }
    return y;
}

// The exact curve: above white the Xbox 360 curve is FusionFix's formula, sRGB decode then Rec.709 encode (hdr_cga.hlsli),
// on its FP16 copy of the frame. ConsoleGamma's straight-line extension parts from it by more than the mask's 3 percent dead
// zone past about 2.95 raw, which marked the brightest headlight glare as interface, and under-restored the exact ratio's
// brightest highlights by 3 to 6 percent. Used by HdrUiCoverage and HdrExactRatio's unclipped `full`; ConsoleGamma's other
// callers never pass a value above white and keep the line. Only a warp with a channel above white computes the formula.
#include "hdr_cga.hlsli"
float3 ConsoleGammaExact(float3 x, sampler2D t, float4 flags)
{
    float3 y = x;
    [branch] if (flags.x > 1.5)
    {
        y = pow(max(x, 0.0), 1.2);
    }
    else [branch] if (flags.x > 0.5)
    {
        float3 hi = x;
        [branch] if (max(x.r, max(x.g, x.b)) > 1.0) hi = CgaXbox(x);
        [unroll] for (int c = 0; c < 3; ++c)
        {
            const float v = x[c];
            y[c] = (v <= 1.0) ? tex2Dlod(t, float4((saturate(v) * 255.0 + 0.5) / 256.0, 0.5, 0.0, 0.0)).r
                              : hi[c];
        }
    }
    return y;
}

// What the game's pipeline makes of the rebuilt output g: the clip or the LUT, then the console gamma. This equals the frame.
float3 GtaSdr(float3 g, sampler2D lut, sampler2D xg, float4 g3, float4 flags)
{
    float3 sdr;
    if (g3.w > 0.5) sdr = GtaLut(lut, saturate(PqEncode(pow(g, 2.2) * 0.01)));
    else            sdr = saturate(g);
    return ConsoleGamma(sdr, xg, flags);
}

// The exact ratio: the luminance the game's output would have had without the clip (or FusionFix's LUT shoulder), over the
// luminance it did have, both through the console gamma and the same decode. 1.0 wherever nothing was lost.
float HdrExactRatio(float3 s, float3 b, float L, sampler2D lut, sampler2D xg, float decode,
                    float4 g0, float4 g1, float4 g2, float4 g3, float4 flags)
{
    const float3 g    = GtaComposite(s, b, L, g0, g1, g2, g3);
    const float3 sdr  = GtaSdr(g, lut, xg, g3, flags);
    const float3 full = ConsoleGammaExact(g, xg, flags);   // FusionFix's formula above white
    const float yFull = Luma709(HdrDecodeOpen(full, decode));
    const float ySdr  = Luma709(HdrDecodeOpen(sdr, decode));
    return max(1.0, yFull / max(ySdr, 1e-5));
}

// The LUT's own curve, extended. The profile (s10) holds T(x): the LUT's grey-axis output luminance, console gamma included,
// decoded, against linear input x in white units, PQ-uniform (FusionFix decodes the composite with 2.2 before the LUT, so
// x = luma(g ^ 2.2)). Below the pivot (flags.z) the ratio is 1, bit-exact; above it the curve continues straight with the
// slope there (flags.w). The ratio is against the LUT's grey response, so a coloured pixel keeps the LUT's colour treatment.
float LutProfileT(sampler2D prof, float x)
{
    return tex2Dlod(prof, float4(PqEncode1(x * 0.01), 0.5, 0.0, 0.0)).r;
}
float HdrLutRatio(float3 g, sampler2D prof, float4 flags)
{
    const float x = Luma709(HdrDecodeOpen(g, 2.2));
    if (x <= flags.z) return 1.0;
    const float T  = LutProfileT(prof, x);
    const float Tp = LutProfileT(prof, flags.z);
    const float H  = Tp + flags.w * (x - flags.z);
    return max(1.0, H / max(T, 1e-5));
}

// ---- The output ----
// Exponential shoulder on the max channel: identity up to `start`, then approaches `ceil_` with continuous slope and never
// exceeds it. All channels scale together, so hue is kept and a saturated highlight rolls off as a whole.
// The form used by RenoDX's RAGE mod (RDR1).
float3 HdrRollOff(float3 c, float ceil_, float start)
{
    const float m = max(c.r, max(c.g, c.b));
    if (m <= start) return c;
    const float room = max(ceil_ - start, 1e-4);
    const float y = start + room * (1.0 - exp(-(m - start) / room));
    return c * (y / m);
}

// Highlight restoration. lin: the real graded pixel, decoded, 1 = white. ratioLost: how much luminance the game's pipeline
// took from it (1 = none). knee < 0 means no knee (the LUT path: its pivot is the knee).
//   1. Strictly additive: the ratio is never below 1, so a wrong input can only under-restore.
//   2. Confined: the knee, a smoothstep on the real pixel's brightest channel, keeps the exact-ratio path to pixels that
//      clipped, which makes the unreproduced blurs harmless. The brightest channel, not luminance, so a clipped saturated
//      tail light (about (1, 0.05, 0.05)) is restored.
//   3. Hue preserved: the graded colour is scaled, not replaced.
float3 HdrApplyRatio(float3 lin, float ratioLost, float4 gHdr, float knee, float rolloff)
{
    const float ceil_ = max(gHdr.y, gHdr.x) / gHdr.x;
    const float Mg    = max(lin.r, max(lin.g, lin.b));
    const float clip  = (knee < 0.0) ? 1.0 : smoothstep(knee, 1.0, Mg);
    const float ratio = 1.0 + (ratioLost - 1.0) * clip * gHdr.w;
    if (rolloff > 0.0) return HdrRollOff(lin * ratio, ceil_, max(1.0, rolloff * ceil_));
    return lin * min(ratio, ceil_);
}

// Source 0: the frame's own values above 1 (the FP16 back buffer), over the same frame clipped. Exactly 1 within 0..1.
// `lin` is the caller's clipped decode of this pixel, passed in so the encode does not decode it twice.
float HdrOpenRatio(float3 s, float3 lin, float decode)
{
    return max(1.0, Luma709(HdrDecodeOpen(s, decode)) / max(Luma709(lin), 1e-5));
}

// Is this pixel on the LUT-profile path? Source 1, the LUT on, a profile ready. gHdr3.w is only ever 0.0 or 1.0.
bool HdrLutPath(float4 gHdr3, float4 g3, float4 flags) { return gHdr3.w > 0.5 && g3.w > 0.5 && flags.z > 0.0; }

// The lost-luminance ratio by source (gHdr3.w), see the header.
// The arms are picked by per-draw uniforms, so one is live for a whole frame. One local and a single exit with [branch] on
// both tests: written as early returns, fxc hoisted the source-0 work past the endif and every source-1 frame paid for it.
// [branch] on the outer test alone does not prevent that.
float HdrLostRatio(float3 s, float3 lin, float3 u, float3 b, float L, sampler2D lut, sampler2D xg, sampler2D prof,
                   float4 gHdr, float4 gHdr2, float4 gHdr3, float4 g0, float4 g1, float4 g2, float4 g3, float4 flags)
{
    float r;
    [branch] if (gHdr3.w > 0.5)
    {
        [branch] if (HdrLutPath(gHdr3, g3, flags)) r = HdrLutRatio(GtaComposite(u, b, L, g0, g1, g2, g3), prof, flags);
        else                                       r = HdrExactRatio(u, b, L, lut, xg, gHdr.z, g0, g1, g2, g3, flags);
    }
    else r = HdrOpenRatio(s, lin, gHdr.z);
    return r;
}

// How much of this pixel is the interface. `pre` is the frame at the interface boundary, `post` as presented, so what moved
// between them was drawn by the game's 2D pass (HUD, map, subtitles, menus). That pass is alpha blended, so only how far the
// pixel moved can be measured, and BT.2408 settles the rest: a pixel the interface replaced is graphics.
// The dead zone rejects the frame's final blended quad (a fade or vignette after the interface), which moves every pixel by
// about 2 percent. Erring here leaves a faint overlay with the scene's level, which is harmless; erring the other way tints
// the whole picture.
// The copy is put in the frame's domain first: it is taken before FusionFix's console gamma pass, so the curve
// FusionFix actually drew (gFlags.x) is applied to it. Otherwise the curve's own shape, |x - curve(x)|, reads as a partial
// mask across the midtones (and, with the wrong curve, across the highlights). A wash that peaks in the midtones and
// vanishes at both ends is this. With console gamma off ConsoleGamma returns its argument, so nothing changes.
// The dead zone is relative above white, floored at 1: the residual is multiplicative and the FP16 buffer is unbounded
// above 1, so a fixed threshold masked every pixel above about 1.5x white. Nothing at or below white moves.
// Real interface over a highlight still saturates; a faint overlay there gets partial coverage, the harmless direction.
// `on` is the UI level with a sign: 0 no mask this frame, positive the mask by difference, negative the whole frame is
// interface (a game menu is up: hdr_encode.h, c[1][1]), since a translucent full-screen layer cannot be masked by difference.
float HdrUiCoverage(float3 post, float3 pre, float on, sampler2D xg, float4 flags)
{
        // One multiply and one compare, not two early returns: `full` is 1 on a menu frame and 0 otherwise (a level is never under
    // 1/1000), which is also right for on == 0. Callers take the level as abs(on).
    const float  full = saturate(on * -65536.0);
    const float3 p   = ConsoleGammaExact(pre, xg, flags);  // FusionFix's formula above white
    const float3 d   = abs(post - p);
    const float3 m   = max(post, p);
    const float  lvl = max(1.0, max(m.r, max(m.g, m.b)));
    return on > 0.0 ? smoothstep(0.03 * lvl, 0.12 * lvl, max(d.r, max(d.g, d.b))) : full;
}

// The shipping pixel, in units of paper white. uiCov 0 and uiLevel 1 is the no-HUD case.
float3 HdrShip(float3 s, float3 u, float3 b, float L, sampler2D lut, sampler2D xg, sampler2D prof,
               float4 gHdr, float4 gHdr2, float4 gHdr3, float4 g0, float4 g1, float4 g2, float4 g3, float4 flags,
               float uiCov, float uiLevel)
{
    float3 lin = HdrDecode(s, gHdr.z);
        // The per-pixel skip: restoration changes nothing for a pixel that lost nothing, and both shipping ratio paths can tell:
    //   source 0: no channel of the frame above white means the ratio is 1, the knee -1 and the roll-off start at least 1, so
    //     the result is `lin` within one float32 step (rcp can make x/x read one step above 1);
    //   source 1, the exact ratio: the brightest channel of `lin` at or below the knee (kHdrKnee, 0.90) gives a ratio of
    //     exactly 1;
    //   the LUT path has no knee (its ratio can exceed 1 below white), so it always runs.
    // A real branch: a warp skips when all its pixels do, and highlights are spatially coherent. tools/test-hdr-skip proves it
    // by patching this condition back to `if (gHdr.w > 0.0)`.
    [branch] if (gHdr.w > 0.0 && ((gHdr3.w < 0.5) ? (max(s.r, max(s.g, s.b)) > 1.0)
                                                 : (HdrLutPath(gHdr3, g3, flags) || max(lin.r, max(lin.g, lin.b)) > gHdr2.x)))
    {
        const float r    = HdrLostRatio(s, lin, u, b, L, lut, xg, prof, gHdr, gHdr2, gHdr3, g0, g1, g2, g3, flags);
                // No knee where the ratio is exact. Source 0 reads the frame's own values, so its ratio is exactly 1 wherever no channel
        // passed 1, whatever the knee says. The knee only confines the rebuilt composite (source 1 without a LUT profile), whose
        // blurs are not reproduced; on source 0 a luminance knee would withhold restoration from saturated emitters.
        const bool  open = gHdr3.w < 0.5;                    // source 0: the frame's own values
        const float knee = (open || HdrLutPath(gHdr3, g3, flags)) ? -1.0 : gHdr2.x;
                // Where the HUD covers, the scene's restoration is withdrawn: the luminance above white belongs to what is behind it.
        lin = HdrApplyRatio(lin, lerp(r, 1.0, uiCov), gHdr, knee, gHdr3.x);
    }
        // ...and the graphics sit at their own level, paper white unless the player asked for another.
    return lin * lerp(1.0, uiLevel, uiCov);
}

#endif
