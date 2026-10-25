// A 3x3 color matrix, the op behind every tool that mixes RGB linearly: Hue/Saturation,
// Black & White and Channel Mixer. The host works out each tool's matrix once per frame.
#ifndef DYELINE_KERNELS_TOOLS_MATRIX_H
#define DYELINE_KERNELS_TOOLS_MATRIX_H

#include "../dl_kernel.h"

// p[0..8]: the matrix, row by row.
DL_DEVICE dl_float4 dl_tool_matrix(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(p[0] * c.x + p[1] * c.y + p[2] * c.z,
                          p[3] * c.x + p[4] * c.y + p[5] * c.z,
                          p[6] * c.x + p[7] * c.y + p[8] * c.z, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_MATRIX_H
