#include "cpu/thumb_instructions/thumb_alu_and_branch.hpp"

#include "cpu/arm_architecture/barrel_shifter.hpp"
#include "cpu/arm_architecture/condition_codes.hpp"
#include "cpu/arm_architecture/exception_vector.hpp"
#include "cpu/arm_instructions/alu_operations.hpp"
#include "primitives/bit_operations.hpp"

namespace elmasds::cpu {
namespace {

[[nodiscard]] RegisterIndex low_reg(u16 opcode, u32 lsb) noexcept {
    return static_cast<RegisterIndex>(bits::extract_bits(opcode, lsb, 3U));
}

void write_nzcv(RegisterBank& regs, const AluResult& result) noexcept {
    regs.set_cpsr(
        regs.cpsr().with_nzcv(result.negative, result.zero, result.carry, result.overflow));
}

void write_nzc(RegisterBank& regs, u32 value, bool carry) noexcept {
    const StatusRegister cpsr = regs.cpsr();
    regs.set_cpsr(cpsr.with_nzcv(bits::test_bit(value, 31U), value == 0U, carry, cpsr.overflow()));
}

[[nodiscard]] u32 read_thumb_hi_reg(const ArmCoreContext& context,
                                    RegisterIndex reg,
                                    Address instruction_pc) noexcept {
    return (reg == RegisterIndex::Pc) ? (instruction_pc + 4U) : context.registers.read(reg);
}

ArmStepResult execute_thumb_alu_shift(RegisterBank& regs,
                                      RegisterIndex rd,
                                      u32 rs_val,
                                      ShiftType type) noexcept {
    const u32 rd_val = regs.read(rd);
    const ShiftResult res =
        shift_by_register(ShiftRequest{rd_val, rs_val & 0xFFU, type, regs.cpsr().carry()});
    regs.write(rd, res.value);
    write_nzc(regs, res.value, res.carry_out);
    return ArmStepResult{2U, false};
}

}  // namespace

ArmStepResult execute_thumb_shift_or_addsub(ArmCoreContext& context, u16 opcode) {
    const RegisterIndex rd = low_reg(opcode, 0U);
    const RegisterIndex rs = low_reg(opcode, 3U);
    const u32 rs_val = context.registers.read(rs);

    if (bits::extract_bits(opcode, 11U, 2U) != 3U) {
        const ShiftType type = static_cast<ShiftType>(bits::extract_bits(opcode, 11U, 2U));
        const u32 imm5 = bits::extract_bits(opcode, 6U, 5U);
        const ShiftResult shifted =
            shift_by_immediate(ShiftRequest{rs_val, imm5, type, context.registers.cpsr().carry()});
        context.registers.write(rd, shifted.value);
        write_nzc(context.registers, shifted.value, shifted.carry_out);
        return ArmStepResult{1U, false};
    }

    const bool imm_flag = bits::test_bit(opcode, 10U);
    const bool sub_flag = bits::test_bit(opcode, 9U);
    const u32 rn_field = bits::extract_bits(opcode, 6U, 3U);
    const u32 rhs = imm_flag ? rn_field : context.registers.read(static_cast<RegisterIndex>(rn_field));
    const StatusRegister cpsr = context.registers.cpsr();
    const AluResult result = evaluate_alu_operation(AluInputs{
        sub_flag ? AluOpcode::Sub : AluOpcode::Add,
        rs_val,
        rhs,
        cpsr.carry(),
        cpsr.carry(),
        cpsr.overflow(),
    });
    context.registers.write(rd, result.value);
    write_nzcv(context.registers, result);
    return ArmStepResult{1U, false};
}

ArmStepResult execute_thumb_alu(ArmCoreContext& context, u16 opcode) {
    const StatusRegister cpsr = context.registers.cpsr();
    if (bits::extract_bits(opcode, 13U, 3U) == 0b001U) {
        const RegisterIndex rd = low_reg(opcode, 8U);
        const u32 op = bits::extract_bits(opcode, 11U, 2U);
        const u32 imm8 = bits::extract_bits(opcode, 0U, 8U);
        static constexpr AluOpcode kImmOps[4] = {
            AluOpcode::Mov, AluOpcode::Cmp, AluOpcode::Add, AluOpcode::Sub};
        const AluResult res = evaluate_alu_operation(AluInputs{
            kImmOps[op], context.registers.read(rd), imm8, cpsr.carry(), cpsr.carry(), cpsr.overflow()});
        if (res.write_rd) {
            context.registers.write(rd, res.value);
        }
        write_nzcv(context.registers, res);
        return ArmStepResult{1U, false};
    }

    const RegisterIndex rd = low_reg(opcode, 0U);
    const RegisterIndex rs = low_reg(opcode, 3U);
    const u32 op = bits::extract_bits(opcode, 6U, 4U);
    const u32 rs_val = context.registers.read(rs);
    const u32 rd_val = context.registers.read(rd);

    switch (op) {
    case 0x2U:
        return execute_thumb_alu_shift(context.registers, rd, rs_val, ShiftType::Lsl);
    case 0x3U:
        return execute_thumb_alu_shift(context.registers, rd, rs_val, ShiftType::Lsr);
    case 0x4U:
        return execute_thumb_alu_shift(context.registers, rd, rs_val, ShiftType::Asr);
    case 0x7U:
        return execute_thumb_alu_shift(context.registers, rd, rs_val, ShiftType::Ror);
    case 0x9U: {
        const AluResult res = evaluate_alu_operation(
            AluInputs{AluOpcode::Sub, 0U, rs_val, cpsr.carry(), cpsr.carry(), cpsr.overflow()});
        context.registers.write(rd, res.value);
        write_nzcv(context.registers, res);
        return ArmStepResult{1U, false};
    }
    case 0xDU: {
        const u32 prod = rd_val * rs_val;
        context.registers.write(rd, prod);
        write_nzc(context.registers, prod, cpsr.carry());
        return ArmStepResult{2U, false};
    }
    default: {
        const AluResult res = evaluate_alu_operation(AluInputs{
            static_cast<AluOpcode>(op), rd_val, rs_val, cpsr.carry(), cpsr.carry(), cpsr.overflow()});
        if (res.write_rd) {
            context.registers.write(rd, res.value);
        }
        write_nzcv(context.registers, res);
        return ArmStepResult{1U, false};
    }
    }
}

ArmStepResult execute_thumb_hi_reg_or_bx(ArmCoreContext& context,
                                         u16 opcode,
                                         Address instruction_pc) {
    const u32 op = bits::extract_bits(opcode, 8U, 2U);
    const bool h1 = bits::test_bit(opcode, 7U);
    const bool h2 = bits::test_bit(opcode, 6U);
    const RegisterIndex rd = static_cast<RegisterIndex>((h1 ? 8U : 0U) | bits::extract_bits(opcode, 0U, 3U));
    const RegisterIndex rs = static_cast<RegisterIndex>((h2 ? 8U : 0U) | bits::extract_bits(opcode, 3U, 3U));

    if (op == 3U) {
        if (h1 && context.registers.architecture() != ArmArchitecture::ArmV5Te) {
            raise_exception(context, CpuException::Undefined, instruction_pc);
            return ArmStepResult{3U, true};
        }
        const Address target = read_thumb_hi_reg(context, rs, instruction_pc);
        if (h1) {
            context.registers.write(RegisterIndex::Lr, (instruction_pc + 2U) | 1U);
        }
        context.branch_exchange(target);
        return ArmStepResult{3U, true};
    }

    const u32 lhs = read_thumb_hi_reg(context, rd, instruction_pc);
    const u32 rhs = read_thumb_hi_reg(context, rs, instruction_pc);

    if (op == 1U) {
        const StatusRegister cpsr = context.registers.cpsr();
        const AluResult cmp = evaluate_alu_operation(
            AluInputs{AluOpcode::Cmp, lhs, rhs, cpsr.carry(), cpsr.carry(), cpsr.overflow()});
        write_nzcv(context.registers, cmp);
        return ArmStepResult{1U, false};
    }

    const u32 result = (op == 0U) ? (lhs + rhs) : rhs;
    if (rd == RegisterIndex::Pc) {
        context.registers.write(RegisterIndex::Pc, result & ~1U);
        return ArmStepResult{3U, true};
    }
    context.registers.write(rd, result);
    return ArmStepResult{1U, false};
}

ArmStepResult execute_thumb_branch_or_swi(ArmCoreContext& context,
                                          u16 opcode,
                                          Address instruction_pc) {
    const u32 top5 = bits::extract_bits(opcode, 11U, 5U);
    if ((opcode & 0xFF00U) == 0xDF00U) {
        const u8 swi_number = static_cast<u8>(opcode & 0xFFU);
        if (context.swi_handler.try_handle(context, swi_number)) {
            return ArmStepResult{3U, false};
        }
        raise_exception(context, CpuException::SoftwareInterrupt, instruction_pc);
        return ArmStepResult{3U, true};
    }

    if ((opcode & 0xF000U) == 0xD000U) {
        const ConditionCode cond = static_cast<ConditionCode>(bits::extract_bits(opcode, 8U, 4U));
        if (!evaluate_condition(cond, context.registers.cpsr())) {
            return ArmStepResult{1U, false};
        }
        const i32 offset = bits::sign_extend(bits::extract_bits(opcode, 0U, 8U), 8U) * 2;
        const Address target = static_cast<Address>(static_cast<i64>(instruction_pc + 4U) + offset);
        context.registers.write(RegisterIndex::Pc, target & ~1U);
        return ArmStepResult{3U, true};
    }

    if (top5 == 0b11100U) {
        const i32 offset = bits::sign_extend(bits::extract_bits(opcode, 0U, 11U), 11U) * 2;
        const Address target = static_cast<Address>(static_cast<i64>(instruction_pc + 4U) + offset);
        context.registers.write(RegisterIndex::Pc, target & ~1U);
        return ArmStepResult{3U, true};
    }

    if (top5 == 0b11110U) {
        const i32 high_offset = bits::sign_extend(bits::extract_bits(opcode, 0U, 11U), 11U) * 4096;
        const Address lr_base =
            static_cast<Address>(static_cast<i64>(instruction_pc + 4U) + high_offset);
        context.registers.write(RegisterIndex::Lr, lr_base);
        return ArmStepResult{1U, false};
    }

    const u32 low_offset = bits::extract_bits(opcode, 0U, 11U) << 1U;
    const Address target = context.registers.read(RegisterIndex::Lr) + low_offset;
    context.registers.write(RegisterIndex::Lr, (instruction_pc + 2U) | 1U);
    if (top5 == 0b11101U) {
        context.branch_exchange(target & ~3U);
    } else {
        context.registers.write(RegisterIndex::Pc, target & ~1U);
    }
    return ArmStepResult{3U, true};
}

}  // namespace elmasds::cpu
