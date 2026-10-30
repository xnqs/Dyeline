#include "curve_edit.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

namespace dyeline::designer {

int curve_point_near(const Curve& curve, double x, double y, double reach) {
    int nearest = -1;
    double best = reach;
    for (std::size_t i = 0; i < curve.size(); ++i) {
        const double d = std::hypot(curve[i].x - x, curve[i].y - y);
        if (d <= best) {
            best = d;
            nearest = static_cast<int>(i);
        }
    }
    return nearest;
}

int add_curve_point(Curve& curve, double x) {
    if (curve.size() >= kMaxCurvePoints || x < 0.0 || x > 1.0) {
        return -1;
    }
    const auto at = std::lower_bound(curve.begin(), curve.end(), x,
                                     [](const CurvePoint& p, double v) { return p.x < v; });
    if ((at != curve.end() && at->x - x < kCurvePointGap) ||
        (at != curve.begin() && x - std::prev(at)->x < kCurvePointGap)) {
        return -1;
    }
    const double y = std::clamp(CurveSpline(curve)(x), 0.0, 1.0);
    // The index first: inserting may move the points, so begin() is only good after it.
    const auto index = at - curve.begin();
    curve.insert(at, {x, y});
    return static_cast<int>(index);
}

void move_curve_point(Curve& curve, int i, double x, double y) {
    if (i < 0 || static_cast<std::size_t>(i) >= curve.size()) {
        return;
    }
    const std::size_t k = static_cast<std::size_t>(i);
    const double lo = k > 0 ? curve[k - 1].x + kCurvePointGap : 0.0;
    const double hi = k + 1 < curve.size() ? curve[k + 1].x - kCurvePointGap : 1.0;
    curve[k].x = std::clamp(x, std::min(lo, curve[k].x), std::max(hi, curve[k].x));
    curve[k].y = std::clamp(y, 0.0, 1.0);
}

bool remove_curve_point(Curve& curve, int i) {
    if (curve.size() <= 2 || i < 0 || static_cast<std::size_t>(i) >= curve.size()) {
        return false;
    }
    curve.erase(curve.begin() + i);
    return true;
}

}  // namespace dyeline::designer
