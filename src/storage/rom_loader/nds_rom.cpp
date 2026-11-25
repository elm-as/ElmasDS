#include "storage/rom_loader/nds_rom.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

namespace elmasds::storage::rom_loader {

namespace {

[[nodiscard]] u16 read_le16(std::span<const u8> data, std::size_t offset) noexcept {
    return static_cast<u16>(data[offset]) |
           static_cast<u16>(static_cast<u16>(data[offset + 1U]) << 8U);
}

[[nodiscard]] u32 read_le32(std::span<const u8> data, std::size_t offset) noexcept {
    return static_cast<u32>(data[offset]) | (static_cast<u32>(data[offset + 1U]) << 8U) |
           (static_cast<u32>(data[offset + 2U]) << 16U) |
           (static_cast<u32>(data[offset + 3U]) << 24U);
}

[[nodiscard]] std::string read_ascii_field(std::span<const u8> data, std::size_t offset,
                                           std::size_t max_len) {
    std::size_t len = 0U;
    while (len < max_len && data[offset + len] != 0U) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), len);
}

[[nodiscard]] bool range_fits(u64 offset, u64 size, u64 total) noexcept {
    return offset <= total && size <= (total - offset);
}

}  // namespace

u16 compute_nds_crc16(std::span<const u8> data) noexcept {
    u16 crc = 0xFFFFU;
    for (u8 byte : data) {
        crc ^= static_cast<u16>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 1U) != 0U) {
                crc = static_cast<u16>((crc >> 1U) ^ 0xA001U);
            } else {
                crc = static_cast<u16>(crc >> 1U);
            }
        }
    }
    return crc;
}

Result<NdsRomHeader> parse_nds_header(std::span<const u8> rom_bytes) {
    if (rom_bytes.size() < kNdsMinHeaderSize) {
        return Result<NdsRomHeader>::err(ErrorCode::CorruptData,
                                         "NDS ROM is smaller than minimum 0x160-byte header.");
    }

    NdsRomHeader header{};
    header.title = read_ascii_field(rom_bytes, 0x000U, 12U);
    header.gamecode = read_ascii_field(rom_bytes, 0x00CU, 4U);
    header.makercode = read_ascii_field(rom_bytes, 0x010U, 2U);
    header.unitcode = rom_bytes[0x012U];
    header.encryption_seed = rom_bytes[0x013U];
    header.device_capacity = rom_bytes[0x014U];
    header.rom_version = rom_bytes[0x01EU];
    header.autostart_flags = rom_bytes[0x01FU];

    header.arm9_rom_offset = read_le32(rom_bytes, 0x020U);
    header.arm9_entry_address = read_le32(rom_bytes, 0x024U);
    header.arm9_ram_address = read_le32(rom_bytes, 0x028U);
    header.arm9_size = read_le32(rom_bytes, 0x02CU);

    header.arm7_rom_offset = read_le32(rom_bytes, 0x030U);
    header.arm7_entry_address = read_le32(rom_bytes, 0x034U);
    header.arm7_ram_address = read_le32(rom_bytes, 0x038U);
    header.arm7_size = read_le32(rom_bytes, 0x03CU);

    header.fnt_offset = read_le32(rom_bytes, 0x040U);
    header.fnt_size = read_le32(rom_bytes, 0x044U);
    header.fat_offset = read_le32(rom_bytes, 0x048U);
    header.fat_size = read_le32(rom_bytes, 0x04CU);

    header.normal_romctrl = read_le32(rom_bytes, 0x060U);
    header.key1_romctrl = read_le32(rom_bytes, 0x064U);
    header.banner_offset = read_le32(rom_bytes, 0x068U);
    header.secure_area_crc = read_le16(rom_bytes, 0x06CU);
    header.header_size = read_le32(rom_bytes, 0x084U);
    header.logo_crc = read_le16(rom_bytes, 0x15CU);
    header.header_crc = read_le16(rom_bytes, 0x15EU);

    const u16 computed_header_crc = compute_nds_crc16(rom_bytes.subspan(0U, 0x15EU));
    if (computed_header_crc != header.header_crc) {
        return Result<NdsRomHeader>::err(ErrorCode::CorruptData,
                                         "NDS ROM header CRC-16 mismatch.");
    }

    const u16 computed_logo_crc =
        compute_nds_crc16(rom_bytes.subspan(kNdsLogoOffset, kNdsLogoSize));
    if (computed_logo_crc != header.logo_crc) {
        return Result<NdsRomHeader>::err(ErrorCode::CorruptData,
                                         "NDS ROM Nintendo logo CRC-16 mismatch.");
    }

    const u64 total = rom_bytes.size();
    if (!range_fits(header.arm9_rom_offset, header.arm9_size, total)) {
        return Result<NdsRomHeader>::err(ErrorCode::CorruptData,
                                         "ARM9 binary range exceeds ROM image size.");
    }
    if (!range_fits(header.arm7_rom_offset, header.arm7_size, total)) {
        return Result<NdsRomHeader>::err(ErrorCode::CorruptData,
                                         "ARM7 binary range exceeds ROM image size.");
    }

    return Result<NdsRomHeader>::ok(std::move(header));
}

NdsRom::NdsRom(NdsRomHeader header, std::vector<u8> bytes) noexcept
    : header_(std::move(header)), bytes_(std::move(bytes)) {}

Result<NdsRom> NdsRom::load_from_bytes(std::span<const u8> bytes) {
    auto parsed = parse_nds_header(bytes);
    if (parsed.is_err()) {
        return Result<NdsRom>::err(parsed.error().code, parsed.error().message);
    }
    std::vector<u8> owned(bytes.begin(), bytes.end());
    return Result<NdsRom>::ok(NdsRom(std::move(parsed.value()), std::move(owned)));
}

Result<NdsRom> NdsRom::load_from_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result<NdsRom>::err(ErrorCode::FileNotFound,
                                   "Failed to open NDS ROM file: " + path.string());
    }

    const std::streamsize size = file.tellg();
    if (size < 0) {
        return Result<NdsRom>::err(ErrorCode::IoFailure,
                                   "Failed to determine NDS ROM file size.");
    }

    std::vector<u8> buffer(static_cast<std::size_t>(size), u8{0});
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return Result<NdsRom>::err(ErrorCode::IoFailure, "Failed to read NDS ROM file.");
    }

    return load_from_bytes(buffer);
}

std::span<const u8> NdsRom::arm9_binary() const noexcept {
    if (header_.arm9_size == 0U) {
        return {};
    }
    return std::span<const u8>(bytes_).subspan(header_.arm9_rom_offset, header_.arm9_size);
}

std::span<const u8> NdsRom::arm7_binary() const noexcept {
    if (header_.arm7_size == 0U) {
        return {};
    }
    return std::span<const u8>(bytes_).subspan(header_.arm7_rom_offset, header_.arm7_size);
}

u32 NdsRom::read32(u32 offset) const noexcept {
    if (static_cast<u64>(offset) + 4U > bytes_.size()) {
        return 0xFFFFFFFFU;
    }
    return read_le32(bytes_, offset);
}

u32 NdsRom::chip_id() const noexcept {
    // Standard NDS chip ID format: byte 0 = manufacturer (0xC2 Macronix),
    // byte 1 = size in MiB - 1 (or 0x00 for <= 1 MiB), bytes 2..3 = flags.
    const u64 size_mib = std::max<u64>(1U, bytes_.size() / (1024U * 1024U));
    const u8 size_code = static_cast<u8>((size_mib > 256U) ? 0xFFU : (size_mib - 1U));
    return 0x000000C2U | (static_cast<u32>(size_code) << 8U);
}

}  // namespace elmasds::storage::rom_loader

