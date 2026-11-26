#pragma once

#include "primitives/fixed_width_types.hpp"

#include <string_view>

namespace elmasds::frontend {

/**
 * @brief Discrete actions triggered by clicking French top-bar dropdown items or welcome controls.
 */
enum class MenuAction : u8 {
    None = 0,
    OpenRom,
    SaveState,
    LoadState,
    CycleSlot,
    Quit,
    TogglePause,
    ResetConsole,
    ToggleFastForward,
    ToggleRewind,
    ToggleMute,
    CycleLayout,
    ToggleSwapScreens,
    CycleGap,
    CycleFilter,
    ToggleFullscreen,
    CaptureScreenshot,
    TogglePerfHud,
    FlushSave,
    OpenQuickMenu,
    ToggleSpeedMult,
    ToggleControlsHelp,
    CloseOverlays,
};

/**
 * @brief Axis-aligned rectangle in the 512x384 overlay coordinate space.
 */
struct MenuBarRect {
    i32 x = 0;
    i32 y = 0;
    i32 w = 0;
    i32 h = 0;
};

/**
 * @brief Result of hit-testing the desktop menu bar and any open dropdown panel.
 */
struct MenuBarHit {
    i32 menu_index = -1;
    i32 item_index = -1;
    MenuAction action = MenuAction::None;
    bool consumed = false;
};

inline constexpr i32 kMenuBarHeight = 18;
inline constexpr i32 kTopMenuCount = 5;
inline constexpr i32 kDropdownItemHeight = 18;
inline constexpr MenuBarRect kWelcomeOpenButton{126, 120, 260, 28};
inline constexpr MenuBarRect kControlsCloseButton{206, 314, 100, 20};

/**
 * @brief Returns true when `(x, y)` falls strictly inside `rect`.
 */
[[nodiscard]] bool point_in_rect(MenuBarRect rect, i32 x, i32 y) noexcept;

/**
 * @brief Returns the uppercase French label for a top-level menu index.
 */
[[nodiscard]] std::string_view top_menu_label(i32 menu_index) noexcept;

/**
 * @brief Returns the overlay-space clickable rectangle for a top-level menu tab.
 */
[[nodiscard]] MenuBarRect top_menu_rect(i32 menu_index) noexcept;

/**
 * @brief Returns the number of entries inside the given top-level dropdown menu.
 */
[[nodiscard]] i32 dropdown_item_count(i32 menu_index) noexcept;

/**
 * @brief Returns the overlay-space rectangle bounding the open dropdown panel.
 */
[[nodiscard]] MenuBarRect dropdown_panel_rect(i32 menu_index) noexcept;

/**
 * @brief Returns the uppercase French label for a dropdown item.
 */
[[nodiscard]] std::string_view dropdown_item_label(i32 menu_index, i32 item_index) noexcept;

/**
 * @brief Returns the action associated with a dropdown item.
 */
[[nodiscard]] MenuAction dropdown_item_action(i32 menu_index, i32 item_index) noexcept;

/**
 * @brief Performs resolution-independent hit testing on the menu bar and active dropdown.
 */
[[nodiscard]] MenuBarHit hit_test_menu_bar(i32 ox, i32 oy, i32 open_menu) noexcept;

}  // namespace elmasds::frontend
