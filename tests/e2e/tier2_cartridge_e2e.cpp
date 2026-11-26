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

namespace elmasds::e2e::m3 {
namespace {

constexpr TickCount kDefaultSteps = 20'000ULL;
constexpr u32 kDoneMagic = 0xD04EC003U;
constexpr u32 kArm7DoneSlot = 29U;
constexpr u32 kSyncSlot = 30U;
constexpr u32 kArm9DoneSlot = 31U;

struct IrqStubSpec {
    Address irq_slot = kArm9IrqSlot;
    u32 irq_mask = kIrqCartridgeMask;
    u32 capture_slot = 0U;
};

void emit_set_sync(ArmProgramEmitter& em, u32 value) {
    emit_store_const_result(em, kSyncSlot, value);
}

void emit_wait_sync(ArmProgramEmitter& em, u32 expected) {
    const Address loop_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, result_slot_addr(kSyncSlot));
    em.emit_cmp_imm(ArmReg::R0, expected);
    em.emit_b(loop_pc, ArmCond::Ne);
}

void emit_read32_to_slot(ArmProgramEmitter& em, Address addr, u32 slot) {
    em.emit_read32_abs(ArmReg::R0, addr);
    emit_store_result(em, slot, ArmReg::R0);
}

void emit_read16_to_slot(ArmProgramEmitter& em, Address addr, u32 slot) {
    emit_read16_abs(em, ArmReg::R0, addr);
    emit_store_result(em, slot, ArmReg::R0);
}

void emit_chip_id_read(ArmProgramEmitter& em, u32 slot) {
    emit_rom_cmd_words(em, 0xB8U, 0U);
    emit_start_romctrl(em, 7U);
    emit_read32_to_slot(em, kRegRomData, slot);
}

void emit_irq_capture_stub(ArmProgramEmitter& em, const IrqStubSpec& spec) {
    const Address main_pc = em.base_address() + 128U;
    em.emit_b(main_pc);
    const Address handler_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(em, spec.capture_slot, ArmReg::R0);
    em.emit_write32_abs(kRegIf, spec.irq_mask);
    em.emit_bx(ArmReg::Lr);
    while (em.current_pc() < main_pc) {
        em.emit_nop();
    }
    em.emit_irq_trampoline_install(spec.irq_slot, handler_pc);
    em.emit_enable_irq(ArmReg::R0);
    em.emit_write32_abs(kRegIe, spec.irq_mask);
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

TEST(Tier2CartridgeE2e, F23Arm7WriteToExmemcntBit11IsIgnored) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_wait_sync(arm9, 1U);
    emit_read16_abs(arm9, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_chip_id_read(arm9, 3U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    arm7.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_read16_abs(arm7, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_chip_id_read(arm7, 1U);
    emit_set_sync(arm7, 1U);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kExMemSlot1Arm7Bit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)) & kExMemSlot1Arm7Bit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x000000C2U);
}

TEST(Tier2CartridgeE2e, F23NonOwnerReadsZeroAndWritesIgnored) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0x820000B7U, 0x11223344U);
    emit_set_sync(arm9, 1U);
    emit_wait_sync(arm9, 2U);
    emit_read32_to_slot(arm9, kRegRomCmdLo, 4U);
    emit_read32_to_slot(arm9, kRegRomCmdHi, 5U);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_set_sync(arm9, 3U);
    emit_wait_sync(arm9, 4U);
    emit_read32_to_slot(arm9, kRegRomCmdLo, 6U);
    emit_read32_to_slot(arm9, kRegRomData, 7U);
    emit_read16_to_slot(arm9, kRegAuxSpiCnt, 8U);
    emit_rom_cmd_words(arm9, 0xBAD0BEEFU, 0xDEAD0000U);
    emit_set_sync(arm9, 5U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_read32_to_slot(arm7, kRegRomCtrl, 0U);
    emit_read32_to_slot(arm7, kRegRomCmdLo, 1U);
    emit_read32_to_slot(arm7, kRegRomData, 2U);
    emit_read16_to_slot(arm7, kRegAuxSpiCnt, 3U);
    emit_rom_cmd_words(arm7, 0xDEADBEEFU, 0xCAFEBABEU);
    arm7.emit_write16_abs(kRegAuxSpiCnt, 0xFFFFU);
    emit_set_sync(arm7, 2U);
    emit_wait_sync(arm7, 3U);
    emit_rom_cmd_words(arm7, 0x12345678U, 0xAABBCCDDU);
    emit_set_sync(arm7, 4U);
    emit_wait_sync(arm7, 5U);
    emit_read32_to_slot(arm7, kRegRomCmdLo, 9U);
    emit_read32_to_slot(arm7, kRegRomCmdHi, 10U);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    for (u32 slot : {0U, 1U, 2U, 3U, 6U, 7U, 8U}) {
        EXPECT_EQ(harness.read_main_ram32(result_slot_addr(slot)), 0U) << "slot=" << slot;
    }
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(4U)), 0x820000B7U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(5U)), 0x11223344U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(9U)), 0x12345678U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(10U)), 0xAABBCCDDU);
}

TEST(Tier2CartridgeE2e, F23ByteWritesToExmemcntHighByteToggleBit11) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write8_abs(kRegExMemCntHigh, 0x08U);
    emit_read16_abs(arm9, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_set_sync(arm9, 1U);
    emit_wait_sync(arm9, 2U);
    arm9.emit_write8_abs(kRegExMemCntHigh, 0x00U);
    emit_read16_abs(arm9, ArmReg::R0, kRegExMemCnt);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_chip_id_read(arm9, 3U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_chip_id_read(arm7, 1U);
    emit_set_sync(arm7, 2U);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kExMemSlot1Arm7Bit);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x000000C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x000000C2U);
}

TEST(Tier2CartridgeE2e, F23Irq19RoutesStrictlyToSlot1Owner) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_irq_capture_stub(arm9, IrqStubSpec{kArm9IrqSlot, kIrqCartridgeMask, 0U});
    emit_store_const_result(arm9, 0U, 0U);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_set_sync(arm9, 1U);
    emit_wait_sync(arm9, 2U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_irq_capture_stub(arm7, IrqStubSpec{kArm7IrqSlot, kIrqCartridgeMask, 1U});
    emit_store_const_result(arm7, 1U, 0U);
    arm7.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiEnable | kAuxSpiIrq);
    emit_chip_id_read(arm7, 3U);
    emit_set_sync(arm7, 2U);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), arm7.words()));
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)) & kIrqCartridgeMask, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), kIrqCartridgeMask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x000000C2U);
}

TEST(Tier2CartridgeE2e, F23ResetRestoresArm9Ownership) {
    ArmProgramEmitter prog1(kArm9DefaultBase);
    prog1.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_read16_abs(prog1, ArmReg::R0, kRegExMemCnt);
    emit_store_result(prog1, 0U, ArmReg::R0);
    emit_store_const_result(prog1, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(prog1);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(prog1.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kExMemSlot1Arm7Bit);

    harness.reset();

    ArmProgramEmitter prog2(kArm9DefaultBase);
    emit_read16_abs(prog2, ArmReg::R0, kRegExMemCnt);
    emit_store_result(prog2, 1U, ArmReg::R0);
    emit_chip_id_read(prog2, 2U);
    emit_store_const_result(prog2, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(prog2);

    boot_and_verify_arm9(harness, make_base_rom_spec(prog2.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)) & kExMemSlot1Arm7Bit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x000000C2U);
}

TEST(Tier2CartridgeE2e, F24AuxSpiCnt0xFFFFMasksTo0xE043) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegAuxSpiCnt, 0xFFFFU);
    emit_read16_abs(arm9, ArmReg::R0, kRegAuxSpiCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kAuxSpiMask);
}

TEST(Tier2CartridgeE2e, F24AuxSpiDataWithSpiOrSlotDisabledReturns0xFFAndLeavesSaveUnchanged) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_disabled.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode, 0x06U);
    const std::array<u8, 5> pp_cmd{0x02U, 0x00U, 0x00U, 0x00U, 0x55U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable, pp_cmd, result_slot_addr(0U)});
    const std::array<u8, 2> rdid_cmd{0x9FU, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiMode, rdid_cmd, result_slot_addr(2U)});

    const u16 spi_en = kAuxSpiEnable | kAuxSpiMode;
    const std::array<u8, 5> read_cmd{0x03U, 0x00U, 0x00U, 0x00U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{spi_en, read_cmd, result_slot_addr(3U)});
    const std::array<u8, 2> rdsr_cmd{0x05U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{spi_en, rdsr_cmd, result_slot_addr(5U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 0U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 4U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 0U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 1U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(3U) + 4U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(5U) + 1U), 0x02U);
}

TEST(Tier2CartridgeE2e, F2432BitStrToAuxSpiCntClocksAuxSpiData) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_str32.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    const u32 word_val = (0x08U << 16U) | kAuxSpiEnable | kAuxSpiMode | kAuxSpiHold;
    arm9.emit_write32_abs(kRegAuxSpiCnt, word_val);
    emit_auxspi_tx(arm9, kAuxSpiEnable | kAuxSpiMode, 0x00U);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xAAU);
}

TEST(Tier2CartridgeE2e, F24ByteWritesToAuxSpiCntLowAndHighBytes) {
    const ScopedTempDir dir;
    EmulatorE2eHarness harness;
    attach_flash_512k(harness, dir, "f24_byte_writes.sav");

    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write8_abs(kRegAuxSpiCnt, 0x43U);
    arm9.emit_write8_abs(kRegAuxSpiCntHigh, 0xA0U);
    emit_read16_abs(arm9, ArmReg::R0, kRegAuxSpiCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);

    const std::array<u8, 4> rdid{0x9FU, 0x00U, 0x00U, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{0xA003U, rdid, result_slot_addr(1U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xA043U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(1U) + 1U), 0x20U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(1U) + 2U), 0x40U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(1U) + 3U), 0x13U);
}

TEST(Tier2CartridgeE2e, F24AuxSpiDataWithNoSaveAttachedReturns0xFF) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    const std::array<u8, 2> cmd{0x9FU, 0x00U};
    emit_auxspi_seq(arm9, AuxSpiSeqSpec{kAuxSpiEnable | kAuxSpiMode, cmd, result_slot_addr(0U)});
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 0U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0xFFU);
}

}  // namespace
}  // namespace elmasds::e2e::m3
