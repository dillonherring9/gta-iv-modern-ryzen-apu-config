// The dented-vehicle shader test, shared with tools\test-vel-damage. Included at the end of DLSS-IV.cpp (no hot data moves).
#pragma once

// Tokens in DWORDs; larger shaders are not asked. The game's 132 damage shaders fit.
#define VEL_DMG_TOK_MAX 4096

// Reads the damage map: four texldl on s0 and c208 (BoundRadius) as a source, which matches exactly the game's 132
// DamageSampler shaders. The walk is CorCountTexldl's (transport/corona_pass.h); a constant is register type 2.
static inline bool VelDmgPrintTok(const DWORD* tok, UINT n)
{
    if (!tok || n < 2 || (tok[0] & 0xFFFF0000u) != 0xFFFE0000u || ((tok[0] >> 8) & 0xFF) < 3) return false;   // a vertex shader of model 3
    int onS0 = 0; bool c208 = false;
    for (UINT i = 1; i < n; )
    {
        const DWORD t = tok[i], op = t & 0xFFFFu;
        if (op == 0xFFFFu) break;                                             // END
        if (op == 0xFFFEu) { i += 1 + ((t >> 16) & 0x7FFFu); continue; }      // a comment block
        const UINT len = (t >> 24) & 0xFu;
        if (op == 0x5Fu && len >= 3 && i + len < n)                          // D3DSIO_TEXLDL
        {
            const DWORD s = tok[i + len];
            if (((s >> 28) & 7u) == 2u && (s & 0x1800u) == 0x0800u && (s & 0x7FFu) == 0u) onS0++;
        }
        if (op != 0x51u && op != 0x30u && op != 0x2Fu && op != 0x1Fu)        // not def, defi, defb, dcl
            for (UINT k = 1; k <= len && i + k < n; k++)
            {
                const DWORD s = tok[i + k];
                if ((s & 0x80000000u) && ((s >> 28) & 7u) == 2u && (s & 0x1800u) == 0u && (s & 0x7FFu) == 208u) c208 = true;
            }
        i += 1 + len;
    }
    return onS0 == 4 && c208;
}
