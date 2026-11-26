#include "hardware/dma/dma_controller.hpp"

namespace elmasds::hardware::dma {
namespace {

[[nodiscard]] constexpr u32 apply_byte_write(u32 current, u32 byte_index, u8 value) noexcept {
    const u32 shift = (byte_index & 3U) * 8U;
    const u32 mask = ~(0xFFU << shift);
    return (current & mask) | (static_cast<u32>(value) << shift);
}

[[nodiscard]] constexpr u32 apply_halfword_write(u32 current, bool high_half, u16 value) noexcept {
    if (high_half) {
        return (current & 0x0000FFFFU) | (static_cast<u32>(value) << 16U);
    }
    return (current & 0xFFFF0000U) | static_cast<u32>(value);
}

[[nodiscard]] constexpr u8 read_lane_byte(u32 word, Address address) noexcept {
    const u32 shift = (address & 3U) * 8U;
    return static_cast<u8>((word >> shift) & 0xFFU);
}

[[nodiscard]] constexpr u16 read_lane_halfword(u32 word, Address address) noexcept {
    const u32 shift = (address & 2U) * 8U;
    return static_cast<u16>((word >> shift) & 0xFFFFU);
}

}  // namespace

DmaController::DmaController(interrupts::CpuKind cpu, cpu::CpuBusInterface& bus,
                             interrupts::InterruptController& irq) noexcept
    : cpu_(cpu), bus_(bus), irq_(irq) {}

void DmaController::reset() noexcept {
    channels_.fill(DmaChannel{});
    fill_registers_.fill(0U);
    in_progress_.fill(false);
    vblank_active_ = false;
}

void DmaController::trigger(DmaTrigger source) {
    if (source == DmaTrigger::HBlank && vblank_active_) {
        return;
    }
    for (std::size_t index = 0U; index < kDmaChannelCount; ++index) {
        const DmaChannel& ch = channels_[index];
        if (!ch.is_enabled() || ch.start_timing(cpu_, index) != source) {
            continue;
        }
        execute_transfer(index);
    }
}

void DmaController::trigger_vblank() {
    trigger(DmaTrigger::VBlank);
}

void DmaController::trigger_hblank() {
    trigger(DmaTrigger::HBlank);
}

void DmaController::trigger_cartridge() {
    trigger(DmaTrigger::CartridgeSlot1);
}

void DmaController::trigger_gxfifo() {
    trigger(DmaTrigger::GeometryFifo);
}

void DmaController::set_vblank_active(bool active) noexcept {
    vblank_active_ = active;
}

bool DmaController::is_channel_active(u8 channel) const noexcept {
    if (channel >= kDmaChannelCount) {
        return false;
    }
    return channels_[channel].is_enabled();
}

const DmaChannel& DmaController::channel(std::size_t index) const noexcept {
    if (index >= kDmaChannelCount) {
        return channels_[0U];
    }
    return channels_[index];
}

u32 DmaController::fill_register(std::size_t index) const noexcept {
    if (cpu_ != interrupts::CpuKind::Arm9 || index >= kDmaChannelCount) {
        return 0U;
    }
    return fill_registers_[index];
}

interrupts::CpuKind DmaController::cpu() const noexcept {
    return cpu_;
}

u8 DmaController::read8(Address address) {
    return read_lane_byte(read32(address), address);
}

u16 DmaController::read16(Address address) {
    const Address aligned = address & ~1U;
    return read_lane_halfword(read32(aligned), aligned);
}

u32 DmaController::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned >= kDmaFillBaseAddress && aligned <= kDmaFillEndAddress) {
        if (cpu_ != interrupts::CpuKind::Arm9) {
            return 0U;
        }
        const std::size_t index = static_cast<std::size_t>((aligned - kDmaFillBaseAddress) >> 2U);
        return fill_registers_[index];
    }
    if (aligned < kDmaBaseAddress || aligned > kDmaChannelEndAddress) {
        return 0U;
    }
    const Address offset = aligned - kDmaBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset / kDmaChannelStride);
    const Address reg = offset % kDmaChannelStride;
    return read_channel_word(index, reg);
}

void DmaController::write8(Address address, u8 value) {
    if (address >= kDmaFillBaseAddress && address <= kDmaFillEndAddress) {
        if (cpu_ != interrupts::CpuKind::Arm9) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>((address - kDmaFillBaseAddress) >> 2U);
        fill_registers_[index] = apply_byte_write(fill_registers_[index], address & 3U, value);
        return;
    }
    if (address < kDmaBaseAddress || address > kDmaChannelEndAddress) {
        return;
    }
    const Address offset = address - kDmaBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset / kDmaChannelStride);
    write_channel_byte(index, offset % kDmaChannelStride, value);
}

void DmaController::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned >= kDmaFillBaseAddress && aligned <= kDmaFillEndAddress) {
        if (cpu_ != interrupts::CpuKind::Arm9) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>((aligned - kDmaFillBaseAddress) >> 2U);
        fill_registers_[index] =
            apply_halfword_write(fill_registers_[index], (aligned & 2U) != 0U, value);
        return;
    }
    if (aligned < kDmaBaseAddress || aligned > kDmaChannelEndAddress) {
        return;
    }
    const Address offset = aligned - kDmaBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset / kDmaChannelStride);
    write_channel_halfword(index, offset % kDmaChannelStride, value);
}

void DmaController::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned >= kDmaFillBaseAddress && aligned <= kDmaFillEndAddress) {
        if (cpu_ != interrupts::CpuKind::Arm9) {
            return;
        }
        const std::size_t index = static_cast<std::size_t>((aligned - kDmaFillBaseAddress) >> 2U);
        fill_registers_[index] = value;
        return;
    }
    if (aligned < kDmaBaseAddress || aligned > kDmaChannelEndAddress) {
        return;
    }
    const Address offset = aligned - kDmaBaseAddress;
    const std::size_t index = static_cast<std::size_t>(offset / kDmaChannelStride);
    write_channel_word(index, offset % kDmaChannelStride, value);
}

u32 DmaController::read_channel_word(std::size_t index, Address reg_offset) const noexcept {
    const DmaChannel& ch = channels_[index];
    switch (reg_offset) {
    case 0U:
        return ch.sad;
    case 4U:
        return ch.dad;
    default:
        return (static_cast<u32>(ch.cnt_h) << 16U) | static_cast<u32>(ch.cnt_l);
    }
}

void DmaController::write_channel_byte(std::size_t index, Address reg_offset, u8 value) {
    DmaChannel& ch = channels_[index];
    const Address lane = reg_offset & 3U;
    if (reg_offset < 4U) {
        ch.sad = apply_byte_write(ch.sad, lane, value) & sad_mask(cpu_, index);
        return;
    }
    if (reg_offset < 8U) {
        ch.dad = apply_byte_write(ch.dad, lane, value) & dad_mask(cpu_, index);
        return;
    }
    if (reg_offset < 10U) {
        const u32 merged = apply_byte_write(static_cast<u32>(ch.cnt_l), lane, value);
        ch.cnt_l = static_cast<u16>(merged & cnt_l_mask(cpu_, index));
        return;
    }
    const u32 merged = apply_byte_write(static_cast<u32>(ch.cnt_h), lane - 2U, value);
    write_control(index, static_cast<u16>(merged & 0xFFFFU));
}

void DmaController::write_channel_halfword(std::size_t index, Address reg_offset, u16 value) {
    DmaChannel& ch = channels_[index];
    const bool high_half = (reg_offset & 2U) != 0U;
    if (reg_offset < 4U) {
        ch.sad = apply_halfword_write(ch.sad, high_half, value) & sad_mask(cpu_, index);
        return;
    }
    if (reg_offset < 8U) {
        ch.dad = apply_halfword_write(ch.dad, high_half, value) & dad_mask(cpu_, index);
        return;
    }
    if (reg_offset == 8U) {
        ch.cnt_l = static_cast<u16>(value & cnt_l_mask(cpu_, index));
        return;
    }
    write_control(index, value);
}

void DmaController::write_channel_word(std::size_t index, Address reg_offset, u32 value) {
    DmaChannel& ch = channels_[index];
    if (reg_offset == 0U) {
        ch.sad = value & sad_mask(cpu_, index);
        return;
    }
    if (reg_offset == 4U) {
        ch.dad = value & dad_mask(cpu_, index);
        return;
    }
    // Update CNT_L before CNT_H so a 32-bit STR with Enable=1 latches the new word count.
    ch.cnt_l = static_cast<u16>((value & 0xFFFFU) & cnt_l_mask(cpu_, index));
    write_control(index, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

void DmaController::write_control(std::size_t index, u16 raw_value) {
    DmaChannel& ch = channels_[index];
    const bool was_enabled = ch.is_enabled();
    ch.cnt_h = static_cast<u16>(raw_value & cnt_h_mask(cpu_));
    if (was_enabled || !ch.is_enabled()) {
        return;
    }
    latch_channel(index);
    const DmaTrigger timing = ch.start_timing(cpu_, index);
    if (timing == DmaTrigger::Immediate) {
        execute_transfer(index);
    }
}

void DmaController::latch_channel(std::size_t index) noexcept {
    DmaChannel& ch = channels_[index];
    ch.internal_sad = ch.sad & sad_mask(cpu_, index);
    ch.internal_dad = ch.dad & dad_mask(cpu_, index);
    ch.internal_count = ch.compute_word_count(cpu_, index);
}

void DmaController::execute_transfer(std::size_t index) {
    if (in_progress_[index]) {
        return;
    }
    primitives::ScopedProfile profile_scope(profiler_, primitives::ProfileCategory::Dma);
    in_progress_[index] = true;
    if (channels_[index].src_ctrl() == 3U) {
        // Source address control = 3 is prohibited on NDS; treat as a safe no-op.
        finish_transfer(index, false);
        in_progress_[index] = false;
        return;
    }
    transfer_units(index);
    finish_transfer(index, true);
    in_progress_[index] = false;
}

void DmaController::transfer_units(std::size_t index) {
    DmaChannel& ch = channels_[index];
    const bool wide = ch.is_32bit();
    const u32 unit_bytes = wide ? 4U : 2U;
    const u32 align_mask = wide ? ~3U : ~1U;
    const u32 src_add = address_step_add(ch.src_ctrl(), unit_bytes);
    const u32 dst_add = address_step_add(ch.dest_ctrl(), unit_bytes);
    const u32 src_mask = sad_mask(cpu_, index);
    const u32 dst_mask = dad_mask(cpu_, index);

    const u32 count = ch.internal_count;
    u32 src = ch.internal_sad & align_mask;
    u32 dst = ch.internal_dad & align_mask;
    for (u32 unit = 0U; unit < count; ++unit) {
        if (wide) {
            bus_.write32(dst, bus_.read32(src));
        } else {
            bus_.write16(dst, bus_.read16(src));
        }
        src = (src + src_add) & src_mask;
        dst = (dst + dst_add) & dst_mask;
    }
    ch.internal_sad = src;
    ch.internal_dad = dst;
}

void DmaController::finish_transfer(std::size_t index, bool raise_irq) noexcept {
    DmaChannel& ch = channels_[index];
    const DmaTrigger timing = ch.start_timing(cpu_, index);
    if (ch.is_repeat() && timing != DmaTrigger::Immediate) {
        ch.internal_count = ch.compute_word_count(cpu_, index);
        if (ch.dest_ctrl() == 3U) {
            ch.internal_dad = ch.dad & dad_mask(cpu_, index);
        }
    } else {
        ch.cnt_h = static_cast<u16>(ch.cnt_h & ~(1U << kDmaEnableBit));
    }

    if (raise_irq && ch.is_irq_enabled()) {
        irq_.request_irq(channel_irq_source(index));
    }
}

interrupts::IrqSource DmaController::channel_irq_source(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return interrupts::IrqSource::Dma0;
    case 1U:
        return interrupts::IrqSource::Dma1;
    case 2U:
        return interrupts::IrqSource::Dma2;
    default:
        return interrupts::IrqSource::Dma3;
    }
}

}  // namespace elmasds::hardware::dma
