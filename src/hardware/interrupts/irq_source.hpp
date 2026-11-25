#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::hardware::interrupts {

/**
 * @brief Identifies one of the two Nintendo DS ARM processor cores.
 */
enum class CpuKind : u8 {
    Arm9 = 0,
    Arm7 = 1,
};

/**
 * @brief Nintendo DS hardware interrupt sources mapped to `IE` / `IF` bit positions (`0..24`).
 */
enum class IrqSource : u8 {
    VBlank = 0,
    HBlank = 1,
    VCount = 2,
    Timer0 = 3,
    Timer1 = 4,
    Timer2 = 5,
    Timer3 = 6,
    Rtc = 7,
    Dma0 = 8,
    Dma1 = 9,
    Dma2 = 10,
    Dma3 = 11,
    Keypad = 12,
    GbaSlot = 13,
    IpcSync = 16,
    IpcSendEmpty = 17,
    IpcRecvNotEmpty = 18,
    CartridgeTransfer = 19,
    CartridgeIreqMc = 20,
    GeometryFifo = 21,
    ScreensUnfold = 22,
    Spi = 23,
    Wifi = 24,
};

/// Valid `IE`/`IF` bitmask for ARM946E-S (bits 0..6, 8..13, 16..21).
inline constexpr u32 kArm9ValidIrqMask = 0x003F3F7FU;

/// Valid `IE`/`IF` bitmask for ARM7TDMI (bits 0..13, 16..20, 22..24).
inline constexpr u32 kArm7ValidIrqMask = 0x01DF3FFFU;

/**
 * @brief Returns the valid `IE`/`IF` bitmask for the specified CPU core.
 */
[[nodiscard]] constexpr u32 valid_irq_mask(CpuKind cpu) noexcept {
    return (cpu == CpuKind::Arm9) ? kArm9ValidIrqMask : kArm7ValidIrqMask;
}

/**
 * @brief Returns the single-bit mask (`1U << bit`) corresponding to `source` (`0` if out of range).
 */
[[nodiscard]] constexpr u32 irq_bit_mask(IrqSource source) noexcept {
    const u32 bit = static_cast<u32>(source);
    if (bit >= 32U) {
        return 0U;
    }
    return 1U << bit;
}

/**
 * @brief Returns true if `source` is supported by the interrupt controller of `cpu`.
 */
[[nodiscard]] constexpr bool is_valid_irq_source(CpuKind cpu, IrqSource source) noexcept {
    return (valid_irq_mask(cpu) & irq_bit_mask(source)) != 0U;
}

}  // namespace elmasds::hardware::interrupts
