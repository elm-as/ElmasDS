#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <span>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr u32 kBiosSwiBranchTo0x100 = 0xEA000032U;
constexpr u32 kArmMovsPcLr = 0xE1B0F00EU;

[[nodiscard]] std::vector<u8> build_bios_with_swi_sentinel(bool is_bios9, u32 slot, u32 magic) {
    std::vector<u8> bios = is_bios9 ? build_synthetic_bios9() : build_synthetic_bios7();
    write_le32(bios, 0x030U, kBiosSwiBranchTo0x100);
    const Address handler_base = is_bios9 ? 0xFFFF0100U : 0x00000100U;
    ArmProgramEmitter handler(handler_base);
    emit_store_const_result(handler, slot, magic);
    handler.emit_word(kArmMovsPcLr);
    const std::vector<u8> bytes = handler.to_bytes();
    std::copy(bytes.begin(), bytes.end(), bios.begin() + 0x100);
    return bios;
}

[[nodiscard]] std::vector<u8> read_ram_span(const EmulatorE2eHarness& harness,
                                            Address start,
                                            std::size_t count) {
    std::vector<u8> out(count, 0U);
    for (std::size_t i = 0U; i < count; ++i) {
        out[i] = harness.read_main_ram8(static_cast<Address>(start + i));
    }
    return out;
}

void emit_postflg_probe(ArmProgramEmitter& emit, u32 base_slot) {
    emit_read8_abs(emit, ArmReg::R0, kRegPostFlg);
    emit_store_result(emit, base_slot + 0U, ArmReg::R0);
    emit.emit_write8_abs(kRegPostFlg, 0x00U);
    emit_read8_abs(emit, ArmReg::R0, kRegPostFlg);
    emit_store_result(emit, base_slot + 1U, ArmReg::R0);
    emit.emit_write8_abs(kRegPostFlg, 0xFFU);
    emit_read8_abs(emit, ArmReg::R0, kRegPostFlg);
    emit_store_result(emit, base_slot + 2U, ArmReg::R0);
    emit.emit_write8_abs(kRegPostFlg, 0x00U);
    emit_read8_abs(emit, ArmReg::R0, kRegPostFlg);
    emit_store_result(emit, base_slot + 3U, ArmReg::R0);
    emit_halt_loop(emit);
}

TEST(Tier2BootE2e, F29_RejectsWrongSizeBios9) {
    EmulatorE2eHarness harness;
    for (const std::size_t bad_size : {std::size_t{0U}, kBios9Size - 1U, kBios9Size + 1U}) {
        const std::vector<u8> bad(bad_size, 0U);
        const Status status = harness.load_bios9(bad);
        EXPECT_TRUE(status.is_err()) << "size=" << bad_size;
        EXPECT_EQ(status.error().code, ErrorCode::CorruptData);
    }
}

TEST(Tier2BootE2e, F29_RejectsWrongSizeBios7) {
    EmulatorE2eHarness harness;
    for (const std::size_t bad_size : {std::size_t{0U}, kBios7Size - 1U, kBios7Size + 1U}) {
        const std::vector<u8> bad(bad_size, 0U);
        const Status status = harness.load_bios7(bad);
        EXPECT_TRUE(status.is_err()) << "size=" << bad_size;
        EXPECT_EQ(status.error().code, ErrorCode::CorruptData);
    }
}

class Tier2InvalidFirmwareSizeE2e : public ::testing::TestWithParam<u32> {};

TEST_P(Tier2InvalidFirmwareSizeE2e, F29_RejectsInvalidFirmwareSize) {
    EmulatorE2eHarness harness;
    const std::vector<u8> bad(GetParam(), 0xFFU);
    const Status status = harness.load_firmware(bad);
    EXPECT_TRUE(status.is_err());
    EXPECT_EQ(status.error().code, ErrorCode::CorruptData);
}

INSTANTIATE_TEST_SUITE_P(InvalidFirmwareSizes,
                         Tier2InvalidFirmwareSizeE2e,
                         ::testing::Values(0U,
                                           64U * 1024U,
                                           kFirmwareSize128K + 1U,
                                           192U * 1024U,
                                           kFirmwareSize256K - 1U));

TEST(Tier2BootE2e, F29_Arm7BiosProtectionBlocksReadsFromWram) {
    EmulatorE2eHarness harness;
    const std::vector<u8> bios7 = build_synthetic_bios7();
    ASSERT_NE(read_le32(bios7, 0x040U), 0U);
    ASSERT_NE(read_le32(bios7, 0x028U), 0U);
    ASSERT_TRUE(harness.load_bios7(bios7).is_ok());

    ArmProgramEmitter arm7(kArm7DefaultBase);
    arm7.emit_read32_abs(ArmReg::R0, 0x00000040U);
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, 0x00000028U);
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_store_const_result(arm7, 2U, 0xA7B10501U);
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7.words()), 800U);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0xA7B10501U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0U);
}

TEST(Tier2BootE2e, F29_CorruptFirmwareCrcsDetectedViaArm7Spi) {
    FirmwareSpec spec{};
    spec.size_bytes = kFirmwareSize256K;
    spec.corrupt_wifi_crc = true;
    spec.corrupt_user_settings_crc = true;

    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(spec)).is_ok());

    const Address wifi_ram = result_slot_addr(0U);
    const Address user_ram = result_slot_addr(90U);
    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{wifi_ram, 0x00002AU, 0x013AU});
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{user_ram, 0x03FF00U, 0x0074U});
    emit_store_const_result(arm7, 130U, 0xC4C5BAD0U);
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7.words()), 40000U);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(130U)), 0xC4C5BAD0U);

    const std::vector<u8> wifi_bytes = read_ram_span(harness, wifi_ram, 0x013AU);
    const u16 wifi_stored_crc = read_le16(wifi_bytes, 0U);
    const u16 wifi_calc_crc =
        compute_crc16(std::span<const u8>(wifi_bytes).subspan(2U, 0x0138U), 0x0000U);
    EXPECT_NE(wifi_stored_crc, wifi_calc_crc);

    const std::vector<u8> user_bytes = read_ram_span(harness, user_ram, 0x0074U);
    const u16 user_stored_crc = read_le16(user_bytes, 0x072U);
    const u16 user_calc_crc =
        compute_crc16(std::span<const u8>(user_bytes).subspan(0U, 0x070U), kCrc16DefaultInit);
    EXPECT_NE(user_stored_crc, user_calc_crc);
}

TEST(Tier2BootE2e, F29_Firmware128KSkipsUserSettingsCopyToMainRam) {
    FirmwareSpec spec{};
    spec.size_bytes = kFirmwareSize128K;
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(spec)).is_ok());

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(0U), 0x01FF00U, 4U});
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7.words()), 8000U);
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings), 0U);
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x006U), 0U);
    EXPECT_EQ(harness.read_main_ram16(result_slot_addr(0U)), 5U);
}

TEST(Tier2BootE2e, F30_DirectBootFailsWithoutRomAndStepTicksIsSafe) {
    EmulatorE2eHarness harness;
    const Status boot_status = harness.direct_boot();
    EXPECT_TRUE(boot_status.is_err());
    EXPECT_EQ(boot_status.error().code, ErrorCode::NotInitialized);

    const Address initial_pc9 = harness.arm9_pc();
    const Address initial_pc7 = harness.arm7_pc();
    harness.step_ticks(100U);
    EXPECT_EQ(harness.current_tick(), 100ULL);
    EXPECT_EQ(harness.arm9_pc(), initial_pc9);
    EXPECT_EQ(harness.arm7_pc(), initial_pc7);
}

TEST(Tier2BootE2e, F30_DirectBootWithZeroSizeBinariesSucceedsSafely) {
    NdsRomSpec spec{};
    spec.title = "ZEROBINS";
    spec.arm9_code = {};
    spec.arm7_code = {};
    const std::vector<u8> rom = build_valid_nds_rom(spec);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 200U);
    EXPECT_EQ(harness.current_tick(), 200ULL);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase), 0x4F52455AU);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x440U), 1U);
}

TEST(Tier2BootE2e, F30_DirectBootPreservesPreloadedBiosInsteadOfHleStubs) {
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_bios9(build_bios_with_swi_sentinel(true, 0U, 0xB1059999U)).is_ok());
    ASSERT_TRUE(harness.load_bios7(build_bios_with_swi_sentinel(false, 1U, 0xB1057777U)).is_ok());

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_swi(0U);
    emit_store_const_result(arm9, 2U, 0x99990001U);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    arm7.emit_swi(0U);
    emit_store_const_result(arm7, 3U, 0x77770001U);
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), arm7.words()), 1200U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xB1059999U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xB1057777U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x99990001U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x77770001U);
}

TEST(Tier2BootE2e, F30_PostflgLatchesBit0AndIgnoresClearWrite) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_postflg_probe(arm9, 0U);
    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_postflg_probe(arm7, 4U);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), arm7.words()), 1200U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 3U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 1U);

    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(4U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(5U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(6U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(7U)), 1U);
}

TEST(Tier2BootE2e, F30_ResetResetsTickAndHaltsExecutionUntilNextDirectBoot) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_mov_imm(ArmReg::R2, 0U);
    arm9.emit_mov_imm(ArmReg::R1, result_slot_addr(0U));
    const Address loop_pc = arm9.current_pc();
    arm9.emit_add_imm(RegImmOp{ArmReg::R2, ArmReg::R2, 1U});
    arm9.emit_str32(MemOffsetOp{ArmReg::R2, ArmReg::R1, 0});
    arm9.emit_b(loop_pc);

    EmulatorE2eHarness harness;
    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), make_idle_words()), 500U);
    EXPECT_EQ(harness.current_tick(), 500ULL);
    EXPECT_GT(harness.read_main_ram32(result_slot_addr(0U)), 0U);

    harness.reset();
    EXPECT_EQ(harness.current_tick(), 0ULL);
    EXPECT_EQ(harness.arm9_pc(), 0xFFFF0000U);
    EXPECT_EQ(harness.arm7_pc(), 0x00000000U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0U);

    harness.step_ticks(500U);
    EXPECT_EQ(harness.current_tick(), 500ULL);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0U);

    ASSERT_TRUE(harness.direct_boot().is_ok());
    harness.step_ticks(500U);
    EXPECT_GT(harness.read_main_ram32(result_slot_addr(0U)), 0U);
}

}  // namespace
}  // namespace elmasds::e2e::m3
