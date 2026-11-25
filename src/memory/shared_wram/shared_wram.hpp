#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace elmasds::memory {

/**
 * @brief 32 KiB Shared WRAM dynamically partitioned between ARM9 and ARM7 via `WRAMCNT`.
 */
class SharedWram {
public:
    static constexpr std::size_t kBankSize = 16U * 1024U;
    static constexpr std::size_t kTotalSize = kBankSize * 2U;

    SharedWram() noexcept = default;

    void reset() noexcept;

    [[nodiscard]] u8 wram_cnt() const noexcept { return wram_cnt_; }
    void set_wram_cnt(u8 value) noexcept { wram_cnt_ = value & 0x03U; }

    [[nodiscard]] bool arm7_has_mapping() const noexcept { return wram_cnt_ != 0U; }

    [[nodiscard]] u8 read_arm9_8(Address offset) const noexcept;
    [[nodiscard]] u16 read_arm9_16(Address offset) const noexcept;
    [[nodiscard]] u32 read_arm9_32(Address offset) const noexcept;

    void write_arm9_8(Address offset, u8 value) noexcept;
    void write_arm9_16(Address offset, u16 value) noexcept;
    void write_arm9_32(Address offset, u32 value) noexcept;

    [[nodiscard]] u8 read_arm7_8(Address offset) const noexcept;
    [[nodiscard]] u16 read_arm7_16(Address offset) const noexcept;
    [[nodiscard]] u32 read_arm7_32(Address offset) const noexcept;

    void write_arm7_8(Address offset, u8 value) noexcept;
    void write_arm7_16(Address offset, u16 value) noexcept;
    void write_arm7_32(Address offset, u32 value) noexcept;

    [[nodiscard]] std::span<const u8> data() const noexcept { return storage_; }
    [[nodiscard]] std::span<u8> mutable_data() noexcept { return storage_; }

private:
    [[nodiscard]] std::span<const u8> arm9_slice() const noexcept;
    [[nodiscard]] std::span<u8> arm9_mutable_slice() noexcept;
    [[nodiscard]] std::span<const u8> arm7_slice() const noexcept;
    [[nodiscard]] std::span<u8> arm7_mutable_slice() noexcept;

    std::array<u8, kTotalSize> storage_{};
    u8 wram_cnt_ = 0U;
};

}  // namespace elmasds::memory
