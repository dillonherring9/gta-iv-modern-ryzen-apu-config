// Passes the fetched value straight through so the CPU can read it back off a render target. All three vertices carry the
// same value, so interpolation cannot alter it.
struct PSIn { float4 val : TEXCOORD0; };
float4 main(PSIn i) : COLOR0 { return i.val; }
