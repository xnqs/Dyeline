// 3-Way Grade: tints the shadows, the midtones and the highlights separately. Each range
// fades in and out over the pixel's luminance in stops from the pivot, like Shadows/Highlights,
// and its tint is a per-channel gain, so grey stays grey only where no tint reaches.
//
// With L the luminance in stops from the pivot and W the fade width in stops:
//   shadows weight    = 1 - smoothstep(-W, 0, L)
//   highlights weight = smoothstep(0, W, L)
//   midtones weight   = 1 - shadows weight - highlights weight
// and each channel gets 2^(the weighted sum of the three ranges' stops for that channel).
#ifndef DYELINE_KERNELS_TOOLS_THREE_WAY_H
#define DYELINE_KERNELS_TOOLS_THREE_WAY_H

#include "../dl_kernel.h"
#include "shadows_highlights.h"

// p[0..2]: the shadows' stops per channel; p[3..5]: the midtones'; p[6..8]: the highlights';
// p[9]: the pivot, in scene-linear; p[10]: the fade width W, in stops;
// p[11..13]: the luminance weights.
DL_DEVICE dl_float4 dl_tool_three_way(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[11] * c.x + p[12] * c.y + p[13] * c.z;
    if (y <= 0.0f) {
        return c;
    }
    const float l = dl_log2(y / p[9]);
    const float ws = 1.0f - dl_smoothstep(-p[10], 0.0f, l);
    const float wh = dl_smoothstep(0.0f, p[10], l);
    const float wm = 1.0f - ws - wh;
    return dl_make_float4(c.x * dl_exp2(ws * p[0] + wm * p[3] + wh * p[6]),
                          c.y * dl_exp2(ws * p[1] + wm * p[4] + wh * p[7]),
                          c.z * dl_exp2(ws * p[2] + wm * p[5] + wh * p[8]), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_THREE_WAY_H
