#include "storage/firmware/system_image_loader.hpp"
#include "storage/rom_loader/nds_rom.hpp"
#include "storage/save_manager/save_memory.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

namespace elmasds::storage {
namespace {

void write_le16(std::vector<u8>& buf, std::size_t off, u16 val) {
    buf[off] = static_cast<u8>(val & 0xFFU);
    buf[off + 1U] = static_cast<u8>((val >> 8U) & 0xFFU);
}

void write_le32(std::vector<u8>& buf, std::size_t off, u32 val) {
    buf[off] = static_cast<u8>(val & 0xFFU);
    buf[off + 1U] = static_cast<u8>((val >> 8U) & 0xFFU);
    buf[off + 2U] = static_cast<u8>((val >> 16U) & 0xFFU);
    buf[off + 3U] = static_cast<u8>((val >> 24U) & 0xFFU);
}

std::vector<u8> make_synthetic_nds_rom() {
    std::vector<u8> rom(0x9000U, u8{0});
    const char* title = "ELMASDS";
    for (std::size_t i = 0U; i < 7U; ++i) {
        rom[i] = static_cast<u8>(title[i]);
    }
    const char* code = "ELMS";
    for (std::size_t i = 0U; i < 4U; ++i) {
        rom[0x00CU + i] = static_cast<u8>(code[i]);
    }
    rom[0x010U] = '0';
    rom[0x011U] = '1';

    write_le32(rom, 0x020U, 0x4000U);      // ARM9 ROM offset
    write_le32(rom, 0x024U, 0x02000000U);  // ARM9 entry
    write_le32(rom, 0x028U, 0x02000000U);  // ARM9 RAM load
    write_le32(rom, 0x02CU, 0x100U);       // ARM9 size

    write_le32(rom, 0x030U, 0x8000U);      // ARM7 ROM offset
    write_le32(rom, 0x034U, 0x03800000U);  // ARM7 entry
    write_le32(rom, 0x038U, 0x03800000U);  // ARM7 RAM load
    write_le32(rom, 0x03CU, 0x100U);       // ARM7 size

    write_le32(rom, 0x084U, 0x4000U);  // Header size

    const u16 logo_crc = rom_loader::compute_nds_crc16(
        std::span<const u8>(rom).subspan(rom_loader::kNdsLogoOffset, rom_loader::kNdsLogoSize));
    write_le16(rom, 0x15CU, logo_crc);

    const u16 header_crc = rom_loader::compute_nds_crc16(std::span<const u8>(rom).subspan(0U, 0x15EU));
    write_le16(rom, 0x15EU, header_crc);
    return rom;
}

TEST(NdsRomLoaderTest, ParsesValidSyntheticRomAndRejectsCorruptCrcs) {
    auto rom_bytes = make_synthetic_nds_rom();
    auto loaded = rom_loader::NdsRom::load_from_bytes(rom_bytes);
    ASSERT_TRUE(loaded.is_ok());
    EXPECT_EQ(loaded.value().header().title, "ELMASDS");
    EXPECT_EQ(loaded.value().header().gamecode, "ELMS");
    EXPECT_EQ(loaded.value().arm9_binary().size(), 0x100U);
    EXPECT_EQ(loaded.value().arm7_binary().size(), 0x100U);

    // Corrupt header CRC.
    auto bad_hdr = rom_bytes;
    bad_hdr[0x15EU] ^= 0xFFU;
    EXPECT_TRUE(rom_loader::NdsRom::load_from_bytes(bad_hdr).is_err());

    // Corrupt logo CRC (recompute header CRC so logo CRC check is specifically exercised).
    auto bad_logo = rom_bytes;
    bad_logo[0x15CU] ^= 0xFFU;
    const u16 fixed_hdr_crc =
        rom_loader::compute_nds_crc16(std::span<const u8>(bad_logo).subspan(0U, 0x15EU));
    write_le16(bad_logo, 0x15EU, fixed_hdr_crc);
    EXPECT_TRUE(rom_loader::NdsRom::load_from_bytes(bad_logo).is_err());

    // Truncated ROM.
    std::vector<u8> tiny(0x100U, u8{0});
    EXPECT_TRUE(rom_loader::NdsRom::load_from_bytes(tiny).is_err());
}

TEST(SaveMemoryTest, FlashAndEepromSpiRoundTripAndDeferredDiskFlush) {
    const auto temp_path =
        std::filesystem::temp_directory_path() / "elmasds_test_save_flash_256k.sav";
    std::error_code ec;
    std::filesystem::remove(temp_path, ec);

    {
        save_manager::SaveMemory save;
        ASSERT_TRUE(save.attach_file(temp_path, save_manager::SaveType::Flash256KB).is_ok());
        EXPECT_FALSE(save.is_dirty());

        // WREN (0x06), then Page Program (0x02) at 0x000100 with 2 bytes.
        static_cast<void>(save.transfer(0x06U, false));
        static_cast<void>(save.transfer(0x02U, true));
        static_cast<void>(save.transfer(0x00U, true));
        static_cast<void>(save.transfer(0x01U, true));
        static_cast<void>(save.transfer(0x00U, true));
        static_cast<void>(save.transfer(0xABU, true));
        static_cast<void>(save.transfer(0xCDU, false));
        EXPECT_TRUE(save.is_dirty());

        // Deferred flush persists to disk and clears dirty flag.
        ASSERT_TRUE(save.flush_if_dirty().is_ok());
        EXPECT_FALSE(save.is_dirty());
    }

    // Re-attach without type hint to verify size auto-detection and persisted bytes.
    {
        save_manager::SaveMemory reloaded;
        ASSERT_TRUE(reloaded.attach_file(temp_path).is_ok());
        EXPECT_EQ(reloaded.type(), save_manager::SaveType::Flash256KB);

        static_cast<void>(reloaded.transfer(0x03U, true));
        static_cast<void>(reloaded.transfer(0x00U, true));
        static_cast<void>(reloaded.transfer(0x01U, true));
        static_cast<void>(reloaded.transfer(0x00U, true));
        EXPECT_EQ(reloaded.transfer(0x00U, true), 0xABU);
        EXPECT_EQ(reloaded.transfer(0x00U, false), 0xCDU);
    }

    std::filesystem::remove(temp_path, ec);
}

TEST(SystemImageLoaderTest, ValidatesBiosSizesAndCreatesDefaultFirmware) {
    memory::BiosMemory bios9(memory::BiosMemory::kArm9BiosSize);
    memory::BiosMemory bios7(memory::BiosMemory::kArm7BiosSize, true);

    std::vector<u8> wrong_size(128U, u8{0});
    EXPECT_TRUE(firmware::load_bios9_image(bios9, wrong_size).is_err());
    EXPECT_TRUE(firmware::load_bios7_image(bios7, wrong_size).is_err());

    std::vector<u8> valid9(firmware::kArm9BiosSize, u8{0xAA});
    std::vector<u8> valid7(firmware::kArm7BiosSize, u8{0x55});
    EXPECT_TRUE(firmware::load_bios9_image(bios9, valid9).is_ok());
    EXPECT_TRUE(firmware::load_bios7_image(bios7, valid7).is_ok());

    const auto default_fw = firmware::create_default_firmware_image();
    EXPECT_EQ(default_fw.size(), hardware::spi::kDefaultFirmwareSize);
}

}  // namespace
}  // namespace elmasds::storage

