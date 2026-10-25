#pragma once

// GPU launchers for the render core. Each one runs a look's render plan (render.h), recording
// work on a queue the host owns and returning without waiting, unless `wait` is set (tests use
// it). Handles are passed as void* so this header stays free of CUDA, Metal and OpenCL types.
// `table` is the chain's table data in host memory (CompiledChain::table), `table_floats`
// floats long; each launcher copies it to the GPU for the render, and allocates the plan's
// work memory there. `src` and `dst` are the host's image buffers, holding the plan's source
// and destination bounds. Each returns an empty string on success, else the error.

#include <cstddef>
#include <string>

#include "dyeline/render.h"

namespace dyeline {

namespace cuda {

// True when this build includes the CUDA backend.
bool available();

// Runs the plan on device buffers. `stream` is a cudaStream_t (null means the default stream).
std::string render_plan(void* stream, const float* src, float* dst, const RenderPlan& plan,
                        const float* table, std::size_t table_floats, bool wait = false);

}  // namespace cuda

namespace metal {

// True when this build includes the Metal backend.
bool available();

// Runs the plan on MTLBuffers using the host's MTLCommandQueue.
std::string render_plan(void* command_queue, const void* src_buffer, void* dst_buffer,
                        const RenderPlan& plan, const float* table, std::size_t table_floats,
                        bool wait = false);

}  // namespace metal

namespace opencl {

// True when this build includes the OpenCL backend. The OpenCL library itself is loaded at
// run time, so the plugin still loads on machines without it.
bool available();

// Runs the plan on cl_mem buffers using the host's cl_command_queue.
std::string render_plan(void* command_queue, const void* src_buffer, void* dst_buffer,
                        const RenderPlan& plan, const float* table, std::size_t table_floats,
                        bool wait = false);

}  // namespace opencl

}  // namespace dyeline
