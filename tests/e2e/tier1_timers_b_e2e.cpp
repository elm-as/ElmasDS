#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace elmasds::e2e::m2 {
namespace {

struct PrescaleParam {
    const char* name;
    u16 control_bits;
    u32 divider;
};

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

class F8PrescalerRatioTest : public ::testing::TestWithParam<PrescaleParam> {};

TEST_P(F8PrescalerRatioTest, F8_PrescalerPeriodMatchesBusDivider) {
    const PrescaleParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFF00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), tc.control_bits | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    const TickCount expected_period = 256ULL * tc.divider * 2ULL;
    harness.step_ticks(expected_period - 200ULL);
    EXPECT_TRUE(harness.is_arm7_halted()) << tc.name;

    harness.step_ticks(400ULL);
    EXPECT_FALSE(harness.is_arm7_halted()) << tc.name;
}

INSTANTIATE_TEST_SUITE_P(
    FourPrescalers,
    F8PrescalerRatioTest,
    ::testing::Values(PrescaleParam{"Div1", kTimerPrescale1, 1U},
                      PrescaleParam{"Div64", kTimerPrescale64, 64U},
                      PrescaleParam{"Div256", kTimerPrescale256, 256U},
                      PrescaleParam{"Div1024", kTimerPrescale1024, 1024U}));

TEST(Tier1TimersBTest, F8_SwitchingPrescalerWhileRunningUpdatesRate) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFF00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerIrqEnable | kTimerStart);
    emit_delay_nop(arm7, 100U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1500U);

    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier1TimersBTest, F9_TwoStageCascadeTm0ToTm1) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm9.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm9.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm9.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm9, 500U);
    arm9.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm9, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(2000U);

    const u32 cascades = harness.read_main_ram32(result_addr(0U));
    EXPECT_GE(cascades, 10U);
    EXPECT_LE(cascades, 40U);
}

TEST(Tier1TimersBTest, F9_FourStageCascadeChainTm0ThroughTm3) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(3U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(3U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(2U), 0xFFFEU);
    arm7.emit_write16_abs(timer_cnt_h(2U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0xFFFEU);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFFEU);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 500U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(3U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(2000U);

    EXPECT_GE(harness.read_main_ram32(result_addr(0U)), 40U);
}

TEST(Tier1TimersBTest, F9_MultiOverflowCatchupPropagatesCascade) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(1U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerIrqEnable | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(400U);
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(300U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

class F9TimerIrqChannelTest : public ::testing::TestWithParam<u32> {};

TEST_P(F9TimerIrqChannelTest, F9_OverflowSetsIfBit3To6AndVectors) {
    const u32 ch = GetParam();
    const u32 irq_bit = timer_irq_bit(ch);
    EmulatorE2eHarness harness;
    ArmProgramEmitter h(kArm9DefaultEntry + 4U);
    h.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(h, 0U, ArmReg::R0);
    h.emit_write16_abs(timer_cnt_h(ch), 0x0000U);
    emit_ack_irq(h, irq_bit);
    emit_store_const_result(h, 1U, 1U);
    emit_hle_irq_return(h);

    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 1U, 0U);
    emit_install_irq_trampoline(m, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, irq_bit);
    m.emit_write16_abs(timer_cnt_l(ch), 0xFF80U);
    m.emit_write16_abs(timer_cnt_h(ch), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(1500U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & irq_bit, irq_bit);
}

INSTANTIATE_TEST_SUITE_P(Channels0To3, F9TimerIrqChannelTest, ::testing::Values(0U, 1U, 2U, 3U));

TEST(Tier1TimersBTest, F9_OverflowWithIrqDisabledDoesNotAssertIf) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(1U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(1U), kTimerCascade | kTimerStart);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 300U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(1U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1500U);

    EXPECT_GE(harness.read_main_ram32(result_addr(0U)), 10U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & timer_irq_bit(0U), 0U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
