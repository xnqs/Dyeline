#pragma once

// Bakes a look into a .cube 3D LUT, for tools and cameras that can't run Dyeline. A LUT maps
// a color to a color, so tools whose result also depends on the pixel's position or its
// neighbors (vignette, grain, glows) can't be held and are left out.

#include <string>
#include <vector>

#include "dyeline/cube.h"
#include "dyeline/look.h"

namespace dyeline {

constexpr int kMinCubeExportSize = 2;
constexpr int kDefaultCubeExportSize = 33;

struct CubeExport {
    CubeLut lut;
    // Tools in the look the LUT doesn't hold, each with why ("Grain: depends on position").
    std::vector<std::string> left_out;
    // Things worth knowing about the result, such as a scene-linear input clipping above 1.
    std::vector<std::string> notes;
};

// Bakes `look` at full strength, color settings included: the LUT takes values in the look's
// input space, 0 to 1, and gives values in its output space. `size` is the points per axis
// (2 to kMaxCube3dSize). Relative paths in the look (LUT files) resolve against `base_dir`.
// Fails, with `error` set, on a bad size or a look that doesn't compile.
bool bake_cube(const Look& look, int size, const std::string& base_dir, CubeExport& out,
               std::string& error);

// The .cube text for a 3D LUT, with `comments` as # lines at the top.
std::string write_cube(const CubeLut& lut, const std::vector<std::string>& comments = {});

// Bakes and writes a .cube file in one go, as the designer's Export does.
bool export_cube_file(const Look& look, int size, const std::string& base_dir,
                      const std::string& path, CubeExport& out, std::string& error);

}  // namespace dyeline
