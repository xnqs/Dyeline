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
#include "../dl_tables.h"

DL_DEVICE float dl_lut_power(float v, float e) {
    return v > 0.0f ? dl_pow(v, e) : v;
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
        o = dl_lut_3d(t, n, x, y, z);
    }
    return dl_make_float4(dl_lut_power(o.x, p[10]), dl_lut_power(o.y, p[10]),
                          dl_lut_power(o.z, p[10]), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_LUT_H
