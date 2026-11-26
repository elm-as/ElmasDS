#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr u32 kDoneMagic = 0xD04EF220U;
constexpr Address kBannerRamDst = 0x02200000U;

void expect_corrupt_rom(std::span<const u8> rom_bytes) {
    EmulatorE2eHarness harness;
    const Status status = harness.load_rom_bytes(rom_bytes);
    EXPECT_TRUE(status.is_err());
    if (status.is_err()) {
        EXPECT_EQ(status.error().code, ErrorCode::CorruptData);
    }
}

[[nodiscard]] std::vector<u8> read_main_ram_bytes(const EmulatorE2eHarness& harness,
                                                  Address addr,
                                                  std::size_t size) {
    std::vector<u8> bytes(size);
    for (std::size_t i = 0U; i < size; ++i) {
        bytes[i] = harness.read_main_ram8(addr + static_cast<Address>(i));
    }
    return bytes;
}

void recompute_header_crc_only(std::vector<u8>& rom) {
    ASSERT_GE(rom.size(), kNdsMinHeaderSize);
    const u16 crc = compute_crc16(std::span<const u8>(rom).subspan(0U, 0x15EU));
    write_le16(rom, 0x15EU, crc);
}

// F21: .nds Header Parser Boundary & Negative Cases (5 Tier 2 tests)

TEST(Tier2NdsLoaderE2e, F21RejectsRomSmallerThan0x160Bytes) {
    expect_corrupt_rom(std::vector<u8>{});
    expect_corrupt_rom(std::vector<u8>{0x00U});

    const std::vector<u8> valid_rom = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));
    const std::vector<u8> near_min = truncate_rom(valid_rom, kNdsMinHeaderSize - 1U);
    ASSERT_EQ(near_min.size(), 0x15FU);
    expect_corrupt_rom(near_min);
}

TEST(Tier2NdsLoaderE2e, F21AcceptsExactMinimum0x160ByteHeader) {
    NdsRomSpec spec{};
    spec.arm9_rom_offset = kNdsMinHeaderSize;
    spec.arm7_rom_offset = kNdsMinHeaderSize;
    spec.header_size = kNdsMinHeaderSize;
    spec.arm9_code.clear();
    spec.arm7_code.clear();

    const std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_EQ(rom.size(), kNdsMinHeaderSize);

    EmulatorE2eHarness harness;
    const Status load_status = harness.load_rom_bytes(rom);
    ASSERT_TRUE(load_status.is_ok()) << load_status.error().message;
    const Status boot_status = harness.direct_boot();
    EXPECT_TRUE(boot_status.is_ok()) << boot_status.error().message;
}

TEST(Tier2NdsLoaderE2e, F21RejectsOutOfBoundsAndOverflowArm9AndArm7Ranges) {
    const std::vector<u8> base = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));
    const u32 rom_size = static_cast<u32>(base.size());
    const u32 arm9_off = read_le32(base, 0x020U);
    const u32 arm7_off = read_le32(base, 0x030U);

    std::vector<u8> arm9_oob = base;
    patch_u32_and_recompute_header_crc(arm9_oob, 0x02CU, (rom_size - arm9_off) + 1U);
    expect_corrupt_rom(arm9_oob);

    std::vector<u8> arm7_oob = base;
    patch_u32_and_recompute_header_crc(arm7_oob, 0x03CU, (rom_size - arm7_off) + 1U);
    expect_corrupt_rom(arm7_oob);

    std::vector<u8> arm9_wrap = base;
    write_le32(arm9_wrap, 0x020U, 0xFFFFFFF0U);
    patch_u32_and_recompute_header_crc(arm9_wrap, 0x02CU, 0x20U);
    expect_corrupt_rom(arm9_wrap);

    std::vector<u8> arm7_wrap = base;
    write_le32(arm7_wrap, 0x030U, 0xFFFFFFF0U);
    patch_u32_and_recompute_header_crc(arm7_wrap, 0x03CU, 0x20U);
    expect_corrupt_rom(arm7_wrap);
}

TEST(Tier2NdsLoaderE2e, F21RejectsNonExistentRomFile) {
    ScopedTempDir temp_dir;
    EmulatorE2eHarness harness;
    const Status status = harness.load_rom_file(temp_dir.path() / "nonexistent_rom.nds");
    EXPECT_TRUE(status.is_err());
    if (status.is_err()) {
        EXPECT_EQ(status.error().code, ErrorCode::FileNotFound);
    }
}

TEST(Tier2NdsLoaderE2e, F21HandlesMax12CharNonNulTitleAndClampedDeviceCapacity) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_rom_cmd_words(emit, 0x000000B8U, 0x00000000U);
    emit_start_romctrl(emit, 7U);
    emit_wait_rom_drq(emit, ArmReg::R0);
    emit.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(emit, 0U, ArmReg::R0);
    emit_store_const_result(emit, 1U, kDoneMagic);
    emit_halt_loop(emit);

    NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    spec.title = "MAXTITLE12CH";
    spec.gamecode = "ABCD";
    spec.device_capacity = 255U;

    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, spec, 30'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(1U)), kDoneMagic);

    const std::vector<u8> hdr = read_main_ram_bytes(harness, kRamHeaderBase, 16U);
    const std::vector<u8> expected = {
        'M', 'A', 'X', 'T', 'I', 'T', 'L', 'E',
        '1', '2', 'C', 'H', 'A', 'B', 'C', 'D',
    };
    EXPECT_EQ(hdr, expected);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x014U), 255U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x000000C2U);
}

// F22: .nds CRC-16 & Banner Boundary & Negative Cases (5 Tier 2 tests)

TEST(Tier2NdsLoaderE2e, F22RejectsCorruptHeaderCrc) {
    std::vector<u8> rom = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));
    corrupt_header_crc(rom);
    expect_corrupt_rom(rom);
}

TEST(Tier2NdsLoaderE2e, F22RejectsSingleBitFlipInHeaderWithoutUpdatingCrc) {
    const std::vector<u8> base = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));

    constexpr std::array<std::size_t, 3U> kOffsets = {0x000U, 0x014U, 0x02CU};
    for (const std::size_t off : kOffsets) {
        std::vector<u8> mutated = base;
        mutated[off] ^= 0x01U;
        expect_corrupt_rom(mutated);
    }
}

TEST(Tier2NdsLoaderE2e, F22RejectsCorruptLogoCrcEvenWhenHeaderCrcRecomputed) {
    std::vector<u8> rom = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));
    corrupt_logo_crc(rom);
    recompute_header_crc_only(rom);
    expect_corrupt_rom(rom);
}

TEST(Tier2NdsLoaderE2e, F22RejectsOneByteLogoTamperEvenWhenHeaderCrcRecomputed) {
    const std::vector<u8> base = build_valid_nds_rom(
        make_base_rom_spec(make_idle_words(), make_idle_words()));

    constexpr std::array<std::size_t, 3U> kLogoOffsets = {0x0C0U, 0x100U, 0x15BU};
    for (const std::size_t off : kLogoOffsets) {
        std::vector<u8> tampered = base;
        tampered[off] ^= 0x01U;
        recompute_header_crc_only(tampered);
        expect_corrupt_rom(tampered);
    }
}

TEST(Tier2NdsLoaderE2e, F22HandlesCorruptSecureAreaAndBannerCrcsGracefully) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.banner.include = true;
    spec.banner.version = 0x0001U;
    spec.banner.corrupt_crc = true;
    spec.corrupt_secure_crc = true;

    const u32 banner_off = read_le32(build_valid_nds_rom(spec), 0x068U);
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_rom_cmd_0xb7(emit, banner_off);
    emit_start_romctrl(emit, 4U);
    emit_read_romdata_words(emit, kBannerRamDst, 1024U);
    emit_store_const_result(emit, 0U, kDoneMagic);
    emit_halt_loop(emit);

    spec.arm9_code = emit.to_bytes();
    const std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_EQ(read_le32(rom, 0x068U), banner_off);

    const u16 actual_sec_crc = compute_crc16(
        std::span<const u8>(rom).subspan(0x4000U, 0x4000U));
    const u16 header_sec_crc = read_le16(rom, 0x06CU);
    ASSERT_NE(header_sec_crc, actual_sec_crc);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 100'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kDoneMagic);

    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x00AU), header_sec_crc);
    EXPECT_NE(harness.read_main_ram16(kRamPostBiosBase + 0x00AU), actual_sec_crc);

    const std::vector<u8> banner = read_main_ram_bytes(harness, kBannerRamDst, kBannerSizeV1);
    const u16 stored_banner_crc = read_le16(banner, 0x002U);
    const u16 computed_banner_crc = compute_crc16(
        std::span<const u8>(banner).subspan(0x020U, 0x820U));
    EXPECT_EQ(stored_banner_crc, read_le16(rom, banner_off + 0x002U));
    EXPECT_NE(stored_banner_crc, computed_banner_crc);
}

}  // namespace
}  // namespace elmasds::e2e::m3
