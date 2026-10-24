#pragma once

#include <cstddef>

namespace dyeline {

// Exposure tool: scales scene-linear RGB by a power of two, like opening or closing the
// lens by whole or fractional stops. Alpha is never changed.
struct ExposureParams {
    double stops = 0.0;     // -16 to +16; +1 doubles the light, -1 halves it
    double strength = 1.0;  // 0 to 1; blends between the untouched and the exposed image
};

inline constexpr double kExposureMinStops = -16.0;
inline constexpr double kExposureMaxStops = 16.0;

// The gain applied to RGB, with strength already folded in.
float exposure_gain(const ExposureParams& params);

// True when the tool would leave every pixel unchanged, so the host can skip it.
bool exposure_is_identity(const ExposureParams& params);

// Processes one row of interleaved float pixels. `channels` is 1 (alpha only), 3 (RGB) or
// 4 (RGBA). An alpha-only row is copied unchanged. `src` and `dst` may be the same row.
//
// Because exposure multiplies RGB by a constant, it gives the same result on premultiplied
// and straight-alpha pixels, so the row is processed as is.
void apply_exposure_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                        const ExposureParams& params);

}  // namespace dyeline
