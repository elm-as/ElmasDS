#pragma once

#include "hardware/spi/spi_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::hardware::spi {

/// Default Nintendo DS firmware flash capacity (256 KiB).
inline constexpr std::size_t kDefaultFirmwareSize = 256U * 1024U;

/**
 * @brief Nintendo DS Power Management IC attached to SPI channel 0.
 */
class PowermanDevice final : public SpiDevice {
public:
    PowermanDevice() noexcept;

    void reset() override;
    [[nodiscard]] u8 transfer(u8 tx_byte, bool hold_chip_select) override;
    void release_chip_select() override;

    [[nodiscard]] u8 register_value(std::size_t index) const noexcept;

private:
    std::array<u8, 5U> registers_{};
    bool expecting_command_ = true;
    bool is_read_ = false;
    u8 active_register_ = 0U;
};

/**
 * @brief Nintendo DS Firmware Serial Flash (M45PE20-compatible) attached to SPI channel 1.
 */
class FirmwareFlashDevice final : public SpiDevice {
public:
    explicit FirmwareFlashDevice(std::size_t size_bytes = kDefaultFirmwareSize);

    void reset() override;
    [[nodiscard]] u8 transfer(u8 tx_byte, bool hold_chip_select) override;
    void release_chip_select() override;

    /**
     * @brief Replaces firmware flash contents with `image` (preserving default size if empty).
     */
    void load_image(std::span<const u8> image);

    [[nodiscard]] const std::vector<u8>& data() const noexcept { return storage_; }

private:
    enum class State : u8 {
        Idle = 0,
        ReadStatus,
        ReadJedecId,
        ReadAddress,
        ReadData,
        FastReadDummy,
        WriteAddress,
        WriteData,
    };

    u8 handle_command(u8 command) noexcept;

    std::vector<u8> storage_;
    State state_ = State::Idle;
    u32 address_ = 0U;
    u8 address_bytes_received_ = 0U;
    u8 jedec_index_ = 0U;
    bool write_enabled_ = false;
};

/**
 * @brief Nintendo DS Touchscreen / Auxiliary ADC (TSC2046) attached to SPI channel 2.
 */
class TouchscreenDevice final : public SpiDevice {
public:
    TouchscreenDevice() noexcept = default;

    void reset() override;
    [[nodiscard]] u8 transfer(u8 tx_byte, bool hold_chip_select) override;
    void release_chip_select() override;

    /**
     * @brief Updates the sampled pen state and 12-bit raw ADC coordinates (`0..4095`).
     */
    void set_touch_state(bool pressed, u16 raw_x, u16 raw_y) noexcept;

    [[nodiscard]] bool is_pressed() const noexcept { return pressed_; }

private:
    [[nodiscard]] u16 sample_channel(u8 channel) const noexcept;

    bool pressed_ = false;
    u16 raw_x_ = 0U;
    u16 raw_y_ = 0U;
    u16 latched_sample_ = 0U;
    u8 output_phase_ = 0U;
};

}  // namespace elmasds::hardware::spi

