// The fused per-pixel pass, shared by every backend. See dl_kernel.h for the rules.
//
// A look's consecutive per-pixel tools run in one pass: each pixel is read once, walks the
// chain's op list and is written once, however many tools the chain has. The host turns each
// tool into an op (core/src/chain.cpp), working out anything that is constant per frame.
#ifndef DYELINE_KERNELS_CHAIN_KERNEL_H
#define DYELINE_KERNELS_CHAIN_KERNEL_H

#include "dl_kernel.h"
#include "tools/contrast.h"
#include "tools/exposure.h"
#include "tools/highlight_shoulder.h"
#include "tools/matrix.h"
#include "tools/s_curve.h"
#include "tools/shadows_highlights.h"

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

// One tool in the pass. `strength` blends between the tool's input and output.
struct dl_op {
    int tool;
    float strength;
    float p[DL_OP_PARAMS];
};

// The whole pass. `strength` blends between the untouched image and the chain's result.
struct dl_chain {
    int count;
    float strength;
    struct dl_op ops[DL_MAX_OPS];
};

// Describes one render: a window of `width` x `height` pixels, located at (x0, y0) inside
// the source and destination buffers. Rows are counted in floats, not bytes. The layout is
// plain ints so it is identical in every backend.
struct dl_image_args {
    int width;
    int height;
    int channels;        // 1 (alpha only), 3 (RGB) or 4 (RGBA)
    int premultiplied;   // 1 when RGB is premultiplied by alpha
    int src_row_floats;
    int dst_row_floats;
    int src_x0;
    int src_y0;
    int dst_x0;
    int dst_y0;
};

DL_DEVICE dl_float4 dl_mix(dl_float4 a, dl_float4 b, float t) {
    return dl_make_float4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                          a.w);
}

DL_DEVICE dl_float4 dl_run_op(dl_float4 c, DL_CONSTANT const struct dl_op* op) {
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
        default:
            return c;
    }
}

// Runs the chain on one straight-alpha color. Alpha is never changed.
DL_DEVICE dl_float4 dl_run_chain(dl_float4 c, DL_CONSTANT const struct dl_chain* chain) {
    const dl_float4 original = c;
    for (int i = 0; i < chain->count; ++i) {
        DL_CONSTANT const struct dl_op* op = &chain->ops[i];
        c = dl_mix(c, dl_run_op(c, op), op->strength);
    }
    return dl_mix(original, c, chain->strength);
}

// Processes the pixel at (x, y) of the render window. Tools see straight (unpremultiplied)
// color, so a non-linear tool treats a soft edge like the solid pixels next to it.
DL_DEVICE void dl_chain_at(DL_GLOBAL const float* src, DL_GLOBAL float* dst, int x, int y,
                           struct dl_image_args a, DL_CONSTANT const struct dl_chain* chain) {
    const int si = (a.src_y0 + y) * a.src_row_floats + (a.src_x0 + x) * a.channels;
    const int di = (a.dst_y0 + y) * a.dst_row_floats + (a.dst_x0 + x) * a.channels;
    if (a.channels == 1) {
        dst[di] = src[si];
        return;
    }
    dl_float4 c = dl_make_float4(src[si], src[si + 1], src[si + 2],
                                 a.channels == 4 ? src[si + 3] : 1.0f);
    const int unpremultiply = a.channels == 4 && a.premultiplied && c.w > 0.0f;
    if (unpremultiply) {
        c = dl_make_float4(c.x / c.w, c.y / c.w, c.z / c.w, c.w);
    }
    c = dl_run_chain(c, chain);
    if (unpremultiply) {
        c = dl_make_float4(c.x * c.w, c.y * c.w, c.z * c.w, c.w);
    }
    dst[di] = c.x;
    dst[di + 1] = c.y;
    dst[di + 2] = c.z;
    if (a.channels == 4) {
        dst[di + 3] = c.w;
    }
}

#endif  // DYELINE_KERNELS_CHAIN_KERNEL_H
