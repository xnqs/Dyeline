// LUT: looks colors up in a 1D or 3D table loaded from a .cube file. 3D tables use
// tetrahedral interpolation, which reproduces any table made from a linear transform exactly
// and keeps grey ramps smooth. Values outside the table's domain are held at its edges.
//
// Most LUTs expect display-encoded values, not scene-linear ones. With LUT Gamma g, each value
// is raised to 1/g before the lookup and the result to g after, so a LUT built for gamma-2.4
// video can be applied to linear light.
#ifndef DYELINE_KERNELS_TOOLS_LUT_H
#define DYELINE_KERNELS_TOOLS_LUT_H

#include "../dl_kernel.h"

DL_DEVICE float dl_lut_power(float v, float e) {
    return v > 0.0f ? dl_pow(v, e) : v;
}

// A position in table points, clamped to [0, last].
DL_DEVICE float dl_lut_coord(float v, float lo, float scale, float last) {
    const float x = (v - lo) * scale;
    return x < 0.0f ? 0.0f : (x > last ? last : x);
}

DL_DEVICE dl_float4 dl_lut_node(DL_GLOBAL const float* t, int i) {
    return dl_make_float4(t[i], t[i + 1], t[i + 2], 0.0f);
}

// a + (b - a) * f, on RGB.
DL_DEVICE dl_float4 dl_lut_step(dl_float4 acc, dl_float4 a, dl_float4 b, float f) {
    return dl_make_float4(acc.x + (b.x - a.x) * f, acc.y + (b.y - a.y) * f,
                          acc.z + (b.z - a.z) * f, 0.0f);
}

DL_DEVICE float dl_lut_1d(DL_GLOBAL const float* t, int n, float x, int channel) {
    int i = (int)x;  // x >= 0, so this is floor
    if (i > n - 2) {
        i = n - 2;
    }
    const float f = x - (float)i;
    const float a = t[i * 3 + channel];
    return a + (t[(i + 1) * 3 + channel] - a) * f;
}

// p[0]: where the table starts in the chain's table data, in floats; p[1]: points per axis N;
// p[2]: 1 for a 3D table, 0 for 1D; p[3..5]: the domain minimum; p[6..8]: (N - 1) divided by
// the domain's width; p[9]: 1 / LUT gamma; p[10]: LUT gamma.
DL_DEVICE dl_float4 dl_tool_lut(dl_float4 c, DL_CONSTANT const float* p,
                                DL_GLOBAL const float* table) {
    DL_GLOBAL const float* t = table + (int)p[0];
    const int n = (int)p[1];
    const float last = (float)(n - 1);
    const float x = dl_lut_coord(dl_lut_power(c.x, p[9]), p[3], p[6], last);
    const float y = dl_lut_coord(dl_lut_power(c.y, p[9]), p[4], p[7], last);
    const float z = dl_lut_coord(dl_lut_power(c.z, p[9]), p[5], p[8], last);
    dl_float4 o;
    if (p[2] == 0.0f) {
        o = dl_make_float4(dl_lut_1d(t, n, x, 0), dl_lut_1d(t, n, y, 1), dl_lut_1d(t, n, z, 2), 0.0f);
    } else {
        int ir = (int)x;
        int ig = (int)y;
        int ib = (int)z;
        ir = ir > n - 2 ? n - 2 : ir;
        ig = ig > n - 2 ? n - 2 : ig;
        ib = ib > n - 2 ? n - 2 : ib;
        const float fr = x - (float)ir;
        const float fg = y - (float)ig;
        const float fb = z - (float)ib;
        // Node offsets in floats: red changes fastest, then green, then blue.
        const int sr = 3;
        const int sg = 3 * n;
        const int sb = 3 * n * n;
        const int base = ir * sr + ig * sg + ib * sb;
        const dl_float4 c000 = dl_lut_node(t, base);
        const dl_float4 c111 = dl_lut_node(t, base + sr + sg + sb);
        // Walk from c000 to c111 along the edges of the tetrahedron holding the point.
        if (fr > fg) {
            if (fg > fb) {
                const dl_float4 c100 = dl_lut_node(t, base + sr);
                const dl_float4 c110 = dl_lut_node(t, base + sr + sg);
                o = dl_lut_step(c000, c000, c100, fr);
                o = dl_lut_step(o, c100, c110, fg);
                o = dl_lut_step(o, c110, c111, fb);
            } else if (fr > fb) {
                const dl_float4 c100 = dl_lut_node(t, base + sr);
                const dl_float4 c101 = dl_lut_node(t, base + sr + sb);
                o = dl_lut_step(c000, c000, c100, fr);
                o = dl_lut_step(o, c100, c101, fb);
                o = dl_lut_step(o, c101, c111, fg);
            } else {
                const dl_float4 c001 = dl_lut_node(t, base + sb);
                const dl_float4 c101 = dl_lut_node(t, base + sr + sb);
                o = dl_lut_step(c000, c000, c001, fb);
                o = dl_lut_step(o, c001, c101, fr);
                o = dl_lut_step(o, c101, c111, fg);
            }
        } else {
            if (fb > fg) {
                const dl_float4 c001 = dl_lut_node(t, base + sb);
                const dl_float4 c011 = dl_lut_node(t, base + sg + sb);
                o = dl_lut_step(c000, c000, c001, fb);
                o = dl_lut_step(o, c001, c011, fg);
                o = dl_lut_step(o, c011, c111, fr);
            } else if (fb > fr) {
                const dl_float4 c010 = dl_lut_node(t, base + sg);
                const dl_float4 c011 = dl_lut_node(t, base + sg + sb);
                o = dl_lut_step(c000, c000, c010, fg);
                o = dl_lut_step(o, c010, c011, fb);
                o = dl_lut_step(o, c011, c111, fr);
            } else {
                const dl_float4 c010 = dl_lut_node(t, base + sg);
                const dl_float4 c110 = dl_lut_node(t, base + sr + sg);
                o = dl_lut_step(c000, c000, c010, fg);
                o = dl_lut_step(o, c010, c110, fr);
                o = dl_lut_step(o, c110, c111, fb);
            }
        }
    }
    return dl_make_float4(dl_lut_power(o.x, p[10]), dl_lut_power(o.y, p[10]),
                          dl_lut_power(o.z, p[10]), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_LUT_H
