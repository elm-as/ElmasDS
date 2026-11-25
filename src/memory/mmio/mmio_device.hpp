#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::memory {

/**
 * @brief Interface implemented by hardware peripherals mapped into MMIO space (`0x04xxxxxx`).
 */
class MmioDevice {
public:
    virtual ~MmioDevice() = default;

    [[nodiscard]] virtual u8 read8(Address address) = 0;
    [[nodiscard]] virtual u16 read16(Address address) = 0;
    [[nodiscard]] virtual u32 read32(Address address) = 0;

    virtual void write8(Address address, u8 value) = 0;
    virtual void write16(Address address, u16 value) = 0;
    virtual void write32(Address address, u32 value) = 0;
};

}  // namespace elmasds::memory
