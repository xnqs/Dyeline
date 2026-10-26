// The looks that ship with Dyeline: each one reads, uses only tools this build has, and
// visibly changes a frame.

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

#include "dyeline/frame_file.h"
#include "dyeline/library.h"
#include "dyeline/tools.h"
#include "preview.h"

namespace {

// A frame with a range of colors and brightness, including highlights over 1.
dyeline::Frame test_frame() {
    dyeline::Frame frame;
    frame.width = 48;
    frame.height = 27;
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(frame.width - 1);
            const float v = static_cast<float>(y) / static_cast<float>(frame.height - 1);
            const float level = 0.01f * std::exp2(v * 9.0f);
            frame.rgba.insert(frame.rgba.end(), {level * (0.6f + 0.8f * u), level * (0.9f - 0.3f * u),
                                                 level * (1.2f - 0.9f * u), 1.0f});
        }
    }
    frame.frame_right = static_cast<float>(frame.width);
    frame.frame_top = static_cast<float>(frame.height);
    return frame;
}

}  // namespace

TEST(BundledLooks, EveryLookReadsAndChangesTheFrame) {
    std::vector<std::string> problems;
    const auto looks = dyeline::scan_library({{DYELINE_LOOKS_DIR, true}}, problems);
    EXPECT_TRUE(problems.empty()) << problems.front();
    EXPECT_EQ(looks.size(), 24u);

    const dyeline::Frame frame = test_frame();
    dyeline::designer::PreviewRequest original;
    original.original = true;
    std::vector<std::string> unused;
    const auto before = dyeline::designer::render_preview(frame, original, unused);

    std::set<std::string> names;
    for (const auto& entry : looks) {
        SCOPED_TRACE(entry.path);
        EXPECT_FALSE(entry.folder.empty());
        EXPECT_TRUE(names.insert(entry.look.meta.name).second) << "two looks share a name";
        EXPECT_EQ(entry.look.meta.license, "CC-BY-4.0");
        EXPECT_FALSE(entry.look.meta.tags.empty());
        for (const auto& tool : entry.look.chain) {
            EXPECT_NE(dyeline::find_tool(tool.tool), nullptr) << tool.tool;
            EXPECT_NE(tool.tool, "lut") << "bundled looks ship no LUT files";
        }
        dyeline::designer::PreviewRequest request;
        request.look = entry.look;
        std::vector<std::string> look_problems;
        const auto after = dyeline::designer::render_preview(frame, request, look_problems);
        EXPECT_TRUE(look_problems.empty()) << look_problems.front();
        long long difference = 0;
        for (std::size_t i = 0; i < after.rgba.size(); ++i) {
            difference += std::abs(static_cast<int>(after.rgba[i]) - static_cast<int>(before.rgba[i]));
        }
        EXPECT_GT(difference / static_cast<long long>(frame.width * frame.height), 6);
    }
}
