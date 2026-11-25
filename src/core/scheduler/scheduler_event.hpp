#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::core {

/**
 * @brief Hardware event categories managed by the centralized EventScheduler.
 */
enum class EventType : u16 {
    None = 0,
    ScanlineHBlank,
    ScanlineHDraw,
    Timer0Overflow,
    Timer1Overflow,
    Timer2Overflow,
    Timer3Overflow,
    DmaTransfer,
    CartridgeTransfer,
    AudioSample,
    CustomTest,
    Arm9Timer0Overflow,
    Arm9Timer1Overflow,
    Arm9Timer2Overflow,
    Arm9Timer3Overflow,
    Arm7Timer0Overflow,
    Arm7Timer1Overflow,
    Arm7Timer2Overflow,
    Arm7Timer3Overflow,
    Arm9DmaTransfer,
    Arm7DmaTransfer,
    Arm7SpiTransfer,
    Arm7RtcTick,
};

/**
 * @brief Non-allocating typed callback invoked when a scheduled event fires.
 */
struct EventHandler {
    void* context = nullptr;
    void (*invoke)(void* context, TickCount late_ticks) = nullptr;

    [[nodiscard]] bool is_valid() const noexcept { return invoke != nullptr; }
};

/**
 * @brief Binds a member method `void Class::Method(TickCount late_ticks)` into an EventHandler.
 */
template <typename Class, void (Class::*Method)(TickCount)>
[[nodiscard]] constexpr EventHandler bind_event_handler(Class* instance) noexcept {
    return EventHandler{
        instance,
        [](void* raw_context, TickCount late_ticks) {
            (static_cast<Class*>(raw_context)->*Method)(late_ticks);
        },
    };
}

/**
 * @brief Request parameters for scheduling a future hardware event.
 */
struct ScheduleRequest {
    EventType type = EventType::None;
    TickCount delay_ticks = 0;
    EventHandler handler{};
};

/**
 * @brief Internal priority-queue entry stored by EventScheduler.
 */
struct ScheduledEvent {
    TickCount trigger_tick = 0;
    u64 sequence_id = 0;
    EventType type = EventType::None;
    EventHandler handler{};

    /// Min-heap ordering: earlier trigger_tick first, then lower sequence_id (FIFO).
    [[nodiscard]] bool operator>(const ScheduledEvent& other) const noexcept {
        if (trigger_tick != other.trigger_tick) {
            return trigger_tick > other.trigger_tick;
        }
        return sequence_id > other.sequence_id;
    }
};

}  // namespace elmasds::core
