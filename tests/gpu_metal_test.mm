// Runs a look's chain through the Metal backend and compares it with the CPU reference. Skipped
// when the machine has no Metal device.

#import <Metal/Metal.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "dyeline/gpu.h"
#include "gpu_test_image.h"

namespace {

TEST(MetalBackend, IsBuilt) {
    EXPECT_TRUE(dyeline::metal::available());
}

TEST(MetalBackend, MatchesCpuReference) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        GTEST_SKIP() << "no Metal device";
    }
    id<MTLCommandQueue> queue = [device newCommandQueue];
    ASSERT_TRUE(queue != nil);

    const int width = 37;
    const int height = 23;
    const std::vector<float> src = dyeline::test::make_frame(width, height);
    const dyeline::RenderGeometry geometry = dyeline::test::window_geometry(width, height);
    const dyeline::CompiledChain chain = dyeline::test::test_chain();
    ASSERT_EQ(chain.pass.count, 15);  // all 15 tools, so the LUT loaded
    const dyeline::RenderPlan plan = dyeline::plan_render(chain.pass, geometry);
    const std::vector<float> expected = dyeline::test::cpu_reference(src, plan, chain);

    const NSUInteger bytes = src.size() * sizeof(float);
    id<MTLBuffer> src_buffer = [device newBufferWithBytes:src.data()
                                                   length:bytes
                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst_buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    std::memset(dst_buffer.contents, 0, bytes);

    EXPECT_EQ(dyeline::metal::render_plan((__bridge void*)queue, (__bridge const void*)src_buffer,
                                          (__bridge void*)dst_buffer, plan, chain.table.data(),
                                          chain.table.size(), /*wait=*/true),
              "");

    const auto* result = static_cast<const float*>(dst_buffer.contents);
    for (std::size_t i = 0; i < src.size(); ++i) {
        ASSERT_NEAR(result[i], expected[i], dyeline::test::tolerance(expected[i])) << "index " << i;
    }

    const dyeline::CompiledChain plain = dyeline::test::table_free_chain();
    ASSERT_TRUE(plain.table.empty());
    const dyeline::RenderPlan plain_plan = dyeline::plan_render(plain.pass, geometry);
    const std::vector<float> plain_expected = dyeline::test::cpu_reference(src, plain_plan, plain);
    EXPECT_EQ(dyeline::metal::render_plan((__bridge void*)queue, (__bridge const void*)src_buffer,
                                          (__bridge void*)dst_buffer, plain_plan, nullptr, 0,
                                          /*wait=*/true),
              "");
    for (std::size_t i = 0; i < src.size(); ++i) {
        ASSERT_NEAR(result[i], plain_expected[i], dyeline::test::tolerance(plain_expected[i]))
            << "index " << i;
    }
}

}  // namespace
