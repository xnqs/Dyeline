#pragma once

// The OpenCL entry points Dyeline uses, loaded from the system's OpenCL library at run time.
// Loading lazily means the plugin has no link-time dependency on OpenCL and still loads on
// machines without it.

#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>

namespace dyeline::opencl {

struct Api {
    decltype(&::clGetPlatformIDs) GetPlatformIDs = nullptr;
    decltype(&::clGetDeviceIDs) GetDeviceIDs = nullptr;
    decltype(&::clGetDeviceInfo) GetDeviceInfo = nullptr;
    decltype(&::clCreateContext) CreateContext = nullptr;
    decltype(&::clReleaseContext) ReleaseContext = nullptr;
    decltype(&::clCreateCommandQueue) CreateCommandQueue = nullptr;
    decltype(&::clReleaseCommandQueue) ReleaseCommandQueue = nullptr;
    decltype(&::clGetCommandQueueInfo) GetCommandQueueInfo = nullptr;
    decltype(&::clCreateBuffer) CreateBuffer = nullptr;
    decltype(&::clReleaseMemObject) ReleaseMemObject = nullptr;
    decltype(&::clEnqueueWriteBuffer) EnqueueWriteBuffer = nullptr;
    decltype(&::clEnqueueReadBuffer) EnqueueReadBuffer = nullptr;
    decltype(&::clEnqueueReadBufferRect) EnqueueReadBufferRect = nullptr;
    decltype(&::clCreateProgramWithSource) CreateProgramWithSource = nullptr;
    decltype(&::clBuildProgram) BuildProgram = nullptr;
    decltype(&::clGetProgramBuildInfo) GetProgramBuildInfo = nullptr;
    decltype(&::clReleaseProgram) ReleaseProgram = nullptr;
    decltype(&::clCreateKernel) CreateKernel = nullptr;
    decltype(&::clReleaseKernel) ReleaseKernel = nullptr;
    decltype(&::clSetKernelArg) SetKernelArg = nullptr;
    decltype(&::clEnqueueNDRangeKernel) EnqueueNDRangeKernel = nullptr;
    decltype(&::clFinish) Finish = nullptr;
};

// The loaded API, or nullptr when no OpenCL library (or a too-old one) is installed.
const Api* api();

}  // namespace dyeline::opencl
