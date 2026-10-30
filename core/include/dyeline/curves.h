#pragma once

// Curves drawn through points, as the Curves tool stores them: a list of [x, y] pairs, both 0
// to 1, in a look's params. The same spline is sampled into the tool's table and drawn by the
// Look Designer, so what the editor shows is what renders.
//
// Between points the curve is a monotone cubic (Fritsch-Carlson): it passes through every
// point and never overshoots between two of them, so a curve that only rises keeps rising.
// Before the first point and after the last it continues in a straight line along its end
// slope, which is never allowed to fall.

#include <cstddef>
#include <vector>

#include <nlohmann/json.hpp>

namespace dyeline {

struct CurvePoint {
    double x;
    double y;
};

using Curve = std::vector<CurvePoint>;

inline constexpr std::size_t kMaxCurvePoints = 16;

// The straight line from (0, 0) to (1, 1), which changes nothing.
Curve identity_curve();
bool is_identity_curve(const Curve& curve);

// Reads a stored curve. Values are clamped to 0 to 1, points sorted by x, and points closer
// in x than 1/1000 of the width merged (the later one wins). Anything that leaves fewer than
// two points, including a missing or malformed value, reads as the identity. Extra points past
// kMaxCurvePoints are dropped.
Curve read_curve(const nlohmann::json& value);
nlohmann::json write_curve(const Curve& curve);

// A curve ready to evaluate.
class CurveSpline {
public:
    explicit CurveSpline(Curve points);

    double operator()(double x) const;
    // The straight continuations' slopes: before the first point, and after the last.
    double slope_before() const { return tangents_.front(); }
    double slope_after() const { return tangents_.back(); }

private:
    Curve points_;
    std::vector<double> tangents_;
};

}  // namespace dyeline
