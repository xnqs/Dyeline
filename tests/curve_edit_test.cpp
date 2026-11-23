// Editing a curve on the designer's graph.

#include "curve_edit.h"

#include <gtest/gtest.h>

namespace {

using dyeline::Curve;
using namespace dyeline::designer;

TEST(CurveEdit, FindsTheNearestPointInReach) {
    const Curve curve = {{0.0, 0.0}, {0.5, 0.6}, {1.0, 1.0}};
    EXPECT_EQ(curve_point_near(curve, 0.52, 0.58, 0.05), 1);
    EXPECT_EQ(curve_point_near(curve, 0.98, 0.99, 0.05), 2);
    EXPECT_EQ(curve_point_near(curve, 0.25, 0.8, 0.05), -1);
}

TEST(CurveEdit, AddsPointsOnTheCurve) {
    Curve curve = dyeline::identity_curve();
    EXPECT_EQ(add_curve_point(curve, 0.25), 1);
    ASSERT_EQ(curve.size(), 3u);
    EXPECT_NEAR(curve[1].y, 0.25, 1e-12);  // on the line, so nothing changes until dragged
    EXPECT_EQ(add_curve_point(curve, 0.255), -1);  // too close to the last one
    EXPECT_EQ(add_curve_point(curve, 0.75), 2);
    while (curve.size() < dyeline::kMaxCurvePoints) {
        ASSERT_GE(add_curve_point(curve, 0.3 + 0.02 * static_cast<double>(curve.size())), 0);
    }
    EXPECT_EQ(add_curve_point(curve, 0.05), -1);  // full
}

TEST(CurveEdit, MovesPointsBetweenTheirNeighbors) {
    Curve curve = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    move_curve_point(curve, 1, 1.4, 1.3);
    EXPECT_DOUBLE_EQ(curve[1].x, 1.0 - kCurvePointGap);
    EXPECT_DOUBLE_EQ(curve[1].y, 1.0);
    move_curve_point(curve, 0, 0.5, -0.2);
    EXPECT_DOUBLE_EQ(curve[0].x, 0.5);  // an end may move in, toward its neighbor
    EXPECT_DOUBLE_EQ(curve[0].y, 0.0);
    move_curve_point(curve, 0, -0.3, 0.1);
    EXPECT_DOUBLE_EQ(curve[0].x, 0.0);
    move_curve_point(curve, 7, 0.5, 0.5);  // no such point: nothing happens
    EXPECT_EQ(curve.size(), 3u);
}

TEST(CurveEdit, KeepsTwoPoints) {
    Curve curve = {{0.0, 0.0}, {0.5, 0.6}, {1.0, 1.0}};
    EXPECT_TRUE(remove_curve_point(curve, 1));
    EXPECT_FALSE(remove_curve_point(curve, 0));
    EXPECT_FALSE(remove_curve_point(curve, -1));
    EXPECT_EQ(curve.size(), 2u);
}

}  // namespace
