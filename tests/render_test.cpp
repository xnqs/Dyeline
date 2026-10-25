// The render plan: passes for spatial tools, the blur's size, Diffusion and Halation, and
// renders split into tiles or given only the source the plugin asks for matching one render
// of the whole frame.

#include "dyeline/render.h"

#include <gtest/gtest.h>

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
    const dyeline::PixelMargin margin = dyeline::spatial_margin(chain.pass, frame_geometry(8, 8));
    EXPECT_EQ(margin.x, 0);
    EXPECT_EQ(margin.y, 0);
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
    const dyeline::PixelMargin margin = dyeline::spatial_margin(chain.pass, g);
    EXPECT_EQ(margin.x, 3 * 10 + 3 * 4);
    EXPECT_EQ(margin.y, margin.x);
    // Diffusion's extract covers the window plus both blurs' reach.
    EXPECT_EQ(plan.passes[0].image.width, 200 + 2 * margin.x);
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

TEST(RenderPlan, TheSourceMarginIsEnough) {
    // Give a tile only the source the plugin would ask for, the window grown by the margin,
    // and it must render as if it had the whole frame.
    const CompiledChain chain = compile({tool("diffusion", {{"amount", 0.6}, {"size", 4.0}}),
                                         tool("halation", {{"amount", 0.8}, {"size", 2.0}})});
    const int w = 90;
    const int h = 70;
    const std::vector<float> frame = test_frame(w, h);
    RenderGeometry g = frame_geometry(w, h);
    g.window = {40, 30, 52, 41};
    g.dst_bounds = g.window;
    g.dst_row_floats = g.window.width() * 3;
    const dyeline::PixelMargin m = dyeline::spatial_margin(chain.pass, g);
    ASSERT_GT(m.x, 0);
    ASSERT_LT(g.window.x2 + m.x, w);  // the margin stays inside the frame
    ASSERT_LT(g.window.y2 + m.y, h);
    const std::vector<float> full = render(chain, g, frame);

    RenderGeometry cropped = g;
    cropped.src_bounds = {g.window.x1 - m.x, g.window.y1 - m.y, g.window.x2 + m.x, g.window.y2 + m.y};
    cropped.src_row_floats = cropped.src_bounds.width() * 3;
    std::vector<float> src;
    for (int y = cropped.src_bounds.y1; y < cropped.src_bounds.y2; ++y) {
        for (int x = cropped.src_bounds.x1; x < cropped.src_bounds.x2; ++x) {
            for (int k = 0; k < 3; ++k) {
                src.push_back(at(frame, w, x, y, k));
            }
        }
    }
    EXPECT_EQ(render(chain, cropped, src), full);
}

}  // namespace
