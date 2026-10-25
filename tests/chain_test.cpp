// Compiling looks into the fused pass, and the pass's per-pixel math on the CPU.

#include "dyeline/chain.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "dyeline/tools.h"

namespace {

using dyeline::CompiledChain;
using dyeline::Look;
using dyeline::ToolEntry;

ToolEntry exposure(double stops, double strength = 1.0) {
    ToolEntry entry;
    entry.tool = "exposure";
    entry.strength = strength;
    entry.params["stops"] = stops;
    return entry;
}

CompiledChain compile(const Look& look, double strength = 1.0) {
    CompiledChain chain;
    std::string error;
    EXPECT_TRUE(dyeline::compile_chain(look, strength, chain, error)) << error;
    return chain;
}

// Runs a chain on one straight-alpha RGBA pixel.
std::vector<float> run(const CompiledChain& chain, std::vector<float> px) {
    std::vector<float> out(4);
    dyeline::render_chain_row(px.data(), out.data(), 1, 4, false, chain);
    return out;
}

TEST(ToolRegistry, KnowsExposure) {
    const dyeline::ToolSpec* tool = dyeline::find_tool("exposure");
    ASSERT_NE(tool, nullptr);
    EXPECT_STREQ(tool->label, "Exposure");
    EXPECT_EQ(tool->version, 1);
    ASSERT_EQ(tool->params.size(), 1u);
    EXPECT_STREQ(tool->params[0].id, "stops");
    EXPECT_EQ(dyeline::find_tool("no-such-tool"), nullptr);
}

TEST(CompileChain, EmptyLookIsIdentity) {
    EXPECT_TRUE(compile(Look{}).is_identity());
}

TEST(CompileChain, SkipsToolsThatChangeNothing) {
    Look look;
    look.chain = {exposure(0.0), exposure(2.0, 0.0), exposure(1.0)};
    look.chain.push_back(exposure(3.0));
    look.chain.back().enabled = false;
    const CompiledChain chain = compile(look);
    ASSERT_EQ(chain.pass.count, 1);
    EXPECT_FLOAT_EQ(chain.pass.ops[0].p[0], 2.0f);
}

TEST(CompileChain, OverallStrengthZeroIsIdentity) {
    Look look;
    look.chain = {exposure(1.0)};
    EXPECT_TRUE(compile(look, 0.0).is_identity());
    EXPECT_FALSE(compile(look, 0.5).is_identity());
}

TEST(CompileChain, DefaultsAndClampsParameters) {
    Look look;
    look.chain = {exposure(40.0)};
    ToolEntry no_params;
    no_params.tool = "exposure";
    look.chain.push_back(no_params);  // stops defaults to 0, so it is skipped
    ToolEntry wrong_type = exposure(0.0);
    wrong_type.params["stops"] = "lots";
    look.chain.push_back(wrong_type);
    const CompiledChain chain = compile(look);
    ASSERT_EQ(chain.pass.count, 1);
    EXPECT_FLOAT_EQ(chain.pass.ops[0].p[0], 65536.0f);
}

TEST(CompileChain, BypassesUnknownAndNewerTools) {
    Look look;
    ToolEntry unknown;
    unknown.tool = "film-negative";
    ToolEntry newer = exposure(1.0);
    newer.tool_version = 99;
    look.chain = {unknown, exposure(1.0), newer};
    const CompiledChain chain = compile(look);
    EXPECT_EQ(chain.pass.count, 1);
    ASSERT_EQ(chain.missing_tools.size(), 2u);
    EXPECT_EQ(chain.missing_tools[0], "film-negative");
    EXPECT_EQ(chain.missing_tools[1], "exposure");
}

TEST(CompileChain, RefusesTooManyActiveTools) {
    Look look;
    for (int i = 0; i < DL_MAX_OPS + 1; ++i) {
        look.chain.push_back(exposure(0.1));
    }
    CompiledChain chain;
    std::string error;
    EXPECT_FALSE(dyeline::compile_chain(look, 1.0, chain, error));
    EXPECT_NE(error.find("16"), std::string::npos) << error;
}

TEST(ChainPass, ExposureScalesRgbAndKeepsAlpha) {
    Look look;
    look.chain = {exposure(1.0)};
    const auto out = run(compile(look), {0.25f, 0.5f, 1.0f, 0.75f});
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], 1.0f);
    EXPECT_FLOAT_EQ(out[2], 2.0f);
    EXPECT_FLOAT_EQ(out[3], 0.75f);
}

TEST(ChainPass, ToolsApplyInOrderWithTheirStrengths) {
    Look look;
    look.chain = {exposure(1.0), exposure(2.0, 0.5)};
    // 1.0 -> x2 = 2.0 -> halfway to x4 (8.0) = 5.0
    const auto out = run(compile(look), {1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_FLOAT_EQ(out[0], 5.0f);
}

TEST(ChainPass, OverallStrengthBlendsWithTheOriginal) {
    Look look;
    look.chain = {exposure(2.0)};
    const auto out = run(compile(look, 0.25), {1.0f, 0.0f, 2.0f, 1.0f});
    EXPECT_FLOAT_EQ(out[0], 1.75f);  // 1 + (4 - 1) * 0.25
    EXPECT_FLOAT_EQ(out[1], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 3.5f);
}

TEST(ChainPass, PremultipliedPixelsAreGradedAsStraightColor) {
    Look look;
    look.chain = {exposure(1.0)};
    const CompiledChain chain = compile(look);
    const std::vector<float> premultiplied = {0.25f, 0.125f, 0.0f, 0.5f};
    std::vector<float> out(4);
    dyeline::render_chain_row(premultiplied.data(), out.data(), 1, 4, true, chain);
    EXPECT_FLOAT_EQ(out[0], 0.5f);
    EXPECT_FLOAT_EQ(out[1], 0.25f);
    EXPECT_FLOAT_EQ(out[3], 0.5f);
    // Fully transparent pixels stay as they are.
    const std::vector<float> clear = {0.0f, 0.0f, 0.0f, 0.0f};
    dyeline::render_chain_row(clear.data(), out.data(), 1, 4, true, chain);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
}

TEST(ChainPass, HandlesRgbAndAlphaOnlyRows) {
    Look look;
    look.chain = {exposure(1.0)};
    const CompiledChain chain = compile(look);
    const std::vector<float> rgb = {0.1f, 0.2f, 0.3f, 1.0f, 2.0f, 3.0f};
    std::vector<float> out(6);
    dyeline::render_chain_row(rgb.data(), out.data(), 2, 3, false, chain);
    EXPECT_FLOAT_EQ(out[5], 6.0f);
    const std::vector<float> alpha = {0.3f, 0.7f};
    std::vector<float> alpha_out(2);
    dyeline::render_chain_row(alpha.data(), alpha_out.data(), 2, 1, false, chain);
    EXPECT_FLOAT_EQ(alpha_out[1], 0.7f);
}

TEST(ChainPass, WorksInPlace) {
    Look look;
    look.chain = {exposure(-1.0)};
    std::vector<float> px = {1.0f, 2.0f, 4.0f, 1.0f};
    dyeline::render_chain_row(px.data(), px.data(), 1, 4, false, compile(look));
    EXPECT_FLOAT_EQ(px[2], 2.0f);
}

}  // namespace
