// The second-target encode of a frame with a 3D overlay ahead of the interface (the phone), plain frame, sharpening
// encode (source/fg/fg_hud.h, "THE PHONE"; the shader is hdr_encode_ps.hlsl, HDR_FGMRT with HDR_FGOV).
#define HDR_FGMRT 1
#define HDR_FGOV 1
#define HDR_G10 1
#include "../../shaders/hdr_encode_ps.hlsl"
