#include "dyeline/render.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>

namespace dyeline {

namespace {

// Three box blurs of width w = 2r + 1 in a row have a variance of 3 (w^2 - 1) / 12, so the
// radius for a Gaussian of `sigma` pixels is about (sqrt(4 sigma^2 + 1) - 1) / 2.
constexpr int kBoxPasses = 3;
constexpr int kMaxBoxRadius = 4096;

int box_radius(double sigma) {
    if (!(sigma > 0.0)) {
        return 0;
    }
    const double r = (std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) * 0.5;
    return static_cast<int>(std::min<double>(std::lround(r), kMaxBoxRadius));
}

struct Radius {
    int x = 0;
    int y = 0;
};

Radius blur_radius(const dl_op& op, const RenderGeometry& g) {
    const double sigma = static_cast<double>(op.p[DL_OP_BLUR_SIZE]) * (g.frame_right - g.frame_left);
    return {box_radius(sigma / g.canonical_x), box_radius(sigma / g.canonical_y)};
}

PixelRect grow(PixelRect r, int x, int y) {
    return {r.x1 - x, r.y1 - y, r.x2 + x, r.y2 + y};
}

std::size_t rgb_floats(const PixelRect& r) {
    return static_cast<std::size_t>(r.width()) * static_cast<std::size_t>(r.height()) * 3;
}

// Image args for a pass over `region`, reading the source and placing pixels in the frame.
dl_image_args pass_args(const RenderGeometry& g, const PixelRect& region) {
    dl_image_args a{};
    a.width = region.width();
    a.height = region.height();
    a.channels = g.channels;
    a.premultiplied = g.premultiplied ? 1 : 0;
    a.src_row_floats = g.src_row_floats;
    a.dst_row_floats = g.dst_row_floats;
    a.src_x0 = region.x1 - g.src_bounds.x1;
    a.src_y0 = region.y1 - g.src_bounds.y1;
    a.src_width = g.src_bounds.width();
    a.src_height = g.src_bounds.height();
    a.dst_x0 = region.x1 - g.dst_bounds.x1;
    a.dst_y0 = region.y1 - g.dst_bounds.y1;
    a.frame_x0 = region.x1;
    a.frame_y0 = region.y1;
    a.canonical_x = g.canonical_x;
    a.canonical_y = g.canonical_y;
    a.frame_left = g.frame_left;
    a.frame_bottom = g.frame_bottom;
    a.frame_right = g.frame_right;
    a.frame_top = g.frame_top;
    a.seed = g.seed;
    return a;
}

// A blur pass along rows (horizontal) or columns, from an RGB image `in_width` pixels wide
// to one `out_width` wide.
RenderPass blur_pass(bool horizontal, std::size_t in, int in_width, std::size_t out, int out_width,
                     int lines, int length, int radius) {
    RenderPass pass;
    pass.kind = PassKind::Blur;
    dl_box_args& b = pass.box;
    b.in_offset = static_cast<int>(in);
    b.out_offset = static_cast<int>(out);
    b.in_step = horizontal ? 3 : in_width * 3;
    b.in_cross = horizontal ? in_width * 3 : 3;
    b.out_step = horizontal ? 3 : out_width * 3;
    b.out_cross = horizontal ? out_width * 3 : 3;
    b.lines = lines;
    b.length = length;
    b.radius = radius;
    return pass;
}

void parallel_for(int count, unsigned threads, const std::function<void(int)>& body) {
    if (threads == 0) {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    threads = std::min<unsigned>(threads, static_cast<unsigned>(std::max(count, 1)));
    if (threads <= 1) {
        for (int i = 0; i < count; ++i) {
            body(i);
        }
        return;
    }
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&] {
            for (int i = next++; i < count; i = next++) {
                body(i);
            }
        });
    }
    for (std::thread& thread : pool) {
        thread.join();
    }
}

}  // namespace

PixelMargin spatial_margin(const dl_chain& chain, const RenderGeometry& geometry) {
    PixelMargin margin;
    for (int i = 0; i < chain.count; ++i) {
        if (dl_is_spatial(chain.ops[i].tool)) {
            const Radius r = blur_radius(chain.ops[i], geometry);
            margin.x += kBoxPasses * r.x;
            margin.y += kBoxPasses * r.y;
        }
    }
    return margin;
}

RenderPlan plan_render(const dl_chain& chain, const RenderGeometry& g) {
    RenderPlan plan;
    plan.chain = chain;

    std::vector<int> spatial;
    for (int i = 0; i < chain.count; ++i) {
        if (dl_is_spatial(chain.ops[i].tool)) {
            spatial.push_back(i);
        }
    }

    if (!spatial.empty()) {
        // Each spatial op's blurred image must cover the window plus whatever later spatial
        // ops reach, and its extract pass that plus its own blur's reach.
        const std::size_t n = spatial.size();
        std::vector<Radius> radius(n);
        std::vector<PixelRect> blurred(n);
        std::vector<PixelRect> extract(n);
        PixelMargin after;
        for (std::size_t k = n; k-- > 0;) {
            radius[k] = blur_radius(chain.ops[spatial[k]], g);
            blurred[k] = grow(g.window, after.x, after.y);
            extract[k] = grow(blurred[k], kBoxPasses * radius[k].x, kBoxPasses * radius[k].y);
            after.x += kBoxPasses * radius[k].x;
            after.y += kBoxPasses * radius[k].y;
        }

        // Work memory: two scratch images the size of the largest extract region, then each
        // op's blurred image.
        std::size_t scratch = 0;
        for (const PixelRect& r : extract) {
            scratch = std::max(scratch, rgb_floats(r));
        }
        const std::size_t a = 0;
        const std::size_t b = scratch;
        std::size_t next = 2 * scratch;
        for (std::size_t k = 0; k < n; ++k) {
            dl_op& op = plan.chain.ops[spatial[k]];
            op.ip[0] = static_cast<int>(next);
            op.ip[1] = blurred[k].x1;
            op.ip[2] = blurred[k].y1;
            op.ip[3] = blurred[k].width();
            next += rgb_floats(blurred[k]);
        }
        plan.work_floats = next;

        for (std::size_t k = 0; k < n; ++k) {
            RenderPass ex;
            ex.kind = PassKind::Extract;
            ex.image = pass_args(g, extract[k]);
            ex.image.work_offset = static_cast<int>(a);
            ex.stop = spatial[k];
            plan.passes.push_back(ex);

            // Three horizontal passes, A -> B -> A -> B, each narrowing the image by 2r; then
            // three vertical ones, B -> A -> B -> the op's blurred image.
            const int rows = extract[k].height();
            int width = extract[k].width();
            const int rx = radius[k].x;
            std::size_t from = a;
            std::size_t to = b;
            for (int i = 0; i < kBoxPasses; ++i) {
                plan.passes.push_back(blur_pass(true, from, width, to, width - 2 * rx, rows,
                                                width - 2 * rx, rx));
                width -= 2 * rx;
                std::swap(from, to);
            }
            int height = rows;
            const int ry = radius[k].y;
            for (int i = 0; i < kBoxPasses; ++i) {
                const std::size_t target = i == kBoxPasses - 1
                                               ? static_cast<std::size_t>(plan.chain.ops[spatial[k]].ip[0])
                                               : to;
                plan.passes.push_back(blur_pass(false, from, width, target, width, width,
                                                height - 2 * ry, ry));
                height -= 2 * ry;
                std::swap(from, to);
            }
        }
    }

    RenderPass final_pass;
    final_pass.kind = PassKind::Final;
    final_pass.image = pass_args(g, g.window);
    plan.passes.push_back(final_pass);
    return plan;
}

void run_plan_cpu(const float* src, float* dst, const float* table, const RenderPlan& plan,
                  unsigned threads) {
    std::vector<float> work(plan.work_floats);
    for (const RenderPass& pass : plan.passes) {
        switch (pass.kind) {
            case PassKind::Extract:
                parallel_for(pass.image.height, threads, [&](int y) {
                    for (int x = 0; x < pass.image.width; ++x) {
                        dl_extract_at(src, work.data(), x, y, pass.image, &plan.chain, pass.stop,
                                      table);
                    }
                });
                break;
            case PassKind::Blur:
                parallel_for(pass.box.lines, threads,
                             [&](int line) { dl_box_line(work.data(), pass.box, line); });
                break;
            case PassKind::Final:
                parallel_for(pass.image.height, threads, [&](int y) {
                    for (int x = 0; x < pass.image.width; ++x) {
                        dl_chain_at(src, dst, x, y, pass.image, &plan.chain, table, work.data());
                    }
                });
                break;
        }
    }
}

}  // namespace dyeline
