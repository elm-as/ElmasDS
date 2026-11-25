#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief ARM barrel shifter operation encoded in instruction bits [6:5].
 */
enum class ShiftType : u8 {
    Lsl = 0,
    Lsr = 1,
    Asr = 2,
    Ror = 3,
};

/**
 * @brief Input parameters for an ARM barrel shift evaluation.
 */
struct ShiftRequest {
    u32 value = 0;
    u32 amount = 0;
    ShiftType type = ShiftType::Lsl;
    bool carry_in = false;
};

/**
 * @brief Output value and shifter carry-out produced by the ARM barrel shifter.
 */
struct ShiftResult {
    u32 value = 0;
    bool carry_out = false;
};

/**
 * @brief Evaluates an ARM immediate-encoded shift (`amount` in 0..31).
 *
 * Hardware semantics:
 *   - `LSL #0` leaves value and carry untouched.
 *   - `LSR #0` encodes `LSR #32` (result 0, carry = bit 31).
 *   - `ASR #0` encodes `ASR #32` (result all bit 31s, carry = bit 31).
 *   - `ROR #0` encodes `RRX` (33-bit rotate right through `carry_in`).
 */
[[nodiscard]] ShiftResult shift_by_immediate(const ShiftRequest& request) noexcept;

/**
 * @brief Evaluates an ARM register-specified shift (`amount` = `Rs[7:0]`).
 *
 * Hardware semantics:
 *   - `amount == 0` preserves `value` and `carry_in` for all shift types.
 *   - Shifts >= 32 follow ARMv4T/ARMv5TE edge-case carry-out rules.
 */
[[nodiscard]] ShiftResult shift_by_register(const ShiftRequest& request) noexcept;

/**
 * @brief Decodes a 12-bit ARM immediate operand (`imm8` rotated right by `2 * rotate4`).
 */
[[nodiscard]] ShiftResult decode_rotated_immediate(u8 imm8, u8 rotate4, bool carry_in) noexcept;

}  // namespace elmasds::cpu
