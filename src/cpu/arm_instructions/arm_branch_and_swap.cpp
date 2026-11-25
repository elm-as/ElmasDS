#include "cpu/arm_instructions/arm_branch_and_swap.hpp"

#include "cpu/arm_architecture/exception_vector.hpp"
#include "primitives/bit_operations.hpp"

namespace elmasds::cpu {

ArmStepResult execute_arm_branch(ArmCoreContext& context, u32 opcode, Address instruction_pc) {
    const bool link = bits::test_bit(opcode, 24U);
    if (link) {
        context.registers.write(RegisterIndex::Lr, instruction_pc + 4U);
    }

    const i32 offset = bits::sign_extend(bits::extract_bits(opcode, 0U, 24U), 24U) * 4;
    const Address target = static_cast<Address>(static_cast<i64>(instruction_pc + 8U) + offset);
    context.branch_arm(target);
    return ArmStepResult{3U, true};
}

ArmStepResult execute_arm_branch_exchange_reg(ArmCoreContext& context,
                                              u32 opcode,
                                              Address instruction_pc) {
    const RegisterIndex rm = static_cast<RegisterIndex>(bits::extract_bits(opcode, 0U, 4U));
    const Address target = context.read_arm_operand(rm, instruction_pc);
    const bool link = bits::test_bit(opcode, 5U);
    if (link) {
        context.registers.write(RegisterIndex::Lr, instruction_pc + 4U);
    }
    context.branch_exchange(target);
    return ArmStepResult{3U, true};
}

ArmStepResult execute_arm_blx_immediate(ArmCoreContext& context,
                                        u32 opcode,
                                        Address instruction_pc) {
    context.registers.write(RegisterIndex::Lr, instruction_pc + 4U);
    const i32 word_offset = bits::sign_extend(bits::extract_bits(opcode, 0U, 24U), 24U) * 4;
    const i32 half_bit = bits::test_bit(opcode, 24U) ? 2 : 0;
    const Address target =
        static_cast<Address>(static_cast<i64>(instruction_pc + 8U) + word_offset + half_bit);
    context.branch_exchange(target | 1U);
    return ArmStepResult{3U, true};
}

ArmStepResult execute_arm_swap(ArmCoreContext& context, u32 opcode) {
    const bool byte_swap = bits::test_bit(opcode, 22U);
    const RegisterIndex rn = static_cast<RegisterIndex>(bits::extract_bits(opcode, 16U, 4U));
    const RegisterIndex rd = static_cast<RegisterIndex>(bits::extract_bits(opcode, 12U, 4U));
    const RegisterIndex rm = static_cast<RegisterIndex>(bits::extract_bits(opcode, 0U, 4U));

    const Address addr = context.registers.read(rn);
    const u32 write_val = context.registers.read(rm);

    if (byte_swap) {
        const u8 old_val = context.bus.read8(addr);
        context.bus.write8(addr, static_cast<u8>(write_val & 0xFFU));
        context.registers.write(rd, static_cast<u32>(old_val));
        return ArmStepResult{4U, false};
    }

    const u32 old_val = read_word_rotated(context.bus, addr);
    context.bus.write32(addr & ~3U, write_val);
    context.registers.write(rd, old_val);
    return ArmStepResult{4U, false};
}

ArmStepResult execute_arm_swi(ArmCoreContext& context, u32 opcode, Address instruction_pc) {
    const u8 swi_number = static_cast<u8>(bits::extract_bits(opcode, 16U, 8U));
    if (context.swi_handler.try_handle(context, swi_number)) {
        return ArmStepResult{3U, false};
    }
    raise_exception(context, CpuException::SoftwareInterrupt, instruction_pc);
    return ArmStepResult{3U, true};
}

}  // namespace elmasds::cpu
