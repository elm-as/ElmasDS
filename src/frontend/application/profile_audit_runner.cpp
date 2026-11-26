#include "frontend/application/profile_audit_runner.hpp"

#include "core/emulator.hpp"
#include "frontend/renderer/gl_screen_renderer.hpp"
#include "frontend/window/display_layout.hpp"
#include "primitives/performance_profiler.hpp"
#include "storage/save_manager/save_memory.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace elmasds::frontend {
namespace {

struct AuditContext {
    core::Emulator& emu;
    GlScreenRenderer& renderer;
    SDL_Window* window = nullptr;
    SDL_AudioStream* audio = nullptr;
    DualScreenLayout layout{};
    int width = 512;
    int height = 768;
    std::vector<i16> audio_scratch{};
};

void step_one_presented_frame(AuditContext& ctx, bool profile_on) {
    auto& profiler = ctx.emu.profiler();
    const TickCount cyc9_before = ctx.emu.arm9().total_cycles();
    const TickCount cyc7_before = ctx.emu.arm7().total_cycles();
    if (profile_on) { profiler.begin_frame(); }

    ctx.emu.run_frame();
    ctx.emu.audio().drain_all_into(ctx.audio_scratch);
    if (ctx.audio != nullptr && !ctx.audio_scratch.empty() && SDL_GetAudioStreamQueued(ctx.audio) < 32768) {
        SDL_PutAudioStreamData(ctx.audio, ctx.audio_scratch.data(), static_cast<int>(ctx.audio_scratch.size() * sizeof(i16)));
    }
    {
        primitives::ScopedProfile profile_gl(&profiler, primitives::ProfileCategory::OpenGlPresent);
        ctx.renderer.render(ctx.layout, ctx.width, ctx.height, ctx.emu.top_screen(), ctx.emu.bottom_screen());
        SDL_GL_SwapWindow(ctx.window);
    }
    if (profile_on) {
        profiler.end_frame(ctx.emu.arm9().total_cycles() - cyc9_before,
                           ctx.emu.arm7().total_cycles() - cyc7_before);
    }
}

void measure_scenario(AuditContext& ctx, std::string_view name, int frames,
                      void (*input_fn)(core::Emulator&, int)) {
    std::cout << "\n=== " << name << " (" << frames << " frames) ===\n";
    auto& prof = ctx.emu.profiler();
    prof.reset();
    prof.set_enabled(true);
    for (int f = 0; f < frames; ++f) {
        if (input_fn != nullptr) { input_fn(ctx.emu, f); }
        SDL_Event ev{};
        while (SDL_PollEvent(&ev)) {}
        step_one_presented_frame(ctx, true);
    }
    prof.set_enabled(false);
    ctx.emu.set_button_pressed(input::DsButton::A, false);
    ctx.emu.set_button_pressed(input::DsButton::Right, false);
    ctx.emu.set_button_pressed(input::DsButton::Left, false);
    ctx.emu.set_button_pressed(input::DsButton::Up, false);
    ctx.emu.set_button_pressed(input::DsButton::Down, false);
    ctx.emu.set_touch_pixel(0U, 0U, false);

    const u64 n = prof.frame_count();
    const u64 meas_ns = prof.total_measured_ns();
    const double wall_ms = static_cast<double>(prof.total_wall_ns()) / 1'000'000.0;
    const auto& c = prof.counters();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Host FPS=" << prof.host_fps()
              << " | AvgFrameMs=" << (n > 0U ? wall_ms / static_cast<double>(n) : 0.0)
              << " | MaxFrameMs=" << (static_cast<double>(prof.max_frame_wall_ns()) / 1'000'000.0)
              << " | Speed=" << prof.emulation_speed_percent() << "%\n";
    std::cout << "ARM9_cycles=" << c.arm9_cycles << " (" << (n > 0U ? c.arm9_cycles / n : 0U) << "/f)"
              << " | ARM7_cycles=" << c.arm7_cycles << " (" << (n > 0U ? c.arm7_cycles / n : 0U) << "/f)"
              << " | SchedEvents=" << c.scheduler_events << " (" << (n > 0U ? c.scheduler_events / n : 0U) << "/f)\n";
    std::cout << "Scanlines=" << c.scanlines_rendered
              << " | SpritesEval=" << c.sprites_evaluated
              << " | SpritesVis=" << c.sprites_visible
              << " | PixelsComp=" << c.pixels_composited
              << " | Polys3D=" << c.polygons_3d << " (" << (n > 0U ? c.polygons_3d / n : 0U) << "/f)"
              << " | TotalMeasuredMs=" << (static_cast<double>(meas_ns) / 1'000'000.0)
              << " | WallMs=" << wall_ms << "\n";

    std::cout << std::left << std::setw(20) << "Subsystem"
              << std::right << std::setw(12) << "Total(ms)"
              << std::setw(12) << "Avg/F(ms)"
              << std::setw(12) << "Max/F(ms)"
              << std::setw(10) << "Share(%)" << '\n';
    for (std::size_t i = 0U; i < primitives::kProfileCategoryCount; ++i) {
        const auto cat = static_cast<primitives::ProfileCategory>(i);
        const auto& s = prof.category_stats(cat);
        std::cout << std::left << std::setw(20) << primitives::profile_category_name(cat)
                  << std::right << std::setw(12) << s.total_ms()
                  << std::setw(12) << s.avg_frame_ms(n)
                  << std::setw(12) << s.max_frame_ms()
                  << std::setw(9) << s.percent_of(meas_ns) << "%\n";
    }
    std::cout << std::hex << "EndState: DISPCNT_A=0x" << ctx.emu.gpu().regs_a().dispcnt()
              << " DISPCNT_B=0x" << ctx.emu.gpu().regs_b().dispcnt()
              << " PC9=0x" << ctx.emu.arm9_pc()
              << std::dec << " LivePolys3D=" << ctx.emu.gpu().engine_3d().rendered_polygon_count() << '\n';
}

void input_idle(core::Emulator& /*emu*/, int /*f*/) {}

void input_walking(core::Emulator& emu, int f) {
    const int phase = (f / 60) % 4;
    emu.set_button_pressed(input::DsButton::Right, phase == 0);
    emu.set_button_pressed(input::DsButton::Down, phase == 1);
    emu.set_button_pressed(input::DsButton::Left, phase == 2);
    emu.set_button_pressed(input::DsButton::Up, phase == 3);
}

bool advance_until_polys(AuditContext& ctx, std::size_t min_polys, int max_frames, bool tap_advance) {
    int consecutive = 0;
    for (int f = 0; f < max_frames; ++f) {
        if (tap_advance) {
            const int step = (f / 20) % 5;
            const bool press = (f % 20) < 6;
            ctx.emu.set_button_pressed(input::DsButton::A, press && (step == 0 || step == 2));
            ctx.emu.set_button_pressed(input::DsButton::Start, press && step == 4);
            const u16 tx = (step == 1) ? 128U : ((step == 3) ? 200U : 80U);
            const u16 ty = (step == 1) ? 100U : ((step == 3) ? 170U : 96U);
            ctx.emu.set_touch_pixel(tx, ty, press && (step == 1 || step == 3 || step == 4));
        }
        step_one_presented_frame(ctx, false);
        const bool ok = ((ctx.emu.gpu().regs_a().dispcnt() & (1U << 3U)) != 0U) &&
                        (ctx.emu.gpu().engine_3d().rendered_polygon_count() >= min_polys);
        consecutive = ok ? (consecutive + 1) : 0;
        if (consecutive >= 20) {
            ctx.emu.set_button_pressed(input::DsButton::A, false);
            ctx.emu.set_button_pressed(input::DsButton::Start, false);
            ctx.emu.set_touch_pixel(0U, 0U, false);
            return true;
        }
    }
    ctx.emu.set_button_pressed(input::DsButton::A, false);
    ctx.emu.set_button_pressed(input::DsButton::Start, false);
    ctx.emu.set_touch_pixel(0U, 0U, false);
    return false;
}

bool advance_until_2d_dialogue(AuditContext& ctx, int max_frames) {
    for (int f = 0; f < max_frames; ++f) {
        const bool press_a = (f < 240) && ((f % 24) < 6);
        const bool press_start = (f >= 40 && f < 100) && ((f % 20) < 6);
        ctx.emu.set_button_pressed(input::DsButton::A, press_a);
        ctx.emu.set_button_pressed(input::DsButton::Start, press_start);
        step_one_presented_frame(ctx, false);
        if (f >= 180 && (ctx.emu.gpu().regs_a().dispcnt() & (1U << 3U)) == 0U &&
            (ctx.emu.gpu().regs_a().dispcnt() & 0x0F00U) != 0U) {
            ctx.emu.set_button_pressed(input::DsButton::A, false);
            ctx.emu.set_button_pressed(input::DsButton::Start, false);
            return true;
        }
    }
    ctx.emu.set_button_pressed(input::DsButton::A, false);
    ctx.emu.set_button_pressed(input::DsButton::Start, false);
    return false;
}

}  // namespace

int run_profile_audit(const std::filesystem::path& rom_path) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* win = SDL_CreateWindow("ElmasDS Audit", 512, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (win == nullptr) { SDL_Quit(); return 1; }
    SDL_GLContext gl_ctx = SDL_GL_CreateContext(win);
    if (gl_ctx == nullptr) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(0);

    GlScreenRenderer renderer;
    (void)renderer.initialize();
    SDL_AudioStream* audio_stream = nullptr;

    core::Emulator emu;
    if (const auto s = emu.load_rom_file(rom_path); s.is_err() || emu.direct_boot().is_err()) {
        renderer.destroy();
        if (audio_stream != nullptr) { SDL_DestroyAudioStream(audio_stream); }
        SDL_GL_DestroyContext(gl_ctx); SDL_DestroyWindow(win); SDL_Quit();
        return 1;
    }
    emu.cartridge().save_memory().configure(storage::save_manager::SaveType::Flash512KB);

    int pw = 512, ph = 768;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    AuditContext ctx{emu, renderer, win, audio_stream,
                     compute_dual_screen_layout(LayoutRequest{pw, ph, 24U, true, ScreenLayoutMode::Vertical, false}),
                     pw, ph};

    // Warmup GPU shaders + initial boot frames before recording any measurements.
    for (int i = 0; i < 180; ++i) { step_one_presented_frame(ctx, false); }

    (void)advance_until_polys(ctx, 1100U, 2600, false);
    for (int i = 0; i < 60; ++i) { step_one_presented_frame(ctx, false); }
    measure_scenario(ctx, "Scenario 3 - Animated 3D Scene (Ho-Oh Title / Multi-Layer)", 300, input_idle);

    if (advance_until_2d_dialogue(ctx, 1800)) {
        for (int i = 0; i < 45; ++i) { step_one_presented_frame(ctx, false); }
        measure_scenario(ctx, "Scenario 1 - Static 2D Dialogue (Prof. Oak Intro)", 300, input_idle);
    } else {
        std::cout << "\n[WARN] Scenario 1 fallback: capturing current 2D state.\n";
        measure_scenario(ctx, "Scenario 1 - Static 2D Fallback", 300, input_idle);
    }

    if (advance_until_polys(ctx, 40U, 4500, true)) {
        for (int i = 0; i < 45; ++i) { step_one_presented_frame(ctx, false); }
        measure_scenario(ctx, "Scenario 2 - Map Character Movement (Overworld Walking)", 300, input_walking);
    } else {
        (void)ctx.emu.direct_boot();
        ctx.emu.cartridge().save_memory().configure(storage::save_manager::SaveType::Flash512KB);
        (void)advance_until_polys(ctx, 100U, 1500, false);
        measure_scenario(ctx, "Scenario 2 - 3D Scene Camera Pan + Walking Input (Forest/Intro)", 300, input_walking);
    }

    renderer.destroy();
    if (audio_stream != nullptr) { SDL_DestroyAudioStream(audio_stream); }
    SDL_GL_DestroyContext(gl_ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace elmasds::frontend
