#include "memory/vram/vram_controller.hpp"

#include "memory/memory_region.hpp"

#include <algorithm>

namespace elmasds::memory {
namespace {

struct BankDescriptor {
    Address size = 0;
    Address lcdc_base = 0;
    u8 mst_mask = 0x07U;
};

constexpr std::array<BankDescriptor, VramController::kBankCount> kBankDescriptors = {{
    {128U * 1024U, 0x06800000U, 0x03U},  // A
    {128U * 1024U, 0x06820000U, 0x03U},  // B
    {128U * 1024U, 0x06840000U, 0x07U},  // C
    {128U * 1024U, 0x06860000U, 0x07U},  // D
    {64U * 1024U, 0x06880000U, 0x07U},   // E
    {16U * 1024U, 0x06890000U, 0x07U},   // F
    {16U * 1024U, 0x06894000U, 0x07U},   // G
    {32U * 1024U, 0x06898000U, 0x03U},   // H
    {16U * 1024U, 0x068A0000U, 0x03U},   // I
}};

[[nodiscard]] Address fg_engine_offset(u8 ofs) noexcept {
    return static_cast<Address>((ofs & 1U) * 0x4000U + ((ofs >> 1U) & 1U) * 0x10000U);
}

[[nodiscard]] Address map_arm9_base(std::size_t index, u8 mst, u8 ofs) noexcept {
    if (mst == 0U) {
        return kBankDescriptors[index].lcdc_base;
    }
    switch (index) {
    case 0U:
    case 1U:
        if (mst == 1U) {
            return 0x06000000U + static_cast<Address>(ofs) * 0x20000U;
        }
        return (mst == 2U) ? (0x06400000U + static_cast<Address>(ofs & 1U) * 0x20000U) : 0U;
    case 2U:
        if (mst == 1U) {
            return 0x06000000U + static_cast<Address>(ofs) * 0x20000U;
        }
        return (mst == 4U) ? 0x06200000U : 0U;
    case 3U:
        if (mst == 1U) {
            return 0x06000000U + static_cast<Address>(ofs) * 0x20000U;
        }
        return (mst == 4U) ? 0x06600000U : 0U;
    case 4U:
        if (mst == 1U) {
            return 0x06000000U;
        }
        return (mst == 2U) ? 0x06400000U : 0U;
    case 5U:
    case 6U:
        if (mst == 1U) {
            return 0x06000000U + fg_engine_offset(ofs);
        }
        return (mst == 2U) ? (0x06400000U + fg_engine_offset(ofs)) : 0U;
    case 7U:
        return (mst == 1U) ? 0x06200000U : 0U;
    case 8U:
        if (mst == 1U) {
            return 0x06208000U;
        }
        return (mst == 2U) ? 0x06600000U : 0U;
    default:
        return 0U;
    }
}

[[nodiscard]] Address mask_arm9_region(Address address) noexcept {
    if (address >= 0x06800000U) {
        return 0x06800000U | (address & 0x000FFFFFU);
    }
    return 0x06000000U | (address & 0x0067FFFFU);
}

}  // namespace

VramController::VramController() {
    for (std::size_t i = 0; i < kBankCount; ++i) {
        banks_[i].assign(kBankDescriptors[i].size, u8{0});
    }
}

void VramController::reset() noexcept {
    vram_cnt_.fill(0U);
    for (auto& bank : banks_) {
        std::fill(bank.begin(), bank.end(), u8{0});
    }
}

void VramController::write_vram_cnt(VramBankId bank, u8 value) noexcept {
    vram_cnt_[static_cast<std::size_t>(bank)] = value;
}

u8 VramController::read_vram_cnt(VramBankId bank) const noexcept {
    return vram_cnt_[static_cast<std::size_t>(bank)];
}

u8 VramController::read_vram_stat() const noexcept {
    const bool c_arm7 =
        (vram_cnt_[2U] & 0x80U) != 0U && ((vram_cnt_[2U] & 0x07U) == 2U);
    const bool d_arm7 =
        (vram_cnt_[3U] & 0x80U) != 0U && ((vram_cnt_[3U] & 0x07U) == 2U);
    return static_cast<u8>((c_arm7 ? 0x01U : 0x00U) | (d_arm7 ? 0x02U : 0x00U));
}

VramController::BankMapping VramController::arm9_mapping(std::size_t index,
                                                         Address address) const noexcept {
    (void)address;
    const u8 cnt = vram_cnt_[index];
    if ((cnt & 0x80U) == 0U) {
        return BankMapping{};
    }
    const u8 mst = static_cast<u8>(cnt & kBankDescriptors[index].mst_mask);
    const u8 ofs = static_cast<u8>((cnt >> 3U) & 0x03U);
    const Address base = map_arm9_base(index, mst, ofs);
    if (base == 0U) {
        return BankMapping{};
    }
    return BankMapping{base, kBankDescriptors[index].size};
}

VramController::BankMapping VramController::arm7_mapping(std::size_t index) const noexcept {
    if (index != 2U && index != 3U) {
        return BankMapping{};
    }
    const u8 cnt = vram_cnt_[index];
    if ((cnt & 0x80U) == 0U || (cnt & 0x07U) != 2U) {
        return BankMapping{};
    }
    const Address ofs = static_cast<Address>((cnt >> 3U) & 0x01U);
    return BankMapping{0x06000000U + ofs * 0x20000U, kBankDescriptors[index].size};
}

bool VramController::matches_arm9(std::size_t index,
                                  Address address,
                                  Address& bank_offset) const noexcept {
    const BankMapping map = arm9_mapping(index, address);
    if (map.size == 0U) {
        return false;
    }
    const Address masked = mask_arm9_region(address);
    if (masked < map.start || masked >= (map.start + map.size)) {
        return false;
    }
    bank_offset = masked - map.start;
    return true;
}

bool VramController::matches_arm7(std::size_t index,
                                  Address address,
                                  Address& bank_offset) const noexcept {
    const BankMapping map = arm7_mapping(index);
    if (map.size == 0U) {
        return false;
    }
    const Address masked = 0x06000000U | (address & 0x0003FFFFU);
    if (masked < map.start || masked >= (map.start + map.size)) {
        return false;
    }
    bank_offset = masked - map.start;
    return true;
}

u8 VramController::read_arm9_8(Address address) const noexcept {
    u8 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 0; i < kBankCount; ++i) {
        if (matches_arm9(i, address, offset)) {
            result |= read_buffer8(banks_[i], offset);
        }
    }
    return result;
}

u16 VramController::read_arm9_16(Address address) const noexcept {
    u16 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 0; i < kBankCount; ++i) {
        if (matches_arm9(i, address, offset)) {
            result |= read_buffer16(banks_[i], offset);
        }
    }
    return result;
}

u32 VramController::read_arm9_32(Address address) const noexcept {
    u32 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 0; i < kBankCount; ++i) {
        if (matches_arm9(i, address, offset)) {
            result |= read_buffer32(banks_[i], offset);
        }
    }
    return result;
}

void VramController::write_arm9_8(Address /*address*/, u8 /*value*/) noexcept {
    // NDS hardware ignores 8-bit CPU writes to ARM9-mapped VRAM.
}

void VramController::write_arm9_16(Address address, u16 value) noexcept {
    Address offset = 0U;
    for (std::size_t i = 0; i < kBankCount; ++i) {
        if (matches_arm9(i, address, offset)) {
            write_buffer16(banks_[i], offset, value);
        }
    }
}

void VramController::write_arm9_32(Address address, u32 value) noexcept {
    Address offset = 0U;
    for (std::size_t i = 0; i < kBankCount; ++i) {
        if (matches_arm9(i, address, offset)) {
            write_buffer32(banks_[i], offset, value);
        }
    }
}

u8 VramController::read_arm7_8(Address address) const noexcept {
    u8 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            result |= read_buffer8(banks_[i], offset);
        }
    }
    return result;
}

u16 VramController::read_arm7_16(Address address) const noexcept {
    u16 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            result |= read_buffer16(banks_[i], offset);
        }
    }
    return result;
}

u32 VramController::read_arm7_32(Address address) const noexcept {
    u32 result = 0U;
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            result |= read_buffer32(banks_[i], offset);
        }
    }
    return result;
}

void VramController::write_arm7_8(Address address, u8 value) noexcept {
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            write_buffer8(banks_[i], offset, value);
        }
    }
}

void VramController::write_arm7_16(Address address, u16 value) noexcept {
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            write_buffer16(banks_[i], offset, value);
        }
    }
}

void VramController::write_arm7_32(Address address, u32 value) noexcept {
    Address offset = 0U;
    for (std::size_t i = 2U; i <= 3U; ++i) {
        if (matches_arm7(i, address, offset)) {
            write_buffer32(banks_[i], offset, value);
        }
    }
}

std::span<const u8> VramController::bank_data(VramBankId bank) const noexcept {
    return banks_[static_cast<std::size_t>(bank)];
}

}  // namespace elmasds::memory
