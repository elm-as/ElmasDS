#include "hardware/interrupts/interrupt_controller.hpp"

namespace elmasds::hardware::interrupts {

InterruptController::InterruptController(CpuKind cpu, InterruptTarget target) noexcept
    : cpu_(cpu), target_(target) {}

void InterruptController::reset() noexcept {
    ime_ = 0U;
    ie_ = 0U;
    if_ = 0U;
    notify_target();
}

void InterruptController::request_irq(IrqSource source) noexcept {
    if (!is_valid_irq_source(cpu_, source)) {
        return;
    }
    if_ |= irq_bit_mask(source);
    notify_target();
}

bool InterruptController::is_irq_asserted() const noexcept {
    return (ime_ & 1U) != 0U && (ie_ & if_) != 0U;
}

bool InterruptController::has_pending_wake() const noexcept {
    return (ie_ & if_) != 0U;
}

CpuKind InterruptController::cpu() const noexcept {
    return cpu_;
}

u32 InterruptController::ime() const noexcept {
    return ime_;
}

u32 InterruptController::ie() const noexcept {
    return ie_;
}

u32 InterruptController::if_flags() const noexcept {
    return if_;
}

u8 InterruptController::read8(Address address) {
    if (address < kMmioStart || address > kMmioEnd) {
        return 0U;
    }
    if (address == kRegIme) {
        return static_cast<u8>(ime_ & 1U);
    }
    if (address >= kRegIe && address < kRegIe + 4U) {
        const u32 shift = (address - kRegIe) * 8U;
        return static_cast<u8>((ie_ >> shift) & 0xFFU);
    }
    if (address >= kRegIf && address < kRegIf + 4U) {
        const u32 shift = (address - kRegIf) * 8U;
        return static_cast<u8>((if_ >> shift) & 0xFFU);
    }
    return 0U;
}

u16 InterruptController::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned < kMmioStart || aligned > kMmioEnd) {
        return 0U;
    }
    return static_cast<u16>(read8(aligned)) | (static_cast<u16>(read8(aligned + 1U)) << 8U);
}

u32 InterruptController::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned < kMmioStart || aligned > kMmioEnd) {
        return 0U;
    }
    return static_cast<u32>(read8(aligned)) | (static_cast<u32>(read8(aligned + 1U)) << 8U) |
           (static_cast<u32>(read8(aligned + 2U)) << 16U) |
           (static_cast<u32>(read8(aligned + 3U)) << 24U);
}

void InterruptController::write8(Address address, u8 value) {
    if (address < kMmioStart || address > kMmioEnd) {
        return;
    }
    apply_write(address, value, 0xFFU);
}

void InterruptController::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned < kMmioStart || aligned > kMmioEnd) {
        return;
    }
    apply_write(aligned, value, 0xFFFFU);
}

void InterruptController::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned < kMmioStart || aligned > kMmioEnd) {
        return;
    }
    apply_write(aligned, value, 0xFFFFFFFFU);
}

void InterruptController::apply_write(Address address, u32 value, u32 width_mask) noexcept {
    if (address == kRegIme) {
        ime_ = value & 1U;
        notify_target();
        return;
    }
    if (address >= kRegIe && address < kRegIe + 4U) {
        const u32 shift = (address - kRegIe) * 8U;
        const u32 byte_mask = width_mask << shift;
        const u32 updated = (ie_ & ~byte_mask) | ((value << shift) & byte_mask);
        ie_ = updated & valid_irq_mask(cpu_);
        notify_target();
        return;
    }
    if (address >= kRegIf && address < kRegIf + 4U) {
        const u32 shift = (address - kRegIf) * 8U;
        if_ &= ~((value & width_mask) << shift);
        notify_target();
    }
}

void InterruptController::notify_target() const noexcept {
    if (target_.set_irq_line != nullptr) {
        target_.set_irq_line(target_.context, is_irq_asserted());
    }
    if (target_.set_wake_pending != nullptr) {
        target_.set_wake_pending(target_.context, has_pending_wake());
    }
}

}  // namespace elmasds::hardware::interrupts
