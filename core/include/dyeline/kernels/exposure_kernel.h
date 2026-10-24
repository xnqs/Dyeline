// Exposure kernel, shared by every backend. See dl_kernel.h for the rules.
#ifndef DYELINE_KERNELS_EXPOSURE_KERNEL_H
#define DYELINE_KERNELS_EXPOSURE_KERNEL_H

#include "dl_kernel.h"

// Describes one render: a window of `width` x `height` pixels, located at (x0, y0) inside
// the source and destination buffers. Rows are counted in floats, not bytes. The layout is
// plain ints and floats so it is identical in C++, CUDA and Metal.
struct dl_exposure_args {
    int width;
    int height;
    int channels;        // 1 (alpha only), 3 (RGB) or 4 (RGBA)
    int src_row_floats;
    int dst_row_floats;
    int src_x0;
    int src_y0;
    int dst_x0;
    int dst_y0;
    float gain;          // from dyeline::exposure_gain, strength already folded in
};

// Scales RGB by the gain; alpha is untouched.
DL_DEVICE dl_float4 dl_exposure_pixel(dl_float4 p, float gain) {
    return dl_make_float4(p.x * gain, p.y * gain, p.z * gain, p.w);
}

// Processes the pixel at (x, y) of the render window.
DL_DEVICE void dl_exposure_at(DL_GLOBAL const float* src, DL_GLOBAL float* dst, int x, int y,
                              struct dl_exposure_args a) {
    const int si = (a.src_y0 + y) * a.src_row_floats + (a.src_x0 + x) * a.channels;
    const int di = (a.dst_y0 + y) * a.dst_row_floats + (a.dst_x0 + x) * a.channels;
    if (a.channels == 4) {
        dl_float4 p = dl_exposure_pixel(
            dl_make_float4(src[si], src[si + 1], src[si + 2], src[si + 3]), a.gain);
        dst[di] = p.x;
        dst[di + 1] = p.y;
        dst[di + 2] = p.z;
        dst[di + 3] = p.w;
    } else if (a.channels == 3) {
        dl_float4 p = dl_exposure_pixel(
            dl_make_float4(src[si], src[si + 1], src[si + 2], 1.0f), a.gain);
        dst[di] = p.x;
        dst[di + 1] = p.y;
        dst[di + 2] = p.z;
    } else {
        dst[di] = src[si];
    }
}

#endif  // DYELINE_KERNELS_EXPOSURE_KERNEL_H
