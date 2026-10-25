// Table lookups shared by the tools that read table data (LUT, Film Negative, Film Print).
// Tables are RGB triples: a 1D table holds one triple per point, a 3D table one per node.
#ifndef DYELINE_KERNELS_DL_TABLES_H
#define DYELINE_KERNELS_DL_TABLES_H

#include "dl_kernel.h"

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

// A 3D table of n points per axis at table positions (x, y, z), each in [0, n - 1], with
// tetrahedral interpolation.
DL_DEVICE dl_float4 dl_lut_3d(DL_GLOBAL const float* t, int n, float x, float y, float z) {
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
    dl_float4 o;
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
    return o;
}

#endif  // DYELINE_KERNELS_DL_TABLES_H
