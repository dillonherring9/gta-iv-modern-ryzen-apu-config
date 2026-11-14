// The on-screen notice: two lines of text on a dark see-through box at the top centre (transport/notice_pass.h makes the
// picture and draws this). Premultiplied colour and its own alpha, blended ONE, INVSRCALPHA. The texel comes from the
// pixel's position, so each texel lands on one pixel and nothing outside the box is touched.
//   s0  the picture, POINT, CLAMP
//   c0  xy = the box's top-left pixel, zw = 1 / the picture's width and height
//   c1  x = how much of it shows, 0 to 1 (the fade in and out)
sampler2D sText : register(s0);
float4    gBox  : register(c0);
float4    gShow : register(c1);

float4 main(float2 vpos : VPOS) : COLOR0
{
    const float2 uv = (vpos + 0.5 - gBox.xy) * gBox.zw;
    clip(float4(uv, 1.0 - uv));
    return tex2Dlod(sText, float4(uv, 0.0, 0.0)) * gShow.x;
}
