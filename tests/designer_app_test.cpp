// Runs the real Look Designer for a few frames, headless (CMake sets SDL's offscreen video
// driver), and checks it hands the look back unchanged.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "dyeline/cube.h"
#include "dyeline/designer_link.h"
#include "dyeline/frame_file.h"
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

TEST(DesignerApp, DrawsTheCurvesGraph) {
    Look look;
    look.meta.name = "Curves";
    dyeline::ToolEntry curves;
    curves.tool = "curves";
    curves.params["master"] = nlohmann::json::parse("[[0, 0.05], [0.5, 0.55], [1, 0.95]]");
    curves.params["blue"] = nlohmann::json::parse("[[0, 0], [0.4, 0.45], [1, 1]]");
    look.chain.push_back(curves);
    const std::string text = dyeline::write_look(look);
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

TEST(DesignerApp, PreviewsTheHostsFrame) {
    dyeline::Frame frame;
    frame.width = 64;
    frame.height = 36;
    for (int i = 0; i < frame.width * frame.height; ++i) {
        const float v = static_cast<float>(i % frame.width) / static_cast<float>(frame.width);
        frame.rgba.insert(frame.rgba.end(), {v, v * 0.5f, 1.0f - v, 1.0f});
    }
    frame.frame_right = 64.0f;
    frame.frame_top = 36.0f;
    const std::string frame_path = dyeline::designer::temp_path("-test.dlframe");
    std::string error;
    ASSERT_TRUE(dyeline::write_frame_file(frame_path, frame, error)) << error;
    const std::string text = dyeline::write_look(sample_look());
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, text,
                                             {"--frame", frame_path, "--test-frames", "3"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    EXPECT_EQ(trip.look, text);
    std::filesystem::remove(std::filesystem::u8path(frame_path));
}

TEST(DesignerApp, DrawsTheLooksBrowser) {
    // The user's looks go to a scratch folder, not the home folder.
    const auto user_dir = std::filesystem::temp_directory_path() / "dyeline_designer_app_user";
    std::filesystem::remove_all(user_dir);
#ifdef _WIN32
    _putenv_s("DYELINE_USER_DIR", user_dir.string().c_str());
#else
    setenv("DYELINE_USER_DIR", user_dir.string().c_str(), 1);
#endif
    const std::string text = dyeline::write_look(sample_look());
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, text, {"--test-looks", "--test-frames", "3"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    EXPECT_EQ(trip.look, text);
    std::filesystem::remove_all(user_dir);
}

TEST(DesignerApp, ExportsTheLookAsACube) {
    const auto path = std::filesystem::temp_directory_path() / "dyeline_designer_app_test.cube";
    std::filesystem::remove(path);
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, dyeline::write_look(sample_look()),
                                             {"--test-export-cube", path.string(), "--test-frames", "2"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    std::string error;
    const auto lut = dyeline::load_cube(path.string(), error);
    ASSERT_NE(lut, nullptr) << error;
    EXPECT_EQ(lut->size, 33);
    EXPECT_EQ(lut->title, "Sample");
    std::filesystem::remove(path);
}
