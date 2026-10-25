// The selective tools: changes limited to part of the frame (a spot or one side of a line) or
// to the shadows. Gradient Tint, Grad Exposure and Spot Exposure scale each channel by a
// number of stops, weighted by where the pixel is; Fill Light and Spot Fill add stops to the
// shadows, weighted by how dark the pixel is (and, for Spot Fill, where it is).
//
// Positions are those Lens Vignette uses: the center is a fraction of the frame's half width
// and half height from its middle, with y up, and distances are 1 at the frame's corners, so a
// spot is a circle and a look renders the same at any resolution and render scale.
#ifndef DYELINE_KERNELS_TOOLS_SELECTIVE_H
#define DYELINE_KERNELS_TOOLS_SELECTIVE_H

#include "../dl_kernel.h"

// The pixel's offset from the center (cx, cy), in half diagonals, into dx and dy.
DL_DEVICE int dl_selective_offset(struct dl_place at, float cx, float cy, DL_PRIVATE float* dx,
                                  DL_PRIVATE float* dy) {
    const float half_w = 0.5f * (at.frame_right - at.frame_left);
    const float half_h = 0.5f * (at.frame_top - at.frame_bottom);
    if (half_w <= 0.0f || half_h <= 0.0f) {
        return 0;
    }
    const float scale = 1.0f / dl_sqrt(half_w * half_w + half_h * half_h);
    *dx = (at.x - (0.5f * (at.frame_left + at.frame_right) + cx * half_w)) * scale;
    *dy = (at.y - (0.5f * (at.frame_bottom + at.frame_top) + cy * half_h)) * scale;
    return 1;
}

// 1 inside a spot of radius `size` around (cx, cy), fading to 0 over `softness` beyond it.
DL_DEVICE float dl_spot_weight(struct dl_place at, float cx, float cy, float size, float softness) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (!dl_selective_offset(at, cx, cy, &dx, &dy)) {
        return 0.0f;
    }
    return 1.0f - dl_smoothstep(size, size + softness, dl_sqrt(dx * dx + dy * dy));
}

// 1 on the side of the line through (cx, cy) that (sx, sy) points to, 0 on the other, fading
// across a band `softness` wide centered on the line.
DL_DEVICE float dl_gradient_weight(struct dl_place at, float cx, float cy, float sx, float sy,
                                   float softness) {
    float dx = 0.0f;
    float dy = 0.0f;
    if (!dl_selective_offset(at, cx, cy, &dx, &dy)) {
        return 0.0f;
    }
    const float t = dx * sx + dy * sy;
    return dl_smoothstep(-0.5f * softness, 0.5f * softness, t);
}

DL_DEVICE dl_float4 dl_selective_gain(dl_float4 c, DL_CONSTANT const float* stops, float w) {
    return dl_make_float4(c.x * dl_exp2(w * stops[0]), c.y * dl_exp2(w * stops[1]),
                          c.z * dl_exp2(w * stops[2]), c.w);
}

// Gradient Tint and Grad Exposure. p[0..2]: stops per channel; p[3], p[4]: the center;
// p[5], p[6]: the direction the change lies in (sin and cos of the angle clockwise from up);
// p[7]: softness.
DL_DEVICE dl_float4 dl_tool_gradient(dl_float4 c, DL_CONSTANT const float* p, struct dl_place at) {
    return dl_selective_gain(c, p, dl_gradient_weight(at, p[3], p[4], p[5], p[6], p[7]));
}

// Spot Exposure. p[0..2]: stops per channel; p[3], p[4]: the center; p[5]: size; p[6]:
// softness.
DL_DEVICE dl_float4 dl_tool_spot(dl_float4 c, DL_CONSTANT const float* p, struct dl_place at) {
    return dl_selective_gain(c, p, dl_spot_weight(at, p[3], p[4], p[5], p[6]));
}

// Fill Light and Spot Fill. The stops fade from full three stops under the pivot to none a
// stop over it. That band is wide enough that, with at most 2 stops of fill, a brighter pixel
// always stays brighter. p[0..2]: stops per channel; p[3]: pivot; p[4..6]: the luminance
// weights; p[7]: 1 for Spot Fill; p[8], p[9]: the spot's center; p[10]: its size; p[11]: its
// softness.
DL_DEVICE dl_float4 dl_tool_fill(dl_float4 c, DL_CONSTANT const float* p, struct dl_place at) {
    const float y = p[4] * c.x + p[5] * c.y + p[6] * c.z;
    float w = y > 0.0f ? 1.0f - dl_smoothstep(-3.0f, 1.0f, dl_log2(y / p[3])) : 1.0f;
    if (p[7] != 0.0f) {
        w *= dl_spot_weight(at, p[8], p[9], p[10], p[11]);
    }
    return dl_selective_gain(c, p, w);
}

#endif  // DYELINE_KERNELS_TOOLS_SELECTIVE_H
