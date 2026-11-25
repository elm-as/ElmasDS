#pragma once

#include "hardware/interrupts/irq_source.hpp"
#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

#include <cstddef>

namespace elmasds::hardware::dma {

/// First MMIO address of the DMA register block (`DMA0SAD`).
inline constexpr Address kDmaBaseAddress = 0x040000B0U;

/// Last MMIO byte address of the DMA channel registers (`DMA3CNT_H` high byte).
inline constexpr Address kDmaChannelEndAddress = 0x040000DFU;

/// First MMIO address of ARM9-only `DMA0..3FILL` registers.
inline constexpr Address kDmaFillBaseAddress = 0x040000E0U;

/// Last MMIO byte address of ARM9-only `DMA0..3FILL` registers.
inline constexpr Address kDmaFillEndAddress = 0x040000EFU;

/// Number of general-purpose DMA channels per CPU core.
inline constexpr std::size_t kDmaChannelCount = 4U;

/// Byte stride between consecutive DMA channel register blocks (`DMA0`..`DMA3`).
inline constexpr Address kDmaChannelStride = 12U;

/// Writable bitmask of `DMAxCNT_H` on ARM9 (all 16 bits writable).
inline constexpr u16 kDmaArm9CntHMask = 0xFFFFU;

/// Writable bitmask of `DMAxCNT_H` on ARM7 (bits 0..4 and bit 11 unused).
inline constexpr u16 kDmaArm7CntHMask = 0xF7E0U;

/// Bit indices within 16-bit `DMAxCNT_H`.
inline constexpr u32 kDmaEnableBit = 15U;
inline constexpr u32 kDmaIrqBit = 14U;
inline constexpr u32 kDmaWidthBit = 10U;
inline constexpr u32 kDmaRepeatBit = 9U;

/**
 * @brief Hardware start-timing trigger sources for Nintendo DS DMA channels.
 */
enum class DmaTrigger : u8 {
    Immediate = 0,
    VBlank = 1,
    HBlank = 2,
    DisplayStart = 3,
    MainMemDisplay = 4,
    CartridgeSlot1 = 5,
    GbaSlot = 6,
    GeometryFifo = 7,
    Wireless = 8,
    SoundFifo = 9,
};

/**
 * @brief Returns the valid, halfword-aligned `DMAxSAD` mask for `cpu` and `channel`.
 */
[[nodiscard]] constexpr u32 sad_mask(interrupts::CpuKind cpu, std::size_t channel) noexcept {
    if (cpu == interrupts::CpuKind::Arm7 && channel == 0U) {
        return 0x07FFFFFEU;
    }
    return 0x0FFFFFFEU;
}

/**
 * @brief Returns the valid, halfword-aligned `DMAxDAD` mask for `cpu` and `channel`.
 */
[[nodiscard]] constexpr u32 dad_mask(interrupts::CpuKind cpu, std::size_t channel) noexcept {
    if (cpu == interrupts::CpuKind::Arm7 && channel < 3U) {
        return 0x07FFFFFEU;
    }
    return 0x0FFFFFFEU;
}

/**
 * @brief Returns the valid `DMAxCNT_L` word-count mask for `cpu` and `channel`.
 */
[[nodiscard]] constexpr u16 cnt_l_mask(interrupts::CpuKind cpu, std::size_t channel) noexcept {
    if (cpu == interrupts::CpuKind::Arm7 && channel < 3U) {
        return 0x3FFFU;
    }
    return 0xFFFFU;
}

/**
 * @brief Returns the writable `DMAxCNT_H` bitmask for `cpu`.
 */
[[nodiscard]] constexpr u16 cnt_h_mask(interrupts::CpuKind cpu) noexcept {
    return (cpu == interrupts::CpuKind::Arm9) ? kDmaArm9CntHMask : kDmaArm7CntHMask;
}

/**
 * @brief Computes the unsigned address delta (`+unit`, `-unit`, or `0`) for addressing `mode`.
 */
[[nodiscard]] constexpr u32 address_step_add(u32 mode, u32 unit_bytes) noexcept {
    if (mode == 0U || mode == 3U) {
        return unit_bytes;
    }
    if (mode == 1U) {
        return 0U - unit_bytes;
    }
    return 0U;
}

/**
 * @brief Decodes the ARM9 3-bit `DMAxCNT_H` start timing field (`bits 11..13`).
 */
[[nodiscard]] constexpr DmaTrigger decode_arm9_timing(u32 timing) noexcept {
    switch (timing & 0x07U) {
    case 0U:
        return DmaTrigger::Immediate;
    case 1U:
        return DmaTrigger::VBlank;
    case 2U:
        return DmaTrigger::HBlank;
    case 3U:
        return DmaTrigger::DisplayStart;
    case 4U:
        return DmaTrigger::MainMemDisplay;
    case 5U:
        return DmaTrigger::CartridgeSlot1;
    case 6U:
        return DmaTrigger::GbaSlot;
    default:
        return DmaTrigger::GeometryFifo;
    }
}

/**
 * @brief Decodes the ARM7 2-bit `DMAxCNT_H` start timing field (`bits 12..13`).
 */
[[nodiscard]] constexpr DmaTrigger decode_arm7_timing(u32 timing, std::size_t channel) noexcept {
    switch (timing & 0x03U) {
    case 0U:
        return DmaTrigger::Immediate;
    case 1U:
        return DmaTrigger::VBlank;
    case 2U:
        return DmaTrigger::CartridgeSlot1;
    default:
        return ((channel & 1U) == 0U) ? DmaTrigger::Wireless : DmaTrigger::GbaSlot;
    }
}

/**
 * @brief Register and internal shadow-latch state of a single Nintendo DS DMA channel.
 */
struct DmaChannel {
    u32 sad = 0U;
    u32 dad = 0U;
    u16 cnt_l = 0U;
    u16 cnt_h = 0U;

    u32 internal_sad = 0U;
    u32 internal_dad = 0U;
    u32 internal_count = 0U;

    [[nodiscard]] constexpr bool is_enabled() const noexcept {
        return bits::test_bit(cnt_h, kDmaEnableBit);
    }

    [[nodiscard]] constexpr bool is_irq_enabled() const noexcept {
        return bits::test_bit(cnt_h, kDmaIrqBit);
    }

    [[nodiscard]] constexpr bool is_32bit() const noexcept {
        return bits::test_bit(cnt_h, kDmaWidthBit);
    }

    [[nodiscard]] constexpr bool is_repeat() const noexcept {
        return bits::test_bit(cnt_h, kDmaRepeatBit);
    }

    [[nodiscard]] constexpr u32 dest_ctrl() const noexcept {
        return bits::extract_bits(cnt_h, 5U, 2U);
    }

    [[nodiscard]] constexpr u32 src_ctrl() const noexcept {
        return bits::extract_bits(cnt_h, 7U, 2U);
    }

    [[nodiscard]] constexpr DmaTrigger start_timing(interrupts::CpuKind cpu,
                                                    std::size_t channel) const noexcept {
        if (cpu == interrupts::CpuKind::Arm9) {
            return decode_arm9_timing(bits::extract_bits(cnt_h, 11U, 3U));
        }
        return decode_arm7_timing(bits::extract_bits(cnt_h, 12U, 2U), channel);
    }

    /**
     * @brief Computes the effective unit count (`0` maps to channel max: `0x200000`/`0x4000`/`0x10000`).
     */
    [[nodiscard]] constexpr u32 compute_word_count(interrupts::CpuKind cpu,
                                                   std::size_t channel) const noexcept {
        if (cpu == interrupts::CpuKind::Arm9) {
            const u32 raw = (static_cast<u32>(cnt_h & 0x001FU) << 16U) | static_cast<u32>(cnt_l);
            return (raw == 0U) ? 0x200000U : raw;
        }
        if (channel == 3U) {
            const u32 raw = static_cast<u32>(cnt_l);
            return (raw == 0U) ? 0x10000U : raw;
        }
        const u32 raw = static_cast<u32>(cnt_l & 0x3FFFU);
        return (raw == 0U) ? 0x4000U : raw;
    }
};

}  // namespace elmasds::hardware::dma
