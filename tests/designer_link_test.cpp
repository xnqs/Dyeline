// Runs the plugin's side of the Look Designer round trip against the stub designer app.

#include "dyeline/designer_link.h"

#include <gtest/gtest.h>

#include <clocale>
#include <filesystem>
#include <string>

namespace {

using dyeline::ExposureParams;
using dyeline::designer::Outcome;

ExposureParams params(double stops, double strength) {
    ExposureParams p;
    p.stops = stops;
    p.strength = strength;
    return p;
}

TEST(DesignerLook, RoundTripsThroughText) {
    ExposureParams parsed;
    ASSERT_TRUE(dyeline::designer::parse_look(dyeline::designer::format_look(params(-1.25, 0.3)),
                                              parsed));
    EXPECT_DOUBLE_EQ(parsed.stops, -1.25);
    EXPECT_DOUBLE_EQ(parsed.strength, 0.3);
}

TEST(DesignerLook, RejectsOtherVersionsAndMissingFields) {
    ExposureParams parsed;
    EXPECT_FALSE(dyeline::designer::parse_look("{\"version\": 2, \"exposure\": 1, \"strength\": 1}",
                                               parsed));
    EXPECT_FALSE(dyeline::designer::parse_look("{\"version\": 1, \"exposure\": 1}", parsed));
    EXPECT_FALSE(dyeline::designer::parse_look("", parsed));
}

TEST(DesignerLook, IgnoresTheHostLocale) {
    // Hosts built on Qt set the user's locale, which may write 0.5 as "0,5".
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr) {
        GTEST_SKIP() << "de_DE locale not installed";
    }
    const std::string look = dyeline::designer::format_look(params(0.5, 1.0));
    ExposureParams parsed;
    const bool ok = dyeline::designer::parse_look(look, parsed);
    std::setlocale(LC_NUMERIC, "C");
    EXPECT_NE(look.find("0.5"), std::string::npos) << look;
    ASSERT_TRUE(ok);
    EXPECT_DOUBLE_EQ(parsed.stops, 0.5);
}

TEST(DesignerRoundTrip, AppliesTheEditedLook) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH,
                                             dyeline::designer::format_look(params(0.5, 0.75)),
                                             {"--delay", "0"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    ExposureParams edited;
    ASSERT_TRUE(dyeline::designer::parse_look(trip.look, edited)) << trip.look;
    EXPECT_DOUBLE_EQ(edited.stops, 1.5);  // the stub raises Exposure by one stop
    EXPECT_DOUBLE_EQ(edited.strength, 0.75);
}

TEST(DesignerRoundTrip, ReportsCancel) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH,
                                             dyeline::designer::format_look(params(0.0, 1.0)),
                                             {"--delay", "0", "--cancel"});
    EXPECT_EQ(trip.outcome, Outcome::Cancelled) << trip.error;
}

TEST(DesignerRoundTrip, WaitsForTheDesigner) {
    const auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH,
                                             dyeline::designer::format_look(params(0.0, 1.0)),
                                             {"--delay", "0.5"});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    EXPECT_GE(trip.seconds, 0.5);
}

TEST(DesignerRoundTrip, FailsCleanlyWhenTheDesignerIsMissing) {
    const auto trip = dyeline::designer::run(std::string(DYELINE_DESIGNER_PATH) + "-missing",
                                             dyeline::designer::format_look(params(0.0, 1.0)));
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

TEST(DesignerLookFile, WritesAndReadsBack) {
    const std::string path = temp_look_path("dyeline-test-write.dyeline");
    std::string error;
    ASSERT_TRUE(dyeline::designer::write_look_file(path, params(2.0, 0.5), error)) << error;
    ExposureParams read;
    ASSERT_TRUE(dyeline::designer::read_look_file(path, read, error)) << error;
    EXPECT_DOUBLE_EQ(read.stops, 2.0);
    EXPECT_DOUBLE_EQ(read.strength, 0.5);
    EXPECT_FALSE(std::filesystem::exists(path + ".partial"));
    std::filesystem::remove(path);
}

TEST(DesignerLookFile, ReportsMissingAndForeignFiles) {
    const std::string path = temp_look_path("dyeline-test-missing.dyeline");
    ExposureParams read;
    std::string error;
    EXPECT_FALSE(dyeline::designer::read_look_file(path, read, error));
    EXPECT_NE(error.find("could not open"), std::string::npos) << error;
}

TEST(DesignerRoundTrip, EditsTheLookFile) {
    // Without a file yet, the designer starts from the look the plugin hands over.
    const std::string path = temp_look_path("dyeline-test-edit.dyeline");
    auto trip = dyeline::designer::run(DYELINE_DESIGNER_PATH,
                                       dyeline::designer::format_look(params(0.5, 0.75)),
                                       {"--delay", "0", "--file", path});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    ExposureParams saved;
    std::string error;
    ASSERT_TRUE(dyeline::designer::read_look_file(path, saved, error)) << error;
    EXPECT_DOUBLE_EQ(saved.stops, 1.5);
    EXPECT_DOUBLE_EQ(saved.strength, 0.75);

    // Once it exists, the file wins over the handed-over look.
    trip = dyeline::designer::run(DYELINE_DESIGNER_PATH,
                                  dyeline::designer::format_look(params(-4.0, 1.0)),
                                  {"--delay", "0", "--file", path});
    ASSERT_EQ(trip.outcome, Outcome::Applied) << trip.error;
    ASSERT_TRUE(dyeline::designer::read_look_file(path, saved, error)) << error;
    EXPECT_DOUBLE_EQ(saved.stops, 2.5);
    std::filesystem::remove(path);
}

}  // namespace
