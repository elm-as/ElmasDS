#pragma once

#include "primitives/fixed_width_types.hpp"

#include <optional>

namespace elmasds::cpu {

/**
 * @brief Target ARM ISA revision emulated by a Nintendo DS CPU core.
 */
enum class ArmArchitecture {
    ArmV4T,   ///< ARM7TDMI (33.51 MHz)
    ArmV5Te,  ///< ARM946E-S (67.03 MHz)
};

/**
 * @brief ARM processor operating modes encoded in CPSR/SPSR bits [4:0].
 */
enum class CpuMode : u8 {
    User = 0x10,
    Fiq = 0x11,
    Irq = 0x12,
    Supervisor = 0x13,
    Abort = 0x17,
    Undefined = 0x1B,
    System = 0x1F,
};

/**
 * @brief Bank index separating physically distinct SP/LR/SPSR register sets.
 */
enum class ModeBank : u8 {
    UserSystem = 0,
    Fiq = 1,
    Irq = 2,
    Supervisor = 3,
    Abort = 4,
    Undefined = 5,
};

/**
 * @brief Decodes raw CPSR[4:0] mode bits into a valid CpuMode if recognized.
 */
[[nodiscard]] constexpr std::optional<CpuMode> decode_cpu_mode(u32 mode_bits) noexcept {
    switch (mode_bits & 0x1FU) {
    case 0x10U:
        return CpuMode::User;
    case 0x11U:
        return CpuMode::Fiq;
    case 0x12U:
        return CpuMode::Irq;
    case 0x13U:
        return CpuMode::Supervisor;
    case 0x17U:
        return CpuMode::Abort;
    case 0x1BU:
        return CpuMode::Undefined;
    case 0x1FU:
        return CpuMode::System;
    default:
        return std::nullopt;
    }
}

/**
 * @brief Returns true if the mode has privileged access to CPSR/SPSR and CP15.
 */
[[nodiscard]] constexpr bool is_privileged_mode(CpuMode mode) noexcept {
    return mode != CpuMode::User;
}

/**
 * @brief Returns true if the mode owns a dedicated Saved Program Status Register (SPSR).
 */
[[nodiscard]] constexpr bool mode_has_spsr(CpuMode mode) noexcept {
    return mode != CpuMode::User && mode != CpuMode::System;
}

/**
 * @brief Maps a CpuMode to its register bank slot.
 */
[[nodiscard]] constexpr ModeBank mode_to_bank(CpuMode mode) noexcept {
    switch (mode) {
    case CpuMode::Fiq:
        return ModeBank::Fiq;
    case CpuMode::Irq:
        return ModeBank::Irq;
    case CpuMode::Supervisor:
        return ModeBank::Supervisor;
    case CpuMode::Abort:
        return ModeBank::Abort;
    case CpuMode::Undefined:
        return ModeBank::Undefined;
    case CpuMode::User:
    case CpuMode::System:
        return ModeBank::UserSystem;
    }
    return ModeBank::UserSystem;
}

}  // namespace elmasds::cpu
