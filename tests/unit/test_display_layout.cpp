#include "frontend/window/display_layout.hpp"

#include <gtest/gtest.h>

namespace elmasds::frontend {
namespace {

TEST(DisplayLayoutTest, UsesIntegerScalingAndCentersScreens) {
    const DualScreenLayout layout = compute_dual_screen_layout(LayoutRequest{
        1280,
        800,
        0,
        true,
    });

    // 256 * 2 = 512, 384 * 2 = 768 fits in 1280x800 (3x would need 1152 height).
    EXPECT_EQ(layout.integer_scale, 2U);
    EXPECT_EQ(layout.top_screen.width, 512);
    EXPECT_EQ(layout.top_screen.height, 384);
    EXPECT_EQ(layout.top_screen.x, (1280 - 512) / 2);
    EXPECT_EQ(layout.top_screen.y, (800 - 768) / 2);
    EXPECT_EQ(layout.bottom_screen.y, layout.top_screen.y + 384);
}

TEST(DisplayLayoutTest, FallsBackToContinuousAspectFitWhenSmallerThanOneX) {
    const DualScreenLayout layout = compute_dual_screen_layout(LayoutRequest{
        200,
        200,
        0,
        true,
    });

    EXPECT_EQ(layout.integer_scale, 0U);
    EXPECT_EQ(layout.bottom_screen.height, 100);
    EXPECT_EQ(layout.top_screen.height, 100);
    EXPECT_GT(layout.top_screen.width, 0);
    EXPECT_LE(layout.top_screen.width, 200);
}

TEST(DisplayLayoutTest, MapsWindowCoordinatesOnlyInsideBottomScreen) {
    const DualScreenLayout layout = compute_dual_screen_layout(LayoutRequest{
        512,
        768,
        0,
        true,
    });

    // Point in top screen must not register as touchscreen press.
    EXPECT_FALSE(map_window_point_to_touch(layout, WindowPoint{100, 100}).has_value());

    // Bottom screen starts at y=384; (0, 384) -> (0, 0), (511, 767) -> (255, 191).
    const auto top_left = map_window_point_to_touch(layout, WindowPoint{0, 384});
    ASSERT_TRUE(top_left.has_value());
    EXPECT_EQ(top_left->x, 0U);
    EXPECT_EQ(top_left->y, 0U);

    const auto bottom_right = map_window_point_to_touch(layout, WindowPoint{511, 767});
    ASSERT_TRUE(bottom_right.has_value());
    EXPECT_EQ(bottom_right->x, 255U);
    EXPECT_EQ(bottom_right->y, 191U);
}

}  // namespace
}  // namespace elmasds::frontend
