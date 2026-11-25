#include "core/scheduler/event_scheduler.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace elmasds::core {
namespace {

struct EventRecorder {
    std::vector<int> fired_ids;
    std::vector<TickCount> fired_ticks;
    EventScheduler* scheduler = nullptr;
    int id = 0;

    void on_event(TickCount late_ticks) {
        EXPECT_EQ(late_ticks, 0ULL);
        fired_ids.push_back(id);
        if (scheduler != nullptr) {
            fired_ticks.push_back(scheduler->current_tick());
        }
    }
};

struct SelfRescheduler {
    EventScheduler* scheduler = nullptr;
    int remaining = 0;
    int count = 0;

    void on_tick(TickCount /*late_ticks*/) {
        ++count;
        --remaining;
        if (remaining > 0 && scheduler != nullptr) {
            scheduler->schedule(ScheduleRequest{
                EventType::Timer0Overflow,
                10,
                bind_event_handler<SelfRescheduler, &SelfRescheduler::on_tick>(this),
            });
        }
    }
};

TEST(EventSchedulerTest, FiresEventsInChronologicalAndFifoOrder) {
    EventScheduler scheduler;
    std::vector<int> order;

    struct Tagged {
        std::vector<int>* log;
        int tag;
        void handle(TickCount /*late*/) { log->push_back(tag); }
    };

    Tagged first{&order, 1};
    Tagged second{&order, 2};
    Tagged third{&order, 3};

    scheduler.schedule(ScheduleRequest{
        EventType::ScanlineHBlank,
        50,
        bind_event_handler<Tagged, &Tagged::handle>(&second),
    });
    scheduler.schedule(ScheduleRequest{
        EventType::ScanlineHDraw,
        25,
        bind_event_handler<Tagged, &Tagged::handle>(&first),
    });
    scheduler.schedule(ScheduleRequest{
        EventType::DmaTransfer,
        50,
        bind_event_handler<Tagged, &Tagged::handle>(&third),
    });

    scheduler.advance_by(50);

    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_EQ(scheduler.current_tick(), 50ULL);
    EXPECT_EQ(scheduler.pending_count(), 0U);
}

TEST(EventSchedulerTest, CancelByTypeRemovesOnlyMatchingEvents) {
    EventScheduler scheduler;
    EventRecorder keep_recorder;
    keep_recorder.scheduler = &scheduler;
    keep_recorder.id = 42;

    EventRecorder drop_recorder;
    drop_recorder.scheduler = &scheduler;
    drop_recorder.id = 99;

    scheduler.schedule(ScheduleRequest{
        EventType::Timer0Overflow,
        100,
        bind_event_handler<EventRecorder, &EventRecorder::on_event>(&drop_recorder),
    });
    scheduler.schedule(ScheduleRequest{
        EventType::AudioSample,
        120,
        bind_event_handler<EventRecorder, &EventRecorder::on_event>(&keep_recorder),
    });

    const std::size_t removed = scheduler.cancel_by_type(EventType::Timer0Overflow);
    EXPECT_EQ(removed, 1U);
    EXPECT_EQ(scheduler.pending_count(), 1U);

    scheduler.advance_to(200);
    EXPECT_TRUE(drop_recorder.fired_ids.empty());
    ASSERT_EQ(keep_recorder.fired_ids.size(), 1U);
    EXPECT_EQ(keep_recorder.fired_ticks[0], 120ULL);
}

TEST(EventSchedulerTest, SupportsEventsScheduledInsideCallbacks) {
    EventScheduler scheduler;
    SelfRescheduler rescheduler{&scheduler, 3, 0};

    scheduler.schedule(ScheduleRequest{
        EventType::Timer0Overflow,
        10,
        bind_event_handler<SelfRescheduler, &SelfRescheduler::on_tick>(&rescheduler),
    });

    scheduler.advance_by(35);
    EXPECT_EQ(rescheduler.count, 3);
    EXPECT_EQ(scheduler.current_tick(), 35ULL);
}

}  // namespace
}  // namespace elmasds::core
