// G10: hdr_encode_ps.hlsl compiled with HDR_G10, the encode that sharpens the frame at its own sample (RCAS, rcas_ps.hlsl's
// arithmetic) in place of the mod's sharpen pass, the HUD included; the HUD mask still compares the unsharpened sample.
// The sharpen pass is the fallback when this encode is refused.
// build.ps1's staleness check follows the include of hdr_encode_ps.hlsl but not hdr_common.hlsli: after editing either,
// regenerate with tools\compile-shaders.ps1 (-Verify catches it if forgotten).

#define HDR_G10 1
#include "hdr_encode_ps.hlsl"
