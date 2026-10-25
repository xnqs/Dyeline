#include "dyeline/frame_file.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

namespace dyeline {

namespace {

constexpr char kMagic[8] = {'D', 'L', 'F', 'R', 'A', 'M', 'E', '1'};
// A preview never needs more; the limit stops a bad file asking for a huge allocation.
constexpr int kMaxSide = 16384;

template <typename T>
void put(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T>
bool get(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(value)));
}

int floor_div(int a, int b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

}  // namespace

bool write_frame_file(const std::string& path, const Frame& frame, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "can't write " + path;
        return false;
    }
    out.write(kMagic, sizeof(kMagic));
    put<std::int32_t>(out, frame.width);
    put<std::int32_t>(out, frame.height);
    put<std::int32_t>(out, frame.premultiplied ? 1 : 0);
    put<std::int32_t>(out, frame.x0);
    put<std::int32_t>(out, frame.y0);
    put<std::int32_t>(out, frame.seed);
    for (float v : {frame.canonical_x, frame.canonical_y, frame.frame_left, frame.frame_bottom,
                    frame.frame_right, frame.frame_top}) {
        put<float>(out, v);
    }
    out.write(reinterpret_cast<const char*>(frame.rgba.data()),
              static_cast<std::streamsize>(frame.rgba.size() * sizeof(float)));
    if (!out) {
        error = "can't write " + path;
        return false;
    }
    return true;
}

bool read_frame_file(const std::string& path, Frame& frame, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    char magic[sizeof(kMagic)] = {};
    if (!in || !in.read(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        error = path + " is not a Dyeline frame";
        return false;
    }
    Frame f;
    std::int32_t width = 0, height = 0, premultiplied = 0, x0 = 0, y0 = 0, seed = 0;
    bool ok = get(in, width) && get(in, height) && get(in, premultiplied) && get(in, x0) &&
              get(in, y0) && get(in, seed);
    for (float* v : {&f.canonical_x, &f.canonical_y, &f.frame_left, &f.frame_bottom,
                     &f.frame_right, &f.frame_top}) {
        ok = ok && get(in, *v);
    }
    if (!ok || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) {
        error = path + " has a bad header";
        return false;
    }
    f.width = width;
    f.height = height;
    f.premultiplied = premultiplied != 0;
    f.x0 = x0;
    f.y0 = y0;
    f.seed = seed;
    f.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    if (!in.read(reinterpret_cast<char*>(f.rgba.data()),
                 static_cast<std::streamsize>(f.rgba.size() * sizeof(float)))) {
        error = path + " is cut short";
        return false;
    }
    frame = std::move(f);
    return true;
}

Frame shrink_to_fit(const float* pixels, int width, int height, int channels, int row_floats,
                    const Frame& geometry, int max_width, int max_height) {
    const int factor = std::max({1, (width + max_width - 1) / std::max(max_width, 1),
                                 (height + max_height - 1) / std::max(max_height, 1)});
    Frame out = geometry;
    out.width = (width + factor - 1) / factor;
    out.height = (height + factor - 1) / factor;
    out.x0 = floor_div(geometry.x0, factor);
    out.y0 = floor_div(geometry.y0, factor);
    out.canonical_x = geometry.canonical_x * static_cast<float>(factor);
    out.canonical_y = geometry.canonical_y * static_cast<float>(factor);
    out.rgba.assign(static_cast<std::size_t>(out.width) * out.height * 4, 0.0f);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int count = 0;
            for (int sy = y * factor; sy < std::min(height, (y + 1) * factor); ++sy) {
                const float* row = pixels + static_cast<std::ptrdiff_t>(sy) * row_floats;
                for (int sx = x * factor; sx < std::min(width, (x + 1) * factor); ++sx) {
                    const float* p = row + static_cast<std::ptrdiff_t>(sx) * channels;
                    if (channels == 1) {
                        sum[3] += p[0];
                    } else {
                        sum[0] += p[0];
                        sum[1] += p[1];
                        sum[2] += p[2];
                        sum[3] += channels == 4 ? p[3] : 1.0f;
                    }
                    ++count;
                }
            }
            float* o = &out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4];
            for (int c = 0; c < 4; ++c) {
                o[c] = sum[c] / static_cast<float>(count);
            }
        }
    }
    return out;
}

}  // namespace dyeline
