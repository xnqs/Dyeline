#pragma once

// A small test frame, a test chain and the CPU reference result, shared by the GPU backend
// tests.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"
#include "dyeline/render.h"
#include "temp_file.h"

namespace dyeline::test {

// An RGBA frame with values spread across shadows, mids and over-bright highlights.
inline std::vector<float> make_frame(int width, int height) {
    std::vector<float> frame(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float* p = &frame[(static_cast<std::size_t>(y) * width + x) * 4];
            p[0] = 0.01f * static_cast<float>(x);
            p[1] = 0.02f * static_cast<float>(y);
            p[2] = 0.5f + 0.03f * static_cast<float>(x + y);
            p[3] = (x % 2 == 0) ? 1.0f : 0.5f;
        }
    }
    return frame;
}

// A render of the frame minus its first column and row, so the tests also check the window's
// offsets in the buffers and its place in the frame, and spatial tools reach past the
// source's edges.
inline RenderGeometry window_geometry(int width, int height) {
    RenderGeometry g;
    g.window = {1, 1, width, height};
    g.src_bounds = {0, 0, width, height};
    g.dst_bounds = {0, 0, width, height};
    g.src_row_floats = width * 4;
    g.dst_row_floats = width * 4;
    g.channels = 4;
    g.premultiplied = true;
    // The frame is the whole test image at half render scale with 2:1 pixels, so canonical
    // positions differ from pixel ones, and blurs reach different distances in x and y.
    g.canonical_x = 4.0f;
    g.canonical_y = 2.0f;
    g.frame_right = 4.0f * static_cast<float>(width);
    g.frame_top = 2.0f * static_cast<float>(height);
    g.seed = 7;
    return g;
}

// A look using every tool at partial strengths, under a partial overall strength, so each
// tool's math, op order and both blends run on the GPU. It converts sRGB in to LogC4 out
// with full color management, so both transforms and their matrices run too. Two spatial tools, with per-pixel
// tools between them, check that each blur sees the chain up to its own tool.
inline CompiledChain test_chain() {
    // A curved 3D LUT under LUT Gamma 2, with the frame's values reaching past its domain.
    const TempFile lut(cube_3d(9, [](double r, double g, double b, double out[3]) {
                           out[0] = 0.9 * std::pow(r, 0.8) + 0.05 * g;
                           out[1] = 0.7 * g * g + 0.2 * b + 0.05;
                           out[2] = 0.8 * std::sqrt(b) + 0.1 * r;
                       }),
                       ".cube");
    Look look;
    look.color.mode = "full";
    look.color.input = "srgb";
    look.color.output = "logc4";
    auto add = [&](const char* id, double strength, nlohmann::json params) {
        ToolEntry entry;
        entry.tool = id;
        entry.strength = strength;
        entry.params = std::move(params);
        look.chain.push_back(std::move(entry));
    };
    add("exposure", 1.0, {{"stops", 1.3}});
    add("halation", 0.8, {{"amount", 1.2}, {"threshold", 0.6}, {"size", 3.0}});
    add("contrast", 0.9, {{"contrast", 1.4}, {"pivot", 0.2}});
    add("s-curve", 1.0, {{"amount", 0.6}, {"range", 4.0}});
    add("hue-saturation", 0.7, {{"hue", 25.0}, {"saturation", 1.6}});
    add("shadows-highlights", 1.0, {{"shadows", 0.8}, {"highlights", -0.6}});
    add("channel-mixer", 0.6, {{"red-from-green", 0.2}, {"blue-from-blue", 0.9}});
    add("highlight-shoulder", 1.0, {{"start", 0.9}, {"headroom", 1.2}});
    add("bleach-bypass", 0.7, {{"amount", 0.8}, {"pivot", 0.25}});
    add("three-way", 0.8, {{"shadows-hue", 210.0}, {"shadows-amount", 0.7},
                           {"midtones-hue", 30.0}, {"midtones-amount", 0.3},
                           {"highlights-hue", 60.0}, {"highlights-amount", 0.5}});
    add("lut", 0.9, {{"file", lut.path()}, {"gamma", 2.0}});
    add("diffusion", 0.9, {{"amount", 0.7}, {"size", 5.0}, {"highlights", 0.4}});
    add("lens-vignette", 0.9, {{"amount", -1.5}, {"size", 0.2}, {"softness", 0.7},
                               {"roundness", 0.4}, {"center-x", 0.1}, {"center-y", -0.2}});
    add("film-negative", 0.8, {{"stock", 4}, {"exposure", 0.5}});
    add("film-print", 0.9, {{"stock", 1}, {"red", 3.0}, {"contrast", 1.2}});
    add("grain", 1.0, {{"amount", 0.8}, {"size", 3.0}, {"color", 0.6}, {"seed", 12}});
    CompiledChain chain;
    std::string error;
    compile_chain(look, 0.8, chain, error);
    return chain;
}

// A chain without table data, so the backends' path for a look with no LUT runs too. It
// holds the tools that depend on where a pixel is, beside Lens Vignette and Grain above.
inline CompiledChain table_free_chain() {
    Look look;
    auto add = [&](const char* id, nlohmann::json params) {
        ToolEntry entry;
        entry.tool = id;
        entry.strength = 0.9;
        entry.params = std::move(params);
        look.chain.push_back(std::move(entry));
    };
    add("exposure", {{"stops", 0.7}});
    add("gradient-tint", {{"hue", 200.0}, {"amount", 0.8}, {"angle", 30.0}, {"softness", 0.5}});
    add("grad-exposure", {{"stops", -1.2}, {"center-y", 0.2}, {"angle", -100.0}});
    add("spot-exposure", {{"stops", 0.8}, {"center-x", 0.3}, {"size", 0.2}, {"softness", 0.4}});
    add("fill-light", {{"amount", 0.9}, {"tint-amount", 0.3}});
    add("spot-fill", {{"amount", 1.5}, {"center-x", -0.4}, {"center-y", 0.1}, {"size", 0.3}});
    CompiledChain chain;
    std::string error;
    compile_chain(look, 1.0, chain, error);
    return chain;
}

// The CPU result for the same plan: the reference every GPU backend must match.
inline std::vector<float> cpu_reference(const std::vector<float>& src, const RenderPlan& plan,
                                        const CompiledChain& chain) {
    std::vector<float> dst(src.size(), 0.0f);
    run_plan_cpu(src.data(), dst.data(), chain.table.data(), plan);
    return dst;
}

// How far a GPU result may be from the CPU one. GPUs may use faster division and fused
// multiply-adds, so allow a few units in the last place, relative to the value.
inline float tolerance(float expected) {
    return 1e-5f * std::max(1.0f, std::fabs(expected));
}

}  // namespace dyeline::test
