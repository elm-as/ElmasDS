#pragma once

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Bitmask request for MSR partial PSR updates (fields f, s, x, c).
 */
struct PsrWriteRequest {
    u32 value = 0;
    u8 field_mask = 0;  ///< Bit 0: control [7:0], bit 1: ext [15:8], bit 2: status [23:16], bit 3: flags [31:24].
    ArmArchitecture architecture = ArmArchitecture::ArmV4T;
};

/**
 * @brief Immutable-friendly value wrapper for ARM CPSR / SPSR registers.
 */
class StatusRegister {
public:
    static constexpr u32 kBitN = 31;
    static constexpr u32 kBitZ = 30;
    static constexpr u32 kBitC = 29;
    static constexpr u32 kBitV = 28;
    static constexpr u32 kBitQ = 27;  ///< ARMv5TE sticky saturation flag.
    static constexpr u32 kBitI = 7;   ///< IRQ disable.
    static constexpr u32 kBitF = 6;   ///< FIQ disable.
    static constexpr u32 kBitT = 5;   ///< Thumb execution state.

    constexpr StatusRegister() = default;
    constexpr explicit StatusRegister(u32 raw_value) noexcept : raw_(raw_value) {}

    [[nodiscard]] constexpr u32 raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr bool negative() const noexcept { return bits::test_bit(raw_, kBitN); }
    [[nodiscard]] constexpr bool zero() const noexcept { return bits::test_bit(raw_, kBitZ); }
    [[nodiscard]] constexpr bool carry() const noexcept { return bits::test_bit(raw_, kBitC); }
    [[nodiscard]] constexpr bool overflow() const noexcept { return bits::test_bit(raw_, kBitV); }
    [[nodiscard]] constexpr bool saturation() const noexcept { return bits::test_bit(raw_, kBitQ); }
    [[nodiscard]] constexpr bool irq_disabled() const noexcept { return bits::test_bit(raw_, kBitI); }
    [[nodiscard]] constexpr bool fiq_disabled() const noexcept { return bits::test_bit(raw_, kBitF); }
    [[nodiscard]] constexpr bool thumb() const noexcept { return bits::test_bit(raw_, kBitT); }

    [[nodiscard]] constexpr CpuMode mode() const noexcept {
        return decode_cpu_mode(raw_ & 0x1FU).value_or(CpuMode::Supervisor);
    }

    [[nodiscard]] constexpr StatusRegister with_nzcv(bool n, bool z, bool c, bool v) const noexcept {
        u32 updated = bits::write_bit(raw_, kBitN, n);
        updated = bits::write_bit(updated, kBitZ, z);
        updated = bits::write_bit(updated, kBitC, c);
        updated = bits::write_bit(updated, kBitV, v);
        return StatusRegister(updated);
    }

    [[nodiscard]] constexpr StatusRegister with_saturation(bool q) const noexcept {
        return StatusRegister(bits::write_bit(raw_, kBitQ, q));
    }

    [[nodiscard]] constexpr StatusRegister with_thumb(bool enabled) const noexcept {
        return StatusRegister(bits::write_bit(raw_, kBitT, enabled));
    }

    [[nodiscard]] constexpr StatusRegister with_mode(CpuMode next_mode) const noexcept {
        const u32 cleared = raw_ & ~0x1FU;
        return StatusRegister(cleared | static_cast<u32>(next_mode));
    }

    [[nodiscard]] constexpr StatusRegister with_irq_disabled(bool disabled) const noexcept {
        return StatusRegister(bits::write_bit(raw_, kBitI, disabled));
    }

    [[nodiscard]] constexpr StatusRegister with_fiq_disabled(bool disabled) const noexcept {
        return StatusRegister(bits::write_bit(raw_, kBitF, disabled));
    }

    /**
     * @brief Applies an MSR-masked write while enforcing User-mode privilege and ISA bits.
     */
    [[nodiscard]] constexpr StatusRegister with_msr_write(
        const PsrWriteRequest& request,
        bool allow_control_bits) const noexcept {
        u32 byte_mask = 0;
        if ((request.field_mask & 0x08U) != 0U) {
            byte_mask |= (request.architecture == ArmArchitecture::ArmV5Te) ? 0xF8000000U
                                                                            : 0xF0000000U;
        }
        if (allow_control_bits && (request.field_mask & 0x01U) != 0U) {
            byte_mask |= 0x000000FFU;
        }
        return StatusRegister((raw_ & ~byte_mask) | (request.value & byte_mask));
    }

private:
    u32 raw_ = static_cast<u32>(CpuMode::Supervisor) | (1U << kBitI) | (1U << kBitF);
};

}  // namespace elmasds::cpu
