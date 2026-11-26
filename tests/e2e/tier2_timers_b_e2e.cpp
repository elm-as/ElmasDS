#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

namespace elmasds::e2e::m2 {
namespace {

TEST(Tier2TimersBTest, F8_Timer0IgnoresCascadeBitOnBothCpus) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm9.emit_write16_abs(timer_cnt_h(0U), kTimerCascade | kTimerStart);
    emit_read16_abs(arm9, ArmReg::R0, timer_cnt_h(0U));
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_delay_nop(arm9, 100U);
    arm9.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm9, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerCascade | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_h(0U));
    emit_store_result(arm7, 2U, ArmReg::R0);
    emit_delay_nop(arm7, 120U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(1200U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kTimerStart);
    EXPECT_GT(harness.read_main_ram32(result_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), kTimerStart);
    EXPECT_GT(harness.read_main_ram32(result_addr(3U)), 0U);
}

TEST(Tier2TimersBTest, F8_Prescaler1To1024SwitchWhileRunningSlowsOverflow) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xF000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    emit_delay_nop(arm7, 50U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(20000U);

    EXPECT_TRUE(harness.is_arm7_halted());
}

TEST(Tier2TimersBTest, F8_PrescalerSwitchWhileStoppedPreservesCounter) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x1000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 60U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale256);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1200U);

    const u32 c0 = harness.read_main_ram32(result_addr(0U));
    EXPECT_GT(c0, 0x1000U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), c0);
}

TEST(Tier2TimersBTest, F8_SimultaneousTimersMaintainPrescalerRatio) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerPrescale64 | kTimerStart);
    emit_delay_nop(arm7, 1500U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(1U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(4000U);

    const u32 t0 = harness.read_main_ram32(result_addr(0U));
    const u32 t1 = harness.read_main_ram32(result_addr(1U));
    EXPECT_GE(t1, 15U);
    EXPECT_GE(t0, t1 * 55U);
    EXPECT_LE(t0, (t1 + 2U) * 70U);
}

TEST(Tier2TimersBTest, F8_HighFrequencyTimerAdvancesSchedulerCleanly) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFFFU);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    const TickCount before = harness.current_tick();
    harness.step_ticks(2000U);
    EXPECT_EQ(harness.current_tick(), before + 2000U);
}

TEST(Tier2TimersBTest, F9_CascadeTimerWithStoppedPredecessorNeverCounts) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 300U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(2000U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
}

TEST(Tier2TimersBTest, F9_StoppedCascadeTimerIgnoresPredecessorOverflows) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 250U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1200U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
}

TEST(Tier2TimersBTest, F9_StoppingPredecessorFreezesCascadeChain) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(2U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(2U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0xFFF8U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 200U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(2U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_delay_nop(arm7, 300U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 2U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(2U));
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(2000U);

    EXPECT_GT(harness.read_main_ram32(result_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)),
              harness.read_main_ram32(result_addr(2U)));
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)),
              harness.read_main_ram32(result_addr(3U)));
}

TEST(Tier2TimersBTest, F9_CancellingArm9TimerDoesNotDropArm7TimerEvent) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIme, 0U);
    arm9.emit_write16_abs(timer_cnt_l(0U), 0xF000U);
    arm9.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    emit_delay_nop(arm9, 30U);
    arm9.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFE00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(1500U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & timer_irq_bit(0U), 0U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier2TimersBTest, F9_FourStageCascadePropagatesOverflowIrqToTm3) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(3U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(3U), 0xFFFFU);
    arm7.emit_write16_abs(timer_cnt_h(3U), kTimerCascade | kTimerIrqEnable | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(2U), 0xFFFFU);
    arm7.emit_write16_abs(timer_cnt_h(2U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0xFFFFU);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFF00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(400U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(350U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

}  // namespace
}  // namespace elmasds::e2e::m2
