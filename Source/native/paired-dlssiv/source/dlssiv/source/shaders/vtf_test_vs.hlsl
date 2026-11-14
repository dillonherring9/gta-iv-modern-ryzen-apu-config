// ---- Vertex texture fetch: an end-to-end round trip, not a capability query ----
// Previous-frame bone matrices cannot travel as constants (two sets of 144 registers against a file the game fills to
// c237), so they arrive as a texture read in the vertex stage. CheckDeviceFormat's S_OK only reports what the driver claims
// through a translation layer, so this proves the transport carries an exact value. It reads two texels at CPU-supplied
// coordinates, which also proves addressing: a sampler that always returns texel zero would fail.
sampler2D sBoneTest : register(s0);

// xy = first texel uv, zw = second, chosen on the CPU so the shader cannot agree with a hardcoded expectation.
// At c245 because the game fills c0..c237: probing at c0 would corrupt the next draw.
float4 gTexelCoords : register(c245);

struct VSIn  { float4 pos : POSITION; };
struct VSOut { float4 pos : POSITION; float4 val : TEXCOORD0; };

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = i.pos;
        // tex2Dlod: a vertex shader has no derivatives, so the mip level must be stated.
    float4 a = tex2Dlod(sBoneTest, float4(gTexelCoords.xy, 0.0, 0.0));
    float4 b = tex2Dlod(sBoneTest, float4(gTexelCoords.zw, 0.0, 0.0));
    o.val = float4(a.x, a.w, b.x, b.w);
    return o;
}
