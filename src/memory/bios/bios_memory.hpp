#pragma once

#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::memory {

/**
 * @brief Read-only BIOS storage supporting ARM9 (4 KiB) and ARM7 (16 KiB + read protection).
 */
class BiosMemory {
public:
    static constexpr std::size_t kArm9BiosSize = 4U * 1024U;
    static constexpr std::size_t kArm7BiosSize = 16U * 1024U;

    explicit BiosMemory(std::size_t size_bytes, bool enforce_pc_protection = false);

    Status load(std::span<const u8> image) noexcept;
    void clear() noexcept;

    [[nodiscard]] u8 read8(Address offset, Address current_pc = 0U) const noexcept;
    [[nodiscard]] u16 read16(Address offset, Address current_pc = 0U) const noexcept;
    [[nodiscard]] u32 read32(Address offset, Address current_pc = 0U) const noexcept;

    [[nodiscard]] bool is_loaded() const noexcept { return loaded_; }
    [[nodiscard]] std::size_t size_bytes() const noexcept { return storage_.size(); }

private:
    [[nodiscard]] bool is_access_allowed(Address offset, Address current_pc) const noexcept;

    std::vector<u8> storage_;
    bool enforce_pc_protection_ = false;
    bool loaded_ = false;
};

}  // namespace elmasds::memory
