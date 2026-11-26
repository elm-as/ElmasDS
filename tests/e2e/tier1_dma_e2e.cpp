#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace elmasds::e2e::m2 {
namespace {

constexpr Address kSrcBuf = 0x02200000U;
constexpr Address kDstBuf = 0x02210000U;
constexpr Address kCascadeBase = 0x02220000U;

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

void seed_ram(EmulatorE2eHarness& harness, Address base, std::span<const u32> values) {
    for (std::size_t i = 0U; i < values.size(); ++i) {
        harness.write_main_ram32(base + static_cast<Address>(i * 4U), values[i]);
    }
}

void emit_irq_capture_stub(ArmProgramEmitter& emitter, Address irq_slot, u32 ie_mask) {
    const Address main_pc = emitter.base_address() + 128U;
    emitter.emit_b(main_pc);
    const Address handler_pc = emitter.current_pc();
    emitter.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(emitter, 0U, ArmReg::R0);
    emit_ack_irq(emitter, ie_mask);
    emit_hle_irq_return(emitter);
    ASSERT_LE(emitter.current_pc(), main_pc);
    while (emitter.current_pc() < main_pc) {
        emitter.emit_nop();
    }
    emit_install_irq_trampoline(emitter, irq_slot, handler_pc);
    emit_enable_interrupts(emitter, ie_mask);
}

void emit_all_channel_irqs_program(ArmProgramEmitter& emitter, Address irq_slot) {
    emit_irq_capture_stub(emitter, irq_slot, 0x0F00U);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        emit_store_const_result(emitter, 0U, 0U);
        const u16 ctrl = static_cast<u16>(kDmaWidth32 | kDmaIrqEnable | kDmaEnable);
        emit_dma_setup(emitter, DmaSetup{ch, kSrcBuf, kDstBuf + (ch * 16U), 1U, ctrl});
        emit_delay_nop(emitter, 6U);
        emitter.emit_read32_abs(ArmReg::R0, result_addr(0U));
        emit_store_result(emitter, ch + 1U, ArmReg::R0);
    }
    emitter.emit_branch_self();
}

void verify_vblank_cascade(bool is_arm9, u32 pattern) {
    ArmProgramEmitter prog(is_arm9 ? kArm9DefaultEntry : kArm7DefaultEntry);
    const u32 shift = is_arm9 ? 11U : 12U;
    const u16 ctrl = static_cast<u16>(kDmaWidth32 | (1U << shift) | kDmaEnable);
    emit_dma_setup(prog, DmaSetup{3U, kCascadeBase + 0x300U, kCascadeBase + 0x400U, 1U, ctrl});
    emit_dma_setup(prog, DmaSetup{2U, kCascadeBase + 0x200U, kCascadeBase + 0x300U, 1U, ctrl});
    emit_dma_setup(prog, DmaSetup{1U, kCascadeBase + 0x100U, kCascadeBase + 0x200U, 1U, ctrl});
    emit_dma_setup(prog, DmaSetup{0U, kCascadeBase + 0x000U, kCascadeBase + 0x100U, 1U, ctrl});
    prog.emit_branch_self();

    EmulatorE2eHarness harness;
    const DualProgramSpec spec =
        is_arm9 ? DualProgramSpec{prog.words(), {}} : DualProgramSpec{{}, prog.words()};
    boot_dual_program(harness, spec);
    harness.write_main_ram32(kCascadeBase, pattern);
    harness.step_ticks(2500ULL);
    EXPECT_EQ(harness.read_main_ram32(kCascadeBase + 0x400U), 0U);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kCascadeBase + 0x400U), pattern);
}

void verify_immediate_channel(u32 ch, bool is_arm9) {
    ArmProgramEmitter prog(is_arm9 ? kArm9DefaultEntry : kArm7DefaultEntry);
    emit_dma_setup(prog, DmaSetup{ch, kSrcBuf, kDstBuf, 4U, kDmaWidth32 | kDmaEnable});
    emit_read16_abs(prog, ArmReg::R0, dma_cnt_h(ch));
    emit_store_result(prog, 0U, ArmReg::R0);
    prog.emit_branch_self();

    EmulatorE2eHarness harness;
    const DualProgramSpec spec =
        is_arm9 ? DualProgramSpec{prog.words(), {}} : DualProgramSpec{{}, prog.words()};
    boot_dual_program(harness, spec);
    const std::array<u32, 4> data{0x10203000U + ch, 0x10203001U, 0x10203002U, 0x10203003U};
    seed_ram(harness, kSrcBuf, data);
    harness.step_ticks(4000ULL);
    for (u32 i = 0U; i < 4U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), data[i]);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & kDmaEnable, 0U);
}

class Tier1DmaChannelTest : public testing::TestWithParam<u32> {};

TEST_P(Tier1DmaChannelTest, Arm9ImmediateChannelCopiesWordArray) {
    verify_immediate_channel(GetParam(), true);
}

TEST_P(Tier1DmaChannelTest, Arm7ImmediateChannelCopiesWordArray) {
    verify_immediate_channel(GetParam(), false);
}

TEST_P(Tier1DmaChannelTest, Arm9DmaFill32WritesPatternPerChannel) {
    const u32 ch = GetParam();
    const u32 pattern = 0xA55A0000U | (ch * 0x1111U);
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(dma_fill(ch), pattern);
    arm9.emit_read32_abs(ArmReg::R0, dma_fill(ch));
    emit_store_result(arm9, 0U, ArmReg::R0);
    const u16 ctrl = static_cast<u16>(kDmaSrcFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{ch, dma_fill(ch), kDstBuf, 6U, ctrl});
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kDstBuf + 24U, 0xDEADBEEFU);
    harness.step_ticks(4000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), pattern);
    for (u32 i = 0U; i < 6U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), pattern);
    }
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 24U), 0xDEADBEEFU);
}

INSTANTIATE_TEST_SUITE_P(AllChannels, Tier1DmaChannelTest, testing::Values(0U, 1U, 2U, 3U));

TEST(Tier1DmaE2e, Arm9ChannelPriorityZeroToThreeCascade) {
    verify_vblank_cascade(true, 0xCAFE1234U);
}

TEST(Tier1DmaE2e, Arm7ChannelPriorityZeroToThreeCascade) {
    verify_vblank_cascade(false, 0xBEEF5678U);
}

TEST(Tier1DmaE2e, Arm9DmaFill16HalfwordPattern) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write32_abs(dma_fill(0U), 0xA55A1234U);
    emit_dma_setup(arm9, DmaSetup{0U, dma_fill(0U), kDstBuf, 5U, kDmaSrcFixed | kDmaEnable});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kDstBuf + 8U, 0xFFFFFFFFU);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x12341234U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x12341234U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 8U), 0x1234U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 10U), 0xFFFFU);
}

TEST(Tier1DmaE2e, Dma16BitAnd32BitWidthsPreserveBounds) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 3U, kDmaEnable});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf + 16U, kDstBuf + 32U, 2U, kDmaWidth32 | kDmaEnable});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 2>{0x22221111U, 0x44443333U});
    seed_ram(harness, kSrcBuf + 16U, std::array<u32, 2>{0xAABBCCDDU, 0x11223344U});
    harness.write_main_ram32(kDstBuf + 4U, 0x99998888U);
    harness.write_main_ram32(kDstBuf + 40U, 0xCAFEBABEU);
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x22221111U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 4U), 0x3333U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 6U), 0x9999U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 32U), 0xAABBCCDDU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 36U), 0x11223344U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 40U), 0xCAFEBABEU);
}

TEST(Tier1DmaE2e, DmaDestIncDecFixedModes) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 inc = static_cast<u16>(kDmaDestInc | kDmaWidth32 | kDmaEnable);
    const u16 dec = static_cast<u16>(kDmaDestDec | kDmaWidth32 | kDmaEnable);
    const u16 fix = static_cast<u16>(kDmaDestFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 3U, inc});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf, kDstBuf + 0x48U, 3U, dec});
    emit_dma_setup(arm9, DmaSetup{2U, kSrcBuf, kDstBuf + 0x80U, 3U, fix});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 3>{0x10U, 0x20U, 0x30U});
    harness.step_ticks(3500ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x00U), 0x10U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x08U), 0x30U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x48U), 0x10U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x40U), 0x30U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x80U), 0x30U);
}

TEST(Tier1DmaE2e, DmaSrcIncDecFixedModes) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 dec = static_cast<u16>(kDmaSrcDec | kDmaWidth32 | kDmaEnable);
    const u16 fix = static_cast<u16>(kDmaSrcFixed | kDmaWidth32 | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf + 8U, kDstBuf, 3U, dec});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf + 4U, kDstBuf + 32U, 3U, fix});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 3>{0xAAA1U, 0xBBB2U, 0xCCC3U});
    harness.step_ticks(3000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0xCCC3U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 8U), 0xAAA1U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 32U), 0xBBB2U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 40U), 0xBBB2U);
}

TEST(Tier1DmaE2e, Arm9Large21BitWordCountViaCntHUpperBits) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_dma_setup(arm9, DmaSetup{0U, 0x02200000U, 0x02240000U, 0x0004U, 0x0001U | kDmaEnable});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(0x02200000U, 0x11112222U);
    harness.write_main_ram32(0x02220004U, 0x77776666U);
    harness.write_main_ram32(0x02260008U, 0x99998888U);
    harness.step_ticks(5000ULL);
    EXPECT_EQ(harness.read_main_ram16(0x02240000U), 0x2222U);
    EXPECT_EQ(harness.read_main_ram16(0x02260006U), 0x7777U);
    EXPECT_EQ(harness.read_main_ram16(0x02260008U), 0x8888U);
}

TEST(Tier1DmaE2e, Arm7DmaCopiesFromArm7WramToMainRam) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write32_abs(0x03801000U, 0x13572468U);
    arm7.emit_write32_abs(0x03801004U, 0x9BDF0246U);
    emit_dma_setup(arm7, DmaSetup{1U, 0x03801000U, kDstBuf, 2U, kDmaWidth32 | kDmaEnable});
    arm7.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{{}, arm7.words()});
    harness.step_ticks(4000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x13572468U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x9BDF0246U);
}

TEST(Tier1DmaE2e, Arm9VBlankAndHBlankTimingsWaitUntilTriggered) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 vb = static_cast<u16>(kDmaWidth32 | (1U << 11U) | kDmaEnable);
    const u16 hb = static_cast<u16>(kDmaWidth32 | (2U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 1U, vb});
    emit_dma_setup(arm9, DmaSetup{1U, kSrcBuf + 4U, kDstBuf + 4U, 1U, hb});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 2>{0xAAAA1111U, 0xBBBB2222U});
    harness.step_ticks(1500ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0U);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0xAAAA1111U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0U);
    harness.trigger_hblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0xBBBB2222U);
}

TEST(Tier1DmaE2e, Arm9RepeatDestReloadReArmsSameDestination) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 ctrl = static_cast<u16>(
        kDmaDestReload | kDmaRepeat | kDmaWidth32 | (1U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 2U, ctrl});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 4>{0x111U, 0x222U, 0x333U, 0x444U});
    harness.step_ticks(1500ULL);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x111U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x222U);
    harness.trigger_vblank();
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x333U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x444U);
}

TEST(Tier1DmaE2e, Arm9RepeatDestIncAdvancesDestination) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    const u16 ctrl = static_cast<u16>(
        kDmaDestInc | kDmaSrcFixed | kDmaRepeat | kDmaWidth32 | (1U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 2U, ctrl});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.write_main_ram32(kSrcBuf, 0x76543210U);
    harness.step_ticks(1500ULL);
    harness.trigger_vblank();
    harness.trigger_vblank();
    for (u32 i = 0U; i < 4U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0x76543210U);
    }
}

TEST(Tier1DmaE2e, Arm9DmaCompletionIrqsFireForAllChannels) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_all_channel_irqs_program(arm9, kArm9IrqSlot);
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    harness.step_ticks(8000ULL);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(ch + 1U)), dma_irq_bit(ch));
    }
}

TEST(Tier1DmaE2e, Arm7DmaCompletionIrqsFireForAllChannels) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_all_channel_irqs_program(arm7, kArm7IrqSlot);
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{{}, arm7.words()});
    harness.step_ticks(12000ULL);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(ch + 1U)), dma_irq_bit(ch));
    }
}

TEST(Tier1DmaE2e, Arm9GeometryFifoTimingAndTrigger) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_irq_capture_stub(arm9, kArm9IrqSlot, kIrqGxFifo);
    const u16 gx_ctrl = static_cast<u16>(kDmaWidth32 | (7U << 11U) | kDmaEnable);
    emit_dma_setup(arm9, DmaSetup{0U, kSrcBuf, kDstBuf, 2U, gx_ctrl});
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), {}});
    seed_ram(harness, kSrcBuf, std::array<u32, 2>{0x33334444U, 0x55556666U});
    harness.step_ticks(2000ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0U);
    harness.trigger_gxfifo();
    harness.step_ticks(500ULL);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0x33334444U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 4U), 0x55556666U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqGxFifo);
}

}  // namespace
}  // namespace elmasds::e2e::m2
