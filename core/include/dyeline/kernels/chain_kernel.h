// The fused per-pixel pass, shared by every backend. See dl_kernel.h for the rules.
//
// A look's consecutive per-pixel tools run in one pass: each pixel is read once, walks the
// chain's op list and is written once, however many tools the chain has. The host turns each
// tool into an op (core/src/chain.cpp), working out anything that is constant per frame.
// Tools that need more data than an op's parameters, such as a LUT's table, read it from one
// shared `table` buffer at an offset their op names.
//
// Spatial tools (Diffusion, Halation) need their neighbors, so a look that has them renders
// in several passes (planned in core/src/render.cpp). For each spatial tool, an extract pass
// runs the chain up to that tool over a region wider than the window and writes what the tool
// spreads to a `work` buffer; separable box blurs (dl_box_line) blur it there; and the final
// pass, running the whole chain, reads the blurred image back where the tool sits.
#ifndef DYELINE_KERNELS_CHAIN_KERNEL_H
#define DYELINE_KERNELS_CHAIN_KERNEL_H

#include "dl_kernel.h"
#include "tools/contrast.h"
#include "tools/diffusion.h"
#include "tools/exposure.h"
#include "tools/grain.h"
#include "tools/halation.h"
#include "tools/highlight_shoulder.h"
#include "tools/lens_vignette.h"
#include "tools/lut.h"
#include "tools/matrix.h"
#include "tools/s_curve.h"
#include "tools/shadows_highlights.h"
#include "tools/three_way.h"

// At most this many tools run in one pass, and each gets this many floats of parameters.
#define DL_MAX_OPS 16
#define DL_OP_PARAMS 16

// Op codes. Saved looks name tools by string, so these may be renumbered freely.
#define DL_OP_EXPOSURE 1
#define DL_OP_CONTRAST 2
#define DL_OP_S_CURVE 3
#define DL_OP_MATRIX 4
#define DL_OP_SHADOWS_HIGHLIGHTS 5
#define DL_OP_HIGHLIGHT_SHOULDER 6
#define DL_OP_THREE_WAY 7
#define DL_OP_LUT 8
#define DL_OP_LENS_VIGNETTE 9
#define DL_OP_GRAIN 10
#define DL_OP_DIFFUSION 11
#define DL_OP_HALATION 12

// Spatial ops keep their blur size in their last parameter, for the host's render plan.
#define DL_OP_BLUR_SIZE (DL_OP_PARAMS - 1)

// One tool in the pass. `strength` blends between the tool's input and output. A spatial
// op's `ip` locate its blurred image in the work buffer: ip[0] is where it starts, in floats,
// ip[1] and ip[2] the pixel it begins at, and ip[3] its width. Each pixel has 3 floats.
struct dl_op {
    int tool;
    float strength;
    int ip[4];
    float p[DL_OP_PARAMS];
};

// The whole pass. `strength` blends between the untouched image and the chain's result.
struct dl_chain {
    int count;
    float strength;
    struct dl_op ops[DL_MAX_OPS];
};

// Describes one render pass: a window of `width` x `height` pixels. The source is read at
// (src_x0, src_y0) onward, clamped to its `src_width` x `src_height` pixels so a pass may
// reach past the source's edges; the destination is written at (dst_x0, dst_y0) onward. Rows
// are counted in floats, not bytes. The rest places the window in the frame for tools that
// depend on position (see dl_place). Every field is a 4-byte int or float, so the layout is
// identical in every backend.
struct dl_image_args {
    int width;
    int height;
    int channels;        // 1 (alpha only), 3 (RGB) or 4 (RGBA)
    int premultiplied;   // 1 when RGB is premultiplied by alpha
    int src_row_floats;
    int dst_row_floats;
    int src_x0;
    int src_y0;
    int src_width;
    int src_height;
    int dst_x0;
    int dst_y0;
    int frame_x0;          // the window's first pixel, in the host's pixel coordinates
    int frame_y0;
    float canonical_x;     // canonical units per pixel: pixel aspect / render scale
    float canonical_y;     // 1 / render scale
    float frame_left;      // the frame's edges, in canonical coordinates
    float frame_bottom;
    float frame_right;
    float frame_top;
    int seed;              // differs from frame to frame
    int work_offset;       // extract passes: where they write in the work buffer, in floats
};

// One box blur over lines of RGB pixels in the work buffer. Line `i` reads from
// in_offset + i * in_cross, one pixel every in_step floats, and writes `length` pixels from
// out_offset + i * out_cross, every out_step floats. Output pixel k averages input pixels k
// to k + 2 * radius, so the input is 2 * radius pixels longer than the output.
struct dl_box_args {
    int in_offset;
    int in_step;
    int in_cross;
    int out_offset;
    int out_step;
    int out_cross;
    int lines;
    int length;
    int radius;
};

DL_DEVICE int dl_is_spatial(int tool) {
    return tool == DL_OP_DIFFUSION || tool == DL_OP_HALATION;
}

DL_DEVICE dl_float4 dl_mix(dl_float4 a, dl_float4 b, float t) {
    return dl_make_float4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                          a.w);
}

// A spatial op's blurred image at this pixel.
DL_DEVICE dl_float4 dl_blurred_at(DL_CONSTANT const struct dl_op* op, DL_GLOBAL const float* work,
                                  struct dl_place at) {
    const int i = op->ip[0] + ((at.py - op->ip[2]) * op->ip[3] + (at.px - op->ip[1])) * 3;
    return dl_make_float4(work[i], work[i + 1], work[i + 2], 0.0f);
}

DL_DEVICE dl_float4 dl_run_op(dl_float4 c, DL_CONSTANT const struct dl_op* op,
                             DL_GLOBAL const float* table, DL_GLOBAL const float* work,
                             struct dl_place at) {
    switch (op->tool) {
        case DL_OP_EXPOSURE:
            return dl_tool_exposure(c, op->p);
        case DL_OP_CONTRAST:
            return dl_tool_contrast(c, op->p);
        case DL_OP_S_CURVE:
            return dl_tool_s_curve(c, op->p);
        case DL_OP_MATRIX:
            return dl_tool_matrix(c, op->p);
        case DL_OP_SHADOWS_HIGHLIGHTS:
            return dl_tool_shadows_highlights(c, op->p);
        case DL_OP_HIGHLIGHT_SHOULDER:
            return dl_tool_highlight_shoulder(c, op->p);
        case DL_OP_THREE_WAY:
            return dl_tool_three_way(c, op->p);
        case DL_OP_LUT:
            return dl_tool_lut(c, op->p, table);
        case DL_OP_LENS_VIGNETTE:
            return dl_tool_lens_vignette(c, op->p, at);
        case DL_OP_GRAIN:
            return dl_tool_grain(c, op->p, at);
        case DL_OP_DIFFUSION:
            return dl_tool_diffusion_combine(c, op->p, dl_blurred_at(op, work, at));
        case DL_OP_HALATION:
            return dl_tool_halation_combine(c, op->p, dl_blurred_at(op, work, at));
        default:
            return c;
    }
}

// What a spatial op spreads, from its input color.
DL_DEVICE dl_float4 dl_extract_op(dl_float4 c, DL_CONSTANT const struct dl_op* op) {
    switch (op->tool) {
        case DL_OP_DIFFUSION:
            return dl_tool_diffusion_extract(c, op->p);
        case DL_OP_HALATION:
            return dl_tool_halation_extract(c, op->p);
        default:
            return dl_make_float4(0.0f, 0.0f, 0.0f, c.w);
    }
}

// Runs the first `count` ops of the chain on one straight-alpha color. Alpha is never
// changed.
DL_DEVICE dl_float4 dl_run_ops(dl_float4 c, DL_CONSTANT const struct dl_chain* chain, int count,
                               DL_GLOBAL const float* table, DL_GLOBAL const float* work,
                               struct dl_place at) {
    for (int i = 0; i < count; ++i) {
        DL_CONSTANT const struct dl_op* op = &chain->ops[i];
        c = dl_mix(c, dl_run_op(c, op, table, work, at), op->strength);
    }
    return c;
}

// Reads the source pixel under window pixel (x, y), as straight color, and says where it is.
// Tools see straight (unpremultiplied) color, so a non-linear tool treats a soft edge like the
// solid pixels next to it.
DL_DEVICE dl_float4 dl_read_source(DL_GLOBAL const float* src, int x, int y, struct dl_image_args a,
                                   DL_PRIVATE struct dl_place* at,
                                   DL_PRIVATE int* unpremultiplied) {
    int sx = a.src_x0 + x;
    int sy = a.src_y0 + y;
    sx = sx < 0 ? 0 : (sx >= a.src_width ? a.src_width - 1 : sx);
    sy = sy < 0 ? 0 : (sy >= a.src_height ? a.src_height - 1 : sy);
    const int si = sy * a.src_row_floats + sx * a.channels;
    dl_float4 c = dl_make_float4(src[si], src[si + 1], src[si + 2],
                                 a.channels == 4 ? src[si + 3] : 1.0f);
    *unpremultiplied = a.channels == 4 && a.premultiplied && c.w > 0.0f;
    if (*unpremultiplied) {
        c = dl_make_float4(c.x / c.w, c.y / c.w, c.z / c.w, c.w);
    }
    at->px = a.frame_x0 + x;
    at->py = a.frame_y0 + y;
    at->x = ((float)at->px + 0.5f) * a.canonical_x;
    at->y = ((float)at->py + 0.5f) * a.canonical_y;
    at->frame_left = a.frame_left;
    at->frame_bottom = a.frame_bottom;
    at->frame_right = a.frame_right;
    at->frame_top = a.frame_top;
    at->seed = a.seed;
    return c;
}

// The final pass: processes the pixel at (x, y) of the render window with the whole chain.
DL_DEVICE void dl_chain_at(DL_GLOBAL const float* src, DL_GLOBAL float* dst, int x, int y,
                           struct dl_image_args a, DL_CONSTANT const struct dl_chain* chain,
                           DL_GLOBAL const float* table, DL_GLOBAL const float* work) {
    const int di = (a.dst_y0 + y) * a.dst_row_floats + (a.dst_x0 + x) * a.channels;
    if (a.channels == 1) {
        int sx = a.src_x0 + x;
        int sy = a.src_y0 + y;
        sx = sx < 0 ? 0 : (sx >= a.src_width ? a.src_width - 1 : sx);
        sy = sy < 0 ? 0 : (sy >= a.src_height ? a.src_height - 1 : sy);
        dst[di] = src[sy * a.src_row_floats + sx];
        return;
    }
    struct dl_place at;
    int unpremultiplied = 0;
    const dl_float4 original = dl_read_source(src, x, y, a, &at, &unpremultiplied);
    dl_float4 c = dl_run_ops(original, chain, chain->count, table, work, at);
    c = dl_mix(original, c, chain->strength);
    if (unpremultiplied) {
        c = dl_make_float4(c.x * c.w, c.y * c.w, c.z * c.w, c.w);
    }
    dst[di] = c.x;
    dst[di + 1] = c.y;
    dst[di + 2] = c.z;
    if (a.channels == 4) {
        dst[di + 3] = c.w;
    }
}

// An extract pass: runs the chain up to op `stop`, a spatial op, at pixel (x, y) of the pass's
// region and writes what that op spreads, weighted by alpha, to the work buffer.
DL_DEVICE void dl_extract_at(DL_GLOBAL const float* src, DL_GLOBAL float* work, int x, int y,
                             struct dl_image_args a, DL_CONSTANT const struct dl_chain* chain,
                             int stop, DL_GLOBAL const float* table) {
    const int wi = a.work_offset + (y * a.width + x) * 3;
    if (a.channels == 1) {
        work[wi] = 0.0f;
        work[wi + 1] = 0.0f;
        work[wi + 2] = 0.0f;
        return;
    }
    struct dl_place at;
    int unpremultiplied = 0;
    dl_float4 c = dl_read_source(src, x, y, a, &at, &unpremultiplied);
    c = dl_run_ops(c, chain, stop, table, work, at);
    const dl_float4 e = dl_extract_op(c, &chain->ops[stop]);
    work[wi] = e.x * c.w;
    work[wi + 1] = e.y * c.w;
    work[wi + 2] = e.z * c.w;
}

// One line of a box blur (see dl_box_args). The running sum is restarted every 32 pixels,
// which bounds its rounding error, and keeps the result identical on every backend.
DL_DEVICE void dl_box_line(DL_GLOBAL float* work, struct dl_box_args b, int line) {
    const int in0 = b.in_offset + line * b.in_cross;
    const int out0 = b.out_offset + line * b.out_cross;
    const int taps = 2 * b.radius + 1;
    const float scale = 1.0f / (float)taps;
    float s0 = 0.0f;
    float s1 = 0.0f;
    float s2 = 0.0f;
    for (int k = 0; k < b.length; ++k) {
        if (k % 32 == 0) {
            s0 = 0.0f;
            s1 = 0.0f;
            s2 = 0.0f;
            for (int t = 0; t < taps; ++t) {
                const int i = in0 + (k + t) * b.in_step;
                s0 += work[i];
                s1 += work[i + 1];
                s2 += work[i + 2];
            }
        } else {
            const int add = in0 + (k + taps - 1) * b.in_step;
            const int sub = in0 + (k - 1) * b.in_step;
            s0 += work[add] - work[sub];
            s1 += work[add + 1] - work[sub + 1];
            s2 += work[add + 2] - work[sub + 2];
        }
        const int o = out0 + k * b.out_step;
        work[o] = s0 * scale;
        work[o + 1] = s1 * scale;
        work[o + 2] = s2 * scale;
    }
}

#endif  // DYELINE_KERNELS_CHAIN_KERNEL_H
