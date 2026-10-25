// The Look Designer's preview: rendering the host's frame through the look being edited.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "dyeline/frame_file.h"
#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"
#include "preview.h"

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
    DisplayImage image;
    std::vector<std::string> problems;
    ASSERT_TRUE(worker.take(image, problems));
    EXPECT_EQ(image.rgba[8], srgb_byte(0.18f * 2.8284271f));
    EXPECT_FALSE(worker.take(image, problems));  // nothing new since
}
