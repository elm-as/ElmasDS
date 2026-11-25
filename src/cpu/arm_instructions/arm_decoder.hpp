#pragma once

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Decoded ARM (32-bit) instruction family.
 */
enum class ArmInstructionKind : u8 {
    DataProcessing,
    Mrs,
    MsrRegister,
    MsrImmediate,
    Multiply,
    MultiplyLong,
    ArmV5DspMultiply,
    ArmV5Saturating,
    CountLeadingZeros,
    SingleDataSwap,
    BranchAndExchange,
    BranchLinkExchangeReg,
    BranchLinkExchangeImm,
    HalfwordOrDoubleTransfer,
    SingleDataTransfer,
    BlockDataTransfer,
    Branch,
    SoftwareInterrupt,
    CoprocessorTransfer,
    PreloadData,
    Breakpoint,
    Undefined,
};

namespace detail {

[[nodiscard]] constexpr ArmInstructionKind decode_unconditional_v5(u32 opcode) noexcept {
    if ((opcode & 0xFE000000U) == 0xFA000000U) {
        return ArmInstructionKind::BranchLinkExchangeImm;
    }
    if ((opcode & 0xFD70F000U) == 0xF550F000U) {
        return ArmInstructionKind::PreloadData;
    }
    return ArmInstructionKind::Undefined;
}

[[nodiscard]] constexpr ArmInstructionKind decode_misc_group0(u32 opcode,
                                                              ArmArchitecture arch) noexcept {
    if ((opcode & 0x0FFFFFF0U) == 0x012FFF10U) {
        return ArmInstructionKind::BranchAndExchange;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0x0FFFFFF0U) == 0x012FFF30U) {
        return ArmInstructionKind::BranchLinkExchangeReg;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0x0FFF0FF0U) == 0x016F0F10U) {
        return ArmInstructionKind::CountLeadingZeros;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0x0F9000F0U) == 0x01000050U) {
        return ArmInstructionKind::ArmV5Saturating;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0xFFF000F0U) == 0xE1200070U) {
        return ArmInstructionKind::Breakpoint;
    }
    if (arch == ArmArchitecture::ArmV5Te && (opcode & 0x0F900090U) == 0x01000080U) {
        return ArmInstructionKind::ArmV5DspMultiply;
    }
    if ((opcode & 0x0FBF0FFFU) == 0x010F0000U) {
        return ArmInstructionKind::Mrs;
    }
    if ((opcode & 0x0FB0FFF0U) == 0x0120F000U) {
        return ArmInstructionKind::MsrRegister;
    }
    return ArmInstructionKind::Undefined;
}

[[nodiscard]] constexpr ArmInstructionKind decode_group0(u32 opcode, ArmArchitecture arch) noexcept {
    const bool bit4 = bits::test_bit(opcode, 4U);
    const bool bit7 = bits::test_bit(opcode, 7U);

    if (bit4 && bit7) {
        if (bits::extract_bits(opcode, 5U, 2U) != 0U) {
            return ArmInstructionKind::HalfwordOrDoubleTransfer;
        }
        if ((opcode & 0x0FC000F0U) == 0x00000090U) {
            return ArmInstructionKind::Multiply;
        }
        if ((opcode & 0x0F8000F0U) == 0x00800090U) {
            return ArmInstructionKind::MultiplyLong;
        }
        if ((opcode & 0x0FB00FF0U) == 0x01000090U) {
            return ArmInstructionKind::SingleDataSwap;
        }
        return ArmInstructionKind::Undefined;
    }

    if ((opcode & 0x01900000U) == 0x01000000U) {
        return decode_misc_group0(opcode, arch);
    }
    return ArmInstructionKind::DataProcessing;
}

}  // namespace detail

/**
 * @brief Classifies a 32-bit ARM instruction opcode for the given ISA variant.
 */
[[nodiscard]] constexpr ArmInstructionKind decode_arm_instruction(
    u32 opcode,
    ArmArchitecture architecture) noexcept {
    if (bits::extract_bits(opcode, 28U, 4U) == 0xFU) {
        return (architecture == ArmArchitecture::ArmV5Te) ? detail::decode_unconditional_v5(opcode)
                                                          : ArmInstructionKind::Undefined;
    }

    switch (bits::extract_bits(opcode, 25U, 3U)) {
    case 0b000U:
        return detail::decode_group0(opcode, architecture);
    case 0b001U:
        if ((opcode & 0x0FB0F000U) == 0x0320F000U) {
            return ArmInstructionKind::MsrImmediate;
        }
        if ((opcode & 0x0F900000U) == 0x03000000U) {
            return ArmInstructionKind::Undefined;
        }
        return ArmInstructionKind::DataProcessing;
    case 0b010U:
        return ArmInstructionKind::SingleDataTransfer;
    case 0b011U:
        return bits::test_bit(opcode, 4U) ? ArmInstructionKind::Undefined
                                          : ArmInstructionKind::SingleDataTransfer;
    case 0b100U:
        return ArmInstructionKind::BlockDataTransfer;
    case 0b101U:
        return ArmInstructionKind::Branch;
    case 0b110U:
        return ArmInstructionKind::CoprocessorTransfer;
    case 0b111U:
        return bits::test_bit(opcode, 24U) ? ArmInstructionKind::SoftwareInterrupt
                                           : ArmInstructionKind::CoprocessorTransfer;
    }
    return ArmInstructionKind::Undefined;
}

}  // namespace elmasds::cpu
