#include "e2e/arm_program_emitter.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace elmasds::e2e {
namespace {

[[nodiscard]] u16 read_le16(std::span<const u8> data, std::size_t offset) noexcept {
    return static_cast<u16>(data[offset]) |
           static_cast<u16>(static_cast<u16>(data[offset + 1U]) << 8U);
}

[[nodiscard]] u32 read_le32(std::span<const u8> data, std::size_t offset) noexcept {
    return static_cast<u32>(data[offset + 0U]) |
           (static_cast<u32>(data[offset + 1U]) << 8U) |
           (static_cast<u32>(data[offset + 2U]) << 16U) |
           (static_cast<u32>(data[offset + 3U]) << 24U);
}

TEST(NdsRomBuilderTest, ComputesKnownCrc16Vectors) {
    constexpr std::string_view kVector = "123456789";
    const std::span<const u8> bytes(
        reinterpret_cast<const u8*>(kVector.data()), kVector.size());
    EXPECT_EQ(compute_crc16(bytes, 0xFFFFU), 0x4B37U);
    EXPECT_EQ(compute_crc16(bytes, 0x0000U), 0xBB3DU);
    EXPECT_EQ(compute_crc16({}, 0xFFFFU), 0xFFFFU);
}

TEST(NdsRomBuilderTest, SynthesizesNonProprietaryValidLogo) {
    const auto logo_a = synthesize_valid_logo();
    const auto logo_b = synthesize_valid_logo();
    EXPECT_EQ(logo_a.size(), kNintendoLogoSize);
    EXPECT_EQ(logo_a, logo_b);
    EXPECT_EQ(compute_crc16(logo_a, kCrc16DefaultInit), kExpectedLogoCrc16);
}

TEST(NdsRomBuilderTest, BuildsValidRomWithSecureAreaAndBanner) {
    NdsRomSpec spec{};
    spec.title = "TESTGAME";
    spec.gamecode = "TSTE";
    spec.makercode = "99";
    spec.device_capacity = 2U;
    spec.arm9_code = words_to_le_bytes(std::array<u32, 2>{0xE1A00000U, 0xEAFFFFFEU});
    spec.arm7_code = words_to_le_bytes(std::array<u32, 1>{0xEAFFFFFEU});
    spec.banner.include = true;
    spec.banner.title_english = u"E2E Title";

    const std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_GE(rom.size(), 0x8000U + kBannerSizeV1);
    EXPECT_EQ(read_le16(rom, 0x15CU), kExpectedLogoCrc16);

    const std::span<const u8> rom_span(rom);
    EXPECT_EQ(read_le16(rom, 0x15EU), compute_crc16(rom_span.subspan(0U, 0x15EU)));
    EXPECT_EQ(read_le16(rom, 0x06CU), compute_crc16(rom_span.subspan(0x4000U, 0x4000U)));
    EXPECT_EQ(read_le32(rom, 0x080U), static_cast<u32>(rom.size()));
    EXPECT_EQ(read_le32(rom, 0x084U), kNdsDefaultHeaderSize);

    const u32 banner_off = read_le32(rom, 0x068U);
    ASSERT_GE(banner_off, 0x8000U);
    EXPECT_EQ(read_le16(rom, banner_off), 0x0001U);
    EXPECT_EQ(read_le16(rom, banner_off + 2U),
              compute_crc16(rom_span.subspan(banner_off + 0x020U, 0x820U)));
    EXPECT_EQ(read_le16(rom, banner_off + 0x340U), static_cast<u16>(u'E'));
}

TEST(NdsRomBuilderTest, SupportsNoSecureAreaAndNegativeMutators) {
    NdsRomSpec spec{};
    spec.arm9_rom_offset = 0x8000U;
    spec.arm7_rom_offset = 0x9000U;
    spec.arm9_code = {1U, 2U, 3U, 4U};
    spec.arm7_code = {5U, 6U, 7U, 8U};

    std::vector<u8> rom = build_valid_nds_rom(spec);
    EXPECT_EQ(read_le16(rom, 0x06CU), 0x0000U);

    const u16 orig_hdr = read_le16(rom, 0x15EU);
    const u16 orig_logo = read_le16(rom, 0x15CU);
    corrupt_header_crc(rom);
    corrupt_logo_crc(rom);
    corrupt_secure_crc(rom);
    EXPECT_NE(read_le16(rom, 0x15EU), orig_hdr);
    EXPECT_NE(read_le16(rom, 0x15CU), orig_logo);
    EXPECT_NE(read_le16(rom, 0x06CU), 0x0000U);

    patch_rom_u32(rom, 0x020U, 0xDEADBEEFU);
    EXPECT_EQ(read_le32(rom, 0x020U), 0xDEADBEEFU);

    const std::vector<u8> truncated = truncate_rom(std::move(rom), 0x100U);
    EXPECT_EQ(truncated.size(), 0x100U);
}

TEST(NdsRomBuilderTest, PlacesBannerAfterExtraRomPayloadWithoutOverlap) {
    NdsRomSpec spec{};
    spec.arm9_rom_offset = 0x4000U;
    spec.arm9_code = {0x00U, 0x00U, 0xA0U, 0xE1U};
    spec.arm7_rom_offset = 0x7000U;
    spec.arm7_code = {0xFEU, 0xFFU, 0xFFU, 0xEAU};
    spec.extra_rom_offset = 0x8000U;
    spec.extra_rom_payload = std::vector<u8>(0x1004U, 0x77U);
    spec.banner.include = true;
    spec.banner.version = 0x0003U;

    const std::vector<u8> rom = build_valid_nds_rom(spec);
    const std::span<const u8> rom_span(rom);
    const u32 banner_off = read_le32(rom, 0x068U);
    EXPECT_EQ(banner_off, 0x9200U);
    ASSERT_GE(rom.size(), banner_off + 0x0A40U);
    EXPECT_EQ(rom[0x8000U], 0x77U);
    EXPECT_EQ(rom[0x9003U], 0x77U);
    EXPECT_EQ(read_le16(rom, 0x15EU), compute_crc16(rom_span.subspan(0U, 0x15EU)));
    EXPECT_EQ(read_le16(rom, banner_off + 0x002U),
              compute_crc16(rom_span.subspan(banner_off + 0x020U, 0x820U)));
    EXPECT_EQ(read_le16(rom, banner_off + 0x004U),
              compute_crc16(rom_span.subspan(banner_off + 0x020U, 0x920U)));
    EXPECT_EQ(read_le16(rom, banner_off + 0x006U),
              compute_crc16(rom_span.subspan(banner_off + 0x020U, 0xA20U)));

    NdsRomSpec crossing_spec{};
    crossing_spec.extra_rom_offset = 0x7800U;
    crossing_spec.extra_rom_payload = std::vector<u8>(0x1000U, 0x5AU);
    crossing_spec.banner.include = true;
    const std::vector<u8> crossing_rom = build_valid_nds_rom(crossing_spec);
    const u32 crossing_banner_off = read_le32(crossing_rom, 0x068U);
    EXPECT_EQ(crossing_banner_off, 0x8800U);
    EXPECT_EQ(crossing_rom[0x7800U], 0x5AU);
    EXPECT_EQ(crossing_rom[0x87FFU], 0x5AU);
    EXPECT_EQ(read_le16(crossing_rom, crossing_banner_off + 0x002U),
              compute_crc16(std::span<const u8>(crossing_rom)
                                .subspan(crossing_banner_off + 0x020U, 0x820U)));

    NdsRomSpec overflow_spec{};
    overflow_spec.extra_rom_offset = 0xFFFFFE00U;
    overflow_spec.extra_rom_payload = {1U};
    overflow_spec.banner.include = true;
    EXPECT_THROW((void)build_valid_nds_rom(overflow_spec), std::out_of_range);
}

TEST(NdsRomBuilderTest, ComputesDsiBannerCrcAndCorruptsAllBannerVersions) {
    NdsRomSpec dsi_spec{};
    dsi_spec.banner.include = true;
    dsi_spec.banner.version = 0x0103U;
    const std::vector<u8> dsi_rom = build_valid_nds_rom(dsi_spec);
    const std::span<const u8> dsi_span(dsi_rom);
    const u32 dsi_off = read_le32(dsi_rom, 0x068U);
    ASSERT_GE(dsi_rom.size(), dsi_off + 0x23C0U);
    EXPECT_EQ(read_le16(dsi_rom, dsi_off + 0x008U),
              compute_crc16(dsi_span.subspan(dsi_off + 0x1240U, 0x1180U)));

    NdsRomSpec bad_spec{};
    bad_spec.banner.include = true;
    bad_spec.banner.version = 0x0103U;
    bad_spec.banner.corrupt_crc = true;
    const std::vector<u8> bad_rom = build_valid_nds_rom(bad_spec);
    const std::span<const u8> bad_span(bad_rom);
    const u32 bad_off = read_le32(bad_rom, 0x068U);
    EXPECT_NE(read_le16(bad_rom, bad_off + 0x002U),
              compute_crc16(bad_span.subspan(bad_off + 0x020U, 0x820U)));
    EXPECT_NE(read_le16(bad_rom, bad_off + 0x004U),
              compute_crc16(bad_span.subspan(bad_off + 0x020U, 0x920U)));
    EXPECT_NE(read_le16(bad_rom, bad_off + 0x006U),
              compute_crc16(bad_span.subspan(bad_off + 0x020U, 0xA20U)));
    EXPECT_NE(read_le16(bad_rom, bad_off + 0x008U),
              compute_crc16(bad_span.subspan(bad_off + 0x1240U, 0x1180U)));
}

TEST(ArmProgramEmitterTest, EmitsCoreInstructionsAndImmediates) {
    ArmProgramEmitter emit(0x02000000U);
    emit.emit_nop();
    emit.emit_mov_imm(ArmReg::R0, 0x02000000U);
    emit.emit_mov_imm(ArmReg::R1, 0x12345678U);
    emit.emit_cmp_imm(ArmReg::R1, 0x10U);
    emit.emit_mcr_wfi();
    emit.emit_irq_return();
    emit.emit_branch_self();
    emit.emit_swi(0x06U);

    const auto& w = emit.words();
    ASSERT_EQ(w.size(), 11U);
    EXPECT_EQ(w[0], 0xE1A00000U);
    EXPECT_EQ(w[1], 0xE3A00402U);
    EXPECT_EQ(w[2], 0xE3A01078U);
    EXPECT_EQ(w[3], 0xE3811C56U);
    EXPECT_EQ(w[4], 0xE381170DU);
    EXPECT_EQ(w[5], 0xE3811412U);
    EXPECT_EQ(w[6], 0xE3510010U);
    EXPECT_EQ(w[7], 0xEE070F90U);
    EXPECT_EQ(w[8], 0xE25EF004U);
    EXPECT_EQ(w[9], 0xEAFFFFFEU);
    EXPECT_EQ(w[10], 0xEF000006U);
    EXPECT_EQ(emit.to_bytes().size(), w.size() * 4U);
}

TEST(ArmProgramEmitterTest, EmitsBranchesMemoryTransfersAndLiteralPools) {
    ArmProgramEmitter emit(0x02000000U);
    emit.emit_ldr_literal(ArmReg::R2, 0xCAFEBABEU);
    emit.emit_b(0x02000010U);
    emit.emit_bl(0x02000000U);
    emit.emit_str32(MemOffsetOp{ArmReg::R2, ArmReg::R0, 4});
    emit.emit_ldr16(MemOffsetOp{ArmReg::R3, ArmReg::R0, -16});
    emit.emit_str8(MemOffsetOp{ArmReg::R3, ArmReg::R0, 1});
    emit.emit_bx(ArmReg::Lr);
    emit.finalize_literal_pool();

    const auto& w = emit.words();
    ASSERT_EQ(w.size(), 8U);
    EXPECT_EQ(w[0], 0xE59F2014U);
    EXPECT_EQ(w[1], 0xEA000001U);
    EXPECT_EQ(w[2], 0xEBFFFFFCU);
    EXPECT_EQ(w[3], 0xE5802004U);
    EXPECT_EQ(w[4], 0xE15031B0U);
    EXPECT_EQ(w[5], 0xE5C03001U);
    EXPECT_EQ(w[6], 0xE12FFF1EU);
    EXPECT_EQ(w[7], 0xCAFEBABEU);
}

TEST(ArmProgramEmitterTest, EmitsStatusModeAndMmioHelpers) {
    ArmProgramEmitter emit(0x02000100U);
    emit.emit_enable_irq(ArmReg::R4);
    emit.emit_set_mode_sp(0x12U, 0x027FF000U, ArmReg::R5);
    emit.emit_irq_handler_prologue();
    emit.emit_write16_abs(0x04000102U, 0x00C1U);
    emit.emit_irq_handler_epilogue();

    const auto& w = emit.words();
    EXPECT_EQ(w[0], 0xE10F4000U);
    EXPECT_EQ(w[1], 0xE3C44080U);
    EXPECT_EQ(w[2], 0xE121F004U);
    EXPECT_EQ(w[3], 0xE10F5000U);
    EXPECT_EQ(w[4], 0xE321F0D2U);
    EXPECT_EQ(w.back(), 0xE25EF004U);
}

TEST(ArmProgramEmitterTest, ValidatesMultiChunkFlagsAndModeScratch) {
    ArmProgramEmitter emit(0x02000000U);
    emit.emit_add_imm(RegImmOp{ArmReg::R1, ArmReg::R0, 0x10U, true});
    emit.emit_sub_imm(RegImmOp{ArmReg::R2, ArmReg::R1, 0x04U, true});
    EXPECT_NE(emit.words()[0] & (1U << 20U), 0U);
    EXPECT_NE(emit.words()[1] & (1U << 20U), 0U);

    emit.emit_add_imm(RegImmOp{ArmReg::R3, ArmReg::R0, 0x00010001U, false});
    emit.emit_sub_imm(RegImmOp{ArmReg::R4, ArmReg::R3, 0x00010001U, false});
    emit.emit_orr_imm(RegImmOp{ArmReg::R5, ArmReg::R0, 0x00010001U, true});
    const std::size_t saved_words = emit.word_count();

    EXPECT_THROW(emit.emit_add_imm(RegImmOp{ArmReg::R0, ArmReg::R0, 0x00010001U, true}),
                 std::invalid_argument);
    EXPECT_EQ(emit.word_count(), saved_words);
    EXPECT_THROW(emit.emit_sub_imm(RegImmOp{ArmReg::R0, ArmReg::R0, 0x12345678U, true}),
                 std::invalid_argument);
    EXPECT_EQ(emit.word_count(), saved_words);
    EXPECT_THROW(emit.emit_cmp_imm(ArmReg::R0, 0x00010001U), std::invalid_argument);

    EXPECT_THROW(emit.emit_set_mode_sp(0x11U, 0x0380FC00U, ArmReg::R8), std::invalid_argument);
    EXPECT_EQ(emit.word_count(), saved_words);
    EXPECT_THROW(emit.emit_set_mode_sp(0x11U, 0x0380FC00U, ArmReg::R12), std::invalid_argument);
    EXPECT_EQ(emit.word_count(), saved_words);
    EXPECT_THROW(emit.emit_set_mode_sp(0x12U, 0x027FF000U, ArmReg::Sp), std::invalid_argument);
    EXPECT_EQ(emit.word_count(), saved_words);

    emit.emit_set_mode_sp(0x11U, 0x0380FC00U, ArmReg::R7);
    emit.emit_set_mode_sp(0x12U, 0x027FF000U, ArmReg::R12);
    EXPECT_GT(emit.word_count(), saved_words);
}

TEST(SystemImageBuilderTest, BuildsSyntheticBios9AndBios7) {
    const std::vector<u8> bios9 = build_synthetic_bios9(BiosSpec{.include_swi_halt = true});
    ASSERT_EQ(bios9.size(), kBios9Size);
    EXPECT_EQ(read_le32(bios9, 0x020U), 0xE59FF000U);
    EXPECT_EQ(read_le32(bios9, 0x028U), 0x02000000U);
    EXPECT_EQ(read_le32(bios9, 0x030U), 0xEE070F90U);
    EXPECT_EQ(read_le32(bios9, 0x040U), 0xE92D500FU);
    EXPECT_EQ(read_le32(bios9, 39U * 4U), 0xE25EF004U);
    EXPECT_EQ(read_le32(bios9, 40U * 4U), 0x027FFFFCU);

    const std::vector<u8> bios7 = build_synthetic_bios7(BiosSpec{.include_swi_halt = true});
    ASSERT_EQ(bios7.size(), kBios7Size);
    EXPECT_EQ(read_le32(bios7, 0x028U), 0x03800000U);
    EXPECT_EQ(read_le32(bios7, 0x040U), 0xE92D500FU);
    EXPECT_EQ(read_le32(bios7, 24U * 4U), 0xE25EF004U);
    EXPECT_EQ(read_le32(bios7, 25U * 4U), 0x0380FFFCU);
    EXPECT_EQ(read_le32(bios7, 30U * 4U), 0x04000301U);
}

TEST(SystemImageBuilderTest, BuildsSyntheticFirmwareWithValidAndCorruptCrcs) {
    for (const u32 fw_size : {kFirmwareSize128K, kFirmwareSize256K, kFirmwareSize512K}) {
        FirmwareSpec spec{};
        spec.size_bytes = fw_size;
        const std::vector<u8> fw = build_synthetic_firmware(spec);
        ASSERT_EQ(fw.size(), fw_size);
        EXPECT_EQ(read_le16(fw, 0x020U), static_cast<u16>((fw_size - 0x200U) / 8U));
        EXPECT_EQ(read_le16(fw, 0x02AU),
                  compute_crc16(std::span<const u8>(fw).subspan(0x02CU, 0x0138U), 0x0000U));

        const std::size_t slot1 = fw_size - 0x100U;
        EXPECT_EQ(read_le16(fw, slot1 + 0x070U), 1U);
        EXPECT_EQ(read_le16(fw, slot1 + 0x072U),
                  compute_crc16(std::span<const u8>(fw).subspan(slot1, 0x070U), 0xFFFFU));
    }

    FirmwareSpec bad{};
    bad.corrupt_wifi_crc = true;
    bad.corrupt_user_settings_crc = true;
    const std::vector<u8> corrupt_fw = build_synthetic_firmware(bad);
    EXPECT_NE(read_le16(corrupt_fw, 0x02AU),
              compute_crc16(std::span<const u8>(corrupt_fw).subspan(0x02CU, 0x0138U), 0x0000U));
    EXPECT_NE(read_le16(corrupt_fw, corrupt_fw.size() - 0x100U + 0x072U),
              compute_crc16(
                  std::span<const u8>(corrupt_fw).subspan(corrupt_fw.size() - 0x100U, 0x070U),
                  0xFFFFU));
}

TEST(SystemImageBuilderTest, BuildsSavesAndCleansUpScopedTempDir) {
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Sram32K}).size(), 32U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Eeprom512B}).size(), 512U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Eeprom8K}).size(), 8U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Eeprom64K}).size(), 64U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Eeprom128K}).size(), 128U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Flash256K}).size(), 256U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Flash512K}).size(), 512U * 1024U);
    EXPECT_EQ(build_save_image({SyntheticSaveKind::Flash1M}).size(), 1024U * 1024U);

    std::filesystem::path captured_path;
    {
        ScopedTempDir dir;
        captured_path = dir.path();
        EXPECT_TRUE(std::filesystem::exists(captured_path));
        const std::vector<u8> payload{0xAAU, 0xBBU, 0xCCU, 0xDDU};
        const auto file = dir.write_file("test.sav", payload);
        EXPECT_TRUE(std::filesystem::exists(file));
        EXPECT_EQ(dir.read_file("test.sav"), payload);

        ScopedTempDir moved(std::move(dir));
        EXPECT_TRUE(std::filesystem::exists(captured_path));
    }
    EXPECT_FALSE(std::filesystem::exists(captured_path));
}

}  // namespace
}  // namespace elmasds::e2e
