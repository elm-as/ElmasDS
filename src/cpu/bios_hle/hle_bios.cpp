#include "cpu/bios_hle/hle_bios.hpp"

#include "memory/bios/bios_memory.hpp"
#include "primitives/bit_operations.hpp"

#include <cmath>

namespace elmasds::cpu {
namespace {

constexpr Address kArm7IrqCheckFlagsAddress = 0x0380FFF8U;

void write_word_le(std::vector<u8>& buf, std::size_t off, u32 value) noexcept {
    buf[off + 0U] = static_cast<u8>(value & 0xFFU);
    buf[off + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    buf[off + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
    buf[off + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
}

}  // namespace

HleBios::HleBios(hardware::interrupts::CpuKind cpu_kind,
                 hardware::interrupts::InterruptController& irq_controller,
                 const Cp15SystemControl* cp15) noexcept
    : cpu_kind_(cpu_kind), irq_(irq_controller), cp15_(cp15) {}

SwiHandler HleBios::make_swi_handler() noexcept {
    return SwiHandler{
        this,
        [](void* ctx, ArmCoreContext& core, u8 swi_number) {
            return static_cast<HleBios*>(ctx)->handle_swi(core, swi_number);
        },
    };
}

Address HleBios::bios_irq_check_flag_address() const noexcept {
    if (cpu_kind_ == hardware::interrupts::CpuKind::Arm9) {
        const Address dtcm_base =
            (cp15_ != nullptr) ? cp15_->dtcm_config().base_address : 0x03000000U;
        return dtcm_base + 0x3FF8U;
    }
    return kArm7IrqCheckFlagsAddress;
}

SwiOutcome HleBios::handle_swi(ArmCoreContext& core, u8 swi_number) {
    if (!enabled_) {
        return SwiOutcome{};
    }
    switch (swi_number) {
    case 0x03U: swi_wait_by_loop(core); return {true, false};
    case 0x04U:
        return swi_intr_wait(core, core.registers.read(RegisterIndex::R0) != 0U,
                             core.registers.read(RegisterIndex::R1));
    case 0x05U:
        core.registers.write(RegisterIndex::R0, 1U);
        core.registers.write(RegisterIndex::R1, 1U);
        return swi_intr_wait(core, true, 1U);
    case 0x06U: core.halted = true; return {true, false};
    case 0x09U: swi_div(core); return {true, false};
    case 0x0BU: swi_cpu_set(core); return {true, false};
    case 0x0CU: swi_cpu_fast_set(core); return {true, false};
    case 0x0DU: swi_sqrt(core); return {true, false};
    case 0x0EU: swi_get_crc16(core); return {true, false};
    case 0x0FU: core.registers.write(RegisterIndex::R0, 0U); return {true, false};
    case 0x10U: swi_bit_unpack(core); return {true, false};
    case 0x11U:
    case 0x12U: swi_lz77_uncomp(core); return {true, false};
    case 0x14U:
    case 0x15U: swi_rl_uncomp(core); return {true, false};
    default: return {true, false};
    }
}

void HleBios::swi_wait_by_loop(ArmCoreContext& core) const noexcept {
    core.registers.write(RegisterIndex::R0, 0U);
}

SwiOutcome HleBios::swi_intr_wait(ArmCoreContext& core,
                                  bool discard_old,
                                  u32 wait_flags) noexcept {
    const Address check_addr = bios_irq_check_flag_address();
    u32 bios_flags = core.bus.read32(check_addr);
    if (!intr_wait_active_ && discard_old) {
        bios_flags &= ~wait_flags;
        core.bus.write32(check_addr, bios_flags);
    }

    if ((bios_flags & wait_flags) != 0U) {
        core.bus.write32(check_addr, bios_flags & ~wait_flags);
        intr_wait_active_ = false;
        return SwiOutcome{true, false};
    }

    intr_wait_active_ = true;
    irq_.write32(hardware::interrupts::InterruptController::kRegIme, 1U);
    core.halted = true;
    // Keep PC on SWI so waking from IRQ re-checks the user handler's updated BIOS check flags.
    return SwiOutcome{true, true};
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
            core.bus.write32(dst, fixed_src ? fixed_val : core.bus.read32(src));
            src += fixed_src ? 0U : 4U;
            dst += 4U;
        }
        return;
    }

    src &= ~1U;
    dst &= ~1U;
    const u16 fixed_val = core.bus.read16(src);
    for (u32 i = 0U; i < count; ++i) {
        core.bus.write16(dst, fixed_src ? fixed_val : core.bus.read16(src));
        src += fixed_src ? 0U : 2U;
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
        core.bus.write32(dst, fixed_src ? fixed_val : core.bus.read32(src));
        src += fixed_src ? 0U : 4U;
        dst += 4U;
    }
}

void HleBios::swi_sqrt(ArmCoreContext& core) const noexcept {
    const u32 val = core.registers.read(RegisterIndex::R0);
    core.registers.write(RegisterIndex::R0, static_cast<u32>(std::sqrt(static_cast<double>(val))));
}

void HleBios::swi_get_crc16(ArmCoreContext& core) const {
    u16 crc = static_cast<u16>(core.registers.read(RegisterIndex::R0) & 0xFFFFU);
    const Address addr = core.registers.read(RegisterIndex::R1);
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
    const Address src = core.registers.read(RegisterIndex::R0);
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
    u32 remaining = core.bus.read32(src) >> 8U;
    src += 4U;
    const Address dst_start = dst;

    while (remaining > 0U) {
        const u8 flags = core.bus.read8(src++);
        for (u32 bit = 0U; bit < 8U && remaining > 0U; ++bit) {
            if ((flags & (0x80U >> bit)) == 0U) {
                core.bus.write8(dst++, core.bus.read8(src++));
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
                core.bus.write8(dst++, val);
                --remaining;
            }
        }
    }
}

void HleBios::swi_rl_uncomp(ArmCoreContext& core) const {
    Address src = core.registers.read(RegisterIndex::R0);
    Address dst = core.registers.read(RegisterIndex::R1);
    u32 remaining = core.bus.read32(src) >> 8U;
    src += 4U;

    while (remaining > 0U) {
        const u8 flag = core.bus.read8(src++);
        if ((flag & 0x80U) != 0U) {
            const u32 run_len = static_cast<u32>(flag & 0x7FU) + 3U;
            const u8 val = core.bus.read8(src++);
            for (u32 i = 0U; i < run_len && remaining > 0U; ++i) {
                core.bus.write8(dst++, val);
                --remaining;
            }
            continue;
        }
        const u32 copy_len = static_cast<u32>(flag & 0x7FU) + 1U;
        for (u32 i = 0U; i < copy_len && remaining > 0U; ++i) {
            core.bus.write8(dst++, core.bus.read8(src++));
            --remaining;
        }
    }
}

std::vector<u8> create_hle_bios9_stub() {
    std::vector<u8> stub(memory::BiosMemory::kArm9BiosSize, u8{0});
    write_word_le(stub, 0x0018U, 0xEA000006U);  // B +0x20 -> 0x0038
    write_word_le(stub, 0x0038U, 0xE92D500FU);  // STMDB SP!, {R0-R3, R12, LR}
    write_word_le(stub, 0x003CU, 0xEE190F11U);  // MRC p15, 0, R0, c9, c1, 0 (DTCM)
    write_word_le(stub, 0x0040U, 0xE1A00620U);  // MOV R0, R0, LSR #12
    write_word_le(stub, 0x0044U, 0xE1A00600U);  // MOV R0, R0, LSL #12
    write_word_le(stub, 0x0048U, 0xE2800901U);  // ADD R0, R0, #0x4000
    write_word_le(stub, 0x004CU, 0xE5101004U);  // LDR R1, [R0, #-4]
    write_word_le(stub, 0x0050U, 0xE3510000U);  // CMP R1, #0
    write_word_le(stub, 0x0054U, 0xE28FE000U);  // ADD LR, PC, #0 (-> 0xFFFF005C)
    write_word_le(stub, 0x0058U, 0x112FFF11U);  // BXNE R1
    write_word_le(stub, 0x005CU, 0xE8BD500FU);  // LDMIA SP!, {R0-R3, R12, LR}
    write_word_le(stub, 0x0060U, 0xE25EF004U);  // SUBS PC, LR, #4
    return stub;
}

std::vector<u8> create_hle_bios7_stub() {
    std::vector<u8> stub(memory::BiosMemory::kArm7BiosSize, u8{0});
    write_word_le(stub, 0x0018U, 0xEA000006U);  // B +0x20 -> 0x0038
    write_word_le(stub, 0x0038U, 0xE92D500FU);  // STMDB SP!, {R0-R3, R12, LR}
    write_word_le(stub, 0x003CU, 0xE3A00301U);  // MOV R0, #0x04000000
    write_word_le(stub, 0x0040U, 0xE5101004U);  // LDR R1, [R0, #-4] ([0x0380FFFC])
    write_word_le(stub, 0x0044U, 0xE3510000U);  // CMP R1, #0
    write_word_le(stub, 0x0048U, 0xE28FE000U);  // ADD LR, PC, #0 (-> 0x00000050)
    write_word_le(stub, 0x004CU, 0x112FFF11U);  // BXNE R1
    write_word_le(stub, 0x0050U, 0xE8BD500FU);  // LDMIA SP!, {R0-R3, R12, LR}
    write_word_le(stub, 0x0054U, 0xE25EF004U);  // SUBS PC, LR, #4
    return stub;
}

}  // namespace elmasds::cpu
