#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace elmasds::e2e::m2 {
namespace {

struct TimerChannelParam {
    const char* name;
    bool is_arm9;
    u32 channel;
};

[[nodiscard]] Address cpu_entry(bool is_arm9) noexcept {
    return is_arm9 ? kArm9DefaultEntry : kArm7DefaultEntry;
}

void boot_for_cpu(EmulatorE2eHarness& harness, bool is_arm9, std::vector<u32> code) {
    const DualProgramSpec spec =
        is_arm9 ? DualProgramSpec{.arm9_code = std::move(code)}
                : DualProgramSpec{.arm7_code = std::move(code)};
    boot_dual_program(harness, spec);
}

class F6TimerChannelTest : public ::testing::TestWithParam<TimerChannelParam> {};

TEST_P(F6TimerChannelTest, F6_ChannelCountsUpOnBusClock) {
    const TimerChannelParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write16_abs(timer_cnt_l(tc.channel), 0x0000U);
    cpu.emit_write16_abs(timer_cnt_h(tc.channel), kTimerPrescale1 | kTimerStart);
    emit_read16_abs(cpu, ArmReg::R0, timer_cnt_h(tc.channel));
    emit_store_result(cpu, 0U, ArmReg::R0);
    emit_delay_nop(cpu, tc.is_arm9 ? 200U : 220U);
    cpu.emit_write16_abs(timer_cnt_h(tc.channel), 0x0000U);
    emit_read16_abs(cpu, ArmReg::R0, timer_cnt_l(tc.channel));
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(1500U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kTimerStart);
    const u32 count = harness.read_main_ram32(result_addr(1U));
    EXPECT_GT(count, 50U);
    EXPECT_LT(count, 0x1000U);
}

INSTANTIATE_TEST_SUITE_P(
    Arm9AndArm7All4Timers,
    F6TimerChannelTest,
    ::testing::Values(TimerChannelParam{"Arm9Tm0", true, 0U},
                      TimerChannelParam{"Arm9Tm1", true, 1U},
                      TimerChannelParam{"Arm9Tm2", true, 2U},
                      TimerChannelParam{"Arm9Tm3", true, 3U},
                      TimerChannelParam{"Arm7Tm0", false, 0U},
                      TimerChannelParam{"Arm7Tm1", false, 1U},
                      TimerChannelParam{"Arm7Tm2", false, 2U},
                      TimerChannelParam{"Arm7Tm3", false, 3U}));

TEST(Tier1TimersATest, F6_Arm9AndArm7ShareBusClockRate) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm9.emit_write32_abs(kRegIme, 0U);
    arm9.emit_write16_abs(timer_cnt_l(0U), 0xFB00U);
    arm9.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm9.emit_mcr_wfi();
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, timer_irq_bit(0U));
    arm7.emit_write32_abs(kRegIme, 0U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0xFB00U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(2400U);
    EXPECT_TRUE(harness.is_arm9_halted());
    EXPECT_TRUE(harness.is_arm7_halted());

    harness.step_ticks(400U);
    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier1TimersATest, F7_WriteCntLWritesReloadLatchOnlyWhileStopped) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x1234U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x0000U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x1234U);
}

TEST(Tier1TimersATest, F7_WriteCntLWhileRunningDoesNotOverwriteLiveCounter) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x1000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x9000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x1000U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x1000U);
}

TEST(Tier1TimersATest, F7_ZeroToOneStartReloadsCounterAfterAdvance) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x1000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 100U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x2222U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1024 | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1200U);

    EXPECT_GT(harness.read_main_ram32(result_addr(0U)), 0x1000U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x2222U);
}

TEST(Tier1TimersATest, F7_OneToZeroStopFreezesCounter) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerStart);
    emit_delay_nop(arm7, 100U);
    arm7.emit_write16_abs(timer_cnt_h(0U), 0x0000U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_delay_nop(arm7, 200U);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1500U);

    const u32 frozen = harness.read_main_ram32(result_addr(0U));
    EXPECT_GT(frozen, 50U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), frozen);
}

TEST(Tier1TimersATest, F7_OneToOneWritePreservesLiveCounter) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x0000U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale64 | kTimerStart);
    emit_delay_nop(arm7, 150U);
    arm7.emit_write16_abs(timer_cnt_l(0U), 0x7777U);
    arm7.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale64 | kTimerIrqEnable | kTimerStart);
    emit_read16_abs(arm7, ArmReg::R0, timer_cnt_l(0U));
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(1200U);

    EXPECT_LT(harness.read_main_ram32(result_addr(0U)), 0x1000U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
