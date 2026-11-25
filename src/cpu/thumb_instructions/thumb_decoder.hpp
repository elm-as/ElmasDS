#pragma once

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Decoded 16-bit Thumb instruction format family (Formats 1-19).
 */
enum class ThumbInstructionKind : u8 {
    MoveShiftedRegister,    ///< Format 1: LSL/LSR/ASR #imm5
    AddSubtract,            ///< Format 2: ADD/SUB Rd, Rs, Rn/#imm3
    ImmediateAlu,           ///< Format 3: MOV/CMP/ADD/SUB Rd, #imm8
    AluRegister,            ///< Format 4: 16 ALU ops on low registers
    HiRegOrBranchExchange,  ///< Format 5: ADD/CMP/MOV hi-reg, BX/BLX reg
    PcRelativeLoad,         ///< Format 6: LDR Rd, [PC, #imm8*4]
    RegisterOffsetTransfer, ///< Format 7: STR/STRB/LDR/LDRB [Rb, Ro]
    HalfwordSignTransfer,   ///< Format 8: STRH/LDSB/LDRH/LDSH [Rb, Ro]
    ImmediateTransfer,      ///< Format 9: STR/LDR/STRB/LDRB [Rb, #imm5]
    HalfwordTransfer,       ///< Format 10: STRH/LDRH [Rb, #imm5*2]
    SpRelativeTransfer,     ///< Format 11: STR/LDR [SP, #imm8*4]
    LoadAddress,            ///< Format 12: ADD Rd, PC/SP, #imm8*4
    AdjustStackPointer,     ///< Format 13: ADD/SUB SP, #imm7*4
    PushPopRegisters,       ///< Format 14: PUSH/POP {rlist, LR/PC}
    MultipleTransfer,       ///< Format 15: STMIA/LDMIA Rb!, {rlist}
    ConditionalBranch,      ///< Format 16: B<cond> label
    SoftwareInterrupt,      ///< Format 17: SWI #imm8
    UnconditionalBranch,    ///< Format 18: B label
    LongBranchPrefix,       ///< Format 19 (1/2): BL/BLX prefix
    LongBranchSuffix,       ///< Format 19 (2/2): BL/BLX suffix
    Breakpoint,             ///< ARMv5TE BKPT #imm8
    Undefined,
};

namespace detail {

[[nodiscard]] constexpr ThumbInstructionKind decode_thumb_0100_0101(u16 opcode) noexcept {
    if ((opcode & 0xFC00U) == 0x4000U) {
        return ThumbInstructionKind::AluRegister;
    }
    if ((opcode & 0xFC00U) == 0x4400U) {
        return ThumbInstructionKind::HiRegOrBranchExchange;
    }
    if ((opcode & 0xF800U) == 0x4800U) {
        return ThumbInstructionKind::PcRelativeLoad;
    }
    return bits::test_bit(opcode, 9U) ? ThumbInstructionKind::HalfwordSignTransfer
                                      : ThumbInstructionKind::RegisterOffsetTransfer;
}

[[nodiscard]] constexpr ThumbInstructionKind decode_thumb_1011(u16 opcode,
                                                               ArmArchitecture arch) noexcept {
    if ((opcode & 0xFF00U) == 0xB000U) {
        return ThumbInstructionKind::AdjustStackPointer;
    }
    if ((opcode & 0xF600U) == 0xB400U) {
        return ThumbInstructionKind::PushPopRegisters;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0xFF00U) == 0xBE00U) {
        return ThumbInstructionKind::Breakpoint;
    }
    return ThumbInstructionKind::Undefined;
}

[[nodiscard]] constexpr ThumbInstructionKind decode_thumb_1101(u16 opcode) noexcept {
    const u32 cond = bits::extract_bits(opcode, 8U, 4U);
    if (cond == 0xFU) {
        return ThumbInstructionKind::SoftwareInterrupt;
    }
    if (cond == 0xEU) {
        return ThumbInstructionKind::Undefined;
    }
    return ThumbInstructionKind::ConditionalBranch;
}

[[nodiscard]] constexpr ThumbInstructionKind decode_thumb_111x(u16 opcode,
                                                               ArmArchitecture arch) noexcept {
    const u32 top3 = bits::extract_bits(opcode, 11U, 3U);
    switch (top3) {
    case 0b100U:
        return ThumbInstructionKind::UnconditionalBranch;
    case 0b101U:
        return (arch == ArmArchitecture::ArmV5Te && (opcode & 1U) == 0U)
                   ? ThumbInstructionKind::LongBranchSuffix
                   : ThumbInstructionKind::Undefined;
    case 0b110U:
        return ThumbInstructionKind::LongBranchPrefix;
    case 0b111U:
        return ThumbInstructionKind::LongBranchSuffix;
    default:
        return ThumbInstructionKind::Undefined;
    }
}

}  // namespace detail

/**
 * @brief Classifies a 16-bit Thumb instruction opcode for the given ISA variant.
 */
[[nodiscard]] constexpr ThumbInstructionKind decode_thumb_instruction(
    u16 opcode,
    ArmArchitecture architecture) noexcept {
    const u32 top3 = bits::extract_bits(opcode, 13U, 3U);
    switch (top3) {
    case 0b000U:
        return (bits::extract_bits(opcode, 11U, 2U) == 3U)
                   ? ThumbInstructionKind::AddSubtract
                   : ThumbInstructionKind::MoveShiftedRegister;
    case 0b001U:
        return ThumbInstructionKind::ImmediateAlu;
    case 0b010U:
        return detail::decode_thumb_0100_0101(opcode);
    case 0b011U:
        return ThumbInstructionKind::ImmediateTransfer;
    case 0b100U:
        return bits::test_bit(opcode, 12U) ? ThumbInstructionKind::SpRelativeTransfer
                                           : ThumbInstructionKind::HalfwordTransfer;
    case 0b101U:
        return bits::test_bit(opcode, 12U) ? detail::decode_thumb_1011(opcode, architecture)
                                           : ThumbInstructionKind::LoadAddress;
    case 0b110U:
        return bits::test_bit(opcode, 12U) ? detail::decode_thumb_1101(opcode)
                                           : ThumbInstructionKind::MultipleTransfer;
    case 0b111U:
        return detail::decode_thumb_111x(opcode, architecture);
    }
    return ThumbInstructionKind::Undefined;
}

}  // namespace elmasds::cpu
