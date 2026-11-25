#pragma once

#include "frontend/window/display_layout.hpp"
#include "primitives/fixed_width_types.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <span>

namespace elmasds::frontend {

/**
 * @brief OpenGL 3.3 Core dual-screen framebuffer renderer using `GL_NEAREST` pixel-sharp filtering.
 *
 * Responsibility:
 *   Loads required OpenGL 3.3 Core entry points via `SDL_GL_GetProcAddress`, compiles a minimal
 *   textured-quad shader pair, uploads the 256×192 `ARGB8888` top and bottom framebuffers to
 *   2D textures, and draws both screens inside the aspect-preserving `DualScreenLayout` viewports.
 */
class GlScreenRenderer final {
public:
    GlScreenRenderer() noexcept = default;
    ~GlScreenRenderer() noexcept;

    GlScreenRenderer(const GlScreenRenderer&) = delete;
    GlScreenRenderer& operator=(const GlScreenRenderer&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    void destroy() noexcept;

    void render(const DualScreenLayout& layout,
                i32 window_width,
                i32 window_height,
                std::span<const u32> top_pixels,
                std::span<const u32> bottom_pixels) noexcept;

private:
    void draw_screen_quad(const ScreenRect& rect,
                          i32 window_height,
                          GLuint texture,
                          std::span<const u32> pixels) noexcept;

    GLuint program_ = 0U;
    GLuint vao_ = 0U;
    GLuint top_tex_ = 0U;
    GLuint bottom_tex_ = 0U;
    bool initialized_ = false;
};

}  // namespace elmasds::frontend
