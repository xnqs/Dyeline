#pragma once

// A still frame the plugin hands the Look Designer for its preview: the source image at the
// current time, scaled down to preview size, with what the renderer needs to place it in the
// frame, so position-dependent and spatial tools look as they do in the host.
//
// On disk it is a small header and then the pixels as little-endian float32 RGBA, rows from
// the bottom up as in OFX.

#include <cstdint>
#include <string>
#include <vector>

namespace dyeline {

struct Frame {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;    // width * height * 4 floats, bottom row first
    bool premultiplied = false;
    // The first pixel's position in the host's pixel coordinates at this scale, and the rest
    // of the render geometry (see RenderGeometry).
    int x0 = 0;
    int y0 = 0;
    float canonical_x = 1.0f;
    float canonical_y = 1.0f;
    float frame_left = 0.0f;
    float frame_bottom = 0.0f;
    float frame_right = 0.0f;
    float frame_top = 0.0f;
    int seed = 0;
};

bool write_frame_file(const std::string& path, const Frame& frame, std::string& error);
bool read_frame_file(const std::string& path, Frame& frame, std::string& error);

// Copies an image with `channels` (1, 3 or 4) floats per pixel and rows `row_floats` apart
// into a frame, averaging boxes of whole pixels so it fits in max_width x max_height. The
// geometry fields are scaled to match; the caller sets them for the full-size image first.
Frame shrink_to_fit(const float* pixels, int width, int height, int channels, int row_floats,
                    const Frame& geometry, int max_width, int max_height);

}  // namespace dyeline
