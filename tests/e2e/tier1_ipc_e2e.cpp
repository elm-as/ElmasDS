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
    u32 result_slot = 0U;
};

void emit_sync_irq_handler_stub(ArmProgramEmitter& emitter, const IrqStubConfig& cfg) {
    const Address main_pc = emitter.base_address() + 128U;
    emitter.emit_b(main_pc);
    const Address handler_pc = emitter.current_pc();
    emitter.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(emitter, cfg.result_slot, ArmReg::R0);
    emit_read16_abs(emitter, ArmReg::R0, kRegIpcSync);
    emit_store_result(emitter, cfg.result_slot + 1U, ArmReg::R0);
    emit_ack_irq(emitter, cfg.ie_mask);
    emit_hle_irq_return(emitter);
    ASSERT_LE(emitter.current_pc(), main_pc);
    while (emitter.current_pc() < main_pc) {
        emitter.emit_nop();
    }
    emit_install_irq_trampoline(emitter, cfg.irq_slot, handler_pc);
    emit_enable_interrupts(emitter, cfg.ie_mask);
}

TEST(Tier1IpcE2e, Arm9ToArm7SyncDataVisibleInLowerNibble) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcSync, 0x0A00U);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm7, 0U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(4000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x0FU, 0x0AU);
}

TEST(Tier1IpcE2e, Arm7ToArm9SyncDataVisibleInLowerNibble) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(kRegIpcSync, 0x0500U);
    arm7.emit_write32_abs(kFlagAddr, 1U);
    arm7.emit_branch_self();

    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(4000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x0FU, 0x05U);
}

TEST(Tier1IpcE2e, BidirectionalSyncSimultaneousNibbleExchange) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcSync, 0x0C00U);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm9, kFlagAddr + 4U);
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(kRegIpcSync, 0x0300U);
    arm7.emit_write32_abs(kFlagAddr + 4U, 1U);
    emit_poll_nonzero(arm7, kFlagAddr);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcSync);
    emit_store_result(arm7, 1U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(6000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x0C03U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x030CU);
}

TEST(Tier1IpcE2e, Arm9TriggerSyncIrqWakesArm7Handler) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_sync_irq_handler_stub(arm7, IrqStubConfig{kArm7IrqSlot, kIrqIpcSync, 0U});
    arm7.emit_write16_abs(kRegIpcSync, kIpcSyncIrqEnable);
    arm7.emit_write32_abs(kFlagAddr, 1U);
    arm7.emit_write8_abs(kRegHaltcnt, kHaltcntHalt);
    emit_store_const_result(arm7, 2U, 1U);
    arm7.emit_branch_self();

    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    emit_delay_nop(arm9, 16U);
    arm9.emit_write16_abs(kRegIpcSync, static_cast<u16>(0x0700U | kIpcSyncIrqRemote));
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqIpcSync);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x4007U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 1U);
    EXPECT_FALSE(harness.is_arm7_halted());
}

TEST(Tier1IpcE2e, Arm7TriggerSyncIrqWakesArm9WfiHandler) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_sync_irq_handler_stub(arm9, IrqStubConfig{kArm9IrqSlot, kIrqIpcSync, 0U});
    arm9.emit_write16_abs(kRegIpcSync, kIpcSyncIrqEnable);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_mcr_wfi();
    emit_store_const_result(arm9, 2U, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    emit_delay_nop(arm7, 16U);
    arm7.emit_write16_abs(kRegIpcSync, static_cast<u16>(0x0900U | kIpcSyncIrqRemote));
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqIpcSync);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x4009U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 1U);
    EXPECT_FALSE(harness.is_arm9_halted());
}

TEST(Tier1IpcE2e, Arm9ToArm7MultiWordFifoTransfer) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 4U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0x11110001U + i);
    }
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 1U, ArmReg::R0);
    for (u32 i = 0U; i < 4U; ++i) {
        arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm7, 2U + i, ArmReg::R0);
    }
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 6U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(8000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x03U, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & 0x0300U, 0U);
    for (u32 i = 0U; i < 4U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(2U + i)), 0x11110001U + i);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(6U)) & 0x0100U, 0x0100U);
}

TEST(Tier1IpcE2e, Arm7ToArm9MultiWordFifoTransfer) {
    ArmProgramEmitter arm7(kArm7DefaultEntry);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 4U; ++i) {
        arm7.emit_write32_abs(kRegIpcFifoSend, 0x22220001U + i);
    }
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 5U, ArmReg::R0);
    arm7.emit_branch_self();

    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 4U; ++i) {
        arm9.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm9, i, ArmReg::R0);
    }
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 4U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(8000ULL);
    for (u32 i = 0U; i < 4U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(i)), 0x22220001U + i);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)) & 0x0100U, 0x0100U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)) & 0x0001U, 0x0001U);
}

TEST(Tier1IpcE2e, FillSixteenWordFifoSetsFullFlagsAndDrainsCleanly) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 16U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0x5000U + i);
    }
    emit_read16_abs(arm9, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_write32_abs(kFlagAddr, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_poll_nonzero(arm7, kFlagAddr);
    arm7.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 1U, ArmReg::R0);
    for (u32 i = 0U; i < 16U; ++i) {
        arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
        emit_store_result(arm7, 2U + i, ArmReg::R0);
    }
    emit_read16_abs(arm7, ArmReg::R0, kRegIpcFifoCnt);
    emit_store_result(arm7, 18U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(12000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x0003U, 0x0002U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & 0x0300U, 0x0200U);
    for (u32 i = 0U; i < 16U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(2U + i)), 0x5000U + i);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(18U)) & 0x0300U, 0x0100U);
}

TEST(Tier1IpcE2e, SendFifoClearEmptiesQueuedWords) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    for (u32 i = 0U; i < 5U; ++i) {
        arm9.emit_write32_abs(kRegIpcFifoSend, 0x9000U + i);
    }
    arm9.emit_write16_abs(kRegIpcFifoCnt, static_cast<u16>(kIpcFifoEnable | kIpcFifoSendClear));
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
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(6000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)) & 0x0003U, 0x0001U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & 0x0300U, 0x0100U);
}

TEST(Tier1IpcE2e, FifoSendEmptyAndRecvNotEmptyIrqsFire) {
    ArmProgramEmitter arm9(kArm9DefaultEntry);
    emit_sync_irq_handler_stub(arm9, IrqStubConfig{kArm9IrqSlot, kIrqIpcSendEmpty, 0U});
    emit_poll_nonzero(arm9, kFlagAddr);
    arm9.emit_write16_abs(kRegIpcFifoCnt, kIpcFifoEnable);
    arm9.emit_write32_abs(kRegIpcFifoSend, 0xCAFE0001U);
    arm9.emit_write16_abs(kRegIpcFifoCnt,
                          static_cast<u16>(kIpcFifoEnable | kIpcFifoSendEmptyIrq));
    arm9.emit_write32_abs(kFlagAddr + 4U, 1U);
    arm9.emit_branch_self();

    ArmProgramEmitter arm7(kArm7DefaultEntry);
    emit_sync_irq_handler_stub(arm7, IrqStubConfig{kArm7IrqSlot, kIrqIpcRecvNotEmpty, 2U});
    arm7.emit_write16_abs(kRegIpcFifoCnt,
                          static_cast<u16>(kIpcFifoEnable | kIpcFifoRecvNotEmptyIrq));
    arm7.emit_write32_abs(kFlagAddr, 1U);
    emit_poll_nonzero(arm7, kFlagAddr + 4U);
    emit_delay_nop(arm7, 8U);
    arm7.emit_read32_abs(ArmReg::R0, kRegIpcFifoRecv);
    emit_store_result(arm7, 4U, ArmReg::R0);
    arm7.emit_branch_self();

    EmulatorE2eHarness harness;
    boot_dual_program(harness, DualProgramSpec{arm9.words(), arm7.words()});
    harness.step_ticks(12000ULL);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), kIrqIpcSendEmpty);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), kIrqIpcRecvNotEmpty);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0xCAFE0001U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
