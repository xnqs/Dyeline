#pragma once

// The film stocks behind Film Negative and Film Print. Their tables are generated from
// datasheet data by scripts/film/make_film_data.py, which describes the model. Looks store a
// stock as its index in these lists, so stocks are only ever added to the end.

namespace dyeline::film {

struct NegativeStock {
    const char* id;
    const char* label;
    // ACEScg to the exposure of the red, green and blue layers, row by row; grey exposes all
    // three equally.
    float input[9];
    // The scan, in stops around grey, for layer exposures `stops_lo` stops from grey onward,
    // `stops_scale` points per stop: `points` red, green, blue triples.
    float stops_lo;
    float stops_scale;
    int points;
    const float* scan;
};

struct PrintStock {
    const char* id;
    const char* label;
    // Status A density above base for print log exposures from `log_e_lo` onward,
    // `log_e_scale` points per unit: `points` red, green, blue triples.
    float log_e_lo;
    float log_e_scale;
    int points;
    const float* curves;
    // The log exposure of each layer that prints a grey card as a neutral grey.
    float aim[3];
    // The densest each layer gets, where the display table ends.
    float d_max[3];
    // Status A densities, from 0 to d_max, to the print on screen: `cube_points` per axis,
    // red changing fastest, each an RGB triple holding log2(ACEScg / 0.18).
    int cube_points;
    const float* display;
};

int negative_stock_count();
const NegativeStock& negative_stock(int index);  // clamped to the list

int print_stock_count();
const PrintStock& print_stock(int index);  // clamped to the list

}  // namespace dyeline::film
