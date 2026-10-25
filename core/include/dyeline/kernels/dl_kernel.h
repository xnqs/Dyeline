// Portability layer for Dyeline kernels.
//
// Tool math is written once, in headers that include this file, and compiled four ways:
// as plain C++ (the CPU path and the reference every GPU result is tested against), as CUDA,
// as Metal Shading Language and as OpenCL C. Kernel headers may use only what this
// file defines plus basic C arithmetic: no standard library, no exceptions, no templates.
// DL_GLOBAL marks image buffers and table data, DL_CONSTANT the small read-only chain
// description, DL_PRIVATE pointers to a function's own variables. The dl_
// math functions are single precision on every backend.
#ifndef DYELINE_KERNELS_DL_KERNEL_H
#define DYELINE_KERNELS_DL_KERNEL_H

#if defined(__CUDACC__)

#define DL_DEVICE __device__ __forceinline__
#define DL_GLOBAL
#define DL_CONSTANT
#define DL_PRIVATE
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) make_float4((x), (y), (z), (w))
#define dl_pow(x, y) powf((x), (y))
#define dl_exp2(x) exp2f(x)
#define dl_log2(x) log2f(x)
#define dl_fabs(x) fabsf(x)
#define dl_sqrt(x) sqrtf(x)
#define dl_floor(x) floorf(x)
#define dl_atan2(y, x) atan2f((y), (x))
#define dl_cos(x) cosf(x)
#define dl_sin(x) sinf(x)

#elif defined(__METAL_VERSION__)

#include <metal_stdlib>
#define DL_DEVICE inline
#define DL_GLOBAL device
#define DL_CONSTANT constant
#define DL_PRIVATE thread
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) float4((x), (y), (z), (w))
// The library is compiled without fast math (metal_backend.mm), so these are the precise forms.
#define dl_pow(x, y) metal::pow((x), (y))
#define dl_exp2(x) metal::exp2(x)
#define dl_log2(x) metal::log2(x)
#define dl_fabs(x) metal::fabs(x)
#define dl_sqrt(x) metal::sqrt(x)
#define dl_floor(x) metal::floor(x)
#define dl_atan2(y, x) metal::atan2((y), (x))
#define dl_cos(x) metal::cos(x)
#define dl_sin(x) metal::sin(x)

#elif defined(__OPENCL_VERSION__)

// OpenCL C 1.2: plain functions (no inline), which every OpenCL compiler accepts.
#define DL_DEVICE
#define DL_GLOBAL __global
#define DL_CONSTANT __constant
#define DL_PRIVATE
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) ((float4)((x), (y), (z), (w)))
#define dl_pow(x, y) pow((x), (y))
#define dl_exp2(x) exp2(x)
#define dl_log2(x) log2(x)
#define dl_fabs(x) fabs(x)
#define dl_sqrt(x) sqrt(x)
#define dl_floor(x) floor(x)
#define dl_atan2(y, x) atan2((y), (x))
#define dl_cos(x) cos(x)
#define dl_sin(x) sin(x)

#else  // C++ (CPU)

#include <cmath>

#define DL_DEVICE inline
#define DL_GLOBAL
#define DL_CONSTANT
#define DL_PRIVATE
struct dl_float4 {
    float x, y, z, w;
};
inline dl_float4 dl_make_float4(float x, float y, float z, float w) {
    dl_float4 v = {x, y, z, w};
    return v;
}
inline float dl_pow(float x, float y) { return std::pow(x, y); }
inline float dl_exp2(float x) { return std::exp2(x); }
inline float dl_log2(float x) { return std::log2(x); }
inline float dl_fabs(float x) { return std::fabs(x); }
inline float dl_sqrt(float x) { return std::sqrt(x); }
inline float dl_floor(float x) { return std::floor(x); }
inline float dl_atan2(float y, float x) { return std::atan2(y, x); }
inline float dl_cos(float x) { return std::cos(x); }
inline float dl_sin(float x) { return std::sin(x); }

#endif

// Shared by several tools.

DL_DEVICE float dl_smoothstep(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

// Where a pixel is, for tools that depend on position (Lens Vignette, Grain). Positions are in
// the host's canonical coordinates: full-resolution pixels, made square by the pixel aspect
// ratio, with y up. So a look renders the same at any render scale.
struct dl_place {
    int px;   // the pixel, in the host's pixel coordinates
    int py;
    float x;  // the pixel's center, in canonical coordinates
    float y;
    float frame_left;  // the frame's edges
    float frame_bottom;
    float frame_right;
    float frame_top;
    int seed;  // differs from frame to frame, for grain
};

#endif  // DYELINE_KERNELS_DL_KERNEL_H
