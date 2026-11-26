#pragma once

#include "frontend/application/rewind_buffer.hpp"
#include "frontend/overlay/hud_overlay.hpp"
#include "frontend/renderer/gl_screen_renderer.hpp"
#include "frontend/window/display_layout.hpp"
#include "input/input_mapping.hpp"
#include "primitives/fixed_width_types.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace elmasds::frontend {

/**
 * @brief Mutable frontend state driving the SDL3 window, OpenGL shaders, rewind, and HUD menu.
 */
struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext gl_context = nullptr;
    SDL_AudioStream* audio_stream = nullptr;
    GlScreenRenderer renderer;
    HudOverlay overlay;
    input::InputKeyMap key_map;
    RewindBuffer rewind_buffer{40U};

    std::filesystem::path loaded_rom_path;
    std::filesystem::path pending_rom_path;
    std::mutex dialog_mutex;
    std::string rom_title;

    bool running = true;
    bool paused = false;
    bool menu_open = false;
    i32 menu_index = 0;
    bool perf_hud_open = false;
    bool fullscreen = false;
    bool mouse_down = false;
    bool fast_forwarding = false;
    bool rewind_enabled = false;
    bool rewinding = false;
    bool audio_muted = false;
    bool controls_help_open = false;
    i32 open_dropdown = -1;
    i32 hover_menu = -1;
    i32 hover_item = -1;
    bool hover_welcome_btn = false;

    ScreenLayoutMode layout_mode = ScreenLayoutMode::Vertical;
    bool swap_screens = false;
    u32 native_gap = 16U;
    u32 speed_mult = 2U;
    u32 active_slot = 1U;
    u32 screenshot_counter = 0U;
    u32 rewind_tick = 0U;

    std::string toast_text;
    u64 toast_until_ns = 0ULL;
    double ema_fps = 59.8;
    double ema_frame_ms = 16.7;
    std::vector<i16> audio_scratch{};
};

}  // namespace elmasds::frontend
