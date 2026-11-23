#pragma once

// Editing a curve on the designer's graph (see curves.h). Positions are graph units: 0 to 1
// on both axes, y up. Kept apart from the window so each edit is tested without one.

#include "dyeline/curves.h"

namespace dyeline::designer {

// Points stay at least this far apart in x, so a drag never lands one on its neighbor.
inline constexpr double kCurvePointGap = 0.01;

// The point within `reach` of (x, y), the nearest when several are, or -1.
int curve_point_near(const Curve& curve, double x, double y, double reach);

// Adds a point on the curve at x, so the curve keeps its shape until it is dragged, and
// returns its index. Returns -1 when the curve is full or x is too close to another point.
int add_curve_point(Curve& curve, double x);

// Moves point i toward (x, y), within the graph and between its neighbors.
void move_curve_point(Curve& curve, int i, double x, double y);

// Removes point i, unless only two points are left. Returns whether it did.
bool remove_curve_point(Curve& curve, int i);

}  // namespace dyeline::designer
