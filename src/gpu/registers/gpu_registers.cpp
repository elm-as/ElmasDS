#include "gpu/registers/gpu_registers.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::gpu {

void AffineBgParams::latch_reference() noexcept {
    internal_x = bits::sign_extend(ref_x_raw & 0x0FFFFFFFU, 28U);
    internal_y = bits::sign_extend(ref_y_raw & 0x0FFFFFFFU, 28U);
}

void AffineBgParams::step_scanline() noexcept {
    internal_x += static_cast<i32>(pb);
    internal_y += static_cast<i32>(pd);
}

GpuRegisters::GpuRegisters(Address base_address, bool is_engine_a) noexcept
    : base_address_(base_address), is_engine_a_(is_engine_a) {
    reset();
}

void GpuRegisters::reset() noexcept {
    dispcnt_ = 0U;
    bg_cnt_.fill(0U);
    bg_hofs_.fill(0U);
    bg_vofs_.fill(0U);
    affine_[0U] = AffineBgParams{};
    affine_[1U] = AffineBgParams{};
    win_h_.fill(0U);
    win_v_.fill(0U);
    win_in_ = 0U;
    win_out_ = 0U;
    mosaic_ = 0U;
    bldcnt_ = 0U;
    bldalpha_ = 0U;
    bldy_ = 0U;
    master_bright_ = 0U;
}

void GpuRegisters::reload_affine_latches() noexcept {
    affine_[0U].latch_reference();
    affine_[1U].latch_reference();
}

void GpuRegisters::step_affine_latches() noexcept {
    affine_[0U].step_scanline();
    affine_[1U].step_scanline();
}

u8 GpuRegisters::read8(Address address) {
    const u16 half = read16(address & ~1U);
    return ((address & 1U) == 0U) ? static_cast<u8>(half & 0xFFU)
                                  : static_cast<u8>((half >> 8U) & 0xFFU);
}

u16 GpuRegisters::read16(Address address) {
    const Address off = (address & ~1U) - base_address_;
    switch (off) {
        case 0x00U:
            return static_cast<u16>(dispcnt_ & 0xFFFFU);
        case 0x02U:
            return static_cast<u16>((dispcnt_ >> 16U) & 0xFFFFU);
        case 0x08U:
        case 0x0AU:
        case 0x0CU:
        case 0x0EU:
            return bg_cnt_[(off - 0x08U) >> 1U];
        case 0x48U:
            return win_in_;
        case 0x4AU:
            return win_out_;
        case 0x50U:
            return bldcnt_;
        case 0x52U:
            return bldalpha_;
        case 0x6CU:
            return master_bright_;
        default:
            return 0U;
    }
}

u32 GpuRegisters::read32(Address address) {
    const Address aligned = address & ~3U;
    return static_cast<u32>(read16(aligned)) |
           (static_cast<u32>(read16(aligned + 2U)) << 16U);
}

void GpuRegisters::write8(Address address, u8 value) {
    const Address aligned = address & ~1U;
    const u16 cur = read16(aligned);
    const u16 merged = ((address & 1U) == 0U)
                           ? static_cast<u16>((cur & 0xFF00U) | value)
                           : static_cast<u16>((cur & 0x00FFU) | (static_cast<u16>(value) << 8U));
    write16(aligned, merged);
}

void GpuRegisters::write16(Address address, u16 value) {
    const Address off = (address & ~1U) - base_address_;
    if (off == 0x00U) {
        dispcnt_ = (dispcnt_ & 0xFFFF0000U) | value;
        return;
    }
    if (off == 0x02U) {
        const u32 mask = is_engine_a_ ? 0xFFFFFFFFU : 0xC033FFF7U;
        dispcnt_ = ((dispcnt_ & 0x0000FFFFU) | (static_cast<u32>(value) << 16U)) & mask;
        return;
    }
    if (off >= 0x08U && off <= 0x0EU) {
        bg_cnt_[(off - 0x08U) >> 1U] = value;
        return;
    }
    if (off >= 0x10U && off <= 0x1EU) {
        const std::size_t idx = (off - 0x10U) >> 2U;
        if ((off & 2U) == 0U) {
            bg_hofs_[idx] = static_cast<u16>(value & 0x01FFU);
        } else {
            bg_vofs_[idx] = static_cast<u16>(value & 0x01FFU);
        }
        return;
    }
    if (off >= 0x20U && off <= 0x3EU) {
        const std::size_t idx = (off >= 0x30U) ? 1U : 0U;
        write_affine_reg(idx, off & 0x0FU, value);
        return;
    }
    switch (off) {
        case 0x40U:
            win_h_[0U] = value;
            break;
        case 0x42U:
            win_h_[1U] = value;
            break;
        case 0x44U:
            win_v_[0U] = value;
            break;
        case 0x46U:
            win_v_[1U] = value;
            break;
        case 0x48U:
            win_in_ = static_cast<u16>(value & 0x3F3FU);
            break;
        case 0x4AU:
            win_out_ = static_cast<u16>(value & 0x3F3FU);
            break;
        case 0x4CU:
            mosaic_ = value;
            break;
        case 0x50U:
            bldcnt_ = static_cast<u16>(value & 0x3FFFU);
            break;
        case 0x52U:
            bldalpha_ = static_cast<u16>(value & 0x1F1FU);
            break;
        case 0x54U:
            bldy_ = static_cast<u16>(value & 0x001FU);
            break;
        case 0x6CU:
            master_bright_ = static_cast<u16>(value & 0xC01FU);
            break;
        default:
            break;
    }
}

void GpuRegisters::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    write16(aligned, static_cast<u16>(value & 0xFFFFU));
    write16(aligned + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

void GpuRegisters::write_affine_reg(std::size_t idx, Address sub_offset, u16 value) noexcept {
    AffineBgParams& param = affine_[idx];
    switch (sub_offset) {
        case 0x00U:
            param.pa = static_cast<i16>(value);
            break;
        case 0x02U:
            param.pb = static_cast<i16>(value);
            break;
        case 0x04U:
            param.pc = static_cast<i16>(value);
            break;
        case 0x06U:
            param.pd = static_cast<i16>(value);
            break;
        case 0x08U:
            param.ref_x_raw = (param.ref_x_raw & 0x0FFF0000U) | value;
            param.internal_x = bits::sign_extend(param.ref_x_raw, 28U);
            break;
        case 0x0AU:
            param.ref_x_raw =
                (param.ref_x_raw & 0x0000FFFFU) | (static_cast<u32>(value & 0x0FFFU) << 16U);
            param.internal_x = bits::sign_extend(param.ref_x_raw, 28U);
            break;
        case 0x0CU:
            param.ref_y_raw = (param.ref_y_raw & 0x0FFF0000U) | value;
            param.internal_y = bits::sign_extend(param.ref_y_raw, 28U);
            break;
        case 0x0EU:
            param.ref_y_raw =
                (param.ref_y_raw & 0x0000FFFFU) | (static_cast<u32>(value & 0x0FFFU) << 16U);
            param.internal_y = bits::sign_extend(param.ref_y_raw, 28U);
            break;
        default:
            break;
    }
}

}  // namespace elmasds::gpu
