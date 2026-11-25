#include "cpu/arm_architecture/barrel_shifter.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::cpu {
namespace {

[[nodiscard]] ShiftResult shift_lsl_nonzero(u32 value, u32 amount) noexcept {
    if (amount < 32U) {
        return ShiftResult{value << amount, bits::test_bit(value, 32U - amount)};
    }
    if (amount == 32U) {
        return ShiftResult{0U, bits::test_bit(value, 0U)};
    }
    return ShiftResult{0U, false};
}

[[nodiscard]] ShiftResult shift_lsr_nonzero(u32 value, u32 amount) noexcept {
    if (amount < 32U) {
        return ShiftResult{value >> amount, bits::test_bit(value, amount - 1U)};
    }
    if (amount == 32U) {
        return ShiftResult{0U, bits::test_bit(value, 31U)};
    }
    return ShiftResult{0U, false};
}

[[nodiscard]] ShiftResult shift_asr_nonzero(u32 value, u32 amount) noexcept {
    const bool sign_bit = bits::test_bit(value, 31U);
    if (amount < 32U) {
        const i32 signed_val = static_cast<i32>(value);
        const u32 shifted = static_cast<u32>(signed_val >> static_cast<int>(amount));
        return ShiftResult{shifted, bits::test_bit(value, amount - 1U)};
    }
    return ShiftResult{sign_bit ? 0xFFFFFFFFU : 0U, sign_bit};
}

[[nodiscard]] ShiftResult shift_ror_nonzero(u32 value, u32 amount) noexcept {
    const u32 mod_amount = amount & 31U;
    if (mod_amount == 0U) {
        return ShiftResult{value, bits::test_bit(value, 31U)};
    }
    return ShiftResult{bits::rotate_right_32(value, mod_amount),
                       bits::test_bit(value, mod_amount - 1U)};
}

}  // namespace

ShiftResult shift_by_immediate(const ShiftRequest& request) noexcept {
    const u32 amount = request.amount & 31U;
    switch (request.type) {
    case ShiftType::Lsl:
        if (amount == 0U) {
            return ShiftResult{request.value, request.carry_in};
        }
        return shift_lsl_nonzero(request.value, amount);
    case ShiftType::Lsr:
        return shift_lsr_nonzero(request.value, amount == 0U ? 32U : amount);
    case ShiftType::Asr:
        return shift_asr_nonzero(request.value, amount == 0U ? 32U : amount);
    case ShiftType::Ror:
        if (amount == 0U) {
            // ROR #0 encodes RRX: 33-bit rotate right through carry_in.
            const u32 carry_bit = request.carry_in ? (1U << 31U) : 0U;
            return ShiftResult{carry_bit | (request.value >> 1U),
                               bits::test_bit(request.value, 0U)};
        }
        return shift_ror_nonzero(request.value, amount);
    }
    return ShiftResult{request.value, request.carry_in};
}

ShiftResult shift_by_register(const ShiftRequest& request) noexcept {
    const u32 amount = request.amount & 0xFFU;
    if (amount == 0U) {
        return ShiftResult{request.value, request.carry_in};
    }

    switch (request.type) {
    case ShiftType::Lsl:
        return shift_lsl_nonzero(request.value, amount);
    case ShiftType::Lsr:
        return shift_lsr_nonzero(request.value, amount);
    case ShiftType::Asr:
        return shift_asr_nonzero(request.value, amount);
    case ShiftType::Ror:
        return shift_ror_nonzero(request.value, amount);
    }
    return ShiftResult{request.value, request.carry_in};
}

ShiftResult decode_rotated_immediate(u8 imm8, u8 rotate4, bool carry_in) noexcept {
    const u32 rotation = static_cast<u32>(rotate4 & 0x0FU) * 2U;
    if (rotation == 0U) {
        return ShiftResult{static_cast<u32>(imm8), carry_in};
    }
    const u32 rotated = bits::rotate_right_32(static_cast<u32>(imm8), rotation);
    return ShiftResult{rotated, bits::test_bit(rotated, 31U)};
}

}  // namespace elmasds::cpu
