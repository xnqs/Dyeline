// The .cube reader: the format's keywords, its failure cases, and the file cache.

#include "dyeline/cube.h"

#include <gtest/gtest.h>

#include <clocale>
#include <filesystem>
#include <fstream>
#include <string>

#include "temp_file.h"

namespace {

using dyeline::CubeLut;

TEST(Cube, Parses3dWithTitleDomainAndComments) {
    const std::string text =
        "# made by hand\n"
        "TITLE \"Warm\"\n"
        "LUT_3D_SIZE 2\r\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 1 2 4   # wider for green and blue\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    CubeLut lut;
    std::string error;
    ASSERT_TRUE(dyeline::parse_cube(text, lut, error)) << error;
    EXPECT_EQ(lut.title, "Warm");
    EXPECT_TRUE(lut.is_3d);
    EXPECT_EQ(lut.size, 2);
    EXPECT_FLOAT_EQ(lut.domain_max[1], 2.0f);
    EXPECT_FLOAT_EQ(lut.domain_max[2], 4.0f);
    ASSERT_EQ(lut.data.size(), 24u);
    EXPECT_FLOAT_EQ(lut.data[3], 1.0f);  // the second row is red's next point
}

TEST(Cube, Parses1dWithAnInputRange) {
    CubeLut lut;
    std::string error;
    ASSERT_TRUE(dyeline::parse_cube("LUT_1D_SIZE 3\nLUT_1D_INPUT_RANGE -0.5 1.5\n"
                                    "0 0 0\n0.5 .25 +0.1\n1 1 1\n",
                                    lut, error))
        << error;
    EXPECT_FALSE(lut.is_3d);
    EXPECT_EQ(lut.size, 3);
    EXPECT_FLOAT_EQ(lut.domain_min[0], -0.5f);
    EXPECT_FLOAT_EQ(lut.domain_max[2], 1.5f);
    EXPECT_FLOAT_EQ(lut.data[4], 0.25f);
}

TEST(Cube, ReadsNumbersTheSameInAnyLocale) {
    // In a locale with decimal commas, a careless reader would stop at the point.
    const char* previous = std::setlocale(LC_ALL, nullptr);
    const std::string saved = previous != nullptr ? previous : "C";
    if (std::setlocale(LC_ALL, "de_DE.UTF-8") == nullptr) {
        GTEST_SKIP() << "no de_DE.UTF-8 locale";
    }
    std::locale::global(std::locale(""));
    CubeLut lut;
    std::string error;
    const bool ok = dyeline::parse_cube("LUT_1D_SIZE 2\n0.25 0.5 0.75\n1 1 1\n", lut, error);
    std::locale::global(std::locale::classic());
    std::setlocale(LC_ALL, saved.c_str());
    ASSERT_TRUE(ok) << error;
    EXPECT_FLOAT_EQ(lut.data[0], 0.25f);
}

TEST(Cube, RefusesWhatItCantReadExactly) {
    const char* bad[] = {
        "",                                                // no size
        "LUT_3D_SIZE 2\n0 0 0\n",                          // too few rows
        "LUT_1D_SIZE 2\n0 0 0\n1 1 1\n1 1 1\n",            // too many rows
        "LUT_1D_SIZE 1\n0 0 0\n",                          // too small
        "LUT_3D_SIZE 300\n",                               // too large
        "LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n",                  // both kinds
        "LUT_1D_SIZE 2\n0 0\n1 1 1\n",                     // short row
        "LUT_1D_SIZE 2\n0 0 0 0\n1 1 1\n",                 // long row
        "LUT_1D_SIZE 2\n0 0 x\n1 1 1\n",                   // not a number
        "LUT_1D_SIZE 2\n0 0 0\nTITLE \"late\"\n1 1 1\n",   // keyword inside the data
        "LUT_1D_SIZE 2\nDOMAIN_MIN 1 0 0\nDOMAIN_MAX 1 1 1\n0 0 0\n1 1 1\n",  // empty domain
    };
    for (const char* text : bad) {
        CubeLut lut;
        std::string error;
        EXPECT_FALSE(dyeline::parse_cube(text, lut, error)) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
}

TEST(Cube, LoadReusesTheParseUntilTheFileChanges) {
    const dyeline::test::TempFile file("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n", ".cube");
    std::string error;
    const auto first = dyeline::load_cube(file.path(), error);
    ASSERT_NE(first, nullptr) << error;
    EXPECT_EQ(dyeline::load_cube(file.path(), error), first);

    std::ofstream(file.path(), std::ios::binary) << "LUT_1D_SIZE 3\n0 0 0\n0.5 0.5 0.5\n1 1 1\n";
    const auto second = dyeline::load_cube(file.path(), error);
    ASSERT_NE(second, nullptr) << error;
    EXPECT_EQ(second->size, 3);

    EXPECT_EQ(dyeline::load_cube(file.path() + ".missing", error), nullptr);
    EXPECT_NE(error.find("could not open"), std::string::npos);
}

}  // namespace
