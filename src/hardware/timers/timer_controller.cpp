#include "hardware/timers/timer_controller.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::hardware::timers {

TimerController::TimerController(interrupts::CpuKind cpu, core::EventScheduler& scheduler,
                                 interrupts::InterruptController& irq) noexcept
    : cpu_(cpu), scheduler_(scheduler), irq_(irq) {}

void TimerController::reset() {
    for (std::size_t index = 0U; index < kTimerChannelCount; ++index) {
        cancel_channel_event(index);
        channels_[index] = TimerChannel{};
    }
}

const TimerChannel& TimerController::channel(std::size_t index) const noexcept {
    if (index >= kTimerChannelCount) {
        return channels_[0];
    }
    return channels_[index];
}

u8 TimerController::read8(Address address) {
    if (address < kTimerBaseAddress || address > kTimerEndAddress) {
        return 0U;
    }
    const Address offset = address - kTimerBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset >> 2U);
    const Address reg = offset & 3U;
    if (reg == 0U || reg == 1U) {
        sync_channel(index);
        const u16 counter = channels_[index].counter;
        return static_cast<u8>((reg == 0U) ? (counter & 0xFFU) : ((counter >> 8U) & 0xFFU));
    }
    if (reg == 2U) {
        return static_cast<u8>(channels_[index].control & 0xFFU);
    }
    return 0U;
}

u16 TimerController::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned < kTimerBaseAddress || aligned > kTimerEndAddress) {
        return 0U;
    }
    const Address offset = aligned - kTimerBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset >> 2U);
    if ((offset & 3U) == 0U) {
        sync_channel(index);
        return channels_[index].counter;
    }
    return channels_[index].control;
}

u32 TimerController::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned < kTimerBaseAddress || aligned > kTimerEndAddress) {
        return 0U;
    }
    const std::size_t index = static_cast<std::size_t>((aligned - kTimerBaseAddress) >> 2U);
    sync_channel(index);
    return (static_cast<u32>(channels_[index].control) << 16U) |
           static_cast<u32>(channels_[index].counter);
}

void TimerController::write8(Address address, u8 value) {
    if (address < kTimerBaseAddress || address > kTimerEndAddress) {
        return;
    }
    const Address offset = address - kTimerBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset >> 2U);
    const Address reg = offset & 3U;
    switch (reg) {
        case 0U:
            channels_[index].reload =
                static_cast<u16>((channels_[index].reload & 0xFF00U) | value);
            break;
        case 1U:
            channels_[index].reload =
                static_cast<u16>((channels_[index].reload & 0x00FFU) |
                                 (static_cast<u16>(value) << 8U));
            break;
        case 2U:
            write_control(index, static_cast<u16>(value));
            break;
        default:
            break;
    }
}

void TimerController::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned < kTimerBaseAddress || aligned > kTimerEndAddress) {
        return;
    }
    const Address offset = aligned - kTimerBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset >> 2U);
    if ((offset & 3U) == 0U) {
        channels_[index].reload = value;
        return;
    }
    write_control(index, value);
}

void TimerController::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned < kTimerBaseAddress || aligned > kTimerEndAddress) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>((aligned - kTimerBaseAddress) >> 2U);
    // Apply reload before control so a combined write with start 0->1 loads the new reload value.
    channels_[index].reload = static_cast<u16>(value & 0xFFFFU);
    write_control(index, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

void TimerController::sync_channel(std::size_t index) {
    TimerChannel& ch = channels_[index];
    if (!ch.is_running() || ch.is_cascaded(index)) {
        return;
    }
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Timers);
    const TickCount now = scheduler_.current_tick();
    if (now <= ch.last_sync_tick) {
        return;
    }
    if (now >= ch.next_overflow_tick) {
        const TickCount late_ticks = now - ch.next_overflow_tick;
        cancel_channel_event(index);
        handle_overflow(index, late_ticks);
        return;
    }
    const TickCount step = ch.step_ticks();
    const TickCount increments = (now - ch.last_sync_tick) / step;
    if (increments > 0ULL) {
        ch.counter = static_cast<u16>(static_cast<TickCount>(ch.counter) + increments);
        ch.last_sync_tick += increments * step;
    }
}

void TimerController::write_control(std::size_t index, u16 raw_value) {
    TimerChannel& ch = channels_[index];
    const u16 mask = (index == 0U) ? kTimer0ControlMask : kTimerNControlMask;
    const u16 new_control = static_cast<u16>(raw_value & mask);
    const bool was_running = ch.is_running();
    const bool now_running = bits::test_bit(new_control, 7U);

    if (!was_running && !now_running) {
        ch.control = new_control;
        return;
    }
    if (!was_running && now_running) {
        ch.control = new_control;
        ch.counter = ch.reload;
        ch.last_sync_tick = scheduler_.current_tick();
        if (!ch.is_cascaded(index)) {
            schedule_overflow(index);
        }
        return;
    }
    if (was_running && !now_running) {
        sync_channel(index);
        cancel_channel_event(index);
        ch.control = new_control;
        return;
    }
    // Running -> Running (1 -> 1): preserve live counter and reschedule only if timing changed.
    const bool timing_changed = ((ch.control ^ new_control) & 0x0007U) != 0U;
    if (!timing_changed) {
        ch.control = new_control;
        return;
    }
    sync_channel(index);
    cancel_channel_event(index);
    ch.control = new_control;
    ch.last_sync_tick = scheduler_.current_tick();
    if (!ch.is_cascaded(index)) {
        schedule_overflow(index);
    }
}

void TimerController::schedule_overflow(std::size_t index) {
    TimerChannel& ch = channels_[index];
    const TickCount now = scheduler_.current_tick();
    const TickCount ticks_until_overflow =
        (kTimerCounterMax - static_cast<TickCount>(ch.counter)) * ch.step_ticks();
    const TickCount elapsed_in_step = (now > ch.last_sync_tick) ? (now - ch.last_sync_tick) : 0ULL;
    const TickCount delay = (ticks_until_overflow > elapsed_in_step)
                                ? (ticks_until_overflow - elapsed_in_step)
                                : 1ULL;
    ch.next_overflow_tick = now + delay;
    scheduler_.schedule(core::ScheduleRequest{
        channel_event_type(index),
        delay,
        channel_event_handler(index),
    });
}

void TimerController::cancel_channel_event(std::size_t index) {
    scheduler_.cancel_by_type(channel_event_type(index));
}

void TimerController::handle_overflow(std::size_t index, TickCount late_ticks) {
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Timers);
    TimerChannel& ch = channels_[index];
    if (!ch.is_running() || ch.is_cascaded(index)) {
        return;
    }
    const TickCount period = ch.period_ticks();
    const TickCount step = ch.step_ticks();
    const TickCount overflows = 1ULL + (late_ticks / period);
    const TickCount into_new_period = late_ticks % period;
    const TickCount now = scheduler_.current_tick();

    ch.counter = static_cast<u16>(static_cast<TickCount>(ch.reload) + (into_new_period / step));
    ch.last_sync_tick = now - (into_new_period % step);

    if (ch.is_irq_enabled()) {
        irq_.request_irq(channel_irq_source(index));
    }
    propagate_cascade(index + 1U, overflows);
    if (ch.is_running() && !ch.is_cascaded(index)) {
        schedule_overflow(index);
    }
}

void TimerController::propagate_cascade(std::size_t start_index, TickCount count) {
    std::size_t index = start_index;
    TickCount remaining = count;
    while (index < kTimerChannelCount && remaining > 0ULL) {
        TimerChannel& ch = channels_[index];
        if (!ch.is_running() || !ch.is_cascaded(index)) {
            break;
        }
        const TickCount period = kTimerCounterMax - static_cast<TickCount>(ch.reload);
        const TickCount to_overflow = kTimerCounterMax - static_cast<TickCount>(ch.counter);
        if (remaining < to_overflow) {
            ch.counter = static_cast<u16>(static_cast<TickCount>(ch.counter) + remaining);
            break;
        }
        const TickCount after_first = remaining - to_overflow;
        const TickCount overflows = 1ULL + (after_first / period);
        ch.counter = static_cast<u16>(static_cast<TickCount>(ch.reload) + (after_first % period));
        if (ch.is_irq_enabled()) {
            irq_.request_irq(channel_irq_source(index));
        }
        remaining = overflows;
        ++index;
    }
}

void TimerController::on_timer0_overflow(TickCount late_ticks) {
    handle_overflow(0U, late_ticks);
}

void TimerController::on_timer1_overflow(TickCount late_ticks) {
    handle_overflow(1U, late_ticks);
}

void TimerController::on_timer2_overflow(TickCount late_ticks) {
    handle_overflow(2U, late_ticks);
}

void TimerController::on_timer3_overflow(TickCount late_ticks) {
    handle_overflow(3U, late_ticks);
}

core::EventType TimerController::channel_event_type(std::size_t index) const noexcept {
    if (cpu_ == interrupts::CpuKind::Arm9) {
        switch (index) {
            case 0U:
                return core::EventType::Arm9Timer0Overflow;
            case 1U:
                return core::EventType::Arm9Timer1Overflow;
            case 2U:
                return core::EventType::Arm9Timer2Overflow;
            default:
                return core::EventType::Arm9Timer3Overflow;
        }
    }
    switch (index) {
        case 0U:
            return core::EventType::Arm7Timer0Overflow;
        case 1U:
            return core::EventType::Arm7Timer1Overflow;
        case 2U:
            return core::EventType::Arm7Timer2Overflow;
        default:
            return core::EventType::Arm7Timer3Overflow;
    }
}

core::EventHandler TimerController::channel_event_handler(std::size_t index) noexcept {
    switch (index) {
        case 0U:
            return core::bind_event_handler<TimerController, &TimerController::on_timer0_overflow>(
                this);
        case 1U:
            return core::bind_event_handler<TimerController, &TimerController::on_timer1_overflow>(
                this);
        case 2U:
            return core::bind_event_handler<TimerController, &TimerController::on_timer2_overflow>(
                this);
        default:
            return core::bind_event_handler<TimerController, &TimerController::on_timer3_overflow>(
                this);
    }
}

interrupts::IrqSource TimerController::channel_irq_source(std::size_t index) noexcept {
    switch (index) {
        case 0U:
            return interrupts::IrqSource::Timer0;
        case 1U:
            return interrupts::IrqSource::Timer1;
        case 2U:
            return interrupts::IrqSource::Timer2;
        default:
            return interrupts::IrqSource::Timer3;
    }
}

}  // namespace elmasds::hardware::timers
