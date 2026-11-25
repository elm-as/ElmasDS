#pragma once

#include "hardware/interrupts/interrupt_controller.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::input {

inline constexpr Address kKeypadMmioStart = 0x04000130U;
inline constexpr Address kKeypadMmioEnd = 0x04000137U;
inline constexpr Address kRegKeyInput = 0x04000130U;
inline constexpr Address kRegKeyCnt = 0x04000132U;
inline constexpr Address kRegExtKeyIn = 0x04000136U;

/**
 * @brief Logical Nintendo DS hardware button identifiers.
 */
enum class DsButton : u8 {
    A = 0,
    B = 1,
    Select = 2,
    Start = 3,
    Right = 4,
    Left = 5,
    Up = 6,
    Down = 7,
    R = 8,
    L = 9,
    X = 10,
    Y = 11,
    Debug = 12,
    PenDown = 13,
    HingeClosed = 14,
};

/**
 * @brief Shared active-low keypad state and per-CPU `KEYINPUT`/`KEYCNT`/`EXTKEYIN` MMIO handlers.
 *
 * Responsibility:
 *   Tracks the 10 primary buttons (`KEYINPUT` `0x04000130`, active-low `0 = pressed`,
 *   `1 = released`), the extended ARM7 keys (`EXTKEYIN` `0x04000136`: X, Y, Debug, PenDown,
 *   HingeClosed), and evaluates ARM9/ARM7 `KEYCNT` (`0x04000132`) keypad interrupt conditions
 *   (logical OR vs logical AND).
 */
class KeypadController final {
public:
    KeypadController(hardware::interrupts::InterruptController& arm9_irq,
                     hardware::interrupts::InterruptController& arm7_irq) noexcept;

    void reset() noexcept;

    /**
     * @brief Sets the pressed/released state of one logical Nintendo DS button or sensor.
     */
    void set_button_pressed(DsButton button, bool pressed) noexcept;
    [[nodiscard]] bool is_button_pressed(DsButton button) const noexcept;

    [[nodiscard]] u16 keyinput() const noexcept { return keyinput_; }
    [[nodiscard]] u16 extkeyin() const noexcept { return extkeyin_; }
    [[nodiscard]] u16 arm9_keycnt() const noexcept { return arm9_port_.keycnt(); }
    [[nodiscard]] u16 arm7_keycnt() const noexcept { return arm7_port_.keycnt(); }

    void restore_state(u16 keyinput, u16 extkeyin, u16 arm9_keycnt, u16 arm7_keycnt) noexcept;

    [[nodiscard]] memory::MmioDevice& arm9_mmio() noexcept { return arm9_port_; }
    [[nodiscard]] memory::MmioDevice& arm7_mmio() noexcept { return arm7_port_; }

private:
    class CpuPort final : public memory::MmioDevice {
    public:
        CpuPort(KeypadController& owner,
                hardware::interrupts::InterruptController& irq,
                bool is_arm7) noexcept;

        void reset() noexcept;
        [[nodiscard]] u8 read8(Address address) noexcept override;
        [[nodiscard]] u16 read16(Address address) noexcept override;
        [[nodiscard]] u32 read32(Address address) noexcept override;
        void write8(Address address, u8 value) noexcept override;
        void write16(Address address, u16 value) noexcept override;
        void write32(Address address, u32 value) noexcept override;

        void evaluate_irq(u16 keyinput) noexcept;
        [[nodiscard]] u16 keycnt() const noexcept { return keycnt_; }
        void set_keycnt_raw(u16 value) noexcept {
            keycnt_ = value & 0xC3FFU;
            condition_met_ = false;
        }

    private:
        KeypadController& owner_;
        hardware::interrupts::InterruptController& irq_;
        bool is_arm7_ = false;
        u16 keycnt_ = 0U;
        bool condition_met_ = false;
    };

    void evaluate_irqs() noexcept;

    u16 keyinput_ = 0x03FFU;
    u16 extkeyin_ = 0x007FU;
    CpuPort arm9_port_;
    CpuPort arm7_port_;
};

}  // namespace elmasds::input
