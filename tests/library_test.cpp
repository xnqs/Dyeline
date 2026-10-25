// The looks library: finding .dyeline files in folders, searching them, and favorites.

#include "dyeline/library.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

class Library : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("dyeline_library_test_" +
                                             std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }
    void TearDown() override { fs::remove_all(root_); }

    void write(const fs::path& relative, const std::string& name, std::vector<std::string> tags = {}) {
        dyeline::Look look;
        look.meta.name = name;
        look.meta.tags = std::move(tags);
        fs::create_directories((root_ / relative).parent_path());
        std::string error;
        ASSERT_TRUE(dyeline::write_look_file((root_ / relative).u8string(), look, error)) << error;
    }

    fs::path root_;
};

}  // namespace

TEST_F(Library, FindsLooksInFoldersSortedByFolderAndName) {
    write("Warm/b.dyeline", "Sunset", {"orange"});
    write("Warm/a.dyeline", "amber");
    write("Cool/x.dyeline", "Steel");
    write("top.dyeline", "Plain");
    std::ofstream(root_ / "notes.txt") << "not a look";
    std::ofstream(root_ / "Cool" / "broken.dyeline") << "{ nope";

    std::vector<std::string> problems;
    const auto looks = dyeline::scan_library(
        {{root_.u8string(), true}, {(root_ / "missing").u8string(), false}}, problems);
    ASSERT_EQ(looks.size(), 4u);
    EXPECT_EQ(looks[0].look.meta.name, "Plain");
    EXPECT_EQ(looks[0].folder, "");
    EXPECT_EQ(looks[1].look.meta.name, "Steel");
    EXPECT_EQ(looks[1].folder, "Cool");
    EXPECT_EQ(looks[2].look.meta.name, "amber");
    EXPECT_EQ(looks[3].look.meta.name, "Sunset");
    EXPECT_EQ(looks[3].key, "bundled/Warm/b.dyeline");
    EXPECT_TRUE(looks[3].bundled);
    EXPECT_EQ(problems.size(), 1u);  // broken.dyeline
}

TEST_F(Library, NamesUnnamedLooksAfterTheirFile) {
    write("Quiet one.dyeline", "");
    std::vector<std::string> problems;
    const auto looks = dyeline::scan_library({{root_.u8string(), false}}, problems);
    ASSERT_EQ(looks.size(), 1u);
    EXPECT_EQ(looks[0].look.meta.name, "Quiet one");
    EXPECT_EQ(looks[0].key, "user/Quiet one.dyeline");
}

TEST(LibraryMatch, MatchesEveryWordInNameTagsOrFolder) {
    dyeline::LibraryLook look;
    look.look.meta.name = "Golden Hour";
    look.look.meta.tags = {"warm", "sun"};
    look.folder = "Warm Drama";
    EXPECT_TRUE(dyeline::library_match(look, ""));
    EXPECT_TRUE(dyeline::library_match(look, "golden"));
    EXPECT_TRUE(dyeline::library_match(look, "SUN drama"));
    EXPECT_FALSE(dyeline::library_match(look, "golden cool"));
}

TEST_F(Library, FavoritesRoundTrip) {
    const std::string path = (root_ / "deeper" / "favorites.json").u8string();
    EXPECT_TRUE(dyeline::read_favorites(path).empty());
    std::string error;
    ASSERT_TRUE(dyeline::write_favorites(path, {"bundled/Warm/a.dyeline", "user/mine.dyeline"}, error))
        << error;
    const auto keys = dyeline::read_favorites(path);
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys.count("user/mine.dyeline"), 1u);
}

TEST(LookFileName, ReplacesCharactersFileSystemsRefuse) {
    EXPECT_EQ(dyeline::look_file_name("Golden Hour"), "Golden Hour.dyeline");
    EXPECT_EQ(dyeline::look_file_name("a/b:c?"), "a_b_c_.dyeline");
    EXPECT_EQ(dyeline::look_file_name("trailing. "), "trailing.dyeline");
    EXPECT_EQ(dyeline::look_file_name(""), "Untitled.dyeline");
}

TEST_F(Library, PresetLutPathsBecomeAbsolute) {
    dyeline::LibraryLook preset;
    preset.path = (root_ / "Mine" / "look.dyeline").u8string();
    dyeline::ToolEntry lut;
    lut.tool = "lut";
    lut.params["file"] = "luts/warm.cube";
    preset.look.chain.push_back(lut);
    const dyeline::Look look = dyeline::preset_for_editing(preset);
    EXPECT_EQ(fs::u8path(look.chain[0].params["file"].get<std::string>()),
              root_ / "Mine" / "luts" / "warm.cube");
}
