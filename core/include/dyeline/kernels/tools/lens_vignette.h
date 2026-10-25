// Lens Vignette: darkens (or brightens) the frame toward its edges, the way a lens loses
// light off-axis. The change is in stops and fades in with distance from the center.
//
// Distance is measured so that it is 1 at the frame's corners. With Roundness 1 the falloff is
// a circle; with Roundness 0 it is an ellipse that follows the frame's shape.
#ifndef DYELINE_KERNELS_TOOLS_LENS_VIGNETTE_H
#define DYELINE_KERNELS_TOOLS_LENS_VIGNETTE_H

#include "../dl_kernel.h"

// p[0]: stops at full falloff; p[1]: the distance where the falloff begins; p[2]: how far it
// takes to reach full; p[3]: roundness, 0 to 1; p[4], p[5]: the center, as a fraction of the
// frame's half width and half height from its middle.
DL_DEVICE dl_float4 dl_tool_lens_vignette(dl_float4 c, DL_CONSTANT const float* p,
                                          struct dl_place at) {
    const float half_w = 0.5f * (at.frame_right - at.frame_left);
    const float half_h = 0.5f * (at.frame_top - at.frame_bottom);
    if (half_w <= 0.0f || half_h <= 0.0f) {
        return c;
    }
    const float dx = at.x - (0.5f * (at.frame_left + at.frame_right) + p[4] * half_w);
    const float dy = at.y - (0.5f * (at.frame_bottom + at.frame_top) + p[5] * half_h);
    const float half_diagonal = dl_sqrt(half_w * half_w + half_h * half_h);
    // Scales that make a corner distance 1: per axis for the ellipse, shared for the circle.
    const float ellipse = 0.70710678f;  // 1 / sqrt(2)
    const float sx = ellipse / half_w + (1.0f / half_diagonal - ellipse / half_w) * p[3];
    const float sy = ellipse / half_h + (1.0f / half_diagonal - ellipse / half_h) * p[3];
    const float r = dl_sqrt(dx * sx * dx * sx + dy * sy * dy * sy);
    const float gain = dl_exp2(p[0] * dl_smoothstep(p[1], p[1] + p[2], r));
    return dl_make_float4(c.x * gain, c.y * gain, c.z * gain, c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_LENS_VIGNETTE_H
