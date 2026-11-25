#include "memory/arm7_wram/arm7_wram.hpp"

#include "memory/memory_region.hpp"

#include <algorithm>

namespace elmasds::memory {

Arm7Wram::Arm7Wram() : storage_(kSizeBytes, 0U) {}

void Arm7Wram::reset() noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
}

u8 Arm7Wram::read8(Address offset) const noexcept {
    return read_buffer8(storage_, offset);
}

u16 Arm7Wram::read16(Address offset) const noexcept {
    return read_buffer16(storage_, offset);
}

u32 Arm7Wram::read32(Address offset) const noexcept {
    return read_buffer32(storage_, offset);
}

void Arm7Wram::write8(Address offset, u8 value) noexcept {
    write_buffer8(storage_, offset, value);
}

void Arm7Wram::write16(Address offset, u16 value) noexcept {
    write_buffer16(storage_, offset, value);
}

void Arm7Wram::write32(Address offset, u32 value) noexcept {
    write_buffer32(storage_, offset, value);
}

}  // namespace elmasds::memory
