// Full-screen pass-through for the transport's guide passes. The quad's
// vertices are already in clip space; the pixel shaders take their position
// from VPOS, so nothing else is interpolated.
struct VSIn  { float4 pos : POSITION; };
struct VSOut { float4 pos : POSITION; };

VSOut main(VSIn i)
{
    VSOut o;
    o.pos = i.pos;
    return o;
}
