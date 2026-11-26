#include "frontend/application/rewind_buffer.hpp"
#include "frontend/application/screenshot_writer.hpp"
#include "frontend/overlay/menu_bar.hpp"
#include "frontend/window/display_layout.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <vector>

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

TEST(DisplayLayoutTest, SupportsSideBySideHybridAndSwapScreenModes) {
    const DualScreenLayout sbs = compute_dual_screen_layout(LayoutRequest{
        1200, 600, 16, true, ScreenLayoutMode::SideBySide, false});
    EXPECT_EQ(sbs.integer_scale, 2U);
    EXPECT_EQ(sbs.top_screen.y, sbs.bottom_screen.y);
    EXPECT_GT(sbs.bottom_screen.x, sbs.top_screen.x + sbs.top_screen.width);

    const DualScreenLayout hybrid_bot = compute_dual_screen_layout(LayoutRequest{
        1280, 720, 12, false, ScreenLayoutMode::HybridBottom, false});
    EXPECT_GT(hybrid_bot.bottom_screen.width, hybrid_bot.top_screen.width);
    EXPECT_LT(hybrid_bot.bottom_screen.x, hybrid_bot.top_screen.x);

    const DualScreenLayout swapped = compute_dual_screen_layout(LayoutRequest{
        512, 768, 0, true, ScreenLayoutMode::Vertical, true});
    EXPECT_GT(swapped.top_screen.y, swapped.bottom_screen.y);
    EXPECT_TRUE(map_window_point_to_touch(swapped, WindowPoint{100, 100}).has_value());
}

TEST(DisplayLayoutTest, ReservesTopInsetWithoutOverlappingTouchMapping) {
    const DualScreenLayout layout = compute_dual_screen_layout(LayoutRequest{
        512, 800, 0, true, ScreenLayoutMode::Vertical, false, 32});
    EXPECT_EQ(layout.integer_scale, 2U);
    EXPECT_GE(layout.top_screen.y, 32);
    EXPECT_EQ(layout.bottom_screen.y, layout.top_screen.y + 384);
    EXPECT_FALSE(map_window_point_to_touch(layout, WindowPoint{100, 10}).has_value());
    EXPECT_TRUE(map_window_point_to_touch(layout, WindowPoint{100, layout.bottom_screen.y + 10}).has_value());
}

TEST(RewindAndScreenshotTest, RewindRingBufferAndDualScreenBmpWriterWork) {
    RewindBuffer rewind(2U);
    rewind.push({1U});
    rewind.push({2U});
    rewind.push({3U});
    EXPECT_EQ(rewind.size(), 2U);
    EXPECT_EQ(rewind.pop().value(), std::vector<u8>{3U});
    EXPECT_EQ(rewind.pop().value(), std::vector<u8>{2U});
    EXPECT_FALSE(rewind.pop().has_value());

    constexpr std::size_t kPixels = static_cast<std::size_t>(kScreenWidth) * kScreenHeight;
    std::vector<u32> top(kPixels, 0xFF112233U);
    std::vector<u32> bottom(kPixels, 0xFF445566U);
    const auto path = std::filesystem::temp_directory_path() / "elmasds_test_shot.bmp";
    ASSERT_TRUE(save_dual_screen_bmp(path, top, bottom).is_ok());

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(in.is_open());
    EXPECT_EQ(static_cast<std::size_t>(in.tellg()), 54U + (256U * 384U * 3U));
    in.seekg(0);
    char sig[2]{};
    in.read(sig, 2);
    EXPECT_EQ(sig[0], 'B');
    EXPECT_EQ(sig[1], 'M');
    in.close();
    const auto open_hit = hit_test_menu_bar(20, 8, -1);
    EXPECT_TRUE(open_hit.consumed);
    EXPECT_EQ(open_hit.menu_index, 0);
    EXPECT_EQ(top_menu_label(0), "FICHIER");

    const auto item_hit = hit_test_menu_bar(40, kMenuBarHeight + 10, 0);
    EXPECT_TRUE(item_hit.consumed);
    EXPECT_EQ(item_hit.action, MenuAction::OpenRom);

    const auto x2_hit = hit_test_menu_bar(80, kMenuBarHeight + 4 + kDropdownItemHeight, 1);
    EXPECT_TRUE(x2_hit.consumed);
    EXPECT_EQ(x2_hit.action, MenuAction::ToggleSpeed2x);

    const auto x4_hit = hit_test_menu_bar(80, kMenuBarHeight + 4 + (2 * kDropdownItemHeight), 1);
    EXPECT_TRUE(x4_hit.consumed);
    EXPECT_EQ(x4_hit.action, MenuAction::ToggleSpeed4x);

    const auto outside_hit = hit_test_menu_bar(480, 300, 0);
    EXPECT_TRUE(outside_hit.consumed);
    EXPECT_EQ(outside_hit.action, MenuAction::CloseOverlays);
    EXPECT_TRUE(point_in_rect(kWelcomeOpenButton, 200, 130));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace
}  // namespace elmasds::frontend
