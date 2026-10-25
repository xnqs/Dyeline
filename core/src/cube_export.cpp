#include "dyeline/cube_export.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "dyeline/chain.h"
#include "dyeline/color.h"
#include "dyeline/tools.h"

namespace dyeline {

bool bake_cube(const Look& look, int size, const std::string& base_dir, CubeExport& out,
               std::string& error) {
    out = CubeExport{};
    if (size < kMinCubeExportSize || size > kMaxCube3dSize) {
        error = "a .cube size must be " + std::to_string(kMinCubeExportSize) + " to " +
                std::to_string(kMaxCube3dSize) + " points";
        return false;
    }
    Look baked = look;
    for (ToolEntry& entry : baked.chain) {
        const ToolSpec* tool = find_tool(entry.tool);
        if (entry.enabled && tool != nullptr && !tool->color_only) {
            entry.enabled = false;
            out.left_out.push_back(std::string(tool->label) + ": depends on where the pixel is");
        }
    }
    CompiledChain chain;
    if (!compile_chain(baked, 1.0, chain, error, base_dir)) {
        return false;
    }
    for (const std::string& tool : chain.missing_tools) {
        out.left_out.push_back(tool + ": not in this build of Dyeline");
    }
    for (const std::string& problem : chain.problems) {
        out.left_out.push_back(problem);
    }
    const std::string& input_id = look.color.input;
    const ColorSpace* input = find_color_space(input_id);
    if (input != nullptr && input->curve == DL_CURVE_LINEAR) {
        out.notes.push_back("The input is scene-linear, and a .cube only covers 0 to 1, so "
                            "brighter values clip. Set the input to the footage's log space "
                            "for a LUT that keeps highlights.");
    }

    CubeLut& lut = out.lut;
    lut.title = look.meta.name.empty() ? std::string("Dyeline look") : look.meta.name;
    lut.is_3d = true;
    lut.size = size;
    const std::size_t points = static_cast<std::size_t>(size) * size * size;
    lut.data.resize(points * 3);
    const float step = 1.0f / static_cast<float>(size - 1);
    std::size_t i = 0;
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {  // red changes fastest
                lut.data[i++] = static_cast<float>(r) * step;
                lut.data[i++] = static_cast<float>(g) * step;
                lut.data[i++] = static_cast<float>(b) * step;
            }
        }
    }
    if (!chain.is_identity()) {
        render_chain_row(lut.data.data(), lut.data.data(), points, 3, false, chain);
    }
    for (float& v : lut.data) {
        if (!std::isfinite(v)) {
            v = 0.0f;
        }
    }
    return true;
}

std::string write_cube(const CubeLut& lut, const std::vector<std::string>& comments) {
    std::string text;
    for (const std::string& comment : comments) {
        text += "# " + comment + "\n";
    }
    std::string title = lut.title;
    for (char& c : title) {
        if (c == '"' || c == '\n' || c == '\r') {
            c = '\'';
        }
    }
    text += "TITLE \"" + title + "\"\n";
    text += "LUT_3D_SIZE " + std::to_string(lut.size) + "\n";
    char line[192];
    std::snprintf(line, sizeof(line), "DOMAIN_MIN %.6g %.6g %.6g\nDOMAIN_MAX %.6g %.6g %.6g\n",
                  lut.domain_min[0], lut.domain_min[1], lut.domain_min[2], lut.domain_max[0],
                  lut.domain_max[1], lut.domain_max[2]);
    text += line;
    text.reserve(text.size() + lut.data.size() * 10);
    for (std::size_t i = 0; i + 2 < lut.data.size(); i += 3) {
        std::snprintf(line, sizeof(line), "%.6f %.6f %.6f\n", lut.data[i], lut.data[i + 1],
                      lut.data[i + 2]);
        text += line;
    }
    return text;
}

bool export_cube_file(const Look& look, int size, const std::string& base_dir,
                      const std::string& path, CubeExport& out, std::string& error) {
    if (!bake_cube(look, size, base_dir, out, error)) {
        return false;
    }
    std::vector<std::string> comments = {"Made with Dyeline from the look \"" + out.lut.title + "\"."};
    for (const std::string& tool : out.left_out) {
        comments.push_back("Left out: " + tool);
    }
    namespace fs = std::filesystem;
    const fs::path target = fs::u8path(path);
    const fs::path temp = fs::u8path(path + ".tmp");
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << write_cube(out.lut, comments);
        if (!file) {
            error = "could not write " + path;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        error = "could not write " + path;
        return false;
    }
    return true;
}

}  // namespace dyeline
