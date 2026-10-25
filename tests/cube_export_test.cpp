// Baking looks into .cube LUTs.

#include "dyeline/cube_export.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "dyeline/chain.h"

namespace {

using dyeline::CubeExport;
using dyeline::Look;
using dyeline::ToolEntry;

ToolEntry tool(const char* id, nlohmann::json params = nlohmann::json::object()) {
    ToolEntry entry;
    entry.tool = id;
    entry.params = std::move(params);
    return entry;
}

// The LUT's point for lattice indices r, g, b.
const float* point(const dyeline::CubeLut& lut, int r, int g, int b) {
    return &lut.data[((static_cast<std::size_t>(b) * lut.size + g) * lut.size + r) * 3];
}

}  // namespace

TEST(CubeExport, AnEmptyLookBakesTheIdentity) {
    CubeExport out;
    std::string error;
    ASSERT_TRUE(dyeline::bake_cube(Look{}, 5, "", out, error)) << error;
    EXPECT_TRUE(out.lut.is_3d);
    EXPECT_EQ(out.lut.size, 5);
    ASSERT_EQ(out.lut.data.size(), 5u * 5u * 5u * 3u);
    const float* p = point(out.lut, 4, 1, 2);
    EXPECT_FLOAT_EQ(p[0], 1.0f);
    EXPECT_FLOAT_EQ(p[1], 0.25f);
    EXPECT_FLOAT_EQ(p[2], 0.5f);
    EXPECT_TRUE(out.left_out.empty());
}

TEST(CubeExport, BakesColorToolsAndLeavesOutPositionalOnes) {
    Look look;
    look.chain = {tool("exposure", {{"stops", 1.0}}), tool("grain"), tool("halation"),
                  tool("lens-vignette"), tool("teleport")};
    look.chain[2].enabled = false;  // a bypassed tool isn't mentioned
    CubeExport out;
    std::string error;
    ASSERT_TRUE(dyeline::bake_cube(look, 3, "", out, error)) << error;
    EXPECT_FLOAT_EQ(point(out.lut, 1, 0, 0)[0], 1.0f);  // 0.5, one stop up
    EXPECT_FLOAT_EQ(point(out.lut, 2, 2, 2)[1], 2.0f);  // values over 1 are kept
    ASSERT_EQ(out.left_out.size(), 3u);
    EXPECT_EQ(out.left_out[0].rfind("Grain", 0), 0u);
    EXPECT_EQ(out.left_out[1].rfind("Lens Vignette", 0), 0u);
    EXPECT_EQ(out.left_out[2].rfind("teleport", 0), 0u);
    EXPECT_FALSE(out.notes.empty());  // the default input is scene-linear
}

TEST(CubeExport, IncludesTheColorTransforms) {
    Look look;
    look.color.mode = "tone";
    look.color.input = "logc3";
    look.color.output = "rec709";
    CubeExport out;
    std::string error;
    ASSERT_TRUE(dyeline::bake_cube(look, 9, "", out, error)) << error;
    EXPECT_TRUE(out.notes.empty());
    // Mid grey in LogC3 is not mid grey in Rec.709: the LUT is not the identity.
    const float* p = point(out.lut, 4, 4, 4);
    EXPECT_GT(std::fabs(p[0] - 0.5f), 0.01f);
    EXPECT_FLOAT_EQ(p[0], p[1]);
}

TEST(CubeExport, RefusesBadSizes) {
    CubeExport out;
    std::string error;
    EXPECT_FALSE(dyeline::bake_cube(Look{}, 1, "", out, error));
    EXPECT_FALSE(dyeline::bake_cube(Look{}, dyeline::kMaxCube3dSize + 1, "", out, error));
    EXPECT_FALSE(error.empty());
}

TEST(CubeExport, TheWrittenFileLoadsAsALutThatMatchesTheLook) {
    Look look;
    look.color.input = "logc3";
    look.chain = {tool("contrast", {{"contrast", 1.2}}), tool("hue-saturation", {{"saturation", 0.8}}),
                  tool("three-way", {{"shadows-amount", 0.3}})};
    const auto path = std::filesystem::temp_directory_path() / "dyeline_cube_export_test.cube";
    CubeExport out;
    std::string error;
    ASSERT_TRUE(dyeline::export_cube_file(look, 33, "", path.u8string(), out, error)) << error;

    const auto loaded = dyeline::load_cube(path.u8string(), error);
    ASSERT_NE(loaded, nullptr) << error;
    EXPECT_EQ(loaded->size, 33);
    EXPECT_EQ(loaded->title, "Dyeline look");

    // The look's own render and the LUT tool running the exported file agree closely.
    Look through_lut;
    through_lut.chain = {tool("lut", {{"file", path.u8string()}})};
    dyeline::CompiledChain direct;
    dyeline::CompiledChain via_lut;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, direct, error)) << error;
    ASSERT_TRUE(dyeline::compile_chain(through_lut, 1.0, via_lut, error)) << error;
    ASSERT_TRUE(via_lut.problems.empty()) << via_lut.problems.front();
    // Colors from LogC3's black (code 0.09) up, where footage lives.
    std::vector<float> colors;
    for (int i = 0; i < 200; ++i) {
        for (const float k : {0.137f, 0.291f, 0.613f}) {
            colors.push_back(0.1f + 0.9f * std::fmod(k * static_cast<float>(i) + k, 1.0f));
        }
    }
    std::vector<float> a(colors.size());
    std::vector<float> b(colors.size());
    dyeline::render_chain_row(colors.data(), a.data(), 200, 3, false, direct);
    dyeline::render_chain_row(colors.data(), b.data(), 200, 3, false, via_lut);
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_NEAR(a[i], b[i], 0.004f) << i;
    }
    std::filesystem::remove(path);
}
