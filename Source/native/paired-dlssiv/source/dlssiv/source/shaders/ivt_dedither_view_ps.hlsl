// The view of the dither pass (development builds only; dedither=11 or 12): the same decision as ivt_dedither_ps.hlsl,
// painted instead of written: magenta where a pixel would move, a green tint where the mark was set and nothing moves.
#define DED_VIEW 1
#include "ivt_dedither_ps.hlsl"
