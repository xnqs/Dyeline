// Lens Distortion and Chromatic Aberration: tools that move the image rather than recolor it.
//
// A warp tool says, for each output pixel, where in its input each channel is read from. Its
// input is rendered to an image first and read back with bilinear filtering (see
// chain_kernel.h), so these functions only map positions. Positions are canonical
// coordinates (see dl_place). Like Lens Vignette, distances from the lens's center are
// measured so the frame's corners are at 1, whatever the frame's shape.
//
// Strength scales how far pixels move, so half strength is half the distortion rather than a
// double image.
#ifndef DYELINE_KERNELS_TOOLS_LENS_WARP_H
#define DYELINE_KERNELS_TOOLS_LENS_WARP_H

#include "../dl_kernel.h"

// The lens's center, from a center parameter pair given as fractions of the frame's half
// width and half height, and the frame's half diagonal.
DL_DEVICE void dl_lens_center(float center_x, float center_y, struct dl_place at,
                              DL_PRIVATE float* cx, DL_PRIVATE float* cy,
                              DL_PRIVATE float* half_diagonal) {
    const float half_w = 0.5f * (at.frame_right - at.frame_left);
    const float half_h = 0.5f * (at.frame_top - at.frame_bottom);
    *cx = 0.5f * (at.frame_left + at.frame_right) + center_x * half_w;
    *cy = 0.5f * (at.frame_bottom + at.frame_top) + center_y * half_h;
    *half_diagonal = dl_sqrt(half_w * half_w + half_h * half_h);
}

// Lens Distortion, the radial part of the Brown-Conrady model: a pixel at distance r from the
// center reads from r * (1 + k1 r^2 + k2 r^4) / zoom. Positive k1 bows straight lines outward
// (barrel), negative pinches them in (pincushion); k2 bends the corners on its own.
// p[0]: k1; p[1]: k2; p[2]: 1 / zoom; p[3], p[4]: the center.
DL_DEVICE void dl_lens_distortion_source(DL_CONSTANT const float* p, float strength,
                                         struct dl_place at, DL_PRIVATE float* sx,
                                         DL_PRIVATE float* sy) {
    float cx = 0.0f;
    float cy = 0.0f;
    float half_diagonal = 0.0f;
    dl_lens_center(p[3], p[4], at, &cx, &cy, &half_diagonal);
    const float dx = at.x - cx;
    const float dy = at.y - cy;
    const float r2 = half_diagonal > 0.0f ? (dx * dx + dy * dy) / (half_diagonal * half_diagonal)
                                          : 0.0f;
    const float f = (1.0f + p[0] * r2 + p[1] * r2 * r2) * p[2];
    const float scale = 1.0f + (f - 1.0f) * strength;
    *sx = cx + dx * scale;
    *sy = cy + dy * scale;
}

// Chromatic Aberration. Lateral: a lens magnifies red, green and blue slightly differently,
// so red and blue images are scaled about the center against green, splitting colors apart
// more toward the edges. Longitudinal: red and blue focus a little in front of or behind
// green, so they are softened by a small disc blur everywhere, which leaves colored fringes
// along high-contrast edges.
// p[0], p[1]: how far red and blue move at the corners, as fractions of the frame's width
// (positive is outward); p[2]: the radius of the red and blue blur, as a fraction of the
// frame's width; p[3], p[4]: the center.
#define DL_CA_FRINGE_TAPS 7

DL_DEVICE int dl_chromatic_aberration_taps(DL_CONSTANT const float* p, int channel) {
    return channel != 1 && p[2] > 0.0f ? DL_CA_FRINGE_TAPS : 1;
}

DL_DEVICE void dl_chromatic_aberration_source(DL_CONSTANT const float* p, float strength,
                                              struct dl_place at, int channel, int tap,
                                              DL_PRIVATE float* sx, DL_PRIVATE float* sy) {
    if (channel == 1) {
        *sx = at.x;
        *sy = at.y;
        return;
    }
    float cx = 0.0f;
    float cy = 0.0f;
    float half_diagonal = 0.0f;
    dl_lens_center(p[3], p[4], at, &cx, &cy, &half_diagonal);
    const float width = at.frame_right - at.frame_left;
    const float shift = channel == 0 ? p[0] : p[1];
    const float scale =
        half_diagonal > 0.0f ? 1.0f - strength * shift * width / half_diagonal : 1.0f;
    // An output pixel reads from nearer the center to move its channel outward.
    *sx = cx + (at.x - cx) * scale;
    *sy = cy + (at.y - cy) * scale;
    if (tap > 0) {
        // A hexagon of six taps around the center one: a small disc blur.
        const float radius = strength * p[2] * width;
        const float angle = 1.04719755f * (float)(tap - 1);  // 60 degrees apart
        *sx += radius * dl_cos(angle);
        *sy += radius * dl_sin(angle);
    }
}

#endif  // DYELINE_KERNELS_TOOLS_LENS_WARP_H
