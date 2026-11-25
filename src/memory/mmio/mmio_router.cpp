#include "memory/mmio/mmio_router.hpp"

namespace elmasds::memory {

void MmioRouter::register_device(const MmioRegistration& registration) {
    if (registration.device == nullptr || registration.start > registration.end) {
        return;
    }
    registrations_.push_back(registration);
}

void MmioRouter::clear() noexcept {
    registrations_.clear();
}

MmioDevice* MmioRouter::find_device(Address address) const noexcept {
    for (const auto& entry : registrations_) {
        if (address >= entry.start && address <= entry.end) {
            return entry.device;
        }
    }
    return nullptr;
}

u8 MmioRouter::read8(Address address) const {
    MmioDevice* device = find_device(address);
    return device != nullptr ? device->read8(address) : 0U;
}

u16 MmioRouter::read16(Address address) const {
    MmioDevice* device = find_device(address);
    return device != nullptr ? device->read16(address) : 0U;
}

u32 MmioRouter::read32(Address address) const {
    MmioDevice* device = find_device(address);
    return device != nullptr ? device->read32(address) : 0U;
}

void MmioRouter::write8(Address address, u8 value) {
    MmioDevice* device = find_device(address);
    if (device != nullptr) {
        device->write8(address, value);
    }
}

void MmioRouter::write16(Address address, u16 value) {
    MmioDevice* device = find_device(address);
    if (device != nullptr) {
        device->write16(address, value);
    }
}

void MmioRouter::write32(Address address, u32 value) {
    MmioDevice* device = find_device(address);
    if (device != nullptr) {
        device->write32(address, value);
    }
}

}  // namespace elmasds::memory
