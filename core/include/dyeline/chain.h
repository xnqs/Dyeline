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

    // True when rendering would leave every pixel unchanged.
    bool is_identity() const { return pass.count == 0 || pass.strength == 0.0f; }
};

// Compiles `look` with an overall `strength` (0 to 1). Disabled tools and tools whose settings
// change nothing are left out. Fails, with `error` set, when more than DL_MAX_OPS tools are
// active.
bool compile_chain(const Look& look, double strength, CompiledChain& out, std::string& error);

// Runs a compiled chain on one row of interleaved float pixels on the CPU, with the same kernel
// code the GPU backends use. `channels` is 1 (alpha only), 3 or 4. `src` and `dst` may be the
// same row.
void render_chain_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                      bool premultiplied, const dl_chain& chain);

}  // namespace dyeline
