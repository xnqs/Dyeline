#include "handles.h"

#include <algorithm>
#include <cmath>

namespace dyeline::designer {

namespace {

constexpr double kPi = 3.14159265358979323846;

// An angle handle's arrow, as a share of the half diagonal.
constexpr double kArrowLength = 0.25;

double half_diagonal(const ScreenRect& rect) {
    return 0.5 * std::sqrt(static_cast<double>(rect.width) * rect.width +
                           static_cast<double>(rect.height) * rect.height);
}

}  // namespace

ScreenPoint center_to_screen(const ScreenRect& rect, double cx, double cy) {
    return {static_cast<float>(rect.x + 0.5 * rect.width * (1.0 + cx)),
            static_cast<float>(rect.y + 0.5 * rect.height * (1.0 - cy))};
}

void screen_to_center(const ScreenRect& rect, ScreenPoint at, double& cx, double& cy) {
    cx = rect.width > 0.0f ? std::clamp(2.0 * (at.x - rect.x) / rect.width - 1.0, -1.0, 1.0) : 0.0;
    cy = rect.height > 0.0f ? std::clamp(1.0 - 2.0 * (at.y - rect.y) / rect.height, -1.0, 1.0) : 0.0;
}

float distance_to_screen(const ScreenRect& rect, double distance) {
    return static_cast<float>(distance * half_diagonal(rect));
}

double screen_distance(const ScreenRect& rect, ScreenPoint center, ScreenPoint at) {
    const double d = half_diagonal(rect);
    return d > 0.0 ? std::hypot(at.x - center.x, at.y - center.y) / d : 0.0;
}

ScreenPoint angle_tip(const ScreenRect& rect, ScreenPoint center, double degrees) {
    const double a = degrees * kPi / 180.0;
    const double length = kArrowLength * half_diagonal(rect);
    return {static_cast<float>(center.x + std::sin(a) * length),
            static_cast<float>(center.y - std::cos(a) * length)};
}

double screen_angle(ScreenPoint center, ScreenPoint at) {
    return std::atan2(at.x - center.x, center.y - at.y) * 180.0 / kPi;
}

}  // namespace dyeline::designer
