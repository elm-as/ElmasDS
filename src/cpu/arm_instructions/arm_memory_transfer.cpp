#include "cpu/arm_instructions/arm_memory_transfer.hpp"

#include "cpu/arm_architecture/barrel_shifter.hpp"
#include "primitives/bit_operations.hpp"

#include <bit>
#include <optional>

namespace elmasds::cpu {
namespace {

[[nodiscard]] RegisterIndex reg_at(u32 opcode, u32 lsb) noexcept {
    return static_cast<RegisterIndex>(bits::extract_bits(opcode, lsb, 4U));
}

[[nodiscard]] u32 compute_single_offset(const ArmCoreContext& context, u32 opcode) noexcept {
    if (!bits::test_bit(opcode, 25U)) {
        return bits::extract_bits(opcode, 0U, 12U);
    }
    const u32 rm_val = context.registers.read(reg_at(opcode, 0U));
    const ShiftType type = static_cast<ShiftType>(bits::extract_bits(opcode, 5U, 2U));
    const u32 amount = bits::extract_bits(opcode, 7U, 5U);
    return shift_by_immediate(ShiftRequest{rm_val, amount, type, context.registers.cpsr().carry()})
        .value;
}

[[nodiscard]] Address apply_offset(Address base, u32 offset, bool add) noexcept {
    return add ? (base + offset) : (base - offset);
}

struct TransferRegisterRead {
    RegisterIndex reg = RegisterIndex::R0;
    Address instruction_pc = 0;
    bool user_bank = false;
};

struct TransferRegisterWrite {
    RegisterIndex reg = RegisterIndex::R0;
    u32 value = 0;
    bool user_bank = false;
};

struct EmptyRlistRequest {
    u32 opcode = 0;
    Address instruction_pc = 0;
    Address base_addr = 0;
};

struct LdmTransferRequest {
    u16 rlist = 0;
    Address start_addr = 0;
    bool user_bank = false;
};

[[nodiscard]] u32 read_register_for_transfer(const ArmCoreContext& context,
                                             const TransferRegisterRead& request) noexcept {
    if (request.reg == RegisterIndex::Pc) {
        return request.instruction_pc + 12U;
    }
    return request.user_bank ? context.registers.read_user(request.reg)
                             : context.registers.read(request.reg);
}

void write_register_for_transfer(ArmCoreContext& context,
                                 const TransferRegisterWrite& request) noexcept {
    if (request.user_bank) {
        context.registers.write_user(request.reg, request.value);
    } else {
        context.registers.write(request.reg, request.value);
    }
}

void handle_empty_rlist(ArmCoreContext& context, const EmptyRlistRequest& request) noexcept {
    const bool pre_index = bits::test_bit(request.opcode, 24U);
    const bool add = bits::test_bit(request.opcode, 23U);
    const bool load = bits::test_bit(request.opcode, 20U);
    const RegisterIndex rn = reg_at(request.opcode, 16U);

    const Address final_base = add ? (request.base_addr + 0x40U) : (request.base_addr - 0x40U);
    if (context.registers.architecture() == ArmArchitecture::ArmV4T) {
        Address slot_addr = add ? request.base_addr : (request.base_addr - 0x40U);
        if (pre_index == add) {
            slot_addr += 4U;
        }
        if (load) {
            context.load_pc(read_word_rotated(context.bus, slot_addr));
        } else {
            context.bus.write32(slot_addr & ~3U, request.instruction_pc + 12U);
        }
    }
    context.registers.write(rn, final_base);
}

[[nodiscard]] bool should_ldm_writeback(ArmArchitecture arch, u16 rlist, u8 rn_bit) noexcept {
    const u16 rn_mask = static_cast<u16>(1U << rn_bit);
    if ((rlist & rn_mask) == 0U) {
        return true;
    }
    if (arch == ArmArchitecture::ArmV4T) {
        return false;
    }
    // ARMv5TE: writeback occurs if Rn is the only register or not the highest register in rlist.
    const u16 higher_mask = static_cast<u16>(0xFFFFU << (rn_bit + 1U));
    return (rlist == rn_mask) || ((rlist & higher_mask) != 0U);
}

[[nodiscard]] std::optional<u32> execute_ldm_loop(ArmCoreContext& context,
                                                  const LdmTransferRequest& request) {
    std::optional<u32> loaded_pc;
    Address addr = request.start_addr;
    for (u8 i = 0; i < 16U; ++i) {
        if ((request.rlist & (1U << i)) == 0U) {
            continue;
        }
        const u32 loaded = read_word_rotated(context.bus, addr);
        const auto reg = static_cast<RegisterIndex>(i);
        if (reg == RegisterIndex::Pc) {
            loaded_pc = loaded;
        } else {
            write_register_for_transfer(
                context, TransferRegisterWrite{reg, loaded, request.user_bank});
        }
        addr += 4U;
    }
    return loaded_pc;
}

struct StmTransferRequest {
    u32 opcode = 0;
    Address instruction_pc = 0;
    Address start_addr = 0;
    Address final_base = 0;
};

void execute_stm_loop(ArmCoreContext& context, const StmTransferRequest& req) {
    const u16 rlist = static_cast<u16>(bits::extract_bits(req.opcode, 0U, 16U));
    const u8 rn_idx = static_cast<u8>(bits::extract_bits(req.opcode, 16U, 4U));
    const bool user_bank = bits::test_bit(req.opcode, 22U);
    const bool writeback = bits::test_bit(req.opcode, 21U);
    const bool is_arm7 = context.registers.architecture() == ArmArchitecture::ArmV4T;
    const u16 lower_mask = static_cast<u16>((1U << rn_idx) - 1U);
    const bool rn_is_first = (rlist & lower_mask) == 0U;

    Address addr = req.start_addr;
    for (u8 i = 0; i < 16U; ++i) {
        if ((rlist & (1U << i)) == 0U) {
            continue;
        }
        u32 value = read_register_for_transfer(
            context,
            TransferRegisterRead{static_cast<RegisterIndex>(i), req.instruction_pc, user_bank});
        if (i == rn_idx && writeback && is_arm7 && !rn_is_first) {
            value = req.final_base;
        }
        context.bus.write32(addr & ~3U, value);
        addr += 4U;
    }
}

}  // namespace

ArmStepResult execute_arm_single_transfer(ArmCoreContext& context,
                                          u32 opcode,
                                          Address instruction_pc) {
    const bool pre_index = bits::test_bit(opcode, 24U);
    const bool add = bits::test_bit(opcode, 23U);
    const bool byte = bits::test_bit(opcode, 22U);
    const bool writeback = bits::test_bit(opcode, 21U) || !pre_index;
    const bool load = bits::test_bit(opcode, 20U);

    const RegisterIndex rn = reg_at(opcode, 16U);
    const RegisterIndex rd = reg_at(opcode, 12U);
    const Address base = context.read_arm_operand(rn, instruction_pc);
    const u32 offset = compute_single_offset(context, opcode);
    const Address indexed = apply_offset(base, offset, add);
    const Address effective = pre_index ? indexed : base;

    if (writeback && rn != RegisterIndex::Pc) {
        context.registers.write(rn, indexed);
    }

    if (load) {
        const u32 loaded =
            byte ? static_cast<u32>(context.bus.read8(effective))
                 : read_word_rotated(context.bus, effective);
        if (rd == RegisterIndex::Pc) {
            context.load_pc(loaded);
            return ArmStepResult{5U, true};
        }
        context.registers.write(rd, loaded);
        return ArmStepResult{3U, false};
    }

    const u32 store_val =
        (rd == RegisterIndex::Pc) ? (instruction_pc + 12U) : context.registers.read(rd);
    if (byte) {
        context.bus.write8(effective, static_cast<u8>(store_val & 0xFFU));
    } else {
        context.bus.write32(effective & ~3U, store_val);
    }
    return ArmStepResult{2U, false};
}

ArmStepResult execute_arm_halfword_or_double(ArmCoreContext& context,
                                             u32 opcode,
                                             Address instruction_pc) {
    const bool pre_index = bits::test_bit(opcode, 24U);
    const bool add = bits::test_bit(opcode, 23U);
    const bool imm_form = bits::test_bit(opcode, 22U);
    const bool writeback = bits::test_bit(opcode, 21U) || !pre_index;
    const bool load = bits::test_bit(opcode, 20U);
    const u32 sh_bits = bits::extract_bits(opcode, 5U, 2U);

    const RegisterIndex rn = reg_at(opcode, 16U);
    const RegisterIndex rd = reg_at(opcode, 12U);
    const Address base = context.read_arm_operand(rn, instruction_pc);
    const u32 offset = imm_form
                           ? ((bits::extract_bits(opcode, 8U, 4U) << 4U) |
                              bits::extract_bits(opcode, 0U, 4U))
                           : context.registers.read(reg_at(opcode, 0U));
    const Address indexed = apply_offset(base, offset, add);
    const Address effective = pre_index ? indexed : base;

    if (writeback && rn != RegisterIndex::Pc) {
        context.registers.write(rn, indexed);
    }

    if (!load && sh_bits >= 2U && context.registers.architecture() == ArmArchitecture::ArmV5Te) {
        const RegisterIndex rd_next = static_cast<RegisterIndex>((static_cast<u8>(rd) + 1U) & 0xFU);
        if (sh_bits == 2U) {
            context.registers.write(rd, context.bus.read32(effective & ~3U));
            context.registers.write(rd_next, context.bus.read32((effective & ~3U) + 4U));
            return ArmStepResult{3U, rd == RegisterIndex::Pc};
        }
        context.bus.write32(effective & ~3U, context.registers.read(rd));
        context.bus.write32((effective & ~3U) + 4U, context.registers.read(rd_next));
        return ArmStepResult{3U, false};
    }

    if (!load) {
        const u32 val =
            (rd == RegisterIndex::Pc) ? (instruction_pc + 12U) : context.registers.read(rd);
        context.bus.write16(effective & ~1U, static_cast<u16>(val & 0xFFFFU));
        return ArmStepResult{2U, false};
    }

    u32 loaded = 0U;
    if (sh_bits == 1U) {
        loaded = read_halfword_unsigned(context.bus, effective, context.registers.architecture());
    } else if (sh_bits == 2U) {
        loaded = static_cast<u32>(bits::sign_extend(context.bus.read8(effective), 8U));
    } else if (context.registers.architecture() == ArmArchitecture::ArmV4T &&
               (effective & 1U) != 0U) {
        loaded = static_cast<u32>(bits::sign_extend(context.bus.read8(effective), 8U));
    } else {
        loaded = static_cast<u32>(bits::sign_extend(context.bus.read16(effective & ~1U), 16U));
    }

    if (rd == RegisterIndex::Pc) {
        context.load_pc(loaded);
        return ArmStepResult{5U, true};
    }
    context.registers.write(rd, loaded);
    return ArmStepResult{3U, false};
}

ArmStepResult execute_arm_block_transfer(ArmCoreContext& context,
                                         u32 opcode,
                                         Address instruction_pc) {
    const u16 rlist = static_cast<u16>(bits::extract_bits(opcode, 0U, 16U));
    const RegisterIndex rn = reg_at(opcode, 16U);
    const Address base = context.registers.read(rn);
    if (rlist == 0U) {
        handle_empty_rlist(context, EmptyRlistRequest{opcode, instruction_pc, base});
        return ArmStepResult{3U, bits::test_bit(opcode, 20U)};
    }

    const bool pre_index = bits::test_bit(opcode, 24U);
    const bool add = bits::test_bit(opcode, 23U);
    const bool s_bit = bits::test_bit(opcode, 22U);
    const bool writeback = bits::test_bit(opcode, 21U);
    const bool load = bits::test_bit(opcode, 20U);
    const u32 count = static_cast<u32>(std::popcount(rlist));

    const Address final_base = add ? (base + count * 4U) : (base - count * 4U);
    Address start_addr = add ? base : (base - count * 4U);
    if (pre_index == add) {
        start_addr += 4U;
    }

    if (!load) {
        execute_stm_loop(
            context, StmTransferRequest{opcode, instruction_pc, start_addr, final_base});
        if (writeback) {
            context.registers.write(rn, final_base);
        }
        return ArmStepResult{2U + count, false};
    }

    const bool includes_pc = (rlist & (1U << 15U)) != 0U;
    const bool user_bank = s_bit && !includes_pc;
    const std::optional<u32> loaded_pc =
        execute_ldm_loop(context, LdmTransferRequest{rlist, start_addr, user_bank});
    // Perform base writeback in the pre-restore bank so LDMFD SP!, {..., PC}^ updates SP_irq/fiq.
    if (writeback && should_ldm_writeback(
                         context.registers.architecture(), rlist, static_cast<u8>(rn))) {
        context.registers.write(rn, final_base);
    }
    if (!loaded_pc.has_value()) {
        return ArmStepResult{2U + count, false};
    }
    if (s_bit) {
        // Restore CPSR before masking PC; do not invoke load_pc so ARMv5TE keeps SPSR.T.
        context.registers.set_cpsr(context.registers.spsr());
        const bool thumb = context.registers.cpsr().thumb();
        context.registers.write(RegisterIndex::Pc, *loaded_pc & (thumb ? ~1U : ~3U));
    } else {
        context.load_pc(*loaded_pc);
    }
    return ArmStepResult{2U + count, true};
}

}  // namespace elmasds::cpu
