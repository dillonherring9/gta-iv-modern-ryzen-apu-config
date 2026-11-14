// The real encode with frame generation's HUD-less picture as its second output, absorbed frame
// (source/fg/fg_hud.h; the shader is hdr_encode_ps.hlsl, HDR_FGMRT).
#define HDR_FGMRT 1
#define HDR_CGA 1
#include "../../shaders/hdr_encode_ps.hlsl"
