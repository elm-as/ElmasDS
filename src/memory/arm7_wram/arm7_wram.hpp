#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::memory {

/**
 * @brief 64 KiB private ARM7 Work RAM (`0x03800000-0x03FFFFFF`, also mirrored at `0x03000000` when WRAMCNT==0).
 */
class Arm7Wram {
public:
    static constexpr std::size_t kSizeBytes = 64U * 1024U;

    Arm7Wram();

    void reset() noexcept;

    [[nodiscard]] u8 read8(Address offset) const noexcept;
    [[nodiscard]] u16 read16(Address offset) const noexcept;
    [[nodiscard]] u32 read32(Address offset) const noexcept;

    void write8(Address offset, u8 value) noexcept;
    void write16(Address offset, u16 value) noexcept;
    void write32(Address offset, u32 value) noexcept;

    [[nodiscard]] std::span<const u8> data() const noexcept { return storage_; }
    [[nodiscard]] std::span<u8> mutable_data() noexcept { return storage_; }

private:
    std::vector<u8> storage_;
};

}  // namespace elmasds::memory
