#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace elmasds::e2e {

inline constexpr u16 kCrc16DefaultInit = 0xFFFFU;
inline constexpr u16 kCrc16Poly = 0xA001U;
inline constexpr u16 kExpectedLogoCrc16 = 0xCF56U;
inline constexpr std::size_t kNintendoLogoSize = 156U;
inline constexpr u32 kNdsMinHeaderSize = 0x160U;
inline constexpr u32 kNdsDefaultHeaderSize = 0x4000U;
inline constexpr u32 kNdsSecureAreaEnd = 0x8000U;
inline constexpr u32 kBannerSizeV1 = 0x0840U;

struct BannerSpec {
    bool include = false;
    u16 version = 0x0001U;
    std::u16string title_japanese = u"ElmasDS";
    std::u16string title_english = u"ElmasDS Test";
    std::u16string title_french = u"ElmasDS";
    std::u16string title_german = u"ElmasDS";
    std::u16string title_italian = u"ElmasDS";
    std::u16string title_spanish = u"ElmasDS";
    u16 fill_color = 0x7FFFU;
    bool corrupt_crc = false;
};

struct NdsRomSpec {
    std::string title = "ELMASDS";
    std::string gamecode = "ELMS";
    std::string makercode = "01";
    u8 unitcode = 0x00U;
    u8 encryption_seed = 0x00U;
    u8 device_capacity = 0x00U;
    u8 rom_version = 0x00U;
    u8 autostart_flags = 0x00U;

    u32 arm9_rom_offset = 0x4000U;
    Address arm9_entry_addr = 0x02000000U;
    Address arm9_ram_addr = 0x02000000U;
    std::vector<u8> arm9_code;

    u32 arm7_rom_offset = 0x8000U;
    Address arm7_entry_addr = 0x03800000U;
    Address arm7_ram_addr = 0x03800000U;
    std::vector<u8> arm7_code;

    u32 normal_romctrl = 0x00416017U;
    u32 key1_romctrl = 0x081808F8U;
    u32 header_size = kNdsDefaultHeaderSize;

    BannerSpec banner{};
    std::vector<u8> extra_rom_payload;
    u32 extra_rom_offset = 0U;
    std::optional<std::array<u8, kNintendoLogoSize>> custom_logo;
    u32 min_total_size = 0U;

    bool corrupt_header_crc = false;
    bool corrupt_logo_crc = false;
    bool corrupt_secure_crc = false;
};

[[nodiscard]] u16 compute_crc16(std::span<const u8> data,
                                u16 initial = kCrc16DefaultInit) noexcept;

[[nodiscard]] std::array<u8, kNintendoLogoSize> synthesize_valid_logo() noexcept;

[[nodiscard]] std::vector<u8> words_to_le_bytes(std::span<const u32> words);

[[nodiscard]] std::vector<u8> build_valid_nds_rom(const NdsRomSpec& spec = {});

void corrupt_header_crc(std::span<u8> rom) noexcept;

void corrupt_logo_crc(std::span<u8> rom) noexcept;

void corrupt_secure_crc(std::span<u8> rom) noexcept;

[[nodiscard]] std::vector<u8> truncate_rom(std::vector<u8> rom, std::size_t new_size);

void patch_rom_u32(std::span<u8> rom, u32 offset, u32 value) noexcept;

}  // namespace elmasds::e2e
