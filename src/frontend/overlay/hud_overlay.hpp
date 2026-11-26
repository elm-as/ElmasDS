#pragma once

#include "frontend/renderer/gl_screen_renderer.hpp"
#include "frontend/window/display_layout.hpp"
#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elmasds::frontend {

/**
 * @brief Snapshot of emulator & UI state rendered onto the adaptive RGBA8888 overlay.
 */
struct HudState {
    bool menu_open = false;
    i32 menu_index = 0;
    bool perf_hud_open = false;
    bool rom_loaded = false;
    bool paused = false;
    bool audio_muted = false;
    bool fullscreen = false;
    bool rewind_enabled = false;
    bool controls_help_open = false;
    i32 open_dropdown = -1;
    i32 hover_menu = -1;
    i32 hover_item = -1;
    bool hover_welcome_btn = false;
    std::string rom_title;
    std::string toast_text;
    bool toast_visible = false;
    double fps = 0.0;
    double frame_ms = 0.0;
    std::size_t polys_3d = 0U;
    ScreenLayoutMode layout_mode = ScreenLayoutMode::Vertical;
    bool swap_screens = false;
    u32 native_gap = 16U;
    ShaderFilterMode filter_mode = ShaderFilterMode::SharpNearest;
    u32 speed_mult = 2U;
    u32 locked_speed_mult = 1U;
    u32 active_slot = 1U;
    std::size_t rewind_count = 0U;
    bool rewinding = false;
    bool fast_forwarding = false;
};

/**
 * @brief Adaptive-resolution software RGBA8888 renderer for menu bar, welcome UI, HUD, and modals.
 */
class HudOverlay final {
public:
    static constexpr i32 kMenuItemCount = 12;

    HudOverlay();

    /**
     * @brief Computes integer UI scale (`1` or `2`) so text and bars stay crisp at high resolution.
     */
    [[nodiscard]] static i32 compute_ui_scale(i32 pixel_w, i32 pixel_h) noexcept {
        return (pixel_w >= 1100 && pixel_h >= 720) ? 2 : 1;
    }

    /**
     * @brief Resizes the internal RGBA8888 canvas to match the host aspect ratio.
     */
    void resize(i32 width, i32 height) noexcept;

    /**
     * @brief Renders active overlay elements; returns false when nothing needs to be drawn.
     */
    [[nodiscard]] bool render(const HudState& state) noexcept;

    struct UiRect {
        i32 x = 0;
        i32 y = 0;
        i32 w = 0;
        i32 h = 0;
    };

    [[nodiscard]] std::span<const u32> pixels() const noexcept { return buffer_; }
    [[nodiscard]] i32 width() const noexcept { return width_; }
    [[nodiscard]] i32 height() const noexcept { return height_; }

private:
    void fill_rect(UiRect rect, u32 color) noexcept;
    void stroke_rect(UiRect rect, u32 color) noexcept;
    void draw_text(std::string_view text, UiRect anchor, u32 color) noexcept;
    void draw_text_centered(std::string_view text, UiRect bounds, u32 color) noexcept;
    void draw_perf_strip(const HudState& state) noexcept;
    void draw_toast(std::string_view text) noexcept;
    void draw_menu(const HudState& state) noexcept;
    void draw_top_menu_bar(const HudState& state) noexcept;
    void draw_dropdown(const HudState& state) noexcept;
    void draw_welcome_screen(const HudState& state) noexcept;
    void draw_controls_help() noexcept;

    i32 width_ = 512;
    i32 height_ = 384;
    std::vector<u32> buffer_;
};

}  // namespace elmasds::frontend
