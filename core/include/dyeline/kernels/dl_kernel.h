// Portability layer for Dyeline kernels.
//
// Tool math is written once, in headers that include this file, and compiled four ways:
// as plain C++ (the CPU path and the reference every GPU result is tested against), as CUDA,
// as Metal Shading Language and, later, as OpenCL C. Kernel headers may use only what this
// file defines plus basic C arithmetic: no standard library, no exceptions, no templates.
#ifndef DYELINE_KERNELS_DL_KERNEL_H
#define DYELINE_KERNELS_DL_KERNEL_H

#if defined(__CUDACC__)

#define DL_DEVICE __device__ __forceinline__
#define DL_GLOBAL
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) make_float4((x), (y), (z), (w))

#elif defined(__METAL_VERSION__)

#include <metal_stdlib>
#define DL_DEVICE inline
#define DL_GLOBAL device
typedef float4 dl_float4;
#define dl_make_float4(x, y, z, w) float4((x), (y), (z), (w))

#else  // C++ (CPU)

#define DL_DEVICE inline
#define DL_GLOBAL
struct dl_float4 {
    float x, y, z, w;
};
inline dl_float4 dl_make_float4(float x, float y, float z, float w) {
    dl_float4 v = {x, y, z, w};
    return v;
}

#endif

#endif  // DYELINE_KERNELS_DL_KERNEL_H
