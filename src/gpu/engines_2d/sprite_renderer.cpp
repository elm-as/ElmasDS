#include "gpu/engines_2d/engine_2d.hpp"

#include "gpu/engines_2d/color_effects.hpp"

namespace elmasds::gpu {
namespace {

constexpr u8 kSpriteWidths[4][4] = {
    {8, 16, 32, 64},
    {16, 32, 32, 64},
    {8, 8, 16, 32},
    {8, 8, 8, 8},
};

constexpr u8 kSpriteHeights[4][4] = {
    {8, 16, 32, 64},
    {8, 8, 16, 32},
    {16, 32, 32, 64},
    {8, 8, 8, 8},
};

[[nodiscard]] bool in_win_range(u16 coord, u8 start, u8 end) noexcept {
    if (start <= end) {
        return coord >= start && coord < end;
    }
    return coord >= start || coord < end;
}

}  // namespace

void Engine2D::render_sprites_line(u16 line) {
    for (std::size_t i = 0U; i < 128U; ++i) {
        evaluate_sprite(i, line);
    }
}

void Engine2D::evaluate_sprite(std::size_t obj_index, u16 line) {
    const Address oam_base = (regs_.is_engine_a() ? 0x000U : 0x400U) +
                             static_cast<Address>(obj_index * 8U);
    const u16 attr0 = memory_.oam().read16(oam_base);
    const u16 attr1 = memory_.oam().read16(oam_base + 2U);
    const u16 attr2 = memory_.oam().read16(oam_base + 4U);

    const bool rotscale = (attr0 & (1U << 8U)) != 0U;
    if (!rotscale && (attr0 & (1U << 9U)) != 0U) {
        return;  // Disabled normal sprite.
    }

    const u32 shape = (attr0 >> 14U) & 0x03U;
    if (shape == 3U) {
        return;
    }
    const u32 size_code = (attr1 >> 14U) & 0x03U;
    const i32 width = kSpriteWidths[shape][size_code];
    const i32 height = kSpriteHeights[shape][size_code];
    const bool double_size = rotscale && ((attr0 & (1U << 9U)) != 0U);
    const i32 box_w = double_size ? (width * 2) : width;
    const i32 box_h = double_size ? (height * 2) : height;

    i32 spr_y = attr0 & 0x00FFU;
    if (spr_y >= 192) {
        spr_y -= 256;
    }
    const i32 local_y = static_cast<i32>(line) - spr_y;
    if (local_y < 0 || local_y >= box_h) {
        return;
    }

    i32 spr_x = attr1 & 0x01FFU;
    if (spr_x >= 256) {
        spr_x -= 512;
    }

    const u32 obj_mode = (attr0 >> 10U) & 0x03U;
    const bool is_8bpp = (attr0 & (1U << 13U)) != 0U;
    const u8 priority = static_cast<u8>((attr2 >> 10U) & 0x03U);
    const u32 tile_base = attr2 & 0x03FFU;
    const u32 pal_bank = (attr2 >> 12U) & 0x0FU;

    i16 pa = 0x0100;
    i16 pb = 0;
    i16 pc = 0;
    i16 pd = 0x0100;
    if (rotscale) {
        const Address param_base = (regs_.is_engine_a() ? 0x000U : 0x400U) +
                                   static_cast<Address>(((attr1 >> 9U) & 0x1FU) * 32U);
        pa = static_cast<i16>(memory_.oam().read16(param_base + 6U));
        pb = static_cast<i16>(memory_.oam().read16(param_base + 14U));
        pc = static_cast<i16>(memory_.oam().read16(param_base + 22U));
        pd = static_cast<i16>(memory_.oam().read16(param_base + 30U));
    }

    const u32 dispcnt = regs_.dispcnt();
    const bool map_1d = (dispcnt & (1U << 4U)) != 0U;
    const u32 boundary_shift = regs_.is_engine_a() ? ((dispcnt >> 20U) & 0x03U) : 0U;
    const u32 boundary_bytes = 32U << boundary_shift;

    for (i32 bx = 0; bx < box_w; ++bx) {
        const i32 screen_x = spr_x + bx;
        if (screen_x < 0 || screen_x >= static_cast<i32>(kScreenWidth)) {
            continue;
        }

        i32 tex_x = bx;
        i32 tex_y = local_y;
        if (rotscale) {
            const i32 dx = bx - (box_w / 2);
            const i32 dy = local_y - (box_h / 2);
            tex_x = ((pa * dx + pb * dy) >> 8) + (width / 2);
            tex_y = ((pc * dx + pd * dy) >> 8) + (height / 2);
        } else {
            if ((attr1 & (1U << 12U)) != 0U) {
                tex_x = (width - 1) - tex_x;
            }
            if ((attr1 & (1U << 13U)) != 0U) {
                tex_y = (height - 1) - tex_y;
            }
        }
        if (tex_x < 0 || tex_x >= width || tex_y < 0 || tex_y >= height) {
            continue;
        }

        const auto ux = static_cast<std::size_t>(screen_x);
        if (obj_mode == 3U) {
            const Address bmp_addr = obj_base_address() + (tile_base * 128U) +
                                     static_cast<Address>((tex_y * width + tex_x) * 2);
            const u16 raw = read_vram16(bmp_addr);
            if ((raw & 0x8000U) != 0U && priority < work_.obj_priority[ux]) {
                work_.obj_color[ux] = bgr555_to_argb8888(raw);
                work_.obj_priority[ux] = priority;
                work_.obj_opaque[ux] = true;
                work_.obj_semi_transparent[ux] = false;
            }
            continue;
        }

        const u32 tx = static_cast<u32>(tex_x >> 3);
        const u32 ty = static_cast<u32>(tex_y >> 3);
        const u32 fx = static_cast<u32>(tex_x & 7);
        const u32 fy = static_cast<u32>(tex_y & 7);
        const u32 tiles_w = static_cast<u32>(width >> 3);
        u8 color_idx = 0U;

        if (!is_8bpp) {
            const Address base_bytes = map_1d ? (tile_base * boundary_bytes) : (tile_base * 32U);
            const u32 tile_step = map_1d ? (ty * tiles_w + tx) : (ty * 32U + tx);
            const u8 packed =
                read_vram8(obj_base_address() + base_bytes + (tile_step * 32U) + (fy * 4U) + (fx >> 1U));
            color_idx = ((fx & 1U) == 0U) ? (packed & 0x0FU) : (packed >> 4U);
        } else {
            const Address base_bytes =
                map_1d ? (tile_base * boundary_bytes) : ((tile_base & ~1U) * 32U);
            const u32 tile_step = map_1d ? (ty * tiles_w + tx) : (ty * 16U + tx);
            color_idx = read_vram8(obj_base_address() + base_bytes + (tile_step * 64U) + (fy * 8U) + fx);
        }

        if (color_idx == 0U) {
            continue;
        }
        if (obj_mode == 2U) {
            work_.obj_window[ux] = true;
            continue;
        }
        if (priority < work_.obj_priority[ux]) {
            const u16 col = is_8bpp
                                ? read_obj_palette(pal_bank, color_idx, true)
                                : memory_.palette().read16((regs_.is_engine_a() ? 0x200U : 0x600U) +
                                                           (pal_bank * 32U) + (color_idx * 2U));
            work_.obj_color[ux] = bgr555_to_argb8888(col);
            work_.obj_priority[ux] = priority;
            work_.obj_opaque[ux] = true;
            work_.obj_semi_transparent[ux] = (obj_mode == 1U);
        }
    }
}

void Engine2D::compute_window_masks(u16 line) noexcept {
    const u32 dispcnt = regs_.dispcnt();
    const bool win0_en = (dispcnt & (1U << 13U)) != 0U;
    const bool win1_en = (dispcnt & (1U << 14U)) != 0U;
    const bool objwin_en = (dispcnt & (1U << 15U)) != 0U;
    if (!win0_en && !win1_en && !objwin_en) {
        work_.win_mask.fill(0x3FU);
        return;
    }

    const u8 w0_x1 = static_cast<u8>((regs_.win0_h() >> 8U) & 0xFFU);
    const u8 w0_x2 = static_cast<u8>(regs_.win0_h() & 0xFFU);
    const u8 w0_y1 = static_cast<u8>((regs_.win0_v() >> 8U) & 0xFFU);
    const u8 w0_y2 = static_cast<u8>(regs_.win0_v() & 0xFFU);
    const bool w0_y_ok = win0_en && in_win_range(line, w0_y1, w0_y2);

    const u8 w1_x1 = static_cast<u8>((regs_.win1_h() >> 8U) & 0xFFU);
    const u8 w1_x2 = static_cast<u8>(regs_.win1_h() & 0xFFU);
    const u8 w1_y1 = static_cast<u8>((regs_.win1_v() >> 8U) & 0xFFU);
    const u8 w1_y2 = static_cast<u8>(regs_.win1_v() & 0xFFU);
    const bool w1_y_ok = win1_en && in_win_range(line, w1_y1, w1_y2);

    const u8 mask_in0 = static_cast<u8>(regs_.win_in() & 0x3FU);
    const u8 mask_in1 = static_cast<u8>((regs_.win_in() >> 8U) & 0x3FU);
    const u8 mask_out = static_cast<u8>(regs_.win_out() & 0x3FU);
    const u8 mask_obj = static_cast<u8>((regs_.win_out() >> 8U) & 0x3FU);

    for (std::size_t x = 0U; x < kScreenWidth; ++x) {
        const u16 ux = static_cast<u16>(x);
        if (w0_y_ok && in_win_range(ux, w0_x1, w0_x2)) {
            work_.win_mask[x] = mask_in0;
        } else if (w1_y_ok && in_win_range(ux, w1_x1, w1_x2)) {
            work_.win_mask[x] = mask_in1;
        } else if (objwin_en && work_.obj_window[x]) {
            work_.win_mask[x] = mask_obj;
        } else {
            work_.win_mask[x] = mask_out;
        }
    }
}

u32 Engine2D::composite_pixel(std::size_t x, u32 backdrop) const noexcept {
    const u8 wmask = work_.win_mask[x];
    u32 top1_color = backdrop;
    u32 top2_color = backdrop;
    u8 top1_layer = 5U;  // 0..3 = BG0..3, 4 = OBJ, 5 = Backdrop
    u8 top2_layer = 5U;
    bool top1_semi = false;

    for (u8 prio = 0U; prio < 4U; ++prio) {
        if (work_.obj_opaque[x] && work_.obj_priority[x] == prio && (wmask & (1U << 4U)) != 0U) {
            if (top1_layer == 5U) {
                top1_color = work_.obj_color[x];
                top1_layer = 4U;
                top1_semi = work_.obj_semi_transparent[x];
            } else if (top2_layer == 5U) {
                top2_color = work_.obj_color[x];
                top2_layer = 4U;
            }
        }
        for (std::size_t bg = 0U; bg < 4U; ++bg) {
            if (!work_.bg_opaque[bg][x] || (wmask & (1U << bg)) == 0U ||
                (regs_.bg_cnt(bg) & 0x03U) != prio) {
                continue;
            }
            if (top1_layer == 5U) {
                top1_color = work_.bg_color[bg][x];
                top1_layer = static_cast<u8>(bg);
            } else if (top2_layer == 5U) {
                top2_color = work_.bg_color[bg][x];
                top2_layer = static_cast<u8>(bg);
            }
        }
    }

    const u16 bldcnt = regs_.bldcnt();
    const bool sfx_allowed = (wmask & (1U << 5U)) != 0U;
    const bool is_1st = (bldcnt & (1U << top1_layer)) != 0U;
    const bool is_2nd = (bldcnt & (1U << (8U + top2_layer))) != 0U;
    const u32 eva = regs_.bldalpha() & 0x1FU;
    const u32 evb = (regs_.bldalpha() >> 8U) & 0x1FU;

    if (top1_semi && is_2nd) {
        return blend_argb8888(top1_color, top2_color, eva, evb);
    }
    if (!sfx_allowed || !is_1st) {
        return top1_color;
    }

    const u32 effect = (bldcnt >> 6U) & 0x03U;
    if (effect == 1U && is_2nd) {
        return blend_argb8888(top1_color, top2_color, eva, evb);
    }
    if (effect == 2U || effect == 3U) {
        return fade_argb8888(top1_color, effect - 1U, regs_.bldy() & 0x1FU);
    }
    return top1_color;
}

}  // namespace elmasds::gpu
