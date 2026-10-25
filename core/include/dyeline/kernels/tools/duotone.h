// Duotone: the image as its luminance, colored by two tints, one for the shadows and one for
// the highlights, blending across the pivot. Each tint is a gain per channel whose
// luminance-weighted sum is 1, so luminance is kept and only the color is replaced.
#ifndef DYELINE_KERNELS_TOOLS_DUOTONE_H
#define DYELINE_KERNELS_TOOLS_DUOTONE_H

#include "../dl_kernel.h"

// p[0..2]: the shadow tint; p[3..5]: the highlight tint; p[6]: the pivot; p[7]: the blend's
// half width, in stops; p[8..10]: the luminance weights.
DL_DEVICE dl_float4 dl_tool_duotone(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[8] * c.x + p[9] * c.y + p[10] * c.z;
    if (y <= 0.0f) {
        return dl_make_float4(0.0f, 0.0f, 0.0f, c.w);
    }
    const float t = dl_smoothstep(-p[7], p[7], dl_log2(y / p[6]));
    return dl_make_float4(y * (p[0] + (p[3] - p[0]) * t), y * (p[1] + (p[4] - p[1]) * t),
                          y * (p[2] + (p[5] - p[2]) * t), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_DUOTONE_H
