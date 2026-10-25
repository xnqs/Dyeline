// Runs a look's chain through the OpenCL backend and compares it with the CPU reference. Uses the
// first OpenCL device found (a GPU, or a CPU driver such as PoCL in CI). Skipped when no
// OpenCL library or device is installed.

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

#include "dyeline/gpu.h"
#include "gpu_test_image.h"
#include "opencl_api.h"

namespace {

#define SKIP_UNLESS_REQUIRED(reason)                             \
    do {                                                         \
        if (std::getenv("DYELINE_REQUIRE_OPENCL") != nullptr) {  \
            FAIL() << reason;                                    \
        }                                                        \
        GTEST_SKIP() << reason;                                  \
    } while (false)

TEST(OpenCLBackend, IsBuilt) {
    EXPECT_TRUE(dyeline::opencl::available());
}

TEST(OpenCLBackend, MatchesCpuReference) {
    const dyeline::opencl::Api* cl = dyeline::opencl::api();
    if (cl == nullptr) {
        SKIP_UNLESS_REQUIRED("no OpenCL library");
    }
    cl_platform_id platforms[8];
    cl_uint platform_count = 0;
    if (cl->GetPlatformIDs(8, platforms, &platform_count) != CL_SUCCESS || platform_count == 0) {
        SKIP_UNLESS_REQUIRED("no OpenCL platform");
    }
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    for (cl_uint i = 0; i < platform_count && device == nullptr; ++i) {
        if (cl->GetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 1, &device, nullptr) == CL_SUCCESS) {
            platform = platforms[i];
        } else {
            device = nullptr;
        }
    }
    if (device == nullptr) {
        SKIP_UNLESS_REQUIRED("no OpenCL device");
    }

    cl_int err = CL_SUCCESS;
    const cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform), 0};
    cl_context context = cl->CreateContext(props, 1, &device, nullptr, nullptr, &err);
    ASSERT_EQ(err, CL_SUCCESS);
    cl_command_queue queue = cl->CreateCommandQueue(context, device, 0, &err);
    ASSERT_EQ(err, CL_SUCCESS);

    const int width = 37;
    const int height = 23;
    const std::vector<float> src = dyeline::test::make_frame(width, height);
    const dyeline::RenderGeometry geometry = dyeline::test::window_geometry(width, height);
    const dyeline::CompiledChain chain = dyeline::test::test_chain();
    ASSERT_EQ(chain.pass.count, 15);  // all 15 tools, so the LUT loaded
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, geometry);
    const std::vector<float> expected = dyeline::test::cpu_reference(src, plan, chain);

    const std::size_t bytes = src.size() * sizeof(float);
    std::vector<float> result(src.size(), 0.0f);
    cl_mem d_src = cl->CreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, bytes,
                                    const_cast<float*>(src.data()), &err);
    ASSERT_EQ(err, CL_SUCCESS);
    cl_mem d_dst = cl->CreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, bytes,
                                    result.data(), &err);
    ASSERT_EQ(err, CL_SUCCESS);

    EXPECT_EQ(dyeline::opencl::render_plan(queue, d_src, d_dst, plan, chain.table.data(),
                                           chain.table.size(), /*wait=*/true),
              "");

    ASSERT_EQ(cl->EnqueueReadBuffer(queue, d_dst, CL_TRUE, 0, bytes, result.data(), 0, nullptr,
                                    nullptr),
              CL_SUCCESS);
    for (std::size_t i = 0; i < result.size(); ++i) {
        ASSERT_NEAR(result[i], expected[i], dyeline::test::tolerance(expected[i])) << "index " << i;
    }

    const dyeline::CompiledChain plain = dyeline::test::table_free_chain();
    ASSERT_TRUE(plain.table.empty());
    const dyeline::RenderPlan plain_plan = dyeline::plan_render(plain.pass, geometry);
    ASSERT_EQ(plain_plan.work_floats, 0u);
    const std::vector<float> plain_expected = dyeline::test::cpu_reference(src, plain_plan, plain);
    EXPECT_EQ(dyeline::opencl::render_plan(queue, d_src, d_dst, plain_plan, nullptr, 0,
                                           /*wait=*/true),
              "");
    ASSERT_EQ(cl->EnqueueReadBuffer(queue, d_dst, CL_TRUE, 0, bytes, result.data(), 0, nullptr,
                                    nullptr),
              CL_SUCCESS);
    cl->ReleaseMemObject(d_src);
    cl->ReleaseMemObject(d_dst);
    cl->ReleaseCommandQueue(queue);
    cl->ReleaseContext(context);

    for (std::size_t i = 0; i < result.size(); ++i) {
        ASSERT_NEAR(result[i], plain_expected[i], dyeline::test::tolerance(plain_expected[i]))
            << "index " << i;
    }
}

}  // namespace
