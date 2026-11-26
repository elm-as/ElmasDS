#include "core/emulator.hpp"
#include "frontend/application/app_actions.hpp"
#include "frontend/application/app_state.hpp"
#include "frontend/application/diag_runner.hpp"
#include "frontend/application/profile_audit_runner.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kDefaultScale = 2;
constexpr elmasds::u64 kTargetFrameNs = 16715109ULL;
constexpr int kMaxQueuedAudioBytes = 32768;

void cleanup_app(elmasds::frontend::AppState& app) noexcept {
    app.renderer.destroy();
    if (app.audio_stream != nullptr) { SDL_DestroyAudioStream(app.audio_stream); app.audio_stream = nullptr; }
    if (app.gl_context != nullptr) { SDL_GL_DestroyContext(app.gl_context); app.gl_context = nullptr; }
    if (app.window != nullptr) { SDL_DestroyWindow(app.window); app.window = nullptr; }
    SDL_Quit();
}

bool initialize_sdl(elmasds::frontend::AppState& app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const int init_w = static_cast<int>(elmasds::kScreenWidth) * kDefaultScale;
    const int init_h = static_cast<int>(elmasds::kDualScreenHeight + app.native_gap) * kDefaultScale + 36;
    app.window = SDL_CreateWindow("ElmasDS", init_w, init_h,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (app.window == nullptr) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n'; return false; }

    app.gl_context = SDL_GL_CreateContext(app.window);
    if (app.gl_context == nullptr) { std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n'; return false; }
    SDL_GL_SetSwapInterval(0);
    (void)app.renderer.initialize();

    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(elmasds::audio::kAudioSampleRateHz)};
    app.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (app.audio_stream != nullptr) { SDL_ResumeAudioStreamDevice(app.audio_stream); }
    return true;
}

void update_stylus_touch(elmasds::frontend::AppState& app,
                         elmasds::core::Emulator& emu,
                         const elmasds::frontend::DualScreenLayout& layout,
                         SDL_FPoint pt_win) {
    if (!app.mouse_down || app.menu_open) {
        emu.set_touch_pixel(0U, 0U, false);
        return;
    }
    const auto pt = elmasds::frontend::map_window_point_to_touch(
        layout, elmasds::frontend::WindowPoint{static_cast<elmasds::i32>(pt_win.x),
                                               static_cast<elmasds::i32>(pt_win.y)});
    emu.set_touch_pixel(pt ? pt->x : 0U, pt ? pt->y : 0U, pt.has_value());
}

void step_emulator_frame(elmasds::frontend::AppState& app, elmasds::core::Emulator& emu) {
    if (app.paused || app.menu_open || app.controls_help_open || app.loaded_rom_path.empty()) { return; }

    if (app.rewinding && !app.loaded_rom_path.empty()) {
        if ((++app.rewind_tick % 3U) == 0U) {
            if (const auto snap = app.rewind_buffer.pop(); snap.has_value()) {
                (void)emu.load_state(*snap);
                elmasds::frontend::show_toast(app, "REMBOBINAGE");
            }
        }
        return;
    }

    const elmasds::u32 steps = app.fast_forwarding ? app.speed_mult : 1U;
    for (elmasds::u32 s = 0U; s < steps; ++s) {
        emu.run_frame();
        if (app.rewind_enabled && !app.loaded_rom_path.empty() && (++app.rewind_tick % 45U) == 0U) {
            if (auto snap = emu.save_state(); snap.is_ok()) {
                app.rewind_buffer.push(std::move(snap.value()));
            }
        }
    }

    emu.audio().drain_all_into(app.audio_scratch);
    if (!app.audio_muted && app.audio_stream != nullptr && !app.audio_scratch.empty() &&
        SDL_GetAudioStreamQueued(app.audio_stream) < kMaxQueuedAudioBytes) {
        SDL_PutAudioStreamData(app.audio_stream, app.audio_scratch.data(),
                               static_cast<int>(app.audio_scratch.size() * sizeof(elmasds::i16)));
    }
}

void poll_events(elmasds::frontend::AppState& app,
                 elmasds::core::Emulator& emu,
                 const elmasds::frontend::DualScreenLayout& layout) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) { app.running = false; }
        else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            elmasds::frontend::handle_key_event(app, emu, event.key);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (!elmasds::frontend::handle_overlay_mouse_down(app, emu, {event.button.x, event.button.y})) {
                app.mouse_down = true;
                update_stylus_touch(app, emu, layout, {event.button.x, event.button.y});
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            app.mouse_down = false;
            emu.set_touch_pixel(0U, 0U, false);
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            elmasds::frontend::handle_overlay_mouse_move(app, {event.motion.x, event.motion.y});
            if (app.mouse_down) {
                update_stylus_touch(app, emu, layout, {event.motion.x, event.motion.y});
            }
        } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr) {
            elmasds::frontend::boot_rom_path(app, emu, event.drop.data);
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path startup_rom;
    int diag_frames = 0;
    bool profile_audit = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version") { std::cout << "ElmasDS 0.2.0\n"; return 0; }
        if (arg == "--headless-check") {
            elmasds::core::Emulator emu;
            emu.step_ticks(1024);
            std::cout << "ElmasDS headless check OK (ticks=" << emu.current_tick() << ")\n";
            return 0;
        }
        if (arg == "--profile-audit") { profile_audit = true; continue; }
        if (arg == "--diag-frames" && (i + 1) < argc) { diag_frames = std::max(1, std::stoi(argv[++i])); continue; }
        if (!arg.starts_with("--")) { startup_rom = arg; }
    }
    if (profile_audit && !startup_rom.empty()) {
        return elmasds::frontend::run_profile_audit(startup_rom);
    }
    if (diag_frames > 0 && !startup_rom.empty()) {
        return elmasds::frontend::run_diag_frames(startup_rom, diag_frames);
    }

    elmasds::frontend::AppState app;
    (void)app.key_map.load_from_file("elmasds_input.cfg");
    if (!initialize_sdl(app)) { cleanup_app(app); return 1; }

    elmasds::core::Emulator emulator;
    if (!startup_rom.empty()) {
        elmasds::frontend::boot_rom_path(app, emulator, startup_rom);
    }

    elmasds::u64 next_frame_ns = SDL_GetTicksNS() + kTargetFrameNs;
    elmasds::u64 prev_ns = SDL_GetTicksNS();
    elmasds::u32 dirty_save_frames = 0U;
    while (app.running) {
        elmasds::frontend::poll_pending_rom(app, emulator);

        int pixel_w = 0;
        int pixel_h = 0;
        SDL_GetWindowSizeInPixels(app.window, &pixel_w, &pixel_h);
        const elmasds::i32 top_inset = app.fullscreen
            ? 0
            : std::max(18, (pixel_h * elmasds::frontend::kMenuBarHeight) /
                               elmasds::frontend::HudOverlay::kHeight);
        const auto layout = elmasds::frontend::compute_dual_screen_layout(elmasds::frontend::LayoutRequest{
            pixel_w, pixel_h, app.native_gap, true, app.layout_mode, app.swap_screens, top_inset});

        poll_events(app, emulator, layout);
        const elmasds::u64 sim_start = SDL_GetTicksNS();
        step_emulator_frame(app, emulator);
        if (emulator.cartridge().save_memory().is_dirty() && (++dirty_save_frames >= 120U)) {
            (void)emulator.flush_save();
            dirty_save_frames = 0U;
        }
        const double sim_ms = static_cast<double>(SDL_GetTicksNS() - sim_start) / 1'000'000.0;
        app.ema_frame_ms = (app.ema_frame_ms * 0.90) + (sim_ms * 0.10);

        {
            elmasds::primitives::ScopedProfile profile_gl(
                &emulator.profiler(), elmasds::primitives::ProfileCategory::OpenGlPresent);
            app.renderer.render(layout, pixel_w, pixel_h, emulator.top_screen(), emulator.bottom_screen());
            if (app.overlay.render(elmasds::frontend::build_hud_state(app, emulator))) {
                app.renderer.render_overlay(app.overlay.pixels(), app.overlay.width(), app.overlay.height());
            }
            SDL_GL_SwapWindow(app.window);
        }

        const elmasds::u64 now_ns = SDL_GetTicksNS();
        const double dt_s = std::max(0.0001, static_cast<double>(now_ns - prev_ns) / 1'000'000'000.0);
        prev_ns = now_ns;
        app.ema_fps = (app.ema_fps * 0.90) + ((1.0 / dt_s) * 0.10);

        if (!app.fast_forwarding && now_ns < next_frame_ns) {
            SDL_DelayNS(next_frame_ns - now_ns);
            next_frame_ns += kTargetFrameNs;
        } else {
            next_frame_ns = now_ns + kTargetFrameNs;
        }
    }

    (void)emulator.flush_save();
    cleanup_app(app);
    return 0;
}
