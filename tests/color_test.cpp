#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "dyeline/chain.h"
#include "dyeline/color.h"
#include "dyeline/kernels/chain_kernel.h"
#include "dyeline/look.h"

using dyeline::CompiledChain;
using dyeline::Look;
using dyeline::ToolEntry;

namespace {

struct Curve {
    const char* name;
    int code;
    double grey;  // the code value of 18% grey, as the curve's specification gives it
};

const std::vector<Curve>& curves() {
    static const std::vector<Curve> list = {
        {"sRGB", DL_CURVE_SRGB, 0.461356},
        {"gamma 2.4", DL_CURVE_GAMMA_2_4, 0.489437},
        {"ACEScct", DL_CURVE_ACESCCT, 0.413588},
        {"LogC3", DL_CURVE_LOGC3, 0.391007},
        {"LogC4", DL_CURVE_LOGC4, 0.278396},
        {"S-Log3", DL_CURVE_SLOG3, 420.0 / 1023.0},
        {"V-Log", DL_CURVE_VLOG, 0.423311},
        {"Canon Log 3", DL_CURVE_CLOG3, 0.343389},
        {"Log3G10", DL_CURVE_LOG3G10, 1.0 / 3.0},
        {"DaVinci Intermediate", DL_CURVE_DAVINCI_INTERMEDIATE, 0.336043},
    };
    return list;
}

// Runs a look over one RGB pixel.
std::vector<float> render(const Look& look, std::vector<float> pixel, double strength = 1.0) {
    CompiledChain chain;
    std::string error;
    EXPECT_TRUE(dyeline::compile_chain(look, strength, chain, error)) << error;
    std::vector<float> out(3);
    dyeline::render_chain_row(pixel.data(), out.data(), 1, 3, false, chain);
    return out;
}

Look look_with(const char* mode, const char* input, const char* output) {
    Look look;
    look.color.mode = mode;
    look.color.input = input;
    look.color.output = output;
    return look;
}

void add_exposure(Look& look, double stops) {
    ToolEntry entry;
    entry.tool = "exposure";
    entry.params = {{"stops", stops}};
    look.chain.push_back(entry);
}

}  // namespace

TEST(Curves, PutMidGreyWhereTheVendorsDo) {
    for (const Curve& curve : curves()) {
        EXPECT_NEAR(dl_encode_curve(curve.code, 0.18f), curve.grey, 2e-5) << curve.name;
        EXPECT_NEAR(dl_decode_curve(curve.code, static_cast<float>(curve.grey)), 0.18, 2e-5)
            << curve.name;
    }
}

TEST(Curves, DecodeUndoesEncodeFromBelowBlackToBrightHighlights) {
    for (const Curve& curve : curves()) {
        for (double x = -0.02; x < 100.0; x = x < 0.01 ? x + 0.001 : x * 1.1) {
            const float code = dl_encode_curve(curve.code, static_cast<float>(x));
            const double back = dl_decode_curve(curve.code, code);
            EXPECT_NEAR(back, x, 2e-4 * std::max(1.0, std::fabs(x))) << curve.name << " at " << x;
        }
    }
}

// The piecewise curves meet where their pieces join. (A pure power curve has no pieces, and
// is steep enough near 0 to fail the jump check, so it's left out.)
TEST(Curves, AreContinuousAndRising) {
    for (const Curve& curve : curves()) {
        if (curve.code == DL_CURVE_GAMMA_2_4) {
            continue;
        }
        float last = dl_encode_curve(curve.code, -0.02f);
        for (double x = -0.0199; x < 0.2; x += 0.0001) {
            const float code = dl_encode_curve(curve.code, static_cast<float>(x));
            EXPECT_GT(code, last) << curve.name << " at " << x;
            EXPECT_LT(code - last, 0.01f) << curve.name << " jumps at " << x;
            last = code;
        }
    }
}

TEST(ColorSpaces, Rec709PrimariesMatchTheAcesConversion) {
    // ACEScg from linear sRGB, as published with the ACES reference configuration.
    const double aces[9] = {0.6130974, 0.3395231, 0.0473795, 0.0701937, 0.9163539,
                            0.0134524, 0.0206156, 0.1095698, 0.8698146};
    const dyeline::ColorSpace* linear = dyeline::find_color_space("linear");
    ASSERT_NE(linear, nullptr);
    for (int i = 0; i < 9; ++i) {
        EXPECT_NEAR(linear->to_working[i], aces[i], 2e-5) << i;
    }
}

TEST(ColorSpaces, KeepWhiteWhite) {
    for (const dyeline::ColorSpace& space : dyeline::all_color_spaces()) {
        for (int row = 0; row < 3; ++row) {
            const double sum = space.to_working[row * 3] + space.to_working[row * 3 + 1] +
                               space.to_working[row * 3 + 2];
            EXPECT_NEAR(sum, 1.0, 1e-9) << space.id << " row " << row;
        }
    }
}

TEST(ColorSpaces, WideGamutsReachPastAcescgsReds) {
    // A pure LogC3 red is more saturated than any Rec.709 red, so it lands further out in AP1.
    const dyeline::ColorSpace* logc3 = dyeline::find_color_space("logc3");
    const dyeline::ColorSpace* rec709 = dyeline::find_color_space("rec709");
    ASSERT_NE(logc3, nullptr);
    ASSERT_NE(rec709, nullptr);
    EXPECT_GT(logc3->to_working[0], rec709->to_working[0]);
    EXPECT_LT(logc3->to_working[3], rec709->to_working[3]);
}

TEST(ColorManagement, DefaultsChangeNothing) {
    Look look;
    CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error));
    EXPECT_TRUE(chain.is_identity());
    EXPECT_TRUE(dyeline::is_identity(chain.pass.input));
    EXPECT_TRUE(dyeline::is_identity(chain.pass.output));
}

TEST(ColorManagement, FullModeBringsCameraGreyToWorkingGrey) {
    const Look look = look_with("full", "logc3", "acescg");
    const float grey = dl_encode_curve(DL_CURVE_LOGC3, 0.18f);
    const std::vector<float> out = render(look, {grey, grey, grey});
    for (float v : out) {
        EXPECT_NEAR(v, 0.18f, 1e-5f);
    }
}

TEST(ColorManagement, ToneOnlyConvertsEachChannelOnItsOwn) {
    const Look look = look_with("tone", "logc3", "linear");
    const std::vector<float> out = render(look, {0.6f, 0.39f, 0.2f});
    EXPECT_FLOAT_EQ(out[0], dl_decode_curve(DL_CURVE_LOGC3, 0.6f));
    EXPECT_FLOAT_EQ(out[1], dl_decode_curve(DL_CURVE_LOGC3, 0.39f));
    EXPECT_FLOAT_EQ(out[2], dl_decode_curve(DL_CURVE_LOGC3, 0.2f));
}

TEST(ColorManagement, FullModeConvertsTheGamut) {
    // Linear Rec.709 red in, ACEScg out: the red of the published matrix's first column.
    const Look look = look_with("full", "linear", "acescg");
    const std::vector<float> out = render(look, {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(out[0], 0.6130974f, 1e-5f);
    EXPECT_NEAR(out[1], 0.0701937f, 1e-5f);
    EXPECT_NEAR(out[2], 0.0206156f, 1e-5f);
}

TEST(ColorManagement, ToolsWorkInLinearLightAndTheOutputIsEncodedBack) {
    for (const char* mode : {"tone", "full"}) {
        Look look = look_with(mode, "slog3", "same");
        add_exposure(look, 1.0);
        const float grey = dl_encode_curve(DL_CURVE_SLOG3, 0.18f);
        const std::vector<float> out = render(look, {grey, grey, grey});
        for (float v : out) {
            EXPECT_NEAR(v, dl_encode_curve(DL_CURVE_SLOG3, 0.36f), 1e-5f) << mode;
        }
    }
}

TEST(ColorManagement, ASameSpaceRoundTripIsAnIdentity) {
    Look look = look_with("full", "canon-log3", "same");
    CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error));
    EXPECT_TRUE(chain.is_identity());
    const std::vector<float> out = render(look, {0.6f, 0.39f, 0.2f});
    EXPECT_NEAR(out[0], 0.6f, 1e-5f);
    EXPECT_NEAR(out[1], 0.39f, 1e-5f);
    EXPECT_NEAR(out[2], 0.2f, 1e-5f);
}

TEST(ColorManagement, StrengthBlendsTheToolsButAlwaysConverts) {
    Look look = look_with("tone", "logc3", "rec709");
    add_exposure(look, 2.0);
    CompiledChain chain;
    std::string error;
    ASSERT_TRUE(dyeline::compile_chain(look, 0.0, chain, error));
    EXPECT_FALSE(chain.is_identity());
    const float grey = dl_encode_curve(DL_CURVE_LOGC3, 0.18f);
    const std::vector<float> out = render(look, {grey, grey, grey}, 0.0);
    EXPECT_NEAR(out[0], dl_encode_curve(DL_CURVE_GAMMA_2_4, 0.18f), 1e-5f);
}

TEST(ColorManagement, UnknownSettingsAreReportedAndLeftOut) {
    for (const Look& look : {look_with("tone", "betacam", "same"),
                             look_with("tone", "logc3", "betacam"),
                             look_with("partial", "logc3", "same")}) {
        CompiledChain chain;
        std::string error;
        ASSERT_TRUE(dyeline::compile_chain(look, 1.0, chain, error));
        ASSERT_EQ(chain.problems.size(), 1u);
        EXPECT_EQ(chain.problems[0].rfind("Color: unknown", 0), 0u) << chain.problems[0];
        EXPECT_TRUE(dyeline::is_identity(chain.pass.input));
        EXPECT_TRUE(dyeline::is_identity(chain.pass.output));
    }
}
