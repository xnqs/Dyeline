#include "dyeline/exposure.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using dyeline::ExposureParams;

ExposureParams params(double stops, double strength = 1.0) {
    ExposureParams p;
    p.stops = stops;
    p.strength = strength;
    return p;
}

TEST(Exposure, GainIsTwoToThePowerOfStops) {
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(0.0)), 1.0f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(1.0)), 2.0f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(-1.0)), 0.5f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(3.0)), 8.0f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(0.5)), 1.41421356f);
}

TEST(Exposure, StopsAreClampedToSixteen) {
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(40.0)), 65536.0f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(-40.0)), 1.0f / 65536.0f);
}

TEST(Exposure, StrengthBlendsTowardTheOriginal) {
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(1.0, 0.0)), 1.0f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(1.0, 0.5)), 1.5f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(-1.0, 0.5)), 0.75f);
    EXPECT_FLOAT_EQ(dyeline::exposure_gain(params(1.0, 2.0)), 2.0f);  // clamped to 1
}

TEST(Exposure, IdentityWhenNothingChanges) {
    EXPECT_TRUE(dyeline::exposure_is_identity(params(0.0)));
    EXPECT_TRUE(dyeline::exposure_is_identity(params(2.0, 0.0)));
    EXPECT_FALSE(dyeline::exposure_is_identity(params(0.01)));
}

TEST(Exposure, ScalesRgbAndKeepsAlpha) {
    const std::vector<float> src = {0.18f, 0.5f, 1.0f, 0.25f, 2.0f, -0.1f, 0.0f, 1.0f};
    std::vector<float> dst(src.size());
    dyeline::apply_exposure_row(src.data(), dst.data(), 2, 4, params(1.0));
    const std::vector<float> expected = {0.36f, 1.0f, 2.0f, 0.25f, 4.0f, -0.2f, 0.0f, 1.0f};
    for (std::size_t i = 0; i < src.size(); ++i) {
        EXPECT_FLOAT_EQ(dst[i], expected[i]) << "index " << i;
    }
}

TEST(Exposure, KeepsValuesAboveOne) {
    // Scene-linear highlights must not be clipped.
    const std::vector<float> src = {4.0f, 8.0f, 16.0f};
    std::vector<float> dst(src.size());
    dyeline::apply_exposure_row(src.data(), dst.data(), 1, 3, params(2.0));
    EXPECT_FLOAT_EQ(dst[0], 16.0f);
    EXPECT_FLOAT_EQ(dst[1], 32.0f);
    EXPECT_FLOAT_EQ(dst[2], 64.0f);
}

TEST(Exposure, WorksInPlace) {
    std::vector<float> row = {0.5f, 0.25f, 0.125f};
    dyeline::apply_exposure_row(row.data(), row.data(), 1, 3, params(-1.0));
    EXPECT_FLOAT_EQ(row[0], 0.25f);
    EXPECT_FLOAT_EQ(row[1], 0.125f);
    EXPECT_FLOAT_EQ(row[2], 0.0625f);
}

TEST(Exposure, CopiesAlphaOnlyRows) {
    const std::vector<float> src = {0.0f, 0.5f, 1.0f};
    std::vector<float> dst(src.size(), -1.0f);
    dyeline::apply_exposure_row(src.data(), dst.data(), 3, 1, params(4.0));
    EXPECT_EQ(dst, src);
}

TEST(Exposure, SameResultOnPremultipliedPixels) {
    // Exposure scales RGB by a constant, so unpremultiplying first must not change the result.
    const float alpha = 0.4f;
    const std::vector<float> straight = {0.3f, 0.6f, 0.9f, alpha};
    const std::vector<float> premult = {0.3f * alpha, 0.6f * alpha, 0.9f * alpha, alpha};
    std::vector<float> from_straight(4);
    std::vector<float> from_premult(4);
    dyeline::apply_exposure_row(straight.data(), from_straight.data(), 1, 4, params(1.5));
    dyeline::apply_exposure_row(premult.data(), from_premult.data(), 1, 4, params(1.5));
    for (int c = 0; c < 3; ++c) {
        EXPECT_FLOAT_EQ(from_premult[c], from_straight[c] * alpha);
    }
    EXPECT_FLOAT_EQ(from_premult[3], alpha);
}

}  // namespace
