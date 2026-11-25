#include "e2e/arm_program_emitter.hpp"
#include "e2e/nds_rom_builder.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <stdexcept>
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

void expect_banner_crc_validity(std::span<const u8> rom, u32 banner_off, bool expect_valid) {
    struct CrcSpan {
        u32 crc_offset;
        u32 data_offset;
        u32 data_len;
    };
    const u16 version = read_le16(rom, banner_off);
    const std::vector<CrcSpan> checks = [&]() {
        std::vector<CrcSpan> list{{0x002U, 0x020U, 0x820U}};
        if (version == 0x0002U || version == 0x0003U || version == 0x0103U) {
            list.push_back({0x004U, 0x020U, 0x920U});
        }
        if (version == 0x0003U || version == 0x0103U) {
            list.push_back({0x006U, 0x020U, 0xA20U});
        }
        if (version == 0x0103U) {
            list.push_back({0x008U, 0x1240U, 0x1180U});
        }
        return list;
    }();
    for (const CrcSpan& entry : checks) {
        const u16 actual = read_le16(rom, banner_off + entry.crc_offset);
        const u16 computed =
            compute_crc16(rom.subspan(banner_off + entry.data_offset, entry.data_len));
        if (expect_valid) {
            EXPECT_EQ(actual, computed);
        } else {
            EXPECT_NE(actual, computed);
        }
    }
}

TEST(NdsRomBuilderEdgeTest, HandlesUnknownBannerVersionWithoutCrash) {
    for (const u16 unknown_version : {u16{0x0004U}, u16{0xFFFFU}}) {
        NdsRomSpec spec{};
        spec.banner.include = true;
        spec.banner.version = unknown_version;

        const std::vector<u8> rom = build_valid_nds_rom(spec);
        const u32 banner_off = read_le32(rom, 0x068U);
        ASSERT_GE(banner_off, kNdsSecureAreaEnd);
        ASSERT_EQ(rom.size(), static_cast<std::size_t>(banner_off) + kBannerSizeV1);
        EXPECT_EQ(read_le16(rom, banner_off), unknown_version);
        expect_banner_crc_validity(rom, banner_off, true);
    }

    NdsRomSpec corrupt_spec{};
    corrupt_spec.banner.include = true;
    corrupt_spec.banner.version = 0x0004U;
    corrupt_spec.banner.corrupt_crc = true;
    const std::vector<u8> corrupt_rom = build_valid_nds_rom(corrupt_spec);
    const u32 corrupt_off = read_le32(corrupt_rom, 0x068U);
    ASSERT_EQ(corrupt_rom.size(), static_cast<std::size_t>(corrupt_off) + kBannerSizeV1);
    expect_banner_crc_validity(corrupt_rom, corrupt_off, false);
}

TEST(NdsRomBuilderEdgeTest, CorruptsAllBannerCrcsForV2AndV3) {
    for (const u16 version : {u16{0x0002U}, u16{0x0003U}}) {
        NdsRomSpec valid_spec{};
        valid_spec.banner.include = true;
        valid_spec.banner.version = version;
        const std::vector<u8> valid_rom = build_valid_nds_rom(valid_spec);
        const u32 valid_off = read_le32(valid_rom, 0x068U);
        expect_banner_crc_validity(valid_rom, valid_off, true);

        NdsRomSpec corrupt_spec = valid_spec;
        corrupt_spec.banner.corrupt_crc = true;
        const std::vector<u8> corrupt_rom = build_valid_nds_rom(corrupt_spec);
        const u32 corrupt_off = read_le32(corrupt_rom, 0x068U);
        expect_banner_crc_validity(corrupt_rom, corrupt_off, false);
    }
}

TEST(NdsRomBuilderEdgeTest, AlignsBannerAtExact512ByteAndPre8000ExtraOffsets) {
    NdsRomSpec exact_spec{};
    exact_spec.extra_rom_offset = 0x8000U;
    exact_spec.extra_rom_payload = std::vector<u8>(0x0400U, 0x33U);
    exact_spec.banner.include = true;

    const std::vector<u8> exact_rom = build_valid_nds_rom(exact_spec);
    const u32 exact_banner_off = read_le32(exact_rom, 0x068U);
    EXPECT_EQ(exact_banner_off, 0x8400U);
    EXPECT_EQ(exact_rom[0x8000U], 0x33U);
    EXPECT_EQ(exact_rom[0x83FFU], 0x33U);
    expect_banner_crc_validity(exact_rom, exact_banner_off, true);

    NdsRomSpec pre_spec{};
    pre_spec.extra_rom_offset = 0x2000U;
    pre_spec.extra_rom_payload = std::vector<u8>(0x0100U, 0x44U);
    pre_spec.banner.include = true;

    const std::vector<u8> pre_rom = build_valid_nds_rom(pre_spec);
    const u32 pre_banner_off = read_le32(pre_rom, 0x068U);
    EXPECT_EQ(pre_banner_off, 0x8000U);
    EXPECT_EQ(pre_rom[0x2000U], 0x44U);
    EXPECT_EQ(pre_rom[0x20FFU], 0x44U);
    expect_banner_crc_validity(pre_rom, pre_banner_off, true);
}

TEST(NdsRomBuilderEdgeTest, ThrowsOverflowWithoutBannerWhenSizeExceedsU32) {
    NdsRomSpec spec{};
    spec.banner.include = false;
    spec.extra_rom_offset = 0xFFFFFE00U;
    spec.extra_rom_payload = std::vector<u8>(0x300U, 0x11U);

    EXPECT_THROW((void)build_valid_nds_rom(spec), std::out_of_range);
}

TEST(ArmProgramEmitterEdgeTest, RejectsMultiChunkSubFlagsAndAllowsRotatedSingleChunk) {
    ArmProgramEmitter emit(0x02000000U);
    const std::size_t initial_words = emit.word_count();

    EXPECT_THROW(emit.emit_sub_imm(RegImmOp{ArmReg::R1, ArmReg::R0, 0x00010001U, true}),
                 std::invalid_argument);
    EXPECT_EQ(emit.word_count(), initial_words);

    emit.emit_sub_imm(RegImmOp{ArmReg::R1, ArmReg::R0, 0xFF000000U, true});
    ASSERT_EQ(emit.word_count(), initial_words + 1U);
    EXPECT_NE(emit.words().back() & (1U << 20U), 0U);

    emit.emit_add_imm(RegImmOp{ArmReg::R2, ArmReg::R1, 0xFF000000U, true});
    ASSERT_EQ(emit.word_count(), initial_words + 2U);
    EXPECT_NE(emit.words().back() & (1U << 20U), 0U);
}

}  // namespace
}  // namespace elmasds::e2e
