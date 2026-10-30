// Curves: a master curve and one per channel, drawn through points (core/src/curves.cpp).
// The graph spans `range` stops either side of middle grey (0.18) on both axes, so for a
// value v:
//
//   u = 1/2 + log2(v / 0.18) / (2 range)       0 and 1 are the graph's left and right edges
//   v' = 0.18 * 2^((curve(u) - 1/2) * 2 range)
//
// Each curve is sampled into DL_TONE_CURVE_SAMPLES points of the chain's table, and continues
// past the graph's edges in a straight line along its end slopes, which the host keeps from
// falling. Values at or below zero pass through.
//
// The master curve runs on each channel, or, with "Master On" set to Luminance, on the pixel's
// luminance, scaling all three channels together so hue and saturation stay. The channel
// curves run after it.
#ifndef DYELINE_KERNELS_TOOLS_CURVES_H
#define DYELINE_KERNELS_TOOLS_CURVES_H

#include "../dl_kernel.h"

#define DL_TONE_CURVE_SAMPLES 1024
#define DL_TONE_CURVE_GREY 0.18f

// Curve `t` (DL_TONE_CURVE_SAMPLES values) at graph position u, with slopes `lo` and `hi`
// past its edges.
DL_DEVICE float dl_tone_curve_at(DL_GLOBAL const float* t, float u, float lo, float hi) {
    const float last = (float)(DL_TONE_CURVE_SAMPLES - 1);
    if (u <= 0.0f) {
        return t[0] + u * lo;
    }
    if (u >= 1.0f) {
        return t[DL_TONE_CURVE_SAMPLES - 1] + (u - 1.0f) * hi;
    }
    const float s = u * last;
    int i = (int)s;
    if (i > DL_TONE_CURVE_SAMPLES - 2) {
        i = DL_TONE_CURVE_SAMPLES - 2;
    }
    const float f = s - (float)i;
    return t[i] + (t[i + 1] - t[i]) * f;
}

// p[1]: the graph's width in stops (2 range); p[2]: its inverse. `k` picks the curve (0 the
// master, 1 to 3 red, green and blue).
DL_DEVICE float dl_tone_curve_value(float v, DL_CONSTANT const float* p, DL_GLOBAL const float* table,
                                    int k) {
    if (v <= 0.0f) {
        return v;
    }
    DL_GLOBAL const float* t = table + (int)p[0] + k * DL_TONE_CURVE_SAMPLES;
    const float u = 0.5f + dl_log2(v / DL_TONE_CURVE_GREY) * p[2];
    const float y = dl_tone_curve_at(t, u, p[7 + 2 * k], p[8 + 2 * k]);
    return DL_TONE_CURVE_GREY * dl_exp2((y - 0.5f) * p[1]);
}

// p[0]: where the four curves start in the chain's table data (master, red, green, blue);
// p[3]: 1 when the master curve runs on luminance; p[4..6]: the luminance weights; p[7..14]:
// each curve's slopes before and after the graph, in the same order.
DL_DEVICE dl_float4 dl_tool_curves(dl_float4 c, DL_CONSTANT const float* p,
                                   DL_GLOBAL const float* table) {
    float r = c.x;
    float g = c.y;
    float b = c.z;
    if (p[3] != 0.0f) {
        const float l = p[4] * r + p[5] * g + p[6] * b;
        if (l > 0.0f) {
            const float scale = dl_tone_curve_value(l, p, table, 0) / l;
            r *= scale;
            g *= scale;
            b *= scale;
        }
    } else {
        r = dl_tone_curve_value(r, p, table, 0);
        g = dl_tone_curve_value(g, p, table, 0);
        b = dl_tone_curve_value(b, p, table, 0);
    }
    return dl_make_float4(dl_tone_curve_value(r, p, table, 1), dl_tone_curve_value(g, p, table, 2),
                          dl_tone_curve_value(b, p, table, 3), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_CURVES_H
