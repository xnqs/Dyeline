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

void encode_three_way(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_THREE_WAY;
    for (int range = 0; range < 3; ++range) {
        const double hue = v[static_cast<std::size_t>(range * 2)] * kPi / 180.0;
        const double amount = v[static_cast<std::size_t>(range * 2 + 1)];
        double d[3];
        double mean = 0.0;
        for (int k = 0; k < 3; ++k) {
            d[k] = std::cos(hue - k * 2.0 * kPi / 3.0);
            mean += kLumaWeights[k] * d[k];
        }
        for (int k = 0; k < 3; ++k) {
            op.p[range * 3 + k] = static_cast<float>((d[k] - mean) * amount * kThreeWayMaxStops);
        }
    }
    op.p[9] = static_cast<float>(v[6]);
    op.p[10] = static_cast<float>(kShadowsHighlightsWidth);
    for (int k = 0; k < 3; ++k) {
        op.p[11 + k] = static_cast<float>(kLumaWeights[k]);
    }
}

bool three_way_is_identity(const ParamValues& v) {
    return v[1] == 0.0 && v[3] == 0.0 && v[5] == 0.0;
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

bool amount_is_zero(const ParamValues& v) {
    return v[0] == 0.0;
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

void encode_diffusion(const ParamValues& v, dl_op& op) {
    op.tool = DL_OP_DIFFUSION;
    op.p[0] = static_cast<float>(v[0]);
    op.p[1] = static_cast<float>(v[2]);
    for (int k = 0; k < 3; ++k) {
        op.p[2 + k] = static_cast<float>(kLumaWeights[k]);
    }
    set_blur_size(v[1], op);
}

bool diffusion_is_identity(const ParamValues& v) {
    return v[0] == 0.0;
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
    tools.push_back({"three-way", "3-Way Grade", ToolCategory::Matte, 1,
                     {{"shadows-hue", "Shadows Hue", 220.0, 0.0, 360.0},
                      {"shadows-amount", "Shadows Amount", 0.0, 0.0, 1.0},
                      {"midtones-hue", "Midtones Hue", 40.0, 0.0, 360.0},
                      {"midtones-amount", "Midtones Amount", 0.0, 0.0, 1.0},
                      {"highlights-hue", "Highlights Hue", 50.0, 0.0, 360.0},
                      {"highlights-amount", "Highlights Amount", 0.0, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_three_way, three_way_is_identity, nullptr});
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
    tools.push_back({"lens-vignette", "Lens Vignette", ToolCategory::Lens, 1,
                     {{"amount", "Amount", -1.0, -4.0, 4.0},
                      {"size", "Size", 0.5, 0.0, 2.0},
                      {"softness", "Softness", 0.6, 0.01, 2.0},
                      {"roundness", "Roundness", 1.0, 0.0, 1.0},
                      {"center-x", "Center X", 0.0, -1.0, 1.0},
                      {"center-y", "Center Y", 0.0, -1.0, 1.0}},
                     nullptr, encode_lens_vignette, lens_vignette_is_identity, nullptr, nullptr, false});
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
                      {"highlights", "Highlights", 0.5, 0.0, 1.0}},
                     nullptr, encode_diffusion, diffusion_is_identity, nullptr, nullptr, false});
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
