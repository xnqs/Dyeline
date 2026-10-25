#pragma once

// Reads .cube LUT files, the text format most grading tools export: one 1D or 3D table of RGB
// values, with an optional input domain.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dyeline {

// The largest 3D LUT accepted, in points per axis. 129 points is the largest size common tools
// export, and keeps one LUT under 25 MB of floats.
constexpr int kMaxCube3dSize = 129;
constexpr int kMaxCube1dSize = 65536;

struct CubeLut {
    std::string title;
    bool is_3d = false;
    int size = 0;  // points per axis
    float domain_min[3] = {0.0f, 0.0f, 0.0f};
    float domain_max[3] = {1.0f, 1.0f, 1.0f};
    // RGB triples. A 3D table has size^3 of them with red changing fastest, then green, then
    // blue; a 1D table has `size`.
    std::vector<float> data;
};

// Parses .cube text. Fails, with `error` set, on anything it can't read exactly.
bool parse_cube(std::string_view text, CubeLut& out, std::string& error);

// Reads a .cube file, reusing the last parse while the file's size and time are unchanged.
// Returns null, with `error` set, when the file can't be read or parsed.
std::shared_ptr<const CubeLut> load_cube(const std::string& path, std::string& error);

}  // namespace dyeline
