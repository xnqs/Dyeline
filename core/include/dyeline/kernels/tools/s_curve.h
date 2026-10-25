// S Curve: adds contrast through the mid-tones while rolling off the shadows and highlights,
// like a film print. It works in stops around a pivot grey:
//
//   u = log2(v / pivot) / range          (-1 and +1 are `range` stops below and above pivot)
//   f(u) = u (1 + a) / (1 + a |u|)       for |u| <= 1
//   f(u) = sign(u) (1 + (|u| - 1) / (1 + a))   beyond, continuing with the same slope
//
// f is smooth and always rising, keeps the pivot and the two ends where they are, has slope
// 1 + a at the pivot and 1 / (1 + a) at the ends, so detail is never crushed to a flat line.
#ifndef DYELINE_KERNELS_TOOLS_S_CURVE_H
#define DYELINE_KERNELS_TOOLS_S_CURVE_H

#include "../dl_kernel.h"

DL_DEVICE float dl_s_curve_shape(float u, float a) {
    const float m = dl_fabs(u);
    const float f = m <= 1.0f ? m * (1.0f + a) / (1.0f + a * m) : 1.0f + (m - 1.0f) / (1.0f + a);
    return u < 0.0f ? -f : f;
}

// p[0]: the curve's strength a (0 leaves the image unchanged); p[1]: the pivot, in
// scene-linear; p[2]: the range in stops either side of the pivot. Values at or below zero
// pass through.
DL_DEVICE float dl_s_curve_channel(float v, DL_CONSTANT const float* p) {
    if (v <= 0.0f) {
        return v;
    }
    const float u = dl_log2(v / p[1]) / p[2];
    return p[1] * dl_exp2(dl_s_curve_shape(u, p[0]) * p[2]);
}

DL_DEVICE dl_float4 dl_tool_s_curve(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(dl_s_curve_channel(c.x, p), dl_s_curve_channel(c.y, p),
                          dl_s_curve_channel(c.z, p), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_S_CURVE_H
