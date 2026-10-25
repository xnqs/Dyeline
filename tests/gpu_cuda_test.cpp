// Runs a look's chain through the CUDA backend and compares it with the CPU reference. Skipped on
// machines without an NVIDIA GPU, such as CI runners.

#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <vector>

#include "dyeline/gpu.h"
#include "gpu_test_image.h"

namespace {

TEST(CudaBackend, IsBuilt) {
    EXPECT_TRUE(dyeline::cuda::available());
}

TEST(CudaBackend, MatchesCpuReference) {
    int devices = 0;
    if (cudaGetDeviceCount(&devices) != cudaSuccess || devices == 0) {
        GTEST_SKIP() << "no CUDA device";
    }

    const int width = 37;
    const int height = 23;
    const std::vector<float> src = dyeline::test::make_frame(width, height);
    const dl_image_args args = dyeline::test::window_args(width, height);
    const dyeline::CompiledChain chain = dyeline::test::test_chain();
    ASSERT_EQ(chain.pass.count, 13);  // all 13 tools, so the LUT loaded
    const std::vector<float> expected = dyeline::test::cpu_reference(src, args, chain);

    const std::size_t bytes = src.size() * sizeof(float);
    float* d_src = nullptr;
    float* d_dst = nullptr;
    ASSERT_EQ(cudaMalloc(&d_src, bytes), cudaSuccess);
    ASSERT_EQ(cudaMalloc(&d_dst, bytes), cudaSuccess);
    ASSERT_EQ(cudaMemcpy(d_src, src.data(), bytes, cudaMemcpyHostToDevice), cudaSuccess);
    ASSERT_EQ(cudaMemset(d_dst, 0, bytes), cudaSuccess);

    EXPECT_EQ(dyeline::cuda::render_chain(nullptr, d_src, d_dst, args, chain.pass,
                                          chain.table.data(), chain.table.size(), /*wait=*/true), "");

    std::vector<float> result(src.size());
    ASSERT_EQ(cudaMemcpy(result.data(), d_dst, bytes, cudaMemcpyDeviceToHost), cudaSuccess);
    cudaFree(d_src);
    cudaFree(d_dst);

    for (std::size_t i = 0; i < result.size(); ++i) {
        ASSERT_NEAR(result[i], expected[i], dyeline::test::tolerance(expected[i])) << "index " << i;
    }
}

}  // namespace
