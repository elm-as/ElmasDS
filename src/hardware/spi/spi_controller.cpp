#include "hardware/spi/spi_controller.hpp"

#include "hardware/interrupts/irq_source.hpp"

namespace elmasds::hardware::spi {

namespace {

constexpr u16 kSpiCntWriteMask = 0xCF03U;

}  // namespace

SpiController::SpiController(interrupts::InterruptController* arm7_irq) : arm7_irq_(arm7_irq) {
    reset();
}

void SpiController::reset() {
    control_ = 0U;
    rx_data_ = 0U;
    powerman_.reset();
    firmware_.reset();
    touchscreen_.reset();
}

u8 SpiController::read8(Address address) {
    const u16 halfword = read16(address & ~1U);
    return ((address & 1U) == 0U) ? static_cast<u8>(halfword & 0xFFU)
                                  : static_cast<u8>((halfword >> 8U) & 0xFFU);
}

u16 SpiController::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned == kRegSpiCnt) {
        return control_;
    }
    if (aligned == kRegSpiData) {
        return static_cast<u16>(rx_data_);
    }
    return 0U;
}

u32 SpiController::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned == kRegSpiCnt) {
        return static_cast<u32>(control_) | (static_cast<u32>(rx_data_) << 16U);
    }
    return 0U;
}

void SpiController::write8(Address address, u8 value) {
    const Address aligned = address & ~1U;
    if (aligned == kRegSpiCnt) {
        const bool high = (address & 1U) != 0U;
        const u16 merged = high ? static_cast<u16>((control_ & 0x00FFU) | (value << 8U))
                                : static_cast<u16>((control_ & 0xFF00U) | value);
        write_control(merged);
        return;
    }
    if (aligned == kRegSpiData && (address & 1U) == 0U) {
        write_data(value);
    }
}

void SpiController::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned == kRegSpiCnt) {
        write_control(value);
        return;
    }
    if (aligned == kRegSpiData) {
        write_data(static_cast<u8>(value & 0xFFU));
    }
}

void SpiController::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned == kRegSpiCnt) {
        write_control(static_cast<u16>(value & 0xFFFFU));
        write_data(static_cast<u8>((value >> 16U) & 0xFFU));
    }
}

void SpiController::write_control(u16 value) {
    const u16 prev_device = static_cast<u16>((control_ >> 8U) & 0x03U);
    const u16 next_device = static_cast<u16>((value >> 8U) & 0x03U);
    const bool was_enabled = (control_ & (1U << 15U)) != 0U;
    const bool now_enabled = (value & (1U << 15U)) != 0U;

    if (was_enabled && (!now_enabled || prev_device != next_device)) {
        if (SpiDevice* prev = active_device(); prev != nullptr) {
            prev->release_chip_select();
        }
    }

    control_ = static_cast<u16>(value & kSpiCntWriteMask);
}

void SpiController::write_data(u8 tx_byte) {
    if ((control_ & (1U << 15U)) == 0U) {
        rx_data_ = 0U;
        return;
    }

    const bool hold_cs = (control_ & (1U << 11U)) != 0U;
    SpiDevice* device = active_device();
    rx_data_ = (device != nullptr) ? device->transfer(tx_byte, hold_cs) : 0xFFU;

    if ((control_ & (1U << 14U)) != 0U && arm7_irq_ != nullptr) {
        arm7_irq_->request_irq(interrupts::IrqSource::Spi);
    }
}

SpiDevice* SpiController::active_device() noexcept {
    switch ((control_ >> 8U) & 0x03U) {
        case 0U:
            return &powerman_;
        case 1U:
            return &firmware_;
        case 2U:
            return &touchscreen_;
        default:
            return nullptr;
    }
}

}  // namespace elmasds::hardware::spi

