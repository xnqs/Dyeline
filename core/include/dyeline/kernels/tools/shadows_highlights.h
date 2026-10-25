// Shadows/Highlights: lifts or lowers the shadows and the highlights separately, in stops,
// fading each in from the pivot grey. The change follows the pixel's luminance and is applied
// as a gain, so hues don't shift.
//
// With L the luminance in stops from the pivot and W the fade width in stops:
//   stops added = shadows * (1 - smoothstep(-W, 0, L)) + highlights * smoothstep(0, W, L)
// The steepest fade slope is 1.5 / W, so with |shadows|, |highlights| <= 2 and W = 6 the
// result keeps rising with the input: tones never swap places.
#ifndef DYELINE_KERNELS_TOOLS_SHADOWS_HIGHLIGHTS_H
#define DYELINE_KERNELS_TOOLS_SHADOWS_HIGHLIGHTS_H

#include "../dl_kernel.h"

DL_DEVICE float dl_smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// p[0]: shadows, in stops; p[1]: highlights, in stops; p[2]: the pivot, in scene-linear;
// p[3]: the fade width W, in stops; p[4..6]: the luminance weights.
DL_DEVICE dl_float4 dl_tool_shadows_highlights(dl_float4 c, DL_CONSTANT const float* p) {
    const float y = p[4] * c.x + p[5] * c.y + p[6] * c.z;
    if (y <= 0.0f) {
        return c;
    }
    const float l = dl_log2(y / p[2]);
    const float stops = p[0] * (1.0f - dl_smoothstep(-p[3], 0.0f, l)) +
                        p[1] * dl_smoothstep(0.0f, p[3], l);
    const float gain = dl_exp2(stops);
    return dl_make_float4(c.x * gain, c.y * gain, c.z * gain, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_SHADOWS_HIGHLIGHTS_H
