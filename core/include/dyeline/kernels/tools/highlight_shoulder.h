// Highlight Shoulder: rolls values above a start point smoothly toward a ceiling, the way film
// eases into its brightest tones instead of clipping:
//
//   v' = v                                          for v <= s
//   v' = s + (c - s) (1 - exp(-(v - s) / (c - s)))  above
//
// The slope is 1 at s, so the join is invisible, and v' rises toward c without reaching it.
// Each channel is rolled separately, so very bright colors also lose some saturation, as on
// film.
#ifndef DYELINE_KERNELS_TOOLS_HIGHLIGHT_SHOULDER_H
#define DYELINE_KERNELS_TOOLS_HIGHLIGHT_SHOULDER_H

#include "../dl_kernel.h"

#define DL_LOG2_E 1.44269504f

DL_DEVICE float dl_shoulder_channel(float v, float start, float room) {
    if (v <= start) {
        return v;
    }
    return start + room * (1.0f - dl_exp2(-(v - start) / room * DL_LOG2_E));
}

// p[0]: the start s; p[1]: the room above it, c - s. Both scene-linear.
DL_DEVICE dl_float4 dl_tool_highlight_shoulder(dl_float4 c, DL_CONSTANT const float* p) {
    return dl_make_float4(dl_shoulder_channel(c.x, p[0], p[1]), dl_shoulder_channel(c.y, p[0], p[1]),
                          dl_shoulder_channel(c.z, p[0], p[1]), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_HIGHLIGHT_SHOULDER_H
