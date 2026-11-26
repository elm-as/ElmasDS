#include "memory/bios/bios_memory.hpp"

#include "memory/memory_region.hpp"

#include <algorithm>

namespace elmasds::memory {

BiosMemory::BiosMemory(std::size_t size_bytes, bool enforce_pc_protection)
    : storage_(size_bytes, 0U), enforce_pc_protection_(enforce_pc_protection) {}

Status BiosMemory::load(std::span<const u8> image) noexcept {
    if (image.size() != storage_.size()) {
        return Status::err(ErrorCode::InvalidArgument, "BIOS image size mismatch.");
    }
    std::copy(image.begin(), image.end(), storage_.begin());
    loaded_ = true;
    return Status::ok();
}

void BiosMemory::install_hle_stub(std::span<const u8> image) noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
    const std::size_t count = std::min(image.size(), storage_.size());
    std::copy_n(image.begin(), count, storage_.begin());
    loaded_ = false;
}

void BiosMemory::clear() noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
    loaded_ = false;
}

bool BiosMemory::is_access_allowed(Address offset, Address current_pc) const noexcept {
    if (offset >= storage_.size()) {
        return false;
    }
    if (!enforce_pc_protection_) {
        return true;
    }
    return current_pc < storage_.size();
}

u8 BiosMemory::read8(Address offset, Address current_pc) const noexcept {
    return is_access_allowed(offset, current_pc) ? read_buffer8(storage_, offset) : 0U;
}

u16 BiosMemory::read16(Address offset, Address current_pc) const noexcept {
    return is_access_allowed(offset, current_pc) ? read_buffer16(storage_, offset) : 0U;
}

u32 BiosMemory::read32(Address offset, Address current_pc) const noexcept {
    return is_access_allowed(offset, current_pc) ? read_buffer32(storage_, offset) : 0U;
}

}  // namespace elmasds::memory
