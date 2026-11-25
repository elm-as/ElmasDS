#include "cpu/bios_hle/hle_bios.hpp"

#include "primitives/bit_operations.hpp"

#include <cmath>

namespace elmasds::cpu {

namespace {

constexpr Address kArm9IrqCheckFlagsAddress = 0x03FFFFF8U;
constexpr Address kArm7IrqCheckFlagsAddress = 0x0380FFF8U;

void write_uncomp_byte(CpuBusInterface& bus, Address dest, u8 value) {
    bus.write8(dest, value);
}

}  // namespace

HleBios::HleBios(hardware::interrupts::CpuKind cpu_kind,
                 hardware::interrupts::InterruptController& irq_controller) noexcept
    : cpu_kind_(cpu_kind), irq_(irq_controller) {}

SwiHandler HleBios::make_swi_handler() noexcept {
    return SwiHandler{
        this,
        [](void* ctx, ArmCoreContext& core, u8 swi_number) {
            return static_cast<HleBios*>(ctx)->handle_swi(core, swi_number);
        },
    };
}

Address HleBios::bios_irq_Check_flag_address() const noexcept {
    return (cpu_kind_ == hardware::interrupts::CpuKind::Arm9) ? kArm9IrqCheckFlagsAddress
                                                              : kArm7IrqCheckFlagsAddress;
}

bool HleBios::handle_swi(ArmCoreContext& core, u8 swi_number) {
    if (!enabled_) {
        return false;
    }

    switch (swi_number) {
    case 0x03U:
        swi_wait_by_loop(core);
        return true;
    case 0x04U:
        swi_intr_wait(core, core.registers.read(RegisterIndex::R0) != 0U,
                      core.registers.read(RegisterIndex::R1));
        return true;
    case 0x05U:
        core.registers.write(RegisterIndex::R0, 1U);
        core.registers.write(RegisterIndex::R1, 1U);
        swi_intr_wait(core, true, 1U);
        return true;
    case 0x06U:
        core.halted = true;
        return true;
    case 0x09U:
        swi_div(core);
        return true;
    case 0x0BU:
        swi_cpu_set(core);
        return true;
    case 0x0CU:
        swi_cpu_fast_set(core);
        return true;
    case 0x0DU:
        swi_sqrt(core);
        return true;
    case 0x0EU:
        swi_get_crc16(core);
        return true;
    case 0x0FU:
        core.registers.write(RegisterIndex::R0, 0U);
        return true;
    case 0x10U:
        swi_bit_unpack(core);
        return true;
    case 0x11U:
    case 0x12U:
        swi_lz77_uncomp(core);
        return true;
    case 0x14U:
    case 0x15U:
        swi_rl_uncomp(core);
        return true;
    default:
        return true;
    }
}

void HleBios::swi_wait_by_loop(ArmCoreContext& core) const noexcept {
    core.registers.write(RegisterIndex::R0, 0U);
}

void HleBios::swi_intr_wait(ArmCoreContext& core, bool discard_old, u32 wait_flags) noexcept {
    const Address check_addr = bios_irq_Check_flag_address();
    u32 bios_flags = core.bus.read32(check_addr);
    if (discard_old) {
        bios_flags &= ~wait_flags;
        core.bus.write32(check_addr, bios_flags);
    }

    if ((bios_flags & wait_flags) != 0U) {
        core.bus.write32(check_addr, bios_flags & ~wait_flags);
        return;
    }

    irq_.write32(hardware::interrupts::InterruptController::kRegIme, 1U);
    core.halted = true;
}

void HleBios::swi_div(ArmCoreContext& core) const noexcept {
    const i32 numerator = static_cast<i32>(core.registers.read(RegisterIndex::R0));
    const i32 denominator = static_cast<i32>(core.registers.read(RegisterIndex::R1));
    if (denominator == 0) {
        return;
    }
    if (numerator == (-2147483647 - 1) && denominator == -1) {
        core.registers.write(RegisterIndex::R0, 0x80000000U);
        core.registers.write(RegisterIndex::R1, 0U);
        core.registers.write(RegisterIndex::R3, 0x80000000U);
        return;
    }
    const i32 quotient = numerator / denominator;
    const i32 remainder = numerator % denominator;
    const i32 abs_quotient = (quotient < 0) ? -quotient : quotient;
    core.registers.write(RegisterIndex::R0, static_cast<u32>(quotient));
    core.registers.write(RegisterIndex::R1, static_cast<u32>(remainder));
    core.registers.write(RegisterIndex::R3, static_cast<u32>(abs_quotient));
}

void HleBios::swi_cpu_set(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0);
    Address dst = core.registers.read(RegisterIndex::R1);
    const u32 control = core.registers.read(RegisterIndex::R2);
    const u32 count = control & 0x001FFFFFU;
    const bool fixed_src = bits::test_bit(control, 24U);
    const bool word_mode = bits::test_bit(control, 26U);

    if (word_mode) {
        src &= ~3U;
        dst &= ~3U;
        const u32 fixed_val = core.bus.read32(src);
        for (u32 i = 0U; i < count; ++i) {
            const u32 val = fixed_src ? fixed_val : core.bus.read32(src);
            core.bus.write32(dst, val);
            if (!fixed_src) {
                src += 4U;
            }
            dst += 4U;
        }
        return;
    }

    src &= ~1U;
    dst &= ~1U;
    const u16 fixed_val = core.bus.read16(src);
    for (u32 i = 0U; i < count; ++i) {
        const u16 val = fixed_src ? fixed_val : core.bus.read16(src);
        core.bus.write16(dst, val);
        if (!fixed_src) {
            src += 2U;
        }
        dst += 2U;
    }
}

void HleBios::swi_cpu_fast_set(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0) & ~3U;
    Address dst = core.registers.read(RegisterIndex::R1) & ~3U;
    const u32 control = core.registers.read(RegisterIndex::R2);
    const u32 count = (control & 0x001FFFFFU) & ~7U;
    const bool fixed_src = bits::test_bit(control, 24U);
    const u32 fixed_val = core.bus.read32(src);

    for (u32 i = 0U; i < count; ++i) {
        const u32 val = fixed_src ? fixed_val : core.bus.read32(src);
        core.bus.write32(dst, val);
        if (!fixed_src) {
            src += 4U;
        }
        dst += 4U;
    }
}

void HleBios::swi_sqrt(ArmCoreContext& core) const noexcept {
    const u32 val = core.registers.read(RegisterIndex::R0);
    const auto root = static_cast<u32>(std::sqrt(static_cast<double>(val)));
    core.registers.write(RegisterIndex::R0, root);
}

void HleBios::swi_get_crc16(ArmCoreContext& core) const {
    u16 crc = static_cast<u16>(core.registers.read(RegisterIndex::R0) & 0xFFFFU);
    Address addr = core.registers.read(RegisterIndex::R1);
    const u32 length = core.registers.read(RegisterIndex::R2);

    for (u32 i = 0U; i < length; ++i) {
        crc ^= core.bus.read8(addr + i);
        for (u32 bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 1U) != 0U) ? static_cast<u16>((crc >> 1U) ^ 0xA001U)
                                     : static_cast<u16>(crc >> 1U);
        }
    }
    core.registers.write(RegisterIndex::R0, crc);
}

void HleBios::swi_bit_unpack(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0);
    Address dst = core.registers.read(RegisterIndex::R1) & ~3U;
    const Address info = core.registers.read(RegisterIndex::R2);

    const u16 src_len = core.bus.read16(info);
    const u8 src_width = core.bus.read8(info + 2U);
    const u8 dst_width = core.bus.read8(info + 3U);
    const u32 data_offset = core.bus.read32(info + 4U);
    if (src_width == 0U || dst_width == 0U) {
        return;
    }

    const bool zero_offset = bits::test_bit(data_offset, 31U);
    const u32 offset_val = data_offset & 0x7FFFFFFFU;
    const u32 src_mask = (1U << src_width) - 1U;

    u32 out_word = 0U;
    u32 out_bit = 0U;
    for (u16 i = 0U; i < src_len; ++i) {
        const u8 byte = core.bus.read8(src + i);
        for (u32 shift = 0U; shift < 8U; shift += src_width) {
            u32 unit = (byte >> shift) & src_mask;
            if (unit != 0U || zero_offset) {
                unit += offset_val;
            }
            out_word |= (unit << out_bit);
            out_bit += dst_width;
            if (out_bit >= 32U) {
                core.bus.write32(dst, out_word);
                dst += 4U;
                out_word = 0U;
                out_bit = 0U;
            }
        }
    }
}

void HleBios::swi_lz77_uncomp(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0);
    Address dst = core.registers.read(RegisterIndex::R1);
    const u32 header = core.bus.read32(src);
    src += 4U;
    u32 remaining = header >> 8U;
    const Address dst_start = dst;

    while (remaining > 0U) {
        const u8 flags = core.bus.read8(src++);
        for (u32 bit = 0U; bit < 8U && remaining > 0U; ++bit) {
            if ((flags & (0x80U >> bit)) == 0U) {
                write_uncomp_byte(core.bus, dst++, core.bus.read8(src++));
                --remaining;
                continue;
            }
            const u8 b1 = core.bus.read8(src++);
            const u8 b2 = core.bus.read8(src++);
            const u32 len = static_cast<u32>(b1 >> 4U) + 3U;
            const u32 disp = (static_cast<u32>(b1 & 0x0FU) << 8U) | b2;
            const Address copy_base = dst - (disp + 1U);
            for (u32 k = 0U; k < len && remaining > 0U; ++k) {
                const u8 val = (copy_base + k >= dst_start) ? core.bus.read8(copy_base + k) : 0U;
                write_uncomp_byte(core.bus, dst++, val);
                --remaining;
            }
        }
    }
}

void HleBios::swi_rl_uncomp(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0);
    Address dst = core.registers.read(RegisterIndex::R1);
    const u32 header = core.bus.read32(src);
    src += 4U;
    u32 remaining = header >> 8U;

    while (remaining > 0U) {
        const u8 flag = core.bus.read8(src++);
        if ((flag & 0x80U) != 0U) {
            const u32 run_len = static_cast<u32>(flag & 0x7FU) + 3U;
            const u8 val = core.bus.read8(src++);
            for (u32 i = 0U; i < run_len && remaining > 0U; ++i) {
                write_uncomp_byte(core.bus, dst++, val);
                --remaining;
            }
            continue;
        }
        const u32 copy_len = static_cast<u32>(flag & 0x7FU) + 1U;
        for (u32 i = 0U; i < copy_len && remaining > 0U; ++i) {
            write_uncomp_byte(core.bus, dst++, core.bus.read8(src++));
            --remaining;
        }
    }
}

}  // namespace elmasds::cpu
