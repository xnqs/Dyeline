#pragma once

// On-image handles: where a tool's position parameters sit on the preview, and the values a
// drag sets (see HandleSpec in tools.h). The preview shows the whole frame in `rect`, so a
// center at (0, 0) is the rect's middle, (1, 1) its top right corner, and a distance of 1 its
// half diagonal.

namespace dyeline::designer {

struct ScreenRect {
    float x = 0.0f;  // top left corner, in screen pixels (y down)
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

// A center's screen position, and back. The center is clamped to the frame.
ScreenPoint center_to_screen(const ScreenRect& rect, double cx, double cy);
void screen_to_center(const ScreenRect& rect, ScreenPoint at, double& cx, double& cy);

// A distance (1 at the frame's corners) in screen pixels, and the distance of a screen point
// from a center.
float distance_to_screen(const ScreenRect& rect, double distance);
double screen_distance(const ScreenRect& rect, ScreenPoint center, ScreenPoint at);

// The tip of an angle handle's arrow (degrees clockwise from up), and the angle a point at
// `at` gives, from -180 to 180.
ScreenPoint angle_tip(const ScreenRect& rect, ScreenPoint center, double degrees);
double screen_angle(ScreenPoint center, ScreenPoint at);

}  // namespace dyeline::designer
