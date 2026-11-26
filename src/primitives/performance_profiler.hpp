#pragma once

#include "primitives/fixed_width_types.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>

namespace elmasds::primitives {

/**
 * @brief Subsystem buckets tracked by the exclusive-time performance profiler.
 */
enum class ProfileCategory : u8 {
    Arm9 = 0,
    Arm7 = 1,
    Scheduler = 2,
    MemoryBus = 3,
    Dma = 4,
    Timers = 5,
    Gpu2dEngineA = 6,
    Gpu2dEngineB = 7,
    Gpu3d = 8,
    LayerComposition = 9,
    OpenGlPresent = 10,
    Audio = 11,
};

inline constexpr std::size_t kProfileCategoryCount = 12U;

[[nodiscard]] constexpr std::string_view profile_category_name(ProfileCategory category) noexcept {
    switch (category) {
    case ProfileCategory::Arm9: return "ARM9 CPU";
    case ProfileCategory::Arm7: return "ARM7 CPU";
    case ProfileCategory::Scheduler: return "Scheduler";
    case ProfileCategory::MemoryBus: return "Memory Bus";
    case ProfileCategory::Dma: return "DMA";
    case ProfileCategory::Timers: return "Timers";
    case ProfileCategory::Gpu2dEngineA: return "GPU 2D Engine A";
    case ProfileCategory::Gpu2dEngineB: return "GPU 2D Engine B";
    case ProfileCategory::Gpu3d: return "GPU 3D";
    case ProfileCategory::LayerComposition: return "Layer Composition";
    case ProfileCategory::OpenGlPresent: return "OpenGL Present";
    case ProfileCategory::Audio: return "Audio";
    }
    return "Unknown";
}

struct CategoryStats {
    u64 total_ns = 0ULL;
    u64 max_frame_ns = 0ULL;

    [[nodiscard]] double total_ms() const noexcept {
        return static_cast<double>(total_ns) / 1'000'000.0;
    }

    [[nodiscard]] double avg_frame_ms(u64 frames) const noexcept {
        return (frames > 0ULL) ? (total_ms() / static_cast<double>(frames)) : 0.0;
    }

    [[nodiscard]] double max_frame_ms() const noexcept {
        return static_cast<double>(max_frame_ns) / 1'000'000.0;
    }

    [[nodiscard]] double percent_of(u64 total_measured_ns) const noexcept {
        return (total_measured_ns > 0ULL)
                   ? (100.0 * static_cast<double>(total_ns) / static_cast<double>(total_measured_ns))
                   : 0.0;
    }
};

struct ProfileCounters {
    u64 arm9_cycles = 0ULL;
    u64 arm7_cycles = 0ULL;
    u64 scheduler_events = 0ULL;
    u64 scanlines_rendered = 0ULL;
    u64 sprites_evaluated = 0ULL;
    u64 sprites_visible = 0ULL;
    u64 pixels_composited = 0ULL;
    u64 polygons_3d = 0ULL;
};

/**
 * @brief Lightweight, toggleable exclusive-time profiler using `std::chrono::steady_clock`.
 *
 * Responsibility:
 *   Tracks per-subsystem exclusive durations across nested scopes without double counting,
 *   alongside sampled MemoryBus timings and hardware activity counters. Single owner: `Emulator`.
 */
class PerformanceProfiler final {
public:
    using Clock = std::chrono::steady_clock;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }

    void reset() noexcept {
        stats_.fill(CategoryStats{});
        current_frame_ns_.fill(0ULL);
        counters_ = ProfileCounters{};
        frame_count_ = 0ULL;
        total_wall_ns_ = 0ULL;
        max_frame_wall_ns_ = 0ULL;
        stack_depth_ = 0U;
        bus_sample_counter_ = 0ULL;
        frame_active_ = false;
        qpc_overhead_ns_ = measure_clock_overhead();
    }

    void begin_frame() noexcept {
        if (!enabled_) { return; }
        current_frame_ns_.fill(0ULL);
        frame_start_ = Clock::now();
        frame_active_ = true;
    }

    void end_frame(u64 delta_arm9_cycles, u64 delta_arm7_cycles) noexcept {
        if (!enabled_ || !frame_active_) { return; }
        const u64 wall_ns = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - frame_start_).count());
        total_wall_ns_ += wall_ns;
        max_frame_wall_ns_ = std::max(max_frame_wall_ns_, wall_ns);
        for (std::size_t i = 0U; i < kProfileCategoryCount; ++i) {
            stats_[i].max_frame_ns = std::max(stats_[i].max_frame_ns, current_frame_ns_[i]);
        }
        counters_.arm9_cycles += delta_arm9_cycles;
        counters_.arm7_cycles += delta_arm7_cycles;
        ++frame_count_;
        frame_active_ = false;
    }

    [[nodiscard]] Clock::time_point push_scope(ProfileCategory category) noexcept {
        const Clock::time_point now = Clock::now();
        if (stack_depth_ < scope_stack_.size()) {
            scope_stack_[stack_depth_++] = ActiveScope{category, now, 0ULL};
        }
        return now;
    }

    void pop_scope() noexcept {
        if (stack_depth_ == 0U) { return; }
        const ActiveScope finished = scope_stack_[--stack_depth_];
        const u64 elapsed_ns = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - finished.start).count());
        const u64 exclusive_ns = (elapsed_ns > finished.child_ns) ? (elapsed_ns - finished.child_ns) : 0ULL;
        const std::size_t idx = static_cast<std::size_t>(finished.category);
        stats_[idx].total_ns += exclusive_ns;
        current_frame_ns_[idx] += exclusive_ns;
        if (stack_depth_ > 0U) {
            scope_stack_[stack_depth_ - 1U].child_ns += elapsed_ns;
        }
    }

    [[nodiscard]] bool try_begin_bus_sample(Clock::time_point& out_start) noexcept {
        if (!enabled_ || ((++bus_sample_counter_ & 63ULL) != 0ULL)) {
            return false;
        }
        out_start = Clock::now();
        return true;
    }

    void end_bus_sample(Clock::time_point start) noexcept {
        const u64 raw_ns = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        const u64 net_ns = (raw_ns > qpc_overhead_ns_) ? (raw_ns - qpc_overhead_ns_) : 1ULL;
        const u64 scaled_ns = net_ns << 6U;
        constexpr std::size_t kIdx = static_cast<std::size_t>(ProfileCategory::MemoryBus);
        stats_[kIdx].total_ns += scaled_ns;
        current_frame_ns_[kIdx] += scaled_ns;
        if (stack_depth_ > 0U) {
            scope_stack_[stack_depth_ - 1U].child_ns += scaled_ns;
        }
    }

    void add_scheduler_event() noexcept { ++counters_.scheduler_events; }
    void add_scanline_rendered() noexcept { ++counters_.scanlines_rendered; }
    void add_sprite_evaluated() noexcept { ++counters_.sprites_evaluated; }
    void add_sprites_evaluated(u64 count) noexcept { counters_.sprites_evaluated += count; }
    void add_sprite_visible() noexcept { ++counters_.sprites_visible; }
    void add_pixels_composited(u64 count) noexcept { counters_.pixels_composited += count; }
    void add_polygons_3d(u64 count) noexcept { counters_.polygons_3d += count; }

    [[nodiscard]] const CategoryStats& category_stats(ProfileCategory cat) const noexcept {
        return stats_[static_cast<std::size_t>(cat)];
    }

    [[nodiscard]] u64 total_measured_ns() const noexcept {
        u64 sum = 0ULL;
        for (const auto& s : stats_) { sum += s.total_ns; }
        return sum;
    }

    [[nodiscard]] u64 frame_count() const noexcept { return frame_count_; }
    [[nodiscard]] u64 total_wall_ns() const noexcept { return total_wall_ns_; }
    [[nodiscard]] u64 max_frame_wall_ns() const noexcept { return max_frame_wall_ns_; }
    [[nodiscard]] const ProfileCounters& counters() const noexcept { return counters_; }

    [[nodiscard]] double host_fps() const noexcept {
        const double sec = static_cast<double>(total_wall_ns_) / 1'000'000'000.0;
        return (sec > 0.0) ? (static_cast<double>(frame_count_) / sec) : 0.0;
    }

    [[nodiscard]] double emulation_speed_percent() const noexcept {
        constexpr double kDsFrameRate = 59.8261;
        return (host_fps() / kDsFrameRate) * 100.0;
    }

private:
    [[nodiscard]] static u64 measure_clock_overhead() noexcept {
        u64 min_ns = 1000ULL;
        for (int i = 0; i < 128; ++i) {
            const Clock::time_point t0 = Clock::now();
            const Clock::time_point t1 = Clock::now();
            const u64 dt = static_cast<u64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            if (dt > 0ULL) { min_ns = std::min(min_ns, dt); }
        }
        return (min_ns < 1000ULL) ? min_ns : 0ULL;
    }

    struct ActiveScope {
        ProfileCategory category = ProfileCategory::Arm9;
        Clock::time_point start{};
        u64 child_ns = 0ULL;
    };

    bool enabled_ = false;
    bool frame_active_ = false;
    std::array<CategoryStats, kProfileCategoryCount> stats_{};
    std::array<u64, kProfileCategoryCount> current_frame_ns_{};
    ProfileCounters counters_{};
    std::array<ActiveScope, 16U> scope_stack_{};
    std::size_t stack_depth_ = 0U;
    u64 bus_sample_counter_ = 0ULL;
    u64 qpc_overhead_ns_ = 0ULL;
    u64 frame_count_ = 0ULL;
    u64 total_wall_ns_ = 0ULL;
    u64 max_frame_wall_ns_ = 0ULL;
    Clock::time_point frame_start_{};
};

class ScopedProfile final {
public:
    ScopedProfile(PerformanceProfiler* profiler, ProfileCategory category) noexcept
        : active_(profiler != nullptr && profiler->is_enabled()), profiler_(profiler) {
        if (active_) { (void)profiler_->push_scope(category); }
    }
    ~ScopedProfile() {
        if (active_) { profiler_->pop_scope(); }
    }
    ScopedProfile(const ScopedProfile&) = delete;
    ScopedProfile& operator=(const ScopedProfile&) = delete;

private:
    bool active_ = false;
    PerformanceProfiler* profiler_ = nullptr;
};

class ScopedBusSample final {
public:
    explicit ScopedBusSample(PerformanceProfiler* profiler) noexcept : profiler_(profiler) {
        if (profiler_ != nullptr && profiler_->try_begin_bus_sample(start_)) {
            sampled_ = true;
        }
    }
    ~ScopedBusSample() {
        if (sampled_) { profiler_->end_bus_sample(start_); }
    }
    ScopedBusSample(const ScopedBusSample&) = delete;
    ScopedBusSample& operator=(const ScopedBusSample&) = delete;

private:
    bool sampled_ = false;
    PerformanceProfiler* profiler_ = nullptr;
    PerformanceProfiler::Clock::time_point start_{};
};

}  // namespace elmasds::primitives
