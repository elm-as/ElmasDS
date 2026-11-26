#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

namespace elmasds::e2e::m2 {
namespace {

TEST(Tier2TimersATest, F6_Arm9AndArm7TimersAreStrictlyIsolated) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm9.emit_write16_abs(timer_cnt_l(ch), 0x0000U);
        arm9.emit_write16_abs(timer_cnt_h(ch), kTimerPrescale1 | kTimerStart);
    }
    emit_delay_nop(arm9, 200U);
    arm9.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm9, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_delay_nop(arm7, 250U);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm7.emit_read32_abs(ArmReg::R0, timer_cnt_l(ch));
        emit_store_result(arm7, 1U + ch, ArmReg::R0);
    }
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(1500U);

    EXPECT_GT(harness.read_main_ram32(result_addr(0U)), 50U);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(1U + ch)), 0U);
    }
}

TEST(Tier2TimersATest, F6_UnusedControlBitsAreMaskedOnReadback) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0xFFFFU);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_h(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write16_abs(timer_cnt_h(1U), 0xFFFFU);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_h(1U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00C3U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x00C7U);
}

TEST(Tier2TimersATest, F6_32BitWriteAppliesReloadBeforeStart) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(timer_cnt_l(0U), 0x0083ABCDU);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0xABCDU);
}

TEST(Tier2TimersATest, F6_32BitReadCombinesCounterAndControl) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(timer_cnt_l(0U), 0x00834321U);
    arm7.emit_read32_abs(ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00834321U);
}

TEST(Tier2TimersATest, F6_8BitControlHighByteIgnoredLowByteStarts) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x1111U);
    arm7.emit_write8_abs(timer_cnt_h(0U) + 1U, 0x83U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_h(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write8_abs(timer_cnt_h(0U), 0x83U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_h(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x0000U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x0083U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x1111U);
}

TEST(Tier2TimersATest, F7_ReloadZeroProducesFull65536Period) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(130000U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(2000U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier2TimersATest, F7_Reload0xFFFFProducesSingleStepPeriod) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFFFU);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1800U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(400U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier2TimersATest, F7_MidRunReloadTakesEffectOnlyOnNextOverflow) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFF00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x8000U);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    emit_ack_irq(arm7, timer_irq_bit(0U));
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(300U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(400U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(60000U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(8000U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier2TimersATest, F7_RestartAfterOverflowReloadsNewLatch) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 60U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xCAFEU);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1000U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0xCAFEU);
}

TEST(Tier2TimersATest, F7_StoppingBeforeOverflowPreventsSpuriousIrq) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFF00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    emit_delay_nop(arm7, 40U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_delay_nop(arm7, 500U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(2000U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)),
              harness.read_main_ram32(result_addr(1U)));
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)) & timer_irq_bit(0U), 0U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
