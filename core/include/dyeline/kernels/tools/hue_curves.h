// Hue Curves: Hue vs Hue, Hue vs Sat and Hue vs Lum, curves around the hue circle drawn
// through points (core/src/curves.cpp). They work on the color's hue and saturation in the
// plane across the grey axis, as the 3-Way Grade's HSL tab does (three_way.h), but follow a
// smooth curve instead of six bands. The graph's x is the pixel's hue, from red (0) through
// yellow, green, cyan, blue and magenta back to red (1); y = 1/2 changes nothing, and at the
// graph's top and bottom:
//
//   Hue vs Hue   turns the hue by +/- DL_HUE_CURVE_TURN degrees
//   Hue vs Sat   scales saturation from 0 (grey) through 1 to 2, keeping the channels' average
//   Hue vs Lum   adds +/- DL_HUE_CURVE_STOPS stops, to colors only: full from a chroma of a
//                third of the mean, none on greys
//
// All three read the pixel's original hue. Each curve is sampled, already turned into radians,
// a saturation scale and stops, into DL_HUE_CURVE_SAMPLES points of the chain's table, which
// wrap around.
#ifndef DYELINE_KERNELS_TOOLS_HUE_CURVES_H
#define DYELINE_KERNELS_TOOLS_HUE_CURVES_H

#include "../dl_kernel.h"

#define DL_HUE_CURVE_SAMPLES 1024
#define DL_HUE_CURVE_TURN 90.0f
#define DL_HUE_CURVE_STOPS 2.0f

// p[0]: where the three curves start in the chain's table data (turn, saturation, stops).
DL_DEVICE dl_float4 dl_tool_hue_curves(dl_float4 c, DL_CONSTANT const float* p,
                                       DL_GLOBAL const float* table) {
    // Orthonormal axes across grey: `a` toward red, `b` toward yellow-green.
    const float m = (c.x + c.y + c.z) / 3.0f;
    const float a = (2.0f * c.x - c.y - c.z) * 0.40824829f;  // 1 / sqrt(6)
    const float b = (c.y - c.z) * 0.70710678f;               // 1 / sqrt(2)
    const float chroma = dl_sqrt(a * a + b * b);
    if (chroma <= 0.0f) {
        return c;
    }
    const float n = (float)DL_HUE_CURVE_SAMPLES;
    float x = dl_atan2(b, a) * (n * 0.15915494f);  // in samples: n / (2 pi)
    x = x < 0.0f ? x + n : x;
    int i = (int)x;
    i = i > DL_HUE_CURVE_SAMPLES - 1 ? DL_HUE_CURVE_SAMPLES - 1 : i;
    const float f = x - (float)i;
    const int j = i == DL_HUE_CURVE_SAMPLES - 1 ? 0 : i + 1;
    DL_GLOBAL const float* t = table + (int)p[0];
    DL_GLOBAL const float* ts = t + DL_HUE_CURVE_SAMPLES;
    DL_GLOBAL const float* tl = t + 2 * DL_HUE_CURVE_SAMPLES;
    const float turn = t[i] + (t[j] - t[i]) * f;
    const float sat = ts[i] + (ts[j] - ts[i]) * f;
    const float colored = m > 0.0f ? dl_smoothstep(0.0f, 0.33f * m, chroma) : 1.0f;
    const float stops = (tl[i] + (tl[j] - tl[i]) * f) * colored;

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

#endif  // DYELINE_KERNELS_TOOLS_HUE_CURVES_H
