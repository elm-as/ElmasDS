#pragma once

#include "hardware/interrupts/irq_source.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::hardware::interrupts {

/**
 * @brief Non-owning callbacks connecting InterruptController to a CPU's IRQ and wake lines.
 */
struct InterruptTarget {
    void* context = nullptr;
    void (*set_irq_line)(void* context, bool asserted) = nullptr;
    void (*set_wake_pending)(void* context, bool pending) = nullptr;
};

/**
 * @brief Binds any CPU exposing `set_irq_line(bool)` and `set_wake_pending(bool)` as a target.
 */
template <typename Cpu>
[[nodiscard]] constexpr InterruptTarget make_interrupt_target(Cpu* cpu) noexcept {
    return InterruptTarget{
        cpu,
        [](void* context, bool asserted) {
            if (context != nullptr) {
                static_cast<Cpu*>(context)->set_irq_line(asserted);
            }
        },
        [](void* context, bool pending) {
            if (context != nullptr) {
                static_cast<Cpu*>(context)->set_wake_pending(pending);
            }
        },
    };
}

/**
 * @brief Nintendo DS per-CPU interrupt controller (`IME`, `IE`, `IF` at `0x04000208..0x04000217`).
 *
 * Responsibility:
 *   Maintains `IME`, `IE`, and write-1-to-clear `IF` registers, enforces per-CPU valid IRQ
 *   bitmasks, asserts the CPU IRQ line when `(IME & 1) != 0 && (IE & IF) != 0`, and asserts
 *   the CPU halt-wake signal whenever `(IE & IF) != 0` regardless of `IME`.
 */
class InterruptController final : public memory::MmioDevice {
public:
    static constexpr Address kMmioStart = 0x04000208U;
    static constexpr Address kMmioEnd = 0x04000217U;
    static constexpr Address kRegIme = 0x04000208U;
    static constexpr Address kRegIe = 0x04000210U;
    static constexpr Address kRegIf = 0x04000214U;

    /**
     * @brief Constructs an interrupt controller for `cpu` with optional target callbacks.
     */
    explicit InterruptController(CpuKind cpu, InterruptTarget target = {}) noexcept;

    /**
     * @brief Clears `IME`, `IE`, and `IF` to zero and notifies the attached CPU target.
     */
    void reset() noexcept;

    /**
     * @brief Latches the `IF` flag for `source` if valid for this CPU and updates target lines.
     */
    void request_irq(IrqSource source) noexcept;

    /**
     * @brief Returns true when master IRQ is enabled and at least one enabled IRQ is pending.
     */
    [[nodiscard]] bool is_irq_asserted() const noexcept;

    /**
     * @brief Returns true when `(IE & IF) != 0`, which wakes a halted CPU even if `IME == 0`.
     */
    [[nodiscard]] bool has_pending_wake() const noexcept;

    [[nodiscard]] CpuKind cpu() const noexcept;
    [[nodiscard]] u32 ime() const noexcept;
    [[nodiscard]] u32 ie() const noexcept;
    [[nodiscard]] u32 if_flags() const noexcept;
    void restore_state(u32 ime_val, u32 ie_val, u32 if_val) noexcept {
        ime_ = ime_val & 1U;
        ie_ = ie_val;
        if_ = if_val;
        notify_target();
    }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    void apply_write(Address address, u32 value, u32 width_mask) noexcept;
    void notify_target() const noexcept;

    CpuKind cpu_;
    InterruptTarget target_{};
    u32 ime_ = 0U;
    u32 ie_ = 0U;
    u32 if_ = 0U;
};

}  // namespace elmasds::hardware::interrupts
