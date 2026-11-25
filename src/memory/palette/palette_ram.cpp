#include "memory/palette/palette_ram.hpp"

#include "memory/memory_region.hpp"

namespace elmasds::memory {

void PaletteRam::reset() noexcept {
    storage_.fill(0U);
}

u8 PaletteRam::read8(Address offset) const noexcept {
    return read_buffer8(storage_, offset);
}

u16 PaletteRam::read16(Address offset) const noexcept {
    return read_buffer16(storage_, offset);
}

u32 PaletteRam::read32(Address offset) const noexcept {
    return read_buffer32(storage_, offset);
}

void PaletteRam::write8(Address /*offset*/, u8 /*value*/) noexcept {
    // NDS hardware ignores 8-bit writes to Standard Palette RAM.
}

void PaletteRam::write16(Address offset, u16 value) noexcept {
    write_buffer16(storage_, offset, value);
}

void PaletteRam::write32(Address offset, u32 value) noexcept {
    write_buffer32(storage_, offset, value);
}

}  // namespace elmasds::memory
