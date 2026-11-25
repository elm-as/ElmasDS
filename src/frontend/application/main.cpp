#include "core/emulator.hpp"
#include "frontend/renderer/gl_screen_renderer.hpp"
#include "frontend/window/display_layout.hpp"
#include "input/input_mapping.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

constexpr int kDefaultScale = 2;
constexpr elmasds::u32 kDefaultNativeGap = 16U;

struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext gl_context = nullptr;
    SDL_AudioStream* audio_stream = nullptr;
    elmasds::frontend::GlScreenRenderer renderer;
    elmasds::input::InputKeyMap key_map;
    std::filesystem::path loaded_rom_path;
    bool running = true;
    bool paused = false;
    bool fullscreen = false;
    bool mouse_down = false;
    elmasds::u32 native_gap = kDefaultNativeGap;
};

[[nodiscard]] elmasds::input::HostKey map_sdl_keycode(SDL_Keycode key) noexcept {
    using elmasds::input::HostKey;
    if (key >= SDLK_A && key <= SDLK_Z) {
        return static_cast<HostKey>(static_cast<elmasds::u16>(HostKey::A) + (key - SDLK_A));
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

void cleanup_app(AppState& app) noexcept {
    app.renderer.destroy();
    if (app.audio_stream != nullptr) {
        SDL_DestroyAudioStream(app.audio_stream);
        app.audio_stream = nullptr;
    }
    if (app.gl_context != nullptr) {
        SDL_GL_DestroyContext(app.gl_context);
        app.gl_context = nullptr;
    }
    if (app.window != nullptr) {
        SDL_DestroyWindow(app.window);
        app.window = nullptr;
    }
    SDL_Quit();
}

bool initialize_sdl(AppState& app) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const int init_w = static_cast<int>(elmasds::kScreenWidth) * kDefaultScale;
    const int init_h =
        static_cast<int>(elmasds::kDualScreenHeight + app.native_gap) * kDefaultScale;

    app.window = SDL_CreateWindow(
        "ElmasDS",
        init_w,
        init_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (app.window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return false;
    }

    app.gl_context = SDL_GL_CreateContext(app.window);
    if (app.gl_context == nullptr) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetSwapInterval(1);
    (void)app.renderer.initialize();

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(elmasds::audio::kAudioSampleRateHz);
    app.audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                                 nullptr);
    if (app.audio_stream != nullptr) {
        SDL_ResumeAudioStreamDevice(app.audio_stream);
    }
    return true;
}

void boot_rom_path(AppState& app,
                   elmasds::core::Emulator& emu,
                   const std::filesystem::path& rom_path) {
    const auto status = emu.load_rom_file(rom_path);
    if (status.is_err()) {
        std::cerr << "ROM load failed: " << status.error().message << '\n';
        return;
    }
    auto save_path = rom_path;
    save_path.replace_extension(".sav");
    (void)emu.attach_save(save_path);
    if (emu.direct_boot().is_ok()) {
        app.loaded_rom_path = rom_path;
        app.paused = false;
        const std::string title =
            "ElmasDS - " + std::string(emu.cartridge().rom()->header().title);
        SDL_SetWindowTitle(app.window, title.c_str());
    }
}

void handle_shortcut(AppState& app,
                     elmasds::core::Emulator& emu,
                     elmasds::input::EmulatorShortcut shortcut) {
    using elmasds::input::EmulatorShortcut;
    switch (shortcut) {
    case EmulatorShortcut::TogglePause:
        app.paused = !app.paused;
        break;
    case EmulatorShortcut::ToggleFullscreen:
        app.fullscreen = !app.fullscreen;
        SDL_SetWindowFullscreen(app.window, app.fullscreen);
        break;
    case EmulatorShortcut::ResetConsole:
        if (!app.loaded_rom_path.empty()) {
            (void)emu.direct_boot();
        } else {
            emu.reset();
        }
        break;
    case EmulatorShortcut::SaveState:
        if (!app.loaded_rom_path.empty()) {
            auto path = app.loaded_rom_path;
            path.replace_extension(".ess");
            (void)emu.save_state_to_file(path);
        }
        break;
    case EmulatorShortcut::LoadState:
        if (!app.loaded_rom_path.empty()) {
            auto path = app.loaded_rom_path;
            path.replace_extension(".ess");
            (void)emu.load_state_from_file(path);
        }
        break;
    case EmulatorShortcut::MenuOrBack:
    case EmulatorShortcut::QuitApplication:
        app.running = false;
        break;
    case EmulatorShortcut::OpenRom:
        break;
    }
}

void handle_key_event(AppState& app,
                      elmasds::core::Emulator& emu,
                      const SDL_KeyboardEvent& key_ev) {
    const auto host_key = map_sdl_keycode(key_ev.key);
    if (host_key == elmasds::input::HostKey::None) {
        return;
    }
    if (key_ev.down && !key_ev.repeat) {
        const elmasds::input::KeyModifiers mods{
            (key_ev.mod & SDL_KMOD_CTRL) != 0U,
            (key_ev.mod & SDL_KMOD_ALT) != 0U,
            (key_ev.mod & SDL_KMOD_SHIFT) != 0U,
        };
        if (const auto shortcut = elmasds::input::InputKeyMap::match_shortcut(host_key, mods);
            shortcut.has_value()) {
            handle_shortcut(app, emu, *shortcut);
            return;
        }
    }
    if (const auto button = app.key_map.button_for_key(host_key); button.has_value()) {
        emu.set_button_pressed(*button, key_ev.down);
    }
}

void update_stylus_touch(AppState& app,
                         elmasds::core::Emulator& emu,
                         const elmasds::frontend::DualScreenLayout& layout,
                         float win_x,
                         float win_y) {
    if (!app.mouse_down) {
        emu.set_touch_pixel(0U, 0U, false);
        return;
    }
    const auto pt = elmasds::frontend::map_window_point_to_touch(
        layout, elmasds::frontend::WindowPoint{static_cast<elmasds::i32>(win_x),
                                               static_cast<elmasds::i32>(win_y)});
    if (pt.has_value()) {
        emu.set_touch_pixel(pt->x, pt->y, true);
    } else {
        emu.set_touch_pixel(0U, 0U, false);
    }
}

int run_headless_check() {
    elmasds::core::Emulator emulator;
    emulator.step_ticks(1024);
    std::cout << "ElmasDS headless check OK (ticks=" << emulator.current_tick() << ")\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path startup_rom;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version") {
            std::cout << "ElmasDS 0.1.0\n";
            return 0;
        }
        if (arg == "--headless-check") {
            return run_headless_check();
        }
        if (!arg.starts_with("--")) {
            startup_rom = arg;
        }
    }

    AppState app;
    (void)app.key_map.load_from_file("elmasds_input.cfg");
    if (!initialize_sdl(app)) {
        cleanup_app(app);
        return 1;
    }

    elmasds::core::Emulator emulator;
    if (!startup_rom.empty()) {
        boot_rom_path(app, emulator, startup_rom);
    }

    while (app.running) {
        int pixel_w = 0;
        int pixel_h = 0;
        SDL_GetWindowSizeInPixels(app.window, &pixel_w, &pixel_h);
        const auto layout = elmasds::frontend::compute_dual_screen_layout(
            elmasds::frontend::LayoutRequest{pixel_w, pixel_h, app.native_gap, true});

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                app.running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                handle_key_event(app, emulator, event.key);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app.mouse_down = true;
                    update_stylus_touch(app, emulator, layout, event.button.x, event.button.y);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    app.mouse_down = false;
                    emulator.set_touch_pixel(0U, 0U, false);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (app.mouse_down) {
                    update_stylus_touch(app, emulator, layout, event.motion.x, event.motion.y);
                }
                break;
            case SDL_EVENT_DROP_FILE:
                if (event.drop.data != nullptr) {
                    boot_rom_path(app, emulator, event.drop.data);
                }
                break;
            default:
                break;
            }
        }

        if (!app.paused) {
            emulator.run_frame();
            const auto audio_samples = emulator.audio().drain_samples();
            if (app.audio_stream != nullptr && !audio_samples.empty()) {
                SDL_PutAudioStreamData(app.audio_stream, audio_samples.data(),
                                       static_cast<int>(audio_samples.size() * sizeof(elmasds::i16)));
            }
        }

        app.renderer.render(layout, pixel_w, pixel_h, emulator.top_screen(),
                            emulator.bottom_screen());
        SDL_GL_SwapWindow(app.window);
    }

    (void)emulator.flush_save();
    cleanup_app(app);
    return 0;
}
