#pragma once

// Turns a look into the fused per-pixel pass the kernels run, and runs it on the CPU.

#include <cstddef>
#include <string>
#include <vector>

#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"

namespace dyeline {

struct CompiledChain {
    dl_chain pass{};
    // Tools in the look this build can't run (unknown, or saved by a newer Dyeline). They
    // are bypassed, and the plugin reports them.
    std::vector<std::string> missing_tools;
    // Tools left out because something they need couldn't be loaded, such as a LUT file,
    // each with the reason. The plugin reports them.
    std::vector<std::string> problems;
    // Data the pass reads besides its ops, such as LUT tables (see chain_kernel.h).
    std::vector<float> table;

    // True when rendering would leave every pixel unchanged.
    bool is_identity() const { return pass.count == 0 || pass.strength == 0.0f; }
};

// Compiles `look` with an overall `strength` (0 to 1). Disabled tools and tools whose settings
// change nothing are left out. Relative file paths in the look, such as a LUT's, are resolved
// against `base_dir` when it is set. Fails, with `error` set, when more than DL_MAX_OPS tools
// are active.
bool compile_chain(const Look& look, double strength, CompiledChain& out, std::string& error,
                   const std::string& base_dir = {});

// Runs a compiled chain over the window `args` describes, on the CPU, with the same kernel
// code the GPU backends use. `src` and `dst` may be the same buffer.
void render_chain_window(const float* src, float* dst, const dl_image_args& args,
                         const CompiledChain& chain);

// Runs a compiled chain on one row of interleaved float pixels. `channels` is 1 (alpha only),
// 3 or 4. The row is treated as the whole frame, one canonical pixel per pixel, at seed 0.
void render_chain_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                      bool premultiplied, const CompiledChain& chain);

}  // namespace dyeline
