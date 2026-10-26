// The render plan: passes for spatial and warp tools, the blur's size, Diffusion, Halation,
// Lens Distortion, Chromatic Aberration, Edge Softness and Swing-Tilt, and renders split into tiles or given only the
// source the plugin asks for matching one render of the whole frame.

#include "dyeline/render.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dyeline/chain.h"

namespace {

using dyeline::CompiledChain;
using dyeline::Look;
using dyeline::PixelRect;
using dyeline::RenderGeometry;
using dyeline::ToolEntry;

ToolEntry tool(const char* id, nlohmann::json params, double strength = 1.0) {
    ToolEntry entry;
    entry.tool = id;
    entry.params = std::move(params);
    entry.strength = strength;
    return entry;
}

CompiledChain compile(std::vector<ToolEntry> tools) {
    Look look;
    look.chain = std::move(tools);
    CompiledChain chain;
    std::string error;
    EXPECT_TRUE(dyeline::compile_chain(look, 1.0, chain, error)) << error;
    return chain;
}

// An RGB frame of `width` x `height`, one canonical pixel per pixel, rendered whole.
RenderGeometry frame_geometry(int width, int height) {
    RenderGeometry g;
    g.window = {0, 0, width, height};
    g.src_bounds = g.window;
    g.dst_bounds = g.window;
    g.src_row_floats = width * 3;
    g.dst_row_floats = width * 3;
    g.channels = 3;
    g.frame_right = static_cast<float>(width);
    g.frame_top = static_cast<float>(height);
    g.seed = 3;
    return g;
}

std::vector<float> render(const CompiledChain& chain, const RenderGeometry& g,
                          const std::vector<float>& src) {
    std::vector<float> dst(static_cast<std::size_t>(g.dst_bounds.width()) * g.dst_bounds.height() *
                               3,
                           -1.0f);
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
    dyeline::run_plan_cpu(src.data(), dst.data(), chain.table.data(), plan);
    return dst;
}

// A frame with structure everywhere: gradients, a few bright highlights and dark areas.
std::vector<float> test_frame(int width, int height) {
    std::vector<float> f(static_cast<std::size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float* p = &f[(static_cast<std::size_t>(y) * width + x) * 3];
            const bool bright = (x * 7 + y * 3) % 23 == 0;
            p[0] = bright ? 12.0f : 0.02f + 0.01f * static_cast<float>(x);
            p[1] = bright ? 8.0f : 0.05f + 0.008f * static_cast<float>(y);
            p[2] = bright ? 4.0f : 0.3f * std::fabs(std::sin(0.3f * static_cast<float>(x + y)));
        }
    }
    return f;
}

float at(const std::vector<float>& f, int width, int x, int y, int k) {
    return f[(static_cast<std::size_t>(y) * width + x) * 3 + k];
}

TEST(RenderPlan, OnePassWithoutSpatialTools) {
    const CompiledChain chain = compile({tool("exposure", {{"stops", 1.0}})});
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, frame_geometry(8, 8));
    ASSERT_EQ(plan.passes.size(), 1u);
    EXPECT_EQ(plan.passes[0].kind, dyeline::PassKind::Final);
    EXPECT_EQ(plan.work_floats, 0u);
    const PixelRect source = dyeline::source_region(chain.pass, frame_geometry(8, 8));
    EXPECT_EQ(source.x1, 0);
    EXPECT_EQ(source.y1, 0);
    EXPECT_EQ(source.x2, 8);
    EXPECT_EQ(source.y2, 8);
}

TEST(RenderPlan, EachSpatialToolAddsAnExtractAndSixBlurs) {
    const CompiledChain chain = compile({tool("diffusion", {{"size", 5.0}}),
                                         tool("exposure", {{"stops", 1.0}}),
                                         tool("halation", {{"size", 2.0}})});
    const RenderGeometry g = frame_geometry(200, 100);
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
    ASSERT_EQ(plan.passes.size(), 15u);
    EXPECT_EQ(plan.passes[0].kind, dyeline::PassKind::Extract);
    EXPECT_EQ(plan.passes[0].stop, 0);
    EXPECT_EQ(plan.passes[7].kind, dyeline::PassKind::Extract);
    EXPECT_EQ(plan.passes[7].stop, 2);
    EXPECT_EQ(plan.passes.back().kind, dyeline::PassKind::Final);
    // Sigma 10 pixels gives box radius 10 and sigma 4 gives 4; three passes each.
    const PixelRect source = dyeline::source_region(chain.pass, g);
    const int margin = 3 * 10 + 3 * 4;
    EXPECT_EQ(source.x1, -margin);
    EXPECT_EQ(source.y1, -margin);
    EXPECT_EQ(source.x2, 200 + margin);
    EXPECT_EQ(source.y2, 100 + margin);
    // Diffusion's extract covers the window plus both blurs' reach.
    EXPECT_EQ(plan.passes[0].image.width, 200 + 2 * margin);
}

TEST(Blur, SpreadsLikeAGaussianOfTheSizeAndKeepsTheLight) {
    // Diffusion at full amount with Highlights 0 replaces the image with its blur.
    const int n = 101;
    const double sigma = 6.0;
    const CompiledChain chain = compile({tool(
        "diffusion", {{"amount", 1.0}, {"highlights", 0.0}, {"size", 100.0 * sigma / n}})});
    std::vector<float> src(static_cast<std::size_t>(n) * n * 3, 0.0f);
    src[(static_cast<std::size_t>(50) * n + 50) * 3] = 1000.0f;
    const std::vector<float> out = render(chain, frame_geometry(n, n), src);
    double total = 0.0;
    double spread = 0.0;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double v = at(out, n, x, y, 0);
            EXPECT_GE(v, -1e-3) << x << ", " << y;
            total += v;
            spread += v * (x - 50) * (x - 50);
        }
    }
    EXPECT_NEAR(total, 1000.0, 0.1);
    // Three box blurs of radius 6 have a variance of 42 against the Gaussian's 36.
    EXPECT_NEAR(std::sqrt(spread / total), sigma, 0.6);
    EXPECT_NEAR(at(out, n, 50, 50, 0), at(out, n, 50, 50, 0), 0.0);
    EXPECT_NEAR(at(out, n, 44, 50, 0), at(out, n, 56, 50, 0), 1e-3);  // symmetric
    EXPECT_NEAR(at(out, n, 50, 44, 0), at(out, n, 44, 50, 0), 1e-3);
}

TEST(Diffusion, LeavesAnEvenImageAloneAndSoftensEdges) {
    const CompiledChain chain = compile({tool("diffusion", {{"amount", 0.8}, {"size", 4.0}})});
    const int w = 40;
    const int h = 20;
    std::vector<float> even(static_cast<std::size_t>(w) * h * 3, 0.4f);
    for (float v : render(chain, frame_geometry(w, h), even)) {
        EXPECT_NEAR(v, 0.4f, 1e-5f);
    }
    // A hard edge between dark and bright: the dark side picks up light near the edge.
    std::vector<float> edge(even.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int k = 0; k < 3; ++k) {
                edge[(static_cast<std::size_t>(y) * w + x) * 3 + k] = x < w / 2 ? 0.02f : 4.0f;
            }
        }
    }
    const std::vector<float> out = render(chain, frame_geometry(w, h), edge);
    EXPECT_GT(at(out, w, w / 2 - 1, 10, 1), 0.1f);
    EXPECT_LT(at(out, w, w / 2, 10, 1), 4.0f);
    EXPECT_NEAR(at(out, w, 0, 10, 1), 0.02f, 0.01f);  // far from the edge, nearly untouched
}

TEST(TelecineNet, LiftsShadowsBesideHighlightsAndKeepsTheTotal) {
    const CompiledChain chain = compile({tool("telecine-net", {{"amount", 0.5}, {"size", 8.0}})});
    const int w = 40;
    const int h = 20;
    std::vector<float> edge(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int k = 0; k < 3; ++k) {
                edge[(static_cast<std::size_t>(y) * w + x) * 3 + k] = x < w / 2 ? 0.01f : 2.0f;
            }
        }
    }
    const std::vector<float> out = render(chain, frame_geometry(w, h), edge);
    // Unlike Diffusion's default, shadows scatter as much as highlights, so the dark side
    // lifts well beyond its own level and the bright side loses light.
    EXPECT_GT(at(out, w, w / 2 - 2, 10, 1), 0.2f);
    EXPECT_LT(at(out, w, w / 2 + 1, 10, 1), 2.0f);
    EXPECT_TRUE(compile({tool("telecine-net", {{"amount", 0.0}})}).pass.count == 0);
}

TEST(Halation, GlowsWarmAroundHighlightsOnly) {
    const CompiledChain chain =
        compile({tool("halation", {{"amount", 1.0}, {"threshold", 1.0}, {"size", 5.0}})});
    const int n = 41;
    std::vector<float> src(static_cast<std::size_t>(n) * n * 3, 0.5f);
    // Below the threshold everywhere: nothing changes.
    for (float v : render(chain, frame_geometry(n, n), src)) {
        EXPECT_FLOAT_EQ(v, 0.5f);
    }
    for (int k = 0; k < 3; ++k) {
        src[(static_cast<std::size_t>(20) * n + 20) * 3 + k] = 200.0f;
    }
    const std::vector<float> out = render(chain, frame_geometry(n, n), src);
    const float red = at(out, n, 23, 20, 0) - 0.5f;
    const float green = at(out, n, 23, 20, 1) - 0.5f;
    const float blue = at(out, n, 23, 20, 2) - 0.5f;
    EXPECT_GT(red, 0.05f);
    EXPECT_GT(red, green * 3.0f);
    EXPECT_GT(green, blue);
    EXPECT_LT(at(out, n, 0, 0, 0) - 0.5f, red * 0.1f);  // fading with distance
}

TEST(LocalContrast, LeavesAnEvenImageAloneAndSharpensEdges) {
    const CompiledChain chain = compile({tool("local-contrast", {{"amount", 0.5}, {"size", 4.0}})});
    const int w = 40;
    const int h = 20;
    std::vector<float> even(static_cast<std::size_t>(w) * h * 3, 0.4f);
    for (float v : render(chain, frame_geometry(w, h), even)) {
        EXPECT_NEAR(v, 0.4f, 1e-5f);
    }
    std::vector<float> edge(even.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int k = 0; k < 3; ++k) {
                edge[(static_cast<std::size_t>(y) * w + x) * 3 + k] = x < w / 2 ? 0.1f : 0.4f;
            }
        }
    }
    const std::vector<float> out = render(chain, frame_geometry(w, h), edge);
    // Beside the edge, the dark side darkens and the bright side brightens; far from it the
    // image is nearly untouched.
    EXPECT_LT(at(out, w, w / 2 - 1, 10, 1), 0.09f);
    EXPECT_GT(at(out, w, w / 2, 10, 1), 0.44f);
    EXPECT_NEAR(at(out, w, 0, 10, 1), 0.1f, 0.005f);
    // Less than zero softens instead.
    const std::vector<float> soft =
        render(compile({tool("local-contrast", {{"amount", -0.5}, {"size", 4.0}})}),
               frame_geometry(w, h), edge);
    EXPECT_GT(at(soft, w, w / 2 - 1, 10, 1), 0.11f);
    EXPECT_TRUE(compile({tool("local-contrast", {{"amount", 0.0}})}).pass.count == 0);
}

TEST(RenderPlan, TilesMatchTheWholeFrame) {
    const CompiledChain chain = compile({tool("exposure", {{"stops", 0.5}}),
                                         tool("diffusion", {{"amount", 0.6}, {"size", 6.0}}),
                                         tool("grain", {{"amount", 0.5}}),
                                         tool("halation", {{"amount", 0.8}, {"size", 3.0}}, 0.7),
                                         tool("lens-vignette", {{"amount", -1.0}})});
    const int w = 60;
    const int h = 40;
    const std::vector<float> src = test_frame(w, h);
    const RenderGeometry whole = frame_geometry(w, h);
    const std::vector<float> expected = render(chain, whole, src);

    std::vector<float> tiled(expected.size(), -1.0f);
    for (const PixelRect& tile : {PixelRect{0, 0, 25, 17}, PixelRect{25, 0, 60, 17},
                                  PixelRect{0, 17, 25, 40}, PixelRect{25, 17, 60, 40}}) {
        RenderGeometry g = whole;
        g.window = tile;
        const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
        dyeline::run_plan_cpu(src.data(), tiled.data(), chain.table.data(), plan, 2);
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ASSERT_NEAR(tiled[i], expected[i], 1e-5f * std::max(1.0f, std::fabs(expected[i])))
            << "pixel " << i / 3;
    }
}

// Renders `g.window` given only the source region the plugin would ask for, clipped to the
// frame as hosts do, and checks it matches a render that had the whole frame.
void expect_source_region_is_enough(const CompiledChain& chain, const RenderGeometry& g,
                                    const std::vector<float>& frame, int w) {
    const std::vector<float> full = render(chain, g, frame);
    const int h = static_cast<int>(frame.size()) / (w * 3);
    PixelRect s = dyeline::source_region(chain.pass, g);
    s = {std::max(s.x1, 0), std::max(s.y1, 0), std::min(s.x2, w), std::min(s.y2, h)};
    ASSERT_GT(s.width(), 0);
    ASSERT_GT(s.height(), 0);
    RenderGeometry cropped = g;
    cropped.src_bounds = s;
    cropped.src_row_floats = s.width() * 3;
    std::vector<float> src;
    for (int y = s.y1; y < s.y2; ++y) {
        for (int x = s.x1; x < s.x2; ++x) {
            for (int k = 0; k < 3; ++k) {
                src.push_back(at(frame, w, x, y, k));
            }
        }
    }
    EXPECT_EQ(render(chain, cropped, src), full);
}

TEST(RenderPlan, TheSourceRegionIsEnough) {
    // Give a tile only the source the plugin would ask for, the window grown by the blurs'
    // reach, and it must render as if it had the whole frame.
    const CompiledChain chain = compile({tool("diffusion", {{"amount", 0.6}, {"size", 4.0}}),
                                         tool("halation", {{"amount", 0.8}, {"size", 2.0}})});
    const int w = 90;
    const int h = 70;
    RenderGeometry g = frame_geometry(w, h);
    g.window = {40, 30, 52, 41};
    g.dst_bounds = g.window;
    g.dst_row_floats = g.window.width() * 3;
    const PixelRect s = dyeline::source_region(chain.pass, g);
    ASSERT_LT(s.x1, g.window.x1);
    ASSERT_LT(s.x2, w);  // the region stays inside the frame
    ASSERT_LT(s.y2, h);
    expect_source_region_is_enough(chain, g, test_frame(w, h), w);
}

// A frame whose red, green and blue are all `value` in column `column` and 0 elsewhere.
std::vector<float> column_frame(int w, int h, int column, float value) {
    std::vector<float> f(static_cast<std::size_t>(w) * h * 3, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int k = 0; k < 3; ++k) {
            f[(static_cast<std::size_t>(y) * w + column) * 3 + k] = value;
        }
    }
    return f;
}

// The column where channel k of row y is brightest.
int brightest_column(const std::vector<float>& f, int w, int y, int k) {
    int best = 0;
    for (int x = 1; x < w; ++x) {
        if (at(f, w, x, y, k) > at(f, w, best, y, k)) {
            best = x;
        }
    }
    return best;
}

TEST(Warp, ARenderPlanHasAStagePassAndReadsItAfterward) {
    const CompiledChain chain = compile({tool("exposure", {{"stops", 1.0}}),
                                         tool("lens-distortion", {{"amount", 0.5}}),
                                         tool("diffusion", {{"size", 5.0}}),
                                         tool("exposure", {{"stops", -1.0}})});
    const RenderGeometry g = frame_geometry(200, 100);
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
    ASSERT_EQ(plan.passes.size(), 9u);
    EXPECT_EQ(plan.passes[0].kind, dyeline::PassKind::Extract);
    EXPECT_EQ(plan.passes[0].stop, 1);
    EXPECT_EQ(plan.passes[0].image.stage_op, 0);  // reads the source
    EXPECT_EQ(plan.passes[1].kind, dyeline::PassKind::Extract);
    EXPECT_EQ(plan.passes[1].stop, 2);
    EXPECT_EQ(plan.passes[1].image.stage_op, 2);  // reads through the warp
    EXPECT_EQ(plan.passes.back().image.stage_op, 2);
    // The warp only reads inside the frame.
    const PixelRect s = dyeline::source_region(chain.pass, g);
    EXPECT_EQ(s.x1, 0);
    EXPECT_EQ(s.y1, 0);
    EXPECT_EQ(s.x2, 200);
    EXPECT_EQ(s.y2, 100);
}

TEST(LensDistortion, ZoomScalesAboutTheCenterAndBarrelEmptiesTheCorners) {
    const int w = 81;
    const int h = 61;
    // A horizontal ramp: zooming in 2x about the center halves its slope.
    std::vector<float> ramp(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int k = 0; k < 3; ++k) {
                ramp[(static_cast<std::size_t>(y) * w + x) * 3 + k] = static_cast<float>(x);
            }
        }
    }
    const std::vector<float> zoomed =
        render(compile({tool("lens-distortion", {{"amount", 0.0}, {"zoom", 2.0}})}),
               frame_geometry(w, h), ramp);
    EXPECT_NEAR(at(zoomed, w, 40, 30, 1), 40.0f, 1e-4f);
    EXPECT_NEAR(at(zoomed, w, 60, 30, 1), 50.0f, 1e-4f);
    EXPECT_NEAR(at(zoomed, w, 0, 30, 0), 20.0f, 1e-4f);

    // Barrel at full Amount reads past the frame at the corners, which come out black, while
    // the center stays put.
    const std::vector<float> barrel =
        render(compile({tool("lens-distortion", {{"amount", 1.0}})}), frame_geometry(w, h), ramp);
    EXPECT_NEAR(at(barrel, w, 40, 30, 1), 40.0f, 1e-4f);
    EXPECT_EQ(at(barrel, w, 0, 0, 1), 0.0f);
    EXPECT_EQ(at(barrel, w, w - 1, h - 1, 2), 0.0f);
    // Along the middle row, pixels move outward less than they would without distortion:
    // left of center reads further left.
    EXPECT_LT(at(barrel, w, 20, 30, 1), 20.0f);
    EXPECT_GT(at(barrel, w, 60, 30, 1), 60.0f);

    // Pincushion does the opposite.
    const std::vector<float> pincushion =
        render(compile({tool("lens-distortion", {{"amount", -1.0}})}), frame_geometry(w, h), ramp);
    EXPECT_GT(at(pincushion, w, 20, 30, 1), 20.0f);
    EXPECT_LT(at(pincushion, w, 60, 30, 1), 60.0f);

    // Half strength moves pixels half as far.
    const std::vector<float> half = render(
        compile({tool("lens-distortion", {{"amount", 0.0}, {"zoom", 2.0}}, 0.5)}), frame_geometry(w, h), ramp);
    EXPECT_NEAR(at(half, w, 60, 30, 1), 55.0f, 1e-4f);
    EXPECT_TRUE(compile({tool("lens-distortion", {{"amount", 0.0}})}).pass.count == 0);
}

TEST(ChromaticAberration, SplitsRedAndBlueAwayFromGreenTowardTheEdges) {
    const int w = 101;
    const int h = 41;
    const int line = 90;  // a vertical white line right of center
    // At most 1% of the frame's width, about a pixel here, at the corners.
    const CompiledChain strong =
        compile({tool("chromatic-aberration", {{"red", 1.0}, {"blue", -1.0}})});
    const std::vector<float> src = column_frame(w, h, line, 1.0f);
    const std::vector<float> out = render(strong, frame_geometry(w, h), src);
    // Green stays exactly where it was.
    EXPECT_EQ(brightest_column(out, w, 20, 1), line);
    EXPECT_FLOAT_EQ(at(out, w, line, 20, 1), 1.0f);
    // Red moves outward (right) and blue inward.
    EXPECT_GT(at(out, w, line + 1, 20, 0), at(out, w, line - 1, 20, 0));
    EXPECT_LT(at(out, w, line + 1, 20, 2), at(out, w, line - 1, 20, 2));
    // A line through the center doesn't split.
    const std::vector<float> centered =
        render(strong, frame_geometry(w, h), column_frame(w, h, 50, 1.0f));
    EXPECT_EQ(brightest_column(centered, w, 20, 0), 50);
    EXPECT_EQ(brightest_column(centered, w, 20, 2), 50);

    // Fringe blurs red and blue but not green.
    const CompiledChain fringe =
        compile({tool("chromatic-aberration", {{"red", 0.0}, {"blue", 0.0}, {"fringe", 0.5}})});
    const std::vector<float> soft =
        render(fringe, frame_geometry(w, h), column_frame(w, h, 50, 1.0f));
    EXPECT_FLOAT_EQ(at(soft, w, 50, 20, 1), 1.0f);
    EXPECT_FLOAT_EQ(at(soft, w, 51, 20, 1), 0.0f);
    EXPECT_LT(at(soft, w, 50, 20, 0), 0.9f);
    EXPECT_GT(at(soft, w, 50, 20, 0), 0.1f);
    EXPECT_NEAR(at(soft, w, 50, 20, 0), at(soft, w, 50, 20, 2), 1e-6f);
    EXPECT_TRUE(
        compile({tool("chromatic-aberration", {{"red", 0.0}, {"blue", 0.0}})}).pass.count == 0);
}

TEST(Warp, AFoldingPincushionStillGetsTheSourceItReads) {
    // Strong pincushion with Edges, off center, folds back on itself toward the far corners.
    const CompiledChain chain = compile({tool(
        "lens-distortion", {{"amount", -1.0}, {"edges", -1.0}, {"center-x", -0.8}, {"center-y", 0.6}})});
    const int w = 90;
    const int h = 70;
    const std::vector<float> src = test_frame(w, h);
    for (const PixelRect& window : {PixelRect{80, 0, 90, 8}, PixelRect{60, 20, 70, 30},
                                    PixelRect{0, 60, 10, 70}}) {
        RenderGeometry g = frame_geometry(w, h);
        g.window = window;
        g.dst_bounds = window;
        g.dst_row_floats = window.width() * 3;
        expect_source_region_is_enough(chain, g, src, w);
    }
}

TEST(Warp, KeepsAlphaAndPremultiplication) {
    // A premultiplied RGBA frame, half covered: the warp moves the coverage with the color,
    // and solid pixels stay solid.
    const int w = 41;
    const int h = 31;
    std::vector<float> src(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = x < 20 ? 1.0f : 0.0f;
            float* p = &src[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = 0.5f * a;
            p[1] = 0.25f * a;
            p[2] = 0.125f * a;
            p[3] = a;
        }
    }
    RenderGeometry g = frame_geometry(w, h);
    g.channels = 4;
    g.premultiplied = true;
    g.src_row_floats = w * 4;
    g.dst_row_floats = w * 4;
    const CompiledChain chain =
        compile({tool("lens-distortion", {{"amount", 0.0}, {"zoom", 2.0}}), tool("exposure", {{"stops", 1.0}})});
    std::vector<float> dst(src.size(), -1.0f);
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
    dyeline::run_plan_cpu(src.data(), dst.data(), chain.table.data(), plan);
    const auto px = [&](int x, int y, int k) { return dst[(static_cast<std::size_t>(y) * w + x) * 4 + k]; };
    // Zoomed 2x about x = 20.5, the edge between x = 19 and 20 stays near the center, and
    // pixels well left of it are solid, doubled by the exposure.
    EXPECT_FLOAT_EQ(px(5, 15, 3), 1.0f);
    EXPECT_FLOAT_EQ(px(5, 15, 0), 1.0f);
    EXPECT_FLOAT_EQ(px(5, 15, 2), 0.25f);
    EXPECT_FLOAT_EQ(px(35, 15, 3), 0.0f);
    EXPECT_FLOAT_EQ(px(35, 15, 0), 0.0f);
    // Pixel 19 reads halfway between source pixels 19 and 20, so it is half covered, and its
    // color is still premultiplied: straight, it is doubled like the rest.
    const float edge_alpha = px(19, 15, 3);
    EXPECT_NEAR(edge_alpha, 0.5f, 1e-5f);
    EXPECT_NEAR(px(19, 15, 0) / edge_alpha, 1.0f, 1e-5f);
}

TEST(Warp, TilesAndCroppedSourcesMatchTheWholeFrame) {
    // Spatial tools before and after two warps, one off center.
    const CompiledChain chain =
        compile({tool("diffusion", {{"amount", 0.6}, {"size", 3.0}}),
                 tool("lens-distortion", {{"amount", 0.6}, {"edges", -0.5}, {"zoom", 1.2}, {"center-x", 0.3}}),
                 tool("halation", {{"amount", 0.8}, {"size", 2.0}}),
                 tool("chromatic-aberration", {{"red", 1.0}, {"blue", -0.5}, {"fringe", 0.3}}, 0.8),
                 tool("lens-vignette", {{"amount", -1.0}})});
    const int w = 90;
    const int h = 70;
    const std::vector<float> src = test_frame(w, h);
    const RenderGeometry whole = frame_geometry(w, h);
    const std::vector<float> expected = render(chain, whole, src);

    std::vector<float> tiled(expected.size(), -1.0f);
    for (const PixelRect& tile : {PixelRect{0, 0, 37, 29}, PixelRect{37, 0, 90, 29},
                                  PixelRect{0, 29, 37, 70}, PixelRect{37, 29, 90, 70}}) {
        RenderGeometry g = whole;
        g.window = tile;
        const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
        dyeline::run_plan_cpu(src.data(), tiled.data(), chain.table.data(), plan, 2);
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ASSERT_NEAR(tiled[i], expected[i], 1e-5f * std::max(1.0f, std::fabs(expected[i])))
            << "pixel " << i / 3;
    }

    for (const PixelRect& window : {PixelRect{0, 0, 12, 9}, PixelRect{40, 30, 52, 41},
                                    PixelRect{70, 55, 90, 70}}) {
        RenderGeometry g = whole;
        g.window = window;
        g.dst_bounds = window;
        g.dst_row_floats = window.width() * 3;
        expect_source_region_is_enough(chain, g, src, w);
    }
}

TEST(RenderPlan, CachesTheColorBeforeTheFirstSpatialOpAndMatchesWithout) {
    // Color ops before spatial ones, on both sides of a warp, on a premultiplied RGBA frame
    // with soft coverage.
    const CompiledChain chain =
        compile({tool("exposure", {{"stops", 0.5}}),
                 tool("three-way", {{"shadows-hue", 200.0}, {"shadows-amount", 0.3}}),
                 tool("diffusion", {{"amount", 0.6}, {"size", 3.0}}),
                 tool("halation", {{"amount", 0.8}, {"size", 2.0}}),
                 tool("lens-distortion", {{"amount", 0.4}}),
                 tool("contrast", {{"contrast", 1.3}}),
                 tool("local-contrast", {{"amount", 0.5}}),
                 tool("lens-vignette", {{"amount", -1.0}})});
    const int w = 64;
    const int h = 48;
    const std::vector<float> rgb = test_frame(w, h);
    std::vector<float> src(static_cast<std::size_t>(w) * h * 4);
    for (int i = 0; i < w * h; ++i) {
        const float a = static_cast<float>(i % 5) / 4.0f;
        for (int k = 0; k < 3; ++k) {
            src[static_cast<std::size_t>(i) * 4 + k] = rgb[static_cast<std::size_t>(i) * 3 + k] * a;
        }
        src[static_cast<std::size_t>(i) * 4 + 3] = a;
    }
    RenderGeometry g = frame_geometry(w, h);
    g.channels = 4;
    g.premultiplied = true;
    g.src_row_floats = w * 4;
    g.dst_row_floats = w * 4;
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);

    // Diffusion's extract pass keeps its input; Halation's and the warp's read it. After the
    // warp, Local Contrast's extract pass keeps its input for the final pass.
    std::vector<const dyeline::RenderPass*> reads;
    for (const dyeline::RenderPass& pass : plan.passes) {
        if (pass.kind != dyeline::PassKind::Blur) {
            reads.push_back(&pass);
        }
    }
    ASSERT_EQ(reads.size(), 5u);
    EXPECT_EQ(reads[0]->stop, 2);
    EXPECT_EQ(reads[0]->image.cache_write, 1);
    EXPECT_EQ(reads[0]->image.cache_op, 3);
    EXPECT_EQ(reads[1]->stop, 3);
    EXPECT_EQ(reads[1]->image.cache_write, 0);
    EXPECT_EQ(reads[1]->image.cache_op, 3);
    EXPECT_EQ(reads[2]->stop, 4);
    EXPECT_EQ(reads[2]->image.cache_op, 3);
    EXPECT_EQ(reads[3]->stop, 6);
    EXPECT_EQ(reads[3]->image.cache_write, 1);
    EXPECT_EQ(reads[3]->image.cache_op, 7);
    EXPECT_EQ(reads[4]->kind, dyeline::PassKind::Final);
    EXPECT_EQ(reads[4]->image.cache_op, 7);

    // The same plan without the cache gives exactly the same result.
    dyeline::RenderPlan uncached = plan;
    for (dyeline::RenderPass& pass : uncached.passes) {
        pass.image.cache_op = 0;
        pass.image.cache_write = 0;
    }
    std::vector<float> with(src.size(), -1.0f);
    std::vector<float> without(src.size(), -1.0f);
    dyeline::run_plan_cpu(src.data(), with.data(), chain.table.data(), plan, 2);
    dyeline::run_plan_cpu(src.data(), without.data(), chain.table.data(), uncached, 2);
    for (std::size_t i = 0; i < with.size(); ++i) {
        ASSERT_EQ(with[i], without[i]) << "float " << i;
    }

    // Alpha-only images have no color to keep.
    RenderGeometry alpha = frame_geometry(w, h);
    alpha.channels = 1;
    for (const dyeline::RenderPass& pass : dyeline::plan_render(chain.pass, alpha).passes) {
        EXPECT_EQ(pass.image.cache_op, 0);
        EXPECT_EQ(pass.image.cache_write, 0);
    }
}

// A frame of thin bright lines on a dark ground, every `step` pixels across and down.
std::vector<float> grid_frame(int w, int h, int step) {
    std::vector<float> f(static_cast<std::size_t>(w) * h * 3, 0.05f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x % step == step / 2 || y % step == step / 2) {
                for (int k = 0; k < 3; ++k) {
                    f[(static_cast<std::size_t>(y) * w + x) * 3 + k] = 1.0f;
                }
            }
        }
    }
    return f;
}

TEST(RenderPlan, TwoLevelBlursAddTwelveBlurPasses) {
    const CompiledChain chain = compile({tool("edge-softness", {{"blur", 3.0}})});
    const RenderGeometry g = frame_geometry(200, 100);
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
    ASSERT_EQ(plan.passes.size(), 1u + 12u + 1u);
    EXPECT_EQ(plan.passes[0].kind, dyeline::PassKind::Extract);
    EXPECT_EQ(plan.passes[12].kind, dyeline::PassKind::Blur);
    // Sigma 6 pixels: a light blur of sigma 2 (box radius 2), then sigma 5.66 (radius 5).
    const PixelRect s = dyeline::source_region(chain.pass, g);
    EXPECT_EQ(s.x1, -3 * (2 + 5));
    EXPECT_EQ(s.x2, 200 + 3 * (2 + 5));
}

TEST(EdgeSoftness, KeepsTheCenterSharpAndBlursTheEdges) {
    const int w = 120;
    const int h = 80;
    const std::vector<float> src = grid_frame(w, h, 20);
    const CompiledChain chain =
        compile({tool("edge-softness", {{"blur", 2.0}, {"size", 0.3}, {"softness", 0.4}})});
    const std::vector<float> out = render(chain, frame_geometry(w, h), src);
    // Lines through the middle stay as they were; lines near the corners spread.
    EXPECT_FLOAT_EQ(at(out, w, 50, 40, 1), 1.0f);
    EXPECT_FLOAT_EQ(at(out, w, 49, 40, 1), 0.05f);
    EXPECT_LT(at(out, w, 10, 10, 1), 0.6f);
    EXPECT_GT(at(out, w, 9, 6, 1), 0.1f);
    // An even image stays even.
    std::vector<float> even(src.size(), 0.3f);
    for (float v : render(chain, frame_geometry(w, h), even)) {
        EXPECT_NEAR(v, 0.3f, 1e-5f);
    }
    EXPECT_TRUE(compile({tool("edge-softness", {{"blur", 0.0}})}).pass.count == 0);
}

TEST(SwingTilt, KeepsABandSharpAcrossTheFrame) {
    const int w = 120;
    const int h = 80;
    const std::vector<float> src = grid_frame(w, h, 20);
    // Angle 0: the band runs across the frame, so rows near the middle stay sharp and the
    // top and bottom blur, left and right alike.
    const CompiledChain across = compile(
        {tool("swing-tilt", {{"blur", 2.0}, {"focus", 0.1}, {"softness", 0.2}, {"angle", 0.0}})});
    const std::vector<float> a = render(across, frame_geometry(w, h), src);
    EXPECT_FLOAT_EQ(at(a, w, 15, 40, 1), 0.05f);
    EXPECT_FLOAT_EQ(at(a, w, 15, 39, 1), 0.05f);
    EXPECT_FLOAT_EQ(at(a, w, 105, 41, 1), 0.05f);
    EXPECT_FLOAT_EQ(at(a, w, 15, 50, 1), 1.0f);  // a row line just inside the band
    EXPECT_LT(at(a, w, 15, 70, 1), 0.6f);       // far from it, the row line spreads
    EXPECT_NEAR(at(a, w, 15, 70, 1), at(a, w, 105, 70, 1), 1e-5f);
    // Angle 90: the band runs up and down instead.
    const CompiledChain upright = compile(
        {tool("swing-tilt", {{"blur", 2.0}, {"focus", 0.1}, {"softness", 0.2}, {"angle", 90.0}})});
    const std::vector<float> u = render(upright, frame_geometry(w, h), src);
    EXPECT_FLOAT_EQ(at(u, w, 50, 15, 1), 1.0f);
    EXPECT_LT(at(u, w, 10, 15, 1), 0.6f);
}

TEST(SoftFocus, TilesAndCroppedSourcesMatchTheWholeFrame) {
    const CompiledChain chain =
        compile({tool("exposure", {{"stops", 0.3}}),
                 tool("swing-tilt", {{"blur", 2.0}, {"angle", 30.0}, {"center-y", 0.2}}, 0.8),
                 tool("halation", {{"amount", 0.8}, {"size", 2.0}}),
                 tool("edge-softness", {{"blur", 1.5}, {"center-x", -0.2}})});
    const int w = 90;
    const int h = 70;
    const std::vector<float> src = test_frame(w, h);
    const RenderGeometry whole = frame_geometry(w, h);
    const std::vector<float> expected = render(chain, whole, src);
    std::vector<float> tiled(expected.size(), -1.0f);
    for (const PixelRect& tile : {PixelRect{0, 0, 37, 29}, PixelRect{37, 0, 90, 29},
                                  PixelRect{0, 29, 37, 70}, PixelRect{37, 29, 90, 70}}) {
        RenderGeometry g = whole;
        g.window = tile;
        const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, g);
        dyeline::run_plan_cpu(src.data(), tiled.data(), chain.table.data(), plan, 2);
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        ASSERT_NEAR(tiled[i], expected[i], 1e-5f * std::max(1.0f, std::fabs(expected[i])))
            << "pixel " << i / 3;
    }
    RenderGeometry g = whole;
    g.window = {40, 30, 52, 41};
    g.dst_bounds = g.window;
    g.dst_row_floats = g.window.width() * 3;
    expect_source_region_is_enough(chain, g, src, w);
}

// A frame split down the middle: `left` on the left half, `right` on the right.
std::vector<float> split_frame(int w, int h, float left, float right) {
    std::vector<float> f(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int k = 0; k < 3; ++k) {
                f[(static_cast<std::size_t>(y) * w + x) * 3 + k] = x < w / 2 ? left : right;
            }
        }
    }
    return f;
}

TEST(Diffusion, ProMistVeilsTheShadowsAndBlackProMistKeepsThem) {
    const int w = 40;
    const int h = 20;
    // An edge between two dark tones, both under middle grey.
    const std::vector<float> dark = split_frame(w, h, 0.01f, 0.08f);
    const auto style = [&](int s) {
        return render(compile({tool("diffusion", {{"amount", 1.0}, {"size", 4.0},
                                                  {"highlights", 0.5}, {"style", s}})}),
                      frame_geometry(w, h), dark);
    };
    const std::vector<float> diffusion = style(0);
    const std::vector<float> pro_mist = style(1);
    const std::vector<float> black = style(2);
    // Pro-Mist scatters more of the shadows than plain Diffusion, lifting the darker side.
    EXPECT_GT(at(pro_mist, w, w / 2 - 1, 10, 1), at(diffusion, w, w / 2 - 1, 10, 1));
    EXPECT_GT(at(diffusion, w, w / 2 - 1, 10, 1), 0.012f);
    // Black Pro-Mist leaves shadows alone.
    EXPECT_NEAR(at(black, w, w / 2 - 1, 10, 1), 0.01f, 1e-4f);
    EXPECT_NEAR(at(black, w, w / 2, 10, 1), 0.08f, 1e-3f);
    // Highlights still bloom with Black Pro-Mist.
    const std::vector<float> bright = render(
        compile({tool("diffusion", {{"amount", 1.0}, {"size", 4.0}, {"style", 2}})}),
        frame_geometry(w, h), split_frame(w, h, 0.01f, 8.0f));
    EXPECT_GT(at(bright, w, w / 2 - 1, 10, 1), 0.2f);
}

TEST(Streaks, SpreadHighlightsAlongTheRowOnly) {
    const int w = 81;
    const int h = 41;
    std::vector<float> src(static_cast<std::size_t>(w) * h * 3, 0.1f);
    for (int k = 0; k < 3; ++k) {
        src[(static_cast<std::size_t>(20) * w + 40) * 3 + k] = 500.0f;
    }
    const std::vector<float> out = render(
        compile({tool("streaks", {{"amount", 1.0}, {"length", 10.0}})}), frame_geometry(w, h), src);
    // Far along the row the streak is bright; one row away there is none.
    EXPECT_GT(at(out, w, 25, 20, 2), 0.5f);
    EXPECT_FLOAT_EQ(at(out, w, 40, 21, 2), 0.1f);
    EXPECT_FLOAT_EQ(at(out, w, 25, 19, 0), 0.1f);
    // Tinted blue by default.
    EXPECT_GT(at(out, w, 25, 20, 2) - 0.1f, 1.5f * (at(out, w, 25, 20, 0) - 0.1f));
    // The source region widens along x only.
    const PixelRect s = dyeline::source_region(
        compile({tool("streaks", {{"length", 10.0}})}).pass, frame_geometry(w, h));
    EXPECT_LT(s.x1, 0);
    EXPECT_EQ(s.y1, 0);
    EXPECT_EQ(s.y2, h);
}

}  // namespace
