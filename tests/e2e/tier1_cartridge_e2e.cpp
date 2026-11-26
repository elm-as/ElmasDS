#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr TickCount kDefaultSteps = 20'000ULL;
constexpr u32 kDoneMagic = 0xD04EC001U;
constexpr u32 kArm7DoneSlot = 29U;
constexpr u32 kSyncSlot = 30U;
constexpr u32 kArm9DoneSlot = 31U;

void emit_set_sync(ArmProgramEmitter& em, u32 value) {
    emit_store_const_result(em, kSyncSlot, value);
}

void emit_wait_sync(ArmProgramEmitter& em, u32 expected) {
    const Address loop_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, result_slot_addr(kSyncSlot));
    em.emit_cmp_imm(ArmReg::R0, expected);
    em.emit_b(loop_pc, ArmCond::Ne);
}

void emit_chip_id_read(ArmProgramEmitter& em, u8 opcode, u32 slot) {
    emit_rom_cmd_words(em, static_cast<u32>(opcode), 0U);
    emit_start_romctrl(em, 7U);
    emit_wait_rom_drq(em, ArmReg::R0);
    em.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(em, slot, ArmReg::R0);
}

void emit_arm9_irq19_stub(ArmProgramEmitter& em, u32 capture_slot) {
    const Address main_pc = em.base_address() + 128U;
    em.emit_b(main_pc);
    const Address handler_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(em, capture_slot, ArmReg::R0);
    em.emit_write32_abs(kRegIf, kIrqCartridgeMask);
    em.emit_bx(ArmReg::Lr);
    while (em.current_pc() < main_pc) {
        em.emit_nop();
    }
    em.emit_irq_trampoline_install(kArm9IrqSlot, handler_pc);
    em.emit_enable_irq(ArmReg::R0);
    em.emit_write32_abs(kRegIe, kIrqCartridgeMask);
    em.emit_write32_abs(kRegIme, 1U);
}

void attach_flash_512k(EmulatorE2eHarness& harness, const ScopedTempDir& dir, const char* name) {
    const auto path = dir.path() / name;
    const Status st = harness.attach_save(path, static_cast<u32>(M3SaveTypeHint::Flash512KB));
    ASSERT_TRUE(st.is_ok()) << st.error().message;
}

void boot_and_verify_arm9(EmulatorE2eHarness& harness, const NdsRomSpec& spec) {
    boot_rom_and_step(harness, spec, kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm9DoneSlot)), kDoneMagic);
}

TEST(Tier1CartridgeE2e, F23DefaultResetOwnershipIsArm9) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_read16_abs(arm9, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_chip_id_read(arm9, 0xB8U, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kExMemSlot1Arm7Bit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x000000C2U);
}

TEST(Tier1CartridgeE2e, F23Arm9DelegatesSlot1ToArm7) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_set_sync(arm9, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_read16_abs(arm7, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_chip_id_read(arm7, 0x90U, 1U);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kExMemSlot1Arm7Bit,
              kExMemSlot1Arm7Bit);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x000000C2U);
}

TEST(Tier1CartridgeE2e, F23Arm9ReclaimsSlot1FromArm7) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_set_sync(arm9, 1U);
    emit_wait_sync(arm9, 2U);
    arm9.emit_write16_abs(kRegExMemCnt, 0U);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 7U);
    emit_wait_rom_drq(arm9, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(arm9, 1U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_chip_id_read(arm7, 0x90U, 0U);
    emit_set_sync(arm7, 2U);
    emit_halt_loop(arm7);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), arm7.words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(std::array<u32, 1>{0xCAFEBABEU});

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x000000C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xCAFEBABEU);
}

TEST(Tier1CartridgeE2e, F23Arm9WritesExmemcntPatternAndBothCpusReadBack) {
    constexpr u16 kPattern = 0x4885U;
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegExMemCnt, kPattern);
    emit_read16_abs(arm9, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_set_sync(arm9, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_read16_abs(arm7, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kPattern);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), kPattern);
}

TEST(Tier1CartridgeE2e, F23Arm7ReadsFlashJedecIdWhenOwningSlot1) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f23_arm7_jedec.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_set_sync(arm9, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    const std::array<u8, 4> rdid{0x9FU, 0x00U, 0x00U, 0x00U};
    emit_auxspi_seq(arm7, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, rdid, result_slot_addr(0U)});
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x20U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 2U), 0x40U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 3U), 0x13U);
}

TEST(Tier1CartridgeE2e, F24AuxSpiCntWritableMaskReadback) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiMask);
    emit_read16_abs(arm9, ArmReg::R0, kRegAuxSpiCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write16_abs(kRegAuxSpiCnt, 0xA041U);
    emit_read16_abs(arm9, ArmReg::R0, kRegAuxSpiCnt);
    emit_store_result(arm9, 1U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kAuxSpiMask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xA041U);
}

TEST(Tier1CartridgeE2e, F24SingleByteWithoutCsHoldCommitsWrenAndReadsRdsr) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_wren_rdsr.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode, 0x06U);
    const std::array<u8, 2> rdsr{0x05U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, rdsr, result_slot_addr(0U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x02U);
}

TEST(Tier1CartridgeE2e, F24MultiByteJedecIdWithCsHold) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_jedec.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    const std::array<u8, 4> rdid{0x9FU, 0x00U, 0x00U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, rdid, result_slot_addr(0U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x20U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 2U), 0x40U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 3U), 0x13U);
}

TEST(Tier1CartridgeE2e, F24ClearingBit13OrBit15ReleasesChipSelect) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_release_cs.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode, 0x06U);
    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode | kAuxSpiHold, 0x9FU);
    arm9.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiEnable | kAuxSpiHold);
    const std::array<u8, 2> rdsr{0x05U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, rdsr, result_slot_addr(0U)});

    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode | kAuxSpiHold, 0x9FU);
    arm9.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiMode | kAuxSpiHold);
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, rdsr, result_slot_addr(1U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x02U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(1U) + 1U), 0x02U);
}

TEST(Tier1CartridgeE2e, F24AuxSpiCntIrqBitTriggersRomctrlIrqOnly) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_irq19.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_arm9_irq19_stub(arm9, 0U);
    emit_store_const_result(arm9, 0U, 0U);
    const std::array<u8, 2> rdid_prefix{0x9FU, 0x00U};
    const u16 irq_spi_cnt = kAuxSpiEnable | kAuxSpiIrq | kAuxSpiMode;
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{irq_spi_cnt, rdid_prefix, 0U});
    arm9.emit_read32_abs(ArmReg::R0, result_slot_addr(0U));
    emit_store_result(arm9, 1U, ArmReg::R0);

    arm9.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiEnable | kAuxSpiIrq);
    emit_chip_id_read(arm9, 0xB8U, 2U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kIrqCartridgeMask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x000000C2U);
}

}  // namespace
}  // namespace elmasds::e2e::m3
