#include "hardware/interrupts/system_control_mmio.hpp"

namespace elmasds::hardware::interrupts {

SystemControlMmio::SystemControlMmio(CpuKind cpu, HaltHandler handler) noexcept
    : cpu_(cpu), handler_(handler) {}

void SystemControlMmio::reset() noexcept {
    postflg_ = 0U;
    last_power_down_mode_ = PowerDownMode::None;
}

u8 SystemControlMmio::post_flag() const noexcept {
    return postflg_;
}

PowerDownMode SystemControlMmio::last_power_down_mode() const noexcept {
    return last_power_down_mode_;
}

u8 SystemControlMmio::read8(Address address) {
    if (address == kRegPostFlg) {
        return postflg_;
    }
    return 0U;
}

u16 SystemControlMmio::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned == kRegPostFlg) {
        return static_cast<u16>(postflg_);
    }
    return 0U;
}

u32 SystemControlMmio::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned == kRegPostFlg) {
        return static_cast<u32>(postflg_);
    }
    return 0U;
}

void SystemControlMmio::write8(Address address, u8 value) {
    if (address == kRegPostFlg) {
        // Bit 0 latches to 1 until hardware reset; ARM9 also exposes R/W bit 1.
        const u8 writable_mask = (cpu_ == CpuKind::Arm9) ? 0x03U : 0x01U;
        postflg_ = static_cast<u8>((postflg_ & 0x01U) | (value & writable_mask));
        return;
    }
    if (address != kRegHaltCnt || cpu_ != CpuKind::Arm7) {
        return;
    }
    const auto mode = static_cast<PowerDownMode>((value >> 6U) & 0x03U);
    last_power_down_mode_ = mode;
    if ((mode == PowerDownMode::Halt || mode == PowerDownMode::Sleep) &&
        handler_.on_power_down != nullptr) {
        handler_.on_power_down(handler_.context, mode);
    }
}

void SystemControlMmio::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned != kRegPostFlg) {
        return;
    }
    write8(kRegPostFlg, static_cast<u8>(value & 0xFFU));
    write8(kRegHaltCnt, static_cast<u8>((value >> 8U) & 0xFFU));
}

void SystemControlMmio::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned != kRegPostFlg) {
        return;
    }
    write8(kRegPostFlg, static_cast<u8>(value & 0xFFU));
    write8(kRegHaltCnt, static_cast<u8>((value >> 8U) & 0xFFU));
}

}  // namespace elmasds::hardware::interrupts
