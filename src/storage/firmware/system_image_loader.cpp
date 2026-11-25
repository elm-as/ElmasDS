#include "storage/firmware/system_image_loader.hpp"

#include "storage/rom_loader/nds_rom_header.hpp"

#include <fstream>
#include <utility>

namespace elmasds::storage::firmware {

namespace {

void write_le16(std::vector<u8>& buffer, std::size_t offset, u16 value) noexcept {
    buffer[offset] = static_cast<u8>(value & 0xFFU);
    buffer[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

void write_user_settings_block(std::vector<u8>& image, std::size_t base) noexcept {
    write_le16(image, base + 0x00U, 0x0005U);  // Version 5
    image[base + 0x02U] = 0x02U;               // Favorite color
    image[base + 0x03U] = 1U;                  // Birth month
    image[base + 0x04U] = 1U;                  // Birth day

    // UTF-16LE nickname "ElmasDS"
    constexpr const char* kNickname = "ElmasDS";
    for (std::size_t i = 0U; i < 7U; ++i) {
        write_le16(image, base + 0x06U + i * 2U, static_cast<u16>(kNickname[i]));
    }
    write_le16(image, base + 0x1AU, 7U);  // Nickname length

    // Default identity touch calibration (0,0)-(255,191) -> (0,0)-(4095,4095).
    write_le16(image, base + 0x58U, 0x0000U);
    write_le16(image, base + 0x5AU, 0x0000U);
    image[base + 0x5CU] = 0U;
    image[base + 0x5DU] = 0U;
    write_le16(image, base + 0x5EU, 0x0FFFU);
    write_le16(image, base + 0x60U, 0x0FFFU);
    image[base + 0x62U] = 255U;
    image[base + 0x63U] = 191U;

    write_le16(image, base + 0x64U, 0x0001U);  // English language
    write_le16(image, base + 0x70U, 0x0001U);  // Update counter

    const u16 crc = rom_loader::compute_nds_crc16(std::span<const u8>(image).subspan(base, 0x70U));
    write_le16(image, base + 0x72U, crc);
}

}  // namespace

Result<std::vector<u8>> load_binary_file(const std::filesystem::path& path,
                                         std::size_t expected_size) {
    if (path.empty()) {
        return Result<std::vector<u8>>::err(ErrorCode::InvalidArgument, "File path is empty.");
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return Result<std::vector<u8>>::err(ErrorCode::FileNotFound,
                                            "Failed to open file: " + path.string());
    }

    const std::streamsize raw_size = file.tellg();
    if (raw_size < 0) {
        return Result<std::vector<u8>>::err(ErrorCode::IoFailure, "Failed to read file size.");
    }

    const std::size_t size = static_cast<std::size_t>(raw_size);
    if (expected_size != 0U && size != expected_size) {
        return Result<std::vector<u8>>::err(ErrorCode::CorruptData,
                                            "Binary file size does not match expected length.");
    }

    std::vector<u8> buffer(size, u8{0});
    file.seekg(0, std::ios::beg);
    if (size > 0U && !file.read(reinterpret_cast<char*>(buffer.data()), raw_size)) {
        return Result<std::vector<u8>>::err(ErrorCode::IoFailure, "Failed to read file contents.");
    }

    return Result<std::vector<u8>>::ok(std::move(buffer));
}

Status load_bios9_image(memory::BiosMemory& bios9, std::span<const u8> bytes) {
    if (bytes.size() != kArm9BiosSize) {
        return Status::err(ErrorCode::CorruptData, "ARM9 BIOS image must be exactly 4 KiB.");
    }
    return bios9.load(bytes);
}

Status load_bios7_image(memory::BiosMemory& bios7, std::span<const u8> bytes) {
    if (bytes.size() != kArm7BiosSize) {
        return Status::err(ErrorCode::CorruptData, "ARM7 BIOS image must be exactly 16 KiB.");
    }
    return bios7.load(bytes);
}

Status load_firmware_image(hardware::spi::FirmwareFlashDevice& firmware,
                           std::span<const u8> bytes) {
    if (bytes.size() < kMinFirmwareSize || (bytes.size() & (bytes.size() - 1U)) != 0U) {
        return Status::err(ErrorCode::CorruptData,
                           "Firmware image must be a power-of-two size of at least 128 KiB.");
    }
    firmware.load_image(bytes);
    return Status::ok();
}

std::vector<u8> create_default_firmware_image() {
    std::vector<u8> image(hardware::spi::kDefaultFirmwareSize, u8{0xFF});
    // User settings offset field at 0x020 (stored in units of 8 bytes -> 0x3FE00 / 8 = 0x7FC0).
    write_le16(image, 0x020U, 0x7FC0U);
    write_user_settings_block(image, 0x3FE00U);
    write_user_settings_block(image, 0x3FF00U);
    return image;
}

}  // namespace elmasds::storage::firmware
