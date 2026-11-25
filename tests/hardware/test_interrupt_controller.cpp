#include "cpu/arm7/arm7_tdmi.hpp"
#include "cpu/arm9/arm946_es.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "hardware/interrupts/system_control_mmio.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace elmasds::hardware::interrupts {
namespace {

struct RecordedTarget {
    bool irq_line = false;
    bool wake_pending = false;
    int irq_calls = 0;
    int wake_calls = 0;

    [[nodiscard]] InterruptTarget as_target() noexcept {
        return InterruptTarget{
            this,
            [](void* ctx, bool asserted) {
                auto* self = static_cast<RecordedTarget*>(ctx);
                self->irq_line = asserted;
                ++self->irq_calls;
            },
            [](void* ctx, bool pending) {
                auto* self = static_cast<RecordedTarget*>(ctx);
                self->wake_pending = pending;
                ++self->wake_calls;
            },
        };
    }
};

struct RecordedHalt {
    std::vector<PowerDownMode> modes;

    [[nodiscard]] HaltHandler as_handler() noexcept {
        return HaltHandler{
            this,
            [](void* ctx, PowerDownMode mode) {
                static_cast<RecordedHalt*>(ctx)->modes.push_back(mode);
            },
        };
    }
};

TEST(InterruptControllerTest, ImeRegisterMasksUpperBitsAndGatesIrqLineOnly) {
    RecordedTarget target;
    InterruptController ic(CpuKind::Arm9, target.as_target());

    ic.write32(0x04000210U, irq_bit_mask(IrqSource::Timer0));
    ic.request_irq(IrqSource::Timer0);

    // IME == 0: wake is pending, but IRQ line must stay deasserted.
    EXPECT_EQ(ic.read32(0x04000208U), 0U);
    EXPECT_FALSE(ic.is_irq_asserted());
    EXPECT_TRUE(ic.has_pending_wake());
    EXPECT_FALSE(target.irq_line);
    EXPECT_TRUE(target.wake_pending);

    // Writing upper bits of IME leaves bit 0 clear.
    ic.write32(0x04000208U, 0xFFFFFFFEU);
    EXPECT_EQ(ic.read32(0x04000208U), 0U);
    EXPECT_FALSE(ic.is_irq_asserted());

    // Unused gap 0x0400020C..0x0400020F always reads 0 and ignores writes.
    ic.write32(0x0400020CU, 0xFFFFFFFFU);
    EXPECT_EQ(ic.read32(0x0400020CU), 0U);

    // Enable IME bit 0 via 16-bit write with upper bits set.
    ic.write16(0x04000208U, 0xFF01U);
    EXPECT_EQ(ic.read16(0x04000208U), 1U);
    EXPECT_EQ(ic.read8(0x04000208U), 1U);
    EXPECT_EQ(ic.read8(0x04000209U), 0U);
    EXPECT_TRUE(ic.is_irq_asserted());
    EXPECT_TRUE(target.irq_line);

    // Disabling IME via 8-bit write drops IRQ line while keeping wake pending.
    ic.write8(0x04000208U, 0U);
    EXPECT_FALSE(ic.is_irq_asserted());
    EXPECT_TRUE(ic.has_pending_wake());
    EXPECT_FALSE(target.irq_line);
    EXPECT_TRUE(target.wake_pending);
}

TEST(InterruptControllerTest, IeAndIfWriteOneToClearSemantics) {
    RecordedTarget target;
    InterruptController ic(CpuKind::Arm7, target.as_target());

    ic.write16(0x04000210U, 0x0018U);  // Timer0 (3) + Timer1 (4)
    ic.write8(0x04000212U, 0x81U);     // IpcSync (16) + Spi (23)
    EXPECT_EQ(ic.read32(0x04000210U),
              irq_bit_mask(IrqSource::Timer0) | irq_bit_mask(IrqSource::Timer1) |
                  irq_bit_mask(IrqSource::IpcSync) | irq_bit_mask(IrqSource::Spi));
    ic.write32(0x04000208U, 1U);

    ic.request_irq(IrqSource::Timer0);
    ic.request_irq(IrqSource::Timer1);
    ic.request_irq(IrqSource::IpcSync);
    ic.request_irq(IrqSource::Spi);
    EXPECT_TRUE(ic.is_irq_asserted());

    // Writing 0 to IF leaves flags unchanged; writing 1 to unset bit (VBlank) does not set it.
    ic.write32(0x04000214U, irq_bit_mask(IrqSource::VBlank));
    EXPECT_EQ(ic.if_flags(),
              irq_bit_mask(IrqSource::Timer0) | irq_bit_mask(IrqSource::Timer1) |
                  irq_bit_mask(IrqSource::IpcSync) | irq_bit_mask(IrqSource::Spi));

    // Byte W1C on IF low byte clears only Timer0.
    ic.write8(0x04000214U, static_cast<u8>(irq_bit_mask(IrqSource::Timer0)));
    EXPECT_EQ(ic.if_flags(),
              irq_bit_mask(IrqSource::Timer1) | irq_bit_mask(IrqSource::IpcSync) |
                  irq_bit_mask(IrqSource::Spi));
    EXPECT_TRUE(ic.is_irq_asserted());

    // Halfword W1C on IF high halfword clears IpcSync and Spi.
    ic.write16(0x04000216U, 0x0081U);
    EXPECT_EQ(ic.read32(0x04000214U), irq_bit_mask(IrqSource::Timer1));
    EXPECT_TRUE(ic.is_irq_asserted());

    // Clearing final pending flag deasserts both IRQ line and wake_pending.
    ic.write32(0x04000214U, irq_bit_mask(IrqSource::Timer1));
    EXPECT_EQ(ic.if_flags(), 0U);
    EXPECT_FALSE(ic.is_irq_asserted());
    EXPECT_FALSE(ic.has_pending_wake());
    EXPECT_FALSE(target.irq_line);
    EXPECT_FALSE(target.wake_pending);

    // Out-of-range reads return 0 and writes are ignored.
    ic.write32(0x04000200U, 0xFFFFFFFFU);
    EXPECT_EQ(ic.read32(0x04000200U), 0U);
}

TEST(InterruptControllerTest, Arm9AndArm7IrqBitTablesDifferByCpu) {
    InterruptController ic9(CpuKind::Arm9);
    InterruptController ic7(CpuKind::Arm7);

    // Writing all 1s to IE masks strictly to per-CPU valid IRQ bits.
    ic9.write32(0x04000210U, 0xFFFFFFFFU);
    ic7.write32(0x04000210U, 0xFFFFFFFFU);
    EXPECT_EQ(ic9.ie(), kArm9ValidIrqMask);
    EXPECT_EQ(ic7.ie(), kArm7ValidIrqMask);

    // ARM9 accepts GeometryFifo (21) and rejects ARM7-only Rtc (7), ScreensUnfold (22), Spi (23),
    // Wifi (24).
    ic9.request_irq(IrqSource::GeometryFifo);
    ic9.request_irq(IrqSource::Rtc);
    ic9.request_irq(IrqSource::ScreensUnfold);
    ic9.request_irq(IrqSource::Spi);
    ic9.request_irq(IrqSource::Wifi);
    EXPECT_EQ(ic9.if_flags(), irq_bit_mask(IrqSource::GeometryFifo));

    // ARM7 accepts Rtc/ScreensUnfold/Spi/Wifi and rejects ARM9-only GeometryFifo (21).
    ic7.request_irq(IrqSource::GeometryFifo);
    ic7.request_irq(IrqSource::Rtc);
    ic7.request_irq(IrqSource::ScreensUnfold);
    ic7.request_irq(IrqSource::Spi);
    ic7.request_irq(IrqSource::Wifi);
    EXPECT_EQ(ic7.if_flags(), irq_bit_mask(IrqSource::Rtc) |
                                  irq_bit_mask(IrqSource::ScreensUnfold) |
                                  irq_bit_mask(IrqSource::Spi) | irq_bit_mask(IrqSource::Wifi));

    // Reset clears IME, IE, IF.
    ic9.reset();
    EXPECT_EQ(ic9.ime(), 0U);
    EXPECT_EQ(ic9.ie(), 0U);
    EXPECT_EQ(ic9.if_flags(), 0U);
}

TEST(SystemControlMmioTest, PostflgWriteOnceAndArm7HaltcntBehavior) {
    RecordedHalt halt9;
    RecordedHalt halt7;
    SystemControlMmio sys9(CpuKind::Arm9, halt9.as_handler());
    SystemControlMmio sys7(CpuKind::Arm7, halt7.as_handler());

    // POSTFLG bit 0 latches 1 and ignores 1->0 writes until reset; ARM9 also has R/W bit 1.
    sys9.write8(0x04000300U, 0xFFU);
    EXPECT_EQ(sys9.read8(0x04000300U), 0x03U);
    sys9.write8(0x04000300U, 0x00U);
    EXPECT_EQ(sys9.read8(0x04000300U), 0x01U);

    // ARM7 POSTFLG only exposes bit 0.
    sys7.write8(0x04000300U, 0xFFU);
    EXPECT_EQ(sys7.read16(0x04000300U), 0x0001U);
    sys7.write8(0x04000300U, 0x00U);
    EXPECT_EQ(sys7.read32(0x04000300U), 0x00000001U);

    // HALTCNT is no-op on ARM9, and triggers Halt/Sleep on ARM7.
    sys9.write8(0x04000301U, 0x80U);
    EXPECT_TRUE(halt9.modes.empty());

    sys7.write8(0x04000301U, 0x40U);  // GbaMode -> no halt callback
    EXPECT_EQ(sys7.last_power_down_mode(), PowerDownMode::GbaMode);
    EXPECT_TRUE(halt7.modes.empty());

    sys7.write8(0x04000301U, 0x80U);  // Halt
    sys7.write16(0x04000300U, 0xC000U);  // Combined write: POSTFLG stays 1, HALTCNT = Sleep
    EXPECT_EQ(sys7.post_flag(), 1U);
    EXPECT_EQ(sys7.read8(0x04000301U), 0U);
    ASSERT_EQ(halt7.modes.size(), 2U);
    EXPECT_EQ(halt7.modes[0], PowerDownMode::Halt);
    EXPECT_EQ(halt7.modes[1], PowerDownMode::Sleep);

    sys7.reset();
    EXPECT_EQ(sys7.post_flag(), 0U);
    EXPECT_EQ(sys7.last_power_down_mode(), PowerDownMode::None);
}

TEST(CpuInterruptIntegrationTest, Arm7HaltcntAndArm9WfiWakeWhenImeIsZeroOrOne) {
    memory::MemorySystem mem;
    memory::Arm9MemoryBus bus9(mem);
    memory::Arm7MemoryBus bus7(mem);
    cpu::Arm946Es arm9(bus9);
    cpu::Arm7Tdmi arm7(bus7);

    InterruptController ic9(CpuKind::Arm9, make_interrupt_target(&arm9));
    InterruptController ic7(CpuKind::Arm7, make_interrupt_target(&arm7));
    SystemControlMmio sys9(CpuKind::Arm9);
    SystemControlMmio sys7(CpuKind::Arm7, make_halt_handler(&arm7));

    mem.arm9_mmio().register_device({0x04000208U, 0x04000217U, &ic9});
    mem.arm9_mmio().register_device({0x04000300U, 0x04000301U, &sys9});
    mem.arm7_mmio().register_device({0x04000208U, 0x04000217U, &ic7});
    mem.arm7_mmio().register_device({0x04000300U, 0x04000301U, &sys7});

    arm9.reset(0x02000000U, true);
    arm7.reset(0x03800000U);

    // Program ARM7 at 0x03800000:
    //   ADD R0, R0, #1   (0xE2800001)
    //   ADD R0, R0, #2   (0xE2800002)
    bus7.write32(0x03800000U, 0xE2800001U);
    bus7.write32(0x03800004U, 0xE2800002U);
    bus7.write32(0x00000018U, 0xE25EF004U);  // SUBS PC, LR, #4

    // ARM7 enables Timer0 in IE while IME == 0, then halts via HALTCNT (0x80).
    bus7.write32(0x04000208U, 0U);
    bus7.write32(0x04000210U, irq_bit_mask(IrqSource::Timer0));
    bus7.write8(0x04000301U, 0x80U);
    EXPECT_TRUE(arm7.is_halted());
    EXPECT_EQ(arm7.run_cycles(3U), 3U);
    EXPECT_TRUE(arm7.is_halted());
    EXPECT_EQ(arm7.registers().read(cpu::RegisterIndex::Pc), 0x03800000U);

    // Timer0 IRQ wakes ARM7 even though IME == 0, resuming inline without vectoring to 0x18.
    ic7.request_irq(IrqSource::Timer0);
    EXPECT_EQ(arm7.step(), 1U);
    EXPECT_FALSE(arm7.is_halted());
    EXPECT_EQ(arm7.registers().read(cpu::RegisterIndex::R0), 1U);
    EXPECT_EQ(arm7.registers().read(cpu::RegisterIndex::Pc), 0x03800004U);

    // Writing HALTCNT=0x80 while (IE & IF) != 0 does not latch halt.
    bus7.write8(0x04000301U, 0x80U);
    EXPECT_FALSE(arm7.is_halted());

    // Acknowledge IF, enable IME and CPSR.I=0, halt again, then wake into 0x18 vector.
    bus7.write32(0x04000214U, irq_bit_mask(IrqSource::Timer0));
    bus7.write32(0x04000208U, 1U);
    arm7.registers().set_cpsr(arm7.registers().cpsr().with_irq_disabled(false));
    bus7.write8(0x04000301U, 0x80U);
    EXPECT_TRUE(arm7.is_halted());
    ic7.request_irq(IrqSource::Timer0);
    EXPECT_EQ(arm7.step(), 3U);
    EXPECT_FALSE(arm7.is_halted());
    EXPECT_EQ(arm7.registers().read(cpu::RegisterIndex::Pc), 0x00000018U);

    // ARM9 WFI wakes when (IE & IF) != 0 even with IME == 0.
    bus9.write32(0x02000000U, 0xEE070F90U);  // MCR p15, 0, R0, c7, c0, 4 (WFI)
    bus9.write32(0x02000004U, 0xE2800005U);  // ADD R0, R0, #5
    bus9.write32(0x04000208U, 0U);
    bus9.write32(0x04000210U, irq_bit_mask(IrqSource::VBlank));
    EXPECT_EQ(arm9.step(), 1U);
    EXPECT_TRUE(arm9.is_halted());
    EXPECT_EQ(arm9.step(), 1U);
    EXPECT_TRUE(arm9.is_halted());

    ic9.request_irq(IrqSource::VBlank);
    EXPECT_EQ(arm9.step(), 1U);
    EXPECT_FALSE(arm9.is_halted());
    EXPECT_EQ(arm9.registers().read(cpu::RegisterIndex::R0), 5U);
}

}  // namespace
}  // namespace elmasds::hardware::interrupts
