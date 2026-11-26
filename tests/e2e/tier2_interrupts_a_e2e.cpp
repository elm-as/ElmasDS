#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace elmasds::e2e::m2 {
namespace {

struct CpuParam {
    const char* name;
    bool is_arm9;
};

[[nodiscard]] Address cpu_entry(bool is_arm9) noexcept {
    return is_arm9 ? kArm9DefaultEntry : kArm7DefaultEntry;
}

[[nodiscard]] Address cpu_irq_slot(bool is_arm9) noexcept {
    return is_arm9 ? kArm9IrqSlot : kArm7IrqSlot;
}

void boot_for_cpu(EmulatorE2eHarness& harness, bool is_arm9, std::vector<u32> code) {
    const DualProgramSpec spec =
        is_arm9 ? DualProgramSpec{.arm9_code = std::move(code)}
                : DualProgramSpec{.arm7_code = std::move(code)};
    boot_dual_program(harness, spec);
}

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

class T2PerCpuIntTest : public ::testing::TestWithParam<CpuParam> {};

TEST_P(T2PerCpuIntTest, F1_ImeUpperBitsMaskedToZero) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write32_abs(kRegIme, 0xFFFFFFFFU);
    cpu.emit_read32_abs(ArmReg::R0, kRegIme);
    emit_store_result(cpu, 0U, ArmReg::R0);
    cpu.emit_write32_abs(kRegIme, 0xFFFFFFFEU);
    cpu.emit_read32_abs(ArmReg::R0, kRegIme);
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
}

TEST_P(T2PerCpuIntTest, F1_Ime0WakesHaltedCpuWithoutVectoring) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    const Address entry = cpu_entry(tc.is_arm9);
    const ArmProgramEmitter h = make_inc_ack_handler(entry + 4U, 0U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, cpu_irq_slot(tc.is_arm9), entry + 4U);
    m.emit_write32_abs(kRegIe, kIrqVBlank);
    m.emit_write32_abs(kRegIme, 0U);
    if (tc.is_arm9) {
        m.emit_mcr_wfi();
    } else {
        m.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    }
    emit_store_const_result(m, 1U, 0xAA551234U);
    m.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, build_with_handler(entry, h, m));
    harness.step_ticks(500U);
    EXPECT_TRUE(tc.is_arm9 ? harness.is_arm9_halted() : harness.is_arm7_halted());

    harness.trigger_vblank();
    harness.step_ticks(500U);
    EXPECT_FALSE(tc.is_arm9 ? harness.is_arm9_halted() : harness.is_arm7_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0xAA551234U);
}

TEST_P(T2PerCpuIntTest, F2_IeMasksInvalidBitsAndGap020CReadsZero) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write32_abs(kRegIe, 0xFFFFFFFFU);
    cpu.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(cpu, 0U, ArmReg::R0);
    cpu.emit_write32_abs(0x0400020CU, 0xFFFFFFFFU);
    cpu.emit_read32_abs(ArmReg::R0, 0x0400020CU);
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(800U);

    const u32 expected_mask = tc.is_arm9 ? 0x003F3F7FU : 0x01DF3FFFU;
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), expected_mask);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
}

TEST_P(T2PerCpuIntTest, F3_WritingZeroToIfLeavesPendingFlagsIntact) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(cpu, 8U, 9U);
    cpu.emit_write32_abs(kRegIf, 0U);
    cpu.emit_write16_abs(kRegIf, 0U);
    cpu.emit_write8_abs(kRegIf, 0U);
    cpu.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(cpu, 0U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqVBlank);
}

TEST_P(T2PerCpuIntTest, F3_WritingOnesToUnsetIfIsSafeNoOp) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    const Address entry = cpu_entry(tc.is_arm9);
    const ArmProgramEmitter h = make_inc_ack_handler(entry + 4U, 0U, 0xFFFFFFFFU);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, cpu_irq_slot(tc.is_arm9), entry + 4U);
    emit_enable_interrupts(m, tc.is_arm9 ? 0x003F3F7FU : 0x01DF3FFFU);
    m.emit_write32_abs(kRegIf, 0xFFFFFFFFU);
    m.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(m, 1U, ArmReg::R0);
    m.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, build_with_handler(entry, h, m));
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
}

TEST_P(T2PerCpuIntTest, F5_PostflgWriteOnceAndMasking) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write8_abs(kRegPostflg, 0x00U);
    emit_read8_abs(cpu, ArmReg::R0, kRegPostflg);
    emit_store_result(cpu, 0U, ArmReg::R0);
    cpu.emit_write8_abs(kRegPostflg, 0xFFU);
    emit_read8_abs(cpu, ArmReg::R0, kRegPostflg);
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), tc.is_arm9 ? 0x03U : 0x01U);
}

INSTANTIATE_TEST_SUITE_P(Tier2PerCpu,
                         T2PerCpuIntTest,
                         ::testing::Values(CpuParam{"Arm9", true}, CpuParam{"Arm7", false}));

TEST(Tier2InterruptsATest, F1_Ime1WithCpsrI1WakesWfiThenVectorsOnCpsrClear) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h = make_inc_ack_handler(kArm9DefaultEntry + 4U, 0U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    m.emit_write32_abs(kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, kIrqVBlank);
    m.emit_mcr_wfi();
    m.emit_read32_abs(ArmReg::R0, result_addr(0U));
    emit_store_result(m, 1U, ArmReg::R0);
    m.emit_enable_irq(ArmReg::R0);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(500U);
    EXPECT_TRUE(harness.is_arm9_halted());

    harness.trigger_vblank();
    harness.step_ticks(600U);
    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
}

TEST(Tier2InterruptsATest, F2_IeHalfwordWritesMaskInvalidBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIe, 0xFFFFU);
    arm9.emit_write16_abs(kRegIe + 2U, 0xFFFFU);
    arm9.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(kRegIe, 0xFFFFU);
    arm7.emit_write16_abs(kRegIe + 2U, 0xFFFFU);
    arm7.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x003F3F7FU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x01DF3FFFU);
}

TEST(Tier2InterruptsATest, F2_DisablingIeMidRunSuppressesTimerIrq) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h = make_inc_ack_handler(kArm7DefaultEntry + 4U, 0U, timer_irq_bit(0U));
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, kArm7IrqSlot, kArm7DefaultEntry + 4U);
    emit_enable_interrupts(m, timer_irq_bit(0U));
    m.emit_write32_abs(kRegIe, 0U);
    m.emit_write16_abs(timer_cnt_l(0U), 0xFFF0U);
    m.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    emit_delay_nop(m, 200U);
    m.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(m, 1U, ArmReg::R0);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm7_code = build_with_handler(kArm7DefaultEntry, h, m)});
    harness.step_ticks(1200U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & timer_irq_bit(0U), timer_irq_bit(0U));
}

}  // namespace
}  // namespace elmasds::e2e::m2
