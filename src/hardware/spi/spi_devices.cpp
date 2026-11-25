#include "hardware/spi/spi_devices.hpp"

#include <algorithm>

namespace elmasds::hardware::spi {

PowermanDevice::PowermanDevice() noexcept {
    reset();
}

void PowermanDevice::reset() {
    registers_.fill(u8{0});
    registers_[0U] = 0x0DU;  // Sound amp + backlights default enabled.
    registers_[1U] = 0x00U;  // Battery OK.
    expecting_command_ = true;
    is_read_ = false;
    active_register_ = 0U;
}

u8 PowermanDevice::transfer(u8 tx_byte, bool hold_chip_select) {
    u8 response = 0U;
    if (expecting_command_) {
        is_read_ = (tx_byte & 0x80U) != 0U;
        active_register_ = static_cast<u8>(tx_byte & 0x07U);
        expecting_command_ = false;
    } else if (active_register_ < registers_.size()) {
        if (is_read_) {
            response = registers_[active_register_];
        } else {
            registers_[active_register_] = tx_byte;
        }
        expecting_command_ = true;
    }

    if (!hold_chip_select) {
        release_chip_select();
    }
    return response;
}

void PowermanDevice::release_chip_select() {
    expecting_command_ = true;
}

u8 PowermanDevice::register_value(std::size_t index) const noexcept {
    if (index >= registers_.size()) {
        return 0U;
    }
    return registers_[index];
}

FirmwareFlashDevice::FirmwareFlashDevice(std::size_t size_bytes)
    : storage_(size_bytes == 0U ? kDefaultFirmwareSize : size_bytes, u8{0xFF}) {}

void FirmwareFlashDevice::reset() {
    state_ = State::Idle;
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;
    write_enabled_ = false;
}

void FirmwareFlashDevice::load_image(std::span<const u8> image) {
    if (image.empty()) {
        storage_.assign(kDefaultFirmwareSize, u8{0xFF});
    } else {
        storage_.assign(image.begin(), image.end());
    }
    reset();
}

u8 FirmwareFlashDevice::transfer(u8 tx_byte, bool hold_chip_select) {
    u8 response = 0U;
    switch (state_) {
        case State::Idle:
            response = handle_command(tx_byte);
            break;
        case State::ReadStatus:
            response = write_enabled_ ? 0x02U : 0x00U;
            break;
        case State::ReadJedecId: {
            static constexpr std::array<u8, 3U> kJedecId = {0x20U, 0x40U, 0x12U};
            response = (jedec_index_ < kJedecId.size()) ? kJedecId[jedec_index_++] : 0x00U;
            break;
        }
        case State::ReadAddress:
        case State::WriteAddress:
            address_ = (address_ << 8U) | tx_byte;
            ++address_bytes_received_;
            if (address_bytes_received_ == 3U) {
                state_ = (state_ == State::ReadAddress) ? State::ReadData : State::WriteData;
            }
            break;
        case State::FastReadDummy:
            state_ = State::ReadData;
            break;
        case State::ReadData:
            if (!storage_.empty()) {
                response = storage_[address_ % storage_.size()];
                ++address_;
            }
            break;
        case State::WriteData:
            if (write_enabled_ && !storage_.empty()) {
                storage_[address_ % storage_.size()] = tx_byte;
                ++address_;
            }
            break;
    }

    if (!hold_chip_select) {
        release_chip_select();
    }
    return response;
}

void FirmwareFlashDevice::release_chip_select() {
    if (state_ == State::WriteData) {
        write_enabled_ = false;
    }
    state_ = State::Idle;
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;
}

u8 FirmwareFlashDevice::handle_command(u8 command) noexcept {
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;

    switch (command) {
        case 0x03U:  // Read Data Bytes
            state_ = State::ReadAddress;
            return 0U;
        case 0x0BU:  // Fast Read
            state_ = State::FastReadDummy;
            return 0U;
        case 0x05U:  // Read Status Register
            state_ = State::ReadStatus;
            return 0U;
        case 0x06U:  // Write Enable
            write_enabled_ = true;
            return 0U;
        case 0x04U:  // Write Disable
            write_enabled_ = false;
            return 0U;
        case 0x0AU:  // Page Write
        case 0x02U:  // Page Program
            state_ = State::WriteAddress;
            return 0U;
        case 0x9FU:  // Read JEDEC ID
            state_ = State::ReadJedecId;
            return 0U;
        default:
            state_ = State::Idle;
            return 0U;
    }
}

void TouchscreenDevice::reset() {
    pressed_ = false;
    raw_x_ = 0U;
    raw_y_ = 0U;
    latched_sample_ = 0U;
    output_phase_ = 0U;
}

void TouchscreenDevice::set_touch_state(bool pressed, u16 raw_x, u16 raw_y) noexcept {
    pressed_ = pressed;
    raw_x_ = static_cast<u16>(raw_x & 0x0FFFU);
    raw_y_ = static_cast<u16>(raw_y & 0x0FFFU);
}

u8 TouchscreenDevice::transfer(u8 tx_byte, bool hold_chip_select) {
    u8 response = 0U;
    if (output_phase_ == 1U) {
        response = static_cast<u8>((latched_sample_ >> 5U) & 0x7FU);
        output_phase_ = 2U;
    } else if (output_phase_ == 2U) {
        response = static_cast<u8>((latched_sample_ << 3U) & 0xF8U);
        output_phase_ = 0U;
    }

    if ((tx_byte & 0x80U) != 0U) {
        const u8 channel = static_cast<u8>((tx_byte >> 4U) & 0x07U);
        latched_sample_ = sample_channel(channel);
        output_phase_ = 1U;
    }

    if (!hold_chip_select) {
        release_chip_select();
    }
    return response;
}

void TouchscreenDevice::release_chip_select() {
    output_phase_ = 0U;
}

u16 TouchscreenDevice::sample_channel(u8 channel) const noexcept {
    switch (channel) {
        case 1U:  // Y position
            return pressed_ ? raw_y_ : 0x0FFFU;
        case 5U:  // X position
            return pressed_ ? raw_x_ : 0x0000U;
        case 0U:  // Temperature 0
            return 0x0700U;
        case 7U:  // Temperature 1
            return 0x0800U;
        default:
            return 0x0800U;
    }
}

}  // namespace elmasds::hardware::spi

