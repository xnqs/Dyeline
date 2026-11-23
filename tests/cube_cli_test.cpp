// dyeline-cube, the command-line LUT baker, run in-process.

#include "cube_cli.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "dyeline/cube.h"
#include "dyeline/look.h"

namespace {

namespace fs = std::filesystem;

// A scratch folder, removed when the test is done.
class TempDir {
public:
    TempDir() {
        std::random_device random;
        path_ = fs::temp_directory_path() /
                ("dyeline-cube-test-" + std::to_string(random()) + std::to_string(random()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void write_look(const fs::path& path, const std::string& name, double stops) {
    dyeline::Look look;
    look.meta.name = name;
    dyeline::ToolEntry exposure;
    exposure.tool = "exposure";
    exposure.params["stops"] = stops;
    dyeline::ToolEntry grain;
    grain.tool = "grain";
    look.chain = {exposure, grain};
    fs::create_directories(path.parent_path());
    std::string error;
    ASSERT_TRUE(dyeline::write_look_file(path.u8string(), look, error)) << error;
}

struct Result {
    int code;
    std::string out;
    std::string err;
};

Result cube(const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = dyeline::cli::run_cube(args, out, err);
    return {code, out.str(), err.str()};
}

std::shared_ptr<const dyeline::CubeLut> load(const fs::path& path) {
    std::string error;
    auto lut = dyeline::load_cube(path.u8string(), error);
    EXPECT_NE(lut, nullptr) << error;
    return lut;
}

}  // namespace

TEST(CubeCli, BakesALookBesideItself) {
    TempDir dir;
    write_look(dir.path() / "Bright.dyeline", "Bright", 1.0);
    const Result run = cube({(dir.path() / "Bright.dyeline").u8string(), "--size", "5"});
    ASSERT_EQ(run.code, 0) << run.err;
    EXPECT_NE(run.out.find("left out: Grain"), std::string::npos) << run.out;
    const auto lut = load(dir.path() / "Bright.cube");
    ASSERT_NE(lut, nullptr);
    EXPECT_EQ(lut->size, 5);
    EXPECT_EQ(lut->title, "Bright");
    // One stop up: the darkest grey point above black doubles.
    EXPECT_NEAR(lut->data[(5 * 5 + 5 + 1) * 3], 0.5f, 1e-5f);
}

TEST(CubeCli, BakesAFolderIntoAnotherKeepingSubfolders) {
    TempDir dir;
    write_look(dir.path() / "looks" / "A.dyeline", "A", 0.5);
    write_look(dir.path() / "looks" / "warm" / "B.dyeline", "B", -0.5);
    const fs::path out = dir.path() / "luts";
    const Result run = cube({(dir.path() / "looks").u8string(), "--out", out.u8string(), "--size", "3"});
    ASSERT_EQ(run.code, 0) << run.err;
    EXPECT_TRUE(fs::exists(out / "A.cube"));
    EXPECT_TRUE(fs::exists(out / "warm" / "B.cube"));
    EXPECT_FALSE(fs::exists(dir.path() / "looks" / "A.cube"));
    EXPECT_NE(run.out.find("Baked 2 of 2 looks."), std::string::npos) << run.out;
}

TEST(CubeCli, OverridesTheLooksColorSettings) {
    TempDir dir;
    write_look(dir.path() / "Flat.dyeline", "Flat", 0.0);
    const Result run = cube({(dir.path() / "Flat.dyeline").u8string(), "--size", "3", "--input",
                          "srgb", "--output", "rec709"});
    ASSERT_EQ(run.code, 0) << run.err;
    const auto lut = load(dir.path() / "Flat.cube");
    ASSERT_NE(lut, nullptr);
    // sRGB in, Rec.709 (gamma 2.4) out: mid grey no longer maps to itself.
    const float mid = lut->data[(3 * 3 + 3 + 1) * 3];
    EXPECT_GT(std::abs(mid - 0.5f), 0.01f);
}

TEST(CubeCli, FailsLooksWhoseLutsWouldCollide) {
    TempDir dir;
    write_look(dir.path() / "one" / "Same.dyeline", "One", 0.5);
    write_look(dir.path() / "two" / "Same.dyeline", "Two", -0.5);
    const fs::path out = dir.path() / "luts";
    const Result run = cube({(dir.path() / "one" / "Same.dyeline").u8string(),
                          (dir.path() / "two" / "Same.dyeline").u8string(), "--out", out.u8string()});
    EXPECT_EQ(run.code, 1);
    EXPECT_NE(run.err.find("already written"), std::string::npos) << run.err;
    const auto lut = load(out / "Same.cube");
    ASSERT_NE(lut, nullptr);
    EXPECT_EQ(lut->title, "One");  // the first one stays
}

TEST(CubeCli, ReportsUsageErrors) {
    EXPECT_EQ(cube({}).code, 2);
    EXPECT_EQ(cube({"--size", "1", "x.dyeline"}).code, 2);
    EXPECT_EQ(cube({"--size", "33x", "x.dyeline"}).code, 2);
    EXPECT_EQ(cube({"--input", "betamax", "x.dyeline"}).code, 2);
    EXPECT_EQ(cube({"--mode", "half", "x.dyeline"}).code, 2);
    EXPECT_EQ(cube({"--frobnicate", "x.dyeline"}).code, 2);
    EXPECT_EQ(cube({"x.dyeline", "--out"}).code, 2);
    const Result missing = cube({"no-such-look.dyeline"});
    EXPECT_EQ(missing.code, 1);
    EXPECT_NE(missing.err.find("no such file or folder"), std::string::npos);
    const Result spaces = cube({"--list-spaces"});
    EXPECT_EQ(spaces.code, 0);
    EXPECT_NE(spaces.out.find("logc3"), std::string::npos);
}
