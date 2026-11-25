#pragma once

#include "hardware/interrupts/irq_source.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::hardware::interrupts {

/**
 * @brief Power-down mode selected by bits 6..7 of ARM7 `HALTCNT` (`0x04000301`).
 */
enum class PowerDownMode : u8 {
    None = 0,
    GbaMode = 1,
    Halt = 2,
    Sleep = 3,
};

/**
 * @brief Non-owning callback triggered when ARM7 `HALTCNT` requests Halt or Sleep.
 */
struct HaltHandler {
    void* context = nullptr;
    void (*on_power_down)(void* context, PowerDownMode mode) = nullptr;
};

/**
 * @brief Binds any CPU exposing `set_halted(bool)` to halt on `Halt` or `Sleep` requests.
 */
template <typename Cpu>
[[nodiscard]] constexpr HaltHandler make_halt_handler(Cpu* cpu) noexcept {
    return HaltHandler{
        cpu,
        [](void* context, PowerDownMode mode) {
            if (context != nullptr &&
                (mode == PowerDownMode::Halt || mode == PowerDownMode::Sleep)) {
                static_cast<Cpu*>(context)->set_halted(true);
            }
        },
    };
}

/**
 * @brief Nintendo DS system control registers `POSTFLG` (`0x04000300`) and `HALTCNT` (`0x04000301`).
 *
 * Responsibility:
 *   Enforces `POSTFLG` bit 0 write-once (`1 -> 0` immunity) and ARM9 bit 1 R/W vs ARM7 bit 0
 *   masking, and routes ARM7 `HALTCNT` Halt/Sleep requests to the attached `HaltHandler`.
 */
class SystemControlMmio final : public memory::MmioDevice {
public:
    static constexpr Address kMmioStart = 0x04000300U;
    static constexpr Address kMmioEnd = 0x04000301U;
    static constexpr Address kRegPostFlg = 0x04000300U;
    static constexpr Address kRegHaltCnt = 0x04000301U;

    /**
     * @brief Constructs system control MMIO for `cpu` with an optional `HALTCNT` callback.
     */
    explicit SystemControlMmio(CpuKind cpu, HaltHandler handler = {}) noexcept;

    /**
     * @brief Resets `POSTFLG` and the last recorded power-down mode to zero/None.
     */
    void reset() noexcept;

    [[nodiscard]] u8 post_flag() const noexcept;
    [[nodiscard]] PowerDownMode last_power_down_mode() const noexcept;

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    CpuKind cpu_;
    HaltHandler handler_{};
    u8 postflg_ = 0U;
    PowerDownMode last_power_down_mode_ = PowerDownMode::None;
};

}  // namespace elmasds::hardware::interrupts
