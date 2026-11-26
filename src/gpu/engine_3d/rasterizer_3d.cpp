#include "gpu/engine_3d/engine_3d.hpp"

#include "gpu/engines_2d/color_effects.hpp"
#include "primitives/bit_operations.hpp"

#include <algorithm>

namespace elmasds::gpu {
namespace {

[[nodiscard]] inline u8 read_cached_tex8(const std::array<const u8*, 4U>& slots, Address addr) noexcept {
    const u8* const bank = slots[(addr >> 17U) & 3U];
    return (bank != nullptr) ? bank[addr & 0x1FFFFU] : 0U;
}

[[nodiscard]] inline u16 read_cached_tex16(const std::array<const u8*, 4U>& slots, Address addr) noexcept {
    const u8* const bank = slots[(addr >> 17U) & 3U];
    if (bank == nullptr) { return 0U; }
    const Address off = addr & 0x1FFFEU;
    return static_cast<u16>(bank[off]) | (static_cast<u16>(bank[off + 1U]) << 8U);
}

[[nodiscard]] inline u16 read_cached_pal16(const std::array<const u8*, 6U>& slots, Address addr) noexcept {
    const std::size_t slot = (addr >> 14U) & 7U;
    if (slot >= slots.size() || slots[slot] == nullptr) { return 0U; }
    const Address off = addr & 0x3FFEU;
    return static_cast<u16>(slots[slot][off]) | (static_cast<u16>(slots[slot][off + 1U]) << 8U);
}

[[nodiscard]] inline u32 blend_bgr555_weights(u16 c0, u16 c1, u32 w0, u32 w1) noexcept {
    const u32 r = (((c0 & 0x1FU) * w0) + ((c1 & 0x1FU) * w1)) >> 3U;
    const u32 g = ((((c0 >> 5U) & 0x1FU) * w0) + (((c1 >> 5U) & 0x1FU) * w1)) >> 3U;
    const u32 b = ((((c0 >> 10U) & 0x1FU) * w0) + (((c1 >> 10U) & 0x1FU) * w1)) >> 3U;
    return bgr555_to_argb8888(static_cast<u16>(r | (g << 5U) | (b << 10U)));
}

[[nodiscard]] inline i64 edge_fn(const Vertex3D& a, const Vertex3D& b, i32 px, i32 py) noexcept {
    return static_cast<i64>(px - a.screen_x) * (b.screen_y - a.screen_y) - static_cast<i64>(py - a.screen_y) * (b.screen_x - a.screen_x);
}
}  // namespace

void Engine3D::execute_vertex_command(u8 cmd, std::span<const u32> params) noexcept {
    if (params.empty() && cmd != 0x41U) { return; }
    switch (cmd) {
    case 0x20U: cur_r_ = static_cast<u8>(params[0U] & 0x1FU); cur_g_ = static_cast<u8>((params[0U] >> 5U) & 0x1FU); cur_b_ = static_cast<u8>((params[0U] >> 10U) & 0x1FU); break;
    case 0x22U: cur_s_ = static_cast<i16>(params[0U] & 0xFFFFU); cur_t_ = static_cast<i16>((params[0U] >> 16U) & 0xFFFFU); break;
    case 0x23U: emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU), static_cast<i16>((params[0U] >> 16U) & 0xFFFFU), static_cast<i16>(params[1U] & 0xFFFFU)); break;
    case 0x24U:
        emit_vertex(static_cast<i16>(bits::sign_extend(params[0U] & 0x3FFU, 10U) << 6),
                    static_cast<i16>(bits::sign_extend((params[0U] >> 10U) & 0x3FFU, 10U) << 6),
                    static_cast<i16>(bits::sign_extend((params[0U] >> 20U) & 0x3FFU, 10U) << 6)); break;
    case 0x25U: emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU), static_cast<i16>((params[0U] >> 16U) & 0xFFFFU), cur_vtx_z_); break;
    case 0x26U: emit_vertex(static_cast<i16>(params[0U] & 0xFFFFU), cur_vtx_y_, static_cast<i16>((params[0U] >> 16U) & 0xFFFFU)); break;
    case 0x27U: emit_vertex(cur_vtx_x_, static_cast<i16>(params[0U] & 0xFFFFU), static_cast<i16>((params[0U] >> 16U) & 0xFFFFU)); break;
    case 0x28U:
        emit_vertex(static_cast<i16>(cur_vtx_x_ + bits::sign_extend(params[0U] & 0x3FFU, 10U)),
                    static_cast<i16>(cur_vtx_y_ + bits::sign_extend((params[0U] >> 10U) & 0x3FFU, 10U)),
                    static_cast<i16>(cur_vtx_z_ + bits::sign_extend((params[0U] >> 20U) & 0x3FFU, 10U))); break;
    case 0x29U: poly_attr_ = params[0U]; break;
    case 0x2AU: teximage_param_ = params[0U]; break;
    case 0x2BU: pltt_base_ = static_cast<u16>(params[0U] & 0x1FFFU); break;
    case 0x30U:
        if (bits::test_bit(params[0U], 15U)) { cur_r_ = static_cast<u8>(params[0U] & 0x1FU); cur_g_ = static_cast<u8>((params[0U] >> 5U) & 0x1FU); cur_b_ = static_cast<u8>((params[0U] >> 10U) & 0x1FU); } break;
    case 0x40U: primitive_type_ = static_cast<PrimitiveType>(params[0U] & 3U); latched_poly_attr_ = poly_attr_; primitive_vtx_counter_ = 0U; break;
    default: break;
    }
}

void Engine3D::emit_vertex(i16 x, i16 y, i16 z) noexcept {
    cur_vtx_x_ = x; cur_vtx_y_ = y; cur_vtx_z_ = z;
    if (submitted_vertex_count_ >= kMaxVertices) { return; }

    const i64 vx = x, vy = y, vz = z;
    const auto& m = clip_mtx_.m;
    Vertex3D vtx{};
    vtx.clip_x = static_cast<i32>(((vx * m[0U]) + (vy * m[4U]) + (vz * m[8U]) + (static_cast<i64>(kFx12One) * m[12U])) >> 12);
    vtx.clip_y = static_cast<i32>(((vx * m[1U]) + (vy * m[5U]) + (vz * m[9U]) + (static_cast<i64>(kFx12One) * m[13U])) >> 12);
    vtx.clip_z = static_cast<i32>(((vx * m[2U]) + (vy * m[6U]) + (vz * m[10U]) + (static_cast<i64>(kFx12One) * m[14U])) >> 12);
    vtx.clip_w = static_cast<i32>(((vx * m[3U]) + (vy * m[7U]) + (vz * m[11U]) + (static_cast<i64>(kFx12One) * m[15U])) >> 12);

    const i32 w = (vtx.clip_w == 0) ? 1 : std::abs(vtx.clip_w);
    const i32 vp_w = static_cast<i32>(viewport_.x2) - static_cast<i32>(viewport_.x1) + 1;
    const i32 vp_h = static_cast<i32>(viewport_.y2) - static_cast<i32>(viewport_.y1) + 1;

    vtx.screen_x = static_cast<i32>(viewport_.x1) + static_cast<i32>(((static_cast<i64>(vtx.clip_x) + w) * vp_w) / (2LL * w));
    vtx.screen_y = static_cast<i32>(191 - viewport_.y2) + static_cast<i32>(((static_cast<i64>(w) - vtx.clip_y) * vp_h) / (2LL * w));
    vtx.depth_z = static_cast<i32>(((static_cast<i64>(vtx.clip_z) * 0x4000LL) / w) + 0x3FFFLL);
    vtx.r = cur_r_; vtx.g = cur_g_; vtx.b = cur_b_; vtx.tex_s = cur_s_; vtx.tex_t = cur_t_;

    vtx_ring_[primitive_vtx_counter_ & 3U] = vtx;
    ++primitive_vtx_counter_;
    ++submitted_vertex_count_;
    assemble_polygon();
}

void Engine3D::assemble_polygon() noexcept {
    if (submit_polygons_.size() >= kMaxPolygons) { return; }

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
        if ((primitive_vtx_counter_ % 3U) != 0U) { return; }
        poly.vertex_count = 3U;
        for (u32 i = 0U; i < 3U; ++i) { poly.vertices[i] = vtx_ring_[(primitive_vtx_counter_ - 3U + i) & 3U]; }
        break;
    case PrimitiveType::Quads:
        if ((primitive_vtx_counter_ % 4U) != 0U) { return; }
        poly.vertex_count = 4U;
        for (u32 i = 0U; i < 4U; ++i) { poly.vertices[i] = vtx_ring_[(primitive_vtx_counter_ - 4U + i) & 3U]; }
        break;
    case PrimitiveType::TriangleStrip:
        if (primitive_vtx_counter_ < 3U) { return; }
        poly.vertex_count = 3U;
        poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - ((primitive_vtx_counter_ & 1U) != 0U ? 3U : 2U)) & 3U];
        poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - ((primitive_vtx_counter_ & 1U) != 0U ? 2U : 3U)) & 3U];
        poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U];
        break;
    case PrimitiveType::QuadStrip:
        if (primitive_vtx_counter_ < 4U || (primitive_vtx_counter_ & 1U) != 0U) { return; }
        poly.vertex_count = 4U;
        poly.vertices[0U] = vtx_ring_[(primitive_vtx_counter_ - 4U) & 3U]; poly.vertices[1U] = vtx_ring_[(primitive_vtx_counter_ - 3U) & 3U];
        poly.vertices[2U] = vtx_ring_[(primitive_vtx_counter_ - 1U) & 3U]; poly.vertices[3U] = vtx_ring_[(primitive_vtx_counter_ - 2U) & 3U];
        break;
    }
    submit_polygons_.push_back(poly);
}

void Engine3D::on_vblank() noexcept {
    if (!swap_pending_) { return; }
    primitives::ScopedProfile profile_3d(profiler_, primitives::ProfileCategory::Gpu3d);
    swap_pending_ = false;
    render_polygons_.swap(submit_polygons_);
    submit_polygons_.clear();
    rendered_vertex_count_ = submitted_vertex_count_;
    submitted_vertex_count_ = 0U;
    if (profiler_ != nullptr && profiler_->is_enabled()) { profiler_->add_polygons_3d(render_polygons_.size()); }
    rasterize_frame();
}
void Engine3D::refresh_vram_texture_cache() noexcept {
    const auto& vram = memory_.vram();
    tex_slots_.fill(nullptr);
    for (u8 bank_idx = 0U; bank_idx < 4U; ++bank_idx) {
        const auto bank = static_cast<memory::VramBankId>(bank_idx);
        const u8 cnt = vram.read_vram_cnt(bank);
        if ((cnt & 0x87U) == 0x83U) { tex_slots_[(cnt >> 3U) & 3U] = vram.bank_data(bank).data(); }
    }
    tex_pal_slots_.fill(nullptr);
    if ((vram.read_vram_cnt(memory::VramBankId::E) & 0x87U) == 0x83U) {
        const u8* const e_data = vram.bank_data(memory::VramBankId::E).data();
        for (std::size_t s = 0U; s < 4U; ++s) { tex_pal_slots_[s] = e_data + (s * 0x4000U); }
    }
    for (const auto bank : {memory::VramBankId::F, memory::VramBankId::G}) {
        const u8 cnt = vram.read_vram_cnt(bank);
        if ((cnt & 0x87U) != 0x83U) { continue; }
        const u8 ofs = static_cast<u8>((cnt >> 3U) & 0x03U);
        const std::size_t slot = (ofs & 1U) + (((ofs >> 1U) & 1U) * 4U);
        if (slot < tex_pal_slots_.size()) { tex_pal_slots_[slot] = vram.bank_data(bank).data(); }
    }
}
void Engine3D::rasterize_frame() noexcept {
    refresh_vram_texture_cache();
    const u8 clear_alpha = static_cast<u8>((clear_color_ >> 16U) & 0x1FU);
    const u32 clear_argb = (clear_alpha > 0U) ? bgr555_to_argb8888(static_cast<u16>(clear_color_ & 0x7FFFU)) : 0U;
    std::fill(color_buffer_.begin(), color_buffer_.end(), clear_argb);
    std::fill(depth_buffer_.begin(), depth_buffer_.end(), static_cast<i32>(clear_depth_) << 9);
    for (const auto& poly : render_polygons_) { if (poly.alpha != 0U) { rasterize_polygon(poly); } }
}
struct Engine3D::TriStepInfo {
    const Polygon3D& poly; const PolygonTexInfo& tex;
    const Vertex3D& v0; const Vertex3D& v1; const Vertex3D& v2;
    i64 dw0_dx, dw1_dx, dw2_dx;
    float inv_area, dz_dx, ds_dx, dt_dx, dr_dx, dg_dx, db_dx;
};
struct Engine3D::ScanlineCursor { i32 x, max_x; std::size_t idx; i64 w0, w1, w2; };

void Engine3D::rasterize_polygon(const Polygon3D& poly) noexcept {
    PolygonTexInfo tex{};
    tex.format = (poly.teximage_param >> 26U) & 7U;
    tex.tex_enabled = bits::test_bit(disp3dcnt_, 0U) && tex.format != 0U;
    if (tex.tex_enabled) {
        tex.width = 8 << ((poly.teximage_param >> 20U) & 7U);
        tex.height = 8 << ((poly.teximage_param >> 23U) & 7U);
        tex.width_blocks = static_cast<u32>(tex.width) >> 2U;
        tex.wrap_s = bits::test_bit(poly.teximage_param, 16U);
        tex.wrap_t = bits::test_bit(poly.teximage_param, 17U);
        tex.color0_transparent = bits::test_bit(poly.teximage_param, 29U);
        tex.base = (poly.teximage_param & 0xFFFFU) << 3U;
        tex.slot1_base = 0x20000U + ((tex.base & 0x1FFFFU) >> 1U) + (((tex.base & 0x40000U) != 0U) ? 0x10000U : 0U);
        tex.pal_base4 = static_cast<Address>(poly.pltt_base) << 4U;
        tex.pal_base3 = static_cast<Address>(poly.pltt_base) << 3U;
        const u32 texels = static_cast<u32>(tex.width * tex.height);
        const u32 bytes = (tex.format == 2U) ? (texels >> 2U) : (tex.format == 3U) ? (texels >> 1U)
                        : (tex.format == 7U) ? (texels << 1U) : (tex.format == 5U) ? 0U : texels;
        const Address slot_off = tex.base & 0x1FFFFU;
        if (bytes > 0U && (slot_off + bytes) <= 0x20000U) {
            tex.single_tex_bank = tex_slots_[(tex.base >> 17U) & 3U];
            tex.single_tex_off = slot_off;
        }
    }
    rasterize_triangle(poly, tex, {&poly.vertices[0U], &poly.vertices[1U], &poly.vertices[2U]});
    if (poly.vertex_count == 4U) {
        rasterize_triangle(poly, tex, {&poly.vertices[0U], &poly.vertices[2U], &poly.vertices[3U]});
    }
}

void Engine3D::rasterize_span_textured(const TriStepInfo& s, ScanlineCursor& c) noexcept {
    const float f0 = static_cast<float>(c.w0) * s.inv_area, f1 = static_cast<float>(c.w1) * s.inv_area, f2 = static_cast<float>(c.w2) * s.inv_area;
    float z = f0 * s.v0.depth_z + f1 * s.v1.depth_z + f2 * s.v2.depth_z;
    float ts = f0 * s.v0.tex_s + f1 * s.v1.tex_s + f2 * s.v2.tex_s;
    float tt = f0 * s.v0.tex_t + f1 * s.v1.tex_t + f2 * s.v2.tex_t;
    for (; c.x <= c.max_x && c.w0 >= 0 && c.w1 >= 0 && c.w2 >= 0;
         ++c.x, ++c.idx, c.w0 += s.dw0_dx, c.w1 += s.dw1_dx, c.w2 += s.dw2_dx, z += s.dz_dx, ts += s.ds_dx, tt += s.dt_dx) {
        const i32 zi = static_cast<i32>(z);
        if (s.poly.depth_equal ? (std::abs(zi - depth_buffer_[c.idx]) > 0x200) : (zi >= depth_buffer_[c.idx])) { continue; }
        const u32 color = sample_texture(s.tex, static_cast<i32>(ts) >> 4, static_cast<i32>(tt) >> 4);
        if (color != 0U) { depth_buffer_[c.idx] = zi; color_buffer_[c.idx] = color; }
    }
}

void Engine3D::rasterize_span_solid(const TriStepInfo& s, ScanlineCursor& c) noexcept {
    const float f0 = static_cast<float>(c.w0) * s.inv_area, f1 = static_cast<float>(c.w1) * s.inv_area, f2 = static_cast<float>(c.w2) * s.inv_area;
    float z = f0 * s.v0.depth_z + f1 * s.v1.depth_z + f2 * s.v2.depth_z;
    float r = f0 * s.v0.r + f1 * s.v1.r + f2 * s.v2.r, g = f0 * s.v0.g + f1 * s.v1.g + f2 * s.v2.g, b = f0 * s.v0.b + f1 * s.v1.b + f2 * s.v2.b;
    for (; c.x <= c.max_x && c.w0 >= 0 && c.w1 >= 0 && c.w2 >= 0;
         ++c.x, ++c.idx, c.w0 += s.dw0_dx, c.w1 += s.dw1_dx, c.w2 += s.dw2_dx, z += s.dz_dx, r += s.dr_dx, g += s.dg_dx, b += s.db_dx) {
        const i32 zi = static_cast<i32>(z);
        if (s.poly.depth_equal ? (std::abs(zi - depth_buffer_[c.idx]) > 0x200) : (zi >= depth_buffer_[c.idx])) { continue; }
        const u32 ri = static_cast<u32>(std::clamp(static_cast<i32>(r), 0, 31));
        const u32 gi = static_cast<u32>(std::clamp(static_cast<i32>(g), 0, 31));
        const u32 bi = static_cast<u32>(std::clamp(static_cast<i32>(b), 0, 31));
        depth_buffer_[c.idx] = zi;
        color_buffer_[c.idx] = bgr555_to_argb8888(static_cast<u16>(ri | (gi << 5U) | (bi << 10U)));
    }
}

void Engine3D::rasterize_triangle(const Polygon3D& poly, const PolygonTexInfo& tex,
                                  const std::array<const Vertex3D*, 3U>& tri) noexcept {
    const Vertex3D& v0 = *tri[0U]; const Vertex3D& v1 = *tri[1U]; const Vertex3D& v2 = *tri[2U];
    i64 area = edge_fn(v0, v1, v2.screen_x, v2.screen_y);
    if (area == 0) { return; }
    const bool is_front = area > 0;
    if ((is_front && !poly.front_surface) || (!is_front && !poly.back_surface)) { return; }
    const i64 sign = is_front ? 1 : -1;
    area *= sign;

    const i32 min_x = std::clamp(std::min({v0.screen_x, v1.screen_x, v2.screen_x}), 0, 255);
    const i32 max_x = std::clamp(std::max({v0.screen_x, v1.screen_x, v2.screen_x}), 0, 255);
    const i32 min_y = std::clamp(std::min({v0.screen_y, v1.screen_y, v2.screen_y}), 0, 191);
    const i32 max_y = std::clamp(std::max({v0.screen_y, v1.screen_y, v2.screen_y}), 0, 191);
    const float inv_area = 1.0f / static_cast<float>(area);

    const i64 dw0_dx = static_cast<i64>(v2.screen_y - v1.screen_y) * sign, dw0_dy = static_cast<i64>(v1.screen_x - v2.screen_x) * sign;
    const i64 dw1_dx = static_cast<i64>(v0.screen_y - v2.screen_y) * sign, dw1_dy = static_cast<i64>(v2.screen_x - v0.screen_x) * sign;
    const i64 dw2_dx = static_cast<i64>(v1.screen_y - v0.screen_y) * sign, dw2_dy = static_cast<i64>(v0.screen_x - v1.screen_x) * sign;
    const float df0 = static_cast<float>(dw0_dx) * inv_area, df1 = static_cast<float>(dw1_dx) * inv_area, df2 = static_cast<float>(dw2_dx) * inv_area;
    const TriStepInfo step{
        poly, tex, v0, v1, v2, dw0_dx, dw1_dx, dw2_dx, inv_area,
        df0 * v0.depth_z + df1 * v1.depth_z + df2 * v2.depth_z,
        df0 * v0.tex_s + df1 * v1.tex_s + df2 * v2.tex_s, df0 * v0.tex_t + df1 * v1.tex_t + df2 * v2.tex_t,
        df0 * v0.r + df1 * v1.r + df2 * v2.r, df0 * v0.g + df1 * v1.g + df2 * v2.g, df0 * v0.b + df1 * v1.b + df2 * v2.b};

    i64 w0_row = edge_fn(v1, v2, min_x, min_y) * sign;
    i64 w1_row = edge_fn(v2, v0, min_x, min_y) * sign;
    i64 w2_row = edge_fn(v0, v1, min_x, min_y) * sign;
    for (i32 y = min_y; y <= max_y; ++y, w0_row += dw0_dy, w1_row += dw1_dy, w2_row += dw2_dy) {
        ScanlineCursor cur{min_x, max_x, 0U, w0_row, w1_row, w2_row};
        while (cur.x <= max_x && (cur.w0 < 0 || cur.w1 < 0 || cur.w2 < 0)) {
            ++cur.x; cur.w0 += dw0_dx; cur.w1 += dw1_dx; cur.w2 += dw2_dx;
        }
        if (cur.x > max_x) { continue; }
        cur.idx = static_cast<std::size_t>(y) * kScreenWidth + static_cast<std::size_t>(cur.x);
        if (tex.tex_enabled) { rasterize_span_textured(step, cur); } else { rasterize_span_solid(step, cur); }
    }
}

u32 Engine3D::sample_compressed_4x4(const PolygonTexInfo& tex, i32 u, i32 v) const noexcept {
    const u32 block_idx = (static_cast<u32>(v >> 2) * tex.width_blocks) + static_cast<u32>(u >> 2);
    const u8 row_byte = read_cached_tex8(tex_slots_, tex.base + (block_idx * 4U) + static_cast<u32>(v & 3));
    const u32 code = (row_byte >> ((u & 3) * 2)) & 3U;
    const u16 pal_info = read_cached_tex16(tex_slots_, tex.slot1_base + (block_idx * 2U));
    const Address pal_addr = tex.pal_base4 + ((pal_info & 0x3FFFU) << 2U);
    const u32 mode = (pal_info >> 14U) & 3U;

    if (mode == 0U) { return (code == 3U) ? 0U : bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, pal_addr + code * 2U)); }
    if (mode == 2U) { return bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, pal_addr + code * 2U)); }
    if (mode == 1U && code == 3U) { return 0U; }
    const u16 c0 = read_cached_pal16(tex_pal_slots_, pal_addr);
    if (code == 0U) { return bgr555_to_argb8888(c0); }
    const u16 c1 = read_cached_pal16(tex_pal_slots_, pal_addr + 2U);
    if (code == 1U) { return bgr555_to_argb8888(c1); }
    if (mode == 1U) { return blend_bgr555_weights(c0, c1, 4U, 4U); }
    return (code == 2U) ? blend_bgr555_weights(c0, c1, 5U, 3U) : blend_bgr555_weights(c0, c1, 3U, 5U);
}

u32 Engine3D::sample_texture(const PolygonTexInfo& tex, i32 s, i32 t) const noexcept {
    const i32 u = tex.wrap_s ? (s & (tex.width - 1)) : std::clamp(s, 0, tex.width - 1);
    const i32 v = tex.wrap_t ? (t & (tex.height - 1)) : std::clamp(t, 0, tex.height - 1);
    if (tex.format == 5U) { return sample_compressed_4x4(tex, u, v); }

    const u32 texel_idx = static_cast<u32>(v * tex.width + u);
    const u8* const flat = tex.single_tex_bank ? (tex.single_tex_bank + tex.single_tex_off) : nullptr;
    auto tex8 = [&](Address off) noexcept -> u8 { return flat ? flat[off] : read_cached_tex8(tex_slots_, tex.base + off); };
    switch (tex.format) {
    case 1U:
    case 6U: {
        const u8 px = tex8(texel_idx);
        const u8 a = (tex.format == 1U) ? (px >> 5U) : (px >> 3U);
        if (a == 0U) { return 0U; }
        const u32 alpha_byte = static_cast<u32>(a) * ((tex.format == 1U) ? 36U : 8U);
        const u8 idx = px & ((tex.format == 1U) ? 0x1FU : 7U);
        return (alpha_byte << 24U) | (bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, tex.pal_base4 + (idx * 2U))) & 0x00FFFFFFU);
    }
    case 2U: {
        const u8 idx = (tex8(texel_idx >> 2U) >> ((texel_idx & 3U) * 2U)) & 3U;
        return (idx == 0U && tex.color0_transparent) ? 0U : bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, tex.pal_base3 + idx * 2U));
    }
    case 3U: {
        const u8 idx = (tex8(texel_idx >> 1U) >> ((texel_idx & 1U) * 4U)) & 15U;
        return (idx == 0U && tex.color0_transparent) ? 0U : bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, tex.pal_base4 + idx * 2U));
    }
    case 4U: {
        const u8 idx = tex8(texel_idx);
        return (idx == 0U && tex.color0_transparent) ? 0U : bgr555_to_argb8888(read_cached_pal16(tex_pal_slots_, tex.pal_base4 + idx * 2U));
    }
    case 7U: {
        const Address off = texel_idx * 2U;
        const u16 direct = flat ? (static_cast<u16>(flat[off]) | (static_cast<u16>(flat[off + 1U]) << 8U))
                                : read_cached_tex16(tex_slots_, tex.base + off);
        return bits::test_bit(direct, 15U) ? bgr555_to_argb8888(direct) : 0U;
    }
    default: return 0xFFFFFFFFU;
    }
}

}  // namespace elmasds::gpu
