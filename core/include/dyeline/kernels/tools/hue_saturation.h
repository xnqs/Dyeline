// Hue/Saturation: turns colors around the grey axis and pushes them toward or away from grey.
// Both are linear in scene-linear RGB, so the host folds them into one 3x3 matrix.
#ifndef DYELINE_KERNELS_TOOLS_HUE_SATURATION_H
#define DYELINE_KERNELS_TOOLS_HUE_SATURATION_H

#include "../dl_kernel.h"

// p[0..8]: the matrix, row by row.
DL_DEVICE dl_float4 dl_tool_hue_saturation(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(p[0] * c.x + p[1] * c.y + p[2] * c.z,
                          p[3] * c.x + p[4] * c.y + p[5] * c.z,
                          p[6] * c.x + p[7] * c.y + p[8] * c.z, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_HUE_SATURATION_H
