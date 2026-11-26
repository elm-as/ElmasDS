#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace elmasds::memory {

/**
 * @brief 64 KiB private ARM7 Work RAM (`0x03800000-0x03FFFFFF`, also mirrored at `0x03000000` when WRAMCNT==0).
 */
class Arm7Wram {
public:
    static constexpr std::size_t kSizeBytes = 64U * 1024U;
    static constexpr Address kAddressMask = static_cast<Address>(kSizeBytes - 1U);

    Arm7Wram();

    void reset() noexcept;

    [[nodiscard]] inline u8 read8(Address offset) const noexcept {
        return storage_[offset & kAddressMask];
    }

    [[nodiscard]] inline u16 read16(Address offset) const noexcept {
        u16 val = 0U;
        std::memcpy(&val, storage_.data() + (offset & (kAddressMask & ~1U)), sizeof(val));
        return val;
    }

    [[nodiscard]] inline u32 read32(Address offset) const noexcept {
        u32 val = 0U;
        std::memcpy(&val, storage_.data() + (offset & (kAddressMask & ~3U)), sizeof(val));
        return val;
    }

    inline void write8(Address offset, u8 value) noexcept {
        storage_[offset & kAddressMask] = value;
    }

    inline void write16(Address offset, u16 value) noexcept {
        std::memcpy(storage_.data() + (offset & (kAddressMask & ~1U)), &value, sizeof(value));
    }

    inline void write32(Address offset, u32 value) noexcept {
        std::memcpy(storage_.data() + (offset & (kAddressMask & ~3U)), &value, sizeof(value));
    }

    [[nodiscard]] std::span<const u8> data() const noexcept { return storage_; }
    [[nodiscard]] std::span<u8> mutable_data() noexcept { return storage_; }

private:
    std::vector<u8> storage_;
};

}  // namespace elmasds::memory
