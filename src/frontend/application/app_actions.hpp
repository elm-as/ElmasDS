#pragma once

#include "core/emulator.hpp"
#include "frontend/application/app_state.hpp"
#include "frontend/overlay/menu_bar.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <string_view>

namespace elmasds::frontend {

void show_toast(AppState& app, std::string_view text) noexcept;
void boot_rom_path(AppState& app, core::Emulator& emu, const std::filesystem::path& path);
void open_rom_dialog(AppState& app) noexcept;
void poll_pending_rom(AppState& app, core::Emulator& emu);
void handle_shortcut(AppState& app, core::Emulator& emu, input::EmulatorShortcut shortcut);
void handle_key_event(AppState& app, core::Emulator& emu, const SDL_KeyboardEvent& ev);
void capture_screenshot(AppState& app, const core::Emulator& emu);
void execute_menu_action(AppState& app, core::Emulator& emu, MenuAction action);
[[nodiscard]] bool handle_overlay_mouse_down(AppState& app, core::Emulator& emu, SDL_FPoint pt_win);
void handle_overlay_mouse_move(AppState& app, SDL_FPoint pt_win) noexcept;
[[nodiscard]] HudState build_hud_state(const AppState& app, const core::Emulator& emu);

}  // namespace elmasds::frontend
