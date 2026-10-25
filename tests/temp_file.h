#pragma once

// Test files on disk: a file with given contents that is removed when the test is done, and
// .cube LUT text built from a function.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <locale>
#include <random>
#include <sstream>
#include <string>

namespace dyeline::test {

class TempFile {
public:
    // Writes `contents` to a new file in the temp folder; `suffix` is its extension. Each
    // file gets a random name, so tests running at once never share one.
    TempFile(const std::string& contents, const std::string& suffix) {
        std::random_device random;
        path_ = std::filesystem::temp_directory_path() /
                ("dyeline-test-" + std::to_string(random()) + std::to_string(random()) + suffix);
        std::ofstream(path_, std::ios::binary) << contents;
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string path() const { return path_.u8string(); }
    std::string folder() const { return path_.parent_path().u8string(); }
    std::string name() const { return path_.filename().u8string(); }

private:
    std::filesystem::path path_;
};

using RgbFunction = std::function<void(double r, double g, double b, double out[3])>;

// A 3D .cube of `size` points per axis sampling `fn` over [0, 1], red changing fastest.
inline std::string cube_3d(int size, const RgbFunction& fn) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(9);
    out << "TITLE \"test\"\n# a comment\nLUT_3D_SIZE " << size << "\n\n";
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                double v[3];
                fn(r / (size - 1.0), g / (size - 1.0), b / (size - 1.0), v);
                out << v[0] << ' ' << v[1] << ' ' << v[2] << '\n';
            }
        }
    }
    return out.str();
}

}  // namespace dyeline::test
