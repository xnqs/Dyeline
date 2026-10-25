// Pins the math of each per-pixel tool, run through the fused pass on the CPU.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/tools.h"
#include "temp_file.h"

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
    dyeline::render_chain_row(px.data(), out.data(), 1, 4, false, chain);
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
    for (const char* id : {"exposure", "contrast", "s-curve", "hue-saturation", "black-white",
                           "channel-mixer", "shadows-highlights", "highlight-shoulder", "three-way", "lut"}) {
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

TEST(BlackWhite, MixesChannelsIntoGrey) {
    // Weights are scaled to sum to 1: a pure red filter makes grey from red alone.
    const auto red_filter = run(tool("black-white", {{"red", 2.0}, {"green", 0.0}, {"blue", 0.0}}),
                                {0.6f, 0.2f, 0.1f, 1.0f});
    EXPECT_NEAR(red_filter[0], 0.6f, 1e-6f);
    EXPECT_NEAR(red_filter[1], 0.6f, 1e-6f);
    EXPECT_NEAR(red_filter[2], 0.6f, 1e-6f);
    // Greys stay the same brightness whatever the weights.
    const auto grey = run(tool("black-white", {{"red", 0.1}, {"green", 0.5}, {"blue", 1.0}}),
                          {0.3f, 0.3f, 0.3f, 1.0f});
    EXPECT_NEAR(grey[0], 0.3f, 1e-6f);
    // All-zero weights fall back to luminance.
    const std::vector<float> in = {0.8f, 0.3f, 0.1f, 1.0f};
    const auto zero = run(tool("black-white", {{"red", 0.0}, {"green", 0.0}, {"blue", 0.0}}), in);
    EXPECT_NEAR(zero[1], luma(in), 1e-6f);
}

TEST(ChannelMixer, MixesEachOutputFromTheInputs) {
    const auto out = run(tool("channel-mixer", {{"red-from-red", 0.0}, {"red-from-blue", 1.0},
                                                {"blue-from-blue", 0.0}, {"blue-from-red", 1.0},
                                                {"green-from-green", 0.5}}),
                         {0.8f, 0.4f, 0.2f, 1.0f});
    EXPECT_NEAR(out[0], 0.2f, 1e-6f);  // red and blue swapped
    EXPECT_NEAR(out[1], 0.2f, 1e-6f);
    EXPECT_NEAR(out[2], 0.8f, 1e-6f);
    EXPECT_TRUE(skipped(tool("channel-mixer", nlohmann::json::object())));
}

TEST(ShadowsHighlights, MovesOnlyItsOwnEndOfTheRange) {
    const ToolEntry lift = tool("shadows-highlights", {{"shadows", 1.0}});
    const float deep = 0.18f / 128.0f;  // 7 stops under the pivot: fully in the shadows
    // The gain follows the pixel's luminance, so test grey pixels one at a time.
    EXPECT_NEAR(run(lift, {deep, deep, deep, 1.0f})[0], deep * 2.0f, deep * 1e-5f);
    EXPECT_NEAR(run(lift, {0.18f, 0.18f, 0.18f, 1.0f})[0], 0.18f, 1e-6f);
    const float bright = 0.18f * 64.0f;
    EXPECT_NEAR(run(lift, {bright, bright, bright, 1.0f})[0], bright, bright * 1e-6f);
    const auto dim = run(tool("shadows-highlights", {{"highlights", -1.0}}),
                         {bright, bright, bright, 1.0f});
    EXPECT_NEAR(dim[0], bright * 0.5f, bright * 1e-5f);
}

TEST(ShadowsHighlights, KeepsHueAndNeverReverses) {
    const auto out = run(tool("shadows-highlights", {{"shadows", 2.0}}), {0.02f, 0.01f, 0.005f, 1.0f});
    EXPECT_NEAR(out[0] / out[1], 2.0f, 1e-5f);
    EXPECT_NEAR(out[1] / out[2], 2.0f, 1e-5f);
    for (double amount : {-2.0, 2.0}) {
        const ToolEntry t = tool("shadows-highlights", {{"shadows", amount}, {"highlights", -amount}});
        float previous = 0.0f;
        for (float stops = -12.0f; stops <= 12.0f; stops += 0.125f) {
            const float v = 0.18f * std::exp2(stops);
            const float o = run(t, {v, v, v, 1.0f})[0];
            EXPECT_GT(o, previous) << "amount " << amount << " at " << stops << " stops";
            previous = o;
        }
    }
}

TEST(HighlightShoulder, LeavesValuesBelowStartAndApproachesTheCeiling) {
    const ToolEntry shoulder = tool("highlight-shoulder", {{"start", 1.0}, {"headroom", 1.0}});
    const auto low = run(shoulder, {0.5f, 1.0f, 0.0f, 1.0f});
    EXPECT_FLOAT_EQ(low[0], 0.5f);
    EXPECT_FLOAT_EQ(low[1], 1.0f);
    // Just above the start the slope is 1; far above, values approach start * 2^headroom = 2.
    const auto near = run(shoulder, {1.001f, 1.001f, 1.001f, 1.0f});
    EXPECT_NEAR(near[0], 1.001f, 1e-5f);
    const auto far = run(shoulder, {8.0f, 4.0f, 2.0f, 1.0f});
    EXPECT_LT(far[0], 2.0f);
    EXPECT_GT(far[0], 1.999f);
    EXPECT_LT(far[2], far[1]);
    EXPECT_LT(far[1], far[0]);
    // Never past the ceiling, however bright.
    EXPECT_LE(run(shoulder, {1e6f, 1e6f, 1e6f, 1.0f})[0], 2.0f);
}

TEST(ThreeWay, TintsEachRangeWithoutChangingItsLuminance) {
    const float deep = 0.18f / 128.0f;
    const float bright = 0.18f * 64.0f;
    // Blue shadows, orange highlights.
    const ToolEntry grade = tool("three-way", {{"shadows-hue", 240.0}, {"shadows-amount", 1.0},
                                               {"highlights-hue", 30.0}, {"highlights-amount", 1.0}});
    const auto low = run(grade, {deep, deep, deep, 1.0f});
    EXPECT_GT(low[2], low[0]);
    EXPECT_GT(low[2], low[1]);
    const auto high = run(grade, {bright, bright, bright, 1.0f});
    EXPECT_GT(high[0], high[1]);
    EXPECT_GT(high[1], high[2]);
    // No midtone tint: the pivot grey is left alone.
    const auto mid = run(grade, {0.18f, 0.18f, 0.18f, 1.0f});
    EXPECT_NEAR(mid[0], 0.18f, 1e-6f);
    EXPECT_NEAR(mid[2], 0.18f, 1e-6f);
    // The tint takes away its luminance-weighted mean in stops, so a small tint barely
    // changes luminance.
    const auto subtle = run(tool("three-way", {{"midtones-hue", 120.0}, {"midtones-amount", 0.1}}),
                            {0.18f, 0.18f, 0.18f, 1.0f});
    EXPECT_NEAR(luma(subtle), 0.18f, 0.18f * 0.01f);
    EXPECT_GT(subtle[1], subtle[0]);
}

TEST(ThreeWay, NeverReversesAndSkipsWhenUntinted) {
    for (double hue = 0.0; hue < 360.0; hue += 30.0) {
        const ToolEntry t = tool("three-way", {{"shadows-hue", hue}, {"shadows-amount", 1.0},
                                               {"midtones-hue", hue + 180.0 > 360.0 ? hue - 180.0 : hue + 180.0},
                                               {"midtones-amount", 1.0},
                                               {"highlights-hue", hue}, {"highlights-amount", 1.0}});
        std::vector<float> previous(3, 0.0f);
        for (float stops = -12.0f; stops <= 12.0f; stops += 0.125f) {
            const float v = 0.18f * std::exp2(stops);
            const auto o = run(t, {v, v, v, 1.0f});
            for (int k = 0; k < 3; ++k) {
                EXPECT_GT(o[k], previous[k]) << "hue " << hue << " at " << stops << " stops";
                previous[k] = o[k];
            }
        }
    }
    EXPECT_TRUE(skipped(tool("three-way", {{"shadows-hue", 90.0}})));
}

using dyeline::test::cube_3d;
using dyeline::test::TempFile;

ToolEntry lut_tool(const TempFile& file, double gamma = 1.0) {
    return tool("lut", {{"file", file.path()}, {"gamma", gamma}});
}

TEST(Lut, IdentityTableGivesTheInputBack) {
    const TempFile file(cube_3d(5, [](double r, double g, double b, double out[3]) {
                            out[0] = r;
                            out[1] = g;
                            out[2] = b;
                        }),
                        ".cube");
    for (const std::vector<float>& px : {std::vector<float>{0.1f, 0.6f, 0.33f, 0.5f},
                                         std::vector<float>{0.9f, 0.05f, 0.5f, 1.0f}}) {
        const auto out = run(lut_tool(file), px);
        for (int k = 0; k < 4; ++k) {
            EXPECT_NEAR(out[k], px[k], 1e-6f);
        }
        // LUT Gamma cancels out around an identity table.
        const auto gamma = run(lut_tool(file, 2.4), px);
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(gamma[k], px[k], 1e-5f);
        }
    }
}

TEST(Lut, TetrahedralInterpolationIsExactForLinearTables) {
    const auto linear = [](double r, double g, double b, double out[3]) {
        out[0] = 0.8 * r + 0.15 * g + 0.05 * b;
        out[1] = 0.1 * r + 0.7 * g + 0.2 * b + 0.02;
        out[2] = -0.1 * r + 0.3 * g + 0.9 * b;
    };
    const TempFile file(cube_3d(3, linear), ".cube");
    // One point inside each of a cell's six tetrahedra: every ordering of the fractions.
    const float a = 0.1f, b = 0.25f, c = 0.4f;
    const float orders[6][3] = {{c, b, a}, {c, a, b}, {b, a, c}, {a, b, c}, {a, c, b}, {b, c, a}};
    for (const auto& o : orders) {
        double expected[3];
        linear(o[0], o[1], o[2], expected);
        const auto out = run(lut_tool(file), {o[0], o[1], o[2], 1.0f});
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(out[k], expected[k], 1e-6) << o[0] << " " << o[1] << " " << o[2];
        }
    }
}

TEST(Lut, HoldsValuesOutsideTheDomainAtItsEdges) {
    const TempFile file(cube_3d(2, [](double r, double g, double b, double out[3]) {
                            out[0] = 0.5 * r;
                            out[1] = 0.5 * g;
                            out[2] = 0.5 * b;
                        }),
                        ".cube");
    const auto out = run(lut_tool(file), {4.0f, -1.0f, 0.5f, 1.0f});
    EXPECT_NEAR(out[0], 0.5f, 1e-6f);
    EXPECT_NEAR(out[1], 0.0f, 1e-6f);
    EXPECT_NEAR(out[2], 0.25f, 1e-6f);
}

TEST(Lut, OneDimensionalTablesUseTheirDomain) {
    // Per channel over [0, 2]: red doubles, green is a curve, blue is inverted.
    const TempFile file("LUT_1D_SIZE 3\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n"
                        "0 0 1\n2 0.25 0.5\n4 1 0\n",
                        ".cube");
    const auto out = run(lut_tool(file), {0.5f, 1.5f, 2.0f, 1.0f});
    EXPECT_NEAR(out[0], 1.0f, 1e-6f);
    EXPECT_NEAR(out[1], 0.625f, 1e-6f);
    EXPECT_NEAR(out[2], 0.0f, 1e-6f);
}

TEST(Lut, FindsRelativePathsAndReportsMissingFiles) {
    const TempFile file("LUT_1D_SIZE 2\n0 0 0\n2 2 2\n", ".cube");
    Look look;
    look.chain = {tool("lut", {{"file", file.name()}})};
    dyeline::CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error, file.folder())) << error;
    EXPECT_EQ(chain.pass.count, 1);
    EXPECT_TRUE(chain.problems.empty());
    EXPECT_EQ(chain.table.size(), 6u);

    look.chain = {tool("lut", {{"file", file.path() + ".missing"}})};
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    EXPECT_EQ(chain.pass.count, 0);
    ASSERT_EQ(chain.problems.size(), 1u);
    EXPECT_NE(chain.problems[0].find("LUT: could not open"), std::string::npos);

    // No file chosen yet: nothing to run, nothing to report.
    EXPECT_TRUE(skipped(tool("lut", nlohmann::json::object())));
}

TEST(Lut, TwoLutsShareTheTableData) {
    const TempFile first("LUT_1D_SIZE 2\n0 0 0\n2 2 2\n", ".cube");
    const TempFile second("LUT_1D_SIZE 2\n0 0 0\n0.5 0.5 0.5\n", ".cube");
    Look look;
    look.chain = {lut_tool(first), lut_tool(second)};
    dyeline::CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    ASSERT_EQ(chain.pass.count, 2);
    EXPECT_EQ(chain.pass.ops[1].p[0], 6.0f);  // the second table starts after the first
    std::vector<float> out(4);
    const std::vector<float> px = {0.3f, 0.3f, 0.3f, 1.0f};
    dyeline::render_chain_row(px.data(), out.data(), 1, 4, false, chain);
    EXPECT_NEAR(out[0], 0.3f, 1e-6f);  // doubled, then halved
}

}  // namespace
