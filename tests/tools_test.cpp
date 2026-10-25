// Pins the math of each per-pixel tool, run through the fused pass on the CPU.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/tools.h"

namespace {

using dyeline::Look;
using dyeline::ToolEntry;

ToolEntry tool(const char* id, nlohmann::json params) {
    ToolEntry entry;
    entry.tool = id;
    entry.params = std::move(params);
    return entry;
}

// Runs one tool on one straight-alpha RGBA pixel.
std::vector<float> run(const ToolEntry& entry, std::vector<float> px) {
    Look look;
    look.chain = {entry};
    dyeline::CompiledChain chain;
    std::string error;
    EXPECT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    std::vector<float> out(4);
    dyeline::render_chain_row(px.data(), out.data(), 1, 4, false, chain.pass);
    return out;
}

float luma(const std::vector<float>& c) {
    return 0.2722287168f * c[0] + 0.6740817658f * c[1] + 0.0536895174f * c[2];
}

bool skipped(const ToolEntry& entry) {
    Look look;
    look.chain = {entry};
    dyeline::CompiledChain chain;
    std::string error;
    return dyeline::compile_chain(look, 1.0, chain, error) && chain.pass.count == 0;
}

TEST(Tools, AllAreRegistered) {
    for (const char* id : {"exposure", "contrast", "s-curve", "hue-saturation"}) {
        EXPECT_NE(dyeline::find_tool(id), nullptr) << id;
    }
}

TEST(Contrast, ScalesStopsAroundThePivot) {
    const auto out = run(tool("contrast", {{"contrast", 2.0}, {"pivot", 0.18}}),
                         {0.18f, 0.36f, 0.09f, 1.0f});
    EXPECT_NEAR(out[0], 0.18f, 1e-6f);  // the pivot stays put
    EXPECT_NEAR(out[1], 0.72f, 1e-6f);  // one stop above becomes two
    EXPECT_NEAR(out[2], 0.045f, 1e-6f); // one stop below becomes two
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(Contrast, PassesNonPositiveValuesAndSkipsWhenNeutral) {
    const auto out = run(tool("contrast", {{"contrast", 0.5}}), {0.0f, -0.25f, 0.18f, 1.0f});
    EXPECT_FLOAT_EQ(out[0], 0.0f);
    EXPECT_FLOAT_EQ(out[1], -0.25f);
    EXPECT_TRUE(skipped(tool("contrast", {{"contrast", 1.0}})));
}

TEST(SCurve, KeepsThePivotAndTheEnds) {
    const ToolEntry curve = tool("s-curve", {{"amount", 0.7}, {"pivot", 0.18}, {"range", 5.0}});
    const float top = 0.18f * 32.0f;    // 5 stops above
    const float bottom = 0.18f / 32.0f; // 5 stops below
    const auto out = run(curve, {0.18f, top, bottom, 1.0f});
    EXPECT_NEAR(out[0], 0.18f, 1e-6f);
    EXPECT_NEAR(out[1], top, top * 1e-5f);
    EXPECT_NEAR(out[2], bottom, bottom * 1e-5f);
}

TEST(SCurve, AddsMidtoneContrastAndNeverReverses) {
    const ToolEntry curve = tool("s-curve", {{"amount", 1.0}});
    // Half a stop above the pivot gets brighter, half a stop below gets darker.
    const auto mid = run(curve, {0.18f * 1.41421356f, 0.18f / 1.41421356f, 0.18f, 1.0f});
    EXPECT_GT(mid[0], 0.18f * 1.41421356f);
    EXPECT_LT(mid[1], 0.18f / 1.41421356f);
    // Rising everywhere, from deep shadows to far past the range.
    float previous = 0.0f;
    for (float stops = -12.0f; stops <= 12.0f; stops += 0.125f) {
        const float v = 0.18f * std::exp2(stops);
        const float out = run(curve, {v, v, v, 1.0f})[0];
        EXPECT_GT(out, previous) << "at " << stops << " stops";
        previous = out;
    }
}

TEST(SCurve, SkipsAtZeroAmount) {
    EXPECT_TRUE(skipped(tool("s-curve", {{"amount", 0.0}})));
    EXPECT_FALSE(skipped(tool("s-curve", nlohmann::json::object())));  // default is 0.5
}

TEST(HueSaturation, ZeroSaturationGivesGreyOfTheSameLuminance) {
    const std::vector<float> in = {0.8f, 0.3f, 0.1f, 1.0f};
    const auto out = run(tool("hue-saturation", {{"saturation", 0.0}}), in);
    EXPECT_NEAR(out[0], luma(in), 1e-6f);
    EXPECT_NEAR(out[1], luma(in), 1e-6f);
    EXPECT_NEAR(out[2], luma(in), 1e-6f);
}

TEST(HueSaturation, SaturationKeepsLuminance) {
    const std::vector<float> in = {0.8f, 0.3f, 0.1f, 1.0f};
    const auto out = run(tool("hue-saturation", {{"saturation", 2.0}}), in);
    EXPECT_NEAR(luma(out), luma(in), 1e-6f);
    EXPECT_GT(out[0] - out[2], in[0] - in[2]);
}

TEST(HueSaturation, HueTurnsAroundTheGreyAxis) {
    // A third of a turn takes red to green and green to blue.
    const auto red = run(tool("hue-saturation", {{"hue", 120.0}}), {1.0f, 0.0f, 0.0f, 1.0f});
    EXPECT_NEAR(red[0], 0.0f, 1e-6f);
    EXPECT_NEAR(red[1], 1.0f, 1e-6f);
    EXPECT_NEAR(red[2], 0.0f, 1e-6f);
    // Greys don't move.
    const auto grey = run(tool("hue-saturation", {{"hue", 73.0}}), {0.4f, 0.4f, 0.4f, 1.0f});
    EXPECT_NEAR(grey[0], 0.4f, 1e-6f);
    EXPECT_NEAR(grey[2], 0.4f, 1e-6f);
    EXPECT_TRUE(skipped(tool("hue-saturation", {{"hue", 0.0}, {"saturation", 1.0}})));
}

}  // namespace
