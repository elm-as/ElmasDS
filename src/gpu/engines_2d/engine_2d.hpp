#pragma once

#include "gpu/registers/gpu_registers.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace elmasds::gpu {

/**
 * @brief Per-scanline compositing state for a single 2D graphics engine (Engine A or Engine B).
 */
struct ScanlineWorkBuffers {
    std::array<std::array<u32, kScreenWidth>, 4U> bg_color{};
    std::array<std::array<bool, kScreenWidth>, 4U> bg_opaque{};

    std::array<u32, kScreenWidth> obj_color{};
    std::array<u8, kScreenWidth> obj_priority{};
    std::array<bool, kScreenWidth> obj_opaque{};
    std::array<bool, kScreenWidth> obj_semi_transparent{};
    std::array<bool, kScreenWidth> obj_window{};
    std::array<u8, kScreenWidth> win_mask{};

    void clear_for_line() noexcept;
};

/**
 * @brief Scanline-accurate 2D graphics renderer for Nintendo DS Engine A or Engine B.
 *
 * Responsibility:
 *   Renders 256-pixel `ARGB8888` scanlines covering Display Modes 0-2, BG Modes 0-5 (text 4/8bpp,
 *   affine rotscale, extended 16-bit tilemap, 256-color & direct-color bitmaps), 128 OAM sprites
 *   (normal, affine, double-size, bitmap, semi-transparent, OBJ window), extended palettes,
 *   mosaic, windows, priority compositing, alpha blending, brightness fade, and master brightness.
 */
class Engine2D {
public:
    Engine2D(const GpuRegisters& regs, const memory::MemorySystem& memory) noexcept;

    /**
     * @brief Supplies an optional 256-pixel 3D scanline (`ARGB8888`, alpha > 0 = opaque) for BG0.
     */
    void set_line_3d_buffer(std::span<const u32> line_3d) noexcept;

    /**
     * @brief Renders scanline `line` (`0..191`) into `out_line` (`256` `ARGB8888` pixels).
     */
    void render_scanline(u16 line, std::span<u32, kScreenWidth> out_line);

private:
    void render_vram_display_line(u16 line, std::span<u32, kScreenWidth> out_line) const noexcept;
    void render_normal_line(u16 line, std::span<u32, kScreenWidth> out_line);

    void render_bg_layer(std::size_t bg, u16 line, u32 bg_mode);
    void render_text_bg_line(std::size_t bg, u16 line);
    void render_affine_bg_line(std::size_t bg, u16 line);
    void render_extended_bg_line(std::size_t bg, u16 line);
    void render_extended_rotscale_line(std::size_t bg, u16 line);
    void render_extended_bitmap_line(std::size_t bg, u16 line);

    void render_sprites_line(u16 line);
    void evaluate_sprite(std::size_t obj_index, u16 line);
    void compute_window_masks(u16 line) noexcept;
    [[nodiscard]] u32 composite_pixel(std::size_t x, u32 backdrop) const noexcept;

    [[nodiscard]] u16 read_vram16(Address offset) const noexcept;
    [[nodiscard]] u8 read_vram8(Address offset) const noexcept;
    [[nodiscard]] u16 read_bg_palette(std::size_t bg, u32 pal_bank, u8 color_idx) const noexcept;
    [[nodiscard]] u16 read_obj_palette(u32 pal_bank, u8 color_idx, bool use_ext) const noexcept;
    [[nodiscard]] Address bg_base_address() const noexcept;
    [[nodiscard]] Address obj_base_address() const noexcept;

    const GpuRegisters& regs_;
    const memory::MemorySystem& memory_;
    std::span<const u32> line_3d_{};
    ScanlineWorkBuffers work_{};
};

}  // namespace elmasds::gpu
