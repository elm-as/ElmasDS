#include "hardware/wifi/wifi_controller.hpp"

namespace elmasds::hardware::wifi {
namespace {

constexpr u16 kWifiHardwareId = 0x1440U;
constexpr u8 kBasebandChipId = 0x6DU;
constexpr Address kWifiRamStart = 0x4000U;
constexpr Address kWifiRamEnd = 0x5FFFU;

}  // namespace

void WifiController::reset() noexcept {
    wifi_ram_.fill(0U);
    regs_.fill(0U);
    bb_regs_.fill(0U);
    bb_regs_[0x00U] = kBasebandChipId;
    regs_[0x000U] = kWifiHardwareId;
    regs_[0x03CU >> 1U] = 0x0200U;  // W_POWERSTATE initial hardware bits.
    regs_[0x19CU >> 1U] = 0x0009U;  // W_RF_STATUS idle.
    random_lfsr_ = 0x02A5U;
}

u16 WifiController::read_reg16(Address offset) noexcept {
    const Address reg = offset & 0x0FFEU;
    if (reg == 0x000U) { return kWifiHardwareId; }
    if (reg == 0x15EU || reg == 0x180U) { return 0U; }
    if (reg == 0x044U) {
        const u16 bit = static_cast<u16>(((random_lfsr_ >> 10U) ^ (random_lfsr_ >> 8U)) & 1U);
        random_lfsr_ = static_cast<u16>(((random_lfsr_ << 1U) | bit) & 0x07FFU);
        return random_lfsr_;
    }
    return regs_[reg >> 1U];
}

void WifiController::write_reg16(Address offset, u16 value) noexcept {
    const Address reg = offset & 0x0FFEU;
    if (reg == 0x000U) { return; }
    if (reg == 0x010U) {
        regs_[reg >> 1U] &= static_cast<u16>(~value);
        return;
    }
    if (reg == 0x03CU) {
        u16 state = regs_[0x03CU >> 1U];
        if ((value & 0x0002U) != 0U) { state |= 0x0200U; }
        else if ((value & 0x0001U) != 0U) { state &= static_cast<u16>(~0x0200U); }
        regs_[0x03CU >> 1U] = static_cast<u16>((state & 0x0200U) | (value & 0x0003U));
        return;
    }
    if (reg == 0x040U) {
        regs_[reg >> 1U] = value;
        if ((value & 0x8000U) != 0U) {
            const u16 ps = regs_[0x03CU >> 1U] & static_cast<u16>(~0x0200U);
            regs_[0x03CU >> 1U] = ((value & 0x0001U) != 0U) ? static_cast<u16>(ps | 0x0200U) : ps;
        }
        return;
    }
    if (reg == 0x158U) {
        regs_[reg >> 1U] = value;
        const u16 dir = (value >> 12U) & 0xFU;
        const u8 bb_idx = static_cast<u8>(value & 0xFFU);
        if (dir == 5U && bb_idx != 0x00U) {
            bb_regs_[bb_idx] = static_cast<u8>(regs_[0x15AU >> 1U] & 0xFFU);
        } else if (dir == 6U) {
            regs_[0x15CU >> 1U] = bb_regs_[bb_idx];
        }
        return;
    }
    regs_[reg >> 1U] = value;
}

u8 WifiController::read8(Address address) noexcept {
    const Address offset = address & 0x7FFFU;
    if (offset >= kWifiRamStart && offset <= kWifiRamEnd) {
        return wifi_ram_[offset - kWifiRamStart];
    }
    const u16 val = read16(address & ~1U);
    return static_cast<u8>((val >> ((address & 1U) * 8U)) & 0xFFU);
}

u16 WifiController::read16(Address address) noexcept {
    const Address offset = address & 0x7FFEU;
    if (offset >= kWifiRamStart && offset <= kWifiRamEnd) {
        const Address ram_off = offset - kWifiRamStart;
        return static_cast<u16>(wifi_ram_[ram_off]) |
               (static_cast<u16>(wifi_ram_[ram_off + 1U]) << 8U);
    }
    if (offset < 0x1000U) {
        return read_reg16(offset);
    }
    return 0U;
}

u32 WifiController::read32(Address address) noexcept {
    const Address aligned = address & ~3U;
    return static_cast<u32>(read16(aligned)) | (static_cast<u32>(read16(aligned + 2U)) << 16U);
}

void WifiController::write8(Address address, u8 value) noexcept {
    const Address offset = address & 0x7FFFU;
    if (offset >= kWifiRamStart && offset <= kWifiRamEnd) {
        wifi_ram_[offset - kWifiRamStart] = value;
        return;
    }
    if (offset < 0x1000U) {
        const Address aligned = address & ~1U;
        const u16 cur = read_reg16(aligned & 0x0FFEU);
        const u16 next = ((address & 1U) == 0U)
                             ? static_cast<u16>((cur & 0xFF00U) | value)
                             : static_cast<u16>((cur & 0x00FFU) | (static_cast<u16>(value) << 8U));
        write_reg16(aligned & 0x0FFEU, next);
    }
}

void WifiController::write16(Address address, u16 value) noexcept {
    const Address offset = address & 0x7FFEU;
    if (offset >= kWifiRamStart && offset <= kWifiRamEnd) {
        const Address ram_off = offset - kWifiRamStart;
        wifi_ram_[ram_off] = static_cast<u8>(value & 0xFFU);
        wifi_ram_[ram_off + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        return;
    }
    if (offset < 0x1000U) {
        write_reg16(offset, value);
    }
}

void WifiController::write32(Address address, u32 value) noexcept {
    const Address aligned = address & ~3U;
    write16(aligned, static_cast<u16>(value & 0xFFFFU));
    write16(aligned + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

}  // namespace elmasds::hardware::wifi
