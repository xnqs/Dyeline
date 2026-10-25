// OpenCL backend: builds the shared kernels for the host's device and records work on the
// host's command queue.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "dyeline/gpu.h"
#include "opencl_api.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Generated at build time from kernels.cl and the shared kernel headers.
extern const char* const kDyelineOpenCLSource;

namespace dyeline::opencl {

namespace {

void* open_library() {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryA("OpenCL.dll"));
#else
    for (const char* name : {"libOpenCL.so.1", "libOpenCL.so"}) {
        if (void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
            return handle;
        }
    }
    return nullptr;
#endif
}

void* find_symbol(void* library, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

template <typename Fn>
bool load(void* library, const char* name, Fn& fn) {
    fn = reinterpret_cast<Fn>(find_symbol(library, name));
    return fn != nullptr;
}

Api load_api(bool& ok) {
    Api a;
    void* library = open_library();
    ok = library != nullptr && load(library, "clGetPlatformIDs", a.GetPlatformIDs) &&
         load(library, "clGetDeviceIDs", a.GetDeviceIDs) &&
         load(library, "clGetDeviceInfo", a.GetDeviceInfo) &&
         load(library, "clCreateContext", a.CreateContext) &&
         load(library, "clReleaseContext", a.ReleaseContext) &&
         load(library, "clCreateCommandQueue", a.CreateCommandQueue) &&
         load(library, "clReleaseCommandQueue", a.ReleaseCommandQueue) &&
         load(library, "clGetCommandQueueInfo", a.GetCommandQueueInfo) &&
         load(library, "clCreateBuffer", a.CreateBuffer) &&
         load(library, "clReleaseMemObject", a.ReleaseMemObject) &&
         load(library, "clEnqueueWriteBuffer", a.EnqueueWriteBuffer) &&
         load(library, "clEnqueueReadBuffer", a.EnqueueReadBuffer) &&
         load(library, "clEnqueueReadBufferRect", a.EnqueueReadBufferRect) &&
         load(library, "clCreateProgramWithSource", a.CreateProgramWithSource) &&
         load(library, "clBuildProgram", a.BuildProgram) &&
         load(library, "clGetProgramBuildInfo", a.GetProgramBuildInfo) &&
         load(library, "clReleaseProgram", a.ReleaseProgram) &&
         load(library, "clCreateKernel", a.CreateKernel) &&
         load(library, "clReleaseKernel", a.ReleaseKernel) &&
         load(library, "clSetKernelArg", a.SetKernelArg) &&
         load(library, "clEnqueueNDRangeKernel", a.EnqueueNDRangeKernel) &&
         load(library, "clFinish", a.Finish);
    // The library stays loaded for the life of the process.
    return a;
}

std::string cl_error(const char* what, cl_int code) {
    return std::string(what) + " (OpenCL error " + std::to_string(code) + ")";
}

// Builds the kernels once per context and device. The cached program keeps its context
// alive, so a context address can't be reused while it is in the cache.
std::string program_for(const Api& cl, cl_context context, cl_device_id device, cl_program& out) {
    static std::mutex mutex;
    static std::map<std::pair<cl_context, cl_device_id>, cl_program> cache;

    std::lock_guard<std::mutex> lock(mutex);
    const auto key = std::make_pair(context, device);
    if (auto it = cache.find(key); it != cache.end()) {
        out = it->second;
        return {};
    }

    cl_int err = CL_SUCCESS;
    const char* source = kDyelineOpenCLSource;
    cl_program program = cl.CreateProgramWithSource(context, 1, &source, nullptr, &err);
    if (err != CL_SUCCESS) {
        return cl_error("Could not create the OpenCL program", err);
    }
    err = cl.BuildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::size_t log_size = 0;
        cl.GetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size + 1, '\0');
        cl.GetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        cl.ReleaseProgram(program);
        return cl_error("OpenCL kernel build failed", err) + ": " + log.data();
    }
    cache.emplace(key, program);
    out = program;
    return {};
}

}  // namespace

const Api* api() {
    static bool ok = false;
    static const Api loaded = load_api(ok);
    return ok ? &loaded : nullptr;
}

bool available() {
    return true;
}

std::string render_plan(void* command_queue, const void* src_buffer, void* dst_buffer,
                        const RenderPlan& plan, const float* table, std::size_t table_floats,
                        bool wait) {
    const Api* cl = api();
    if (cl == nullptr) {
        return "The OpenCL library could not be loaded";
    }
    auto queue = static_cast<cl_command_queue>(command_queue);
    auto src = static_cast<cl_mem>(const_cast<void*>(src_buffer));
    auto dst = static_cast<cl_mem>(dst_buffer);
    if (queue == nullptr || src == nullptr || dst == nullptr) {
        return "OpenCL render called without a command queue or buffers";
    }

    cl_context context = nullptr;
    cl_device_id device = nullptr;
    cl_int err = cl->GetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context), &context, nullptr);
    if (err == CL_SUCCESS) {
        err = cl->GetCommandQueueInfo(queue, CL_QUEUE_DEVICE, sizeof(device), &device, nullptr);
    }
    if (err != CL_SUCCESS) {
        return cl_error("Could not query the host's OpenCL queue", err);
    }

    cl_program program = nullptr;
    if (std::string error = program_for(*cl, context, device, program); !error.empty()) {
        return error;
    }

    // Buffers for this render. Each is released as soon as its work is enqueued; the queue
    // keeps it until that work is done. A look without tables or blurs passes null buffers.
    std::vector<cl_mem> buffers;
    std::vector<cl_kernel> kernels;
    const auto release_all = [&] {
        for (cl_kernel kernel : kernels) {
            cl->ReleaseKernel(kernel);
        }
        for (cl_mem buffer : buffers) {
            cl->ReleaseMemObject(buffer);
        }
    };
    const auto make_buffer = [&](cl_mem_flags flags, std::size_t bytes, void* host, cl_mem& out) {
        out = nullptr;
        if (bytes == 0) {
            return CL_SUCCESS;
        }
        cl_int status = CL_SUCCESS;
        out = cl->CreateBuffer(context, flags, bytes, host, &status);
        if (status == CL_SUCCESS) {
            buffers.push_back(out);
        }
        return status;
    };

    // The chain goes in a small constant buffer: it can exceed the 1 KB of kernel arguments
    // OpenCL 1.2 guarantees.
    cl_mem chain_buffer = nullptr;
    cl_mem table_buffer = nullptr;
    cl_mem work_buffer = nullptr;
    err = make_buffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(plan.chain),
                      const_cast<dl_chain*>(&plan.chain), chain_buffer);
    if (err == CL_SUCCESS) {
        err = make_buffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, table_floats * sizeof(float),
                          const_cast<float*>(table), table_buffer);
    }
    if (err == CL_SUCCESS) {
        err = make_buffer(CL_MEM_READ_WRITE, plan.work_floats * sizeof(float), nullptr, work_buffer);
    }
    if (err != CL_SUCCESS) {
        release_all();
        return cl_error("Could not allocate OpenCL memory for the look", err);
    }

    // A kernel object per render: kernel arguments are not safe to set from several threads.
    const auto kernel = [&](const char* name, cl_kernel& out) {
        cl_int status = CL_SUCCESS;
        out = cl->CreateKernel(program, name, &status);
        if (status == CL_SUCCESS) {
            kernels.push_back(out);
        }
        return status;
    };
    cl_kernel chain_kernel = nullptr;
    cl_kernel extract_kernel = nullptr;
    cl_kernel box_kernel = nullptr;
    err = kernel("dl_chain_kernel", chain_kernel);
    if (err == CL_SUCCESS && plan.work_floats > 0) {
        err = kernel("dl_extract_kernel", extract_kernel);
        if (err == CL_SUCCESS) err = kernel("dl_box_kernel", box_kernel);
    }
    if (err != CL_SUCCESS) {
        release_all();
        return cl_error("Could not create the OpenCL kernels", err);
    }

    // Kernel arguments are copied when a kernel is enqueued, so one kernel object serves
    // every pass of its kind.
    for (const RenderPass& pass : plan.passes) {
        if (pass.kind == PassKind::Blur) {
            if (pass.box.lines <= 0 || pass.box.length <= 0) {
                continue;
            }
            err = cl->SetKernelArg(box_kernel, 0, sizeof(cl_mem), &work_buffer);
            if (err == CL_SUCCESS) err = cl->SetKernelArg(box_kernel, 1, sizeof(pass.box), &pass.box);
            if (err == CL_SUCCESS) {
                const std::size_t global[1] = {static_cast<std::size_t>(pass.box.lines)};
                err = cl->EnqueueNDRangeKernel(queue, box_kernel, 1, nullptr, global, nullptr, 0,
                                               nullptr, nullptr);
            }
        } else {
            if (pass.image.width <= 0 || pass.image.height <= 0) {
                continue;
            }
            const bool final_pass = pass.kind == PassKind::Final;
            cl_kernel k = final_pass ? chain_kernel : extract_kernel;
            err = cl->SetKernelArg(k, 0, sizeof(cl_mem), &src);
            if (err == CL_SUCCESS) {
                err = cl->SetKernelArg(k, 1, sizeof(cl_mem), final_pass ? &dst : &work_buffer);
            }
            if (err == CL_SUCCESS) err = cl->SetKernelArg(k, 2, sizeof(pass.image), &pass.image);
            if (err == CL_SUCCESS) err = cl->SetKernelArg(k, 3, sizeof(cl_mem), &chain_buffer);
            if (err == CL_SUCCESS) err = cl->SetKernelArg(k, 4, sizeof(cl_mem), &table_buffer);
            if (err == CL_SUCCESS) {
                err = final_pass ? cl->SetKernelArg(k, 5, sizeof(cl_mem), &work_buffer)
                                 : cl->SetKernelArg(k, 5, sizeof(int), &pass.stop);
            }
            if (err == CL_SUCCESS) {
                const std::size_t global[2] = {static_cast<std::size_t>(pass.image.width),
                                               static_cast<std::size_t>(pass.image.height)};
                err = cl->EnqueueNDRangeKernel(queue, k, 2, nullptr, global, nullptr, 0, nullptr,
                                               nullptr);
            }
        }
        if (err != CL_SUCCESS) {
            break;
        }
    }
    release_all();
    if (err != CL_SUCCESS) {
        return cl_error("OpenCL kernel failed to launch", err);
    }
    if (wait) {
        err = cl->Finish(queue);
        if (err != CL_SUCCESS) {
            return cl_error("OpenCL kernel failed", err);
        }
    }
    return {};
}

namespace {

// Dyeline's own OpenCL device, for hosts that render on the CPU: the first GPU found, opened
// once and kept for the life of the process. DYELINE_OPENCL_DEVICE=any accepts any device,
// such as a CPU driver (tests use PoCL).
struct OwnDevice {
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    std::string name;
    std::string error;
    std::mutex mutex;  // one render at a time on the shared queue
};

OwnDevice& own_device() {
    static OwnDevice device;
    static std::once_flag opened;
    std::call_once(opened, [] {
        const Api* cl = api();
        if (cl == nullptr) {
            device.error = "The OpenCL library could not be loaded";
            return;
        }
        const char* wanted = std::getenv("DYELINE_OPENCL_DEVICE");
        const cl_device_type type = wanted != nullptr && std::strcmp(wanted, "any") == 0
                                        ? CL_DEVICE_TYPE_ALL
                                        : CL_DEVICE_TYPE_GPU;
        cl_platform_id platforms[16];
        cl_uint platform_count = 0;
        if (cl->GetPlatformIDs(16, platforms, &platform_count) != CL_SUCCESS) {
            platform_count = 0;
        }
        for (cl_uint i = 0; i < platform_count; ++i) {
            cl_device_id id = nullptr;
            if (cl->GetDeviceIDs(platforms[i], type, 1, &id, nullptr) != CL_SUCCESS || id == nullptr) {
                continue;
            }
            cl_int err = CL_SUCCESS;
            const cl_context_properties props[] = {
                CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platforms[i]), 0};
            cl_context context = cl->CreateContext(props, 1, &id, nullptr, nullptr, &err);
            if (err != CL_SUCCESS) {
                continue;
            }
            cl_command_queue queue = cl->CreateCommandQueue(context, id, 0, &err);
            if (err != CL_SUCCESS) {
                cl->ReleaseContext(context);
                continue;
            }
            char name[256] = {};
            cl->GetDeviceInfo(id, CL_DEVICE_NAME, sizeof(name) - 1, name, nullptr);
            device.context = context;
            device.queue = queue;
            device.name = name;
            return;
        }
        device.error = "No OpenCL GPU was found";
    });
    return device;
}

}  // namespace

std::string own_device_name(std::string& error) {
    OwnDevice& device = own_device();
    error = device.error;
    return device.name;
}

std::string render_plan_own_device(const float* src, std::size_t src_floats, float* dst,
                                   std::size_t dst_floats, const RenderPlan& plan,
                                   const float* table, std::size_t table_floats) {
    OwnDevice& device = own_device();
    if (device.queue == nullptr) {
        return device.error;
    }
    const Api* cl = api();
    std::lock_guard<std::mutex> lock(device.mutex);
    cl_int err = CL_SUCCESS;
    cl_mem src_buffer = cl->CreateBuffer(device.context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                         src_floats * sizeof(float), const_cast<float*>(src), &err);
    if (err != CL_SUCCESS) {
        return cl_error("Could not upload the frame to the OpenCL device", err);
    }
    cl_mem dst_buffer = cl->CreateBuffer(device.context, CL_MEM_WRITE_ONLY,
                                         dst_floats * sizeof(float), nullptr, &err);
    if (err != CL_SUCCESS) {
        cl->ReleaseMemObject(src_buffer);
        return cl_error("Could not allocate the OpenCL output", err);
    }
    std::string error = render_plan(device.queue, src_buffer, dst_buffer, plan, table, table_floats);
    // Read back only the window the final pass wrote; the rest of the host's buffer is left
    // as it was, as on the CPU.
    if (error.empty()) {
        for (const RenderPass& pass : plan.passes) {
            if (pass.kind != PassKind::Final || pass.image.width <= 0 || pass.image.height <= 0) {
                continue;
            }
            const dl_image_args& a = pass.image;
            const std::size_t row_bytes = static_cast<std::size_t>(a.dst_row_floats) * sizeof(float);
            const std::size_t origin[3] = {
                static_cast<std::size_t>(a.dst_x0 * a.channels) * sizeof(float),
                static_cast<std::size_t>(a.dst_y0), 0};
            const std::size_t region[3] = {
                static_cast<std::size_t>(a.width * a.channels) * sizeof(float),
                static_cast<std::size_t>(a.height), 1};
            err = cl->EnqueueReadBufferRect(device.queue, dst_buffer, CL_TRUE, origin, origin,
                                            region, row_bytes, 0, row_bytes, 0, dst, 0, nullptr,
                                            nullptr);
            if (err != CL_SUCCESS) {
                error = cl_error("Could not read the frame back from the OpenCL device", err);
            }
        }
    }
    cl->Finish(device.queue);
    cl->ReleaseMemObject(src_buffer);
    cl->ReleaseMemObject(dst_buffer);
    return error;
}

}  // namespace dyeline::opencl
