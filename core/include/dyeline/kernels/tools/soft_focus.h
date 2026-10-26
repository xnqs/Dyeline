// Edge Softness and Swing-Tilt: blur that depends on where the pixel is. Edge Softness blurs
// toward the frame's edges, the way a simple lens loses sharpness off-axis; Swing-Tilt keeps
// a band across the frame sharp and blurs away from it, like a lens tilted against the film.
//
// Both are two-level spatial tools (see chain_kernel.h): the extract half spreads the color
// itself, the host blurs it by a third of Blur and then to the full Blur, and the combine half
// picks between the sharp pixel, the light blur and the full one by a weight from 0 (sharp) to
// 1 (full), so blur grows smoothly instead of a blurred copy fading in over a sharp one.
#ifndef DYELINE_KERNELS_TOOLS_SOFT_FOCUS_H
#define DYELINE_KERNELS_TOOLS_SOFT_FOCUS_H

#include "../dl_kernel.h"
// The GPU backends join the tool headers in alphabetical order, so this file's name must sort
// after selective.h, whose functions it uses.
#include "selective.h"

DL_DEVICE dl_float4 dl_lens_blur_mix(dl_float4 c, dl_float4 fine, dl_float4 full, float w) {
    const float t = 2.0f * (w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w));
    if (t <= 1.0f) {
        return dl_make_float4(c.x + (fine.x - c.x) * t, c.y + (fine.y - c.y) * t,
                              c.z + (fine.z - c.z) * t, c.w);
    }
    const float u = t - 1.0f;
    return dl_make_float4(fine.x + (full.x - fine.x) * u, fine.y + (full.y - fine.y) * u,
                          fine.z + (full.z - fine.z) * u, c.w);
}

// Edge Softness. p[0]: the distance where softening begins (1 is a corner); p[1]: how far past
// it the blur is full; p[2], p[3]: the center. p[15], used by the host only, holds the blur
// size.
DL_DEVICE float dl_edge_softness_weight(DL_CONSTANT const float* p, struct dl_place at) {
    return 1.0f - dl_spot_weight(at, p[2], p[3], p[0], p[1]);
}

// Swing-Tilt. p[0]: half the sharp band's width, in half diagonals; p[1]: how far past it the
// blur is full; p[2], p[3]: the center; p[4], p[5]: the band's normal (sin and cos of the angle
// clockwise from up).
DL_DEVICE float dl_swing_tilt_weight(DL_CONSTANT const float* p, struct dl_place at) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (!dl_selective_offset(at, p[2], p[3], &dx, &dy)) {
        return 0.0f;
    }
    return dl_smoothstep(p[0], p[0] + p[1], dl_fabs(dx * p[4] + dy * p[5]));
}

DL_DEVICE dl_float4 dl_tool_lens_blur_extract(dl_float4 c) {
    return c;
}

#endif  // DYELINE_KERNELS_TOOLS_SOFT_FOCUS_H
