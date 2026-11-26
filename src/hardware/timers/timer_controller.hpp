#pragma once

#include "core/scheduler/event_scheduler.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "hardware/timers/timer_channel.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <array>
#include <cstddef>

namespace elmasds::hardware::timers {

/**
 * @brief Per-CPU 4-channel 16-bit hardware timer controller (`0x04000100..0x0400010F`).
 *
 * Responsibility:
 *   Implements `TM0..TM3` reload latches, live counters, prescalers (`1/64/256/1024`),
 *   count-up cascades (`TM0->TM1->TM2->TM3`), drift-free `EventScheduler` overflow scheduling,
 *   and `IrqSource::Timer0..3` dispatch via `InterruptController`.
 */
class TimerController final : public memory::MmioDevice {
public:
    /**
     * @brief Constructs a timer controller for `cpu` bound to `scheduler` and `irq`.
     */
    TimerController(interrupts::CpuKind cpu, core::EventScheduler& scheduler,
                    interrupts::InterruptController& irq) noexcept;

    TimerController(const TimerController&) = delete;
    TimerController& operator=(const TimerController&) = delete;
    TimerController(TimerController&&) = delete;
    TimerController& operator=(TimerController&&) = delete;

    /**
     * @brief Cancels all pending timer events for this CPU and zeroes all four channels.
     */
    void reset();

    /**
     * @brief Returns read-only state of channel `index` (`0..3`, clamped to `0` if out of range).
     */
    [[nodiscard]] const TimerChannel& channel(std::size_t index) const noexcept;
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept { profiler_ = profiler; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    void sync_channel(std::size_t index);
    void write_control(std::size_t index, u16 raw_value);
    void schedule_overflow(std::size_t index);
    void cancel_channel_event(std::size_t index);
    void handle_overflow(std::size_t index, TickCount late_ticks);
    void propagate_cascade(std::size_t start_index, TickCount count);

    void on_timer0_overflow(TickCount late_ticks);
    void on_timer1_overflow(TickCount late_ticks);
    void on_timer2_overflow(TickCount late_ticks);
    void on_timer3_overflow(TickCount late_ticks);

    [[nodiscard]] core::EventType channel_event_type(std::size_t index) const noexcept;
    [[nodiscard]] core::EventHandler channel_event_handler(std::size_t index) noexcept;
    [[nodiscard]] static interrupts::IrqSource channel_irq_source(std::size_t index) noexcept;

    interrupts::CpuKind cpu_;
    core::EventScheduler& scheduler_;
    interrupts::InterruptController& irq_;
    std::array<TimerChannel, kTimerChannelCount> channels_{};
    primitives::PerformanceProfiler* profiler_ = nullptr;
};

}  // namespace elmasds::hardware::timers
