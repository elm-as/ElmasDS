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

[[nodiscard]] ArmProgramEmitter make_counting_handler(Address base,
                                                      u32 count_slot,
                                                      u32 if_slot,
                                                      u32 ack_mask) {
    ArmProgramEmitter h(base);
    h.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(h, if_slot, ArmReg::R0);
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

class F1ToF3CpuTest : public ::testing::TestWithParam<CpuParam> {};

TEST_P(F1ToF3CpuTest, F1_ImeReadWriteBit0) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write32_abs(kRegIme, 0U);
    cpu.emit_read32_abs(ArmReg::R0, kRegIme);
    emit_store_result(cpu, 0U, ArmReg::R0);
    cpu.emit_write32_abs(kRegIme, 1U);
    cpu.emit_read32_abs(ArmReg::R0, kRegIme);
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_write32_abs(kRegIme, 0U);
    cpu.emit_read32_abs(ArmReg::R0, kRegIme);
    emit_store_result(cpu, 2U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);
}

TEST_P(F1ToF3CpuTest, F1_ImeGatesIrqVectoringUntilEnabled) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    const Address entry = cpu_entry(tc.is_arm9);
    const ArmProgramEmitter h = make_counting_handler(entry + 4U, 0U, 1U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, cpu_irq_slot(tc.is_arm9), entry + 4U);
    m.emit_write32_abs(kRegIe, kIrqVBlank);
    m.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(m, 8U, 9U);
    m.emit_read32_abs(ArmReg::R0, result_addr(0U));
    emit_store_result(m, 2U, ArmReg::R0);
    m.emit_write32_abs(kRegIme, 1U);
    m.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, build_with_handler(entry, h, m));
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & kIrqVBlank, kIrqVBlank);
}

TEST_P(F1ToF3CpuTest, F2_IeGatesPendingIrqUntilEnabled) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    const Address entry = cpu_entry(tc.is_arm9);
    const ArmProgramEmitter h = make_counting_handler(entry + 4U, 0U, 1U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, cpu_irq_slot(tc.is_arm9), entry + 4U);
    emit_enable_interrupts(m, 0U);
    emit_wait_host_flag(m, 8U, 9U);
    m.emit_read32_abs(ArmReg::R0, result_addr(0U));
    emit_store_result(m, 2U, ArmReg::R0);
    m.emit_write32_abs(kRegIe, kIrqVBlank);
    m.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, build_with_handler(entry, h, m));
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
}

TEST_P(F1ToF3CpuTest, F3_IfSetsAndW1cClears) {
    const CpuParam tc = GetParam();
    EmulatorE2eHarness harness;
    ArmProgramEmitter cpu(cpu_entry(tc.is_arm9));
    cpu.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(cpu, 8U, 9U);
    cpu.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(cpu, 0U, ArmReg::R0);
    emit_ack_irq(cpu, kIrqVBlank);
    cpu.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(cpu, 1U, ArmReg::R0);
    cpu.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, cpu.words());
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & kIrqVBlank, kIrqVBlank);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & kIrqVBlank, 0U);
}

INSTANTIATE_TEST_SUITE_P(Tier1PerCpu,
                         F1ToF3CpuTest,
                         ::testing::Values(CpuParam{"Arm9", true}, CpuParam{"Arm7", false}));

TEST(Tier1InterruptsATest, F1_Arm9AndArm7ImeAreIndependent) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h9 = make_counting_handler(kArm9DefaultEntry + 4U, 0U, 1U, kIrqVBlank);
    ArmProgramEmitter m9(h9.current_pc());
    emit_store_const_result(m9, 0U, 0U);
    emit_install_irq_trampoline(m9, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m9, kIrqVBlank);
    m9.emit_branch_self();

    const ArmProgramEmitter h7 = make_counting_handler(kArm7DefaultEntry + 4U, 2U, 3U, kIrqVBlank);
    ArmProgramEmitter m7(h7.current_pc());
    emit_store_const_result(m7, 2U, 0U);
    emit_install_irq_trampoline(m7, kArm7IrqSlot, kArm7DefaultEntry + 4U);
    m7.emit_write32_abs(kRegIe, kIrqVBlank);
    m7.emit_write32_abs(kRegIme, 0U);
    m7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{
                                   .arm9_code = build_with_handler(kArm9DefaultEntry, h9, m9),
                                   .arm7_code = build_with_handler(kArm7DefaultEntry, h7, m7),
                               });
    harness.step_ticks(600U);
    harness.trigger_vblank();
    harness.step_ticks(600U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);
}

TEST(Tier1InterruptsATest, F2_Arm9AndArm7IeReadWriteSelectiveBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIe, 0x00000001U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kRegIe, 0x003F3F7FU);
    arm9.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIe, 0x00000081U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_write32_abs(kRegIe, 0x01DF3FFFU);
    arm7.emit_read32_abs(ArmReg::R0, kRegIe);
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00000001U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x003F3F7FU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x00000081U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x01DF3FFFU);
}

TEST(Tier1InterruptsATest, F2_SelectiveEnableFiresOnlyMatchingSource) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h =
        make_counting_handler(kArm9DefaultEntry + 4U, 0U, 1U, kIrqVBlank | kIrqHBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, kIrqHBlank);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(500U);
    harness.trigger_vblank();
    harness.step_ticks(500U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);

    harness.trigger_hblank();
    harness.step_ticks(500U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), kIrqVBlank | kIrqHBlank);
}

}  // namespace
}  // namespace elmasds::e2e::m2
