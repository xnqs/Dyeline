#include "dyeline/exposure.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dyeline {

float exposure_gain(const ExposureParams& params) {
    const double stops = std::clamp(params.stops, kExposureMinStops, kExposureMaxStops);
    const double strength = std::clamp(params.strength, 0.0, 1.0);
    // Blending in*gain with in by strength is the same as scaling by a blended gain.
    return static_cast<float>(1.0 + (std::exp2(stops) - 1.0) * strength);
}

bool exposure_is_identity(const ExposureParams& params) {
    return exposure_gain(params) == 1.0f;
}

void apply_exposure_row(const float* src, float* dst, std::size_t pixel_count, int channels,
                        const ExposureParams& params) {
    if (channels < 3) {
        if (src != dst) {
            std::memmove(dst, src, pixel_count * static_cast<std::size_t>(channels) * sizeof(float));
        }
        return;
    }

    const float gain = exposure_gain(params);
    for (std::size_t i = 0; i < pixel_count; ++i) {
        const float* in = src + i * static_cast<std::size_t>(channels);
        float* out = dst + i * static_cast<std::size_t>(channels);
        out[0] = in[0] * gain;
        out[1] = in[1] * gain;
        out[2] = in[2] * gain;
        if (channels == 4) {
            out[3] = in[3];
        }
    }
}

}  // namespace dyeline
