// On-image handles: where position parameters sit on the preview, and back.

#include "handles.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include "dyeline/tools.h"

namespace {

using namespace dyeline::designer;

const ScreenRect kRect{100.0f, 50.0f, 400.0f, 300.0f};  // half diagonal 250

TEST(Handles, CentersMapToTheFrame) {
    const ScreenPoint middle = center_to_screen(kRect, 0.0, 0.0);
    EXPECT_FLOAT_EQ(middle.x, 300.0f);
    EXPECT_FLOAT_EQ(middle.y, 200.0f);
    const ScreenPoint top_right = center_to_screen(kRect, 1.0, 1.0);  // y is up
    EXPECT_FLOAT_EQ(top_right.x, 500.0f);
    EXPECT_FLOAT_EQ(top_right.y, 50.0f);
    double cx = 0.0;
    double cy = 0.0;
    screen_to_center(kRect, center_to_screen(kRect, 0.3, -0.6), cx, cy);
    EXPECT_NEAR(cx, 0.3, 1e-6);
    EXPECT_NEAR(cy, -0.6, 1e-6);
    screen_to_center(kRect, {0.0f, 1000.0f}, cx, cy);  // dragged off the frame
    EXPECT_DOUBLE_EQ(cx, -1.0);
    EXPECT_DOUBLE_EQ(cy, -1.0);
}

TEST(Handles, DistancesAreOneAtTheCorners) {
    EXPECT_FLOAT_EQ(distance_to_screen(kRect, 1.0), 250.0f);
    EXPECT_NEAR(screen_distance(kRect, center_to_screen(kRect, 0.0, 0.0),
                                center_to_screen(kRect, 1.0, 1.0)),
                1.0, 1e-6);
}

TEST(Handles, AnglesTurnClockwiseFromUp) {
    const ScreenPoint c{300.0f, 200.0f};
    const ScreenPoint up = angle_tip(kRect, c, 0.0);
    EXPECT_NEAR(up.x, 300.0f, 1e-4f);
    EXPECT_LT(up.y, 200.0f);
    const ScreenPoint right = angle_tip(kRect, c, 90.0);
    EXPECT_GT(right.x, 300.0f);
    EXPECT_NEAR(right.y, 200.0f, 1e-4f);
    EXPECT_NEAR(screen_angle(c, right), 90.0, 1e-4);
    EXPECT_NEAR(screen_angle(c, angle_tip(kRect, c, -135.0)), -135.0, 1e-4);
}

TEST(Handles, EveryHandleNamesARealParameter) {
    for (const dyeline::ToolSpec& tool : dyeline::all_tools()) {
        for (std::size_t i = 0; i < tool.handles.size(); ++i) {
            const dyeline::HandleSpec& handle = tool.handles[i];
            EXPECT_EQ(handle.kind == dyeline::HandleKind::Point, i == 0) << tool.id;
            for (const char* id : {handle.x, handle.y}) {
                if (id == nullptr) {
                    continue;
                }
                bool found = false;
                for (const dyeline::ParamSpec& spec : tool.params) {
                    found = found || std::strcmp(spec.id, id) == 0;
                }
                EXPECT_TRUE(found) << tool.id << " " << id;
            }
        }
    }
}

}  // namespace
