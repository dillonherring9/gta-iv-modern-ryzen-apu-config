// The rain, laid over the picture the upscaler made (rain_layer). A raindrop is in one place for one frame at 6 to 12 percent
// opacity (gtaRainRender.xml), and a temporal upscaler builds each pixel mostly from earlier frames, so the rain nearly vanished.
// So the rain never reaches the upscaler: the game's one rain draw is made into a layer of the mod's own, cleared to black,
// with the game's own shaders, instancing, depth test and blending. This pass adds the layer to the upscaled picture before
// the post chain reads it (transport/rain_pass.h, RainLay: blend ONE, ONE; the picture's alpha is not written).
// Added, not blended over: the layer holds drop times a, but not the coverage the scene's (1 - a) term would need. So the
// scene under a drop is 6 to 12 percent brighter than the game would leave it, on the drop's own pixels.
// A pixel whose layer is black is discarded.
//   s0  the rain layer (the scene buffer's format and the render size); POINT at the picture's size, LINEAR where the
//       picture is larger (a render scale)
//   c0  xy = one texel of the picture
sampler2D sRain : register(s0);
float4    gRain : register(c0);

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float3 c = tex2Dlod(sRain, float4((vpos + 0.5) * gRain.xy, 0.0, 0.0)).rgb;
    clip(max(c.r, max(c.g, c.b)) - 1.0e-6);
    return float4(c, 0.0);
}
