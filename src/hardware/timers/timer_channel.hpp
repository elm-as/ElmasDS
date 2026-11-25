#pragma once

#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

#include <cstddef>

namespace elmasds::hardware::timers {

/// First MMIO register address for Timer 0..3 (`TM0CNT_L`..`TM3CNT_H`).
inline constexpr Address kTimerBaseAddress = 0x04000100U;

/// Last MMIO register address for Timer 0..3 (`TM3CNT_H` high byte).
inline constexpr Address kTimerEndAddress = 0x0400010FU;

/// Number of hardware timer channels per CPU.
inline constexpr std::size_t kTimerChannelCount = 4U;

/// Writable control bitmask for Timer 0 (prescaler 0..1, IRQ enable bit 6, start bit 7).
inline constexpr u16 kTimer0ControlMask = 0x00C3U;

/// Writable control bitmask for Timers 1..3 (adds cascade bit 2).
inline constexpr u16 kTimerNControlMask = 0x00C7U;

/// Overflow ceiling for a 16-bit up-counting timer (`0x10000`).
inline constexpr TickCount kTimerCounterMax = 0x10000ULL;

/**
 * @brief Converts the 2-bit `TMxCNT_H` prescaler selector into its clock divider (`1/64/256/1024`).
 */
[[nodiscard]] constexpr u32 prescaler_divider_from_bits(u32 bits) noexcept {
    switch (bits & 0x03U) {
        case 0U:
            return 1U;
        case 1U:
            return 64U;
        case 2U:
            return 256U;
        default:
            return 1024U;
    }
}

/**
 * @brief State of a single 16-bit Nintendo DS hardware timer channel (`TMxCNT_L` / `TMxCNT_H`).
 */
struct TimerChannel {
    u16 counter = 0U;
    u16 reload = 0U;
    u16 control = 0U;
    TickCount last_sync_tick = 0U;
    TickCount next_overflow_tick = 0U;

    [[nodiscard]] constexpr bool is_running() const noexcept {
        return bits::test_bit(control, 7U);
    }

    [[nodiscard]] constexpr bool is_irq_enabled() const noexcept {
        return bits::test_bit(control, 6U);
    }

    [[nodiscard]] constexpr bool is_cascaded(std::size_t index) const noexcept {
        return index > 0U && bits::test_bit(control, 2U);
    }

    [[nodiscard]] constexpr u32 prescaler_divider() const noexcept {
        return prescaler_divider_from_bits(control);
    }

    /// Master scheduler ticks (`67.027964 MHz`) per single increment of this timer.
    [[nodiscard]] constexpr TickCount step_ticks() const noexcept {
        return static_cast<TickCount>(prescaler_divider()) * kArm7TicksPerCycle;
    }

    /// Master scheduler ticks for one full period from `reload` to overflow (`0x10000`).
    [[nodiscard]] constexpr TickCount period_ticks() const noexcept {
        return (kTimerCounterMax - static_cast<TickCount>(reload)) * step_ticks();
    }
};

}  // namespace elmasds::hardware::timers
