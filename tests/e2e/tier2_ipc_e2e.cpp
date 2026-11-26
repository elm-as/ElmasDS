#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

namespace elmasds::e2e::m2 {
namespace {

constexpr Address kFlagAddr = 0x022F0000U;

void emit_poll_nonzero(ArmProgramEmitter& emitter, Address flag_addr) {
    const Address loop_pc = emitter.current_pc();
    emitter.emit_read32_abs(ArmReg::R0, flag_addr);
    emitter.emit_cmp_imm(ArmReg::R0, 0U);
    emitter.emit_b(loop_pc, ArmCond::Eq);
}

struct IrqStubConfig {
    Address irq_slot = 0U;
    u32 ie_mask = 0U;
    u32 count_slot = 0U;
};

void emit_counting_irq_stub(ArmProgramEmitter& emitter, const IrqStubConfig& cfg) {
    const Address main_pc = emitter.base_address() + 128U;
    emitter.emit_b(main_pc);
    const Address handler_pc = emitter.current_pc();
    emitter.emit_read32_abs(ArmReg::R0, result_addr(cfg.count_slot));
    emitter.emit_add_imm(RegImmOp{ArmReg::R0, ArmReg::R0, 1U});
    emit_store_result(emitter, cfg.count_slot, ArmReg::R0);
    emit_ack_irq(emitter, cfg.ie_mask);
    emit_hle_irq_return(emitter);
    ASSERT_LE(emitter.current_pc(), main_pc);
    while (emitter.current_pc() < main_pc) {
        emitter.emit_nop();
    }
    emit_install_irq_trampoline(emitter, cfg.irq_slot, handler_pc);
    emit_enable_interrupts(emitter, cfg.ie_mask);
}

void run_dual_ipc(EmulatorE2eHarness& harness, const DualProgramSpec& spec, TickCount ticks) {
    boot_dual_program(harness, spec);
    harness.step_ticks(ticks);
}

TEST(Tier2IpcE2e, LowByte8BitWriteToIpcSyncIsIgnored) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write8_abs(kRegIpcSync, 0x0FU);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 4000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
}

TEST(Tier2IpcE2e, HighByte8BitWriteUpdatesSyncAndTriggersRemoteIrq) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_counting_irq_stub(arm7, IrqStubConfig{kArm7IrqSlot, kIrqIpcSync, 0U});
    arm7.emit_write16_abs(kRegIpcSync, kIpcSyncIrqEnable);
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_delay_nop(arm7, 8U);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write8_abs(kRegIpcSync + 1U, 0x6BU);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm9, 2U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x400BU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x4B00U);
}

TEST(Tier2IpcE2e, RemoteSyncIrqSuppressedWhenRemoteBit14Disabled) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_enable_interrupts(arm7, kIrqIpcSync);
    arm7.emit_enable_irq(ArmReg::R0);
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_delay_nop(arm7, 8U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(arm7, 0U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(kRegIpcSync, static_cast<u16>(0x0400U | kIpcSyncIrqRemote));
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 6000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & kIrqIpcSync, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x0004U);
}

TEST(Tier2IpcE2e, ReadOnlyAndWriteOnlyIpcSyncBitsCannotBeSpoofed) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcSync, 0xFFFFU);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), {}}, 3000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x4F00U);
}

TEST(Tier2IpcE2e, RepeatedSyncIrqTriggerReFiresAfterIfAck) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_counting_irq_stub(arm7, IrqStubConfig{kArm7IrqSlot, kIrqIpcSync, 0U});
    arm7.emit_write16_abs(kRegIpcSync, kIpcSyncIrqEnable);
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_delay_nop(arm7, 16U);
    arm7.emit_branch_self();
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(kRegIpcSync, static_cast<u16>(0x0100U | kIpcSyncIrqRemote));
    emit_poll_nonzero(arm9, result_addr(0U));
    emit_delay_nop(arm9, 32U);
    arm9.emit_write16_abs(kRegIpcSync, static_cast<u16>(0x0200U | kIpcSyncIrqRemote));
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();
    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 2U);
}

TEST(Tier2IpcE2e, OverflowOn17thPushSetsErrorBitAndPreserves16Words) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 17U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0x100U + i);
    }
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write16_abs(kRegIpcFifoCnt, static_cast<u16>(kIpcFifoEnable | kIpcFifoError));
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 16U; ++i) {
        arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm7, 2U + i, ArmReg::R0);
    }
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 18U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 12000ULL);
    EXPECT_NE(harness.read_main_ram32(result_addr(0U)) & kIpcFifoError, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & kIpcFifoError, 0U);
    for (u32 i = 0U; i < 16U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(2U + i)), 0x100U + i);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(18U)) & 0x0100U, 0x0100U);
}

TEST(Tier2IpcE2e, UnderflowOnEmptyRecvSetsErrorBitAndReturnsLastWord) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0xCAFEBABEU);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_write16_abs(kRegIpcFifoCnt, static_cast<u16>(kIpcFifoEnable | kIpcFifoError));
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 4U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0xCAFEBABEU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0xCAFEBABEU);
    EXPECT_NE(harness.read_main_ram32(result_addr(2U)) & kIpcFifoError, 0U);
    EXPECT_NE(harness.read_main_ram32(result_addr(3U)) & kIpcFifoError, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)) & kIpcFifoError, 0U);
}

TEST(Tier2IpcE2e, DisabledFifoIgnoresSendsAndPeeksWithoutError) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0x12345678U);
    arm9.emit_write16_abs(kRegIpcFifoCnt, 0U);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0x99999999U);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 2U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 4U, ArmReg::R0);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 5U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & (kIpcFifoError | 0x0001U), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x12345678U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x12345678U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)) & (kIpcFifoError | 0x0100U), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x12345678U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)) & 0x0100U, 0x0100U);
}

TEST(Tier2IpcE2e, SendClearWorksEvenWhenFifoDisabled) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0xAAA1U);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0xAAA2U);
    arm9.emit_write16_abs(kRegIpcFifoCnt, 0U);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoSendClear);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 6000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & (kIpcFifoEnable | 0x0001U), 0x0001U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & 0x0100U, 0x0100U);
}

TEST(Tier2IpcE2e, FifoIrqsAreEdgeTriggeredAndSendReadRecvWriteNoOp) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_counting_irq_stub(arm7, IrqStubConfig{kArm7IrqSlot, kIrqIpcRecvNotEmpty, 0U});
    arm7.emit_write16_abs(kRegIpcFifoCnt,
                          static_cast<u16>(kIpcFifoEnable | kIpcFifoRecvNotEmptyIrq));
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_delay_nop(arm7, 16U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 2U, ArmReg::R0);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 3U, ArmReg::R0);
    arm7.emit_branch_self();

    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0x1111U);
    emit_delay_nop(arm9, 32U);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0x2222U);
    arm9.emit_read32_abs(ArmReg::R0, kRegIpcFifoSend);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_write32_abs(kRegIpcFifoRecv, 0xDEADU);
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 12000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x1111U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x2222U);
}

TEST(Tier2IpcE2e, CircularWrapAroundAcrossTwentyFourWords) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 10U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0xA000U + i);
    }
    arm9.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm9, kFlagAddr + 4U);
    for (u32 i = 10U; i < 24U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0xA000U + i);
    }
    arm9.emit_write32_abs(kFlagAddr + 8U, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 10U; ++i) {
        arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm7, i, ArmReg::R0);
    }
    arm7.emit_write32_abs(kFlagAddr + 4U, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 8U);
    for (u32 i = 10U; i < 24U; ++i) {
        arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm7, i, ArmReg::R0);
    }
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    run_dual_ipc(harness, DualProgramSpec{arm9.words(), arm7.words()}, 20000ULL);
    for (u32 i = 0U; i < 24U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(i)), 0xA000U + i);
    }
}

}  // namespace
}  // namespace elmasds::e2e::m2
