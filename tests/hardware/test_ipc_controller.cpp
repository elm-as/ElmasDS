#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "hardware/ipc/ipc_controller.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <gtest/gtest.h>

namespace elmasds::hardware::ipc {
namespace {

struct IpcHarness {
    interrupts::InterruptController irq9{interrupts::CpuKind::Arm9};
    interrupts::InterruptController irq7{interrupts::CpuKind::Arm7};
    IpcController ipc{irq9, irq7};
};

TEST(IpcControllerTest, ResetAndOutOfRangeAccessesAreSafe) {
    IpcHarness h;
    // Initial IPCFIFOCNT has SendEmpty (bit 0) and RecvEmpty (bit 8) set -> 0x0101.
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt), 0x0101U);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt), 0x0101U);
    EXPECT_EQ(h.ipc.arm9_mmio().read32(0x0400018CU), 0U);
    EXPECT_EQ(h.ipc.arm9_mmio().read32(kRegIpcFifoSend), 0U);

    // Write-only IPCFIFOSEND ignores reads; read-only IPCFIFORECV ignores writes.
    h.ipc.arm9_mmio().write32(kRegIpcFifoRecv, 0x12345678U);
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0xCAFEBABEU);
    EXPECT_EQ(h.ipc.arm9_send_count(), 1U);

    h.ipc.reset();
    EXPECT_EQ(h.ipc.arm9_send_count(), 0U);
    EXPECT_EQ(h.ipc.arm7_send_count(), 0U);
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt), 0x0101U);
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcSync), 0U);
}

TEST(IpcControllerTest, IpcSyncBidirectionalHandshakeAndIrq16) {
    IpcHarness h;
    const u32 sync_irq_mask = interrupts::irq_bit_mask(interrupts::IrqSource::IpcSync);

    // Byte 0 of IPCSYNC is read-only; byte 1 sets output nibble and IRQ enable.
    h.ipc.arm7_mmio().write8(kRegIpcSync, 0x0FU);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcSync), 0U);

    h.ipc.arm7_mmio().write8(kRegIpcSync + 1U, 0x4AU);  // IRQ enable (bit 6) + nibble 0xA
    h.ipc.arm9_mmio().write16(kRegIpcSync, static_cast<u16>((1U << 13U) | (0x5U << 8U)));

    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcSync), 0x050AU);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcSync), 0x4A05U);
    EXPECT_EQ(h.irq7.if_flags(), sync_irq_mask);
    EXPECT_EQ(h.irq9.if_flags(), 0U);

    // Bit 13 reads back 0, and sending IRQ when remote bit 14 == 0 does not assert IRQ.
    h.ipc.arm7_mmio().write32(kRegIpcSync, (1U << 13U) | (0x3U << 8U));
    EXPECT_EQ(h.irq9.if_flags(), 0U);
    EXPECT_EQ(h.ipc.arm9_mmio().read8(kRegIpcSync), 0x03U);
}

TEST(IpcControllerTest, IpcFifoBidirectional16WordTransferAndGbaTekStatusBits) {
    IpcHarness h;
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);
    h.ipc.arm7_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);

    for (u32 i = 0U; i < kIpcFifoCapacity; ++i) {
        h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0xA000U + i);
    }
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & (kFifoCntSendEmpty | kFifoCntSendFull),
              kFifoCntSendFull);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt) & (kFifoCntRecvEmpty | kFifoCntRecvFull),
              kFifoCntRecvFull);

    // Pop 8, push 8 more to verify circular wrap-around across >16 entries.
    for (u32 i = 0U; i < 8U; ++i) {
        EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0xA000U + i);
    }
    for (u32 i = 16U; i < 24U; ++i) {
        h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0xA000U + i);
    }
    for (u32 i = 8U; i < 24U; ++i) {
        EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0xA000U + i);
    }
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt) & kFifoCntRecvEmpty, kFifoCntRecvEmpty);
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & kFifoCntSendEmpty, kFifoCntSendEmpty);

    // Reverse direction: ARM7 -> ARM9.
    h.ipc.arm7_mmio().write32(kRegIpcFifoSend, 0x13579BDFU);
    EXPECT_EQ(h.ipc.arm9_mmio().read32(kRegIpcFifoRecv), 0x13579BDFU);
}

TEST(IpcControllerTest, DisabledFifoIgnoresSendAndPeeksRecvWithoutError) {
    IpcHarness h;
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0x11223344U);

    // ARM7 FIFO disabled (bit 15 == 0): recv peeks without popping or setting Error bit 14.
    EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0x11223344U);
    EXPECT_EQ(h.ipc.arm9_send_count(), 1U);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // ARM7 send while disabled is ignored (no push, no error).
    h.ipc.arm7_mmio().write32(kRegIpcFifoSend, 0xDEAD0000U);
    EXPECT_EQ(h.ipc.arm7_send_count(), 0U);
    EXPECT_EQ(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // ARM9 empty recv while disabled returns 0 without error.
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, 0U);
    EXPECT_EQ(h.ipc.arm9_mmio().read32(kRegIpcFifoRecv), 0U);
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // Send Clear (bit 3) works even when FIFO is disabled.
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntSendClear);
    EXPECT_EQ(h.ipc.arm9_send_count(), 0U);
}

TEST(IpcControllerTest, OverflowUnderflowW1cErrorAndSendClearResetLastWord) {
    IpcHarness h;
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);
    h.ipc.arm7_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);

    for (u32 i = 0U; i < kIpcFifoCapacity; ++i) {
        h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0x50U + i);
    }
    // 17th push overflows -> sets bit 14 and drops word.
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0xFFFFFFFFU);
    EXPECT_NE(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // Writing 0 to bit 14 preserves Error; writing 1 clears it.
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable);
    EXPECT_NE(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable | kFifoCntError);
    EXPECT_EQ(h.ipc.arm9_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // Drain FIFO, then underflow returns last_word (0x5F) and latches Error bit 14.
    for (u32 i = 0U; i < kIpcFifoCapacity; ++i) {
        EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0x50U + i);
    }
    EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0x5FU);
    EXPECT_NE(h.ipc.arm7_mmio().read16(kRegIpcFifoCnt) & kFifoCntError, 0);

    // Send Clear on sender resets last_word back to 0.
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable | kFifoCntSendClear);
    EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0U);
}

TEST(IpcControllerTest, EdgeTriggeredFifoIrqs17And18) {
    IpcHarness h;
    const u32 send_empty_mask = interrupts::irq_bit_mask(interrupts::IrqSource::IpcSendEmpty);
    const u32 recv_not_empty_mask =
        interrupts::irq_bit_mask(interrupts::IrqSource::IpcRecvNotEmpty);

    // Enabling SendEmptyIrq while Send FIFO already empty fires IRQ 17 once.
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable | kFifoCntSendEmptyIrq);
    EXPECT_EQ(h.irq9.if_flags(), send_empty_mask);
    h.irq9.write32(interrupts::InterruptController::kRegIf, send_empty_mask);
    EXPECT_EQ(h.irq9.if_flags(), 0U);

    // Enable RecvNotEmptyIrq on ARM7 while empty: no IRQ yet.
    h.ipc.arm7_mmio().write16(kRegIpcFifoCnt, kFifoCntEnable | kFifoCntRecvNotEmptyIrq);
    EXPECT_EQ(h.irq7.if_flags(), 0U);

    // Pushing 1st word fires IRQ 18; 2nd word does NOT re-trigger.
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0x1111U);
    EXPECT_EQ(h.irq7.if_flags(), recv_not_empty_mask);
    h.irq7.write32(interrupts::InterruptController::kRegIf, recv_not_empty_mask);
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0x2222U);
    EXPECT_EQ(h.irq7.if_flags(), 0U);

    // Popping 1st word does not fire SendEmpty; popping last word fires IRQ 17.
    EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0x1111U);
    EXPECT_EQ(h.irq9.if_flags(), 0U);
    EXPECT_EQ(h.ipc.arm7_mmio().read32(kRegIpcFifoRecv), 0x2222U);
    EXPECT_EQ(h.irq9.if_flags(), send_empty_mask);

    // Send Clear while SendEmptyIrq already enabled transitions non-empty -> empty -> IRQ 17.
    h.irq9.write32(interrupts::InterruptController::kRegIf, send_empty_mask);
    h.ipc.arm9_mmio().write32(kRegIpcFifoSend, 0x3333U);
    h.ipc.arm9_mmio().write16(kRegIpcFifoCnt,
                              kFifoCntEnable | kFifoCntSendEmptyIrq | kFifoCntSendClear);
    EXPECT_EQ(h.irq9.if_flags(), send_empty_mask);
}

TEST(IpcControllerTest, MemoryBusIntegrationArm9AndArm7ViaMmioRouter) {
    memory::MemorySystem mem;
    memory::Arm9MemoryBus bus9(mem);
    memory::Arm7MemoryBus bus7(mem);
    interrupts::InterruptController irq9(interrupts::CpuKind::Arm9);
    interrupts::InterruptController irq7(interrupts::CpuKind::Arm7);
    IpcController ipc(irq9, irq7);
    ipc.register_mmio(mem.arm9_mmio(), mem.arm7_mmio());

    bus9.write16(kRegIpcFifoCnt, kFifoCntEnable);
    bus7.write16(kRegIpcFifoCnt, kFifoCntEnable | kFifoCntRecvNotEmptyIrq);
    bus9.write16(kRegIpcSync, static_cast<u16>(0x7U << 8U));
    EXPECT_EQ(bus7.read16(kRegIpcSync) & 0x0FU, 0x7U);

    bus9.write32(kRegIpcFifoSend, 0x89ABCDEFU);
    EXPECT_EQ(irq7.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::IpcRecvNotEmpty));
    EXPECT_EQ(bus7.read8(kRegIpcFifoRecv + 1U), 0xCDU);
    EXPECT_EQ(bus7.read16(kRegIpcFifoCnt) & kFifoCntRecvEmpty, kFifoCntRecvEmpty);
}

}  // namespace
}  // namespace elmasds::hardware::ipc
