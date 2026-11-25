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

    MainRam();

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
