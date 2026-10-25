// Bleach Bypass: skipping the bleach step leaves the silver image in the film alongside its
// dyes. The silver is a black-and-white copy of the image laid over the color one, so the
// result is less colorful and more contrasty, with denser blacks.
//
// The silver darkens each pixel by the same factor in every channel, set by its luminance Y:
// (Y / pivot)^(amount / 2), so grey at the pivot stays put and the luminance contrast rises
// from 1 to 1.5 at Amount 1. Colors move toward their luminance by up to 65%.
#ifndef DYELINE_KERNELS_TOOLS_BLEACH_BYPASS_H
#define DYELINE_KERNELS_TOOLS_BLEACH_BYPASS_H

#include "../dl_kernel.h"

// p[0]: amount, 0 to 1; p[1]: pivot; p[2..4]: the luminance weights.
DL_DEVICE dl_float4 dl_tool_bleach_bypass(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[2] * c.x + p[3] * c.y + p[4] * c.z;
    const float keep = 1.0f - 0.65f * p[0];
    const float silver = y > 0.0f ? dl_exp2(0.5f * p[0] * dl_log2(y / p[1])) : 0.0f;
    return dl_make_float4((y + (c.x - y) * keep) * silver, (y + (c.y - y) * keep) * silver,
                          (y + (c.z - y) * keep) * silver, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_BLEACH_BYPASS_H
