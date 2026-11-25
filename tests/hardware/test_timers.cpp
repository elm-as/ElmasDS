#include "core/scheduler/event_scheduler.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "hardware/timers/timer_controller.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <gtest/gtest.h>

namespace elmasds::hardware::timers {
namespace {

struct TimerHarness {
    core::EventScheduler scheduler;
    interrupts::InterruptController irq;
    TimerController timers;

    explicit TimerHarness(interrupts::CpuKind cpu = interrupts::CpuKind::Arm9)
        : irq(cpu), timers(cpu, scheduler, irq) {}
};

TEST(TimerControllerTest, ResetAndIdleProduceNoEventsOrIrqs) {
    TimerHarness harness;
    EXPECT_EQ(harness.scheduler.pending_count(), 0U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0U);
    EXPECT_EQ(harness.timers.read16(0x04000102U), 0U);
    EXPECT_EQ(harness.irq.if_flags(), 0U);

    // Out-of-range reads return 0 and writes are ignored.
    harness.timers.write32(0x04000110U, 0x00800000U);
    EXPECT_EQ(harness.timers.read32(0x04000110U), 0U);
    EXPECT_EQ(harness.scheduler.pending_count(), 0U);

    harness.timers.write32(0x04000100U, 0x00C0FFF0U);
    EXPECT_EQ(harness.scheduler.pending_count(), 1U);
    harness.timers.reset();
    EXPECT_EQ(harness.scheduler.pending_count(), 0U);
    EXPECT_EQ(harness.timers.read32(0x04000100U), 0U);
}

TEST(TimerControllerTest, ReloadLatchDoesNotCorruptRunningCounterAndLoadsOnStartEdge) {
    TimerHarness harness;

    // Write reload while stopped: live counter stays 0, reload becomes 0xFFF0.
    harness.timers.write16(0x04000100U, 0xFFF0U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0U);
    EXPECT_EQ(harness.timers.channel(0).reload, 0xFFF0U);

    // Start (0->1): loads counter from reload.
    harness.timers.write16(0x04000102U, 0x0080U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF0U);

    // Advance 4 timer ticks (8 master ticks): counter reaches 0xFFF4.
    harness.scheduler.advance_by(8ULL);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF4U);

    // Writing TM0CNT_L while running updates reload latch without altering live counter.
    harness.timers.write16(0x04000100U, 0x1234U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF4U);
    EXPECT_EQ(harness.timers.channel(0).reload, 0x1234U);

    // Writing TM0CNT_H with start still 1 (1->1) must not reload counter.
    harness.timers.write16(0x04000102U, 0x00C0U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF4U);
}

TEST(TimerControllerTest, Combined32BitWriteLoadsReloadBeforeStart) {
    TimerHarness harness;
    // TM0CNT_L = 0xFF80, TM0CNT_H = 0x0080 (Start, prescaler 1).
    harness.timers.write32(0x04000100U, (0x0080U << 16U) | 0xFF80U);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFF80U);
    EXPECT_EQ(harness.timers.read16(0x04000102U), 0x0080U);
    EXPECT_EQ(harness.scheduler.next_event_tick(), 0x80ULL * 2ULL);
}

TEST(TimerControllerTest, Supports8Bit16BitAnd32BitMmioAcrossAllChannels) {
    TimerHarness harness;

    // Channel 3 at 0x0400010C..0x0400010F via 8-bit writes.
    harness.timers.write8(0x0400010CU, 0x78U);
    harness.timers.write8(0x0400010DU, 0x56U);
    harness.timers.write8(0x0400010EU, 0xC7U);  // Start + IRQ + Cascade + Prescaler 1024
    harness.timers.write8(0x0400010FU, 0xFFU);  // Ignored unused upper control byte

    EXPECT_EQ(harness.timers.read8(0x0400010CU), 0x78U);
    EXPECT_EQ(harness.timers.read8(0x0400010DU), 0x56U);
    EXPECT_EQ(harness.timers.read8(0x0400010EU), 0xC7U);
    EXPECT_EQ(harness.timers.read8(0x0400010FU), 0x00U);
    EXPECT_EQ(harness.timers.read32(0x0400010CU), 0x00C75678U);
}

TEST(TimerControllerTest, PrescalersAndMidTickReadsPreserveExactPhase) {
    struct Case {
        u16 bits;
        u32 divider;
    };
    const Case cases[] = {
        {0U, 1U},
        {1U, 64U},
        {2U, 256U},
        {3U, 1024U},
    };
    for (const Case& entry : cases) {
        TimerHarness harness;
        const TickCount step_ticks = static_cast<TickCount>(entry.divider) * kArm7TicksPerCycle;
        harness.timers.write32(0x04000100U,
                               (static_cast<u32>(0x00C0U | entry.bits) << 16U) | 0xFFFDU);
        EXPECT_EQ(harness.scheduler.next_event_tick(), 3ULL * step_ticks);

        // Repeated odd-tick reads must not lose sub-increment phase.
        for (TickCount t = 0ULL; t < step_ticks; ++t) {
            EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFDU);
            harness.scheduler.advance_by(1ULL);
        }
        EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFEU);
        EXPECT_EQ(harness.irq.if_flags(), 0U);

        harness.scheduler.advance_by(2ULL * step_ticks);
        EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFDU);
        EXPECT_EQ(harness.irq.if_flags(),
                  interrupts::irq_bit_mask(interrupts::IrqSource::Timer0));
    }
}

TEST(TimerControllerTest, ControlAndPrescalerChangeWhileRunningReschedulesCleanly) {
    TimerHarness harness;
    // Start Timer 1 with reload 0xFFF0, prescaler 1 (2 master ticks/inc), IRQ disabled.
    harness.timers.write32(0x04000104U, 0x0080FFF0U);
    harness.scheduler.advance_by(10ULL);  // 5 increments -> counter = 0xFFF5
    EXPECT_EQ(harness.timers.read16(0x04000104U), 0xFFF5U);

    // Enable IRQ only (1->1 without prescaler/cascade change): keeps existing overflow tick.
    const auto original_next = harness.scheduler.next_event_tick();
    harness.timers.write16(0x04000106U, 0x00C0U);
    EXPECT_EQ(harness.scheduler.next_event_tick(), original_next);

    // Switch prescaler to /64 (128 master ticks/inc) while running: counter remains 0xFFF5,
    // remaining 11 increments now take 11 * 128 = 1408 master ticks from tick 10 -> tick 1418.
    harness.timers.write16(0x04000106U, 0x00C1U);
    EXPECT_EQ(harness.timers.read16(0x04000104U), 0xFFF5U);
    EXPECT_EQ(harness.scheduler.pending_count(), 1U);
    EXPECT_EQ(harness.scheduler.next_event_tick(), 10ULL + (11ULL * 128ULL));

    harness.scheduler.advance_to(10ULL + (11ULL * 128ULL));
    EXPECT_EQ(harness.irq.if_flags(),
              interrupts::irq_bit_mask(interrupts::IrqSource::Timer1));
}

TEST(TimerControllerTest, StopFreezesCounterAndCancelsPendingEvent) {
    TimerHarness harness;
    harness.timers.write32(0x04000100U, 0x00C0FFF0U);
    harness.scheduler.advance_by(12ULL);  // +6 ticks -> 0xFFF6
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF6U);

    // Stop timer (1->0): freezes counter at 0xFFF6 and cancels scheduled event.
    harness.timers.write16(0x04000102U, 0x0040U);
    EXPECT_EQ(harness.scheduler.pending_count(), 0U);
    harness.scheduler.advance_by(1000ULL);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFF6U);
    EXPECT_EQ(harness.irq.if_flags(), 0U);
}

TEST(TimerControllerTest, LateTicksOverflowCatchupAvoidsDriftAndPropagatesMultipleOverflows) {
    TimerHarness harness;
    // Timer 0: reload 0xFFFC (period = 4 * 2 = 8 master ticks), IRQ enabled.
    // Timer 1: cascaded from Timer 0, reload 0xFFF0, IRQ enabled.
    harness.timers.write32(0x04000104U, 0x00C4FFF0U);
    harness.timers.write32(0x04000100U, 0x00C0FFFCU);

    // Run to tick 21 (2 overflows at tick 8 and tick 16, 5 ticks into 3rd period).
    harness.scheduler.advance_to(21ULL);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFEU);  // 0xFFFC + (5 / 2)
    EXPECT_EQ(harness.timers.read16(0x04000104U), 0xFFF2U);  // 0xFFF0 + 2 overflows
    // Next overflow must remain aligned to exact 8-tick boundary (tick 24), zero drift!
    EXPECT_EQ(harness.scheduler.next_event_tick(), 24ULL);

    // Advance 3 more ticks to tick 24 -> 3rd overflow fires right on boundary.
    harness.scheduler.advance_to(24ULL);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFCU);
    EXPECT_EQ(harness.timers.read16(0x04000104U), 0xFFF3U);
    EXPECT_EQ(harness.scheduler.next_event_tick(), 32ULL);
}

TEST(TimerControllerTest, CascadeChainAndTimer0CascadeIgnored) {
    TimerHarness harness;

    // Timer 0 ignores bit 2 (Cascade) and still schedules periodic overflow.
    harness.timers.write32(0x04000100U, 0x0084FFFEU);
    EXPECT_EQ(harness.timers.read16(0x04000102U), 0x0080U);
    EXPECT_FALSE(harness.timers.channel(0).is_cascaded(0));
    EXPECT_EQ(harness.scheduler.pending_count(), 1U);

    harness.timers.reset();

    // Configure TM0 -> TM1 -> TM2 -> TM3 cascade chain:
    //   TM0: reload 0xFFFE (period = 4 master ticks), IRQ off
    //   TM1: reload 0xFFFE (overflows every 2 TM0 overflows), cascade, IRQ on
    //   TM2: reload 0xFFFF (overflows every 1 TM1 overflow), cascade, IRQ off
    //   TM3: reload 0xFFFE (overflows every 2 TM2 overflows), cascade, IRQ on
    harness.timers.write32(0x0400010CU, 0x00C4FFFEU);
    harness.timers.write32(0x04000108U, 0x0084FFFFU);
    harness.timers.write32(0x04000104U, 0x00C4FFFEU);
    harness.timers.write32(0x04000100U, 0x0080FFFEU);

    // Only TM0 schedules an event; cascaded TM1..TM3 never schedule periodic events.
    EXPECT_EQ(harness.scheduler.pending_count(), 1U);

    // 4 TM0 overflows (16 master ticks) -> 2 TM1 overflows -> 2 TM2 overflows -> 1 TM3 overflow.
    harness.scheduler.advance_to(16ULL);
    EXPECT_EQ(harness.timers.read16(0x04000100U), 0xFFFEU);
    EXPECT_EQ(harness.timers.read16(0x04000104U), 0xFFFEU);
    EXPECT_EQ(harness.timers.read16(0x04000108U), 0xFFFFU);
    EXPECT_EQ(harness.timers.read16(0x0400010CU), 0xFFFEU);

    // Only Timer1 and Timer3 had IRQ enabled (Timer0 and Timer2 had bit 6 == 0).
    EXPECT_EQ(harness.irq.if_flags(),
              interrupts::irq_bit_mask(interrupts::IrqSource::Timer1) |
                  interrupts::irq_bit_mask(interrupts::IrqSource::Timer3));
}

TEST(TimerControllerTest, DualCpuTimersShareSchedulerWithoutCancelCollision) {
    core::EventScheduler scheduler;
    interrupts::InterruptController irq9(interrupts::CpuKind::Arm9);
    interrupts::InterruptController irq7(interrupts::CpuKind::Arm7);
    TimerController timers9(interrupts::CpuKind::Arm9, scheduler, irq9);
    TimerController timers7(interrupts::CpuKind::Arm7, scheduler, irq7);

    // Start Timer 0 on both ARM9 and ARM7 with identical reload and IRQ enabled.
    timers9.write32(0x04000100U, 0x00C0FFF0U);
    timers7.write32(0x04000100U, 0x00C0FFF0U);
    EXPECT_EQ(scheduler.pending_count(), 2U);

    // Stopping or resetting ARM9 Timer 0 must NOT cancel ARM7 Timer 0.
    timers9.reset();
    EXPECT_EQ(scheduler.pending_count(), 1U);

    scheduler.advance_to(32ULL);
    EXPECT_EQ(irq9.if_flags(), 0U);
    EXPECT_EQ(irq7.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Timer0));
}

TEST(TimerControllerTest, BusIntegrationViaMmioRouter) {
    core::EventScheduler scheduler;
    interrupts::InterruptController irq9(interrupts::CpuKind::Arm9);
    interrupts::InterruptController irq7(interrupts::CpuKind::Arm7);
    TimerController timers9(interrupts::CpuKind::Arm9, scheduler, irq9);
    TimerController timers7(interrupts::CpuKind::Arm7, scheduler, irq7);

    memory::MemorySystem mem;
    mem.arm9_mmio().register_device({kTimerBaseAddress, kTimerEndAddress, &timers9});
    mem.arm7_mmio().register_device({kTimerBaseAddress, kTimerEndAddress, &timers7});

    memory::Arm9MemoryBus bus9(mem);
    memory::Arm7MemoryBus bus7(mem);

    bus9.write32(0x04000100U, 0x00C0FFF8U);  // 8 increments = 16 master ticks
    bus7.write32(0x04000104U, 0x00C0FFFCU);  // 4 increments = 8 master ticks

    scheduler.advance_to(8ULL);
    EXPECT_EQ(irq7.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Timer1));
    EXPECT_EQ(irq9.if_flags(), 0U);
    EXPECT_EQ(bus9.read16(0x04000100U), 0xFFFCU);

    scheduler.advance_to(16ULL);
    EXPECT_EQ(irq9.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Timer0));
    EXPECT_EQ(bus7.read16(0x04000104U), 0xFFFCU);
}

}  // namespace
}  // namespace elmasds::hardware::timers
