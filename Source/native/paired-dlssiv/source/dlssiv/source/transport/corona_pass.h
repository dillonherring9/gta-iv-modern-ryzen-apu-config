// The corona hold's draw and arithmetic, shared with the GPU proof (tools/test-corona); the why is in shaders/ivt_corona_ps.hlsl.
#pragma once

// A mover's top speed in m/s (144 km/h): times the frame's time, how far its depth is pushed back in the held texture.
static const float kCorMoverSpeed = 40.0f;
static const float kCorPushMax    = 1.5f;         // metres: frames of 37 ms or more push no farther

// The corona's signature: texldl count, and how many read vertex sampler s0. The corona's test is twelve on s0 (FusionFix's
// deferred_lighting VS11); waterFx VS16 has twelve on s3. texldl is opcode 0x5F; its last operand is the sampler, register type
// 10 (three bits at 28, two at 11), number in the low eleven.
static inline int CorCountTexldl(const DWORD* tok, UINT n, int* onS0)
{
    *onS0 = 0;
    if (!tok || n < 2 || (tok[0] & 0xFFFF0000u) != 0xFFFE0000u || ((tok[0] >> 8) & 0xFF) < 3) return 0;   // a vertex shader of model 3
    int count = 0;
    for (UINT i = 1; i < n; )
    {
        const DWORD t = tok[i]; const DWORD op = t & 0xFFFFu;
        if (op == 0xFFFFu) break;                                             // END
        if (op == 0xFFFEu) { i += 1 + ((t >> 16) & 0x7FFFu); continue; }      // a comment block
        const UINT len = (t >> 24) & 0xFu;
        if (op == 0x5Fu)                                                      // D3DSIO_TEXLDL
        {
            count++;
            if (len >= 3 && i + len < n)
            {
                const DWORD s = tok[i + len];
                if (((s >> 28) & 7u) == 2u && (s & 0x1800u) == 0x0800u && (s & 0x7FFu) == 0u) (*onS0)++;
            }
        }
        i += 1 + len;
    }
    return count;
}

// c8..c11 without the jitter hkSetVsConstF added (w times the jitter on x and y).
static inline void CorUnjitter(const float rows[16], float jitNdcX, float jitNdcY, float out[16])
{
    for (int r = 0; r < 4; r++)
    {
        out[r * 4 + 0] = rows[r * 4 + 0] - jitNdcX * rows[r * 4 + 3];
        out[r * 4 + 1] = rows[r * 4 + 1] - jitNdcY * rows[r * 4 + 3];
        out[r * 4 + 2] = rows[r * 4 + 2];
        out[r * 4 + 3] = rows[r * 4 + 3];
    }
}
// The largest relative difference of two sets of rows. Each register's z is left out: the corona's test never reads it, and it
// changes with the game's far clip.
static inline float CorRowsApart(const float a[16], const float b[16])
{
    float worst = 0.0f;
    for (int r = 0; r < 4; r++)
    {
        const float wa = a[r * 4 + 3] < 0.0f ? -a[r * 4 + 3] : a[r * 4 + 3];
        for (int k = 0; k < 4; k++)
        {
            if (k == 2) continue;
            const float x = a[r * 4 + k], y = b[r * 4 + k];
            float m = x < 0.0f ? -x : x; const float n = y < 0.0f ? -y : y;
            if (n > m) m = n;
            if (k < 2 && wa > m) m = wa;
            const float d = (x > y ? x - y : y - x) / (m + 1.0e-12f);
            if (d > worst) worst = d;
        }
    }
    return worst;
}
// Same camera: within four float steps (rounding from adding and removing jitter); real motion is orders of magnitude larger.
static const float kCorSame = 5.0e-7f;
static inline bool CorSameRows(const float a[16], const float b[16]) { return CorRowsApart(a, b) <= kCorSame; }
// The Halton radical inverse (as in DLSS-IV.cpp), and the 0-based phase with the smallest jitter, where the snapshot is taken.
static inline float CorHalton(int index, int base)
{
    float f = 1.0f, r = 0.0f;
    while (index > 0) { f /= (float)base; r += f * (float)(index % base); index /= base; }
    return r;
}
static inline int CorBestPhase(int phases)
{
    int best = 0; float least = 1.0e9f;
    for (int p = 0; p < phases; p++)
    {
        const float x = CorHalton(p + 1, 2) - 0.5f, y = CorHalton(p + 1, 3) - 0.5f;
        const float d = x * x + y * y;
        if (d < least) { least = d; best = p; }
    }
    return best;
}

// The caller has set IvtSetupPass's state. `held`: level 0 of the R32F held texture. `all`: snapshot, else movers. `trustMv`:
// this frame's trusted vectors. `invNear`, `invLogRatio`: FusionFix's c227.xy. `push`: metres. `still2`: squared still threshold.
static inline HRESULT CorHoldDraw(IDirect3DDevice9* dev, IDirect3DBaseTexture9* depth, IDirect3DBaseTexture9* mv, IDirect3DSurface9* held,
                                  IDirect3DPixelShader9* ps, UINT w, UINT h, bool all, bool trustMv, float invNear, float invLogRatio,
                                  float push, float still2, const float (*quad)[4], UINT quadStride)
{
    const float c[2][4] = { { 1.0f / (float)w, 1.0f / (float)h, all ? 1.0f : 0.0f, still2 },
                            { invNear, invLogRatio, push, trustMv ? 1.0f : 0.0f } };
    const D3DVIEWPORT9 vp = { 0, 0, w, h, 0.0f, 1.0f };
    dev->SetRenderTarget(0, held);
    dev->SetViewport(&vp);
    dev->SetTexture(0, depth);
    dev->SetTexture(1, mv);
    for (DWORD s = 0; s < 2; s++)
    {
        dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    }
    dev->SetPixelShaderConstantF(0, &c[0][0], 2);
    dev->SetPixelShader(ps);
    return dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, quadStride);
}
