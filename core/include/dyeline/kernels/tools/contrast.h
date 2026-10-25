// Contrast: steepens or flattens the image around a pivot grey, evenly in stops, so a stop of
// difference above the pivot changes as much as a stop below it.
#ifndef DYELINE_KERNELS_TOOLS_CONTRAST_H
#define DYELINE_KERNELS_TOOLS_CONTRAST_H

#include "../dl_kernel.h"

// p[0]: the contrast (1 leaves the image unchanged); p[1]: the pivot, in scene-linear.
// Values at or below zero have no stops to scale and pass through.
DL_DEVICE float dl_contrast_channel(float v, float contrast, float pivot) {
    return v > 0.0f ? pivot * dl_pow(v / pivot, contrast) : v;
}

DL_DEVICE dl_float4 dl_tool_contrast(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(dl_contrast_channel(c.x, p[0], p[1]), dl_contrast_channel(c.y, p[0], p[1]),
                          dl_contrast_channel(c.z, p[0], p[1]), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_CONTRAST_H
