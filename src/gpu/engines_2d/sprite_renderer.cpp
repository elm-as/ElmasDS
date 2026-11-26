#include "gpu/engines_2d/engine_2d.hpp"

#include "gpu/engines_2d/color_effects.hpp"

#include <algorithm>

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

struct CompositeConfig {
    u32 backdrop = 0U;
    u8 active_bgs = 0U;
    bool obj_enabled = false;
    bool use_win = false;
    std::array<u8, 4U> bg_prio{};
    u16 bldcnt = 0U;
    u32 effect = 0U;
    u32 eva = 0U;
    u32 evb = 0U;
    u32 bldy = 0U;
};

[[nodiscard]] inline u16 read_le16(const u8* data, std::size_t off) noexcept {
    return static_cast<u16>(data[off]) | (static_cast<u16>(data[off + 1U]) << 8U);
}

[[nodiscard]] bool in_win_range(u16 coord, u8 start, u8 end) noexcept {
    return (start <= end) ? (coord >= start && coord < end) : (coord >= start || coord < end);
}

[[nodiscard]] inline u32 composite_pixel_fast(const ScanlineWorkBuffers& work,
                                              std::size_t x,
                                              const CompositeConfig& cfg) noexcept {
    const u8 wmask = cfg.use_win ? work.win_mask[x] : 0x3FU;
    u8 k1 = 0xFFU;
    u8 k2 = 0xFFU;
    u32 c1 = cfg.backdrop;
    u32 c2 = cfg.backdrop;
    u8 l1 = 5U;
    u8 l2 = 5U;
    bool semi1 = false;

    const auto consider = [&](u8 key, u8 layer, u32 color, bool semi) noexcept {
        if (key < k1) {
            k2 = k1; c2 = c1; l2 = l1;
            k1 = key; c1 = color; l1 = layer; semi1 = semi;
        } else if (key < k2) {
            k2 = key; c2 = color; l2 = layer;
        }
    };

    if (cfg.obj_enabled && work.obj_opaque[x] && (wmask & (1U << 4U)) != 0U) {
        consider(static_cast<u8>(work.obj_priority[x] << 3U), 4U, work.obj_color[x],
                 work.obj_semi_transparent[x]);
    }
    for (std::size_t bg = 0U; bg < 4U; ++bg) {
        if ((cfg.active_bgs & (1U << bg)) != 0U && work.bg_opaque[bg][x] &&
            (wmask & (1U << bg)) != 0U) {
            const u8 key = static_cast<u8>((cfg.bg_prio[bg] << 3U) | (bg + 1U));
            consider(key, static_cast<u8>(bg), work.bg_color[bg][x], false);
        }
    }

    const bool is_2nd = (cfg.bldcnt & (1U << (8U + l2))) != 0U;
    if (semi1 && is_2nd) {
        return blend_argb8888(c1, c2, cfg.eva, cfg.evb);
    }
    if ((wmask & (1U << 5U)) == 0U || (cfg.bldcnt & (1U << l1)) == 0U) {
        return c1;
    }
    if (cfg.effect == 1U && is_2nd) {
        return blend_argb8888(c1, c2, cfg.eva, cfg.evb);
    }
    if (cfg.effect == 2U || cfg.effect == 3U) {
        return fade_argb8888(c1, cfg.effect - 1U, cfg.bldy);
    }
    return c1;
}

void composite_scanline_no_blend(const ScanlineWorkBuffers& work,
                                 std::span<u32, kScreenWidth> out_line,
                                 const CompositeConfig& cfg) noexcept {
    std::array<u8, 4U> order{};
    u32 count = 0U;
    for (u8 bg = 0U; bg < 4U; ++bg) {
        if ((cfg.active_bgs & (1U << bg)) != 0U) { order[count++] = bg; }
    }
    for (u32 i = 1U; i < count; ++i) {
        const u8 cur = order[i];
        const u8 cur_key = static_cast<u8>((cfg.bg_prio[cur] << 3U) | (cur + 1U));
        u32 j = i;
        while (j > 0U && ((cfg.bg_prio[order[j - 1U]] << 3U) | (order[j - 1U] + 1U)) > cur_key) {
            order[j] = order[j - 1U];
            --j;
        }
        order[j] = cur;
    }
    if (count == 0U && !cfg.obj_enabled) {
        std::fill(out_line.begin(), out_line.end(), cfg.backdrop);
        return;
    }
    for (std::size_t x = 0U; x < kScreenWidth; ++x) {
        u8 best_key = 0xFFU;
        u32 best_color = cfg.backdrop;
        for (u32 i = 0U; i < count; ++i) {
            const u8 bg = order[i];
            if (work.bg_opaque[bg][x]) {
                best_key = static_cast<u8>((cfg.bg_prio[bg] << 3U) | (bg + 1U));
                best_color = work.bg_color[bg][x];
                break;
            }
        }
        if (cfg.obj_enabled && work.obj_opaque[x] &&
            static_cast<u8>(work.obj_priority[x] << 3U) < best_key) {
            best_color = work.obj_color[x];
        }
        out_line[x] = best_color;
    }
}

}  // namespace

void Engine2D::render_sprites_line(u16 line) {
    if (profiler_ != nullptr && profiler_->is_enabled()) { profiler_->add_sprites_evaluated(128U); }
    const u8* const oam = memory_.oam().data().data() + (regs_.is_engine_a() ? 0U : 0x400U);
    for (std::size_t i = 0U; i < 128U; ++i) {
        const u16 attr0 = read_le16(oam, i * 8U);
        if ((attr0 & 0x0300U) == 0x0200U || ((attr0 >> 14U) & 3U) == 3U) { continue; }
        evaluate_sprite(i, line);
    }
}

void Engine2D::evaluate_sprite(std::size_t obj_index, u16 line) {
    const bool prof = profiler_ != nullptr && profiler_->is_enabled();
    const u8* const oam = memory_.oam().data().data();
    const Address oam_base =
        (regs_.is_engine_a() ? 0x000U : 0x400U) + static_cast<Address>(obj_index * 8U);
    const u16 attr0 = read_le16(oam, oam_base);
    if ((attr0 & 0x0300U) == 0x0200U) {
        return;  // Disabled normal sprite.
    }
    const u32 shape = (attr0 >> 14U) & 0x03U;
    if (shape == 3U) {
        return;
    }

    const bool rotscale = (attr0 & (1U << 8U)) != 0U;
    const u16 attr1 = read_le16(oam, oam_base + 2U);
    const u32 size_code = (attr1 >> 14U) & 0x03U;
    const i32 width = kSpriteWidths[shape][size_code];
    const i32 height = kSpriteHeights[shape][size_code];
    const bool double_size = rotscale && ((attr0 & (1U << 9U)) != 0U);
    const i32 box_w = double_size ? (width * 2) : width;
    const i32 box_h = double_size ? (height * 2) : height;

    i32 spr_y = attr0 & 0x00FFU;
    if (spr_y >= 192) { spr_y -= 256; }
    const i32 local_y = static_cast<i32>(line) - spr_y;
    if (local_y < 0 || local_y >= box_h) {
        return;
    }

    i32 spr_x = attr1 & 0x01FFU;
    if (spr_x >= 256) { spr_x -= 512; }
    const i32 start_bx = std::max(0, -spr_x);
    const i32 end_bx = std::min(box_w, static_cast<i32>(kScreenWidth) - spr_x);
    if (start_bx >= end_bx) {
        return;
    }
    if (prof) { profiler_->add_sprite_visible(); }

    const u16 attr2 = read_le16(oam, oam_base + 4U);
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
        pa = static_cast<i16>(read_le16(oam, param_base + 6U));
        pb = static_cast<i16>(read_le16(oam, param_base + 14U));
        pc = static_cast<i16>(read_le16(oam, param_base + 22U));
        pd = static_cast<i16>(read_le16(oam, param_base + 30U));
    }

    const u32 dispcnt = regs_.dispcnt();
    const bool map_1d = (dispcnt & (1U << 4U)) != 0U;
    const u32 boundary_bytes = 32U << (regs_.is_engine_a() ? ((dispcnt >> 20U) & 0x03U) : 0U);
    const Address obj_base = obj_base_address();
    const u8* const pal_ptr = memory_.palette().data().data();
    const Address obj_pal_base = regs_.is_engine_a() ? 0x200U : 0x600U;
    const bool flip_x = !rotscale && ((attr1 & (1U << 12U)) != 0U);
    const bool flip_y = !rotscale && ((attr1 & (1U << 13U)) != 0U);
    const u32 tiles_w = static_cast<u32>(width >> 3);

    for (i32 bx = start_bx; bx < end_bx; ++bx) {
        const i32 dx = bx - (box_w / 2);
        const i32 dy = local_y - (box_h / 2);
        const i32 tex_x = rotscale ? (((pa * dx + pb * dy) >> 8) + (width / 2))
                                   : (flip_x ? ((width - 1) - bx) : bx);
        const i32 tex_y = rotscale ? (((pc * dx + pd * dy) >> 8) + (height / 2))
                                   : (flip_y ? ((height - 1) - local_y) : local_y);
        if (tex_x < 0 || tex_x >= width || tex_y < 0 || tex_y >= height) {
            continue;
        }

        const auto ux = static_cast<std::size_t>(spr_x + bx);
        if (obj_mode == 3U) {
            const Address bmp_addr =
                obj_base + (tile_base * 128U) + static_cast<Address>((tex_y * width + tex_x) * 2);
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
        u8 color_idx = 0U;

        if (!is_8bpp) {
            const Address base_bytes = map_1d ? (tile_base * boundary_bytes) : (tile_base * 32U);
            const u32 tile_step = map_1d ? (ty * tiles_w + tx) : (ty * 32U + tx);
            const u8 packed = read_vram8(obj_base + base_bytes + (tile_step * 32U) + (fy * 4U) + (fx >> 1U));
            color_idx = ((fx & 1U) == 0U) ? (packed & 0x0FU) : (packed >> 4U);
        } else {
            const Address base_bytes =
                map_1d ? (tile_base * boundary_bytes) : ((tile_base & ~1U) * 32U);
            const u32 tile_step = map_1d ? (ty * tiles_w + tx) : (ty * 16U + tx);
            color_idx = read_vram8(obj_base + base_bytes + (tile_step * 64U) + (fy * 8U) + fx);
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
                                : read_le16(pal_ptr, obj_pal_base + (pal_bank * 32U) + (color_idx * 2U));
            work_.obj_color[ux] = bgr555_to_argb8888(col);
            work_.obj_priority[ux] = priority;
            work_.obj_opaque[ux] = true;
            work_.obj_semi_transparent[ux] = (obj_mode == 1U);
            if (obj_mode == 1U) { work_.has_semi_obj = true; }
        }
    }
}

void Engine2D::compute_window_masks(u16 line) noexcept {
    const u32 dispcnt = regs_.dispcnt();
    const bool win0_en = (dispcnt & (1U << 13U)) != 0U;
    const bool win1_en = (dispcnt & (1U << 14U)) != 0U;
    const bool objwin_en = ((dispcnt & (1U << 15U)) != 0U) && ((dispcnt & (1U << 12U)) != 0U);
    if (!win0_en && !win1_en && !objwin_en) {
        work_.win_mask.fill(0x3FU);
        return;
    }

    const u8 w0_x1 = static_cast<u8>((regs_.win0_h() >> 8U) & 0xFFU);
    const u8 w0_x2 = static_cast<u8>(regs_.win0_h() & 0xFFU);
    const bool w0_y_ok = win0_en && in_win_range(line, static_cast<u8>(regs_.win0_v() >> 8U),
                                                 static_cast<u8>(regs_.win0_v() & 0xFFU));
    const u8 w1_x1 = static_cast<u8>((regs_.win1_h() >> 8U) & 0xFFU);
    const u8 w1_x2 = static_cast<u8>(regs_.win1_h() & 0xFFU);
    const bool w1_y_ok = win1_en && in_win_range(line, static_cast<u8>(regs_.win1_v() >> 8U),
                                                 static_cast<u8>(regs_.win1_v() & 0xFFU));

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

void Engine2D::composite_scanline(std::span<u32, kScreenWidth> out_line,
                                  u32 backdrop,
                                  u8 active_bgs) const noexcept {
    primitives::ScopedProfile profile_comp(profiler_, primitives::ProfileCategory::LayerComposition);
    if (profiler_ != nullptr && profiler_->is_enabled()) {
        profiler_->add_pixels_composited(kScreenWidth);
    }
    const u32 dispcnt = regs_.dispcnt();
    const u16 bldcnt = regs_.bldcnt();
    const CompositeConfig cfg{
        backdrop,
        active_bgs,
        (dispcnt & (1U << 12U)) != 0U,
        (dispcnt & 0xE000U) != 0U,
        {static_cast<u8>(regs_.bg_cnt(0U) & 0x03U), static_cast<u8>(regs_.bg_cnt(1U) & 0x03U),
         static_cast<u8>(regs_.bg_cnt(2U) & 0x03U), static_cast<u8>(regs_.bg_cnt(3U) & 0x03U)},
        bldcnt,
        (bldcnt >> 6U) & 0x03U,
        regs_.bldalpha() & 0x1FU,
        (regs_.bldalpha() >> 8U) & 0x1FU,
        regs_.bldy() & 0x1FU,
    };

    if (!cfg.use_win && cfg.effect == 0U && !work_.has_semi_obj) {
        composite_scanline_no_blend(work_, out_line, cfg);
        return;
    }
    for (std::size_t x = 0U; x < kScreenWidth; ++x) {
        out_line[x] = composite_pixel_fast(work_, x, cfg);
    }
}

}  // namespace elmasds::gpu
