// Halation: the reddish glow film shows around bright highlights, from light that passes
// through the emulsion, reflects off the film base and exposes the red-sensitive layer
// again around where it came in.
//
// Halation is a spatial tool and runs in two halves around a blur (see chain_kernel.h):
//   extract:  h = max(c - threshold, 0), the light above the threshold
//   combine:  out = c + amount * tint * blur(h)
#ifndef DYELINE_KERNELS_TOOLS_HALATION_H
#define DYELINE_KERNELS_TOOLS_HALATION_H

#include "../dl_kernel.h"

// p[0]: amount; p[1]: threshold, in scene-linear; p[2..4]: the glow's tint. p[15], used by
// the host only, holds the blur size.
DL_DEVICE dl_float4 dl_tool_halation_extract(dl_float4 c, DL_CONSTANT const float* p) {
    const float t = p[1];
    return dl_make_float4(c.x > t ? c.x - t : 0.0f, c.y > t ? c.y - t : 0.0f,
                          c.z > t ? c.z - t : 0.0f, c.w);
}

DL_DEVICE dl_float4 dl_tool_halation_combine(dl_float4 c, DL_CONSTANT const float* p,
                                             dl_float4 blurred) {
    // The glow takes its strength from all three channels, and its color from the tint.
    const float glow = p[0] * (blurred.x + blurred.y + blurred.z) * (1.0f / 3.0f);
    return dl_make_float4(c.x + glow * p[2], c.y + glow * p[3], c.z + glow * p[4], c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_HALATION_H
