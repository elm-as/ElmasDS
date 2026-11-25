#include "core/scheduler/event_scheduler.hpp"

#include <algorithm>
#include <functional>

namespace elmasds::core {

void EventScheduler::reset() noexcept {
    current_tick_ = 0;
    next_sequence_id_ = 1;
    heap_.clear();
}

void EventScheduler::schedule(const ScheduleRequest& request) {
    if (!request.handler.is_valid()) {
        return;
    }

    const ScheduledEvent entry{
        current_tick_ + request.delay_ticks,
        next_sequence_id_++,
        request.type,
        request.handler,
    };
    heap_.push_back(entry);
    std::push_heap(heap_.begin(), heap_.end(), std::greater<ScheduledEvent>{});
}

std::size_t EventScheduler::cancel_by_type(EventType type) {
    const std::size_t original_size = heap_.size();
    std::erase_if(heap_, [type](const ScheduledEvent& event) { return event.type == type; });
    const std::size_t removed = original_size - heap_.size();
    if (removed > 0) {
        std::make_heap(heap_.begin(), heap_.end(), std::greater<ScheduledEvent>{});
    }
    return removed;
}

void EventScheduler::advance_by(TickCount delta_ticks) {
    advance_to(current_tick_ + delta_ticks);
}

void EventScheduler::advance_to(TickCount target_tick) {
    if (target_tick < current_tick_) {
        return;
    }

    while (!heap_.empty() && heap_.front().trigger_tick <= target_tick) {
        pop_and_dispatch();
    }
    current_tick_ = target_tick;
}

bool EventScheduler::run_next_event() {
    if (heap_.empty()) {
        return false;
    }
    pop_and_dispatch();
    return true;
}

TickCount EventScheduler::current_tick() const noexcept {
    return current_tick_;
}

std::optional<TickCount> EventScheduler::next_event_tick() const noexcept {
    if (heap_.empty()) {
        return std::nullopt;
    }
    return heap_.front().trigger_tick;
}

std::size_t EventScheduler::pending_count() const noexcept {
    return heap_.size();
}

void EventScheduler::pop_and_dispatch() {
    std::pop_heap(heap_.begin(), heap_.end(), std::greater<ScheduledEvent>{});
    const ScheduledEvent event = heap_.back();
    heap_.pop_back();

    if (event.trigger_tick > current_tick_) {
        current_tick_ = event.trigger_tick;
    }
    const TickCount late_ticks = current_tick_ - event.trigger_tick;
    event.handler.invoke(event.handler.context, late_ticks);
}

}  // namespace elmasds::core
