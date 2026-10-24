// Runs Exposure through the Metal backend and compares it with the CPU reference. Skipped
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
    const dl_exposure_args args = dyeline::test::window_args(width, height, 2.5f);
    const std::vector<float> expected = dyeline::test::cpu_reference(src, args);

    const NSUInteger bytes = src.size() * sizeof(float);
    id<MTLBuffer> src_buffer = [device newBufferWithBytes:src.data()
                                                   length:bytes
                                                  options:MTLResourceStorageModeShared];
    id<MTLBuffer> dst_buffer = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    std::memset(dst_buffer.contents, 0, bytes);

    EXPECT_EQ(dyeline::metal::exposure((__bridge void*)queue, (__bridge const void*)src_buffer,
                                       (__bridge void*)dst_buffer, args, /*wait=*/true),
              "");

    const auto* result = static_cast<const float*>(dst_buffer.contents);
    for (std::size_t i = 0; i < src.size(); ++i) {
        ASSERT_NEAR(result[i], expected[i], 1e-6f) << "index " << i;
    }
}

}  // namespace
