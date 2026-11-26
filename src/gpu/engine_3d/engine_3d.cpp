#include "gpu/engine_3d/engine_3d.hpp"

#include "gpu/engines_2d/color_effects.hpp"
#include "primitives/bit_operations.hpp"

#include <algorithm>

namespace elmasds::gpu {

Engine3D::Engine3D(const memory::MemorySystem& memory,
                   hardware::interrupts::InterruptController& arm9_irq,
                   hardware::dma::DmaController& arm9_dma) noexcept
    : memory_(memory),
      arm9_irq_(arm9_irq),
      arm9_dma_(arm9_dma),
      color_buffer_(kScreenWidth * kScreenHeight, 0U),
      depth_buffer_(kScreenWidth * kScreenHeight, 0x7FFFFFFF) {
    submit_polygons_.reserve(kMaxPolygons);
    render_polygons_.reserve(kMaxPolygons);
    reset();
}

void Engine3D::reset() noexcept {
    disp3dcnt_ = 0U;
    gxstat_latch_ = 0x04000000U;
    clear_color_ = 0x001F0000U;
    clear_depth_ = 0x7FFFU;
    mtx_mode_ = 0U;
    proj_mtx_ = Matrix4x4::identity();
    proj_stack_ = Matrix4x4::identity();
    proj_sp_ = 0U;
    pos_mtx_ = Matrix4x4::identity();
    vec_mtx_ = Matrix4x4::identity();
    pos_stack_.fill(Matrix4x4::identity());
    vec_stack_.fill(Matrix4x4::identity());
    pos_sp_ = 0U;
    tex_mtx_ = Matrix4x4::identity();
    tex_stack_ = Matrix4x4::identity();
    clip_mtx_ = Matrix4x4::identity();

    clear_cmds();
    active_cmd_ = 0U;
    active_needed_params_ = 0U;
    active_param_index_ = 0U;
    primitive_type_ = PrimitiveType::Triangles;
    poly_attr_ = 0x001F00C0U;
    latched_poly_attr_ = 0x001F00C0U;
    teximage_param_ = 0U;
    pltt_base_ = 0U;
    viewport_ = Viewport3D{};
    cur_vtx_x_ = 0;
    cur_vtx_y_ = 0;
    cur_vtx_z_ = 0;
    cur_r_ = 31U;
    cur_g_ = 31U;
    cur_b_ = 31U;
    cur_s_ = 0;
    cur_t_ = 0;
    primitive_vtx_counter_ = 0U;
    swap_pending_ = false;
    swap_flags_ = 0U;
    submit_polygons_.clear();
    render_polygons_.clear();
    submitted_vertex_count_ = 0U;
    rendered_vertex_count_ = 0U;
    std::fill(color_buffer_.begin(), color_buffer_.end(), 0U);
    std::fill(depth_buffer_.begin(), depth_buffer_.end(), 0x7FFFFFFF);
}

u8 Engine3D::param_count_for_command(u8 cmd) noexcept {
    switch (cmd) {
    case 0x10U: case 0x12U: case 0x13U: case 0x14U: case 0x20U: case 0x21U:
    case 0x22U: case 0x24U: case 0x25U: case 0x26U: case 0x27U: case 0x28U:
    case 0x29U: case 0x2AU: case 0x2BU: case 0x30U: case 0x31U: case 0x32U:
    case 0x33U: case 0x40U: case 0x50U: case 0x60U: case 0x71U: case 0x72U:
        return 1U;
    case 0x23U:
        return 2U;
    case 0x1BU: case 0x1CU: case 0x70U:
        return 3U;
    case 0x1AU: case 0x1EU:
        return 9U;
    case 0x17U: case 0x19U:
        return 12U;
    case 0x16U: case 0x18U: case 0x34U:
        return 16U;
    default:
        return 0U;
    }
}

u32 Engine3D::gxstat() const noexcept {
    u32 stat = (gxstat_latch_ & 0xC0008000U) | (1U << 1U);
    stat |= (static_cast<u32>(pos_sp_ & 0x1FU) << 8U);
    stat |= (static_cast<u32>(proj_sp_ & 1U) << 13U);
    stat |= (1U << 25U) | (1U << 26U);
    return stat;
}

u8 Engine3D::read8(Address address) noexcept {
    const u32 word = read32(address & ~3U);
    return static_cast<u8>((word >> ((address & 3U) * 8U)) & 0xFFU);
}

u16 Engine3D::read16(Address address) noexcept {
    const Address aligned = address & ~1U;
    if (aligned == kDisp3dCntStart) {
        return disp3dcnt_;
    }
    const u32 word = read32(aligned & ~3U);
    return static_cast<u16>((word >> ((aligned & 2U) * 8U)) & 0xFFFFU);
}

u32 Engine3D::read32(Address address) noexcept {
    const Address aligned = address & ~3U;
    if (aligned == kDisp3dCntStart) {
        return disp3dcnt_;
    }
    if (aligned == kRegGxStat) {
        return gxstat();
    }
    if (aligned == kRegRamCount) {
        return static_cast<u32>(submit_polygons_.size() & 0xFFFFU) |
               (submitted_vertex_count_ << 16U);
    }
    if (aligned >= kRegClipMtxResultStart && aligned <= kRegClipMtxResultEnd) {
        const std::size_t idx = (aligned - kRegClipMtxResultStart) >> 2U;
        return static_cast<u32>(clip_mtx_.m[idx & 15U]);
    }
    if (aligned >= kRegVecMtxResultStart && aligned <= kRegVecMtxResultEnd) {
        const std::size_t idx = (aligned - kRegVecMtxResultStart) >> 2U;
        return static_cast<u32>(vec_mtx_.m[(idx / 3U) * 4U + (idx % 3U)]);
    }
    return 0U;
}

void Engine3D::write8(Address address, u8 value) noexcept {
    if ((address & ~1U) == kDisp3dCntStart) {
        const u16 merged = ((address & 1U) == 0U)
                               ? static_cast<u16>((disp3dcnt_ & 0xFF00U) | value)
                               : static_cast<u16>((disp3dcnt_ & 0x00FFU) | (value << 8U));
        write16(kDisp3dCntStart, merged);
    }
}

void Engine3D::write16(Address address, u16 value) noexcept {
    const Address aligned = address & ~1U;
    if (aligned == kDisp3dCntStart) {
        disp3dcnt_ = (value & 0x4FFFU);
    } else if (aligned == 0x04000350U) {
        clear_color_ = (clear_color_ & 0xFFFF0000U) | value;
    } else if (aligned == 0x04000352U) {
        clear_color_ = (clear_color_ & 0x0000FFFFU) | (static_cast<u32>(value) << 16U);
    } else if (aligned == 0x04000354U) {
        clear_depth_ = value & 0x7FFFU;
    }
}

void Engine3D::write32(Address address, u32 value) noexcept {
    primitives::ScopedProfile profile_3d(profiler_, primitives::ProfileCategory::Gpu3d);
    const Address aligned = address & ~3U;
    if (aligned == kDisp3dCntStart) {
        write16(kDisp3dCntStart, static_cast<u16>(value & 0xFFFFU));
        return;
    }
    if (aligned == 0x04000350U) {
        clear_color_ = value;
        return;
    }
    if (aligned == 0x04000354U) {
        clear_depth_ = static_cast<u16>(value & 0x7FFFU);
        return;
    }
    if (aligned == kRegGxStat) {
        if (bits::test_bit(value, 15U)) {
            proj_sp_ = 0U;
            pos_sp_ = 0U;
        }
        gxstat_latch_ = (gxstat_latch_ & ~0xC0000000U) | (value & 0xC0000000U);
        check_fifo_irq_and_dma();
        return;
    }
    if (aligned >= kRegGxFifoStart && aligned <= kRegGxFifoEnd) {
        if (active_needed_params_ > 0U || has_cmds()) {
            submit_parameter(value);
        } else {
            enqueue_packed_commands(value);
        }
        check_fifo_irq_and_dma();
        return;
    }
    if (aligned >= kRegGxCmdPortStart && aligned <= kRegGxCmdPortEnd) {
        const u8 port_cmd = static_cast<u8>((aligned - 0x04000400U) >> 2U);
        if (active_cmd_ != port_cmd || active_needed_params_ == 0U) {
            active_cmd_ = port_cmd;
            active_needed_params_ = param_count_for_command(port_cmd);
            active_param_index_ = 0U;
            if (active_needed_params_ == 0U) {
                execute_command(port_cmd, {});
                check_fifo_irq_and_dma();
                return;
            }
        }
        submit_parameter(value);
        check_fifo_irq_and_dma();
    }
}

void Engine3D::enqueue_packed_commands(u32 packed_cmds) noexcept {
    for (u32 shift = 0U; shift < 32U; shift += 8U) {
        const u8 cmd = static_cast<u8>((packed_cmds >> shift) & 0xFFU);
        if (cmd != 0U) {
            push_cmd(cmd);
        }
    }
    while (active_needed_params_ == 0U && has_cmds()) {
        const u8 next_cmd = pop_cmd();
        const u8 needed = param_count_for_command(next_cmd);
        if (needed == 0U) {
            execute_command(next_cmd, {});
        } else {
            active_cmd_ = next_cmd;
            active_needed_params_ = needed;
            active_param_index_ = 0U;
        }
    }
}

void Engine3D::submit_parameter(u32 param) noexcept {
    if (active_needed_params_ == 0U) {
        if (!has_cmds()) {
            return;
        }
        active_cmd_ = pop_cmd();
        active_needed_params_ = param_count_for_command(active_cmd_);
        active_param_index_ = 0U;
    }
    if (active_needed_params_ == 0U) {
        execute_command(active_cmd_, {});
        return;
    }

    active_params_[active_param_index_++] = param;
    if (active_param_index_ < active_needed_params_) {
        return;
    }
    execute_command(active_cmd_, std::span<const u32>(active_params_.data(), active_needed_params_));
    active_needed_params_ = 0U;
    active_param_index_ = 0U;
    while (active_needed_params_ == 0U && has_cmds()) {
        const u8 next_cmd = pop_cmd();
        const u8 needed = param_count_for_command(next_cmd);
        if (needed == 0U) {
            execute_command(next_cmd, {});
        } else {
            active_cmd_ = next_cmd;
            active_needed_params_ = needed;
            active_param_index_ = 0U;
        }
    }
}

void Engine3D::execute_command(u8 cmd, std::span<const u32> params) noexcept {
    if (cmd >= 0x10U && cmd <= 0x1CU) {
        execute_matrix_command(cmd, params);
    } else if (cmd >= 0x20U && cmd <= 0x41U) {
        execute_vertex_command(cmd, params);
    } else if (cmd == 0x50U && !params.empty()) {
        swap_pending_ = true;
        swap_flags_ = params[0U];
    } else if (cmd == 0x60U && !params.empty()) {
        viewport_.x1 = static_cast<u8>(params[0U] & 0xFFU);
        viewport_.y1 = static_cast<u8>((params[0U] >> 8U) & 0xFFU);
        viewport_.x2 = static_cast<u8>((params[0U] >> 16U) & 0xFFU);
        viewport_.y2 = static_cast<u8>((params[0U] >> 24U) & 0xFFU);
    }
}

void Engine3D::update_clip_matrix() noexcept {
    clip_mtx_ = multiply_matrix(pos_mtx_, proj_mtx_);
}

void Engine3D::check_fifo_irq_and_dma() noexcept {
    arm9_dma_.trigger_gxfifo();
    const u32 irq_mode = (gxstat_latch_ >> 30U) & 3U;
    if (irq_mode == 1U || irq_mode == 2U) {
        arm9_irq_.request_irq(hardware::interrupts::IrqSource::GeometryFifo);
    }
}

std::span<const u32> Engine3D::scanline(u16 line) const noexcept {
    if (line >= kScreenHeight) {
        return {};
    }
    return std::span<const u32>(color_buffer_.data() + static_cast<std::size_t>(line) * kScreenWidth,
                                kScreenWidth);
}

}  // namespace elmasds::gpu
