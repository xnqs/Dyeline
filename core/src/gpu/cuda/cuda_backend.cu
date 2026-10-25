// CUDA backend: thin __global__ wrappers around the shared kernel headers.

#include <cuda_runtime.h>

#include <string>

#include "dyeline/gpu.h"
#include "dyeline/kernels/chain_kernel.h"

namespace {

// The chain is passed by value (about 1.2 KB, well inside CUDA's 4 KB parameter limit), so
// renders on different streams never share it.
__global__ void dl_chain_kernel(const float* src, float* dst, dl_image_args args, dl_chain chain) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < args.width && y < args.height) {
        dl_chain_at(src, dst, x, y, args, &chain);
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

std::string render_chain(void* stream, const float* src, float* dst, const dl_image_args& args,
                         const dl_chain& chain, bool wait) {
    if (args.width <= 0 || args.height <= 0) {
        return {};
    }
    auto* cuda_stream = static_cast<cudaStream_t>(stream);
    const dim3 block(16, 16);
    const dim3 grid((args.width + block.x - 1) / block.x, (args.height + block.y - 1) / block.y);
    dl_chain_kernel<<<grid, block, 0, cuda_stream>>>(src, dst, args, chain);
    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return error_text("Dyeline kernel launch failed", err);
    }
    if (wait) {
        err = cudaStreamSynchronize(cuda_stream);
        if (err != cudaSuccess) {
            return error_text("Dyeline kernel failed", err);
        }
    }
    return {};
}

}  // namespace dyeline::cuda
