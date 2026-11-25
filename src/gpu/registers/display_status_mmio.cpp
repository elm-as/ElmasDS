#include "gpu/registers/display_status_mmio.hpp"

namespace elmasds::gpu {

DisplayStatusMmio::DisplayStatusMmio(const u16& shared_vcount) noexcept
    : shared_vcount_(shared_vcount) {}

void DisplayStatusMmio::reset() noexcept {
    dispstat_ = 0U;
}

void DisplayStatusMmio::set_vblank(bool active) noexcept {
    if (active) {
        dispstat_ |= 0x0001U;
    } else {
        dispstat_ &= static_cast<u16>(~0x0001U);
    }
}

void DisplayStatusMmio::set_hblank(bool active) noexcept {
    if (active) {
        dispstat_ |= 0x0002U;
    } else {
        dispstat_ &= static_cast<u16>(~0x0002U);
    }
}

u16 DisplayStatusMmio::lyc() const noexcept {
    return static_cast<u16>(((dispstat_ >> 8U) & 0xFFU) | ((dispstat_ & 0x0080U) << 1U));
}

void DisplayStatusMmio::update_vcount_match() noexcept {
    if (shared_vcount_ == lyc()) {
        dispstat_ |= 0x0004U;
    } else {
        dispstat_ &= static_cast<u16>(~0x0004U);
    }
}

u8 DisplayStatusMmio::read8(Address address) {
    const u16 half = read16(address & ~1U);
    return ((address & 1U) == 0U) ? static_cast<u8>(half & 0xFFU)
                                  : static_cast<u8>((half >> 8U) & 0xFFU);
}

u16 DisplayStatusMmio::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned == kRegDispStat) {
        return dispstat_;
    }
    if (aligned == kRegVCount) {
        return shared_vcount_;
    }
    return 0U;
}

u32 DisplayStatusMmio::read32(Address address) {
    if ((address & ~3U) == kRegDispStat) {
        return static_cast<u32>(dispstat_) | (static_cast<u32>(shared_vcount_) << 16U);
    }
    return 0U;
}

void DisplayStatusMmio::write8(Address address, u8 value) {
    const Address aligned = address & ~1U;
    if (aligned != kRegDispStat) {
        return;
    }
    const u16 merged = ((address & 1U) == 0U)
                           ? static_cast<u16>((dispstat_ & 0xFF00U) | value)
                           : static_cast<u16>((dispstat_ & 0x00FFU) | (static_cast<u16>(value) << 8U));
    write16(kRegDispStat, merged);
}

void DisplayStatusMmio::write16(Address address, u16 value) {
    if ((address & ~1U) != kRegDispStat) {
        return;
    }
    // Bits 0..2 are read-only status flags; bits 3..5 and 7..15 are writable.
    dispstat_ = static_cast<u16>((dispstat_ & 0x0007U) | (value & 0xFFB8U));
    update_vcount_match();
}

void DisplayStatusMmio::write32(Address address, u32 value) {
    if ((address & ~3U) == kRegDispStat) {
        write16(kRegDispStat, static_cast<u16>(value & 0xFFFFU));
    }
}

u8 PowCnt1Mmio::read8(Address address) {
    const u16 half = read16(address & ~1U);
    return ((address & 1U) == 0U) ? static_cast<u8>(half & 0xFFU)
                                  : static_cast<u8>((half >> 8U) & 0xFFU);
}

u16 PowCnt1Mmio::read16(Address address) {
    return ((address & ~1U) == kPowCnt1MmioStart) ? value_ : 0U;
}

u32 PowCnt1Mmio::read32(Address address) {
    return ((address & ~3U) == kPowCnt1MmioStart) ? static_cast<u32>(value_) : 0U;
}

void PowCnt1Mmio::write8(Address address, u8 value) {
    if ((address & ~1U) != kPowCnt1MmioStart) {
        return;
    }
    const u16 merged = ((address & 1U) == 0U)
                           ? static_cast<u16>((value_ & 0xFF00U) | value)
                           : static_cast<u16>((value_ & 0x00FFU) | (static_cast<u16>(value) << 8U));
    set_value(merged);
}

void PowCnt1Mmio::write16(Address address, u16 value) {
    if ((address & ~1U) == kPowCnt1MmioStart) {
        set_value(value);
    }
}

void PowCnt1Mmio::write32(Address address, u32 value) {
    if ((address & ~3U) == kPowCnt1MmioStart) {
        set_value(static_cast<u16>(value & 0xFFFFU));
    }
}

}  // namespace elmasds::gpu
