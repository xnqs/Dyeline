// Diffusion Filter: the soft glow of a diffusion filter in front of the lens. Part of each
// pixel's light is spread over its neighbors with a wide blur, so highlights bloom and
// contrast softens, while the total light stays the same.
//
// Diffusion is a spatial tool and runs in two halves around a blur (see chain_kernel.h):
//   extract:  e = c * w(Y), the share of the light that scatters
//   combine:  out = c + amount * (blur(e) - e)
// With Highlights 0 every tone scatters equally and the result is a mix toward the blurred
// image; raising it scatters mostly the light from bright areas.
#ifndef DYELINE_KERNELS_TOOLS_DIFFUSION_H
#define DYELINE_KERNELS_TOOLS_DIFFUSION_H

#include "../dl_kernel.h"

// p[0]: amount; p[1]: highlights, 0 to 1; p[2..4]: the luminance weights. p[15], used by
// the host only, holds the blur size.
DL_DEVICE float dl_diffusion_weight(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[2] * c.x + p[3] * c.y + p[4] * c.z;
    // 0 from a stop under middle grey, rising to 1 three stops over it.
    const float t = y > 0.0f ? dl_smoothstep(-1.0f, 3.0f, dl_log2(y / 0.18f)) : 0.0f;
    return 1.0f - p[1] * (1.0f - t);
}

DL_DEVICE dl_float4 dl_tool_diffusion_extract(dl_float4 c, DL_CONSTANT const float* p) {
    const float w = dl_diffusion_weight(c, p);
    return dl_make_float4(c.x * w, c.y * w, c.z * w, c.w);
}

DL_DEVICE dl_float4 dl_tool_diffusion_combine(dl_float4 c, DL_CONSTANT const float* p,
                                              dl_float4 blurred) {
    const float w = dl_diffusion_weight(c, p);
    return dl_make_float4(c.x + p[0] * (blurred.x - c.x * w), c.y + p[0] * (blurred.y - c.y * w),
                          c.z + p[0] * (blurred.z - c.z * w), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_DIFFUSION_H
