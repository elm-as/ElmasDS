#include "memory/memory_bus/memory_system.hpp"

namespace elmasds::memory {

MemorySystem::MemorySystem()
    : bios9_(BiosMemory::kArm9BiosSize, false),
      bios7_(BiosMemory::kArm7BiosSize, true) {}

void MemorySystem::reset() noexcept {
    main_ram_.reset();
    shared_wram_.reset();
    arm7_wram_.reset();
    vram_.reset();
    palette_.reset();
    oam_.reset();
}

}  // namespace elmasds::memory
