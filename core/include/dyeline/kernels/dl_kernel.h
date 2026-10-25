// Portability layer for Dyeline kernels.
//
// Tool math is written once, in headers that include this file, and compiled four ways:
// as plain C++ (the CPU path and the reference every GPU result is tested against), as CUDA,
// as Metal Shading Language and as OpenCL C. Kernel headers may use only what this
// file defines plus basic C arithmetic: no standard library, no exceptions, no templates.
// DL_GLOBAL marks image buffers and table data, DL_CONSTANT the small read-only chain
// description. The dl_
// math functions are single precision on every backend.
#ifndef DYELINE_KERNELS_DL_KERNEL_H
#define DYELINE_KERNELS_DL_KERNEL_H

#if defined(__CUDACC__)

#define DL_DEVICE __device__ __forceinline__
#define DL_GLOBAL
#define DL_CONSTANT
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) make_float4((x), (y), (z), (w))
#define dl_pow(x, y) powf((x), (y))
#define dl_exp2(x) exp2f(x)
#define dl_log2(x) log2f(x)
#define dl_fabs(x) fabsf(x)

#elif defined(__METAL_VERSION__)

#include <metal_stdlib>
#define DL_DEVICE inline
#define DL_GLOBAL device
#define DL_CONSTANT constant
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) float4((x), (y), (z), (w))
// The library is compiled without fast math (metal_backend.mm), so these are the precise forms.
#define dl_pow(x, y) metal::pow((x), (y))
#define dl_exp2(x) metal::exp2(x)
#define dl_log2(x) metal::log2(x)
#define dl_fabs(x) metal::fabs(x)

#elif defined(__OPENCL_VERSION__)

// OpenCL C 1.2: plain functions (no inline), which every OpenCL compiler accepts.
#define DL_DEVICE
#define DL_GLOBAL __global
#define DL_CONSTANT __constant
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) ((float4)((x), (y), (z), (w)))
#define dl_pow(x, y) pow((x), (y))
#define dl_exp2(x) exp2(x)
#define dl_log2(x) log2(x)
#define dl_fabs(x) fabs(x)

#else  // C++ (CPU)

#include <cmath>

#define DL_DEVICE inline
#define DL_GLOBAL
#define DL_CONSTANT
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

#endif

#endif  // DYELINE_KERNELS_DL_KERNEL_H
