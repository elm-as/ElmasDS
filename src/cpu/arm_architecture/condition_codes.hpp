#pragma once

#include "cpu/arm_architecture/status_register.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief 4-bit ARM condition codes encoded in bits [31:28] of ARM instructions.
 */
enum class ConditionCode : u8 {
    Eq = 0x0,  ///< Z == 1
    Ne = 0x1,  ///< Z == 0
    Cs = 0x2,  ///< C == 1 (HS)
    Cc = 0x3,  ///< C == 0 (LO)
    Mi = 0x4,  ///< N == 1
    Pl = 0x5,  ///< N == 0
    Vs = 0x6,  ///< V == 1
    Vc = 0x7,  ///< V == 0
    Hi = 0x8,  ///< C == 1 && Z == 0
    Ls = 0x9,  ///< C == 0 || Z == 1
    Ge = 0xA,  ///< N == V
    Lt = 0xB,  ///< N != V
    Gt = 0xC,  ///< Z == 0 && N == V
    Le = 0xD,  ///< Z == 1 || N != V
    Al = 0xE,  ///< Always
    Nv = 0xF,  ///< Never (ARMv4T) / Unconditional opcode prefix (ARMv5TE)
};

/**
 * @brief Evaluates whether an ARM condition passes given the current CPSR flags.
 *
 * Note: For `ConditionCode::Nv` (0xF), returns false; ARMv5TE decoders must check
 * for `0xF` unconditional instructions before calling this function.
 */
[[nodiscard]] constexpr bool evaluate_condition(ConditionCode condition,
                                                StatusRegister cpsr) noexcept {
    switch (condition) {
    case ConditionCode::Eq:
        return cpsr.zero();
    case ConditionCode::Ne:
        return !cpsr.zero();
    case ConditionCode::Cs:
        return cpsr.carry();
    case ConditionCode::Cc:
        return !cpsr.carry();
    case ConditionCode::Mi:
        return cpsr.negative();
    case ConditionCode::Pl:
        return !cpsr.negative();
    case ConditionCode::Vs:
        return cpsr.overflow();
    case ConditionCode::Vc:
        return !cpsr.overflow();
    case ConditionCode::Hi:
        return cpsr.carry() && !cpsr.zero();
    case ConditionCode::Ls:
        return !cpsr.carry() || cpsr.zero();
    case ConditionCode::Ge:
        return cpsr.negative() == cpsr.overflow();
    case ConditionCode::Lt:
        return cpsr.negative() != cpsr.overflow();
    case ConditionCode::Gt:
        return !cpsr.zero() && (cpsr.negative() == cpsr.overflow());
    case ConditionCode::Le:
        return cpsr.zero() || (cpsr.negative() != cpsr.overflow());
    case ConditionCode::Al:
        return true;
    case ConditionCode::Nv:
        return false;
    }
    return false;
}

}  // namespace elmasds::cpu
