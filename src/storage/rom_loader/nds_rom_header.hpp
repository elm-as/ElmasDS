#pragma once

#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <cstddef>
#include <span>
#include <string>

namespace elmasds::storage::rom_loader {

inline constexpr std::size_t kNdsMinHeaderSize = 0x160U;
inline constexpr std::size_t kNdsFullHeaderSize = 0x200U;
inline constexpr std::size_t kNdsLogoOffset = 0x0C0U;
inline constexpr std::size_t kNdsLogoSize = 156U;

/**
 * @brief Parsed Nintendo DS cartridge header (`0x000..0x1FF`).
 */
struct NdsRomHeader {
    std::string title;
    std::string gamecode;
    std::string makercode;
    u8 unitcode = 0U;
    u8 encryption_seed = 0U;
    u8 device_capacity = 0U;
    u8 rom_version = 0U;
    u8 autostart_flags = 0U;

    u32 arm9_rom_offset = 0U;
    Address arm9_entry_address = 0U;
    Address arm9_ram_address = 0U;
    u32 arm9_size = 0U;

    u32 arm7_rom_offset = 0U;
    Address arm7_entry_address = 0U;
    Address arm7_ram_address = 0U;
    u32 arm7_size = 0U;

    u32 fnt_offset = 0U;
    u32 fnt_size = 0U;
    u32 fat_offset = 0U;
    u32 fat_size = 0U;

    u32 normal_romctrl = 0U;
    u32 key1_romctrl = 0U;
    u32 banner_offset = 0U;
    u16 secure_area_crc = 0U;
    u32 header_size = 0U;
    u16 logo_crc = 0U;
    u16 header_crc = 0U;

    /**
     * @brief Returns nominal cartridge capacity in bytes (`128 KiB << device_capacity`).
     */
    [[nodiscard]] u64 nominal_capacity_bytes() const noexcept {
        const u8 shift = (device_capacity > 20U) ? 20U : device_capacity;
        return u64{128U * 1024U} << shift;
    }
};

/**
 * @brief Computes Nintendo DS CRC-16 (Modbus polynomial `0xA001`, initial `0xFFFF`).
 */
[[nodiscard]] u16 compute_nds_crc16(std::span<const u8> data) noexcept;

/**
 * @brief Parses and validates an `.nds` header including header/logo CRCs and binary bounds.
 * @param rom_bytes Raw ROM image bytes.
 * @return Parsed `NdsRomHeader` on success, or `ErrorCode::CorruptData` / `UnsupportedFormat`.
 */
[[nodiscard]] Result<NdsRomHeader> parse_nds_header(std::span<const u8> rom_bytes);

}  // namespace elmasds::storage::rom_loader

