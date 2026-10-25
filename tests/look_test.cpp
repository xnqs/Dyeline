// The .dyeline look format: parsing, writing, and keeping what this build doesn't know.

#include "dyeline/look.h"

#include <gtest/gtest.h>

#include <clocale>
#include <filesystem>
#include <string>

namespace {

using dyeline::Look;

constexpr const char* kExample = R"({
  "formatVersion": 1,
  "meta": {"name": "Warm Drama", "tags": ["warm", "drama"], "author": "Robert", "license": "CC-BY-4.0"},
  "color": {"mode": "full", "input": "logc3", "output": "rec709"},
  "chain": [
    {"tool": "exposure", "toolVersion": 1, "enabled": true, "pinned": false, "strength": 0.5,
     "params": {"stops": 1.5}},
    {"tool": "future-tool", "toolVersion": 3, "params": {"stock": "5219"}, "mask": {"kind": "spot"}}
  ],
  "thumbnail": "data:image/png;base64,AAAA"
})";

Look parse(const std::string& text) {
    Look look;
    std::string error;
    EXPECT_TRUE(dyeline::parse_look(text, look, error)) << error;
    return look;
}

TEST(LookFormat, ReadsEveryField) {
    const Look look = parse(kExample);
    EXPECT_EQ(look.meta.name, "Warm Drama");
    ASSERT_EQ(look.meta.tags.size(), 2u);
    EXPECT_EQ(look.meta.tags[1], "drama");
    EXPECT_EQ(look.meta.author, "Robert");
    EXPECT_EQ(look.color.mode, "full");
    EXPECT_EQ(look.color.input, "logc3");
    EXPECT_EQ(look.color.output, "rec709");
    ASSERT_EQ(look.chain.size(), 2u);
    EXPECT_EQ(look.chain[0].tool, "exposure");
    EXPECT_DOUBLE_EQ(look.chain[0].strength, 0.5);
    EXPECT_DOUBLE_EQ(look.chain[0].params["stops"].get<double>(), 1.5);
    EXPECT_EQ(look.chain[1].tool_version, 3);
}

TEST(LookFormat, FillsDefaults) {
    const Look look = parse(R"({"formatVersion": 1, "chain": [{"tool": "exposure"}]})");
    EXPECT_EQ(look.color.mode, "tone");
    ASSERT_EQ(look.chain.size(), 1u);
    EXPECT_TRUE(look.chain[0].enabled);
    EXPECT_FALSE(look.chain[0].pinned);
    EXPECT_DOUBLE_EQ(look.chain[0].strength, 1.0);
    EXPECT_EQ(look.chain[0].tool_version, 1);
    EXPECT_TRUE(look.chain[0].params.is_object());
}

TEST(LookFormat, RoundTripsUnknownToolsAndFields) {
    const Look again = parse(dyeline::write_look(parse(kExample)));
    ASSERT_EQ(again.chain.size(), 2u);
    EXPECT_EQ(again.chain[1].tool, "future-tool");
    EXPECT_EQ(again.chain[1].params["stock"], "5219");
    EXPECT_EQ(again.chain[1].extra["mask"]["kind"], "spot");
    EXPECT_EQ(again.extra["thumbnail"], "data:image/png;base64,AAAA");
    EXPECT_EQ(again.meta.name, "Warm Drama");
}

TEST(LookFormat, RejectsBadInput) {
    Look look;
    std::string error;
    EXPECT_FALSE(dyeline::parse_look("", look, error));
    EXPECT_FALSE(dyeline::parse_look("[1, 2]", look, error));
    EXPECT_FALSE(dyeline::parse_look(R"({"chain": []})", look, error));
    EXPECT_FALSE(dyeline::parse_look(R"({"formatVersion": 1, "chain": {}})", look, error));
    EXPECT_FALSE(dyeline::parse_look(R"({"formatVersion": 1, "chain": [{"params": {}}]})", look, error));
    EXPECT_FALSE(dyeline::parse_look(
        R"({"formatVersion": 1, "chain": [{"tool": "exposure", "strength": "high"}]})", look, error));
    EXPECT_NE(error.find("strength"), std::string::npos) << error;
}

TEST(LookFormat, RefusesLooksFromANewerFormat) {
    Look look;
    std::string error;
    EXPECT_FALSE(dyeline::parse_look(R"({"formatVersion": 2, "chain": []})", look, error));
    EXPECT_NE(error.find("newer"), std::string::npos) << error;
}

TEST(LookFormat, WritesNumbersInTheCLocale) {
    // Hosts built on Qt set the user's locale, which may write 0.5 as "0,5".
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr) {
        GTEST_SKIP() << "de_DE locale not installed";
    }
    Look look;
    look.chain.push_back({});
    look.chain[0].tool = "exposure";
    look.chain[0].params["stops"] = 0.5;
    const std::string text = dyeline::write_look(look);
    std::setlocale(LC_NUMERIC, "C");
    EXPECT_NE(text.find("0.5"), std::string::npos) << text;
}

TEST(LookFile, WritesAndReadsBack) {
    const auto path = (std::filesystem::temp_directory_path() / "dyeline-look-test.dyeline").string();
    std::string error;
    ASSERT_TRUE(dyeline::write_look_file(path, parse(kExample), error)) << error;
    EXPECT_FALSE(std::filesystem::exists(path + ".partial"));
    Look read;
    ASSERT_TRUE(dyeline::read_look_file(path, read, error)) << error;
    EXPECT_EQ(read.meta.name, "Warm Drama");
    std::filesystem::remove(path);
    EXPECT_FALSE(dyeline::read_look_file(path, read, error));
    EXPECT_NE(error.find("could not open"), std::string::npos) << error;
}

}  // namespace
