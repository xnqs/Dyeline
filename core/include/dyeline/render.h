#pragma once

// Plans how a compiled look renders one window, as a list of passes every backend runs the
// same way, and runs a plan on the CPU. Looks with only per-pixel tools render in one pass;
// each spatial tool (Diffusion, Halation) adds an extract pass and six box-blur passes ahead
// of it (see chain_kernel.h).

#include <cstddef>
#include <vector>

#include "dyeline/kernels/chain_kernel.h"

namespace dyeline {

// A rectangle of pixels in the host's pixel coordinates: x1 and y1 inside, x2 and y2 just past.
struct PixelRect {
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;

    int width() const { return x2 - x1; }
    int height() const { return y2 - y1; }
};

// One render, as the host describes it.
struct RenderGeometry {
    PixelRect window;      // the pixels to render
    PixelRect src_bounds;  // the pixels the source image holds
    PixelRect dst_bounds;  // the pixels the destination image holds
    int src_row_floats = 0;
    int dst_row_floats = 0;
    int channels = 4;
    bool premultiplied = false;
    float canonical_x = 1.0f;  // canonical units per pixel: pixel aspect / render scale
    float canonical_y = 1.0f;  // 1 / render scale
    float frame_left = 0.0f;   // the frame's edges, in canonical coordinates
    float frame_bottom = 0.0f;
    float frame_right = 0.0f;
    float frame_top = 0.0f;
    int seed = 0;              // differs from frame to frame
};

enum class PassKind { Extract, Blur, Final };

struct RenderPass {
    PassKind kind = PassKind::Final;
    dl_image_args image{};  // Extract and Final: the region and buffers
    int stop = 0;           // Extract: the spatial op whose input this pass extracts
    dl_box_args box{};      // Blur
};

struct RenderPlan {
    // The compiled pass, with each spatial op pointed at its blurred image.
    dl_chain chain{};
    std::vector<RenderPass> passes;
    // Floats of scratch memory the passes share (none for a look without spatial tools).
    std::size_t work_floats = 0;
};

// How many pixels beyond the one it renders a chain reaches, on each side. The host widens
// the source region it asks for by this much.
struct PixelMargin {
    int x = 0;
    int y = 0;
};

PixelMargin spatial_margin(const dl_chain& chain, const RenderGeometry& geometry);

RenderPlan plan_render(const dl_chain& chain, const RenderGeometry& geometry);

// Runs a plan on the CPU, using up to `threads` threads (0 means one per core). `src` and
// `dst` point at the first pixel of their bounds; `table` is the compiled chain's table data.
void run_plan_cpu(const float* src, float* dst, const float* table, const RenderPlan& plan,
                  unsigned threads = 0);

}  // namespace dyeline
