#pragma once

#include "frontend/renderer/gl_screen_renderer.hpp"
#include "frontend/window/display_layout.hpp"
#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace elmasds::frontend {

/**
 * @brief Snapshot of emulator & UI state rendered lazily onto the on-demand overlay.
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
 * @brief Software RGBA8888 renderer for the on-demand Esc menu, F3 perf strip, and status toasts.
 */
class HudOverlay final {
public:
    static constexpr i32 kWidth = 512;
    static constexpr i32 kHeight = 384;
    static constexpr i32 kMenuItemCount = 12;

    HudOverlay();

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
    [[nodiscard]] i32 width() const noexcept { return kWidth; }
    [[nodiscard]] i32 height() const noexcept { return kHeight; }

private:
    void fill_rect(UiRect rect, u32 color) noexcept;
    void stroke_rect(UiRect rect, u32 color) noexcept;
    void draw_text(std::string_view text, UiRect anchor, u32 color) noexcept;
    void draw_perf_strip(const HudState& state) noexcept;
    void draw_toast(std::string_view text) noexcept;
    void draw_menu(const HudState& state) noexcept;
    void draw_top_menu_bar(const HudState& state) noexcept;
    void draw_dropdown(const HudState& state) noexcept;
    void draw_welcome_screen(const HudState& state) noexcept;
    void draw_controls_help() noexcept;

    std::vector<u32> buffer_;
};

}  // namespace elmasds::frontend
