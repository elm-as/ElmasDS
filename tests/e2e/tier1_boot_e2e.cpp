#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr Address kRegTm0CntL = 0x04000100U;
constexpr Address kRegTm0CntH = 0x04000102U;
constexpr u32 kIrqVBlankBit = 1U << 0U;
constexpr u32 kIrqTimer0Bit = 1U << 3U;
constexpr u32 kBiosSwiBranchTo0x100 = 0xEA000032U;
constexpr u32 kArmMovsPcLr = 0xE1B0F00EU;
constexpr u32 kArmMovR1Sp = 0xE1A0100DU;

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

[[nodiscard]] std::vector<u32> build_timer0_irq_arm7_program(u32 slot, u32 magic) {
    ArmProgramEmitter handler(kArm7DefaultBase + 4U);
    emit_store_const_result(handler, slot, magic);
    handler.emit_write16_abs(kRegTm0CntH, 0U);
    handler.emit_write32_abs(kRegIf, kIrqTimer0Bit);
    handler.emit_bx(ArmReg::Lr);

    ArmProgramEmitter prog(kArm7DefaultBase);
    prog.emit_b(handler.current_pc());
    for (const u32 word : handler.words()) {
        prog.emit_word(word);
    }
    prog.emit_irq_trampoline_install(kArm7IrqSlot, kArm7DefaultBase + 4U);
    prog.emit_enable_irq(ArmReg::R0);
    prog.emit_write32_abs(kRegIe, kIrqTimer0Bit);
    prog.emit_write32_abs(kRegIme, 1U);
    prog.emit_write16_abs(kRegTm0CntL, 0xFF00U);
    prog.emit_write16_abs(kRegTm0CntH, 0x00C0U);
    emit_halt_loop(prog);
    return prog.words();
}

void emit_read_stacks_and_postflg(ArmProgramEmitter& emit, u32 base_slot) {
    emit.emit_mrs_cpsr(ArmReg::R0);
    emit_store_result(emit, base_slot + 0U, ArmReg::R0);
    emit.emit_word(kArmMovR1Sp);
    emit_store_result(emit, base_slot + 1U, ArmReg::R1);
    emit.emit_msr_cpsr_c_imm(0xD2U);
    emit.emit_word(kArmMovR1Sp);
    emit_store_result(emit, base_slot + 2U, ArmReg::R1);
    emit.emit_msr_cpsr_c_imm(0xD3U);
    emit.emit_word(kArmMovR1Sp);
    emit_store_result(emit, base_slot + 3U, ArmReg::R1);
    emit.emit_msr_cpsr_c_imm(0xDFU);
    emit_read8_abs(emit, ArmReg::R1, kRegPostFlg);
    emit_store_result(emit, base_slot + 4U, ArmReg::R1);
    emit_halt_loop(emit);
}

[[nodiscard]] std::vector<u32> make_tight_counter_loop(Address base, u32 slot) {
    ArmProgramEmitter emit(base);
    emit.emit_mov_imm(ArmReg::R2, 0U);
    emit.emit_mov_imm(ArmReg::R1, result_slot_addr(slot));
    const Address loop_pc = emit.current_pc();
    emit.emit_add_imm(RegImmOp{ArmReg::R2, ArmReg::R2, 1U});
    emit.emit_str32(MemOffsetOp{ArmReg::R2, ArmReg::R1, 0});
    emit.emit_b(loop_pc);
    return emit.words();
}

TEST(Tier1BootE2e, F29_LoadsValidBios9AndExecutesSwi) {
    EmulatorE2eHarness harness;
    const std::vector<u8> bios9 = build_bios_with_swi_sentinel(true, 0U, 0xB1059009U);
    ASSERT_TRUE(harness.load_bios9(bios9).is_ok());

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_swi(0U);
    emit_store_const_result(arm9, 1U, 0x900D9009U);
    emit_halt_loop(arm9);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), make_idle_words()), 800U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xB1059009U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x900D9009U);
}

TEST(Tier1BootE2e, F29_Bios9SwiHaltVectorHaltsUntilIrqWake) {
    EmulatorE2eHarness harness;
    const std::vector<u8> bios9 = build_synthetic_bios9(BiosSpec{.include_swi_halt = true});
    ASSERT_TRUE(harness.load_bios9(bios9).is_ok());

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write32_abs(kRegIe, kIrqVBlankBit);
    arm9.emit_write32_abs(kRegIme, 0U);
    arm9.emit_swi(0U);
    emit_store_const_result(arm9, 0U, 0xAA11BB22U);
    emit_halt_loop(arm9);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), make_idle_words()), 600U);
    EXPECT_TRUE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0U);

    harness.trigger_vblank();
    harness.step_ticks(400U);
    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xAA11BB22U);
}

TEST(Tier1BootE2e, F29_LoadsValidBios7AndExecutesTimer0IrqTrampoline) {
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_bios7(build_synthetic_bios7()).is_ok());

    const std::vector<u32> arm7 = build_timer0_irq_arm7_program(0U, 0xB1057130U);
    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7), 2000U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xB1057130U);
}

TEST(Tier1BootE2e, F29_LoadsBiosAndFirmwareViaE2eConfigPaths) {
    ScopedTempDir tmp;
    const std::vector<u8> bios9 = build_bios_with_swi_sentinel(true, 0U, 0xC9009001U);
    const auto bios9_path = tmp.write_file("bios9.bin", bios9);
    const auto bios7_path = tmp.write_file("bios7.bin", build_synthetic_bios7());
    FirmwareSpec fw_spec{};
    fw_spec.mac_address = {0x00U, 0x09U, 0xBFU, 0xAAU, 0xBBU, 0xCCU};
    fw_spec.nickname = u"CfgTest";
    const auto fw_path = tmp.write_file("firmware.bin", build_synthetic_firmware(fw_spec));

    E2eConfig cfg{};
    cfg.bios9_path = bios9_path.string();
    cfg.bios7_path = bios7_path.string();
    cfg.firmware_path = fw_path.string();
    EmulatorE2eHarness harness(cfg);

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_swi(0U);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(4U), 0x000036U, 6U});
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), arm7.words()), 8000U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xC9009001U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 0U), 0x00U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 3U), 0xAAU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 5U), 0xCCU);
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x006U), static_cast<u16>(u'C'));
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x012U), static_cast<u16>(u't'));
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x01AU), 7U);
}

class Tier1FirmwareSizesE2e : public ::testing::TestWithParam<u32> {};

TEST_P(Tier1FirmwareSizesE2e, F29_LoadsFirmwareAndReadsHeaderMacAndUserSettingsViaSpi) {
    const u32 fw_size = GetParam();
    FirmwareSpec spec{};
    spec.size_bytes = fw_size;
    spec.mac_address = {0x00U, 0x09U, 0xBFU, 0x55U, 0x66U, 0x77U};
    spec.nickname = u"SizeOk";
    const std::vector<u8> fw = build_synthetic_firmware(spec);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_firmware(fw).is_ok());

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(0U), 0x000008U, 4U});
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(2U), 0x000036U, 6U});
    const u32 user_nick_off = fw_size - 0x100U + 0x006U;
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(4U), user_nick_off, 4U});
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7.words()), 12000U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x534D4C45U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 0U), 0x00U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 3U), 0x55U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 5U), 0x77U);
    EXPECT_EQ(harness.read_main_ram16(result_slot_addr(4U)), static_cast<u16>(u'S'));
    EXPECT_EQ(harness.read_main_ram16(result_slot_addr(4U) + 2U), static_cast<u16>(u'i'));
}

INSTANTIATE_TEST_SUITE_P(
    FirmwareSizes128K256K512K,
    Tier1FirmwareSizesE2e,
    ::testing::Values(kFirmwareSize128K, kFirmwareSize256K, kFirmwareSize512K));

TEST(Tier1BootE2e, F29_DefaultFirmwarePopulatedWhenNoneProvided) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_spi_firmware_read(arm7, SpiFirmwareReadSpec{result_slot_addr(0U), 0x000036U, 6U});
    emit_halt_loop(arm7);

    boot_rom_and_step(harness, make_base_rom_spec(make_idle_words(), arm7.words()), 8000U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 0U), 0x00U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x09U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 2U), 0xBFU);
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x006U), static_cast<u16>(u'E'));
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x012U), static_cast<u16>(u'S'));
    EXPECT_EQ(harness.read_main_ram16(kRamFwUserSettings + 0x01AU), 7U);
}

TEST(Tier1BootE2e, F30_InitializesMainRamHeaderAndPostBiosSlots) {
    NdsRomSpec spec = make_base_rom_spec(make_idle_words(), make_idle_words());
    spec.title = "BOOTTEST";
    spec.gamecode = "BT01";
    spec.device_capacity = 2U;
    spec.min_total_size = 2U * 1024U * 1024U;
    const std::vector<u8> rom = build_valid_nds_rom(spec);

    EmulatorE2eHarness harness;
    boot_bytes_and_step(harness, rom, 200U);

    const u16 hdr_crc = read_le16(rom, 0x15EU);
    const u16 sec_crc = read_le16(rom, 0x06CU);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x000U), 0x544F4F42U);
    EXPECT_EQ(harness.read_main_ram32(kRamHeaderBase + 0x00CU), 0x31305442U);
    EXPECT_EQ(harness.read_main_ram8(kRamHeaderBase + 0x014U), 2U);
    EXPECT_EQ(harness.read_main_ram32(kRamPostBiosBase + 0x000U), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram32(kRamPostBiosBase + 0x400U), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x008U), hdr_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x00AU), sec_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x050U), 0x5835U);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x068U), 0x7FC0U);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x408U), hdr_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x40AU), sec_crc);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x410U), 0x5835U);
    EXPECT_EQ(harness.read_main_ram16(kRamPostBiosBase + 0x440U), 0x0001U);
}

TEST(Tier1BootE2e, F30_InitializesArm9Cp15HivectsAndTcm) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_cp15_read_c1(arm9, ArmReg::R0);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_cp15_read_dtcm(arm9, ArmReg::R0);
    emit_store_result(arm9, 1U, ArmReg::R0);
    emit_cp15_read_itcm(arm9, ArmReg::R0);
    emit_store_result(arm9, 2U, ArmReg::R0);
    arm9.emit_write32_abs(0x03001000U, 0xD7C357ACU);
    arm9.emit_read32_abs(ArmReg::R0, 0x03001000U);
    emit_store_result(arm9, 3U, ArmReg::R0);
    emit_halt_loop(arm9);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), make_idle_words()), 800U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x00052078U);
    EXPECT_NE(harness.read_main_ram32(result_slot_addr(0U)) & (1U << 13U), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x0300000AU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x00000020U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0xD7C357ACU);
    EXPECT_EQ(harness.read_shared_wram32(0x1000U), 0U);
}

TEST(Tier1BootE2e, F30_InitializesBankedStacksCpsrAndPostflg) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_read_stacks_and_postflg(arm9, 0U);
    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_read_stacks_and_postflg(arm7, 5U);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), arm7.words()), 1200U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x000000DFU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x03002F7CU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x03003F80U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x03003FC0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(4U)), 1U);

    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(5U)), 0x000000DFU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(6U)), 0x0380FD80U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(7U)), 0x0380FF80U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(8U)), 0x0380FFC0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(9U)), 1U);
}

TEST(Tier1BootE2e, F30_SetsWramcnt3OnBootAndAllowsArm9Reconfiguration) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultBase);
    arm7.emit_write32_abs(kSharedWramBase, 0xABCD1234U);
    emit_store_const_result(arm7, 0U, 1U);
    emit_halt_loop(arm7);

    ArmProgramEmitter arm9(kArm9DefaultBase);
    const Address poll_pc = arm9.current_pc();
    arm9.emit_read32_abs(ArmReg::R0, result_slot_addr(1U));
    arm9.emit_cmp_imm(ArmReg::R0, 0U);
    arm9.emit_b(poll_pc, ArmCond::Eq);
    arm9.emit_write8_abs(kRegWramCnt, 0x00U);
    arm9.emit_read32_abs(ArmReg::R0, kArm9SharedWramBase);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_halt_loop(arm9);

    boot_rom_and_step(harness, make_base_rom_spec(arm9.words(), arm7.words()), 600U);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 1U);
    EXPECT_EQ(harness.read_shared_wram32(0U), 0U);

    harness.write_main_ram32(result_slot_addr(1U), 1U);
    harness.step_ticks(600U);
    EXPECT_EQ(harness.read_shared_wram32(0U), 0xABCD1234U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0xABCD1234U);
}

TEST(Tier1BootE2e, F30_RunsDualCpuInDeterministic2To1Lockstep) {
    EmulatorE2eHarness harness;
    const std::vector<u32> arm9 = make_tight_counter_loop(kArm9DefaultBase, 0U);
    const std::vector<u32> arm7 = make_tight_counter_loop(kArm7DefaultBase, 1U);

    boot_rom_and_step(harness, make_base_rom_spec(arm9, arm7), 2000U);
    const u32 arm9_iters = harness.read_main_ram32(result_slot_addr(0U));
    const u32 arm7_iters = harness.read_main_ram32(result_slot_addr(1U));
    ASSERT_GT(arm7_iters, 0U);
    EXPECT_GT(arm9_iters, arm7_iters);
    const double ratio = static_cast<double>(arm9_iters) / static_cast<double>(arm7_iters);
    EXPECT_GE(ratio, 1.5);
    EXPECT_LE(ratio, 2.5);
}

}  // namespace
}  // namespace elmasds::e2e::m3
