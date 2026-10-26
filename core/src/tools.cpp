#include "dyeline/tools.h"

#include <cmath>
#include <cstddef>
#include <memory>

#include "dyeline/cube.h"
#include "dyeline/film.h"
#include "dyeline/paths.h"

namespace dyeline {

namespace {

// Exposure: stops of light, +1 doubles it.
void encode_exposure(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_EXPOSURE;
    op.p[0] = static_cast<float>(std::exp2(v[0]));
}

bool exposure_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

// Contrast: the slope in stops around the pivot.
void encode_contrast(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_CONTRAST;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[1]);
}

bool contrast_is_identity(const ParamValues& v) {
    return v[0] == 1.0;
}

// S Curve: Amount 0 to 1 sets the mid-tone slope from 1 to 4 (see s_curve.h).
constexpr double kSCurveMaxBoost = 3.0;

void encode_s_curve(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_S_CURVE;
    op.p[0] = static_cast<float>(v[0] * kSCurveMaxBoost);
    op.p[1] = static_cast<float>(v[1]);
    op.p[2] = static_cast<float>(v[2]);
}

bool s_curve_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

// The working space's luminance weights: ACEScg (AP1), the space looks are graded in.
constexpr double kLumaWeights[3] = {0.2722287168, 0.6740817658, 0.0536895174};
constexpr double kPi = 3.14159265358979323846;

void set_matrix(const double m[3][3], dl_op& op) {
    op.tool = DL_OP_MATRIX;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            op.p[row * 3 + col] = static_cast<float>(m[row][col]);
        }
    }
}

// Hue/Saturation: a rotation about the grey axis, then a saturation scale that keeps each
// color's luminance.
void encode_hue_saturation(const ParamValues& v, dl_op& op) {
    const double angle = v[0] * kPi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle) / std::sqrt(3.0);
    const double t = (1.0 - c) / 3.0;
    // Rodrigues' rotation about (1, 1, 1) / sqrt(3).
    const double rotate[3][3] = {{c + t, t - s, t + s}, {t + s, c + t, t - s}, {t - s, t + s, c + t}};
    const double sat = v[1];
    double m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[row][col] = 0.0;
            for (int k = 0; k < 3; ++k) {
                const double saturate = (1.0 - sat) * kLumaWeights[k] + (row == k ? sat : 0.0);
                m[row][col] += saturate * rotate[k][col];
            }
        }
    }
    set_matrix(m, op);
}

bool hue_saturation_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[1] == 1.0;
}

// Black & White: grey from a weighted mix of the channels, like a filter on black-and-white
// film. The weights are scaled to sum to 1, so changing them shifts tones without changing
// overall brightness; all zero falls back to luminance.
void encode_black_white(const ParamValues& v, dl_op& op) {
    double w[3] = {v[0], v[1], v[2]};
    const double sum = w[0] + w[1] + w[2];
    for (int k = 0; k < 3; ++k) {
        w[k] = sum > 0.0 ? w[k] / sum : kLumaWeights[k];
    }
    const double m[3][3] = {{w[0], w[1], w[2]}, {w[0], w[1], w[2]}, {w[0], w[1], w[2]}};
    set_matrix(m, op);
}

bool never_identity(const ParamValues&) {
    return false;
}

// Channel Mixer: each output channel as a mix of the three inputs.
void encode_channel_mixer(const ParamValues& v, dl_op& op) {
    const double m[3][3] = {{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, {v[6], v[7], v[8]}};
    set_matrix(m, op);
}

bool channel_mixer_is_identity(const ParamValues& v) {
    for (int i = 0; i < 9; ++i) {
        if (v[static_cast<std::size_t>(i)] != (i % 4 == 0 ? 1.0 : 0.0)) {
            return false;
        }
    }
    return true;
}

// Shadows/Highlights: stops added below and above the pivot (see shadows_highlights.h). The
// fade width is fixed so the parameter ranges can't make tones swap places.
constexpr double kShadowsHighlightsWidth = 6.0;

void encode_shadows_highlights(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_SHADOWS_HIGHLIGHTS;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[1]);
    op.p[2] = static_cast<float>(v[2]);
    op.p[3] = static_cast<float>(kShadowsHighlightsWidth);
    for (int k = 0; k < 3; ++k) {
        op.p[4 + k] = static_cast<float>(kLumaWeights[k]);
    }
}

bool shadows_highlights_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[1] == 0.0;
}

// 3-Way Grade: each range's tint is a hue and an amount. The hue picks a direction around the
// grey axis, from cos(hue - 120 degrees * channel); taking away its luminance-weighted mean
// means a tint shifts color without brightening or darkening. At Amount 1 the strongest
// channel moves by about kThreeWayMaxStops. With the fade width above, a tone's output keeps
// rising with its input, as for Shadows/Highlights.
constexpr double kThreeWayMaxStops = 1.0;

// A tint's stops per channel, from a hue (degrees) and an amount (0 to 1).
void tint_stops(double hue_degrees, double amount, double out[3]) {
    const double hue = hue_degrees * kPi / 180.0;
    double mean = 0.0;
    for (int k = 0; k < 3; ++k) {
        out[k] = std::cos(hue - k * 2.0 * kPi / 3.0);
        mean += kLumaWeights[k] * out[k];
    }
    for (int k = 0; k < 3; ++k) {
        out[k] = (out[k] - mean) * amount * kThreeWayMaxStops;
    }
}

// Fills the 3-Way op from each range's stops (shadows, midtones, highlights).
void set_three_way(const double stops[3][3], double pivot, dl_op& op) {
    op.tool = DL_OP_THREE_WAY;
    for (int range = 0; range < 3; ++range) {
        for (int k = 0; k < 3; ++k) {
            op.p[range * 3 + k] = static_cast<float>(stops[range][k]);
        }
    }
    op.p[9] = static_cast<float>(pivot);
    op.p[10] = static_cast<float>(kShadowsHighlightsWidth);
    for (int k = 0; k < 3; ++k) {
        op.p[11 + k] = static_cast<float>(kLumaWeights[k]);
    }
    op.p[14] = -1.0f;  // no EQ
}

void encode_three_way(const ParamValues& v, dl_op& op) {
    double stops[3][3];
    for (int range = 0; range < 3; ++range) {
        tint_stops(v[static_cast<std::size_t>(range * 2)], v[static_cast<std::size_t>(range * 2 + 1)],
                   stops[range]);
    }
    set_three_way(stops, v[6], op);
}

// The 3-Way Grade's HSL tab (hue, saturation and lightness for six hue bands) and Sat EQ tab
// (saturation for five luminance bands) follow the wheels' seven parameters.
constexpr std::size_t kHslFirst = 7;
constexpr std::size_t kSatEqFirst = kHslFirst + DL_EQ_HUE_BANDS * 3;

bool three_way_eq_is_identity(const ParamValues& v) {
    for (std::size_t i = kHslFirst; i < v.size(); ++i) {
        const bool scale = i >= kSatEqFirst || (i - kHslFirst) % 3 == 1;
        if (v[i] != (scale ? 1.0 : 0.0)) {
            return false;
        }
    }
    return true;
}

bool prepare_three_way(PrepareContext& context, dl_op& op, std::string& problem) {
    const ParamValues& v = context.values;
    encode_three_way(v, op);
    if (three_way_eq_is_identity(v)) {
        return true;
    }
    float eq[DL_EQ_TABLE_FLOATS];
    for (int band = 0; band < DL_EQ_HUE_BANDS; ++band) {
        const std::size_t at = kHslFirst + static_cast<std::size_t>(band) * 3;
        eq[band * 3] = static_cast<float>(v[at] * kPi / 180.0);
        eq[band * 3 + 1] = static_cast<float>(v[at + 1]);
        eq[band * 3 + 2] = static_cast<float>(v[at + 2]);
    }
    for (int band = 0; band < DL_EQ_SAT_BANDS; ++band) {
        eq[DL_EQ_HUE_BANDS * 3 + band] = static_cast<float>(v[kSatEqFirst + static_cast<std::size_t>(band)]);
    }
    op.p[14] = static_cast<float>(context.table.size());
    if (context.table.size() + DL_EQ_TABLE_FLOATS > (std::size_t{1} << 24)) {
        problem = "the look's tables are too large to load together";
        return false;
    }
    context.table.insert(context.table.end(), eq, eq + DL_EQ_TABLE_FLOATS);
    return true;
}

bool three_way_is_identity(const ParamValues& v) {
    return v[1] == 0.0 && v[3] == 0.0 && v[5] == 0.0 && three_way_eq_is_identity(v);
}

ParamSpec in_tab(ParamSpec spec, const char* group) {
    spec.group = group;
    return spec;
}

std::vector<ParamSpec> three_way_params() {
    std::vector<ParamSpec> params = {{"shadows-hue", "Shadows Hue", 220.0, 0.0, 360.0},
                                     {"shadows-amount", "Shadows Amount", 0.0, 0.0, 1.0},
                                     {"midtones-hue", "Midtones Hue", 40.0, 0.0, 360.0},
                                     {"midtones-amount", "Midtones Amount", 0.0, 0.0, 1.0},
                                     {"highlights-hue", "Highlights Hue", 50.0, 0.0, 360.0},
                                     {"highlights-amount", "Highlights Amount", 0.0, 0.0, 1.0},
                                     {"pivot", "Pivot", 0.18, 0.001, 10.0}};
    static const char* const kHsl[DL_EQ_HUE_BANDS][6] = {
        {"red-hue", "Red Hue", "red-saturation", "Red Saturation", "red-lightness", "Red Lightness"},
        {"yellow-hue", "Yellow Hue", "yellow-saturation", "Yellow Saturation", "yellow-lightness",
         "Yellow Lightness"},
        {"green-hue", "Green Hue", "green-saturation", "Green Saturation", "green-lightness",
         "Green Lightness"},
        {"cyan-hue", "Cyan Hue", "cyan-saturation", "Cyan Saturation", "cyan-lightness",
         "Cyan Lightness"},
        {"blue-hue", "Blue Hue", "blue-saturation", "Blue Saturation", "blue-lightness",
         "Blue Lightness"},
        {"magenta-hue", "Magenta Hue", "magenta-saturation", "Magenta Saturation",
         "magenta-lightness", "Magenta Lightness"}};
    for (const auto& band : kHsl) {
        params.push_back(in_tab({band[0], band[1], 0.0, -60.0, 60.0}, "HSL"));
        params.push_back(in_tab({band[2], band[3], 1.0, 0.0, 2.0}, "HSL"));
        params.push_back(in_tab({band[4], band[5], 0.0, -1.0, 1.0}, "HSL"));
    }
    static const char* const kSatEq[DL_EQ_SAT_BANDS][2] = {
        {"sat-deep-shadows", "Deep Shadows"}, {"sat-shadows", "Shadows"},
        {"sat-midtones", "Midtones"}, {"sat-highlights", "Highlights"},
        {"sat-bright-highlights", "Bright Highlights"}};
    for (const auto& band : kSatEq) {
        params.push_back(in_tab({band[0], band[1], 1.0, 0.0, 2.0}, "Sat EQ"));
    }
    return params;
}

// 4-Way Wheels: the 3-Way Grade's three ranges plus an Offset wheel that reaches every tone,
// each wheel a hue, an amount and stops of exposure. The ranges' weights add up to 1, so the
// Offset is added to all three.
void encode_four_way(const ParamValues& v, dl_op& op) {
    double offset[3];
    tint_stops(v[9], v[10], offset);
    double stops[3][3];
    for (int range = 0; range < 3; ++range) {
        const std::size_t at = static_cast<std::size_t>(range) * 3;
        tint_stops(v[at], v[at + 1], stops[range]);
        for (int k = 0; k < 3; ++k) {
            stops[range][k] += v[at + 2] + offset[k] + v[11];
        }
    }
    set_three_way(stops, v[12], op);
}

bool four_way_is_identity(const ParamValues& v) {
    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
        if (v[wheel * 3 + 1] != 0.0 || v[wheel * 3 + 2] != 0.0) {
            return false;
        }
    }
    return true;
}

// Blockbuster: pulls every color toward one warm-cool axis, orange against teal by default,
// and spreads colors along it. In the plane of colors across grey (with luminance taken out,
// so luminance never changes), the part of a color off the axis shrinks by Amount and the part
// along it grows by half of Amount. It is linear, so it's a matrix.
void encode_blockbuster(const ParamValues& v, dl_op& op) {
    const double amount = v[0];
    double u[3];
    tint_stops(v[1], 1.0, u);  // a direction in that plane
    const double norm = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
    // c' = Y + P (c - Y), with P scaling the axis by (1 + amount / 2) and the rest by
    // (1 - amount); c - Y = (I - 1 w^T) c.
    double p[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const double along = u[r] * u[c] / norm;
            p[r][c] = (1.0 + 0.5 * amount) * along + (1.0 - amount) * ((r == c ? 1.0 : 0.0) - along);
        }
    }
    double m[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            double sum = kLumaWeights[c];  // Y
            for (int k = 0; k < 3; ++k) {
                sum += p[r][k] * ((k == c ? 1.0 : 0.0) - kLumaWeights[c]);
            }
            m[r][c] = sum;
        }
    }
    set_matrix(m, op);
}

bool amount_is_zero(const ParamValues& v) {
    return v[0] == 0.0;
}

// Color Filter: a colored filter on the lens. It passes its own hue and takes up to twice
// Density stops from the opposite one; with Preserve Brightness on, the result is scaled so
// greys keep their brightness, as a camera's exposure would make up for the filter.
void encode_color_filter(const ParamValues& v, dl_op& op) {
    const double hue = v[0] * kPi / 180.0;
    double gain[3];
    double luma = 0.0;
    for (int k = 0; k < 3; ++k) {
        gain[k] = std::exp2(-v[1] * (1.0 - std::cos(hue - k * 2.0 * kPi / 3.0)));
        luma += kLumaWeights[k] * gain[k];
    }
    const double scale = v[2] >= 0.5 ? 1.0 / luma : 1.0;
    const double m[3][3] = {{gain[0] * scale, 0.0, 0.0},
                            {0.0, gain[1] * scale, 0.0},
                            {0.0, 0.0, gain[2] * scale}};
    set_matrix(m, op);
}

bool color_filter_is_identity(const ParamValues& v) {
    return v[1] == 0.0;
}

// Duotone: a tint for the shadows and one for the highlights. Each is 1 plus Saturation
// times the hue's direction, its negative parts clipped, scaled so its luminance is 1.
void duotone_tint(double hue_degrees, double saturation, float out[3]) {
    const double hue = hue_degrees * kPi / 180.0;
    double t[3];
    double mean = 0.0;
    for (int k = 0; k < 3; ++k) {
        t[k] = std::cos(hue - k * 2.0 * kPi / 3.0);
        mean += kLumaWeights[k] * t[k];
    }
    double luma = 0.0;
    for (int k = 0; k < 3; ++k) {
        t[k] = std::max(0.0, 1.0 + saturation * (t[k] - mean));
        luma += kLumaWeights[k] * t[k];
    }
    for (int k = 0; k < 3; ++k) {
        out[k] = static_cast<float>(t[k] / luma);
    }
}

constexpr double kDuotoneHalfWidth = 3.0;  // stops

void encode_duotone(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_DUOTONE;
    duotone_tint(v[0], v[2], op.p);
    duotone_tint(v[1], v[2], op.p + 3);
    op.p[6] = static_cast<float>(v[3]);
    op.p[7] = static_cast<float>(kDuotoneHalfWidth);
    for (int k = 0; k < 3; ++k) {
        op.p[8 + k] = static_cast<float>(kLumaWeights[k]);
    }
}

// Highlight Shoulder: the roll-off starts at Start and approaches Headroom stops above it.
void encode_highlight_shoulder(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_HIGHLIGHT_SHOULDER;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[0] * (std::exp2(v[1]) - 1.0));
}

// LUT: a .cube file, with LUT Gamma applied around the lookup (see lut.h). The table goes in
// the chain's table data; offsets are stored as floats, which hold integers exactly below 2^24.
constexpr std::size_t kMaxTableFloats = std::size_t{1} << 24;

void encode_unused(const ParamValues&, dl_op&) {}

bool prepare_lut(PrepareContext& context, dl_op& op, std::string& problem) {
    const auto file = context.params.find("file");
    if (file == context.params.end() || !file->is_string() || file->get<std::string>().empty()) {
        return false;  // no LUT chosen yet
    }
    const std::string path = resolve_path(file->get<std::string>(), context.base_dir);
    std::string error;
    const std::shared_ptr<const CubeLut> lut = load_cube(path, error);
    if (lut == nullptr) {
        problem = error;
        return false;
    }
    if (context.table.size() + lut->data.size() > kMaxTableFloats) {
        problem = path + ": the look's LUTs are too large to load together";
        return false;
    }
    op.tool = DL_OP_LUT;
    op.p[0] = static_cast<float>(context.table.size());
    op.p[1] = static_cast<float>(lut->size);
    op.p[2] = lut->is_3d ? 1.0f : 0.0f;
    for (int k = 0; k < 3; ++k) {
        op.p[3 + k] = lut->domain_min[k];
        op.p[6 + k] = static_cast<float>((lut->size - 1) /
                                         (static_cast<double>(lut->domain_max[k]) - lut->domain_min[k]));
    }
    const double gamma = context.values[0];
    op.p[9] = static_cast<float>(1.0 / gamma);
    op.p[10] = static_cast<float>(gamma);
    context.table.insert(context.table.end(), lut->data.begin(), lut->data.end());
    return true;
}

// Film Negative and Film Print: a stock's tables go in the chain's table data (see film.h).
constexpr double kLog10Of2 = 0.30102999566398120;
// One printer point is 0.025 log exposure, as in a film lab.
constexpr double kPrinterPoint = 0.025;

bool append_table(PrepareContext& context, const float* data, std::size_t floats,
                  std::string& problem) {
    if (context.table.size() + floats > kMaxTableFloats) {
        problem = "the look's tables are too large to load together";
        return false;
    }
    context.table.insert(context.table.end(), data, data + floats);
    return true;
}

bool prepare_film_negative(PrepareContext& context, dl_op& op, std::string& problem) {
    const film::NegativeStock& stock = film::negative_stock(static_cast<int>(std::lround(context.values[0])));
    op.tool = DL_OP_FILM_NEGATIVE;
    op.p[0] = static_cast<float>(context.table.size());
    op.p[1] = static_cast<float>(stock.points);
    for (int i = 0; i < 9; ++i) {
        op.p[2 + i] = stock.input[i];
    }
    op.p[11] = static_cast<float>(stock.stops_lo - context.values[1]);
    op.p[12] = stock.stops_scale;
    return append_table(context, stock.scan, static_cast<std::size_t>(stock.points) * 3, problem);
}

bool prepare_film_print(PrepareContext& context, dl_op& op, std::string& problem) {
    const ParamValues& v = context.values;
    const film::PrintStock& stock = film::print_stock(static_cast<int>(std::lround(v[0])));
    op.tool = DL_OP_FILM_PRINT;
    op.p[0] = static_cast<float>(context.table.size());
    op.p[1] = static_cast<float>(stock.points);
    op.p[2] = stock.log_e_lo;
    op.p[3] = stock.log_e_scale;
    // More Exposure brightens the print, so it takes printer light away. A printer point
    // adds light to one layer, so raising Red makes the print more cyan.
    for (int k = 0; k < 3; ++k) {
        op.p[4 + k] = static_cast<float>(stock.aim[k] - v[1] * kLog10Of2 +
                                         v[2 + static_cast<std::size_t>(k)] * kPrinterPoint);
    }
    const std::size_t cube_floats =
        static_cast<std::size_t>(stock.cube_points) * stock.cube_points * stock.cube_points * 3;
    op.p[7] = static_cast<float>(context.table.size() + static_cast<std::size_t>(stock.points) * 3);
    op.p[8] = static_cast<float>(stock.cube_points);
    for (int k = 0; k < 3; ++k) {
        op.p[9 + k] = static_cast<float>((stock.cube_points - 1) / stock.d_max[k]);
    }
    op.p[12] = static_cast<float>(v[5]);
    return append_table(context, stock.curves, static_cast<std::size_t>(stock.points) * 3, problem) &&
           append_table(context, stock.display, cube_floats, problem);
}

std::vector<const char*> negative_stock_labels() {
    std::vector<const char*> labels;
    for (int i = 0; i < film::negative_stock_count(); ++i) {
        labels.push_back(film::negative_stock(i).label);
    }
    return labels;
}

std::vector<const char*> print_stock_labels() {
    std::vector<const char*> labels;
    for (int i = 0; i < film::print_stock_count(); ++i) {
        labels.push_back(film::print_stock(i).label);
    }
    return labels;
}

// Bleach Bypass: see bleach_bypass.h.
void encode_bleach_bypass(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_BLEACH_BYPASS;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[1]);
    for (int k = 0; k < 3; ++k) {
        op.p[2 + k] = static_cast<float>(kLumaWeights[k]);
    }
}


// 2-Strip: the two-color process, which recorded only a red and a green-blue record. Red
// stays; green and blue both become their average, so the image holds only red-orange and
// cyan-green. Amount mixes from the original.
void encode_two_strip(const ParamValues& v, dl_op& op) {
    const double a = v[0];
    const double m[3][3] = {{1.0, 0.0, 0.0},
                            {0.0, 1.0 - 0.5 * a, 0.5 * a},
                            {0.0, 0.5 * a, 1.0 - 0.5 * a}};
    set_matrix(m, op);
}

// The selective tools (see selective.h).
void set_gradient(const double stops[3], const ParamValues& v, std::size_t geometry, dl_op& op) {
    op.tool = DL_OP_GRADIENT;
    for (int k = 0; k < 3; ++k) {
        op.p[k] = static_cast<float>(stops[k]);
    }
    const double angle = v[geometry + 2] * kPi / 180.0;
    op.p[3] = static_cast<float>(v[geometry]);
    op.p[4] = static_cast<float>(v[geometry + 1]);
    op.p[5] = static_cast<float>(std::sin(angle));
    op.p[6] = static_cast<float>(std::cos(angle));
    op.p[7] = static_cast<float>(v[geometry + 3]);
}

// Gradient Tint: hue, amount, then the gradient's center x, y, angle and softness.
void encode_gradient_tint(const ParamValues& v, dl_op& op) {
    double stops[3];
    tint_stops(v[0], v[1], stops);
    set_gradient(stops, v, 2, op);
}

bool tint_is_identity(const ParamValues& v) {
    return v[1] == 0.0;
}

// Grad Exposure: stops, then the gradient.
void encode_grad_exposure(const ParamValues& v, dl_op& op) {
    const double stops[3] = {v[0], v[0], v[0]};
    set_gradient(stops, v, 1, op);
}

// Spot Exposure: stops, center x, y, size and softness.
void encode_spot_exposure(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_SPOT;
    for (int k = 0; k < 3; ++k) {
        op.p[k] = static_cast<float>(v[0]);
    }
    for (int i = 0; i < 4; ++i) {
        op.p[3 + i] = static_cast<float>(v[1 + static_cast<std::size_t>(i)]);
    }
}

// Fill Light: amount in stops, pivot, tint hue and tint amount; Spot Fill adds a spot's center
// x, y, size and softness. The tint leans the fill's color without changing its brightness.
void encode_fill(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_FILL;
    double tint[3];
    tint_stops(v[2], v[3], tint);
    for (int k = 0; k < 3; ++k) {
        op.p[k] = static_cast<float>(v[0] + tint[k]);
        op.p[4 + k] = static_cast<float>(kLumaWeights[k]);
    }
    op.p[3] = static_cast<float>(v[1]);
    op.p[7] = v.size() > 4 ? 1.0f : 0.0f;
    for (std::size_t i = 4; i < v.size(); ++i) {
        op.p[4 + i] = static_cast<float>(v[i]);
    }
}

bool fill_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[3] == 0.0;
}

ParamSpec center_x() {
    return {"center-x", "Center X", 0.0, -1.0, 1.0};
}

ParamSpec center_y(double default_value = 0.0) {
    return {"center-y", "Center Y", default_value, -1.0, 1.0};
}

const std::vector<HandleSpec> kGradientHandles = {{HandleKind::Point, "center-x", "center-y"},
                                                  {HandleKind::Angle, "angle"}};
const std::vector<HandleSpec> kSpotHandles = {{HandleKind::Point, "center-x", "center-y"},
                                              {HandleKind::Radius, "size"}};

// Lens Vignette: Amount in stops, reached Softness past Size (distances are 1 at the corners).
void encode_lens_vignette(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_LENS_VIGNETTE;
    for (int i = 0; i < 6; ++i) {
        op.p[i] = static_cast<float>(v[static_cast<std::size_t>(i)]);
    }
}

bool lens_vignette_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

// Lens Distortion: Amount and Edges scale the Brown-Conrady k1 and k2 terms (lens_warp.h).
// At these scales the distortion folds back on itself only at the strongest pincushion settings.
constexpr double kDistortionK1 = 0.25;
constexpr double kDistortionK2 = 0.1;

void encode_lens_distortion(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_LENS_DISTORTION;
    op.p[0] = static_cast<float>(v[0] * kDistortionK1);
    op.p[1] = static_cast<float>(v[1] * kDistortionK2);
    op.p[2] = static_cast<float>(1.0 / v[2]);
    op.p[3] = static_cast<float>(v[3]);
    op.p[4] = static_cast<float>(v[4]);
}

bool lens_distortion_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[1] == 0.0 && v[2] == 1.0;
}

// Chromatic Aberration: Red, Blue and Fringe are percentages of the frame's width.
void encode_chromatic_aberration(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_CHROMATIC_ABERRATION;
    for (int i = 0; i < 3; ++i) {
        op.p[i] = static_cast<float>(v[static_cast<std::size_t>(i)] / 100.0);
    }
    op.p[3] = static_cast<float>(v[3]);
    op.p[4] = static_cast<float>(v[4]);
}

bool chromatic_aberration_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[1] == 0.0 && v[2] == 0.0;
}

// Grain: at Amount 1 the midtones move by up to half a stop either way.
constexpr double kGrainMaxStops = 0.5;

void encode_grain(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_GRAIN;
    op.p[0] = static_cast<float>(v[0] * kGrainMaxStops);
    op.p[1] = static_cast<float>(1.0 / v[1]);
    op.p[2] = static_cast<float>(v[2]);
    op.p[3] = v[3] >= 0.5 ? 1.0f : 0.0f;
    op.p[4] = static_cast<float>(std::floor(v[4]));
    for (int k = 0; k < 3; ++k) {
        op.p[5 + k] = static_cast<float>(kLumaWeights[k]);
    }
}

bool grain_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

// Diffusion Filter and Halation blur over a share of the frame: Size is the blur's Gaussian
// sigma as a percentage of the frame's width, so a look keeps its glow at any resolution.
void set_blur_size(double percent_of_width, dl_op& op) {
    op.p[DL_OP_BLUR_SIZE] = static_cast<float>(percent_of_width / 100.0);
}

// Diffusion Filter: amount, size, highlights and style (Diffusion, Pro-Mist, Black Pro-Mist).
void encode_diffusion(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_DIFFUSION;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[2]);
    op.p[5] = v.size() > 3 ? static_cast<float>(std::floor(v[3])) : 0.0f;
    for (int k = 0; k < 3; ++k) {
        op.p[2 + k] = static_cast<float>(kLumaWeights[k]);
    }
    set_blur_size(v[1], op);
}

bool diffusion_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
}

// Edge Softness: Blur (percent of the frame's width), center x, y, size and softness.
void encode_edge_softness(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_EDGE_SOFTNESS;
    op.p[0] = static_cast<float>(v[3]);
    op.p[1] = static_cast<float>(v[4]);
    op.p[2] = static_cast<float>(v[1]);
    op.p[3] = static_cast<float>(v[2]);
    set_blur_size(v[0], op);
}

// Swing-Tilt: Blur, center x, y, angle (the band's normal, clockwise from up), focus (half the
// sharp band's width) and softness.
void encode_swing_tilt(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_SWING_TILT;
    const double angle = v[3] * kPi / 180.0;
    op.p[0] = static_cast<float>(v[4]);
    op.p[1] = static_cast<float>(v[5]);
    op.p[2] = static_cast<float>(v[1]);
    op.p[3] = static_cast<float>(v[2]);
    op.p[4] = static_cast<float>(std::sin(angle));
    op.p[5] = static_cast<float>(std::cos(angle));
    set_blur_size(v[0], op);
}

bool blur_is_zero(const ParamValues& v) {
    return v[0] == 0.0;
}

// Local Contrast: see local_contrast.h. Size is the blur's, in percent of the frame's width.
void encode_local_contrast(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_LOCAL_CONTRAST;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(0.18 / 64.0);
    for (int k = 0; k < 3; ++k) {
        op.p[2 + k] = static_cast<float>(kLumaWeights[k]);
    }
    set_blur_size(v[1], op);
}

// Telecine Net: a fine net in the light path scatters a share of every pixel's light evenly
// over a wide area: Diffusion with every tone scattering alike. Shadows near bright areas
// lift and contrast softens, while the frame's total light stays the same.
void encode_telecine_net(const ParamValues& v, dl_op& op) {
    encode_diffusion({v[0], v[1], 0.0}, op);
}

void encode_halation(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_HALATION;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[1]);
    for (int k = 0; k < 3; ++k) {
        op.p[2 + k] = static_cast<float>(v[3 + static_cast<std::size_t>(k)]);
    }
    set_blur_size(v[2], op);
}

bool halation_is_identity(const ParamValues& v) {
    return v[0] == 0.0 || (v[3] == 0.0 && v[4] == 0.0 && v[5] == 0.0);
}

// Streaks: Halation's glow, blurred along rows only, the long horizontal flares of an
// anamorphic lens. Length is the blur's size, in percent of the frame's width.
void encode_streaks(const ParamValues& v, dl_op& op) {
    encode_halation(v, op);
    op.tool = DL_OP_STREAKS;
}

std::vector<ToolSpec> make_tools() {
    std::vector<ToolSpec> tools;
    tools.push_back({"exposure", "Exposure", ToolCategory::Matte, 1,
                     {{"stops", "Exposure", 0.0, -16.0, 16.0}},
                     nullptr, encode_exposure, exposure_is_identity, nullptr});
    tools.push_back({"contrast", "Contrast", ToolCategory::Matte, 1,
                     {{"contrast", "Contrast", 1.0, 0.0, 4.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_contrast, contrast_is_identity, nullptr});
    tools.push_back({"s-curve", "S Curve", ToolCategory::Matte, 1,
                     {{"amount", "Amount", 0.5, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0},
                      {"range", "Range", 5.0, 1.0, 12.0}},
                     nullptr, encode_s_curve, s_curve_is_identity, nullptr});
    tools.push_back({"hue-saturation", "Hue/Saturation", ToolCategory::Matte, 1,
                     {{"hue", "Hue", 0.0, -180.0, 180.0},
                      {"saturation", "Saturation", 1.0, 0.0, 4.0}},
                     nullptr, encode_hue_saturation, hue_saturation_is_identity, nullptr});
    tools.push_back({"black-white", "Black & White", ToolCategory::Matte, 1,
                     {{"red", "Red", 0.27, 0.0, 2.0},
                      {"green", "Green", 0.67, 0.0, 2.0},
                      {"blue", "Blue", 0.06, 0.0, 2.0}},
                     nullptr, encode_black_white, never_identity, nullptr});
    tools.push_back({"channel-mixer", "Channel Mixer", ToolCategory::Matte, 1,
                     {{"red-from-red", "Red from Red", 1.0, -2.0, 2.0},
                      {"red-from-green", "Red from Green", 0.0, -2.0, 2.0},
                      {"red-from-blue", "Red from Blue", 0.0, -2.0, 2.0},
                      {"green-from-red", "Green from Red", 0.0, -2.0, 2.0},
                      {"green-from-green", "Green from Green", 1.0, -2.0, 2.0},
                      {"green-from-blue", "Green from Blue", 0.0, -2.0, 2.0},
                      {"blue-from-red", "Blue from Red", 0.0, -2.0, 2.0},
                      {"blue-from-green", "Blue from Green", 0.0, -2.0, 2.0},
                      {"blue-from-blue", "Blue from Blue", 1.0, -2.0, 2.0}},
                     nullptr, encode_channel_mixer, channel_mixer_is_identity, nullptr});
    tools.push_back({"shadows-highlights", "Shadows/Highlights", ToolCategory::Matte, 1,
                     {{"shadows", "Shadows", 0.0, -2.0, 2.0},
                      {"highlights", "Highlights", 0.0, -2.0, 2.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_shadows_highlights, shadows_highlights_is_identity, nullptr});
    tools.push_back({"highlight-shoulder", "Highlight Shoulder", ToolCategory::Camera, 1,
                     {{"start", "Start", 1.0, 0.05, 16.0},
                      {"headroom", "Headroom", 1.5, 0.1, 8.0}},
                     nullptr, encode_highlight_shoulder, never_identity, nullptr});
    // Version 2 adds the HSL and Sat EQ tabs; their defaults change nothing, so version 1
    // entries need no migration.
    tools.push_back({"three-way", "3-Way Grade", ToolCategory::Matte, 2, three_way_params(),
                     nullptr, encode_three_way, three_way_is_identity, prepare_three_way});
    tools.back().main_tab = "Wheels";
    tools.push_back({"four-way", "4-Way Wheels", ToolCategory::Matte, 1,
                     {{"shadows-hue", "Shadows Hue", 220.0, 0.0, 360.0},
                      {"shadows-amount", "Shadows Amount", 0.0, 0.0, 1.0},
                      {"shadows-exposure", "Shadows Exposure", 0.0, -2.0, 2.0},
                      {"midtones-hue", "Midtones Hue", 40.0, 0.0, 360.0},
                      {"midtones-amount", "Midtones Amount", 0.0, 0.0, 1.0},
                      {"midtones-exposure", "Midtones Exposure", 0.0, -2.0, 2.0},
                      {"highlights-hue", "Highlights Hue", 50.0, 0.0, 360.0},
                      {"highlights-amount", "Highlights Amount", 0.0, 0.0, 1.0},
                      {"highlights-exposure", "Highlights Exposure", 0.0, -2.0, 2.0},
                      {"offset-hue", "Offset Hue", 40.0, 0.0, 360.0},
                      {"offset-amount", "Offset Amount", 0.0, 0.0, 1.0},
                      {"offset-exposure", "Offset Exposure", 0.0, -4.0, 4.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_four_way, four_way_is_identity, nullptr});
    tools.push_back({"blockbuster", "Blockbuster", ToolCategory::Matte, 1,
                     {{"amount", "Amount", 0.6, 0.0, 1.0},
                      {"warm-hue", "Warm Hue", 30.0, 0.0, 360.0}},
                     nullptr, encode_blockbuster, amount_is_zero, nullptr});
    tools.push_back({"duotone", "Duotone", ToolCategory::Matte, 1,
                     {{"shadows-hue", "Shadows Hue", 220.0, 0.0, 360.0},
                      {"highlights-hue", "Highlights Hue", 40.0, 0.0, 360.0},
                      {"saturation", "Saturation", 0.4, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_duotone, never_identity, nullptr});
    tools.push_back({"lut", "LUT", ToolCategory::Matte, 1,
                     {{"gamma", "LUT Gamma", 1.0, 0.2, 5.0}},
                     nullptr, encode_unused, never_identity, prepare_lut, "file"});
    tools.push_back({"film-negative", "Film Negative", ToolCategory::Camera, 1,
                     {{"stock", "Stock", 0.0, 0.0, film::negative_stock_count() - 1.0,
                       ParamKind::Choice, negative_stock_labels()},
                      {"exposure", "Exposure", 0.0, -6.0, 6.0}},
                     nullptr, encode_unused, never_identity, prepare_film_negative});
    tools.push_back({"film-print", "Film Print", ToolCategory::Post, 1,
                     {{"stock", "Stock", 0.0, 0.0, film::print_stock_count() - 1.0,
                       ParamKind::Choice, print_stock_labels()},
                      {"exposure", "Exposure", 0.0, -4.0, 4.0},
                      {"red", "Red Points", 0.0, -24.0, 24.0},
                      {"green", "Green Points", 0.0, -24.0, 24.0},
                      {"blue", "Blue Points", 0.0, -24.0, 24.0},
                      {"contrast", "Contrast", 1.0, 0.5, 2.0}},
                     nullptr, encode_unused, never_identity, prepare_film_print});
    tools.push_back({"bleach-bypass", "Bleach Bypass", ToolCategory::Camera, 1,
                     {{"amount", "Amount", 0.7, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_bleach_bypass, amount_is_zero, nullptr});
    tools.push_back({"two-strip", "2-Strip", ToolCategory::Camera, 1,
                     {{"amount", "Amount", 1.0, 0.0, 1.0}},
                     nullptr, encode_two_strip, amount_is_zero, nullptr});
    tools.push_back({"gradient-tint", "Gradient Tint", ToolCategory::Matte, 1,
                     {{"hue", "Hue", 210.0, 0.0, 360.0},
                      {"amount", "Amount", 0.5, 0.0, 1.0},
                      center_x(), center_y(),
                      {"angle", "Angle", 0.0, -180.0, 180.0},
                      {"softness", "Softness", 0.8, 0.01, 2.0}},
                     nullptr, encode_gradient_tint, tint_is_identity, nullptr, nullptr, false,
                     kGradientHandles});
    tools.push_back({"grad-exposure", "Grad Exposure", ToolCategory::Matte, 1,
                     {{"stops", "Exposure", -1.0, -4.0, 4.0},
                      center_x(), center_y(),
                      {"angle", "Angle", 0.0, -180.0, 180.0},
                      {"softness", "Softness", 0.8, 0.01, 2.0}},
                     nullptr, encode_grad_exposure, exposure_is_identity, nullptr, nullptr, false,
                     kGradientHandles});
    tools.push_back({"spot-exposure", "Spot Exposure", ToolCategory::Subject, 1,
                     {{"stops", "Exposure", 1.0, -4.0, 4.0},
                      center_x(), center_y(),
                      {"size", "Size", 0.3, 0.0, 2.0},
                      {"softness", "Softness", 0.3, 0.01, 2.0}},
                     nullptr, encode_spot_exposure, exposure_is_identity, nullptr, nullptr, false,
                     kSpotHandles});
    tools.push_back({"fill-light", "Fill Light", ToolCategory::Subject, 1,
                     {{"amount", "Amount", 0.5, 0.0, 2.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0},
                      {"tint-hue", "Tint Hue", 40.0, 0.0, 360.0},
                      {"tint-amount", "Tint Amount", 0.0, 0.0, 0.5}},
                     nullptr, encode_fill, fill_is_identity, nullptr});
    tools.push_back({"spot-fill", "Spot Fill", ToolCategory::Subject, 1,
                     {{"amount", "Amount", 0.75, 0.0, 2.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0},
                      {"tint-hue", "Tint Hue", 40.0, 0.0, 360.0},
                      {"tint-amount", "Tint Amount", 0.0, 0.0, 0.5},
                      center_x(), center_y(),
                      {"size", "Size", 0.3, 0.0, 2.0},
                      {"softness", "Softness", 0.3, 0.01, 2.0}},
                     nullptr, encode_fill, fill_is_identity, nullptr, nullptr, false, kSpotHandles});
    tools.push_back({"color-filter", "Color Filter", ToolCategory::Lens, 1,
                     {{"hue", "Hue", 30.0, 0.0, 360.0},
                      {"density", "Density", 0.3, 0.0, 2.0},
                      {"preserve", "Preserve Brightness", 1.0, 0.0, 1.0, ParamKind::Toggle}},
                     nullptr, encode_color_filter, color_filter_is_identity, nullptr});
    tools.push_back({"lens-vignette", "Lens Vignette", ToolCategory::Lens, 1,
                     {{"amount", "Amount", -1.0, -4.0, 4.0},
                      {"size", "Size", 0.5, 0.0, 2.0},
                      {"softness", "Softness", 0.6, 0.01, 2.0},
                      {"roundness", "Roundness", 1.0, 0.0, 1.0},
                      {"center-x", "Center X", 0.0, -1.0, 1.0},
                      {"center-y", "Center Y", 0.0, -1.0, 1.0}},
                     nullptr, encode_lens_vignette, lens_vignette_is_identity, nullptr, nullptr, false,
                     {{HandleKind::Point, "center-x", "center-y"}}});
    tools.push_back({"lens-distortion", "Lens Distortion", ToolCategory::Lens, 1,
                     {{"amount", "Amount", 0.3, -1.0, 1.0},
                      {"edges", "Edges", 0.0, -1.0, 1.0},
                      {"zoom", "Zoom", 1.0, 0.5, 2.0},
                      {"center-x", "Center X", 0.0, -1.0, 1.0},
                      {"center-y", "Center Y", 0.0, -1.0, 1.0}},
                     nullptr, encode_lens_distortion, lens_distortion_is_identity, nullptr, nullptr,
                     false, {{HandleKind::Point, "center-x", "center-y"}}});
    tools.push_back({"chromatic-aberration", "Chromatic Aberration", ToolCategory::Lens, 1,
                     {{"red", "Red", 0.15, -1.0, 1.0},
                      {"blue", "Blue", -0.15, -1.0, 1.0},
                      {"fringe", "Fringe", 0.0, 0.0, 0.5},
                      {"center-x", "Center X", 0.0, -1.0, 1.0},
                      {"center-y", "Center Y", 0.0, -1.0, 1.0}},
                     nullptr, encode_chromatic_aberration, chromatic_aberration_is_identity,
                     nullptr, nullptr, false, {{HandleKind::Point, "center-x", "center-y"}}});
    tools.push_back({"edge-softness", "Edge Softness", ToolCategory::Lens, 1,
                     {{"blur", "Blur", 0.8, 0.0, 5.0},
                      center_x(), center_y(),
                      {"size", "Size", 0.5, 0.0, 2.0},
                      {"softness", "Softness", 0.6, 0.01, 2.0}},
                     nullptr, encode_edge_softness, blur_is_zero, nullptr, nullptr, false,
                     kSpotHandles});
    tools.push_back({"swing-tilt", "Swing-Tilt", ToolCategory::Lens, 1,
                     {{"blur", "Blur", 1.5, 0.0, 5.0},
                      center_x(), center_y(),
                      {"angle", "Angle", 0.0, -180.0, 180.0},
                      {"focus", "Focus", 0.15, 0.0, 1.0},
                      {"softness", "Softness", 0.4, 0.01, 2.0}},
                     nullptr, encode_swing_tilt, blur_is_zero, nullptr, nullptr, false,
                     {{HandleKind::Point, "center-x", "center-y"}, {HandleKind::Angle, "angle"}}});
    tools.push_back({"grain", "Grain", ToolCategory::Camera, 1,
                     {{"amount", "Amount", 0.3, 0.0, 1.0},
                      {"size", "Size", 1.5, 0.5, 8.0},
                      {"color", "Color", 0.3, 0.0, 1.0},
                      {"animate", "Animate", 1.0, 0.0, 1.0, ParamKind::Toggle},
                      {"seed", "Seed", 0.0, 0.0, 9999.0, ParamKind::Integer}},
                     nullptr, encode_grain, grain_is_identity, nullptr, nullptr, false});
    tools.push_back({"diffusion", "Diffusion Filter", ToolCategory::Lens, 1,
                     {{"amount", "Amount", 0.5, 0.0, 1.0},
                      {"size", "Size", 1.0, 0.05, 10.0},
                      {"highlights", "Highlights", 0.5, 0.0, 1.0},
                      {"style", "Style", 0.0, 0.0, 2.0, ParamKind::Choice,
                       {"Diffusion", "Pro-Mist", "Black Pro-Mist"}}},
                     nullptr, encode_diffusion, diffusion_is_identity, nullptr, nullptr, false});
    tools.push_back({"streaks", "Streaks", ToolCategory::Lens, 1,
                     {{"amount", "Amount", 0.6, 0.0, 2.0},
                      {"threshold", "Threshold", 1.0, 0.05, 16.0},
                      {"length", "Length", 6.0, 0.5, 30.0},
                      {"tint-red", "Tint Red", 0.5, 0.0, 1.0},
                      {"tint-green", "Tint Green", 0.75, 0.0, 1.0},
                      {"tint-blue", "Tint Blue", 1.0, 0.0, 1.0}},
                     nullptr, encode_streaks, halation_is_identity, nullptr, nullptr, false});
    tools.push_back({"local-contrast", "Local Contrast", ToolCategory::Post, 1,
                     {{"amount", "Amount", 0.3, -1.0, 1.0},
                      {"size", "Size", 1.5, 0.1, 10.0}},
                     nullptr, encode_local_contrast, amount_is_zero, nullptr, nullptr, false});
    tools.push_back({"telecine-net", "Telecine Net", ToolCategory::Post, 1,
                     {{"amount", "Amount", 0.3, 0.0, 1.0},
                      {"size", "Size", 4.0, 0.5, 10.0}},
                     nullptr, encode_telecine_net, diffusion_is_identity, nullptr, nullptr, false});
    tools.push_back({"halation", "Halation", ToolCategory::Camera, 1,
                     {{"amount", "Amount", 0.5, 0.0, 2.0},
                      {"threshold", "Threshold", 1.0, 0.05, 16.0},
                      {"size", "Size", 0.6, 0.05, 5.0},
                      {"tint-red", "Tint Red", 1.0, 0.0, 1.0},
                      {"tint-green", "Tint Green", 0.25, 0.0, 1.0},
                      {"tint-blue", "Tint Blue", 0.05, 0.0, 1.0}},
                     nullptr, encode_halation, halation_is_identity, nullptr, nullptr, false});
    return tools;
}

}  // namespace

const std::vector<ToolSpec>& all_tools() {
    static const std::vector<ToolSpec> tools = make_tools();
    return tools;
}

const ToolSpec* find_tool(std::string_view id) {
    for (const ToolSpec& tool : all_tools()) {
        if (id == tool.id) {
            return &tool;
        }
    }
    return nullptr;
}

}  // namespace dyeline
