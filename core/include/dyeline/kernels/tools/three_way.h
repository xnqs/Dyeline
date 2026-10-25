// 3-Way Grade (and 4-Way Wheels, which uses the same op): tints the shadows, the midtones and
// the highlights separately. Each range fades in and out over the pixel's luminance in stops
// from the pivot, like Shadows/Highlights, and its tint is a per-channel gain, so grey stays
// grey only where no tint reaches.
//
// With L the luminance in stops from the pivot and W the fade width in stops:
//   shadows weight    = 1 - smoothstep(-W, 0, L)
//   highlights weight = smoothstep(0, W, L)
//   midtones weight   = 1 - shadows weight - highlights weight
// and each channel gets 2^(the weighted sum of the three ranges' stops for that channel).
// The weights add up to 1, so stops added to all three ranges reach every tone alike.
//
// The grade's HSL and Sat EQ tabs then work on the color's hue and saturation, in the plane
// across the grey axis: each pixel's hue picks its share of the six hue bands (red, yellow,
// green, cyan, blue, magenta, 60 degrees apart, blending linearly between neighbors), which
// turn its hue, scale its saturation and add stops to colored pixels; and its luminance picks
// its share of the five Sat EQ bands (4 and 2 stops under the pivot, the pivot, 2 and 4 over),
// which scale its saturation again. Saturation here keeps the channels' average.
#ifndef DYELINE_KERNELS_TOOLS_THREE_WAY_H
#define DYELINE_KERNELS_TOOLS_THREE_WAY_H

#include "../dl_kernel.h"

#define DL_EQ_HUE_BANDS 6
#define DL_EQ_SAT_BANDS 5
// Floats in the EQ's table data: per hue band its turn (radians), saturation and stops, then
// per luminance band its saturation.
#define DL_EQ_TABLE_FLOATS (DL_EQ_HUE_BANDS * 3 + DL_EQ_SAT_BANDS)

// The EQ tabs on a color whose luminance is `l` stops from the pivot.
DL_DEVICE dl_float4 dl_three_way_eq(dl_float4 c, float l, DL_GLOBAL const float* t) {
    // Orthonormal axes across grey: `a` toward red, `b` toward yellow-green, so the angle from
    // `a` is 0 for red, 60 for yellow, 120 for green and so on.
    const float m = (c.x + c.y + c.z) / 3.0f;
    const float a = (2.0f * c.x - c.y - c.z) * 0.40824829f;  // 1 / sqrt(6)
    const float b = (c.y - c.z) * 0.70710678f;               // 1 / sqrt(2)
    const float chroma = dl_sqrt(a * a + b * b);
    float turn = 0.0f;
    float sat = 1.0f;
    float stops = 0.0f;
    if (chroma > 0.0f) {
        float x = dl_atan2(b, a) * 0.95492966f;  // in sixths of a turn: 3 / pi
        x = x < 0.0f ? x + 6.0f : x;
        int i = (int)x;
        i = i > 5 ? 5 : i;
        const float f = x - (float)i;
        const int j = i == 5 ? 0 : i + 1;
        turn = t[i * 3] + (t[j * 3] - t[i * 3]) * f;
        sat = t[i * 3 + 1] + (t[j * 3 + 1] - t[i * 3 + 1]) * f;
        // Lightness reaches colors, not greys: full from a chroma of a third of the mean.
        const float colored = m > 0.0f ? dl_smoothstep(0.0f, 0.33f * m, chroma) : 1.0f;
        stops = (t[i * 3 + 2] + (t[j * 3 + 2] - t[i * 3 + 2]) * f) * colored;
    }
    // Sat EQ: bands at -4, -2, 0, 2 and 4 stops, held beyond the ends.
    float y = (l + 4.0f) * 0.5f;
    y = y < 0.0f ? 0.0f : (y > 4.0f ? 4.0f : y);
    int k = (int)y;
    k = k > 3 ? 3 : k;
    DL_GLOBAL const float* e = t + DL_EQ_HUE_BANDS * 3;
    sat *= e[k] + (e[k + 1] - e[k]) * (y - (float)k);

    const float cs = dl_cos(turn) * sat;
    const float sn = dl_sin(turn) * sat;
    const float a2 = a * cs - b * sn;
    const float b2 = a * sn + b * cs;
    const float gain = dl_exp2(stops);
    // Back from (m, a, b) to RGB.
    const float r = m + a2 * 0.81649658f;  // 2 / sqrt(6)
    const float g = m - a2 * 0.40824829f + b2 * 0.70710678f;
    const float bl = m - a2 * 0.40824829f - b2 * 0.70710678f;
    return dl_make_float4(r * gain, g * gain, bl * gain, c.w);
}

// p[0..2]: the shadows' stops per channel; p[3..5]: the midtones'; p[6..8]: the highlights';
// p[9]: the pivot, in scene-linear; p[10]: the fade width W, in stops;
// p[11..13]: the luminance weights; p[14]: where the EQ tabs' table starts, or -1 when they
// change nothing.
DL_DEVICE dl_float4 dl_tool_three_way(dl_float4 c, DL_CONSTANT const float* p,
                                      DL_GLOBAL const float* table) {
    const float y = p[11] * c.x + p[12] * c.y + p[13] * c.z;
    if (y <= 0.0f) {
        return c;
    }
    const float l = dl_log2(y / p[9]);
    const float ws = 1.0f - dl_smoothstep(-p[10], 0.0f, l);
    const float wh = dl_smoothstep(0.0f, p[10], l);
    const float wm = 1.0f - ws - wh;
    const dl_float4 tinted =
        dl_make_float4(c.x * dl_exp2(ws * p[0] + wm * p[3] + wh * p[6]),
                       c.y * dl_exp2(ws * p[1] + wm * p[4] + wh * p[7]),
                       c.z * dl_exp2(ws * p[2] + wm * p[5] + wh * p[8]), c.w);
    return p[14] < 0.0f ? tinted : dl_three_way_eq(tinted, l, table + (int)p[14]);
}

#endif  // DYELINE_KERNELS_TOOLS_THREE_WAY_H
