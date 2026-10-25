// CUDA backend: thin __global__ wrappers around the shared kernel headers.

#include <cuda_runtime.h>

#include <string>

#include "dyeline/gpu.h"
#include "dyeline/kernels/chain_kernel.h"

namespace {

// The chain is passed by value (about 1.2 KB, well inside CUDA's 4 KB parameter limit), so
// renders on different streams never share it.
__global__ void dl_chain_kernel(const float* src, float* dst, dl_image_args args, dl_chain chain,
                                const float* table) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < args.width && y < args.height) {
        dl_chain_at(src, dst, x, y, args, &chain, table);
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
                         const dl_chain& chain, const float* table, std::size_t table_floats,
                         bool wait) {
    if (args.width <= 0 || args.height <= 0) {
        return {};
    }
    auto* cuda_stream = static_cast<cudaStream_t>(stream);

    // The table is copied in stream order and freed in stream order after the kernel, so the
    // render still returns without waiting. Devices without stream-ordered allocation fall
    // back to a plain allocation and wait for the kernel before freeing it.
    float* device_table = nullptr;
    bool stream_ordered = true;
    cudaError_t err = cudaSuccess;
    if (table_floats > 0) {
        const std::size_t bytes = table_floats * sizeof(float);
        err = cudaMallocAsync(reinterpret_cast<void**>(&device_table), bytes, cuda_stream);
        if (err != cudaSuccess) {
            cudaGetLastError();  // clear it
            stream_ordered = false;
            err = cudaMalloc(reinterpret_cast<void**>(&device_table), bytes);
        }
        if (err != cudaSuccess) {
            return error_text("Could not allocate the look's tables on the GPU", err);
        }
        // From pageable memory, this returns once `table` has been read, so the caller may
        // free it right after.
        err = cudaMemcpyAsync(device_table, table, bytes, cudaMemcpyHostToDevice, cuda_stream);
        if (err != cudaSuccess) {
            stream_ordered ? cudaFreeAsync(device_table, cuda_stream) : cudaFree(device_table);
            return error_text("Could not upload the look's tables to the GPU", err);
        }
    }
    const dim3 block(16, 16);
    const dim3 grid((args.width + block.x - 1) / block.x, (args.height + block.y - 1) / block.y);
    dl_chain_kernel<<<grid, block, 0, cuda_stream>>>(src, dst, args, chain, device_table);
    err = cudaGetLastError();
    if (device_table != nullptr) {
        if (stream_ordered) {
            cudaFreeAsync(device_table, cuda_stream);
        } else {
            cudaStreamSynchronize(cuda_stream);
            cudaFree(device_table);
        }
    }
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
