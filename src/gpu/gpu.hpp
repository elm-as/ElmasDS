#pragma once

#include "core/scheduler/event_scheduler.hpp"
#include "gpu/engine_3d/engine_3d.hpp"
#include "gpu/engines_2d/engine_2d.hpp"
#include "gpu/registers/display_status_mmio.hpp"
#include "gpu/registers/gpu_registers.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::gpu {

/// Master ticks per HDraw period (256 dots * 6 ARM7 cycles * 2 master ticks/cycle = 3072).
inline constexpr TickCount kHDrawTicks = 3072ULL;

/// Master ticks per HBlank period (99 dots * 6 ARM7 cycles * 2 master ticks/cycle = 1188).
inline constexpr TickCount kHBlankTicks = 1188ULL;

/// Master ticks per full scanline (`3072 + 1188 = 4260`).
inline constexpr TickCount kScanlineTicks = kHDrawTicks + kHBlankTicks;

/// Total scanlines per frame (`192` visible + `71` VBlank = `263`).
inline constexpr u16 kLinesPerFrame = 263U;

/// Total master ticks per frame (`263 * 4260 = 1,120,380`).
inline constexpr TickCount kTicksPerFrame = kScanlineTicks * kLinesPerFrame;

/**
 * @brief Non-owning references required to construct the `Gpu` coordinator.
 */
struct GpuDependencies {
    core::EventScheduler& scheduler;
    memory::MemorySystem& memory;
    hardware::interrupts::InterruptController& arm9_irq;
    hardware::interrupts::InterruptController& arm7_irq;
    hardware::dma::DmaController& arm9_dma;
    hardware::dma::DmaController& arm7_dma;
};

/**
 * @brief Top-level Nintendo DS GPU coordinator owning display timing, 2D engines, and framebuffers.
 */
class Gpu {
public:
    static constexpr std::size_t kFramebufferPixels = kScreenWidth * kScreenHeight;

    explicit Gpu(const GpuDependencies& deps);

    Gpu(const Gpu&) = delete;
    Gpu& operator=(const Gpu&) = delete;
    Gpu(Gpu&&) = delete;
    Gpu& operator=(Gpu&&) = delete;

    void register_mmio(memory::MmioRouter& arm9_mmio, memory::MmioRouter& arm7_mmio);
    void reset();
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept;

    [[nodiscard]] u16 vcount() const noexcept { return vcount_; }
    [[nodiscard]] u64 frame_count() const noexcept { return frame_count_; }

    [[nodiscard]] std::span<const u32> top_framebuffer() const noexcept { return top_screen_; }
    [[nodiscard]] std::span<const u32> bottom_framebuffer() const noexcept {
        return bottom_screen_;
    }

    [[nodiscard]] GpuRegisters& regs_a() noexcept { return regs_a_; }
    [[nodiscard]] const GpuRegisters& regs_a() const noexcept { return regs_a_; }
    [[nodiscard]] GpuRegisters& regs_b() noexcept { return regs_b_; }
    [[nodiscard]] const GpuRegisters& regs_b() const noexcept { return regs_b_; }
    [[nodiscard]] PowCnt1Mmio& powcnt1() noexcept { return powcnt1_; }
    [[nodiscard]] const PowCnt1Mmio& powcnt1() const noexcept { return powcnt1_; }
    [[nodiscard]] Engine2D& engine_a() noexcept { return engine_a_; }
    [[nodiscard]] Engine3D& engine_3d() noexcept { return engine_3d_; }
    [[nodiscard]] const Engine3D& engine_3d() const noexcept { return engine_3d_; }

private:
    void schedule_hblank(TickCount late_ticks);
    void schedule_hdraw(TickCount late_ticks);
    void on_hblank_start(TickCount late_ticks);
    void on_scanline_end(TickCount late_ticks);
    void render_current_scanline();
    void check_vcount_match() noexcept;

    core::EventScheduler& scheduler_;
    hardware::interrupts::InterruptController& arm9_irq_;
    hardware::interrupts::InterruptController& arm7_irq_;
    hardware::dma::DmaController& arm9_dma_;
    hardware::dma::DmaController& arm7_dma_;

    u16 vcount_ = 0U;
    u64 frame_count_ = 0ULL;

    GpuRegisters regs_a_;
    GpuRegisters regs_b_;
    DisplayStatusMmio dispstat_arm9_;
    DisplayStatusMmio dispstat_arm7_;
    PowCnt1Mmio powcnt1_;
    Engine2D engine_a_;
    Engine2D engine_b_;
    Engine3D engine_3d_;

    std::vector<u32> top_screen_;
    std::vector<u32> bottom_screen_;
    primitives::PerformanceProfiler* profiler_ = nullptr;
};

}  // namespace elmasds::gpu
