#pragma once

#include "primitives/fixed_width_types.hpp"

#include <optional>

namespace elmasds::frontend {

/**
 * @brief Supported dual-screen arrangement presets for the frontend window.
 */
enum class ScreenLayoutMode : u8 {
    Vertical = 0,
    SideBySide = 1,
    HybridTop = 2,
    HybridBottom = 3,
};

/**
 * @brief Axis-aligned pixel rectangle in window coordinates.
 */
struct ScreenRect {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
};

/**
 * @brief Native Nintendo DS touchscreen coordinate in [0..255] x [0..191].
 */
struct TouchPoint {
    u16 x = 0;
    u16 y = 0;
};

/**
 * @brief Window-space pointer coordinates used for touchscreen mapping.
 */
struct WindowPoint {
    i32 x = 0;
    i32 y = 0;
};

/**
 * @brief Input parameters describing the host window and scaling preferences.
 */
struct LayoutRequest {
    i32 window_width = 512;
    i32 window_height = 768;
    u32 native_gap = 0;
    bool prefer_integer_scale = true;
    ScreenLayoutMode mode = ScreenLayoutMode::Vertical;
    bool swap_screens = false;
    i32 top_inset = 0;
};

/**
 * @brief Computed viewports for both Nintendo DS screens inside a window.
 */
struct DualScreenLayout {
    ScreenRect top_screen{};
    ScreenRect bottom_screen{};
    u32 integer_scale = 1;
};

/**
 * @brief Computes aspect-preserving viewports for both 256x192 DS screens according to `request.mode`.
 */
[[nodiscard]] DualScreenLayout compute_dual_screen_layout(const LayoutRequest& request) noexcept;

/**
 * @brief Maps a window-space pointer position to native bottom-screen coordinates.
 */
[[nodiscard]] std::optional<TouchPoint> map_window_point_to_touch(
    const DualScreenLayout& layout,
    const WindowPoint& point) noexcept;

}  // namespace elmasds::frontend
