#include "cpu/arm_instructions/arm_data_processing.hpp"

#include "cpu/arm_architecture/barrel_shifter.hpp"
#include "cpu/arm_instructions/alu_operations.hpp"
#include "primitives/bit_operations.hpp"

#include <limits>

namespace elmasds::cpu {
namespace {

[[nodiscard]] RegisterIndex reg_field(u32 opcode, u32 lsb) noexcept {
    return static_cast<RegisterIndex>(bits::extract_bits(opcode, lsb, 4U));
}

[[nodiscard]] ShiftResult decode_dp_operand2(const ArmCoreContext& context,
                                             u32 opcode,
                                             Address instruction_pc,
                                             bool& reg_shift_used) noexcept {
    const bool carry_in = context.registers.cpsr().carry();
    if (bits::test_bit(opcode, 25U)) {
        reg_shift_used = false;
        const u8 imm8 = static_cast<u8>(bits::extract_bits(opcode, 0U, 8U));
        const u8 rotate4 = static_cast<u8>(bits::extract_bits(opcode, 8U, 4U));
        return decode_rotated_immediate(imm8, rotate4, carry_in);
    }

    const RegisterIndex rm = reg_field(opcode, 0U);
    const ShiftType type = static_cast<ShiftType>(bits::extract_bits(opcode, 5U, 2U));
    if (bits::test_bit(opcode, 4U)) {
        reg_shift_used = true;
        const u32 rm_val = (rm == RegisterIndex::Pc) ? (instruction_pc + 12U)
                                                     : context.registers.read(rm);
        const u32 shift_amount = context.registers.read(reg_field(opcode, 8U)) & 0xFFU;
        return shift_by_register(ShiftRequest{rm_val, shift_amount, type, carry_in});
    }

    reg_shift_used = false;
    const u32 rm_val = context.read_arm_operand(rm, instruction_pc);
    const u32 shift_amount = bits::extract_bits(opcode, 7U, 5U);
    return shift_by_immediate(ShiftRequest{rm_val, shift_amount, type, carry_in});
}

[[nodiscard]] i32 saturating_add_q(i64 sum, bool& saturated) noexcept {
    constexpr i64 kMax = std::numeric_limits<i32>::max();
    constexpr i64 kMin = std::numeric_limits<i32>::min();
    if (sum > kMax) {
        saturated = true;
        return static_cast<i32>(kMax);
    }
    if (sum < kMin) {
        saturated = true;
        return static_cast<i32>(kMin);
    }
    return static_cast<i32>(sum);
}

[[nodiscard]] i16 extract_half(u32 value, bool top) noexcept {
    return static_cast<i16>(top ? (value >> 16U) : (value & 0xFFFFU));
}

}  // namespace

u32 execute_arm_data_processing(ArmCoreContext& context, u32 opcode, Address instruction_pc) {
    bool reg_shift = false;
    const ShiftResult op2 = decode_dp_operand2(context, opcode, instruction_pc, reg_shift);
    const RegisterIndex rn = reg_field(opcode, 16U);
    const RegisterIndex rd = reg_field(opcode, 12U);
    const u32 lhs = (rn == RegisterIndex::Pc)
                        ? (instruction_pc + (reg_shift ? 12U : 8U))
                        : context.registers.read(rn);

    const StatusRegister cpsr = context.registers.cpsr();
    const AluResult result = evaluate_alu_operation(AluInputs{
        static_cast<AluOpcode>(bits::extract_bits(opcode, 21U, 4U)),
        lhs,
        op2.value,
        cpsr.carry(),
        op2.carry_out,
        cpsr.overflow(),
    });

    const bool set_flags = bits::test_bit(opcode, 20U);
    if (set_flags && rd == RegisterIndex::Pc) {
        context.registers.set_cpsr(context.registers.spsr());
        if (result.write_rd) {
            const bool thumb = context.registers.cpsr().thumb();
            context.registers.write(RegisterIndex::Pc, result.value & (thumb ? ~1U : ~3U));
        }
        return reg_shift ? 2U : 1U;
    }

    if (set_flags) {
        context.registers.set_cpsr(
            cpsr.with_nzcv(result.negative, result.zero, result.carry, result.overflow));
    }
    if (result.write_rd) {
        if (rd == RegisterIndex::Pc) {
            context.branch_arm(result.value);
        } else {
            context.registers.write(rd, result.value);
        }
    }
    return reg_shift ? 2U : 1U;
}

u32 execute_arm_mrs(ArmCoreContext& context, u32 opcode) {
    const bool use_spsr = bits::test_bit(opcode, 22U);
    const StatusRegister source = use_spsr ? context.registers.spsr() : context.registers.cpsr();
    context.registers.write(reg_field(opcode, 12U), source.raw());
    return 1U;
}

u32 execute_arm_msr(ArmCoreContext& context, u32 opcode, bool immediate_form) {
    const bool write_spsr = bits::test_bit(opcode, 22U);
    u32 operand = 0U;
    if (immediate_form) {
        const u8 imm8 = static_cast<u8>(bits::extract_bits(opcode, 0U, 8U));
        const u8 rot4 = static_cast<u8>(bits::extract_bits(opcode, 8U, 4U));
        operand = decode_rotated_immediate(imm8, rot4, context.registers.cpsr().carry()).value;
    } else {
        operand = context.registers.read(reg_field(opcode, 0U));
    }

    const PsrWriteRequest request{
        operand,
        static_cast<u8>(bits::extract_bits(opcode, 16U, 4U)),
        context.registers.architecture(),
    };
    const bool allow_control = is_privileged_mode(context.registers.current_mode());

    if (write_spsr) {
        if (allow_control && mode_has_spsr(context.registers.current_mode())) {
            context.registers.set_spsr(context.registers.spsr().with_msr_write(request, true));
        }
        return 1U;
    }
    context.registers.set_cpsr(context.registers.cpsr().with_msr_write(request, allow_control));
    return 1U;
}

u32 execute_arm_multiply(ArmCoreContext& context, u32 opcode) {
    const bool accumulate = bits::test_bit(opcode, 21U);
    const bool set_flags = bits::test_bit(opcode, 20U);
    const u32 rm = context.registers.read(reg_field(opcode, 0U));
    const u32 rs = context.registers.read(reg_field(opcode, 8U));
    const u32 rn = accumulate ? context.registers.read(reg_field(opcode, 12U)) : 0U;

    const u32 product = (rm * rs) + rn;
    context.registers.write(reg_field(opcode, 16U), product);
    if (set_flags) {
        const StatusRegister cpsr = context.registers.cpsr();
        context.registers.set_cpsr(cpsr.with_nzcv(
            bits::test_bit(product, 31U), product == 0U, cpsr.carry(), cpsr.overflow()));
    }
    return accumulate ? 3U : 2U;
}

u32 execute_arm_multiply_long(ArmCoreContext& context, u32 opcode) {
    const bool is_signed = bits::test_bit(opcode, 22U);
    const bool accumulate = bits::test_bit(opcode, 21U);
    const bool set_flags = bits::test_bit(opcode, 20U);
    const RegisterIndex rd_hi = reg_field(opcode, 16U);
    const RegisterIndex rd_lo = reg_field(opcode, 12U);
    const u32 rm = context.registers.read(reg_field(opcode, 0U));
    const u32 rs = context.registers.read(reg_field(opcode, 8U));

    u64 result = is_signed
                     ? static_cast<u64>(static_cast<i64>(static_cast<i32>(rm)) *
                                        static_cast<i64>(static_cast<i32>(rs)))
                     : (static_cast<u64>(rm) * static_cast<u64>(rs));

    if (accumulate) {
        const u64 acc = (static_cast<u64>(context.registers.read(rd_hi)) << 32U) |
                        static_cast<u64>(context.registers.read(rd_lo));
        result += acc;
    }

    context.registers.write(rd_lo, static_cast<u32>(result));
    context.registers.write(rd_hi, static_cast<u32>(result >> 32U));

    if (set_flags) {
        const StatusRegister cpsr = context.registers.cpsr();
        context.registers.set_cpsr(cpsr.with_nzcv(
            bits::test_bit(static_cast<u32>(result >> 32U), 31U),
            result == 0ULL,
            cpsr.carry(),
            cpsr.overflow()));
    }
    return accumulate ? 4U : 3U;
}

u32 execute_arm_clz(ArmCoreContext& context, u32 opcode) {
    const u32 rm = context.registers.read(reg_field(opcode, 0U));
    context.registers.write(reg_field(opcode, 12U), bits::count_leading_zeros_32(rm));
    return 1U;
}

u32 execute_arm_saturating(ArmCoreContext& context, u32 opcode) {
    const u32 op = bits::extract_bits(opcode, 21U, 2U);
    const i64 rm = static_cast<i32>(context.registers.read(reg_field(opcode, 0U)));
    i64 rn = static_cast<i32>(context.registers.read(reg_field(opcode, 16U)));
    bool saturated = false;

    if (op >= 2U) {
        rn = saturating_add_q(rn * 2, saturated);
    }
    const i64 sum = ((op & 1U) != 0U) ? (rm - rn) : (rm + rn);
    const i32 result = saturating_add_q(sum, saturated);
    context.registers.write(reg_field(opcode, 12U), static_cast<u32>(result));

    if (saturated) {
        context.registers.set_cpsr(context.registers.cpsr().with_saturation(true));
    }
    return 1U;
}

u32 execute_arm_dsp_multiply(ArmCoreContext& context, u32 opcode) {
    const u32 op = bits::extract_bits(opcode, 21U, 2U);
    const bool x_top = bits::test_bit(opcode, 5U);
    const bool y_top = bits::test_bit(opcode, 6U);
    const u32 rm_raw = context.registers.read(reg_field(opcode, 0U));
    const u32 rs_raw = context.registers.read(reg_field(opcode, 8U));
    const i64 rm_half = extract_half(rm_raw, x_top);
    const i64 rs_half = extract_half(rs_raw, y_top);

    if (op == 1U) {
        const i64 wide = (static_cast<i64>(static_cast<i32>(rm_raw)) * rs_half) >> 16;
        if (!x_top) {
            const i64 acc = static_cast<i32>(context.registers.read(reg_field(opcode, 12U)));
            const i64 total = wide + acc;
            context.registers.write(reg_field(opcode, 16U), static_cast<u32>(static_cast<i32>(total)));
            if (total > std::numeric_limits<i32>::max() || total < std::numeric_limits<i32>::min()) {
                context.registers.set_cpsr(context.registers.cpsr().with_saturation(true));
            }
            return 1U;
        }
        context.registers.write(reg_field(opcode, 16U), static_cast<u32>(static_cast<i32>(wide)));
        return 1U;
    }

    if (op == 2U) {
        const RegisterIndex rd_hi = reg_field(opcode, 16U);
        const RegisterIndex rd_lo = reg_field(opcode, 12U);
        const i64 acc = static_cast<i64>((static_cast<u64>(context.registers.read(rd_hi)) << 32U) |
                                         context.registers.read(rd_lo));
        const u64 total = static_cast<u64>(acc + (rm_half * rs_half));
        context.registers.write(rd_lo, static_cast<u32>(total));
        context.registers.write(rd_hi, static_cast<u32>(total >> 32U));
        return 2U;
    }

    const i64 prod = rm_half * rs_half;
    if (op == 3U) {
        context.registers.write(reg_field(opcode, 16U), static_cast<u32>(static_cast<i32>(prod)));
        return 1U;
    }

    const i64 acc = static_cast<i32>(context.registers.read(reg_field(opcode, 12U)));
    const i64 total = prod + acc;
    context.registers.write(reg_field(opcode, 16U), static_cast<u32>(static_cast<i32>(total)));
    if (total > std::numeric_limits<i32>::max() || total < std::numeric_limits<i32>::min()) {
        context.registers.set_cpsr(context.registers.cpsr().with_saturation(true));
    }
    return 1U;
}

}  // namespace elmasds::cpu
