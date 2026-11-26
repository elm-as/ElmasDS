#include "memory/memory_bus/arm7_memory_bus.hpp"

namespace elmasds::memory {

Arm7MemoryBus::Arm7MemoryBus(MemorySystem& system) noexcept : system_(system) {}

bool Arm7MemoryBus::use_shared_wram(Address address) const noexcept {
    return (address < 0x03800000U) && system_.shared_wram().arm7_has_mapping();
}

u8 Arm7MemoryBus::read_mmio_8(Address address) const {
    if (address == 0x04000240U) {
        return system_.vram().read_vram_stat();
    }
    if (address == 0x04000241U) {
        return system_.shared_wram().wram_cnt();
    }
    return system_.arm7_mmio().read8(address);
}

u16 Arm7MemoryBus::read_mmio_16(Address address) const {
    if (address == 0x04000240U) {
        return static_cast<u16>(system_.vram().read_vram_stat()) |
               (static_cast<u16>(system_.shared_wram().wram_cnt()) << 8U);
    }
    return system_.arm7_mmio().read16(address);
}

u32 Arm7MemoryBus::read_mmio_32(Address address) const {
    if (address == 0x04000240U) {
        return static_cast<u32>(read_mmio_16(address));
    }
    return system_.arm7_mmio().read32(address);
}

u8 Arm7MemoryBus::read8(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    if ((address & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        return system_.arm7_wram().read8(address);
    }
    if ((address >> 24U) == 0x02U) {
        accumulated_waitstates_ += 4U;
        return system_.main_ram().read8(address);
    }
    switch (address >> 24U) {
    case 0x00U:
        accumulated_waitstates_ += 1U;
        return system_.bios7().read8(address, current_pc_);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return use_shared_wram(address) ? system_.shared_wram().read_arm7_8(address)
                                        : system_.arm7_wram().read8(address);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_8(address);
    case 0x06U:
        accumulated_waitstates_ += 1U;
        return system_.vram().read_arm7_8(address);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

u16 Arm7MemoryBus::read16(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    const Address aligned = address & ~1U;
    if ((aligned & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        return system_.arm7_wram().read16(aligned);
    }
    if ((aligned >> 24U) == 0x02U) {
        accumulated_waitstates_ += 4U;
        return system_.main_ram().read16(aligned);
    }
    switch (aligned >> 24U) {
    case 0x00U:
        accumulated_waitstates_ += 1U;
        return system_.bios7().read16(aligned, current_pc_);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return use_shared_wram(aligned) ? system_.shared_wram().read_arm7_16(aligned)
                                        : system_.arm7_wram().read16(aligned);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_16(aligned);
    case 0x06U:
        accumulated_waitstates_ += 1U;
        return system_.vram().read_arm7_16(aligned);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

u32 Arm7MemoryBus::read32(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    const Address aligned = address & ~3U;
    if ((aligned & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        return system_.arm7_wram().read32(aligned);
    }
    if ((aligned >> 24U) == 0x02U) {
        accumulated_waitstates_ += 5U;
        return system_.main_ram().read32(aligned);
    }
    switch (aligned >> 24U) {
    case 0x00U:
        accumulated_waitstates_ += 1U;
        return system_.bios7().read32(aligned, current_pc_);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return use_shared_wram(aligned) ? system_.shared_wram().read_arm7_32(aligned)
                                        : system_.arm7_wram().read32(aligned);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_32(aligned);
    case 0x06U:
        accumulated_waitstates_ += 1U;
        return system_.vram().read_arm7_32(aligned);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

u16 Arm7MemoryBus::fetch16(Address address) {
    current_pc_ = address & ~1U;
    if ((current_pc_ & 0xFF800000U) == 0x03800000U) {
        primitives::ScopedBusSample bus_sample(profiler_);
        accumulated_waitstates_ += 1U;
        return system_.arm7_wram().read16(current_pc_);
    }
    if ((current_pc_ >> 24U) == 0x02U) {
        primitives::ScopedBusSample bus_sample(profiler_);
        accumulated_waitstates_ += 4U;
        return system_.main_ram().read16(current_pc_);
    }
    return read16(current_pc_);
}

u32 Arm7MemoryBus::fetch32(Address address) {
    current_pc_ = address & ~3U;
    if ((current_pc_ & 0xFF800000U) == 0x03800000U) {
        primitives::ScopedBusSample bus_sample(profiler_);
        accumulated_waitstates_ += 1U;
        return system_.arm7_wram().read32(current_pc_);
    }
    if ((current_pc_ >> 24U) == 0x02U) {
        primitives::ScopedBusSample bus_sample(profiler_);
        accumulated_waitstates_ += 5U;
        return system_.main_ram().read32(current_pc_);
    }
    return read32(current_pc_);
}

void Arm7MemoryBus::write8(Address address, u8 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    if ((address & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        system_.arm7_wram().write8(address, value);
        return;
    }
    if ((address >> 24U) == 0x02U) {
        accumulated_waitstates_ += 4U;
        system_.main_ram().write8(address, value);
        return;
    }
    switch (address >> 24U) {
    case 0x03U:
        accumulated_waitstates_ += 1U;
        if (use_shared_wram(address)) {
            system_.shared_wram().write_arm7_8(address, value);
        } else {
            system_.arm7_wram().write8(address, value);
        }
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        system_.arm7_mmio().write8(address, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 1U;
        system_.vram().write_arm7_8(address, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

void Arm7MemoryBus::write16(Address address, u16 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    const Address aligned = address & ~1U;
    if ((aligned & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        system_.arm7_wram().write16(aligned, value);
        return;
    }
    if ((aligned >> 24U) == 0x02U) {
        accumulated_waitstates_ += 4U;
        system_.main_ram().write16(aligned, value);
        return;
    }
    switch (aligned >> 24U) {
    case 0x03U:
        accumulated_waitstates_ += 1U;
        if (use_shared_wram(aligned)) {
            system_.shared_wram().write_arm7_16(aligned, value);
        } else {
            system_.arm7_wram().write16(aligned, value);
        }
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        system_.arm7_mmio().write16(aligned, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 1U;
        system_.vram().write_arm7_16(aligned, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

void Arm7MemoryBus::write32(Address address, u32 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    const Address aligned = address & ~3U;
    if ((aligned & 0xFF800000U) == 0x03800000U) {
        accumulated_waitstates_ += 1U;
        system_.arm7_wram().write32(aligned, value);
        return;
    }
    if ((aligned >> 24U) == 0x02U) {
        accumulated_waitstates_ += 5U;
        system_.main_ram().write32(aligned, value);
        return;
    }
    switch (aligned >> 24U) {
    case 0x03U:
        accumulated_waitstates_ += 1U;
        if (use_shared_wram(aligned)) {
            system_.shared_wram().write_arm7_32(aligned, value);
        } else {
            system_.arm7_wram().write32(aligned, value);
        }
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        system_.arm7_mmio().write32(aligned, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 1U;
        system_.vram().write_arm7_32(aligned, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

}  // namespace elmasds::memory
