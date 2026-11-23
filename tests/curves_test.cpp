// The spline the Curves tool samples and the designer draws.

#include "dyeline/curves.h"

#include <gtest/gtest.h>

namespace {

using dyeline::Curve;
using dyeline::CurveSpline;
using dyeline::read_curve;

TEST(Curves, MissingOrMalformedReadsAsTheIdentity) {
    EXPECT_TRUE(dyeline::is_identity_curve(read_curve(nlohmann::json())));
    EXPECT_TRUE(dyeline::is_identity_curve(read_curve(nlohmann::json("curve"))));
    EXPECT_TRUE(dyeline::is_identity_curve(read_curve(nlohmann::json::parse("[[0.5, 0.7]]"))));
    EXPECT_TRUE(dyeline::is_identity_curve(read_curve(nlohmann::json::parse("[[0, 0], [\"a\", 1]]"))));
    EXPECT_FALSE(dyeline::is_identity_curve(read_curve(nlohmann::json::parse("[[0, 0.1], [1, 1]]"))));
}

TEST(Curves, ReadingSortsClampsAndMerges) {
    const Curve curve = read_curve(nlohmann::json::parse("[[1.5, 1], [0.5, -2], [0, 0], [0.5004, 0.6]]"));
    ASSERT_EQ(curve.size(), 3u);
    EXPECT_DOUBLE_EQ(curve[0].x, 0.0);
    EXPECT_DOUBLE_EQ(curve[1].x, 0.5004);  // the later of two points closer than 1/1000
    EXPECT_DOUBLE_EQ(curve[1].y, 0.6);
    EXPECT_DOUBLE_EQ(curve[2].x, 1.0);     // clamped into the graph
    EXPECT_EQ(read_curve(dyeline::write_curve(curve)).size(), 3u);
}

TEST(Curves, KeepsAtMostSixteenPoints) {
    nlohmann::json many = nlohmann::json::array();
    for (int i = 0; i < 40; ++i) {
        many.push_back({i / 39.0, i / 39.0});
    }
    EXPECT_EQ(read_curve(many).size(), dyeline::kMaxCurvePoints);
}

TEST(Curves, PassesThroughEveryPointWithoutOvershooting) {
    const Curve curve = {{0.0, 0.0}, {0.3, 0.45}, {0.5, 0.5}, {0.8, 0.95}, {1.0, 1.0}};
    const CurveSpline spline(curve);
    for (const auto& p : curve) {
        EXPECT_NEAR(spline(p.x), p.y, 1e-12);
    }
    // The points only rise, so the curve only rises.
    double previous = -1.0;
    for (int i = 0; i <= 1000; ++i) {
        const double y = spline(i / 1000.0);
        EXPECT_GE(y, previous) << i;
        previous = y;
    }
    // A point that turns the curve is its highest: no bump beyond it.
    const CurveSpline peak({{0.0, 0.0}, {0.5, 0.8}, {1.0, 0.6}});
    for (int i = 0; i <= 100; ++i) {
        EXPECT_LE(peak(i / 100.0), 0.8 + 1e-12);
    }
}

TEST(Curves, ContinuesStraightAndNeverFallsPastItsEnds) {
    const CurveSpline straight(dyeline::identity_curve());
    EXPECT_DOUBLE_EQ(straight(-0.5), -0.5);
    EXPECT_DOUBLE_EQ(straight(1.5), 1.5);
    // Lifted blacks hold level below the graph; a top pulled down holds level above it.
    const CurveSpline faded({{0.0, 0.2}, {0.4, 0.25}, {1.0, 0.7}});
    EXPECT_GE(faded.slope_before(), 0.0);
    EXPECT_NEAR(faded(-1.0), 0.2 - faded.slope_before(), 1e-12);
    const CurveSpline falling({{0.0, 0.0}, {0.6, 0.9}, {1.0, 0.5}});
    EXPECT_DOUBLE_EQ(falling.slope_after(), 0.0);
    EXPECT_DOUBLE_EQ(falling(2.0), 0.5);
}

TEST(HueCurves, StartLevelAndReadWrappingAround) {
    using dyeline::CurveKind;
    const Curve neutral = read_curve(nlohmann::json(), CurveKind::Hue);
    EXPECT_EQ(neutral.size(), 6u);
    EXPECT_TRUE(dyeline::is_neutral_curve(neutral, CurveKind::Hue));
    // x wraps: 1.25 is a quarter turn, and a point just short of 1 merges with one at 0.
    const Curve curve = read_curve(nlohmann::json::parse("[[0, 0.5], [1.25, 0.7], [0.9996, 0.2]]"),
                                   CurveKind::Hue);
    ASSERT_EQ(curve.size(), 2u);
    EXPECT_DOUBLE_EQ(curve[0].x, 0.0);
    EXPECT_DOUBLE_EQ(curve[0].y, 0.2);
    EXPECT_DOUBLE_EQ(curve[1].x, 0.25);
    EXPECT_FALSE(dyeline::is_neutral_curve(curve, CurveKind::Hue));
    // One point is enough: a level curve at its height.
    const Curve one = read_curve(nlohmann::json::parse("[[0.3, 0.8]]"), CurveKind::Hue);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_DOUBLE_EQ(CurveSpline(one, CurveKind::Hue)(0.9), 0.8);
}

TEST(HueCurves, AreSmoothAcrossTheWrap) {
    const Curve curve = {{0.05, 0.9}, {1.0 / 3.0, 0.5}, {0.6, 0.5}, {0.9, 0.7}};
    const CurveSpline spline(curve, dyeline::CurveKind::Hue);
    for (const auto& p : curve) {
        EXPECT_NEAR(spline(p.x), p.y, 1e-12);
    }
    EXPECT_NEAR(spline(0.0), spline(1.0), 1e-12);
    EXPECT_NEAR(spline(-0.1), spline(0.9), 1e-12);
    // Between 0.9 and 0.05 (through 0) the curve rises without bumping past either point.
    double previous = spline(0.9);
    for (int i = 1; i <= 150; ++i) {
        const double y = spline(0.9 + i / 1000.0);
        EXPECT_GE(y, previous - 1e-12) << i;
        EXPECT_LE(y, 0.9 + 1e-12);
        previous = y;
    }
    EXPECT_DOUBLE_EQ(spline.slope_before(), 0.0);
}

}  // namespace
