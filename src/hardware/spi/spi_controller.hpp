#pragma once

#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/spi/spi_device.hpp"
#include "hardware/spi/spi_devices.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>

namespace elmasds::hardware::spi {

/// ARM7 SPI MMIO range (`SPICNT` at `0x040001C0`, `SPIDATA` at `0x040001C2`).
inline constexpr Address kSpiMmioStart = 0x040001C0U;
inline constexpr Address kSpiMmioEnd = 0x040001C3U;
inline constexpr Address kRegSpiCnt = 0x040001C0U;
inline constexpr Address kRegSpiData = 0x040001C2U;

/**
 * @brief ARM7 SPI bus controller (`SPICNT`/`SPIDATA`) routing to Powerman, Firmware, and Touch.
 *
 * Responsibility:
 *   Decodes `SPICNT` device selection, chip-select hold, enable, and IRQ flags; exchanges bytes
 *   with the selected `SpiDevice` on `SPIDATA` writes; raises `IrqSource::Spi` when enabled.
 */
class SpiController final : public memory::MmioDevice {
public:
    explicit SpiController(interrupts::InterruptController* arm7_irq = nullptr);

    SpiController(const SpiController&) = delete;
    SpiController& operator=(const SpiController&) = delete;
    SpiController(SpiController&&) = delete;
    SpiController& operator=(SpiController&&) = delete;

    void reset();

    [[nodiscard]] PowermanDevice& powerman() noexcept { return powerman_; }
    [[nodiscard]] const PowermanDevice& powerman() const noexcept { return powerman_; }

    [[nodiscard]] FirmwareFlashDevice& firmware() noexcept { return firmware_; }
    [[nodiscard]] const FirmwareFlashDevice& firmware() const noexcept { return firmware_; }

    [[nodiscard]] TouchscreenDevice& touchscreen() noexcept { return touchscreen_; }
    [[nodiscard]] const TouchscreenDevice& touchscreen() const noexcept { return touchscreen_; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    void write_control(u16 value);
    void write_data(u8 tx_byte);
    [[nodiscard]] SpiDevice* active_device() noexcept;

    interrupts::InterruptController* arm7_irq_;
    PowermanDevice powerman_;
    FirmwareFlashDevice firmware_;
    TouchscreenDevice touchscreen_;
    u16 control_ = 0U;
    u8 rx_data_ = 0U;
};

}  // namespace elmasds::hardware::spi

