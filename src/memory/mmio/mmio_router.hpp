#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <vector>

namespace elmasds::memory {

/**
 * @brief Inclusive MMIO address range bound to a hardware peripheral.
 */
struct MmioRegistration {
    Address start = 0;
    Address end = 0;
    MmioDevice* device = nullptr;
};

/**
 * @brief Dispatches CPU MMIO reads/writes (`0x04000000-0x04FFFFFF`) to registered devices.
 */
class MmioRouter {
public:
    void register_device(const MmioRegistration& registration);
    void clear() noexcept;

    [[nodiscard]] u8 read8(Address address) const;
    [[nodiscard]] u16 read16(Address address) const;
    [[nodiscard]] u32 read32(Address address) const;

    void write8(Address address, u8 value);
    void write16(Address address, u16 value);
    void write32(Address address, u32 value);

private:
    [[nodiscard]] MmioDevice* find_device(Address address) const noexcept;

    std::vector<MmioRegistration> registrations_;
};

}  // namespace elmasds::memory
