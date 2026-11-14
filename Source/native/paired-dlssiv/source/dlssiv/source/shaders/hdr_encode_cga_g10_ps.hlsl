// G10, absorbed: hdr_encode_cga_ps.hlsl's encode compiled with HDR_G10: RCAS on FusionFix's input, then its curve; the HUD mask
// compares the unsharpened sample through the same curve. The sharpen pass is the fallback when this encode is refused.
// build.ps1's staleness check follows the include of hdr_encode_ps.hlsl but not hdr_common.hlsli: after editing either,
// regenerate with tools\compile-shaders.ps1 (-Verify catches it if forgotten).

#define HDR_G10 1
#define HDR_CGA 1
#include "hdr_encode_ps.hlsl"
