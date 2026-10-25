// Runs the real Look Designer for a few frames, headless (CMake sets SDL's offscreen video
// driver), and checks it hands the look back unchanged.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "dyeline/designer_link.h"
#include "dyeline/look.h"

namespace {

using dyeline::Look;
using dyeline::designer::Outcome;

Look sample_look() {
    Look look;
    look.meta.name = "Sample";
    look.color.mode = "full";
    look.color.input = "logc3";
    look.color.output = "rec709";
    for (const char* tool : {"exposure", "lut", "grain", "teleport"}) {
        dyeline::ToolEntry entry;
        entry.tool = tool;
        look.chain.push_back(entry);
    }
    look.chain[0].params["stops"] = 0.5;
    look.chain[0].strength = 0.6;
    look.chain[1].params["file"] = "warm.cube";
    look.chain[2].enabled = false;
    return look;
}

}  // namespace

TEST(DesignerApp, DrawsALookAndHandsItBackUnchanged) {
    const std::string text = dyeline::write_look(sample_look());
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, text, {"--test-frames", "3"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    EXPECT_EQ(trip.look, text);
}

TEST(DesignerApp, SavesTheLookFile) {
    const auto path = std::filesystem::temp_directory_path() / "dyeline_designer_app_test.dyeline";
    std::filesystem::remove(path);
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, dyeline::write_look(sample_look()),
                                             {"--file", path.string(), "--test-frames", "2"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    Look saved;
    std::string error;
    ASSERT_TRUE(dyeline::read_look_file(path.string(), saved, error)) << error;
    EXPECT_EQ(saved.meta.name, "Sample");
    EXPECT_EQ(saved.chain.size(), 4u);
    std::filesystem::remove(path);
}
