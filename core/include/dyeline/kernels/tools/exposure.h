// Exposure: scales RGB by a power of two, like opening or closing the lens.
#ifndef DYELINE_KERNELS_TOOLS_EXPOSURE_H
#define DYELINE_KERNELS_TOOLS_EXPOSURE_H

#include "../dl_kernel.h"

// p[0]: the gain, 2^stops, worked out on the host.
DL_DEVICE dl_float4 dl_tool_exposure(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(c.x * p[0], c.y * p[0], c.z * p[0], c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_EXPOSURE_H
