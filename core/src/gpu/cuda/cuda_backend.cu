// CUDA backend: thin __global__ wrappers around the shared kernel headers.

#include <cuda_runtime.h>

#include <string>

#include "dyeline/gpu.h"
#include "dyeline/kernels/exposure_kernel.h"

namespace {

__global__ void dl_exposure_kernel(const float* src, float* dst, dl_exposure_args args) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < args.width && y < args.height) {
        dl_exposure_at(src, dst, x, y, args);
    }
}

std::string error_text(const char* what, cudaError_t err) {
    return std::string(what) + ": " + cudaGetErrorString(err);
}

}  // namespace

namespace dyeline::cuda {

bool available() {
    return true;
}

std::string exposure(void* stream, const float* src, float* dst, const dl_exposure_args& args,
                     bool wait) {
    if (args.width <= 0 || args.height <= 0) {
        return {};
    }
    auto* cuda_stream = static_cast<cudaStream_t>(stream);
    const dim3 block(16, 16);
    const dim3 grid((args.width + block.x - 1) / block.x, (args.height + block.y - 1) / block.y);
    dl_exposure_kernel<<<grid, block, 0, cuda_stream>>>(src, dst, args);
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return error_text("Exposure kernel launch failed", err);
    }
    if (wait) {
        err = cudaStreamSynchronize(cuda_stream);
        if (err != cudaSuccess) {
            return error_text("Exposure kernel failed", err);
        }
    }
    return {};
}

}  // namespace dyeline::cuda
