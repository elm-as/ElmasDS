#include "gpu/engines_2d/engine_2d.hpp"

#include "gpu/engines_2d/color_effects.hpp"

#include <algorithm>

namespace elmasds::gpu {
namespace {

[[nodiscard]] inline u16 read_le16(const u8* data, std::size_t off) noexcept {
    return static_cast<u16>(data[off]) | (static_cast<u16>(data[off + 1U]) << 8U);
}

}  // namespace

void ScanlineWorkBuffers::clear_for_line() noexcept {
    for (auto& layer : bg_opaque) { layer.fill(false); }
    obj_opaque.fill(false); obj_priority.fill(4U);
    obj_semi_transparent.fill(false); obj_window.fill(false);
    win_mask.fill(0x3FU); has_semi_obj = false;
}

Engine2D::Engine2D(const GpuRegisters& regs, const memory::MemorySystem& memory) noexcept
    : regs_(regs), memory_(memory) {}

void Engine2D::set_line_3d_buffer(std::span<const u32> line_3d) noexcept { line_3d_ = line_3d; }
Address Engine2D::bg_base_address() const noexcept { return regs_.is_engine_a() ? 0x06000000U : 0x06200000U; }
Address Engine2D::obj_base_address() const noexcept { return regs_.is_engine_a() ? 0x06400000U : 0x06600000U; }
u16 Engine2D::read_vram16(Address offset) const noexcept { return memory_.vram().read_arm9_16(offset); }
u8 Engine2D::read_vram8(Address offset) const noexcept { return memory_.vram().read_arm9_8(offset); }

u16 Engine2D::read_bg_palette(std::size_t bg, u32 pal_bank, u8 color_idx) const noexcept {
    const bool ext_enabled = (regs_.dispcnt() & (1U << 30U)) != 0U;
    if (ext_enabled) {
        const u16 cnt = regs_.bg_cnt(bg);
        const std::size_t slot =
            ((bg < 2U) && ((cnt & (1U << 13U)) != 0U)) ? (bg + 2U) : bg;
        const std::size_t byte_off =
            (slot * 8192U) + ((pal_bank & 15U) * 512U) + (static_cast<std::size_t>(color_idx) * 2U);
        const auto bank = regs_.is_engine_a() ? memory::VramBankId::E : memory::VramBankId::H;
        const auto span = memory_.vram().bank_data(bank);
        if (byte_off + 1U < span.size()) { return read_le16(span.data(), byte_off); }
    }
    const Address base = regs_.is_engine_a() ? 0x000U : 0x400U;
    return memory_.palette().read16(base + static_cast<Address>(color_idx) * 2U);
}

u16 Engine2D::read_obj_palette(u32 pal_bank, u8 color_idx, bool use_ext) const noexcept {
    if (use_ext && (regs_.dispcnt() & (1U << 31U)) != 0U) {
        const std::size_t byte_off =
            ((pal_bank & 15U) * 512U) + (static_cast<std::size_t>(color_idx) * 2U);
        const auto bank = regs_.is_engine_a() ? memory::VramBankId::F : memory::VramBankId::I;
        const auto span = memory_.vram().bank_data(bank);
        if (byte_off + 1U < span.size()) { return read_le16(span.data(), byte_off); }
    }
    const Address base = regs_.is_engine_a() ? 0x200U : 0x600U;
    return memory_.palette().read16(base + static_cast<Address>(color_idx) * 2U);
}

void Engine2D::render_scanline(u16 line, std::span<u32, kScreenWidth> out_line) {
    const u32 dispcnt = regs_.dispcnt();
    const u32 disp_mode =
        regs_.is_engine_a() ? ((dispcnt >> 16U) & 0x03U) : ((dispcnt >> 16U) & 0x01U);

    if (disp_mode == 0U) { std::fill(out_line.begin(), out_line.end(), 0xFFFFFFFFU); return; }
    if (disp_mode == 2U && regs_.is_engine_a()) {
        render_vram_display_line(line, out_line);
        apply_master_brightness(out_line, regs_.master_bright());
        return;
    }
    if (disp_mode == 3U) {
        std::fill(out_line.begin(), out_line.end(), 0xFF000000U);
        apply_master_brightness(out_line, regs_.master_bright());
        return;
    }

    render_normal_line(line, out_line);
    apply_master_brightness(out_line, regs_.master_bright());
}

void Engine2D::render_vram_display_line(u16 line,
                                        std::span<u32, kScreenWidth> out_line) const noexcept {
    const auto bank = static_cast<memory::VramBankId>((regs_.dispcnt() >> 18U) & 0x03U);
    const auto data = memory_.vram().bank_data(bank);
    const std::size_t row_base = static_cast<std::size_t>(line) * kScreenWidth * 2U;

    for (std::size_t x = 0U; x < kScreenWidth; ++x) {
        const std::size_t idx = row_base + x * 2U;
        const u16 raw = (idx + 1U < data.size()) ? read_le16(data.data(), idx) : 0U;
        out_line[x] = bgr555_to_argb8888(raw);
    }
}

void Engine2D::render_normal_line(u16 line, std::span<u32, kScreenWidth> out_line) {
    const u32 dispcnt = regs_.dispcnt();
    const u32 bg_mode = dispcnt & 0x07U;
    const u8 active_bgs = static_cast<u8>((dispcnt >> 8U) & 0x0FU);
    const bool obj_enabled = (dispcnt & (1U << 12U)) != 0U;

    for (std::size_t bg = 0U; bg < 4U; ++bg) {
        if ((active_bgs & (1U << bg)) != 0U) {
            work_.bg_opaque[bg].fill(false);
            render_bg_layer(bg, line, bg_mode);
        }
    }
    if (obj_enabled) {
        work_.obj_opaque.fill(false); work_.obj_priority.fill(4U);
        work_.obj_semi_transparent.fill(false); work_.obj_window.fill(false);
        work_.has_semi_obj = false;
        render_sprites_line(line);
    }
    if ((dispcnt & 0xE000U) != 0U) {
        compute_window_masks(line);
    }

    const Address pal_base = regs_.is_engine_a() ? 0x000U : 0x400U;
    const u32 backdrop = bgr555_to_argb8888(memory_.palette().read16(pal_base));
    composite_scanline(out_line, backdrop, active_bgs);
}

void Engine2D::render_bg_layer(std::size_t bg, u16 line, u32 bg_mode) {
    if (bg == 0U && regs_.is_engine_a() && (regs_.dispcnt() & (1U << 3U)) != 0U) {
        if (line_3d_.size() < kScreenWidth) { return; }
        const u32 hofs = regs_.bg_hofs(0U) & 0x1FFU;
        for (std::size_t x = 0U; x < kScreenWidth; ++x) {
            const u32 src_x = (static_cast<u32>(x) + hofs) & 0x1FFU;
            if (src_x >= kScreenWidth) { continue; }
            const u32 px = line_3d_[src_x];
            if ((px >> 24U) != 0U) { work_.bg_color[0U][x] = px | 0xFF000000U; work_.bg_opaque[0U][x] = true; }
        }
        return;
    }

    if (bg < 2U) {
        render_text_bg_line(bg, line);
        return;
    }
    if (bg == 2U) {
        if (bg_mode == 0U || bg_mode == 1U || bg_mode == 3U) { render_text_bg_line(2U, line); }
        else if (bg_mode == 2U || bg_mode == 4U) { render_affine_bg_line(2U, line); }
        else { render_extended_bg_line(2U, line); }
        return;
    }

    if (bg_mode == 0U) { render_text_bg_line(3U, line); }
    else if (bg_mode == 1U || bg_mode == 2U) { render_affine_bg_line(3U, line); }
    else if (bg_mode >= 3U && bg_mode <= 5U) { render_extended_bg_line(3U, line); }
}

void Engine2D::render_text_bg_line(std::size_t bg, u16 line) {
    const u16 cnt = regs_.bg_cnt(bg);
    const u32 dispcnt = regs_.dispcnt();
    const bool is_8bpp = (cnt & (1U << 7U)) != 0U;
    const bool mosaic_en = (cnt & (1U << 6U)) != 0U;
    const u32 mos_x = mosaic_en ? ((regs_.mosaic() & 0x0FU) + 1U) : 1U;
    const u32 mos_y = mosaic_en ? (((regs_.mosaic() >> 4U) & 0x0FU) + 1U) : 1U;
    const u32 eff_line = (line / mos_y) * mos_y;

    const u32 char_step = regs_.is_engine_a() ? ((dispcnt >> 24U) & 0x07U) * 0x10000U : 0U;
    const u32 scr_step = regs_.is_engine_a() ? ((dispcnt >> 27U) & 0x07U) * 0x10000U : 0U;
    const Address char_base = bg_base_address() + char_step + (((cnt >> 2U) & 0x0FU) * 0x4000U);
    const Address scr_base = bg_base_address() + scr_step + (((cnt >> 8U) & 0x1FU) * 0x0800U);

    const u32 size_mode = (cnt >> 14U) & 0x03U;
    const u32 width_mask = ((size_mode & 1U) != 0U) ? 511U : 255U;
    const u32 height_mask = ((size_mode & 2U) != 0U) ? 511U : 255U;
    const u32 py = (eff_line + regs_.bg_vofs(bg)) & height_mask;
    const u32 ty = py >> 3U;
    const u16 hofs = regs_.bg_hofs(bg);
    const u8* const pal_ptr = memory_.palette().data().data();
    const Address pal_base = regs_.is_engine_a() ? 0x000U : 0x400U;
    const bool ext_pal = is_8bpp && ((dispcnt & (1U << 30U)) != 0U);
    const std::span<const u8> ext_span =
        ext_pal ? memory_.vram().bank_data(regs_.is_engine_a() ? memory::VramBankId::E
                                                               : memory::VramBankId::H)
                : std::span<const u8>{};
    const std::size_t ext_slot_base =
        (((bg < 2U) && ((cnt & (1U << 13U)) != 0U)) ? (bg + 2U) : bg) * 8192U;

    const auto& vram = memory_.vram();
    u32 cached_key = 0xFFFFFFFFU;
    u16 entry = 0U;
    u32 row4 = 0U;
    u32 row8_lo = 0U;
    u32 row8_hi = 0U;
    for (std::size_t x = 0U; x < kScreenWidth; ++x) {
        const std::size_t mx = mosaic_en ? ((x / mos_x) * mos_x) : x;
        const u32 px = (static_cast<u32>(mx) + hofs) & width_mask;
        const u32 tx = px >> 3U;
        const u32 block = (size_mode == 2U) ? (ty >> 5U)
                                            : ((tx >> 5U) + ((ty >> 5U) * ((size_mode == 3U) ? 2U : 1U)));
        const u32 key = (block << 8U) | tx;
        if (key != cached_key) {
            cached_key = key;
            entry = read_vram16(scr_base + (block * 0x800U) + (((ty & 31U) * 32U + (tx & 31U)) * 2U));
            const u32 tile_num = entry & 0x03FFU;
            const u32 fy = ((entry & (1U << 11U)) != 0U) ? (7U - (py & 7U)) : (py & 7U);
            if (!is_8bpp) {
                row4 = vram.read_arm9_32_fast(char_base + (tile_num * 32U) + (fy * 4U));
            } else {
                const Address raddr = char_base + (tile_num * 64U) + (fy * 8U);
                row8_lo = vram.read_arm9_32_fast(raddr);
                row8_hi = vram.read_arm9_32_fast(raddr + 4U);
            }
        }
        const u32 fx = ((entry & (1U << 10U)) != 0U) ? (7U - (px & 7U)) : (px & 7U);
        if (!is_8bpp) {
            const u8 idx = static_cast<u8>((row4 >> (fx * 4U)) & 0x0FU);
            if (idx != 0U) {
                const Address off = pal_base + (((entry >> 12U) & 0x0FU) * 32U) + (idx * 2U);
                work_.bg_color[bg][x] = bgr555_to_argb8888(read_le16(pal_ptr, off));
                work_.bg_opaque[bg][x] = true;
            }
            continue;
        }
        const u8 idx = static_cast<u8>((((fx < 4U) ? row8_lo : row8_hi) >> ((fx & 3U) * 8U)) & 0xFFU);
        if (idx == 0U) { continue; }
        u16 col = 0U;
        if (ext_pal) {
            const std::size_t off = ext_slot_base + (((entry >> 12U) & 0x0FU) * 512U) + (idx * 2U);
            if (off + 1U < ext_span.size()) { col = read_le16(ext_span.data(), off); }
        } else {
            col = read_le16(pal_ptr, pal_base + (static_cast<Address>(idx) * 2U));
        }
        work_.bg_color[bg][x] = bgr555_to_argb8888(col);
        work_.bg_opaque[bg][x] = true;
    }
}

void Engine2D::render_affine_bg_line(std::size_t bg, u16 line) {
    (void)line;
    const u16 cnt = regs_.bg_cnt(bg);
    const u32 dispcnt = regs_.dispcnt();
    const u32 char_step = regs_.is_engine_a() ? ((dispcnt >> 24U) & 0x07U) * 0x10000U : 0U;
    const u32 scr_step = regs_.is_engine_a() ? ((dispcnt >> 27U) & 0x07U) * 0x10000U : 0U;
    const Address char_base = bg_base_address() + char_step + (((cnt >> 2U) & 0x0FU) * 0x4000U);
    const Address scr_base = bg_base_address() + scr_step + (((cnt >> 8U) & 0x1FU) * 0x0800U);

    const i32 dim = 128 << ((cnt >> 14U) & 0x03U);
    const bool wrap = (cnt & (1U << 13U)) != 0U;
    const AffineBgParams& aff = regs_.affine(bg - 2U);
    i32 cur_x = aff.internal_x;
    i32 cur_y = aff.internal_y;

    for (std::size_t x = 0U; x < kScreenWidth; ++x, cur_x += aff.pa, cur_y += aff.pc) {
        i32 px = cur_x >> 8;
        i32 py = cur_y >> 8;
        if (px < 0 || px >= dim || py < 0 || py >= dim) {
            if (!wrap) { continue; }
            px &= (dim - 1); py &= (dim - 1);
        }
        const u32 tiles_per_row = static_cast<u32>(dim >> 3);
        const u8 tile_num = read_vram8(
            scr_base + (static_cast<u32>(py >> 3) * tiles_per_row) + static_cast<u32>(px >> 3));
        const u8 idx = read_vram8(
            char_base + (tile_num * 64U) + (static_cast<u32>(py & 7) * 8U) + static_cast<u32>(px & 7));
        if (idx != 0U) {
            work_.bg_color[bg][x] = bgr555_to_argb8888(read_bg_palette(bg, 0U, idx));
            work_.bg_opaque[bg][x] = true;
        }
    }
}

void Engine2D::render_extended_bg_line(std::size_t bg, u16 line) {
    if ((regs_.bg_cnt(bg) & (1U << 7U)) == 0U) { render_extended_rotscale_line(bg, line); }
    else { render_extended_bitmap_line(bg, line); }
}

void Engine2D::render_extended_rotscale_line(std::size_t bg, u16 line) {
    (void)line;
    const u16 cnt = regs_.bg_cnt(bg);
    const u32 dispcnt = regs_.dispcnt();
    const u32 char_step = regs_.is_engine_a() ? ((dispcnt >> 24U) & 0x07U) * 0x10000U : 0U;
    const u32 scr_step = regs_.is_engine_a() ? ((dispcnt >> 27U) & 0x07U) * 0x10000U : 0U;
    const Address char_base = bg_base_address() + char_step + (((cnt >> 2U) & 0x0FU) * 0x4000U);
    const Address scr_base = bg_base_address() + scr_step + (((cnt >> 8U) & 0x1FU) * 0x0800U);
    const i32 dim = 128 << ((cnt >> 14U) & 0x03U);
    const bool wrap = (cnt & (1U << 13U)) != 0U;
    const AffineBgParams& aff = regs_.affine(bg - 2U);
    i32 cur_x = aff.internal_x;
    i32 cur_y = aff.internal_y;

    for (std::size_t x = 0U; x < kScreenWidth; ++x, cur_x += aff.pa, cur_y += aff.pc) {
        i32 px = cur_x >> 8;
        i32 py = cur_y >> 8;
        if (px < 0 || px >= dim || py < 0 || py >= dim) {
            if (!wrap) { continue; }
            px &= (dim - 1); py &= (dim - 1);
        }
        const u32 tiles_per_row = static_cast<u32>(dim >> 3);
        const u16 entry = read_vram16(
            scr_base + ((static_cast<u32>(py >> 3) * tiles_per_row + static_cast<u32>(px >> 3)) * 2U));
        const u32 tile_num = entry & 0x03FFU;
        const u32 fx = ((entry & (1U << 10U)) != 0U) ? (7U - static_cast<u32>(px & 7))
                                                     : static_cast<u32>(px & 7);
        const u32 fy = ((entry & (1U << 11U)) != 0U) ? (7U - static_cast<u32>(py & 7))
                                                     : static_cast<u32>(py & 7);
        const u8 idx = read_vram8(char_base + (tile_num * 64U) + (fy * 8U) + fx);
        if (idx != 0U) {
            const u32 pal_bank = (entry >> 12U) & 0x0FU;
            work_.bg_color[bg][x] = bgr555_to_argb8888(read_bg_palette(bg, pal_bank, idx));
            work_.bg_opaque[bg][x] = true;
        }
    }
}

void Engine2D::render_extended_bitmap_line(std::size_t bg, u16 line) {
    (void)line;
    const u16 cnt = regs_.bg_cnt(bg);
    const bool direct_color = (cnt & (1U << 2U)) != 0U;
    const bool wrap = (cnt & (1U << 13U)) != 0U;
    const Address bmp_base = bg_base_address() + (((cnt >> 8U) & 0x1FU) * 0x4000U);
    static constexpr i32 kWidths[4] = {128, 256, 512, 512};
    static constexpr i32 kHeights[4] = {128, 256, 256, 512};
    const u32 sz = (cnt >> 14U) & 0x03U;
    const i32 w = kWidths[sz];
    const i32 h = kHeights[sz];

    const AffineBgParams& aff = regs_.affine(bg - 2U);
    i32 cur_x = aff.internal_x;
    i32 cur_y = aff.internal_y;

    for (std::size_t x = 0U; x < kScreenWidth; ++x, cur_x += aff.pa, cur_y += aff.pc) {
        i32 px = cur_x >> 8;
        i32 py = cur_y >> 8;
        if (px < 0 || px >= w || py < 0 || py >= h) {
            if (!wrap) { continue; }
            px &= (w - 1); py &= (h - 1);
        }
        const u32 offset = static_cast<u32>(py * w + px);
        if (direct_color) {
            const u16 raw = read_vram16(bmp_base + offset * 2U);
            if ((raw & 0x8000U) != 0U) {
                work_.bg_color[bg][x] = bgr555_to_argb8888(raw);
                work_.bg_opaque[bg][x] = true;
            }
        } else {
            const u8 idx = read_vram8(bmp_base + offset);
            if (idx != 0U) {
                work_.bg_color[bg][x] = bgr555_to_argb8888(read_bg_palette(bg, 0U, idx));
                work_.bg_opaque[bg][x] = true;
            }
        }
    }
}

}  // namespace elmasds::gpu
