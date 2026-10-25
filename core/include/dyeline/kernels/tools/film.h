// Film Negative and Film Print: a film stock's response, from tables built out of its
// datasheet (core/include/dyeline/film.h, scripts/film/make_film_data.py).
//
// Film Negative exposes the stock's three layers, through a matrix fitted to their spectral
// sensitivities, and returns the scan of the developed negative: each layer's characteristic
// curve, read as 0.18 * 10^((D - D_grey) / 0.6), so a grey card stays at 0.18.
//
// Film Print reads its input as such a scan, prints it (a denser negative passes less printer
// light) and shows the print: each layer's curve gives Status A densities, and a 3D table
// takes those to the projected image, with grey at 0.18 and the clear base at 1.0.
#ifndef DYELINE_KERNELS_TOOLS_FILM_H
#define DYELINE_KERNELS_TOOLS_FILM_H

#include "../dl_kernel.h"
#include "../dl_tables.h"

// Stops from 0.18, with values at or below zero held at the floor of the stock's tables.
DL_DEVICE float dl_film_stops(float v) {
    const float least = 0.18f / 16777216.0f;  // 24 stops under grey
    return dl_log2((v > least ? v : least) / 0.18f);
}

// p[0]: where the scan starts in the table data; p[1]: its points; p[2..10]: the layer
// exposure matrix; p[11]: the stops from grey of the scan's first point, less the Exposure;
// p[12]: points per stop.
DL_DEVICE dl_float4 dl_tool_film_negative(dl_float4 c, DL_CONSTANT const float* p,
                                          DL_GLOBAL const float* table) {
    DL_GLOBAL const float* t = table + (int)p[0];
    const int n = (int)p[1];
    const float last = (float)(n - 1);
    const float r = p[2] * c.x + p[3] * c.y + p[4] * c.z;
    const float g = p[5] * c.x + p[6] * c.y + p[7] * c.z;
    const float b = p[8] * c.x + p[9] * c.y + p[10] * c.z;
    const float sr = dl_lut_1d(t, n, dl_lut_coord(dl_film_stops(r), p[11], p[12], last), 0);
    const float sg = dl_lut_1d(t, n, dl_lut_coord(dl_film_stops(g), p[11], p[12], last), 1);
    const float sb = dl_lut_1d(t, n, dl_lut_coord(dl_film_stops(b), p[11], p[12], last), 2);
    return dl_make_float4(0.18f * dl_exp2(sr), 0.18f * dl_exp2(sg), 0.18f * dl_exp2(sb), c.w);
}

// Negative density per stop of a scan: 0.6 * log10(2).
#define DL_FILM_SCAN_DENSITY_PER_STOP 0.180618f

// p[0]: where the print curves start in the table data; p[1]: their points; p[2]: the log
// exposure of their first point; p[3]: points per unit of log exposure; p[4..6]: the log
// exposure each layer gets from a grey card, after printer lights and Exposure; p[7]: where
// the display table starts; p[8]: its points per axis; p[9..11]: its points per unit of
// density; p[12]: Contrast.
DL_DEVICE dl_float4 dl_tool_film_print(dl_float4 c, DL_CONSTANT const float* p,
                                       DL_GLOBAL const float* table) {
    DL_GLOBAL const float* curves = table + (int)p[0];
    const int n = (int)p[1];
    const float last = (float)(n - 1);
    const float er = p[4] - DL_FILM_SCAN_DENSITY_PER_STOP * dl_film_stops(c.x);
    const float eg = p[5] - DL_FILM_SCAN_DENSITY_PER_STOP * dl_film_stops(c.y);
    const float eb = p[6] - DL_FILM_SCAN_DENSITY_PER_STOP * dl_film_stops(c.z);
    const float dr = dl_lut_1d(curves, n, dl_lut_coord(er, p[2], p[3], last), 0);
    const float dg = dl_lut_1d(curves, n, dl_lut_coord(eg, p[2], p[3], last), 1);
    const float db = dl_lut_1d(curves, n, dl_lut_coord(eb, p[2], p[3], last), 2);
    DL_GLOBAL const float* cube = table + (int)p[7];
    const int m = (int)p[8];
    const float cube_last = (float)(m - 1);
    const dl_float4 s = dl_lut_3d(cube, m, dl_lut_coord(dr, 0.0f, p[9], cube_last),
                                  dl_lut_coord(dg, 0.0f, p[10], cube_last),
                                  dl_lut_coord(db, 0.0f, p[11], cube_last));
    return dl_make_float4(0.18f * dl_exp2(p[12] * s.x), 0.18f * dl_exp2(p[12] * s.y),
                          0.18f * dl_exp2(p[12] * s.z), c.w);
}

#endif  // DYELINE_KERNELS_TOOLS_FILM_H
