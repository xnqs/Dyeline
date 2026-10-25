#include "scopes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace dyeline::designer {

namespace {

constexpr float kPi = 3.14159265358979f;

DisplayImage blank(int width, int height, std::uint8_t level) {
    DisplayImage out;
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * height * 4, level);
    for (std::size_t i = 3; i < out.rgba.size(); i += 4) {
        out.rgba[i] = 255;
    }
    return out;
}

// Adds `amount` (0 to 1) of a color to a pixel, saturating.
void add(DisplayImage& out, int x, int y, float r, float g, float b, float amount) {
    if (x < 0 || y < 0 || x >= out.width || y >= out.height) {
        return;
    }
    std::uint8_t* p = &out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4];
    const float rgb[3] = {r, g, b};
    for (int c = 0; c < 3; ++c) {
        p[c] = static_cast<std::uint8_t>(std::min(255.0f, p[c] + rgb[c] * amount * 255.0f));
    }
}

// How bright a trace is: soft enough that a level a few pixels share stays visible, and a
// level most of the column shares saturates.
float trace(std::uint32_t count, float gain) {
    return count == 0 ? 0.0f : 0.25f + 0.75f * (1.0f - std::exp(-static_cast<float>(count) * gain));
}

// Rec.709 chroma, each in -0.5 to 0.5, of display-encoded values in 0 to 1.
void chroma(float r, float g, float b, float& cb, float& cr) {
    const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    cb = (b - y) / 1.8556f;
    cr = (r - y) / 1.5748f;
}

// Where a chroma lands on the vectorscope: Cb to the right, Cr up.
void plot(float cb, float cr, float& x, float& y) {
    x = (0.5f + cb) * static_cast<float>(kVectorscopeSize);
    y = (0.5f - cr) * static_cast<float>(kVectorscopeSize);
}

}  // namespace

DisplayImage rgb_parade(const DisplayImage& image) {
    DisplayImage out = blank(kParadeWidth, kParadeHeight, 18);
    // The graticule: lines at 0, 25, 50, 75 and 100%.
    for (int step = 0; step <= 4; ++step) {
        const int y = std::min(kParadeHeight - 1, (kParadeHeight - 1) - step * 255 / 4);
        for (int x = 0; x < kParadeWidth; ++x) {
            add(out, x, y, 1.0f, 1.0f, 1.0f, step == 0 || step == 4 ? 0.22f : 0.12f);
        }
    }
    if (image.width <= 0 || image.height <= 0) {
        return out;
    }
    std::vector<std::uint32_t> counts(static_cast<std::size_t>(kParadeColumns) * 256 * 3, 0);
    for (int y = 0; y < image.height; ++y) {
        const std::uint8_t* row = &image.rgba[static_cast<std::size_t>(y) * image.width * 4];
        for (int x = 0; x < image.width; ++x) {
            const int column = static_cast<int>(static_cast<long long>(x) * kParadeColumns / image.width);
            for (int c = 0; c < 3; ++c) {
                ++counts[(static_cast<std::size_t>(c) * kParadeColumns + column) * 256 + row[x * 4 + c]];
            }
        }
    }
    const float per_column = static_cast<float>(image.width) * image.height / kParadeColumns;
    const float gain = 48.0f / std::max(per_column, 1.0f);
    const float colors[3][3] = {{1.0f, 0.3f, 0.3f}, {0.3f, 1.0f, 0.3f}, {0.35f, 0.5f, 1.0f}};
    for (int c = 0; c < 3; ++c) {
        const int left = c * (kParadeColumns + kParadeGap);
        for (int column = 0; column < kParadeColumns; ++column) {
            const std::uint32_t* levels = &counts[(static_cast<std::size_t>(c) * kParadeColumns + column) * 256];
            for (int level = 0; level < 256; ++level) {
                if (levels[level] != 0) {
                    add(out, left + column, 255 - level, colors[c][0], colors[c][1], colors[c][2],
                        trace(levels[level], gain));
                }
            }
        }
    }
    return out;
}

DisplayImage vectorscope(const DisplayImage& image) {
    DisplayImage out = blank(kVectorscopeSize, kVectorscopeSize, 18);
    const float center = static_cast<float>(kVectorscopeSize) * 0.5f;
    // The graticule: the edge of the chroma range, the axes, and the skin tone line, which
    // runs from the center at 123 degrees (between red and yellow).
    for (int i = 0; i < 720; ++i) {
        const float a = static_cast<float>(i) * kPi / 360.0f;
        add(out, static_cast<int>(center + std::cos(a) * (center - 1.0f)),
            static_cast<int>(center - std::sin(a) * (center - 1.0f)), 1.0f, 1.0f, 1.0f, 0.2f);
    }
    for (int i = 0; i < kVectorscopeSize; ++i) {
        add(out, i, static_cast<int>(center), 1.0f, 1.0f, 1.0f, 0.08f);
        add(out, static_cast<int>(center), i, 1.0f, 1.0f, 1.0f, 0.08f);
    }
    const float skin = 123.0f * kPi / 180.0f;
    for (int i = 0; i < static_cast<int>(center); ++i) {
        add(out, static_cast<int>(center + std::cos(skin) * static_cast<float>(i)),
            static_cast<int>(center - std::sin(skin) * static_cast<float>(i)), 1.0f, 0.75f, 0.55f,
            0.25f);
    }
    // The 75% color bar targets, as small boxes in their own color.
    const float bars[6][3] = {{0.75f, 0.0f, 0.0f}, {0.75f, 0.75f, 0.0f}, {0.0f, 0.75f, 0.0f},
                              {0.0f, 0.75f, 0.75f}, {0.0f, 0.0f, 0.75f}, {0.75f, 0.0f, 0.75f}};
    for (const auto& bar : bars) {
        float cb = 0.0f;
        float cr = 0.0f;
        chroma(bar[0], bar[1], bar[2], cb, cr);
        float x = 0.0f;
        float y = 0.0f;
        plot(cb, cr, x, y);
        for (int d = -4; d <= 4; ++d) {
            for (const auto& [dx, dy] : {std::pair{d, -4}, std::pair{d, 4}, std::pair{-4, d}, std::pair{4, d}}) {
                add(out, static_cast<int>(x) + dx, static_cast<int>(y) + dy, bar[0] + 0.25f,
                    bar[1] + 0.25f, bar[2] + 0.25f, 0.6f);
            }
        }
    }
    if (image.width <= 0 || image.height <= 0) {
        return out;
    }
    std::vector<std::uint32_t> counts(static_cast<std::size_t>(kVectorscopeSize) * kVectorscopeSize, 0);
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = &image.rgba[i * 4];
        float cb = 0.0f;
        float cr = 0.0f;
        chroma(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, cb, cr);
        float x = 0.0f;
        float y = 0.0f;
        plot(cb, cr, x, y);
        const int ix = std::clamp(static_cast<int>(x), 0, kVectorscopeSize - 1);
        const int iy = std::clamp(static_cast<int>(y), 0, kVectorscopeSize - 1);
        ++counts[static_cast<std::size_t>(iy) * kVectorscopeSize + ix];
    }
    const float gain = 1500.0f / static_cast<float>(pixels);
    for (int y = 0; y < kVectorscopeSize; ++y) {
        for (int x = 0; x < kVectorscopeSize; ++x) {
            const std::uint32_t count = counts[static_cast<std::size_t>(y) * kVectorscopeSize + x];
            if (count != 0) {
                add(out, x, y, 0.55f, 1.0f, 0.6f, trace(count, gain));
            }
        }
    }
    return out;
}

}  // namespace dyeline::designer
