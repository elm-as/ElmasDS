#pragma once

#include "primitives/fixed_width_types.hpp"

#include <optional>

namespace elmasds::frontend {

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
};

/**
 * @brief Computed viewports for both stacked Nintendo DS screens inside a window.
 */
struct DualScreenLayout {
    ScreenRect top_screen{};
    ScreenRect bottom_screen{};
    u32 integer_scale = 1;
};

/**
 * @brief Computes aspect-preserving viewports for the stacked dual 256x192 DS screens.
 * @param request Host window dimensions, native vertical gap, and integer scaling flag.
 * @return Centered top and bottom screen rectangles in window pixel coordinates.
 */
[[nodiscard]] DualScreenLayout compute_dual_screen_layout(const LayoutRequest& request) noexcept;

/**
 * @brief Maps a window-space pointer position to native bottom-screen coordinates.
 * @param layout Active dual-screen viewport layout.
 * @param point Pointer position in window pixels (origin at top-left).
 * @return Native (0..255, 0..191) coordinate if inside the bottom screen; std::nullopt otherwise.
 */
[[nodiscard]] std::optional<TouchPoint> map_window_point_to_touch(
    const DualScreenLayout& layout,
    const WindowPoint& point) noexcept;

}  // namespace elmasds::frontend
