#include "memory/oam/oam_ram.hpp"

#include "memory/memory_region.hpp"

namespace elmasds::memory {

void OamRam::reset() noexcept {
    storage_.fill(0U);
}

u8 OamRam::read8(Address offset) const noexcept {
    return read_buffer8(storage_, offset);
}

u16 OamRam::read16(Address offset) const noexcept {
    return read_buffer16(storage_, offset);
}

u32 OamRam::read32(Address offset) const noexcept {
    return read_buffer32(storage_, offset);
}

void OamRam::write8(Address /*offset*/, u8 /*value*/) noexcept {
    // NDS hardware ignores 8-bit writes to OAM.
}

void OamRam::write16(Address offset, u16 value) noexcept {
    write_buffer16(storage_, offset, value);
}

void OamRam::write32(Address offset, u32 value) noexcept {
    write_buffer32(storage_, offset, value);
}

}  // namespace elmasds::memory
