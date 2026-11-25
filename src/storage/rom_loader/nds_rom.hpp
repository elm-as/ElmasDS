#pragma once

#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"
#include "storage/rom_loader/nds_rom_header.hpp"

#include <filesystem>
#include <span>
#include <vector>

namespace elmasds::storage::rom_loader {

/**
 * @brief Loaded Nintendo DS cartridge ROM image with validated header and binary slices.
 */
class NdsRom {
public:
    /**
     * @brief Loads and validates an `.nds` ROM from an in-memory byte buffer.
     */
    [[nodiscard]] static Result<NdsRom> load_from_bytes(std::span<const u8> bytes);

    /**
     * @brief Reads and validates an `.nds` ROM file from disk.
     */
    [[nodiscard]] static Result<NdsRom> load_from_file(const std::filesystem::path& path);

    [[nodiscard]] const NdsRomHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const u8> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const u8> arm9_binary() const noexcept;
    [[nodiscard]] std::span<const u8> arm7_binary() const noexcept;

    /**
     * @brief Reads a little-endian 32-bit word from the ROM image (`0xFFFFFFFF` if out of range).
     */
    [[nodiscard]] u32 read32(u32 offset) const noexcept;

    /**
     * @brief Returns a Nintendo DS cartridge chip ID encoding the ROM capacity.
     */
    [[nodiscard]] u32 chip_id() const noexcept;

private:
    NdsRom(NdsRomHeader header, std::vector<u8> bytes) noexcept;

    NdsRomHeader header_;
    std::vector<u8> bytes_;
};

}  // namespace elmasds::storage::rom_loader

