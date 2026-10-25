#pragma once

// A small test frame, a test chain and the CPU reference result, shared by the GPU backend
// tests.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dyeline/kernels/chain_kernel.h"

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
// the window offsets.
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
    return args;
}

// Two Exposure ops at partial strengths, under a partial overall strength, so op order,
// per-tool blending and the overall blend are all exercised.
inline dl_chain test_chain() {
    dl_chain chain{};
    chain.count = 2;
    chain.strength = 0.8f;
    chain.ops[0].tool = DL_OP_EXPOSURE;
    chain.ops[0].strength = 1.0f;
    chain.ops[0].p[0] = 2.5f;
    chain.ops[1].tool = DL_OP_EXPOSURE;
    chain.ops[1].strength = 0.5f;
    chain.ops[1].p[0] = 0.25f;
    return chain;
}

// The CPU result for the same args: the reference every GPU backend must match.
inline std::vector<float> cpu_reference(const std::vector<float>& src, const dl_image_args& args,
                                        const dl_chain& chain) {
    std::vector<float> dst(src.size(), 0.0f);
    for (int y = 0; y < args.height; ++y) {
        for (int x = 0; x < args.width; ++x) {
            dl_chain_at(src.data(), dst.data(), x, y, args, &chain);
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
