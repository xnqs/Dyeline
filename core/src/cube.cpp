#include "dyeline/cube.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>

namespace dyeline {

namespace {

// Reads exactly `count` floats from the rest of a line, in the "C" locale whatever the
// process locale is.
bool read_floats(const std::string& rest, int count, float* out) {
    std::istringstream in(rest);
    in.imbue(std::locale::classic());
    for (int i = 0; i < count; ++i) {
        if (!(in >> out[i])) {
            return false;
        }
    }
    std::string extra;
    return !(in >> extra);
}

bool read_int(const std::string& rest, int& out) {
    std::istringstream in(rest);
    in.imbue(std::locale::classic());
    std::string extra;
    return static_cast<bool>(in >> out) && !(in >> extra);
}

}  // namespace

bool parse_cube(std::string_view text, CubeLut& out, std::string& error) {
    CubeLut lut;
    int size_1d = 0;
    int size_3d = 0;
    std::size_t line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string line(text.substr(start, end - start));
        start = end + 1;
        ++line_number;
        if (const auto hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) {
            continue;
        }
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        const std::string where = "line " + std::to_string(line_number) + ": ";

        const auto space = line.find_first_of(" \t");
        const std::string keyword = line.substr(0, space);
        const std::string rest = space == std::string::npos ? std::string() : line.substr(space + 1);
        const bool is_data = !keyword.empty() && (keyword[0] == '-' || keyword[0] == '+' ||
                                                  keyword[0] == '.' ||
                                                  (keyword[0] >= '0' && keyword[0] <= '9'));
        if (is_data) {
            float rgb[3];
            if (!read_floats(line, 3, rgb)) {
                error = where + "expected three numbers";
                return false;
            }
            lut.data.insert(lut.data.end(), rgb, rgb + 3);
        } else if (!lut.data.empty()) {
            error = where + "\"" + keyword + "\" after the table data";
            return false;
        } else if (keyword == "TITLE") {
            lut.title = rest;
            if (lut.title.size() >= 2 && lut.title.front() == '"' && lut.title.back() == '"') {
                lut.title = lut.title.substr(1, lut.title.size() - 2);
            }
        } else if (keyword == "LUT_1D_SIZE") {
            if (!read_int(rest, size_1d) || size_1d < 2 || size_1d > kMaxCube1dSize) {
                error = where + "LUT_1D_SIZE must be 2 to " + std::to_string(kMaxCube1dSize);
                return false;
            }
        } else if (keyword == "LUT_3D_SIZE") {
            if (!read_int(rest, size_3d) || size_3d < 2 || size_3d > kMaxCube3dSize) {
                error = where + "LUT_3D_SIZE must be 2 to " + std::to_string(kMaxCube3dSize);
                return false;
            }
        } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            float* target = keyword == "DOMAIN_MIN" ? lut.domain_min : lut.domain_max;
            if (!read_floats(rest, 3, target)) {
                error = where + keyword + " needs three numbers";
                return false;
            }
        } else if (keyword == "LUT_1D_INPUT_RANGE" || keyword == "LUT_3D_INPUT_RANGE") {
            float range[2];
            if (!read_floats(rest, 2, range)) {
                error = where + keyword + " needs two numbers";
                return false;
            }
            for (int k = 0; k < 3; ++k) {
                lut.domain_min[k] = range[0];
                lut.domain_max[k] = range[1];
            }
        }
        // Other keywords (from other tools' dialects) don't change the table and are skipped.
    }

    if (size_1d != 0 && size_3d != 0) {
        error = "files with both a 1D and a 3D table are not supported yet";
        return false;
    }
    if (size_1d == 0 && size_3d == 0) {
        error = "no LUT_1D_SIZE or LUT_3D_SIZE";
        return false;
    }
    lut.is_3d = size_3d != 0;
    lut.size = lut.is_3d ? size_3d : size_1d;
    const std::size_t points = lut.is_3d ? static_cast<std::size_t>(lut.size) * lut.size * lut.size
                                         : static_cast<std::size_t>(lut.size);
    if (lut.data.size() != points * 3) {
        error = "expected " + std::to_string(points) + " table rows, found " +
                std::to_string(lut.data.size() / 3);
        return false;
    }
    for (int k = 0; k < 3; ++k) {
        if (!(lut.domain_max[k] > lut.domain_min[k])) {
            error = "the domain's maximum must be above its minimum";
            return false;
        }
    }
    out = std::move(lut);
    return true;
}

std::shared_ptr<const CubeLut> load_cube(const std::string& path, std::string& error) {
    namespace fs = std::filesystem;
    struct Entry {
        fs::file_time_type time;
        std::uintmax_t size;
        std::shared_ptr<const CubeLut> lut;
    };
    static std::mutex mutex;
    static std::map<std::string, Entry> cache;

    std::error_code ec;
    const fs::path file = fs::u8path(path);
    const auto time = fs::last_write_time(file, ec);
    const auto size = ec ? 0 : fs::file_size(file, ec);
    if (ec) {
        error = "could not open " + path;
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (const auto it = cache.find(path);
            it != cache.end() && it->second.time == time && it->second.size == size) {
            return it->second.lut;
        }
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "could not read " + path;
        return nullptr;
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto lut = std::make_shared<CubeLut>();
    if (!parse_cube(text.str(), *lut, error)) {
        error = path + ": " + error;
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    // Looks rarely use more than a few LUTs; start over rather than grow without bound.
    if (cache.size() >= 16) {
        cache.clear();
    }
    cache[path] = Entry{time, size, lut};
    return lut;
}

}  // namespace dyeline
