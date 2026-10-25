// Runs the plugin's side of the Look Designer round trip against the stub designer app.

#include "dyeline/designer_link.h"
#include "dyeline/look.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <string>

namespace {

using dyeline::Look;
using dyeline::designer::Outcome;

// A look with one Exposure tool.
std::string look_text(double stops, double strength = 1.0) {
    Look look;
    look.chain.push_back({});
    look.chain[0].tool = "exposure";
    look.chain[0].strength = strength;
    look.chain[0].params["stops"] = stops;
    return dyeline::write_look(look);
}

double exposure_stops(const Look& look) {
    return look.chain.at(0).params.at("stops").get<double>();
}

TEST(DesignerRoundTrip, AppliesTheEditedLook) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, look_text(0.5, 0.75),
                                             {"--delay", "0"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    Look edited;
    std::string error;
    ASSERT_TRUE(dyeline::parse_look(trip.look, edited, error)) << error;
    EXPECT_DOUBLE_EQ(exposure_stops(edited), 1.5);  // the stub raises Exposure by one stop
    EXPECT_DOUBLE_EQ(edited.chain[0].strength, 0.75);
}

TEST(DesignerRoundTrip, AddsAnExposureToolToAnEmptyLook) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, dyeline::write_look(Look{}),
                                             {"--delay", "0"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    Look edited;
    std::string error;
    ASSERT_TRUE(dyeline::parse_look(trip.look, edited, error)) << error;
    ASSERT_EQ(edited.chain.size(), 1u);
    EXPECT_DOUBLE_EQ(exposure_stops(edited), 1.0);
}

TEST(DesignerRoundTrip, ReportsCancel) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, look_text(0.0),
                                             {"--delay", "0", "--cancel"});
    EXPECT_EQ(trip.outcome, Outcome::Cancelled) << trip.error;
}

TEST(DesignerRoundTrip, WaitsForTheDesigner) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, look_text(0.0),
                                             {"--delay", "0.5"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    EXPECT_GE(trip.seconds, 0.5);
}

TEST(DesignerRoundTrip, FailsCleanlyWhenTheDesignerIsMissing) {
    const auto trip = dyeline::designer::run(std::string(DYELINE_DESIGNER_PATH) + "-missing",
                                             look_text(0.0));
    EXPECT_EQ(trip.outcome, Outcome::Failed);
    EXPECT_FALSE(trip.error.empty());
}

TEST(DesignerRoundTrip, FailsWhenTheDesignerRejectsTheLook) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, "not a look", {"--delay", "0"});
    EXPECT_EQ(trip.outcome, Outcome::Failed);
    EXPECT_NE(trip.error.find("code 3"), std::string::npos) << trip.error;
}

std::string temp_look_path(const char* name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path.string();
}

TEST(DesignerRoundTrip, EditsTheLookFile) {
    // Without a file yet, the designer starts from the look the plugin hands over.
    const std::string path = temp_look_path("dyeline-test-edit.dyeline");
    auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, look_text(0.5, 0.75),
                                       {"--delay", "0", "--file", path});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    Look saved;
    std::string error;
    ASSERT_TRUE(dyeline::read_look_file(path, saved, error)) << error;
    EXPECT_DOUBLE_EQ(exposure_stops(saved), 1.5);
    EXPECT_DOUBLE_EQ(saved.chain[0].strength, 0.75);

    // Once it exists, the file wins over the handed-over look.
    trip = dyeline::designer::run(DYELINE_DESIGNER_PATH, look_text(-4.0),
                                  {"--delay", "0", "--file", path});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    ASSERT_TRUE(dyeline::read_look_file(path, saved, error)) << error;
    EXPECT_DOUBLE_EQ(exposure_stops(saved), 2.5);
    std::filesystem::remove(path);
}

TEST(DesignerLaunch, StartsWithoutWaiting) {
    // Open Designer: the designer edits the look file while the caller carries on, and the
    // exit is reported on another thread.
    const std::string path = temp_look_path("dyeline-test-start.dyeline");
    std::promise<int> exited;
    std::string error;
    ASSERT_TRUE(dyeline::designer::start(DYELINE_DESIGNER_PATH, {"--delay", "0", "--file", path},
                                         [&exited](int code) { exited.set_value(code); }, error))
        << error;
    std::future<int> code = exited.get_future();
    ASSERT_EQ(code.wait_for(std::chrono::seconds(30)), std::future_status::ready);
    EXPECT_EQ(code.get(), dyeline::designer::kExitApplied);
    Look saved;
    ASSERT_TRUE(dyeline::read_look_file(path, saved, error)) << error;
    EXPECT_DOUBLE_EQ(exposure_stops(saved), 1.0);
    std::filesystem::remove(path);
}

TEST(DesignerLaunch, ReportsAMissingDesigner) {
    std::string error;
    EXPECT_FALSE(dyeline::designer::start("/no/such/dyeline-designer", {}, nullptr, error));
    EXPECT_NE(error, "");
}

}  // namespace
