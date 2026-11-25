#include "cpu/arm_instructions/alu_operations.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::cpu {
namespace {

[[nodiscard]] AluResult add_with_carry(u32 lhs, u32 rhs, u32 carry_bit) noexcept {
    const u64 wide = static_cast<u64>(lhs) + static_cast<u64>(rhs) + static_cast<u64>(carry_bit);
    const u32 result = static_cast<u32>(wide);
    const bool sign_lhs = bits::test_bit(lhs, 31U);
    const bool sign_rhs = bits::test_bit(rhs, 31U);
    const bool sign_res = bits::test_bit(result, 31U);

    return AluResult{
        result,
        true,
        sign_res,
        result == 0U,
        wide > 0xFFFFFFFFULL,
        (sign_lhs == sign_rhs) && (sign_lhs != sign_res),
    };
}

[[nodiscard]] AluResult sub_with_borrow(u32 lhs, u32 rhs, u32 borrow_bit) noexcept {
    // ARM subtraction carry flag is NOT-borrow: C = 1 when lhs >= rhs + borrow.
    const u64 rhs_total = static_cast<u64>(rhs) + static_cast<u64>(borrow_bit);
    const u32 result = static_cast<u32>(static_cast<u64>(lhs) - rhs_total);
    const bool sign_lhs = bits::test_bit(lhs, 31U);
    const bool sign_rhs = bits::test_bit(rhs, 31U);
    const bool sign_res = bits::test_bit(result, 31U);

    return AluResult{
        result,
        true,
        sign_res,
        result == 0U,
        static_cast<u64>(lhs) >= rhs_total,
        (sign_lhs != sign_rhs) && (sign_lhs != sign_res),
    };
}

[[nodiscard]] AluResult make_logic_result(u32 value,
                                          bool write_rd,
                                          bool shifter_carry,
                                          bool overflow_in) noexcept {
    return AluResult{
        value,
        write_rd,
        bits::test_bit(value, 31U),
        value == 0U,
        shifter_carry,
        overflow_in,
    };
}

}  // namespace

AluResult evaluate_alu_operation(const AluInputs& inputs) noexcept {
    const u32 carry_bit = inputs.carry_in ? 1U : 0U;
    const u32 borrow_bit = inputs.carry_in ? 0U : 1U;

    switch (inputs.opcode) {
    case AluOpcode::And:
        return make_logic_result(
            inputs.lhs & inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Eor:
        return make_logic_result(
            inputs.lhs ^ inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Sub:
        return sub_with_borrow(inputs.lhs, inputs.rhs, 0U);
    case AluOpcode::Rsb:
        return sub_with_borrow(inputs.rhs, inputs.lhs, 0U);
    case AluOpcode::Add:
        return add_with_carry(inputs.lhs, inputs.rhs, 0U);
    case AluOpcode::Adc:
        return add_with_carry(inputs.lhs, inputs.rhs, carry_bit);
    case AluOpcode::Sbc:
        return sub_with_borrow(inputs.lhs, inputs.rhs, borrow_bit);
    case AluOpcode::Rsc:
        return sub_with_borrow(inputs.rhs, inputs.lhs, borrow_bit);
    case AluOpcode::Tst:
        return make_logic_result(
            inputs.lhs & inputs.rhs, false, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Teq:
        return make_logic_result(
            inputs.lhs ^ inputs.rhs, false, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Cmp: {
        AluResult res = sub_with_borrow(inputs.lhs, inputs.rhs, 0U);
        res.write_rd = false;
        return res;
    }
    case AluOpcode::Cmn: {
        AluResult res = add_with_carry(inputs.lhs, inputs.rhs, 0U);
        res.write_rd = false;
        return res;
    }
    case AluOpcode::Orr:
        return make_logic_result(
            inputs.lhs | inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Mov:
        return make_logic_result(inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Bic:
        return make_logic_result(
            inputs.lhs & ~inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    case AluOpcode::Mvn:
        return make_logic_result(~inputs.rhs, true, inputs.shifter_carry, inputs.overflow_in);
    }
    return AluResult{};
}

}  // namespace elmasds::cpu
