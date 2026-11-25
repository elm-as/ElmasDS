#include "memory/main_ram/main_ram.hpp"

#include "memory/memory_region.hpp"

#include <algorithm>

namespace elmasds::memory {

MainRam::MainRam() : storage_(kSizeBytes, 0U) {}

void MainRam::reset() noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
}

u8 MainRam::read8(Address offset) const noexcept {
    return read_buffer8(storage_, offset);
}

u16 MainRam::read16(Address offset) const noexcept {
    return read_buffer16(storage_, offset);
}

u32 MainRam::read32(Address offset) const noexcept {
    return read_buffer32(storage_, offset);
}

void MainRam::write8(Address offset, u8 value) noexcept {
    write_buffer8(storage_, offset, value);
}

void MainRam::write16(Address offset, u16 value) noexcept {
    write_buffer16(storage_, offset, value);
}

void MainRam::write32(Address offset, u32 value) noexcept {
    write_buffer32(storage_, offset, value);
}

}  // namespace elmasds::memory
