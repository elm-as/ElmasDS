#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::memory {

/**
 * @brief Identifiers for the nine physical Nintendo DS VRAM banks (A-I, 656 KiB total).
 */
enum class VramBankId : u8 {
    A = 0,
    B = 1,
    C = 2,
    D = 3,
    E = 4,
    F = 5,
    G = 6,
    H = 7,
    I = 8,
};

/**
 * @brief Manages the 9 Nintendo DS VRAM banks (`656 KiB`) and `VRAMCNT_A..I` dynamic mappings.
 *
 * Responsibility:
 *   Owns physical storage for VRAM banks A-I, interprets `VRAMCNT_A..I` control bytes,
 *   routes ARM9 CPU reads/writes across LCDC, Engine A/B BG, and Engine A/B OBJ spaces
 *   (ignoring 8-bit writes per NDS hardware), and routes ARM7 VRAM-WRAM (`C`/`D`, MST=2)
 *   plus `VRAMSTAT`.
 */
class VramController {
public:
    static constexpr std::size_t kBankCount = 9U;

    VramController();

    void reset() noexcept;

    void write_vram_cnt(VramBankId bank, u8 value) noexcept;
    [[nodiscard]] u8 read_vram_cnt(VramBankId bank) const noexcept;
    [[nodiscard]] u8 read_vram_stat() const noexcept;

    [[nodiscard]] u8 read_arm9_8(Address address) const noexcept;
    [[nodiscard]] u16 read_arm9_16(Address address) const noexcept;
    [[nodiscard]] u32 read_arm9_32(Address address) const noexcept;

    void write_arm9_8(Address address, u8 value) noexcept;
    void write_arm9_16(Address address, u16 value) noexcept;
    void write_arm9_32(Address address, u32 value) noexcept;

    [[nodiscard]] u8 read_arm7_8(Address address) const noexcept;
    [[nodiscard]] u16 read_arm7_16(Address address) const noexcept;
    [[nodiscard]] u32 read_arm7_32(Address address) const noexcept;

    void write_arm7_8(Address address, u8 value) noexcept;
    void write_arm7_16(Address address, u16 value) noexcept;
    void write_arm7_32(Address address, u32 value) noexcept;

    [[nodiscard]] std::span<const u8> bank_data(VramBankId bank) const noexcept;
    [[nodiscard]] std::span<u8> mutable_bank_data(VramBankId bank) noexcept {
        return banks_[static_cast<std::size_t>(bank)];
    }

private:
    struct BankMapping {
        Address start = 0;
        Address size = 0;
    };

    [[nodiscard]] BankMapping arm9_mapping(std::size_t index, Address address) const noexcept;
    [[nodiscard]] BankMapping arm7_mapping(std::size_t index) const noexcept;
    [[nodiscard]] bool matches_arm9(std::size_t index,
                                    Address address,
                                    Address& bank_offset) const noexcept;
    [[nodiscard]] bool matches_arm7(std::size_t index,
                                    Address address,
                                    Address& bank_offset) const noexcept;

    std::array<std::vector<u8>, kBankCount> banks_;
    std::array<u8, kBankCount> vram_cnt_{};
};

}  // namespace elmasds::memory
