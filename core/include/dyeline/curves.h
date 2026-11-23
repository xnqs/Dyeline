#pragma once

// Curves drawn through points, as the curve tools store them: a list of [x, y] pairs, both 0
// to 1, in a look's params. The same spline is sampled into a tool's table and drawn by the
// Look Designer, so what the editor shows is what renders.
//
// Between points the curve is a monotone cubic (Fritsch-Carlson): it passes through every
// point and never overshoots between two of them, so a curve that only rises keeps rising.
//
// There are two kinds:
// - Tone curves (Curves) run from x = 0 to 1, and the diagonal changes nothing. Before the
//   first point and after the last a tone curve continues in a straight line along its end
//   slope, which is never allowed to fall.
// - Hue curves (Hue Curves) go once around the hue circle, so x = 1 is x = 0 again and the
//   curve wraps from its last point to its first. The level line at y = 1/2 changes nothing.

#include <cstddef>
#include <vector>

#include <nlohmann/json.hpp>

namespace dyeline {

struct CurvePoint {
    double x;
    double y;
};

using Curve = std::vector<CurvePoint>;

enum class CurveKind { Tone, Hue };

inline constexpr std::size_t kMaxCurvePoints = 16;

// The straight line from (0, 0) to (1, 1), which changes nothing.
Curve identity_curve();
bool is_identity_curve(const Curve& curve);

// The curve a tool starts with, which changes nothing: the diagonal for a tone curve, and for
// a hue curve six level points at red, yellow, green, cyan, blue and magenta, ready to drag.
Curve neutral_curve(CurveKind kind);
// True when the curve changes nothing: the diagonal, or a hue curve with every point level.
bool is_neutral_curve(const Curve& curve, CurveKind kind);

// Reads a stored curve. Values are clamped to 0 to 1 (a hue curve's x wraps around instead,
// so 1 is 0), points sorted by x, and points closer in x than 1/1000 of the width merged (the
// later one wins). Extra points past kMaxCurvePoints are dropped. A tone curve needs two points
// and a hue curve one; anything less, including a missing or malformed value, reads as the
// neutral curve.
Curve read_curve(const nlohmann::json& value, CurveKind kind = CurveKind::Tone);
nlohmann::json write_curve(const Curve& curve);

// A curve ready to evaluate.
class CurveSpline {
public:
    explicit CurveSpline(Curve points, CurveKind kind = CurveKind::Tone);

    double operator()(double x) const;
    // A tone curve's straight continuations' slopes: before the first point, and after the
    // last. Hue curves have none, and give 0.
    double slope_before() const { return periodic_ ? 0.0 : tangents_.front(); }
    double slope_after() const { return periodic_ ? 0.0 : tangents_.back(); }

private:
    // A hue curve's points are copied one turn before and after, so its tangents and segments
    // near x = 0 and 1 see the points across the wrap.
    Curve points_;
    std::vector<double> tangents_;
    bool periodic_ = false;
};

}  // namespace dyeline
