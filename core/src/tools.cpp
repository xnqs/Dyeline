#include "dyeline/tools.h"

#include <cmath>

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

// Hue/Saturation: a rotation about the grey axis, then a saturation scale that keeps each
// color's luminance. Luminance uses the ACEScg (AP1) weights, the working space looks are
// graded in.
constexpr double kLumaWeights[3] = {0.2722287168, 0.6740817658, 0.0536895174};
constexpr double kPi = 3.14159265358979323846;

void encode_hue_saturation(const ParamValues& v, dl_op& op) {
    const double angle = v[0] * kPi / 180.0;
    const double c = std::cos(angle);
    const double s = std::sin(angle) / std::sqrt(3.0);
    const double t = (1.0 - c) / 3.0;
    // Rodrigues' rotation about (1, 1, 1) / sqrt(3).
    const double rotate[3][3] = {{c + t, t - s, t + s}, {t + s, c + t, t - s}, {t - s, t + s, c + t}};
    const double sat = v[1];
    op.tool = DL_OP_HUE_SATURATION;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double value = 0.0;
            for (int k = 0; k < 3; ++k) {
                const double saturate = (1.0 - sat) * kLumaWeights[k] + (row == k ? sat : 0.0);
                value += saturate * rotate[k][col];
            }
            op.p[row * 3 + col] = static_cast<float>(value);
        }
    }
}

bool hue_saturation_is_identity(const ParamValues& v) {
    return v[0] == 0.0 && v[1] == 1.0;
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
