#include "frontend/application/app_actions.hpp"

#include <algorithm>
#include <array>

namespace elmasds::frontend {
namespace {

struct OverlayPoint {
    i32 x = 0;
    i32 y = 0;
};

[[nodiscard]] OverlayPoint window_to_overlay(const AppState& app, SDL_FPoint pt_win) noexcept {
    int win_w = 1;
    int win_h = 1;
    if (app.window != nullptr) {
        SDL_GetWindowSize(app.window, &win_w, &win_h);
    }
    const i32 cw = app.overlay.width();
    const i32 ch = app.overlay.height();
    const i32 ox = static_cast<i32>((pt_win.x * static_cast<float>(cw)) / static_cast<float>(std::max(1, win_w)));
    const i32 oy = static_cast<i32>((pt_win.y * static_cast<float>(ch)) / static_cast<float>(std::max(1, win_h)));
    return {ox, oy};
}

void cycle_layout_next(AppState& app) noexcept {
    const i32 next = (static_cast<i32>(app.layout_mode) + 1) % 4;
    app.layout_mode = static_cast<ScreenLayoutMode>(next);
    show_toast(app, "DISPOSITION MODIFIEE");
}

void cycle_filter_next(AppState& app) noexcept {
    const i32 next = (static_cast<i32>(app.renderer.filter_mode()) + 1) % 4;
    app.renderer.set_filter_mode(static_cast<ShaderFilterMode>(next));
    show_toast(app, "FILTRE VISUEL MODIFIE");
}

void toggle_audio_mute(AppState& app) noexcept {
    app.audio_muted = !app.audio_muted;
    if (app.audio_muted && app.audio_stream != nullptr) {
        SDL_ClearAudioStream(app.audio_stream);
    }
    show_toast(app, app.audio_muted ? "AUDIO MUET" : "AUDIO ACTIF");
}

void toggle_rewind_mode(AppState& app) noexcept {
    app.rewind_enabled = !app.rewind_enabled;
    if (!app.rewind_enabled) {
        app.rewind_buffer.clear();
    }
    show_toast(app, app.rewind_enabled ? "REWIND ON" : "REWIND OFF");
}

void execute_display_or_tool(AppState& app, core::Emulator& emu, MenuAction action) {
    switch (action) {
    case MenuAction::ToggleMute: toggle_audio_mute(app); break;
    case MenuAction::CycleLayout: cycle_layout_next(app); break;
    case MenuAction::ToggleSwapScreens:
        app.swap_screens = !app.swap_screens;
        show_toast(app, app.swap_screens ? "ECRANS INVERSES" : "ECRANS NORMAUX");
        break;
    case MenuAction::CycleGap: {
        static constexpr std::array<u32, 4U> kGaps = {0U, 16U, 32U, 48U};
        const auto it = std::find(kGaps.begin(), kGaps.end(), app.native_gap);
        const std::size_t idx = (it == kGaps.end()) ? 0U : (static_cast<std::size_t>(it - kGaps.begin()) + 1U) % kGaps.size();
        app.native_gap = kGaps[idx];
        break;
    }
    case MenuAction::CycleFilter: cycle_filter_next(app); break;
    case MenuAction::ToggleFullscreen: handle_shortcut(app, emu, input::EmulatorShortcut::ToggleFullscreen); break;
    case MenuAction::CaptureScreenshot: capture_screenshot(app, emu); break;
    case MenuAction::TogglePerfHud: app.perf_hud_open = !app.perf_hud_open; break;
    case MenuAction::FlushSave:
        show_toast(app, emu.flush_save().is_ok() ? "SAUVEGARDE .SAV OK" : "AUCUN .SAV ACTIF");
        break;
    case MenuAction::OpenQuickMenu: app.menu_open = !app.menu_open; break;
    case MenuAction::ToggleSpeedMult: app.speed_mult = (app.speed_mult == 2U) ? 4U : 2U; break;
    case MenuAction::ToggleControlsHelp: app.controls_help_open = !app.controls_help_open; break;
    default: break;
    }
}

}  // namespace

void execute_menu_action(AppState& app, core::Emulator& emu, MenuAction action) {
    switch (action) {
    case MenuAction::OpenRom: open_rom_dialog(app); break;
    case MenuAction::SaveState: handle_shortcut(app, emu, input::EmulatorShortcut::SaveState); break;
    case MenuAction::LoadState: handle_shortcut(app, emu, input::EmulatorShortcut::LoadState); break;
    case MenuAction::CycleSlot:
        app.active_slot = (app.active_slot % 8U) + 1U;
        show_toast(app, "SLOT " + std::to_string(app.active_slot));
        break;
    case MenuAction::Quit: app.running = false; break;
    case MenuAction::TogglePause: handle_shortcut(app, emu, input::EmulatorShortcut::TogglePause); break;
    case MenuAction::ToggleSpeed2x:
        app.locked_speed_mult = (app.locked_speed_mult == 2U) ? 1U : 2U;
        show_toast(app, app.locked_speed_mult == 2U ? "VITESSE X2 ACTIVE" : "VITESSE NORMALE 1X");
        break;
    case MenuAction::ToggleSpeed4x:
        app.locked_speed_mult = (app.locked_speed_mult == 4U) ? 1U : 4U;
        show_toast(app, app.locked_speed_mult == 4U ? "VITESSE X4 ACTIVE" : "VITESSE NORMALE 1X");
        break;
    case MenuAction::ResetConsole: handle_shortcut(app, emu, input::EmulatorShortcut::ResetConsole); break;
    case MenuAction::ToggleRewind: toggle_rewind_mode(app); break;
    case MenuAction::CloseOverlays:
        app.open_dropdown = -1;
        app.controls_help_open = false;
        break;
    default:
        execute_display_or_tool(app, emu, action);
        break;
    }
}

bool handle_overlay_mouse_down(AppState& app, core::Emulator& emu, SDL_FPoint pt_win) {
    const OverlayPoint pt = window_to_overlay(app, pt_win);
    const i32 cw = app.overlay.width();
    const i32 ch = app.overlay.height();
    if (app.controls_help_open) {
        if (point_in_rect(controls_close_button_rect(cw, ch), pt.x, pt.y) ||
            !point_in_rect(controls_modal_rect(cw, ch), pt.x, pt.y)) {
            app.controls_help_open = false;
        }
        return true;
    }

    if (!app.fullscreen || app.open_dropdown >= 0) {
        const MenuBarHit hit = hit_test_menu_bar_canvas({pt.x, pt.y, cw}, app.open_dropdown);
        if (hit.consumed) {
            if (hit.menu_index >= 0 && hit.item_index < 0) {
                app.open_dropdown = (app.open_dropdown == hit.menu_index) ? -1 : hit.menu_index;
                return true;
            }
            if (hit.action != MenuAction::None) {
                app.open_dropdown = -1;
                execute_menu_action(app, emu, hit.action);
            }
            return true;
        }
    }

    if (app.loaded_rom_path.empty() && !app.menu_open &&
        point_in_rect(welcome_open_button_rect(cw, ch), pt.x, pt.y)) {
        open_rom_dialog(app);
        return true;
    }
    return false;
}

void handle_overlay_mouse_move(AppState& app, SDL_FPoint pt_win) noexcept {
    const OverlayPoint pt = window_to_overlay(app, pt_win);
    const i32 cw = app.overlay.width();
    const i32 ch = app.overlay.height();
    app.hover_welcome_btn = app.loaded_rom_path.empty() && !app.menu_open &&
                            point_in_rect(welcome_open_button_rect(cw, ch), pt.x, pt.y);

    if (app.fullscreen && app.open_dropdown < 0) {
        app.hover_menu = -1;
        app.hover_item = -1;
        return;
    }

    const MenuBarHit hit = hit_test_menu_bar_canvas({pt.x, pt.y, cw}, app.open_dropdown);
    if (hit.menu_index >= 0 && hit.item_index < 0 && pt.y < kMenuBarHeight) {
        app.hover_menu = hit.menu_index;
        app.hover_item = -1;
        if (app.open_dropdown >= 0 && app.open_dropdown != hit.menu_index) {
            app.open_dropdown = hit.menu_index;
        }
        return;
    }

    app.hover_menu = (app.open_dropdown >= 0) ? app.open_dropdown : -1;
    app.hover_item = hit.item_index;
}

}  // namespace elmasds::frontend
