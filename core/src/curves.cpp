#include "dyeline/curves.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dyeline {

namespace {

// Points closer than this in x are merged.
constexpr double kMinGap = 1e-3;

double sign(double v) {
    return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0);
}

// The end tangent from the two segments nearest an end (Moler's PCHIP end condition): a
// one-sided three-point estimate, kept from changing sign and from overshooting.
double end_tangent(double h0, double h1, double d0, double d1) {
    double m = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    if (sign(m) != sign(d0)) {
        m = 0.0;
    } else if (sign(d0) != sign(d1) && std::fabs(m) > std::fabs(3.0 * d0)) {
        m = 3.0 * d0;
    }
    return m;
}

}  // namespace

Curve identity_curve() {
    return {{0.0, 0.0}, {1.0, 1.0}};
}

bool is_identity_curve(const Curve& curve) {
    return std::all_of(curve.begin(), curve.end(), [](const CurvePoint& p) { return p.x == p.y; }) &&
           curve.size() >= 2 && curve.front().x == 0.0 && curve.back().x == 1.0;
}

Curve read_curve(const nlohmann::json& value) {
    if (!value.is_array()) {
        return identity_curve();
    }
    Curve points;
    for (const nlohmann::json& point : value) {
        if (points.size() == kMaxCurvePoints) {
            break;
        }
        if (point.is_array() && point.size() == 2 && point[0].is_number() && point[1].is_number()) {
            const double x = point[0].get<double>();
            const double y = point[1].get<double>();
            if (std::isfinite(x) && std::isfinite(y)) {
                points.push_back({std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
            }
        }
    }
    std::stable_sort(points.begin(), points.end(),
                     [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    Curve merged;
    for (const CurvePoint& p : points) {
        if (!merged.empty() && p.x - merged.back().x < kMinGap) {
            merged.back() = p;
        } else {
            merged.push_back(p);
        }
    }
    return merged.size() >= 2 ? merged : identity_curve();
}

nlohmann::json write_curve(const Curve& curve) {
    nlohmann::json out = nlohmann::json::array();
    for (const CurvePoint& p : curve) {
        out.push_back({p.x, p.y});
    }
    return out;
}

// Tangents by PCHIP (Fritsch-Carlson, with Fritsch-Butland's weighted harmonic mean inside):
// zero where the curve turns, so no segment overshoots its two points.
CurveSpline::CurveSpline(Curve points) : points_(std::move(points)) {
    if (points_.size() < 2) {
        points_ = identity_curve();
    }
    const std::size_t n = points_.size();
    std::vector<double> h(n - 1);
    std::vector<double> d(n - 1);
    for (std::size_t k = 0; k + 1 < n; ++k) {
        h[k] = points_[k + 1].x - points_[k].x;
        d[k] = (points_[k + 1].y - points_[k].y) / h[k];
    }
    tangents_.assign(n, 0.0);
    if (n == 2) {
        tangents_[0] = d[0];
        tangents_[1] = d[0];
    } else {
        for (std::size_t k = 1; k + 1 < n; ++k) {
            if (d[k - 1] * d[k] > 0.0) {
                const double w1 = 2.0 * h[k] + h[k - 1];
                const double w2 = h[k] + 2.0 * h[k - 1];
                tangents_[k] = (w1 + w2) / (w1 / d[k - 1] + w2 / d[k]);
            }
        }
        tangents_[0] = end_tangent(h[0], h[1], d[0], d[1]);
        tangents_[n - 1] = end_tangent(h[n - 2], h[n - 3], d[n - 2], d[n - 3]);
    }
    // A falling continuation would send the darkest or brightest values off to nothing or to
    // infinity, so the ends hold level instead.
    tangents_.front() = std::max(tangents_.front(), 0.0);
    tangents_.back() = std::max(tangents_.back(), 0.0);
}

double CurveSpline::operator()(double x) const {
    if (x <= points_.front().x) {
        return points_.front().y + (x - points_.front().x) * tangents_.front();
    }
    if (x >= points_.back().x) {
        return points_.back().y + (x - points_.back().x) * tangents_.back();
    }
    const auto upper = std::upper_bound(points_.begin(), points_.end(), x,
                                        [](double v, const CurvePoint& p) { return v < p.x; });
    const std::size_t k = static_cast<std::size_t>(upper - points_.begin()) - 1;
    const CurvePoint& a = points_[k];
    const CurvePoint& b = points_[k + 1];
    const double h = b.x - a.x;
    const double t = (x - a.x) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2.0 * t3 - 3.0 * t2 + 1.0) * a.y + (t3 - 2.0 * t2 + t) * h * tangents_[k] +
           (-2.0 * t3 + 3.0 * t2) * b.y + (t3 - t2) * h * tangents_[k + 1];
}

}  // namespace dyeline
