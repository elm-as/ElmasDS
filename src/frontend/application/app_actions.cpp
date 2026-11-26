#include "frontend/application/app_actions.hpp"

#include "frontend/application/screenshot_writer.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace elmasds::frontend {
namespace {

constexpr u64 kToastDurationNs = 2'200'000'000ULL;

void SDLCALL on_rom_file_selected(void* userdata, const char* const* filelist, int /*filter*/) {
    if (userdata == nullptr || filelist == nullptr || filelist[0] == nullptr) {
        return;
    }
    auto* app = static_cast<AppState*>(userdata);
    std::lock_guard<std::mutex> lock(app->dialog_mutex);
    app->pending_rom_path = std::filesystem::path(filelist[0]);
}

[[nodiscard]] input::HostKey map_sdl_keycode(SDL_Keycode key) noexcept {
    using input::HostKey;
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<HostKey>(static_cast<u16>(HostKey::A) + (key - SDLK_A));
    }
    switch (key) {
    case SDLK_UP: return HostKey::Up;
    case SDLK_DOWN: return HostKey::Down;
    case SDLK_LEFT: return HostKey::Left;
    case SDLK_RIGHT: return HostKey::Right;
    case SDLK_RETURN: return HostKey::Enter;
    case SDLK_RSHIFT: return HostKey::RightShift;
    case SDLK_LSHIFT: return HostKey::LeftShift;
    case SDLK_SPACE: return HostKey::Space;
    case SDLK_ESCAPE: return HostKey::Escape;
    case SDLK_F4: return HostKey::F4;
    case SDLK_F5: return HostKey::F5;
    case SDLK_F7: return HostKey::F7;
    case SDLK_F11: return HostKey::F11;
    default: return HostKey::None;
    }
}

[[nodiscard]] std::filesystem::path save_slot_path(const std::filesystem::path& rom_path, u32 slot) {
    std::filesystem::path out = rom_path;
    out.replace_extension(".slot" + std::to_string(slot) + ".ess");
    return out;
}

void cycle_layout(AppState& app, i32 delta) noexcept {
    const i32 next = (static_cast<i32>(app.layout_mode) + delta + 4) % 4;
    app.layout_mode = static_cast<ScreenLayoutMode>(next);
    show_toast(app, "DISPOSITION MODIFIEE");
}

void cycle_filter(AppState& app, i32 delta) noexcept {
    const i32 next = (static_cast<i32>(app.renderer.filter_mode()) + delta + 4) % 4;
    app.renderer.set_filter_mode(static_cast<ShaderFilterMode>(next));
    show_toast(app, "FILTRE VISUEL MODIFIE");
}

void adjust_menu_option(AppState& app, i32 delta) noexcept {
    switch (app.menu_index) {
    case 2: cycle_layout(app, delta); break;
    case 3: app.swap_screens = !app.swap_screens; show_toast(app, app.swap_screens ? "ECRANS INVERSES" : "ECRANS NORMAUX"); break;
    case 4: app.native_gap = static_cast<u32>(std::clamp(static_cast<i32>(app.native_gap) + delta * 8, 0, 64)); break;
    case 5: cycle_filter(app, delta); break;
    case 6: app.speed_mult = (app.speed_mult == 2U) ? 4U : 2U; break;
    case 7: app.active_slot = static_cast<u32>((static_cast<i32>(app.active_slot) - 1 + delta + 8) % 8 + 1); break;
    case 8: app.perf_hud_open = !app.perf_hud_open; break;
    default: break;
    }
}

void activate_menu_item(AppState& app, core::Emulator& emu) {
    switch (app.menu_index) {
    case 0: app.menu_open = false; break;
    case 1: app.menu_open = false; open_rom_dialog(app); break;
    case 2: case 3: case 4: case 5: case 6: case 7: case 8:
        adjust_menu_option(app, 1);
        break;
    case 9: capture_screenshot(app, emu); app.menu_open = false; break;
    case 10: handle_shortcut(app, emu, input::EmulatorShortcut::ResetConsole); app.menu_open = false; break;
    case 11: app.running = false; break;
    default: break;
    }
}

void handle_menu_key(AppState& app, core::Emulator& emu, SDL_Keycode key) {
    switch (key) {
    case SDLK_ESCAPE: app.menu_open = false; break;
    case SDLK_UP: app.menu_index = (app.menu_index + HudOverlay::kMenuItemCount - 1) % HudOverlay::kMenuItemCount; break;
    case SDLK_DOWN: app.menu_index = (app.menu_index + 1) % HudOverlay::kMenuItemCount; break;
    case SDLK_LEFT: adjust_menu_option(app, -1); break;
    case SDLK_RIGHT: adjust_menu_option(app, 1); break;
    case SDLK_RETURN: case SDLK_SPACE: activate_menu_item(app, emu); break;
    default: break;
    }
}

}  // namespace

void show_toast(AppState& app, std::string_view text) noexcept {
    app.toast_text = std::string(text);
    app.toast_until_ns = SDL_GetTicksNS() + kToastDurationNs;
}

void boot_rom_path(AppState& app, core::Emulator& emu, const std::filesystem::path& path) {
    const auto status = emu.load_rom_file(path);
    if (status.is_err()) {
        show_toast(app, "ECHEC CHARGEMENT ROM");
        std::cerr << "ROM load failed: " << status.error().message << '\n';
        return;
    }
    auto save_path = path;
    save_path.replace_extension(".sav");
    (void)emu.attach_save(save_path);
    if (emu.direct_boot().is_ok()) {
        app.loaded_rom_path = path;
        app.rom_title = emu.cartridge().rom()->header().title;
        app.paused = false;
        app.menu_open = false;
        app.open_dropdown = -1;
        app.controls_help_open = false;
        app.rewind_buffer.clear();
        const std::string title = "ElmasDS - " + app.rom_title;
        SDL_SetWindowTitle(app.window, title.c_str());
        show_toast(app, "ROM LANCEE");
    }
}

void open_rom_dialog(AppState& app) noexcept {
    static const SDL_DialogFileFilter kRomFilters[] = {{"Nintendo DS ROM", "nds"}};
    SDL_ShowOpenFileDialog(on_rom_file_selected, &app, app.window, kRomFilters, 1, nullptr, false);
}

void poll_pending_rom(AppState& app, core::Emulator& emu) {
    std::filesystem::path next_path;
    {
        std::lock_guard<std::mutex> lock(app.dialog_mutex);
        if (app.pending_rom_path.empty()) { return; }
        next_path = std::move(app.pending_rom_path);
        app.pending_rom_path.clear();
    }
    boot_rom_path(app, emu, next_path);
}

void capture_screenshot(AppState& app, const core::Emulator& emu) {
    const std::filesystem::path dir = app.loaded_rom_path.empty()
                                          ? std::filesystem::path("screenshots")
                                          : (app.loaded_rom_path.parent_path() / "screenshots");
    const std::string stem = app.loaded_rom_path.empty()
                                 ? "elmasds"
                                 : app.loaded_rom_path.stem().string();
    char name[96]{};
    std::snprintf(name, sizeof(name), "%s_%03u.bmp", stem.c_str(), ++app.screenshot_counter);
    const Status s = save_dual_screen_bmp(dir / name, emu.top_screen(), emu.bottom_screen());
    show_toast(app, s.is_ok() ? "CAPTURE BMP ENREGISTREE" : "ECHEC CAPTURE BMP");
}

void handle_shortcut(AppState& app, core::Emulator& emu, input::EmulatorShortcut shortcut) {
    using input::EmulatorShortcut;
    switch (shortcut) {
    case EmulatorShortcut::OpenRom: open_rom_dialog(app); break;
    case EmulatorShortcut::TogglePause:
        app.paused = !app.paused;
        show_toast(app, app.paused ? "PAUSE" : "REPRISE");
        break;
    case EmulatorShortcut::ToggleFullscreen:
        app.fullscreen = !app.fullscreen;
        SDL_SetWindowFullscreen(app.window, app.fullscreen);
        break;
    case EmulatorShortcut::ResetConsole:
        (void)emu.flush_save();
        if (!app.loaded_rom_path.empty()) { (void)emu.direct_boot(); } else { emu.reset(); }
        app.rewind_buffer.clear();
        show_toast(app, "CONSOLE REINITIALISEE");
        break;
    case EmulatorShortcut::SaveState:
        if (!app.loaded_rom_path.empty()) {
            const bool ok = emu.save_state_to_file(save_slot_path(app.loaded_rom_path, app.active_slot)).is_ok();
            show_toast(app, ok ? ("SAVE SLOT " + std::to_string(app.active_slot) + " OK") : "ECHEC SAVE");
        }
        break;
    case EmulatorShortcut::LoadState:
        if (!app.loaded_rom_path.empty()) {
            const bool ok = emu.load_state_from_file(save_slot_path(app.loaded_rom_path, app.active_slot)).is_ok();
            show_toast(app, ok ? ("LOAD SLOT " + std::to_string(app.active_slot) + " OK") : "SLOT VIDE");
        }
        break;
    case EmulatorShortcut::MenuOrBack: app.menu_open = !app.menu_open; break;
    case EmulatorShortcut::QuitApplication: app.running = false; break;
    }
}

void handle_key_event(AppState& app, core::Emulator& emu, const SDL_KeyboardEvent& ev) {
    if (ev.key == SDLK_TAB) { app.fast_forwarding = ev.down; return; }
    if (ev.key == SDLK_BACKSPACE) { app.rewinding = ev.down; return; }
    if (app.menu_open && ev.down) { handle_menu_key(app, emu, ev.key); return; }

    if (ev.down && !ev.repeat) {
        if (ev.key == SDLK_ESCAPE && (app.controls_help_open || app.open_dropdown >= 0)) {
            app.controls_help_open = false;
            app.open_dropdown = -1;
            return;
        }
        if (ev.key == SDLK_F2) { execute_menu_action(app, emu, MenuAction::CycleFilter); return; }
        if (ev.key == SDLK_F3) { execute_menu_action(app, emu, MenuAction::TogglePerfHud); return; }
        if (ev.key == SDLK_F6) { execute_menu_action(app, emu, MenuAction::CycleLayout); return; }
        if (ev.key == SDLK_F8) { execute_menu_action(app, emu, MenuAction::ToggleSwapScreens); return; }
        if (ev.key == SDLK_F9) { execute_menu_action(app, emu, MenuAction::ToggleRewind); return; }
        if (ev.key == SDLK_F12) { execute_menu_action(app, emu, MenuAction::CaptureScreenshot); return; }
        if (ev.key == SDLK_M) { execute_menu_action(app, emu, MenuAction::ToggleMute); return; }
        if (ev.key >= SDLK_1 && ev.key <= SDLK_8) {
            app.active_slot = static_cast<u32>(ev.key - SDLK_1 + 1);
            show_toast(app, "SLOT " + std::to_string(app.active_slot));
            return;
        }
    }

    const auto host_key = map_sdl_keycode(ev.key);
    if (host_key == input::HostKey::None) { return; }
    if (ev.down && !ev.repeat) {
        const input::KeyModifiers mods{(ev.mod & SDL_KMOD_CTRL) != 0U, (ev.mod & SDL_KMOD_ALT) != 0U,
                                       (ev.mod & SDL_KMOD_SHIFT) != 0U};
        if (const auto sc = input::InputKeyMap::match_shortcut(host_key, mods); sc.has_value()) {
            handle_shortcut(app, emu, *sc);
            return;
        }
    }
    if (const auto button = app.key_map.button_for_key(host_key); button.has_value()) {
        emu.set_button_pressed(*button, ev.down);
    }
}

HudState build_hud_state(const AppState& app, const core::Emulator& emu) {
    const bool toast_vis = !app.toast_text.empty() && SDL_GetTicksNS() < app.toast_until_ns;
    return HudState{
        app.menu_open,
        app.menu_index,
        app.perf_hud_open,
        !app.loaded_rom_path.empty(),
        app.paused,
        app.audio_muted,
        app.fullscreen,
        app.rewind_enabled,
        app.controls_help_open,
        app.open_dropdown,
        app.hover_menu,
        app.hover_item,
        app.hover_welcome_btn,
        app.rom_title,
        app.toast_text,
        toast_vis,
        app.ema_fps,
        app.ema_frame_ms,
        emu.gpu().engine_3d().rendered_polygon_count(),
        app.layout_mode,
        app.swap_screens,
        app.native_gap,
        app.renderer.filter_mode(),
        app.speed_mult,
        app.active_slot,
        app.rewind_buffer.size(),
        app.rewinding,
        app.fast_forwarding,
    };
}

}  // namespace elmasds::frontend
