#pragma once

#include "core/scheduler/scheduler_event.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace elmasds::core {

/**
 * @brief Centralized deterministic hardware event queue driven by ARM9 master ticks.
 *
 * Responsibility:
 *   Maintains a unified timeline for both CPUs, scanline timing, timers, DMA,
 *   cartridge transfers, and audio sampling.
 * Dependencies:
 *   `primitives/fixed_width_types.hpp`, `scheduler_event.hpp`.
 * Threading / State:
 *   Instance-owned, single-threaded deterministic min-heap; zero global state.
 */
class EventScheduler {
public:
    EventScheduler() = default;

    /**
     * @brief Clears all pending events and resets the master tick counter to zero.
     */
    void reset() noexcept;

    /**
     * @brief Schedules a future event relative to `current_tick()`.
     * @param request Event type, delay in master ticks, and callback handler.
     */
    void schedule(const ScheduleRequest& request);

    /**
     * @brief Cancels all pending events matching `type`.
     * @return Number of removed events.
     */
    std::size_t cancel_by_type(EventType type);

    /**
     * @brief Advances the master clock by `delta_ticks`, dispatching all due events in order.
     */
    void advance_by(TickCount delta_ticks);

    /**
     * @brief Advances the master clock to `target_tick`, dispatching due events in order.
     *
     * If `target_tick <= current_tick_`, this is a no-op.
     */
    void advance_to(TickCount target_tick);

    /**
     * @brief Dispatches the earliest pending event, advancing `current_tick_` if needed.
     * @return True if an event was dispatched, false if the queue was empty.
     */
    bool run_next_event();

    [[nodiscard]] TickCount current_tick() const noexcept;
    [[nodiscard]] std::optional<TickCount> next_event_tick() const noexcept;
    [[nodiscard]] std::size_t pending_count() const noexcept;
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept { profiler_ = profiler; }

private:
    void pop_and_dispatch();

    TickCount current_tick_ = 0;
    u64 next_sequence_id_ = 1;
    std::vector<ScheduledEvent> heap_;
    primitives::PerformanceProfiler* profiler_ = nullptr;
};

}  // namespace elmasds::core
