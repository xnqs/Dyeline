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

// The pixels whose centers are inside the frame.
PixelRect frame_pixels(const RenderGeometry& g) {
    return {static_cast<int>(std::ceil(g.frame_left / g.canonical_x - 0.5f)),
            static_cast<int>(std::ceil(g.frame_bottom / g.canonical_y - 0.5f)),
            static_cast<int>(std::floor(g.frame_right / g.canonical_x - 0.5f)) + 1,
            static_cast<int>(std::floor(g.frame_top / g.canonical_y - 0.5f)) + 1};
}

// The pixels a warp op reads to render `region`: every channel's and tap's source position
// for points along the region's edges and on a grid inside it, plus the bilinear filter's
// reach and some slack for the spacing of the points. Only pixels inside the frame are ever
// read (see dl_stage_tap), so the result is clipped to the frame.
PixelRect warp_region(const dl_op& op, const PixelRect& region, const RenderGeometry& g) {
    dl_place at{};
    at.frame_left = g.frame_left;
    at.frame_bottom = g.frame_bottom;
    at.frame_right = g.frame_right;
    at.frame_top = g.frame_top;
    double x_min = HUGE_VAL;
    double y_min = HUGE_VAL;
    double x_max = -HUGE_VAL;
    double y_max = -HUGE_VAL;
    const auto visit = [&](int px, int py) {
        at.px = px;
        at.py = py;
        at.x = (static_cast<float>(px) + 0.5f) * g.canonical_x;
        at.y = (static_cast<float>(py) + 0.5f) * g.canonical_y;
        for (int channel = 0; channel < 3; ++channel) {
            const int taps = dl_warp_taps(&op, channel);
            for (int tap = 0; tap < taps; ++tap) {
                float sx = 0.0f;
                float sy = 0.0f;
                dl_warp_source(&op, at, channel, tap, &sx, &sy);
                const double fx = sx / g.canonical_x - 0.5;
                const double fy = sy / g.canonical_y - 0.5;
                x_min = std::min(x_min, fx);
                x_max = std::max(x_max, fx);
                y_min = std::min(y_min, fy);
                y_max = std::max(y_max, fy);
            }
        }
    };
    constexpr int kEdgeStep = 4;
    constexpr int kGrid = 8;
    const int last_x = region.x2 - 1;
    const int last_y = region.y2 - 1;
    for (int x = region.x1;; x = std::min(x + kEdgeStep, last_x)) {
        visit(x, region.y1);
        visit(x, last_y);
        if (x == last_x) break;
    }
    for (int y = region.y1;; y = std::min(y + kEdgeStep, last_y)) {
        visit(region.x1, y);
        visit(last_x, y);
        if (y == last_y) break;
    }
    for (int j = 1; j < kGrid; ++j) {
        for (int i = 1; i < kGrid; ++i) {
            visit(region.x1 + (last_x - region.x1) * i / kGrid,
                  region.y1 + (last_y - region.y1) * j / kGrid);
        }
    }
    // Sampling finds the bounds when the warp keeps points in order along each ray from the
    // lens's center. A strong pincushion with Edges can fold back on itself far from the
    // center; then any point within the farthest distance the ray reaches may be read.
    if (op.tool == DL_OP_LENS_DISTORTION) {
        float cx = 0.0f;
        float cy = 0.0f;
        float half_diagonal = 0.0f;
        dl_lens_center(op.p[3], op.p[4], at, &cx, &cy, &half_diagonal);
        const double left = (region.x1 + 0.5) * g.canonical_x;
        const double right = (last_x + 0.5) * g.canonical_x;
        const double bottom = (region.y1 + 0.5) * g.canonical_y;
        const double top = (last_y + 0.5) * g.canonical_y;
        const double nx = std::clamp(static_cast<double>(cx), left, right) - cx;
        const double ny = std::clamp(static_cast<double>(cy), bottom, top) - cy;
        const double fx = std::max(std::fabs(left - cx), std::fabs(right - cx));
        const double fy = std::max(std::fabs(bottom - cy), std::fabs(top - cy));
        const double r_near = std::hypot(nx, ny) / half_diagonal;
        const double r_far = std::hypot(fx, fy) / half_diagonal;
        // How far a ray reaches at distance r, in half diagonals.
        const auto reach = [&](double r) {
            const double f = (1.0 + op.p[0] * r * r + op.p[1] * r * r * r * r) * op.p[2];
            return r * (1.0 + (f - 1.0) * op.strength);
        };
        constexpr int kSteps = 256;
        bool folds = false;
        double farthest = 0.0;
        double previous = reach(r_near);
        for (int i = 0; i <= kSteps; ++i) {
            const double value = reach(r_near + (r_far - r_near) * i / kSteps);
            folds = folds || value < previous || value < 0.0;
            farthest = std::max(farthest, std::fabs(value));
            previous = value;
        }
        if (folds) {
            const double radius = farthest * half_diagonal;
            x_min = std::min(x_min, (cx - radius) / g.canonical_x - 0.5);
            x_max = std::max(x_max, (cx + radius) / g.canonical_x - 0.5);
            y_min = std::min(y_min, (cy - radius) / g.canonical_y - 0.5);
            y_max = std::max(y_max, (cy + radius) / g.canonical_y - 0.5);
        }
    }
    constexpr int kSlack = 4;
    const PixelRect frame = frame_pixels(g);
    const auto clip = [](double v, int lo, int hi) {
        return static_cast<int>(std::clamp(v, static_cast<double>(lo), static_cast<double>(hi)));
    };
    PixelRect r;
    r.x1 = clip(std::floor(x_min) - kSlack, frame.x1, frame.x2 - 1);
    r.y1 = clip(std::floor(y_min) - kSlack, frame.y1, frame.y2 - 1);
    r.x2 = clip(std::floor(x_max) + 2 + kSlack, r.x1 + 1, frame.x2);
    r.y2 = clip(std::floor(y_max) + 2 + kSlack, r.y1 + 1, frame.y2);
    return r;
}

// Where each pass of a plan renders, worked out backward from the window: each spatial op's
// blurred image covers every pixel later passes evaluate it at, its extract pass that plus
// its blur's reach, and each warp's input image whatever its later passes read through it.
struct Regions {
    std::vector<PixelRect> blurred;  // by op index
    std::vector<PixelRect> extract;
    std::vector<PixelRect> stage;
    PixelRect source;                // what the passes read of the source
};

Regions plan_regions(const dl_chain& chain, const RenderGeometry& g) {
    Regions regions;
    const std::size_t n = static_cast<std::size_t>(std::max(chain.count, 0));
    regions.blurred.resize(n);
    regions.extract.resize(n);
    regions.stage.resize(n);
    PixelRect needed = g.window;
    for (int i = chain.count; i-- > 0;) {
        const dl_op& op = chain.ops[i];
        if (dl_is_spatial(op.tool)) {
            const Radius r = blur_radius(op, g);
            regions.blurred[i] = needed;
            needed = grow(needed, kBoxPasses * r.x, kBoxPasses * r.y);
            regions.extract[i] = needed;
        } else if (dl_is_warp(op.tool) && needed.width() > 0 && needed.height() > 0) {
            needed = warp_region(op, needed, g);
            regions.stage[i] = needed;
        }
    }
    // The final pass also reads the source under the window, to blend by the look's strength.
    regions.source = {std::min(needed.x1, g.window.x1), std::min(needed.y1, g.window.y1),
                      std::max(needed.x2, g.window.x2), std::max(needed.y2, g.window.y2)};
    return regions;
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

PixelRect source_region(const dl_chain& chain, const RenderGeometry& g) {
    return plan_regions(chain, g).source;
}

RenderPlan plan_render(const dl_chain& chain, const RenderGeometry& g) {
    RenderPlan plan;
    plan.chain = chain;
    const Regions regions = plan_regions(chain, g);

    // Work memory: two scratch images the size of the largest extract region, then each
    // spatial op's blurred image and each warp's input image.
    std::size_t scratch = 0;
    for (int i = 0; i < chain.count; ++i) {
        if (dl_is_spatial(chain.ops[i].tool)) {
            scratch = std::max(scratch, rgb_floats(regions.extract[i]));
        }
    }
    const std::size_t a = 0;
    const std::size_t b = scratch;
    std::size_t next = 2 * scratch;
    std::vector<std::size_t> stage_offset(static_cast<std::size_t>(chain.count), 0);
    for (int i = 0; i < chain.count; ++i) {
        dl_op& op = plan.chain.ops[i];
        if (dl_is_spatial(op.tool)) {
            const PixelRect& r = regions.blurred[i];
            op.ip[0] = static_cast<int>(next);
            op.ip[1] = r.x1;
            op.ip[2] = r.y1;
            op.ip[3] = r.width();
            next += rgb_floats(r);
        } else if (dl_is_warp(op.tool)) {
            stage_offset[static_cast<std::size_t>(i)] = next;
            next += rgb_floats(regions.stage[i]) / 3 * 4;
        }
    }
    plan.work_floats = next;

    // Passes after a warp read its input image through it.
    int stage = -1;
    const auto args_for = [&](const PixelRect& region) {
        dl_image_args args = pass_args(g, region);
        if (stage >= 0) {
            const PixelRect& r = regions.stage[stage];
            args.stage_op = stage + 1;
            args.stage_offset = static_cast<int>(stage_offset[static_cast<std::size_t>(stage)]);
            args.stage_x0 = r.x1;
            args.stage_y0 = r.y1;
            args.stage_width = r.width();
            args.stage_height = r.height();
        }
        return args;
    };

    for (int i = 0; i < chain.count; ++i) {
        const int tool = chain.ops[i].tool;
        if (dl_is_warp(tool)) {
            RenderPass st;
            st.kind = PassKind::Extract;
            st.image = args_for(regions.stage[i]);
            st.image.work_offset = static_cast<int>(stage_offset[static_cast<std::size_t>(i)]);
            st.stop = i;
            plan.passes.push_back(st);
            stage = i;
            continue;
        }
        if (!dl_is_spatial(tool)) {
            continue;
        }
        const PixelRect& extract = regions.extract[i];
        RenderPass ex;
        ex.kind = PassKind::Extract;
        ex.image = args_for(extract);
        ex.image.work_offset = static_cast<int>(a);
        ex.stop = i;
        plan.passes.push_back(ex);

        // Three horizontal passes, A -> B -> A -> B, each narrowing the image by 2r; then
        // three vertical ones, B -> A -> B -> the op's blurred image.
        const Radius radius = blur_radius(chain.ops[i], g);
        const int rows = extract.height();
        int width = extract.width();
        std::size_t from = a;
        std::size_t to = b;
        for (int k = 0; k < kBoxPasses; ++k) {
            plan.passes.push_back(blur_pass(true, from, width, to, width - 2 * radius.x, rows,
                                            width - 2 * radius.x, radius.x));
            width -= 2 * radius.x;
            std::swap(from, to);
        }
        int height = rows;
        for (int k = 0; k < kBoxPasses; ++k) {
            const std::size_t target =
                k == kBoxPasses - 1 ? static_cast<std::size_t>(plan.chain.ops[i].ip[0]) : to;
            plan.passes.push_back(
                blur_pass(false, from, width, target, width, width, height - 2 * radius.y, radius.y));
            height -= 2 * radius.y;
            std::swap(from, to);
        }
    }

    RenderPass final_pass;
    final_pass.kind = PassKind::Final;
    final_pass.image = args_for(g.window);
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
