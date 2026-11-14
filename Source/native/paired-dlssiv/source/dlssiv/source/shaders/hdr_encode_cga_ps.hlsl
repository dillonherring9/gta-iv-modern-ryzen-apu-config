// The encode with FusionFix's console gamma absorbed (hdr_cga.h): hdr_encode_ps.hlsl compiled with HDR_CGA, so the one sample
// of the frame goes through CgaCurve, because on these frames the frame is FusionFix's input (its draw was withheld).
// A separate header so the shipping encode carries no runtime test; compile-shaders.ps1 -Verify holds it byte for byte.
// The slot counts are in the generated headers. 512 slots is only the ps_3_0 floor: DXVK, which the mod requires, reports
// the maximum; without DXVK a refused CreatePixelShader just leaves the absorb unarmed.
// build.ps1's staleness check knows this file includes hdr_encode_ps.hlsl (its $extraSrc map).

#define HDR_CGA 1
#include "hdr_encode_ps.hlsl"
