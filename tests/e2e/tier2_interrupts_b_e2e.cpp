#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace elmasds::e2e::m2 {
namespace {

[[nodiscard]] std::vector<u32> build_with_handler(Address entry,
                                                  const ArmProgramEmitter& handler,
                                                  const ArmProgramEmitter& main_body) {
    ArmProgramEmitter prog(entry);
    prog.emit_b(entry + 4U + static_cast<Address>(handler.word_count() * 4U));
    for (const u32 word : handler.words()) {
        prog.emit_word(word);
    }
    for (const u32 word : main_body.words()) {
        prog.emit_word(word);
    }
    return prog.words();
}

[[nodiscard]] ArmProgramEmitter make_inc_ack_handler(Address base, u32 count_slot, u32 ack_mask) {
    ArmProgramEmitter h(base);
    h.emit_read32_abs(ArmReg::R0, result_addr(count_slot));
    h.emit_add_imm(RegImmOp{ArmReg::R0, ArmReg::R0, 1U});
    emit_store_result(h, count_slot, ArmReg::R0);
    emit_ack_irq(h, ack_mask);
    emit_hle_irq_return(h);
    return h;
}

void emit_wait_host_flag(ArmProgramEmitter& cpu, u32 ready_slot, u32 go_slot) {
    emit_store_const_result(cpu, ready_slot, 1U);
    const Address loop_pc = cpu.current_pc();
    cpu.emit_read32_abs(ArmReg::R0, result_addr(go_slot));
    cpu.emit_cmp_imm(ArmReg::R0, 0U);
    cpu.emit_b(loop_pc, ArmCond::Eq);
}

TEST(Tier2InterruptsBTest, F3_HandlerNotAckingIfLeavesPendingAndReTriggersOnIme) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter h(kArm9DefaultEntry + 4U);
    h.emit_read32_abs(ArmReg::R0, result_addr(0U));
    h.emit_add_imm(RegImmOp{ArmReg::R0, ArmReg::R0, 1U});
    emit_store_result(h, 0U, ArmReg::R0);
    h.emit_write32_abs(kRegIme, 0U);
    emit_hle_irq_return(h);

    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, kIrqVBlank);
    emit_wait_host_flag(m, 8U, 9U);
    m.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(m, 1U, ArmReg::R0);
    m.emit_write32_abs(kRegIme, 1U);
    emit_delay_nop(m, 30U);
    emit_ack_irq(m, kIrqVBlank);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.step_ticks(300U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);

    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 2U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), kIrqVBlank);
}

TEST(Tier2InterruptsBTest, F3_If8BitHighByteClearPreservesLowByteBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(arm9, 8U, 9U);
    arm9.emit_write8_abs(kRegIf + 2U, 0x20U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.trigger_hblank();
    harness.trigger_gxfifo();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqVBlank | kIrqHBlank);
}

TEST(Tier2InterruptsBTest, F4_CrossCpuExclusiveIrqIsolation) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_enable_interrupts(arm9, 0x003F3F7FU);
    emit_wait_host_flag(arm9, 8U, 9U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_enable_interrupts(arm7, 0x01DF3FFFU);
    emit_spi_byte_xfer(arm7, kSpiEnable | kSpiIrqEnable | kSpiDeviceNone, 0x00U);
    emit_wait_host_flag(arm7, 10U, 11U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    harness.trigger_gxfifo();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.write_main_ram32(result_addr(11U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqGxFifo);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), kIrqSpi);
}

TEST(Tier2InterruptsBTest, F4_DynamicIrqHandlerSwapAtRuntime) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter ha(kArm9DefaultEntry + 4U);
    emit_store_const_result(ha, 0U, 0x1111U);
    emit_ack_irq(ha, kIrqVBlank);
    emit_hle_irq_return(ha);

    const Address hb_addr = kArm9DefaultEntry + 4U + static_cast<Address>(ha.word_count() * 4U);
    ArmProgramEmitter hb(hb_addr);
    emit_store_const_result(hb, 1U, 0x2222U);
    emit_ack_irq(hb, kIrqVBlank);
    emit_hle_irq_return(hb);

    ArmProgramEmitter prog(kArm9DefaultEntry);
    prog.emit_b(hb.current_pc());
    for (const u32 w : ha.words()) {
        prog.emit_word(w);
    }
    for (const u32 w : hb.words()) {
        prog.emit_word(w);
    }
    prog.emit_write32_abs(kArm9IrqSlot, kArm9DefaultEntry + 4U);
    prog.emit_enable_irq(ArmReg::R0);
    emit_enable_interrupts(prog, kIrqVBlank);
    emit_wait_host_flag(prog, 8U, 9U);
    prog.emit_write32_abs(kArm9IrqSlot, hb_addr);
    emit_store_const_result(prog, 10U, 1U);
    prog.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = prog.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(600U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x1111U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    harness.trigger_vblank();
    harness.step_ticks(600U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x2222U);
}

TEST(Tier2InterruptsBTest, F4_SimultaneousPendingIrqsAndDualCpuSlots) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h9 =
        make_inc_ack_handler(kArm9DefaultEntry + 4U, 0U, kIrqVBlank | kIrqHBlank | kIrqGxFifo);
    ArmProgramEmitter m9(h9.current_pc());
    emit_store_const_result(m9, 0U, 0U);
    emit_install_irq_trampoline(m9, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    m9.emit_write32_abs(kRegIe, kIrqVBlank | kIrqHBlank | kIrqGxFifo);
    m9.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(m9, 8U, 9U);
    m9.emit_write32_abs(kRegIme, 1U);
    m9.emit_branch_self();

    const ArmProgramEmitter h7 = make_inc_ack_handler(kArm7DefaultEntry + 4U, 1U, kIrqVBlank);
    ArmProgramEmitter m7(h7.current_pc());
    emit_store_const_result(m7, 1U, 0U);
    emit_install_irq_trampoline(m7, kArm7IrqSlot, kArm7DefaultEntry + 4U);
    emit_enable_interrupts(m7, kIrqVBlank);
    m7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{
                                   .arm9_code = build_with_handler(kArm9DefaultEntry, h9, m9),
                                   .arm7_code = build_with_handler(kArm7DefaultEntry, h7, m7),
                               });
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.trigger_hblank();
    harness.trigger_gxfifo();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 1U);
}

TEST(Tier2InterruptsBTest, F4_NullIrqSlotDoesNotCrashOnTrigger) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kArm9IrqSlot, 0U);
    arm9.emit_enable_irq(ArmReg::R0);
    emit_enable_interrupts(arm9, kIrqVBlank);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(400U);
    harness.trigger_vblank();
    harness.step_ticks(200U);
    EXPECT_FALSE(harness.is_arm9_halted());
}

TEST(Tier2InterruptsBTest, F5_Arm7HaltcntReadZeroAndNonHaltModes) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_read8_abs(arm7, ArmReg::R0, kRegHaltcnt);
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write8_abs(kRegHaltcnt, 0x00U);
    arm7.emit_write8_abs(kRegHaltcnt, 0x40U);
    emit_store_const_result(arm7, 1U, 0x00C0FFEEU);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_FALSE(harness.is_arm7_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x00C0FFEEU);
}

TEST(Tier2InterruptsBTest, F5_HaltWithPendingWakeDoesNotHaltEitherCpu) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIe, kIrqVBlank);
    arm9.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(arm9, 8U, 9U);
    arm9.emit_mcr_wfi();
    emit_store_const_result(arm9, 0U, 0x900D0001U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, kIrqVBlank);
    arm7.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(arm7, 10U, 11U);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    emit_store_const_result(arm7, 1U, 0x900D0002U);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.write_main_ram32(result_addr(11U), 1U);
    harness.step_ticks(800U);

    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_FALSE(harness.is_arm7_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x900D0001U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x900D0002U);
}

TEST(Tier2InterruptsBTest, F5_Arm9IgnoresHaltcnt8BitWrites) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm9.emit_write8_abs(kRegHaltcnt, kHaltcntSleep);
    emit_store_const_result(arm9, 0U, 0x11223344U);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(800U);

    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x11223344U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
