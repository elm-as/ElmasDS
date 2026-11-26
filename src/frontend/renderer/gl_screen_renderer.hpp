#pragma once

#include "frontend/window/display_layout.hpp"
#include "primitives/fixed_width_types.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <span>

namespace elmasds::frontend {

/**
 * @brief Visual shader preset applied when presenting both Nintendo DS screens.
 */
enum class ShaderFilterMode : u8 {
    SharpNearest = 0,
    LcdGrid = 1,
    ColorCorrect = 2,
    LcdColorCorrect = 3,
};

/**
 * @brief OpenGL 3.3 Core dual-screen framebuffer renderer with DS LCD shaders and RGBA overlay.
 */
class GlScreenRenderer final {
public:
    GlScreenRenderer() noexcept = default;
    ~GlScreenRenderer() noexcept;

    GlScreenRenderer(const GlScreenRenderer&) = delete;
    GlScreenRenderer& operator=(const GlScreenRenderer&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    void destroy() noexcept;

    void set_filter_mode(ShaderFilterMode mode) noexcept { filter_mode_ = mode; }
    [[nodiscard]] ShaderFilterMode filter_mode() const noexcept { return filter_mode_; }

    void render(const DualScreenLayout& layout,
                i32 window_width,
                i32 window_height,
                std::span<const u32> top_pixels,
                std::span<const u32> bottom_pixels) noexcept;

    /**
     * @brief Draws an alpha-blended full-window RGBA8888 overlay buffer over the DS screens.
     */
    void render_overlay(std::span<const u32> rgba_pixels, i32 width, i32 height) noexcept;

private:
    void draw_screen_quad(const ScreenRect& rect,
                          i32 window_height,
                          GLuint texture,
                          std::span<const u32> pixels) noexcept;

    GLuint program_ = 0U;
    GLuint vao_ = 0U;
    GLuint top_tex_ = 0U;
    GLuint bottom_tex_ = 0U;
    GLuint overlay_tex_ = 0U;
    GLint u_filter_loc_ = -1;
    GLint u_screen_size_loc_ = -1;
    i32 overlay_w_ = 0;
    i32 overlay_h_ = 0;
    ShaderFilterMode filter_mode_ = ShaderFilterMode::SharpNearest;
    bool initialized_ = false;
};

}  // namespace elmasds::frontend
