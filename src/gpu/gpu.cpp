#include "gpu/gpu.hpp"

#include <algorithm>

namespace elmasds::gpu {

Gpu::Gpu(const GpuDependencies& deps)
    : scheduler_(deps.scheduler),
      arm9_irq_(deps.arm9_irq),
      arm7_irq_(deps.arm7_irq),
      arm9_dma_(deps.arm9_dma),
      arm7_dma_(deps.arm7_dma),
      regs_a_(kEngineABase, true),
      regs_b_(kEngineBBase, false),
      dispstat_arm9_(vcount_),
      dispstat_arm7_(vcount_),
      engine_a_(regs_a_, deps.memory),
      engine_b_(regs_b_, deps.memory),
      engine_3d_(deps.memory, deps.arm9_irq, deps.arm9_dma),
      top_screen_(kFramebufferPixels, 0xFFFFFFFFU),
      bottom_screen_(kFramebufferPixels, 0xFFFFFFFFU) {
    reset();
}

void Gpu::register_mmio(memory::MmioRouter& arm9_mmio, memory::MmioRouter& arm7_mmio) {
    // Register DISPSTAT/VCOUNT (0x04000004..0x04000007) before Engine A so first-match routes it.
    arm9_mmio.register_device({kDispStatMmioStart, kDispStatMmioEnd, &dispstat_arm9_});
    arm9_mmio.register_device({0x04000000U, 0x0400005FU, &regs_a_});
    arm9_mmio.register_device({kDisp3dCntStart, kDisp3dCntEnd, &engine_3d_});
    arm9_mmio.register_device({0x0400006CU, 0x0400006FU, &regs_a_});
    arm9_mmio.register_device({kEngineBBase, kEngineBEnd, &regs_b_});
    arm9_mmio.register_device({kPowCnt1MmioStart, kPowCnt1MmioEnd, &powcnt1_});
    arm9_mmio.register_device({kGxMmioStart, kGxMmioEnd, &engine_3d_});

    arm7_mmio.register_device({kDispStatMmioStart, kDispStatMmioEnd, &dispstat_arm7_});
}

void Gpu::reset() {
    scheduler_.cancel_by_type(core::EventType::ScanlineHBlank);
    scheduler_.cancel_by_type(core::EventType::ScanlineHDraw);

    vcount_ = 0U;
    frame_count_ = 0ULL;
    regs_a_.reset();
    regs_b_.reset();
    dispstat_arm9_.reset();
    dispstat_arm7_.reset();
    powcnt1_.reset();
    engine_3d_.reset();

    std::fill(top_screen_.begin(), top_screen_.end(), 0xFFFFFFFFU);
    std::fill(bottom_screen_.begin(), bottom_screen_.end(), 0xFFFFFFFFU);

    check_vcount_match();
    schedule_hblank(0ULL);
}

void Gpu::schedule_hblank(TickCount late_ticks) {
    const TickCount delay = (late_ticks < kHDrawTicks) ? (kHDrawTicks - late_ticks) : 1ULL;
    scheduler_.schedule(core::ScheduleRequest{
        core::EventType::ScanlineHBlank,
        delay,
        core::bind_event_handler<Gpu, &Gpu::on_hblank_start>(this),
    });
}

void Gpu::schedule_hdraw(TickCount late_ticks) {
    const TickCount delay = (late_ticks < kHBlankTicks) ? (kHBlankTicks - late_ticks) : 1ULL;
    scheduler_.schedule(core::ScheduleRequest{
        core::EventType::ScanlineHDraw,
        delay,
        core::bind_event_handler<Gpu, &Gpu::on_scanline_end>(this),
    });
}

void Gpu::on_hblank_start(TickCount late_ticks) {
    if (vcount_ < kScreenHeight) {
        render_current_scanline();
        regs_a_.step_affine_latches();
        regs_b_.step_affine_latches();
    }

    dispstat_arm9_.set_hblank(true);
    dispstat_arm7_.set_hblank(true);

    if (dispstat_arm9_.is_hblank_irq_enabled()) {
        arm9_irq_.request_irq(hardware::interrupts::IrqSource::HBlank);
    }
    if (dispstat_arm7_.is_hblank_irq_enabled()) {
        arm7_irq_.request_irq(hardware::interrupts::IrqSource::HBlank);
    }

    if (vcount_ < kScreenHeight) {
        arm9_dma_.trigger_hblank();
    }

    schedule_hdraw(late_ticks);
}

void Gpu::on_scanline_end(TickCount late_ticks) {
    dispstat_arm9_.set_hblank(false);
    dispstat_arm7_.set_hblank(false);

    vcount_ = static_cast<u16>((vcount_ + 1U) % kLinesPerFrame);
    check_vcount_match();

    if (vcount_ == kScreenHeight) {
        dispstat_arm9_.set_vblank(true);
        dispstat_arm7_.set_vblank(true);
        arm9_dma_.set_vblank_active(true);
        arm7_dma_.set_vblank_active(true);
        regs_a_.reload_affine_latches();
        regs_b_.reload_affine_latches();
        engine_3d_.on_vblank();

        if (dispstat_arm9_.is_vblank_irq_enabled()) {
            arm9_irq_.request_irq(hardware::interrupts::IrqSource::VBlank);
        }
        if (dispstat_arm7_.is_vblank_irq_enabled()) {
            arm7_irq_.request_irq(hardware::interrupts::IrqSource::VBlank);
        }
        arm9_dma_.trigger_vblank();
        arm7_dma_.trigger_vblank();
        ++frame_count_;
    } else if (vcount_ == (kLinesPerFrame - 1U)) {
        dispstat_arm9_.set_vblank(false);
        dispstat_arm7_.set_vblank(false);
        arm9_dma_.set_vblank_active(false);
        arm7_dma_.set_vblank_active(false);
    }

    schedule_hblank(late_ticks);
}

void Gpu::render_current_scanline() {
    const std::size_t row_offset = static_cast<std::size_t>(vcount_) * kScreenWidth;
    auto top_row = std::span<u32, kScreenWidth>(top_screen_.data() + row_offset, kScreenWidth);
    auto bot_row = std::span<u32, kScreenWidth>(bottom_screen_.data() + row_offset, kScreenWidth);

    if (!powcnt1_.lcd_enabled()) {
        std::fill(top_row.begin(), top_row.end(), 0xFFFFFFFFU);
        std::fill(bot_row.begin(), bot_row.end(), 0xFFFFFFFFU);
        return;
    }

    auto line_a = powcnt1_.display_swap() ? top_row : bot_row;
    auto line_b = powcnt1_.display_swap() ? bot_row : top_row;

    if (powcnt1_.engine_a_enabled()) {
        if ((regs_a_.dispcnt() & (1U << 3U)) != 0U) {
            engine_a_.set_line_3d_buffer(engine_3d_.scanline(vcount_));
        }
        engine_a_.render_scanline(vcount_, line_a);
    } else {
        std::fill(line_a.begin(), line_a.end(), 0xFFFFFFFFU);
    }

    if (powcnt1_.engine_b_enabled()) {
        engine_b_.render_scanline(vcount_, line_b);
    } else {
        std::fill(line_b.begin(), line_b.end(), 0xFFFFFFFFU);
    }
}

void Gpu::check_vcount_match() noexcept {
    dispstat_arm9_.update_vcount_match();
    dispstat_arm7_.update_vcount_match();

    if (dispstat_arm9_.is_vcount_matching() && dispstat_arm9_.is_vcount_irq_enabled()) {
        arm9_irq_.request_irq(hardware::interrupts::IrqSource::VCount);
    }
    if (dispstat_arm7_.is_vcount_matching() && dispstat_arm7_.is_vcount_irq_enabled()) {
        arm7_irq_.request_irq(hardware::interrupts::IrqSource::VCount);
    }
}

}  // namespace elmasds::gpu
