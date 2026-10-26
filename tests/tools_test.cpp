// Pins the math of each per-pixel tool, run through the fused pass on the CPU.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/film.h"
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
                           "channel-mixer", "shadows-highlights", "highlight-shoulder", "three-way", "lut",
                           "lens-vignette", "grain", "diffusion", "halation", "film-negative", "film-print", "bleach-bypass", "two-strip",
                           "telecine-net", "gradient-tint", "grad-exposure", "spot-exposure",
                           "fill-light", "spot-fill", "four-way", "blockbuster", "color-filter",
                           "duotone", "local-contrast", "lens-distortion", "chromatic-aberration",
                           "edge-softness", "swing-tilt"}) {
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

// Renders one tool over a whole frame of mid grey, `width` x `height` pixels, one canonical
// pixel each, and returns the red channel.
std::vector<float> run_frame(const ToolEntry& entry, int width, int height, int seed = 0,
                             float grey = 0.18f) {
    Look look;
    look.chain = {entry};
    dyeline::CompiledChain chain;
    std::string error;
    EXPECT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    std::vector<float> frame(static_cast<std::size_t>(width) * height * 3, grey);
    dl_image_args args{};
    args.width = width;
    args.height = height;
    args.channels = 3;
    args.src_row_floats = width * 3;
    args.dst_row_floats = width * 3;
    args.src_width = width;
    args.src_height = height;
    args.canonical_x = 1.0f;
    args.canonical_y = 1.0f;
    args.frame_right = static_cast<float>(width);
    args.frame_top = static_cast<float>(height);
    args.seed = seed;
    dyeline::render_chain_window(frame.data(), frame.data(), args, chain);
    std::vector<float> red;
    for (std::size_t i = 0; i < frame.size(); i += 3) {
        red.push_back(frame[i]);
    }
    return red;
}

TEST(LensVignette, DarkensTowardTheCornersOnly) {
    const auto out = run_frame(tool("lens-vignette", {{"amount", -2.0}, {"size", 0.4},
                                                       {"softness", 0.6}}),
                               40, 20);
    const auto at = [&](int x, int y) { return out[static_cast<std::size_t>(y) * 40 + x]; };
    EXPECT_NEAR(at(20, 10), 0.18f, 1e-6f);        // the clear center
    EXPECT_NEAR(at(0, 0), 0.18f / 4.0f, 0.002f);  // a corner, close to the full -2 stops
    EXPECT_LT(at(0, 10), at(10, 10));             // falling off toward the edge
    EXPECT_NEAR(at(5, 3), at(34, 16), 1e-6f);     // symmetric about the center
    EXPECT_TRUE(skipped(tool("lens-vignette", {{"amount", 0.0}})));
}

TEST(LensVignette, RoundnessAndCenterShapeTheFalloff) {
    // A 2:1 frame: with Roundness 1 the falloff is a circle, so a point at the top edge is
    // closer than one equally far out in x; with Roundness 0 it follows the frame.
    const auto round = run_frame(tool("lens-vignette", {{"amount", -2.0}, {"size", 0.2},
                                                         {"softness", 1.0}}),
                                 40, 20);
    const auto ellipse = run_frame(tool("lens-vignette", {{"amount", -2.0}, {"size", 0.2},
                                                           {"softness", 1.0}, {"roundness", 0.0}}),
                                   40, 20);
    // Top middle and left middle edges.
    EXPECT_GT(round[19 * 40 + 20], round[10 * 40 + 0]);
    EXPECT_NEAR(ellipse[19 * 40 + 20], ellipse[10 * 40 + 0], 0.004f);
    // Moving the center right brightens the right side.
    const auto moved = run_frame(tool("lens-vignette", {{"amount", -2.0}, {"size", 0.2},
                                                         {"center-x", 0.5}}),
                                 40, 20);
    EXPECT_GT(moved[10 * 40 + 35], round[10 * 40 + 35]);
}

TEST(Grain, AddsZeroMeanNoiseThatFollowsTheSeed) {
    const ToolEntry grain = tool("grain", {{"amount", 0.5}, {"size", 1.0}});
    const auto a = run_frame(grain, 64, 64, 1);
    double log_sum = 0.0;
    float lo = 1.0f, hi = 0.0f;
    for (float v : a) {
        log_sum += std::log2(v / 0.18f);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    EXPECT_NEAR(log_sum / static_cast<double>(a.size()), 0.0, 0.02);  // no overall shift
    EXPECT_LT(lo, 0.18f * 0.95f);
    EXPECT_GT(hi, 0.18f * 1.05f);
    EXPECT_GE(lo, 0.18f * std::exp2(-0.5f));  // noise stays within 2 x Amount x half a stop
    EXPECT_LE(hi, 0.18f * std::exp2(0.5f));
    // Repeatable for a frame, different on the next, and fixed when not animated.
    EXPECT_EQ(run_frame(grain, 64, 64, 1), a);
    EXPECT_NE(run_frame(grain, 64, 64, 2), a);
    const ToolEntry still = tool("grain", {{"amount", 0.5}, {"animate", 0.0}});
    EXPECT_EQ(run_frame(still, 16, 16, 1), run_frame(still, 16, 16, 2));
    EXPECT_NE(run_frame(tool("grain", {{"amount", 0.5}, {"seed", 3}}), 16, 16, 1),
              run_frame(tool("grain", {{"amount", 0.5}, {"seed", 4}}), 16, 16, 1));
    EXPECT_TRUE(skipped(tool("grain", {{"amount", 0.0}})));
}

TEST(Grain, IsGreyWhenColorIsZeroAndSofterWhenLarger) {
    const ToolEntry mono = tool("grain", {{"amount", 1.0}, {"color", 0.0}});
    const auto px = run(mono, {0.3f, 0.2f, 0.1f, 1.0f});
    EXPECT_NEAR(px[0] / px[1], 1.5f, 1e-5f);  // every channel moved by the same factor
    EXPECT_NEAR(px[1] / px[2], 2.0f, 1e-5f);
    // Larger grain changes less from one pixel to the next.
    const auto step = [](const std::vector<float>& v) {  // along rows of 64
        double sum = 0.0;
        for (std::size_t i = 1; i < v.size(); ++i) {
            if (i % 64 != 0) {
                sum += std::fabs(std::log2(v[i] / v[i - 1]));
            }
        }
        return sum;
    };
    EXPECT_LT(step(run_frame(tool("grain", {{"size", 6.0}}), 64, 8)),
              step(run_frame(tool("grain", {{"size", 1.0}}), 64, 8)) * 0.5);
}

// Film Negative and Film Print, through every stock.

std::vector<float> grey(float v) {
    return {v, v, v, 1.0f};
}

TEST(Film, StockChoicesNameEveryStock) {
    const dyeline::ToolSpec* negative = dyeline::find_tool("film-negative");
    const dyeline::ToolSpec* print = dyeline::find_tool("film-print");
    ASSERT_NE(negative, nullptr);
    ASSERT_NE(print, nullptr);
    EXPECT_EQ(negative->params[0].kind, dyeline::ParamKind::Choice);
    EXPECT_EQ(negative->params[0].choices.size(),
              static_cast<std::size_t>(dyeline::film::negative_stock_count()));
    EXPECT_EQ(print->params[0].choices.size(),
              static_cast<std::size_t>(dyeline::film::print_stock_count()));
    EXPECT_EQ(negative->params[0].max, dyeline::film::negative_stock_count() - 1.0);
    EXPECT_STREQ(dyeline::film::negative_stock(0).id, "kodak-5219");
    EXPECT_STREQ(dyeline::film::print_stock(0).id, "kodak-2383");
    // An index past the list is held at its end, as the look's value is clamped.
    EXPECT_EQ(&dyeline::film::print_stock(99), &dyeline::film::print_stock(3));
}

TEST(FilmNegative, KeepsGreyAndRollsOffBothEnds) {
    for (int stock = 0; stock < dyeline::film::negative_stock_count(); ++stock) {
        const ToolEntry negative = tool("film-negative", {{"stock", stock}});
        const auto mid = run(negative, grey(0.18f));
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(mid[k], 0.18f, 0.002f) << stock;
        }
        // Brighter in, brighter out, with less than a stop out per stop in at the ends.
        float previous = 0.0f;
        for (float stops = -10.0f; stops <= 10.0f; stops += 0.5f) {
            const float out = luma(run(negative, grey(0.18f * std::exp2(stops))));
            EXPECT_GE(out, previous * (1.0f - 1e-5f)) << stock << " at " << stops;  // flat ends round
            previous = out;
        }
        const float black = luma(run(negative, grey(0.0f)));
        EXPECT_GT(black, 0.18f / 1024.0f) << stock;  // the toe holds shadows above black
        const float over = luma(run(negative, grey(0.18f * 1024.0f)));
        EXPECT_LT(over, 0.18f * 1024.0f / 4.0f) << stock;  // and the shoulder compresses
    }
}

TEST(FilmNegative, ExposureIsStopsOfLightIn) {
    const auto brighter = run(tool("film-negative", {{"stock", 2}, {"exposure", 1.0}}),
                              {0.1f, 0.05f, 0.02f, 1.0f});
    const auto doubled = run(tool("film-negative", {{"stock", 2}}), {0.2f, 0.1f, 0.04f, 1.0f});
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(brighter[k], doubled[k], 1e-4f);
    }
}

TEST(FilmPrint, KeepsGreyAndEndsAtTheClearBase) {
    for (int stock = 0; stock < dyeline::film::print_stock_count(); ++stock) {
        const ToolEntry print = tool("film-print", {{"stock", stock}});
        const auto mid = run(print, grey(0.18f));
        const auto white = run(print, grey(1000.0f));
        for (int k = 0; k < 3; ++k) {
            EXPECT_NEAR(mid[k], 0.18f, 0.003f) << stock;
            EXPECT_NEAR(white[k], 1.0f, 0.01f) << stock;
        }
        float previous = 0.0f;
        for (float stops = -10.0f; stops <= 10.0f; stops += 0.5f) {
            const float out = luma(run(print, grey(0.18f * std::exp2(stops))));
            EXPECT_GE(out, previous * (1.0f - 1e-5f)) << stock << " at " << stops;  // flat ends round
            previous = out;
        }
        // A print has more mid-tone contrast than the image it prints.
        const float above = luma(run(print, grey(0.36f)));
        EXPECT_GT(std::log2(above / 0.18f), 1.1f) << stock;
    }
}

TEST(FilmPrint, PrinterLightsExposureAndContrast) {
    const auto base = run(tool("film-print", {}), grey(0.18f));
    // A red point adds red printer light, which makes the print more cyan.
    const auto red = run(tool("film-print", {{"red", 6.0}}), grey(0.18f));
    // The cyan dye takes a little green too, but far less than red.
    EXPECT_LT(red[0] / base[0], 0.75f);
    EXPECT_GT(red[1] / base[1], red[0] / base[0] + 0.1f);
    const auto brighter = run(tool("film-print", {{"exposure", 1.0}}), grey(0.18f));
    EXPECT_GT(luma(brighter), luma(base) * 1.5f);
    // Contrast scales the print's stops around grey.
    const auto soft = run(tool("film-print", {}), grey(0.05f));
    const auto hard = run(tool("film-print", {{"contrast", 2.0}}), grey(0.05f));
    EXPECT_NEAR(std::log2(hard[1] / 0.18f), 2.0f * std::log2(soft[1] / 0.18f), 1e-3f);
}

TEST(Film, NegativeThenPrintKeepsGreyNeutral) {
    Look look;
    look.chain = {tool("film-negative", {}), tool("film-print", {})};
    dyeline::CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    std::vector<float> out(4);
    const std::vector<float> in = grey(0.18f);
    dyeline::render_chain_row(in.data(), out.data(), 1, 4, false, chain);
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(out[k], 0.18f, 0.005f);
    }
}

TEST(BleachBypass, KeepsGreyAddsContrastAndTakesColor) {
    const ToolEntry bypass = tool("bleach-bypass", {{"amount", 1.0}});
    const auto mid = run(bypass, grey(0.18f));
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(mid[k], 0.18f, 1e-5f);
    }
    // Luminance contrast 1.5: a stop over grey comes out a stop and a half over.
    EXPECT_NEAR(std::log2(luma(run(bypass, grey(0.36f))) / 0.18f), 1.5f, 1e-4f);
    EXPECT_NEAR(std::log2(luma(run(bypass, grey(0.09f))) / 0.18f), -1.5f, 1e-4f);
    // A color keeps 35% of its difference from its luminance (scaled by the silver).
    const std::vector<float> red = {0.3f, 0.12f, 0.1f, 1.0f};
    const auto out = run(bypass, red);
    const float factor = luma(out) / luma(red);
    EXPECT_NEAR((out[0] - out[1]) / factor, 0.35f * (red[0] - red[1]), 1e-4f);
    EXPECT_TRUE(skipped(tool("bleach-bypass", {{"amount", 0.0}})));
}

TEST(TwoStrip, KeepsRedAndAveragesGreenAndBlue) {
    const auto out = run(tool("two-strip", {}), {0.4f, 0.2f, 0.6f, 1.0f});
    EXPECT_NEAR(out[0], 0.4f, 1e-6f);
    EXPECT_NEAR(out[1], 0.4f, 1e-6f);
    EXPECT_NEAR(out[2], 0.4f, 1e-6f);
    const auto half = run(tool("two-strip", {{"amount", 0.5}}), {0.4f, 0.2f, 0.6f, 1.0f});
    EXPECT_NEAR(half[1], 0.3f, 1e-6f);
    EXPECT_NEAR(half[2], 0.5f, 1e-6f);
    EXPECT_TRUE(skipped(tool("two-strip", {{"amount", 0.0}})));
}

// The selective tools, over a 40 x 20 frame of grey: the center is (20, 10), and the half
// diagonal is sqrt(20^2 + 10^2), about 22.4 pixels.

std::vector<float> frame_at(const ToolEntry& entry) {
    return run_frame(entry, 40, 20);
}

float pixel(const std::vector<float>& red, int x, int y) {
    return red[static_cast<std::size_t>(y) * 40 + x];
}

TEST(GradExposure, ChangesOneSideOfTheLine) {
    // Angle 0: the change lies up, across a soft band through the middle.
    const auto up = frame_at(tool("grad-exposure", {{"stops", -1.0}, {"softness", 0.2}}));
    EXPECT_NEAR(pixel(up, 20, 19), 0.09f, 1e-4f);   // the top row (y is up)
    EXPECT_NEAR(pixel(up, 20, 0), 0.18f, 1e-4f);    // the bottom row
    EXPECT_NEAR(pixel(up, 3, 19), pixel(up, 36, 19), 1e-6f);  // even along the line
    // Angle 90: the change lies right; a center moved right moves the line.
    const auto right = frame_at(tool("grad-exposure", {{"stops", 1.0}, {"angle", 90.0},
                                                       {"center-x", 0.5}, {"softness", 0.1}}));
    EXPECT_NEAR(pixel(right, 39, 10), 0.36f, 1e-4f);
    EXPECT_NEAR(pixel(right, 25, 10), 0.18f, 1e-4f);  // left of x = 30
    EXPECT_TRUE(skipped(tool("grad-exposure", {{"stops", 0.0}})));
}

TEST(GradientTint, TintsWithoutChangingLuminance) {
    Look look;
    look.chain = {tool("gradient-tint", {{"hue", 210.0}, {"amount", 1.0}, {"softness", 0.1}})};
    dyeline::CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    // One pixel at the top of a 2 x 2 frame, well inside the tinted side.
    std::vector<float> frame(12, 0.18f);
    dl_image_args args{};
    args.width = 2;
    args.height = 2;
    args.channels = 3;
    args.src_row_floats = 6;
    args.dst_row_floats = 6;
    args.src_width = 2;
    args.src_height = 2;
    args.canonical_x = 1.0f;
    args.canonical_y = 1.0f;
    args.frame_right = 2.0f;
    args.frame_top = 2.0f;
    dyeline::render_chain_window(frame.data(), frame.data(), args, chain);
    const std::vector<float> top(frame.begin() + 6, frame.begin() + 9);
    const std::vector<float> bottom(frame.begin(), frame.begin() + 3);
    EXPECT_GT(top[2], top[0]);  // blue-ish
    EXPECT_NEAR(std::log2(luma({top[0], top[1], top[2], 1.0f}) / 0.18f), 0.0f, 0.15f);
    EXPECT_NEAR(bottom[0], 0.18f, 1e-4f);
    EXPECT_NEAR(bottom[2], 0.18f, 1e-4f);
    EXPECT_TRUE(skipped(tool("gradient-tint", {{"amount", 0.0}})));
}

TEST(SpotExposure, BrightensInsideTheSpotOnly) {
    const auto out = frame_at(tool("spot-exposure", {{"stops", 1.0}, {"size", 0.2},
                                                     {"softness", 0.1}}));
    EXPECT_NEAR(pixel(out, 20, 10), 0.36f, 1e-4f);
    EXPECT_NEAR(pixel(out, 0, 0), 0.18f, 1e-4f);
    EXPECT_GT(pixel(out, 24, 10), pixel(out, 28, 10));  // fading out
    const auto moved = frame_at(tool("spot-exposure", {{"stops", 1.0}, {"size", 0.2},
                                                       {"softness", 0.1}, {"center-x", -0.5},
                                                       {"center-y", 0.5}}));
    EXPECT_NEAR(pixel(moved, 10, 15), 0.36f, 1e-3f);
    EXPECT_NEAR(pixel(moved, 20, 10), 0.18f, 1e-4f);
}

TEST(FillLight, LiftsShadowsAndLeavesHighlights) {
    const ToolEntry fill = tool("fill-light", {{"amount", 1.0}});
    EXPECT_NEAR(luma(run(fill, grey(0.18f / 16.0f))), 0.18f / 8.0f, 1e-4f);  // a full stop
    EXPECT_NEAR(luma(run(fill, grey(0.72f))), 0.72f, 1e-6f);  // two stops over: untouched
    float previous = 0.0f;
    for (float stops = -10.0f; stops <= 4.0f; stops += 0.25f) {
        const float out = luma(run(tool("fill-light", {{"amount", 2.0}}),
                                   grey(0.18f * std::exp2(stops))));
        EXPECT_GT(out, previous) << stops;  // never reverses, even at full amount
        previous = out;
    }
    // A tint leans the fill's color.
    const auto warm = run(tool("fill-light", {{"amount", 1.0}, {"tint-hue", 30.0},
                                              {"tint-amount", 0.5}}),
                          grey(0.02f));
    EXPECT_GT(warm[0], warm[2]);
    EXPECT_TRUE(skipped(tool("fill-light", {{"amount", 0.0}})));
}

TEST(SpotFill, LiftsShadowsInsideTheSpot) {
    const auto out = run_frame(tool("spot-fill", {{"amount", 1.0}, {"size", 0.2},
                                                  {"softness", 0.1}}),
                               40, 20, 0, 0.01f);
    EXPECT_NEAR(pixel(out, 20, 10), 0.02f, 1e-4f);
    EXPECT_NEAR(pixel(out, 0, 0), 0.01f, 1e-6f);
}

// Chroma across grey: the distance from the channels' mean.
float chroma(const std::vector<float>& c) {
    const float m = (c[0] + c[1] + c[2]) / 3.0f;
    return std::sqrt((c[0] - m) * (c[0] - m) + (c[1] - m) * (c[1] - m) + (c[2] - m) * (c[2] - m));
}

TEST(ThreeWayEq, HslBandsTurnScaleAndLightenTheirHue) {
    const std::vector<float> red = {0.4f, 0.1f, 0.1f, 1.0f};
    const std::vector<float> blue = {0.1f, 0.1f, 0.4f, 1.0f};
    const ToolEntry grey_reds = tool("three-way", {{"red-saturation", 0.0}});
    EXPECT_FALSE(skipped(grey_reds));
    const auto out = run(grey_reds, red);
    EXPECT_NEAR(out[0], 0.2f, 1e-5f);  // grey, at the channels' mean
    EXPECT_NEAR(out[1], 0.2f, 1e-5f);
    EXPECT_NEAR(out[2], 0.2f, 1e-5f);
    const auto untouched = run(grey_reds, blue);
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(untouched[k], blue[k], 1e-6f);
    }
    // Sixty degrees turns red into yellow, keeping its saturation.
    const auto yellow = run(tool("three-way", {{"red-hue", 60.0}}), red);
    EXPECT_NEAR(yellow[0], yellow[1], 1e-5f);
    EXPECT_GT(yellow[0], yellow[2] + 0.2f);
    EXPECT_NEAR(chroma(yellow), chroma(red), 1e-5f);
    // Lightness adds stops to colors, not greys.
    const auto light = run(tool("three-way", {{"red-lightness", 1.0}}), red);
    EXPECT_NEAR(light[0], 0.8f, 1e-5f);
    const auto grey_out = run(tool("three-way", {{"red-lightness", 1.0}}), grey(0.18f));
    EXPECT_NEAR(grey_out[0], 0.18f, 1e-6f);
    // Halfway between two bands, each gives half.
    const auto orange = run(tool("three-way", {{"red-saturation", 0.0}}), {0.4f, 0.25f, 0.1f, 1.0f});
    EXPECT_NEAR(chroma(orange), 0.5f * chroma({0.4f, 0.25f, 0.1f, 1.0f}), 1e-5f);
}

TEST(ThreeWayEq, SatEqScalesSaturationByLuminance) {
    const ToolEntry eq = tool("three-way", {{"sat-deep-shadows", 0.0}, {"sat-highlights", 2.0}});
    std::vector<float> red = {0.4f, 0.1f, 0.1f, 1.0f};
    const float to_pivot = 0.18f / luma(red);
    for (int k = 0; k < 3; ++k) {
        red[k] *= to_pivot;
    }
    const auto mid = run(eq, red);
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(mid[k], red[k], 1e-5f);
    }
    std::vector<float> dark = red;
    for (int k = 0; k < 3; ++k) {
        dark[k] /= 64.0f;  // six stops under
    }
    EXPECT_NEAR(chroma(run(eq, dark)), 0.0f, 1e-7f);
    std::vector<float> bright = red;
    for (int k = 0; k < 3; ++k) {
        bright[k] *= 4.0f;  // two stops over
    }
    EXPECT_NEAR(chroma(run(eq, bright)), 2.0f * chroma(bright), 1e-4f);
}

TEST(FourWay, OffsetReachesEveryToneAndEachWheelHasExposure) {
    const ToolEntry offset = tool("four-way", {{"offset-exposure", 1.0}});
    for (float stops : {-8.0f, 0.0f, 8.0f}) {
        const float in = 0.18f * std::exp2(stops);
        EXPECT_NEAR(run(offset, grey(in))[1], 2.0f * in, 1e-4f * in) << stops;
    }
    const ToolEntry shadows = tool("four-way", {{"shadows-exposure", -1.0}});
    EXPECT_NEAR(run(shadows, grey(0.18f / 64.0f))[0], 0.09f / 64.0f, 1e-7f);
    EXPECT_NEAR(run(shadows, grey(0.18f * 64.0f))[0], 0.18f * 64.0f, 1e-4f);
    // An offset tint leans every tone the same way.
    const ToolEntry warm = tool("four-way", {{"offset-hue", 30.0}, {"offset-amount", 0.5}});
    for (float in : {0.002f, 0.18f, 10.0f}) {
        const auto out = run(warm, grey(in));
        EXPECT_GT(out[0], out[2]) << in;
        EXPECT_NEAR(std::log2(luma(out) / in), 0.0f, 0.15f) << in;
    }
    EXPECT_TRUE(skipped(tool("four-way", {{"offset-hue", 90.0}, {"pivot", 0.5}})));
}

TEST(Blockbuster, KeepsLuminanceAndPullsColorsToTheAxis) {
    const ToolEntry look = tool("blockbuster", {{"amount", 0.6}});
    const auto g = run(look, grey(0.18f));
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(g[k], 0.18f, 1e-6f);
    }
    const std::vector<float> orange = {0.3f, 0.18f, 0.08f, 1.0f};
    const std::vector<float> green = {0.1f, 0.25f, 0.1f, 1.0f};
    const auto warmer = run(look, orange);
    const auto greyer = run(look, green);
    EXPECT_NEAR(luma(warmer), luma(orange), 1e-6f);
    EXPECT_NEAR(luma(greyer), luma(green), 1e-6f);
    EXPECT_GT(warmer[0] - warmer[2], orange[0] - orange[2]);  // spread along the axis
    EXPECT_LT(chroma(greyer), 0.6f * chroma(green));          // off it, pulled in
    EXPECT_TRUE(skipped(tool("blockbuster", {{"amount", 0.0}})));
}

TEST(ColorFilter, PassesItsHueAndCanKeepGreysBrightness) {
    const auto kept = run(tool("color-filter", {{"hue", 30.0}, {"density", 0.5}}), grey(0.18f));
    EXPECT_NEAR(luma(kept), 0.18f, 1e-6f);
    EXPECT_GT(kept[0], kept[2]);
    const auto dark = run(tool("color-filter", {{"hue", 30.0}, {"density", 0.5}, {"preserve", 0.0}}),
                          grey(0.18f));
    EXPECT_LT(luma(dark), 0.18f);
    EXPECT_NEAR(dark[0] / dark[2], kept[0] / kept[2], 1e-5f);
    EXPECT_TRUE(skipped(tool("color-filter", {{"density", 0.0}})));
}

TEST(Duotone, KeepsLuminanceAndTintsShadowsAndHighlights) {
    const ToolEntry duo = tool("duotone", {{"shadows-hue", 220.0}, {"highlights-hue", 40.0}});
    const std::vector<float> colors[] = {{0.002f, 0.003f, 0.001f, 1.0f},
                                         {0.4f, 0.1f, 0.1f, 1.0f},
                                         {5.0f, 9.0f, 12.0f, 1.0f}};
    for (const auto& c : colors) {
        EXPECT_NEAR(luma(run(duo, c)) / luma(c), 1.0f, 1e-5f);
    }
    const auto shadow = run(duo, grey(0.18f / 64.0f));
    const auto highlight = run(duo, grey(0.18f * 64.0f));
    EXPECT_GT(shadow[2], shadow[0]);  // cool
    EXPECT_GT(highlight[0], highlight[2]);  // warm
    // Two colors of the same luminance come out the same.
    const auto a = run(duo, {0.4f, 0.1f, 0.1f, 1.0f});
    const float y = luma({0.4f, 0.1f, 0.1f, 1.0f});
    const auto b = run(duo, grey(y));
    for (int k = 0; k < 3; ++k) {
        EXPECT_NEAR(a[k], b[k], 1e-6f);
    }
    EXPECT_FALSE(skipped(tool("duotone", {})));
}

}  // namespace
