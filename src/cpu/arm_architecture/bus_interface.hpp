#pragma once

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "primitives/bit_operations.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Abstract memory-access boundary isolating ARM cores from concrete MemoryBus internals.
 *
 * Responsibility:
 *   Provides byte, halfword, and word reads/writes routed through the console MemoryBus.
 * Dependencies:
 *   `fixed_width_types.hpp`.
 */
class CpuBusInterface {
public:
    virtual ~CpuBusInterface() = default;

    [[nodiscard]] virtual u8 read8(Address address) = 0;
    [[nodiscard]] virtual u16 read16(Address address) = 0;
    [[nodiscard]] virtual u32 read32(Address address) = 0;

    /**
     * @brief Fetches a 16-bit Thumb instruction from `address` (defaults to `read16`).
     */
    [[nodiscard]] virtual u16 fetch16(Address address) { return read16(address); }

    /**
     * @brief Fetches a 32-bit ARM instruction from `address` (defaults to `read32`).
     */
    [[nodiscard]] virtual u32 fetch32(Address address) { return read32(address); }

    virtual void write8(Address address, u8 value) = 0;
    virtual void write16(Address address, u16 value) = 0;
    virtual void write32(Address address, u32 value) = 0;
};

/**
 * @brief Performs an ARM LDR word read with unaligned byte rotation (`addr & ~3`, ROR `(addr & 3) * 8`).
 */
[[nodiscard]] inline u32 read_word_rotated(CpuBusInterface& bus, Address address) {
    const u32 aligned = bus.read32(address & ~3U);
    const u32 misalignment = address & 3U;
    return misalignment == 0U ? aligned : bits::rotate_right_32(aligned, misalignment * 8U);
}

/**
 * @brief Performs an ARM LDRH halfword read honoring ARMv4T unaligned rotation vs ARMv5TE alignment.
 */
[[nodiscard]] inline u16 read_halfword_unsigned(CpuBusInterface& bus,
                                                Address address,
                                                ArmArchitecture architecture) {
    const u16 aligned = bus.read16(address & ~1U);
    if (architecture == ArmArchitecture::ArmV4T && (address & 1U) != 0U) {
        return static_cast<u16>(bits::rotate_right_32(aligned, 8U));
    }
    return aligned;
}

}  // namespace elmasds::cpu
