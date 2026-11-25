#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief 4-bit ARM data-processing opcode encoded in instruction bits [24:21].
 */
enum class AluOpcode : u8 {
    And = 0x0,
    Eor = 0x1,
    Sub = 0x2,
    Rsb = 0x3,
    Add = 0x4,
    Adc = 0x5,
    Sbc = 0x6,
    Rsc = 0x7,
    Tst = 0x8,
    Teq = 0x9,
    Cmp = 0xA,
    Cmn = 0xB,
    Orr = 0xC,
    Mov = 0xD,
    Bic = 0xE,
    Mvn = 0xF,
};

/**
 * @brief Inputs to a pure 32-bit ARM ALU computation.
 */
struct AluInputs {
    AluOpcode opcode = AluOpcode::Mov;
    u32 lhs = 0;
    u32 rhs = 0;
    bool carry_in = false;
    bool shifter_carry = false;
    bool overflow_in = false;
};

/**
 * @brief Result and updated NZCV flags produced by an ALU operation.
 */
struct AluResult {
    u32 value = 0;
    bool write_rd = true;
    bool negative = false;
    bool zero = false;
    bool carry = false;
    bool overflow = false;
};

/**
 * @brief Evaluates an ARM ALU instruction purely without mutating external state.
 */
[[nodiscard]] AluResult evaluate_alu_operation(const AluInputs& inputs) noexcept;

}  // namespace elmasds::cpu
