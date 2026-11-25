#include "memory/shared_wram/shared_wram.hpp"

#include "memory/memory_region.hpp"

namespace elmasds::memory {
namespace {

[[nodiscard]] Address slice_offset(Address offset, std::size_t slice_size) noexcept {
    return offset & static_cast<Address>(slice_size - 1U);
}

}  // namespace

void SharedWram::reset() noexcept {
    storage_.fill(0U);
    wram_cnt_ = 0U;
}

std::span<const u8> SharedWram::arm9_slice() const noexcept {
    switch (wram_cnt_) {
    case 0U:
        return storage_;
    case 1U:
        return std::span<const u8>(storage_).subspan(kBankSize, kBankSize);
    case 2U:
        return std::span<const u8>(storage_).subspan(0U, kBankSize);
    default:
        return {};
    }
}

std::span<u8> SharedWram::arm9_mutable_slice() noexcept {
    switch (wram_cnt_) {
    case 0U:
        return storage_;
    case 1U:
        return std::span<u8>(storage_).subspan(kBankSize, kBankSize);
    case 2U:
        return std::span<u8>(storage_).subspan(0U, kBankSize);
    default:
        return {};
    }
}

std::span<const u8> SharedWram::arm7_slice() const noexcept {
    switch (wram_cnt_) {
    case 1U:
        return std::span<const u8>(storage_).subspan(0U, kBankSize);
    case 2U:
        return std::span<const u8>(storage_).subspan(kBankSize, kBankSize);
    case 3U:
        return storage_;
    default:
        return {};
    }
}

std::span<u8> SharedWram::arm7_mutable_slice() noexcept {
    switch (wram_cnt_) {
    case 1U:
        return std::span<u8>(storage_).subspan(0U, kBankSize);
    case 2U:
        return std::span<u8>(storage_).subspan(kBankSize, kBankSize);
    case 3U:
        return storage_;
    default:
        return {};
    }
}

u8 SharedWram::read_arm9_8(Address offset) const noexcept {
    const auto slice = arm9_slice();
    return slice.empty() ? 0U : read_buffer8(slice, slice_offset(offset, slice.size()));
}

u16 SharedWram::read_arm9_16(Address offset) const noexcept {
    const auto slice = arm9_slice();
    return slice.empty() ? 0U : read_buffer16(slice, slice_offset(offset, slice.size()));
}

u32 SharedWram::read_arm9_32(Address offset) const noexcept {
    const auto slice = arm9_slice();
    return slice.empty() ? 0U : read_buffer32(slice, slice_offset(offset, slice.size()));
}

void SharedWram::write_arm9_8(Address offset, u8 value) noexcept {
    const auto slice = arm9_mutable_slice();
    if (!slice.empty()) {
        write_buffer8(slice, slice_offset(offset, slice.size()), value);
    }
}

void SharedWram::write_arm9_16(Address offset, u16 value) noexcept {
    const auto slice = arm9_mutable_slice();
    if (!slice.empty()) {
        write_buffer16(slice, slice_offset(offset, slice.size()), value);
    }
}

void SharedWram::write_arm9_32(Address offset, u32 value) noexcept {
    const auto slice = arm9_mutable_slice();
    if (!slice.empty()) {
        write_buffer32(slice, slice_offset(offset, slice.size()), value);
    }
}

u8 SharedWram::read_arm7_8(Address offset) const noexcept {
    const auto slice = arm7_slice();
    return slice.empty() ? 0U : read_buffer8(slice, slice_offset(offset, slice.size()));
}

u16 SharedWram::read_arm7_16(Address offset) const noexcept {
    const auto slice = arm7_slice();
    return slice.empty() ? 0U : read_buffer16(slice, slice_offset(offset, slice.size()));
}

u32 SharedWram::read_arm7_32(Address offset) const noexcept {
    const auto slice = arm7_slice();
    return slice.empty() ? 0U : read_buffer32(slice, slice_offset(offset, slice.size()));
}

void SharedWram::write_arm7_8(Address offset, u8 value) noexcept {
    const auto slice = arm7_mutable_slice();
    if (!slice.empty()) {
        write_buffer8(slice, slice_offset(offset, slice.size()), value);
    }
}

void SharedWram::write_arm7_16(Address offset, u16 value) noexcept {
    const auto slice = arm7_mutable_slice();
    if (!slice.empty()) {
        write_buffer16(slice, slice_offset(offset, slice.size()), value);
    }
}

void SharedWram::write_arm7_32(Address offset, u32 value) noexcept {
    const auto slice = arm7_mutable_slice();
    if (!slice.empty()) {
        write_buffer32(slice, slice_offset(offset, slice.size()), value);
    }
}

}  // namespace elmasds::memory
