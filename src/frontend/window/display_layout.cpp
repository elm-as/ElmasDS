#include "frontend/window/display_layout.hpp"

#include <algorithm>
#include <cmath>

namespace elmasds::frontend {
namespace {

[[nodiscard]] i32 clamp_dimension(i32 value) noexcept {
    return value > 0 ? value : 1;
}

}  // namespace

DualScreenLayout compute_dual_screen_layout(const LayoutRequest& request) noexcept {
    const i32 win_w = clamp_dimension(request.window_width);
    const i32 win_h = clamp_dimension(request.window_height);
    const i32 total_native_h =
        static_cast<i32>(kDualScreenHeight) + static_cast<i32>(request.native_gap);

    const i32 max_int_scale = std::min(win_w / static_cast<i32>(kScreenWidth), win_h / total_native_h);
    const bool use_integer = request.prefer_integer_scale && max_int_scale >= 1;

    i32 screen_w = 0;
    i32 screen_h = 0;
    i32 gap_px = 0;
    u32 applied_scale = 0;

    if (use_integer) {
        applied_scale = static_cast<u32>(max_int_scale);
        screen_w = static_cast<i32>(kScreenWidth) * max_int_scale;
        screen_h = static_cast<i32>(kScreenHeight) * max_int_scale;
        gap_px = static_cast<i32>(request.native_gap) * max_int_scale;
    } else {
        const double scale_x = static_cast<double>(win_w) / static_cast<double>(kScreenWidth);
        const double scale_y = static_cast<double>(win_h) / static_cast<double>(total_native_h);
        const double scale = std::min(scale_x, scale_y);
        screen_w = std::max(1, static_cast<i32>(std::lround(static_cast<double>(kScreenWidth) * scale)));
        screen_h =
            std::max(1, static_cast<i32>(std::lround(static_cast<double>(kScreenHeight) * scale)));
        gap_px = static_cast<i32>(std::lround(static_cast<double>(request.native_gap) * scale));
    }

    const i32 total_px_h = (screen_h * 2) + gap_px;
    const i32 origin_x = (win_w - screen_w) / 2;
    const i32 origin_y = (win_h - total_px_h) / 2;

    return DualScreenLayout{
        ScreenRect{origin_x, origin_y, screen_w, screen_h},
        ScreenRect{origin_x, origin_y + screen_h + gap_px, screen_w, screen_h},
        applied_scale,
    };
}

std::optional<TouchPoint> map_window_point_to_touch(const DualScreenLayout& layout,
                                                    const WindowPoint& point) noexcept {
    const ScreenRect& bottom = layout.bottom_screen;
    if (bottom.width <= 0 || bottom.height <= 0) {
        return std::nullopt;
    }

    const i32 rel_x = point.x - bottom.x;
    const i32 rel_y = point.y - bottom.y;
    if (rel_x < 0 || rel_y < 0 || rel_x >= bottom.width || rel_y >= bottom.height) {
        return std::nullopt;
    }

    const u32 native_x = (static_cast<u32>(rel_x) * kScreenWidth) / static_cast<u32>(bottom.width);
    const u32 native_y = (static_cast<u32>(rel_y) * kScreenHeight) / static_cast<u32>(bottom.height);

    return TouchPoint{
        static_cast<u16>(std::min(native_x, kScreenWidth - 1U)),
        static_cast<u16>(std::min(native_y, kScreenHeight - 1U)),
    };
}

}  // namespace elmasds::frontend
