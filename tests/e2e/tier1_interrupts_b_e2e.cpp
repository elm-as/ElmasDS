#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace elmasds::e2e::m2 {
namespace {

struct IrqRouteCase {
    const char* name;
    bool is_arm9;
    u32 irq_bit;
    u32 trigger_kind;  // 0=VBlank, 1=HBlank, 2=GxFifo, 3=Timer0, 4=Timer3, 5=Spi
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

void trigger_host_source(EmulatorE2eHarness& harness, u32 trigger_kind) {
    if (trigger_kind == 0U) {
        harness.trigger_vblank();
    } else if (trigger_kind == 1U) {
        harness.trigger_hblank();
    } else if (trigger_kind == 2U) {
        harness.trigger_gxfifo();
    }
}

void emit_peripheral_trigger(ArmProgramEmitter& main, u32 trigger_kind) {
    if (trigger_kind == 3U) {
        main.emit_write16_abs(timer_cnt_l(0U), 0xFC00U);
        main.emit_write16_abs(timer_cnt_h(0U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    } else if (trigger_kind == 4U) {
        main.emit_write16_abs(timer_cnt_l(3U), 0xFC00U);
        main.emit_write16_abs(timer_cnt_h(3U), kTimerPrescale1 | kTimerIrqEnable | kTimerStart);
    } else if (trigger_kind == 5U) {
        emit_spi_byte_xfer(main, kSpiEnable | kSpiIrqEnable | kSpiDeviceNone, 0x00U);
    }
}

TEST(Tier1InterruptsBTest, F3_Arm9IfPartial32BitW1cPreservesUnackedBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(kRegIme, 0U);
    emit_wait_host_flag(arm9, 8U, 9U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_ack_irq(arm9, kIrqVBlank);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.trigger_gxfifo();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqVBlank | kIrqGxFifo);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), kIrqGxFifo);
}

TEST(Tier1InterruptsBTest, F3_Arm7If8BitAnd16BitW1cPreserveOtherBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(kRegIme, 0U);
    emit_spi_byte_xfer(arm7, kSpiEnable | kSpiIrqEnable | kSpiDeviceNone, 0x00U);
    emit_wait_host_flag(arm7, 8U, 9U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_write8_abs(kRegIf, 0x01U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_write16_abs(kRegIf + 2U, 0x0080U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm7_code = arm7.words()});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.trigger_hblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqVBlank | kIrqHBlank | kIrqSpi);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), kIrqHBlank | kIrqSpi);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), kIrqHBlank);
}

TEST(Tier1InterruptsBTest, F3_HandlerW1cAcksOnlySpecificSource) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h = make_counting_handler(kArm9DefaultEntry + 4U, 0U, 1U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, kIrqVBlank);
    emit_wait_host_flag(m, 8U, 9U);
    m.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(m, 2U, ArmReg::R0);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(400U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(8U)), 1U);
    harness.trigger_vblank();
    harness.trigger_hblank();
    harness.write_main_ram32(result_addr(9U), 1U);
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), kIrqHBlank);
}

class F4IrqRouteTest : public ::testing::TestWithParam<IrqRouteCase> {};

TEST_P(F4IrqRouteTest, F4_RoutesSourceToExpectedCpuAndBit) {
    const IrqRouteCase tc = GetParam();
    EmulatorE2eHarness harness;
    const Address entry = cpu_entry(tc.is_arm9);
    const ArmProgramEmitter h = make_counting_handler(entry + 4U, 0U, 1U, tc.irq_bit);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, cpu_irq_slot(tc.is_arm9), entry + 4U);
    emit_enable_interrupts(m, tc.irq_bit);
    emit_peripheral_trigger(m, tc.trigger_kind);
    m.emit_branch_self();

    boot_for_cpu(harness, tc.is_arm9, build_with_handler(entry, h, m));
    harness.step_ticks(600U);
    trigger_host_source(harness, tc.trigger_kind);
    harness.step_ticks(2000U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U) << tc.name;
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & tc.irq_bit, tc.irq_bit) << tc.name;
}

INSTANTIATE_TEST_SUITE_P(
    Arm9AndArm7IrqSources,
    F4IrqRouteTest,
    ::testing::Values(IrqRouteCase{"Arm9VBlank", true, kIrqVBlank, 0U},
                      IrqRouteCase{"Arm9HBlank", true, kIrqHBlank, 1U},
                      IrqRouteCase{"Arm9GeometryFifo", true, kIrqGxFifo, 2U},
                      IrqRouteCase{"Arm9Timer0", true, timer_irq_bit(0U), 3U},
                      IrqRouteCase{"Arm9Timer3", true, timer_irq_bit(3U), 4U},
                      IrqRouteCase{"Arm7VBlank", false, kIrqVBlank, 0U},
                      IrqRouteCase{"Arm7HBlank", false, kIrqHBlank, 1U},
                      IrqRouteCase{"Arm7Timer0", false, timer_irq_bit(0U), 3U},
                      IrqRouteCase{"Arm7Spi", false, kIrqSpi, 5U}));

TEST(Tier1InterruptsBTest, F5_DirectBootSetsPostflgBit0OnBothCpus) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_read8_abs(arm9, ArmReg::R0, kRegPostflg);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_read8_abs(arm7, ArmReg::R0, kRegPostflg);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    boot_dual_program(
        harness, DualProgramSpec{.arm9_code = arm9.words(), .arm7_code = arm7.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 1U);
}

TEST(Tier1InterruptsBTest, F5_Arm9PostflgBit1IsReadWrite) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write8_abs(kRegPostflg, 0x03U);
    emit_read8_abs(arm9, ArmReg::R0, kRegPostflg);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write8_abs(kRegPostflg, 0x01U);
    emit_read8_abs(arm9, ArmReg::R0, kRegPostflg);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(800U);

    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x03U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x01U);
}

TEST(Tier1InterruptsBTest, F5_Arm7HaltcntHaltAndSleepWakeOnIrq) {
    for (const u8 cmd : {kHaltcntHalt, kHaltcntSleep}) {
        EmulatorE2eHarness harness;
        const ArmProgramEmitter h =
            make_counting_handler(kArm7DefaultEntry + 4U, 0U, 1U, kIrqVBlank);
        ArmProgramEmitter m(h.current_pc());
        emit_store_const_result(m, 0U, 0U);
        emit_install_irq_trampoline(m, kArm7IrqSlot, kArm7DefaultEntry + 4U);
        emit_enable_interrupts(m, kIrqVBlank);
        m.emit_write8_abs(kRegHaltcnt, cmd);
        emit_store_const_result(m, 2U, 0xCAFE0001U);
        m.emit_branch_self();

        boot_dual_program(
            harness, DualProgramSpec{.arm7_code = build_with_handler(kArm7DefaultEntry, h, m)});
        harness.step_ticks(400U);
        EXPECT_TRUE(harness.is_arm7_halted());
        EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);

        harness.trigger_vblank();
        harness.step_ticks(400U);
        EXPECT_FALSE(harness.is_arm7_halted());
        EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
        EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0xCAFE0001U);
    }
}

TEST(Tier1InterruptsBTest, F5_Arm9WfiHaltsAndWakesOnIrq) {
    EmulatorE2eHarness harness;
    const ArmProgramEmitter h = make_counting_handler(kArm9DefaultEntry + 4U, 0U, 1U, kIrqVBlank);
    ArmProgramEmitter m(h.current_pc());
    emit_store_const_result(m, 0U, 0U);
    emit_install_irq_trampoline(m, kArm9IrqSlot, kArm9DefaultEntry + 4U);
    emit_enable_interrupts(m, kIrqVBlank);
    m.emit_mcr_wfi();
    emit_store_const_result(m, 2U, 0xCAFE0003U);
    m.emit_branch_self();

    boot_dual_program(harness,
                      DualProgramSpec{.arm9_code = build_with_handler(kArm9DefaultEntry, h, m)});
    harness.step_ticks(600U);
    EXPECT_TRUE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0U);

    harness.trigger_vblank();
    harness.step_ticks(600U);
    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0xCAFE0003U);
}

TEST(Tier1InterruptsBTest, F5_Arm9Combined16BitPostflgWriteUpdatesPostflgWithoutHalt) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegPostflg, 0x8003U);
    emit_read8_abs(arm9, ArmReg::R0, kRegPostflg);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    boot_dual_program(harness, DualProgramSpec{.arm9_code = arm9.words()});
    harness.step_ticks(800U);

    EXPECT_FALSE(harness.is_arm9_halted());
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x03U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
