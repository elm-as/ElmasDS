#include "gpu/engine_3d/engine_3d.hpp"

#include "gpu/engines_2d/color_effects.hpp"
#include "primitives/bit_operations.hpp"

#include <algorithm>

namespace elmasds::gpu {

void Engine3D::execute_vertex_command(u8 cmd, std::span<const u32> params) noexcept {
    if (params.empty() && cmd != 0x41U) {
        return;
    }
    switch (cmd) {
    case 0x20U:
        cur_r_ = static_cast<u8>(params[0U] & 0x1FU);
        cur_g_ = static_cast<u8>((params[0U] >> 5U) & 0x1FU);
        cur_b_ = static_cast<u8>((params[0U] >> 10U) & 0x1FU);
        break;
    case 0x22U:
        cur_s_ = static_cast<i16>(params[0U] & 0xFFFFU);
        cur_t_ = static_cast<i16>((params[0U] >> 16U) & 0xFFFFU);
        break;
    case 0x23U:
        emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU),
                    static_cast<i16>((params[0U] >> 16U) & 0xFFFFU),
                    static_cast<i16>(params[1U] & 0xFFFFU));
        break;
    case 0x24U: {
        const i16 x = static_cast<i16>(bits::sign_extend(params[0U] & 0x3FFU, 10U) << 6);
        const i16 y = static_cast<i16>(bits::sign_extend((params[0U] >> 10U) & 0x3FFU, 10U) << 6);
        const i16 z = static_cast<i16>(bits::sign_extend((params[0U] >> 20U) & 0x3FFU, 10U) << 6);
        emit_vertex(x, y, z);
        break;
    }
    case 0x25U:
        emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU),
                    static_cast<i16>((params[0U] >> 16U) & 0xFFFFU), cur_vtx_z_);
        break;
    case 0x26U:
        emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU), cur_vtx_y_,
                    static_cast<i16>((params[0U] >> 16U) & 0xFFFFU));
        break;
    case 0x27U:
        emit_vertex(cur_vtx_x_, static_cast<i16>(params[0U] & 0xFFFFU),
                    static_cast<i16>((params[0U] >> 16U) & 0xFFFFU));
        break;
    case 0x28U: {
        const i16 dx = static_cast<i16>(bits::sign_extend(params[0U] & 0x3FFU, 10U));
        const i16 dy = static_cast<i16>(bits::sign_extend((params[0U] >> 10U) & 0x3FFU, 10U));
        const i16 dz = static_cast<i16>(bits::sign_extend((params[0U] >> 20U) & 0x3FFU, 10U));
        emit_vertex(static_cast<i16>(cur_vtx_x_ + dx), static_cast<i16>(cur_vtx_y_ + dy),
                    static_cast<i16>(cur_vtx_z_ + dz));
        break;
    }
    case 0x29U:
        poly_attr_ = params[0U];
        break;
    case 0x2AU:
        teximage_param_ = params[0U];
        break;
    case 0x2BU:
        pltt_base_ = static_cast<u16>(params[0U] & 0x1FFFU);
        break;
    case 0x30U:
        if (bits::test_bit(params[0U], 15U)) {
            cur_r_ = static_cast<u8>(params[0U] & 0x1FU);
            cur_g_ = static_cast<u8>((params[0U] >> 5U) & 0x1FU);
            cur_b_ = static_cast<u8>((params[0U] >> 10U) & 0x1FU);
        }
        break;
    case 0x40U:
        primitive_type_ = static_cast<PrimitiveType>(params[0U] & 3U);
        latched_poly_attr_ = poly_attr_;
        primitive_vtx_counter_ = 0U;
        break;
    case 0x41U:
        break;
    default:
        break;
    }
}

void Engine3D::emit_vertex(i16 x, i16 y, i16 z) noexcept {
    cur_vtx_x_ = x;
    cur_vtx_y_ = y;
    cur_vtx_z_ = z;

    if (submitted_vertex_count_ >= kMaxVertices) {
        return;
    }

    const i64 vx = x;
    const i64 vy = y;
    const i64 vz = z;
    const auto& m = clip_mtx_.m;

    Vertex3D vtx{};
    vtx.clip_x = static_cast<i32>(
        ((vx * m[0U]) + (vy * m[4U]) + (vz * m[8U]) + (static_cast<i64>(kFx12One) * m[12U])) >> 12);
    vtx.clip_y = static_cast<i32>(
        ((vx * m[1U]) + (vy * m[5U]) + (vz * m[9U]) + (static_cast<i64>(kFx12One) * m[13U])) >> 12);
    vtx.clip_z = static_cast<i32>(
        ((vx * m[2U]) + (vy * m[6U]) + (vz * m[10U]) + (static_cast<i64>(kFx12One) * m[14U])) >> 12);
    vtx.clip_w = static_cast<i32>(
        ((vx * m[3U]) + (vy * m[7U]) + (vz * m[11U]) + (static_cast<i64>(kFx12One) * m[15U])) >> 12);

    const i32 w = (vtx.clip_w == 0) ? 1 : std::abs(vtx.clip_w);
    const i32 vp_w = static_cast<i32>(viewport_.x2) - static_cast<i32>(viewport_.x1) + 1;
    const i32 vp_h = static_cast<i32>(viewport_.y2) - static_cast<i32>(viewport_.y1) + 1;

    vtx.screen_x = static_cast<i32>(viewport_.x1) +
                   static_cast<i32>(((static_cast<i64>(vtx.clip_x) + w) * vp_w) / (2LL * w));
    vtx.screen_y = static_cast<i32>(191 - viewport_.y2) +
                   static_cast<i32>(((static_cast<i64>(w) - vtx.clip_y) * vp_h) / (2LL * w));
    vtx.depth_z = static_cast<i32>(((static_cast<i64>(vtx.clip_z) * 0x4000LL) / w) + 0x3FFFLL);
    vtx.r = cur_r_;
    vtx.g = cur_g_;
    vtx.b = cur_b_;
    vtx.tex_s = cur_s_;
    vtx.tex_t = cur_t_;

    vtx_ring_[primitive_vtx_counter_ & 3U] = vtx;
    ++primitive_vtx_counter_;
    ++submitted_vertex_count_;
    assemble_polygon();
}

void Engine3D::assemble_polygon() noexcept {
    if (submit_polygons_.size() >= kMaxPolygons) {
        return;
    }

    Polygon3D poly{};
    poly.alpha = static_cast<u8>((latched_poly_attr_ >> 16U) & 0x1FU);
    poly.poly_id = static_cast<u8>((latched_poly_attr_ >> 24U) & 0x3FU);
    poly.back_surface = bits::test_bit(latched_poly_attr_, 6U);
    poly.front_surface = bits::test_bit(latched_poly_attr_, 7U);
    poly.depth_equal = bits::test_bit(latched_poly_attr_, 14U);
    poly.teximage_param = teximage_param_;
    poly.pltt_base = pltt_base_;

    switch (primitive_type_) {
    case PrimitiveType::Triangles:
        if ((primitive_vtx_counter_ % 3U) != 0U) {
            return;
        }
        poly.vertex_count = 3U;
        poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
        poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
        poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        break;
    case PrimitiveType::Quads:
        if ((primitive_vtx_counter_ % 4U) != 0U) {
            return;
        }
        poly.vertex_count = 4U;
        poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 4U) & 3U];
        poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
        poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
        poly.vertices[3U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        break;
    case PrimitiveType::TriangleStrip:
        if (primitive_vtx_counter_ < 3U) {
            return;
        }
        poly.vertex_count = 3U;
        if ((primitive_vtx_counter_ & 1U) != 0U) {
            poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
            poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
            poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        } else {
            poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
            poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
            poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        }
        break;
    case PrimitiveType::QuadStrip:
        if (primitive_vtx_counter_ < 4U || (primitive_vtx_counter_ & 1U) != 0U) {
            return;
        }
        poly.vertex_count = 4U;
        poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 4U) & 3U];
        poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
        poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        poly.vertices[3U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
        break;
    }

    submit_polygons_.push_back(poly);
}

void Engine3D::on_vblank() noexcept {
    if (!swap_pending_) {
        return;
    }
    swap_pending_ = false;
    render_polygons_ = std::move(submit_polygons_);
    submit_polygons_.clear();
    rendered_vertex_count_ = submitted_vertex_count_;
    submitted_vertex_count_ = 0U;
    rasterize_frame();
}

void Engine3D::rasterize_frame() noexcept {
    const u8 clear_alpha = static_cast<u8>((clear_color_ >> 16U) & 0x1FU);
    const u32 clear_argb =
        (clear_alpha > 0U) ? bgr555_to_argb8888(static_cast<u16>(clear_color_ & 0x7FFFU)) : 0U;
    std::fill(color_buffer_.begin(), color_buffer_.end(), clear_argb);
    std::fill(depth_buffer_.begin(), depth_buffer_.end(), static_cast<i32>(clear_depth_) << 9);

    for (const auto& poly : render_polygons_) {
        if (poly.alpha == 0U) {
            continue;
        }
        rasterize_polygon(poly);
    }
}

void Engine3D::rasterize_polygon(const Polygon3D& poly) noexcept {
    auto rasterize_triangle = [&](const Vertex3D& v0, const Vertex3D& v1, const Vertex3D& v2) {
        const i32 min_x = std::clamp(std::min({v0.screen_x, v1.screen_x, v2.screen_x}), 0, 255);
        const i32 max_x = std::clamp(std::max({v0.screen_x, v1.screen_x, v2.screen_x}), 0, 255);
        const i32 min_y = std::clamp(std::min({v0.screen_y, v1.screen_y, v2.screen_y}), 0, 191);
        const i32 max_y = std::clamp(std::max({v0.screen_y, v1.screen_y, v2.screen_y}), 0, 191);

        auto edge = [](i32 ax, i32 ay, i32 bx, i32 by, i32 px, i32 py) noexcept -> i64 {
            return static_cast<i64>(px - ax) * (by - ay) - static_cast<i64>(py - ay) * (bx - ax);
        };

        i64 area = edge(v0.screen_x, v0.screen_y, v1.screen_x, v1.screen_y, v2.screen_x, v2.screen_y);
        if (area == 0) {
            return;
        }
        const bool is_front = area > 0;
        if ((is_front && !poly.front_surface) || (!is_front && !poly.back_surface)) {
            return;
        }
        const i64 sign = (area > 0) ? 1 : -1;
        area *= sign;

        for (i32 y = min_y; y <= max_y; ++y) {
            for (i32 x = min_x; x <= max_x; ++x) {
                const i64 w0 = edge(v1.screen_x, v1.screen_y, v2.screen_x, v2.screen_y, x, y) * sign;
                const i64 w1 = edge(v2.screen_x, v2.screen_y, v0.screen_x, v0.screen_y, x, y) * sign;
                const i64 w2 = edge(v0.screen_x, v0.screen_y, v1.screen_x, v1.screen_y, x, y) * sign;
                if (w0 < 0 || w1 < 0 || w2 < 0) {
                    continue;
                }

                const i32 z = static_cast<i32>(
                    (w0 * v0.depth_z + w1 * v1.depth_z + w2 * v2.depth_z) / area);
                const std::size_t idx =
                    static_cast<std::size_t>(y) * kScreenWidth + static_cast<std::size_t>(x);
                if (poly.depth_equal ? (std::abs(z - depth_buffer_[idx]) > 0x200)
                                     : (z >= depth_buffer_[idx])) {
                    continue;
                }

                const u8 r = static_cast<u8>((w0 * v0.r + w1 * v1.r + w2 * v2.r) / area);
                const u8 g = static_cast<u8>((w0 * v0.g + w1 * v1.g + w2 * v2.g) / area);
                const u8 b = static_cast<u8>((w0 * v0.b + w1 * v1.b + w2 * v2.b) / area);
                const u16 vtx_bgr = static_cast<u16>(r | (g << 5U) | (b << 10U));
                u32 color = bgr555_to_argb8888(vtx_bgr);

                if (bits::test_bit(disp3dcnt_, 0U) && ((poly.teximage_param >> 26U) & 7U) != 0U) {
                    const i32 s = static_cast<i32>(
                        (w0 * v0.tex_s + w1 * v1.tex_s + w2 * v2.tex_s) / area) >> 4;
                    const i32 t = static_cast<i32>(
                        (w0 * v0.tex_t + w1 * v1.tex_t + w2 * v2.tex_t) / area) >> 4;
                    const u32 tex_color = sample_texture(poly, s, t);
                    if (tex_color == 0U) {
                        continue;
                    }
                    color = tex_color;
                }

                depth_buffer_[idx] = z;
                color_buffer_[idx] = color;
            }
        }
    };

    rasterize_triangle(poly.vertices[0U], poly.vertices[1U], poly.vertices[2U]);
    if (poly.vertex_count == 4U) {
        rasterize_triangle(poly.vertices[0U], poly.vertices[2U], poly.vertices[3U]);
    }
}

u32 Engine3D::sample_texture(const Polygon3D& poly, i32 s, i32 t) const noexcept {
    const u32 format = (poly.teximage_param >> 26U) & 7U;
    const i32 width = 8 << ((poly.teximage_param >> 20U) & 7U);
    const i32 height = 8 << ((poly.teximage_param >> 23U) & 7U);
    const i32 u = bits::test_bit(poly.teximage_param, 16U) ? (s & (width - 1))
                                                           : std::clamp(s, 0, width - 1);
    const i32 v = bits::test_bit(poly.teximage_param, 17U) ? (t & (height - 1))
                                                           : std::clamp(t, 0, height - 1);
    const Address base = (poly.teximage_param & 0xFFFFU) << 3U;
    const u32 texel_index = static_cast<u32>(v * width + u);
    const bool color0_transparent = bits::test_bit(poly.teximage_param, 29U);

    auto read_tex8 = [&](Address addr) noexcept -> u8 {
        for (u8 bank_idx = 0U; bank_idx < 4U; ++bank_idx) {
            const auto bank = static_cast<memory::VramBankId>(bank_idx);
            const u8 cnt = memory_.vram().read_vram_cnt(bank);
            if ((cnt & 0x87U) != 0x83U) {
                continue;
            }
            const Address slot_base = static_cast<Address>((cnt >> 3U) & 3U) * 0x20000U;
            if (addr >= slot_base && addr < (slot_base + 0x20000U)) {
                return memory_.vram().bank_data(bank)[addr - slot_base];
            }
        }
        return 0U;
    };

    auto read_tex_pal16 = [&](Address addr) noexcept -> u16 {
        const auto bank_e = memory_.vram().bank_data(memory::VramBankId::E);
        if ((memory_.vram().read_vram_cnt(memory::VramBankId::E) & 0x87U) == 0x83U &&
            (addr + 1U) < bank_e.size()) {
            return static_cast<u16>(bank_e[addr]) |
                   (static_cast<u16>(bank_e[addr + 1U]) << 8U);
        }
        return 0U;
    };

    if (format == 7U) {
        const Address off = base + texel_index * 2U;
        const u16 direct =
            static_cast<u16>(read_tex8(off)) | (static_cast<u16>(read_tex8(off + 1U)) << 8U);
        return bits::test_bit(direct, 15U) ? bgr555_to_argb8888(direct) : 0U;
    }
    if (format == 4U) {
        const u8 idx = read_tex8(base + texel_index);
        if (idx == 0U && color0_transparent) {
            return 0U;
        }
        const Address pal_addr = (static_cast<Address>(poly.pltt_base) << 4U) + idx * 2U;
        return bgr555_to_argb8888(read_tex_pal16(pal_addr));
    }
    return 0xFFFFFFFFU;
}

}  // namespace elmasds::gpu
