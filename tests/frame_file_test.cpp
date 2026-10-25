#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "dyeline/frame_file.h"

namespace {

std::string temp(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST(FrameFile, RoundTrips) {
    dyeline::Frame frame;
    frame.width = 3;
    frame.height = 2;
    for (int i = 0; i < 24; ++i) {
        frame.rgba.push_back(0.25f * static_cast<float>(i) - 1.0f);
    }
    frame.premultiplied = true;
    frame.x0 = -4;
    frame.y0 = 7;
    frame.canonical_x = 2.0f;
    frame.canonical_y = 0.5f;
    frame.frame_left = -10.0f;
    frame.frame_bottom = 1.0f;
    frame.frame_right = 1920.0f;
    frame.frame_top = 1080.0f;
    frame.seed = 42;
    const std::string path = temp("dyeline_frame_test.dlframe");
    std::string error;
    ASSERT_TRUE(dyeline::write_frame_file(path, frame, error)) << error;
    dyeline::Frame read;
    ASSERT_TRUE(dyeline::read_frame_file(path, read, error)) << error;
    EXPECT_EQ(read.width, 3);
    EXPECT_EQ(read.height, 2);
    EXPECT_EQ(read.rgba, frame.rgba);
    EXPECT_TRUE(read.premultiplied);
    EXPECT_EQ(read.x0, -4);
    EXPECT_EQ(read.y0, 7);
    EXPECT_EQ(read.canonical_x, 2.0f);
    EXPECT_EQ(read.canonical_y, 0.5f);
    EXPECT_EQ(read.frame_left, -10.0f);
    EXPECT_EQ(read.frame_top, 1080.0f);
    EXPECT_EQ(read.seed, 42);
    std::filesystem::remove(path);
}

TEST(FrameFile, RefusesOtherFilesAndShortOnes) {
    const std::string path = temp("dyeline_frame_bad.dlframe");
    std::string error;
    dyeline::Frame frame;
    {
        std::ofstream(path, std::ios::binary) << "not a frame at all";
    }
    EXPECT_FALSE(dyeline::read_frame_file(path, frame, error));
    dyeline::Frame big;
    big.width = 4;
    big.height = 4;
    big.rgba.assign(64, 0.5f);
    ASSERT_TRUE(dyeline::write_frame_file(path, big, error)) << error;
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 4);
    EXPECT_FALSE(dyeline::read_frame_file(path, frame, error));
    EXPECT_NE(error.find("cut short"), std::string::npos) << error;
    EXPECT_FALSE(dyeline::read_frame_file(temp("dyeline_no_such_frame.dlframe"), frame, error));
    std::filesystem::remove(path);
}

TEST(FrameFile, ShrinksByAveragingWholePixels) {
    // A 5 x 2 RGB image with padded rows, shrunk to fit 2 x 2: boxes of 3 x 3 pixels.
    const int width = 5;
    const int height = 2;
    const int row = 16;  // 15 floats of pixels, one of padding
    std::vector<float> pixels(row * height, -99.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float* p = &pixels[y * row + x * 3];
            p[0] = static_cast<float>(x);
            p[1] = static_cast<float>(y);
            p[2] = 1.0f;
        }
    }
    dyeline::Frame geometry;
    geometry.x0 = 7;
    geometry.y0 = -2;
    geometry.canonical_x = 2.0f;
    geometry.canonical_y = 1.0f;
    geometry.seed = 3;
    const dyeline::Frame f = dyeline::shrink_to_fit(pixels.data(), width, height, 3, row, geometry, 2, 2);
    ASSERT_EQ(f.width, 2);
    ASSERT_EQ(f.height, 1);
    EXPECT_FLOAT_EQ(f.rgba[0], 1.0f);  // mean of x 0..2
    EXPECT_FLOAT_EQ(f.rgba[1], 0.5f);
    EXPECT_FLOAT_EQ(f.rgba[2], 1.0f);
    EXPECT_FLOAT_EQ(f.rgba[3], 1.0f);  // RGB images are opaque
    EXPECT_FLOAT_EQ(f.rgba[4], 3.5f);  // the last box holds only x 3 and 4
    EXPECT_EQ(f.x0, 2);
    EXPECT_EQ(f.y0, -1);
    EXPECT_FLOAT_EQ(f.canonical_x, 6.0f);
    EXPECT_FLOAT_EQ(f.canonical_y, 3.0f);
    EXPECT_EQ(f.seed, 3);
}

TEST(FrameFile, KeepsSmallFramesAsTheyAre) {
    const std::vector<float> alpha = {0.0f, 0.5f, 1.0f, 0.25f};
    const dyeline::Frame f = dyeline::shrink_to_fit(alpha.data(), 2, 2, 1, 2, dyeline::Frame{}, 1920, 1080);
    ASSERT_EQ(f.width, 2);
    ASSERT_EQ(f.height, 2);
    EXPECT_FLOAT_EQ(f.rgba[3], 0.0f);
    EXPECT_FLOAT_EQ(f.rgba[7], 0.5f);
    EXPECT_FLOAT_EQ(f.rgba[0], 0.0f);  // alpha-only images have no color
    EXPECT_FLOAT_EQ(f.canonical_x, 1.0f);
}
