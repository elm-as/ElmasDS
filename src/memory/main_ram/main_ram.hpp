#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::memory {

/**
 * @brief 4 MiB Nintendo DS Main RAM shared between ARM9 and ARM7 (`0x02000000-0x02FFFFFF`).
 */
class MainRam {
public:
    static constexpr std::size_t kSizeBytes = 4U * 1024U * 1024U;
    static constexpr Address kAddressMask = static_cast<Address>(kSizeBytes - 1U);

    MainRam();

    void reset() noexcept;

    [[nodiscard]] inline u8 read8(Address offset) const noexcept {
        return storage_[offset & kAddressMask];
    }

    [[nodiscard]] inline u16 read16(Address offset) const noexcept {
        const Address base = offset & (kAddressMask & ~1U);
        return static_cast<u16>(storage_[base]) | (static_cast<u16>(storage_[base + 1U]) << 8U);
    }

    [[nodiscard]] inline u32 read32(Address offset) const noexcept {
        const Address base = offset & (kAddressMask & ~3U);
        return static_cast<u32>(storage_[base]) | (static_cast<u32>(storage_[base + 1U]) << 8U) |
               (static_cast<u32>(storage_[base + 2U]) << 16U) |
               (static_cast<u32>(storage_[base + 3U]) << 24U);
    }

    inline void write8(Address offset, u8 value) noexcept {
        storage_[offset & kAddressMask] = value;
    }

    inline void write16(Address offset, u16 value) noexcept {
        const Address base = offset & (kAddressMask & ~1U);
        storage_[base] = static_cast<u8>(value & 0xFFU);
        storage_[base + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    }

    inline void write32(Address offset, u32 value) noexcept {
        const Address base = offset & (kAddressMask & ~3U);
        storage_[base] = static_cast<u8>(value & 0xFFU);
        storage_[base + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        storage_[base + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
        storage_[base + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
    }

    [[nodiscard]] std::span<const u8> data() const noexcept { return storage_; }
    [[nodiscard]] std::span<u8> mutable_data() noexcept { return storage_; }

private:
    std::vector<u8> storage_;
};

}  // namespace elmasds::memory
