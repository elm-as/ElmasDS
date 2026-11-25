#include "input/keypad_mmio.hpp"

#include "primitives/bit_operations.hpp"

namespace elmasds::input {

namespace {

[[nodiscard]] u16 ext_button_bit(DsButton button) noexcept {
    switch (button) {
    case DsButton::X:
        return 1U << 0U;
    case DsButton::Y:
        return 1U << 1U;
    case DsButton::Debug:
        return 1U << 3U;
    case DsButton::PenDown:
        return 1U << 6U;
    case DsButton::HingeClosed:
        return 1U << 7U;
    default:
        return 0U;
    }
}

}  // namespace

KeypadController::CpuPort::CpuPort(KeypadController& owner,
                                   hardware::interrupts::InterruptController& irq,
                                   bool is_arm7) noexcept
    : owner_(owner), irq_(irq), is_arm7_(is_arm7) {}

void KeypadController::CpuPort::reset() noexcept {
    keycnt_ = 0U;
    condition_met_ = false;
}

u8 KeypadController::CpuPort::read8(Address address) noexcept {
    const u16 val = read16(address & ~1U);
    return static_cast<u8>((val >> ((address & 1U) * 8U)) & 0xFFU);
}

u16 KeypadController::CpuPort::read16(Address address) noexcept {
    switch (address & ~1U) {
    case kRegKeyInput:
        return owner_.keyinput();
    case kRegKeyCnt:
        return keycnt_;
    case kRegExtKeyIn:
        return is_arm7_ ? owner_.extkeyin() : 0U;
    default:
        return 0U;
    }
}

u32 KeypadController::CpuPort::read32(Address address) noexcept {
    const Address aligned = address & ~3U;
    return static_cast<u32>(read16(aligned)) | (static_cast<u32>(read16(aligned + 2U)) << 16U);
}

void KeypadController::CpuPort::write8(Address address, u8 value) noexcept {
    const Address aligned = address & ~1U;
    if (aligned != kRegKeyCnt) {
        return;
    }
    const u16 current = keycnt_;
    const u16 updated = ((address & 1U) == 0U)
                            ? static_cast<u16>((current & 0xFF00U) | value)
                            : static_cast<u16>((current & 0x00FFU) | (static_cast<u16>(value) << 8U));
    write16(aligned, updated);
}

void KeypadController::CpuPort::write16(Address address, u16 value) noexcept {
    if ((address & ~1U) != kRegKeyCnt) {
        return;
    }
    keycnt_ = value & 0xC3FFU;
    evaluate_irq(owner_.keyinput());
}

void KeypadController::CpuPort::write32(Address address, u32 value) noexcept {
    const Address aligned = address & ~3U;
    write16(aligned, static_cast<u16>(value & 0xFFFFU));
    write16(aligned + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
}

void KeypadController::CpuPort::evaluate_irq(u16 keyinput) noexcept {
    if (!bits::test_bit(keycnt_, 14U)) {
        condition_met_ = false;
        return;
    }
    const u16 selected_mask = keycnt_ & 0x03FFU;
    if (selected_mask == 0U) {
        condition_met_ = false;
        return;
    }

    const u16 pressed_mask = static_cast<u16>((~keyinput) & 0x03FFU);
    const bool and_mode = bits::test_bit(keycnt_, 15U);
    const bool now_met = and_mode ? ((pressed_mask & selected_mask) == selected_mask)
                                  : ((pressed_mask & selected_mask) != 0U);
    if (now_met && !condition_met_) {
        irq_.request_irq(hardware::interrupts::IrqSource::Keypad);
    }
    condition_met_ = now_met;
}

KeypadController::KeypadController(hardware::interrupts::InterruptController& arm9_irq,
                                   hardware::interrupts::InterruptController& arm7_irq) noexcept
    : arm9_port_(*this, arm9_irq, false), arm7_port_(*this, arm7_irq, true) {}

void KeypadController::reset() noexcept {
    keyinput_ = 0x03FFU;
    extkeyin_ = 0x007FU;
    arm9_port_.reset();
    arm7_port_.reset();
}

void KeypadController::set_button_pressed(DsButton button, bool pressed) noexcept {
    const u8 index = static_cast<u8>(button);
    if (index <= static_cast<u8>(DsButton::L)) {
        const u16 bit = static_cast<u16>(1U << index);
        if (pressed) {
            keyinput_ = static_cast<u16>(keyinput_ & ~bit);
        } else {
            keyinput_ = static_cast<u16>(keyinput_ | bit);
        }
        evaluate_irqs();
        return;
    }

    const u16 ext_bit = ext_button_bit(button);
    if (ext_bit == 0U) {
        return;
    }
    if (button == DsButton::HingeClosed) {
        // Bit 7 of EXTKEYIN: 0 = open, 1 = closed.
        extkeyin_ = pressed ? static_cast<u16>(extkeyin_ | ext_bit)
                            : static_cast<u16>(extkeyin_ & ~ext_bit);
        return;
    }

    // Active-low bits in EXTKEYIN (X, Y, Debug, PenDown): 0 = pressed, 1 = released.
    extkeyin_ = pressed ? static_cast<u16>(extkeyin_ & ~ext_bit)
                        : static_cast<u16>(extkeyin_ | ext_bit);
}

bool KeypadController::is_button_pressed(DsButton button) const noexcept {
    const u8 index = static_cast<u8>(button);
    if (index <= static_cast<u8>(DsButton::L)) {
        return (keyinput_ & static_cast<u16>(1U << index)) == 0U;
    }
    const u16 ext_bit = ext_button_bit(button);
    if (button == DsButton::HingeClosed) {
        return (extkeyin_ & ext_bit) != 0U;
    }
    return (extkeyin_ & ext_bit) == 0U;
}

void KeypadController::restore_state(u16 keyinput,
                                     u16 extkeyin,
                                     u16 arm9_keycnt,
                                     u16 arm7_keycnt) noexcept {
    keyinput_ = keyinput & 0x03FFU;
    extkeyin_ = extkeyin & 0x00FFU;
    arm9_port_.set_keycnt_raw(arm9_keycnt);
    arm7_port_.set_keycnt_raw(arm7_keycnt);
}

void KeypadController::evaluate_irqs() noexcept {
    arm9_port_.evaluate_irq(keyinput_);
    arm7_port_.evaluate_irq(keyinput_);
}

}  // namespace elmasds::input
