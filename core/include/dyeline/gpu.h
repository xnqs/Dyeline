#pragma once

// GPU launchers for the render core. Each one records work on a queue the host owns and
// returns without waiting, unless `wait` is set (tests use it). Handles are passed as void*
// so this header stays free of CUDA and Metal types.

#include <string>

#include "dyeline/kernels/exposure_kernel.h"

namespace dyeline {

namespace cuda {

// True when this build includes the CUDA backend.
bool available();

// Runs Exposure on device buffers. `stream` is a cudaStream_t (null means the default
// stream). Returns an empty string on success, else the error.
std::string exposure(void* stream, const float* src, float* dst, const dl_exposure_args& args,
                     bool wait = false);

}  // namespace cuda

namespace metal {

// True when this build includes the Metal backend.
bool available();

// Runs Exposure on MTLBuffers using the host's MTLCommandQueue. Returns an empty string on
// success, else the error.
std::string exposure(void* command_queue, const void* src_buffer, void* dst_buffer,
                     const dl_exposure_args& args, bool wait = false);

}  // namespace metal

}  // namespace dyeline
