// The Look Designer's preview: rendering the host's frame through the look being edited.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dyeline/frame_file.h"
#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"
#include "preview.h"
#include "scopes.h"

namespace {

using dyeline::Frame;
using dyeline::Look;
using dyeline::designer::DisplayImage;
using dyeline::designer::PreviewRequest;
using dyeline::designer::ViewTransform;

// A 2 x 2 frame: mid grey in the bottom row, black in the top row.
Frame grey_frame() {
    Frame frame;
    frame.width = 2;
    frame.height = 2;
    frame.rgba = {0.18f, 0.18f, 0.18f, 1.0f, 0.18f, 0.18f, 0.18f, 1.0f,
                  0.0f,  0.0f,  0.0f,  1.0f, 0.0f,  0.0f,  0.0f,  1.0f};
    frame.frame_right = 2.0f;
    frame.frame_top = 2.0f;
    return frame;
}

Look exposure_look(double stops) {
    Look look;
    dyeline::ToolEntry entry;
    entry.tool = "exposure";
    entry.params = {{"stops", stops}};
    look.chain.push_back(entry);
    return look;
}

int srgb_byte(float linear) {
    return static_cast<int>(dl_encode_curve(DL_CURVE_SRGB, linear) * 255.0f + 0.5f);
}

}  // namespace

TEST(Preview, PicksAViewForTheSpace) {
    EXPECT_EQ(dyeline::designer::view_for("linear"), ViewTransform::LinearToSrgb);
    EXPECT_EQ(dyeline::designer::view_for("acescg"), ViewTransform::LinearToSrgb);
    EXPECT_EQ(dyeline::designer::view_for("rec709"), ViewTransform::AsIs);
    EXPECT_EQ(dyeline::designer::view_for("logc3"), ViewTransform::AsIs);
    EXPECT_EQ(dyeline::designer::view_for("betacam"), ViewTransform::AsIs);
}

TEST(Preview, RendersTheLookAndTurnsTheFrameRightWayUp) {
    PreviewRequest request;
    request.look = exposure_look(1.0);
    std::vector<std::string> problems;
    const DisplayImage image = dyeline::designer::render_preview(grey_frame(), request, problems);
    ASSERT_EQ(image.width, 2);
    ASSERT_EQ(image.height, 2);
    EXPECT_TRUE(problems.empty());
    // The top row on screen is the frame's top row, which is black.
    EXPECT_EQ(image.rgba[0], 0);
    EXPECT_EQ(image.rgba[3], 255);
    // The bottom row is grey one stop up, through the sRGB curve (the look is linear).
    EXPECT_EQ(image.rgba[8], srgb_byte(0.36f));
}

TEST(Preview, ShowsTheOriginal) {
    PreviewRequest request;
    request.look = exposure_look(3.0);
    request.original = true;
    std::vector<std::string> problems;
    const DisplayImage image = dyeline::designer::render_preview(grey_frame(), request, problems);
    EXPECT_EQ(image.rgba[8], srgb_byte(0.18f));
}

TEST(Preview, ShowsDisplaySpacesAsTheyAre) {
    PreviewRequest request;
    request.look = exposure_look(0.0);
    request.look.color.input = "rec709";
    std::vector<std::string> problems;
    const DisplayImage image = dyeline::designer::render_preview(grey_frame(), request, problems);
    EXPECT_EQ(image.rgba[8], static_cast<int>(0.18f * 255.0f + 0.5f));
    request.view = ViewTransform::LinearToSrgb;  // unless asked otherwise
    const DisplayImage forced = dyeline::designer::render_preview(grey_frame(), request, problems);
    EXPECT_EQ(forced.rgba[8], srgb_byte(0.18f));
}

TEST(Preview, ReportsWhatItLeftOut) {
    PreviewRequest request;
    Look look;
    dyeline::ToolEntry lut;
    lut.tool = "lut";
    lut.params = {{"file", "no-such-file.cube"}};
    look.chain.push_back(lut);
    request.look = look;
    std::vector<std::string> problems;
    dyeline::designer::render_preview(grey_frame(), request, problems);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_EQ(problems[0].rfind("LUT", 0), 0u) << problems[0];
}

TEST(Preview, WorkerRendersTheLatestRequest) {
    dyeline::designer::PreviewWorker worker(std::make_shared<const Frame>(grey_frame()));
    for (double stops : {0.5, 2.0, 1.5}) {
        PreviewRequest request;
        request.look = exposure_look(stops);
        worker.request(std::move(request));
    }
    worker.wait_idle();
    dyeline::designer::PreviewResult result;
    ASSERT_TRUE(worker.take(result));
    EXPECT_EQ(result.image.rgba[8], srgb_byte(0.18f * 2.8284271f));
    EXPECT_EQ(result.parade.width, dyeline::designer::kParadeWidth);
    EXPECT_EQ(result.vectorscope.width, dyeline::designer::kVectorscopeSize);
    EXPECT_FALSE(worker.take(result));  // nothing new since
}

TEST(Preview, DraftsRenderASmallerFrame) {
    Frame frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.rgba.assign(static_cast<std::size_t>(frame.width) * frame.height * 4, 0.18f);
    frame.frame_right = 1920.0f;
    frame.frame_top = 1080.0f;
    dyeline::designer::PreviewWorker worker(std::make_shared<const Frame>(frame));
    PreviewRequest request;
    request.look = exposure_look(1.0);
    request.draft = true;
    worker.request(request);
    worker.wait_idle();
    dyeline::designer::PreviewResult result;
    ASSERT_TRUE(worker.take(result));
    EXPECT_TRUE(result.draft);
    EXPECT_EQ(result.image.width, 640);
    EXPECT_EQ(result.image.height, 360);
    EXPECT_EQ(result.image.rgba[0], srgb_byte(0.36f));

    request.draft = false;
    worker.request(request);
    worker.wait_idle();
    ASSERT_TRUE(worker.take(result));
    EXPECT_FALSE(result.draft);
    EXPECT_EQ(result.image.width, 1920);
    EXPECT_EQ(result.image.rgba[0], srgb_byte(0.36f));
}

namespace {

DisplayImage flat_image(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    DisplayImage image;
    image.width = 8;
    image.height = 4;
    for (int i = 0; i < image.width * image.height; ++i) {
        image.rgba.insert(image.rgba.end(), {r, g, b, 255});
    }
    return image;
}

const std::uint8_t* pixel(const DisplayImage& image, int x, int y) {
    return &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
}

}  // namespace

TEST(Scopes, ParadeShowsEachChannelsLevel) {
    using dyeline::designer::kParadeColumns;
    using dyeline::designer::kParadeGap;
    const DisplayImage parade = dyeline::designer::rgb_parade(flat_image(200, 100, 30));
    ASSERT_EQ(parade.width, dyeline::designer::kParadeWidth);
    ASSERT_EQ(parade.height, dyeline::designer::kParadeHeight);
    const int column = kParadeColumns / 2;
    // Each panel lights up at its channel's level, in its own color, and nowhere far from it.
    EXPECT_GT(pixel(parade, column, 255 - 200)[0], 200);
    EXPECT_LT(pixel(parade, column, 255 - 100)[0], 60);
    EXPECT_GT(pixel(parade, kParadeColumns + kParadeGap + column, 255 - 100)[1], 200);
    EXPECT_GT(pixel(parade, 2 * (kParadeColumns + kParadeGap) + column, 255 - 30)[2], 200);
    EXPECT_LT(pixel(parade, 2 * (kParadeColumns + kParadeGap) + column, 255 - 200)[2], 60);
}

TEST(Scopes, VectorscopeShowsGreyAtTheCenterAndRedTowardsItsTarget) {
    using dyeline::designer::kVectorscopeSize;
    const int c = kVectorscopeSize / 2;
    const DisplayImage grey = dyeline::designer::vectorscope(flat_image(128, 128, 128));
    EXPECT_GT(pixel(grey, c, c)[1], 200);
    EXPECT_LT(pixel(grey, c + 40, c - 40)[1], 60);

    // Pure red: Cb = -0.2126 / 1.8556, Cr = 0.7874 / 1.5748 = 0.5, so left of center and at
    // the top edge.
    const DisplayImage red = dyeline::designer::vectorscope(flat_image(255, 0, 0));
    const int x = static_cast<int>((0.5f - 0.2126f / 1.8556f) * kVectorscopeSize);
    EXPECT_GT(pixel(red, x, 0)[1], 200);
    EXPECT_LT(pixel(red, c + 6, c + 10)[1], 60);  // near the center, off the graticule
}
