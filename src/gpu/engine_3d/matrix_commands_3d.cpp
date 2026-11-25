#include "gpu/engine_3d/engine_3d.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::gpu {

namespace {

[[nodiscard]] Matrix4x4 load_4x4_from_params(std::span<const u32> p) noexcept {
    Matrix4x4 m{};
    for (std::size_t i = 0U; i < 16U; ++i) {
        m.m[i] = static_cast<i32>(p[i]);
    }
    return m;
}

[[nodiscard]] Matrix4x4 load_4x3_from_params(std::span<const u32> p) noexcept {
    Matrix4x4 m = Matrix4x4::identity();
    for (std::size_t row = 0U; row < 4U; ++row) {
        m.m[row * 4U + 0U] = static_cast<i32>(p[row * 3U + 0U]);
        m.m[row * 4U + 1U] = static_cast<i32>(p[row * 3U + 1U]);
        m.m[row * 4U + 2U] = static_cast<i32>(p[row * 3U + 2U]);
    }
    return m;
}

[[nodiscard]] Matrix4x4 load_3x3_from_params(std::span<const u32> p) noexcept {
    Matrix4x4 m = Matrix4x4::identity();
    for (std::size_t row = 0U; row < 3U; ++row) {
        m.m[row * 4U + 0U] = static_cast<i32>(p[row * 3U + 0U]);
        m.m[row * 4U + 1U] = static_cast<i32>(p[row * 3U + 1U]);
        m.m[row * 4U + 2U] = static_cast<i32>(p[row * 3U + 2U]);
    }
    return m;
}

}  // namespace

void Engine3D::execute_matrix_command(u8 cmd, std::span<const u32> params) noexcept {
    auto apply_to_current = [&](const Matrix4x4& rhs, bool replace) {
        switch (mtx_mode_ & 3U) {
        case 0U:
            proj_mtx_ = replace ? rhs : multiply_matrix(rhs, proj_mtx_);
            break;
        case 1U:
            pos_mtx_ = replace ? rhs : multiply_matrix(rhs, pos_mtx_);
            break;
        case 2U:
            pos_mtx_ = replace ? rhs : multiply_matrix(rhs, pos_mtx_);
            vec_mtx_ = replace ? rhs : multiply_matrix(rhs, vec_mtx_);
            break;
        case 3U:
            tex_mtx_ = replace ? rhs : multiply_matrix(rhs, tex_mtx_);
            break;
        }
        update_clip_matrix();
    };

    switch (cmd) {
    case 0x10U:
        mtx_mode_ = static_cast<u8>(params[0U] & 3U);
        break;
    case 0x11U:
        if ((mtx_mode_ & 3U) == 0U) {
            proj_stack_ = proj_mtx_;
            proj_sp_ = 1U;
        } else if ((mtx_mode_ & 3U) == 3U) {
            tex_stack_ = tex_mtx_;
        } else {
            pos_stack_[pos_sp_ & 31U] = pos_mtx_;
            vec_stack_[pos_sp_ & 31U] = vec_mtx_;
            pos_sp_ = static_cast<u8>((pos_sp_ + 1U) & 31U);
        }
        break;
    case 0x12U:
        if ((mtx_mode_ & 3U) == 0U) {
            proj_mtx_ = proj_stack_;
            proj_sp_ = 0U;
        } else if ((mtx_mode_ & 3U) == 3U) {
            tex_mtx_ = tex_stack_;
        } else {
            const i32 offset = bits::sign_extend(params[0U] & 0x3FU, 6U);
            pos_sp_ = static_cast<u8>((static_cast<i32>(pos_sp_) - offset) & 31U);
            pos_mtx_ = pos_stack_[pos_sp_];
            vec_mtx_ = vec_stack_[pos_sp_];
        }
        update_clip_matrix();
        break;
    case 0x13U:
        if ((mtx_mode_ & 3U) == 0U) {
            proj_stack_ = proj_mtx_;
        } else if ((mtx_mode_ & 3U) == 3U) {
            tex_stack_ = tex_mtx_;
        } else {
            const u8 idx = static_cast<u8>(params[0U] & 31U);
            pos_stack_[idx] = pos_mtx_;
            vec_stack_[idx] = vec_mtx_;
        }
        break;
    case 0x14U:
        if ((mtx_mode_ & 3U) == 0U) {
            proj_mtx_ = proj_stack_;
        } else if ((mtx_mode_ & 3U) == 3U) {
            tex_mtx_ = tex_stack_;
        } else {
            const u8 idx = static_cast<u8>(params[0U] & 31U);
            pos_mtx_ = pos_stack_[idx];
            vec_mtx_ = vec_stack_[idx];
        }
        update_clip_matrix();
        break;
    case 0x15U:
        apply_to_current(Matrix4x4::identity(), true);
        break;
    case 0x16U:
        apply_to_current(load_4x4_from_params(params), true);
        break;
    case 0x17U:
        apply_to_current(load_4x3_from_params(params), true);
        break;
    case 0x18U:
        apply_to_current(load_4x4_from_params(params), false);
        break;
    case 0x19U:
        apply_to_current(load_4x3_from_params(params), false);
        break;
    case 0x1AU:
        apply_to_current(load_3x3_from_params(params), false);
        break;
    case 0x1BU: {
        Matrix4x4 s = Matrix4x4::identity();
        s.m[0U] = static_cast<i32>(params[0U]);
        s.m[5U] = static_cast<i32>(params[1U]);
        s.m[10U] = static_cast<i32>(params[2U]);
        if ((mtx_mode_ & 3U) == 2U) {
            pos_mtx_ = multiply_matrix(s, pos_mtx_);
            update_clip_matrix();
        } else {
            apply_to_current(s, false);
        }
        break;
    }
    case 0x1CU: {
        Matrix4x4 t = Matrix4x4::identity();
        t.m[12U] = static_cast<i32>(params[0U]);
        t.m[13U] = static_cast<i32>(params[1U]);
        t.m[14U] = static_cast<i32>(params[2U]);
        apply_to_current(t, false);
        break;
    }
    default:
        break;
    }
}

}  // namespace elmasds::gpu
