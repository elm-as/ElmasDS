#include "cpu/thumb_instructions/thumb_memory_transfer.hpp"

#include "primitives/bit_operations.hpp"

#include <bit>

namespace elmasds::cpu {
namespace {

[[nodiscard]] RegisterIndex low_reg(u16 opcode, u32 lsb) noexcept {
    return static_cast<RegisterIndex>(bits::extract_bits(opcode, lsb, 3U));
}

ArmStepResult execute_thumb_reg_offset(ArmCoreContext& context, u16 opcode) {
    const Address addr = context.registers.read(low_reg(opcode, 3U)) +
                         context.registers.read(low_reg(opcode, 6U));
    const RegisterIndex rd = low_reg(opcode, 0U);
    const u32 op = bits::extract_bits(opcode, 10U, 2U);

    if (!bits::test_bit(opcode, 9U)) {
        switch (op) {
        case 0U:
            context.bus.write32(addr & ~3U, context.registers.read(rd));
            return ArmStepResult{2U, false};
        case 1U:
            context.bus.write8(addr, static_cast<u8>(context.registers.read(rd) & 0xFFU));
            return ArmStepResult{2U, false};
        case 2U:
            context.registers.write(rd, read_word_rotated(context.bus, addr));
            return ArmStepResult{3U, false};
        default:
            context.registers.write(rd, static_cast<u32>(context.bus.read8(addr)));
            return ArmStepResult{3U, false};
        }
    }

    switch (op) {
    case 0U:
        context.bus.write16(addr & ~1U, static_cast<u16>(context.registers.read(rd) & 0xFFFFU));
        return ArmStepResult{2U, false};
    case 1U:
        context.registers.write(rd, static_cast<u32>(bits::sign_extend(context.bus.read8(addr), 8U)));
        return ArmStepResult{3U, false};
    case 2U:
        context.registers.write(
            rd, read_halfword_unsigned(context.bus, addr, context.registers.architecture()));
        return ArmStepResult{3U, false};
    default:
        if (context.registers.architecture() == ArmArchitecture::ArmV4T && (addr & 1U) != 0U) {
            context.registers.write(
                rd, static_cast<u32>(bits::sign_extend(context.bus.read8(addr), 8U)));
        } else {
            context.registers.write(
                rd, static_cast<u32>(bits::sign_extend(context.bus.read16(addr & ~1U), 16U)));
        }
        return ArmStepResult{3U, false};
    }
}

ArmStepResult execute_thumb_push_pop(ArmCoreContext& context, u16 opcode) {
    const bool pop = bits::test_bit(opcode, 11U);
    const bool lr_or_pc = bits::test_bit(opcode, 8U);
    const u8 rlist = static_cast<u8>(bits::extract_bits(opcode, 0U, 8U));
    const u32 reg_count = static_cast<u32>(std::popcount(rlist)) + (lr_or_pc ? 1U : 0U);
    Address sp = context.registers.read(RegisterIndex::Sp);

    if (reg_count == 0U) {
        const Address next_sp = pop ? (sp + 0x40U) : (sp - 0x40U);
        context.registers.write(RegisterIndex::Sp, next_sp);
        return ArmStepResult{3U, false};
    }

    if (!pop) {
        Address addr = sp - reg_count * 4U;
        context.registers.write(RegisterIndex::Sp, addr);
        for (u8 i = 0; i < 8U; ++i) {
            if ((rlist & (1U << i)) != 0U) {
                context.bus.write32(addr & ~3U, context.registers.read(static_cast<RegisterIndex>(i)));
                addr += 4U;
            }
        }
        if (lr_or_pc) {
            context.bus.write32(addr & ~3U, context.registers.read(RegisterIndex::Lr));
        }
        return ArmStepResult{2U + reg_count, false};
    }

    Address addr = sp;
    for (u8 i = 0; i < 8U; ++i) {
        if ((rlist & (1U << i)) != 0U) {
            context.registers.write(
                static_cast<RegisterIndex>(i), read_word_rotated(context.bus, addr));
            addr += 4U;
        }
    }
    bool pc_written = false;
    if (lr_or_pc) {
        context.load_pc(read_word_rotated(context.bus, addr));
        addr += 4U;
        pc_written = true;
    }
    context.registers.write(RegisterIndex::Sp, addr);
    return ArmStepResult{2U + reg_count, pc_written};
}

ArmStepResult execute_thumb_multiple(ArmCoreContext& context, u16 opcode, Address instruction_pc) {
    const bool load = bits::test_bit(opcode, 11U);
    const RegisterIndex rb = low_reg(opcode, 8U);
    const u8 rb_idx = static_cast<u8>(rb);
    const u8 rlist = static_cast<u8>(bits::extract_bits(opcode, 0U, 8U));
    const Address base = context.registers.read(rb);

    if (rlist == 0U) {
        if (context.registers.architecture() == ArmArchitecture::ArmV4T) {
            if (load) {
                context.load_pc(read_word_rotated(context.bus, base));
            } else {
                context.bus.write32(base & ~3U, instruction_pc + 6U);
            }
        }
        context.registers.write(rb, base + 0x40U);
        return ArmStepResult{3U, load && context.registers.architecture() == ArmArchitecture::ArmV4T};
    }

    const u32 count = static_cast<u32>(std::popcount(rlist));
    const Address final_base = base + count * 4U;
    Address addr = base;

    if (!load) {
        const bool is_arm7 = context.registers.architecture() == ArmArchitecture::ArmV4T;
        const bool rb_is_first = (rlist & static_cast<u8>((1U << rb_idx) - 1U)) == 0U;
        for (u8 i = 0; i < 8U; ++i) {
            if ((rlist & (1U << i)) == 0U) {
                continue;
            }
            u32 value = context.registers.read(static_cast<RegisterIndex>(i));
            if (i == rb_idx && is_arm7 && !rb_is_first) {
                value = final_base;
            }
            context.bus.write32(addr & ~3U, value);
            addr += 4U;
        }
        context.registers.write(rb, final_base);
        return ArmStepResult{2U + count, false};
    }

    for (u8 i = 0; i < 8U; ++i) {
        if ((rlist & (1U << i)) != 0U) {
            context.registers.write(
                static_cast<RegisterIndex>(i), read_word_rotated(context.bus, addr));
            addr += 4U;
        }
    }
    if ((rlist & (1U << rb_idx)) == 0U) {
        context.registers.write(rb, final_base);
    }
    return ArmStepResult{2U + count, false};
}

}  // namespace

ArmStepResult execute_thumb_single_transfer(ArmCoreContext& context,
                                            u16 opcode,
                                            Address instruction_pc) {
    if ((opcode & 0xF800U) == 0x4800U) {
        const Address addr = ((instruction_pc + 4U) & ~3U) +
                             (bits::extract_bits(opcode, 0U, 8U) * 4U);
        context.registers.write(low_reg(opcode, 8U), read_word_rotated(context.bus, addr));
        return ArmStepResult{3U, false};
    }
    if ((opcode & 0xF000U) == 0x5000U) {
        return execute_thumb_reg_offset(context, opcode);
    }
    if ((opcode & 0xF000U) == 0x9000U) {
        const Address addr = context.registers.read(RegisterIndex::Sp) +
                             (bits::extract_bits(opcode, 0U, 8U) * 4U);
        const RegisterIndex rd = low_reg(opcode, 8U);
        if (bits::test_bit(opcode, 11U)) {
            context.registers.write(rd, read_word_rotated(context.bus, addr));
            return ArmStepResult{3U, false};
        }
        context.bus.write32(addr & ~3U, context.registers.read(rd));
        return ArmStepResult{2U, false};
    }

    const RegisterIndex rb = low_reg(opcode, 3U);
    const RegisterIndex rd = low_reg(opcode, 0U);
    const u32 imm5 = bits::extract_bits(opcode, 6U, 5U);
    const bool load = bits::test_bit(opcode, 11U);

    if ((opcode & 0xF000U) == 0x8000U) {
        const Address addr = context.registers.read(rb) + (imm5 * 2U);
        if (load) {
            context.registers.write(
                rd, read_halfword_unsigned(context.bus, addr, context.registers.architecture()));
            return ArmStepResult{3U, false};
        }
        context.bus.write16(addr & ~1U, static_cast<u16>(context.registers.read(rd) & 0xFFFFU));
        return ArmStepResult{2U, false};
    }

    const bool byte = bits::test_bit(opcode, 12U);
    const Address addr = context.registers.read(rb) + (byte ? imm5 : imm5 * 4U);
    if (load) {
        const u32 loaded = byte ? static_cast<u32>(context.bus.read8(addr))
                                : read_word_rotated(context.bus, addr);
        context.registers.write(rd, loaded);
        return ArmStepResult{3U, false};
    }
    if (byte) {
        context.bus.write8(addr, static_cast<u8>(context.registers.read(rd) & 0xFFU));
    } else {
        context.bus.write32(addr & ~3U, context.registers.read(rd));
    }
    return ArmStepResult{2U, false};
}

ArmStepResult execute_thumb_stack_and_block(ArmCoreContext& context,
                                            u16 opcode,
                                            Address instruction_pc) {
    if ((opcode & 0xF000U) == 0xA000U) {
        const bool use_sp = bits::test_bit(opcode, 11U);
        const Address base = use_sp ? context.registers.read(RegisterIndex::Sp)
                                    : ((instruction_pc + 4U) & ~3U);
        context.registers.write(low_reg(opcode, 8U), base + (bits::extract_bits(opcode, 0U, 8U) * 4U));
        return ArmStepResult{1U, false};
    }
    if ((opcode & 0xFF00U) == 0xB000U) {
        const u32 offset = bits::extract_bits(opcode, 0U, 7U) * 4U;
        const Address sp = context.registers.read(RegisterIndex::Sp);
        context.registers.write(RegisterIndex::Sp, bits::test_bit(opcode, 7U) ? (sp - offset) : (sp + offset));
        return ArmStepResult{1U, false};
    }
    if ((opcode & 0xF600U) == 0xB400U) {
        return execute_thumb_push_pop(context, opcode);
    }
    return execute_thumb_multiple(context, opcode, instruction_pc);
}

}  // namespace elmasds::cpu
