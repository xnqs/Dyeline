#include "dyeline/color.h"

#include <cmath>

namespace dyeline {

namespace {

using Primaries = std::array<double, 8>;  // CIE xy of red, green, blue and white

// Primaries and white points, from the published specifications.
constexpr Primaries kRec709 = {0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290};
constexpr Primaries kAp1 = {0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767};
constexpr Primaries kArriWideGamut3 = {0.6840, 0.3130, 0.2210, 0.8480,
                                       0.0861, -0.1020, 0.3127, 0.3290};
constexpr Primaries kArriWideGamut4 = {0.7347, 0.2653, 0.1424, 0.8576,
                                       0.0991, -0.0308, 0.3127, 0.3290};
constexpr Primaries kSGamut3 = {0.730, 0.280, 0.140, 0.855, 0.100, -0.050, 0.3127, 0.3290};
constexpr Primaries kSGamut3Cine = {0.766, 0.275, 0.225, 0.800, 0.089, -0.087, 0.3127, 0.3290};
constexpr Primaries kVGamut = {0.730, 0.280, 0.165, 0.840, 0.100, -0.030, 0.3127, 0.3290};
constexpr Primaries kCinemaGamut = {0.740, 0.270, 0.170, 1.140, 0.080, -0.100, 0.3127, 0.3290};
constexpr Primaries kRedWideGamut = {0.780308, 0.304253, 0.121595, 1.493994,
                                     0.095612, -0.084589, 0.3127, 0.3290};
constexpr Primaries kDaVinciWideGamut = {0.8000, 0.3130, 0.1682, 0.9877,
                                         0.0790, -0.1155, 0.3127, 0.3290};

Matrix3 multiply(const Matrix3& a, const Matrix3& b) {
    Matrix3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
            }
        }
    }
    return r;
}

Matrix3 inverse(const Matrix3& m) {
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    return {c00 / det,
            (m[2] * m[7] - m[1] * m[8]) / det,
            (m[1] * m[5] - m[2] * m[4]) / det,
            c01 / det,
            (m[0] * m[8] - m[2] * m[6]) / det,
            (m[2] * m[3] - m[0] * m[5]) / det,
            c02 / det,
            (m[1] * m[6] - m[0] * m[7]) / det,
            (m[0] * m[4] - m[1] * m[3]) / det};
}

std::array<double, 3> transform(const Matrix3& m, const std::array<double, 3>& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

std::array<double, 3> xy_to_xyz(double x, double y) {
    return {x / y, 1.0, (1.0 - x - y) / y};
}

// Linear RGB to CIE XYZ, so that RGB white (1, 1, 1) is the white point at Y = 1.
Matrix3 rgb_to_xyz(const Primaries& p) {
    const auto r = xy_to_xyz(p[0], p[1]);
    const auto g = xy_to_xyz(p[2], p[3]);
    const auto b = xy_to_xyz(p[4], p[5]);
    const Matrix3 m = {r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const auto s = transform(inverse(m), xy_to_xyz(p[6], p[7]));
    return {m[0] * s[0], m[1] * s[1], m[2] * s[2], m[3] * s[0], m[4] * s[1],
            m[5] * s[2], m[6] * s[0], m[7] * s[1], m[8] * s[2]};
}

// The Bradford chromatic adaptation from one white to another, which ACES' own conversions
// between RGB spaces use.
Matrix3 adaptation(double from_x, double from_y, double to_x, double to_y) {
    const Matrix3 bradford = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135,
                              0.0367, 0.0389, -0.0685, 1.0296};
    const auto from = transform(bradford, xy_to_xyz(from_x, from_y));
    const auto to = transform(bradford, xy_to_xyz(to_x, to_y));
    const Matrix3 scale = {to[0] / from[0], 0, 0, 0, to[1] / from[1], 0, 0, 0, to[2] / from[2]};
    return multiply(inverse(bradford), multiply(scale, bradford));
}

Matrix3 identity_matrix() {
    return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

bool near_identity(const Matrix3& m) {
    const Matrix3 one = identity_matrix();
    for (int i = 0; i < 9; ++i) {
        if (std::fabs(m[i] - one[i]) > 1e-9) {
            return false;
        }
    }
    return true;
}

void set_matrix(dl_transform& t, const Matrix3& m) {
    t.matrix = near_identity(m) ? 0 : 1;
    for (int i = 0; i < 9; ++i) {
        t.m[i] = t.matrix ? static_cast<float>(m[i]) : 0.0f;
    }
}

}  // namespace

Matrix3 rgb_to_rgb(const std::array<double, 8>& from, const std::array<double, 8>& to) {
    const Matrix3 m = multiply(inverse(rgb_to_xyz(to)),
                               multiply(adaptation(from[6], from[7], to[6], to[7]),
                                        rgb_to_xyz(from)));
    return near_identity(m) ? identity_matrix() : m;
}

const std::vector<ColorSpace>& all_color_spaces() {
    static const std::vector<ColorSpace> spaces = {
        {"linear", "Linear (Rec.709 primaries)", DL_CURVE_LINEAR, rgb_to_rgb(kRec709, kAp1)},
        {"rec709", "Rec.709 (gamma 2.4)", DL_CURVE_GAMMA_2_4, rgb_to_rgb(kRec709, kAp1)},
        {"srgb", "sRGB", DL_CURVE_SRGB, rgb_to_rgb(kRec709, kAp1)},
        {"acescg", "ACEScg", DL_CURVE_LINEAR, identity_matrix()},
        {"acescct", "ACEScct", DL_CURVE_ACESCCT, identity_matrix()},
        {"logc3", "ARRI LogC3 (EI 800), Wide Gamut 3", DL_CURVE_LOGC3,
         rgb_to_rgb(kArriWideGamut3, kAp1)},
        {"logc4", "ARRI LogC4, Wide Gamut 4", DL_CURVE_LOGC4,
         rgb_to_rgb(kArriWideGamut4, kAp1)},
        {"slog3", "Sony S-Log3, S-Gamut3.Cine", DL_CURVE_SLOG3,
         rgb_to_rgb(kSGamut3Cine, kAp1)},
        {"slog3-sgamut3", "Sony S-Log3, S-Gamut3", DL_CURVE_SLOG3,
         rgb_to_rgb(kSGamut3, kAp1)},
        {"vlog", "Panasonic V-Log, V-Gamut", DL_CURVE_VLOG, rgb_to_rgb(kVGamut, kAp1)},
        {"canon-log3", "Canon Log 3, Cinema Gamut", DL_CURVE_CLOG3,
         rgb_to_rgb(kCinemaGamut, kAp1)},
        {"log3g10", "RED Log3G10, REDWideGamutRGB", DL_CURVE_LOG3G10,
         rgb_to_rgb(kRedWideGamut, kAp1)},
        {"davinci-intermediate", "DaVinci Intermediate, Wide Gamut", DL_CURVE_DAVINCI_INTERMEDIATE,
         rgb_to_rgb(kDaVinciWideGamut, kAp1)},
    };
    return spaces;
}

const ColorSpace* find_color_space(std::string_view id) {
    for (const ColorSpace& space : all_color_spaces()) {
        if (id == space.id) {
            return &space;
        }
    }
    return nullptr;
}

bool encode_color(const LookColor& color, dl_transform& input, dl_transform& output,
                  std::string& problem) {
    if (color.mode != "tone" && color.mode != "full") {
        problem = "unknown color mode \"" + color.mode + "\"";
        return false;
    }
    const ColorSpace* in = find_color_space(color.input);
    if (in == nullptr) {
        problem = "unknown input color space \"" + color.input + "\"";
        return false;
    }
    const ColorSpace* out = color.output == "same" ? in : find_color_space(color.output);
    if (out == nullptr) {
        problem = "unknown output color space \"" + color.output + "\"";
        return false;
    }
    const bool full = color.mode == "full";
    dl_transform to_working{};
    to_working.decode = in->curve;
    set_matrix(to_working, full ? in->to_working : identity_matrix());
    dl_transform from_working{};
    from_working.encode = out->curve;
    set_matrix(from_working, full ? inverse(out->to_working) : identity_matrix());
    input = to_working;
    output = from_working;
    return true;
}

bool is_identity(const dl_transform& transform) {
    return transform.decode == DL_CURVE_LINEAR && transform.encode == DL_CURVE_LINEAR &&
           transform.matrix == 0;
}

}  // namespace dyeline
