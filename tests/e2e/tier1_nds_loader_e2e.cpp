#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr u32 kDoneMagic = 0xD04EF210U;
constexpr Address kBannerRamDst = 0x02200000U;
constexpr Address kDsiIconRamDst = 0x02201000U;
constexpr Address kRamHeaderBannerOffset = kRamHeaderBase + 0x068U;

struct BannerReadReq {
    u32 rom_offset = 0U;
    Address dst_ram = kBannerRamDst;
    u32 block_code = 4U;
    u32 word_count = 1024U;
};

[[nodiscard]] std::vector<u8> read_main_ram_bytes(const EmulatorE2eHarness& harness,
                                                  Address addr,
                                                  std::size_t size) {
    std::vector<u8> bytes(size);
    for (std::size_t i = 0U; i < size; ++i) {
        bytes[i] = harness.read_main_ram8(addr + static_cast<Address>(i));
    }
    return bytes;
}

void emit_banner_read_0xb7(ArmProgramEmitter& emit, const BannerReadReq& req) {
    emit_rom_cmd_0xb7(emit, req.rom_offset);
    emit_start_romctrl(emit, req.block_code);
    emit_read_romdata_words(emit, req.dst_ram, req.word_count);
}

[[nodiscard]] std::vector<u8> build_chip_id_rom(u32 min_total_size) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_rom_cmd_words(emit, 0x000000B8U, 0x00000000U);
    emit_start_romctrl(emit, 7U);
    emit_wait_rom_drq(emit, ArmReg::R0);
    emit.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(emit, 0U, ArmReg::R0);
    emit_store_const_result(emit, 1U, kDoneMagic);
    emit_halt_loop(emit);

    NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    spec.min_total_size = min_total_size;
    return build_valid_nds_rom(spec);
}

void expect_utf16_ascii(std::span<const u8> utf16_bytes, std::string_view expected) {
    ASSERT_GE(utf16_bytes.size(), (expected.size() + 1U) * 2U);
    for (std::size_t i = 0U; i < expected.size(); ++i) {
        EXPECT_EQ(read_le16(utf16_bytes, i * 2U), static_cast<u16>(expected[i]));
    }
    EXPECT_EQ(read_le16(utf16_bytes, expected.size() * 2U), 0U);
}

// F21: .nds Header Parser (5 Tier 1 tests)

TEST(Tier1NdsLoaderE2e, F21LoadsRomBytesAndCopiesHeaderToMainRam) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_store_const_result(emit, 0U, kDoneMagic);
    emit_halt_loop(emit);

    NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    spec.title = "LOADERTEST";
    spec.gamecode = "LTST";
    spec.makercode = "42";

    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, spec, 20'000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kDoneMagic);

    const std::vector<u8> hdr = read_main_ram_bytes(harness, kRamHeaderBase, 18U);
    const std::vector<u8> expected = {
        'L', 'O', 'A', 'D', 'E', 'R', 'T', 'E', 'S',
        'T', 0U,  0U,  'L', 'T', 'S', 'T', '4', '2',
    };
    EXPECT_EQ(hdr, expected);
}

TEST(Tier1NdsLoaderE2e, F21LoadsValidRomFileViaScopedTempDir) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_store_const_result(emit, 0U, 0x11223344U);
    emit_halt_loop(emit);

    const NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    const std::vector<u8> rom = build_valid_nds_rom(spec);

    ScopedTempDir temp_dir;
    const auto rom_path = temp_dir.write_file("valid_loader.nds", rom);

    EmulatorE2eHarness harness;
    const Status load_status = harness.load_rom_file(rom_path);
    ASSERT_TRUE(load_status.is_ok()) << load_status.error().message;
    const Status boot_status = harness.direct_boot();
    ASSERT_TRUE(boot_status.is_ok()) << boot_status.error().message;
    harness.step_ticks(20'000ULL);

    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x11223344U);
}

TEST(Tier1NdsLoaderE2e, F21ExecutesCustomArm9AndArm7OffsetsAndEntryAddresses) {
    constexpr Address kArm9Ram = 0x02004000U;
    constexpr Address kArm9Entry = 0x02004004U;
    constexpr Address kArm7Ram = 0x03802000U;
    constexpr Address kArm7Entry = 0x03802004U;

    ArmProgramEmitter arm9_emit(kArm9Ram);
    arm9_emit.emit_branch_self();
    emit_store_const_result(arm9_emit, 0U, 0xA9004200U);
    emit_halt_loop(arm9_emit);

    ArmProgramEmitter arm7_emit(kArm7Ram);
    arm7_emit.emit_branch_self();
    emit_store_const_result(arm7_emit, 1U, 0xA7008200U);
    emit_halt_loop(arm7_emit);

    NdsRomSpec spec{};
    spec.arm9_rom_offset = 0x4200U;
    spec.arm9_ram_addr = kArm9Ram;
    spec.arm9_entry_addr = kArm9Entry;
    spec.arm9_code = arm9_emit.to_bytes();
    spec.arm7_rom_offset = 0x8200U;
    spec.arm7_ram_addr = kArm7Ram;
    spec.arm7_entry_addr = kArm7Entry;
    spec.arm7_code = arm7_emit.to_bytes();

    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, spec, 30'000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xA9004200U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xA7008200U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x020U), 0x4200U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x024U), kArm9Entry);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x028U), kArm9Ram);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x030U), 0x8200U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x034U), kArm7Entry);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x038U), kArm7Ram);
}

TEST(Tier1NdsLoaderE2e, F21PreservesCapacityRomctrlVersionAndAutostartInRamHeader) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.unitcode = 0x02U;
    spec.encryption_seed = 0x05U;
    spec.device_capacity = 4U;
    spec.rom_version = 0x07U;
    spec.autostart_flags = 0x04U;
    spec.normal_romctrl = 0x00416657U;
    spec.key1_romctrl = 0x081808F8U;

    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, spec, 10'000ULL);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x012U), 0x02U);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x013U), 0x05U);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x014U), 4U);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x01EU), 0x07U);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x01FU), 0x04U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x060U), 0x00416657U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x064U), 0x081808F8U);
}

TEST(Tier1NdsLoaderE2e, F21ComputesChipId0xB8FromRomSize) {
    EmulatorE2eHarness small_harness;
    boot_bytes_and_step(small_harness, build_chip_id_rom(0U), 30'000ULL);
    EXPECT_EQ(small_harness.read_main_ram32(result_slot_addr(1U)), kDoneMagic);
    EXPECT_EQ(small_harness.read_main_ram32(result_slot_addr(0U)), 0x000000C2U);

    EmulatorE2eHarness two_mib_harness;
    boot_bytes_and_step(two_mib_harness, build_chip_id_rom(2U * 1024U * 1024U), 30'000ULL);
    EXPECT_EQ(two_mib_harness.read_main_ram32(result_slot_addr(1U)), kDoneMagic);
    EXPECT_EQ(two_mib_harness.read_main_ram32(result_slot_addr(0U)), 0x000001C2U);
}

// F22: .nds CRC-16 Verification & Banner Metadata (5 Tier 1 tests)

TEST(Tier1NdsLoaderE2e, F22VerifiesSynthesizedLogoCrc0xCF56InRamHeader) {
    const NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, spec, 10'000ULL);

    EXPECT_EQ(harness.read_main_ram16(kRamHeaderBase + 0x15CU), kExpectedLogoCrc16);
    const std::vector<u8> logo = read_main_ram_bytes(
        harness, kRamHeaderBase + 0x0C0U, kNintendoLogoSize);
    EXPECT_EQ(compute_crc16(logo), kExpectedLogoCrc16);
}

TEST(Tier1NdsLoaderE2e, F22CopiesHeaderAndSecureAreaCrcsToPostBiosSlots) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_store_const_result(emit, 0U, 0xCAFE1234U);
    emit_halt_loop(emit);

    const NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    const std::vector<u8> rom = build_valid_nds_rom(spec);
    const u16 hdr_crc = read_le16(rom, 0x15EU);
    const u16 sec_crc = read_le16(rom, 0x06CU);
    ASSERT_NE(sec_crc, 0U);
    ASSERT_EQ(sec_crc, compute_crc16(std::span<const u8>(rom).subspan(0x4000U, 0x4000U)));

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 20'000ULL);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x008U), hdr_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x074U), hdr_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x408U), hdr_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x00AU), sec_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x076U), sec_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x40AU), sec_crc);
}

TEST(Tier1NdsLoaderE2e, F22ReadsBannerV1MetadataViaRom0xB7) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.banner.include = true;
    spec.banner.version = 0x0001U;
    spec.banner.title_english = u"E2E Banner";

    std::vector<u8> probe_rom = build_valid_nds_rom(spec);
    const u32 banner_off = read_le32(probe_rom, 0x068U);
    ASSERT_GE(banner_off, kNdsSecureAreaEnd);

    ArmProgramEmitter emit(kArm9DefaultBase);
    emit.emit_read32_abs(ArmReg::R0, kRamHeaderBannerOffset);
    emit_store_result(emit, 0U, ArmReg::R0);
    emit_banner_read_0xb7(emit, BannerReadReq{banner_off, kBannerRamDst, 4U, 1024U});
    emit_store_const_result(emit, 1U, kDoneMagic);
    emit_halt_loop(emit);

    spec.arm9_code = emit.to_bytes();
    const std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_EQ(read_le32(rom, 0x068U), banner_off);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 100'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(1U)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), banner_off);

    const std::vector<u8> banner = read_main_ram_bytes(harness, kBannerRamDst, kBannerSizeV1);
    EXPECT_EQ(read_le16(banner, 0x000U), 0x0001U);
    EXPECT_EQ(read_le16(banner, 0x002U),
              compute_crc16(std::span<const u8>(banner).subspan(0x020U, 0x820U)));
    expect_utf16_ascii(std::span<const u8>(banner).subspan(0x340U, 0x100U), "E2E Banner");
}

TEST(Tier1NdsLoaderE2e, F22ReadsBannerV3ExtendedCrcsViaRom0xB7) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.banner.include = true;
    spec.banner.version = 0x0003U;
    spec.banner.title_english = u"Banner V3";

    const u32 banner_off = read_le32(build_valid_nds_rom(spec), 0x068U);
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_banner_read_0xb7(emit, BannerReadReq{banner_off, kBannerRamDst, 4U, 1024U});
    emit_store_const_result(emit, 0U, kDoneMagic);
    emit_halt_loop(emit);

    spec.arm9_code = emit.to_bytes();
    const std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_EQ(read_le32(rom, 0x068U), banner_off);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 100'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kDoneMagic);

    const std::vector<u8> banner = read_main_ram_bytes(harness, kBannerRamDst, 0x0A40U);
    const std::span<const u8> span(banner);
    EXPECT_EQ(read_le16(span, 0x000U), 0x0003U);
    EXPECT_EQ(read_le16(span, 0x002U), compute_crc16(span.subspan(0x020U, 0x820U)));
    EXPECT_EQ(read_le16(span, 0x004U), compute_crc16(span.subspan(0x020U, 0x920U)));
    EXPECT_EQ(read_le16(span, 0x006U), compute_crc16(span.subspan(0x020U, 0xA20U)));
}

TEST(Tier1NdsLoaderE2e, F22ReadsBannerDsi0x0103CrcAndAnimatedIconViaRom0xB7) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.banner.include = true;
    spec.banner.version = 0x0103U;

    const u32 banner_off = read_le32(build_valid_nds_rom(spec), 0x068U);
    ArmProgramEmitter emit(kArm9DefaultBase);
    emit_banner_read_0xb7(emit, BannerReadReq{banner_off, kBannerRamDst, 1U, 128U});
    emit_banner_read_0xb7(emit, BannerReadReq{banner_off + 0x1240U, kDsiIconRamDst, 5U, 2048U});
    emit_store_const_result(emit, 0U, kDoneMagic);
    emit_halt_loop(emit);

    spec.arm9_code = emit.to_bytes();
    std::vector<u8> rom = build_valid_nds_rom(spec);
    ASSERT_EQ(read_le32(rom, 0x068U), banner_off);
    write_le32(rom, banner_off + 0x1240U, 0xA55A1234U);
    write_le32(rom, banner_off + 0x23BCU, 0xCAFEBABEU);
    const u16 dsi_crc = compute_crc16(std::span<const u8>(rom).subspan(
        banner_off + 0x1240U, 0x1180U));
    write_le16(rom, banner_off + 0x008U, dsi_crc);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 200'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kDoneMagic);

    EXPECT_EQ(harness.read_main_ram16(kBannerRamDst + 0x000U), 0x0103U);
    EXPECT_EQ(harness.read_main_ram32(kDsiIconRamDst + 0x0000U), 0xA55A1234U);
    EXPECT_EQ(harness.read_main_ram32(kDsiIconRamDst + 0x117CU), 0xCAFEBABEU);
    const std::vector<u8> anim = read_main_ram_bytes(harness, kDsiIconRamDst, 0x1180U);
    EXPECT_EQ(harness.read_main_ram16(kBannerRamDst + 0x008U), compute_crc16(anim));
}

}  // namespace
}  // namespace elmasds::e2e::m3
