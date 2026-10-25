#pragma once

// GPU launchers for the render core. Each one runs a look's fused per-pixel pass, recording
// work on a queue the host owns and returning without waiting, unless `wait` is set (tests use
// it). Handles are passed as void* so this header stays free of CUDA, Metal and OpenCL types.
// Each returns an empty string on success, else the error.

#include <string>

#include "dyeline/kernels/chain_kernel.h"

namespace dyeline {

namespace cuda {

// True when this build includes the CUDA backend.
bool available();

// Runs the chain on device buffers. `stream` is a cudaStream_t (null means the default stream).
std::string render_chain(void* stream, const float* src, float* dst, const dl_image_args& args,
                         const dl_chain& chain, bool wait = false);

}  // namespace cuda

namespace metal {

// True when this build includes the Metal backend.
bool available();

// Runs the chain on MTLBuffers using the host's MTLCommandQueue.
std::string render_chain(void* command_queue, const void* src_buffer, void* dst_buffer,
                         const dl_image_args& args, const dl_chain& chain, bool wait = false);

}  // namespace metal

namespace opencl {

// True when this build includes the OpenCL backend. The OpenCL library itself is loaded at
// run time, so the plugin still loads on machines without it.
bool available();

// Runs the chain on cl_mem buffers using the host's cl_command_queue.
std::string render_chain(void* command_queue, const void* src_buffer, void* dst_buffer,
                         const dl_image_args& args, const dl_chain& chain, bool wait = false);

}  // namespace opencl

}  // namespace dyeline
