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

// Args for a window that skips the frame's first column and row, so the tests also check
// the window offsets and the window's place in the frame.
inline dl_image_args window_args(int width, int height) {
    dl_image_args args{};
    args.width = width - 1;
    args.height = height - 1;
    args.channels = 4;
    args.premultiplied = 1;
    args.src_row_floats = width * 4;
    args.dst_row_floats = width * 4;
    args.src_x0 = 1;
    args.src_y0 = 1;
    args.dst_x0 = 1;
    args.dst_y0 = 1;
    // The frame is the whole test image at half render scale with 2:1 pixels, so canonical
    // positions differ from pixel ones.
    args.frame_x0 = 1;
    args.frame_y0 = 1;
    args.canonical_x = 4.0f;
    args.canonical_y = 2.0f;
    args.frame_right = 4.0f * static_cast<float>(width);
    args.frame_top = 2.0f * static_cast<float>(height);
    args.seed = 7;
    return args;
}

// A look using every per-pixel tool at partial strengths, under a partial overall strength,
// so each tool's math, op order and both blends run on the GPU.
inline CompiledChain test_chain() {
    // A curved 3D LUT under LUT Gamma 2, with the frame's values reaching past its domain.
    const TempFile lut(cube_3d(9, [](double r, double g, double b, double out[3]) {
                           out[0] = 0.9 * std::pow(r, 0.8) + 0.05 * g;
                           out[1] = 0.7 * g * g + 0.2 * b + 0.05;
                           out[2] = 0.8 * std::sqrt(b) + 0.1 * r;
                       }),
                       ".cube");
    Look look;
    auto add = [&](const char* id, double strength, nlohmann::json params) {
        ToolEntry entry;
        entry.tool = id;
        entry.strength = strength;
        entry.params = std::move(params);
        look.chain.push_back(std::move(entry));
    };
    add("exposure", 1.0, {{"stops", 1.3}});
    add("contrast", 0.9, {{"contrast", 1.4}, {"pivot", 0.2}});
    add("s-curve", 1.0, {{"amount", 0.6}, {"range", 4.0}});
    add("hue-saturation", 0.7, {{"hue", 25.0}, {"saturation", 1.6}});
    add("shadows-highlights", 1.0, {{"shadows", 0.8}, {"highlights", -0.6}});
    add("channel-mixer", 0.6, {{"red-from-green", 0.2}, {"blue-from-blue", 0.9}});
    add("highlight-shoulder", 1.0, {{"start", 0.9}, {"headroom", 1.2}});
    add("black-white", 0.3, {{"red", 0.5}, {"green", 0.4}, {"blue", 0.1}});
    add("three-way", 0.8, {{"shadows-hue", 210.0}, {"shadows-amount", 0.7},
                           {"midtones-hue", 30.0}, {"midtones-amount", 0.3},
                           {"highlights-hue", 60.0}, {"highlights-amount", 0.5}});
    add("lut", 0.9, {{"file", lut.path()}, {"gamma", 2.0}});
    add("lens-vignette", 0.9, {{"amount", -1.5}, {"size", 0.2}, {"softness", 0.7},
                               {"roundness", 0.4}, {"center-x", 0.1}, {"center-y", -0.2}});
    add("grain", 1.0, {{"amount", 0.8}, {"size", 3.0}, {"color", 0.6}, {"seed", 12}});
    add("exposure", 0.5, {{"stops", -2.0}});
    CompiledChain chain;
    std::string error;
    compile_chain(look, 0.8, chain, error);
    return chain;
}

// A chain without table data, so the backends' path for a look with no LUT runs too.
inline CompiledChain table_free_chain() {
    Look look;
    ToolEntry entry;
    entry.tool = "exposure";
    entry.params = {{"stops", 0.7}};
    look.chain.push_back(entry);
    CompiledChain chain;
    std::string error;
    compile_chain(look, 1.0, chain, error);
    return chain;
}

// The CPU result for the same args: the reference every GPU backend must match.
inline std::vector<float> cpu_reference(const std::vector<float>& src, const dl_image_args& args,
                                        const CompiledChain& chain) {
    std::vector<float> dst(src.size(), 0.0f);
    for (int y = 0; y < args.height; ++y) {
        for (int x = 0; x < args.width; ++x) {
            dl_chain_at(src.data(), dst.data(), x, y, args, &chain.pass, chain.table.data());
        }
    }
    return dst;
}

// How far a GPU result may be from the CPU one. GPUs may use faster division and fused
// multiply-adds, so allow a few units in the last place, relative to the value.
inline float tolerance(float expected) {
    return 1e-5f * std::max(1.0f, std::fabs(expected));
}

}  // namespace dyeline::test
