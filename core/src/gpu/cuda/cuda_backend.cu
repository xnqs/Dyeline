// CUDA backend: thin __global__ wrappers around the shared kernel headers, launched in the
// order a render plan lists them.

#include <cuda_runtime.h>

#include <string>

#include "dyeline/gpu.h"
#include "dyeline/kernels/chain_kernel.h"

namespace {

// The chain is passed by value (about 1.5 KB, well inside CUDA's 4 KB parameter limit), so
// renders on different streams never share it.
__global__ void dl_chain_kernel(const float* src, float* dst, dl_image_args args, dl_chain chain,
                                const float* table, const float* work) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < args.width && y < args.height) {
        dl_chain_at(src, dst, x, y, args, &chain, table, work);
    }
}

__global__ void dl_extract_kernel(const float* src, float* work, dl_image_args args,
                                  dl_chain chain, int stop, const float* table) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x < args.width && y < args.height) {
        dl_extract_at(src, work, x, y, args, &chain, stop, table);
    }
}

__global__ void dl_box_kernel(float* work, dl_box_args box) {
    const int line = blockIdx.x * blockDim.x + threadIdx.x;
    if (line < box.lines) {
        dl_box_line(work, box, line);
    }
}

std::string error_text(const char* what, cudaError_t err) {
    return std::string(what) + ": " + cudaGetErrorString(err);
}

// Device memory for one render, allocated and freed in stream order so the render still
// returns without waiting. Devices without stream-ordered allocation fall back to a plain
// allocation, and then the stream is waited on before freeing it.
class StreamBuffer {
public:
    explicit StreamBuffer(cudaStream_t stream) : stream_(stream) {}
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;
    ~StreamBuffer() {
        if (data_ == nullptr) {
            return;
        }
        if (stream_ordered_) {
            cudaFreeAsync(data_, stream_);
        } else {
            cudaStreamSynchronize(stream_);
            cudaFree(data_);
        }
    }

    cudaError_t allocate(std::size_t floats) {
        if (floats == 0) {
            return cudaSuccess;
        }
        const std::size_t bytes = floats * sizeof(float);
        cudaError_t err = cudaMallocAsync(reinterpret_cast<void**>(&data_), bytes, stream_);
        if (err != cudaSuccess) {
            cudaGetLastError();  // clear it
            stream_ordered_ = false;
            err = cudaMalloc(reinterpret_cast<void**>(&data_), bytes);
        }
        return err;
    }

    float* get() const { return data_; }

private:
    cudaStream_t stream_;
    float* data_ = nullptr;
    bool stream_ordered_ = true;
};

dim3 grid_for(int width, int height, dim3 block) {
    return dim3((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);
}

}  // namespace

namespace dyeline::cuda {

bool available() {
    return true;
}

std::string render_plan(void* stream, const float* src, float* dst, const RenderPlan& plan,
                        const float* table, std::size_t table_floats, bool wait) {
    auto* cuda_stream = static_cast<cudaStream_t>(stream);

    StreamBuffer device_table(cuda_stream);
    cudaError_t err = device_table.allocate(table_floats);
    if (err != cudaSuccess) {
        return error_text("Could not allocate the look's tables on the GPU", err);
    }
    if (table_floats > 0) {
        // From pageable memory, this returns once `table` has been read, so the caller may
        // free it right after.
        err = cudaMemcpyAsync(device_table.get(), table, table_floats * sizeof(float),
                              cudaMemcpyHostToDevice, cuda_stream);
        if (err != cudaSuccess) {
            return error_text("Could not upload the look's tables to the GPU", err);
        }
    }
    StreamBuffer work(cuda_stream);
    err = work.allocate(plan.work_floats);
    if (err != cudaSuccess) {
        return error_text("Could not allocate GPU memory for the look's blurs", err);
    }

    const dim3 block(16, 16);
    for (const RenderPass& pass : plan.passes) {
        switch (pass.kind) {
            case PassKind::Extract:
                if (pass.image.width > 0 && pass.image.height > 0) {
                    dl_extract_kernel<<<grid_for(pass.image.width, pass.image.height, block),
                                        block, 0, cuda_stream>>>(src, work.get(), pass.image,
                                                                 plan.chain, pass.stop,
                                                                 device_table.get());
                }
                break;
            case PassKind::Blur:
                if (pass.box.lines > 0 && pass.box.length > 0) {
                    dl_box_kernel<<<(pass.box.lines + 63) / 64, 64, 0, cuda_stream>>>(work.get(),
                                                                                    pass.box);
                }
                break;
            case PassKind::Final:
                if (pass.image.width > 0 && pass.image.height > 0) {
                    dl_chain_kernel<<<grid_for(pass.image.width, pass.image.height, block), block,
                                      0, cuda_stream>>>(src, dst, pass.image, plan.chain,
                                                        device_table.get(), work.get());
                }
                break;
        }
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            return error_text("Dyeline kernel launch failed", err);
        }
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
