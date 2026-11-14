// The game's full-screen quads carry D3D9's half texel in their vertices, for the size the game believes, so at a render scale
// every tap is off centre (blur, and faint lines). Such a quad is redrawn from a copy with the real half texel. Shared with
// tools/test-quad.
#pragma once
#include <intrin.h>        // __cpuid
#include <smmintrin.h>     // _mm_stream_load_si128

static const UINT  kQuvMaxStride = 64;        // the game's immediate vertex is 36 bytes
static const UINT  kQuvMaxVerts  = 6;
// Tolerance: the wrong and right half texels are 1e-4 apart at the largest screen.
static const float kQuvSame = 2.0e-7f;

// Does the vertex shader pass the position straight through (as a full-screen quad's does, unlike coronas, particles and sky)?
// Once a shader: every write of the position's x or y reads only a vertex input. Only picks draws to look at; QuvFix decides.
static inline UINT QuvRegType(DWORD t) { return ((t >> 28) & 7u) | ((t >> 8) & 0x18u); }
static inline bool QuvVsStraight(const DWORD* tok, UINT n)
{
    if (!tok || n < 2 || (tok[0] & 0xFFFF0000u) != 0xFFFE0000u) return false;
    const UINT model = (tok[0] >> 8) & 0xFFu;
    if (model < 2) return false;                                              // model 1's instructions carry no length
    UINT posType = 4u, posNum = 0u; bool havePos = model < 3;                 // RASTOUT 0, until a model 3 shader declares its own
    bool straight = false, computed = false;
    for (UINT i = 1; i < n; )
    {
        const DWORD t = tok[i]; const DWORD op = t & 0xFFFFu;
        if (op == 0xFFFFu) break;                                             // END
        if (op == 0xFFFEu) { i += 1 + ((t >> 16) & 0x7FFFu); continue; }      // a comment block
        const UINT len = (t >> 24) & 0xFu;
        if (i + len >= n) break;
        if (op == 0x1Fu)                                                      // dcl: a usage token, then the register
        {
            if (len >= 2 && QuvRegType(tok[i + 2]) == 6u && (tok[i + 1] & 0x1Fu) == 0u && ((tok[i + 1] >> 16) & 0xFu) == 0u)
            { posType = 6u; posNum = tok[i + 2] & 0x7FFu; havePos = true; }
        }
        else if (op != 0x51u && op != 0x30u && op != 0x2Fu && len >= 2 && havePos)   // not def, defi, defb: their operands are numbers
        {
            const DWORD d = tok[i + 1];
            if (QuvRegType(d) == posType && (d & 0x7FFu) == posNum && (d & 0x00030000u))      // it writes the position's x or y
            {
                bool in = false, tmp = false;
                for (UINT k = 2; k <= len; k++) { const UINT ty = QuvRegType(tok[i + k]); in = in || ty == 1u; tmp = tmp || ty == 0u; }
                if (in && !tmp) straight = true; else computed = true;
            }
        }
        i += 1 + len;
    }
    return straight && !computed;
}

// The vertices of a draw of two primitives: a list of six, or a strip or a fan of four. 0: not a quad's draw.
static inline UINT QuvVerts(D3DPRIMITIVETYPE t)
{
    return t == D3DPT_TRIANGLELIST ? 6u : (t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN) ? 4u : 0u;
}

// Only when every vertex is a corner of the full-screen quad with the believed half texel; then corrected in place, else
// nothing is written.
static inline bool QuvFix(BYTE* v, UINT n, UINT stride, UINT uvOff, UINT origW, UINT origH, UINT newW, UINT newH)
{
    if (!v || !n || n > kQuvMaxVerts || !origW || !origH || !newW || !newH || (origW == newW && origH == newH)) return false;
    if (stride < uvOff + 8u || uvOff < 8u) return false;
    const float was[2] = { 0.5f / (float)origW, 0.5f / (float)origH };
    const float now[2] = { 0.5f / (float)newW,  0.5f / (float)newH };
    float out[kQuvMaxVerts][2];
    for (UINT i = 0; i < n; i++)
    {
        float p[2], t[2];
        memcpy(p, v + i * stride, sizeof(p)); memcpy(t, v + i * stride + uvOff, sizeof(t));
        if (fabsf(p[0]) != 1.0f || fabsf(p[1]) != 1.0f) return false;
        for (int a = 0; a < 2; a++)
        {
            if      (fabsf(t[a] - was[a]) <= kQuvSame)          out[i][a] = now[a];
            else if (fabsf(t[a] - (1.0f + was[a])) <= kQuvSame) out[i][a] = 1.0f + now[a];
            else return false;
        }
    }
    for (UINT i = 0; i < n; i++) memcpy(v + i * stride + uvOff, out[i], sizeof(out[i]));
    return true;
}

// DXVK maps dynamic buffers write-combined, where ordinary loads are very slow, so whole 64-byte lines are read with MOVNTDQA
// (SSE4.1). A line never crosses a page, so this cannot fault. Without SSE4.1, a plain copy.
static inline bool QuvSse41()
{
    static int have = -1;
    if (have < 0) { int r[4]; __cpuid(r, 1); have = (r[2] >> 19) & 1; }
    return have == 1;
}
static inline void QuvCopy(BYTE* dst, const BYTE* src, UINT bytes)
{
    __declspec(align(64)) BYTE line[kQuvMaxVerts * kQuvMaxStride + 128];
    const UINT_PTR a = (UINT_PTR)src & ~(UINT_PTR)63, e = ((UINT_PTR)src + bytes + 63) & ~(UINT_PTR)63;
    if (!QuvSse41() || e - a > sizeof(line)) { memcpy(dst, src, bytes); return; }
    for (UINT_PTR p = a; p < e; p += 16) _mm_store_si128((__m128i*)(line + (p - a)), _mm_stream_load_si128((__m128i*)p));
    memcpy(dst, line + ((UINT_PTR)src - a), bytes);
}

// Only a DEFAULT-pool DYNAMIC buffer locked NOOVERWRITE, which the device thread lets through at once (DtBufLock); any other
// lock would wait for the worker.
static inline bool QuvRead(IDirect3DVertexBuffer9* vb, UINT off, UINT bytes, BYTE* dst)
{
    D3DVERTEXBUFFER_DESC d;
    if (!vb || !bytes || FAILED(vb->GetDesc(&d))) return false;
    if (d.Pool != D3DPOOL_DEFAULT || !(d.Usage & D3DUSAGE_DYNAMIC)) return false;
    if (off > d.Size || bytes > d.Size - off) return false;
    void* p = nullptr;
    if (FAILED(vb->Lock(off, bytes, &p, D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE)) || !p) return false;
    QuvCopy(dst, (const BYTE*)p, bytes);
    vb->Unlock();
    return true;
}
