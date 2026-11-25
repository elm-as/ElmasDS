#include "e2e/nds_rom_builder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace elmasds::e2e {
namespace {

constexpr u32 kHeaderTitleOffset = 0x000U;
constexpr u32 kHeaderGamecodeOffset = 0x00CU;
constexpr u32 kHeaderMakercodeOffset = 0x010U;
constexpr u32 kHeaderUnitcodeOffset = 0x012U;
constexpr u32 kHeaderSeedOffset = 0x013U;
constexpr u32 kHeaderCapacityOffset = 0x014U;
constexpr u32 kHeaderRomVersionOffset = 0x01EU;
constexpr u32 kHeaderAutostartOffset = 0x01FU;

constexpr u32 kHeaderArm9Offset = 0x020U;
constexpr u32 kHeaderArm7Offset = 0x030U;
constexpr u32 kHeaderNormalRomctrlOffset = 0x060U;
constexpr u32 kHeaderKey1RomctrlOffset = 0x064U;
constexpr u32 kHeaderBannerOffset = 0x068U;
constexpr u32 kHeaderSecureCrcOffset = 0x06CU;
constexpr u32 kHeaderTotalSizeOffset = 0x080U;
constexpr u32 kHeaderSizeOffset = 0x084U;
constexpr u32 kHeaderLogoOffset = 0x0C0U;
constexpr u32 kHeaderLogoCrcOffset = 0x15CU;
constexpr u32 kHeaderCrcOffset = 0x15EU;

void write_le16(std::span<u8> dst, u32 offset, u16 value) noexcept {
    if (static_cast<std::size_t>(offset) + 2U > dst.size()) {
        return;
    }
    dst[offset] = static_cast<u8>(value & 0xFFU);
    dst[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

void write_le32(std::span<u8> dst, u32 offset, u32 value) noexcept {
    if (static_cast<std::size_t>(offset) + 4U > dst.size()) {
        return;
    }
    dst[offset] = static_cast<u8>(value & 0xFFU);
    dst[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    dst[offset + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
    dst[offset + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
}

void write_ascii_padded(std::span<u8> field, std::string_view text) noexcept {
    std::fill(field.begin(), field.end(), u8{0});
    const std::size_t copy_len = std::min(field.size(), text.size());
    for (std::size_t i = 0; i < copy_len; ++i) {
        field[i] = static_cast<u8>(text[i]);
    }
}

[[nodiscard]] u16 unstep_crc16_byte(u16 crc) noexcept {
    for (int bit = 0; bit < 8; ++bit) {
        const bool msb = (crc & 0x8000U) != 0U;
        crc = msb ? static_cast<u16>(((crc ^ kCrc16Poly) << 1U) | 1U)
                  : static_cast<u16>(crc << 1U);
    }
    return crc;
}

[[nodiscard]] u32 banner_byte_size(u16 version) noexcept {
    if (version == 0x0103U) {
        return 0x23C0U;
    }
    if (version == 0x0003U) {
        return 0x0A40U;
    }
    if (version == 0x0002U) {
        return 0x0940U;
    }
    return kBannerSizeV1;
}

[[nodiscard]] u64 align_up_512(u64 value) noexcept {
    return (value + 0x1FFULL) & ~0x1FFULL;
}

[[nodiscard]] u32 compute_banner_offset(const NdsRomSpec& spec) {
    if (!spec.banner.include) {
        return 0U;
    }
    const u64 arm9_end = static_cast<u64>(spec.arm9_rom_offset) + spec.arm9_code.size();
    const u64 arm7_end = static_cast<u64>(spec.arm7_rom_offset) + spec.arm7_code.size();
    const u64 extra_end = spec.extra_rom_payload.empty()
                              ? 0ULL
                              : static_cast<u64>(spec.extra_rom_offset) +
                                    spec.extra_rom_payload.size();
    const u64 min_offset = std::max({static_cast<u64>(kNdsSecureAreaEnd),
                                     static_cast<u64>(spec.header_size), arm9_end, arm7_end,
                                     extra_end});
    const u64 aligned = align_up_512(min_offset);
    if (aligned + banner_byte_size(spec.banner.version) > std::numeric_limits<u32>::max()) {
        throw std::out_of_range("Banner offset exceeds 32-bit NDS ROM range");
    }
    return static_cast<u32>(aligned);
}

[[nodiscard]] std::size_t compute_required_rom_size(const NdsRomSpec& spec, u32 banner_offset) {
    u64 required = std::max(spec.header_size, kNdsMinHeaderSize);
    if (spec.arm9_rom_offset >= kNdsDefaultHeaderSize &&
        spec.arm9_rom_offset < kNdsSecureAreaEnd) {
        required = std::max<u64>(required, kNdsSecureAreaEnd);
    }
    required = std::max<u64>(
        required, static_cast<u64>(spec.arm9_rom_offset) + spec.arm9_code.size());
    required = std::max<u64>(
        required, static_cast<u64>(spec.arm7_rom_offset) + spec.arm7_code.size());
    if (spec.banner.include) {
        required = std::max<u64>(
            required, static_cast<u64>(banner_offset) + banner_byte_size(spec.banner.version));
    }
    if (!spec.extra_rom_payload.empty()) {
        required = std::max<u64>(
            required, static_cast<u64>(spec.extra_rom_offset) + spec.extra_rom_payload.size());
    }
    required = std::max<u64>(required, spec.min_total_size);
    if (required > std::numeric_limits<u32>::max()) {
        throw std::out_of_range("NDS ROM size exceeds 32-bit range");
    }
    return static_cast<std::size_t>(required);
}

void write_utf16_title(std::span<u8> slot, const std::u16string& title) noexcept {
    std::fill(slot.begin(), slot.end(), u8{0});
    const std::size_t max_chars = slot.size() / 2U;
    const std::size_t count = std::min(max_chars, title.size());
    for (std::size_t i = 0; i < count; ++i) {
        write_le16(slot, static_cast<u32>(i * 2U), static_cast<u16>(title[i]));
    }
}

void write_banner_section(std::span<u8> rom, u32 offset, const BannerSpec& banner) noexcept {
    const u32 total_size = banner_byte_size(banner.version);
    if (static_cast<std::size_t>(offset) + total_size > rom.size()) {
        return;
    }
    const std::span<u8> area = rom.subspan(offset, total_size);
    write_le16(area, 0x000U, banner.version);

    for (u32 i = 0x020U; i < 0x220U; ++i) {
        area[i] = static_cast<u8>(0x11U + ((i - 0x020U) & 0x0EU));
    }
    write_le16(area, 0x220U, 0x0000U);
    write_le16(area, 0x222U, banner.fill_color);
    for (u32 i = 2U; i < 16U; ++i) {
        write_le16(area, 0x220U + (i * 2U), static_cast<u16>(0x0421U * i));
    }

    write_utf16_title(area.subspan(0x240U, 0x100U), banner.title_japanese);
    write_utf16_title(area.subspan(0x340U, 0x100U), banner.title_english);
    write_utf16_title(area.subspan(0x440U, 0x100U), banner.title_french);
    write_utf16_title(area.subspan(0x540U, 0x100U), banner.title_german);
    write_utf16_title(area.subspan(0x640U, 0x100U), banner.title_italian);
    write_utf16_title(area.subspan(0x740U, 0x100U), banner.title_spanish);

    const u16 crc_mask = banner.corrupt_crc ? 0xFFFFU : 0x0000U;
    write_le16(area, 0x002U,
               static_cast<u16>(compute_crc16(area.subspan(0x020U, 0x820U)) ^ crc_mask));
    if (total_size >= 0x0940U) {
        write_le16(area, 0x004U,
                   static_cast<u16>(compute_crc16(area.subspan(0x020U, 0x920U)) ^ crc_mask));
    }
    if (total_size >= 0x0A40U) {
        write_le16(area, 0x006U,
                   static_cast<u16>(compute_crc16(area.subspan(0x020U, 0xA20U)) ^ crc_mask));
    }
    if (total_size >= 0x23C0U) {
        write_le16(area, 0x008U,
                   static_cast<u16>(compute_crc16(area.subspan(0x1240U, 0x1180U)) ^ crc_mask));
    }
}

void write_header_fields(std::span<u8> rom, const NdsRomSpec& spec, u32 banner_offset) noexcept {
    write_ascii_padded(rom.subspan(kHeaderTitleOffset, 12U), spec.title);
    write_ascii_padded(rom.subspan(kHeaderGamecodeOffset, 4U), spec.gamecode);
    write_ascii_padded(rom.subspan(kHeaderMakercodeOffset, 2U), spec.makercode);

    rom[kHeaderUnitcodeOffset] = spec.unitcode;
    rom[kHeaderSeedOffset] = spec.encryption_seed;
    rom[kHeaderCapacityOffset] = spec.device_capacity;
    rom[kHeaderRomVersionOffset] = spec.rom_version;
    rom[kHeaderAutostartOffset] = spec.autostart_flags;

    write_le32(rom, kHeaderArm9Offset, spec.arm9_rom_offset);
    write_le32(rom, kHeaderArm9Offset + 4U, spec.arm9_entry_addr);
    write_le32(rom, kHeaderArm9Offset + 8U, spec.arm9_ram_addr);
    write_le32(rom, kHeaderArm9Offset + 12U, static_cast<u32>(spec.arm9_code.size()));

    write_le32(rom, kHeaderArm7Offset, spec.arm7_rom_offset);
    write_le32(rom, kHeaderArm7Offset + 4U, spec.arm7_entry_addr);
    write_le32(rom, kHeaderArm7Offset + 8U, spec.arm7_ram_addr);
    write_le32(rom, kHeaderArm7Offset + 12U, static_cast<u32>(spec.arm7_code.size()));

    write_le32(rom, kHeaderNormalRomctrlOffset, spec.normal_romctrl);
    write_le32(rom, kHeaderKey1RomctrlOffset, spec.key1_romctrl);
    write_le32(rom, kHeaderBannerOffset, banner_offset);
    write_le32(rom, kHeaderTotalSizeOffset, static_cast<u32>(rom.size()));
    write_le32(rom, kHeaderSizeOffset, spec.header_size);

    const std::array<u8, kNintendoLogoSize> logo =
        spec.custom_logo.has_value() ? *spec.custom_logo : synthesize_valid_logo();
    std::copy(logo.begin(), logo.end(), rom.begin() + kHeaderLogoOffset);
}

void write_payloads(std::span<u8> rom, const NdsRomSpec& spec, u32 banner_offset) noexcept {
    if (!spec.arm9_code.empty()) {
        std::copy(spec.arm9_code.begin(), spec.arm9_code.end(), rom.begin() + spec.arm9_rom_offset);
    }
    if (!spec.arm7_code.empty()) {
        std::copy(spec.arm7_code.begin(), spec.arm7_code.end(), rom.begin() + spec.arm7_rom_offset);
    }
    if (!spec.extra_rom_payload.empty()) {
        std::copy(spec.extra_rom_payload.begin(),
                  spec.extra_rom_payload.end(),
                  rom.begin() + spec.extra_rom_offset);
    }
    if (spec.banner.include) {
        write_banner_section(rom, banner_offset, spec.banner);
    }
}

void finalize_rom_crcs(std::span<u8> rom, const NdsRomSpec& spec) noexcept {
    u16 secure_crc = 0x0000U;
    if (spec.arm9_rom_offset >= kNdsDefaultHeaderSize &&
        spec.arm9_rom_offset < kNdsSecureAreaEnd) {
        const std::size_t secure_len = kNdsSecureAreaEnd - spec.arm9_rom_offset;
        secure_crc = compute_crc16(rom.subspan(spec.arm9_rom_offset, secure_len));
    }
    write_le16(rom, kHeaderSecureCrcOffset, secure_crc);
    if (spec.corrupt_secure_crc) {
        corrupt_secure_crc(rom);
    }

    const u16 logo_crc = compute_crc16(rom.subspan(kHeaderLogoOffset, kNintendoLogoSize));
    write_le16(rom, kHeaderLogoCrcOffset, logo_crc);
    if (spec.corrupt_logo_crc) {
        corrupt_logo_crc(rom);
    }

    const u16 header_crc = compute_crc16(rom.subspan(0U, kHeaderCrcOffset));
    write_le16(rom, kHeaderCrcOffset, header_crc);
    if (spec.corrupt_header_crc) {
        corrupt_header_crc(rom);
    }
}

}  // namespace

u16 compute_crc16(std::span<const u8> data, u16 initial) noexcept {
    u16 crc = initial;
    for (const u8 byte : data) {
        crc ^= static_cast<u16>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsb = (crc & 1U) != 0U;
            crc = lsb ? static_cast<u16>((crc >> 1U) ^ kCrc16Poly) : static_cast<u16>(crc >> 1U);
        }
    }
    return crc;
}

std::array<u8, kNintendoLogoSize> synthesize_valid_logo() noexcept {
    std::array<u8, kNintendoLogoSize> logo{};
    for (std::size_t i = 0; i < kNintendoLogoSize - 2U; ++i) {
        logo[i] = static_cast<u8>((i * 37U + 13U) & 0xFFU);
    }
    const std::span<const u8> prefix(logo.data(), kNintendoLogoSize - 2U);
    const u16 s0 = compute_crc16(prefix);
    const u16 s1_prime = unstep_crc16_byte(kExpectedLogoCrc16);
    const u16 target_hi = static_cast<u16>(s1_prime >> 8U);

    for (u16 x = 0U; x < 256U; ++x) {
        const u8 x_byte = static_cast<u8>(x);
        const u16 c = compute_crc16(std::span<const u8>(&x_byte, 1U), 0U);
        if ((c >> 8U) == target_hi) {
            const u8 b0 = static_cast<u8>(x_byte ^ static_cast<u8>(s0 & 0xFFU));
            const u16 s1 = static_cast<u16>((s0 >> 8U) ^ c);
            const u8 b1 = static_cast<u8>((s1 ^ s1_prime) & 0xFFU);
            logo[kNintendoLogoSize - 2U] = b0;
            logo[kNintendoLogoSize - 1U] = b1;
            break;
        }
    }
    return logo;
}

std::vector<u8> words_to_le_bytes(std::span<const u32> words) {
    std::vector<u8> bytes(words.size() * 4U);
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_le32(bytes, static_cast<u32>(i * 4U), words[i]);
    }
    return bytes;
}

std::vector<u8> build_valid_nds_rom(const NdsRomSpec& spec) {
    const u32 banner_offset = compute_banner_offset(spec);
    const std::size_t total_size = compute_required_rom_size(spec, banner_offset);
    std::vector<u8> rom(total_size, 0U);

    write_header_fields(rom, spec, banner_offset);
    write_payloads(rom, spec, banner_offset);
    finalize_rom_crcs(rom, spec);
    return rom;
}

void corrupt_header_crc(std::span<u8> rom) noexcept {
    if (rom.size() >= kHeaderCrcOffset + 2U) {
        rom[kHeaderCrcOffset] ^= 0xFFU;
    }
}

void corrupt_logo_crc(std::span<u8> rom) noexcept {
    if (rom.size() >= kHeaderLogoCrcOffset + 2U) {
        rom[kHeaderLogoCrcOffset] ^= 0xFFU;
    }
}

void corrupt_secure_crc(std::span<u8> rom) noexcept {
    if (rom.size() >= kHeaderSecureCrcOffset + 2U) {
        rom[kHeaderSecureCrcOffset] ^= 0xFFU;
    }
}

std::vector<u8> truncate_rom(std::vector<u8> rom, std::size_t new_size) {
    if (new_size < rom.size()) {
        rom.resize(new_size);
    }
    return rom;
}

void patch_rom_u32(std::span<u8> rom, u32 offset, u32 value) noexcept {
    write_le32(rom, offset, value);
}

}  // namespace elmasds::e2e
