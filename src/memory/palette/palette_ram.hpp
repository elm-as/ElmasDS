#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace elmasds::memory {

/**
 * @brief 2 KiB Standard Palette RAM (`0x05000000-0x050007FF`, ARM9 only; 8-bit writes ignored).
 */
class PaletteRam {
public:
    static constexpr std::size_t kSizeBytes = 2U * 1024U;

    PaletteRam() noexcept = default;

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
    std::array<u8, kSizeBytes> storage_{};
};

}  // namespace elmasds::memory
