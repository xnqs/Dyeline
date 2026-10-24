#include "dyeline/exposure.h"
#include "dyeline/kernels/exposure_kernel.h"

#include <algorithm>
#include <cmath>

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
    // The CPU path runs the same per-pixel kernel code as the GPU backends.
    dl_exposure_args args{};
    args.width = static_cast<int>(pixel_count);
    args.height = 1;
    args.channels = channels;
    args.gain = exposure_gain(params);
    for (int x = 0; x < args.width; ++x) {
        dl_exposure_at(src, dst, x, 0, args);
    }
}

}  // namespace dyeline
