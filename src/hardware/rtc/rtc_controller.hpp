#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::hardware::rtc {

/// ARM7 RTC MMIO register address (`0x04000138..0x04000139`).
inline constexpr Address kRtcMmioStart = 0x04000138U;
inline constexpr Address kRtcMmioEnd = 0x04000139U;

/**
 * @brief Calendar date and time snapshot stored by `RtcController` (plain decimal values).
 */
struct RtcDateTime {
    u8 year = 26U;      ///< Years since 2000 (`0..99`).
    u8 month = 1U;      ///< Month (`1..12`).
    u8 day = 1U;        ///< Day of month (`1..31`).
    u8 weekday = 0U;    ///< Day of week (`0..6`).
    u8 hour = 0U;       ///< Hour (`0..23`).
    u8 minute = 0U;     ///< Minute (`0..59`).
    u8 second = 0U;     ///< Second (`0..59`).
};

/**
 * @brief Converts a decimal value (`0..99`) into packed BCD.
 */
[[nodiscard]] constexpr u8 to_bcd(u8 decimal_value) noexcept {
    const u8 clamped = (decimal_value > 99U) ? 99U : decimal_value;
    return static_cast<u8>(((clamped / 10U) << 4U) | (clamped % 10U));
}

/**
 * @brief Converts a packed BCD byte into a decimal value (`0..99`).
 */
[[nodiscard]] constexpr u8 from_bcd(u8 bcd_value) noexcept {
    const u8 tens = static_cast<u8>((bcd_value >> 4U) & 0x0FU);
    const u8 ones = static_cast<u8>(bcd_value & 0x0FU);
    return static_cast<u8>(tens * 10U + ones);
}

/**
 * @brief ARM7 Seiko S-3511A serial Real-Time Clock controller (`0x04000138`).
 *
 * Responsibility:
 *   Implements the 3-wire serial bit-bang protocol (`SIO`, `SCK`, `CS`), command decoding,
 *   Status1/Status2 registers, deterministic BCD Date/Time reads & writes, and free register.
 */
class RtcController final : public memory::MmioDevice {
public:
    RtcController() noexcept;

    void reset() noexcept;

    /**
     * @brief Sets the RTC calendar date/time deterministically (no wall-clock dependency).
     */
    void set_date_time(const RtcDateTime& date_time) noexcept;

    [[nodiscard]] RtcDateTime date_time() const noexcept { return date_time_; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    enum class Phase : u8 {
        Idle = 0,
        Command,
        ReadData,
        WriteData,
    };

    void handle_clock_edge(u8 new_reg) noexcept;
    void shift_in_command_bit(u8 bit) noexcept;
    void shift_in_write_bit(u8 bit) noexcept;
    void decode_command(u8 raw_command) noexcept;
    void load_read_buffer(u8 reg_index) noexcept;
    void commit_write_buffer() noexcept;
    [[nodiscard]] static u8 reverse_bits8(u8 value) noexcept;
    [[nodiscard]] static std::size_t register_byte_length(u8 reg_index) noexcept;

    RtcDateTime date_time_{};
    u8 status1_ = 0x02U;  // 24-hour mode enabled.
    u8 status2_ = 0x00U;
    u8 clock_adjust_ = 0x00U;
    u8 free_register_ = 0x00U;
    std::array<u8, 3U> alarm1_{};
    std::array<u8, 3U> alarm2_{};

    u8 io_reg_ = 0U;
    Phase phase_ = Phase::Idle;
    u8 active_reg_ = 0U;
    u8 shift_byte_ = 0U;
    u8 bit_count_ = 0U;
    std::size_t byte_index_ = 0U;
    std::size_t transfer_length_ = 0U;
    std::array<u8, 7U> transfer_buffer_{};
};

}  // namespace elmasds::hardware::rtc
