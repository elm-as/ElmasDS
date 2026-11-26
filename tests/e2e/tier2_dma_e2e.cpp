#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>

namespace elmasds::e2e::m2 {
namespace {
constexpr Address kSrcBuf = 0x02200000U;
constexpr Address kDstBuf = 0x02240000U;
constexpr Address kFlagAddr = 0x022F0000U;
struct DmaSetup {
    u32 channel = 0U;
    Address sad = 0U;
    Address dad = 0U;
    u16 cnt_l = 0U;
    u16 cnt_h = 0U;
};

void emit_dma_setup(ArmProgramEmitter& emitter, const DmaSetup& cfg) {
    emitter.emit_write32_abs(dma_sad(cfg.channel), cfg.sad);
    emitter.emit_write32_abs(dma_dad(cfg.channel), cfg.dad);
    emitter.emit_write16_abs(dma_cnt_l(cfg.channel), cfg.cnt_l);
    emitter.emit_write16_abs(dma_cnt_h(cfg.channel), cfg.cnt_h);
}

void emit_poll_nonzero(ArmProgramEmitter& emitter, Address flag_addr) {
    const Address loop_pc = emitter.current_pc();
    emitter.emit_read32_abs(ArmReg::R0, flag_addr);
    emitter.emit_cmp_imm(ArmReg::R0, 0U);
    emitter.emit_b(loop_pc, ArmCond::Eq);
}

void emit_read_dma_regs(ArmProgramEmitter& emitter, u32 ch, u32 base_slot) {
    emitter.emit_read32_abs(ArmReg::R0, dma_sad(ch));
    emit_store_result(emitter, base_slot + 0U, ArmReg::R0);
    emitter.emit_read32_abs(ArmReg::R0, dma_dad(ch));
    emit_store_result(emitter, base_slot + 1U, ArmReg::R0);
    emit_read16_abs(emitter, ArmReg::R0, dma_cnt_l(ch));
    emit_store_result(emitter, base_slot + 2U, ArmReg::R0);
    emit_read16_abs(emitter, ArmReg::R0, dma_cnt_h(ch));
    emit_store_result(emitter, base_slot + 3U, ArmReg::R0);
}

void run_spec(EmulatorE2eHarness& harness, const DualProgramSpec& spec, TickCount ticks) {
    boot_dual_program(harness, spec);
    harness.step_ticks(ticks);
}

class Tier2DmaArm7MaskTest : public testing::TestWithParam<u32> {};

TEST_P(Tier2DmaArm7MaskTest, Arm7DmaRegisterMasksPerChannel) {
    const u32 ch = GetParam();
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_dma_setup(arm7, DmaSetup{ch, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFU, 0x7FFFU});
    emit_read_dma_regs(arm7, ch, 0U);
    arm7.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{{}, arm7.words()}, 4000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), ch == 0U ? 0x07FFFFFEU : 0x0FFFFFFEU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), ch <= 2U ? 0x07FFFFFEU : 0x0FFFFFFEU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), ch <= 2U ? 0x3FFFU : 0xFFFFU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x77E0U);
}

INSTANTIATE_TEST_SUITE_P(AllChannels, Tier2DmaArm7MaskTest, testing::Values(0U, 1U, 2U, 3U));

TEST(Tier2DmaE2e, Arm9DmaRegisterMasksAndIsolationFromArm7) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_dma_setup(arm9, DmaSetup{0U, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFU, 0x7FFFU});
    emit_read_dma_regs(arm9, 0U, 0U);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    emit_read_dma_regs(arm7, 0U, 4U);
    arm7.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), arm7.words()}, 6000ULL);
    const std::array<u32, 4> expected{0x0FFFFFFEU, 0x0FFFFFFEU, 0xFFFFU, 0x7FFFU};
    for (u32 slot = 0U; slot < 4U; ++slot) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(slot)), expected[slot]);
        EXPECT_EQ(harness.read_main_ram32(result_addr(slot + 4U)), 0U);
    }
}

TEST(Tier2DmaE2e, Arm7DmaFillReadsZeroAndIgnoresWrites) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(dma_fill(0U), 0xCAFEBABEU);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm9, kFlagAddr + 4U);
    arm9.emit_read32_abs(ArmReg::R0, dma_fill(0U));
    emit_store_result(arm9, 4U, ArmReg::R0);
    arm9.emit_branch_self();
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm7.emit_write32_abs(dma_fill(ch), 0xDEADBEEFU);
        arm7.emit_read32_abs(ArmReg::R0, dma_fill(ch));
        emit_store_result(arm7, ch, ArmReg::R0);
    }
    arm7.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm7.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), arm7.words()}, 8000ULL);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(ch)), 0U);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0xCAFEBABEU);
}

TEST(Tier2DmaE2e, Arm9DmaFillDisabledChannelDoesNotWriteMemory) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(dma_fill(0U), 0x11223344U);
    emit_dma_setup(arm9, DmaSetup{0U, dma_fill(0U), kDstBuf, 4U, kDmaSrcFixed | kDmaWidth32});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kDstBuf, 0x99887766U);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0x99887766U);
}

TEST(Tier2DmaE2e, Arm9DmaFillWithDestDecAndFixed) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(dma_fill(1U), 0x55667788U);
    const u16 dec = static_cast<u16>(kDmaDestDec | kDmaSrcFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{1U, dma_fill(1U), kDstBuf + 8U, 3U, dec});
    arm9.emit_write32_abs(dma_fill(2U), 0xAABBCCDDU);
    const u16 fix = static_cast<u16>(kDmaDestFixed | kDmaSrcFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{2U, dma_fill(2U), kDstBuf + 32U, 4U, fix});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), {}}, 4000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x55667788U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 8U), 0x55667788U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 32U), 0xAABBCCDDU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 36U), 0U);
}

TEST(Tier2DmaE2e, Arm9DmaFillSrcIncReadsConsecutiveFillRegs) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm9.emit_write32_abs(dma_fill(ch), 0x10010000U + (ch * 0x1001U));
    }
    const u16 ctrl = static_cast<u16>(kDmaSrcInc | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, dma_fill(0U), kDstBuf, 4U, ctrl});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), {}}, 4000ULL);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (ch * 4U)), 0x10010000U + (ch * 0x1001U));
    }
}

TEST(Tier2DmaE2e, Arm9DmaFillChannelsAreIndependent) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm9.emit_write32_abs(dma_fill(ch), 0x70000000U | (ch + 1U));
    }
    const u16 ctrl = static_cast<u16>(kDmaSrcFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{2U, dma_fill(2U), kDstBuf, 2U, ctrl});
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm9.emit_read32_abs(ArmReg::R0, dma_fill(ch));
        emit_store_result(arm9, ch, ArmReg::R0);
    }
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), {}}, 4000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x70000003U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x70000003U);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(ch)), 0x70000000U | (ch + 1U));
    }
}

TEST(Tier2DmaE2e, DmaUnalignedAddressesMaskLowBits) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf + 3U, kDstBuf + 3U, 1U, kDmaEnable});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf + 18U, kDstBuf + 18U, 1U, kDmaWidth32 | kDmaEnable});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf + 0U, 0xCAFE1111U);
    harness.write_main_ram32(kSrcBuf + 16U, 0x12345678U);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 0U), 0U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 2U), 0xCAFEU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 16U), 0x12345678U);
}

TEST(Tier2DmaE2e, DmaProhibitedSrcCtrlIsSafeNoOp) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 ctrl = static_cast<u16>((3U << 7U) | kDmaWidth32 | kDmaIrqEnable | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 4U, ctrl});
    emit_read16_abs(arm9, ArmReg::R0, dma_cnt_h(0U));
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kDstBuf, 0x55555555U);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0x55555555U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & kDmaEnable, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & dma_irq_bit(0U), 0U);
}

TEST(Tier2DmaE2e, Arm9ZeroCountTransfersTwoMillionUnits) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 0U, kDmaDestFixed | kDmaEnable});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0x11112222U);
    harness.write_main_ram32(0x021FFFFCU, 0xBEEF1234U);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf), 0xBEEFU);
}

void verify_arm7_zero_count(u32 ch, Address span_bytes) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_dma_setup(arm7, DmaSetup{ch, kSrcBuf, kDstBuf, 0U, kDmaEnable});
    arm7.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{{}, arm7.words()});
    harness.write_main_ram32(kSrcBuf, 0x00001111U);
    harness.write_main_ram32(kSrcBuf + span_bytes - 4U, 0xDDDD4444U);
    harness.write_main_ram32(kSrcBuf + span_bytes, 0xCCCC3333U);
    harness.write_main_ram32(kDstBuf + span_bytes, 0x77776666U);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf), 0x1111U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + span_bytes - 2U), 0xDDDDU);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + span_bytes), 0x6666U);
}

TEST(Tier2DmaE2e, Arm7Ch0To2ZeroCountTransfers16384Units) {
    verify_arm7_zero_count(0U, 0x8000U);
}

TEST(Tier2DmaE2e, Arm7Ch3ZeroCountTransfers65536Units) {
    verify_arm7_zero_count(3U, 0x20000U);
}

TEST(Tier2DmaE2e, Arm9HBlankDmaSuppressedDuringVBlank) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    const u16 hb = static_cast<u16>(kDmaWidth32 | (2U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 1U, hb});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0xCAFE5555U);
    harness.step_ticks(820000ULL);
    harness.write_main_ram32(kFlagAddr, 1U);
    harness.step_ticks(1000ULL);
    harness.trigger_hblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0U);
    harness.step_ticks(310000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0xCAFE5555U);
}

TEST(Tier2DmaE2e, ImmediateIgnoresRepeatAndGxFifoRejectsWrongTriggers) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 imm = static_cast<u16>(kDmaRepeat | kDmaWidth32 | kDmaEnable);
    const u16 gx = static_cast<u16>(kDmaWidth32 | (7U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 1U, imm});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf + 4U, kDstBuf + 4U, 1U, gx});
    emit_read16_abs(arm9, ArmReg::R0, dma_cnt_h(0U));
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0x11U);
    harness.write_main_ram32(kSrcBuf + 4U, 0x22U);
    harness.step_ticks(2500ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & kDmaEnable, 0U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0x11U);
    harness.trigger_vblank();
    harness.trigger_hblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0U);
    harness.trigger_gxfifo();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x22U);
}

TEST(Tier2DmaE2e, WritingMmioWhileArmedDoesNotAlterLatchedTransfer) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 vb = static_cast<u16>(kDmaWidth32 | (1U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 1U, vb});
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write32_abs(dma_sad(0U), kSrcBuf + 16U);
    arm9.emit_write32_abs(dma_dad(0U), kDstBuf + 16U);
    arm9.emit_write16_abs(dma_cnt_l(0U), 4U);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0x1234ABCDU);
    harness.write_main_ram32(kSrcBuf + 16U, 0x99999999U);
    harness.step_ticks(1500ULL);
    harness.write_main_ram32(kFlagAddr, 1U);
    harness.step_ticks(1000ULL);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0x1234ABCDU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 16U), 0U);
}

TEST(Tier2DmaE2e, DmaCompletionWithoutIrqEnableDoesNotSetIf) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_enable_interrupts(arm9, 0x0F00U);
    const u16 ctrl = static_cast<u16>(kDmaWidth32 | kDmaEnable);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        emit_dma_setup(arm9, DmaSetup{ch, kSrcBuf, kDstBuf + (ch * 4U), 1U, ctrl});
    }
    arm9.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_spec(harness, DualProgramSpec{arm9.words(), {}}, 3000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x0F00U, 0U);
}

TEST(Tier2DmaE2e, DisablingRepeatDmaStopsSubsequentTransfers) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 ctrl = static_cast<u16>(
        kDmaDestReload | kDmaSrcFixed | kDmaRepeat | kDmaWidth32 | (1U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 1U, ctrl});
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(dma_cnt_h(0U), 0U);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0xABCD0001U);
    harness.step_ticks(1500ULL);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0xABCD0001U);
    harness.write_main_ram32(kDstBuf, 0U);
    harness.write_main_ram32(kFlagAddr, 1U);
    harness.step_ticks(1000ULL);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf), 0U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
