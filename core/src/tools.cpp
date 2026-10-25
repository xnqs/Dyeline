#include "dyeline/tools.h"

#include <cmath>
#include <cstddef>

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

std::vector<ToolSpec> make_tools() {
    std::vector<ToolSpec> tools;
    tools.push_back({"exposure", "Exposure", ToolCategory::Matte, 1,
                     {{"stops", "Exposure", 0.0, -16.0, 16.0}},
                     nullptr, encode_exposure, exposure_is_identity});
    tools.push_back({"contrast", "Contrast", ToolCategory::Matte, 1,
                     {{"contrast", "Contrast", 1.0, 0.0, 4.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_contrast, contrast_is_identity});
    tools.push_back({"s-curve", "S Curve", ToolCategory::Matte, 1,
                     {{"amount", "Amount", 0.5, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0},
                      {"range", "Range", 5.0, 1.0, 12.0}},
                     nullptr, encode_s_curve, s_curve_is_identity});
    tools.push_back({"hue-saturation", "Hue/Saturation", ToolCategory::Matte, 1,
                     {{"hue", "Hue", 0.0, -180.0, 180.0},
                      {"saturation", "Saturation", 1.0, 0.0, 4.0}},
                     nullptr, encode_hue_saturation, hue_saturation_is_identity});
    tools.push_back({"black-white", "Black & White", ToolCategory::Matte, 1,
                     {{"red", "Red", 0.27, 0.0, 2.0},
                      {"green", "Green", 0.67, 0.0, 2.0},
                      {"blue", "Blue", 0.06, 0.0, 2.0}},
                     nullptr, encode_black_white, never_identity});
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
                     nullptr, encode_channel_mixer, channel_mixer_is_identity});
    tools.push_back({"shadows-highlights", "Shadows/Highlights", ToolCategory::Matte, 1,
                     {{"shadows", "Shadows", 0.0, -2.0, 2.0},
                      {"highlights", "Highlights", 0.0, -2.0, 2.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_shadows_highlights, shadows_highlights_is_identity});
    tools.push_back({"highlight-shoulder", "Highlight Shoulder", ToolCategory::Camera, 1,
                     {{"start", "Start", 1.0, 0.05, 16.0},
                      {"headroom", "Headroom", 1.5, 0.1, 8.0}},
                     nullptr, encode_highlight_shoulder, never_identity});
    tools.push_back({"three-way", "3-Way Grade", ToolCategory::Matte, 1,
                     {{"shadows-hue", "Shadows Hue", 220.0, 0.0, 360.0},
                      {"shadows-amount", "Shadows Amount", 0.0, 0.0, 1.0},
                      {"midtones-hue", "Midtones Hue", 40.0, 0.0, 360.0},
                      {"midtones-amount", "Midtones Amount", 0.0, 0.0, 1.0},
                      {"highlights-hue", "Highlights Hue", 50.0, 0.0, 360.0},
                      {"highlights-amount", "Highlights Amount", 0.0, 0.0, 1.0},
                      {"pivot", "Pivot", 0.18, 0.001, 10.0}},
                     nullptr, encode_three_way, three_way_is_identity});
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
