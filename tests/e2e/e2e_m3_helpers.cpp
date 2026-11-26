#include "e2e/e2e_m3_helpers.hpp"

#include "primitives/status_result.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace elmasds::e2e::m3 {
namespace {

constexpr std::size_t kHeaderCrcOffset = 0x15EU;

void recompute_header_crc(std::vector<u8>& rom) {
    if (rom.size() < kNdsMinHeaderSize) {
        throw std::out_of_range("ROM buffer smaller than minimum 0x160-byte NDS header");
    }
    const u16 crc = compute_crc16(std::span<const u8>(rom).subspan(0U, kHeaderCrcOffset));
    write_le16(rom, kHeaderCrcOffset, crc);
}

}  // namespace

u16 read_le16(std::span<const u8> data, std::size_t offset) {
    if (offset > data.size() || (data.size() - offset) < 2U) {
        throw std::out_of_range("read_le16 out of bounds");
    }
    return static_cast<u16>(data[offset]) |
           static_cast<u16>(static_cast<u16>(data[offset + 1U]) << 8U);
}

u32 read_le32(std::span<const u8> data, std::size_t offset) {
    if (offset > data.size() || (data.size() - offset) < 4U) {
        throw std::out_of_range("read_le32 out of bounds");
    }
    return static_cast<u32>(data[offset]) | (static_cast<u32>(data[offset + 1U]) << 8U) |
           (static_cast<u32>(data[offset + 2U]) << 16U) |
           (static_cast<u32>(data[offset + 3U]) << 24U);
}

void write_le16(std::span<u8> data, std::size_t offset, u16 value) {
    if (offset > data.size() || (data.size() - offset) < 2U) {
        throw std::out_of_range("write_le16 out of bounds");
    }
    data[offset] = static_cast<u8>(value & 0xFFU);
    data[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

void write_le32(std::span<u8> data, std::size_t offset, u32 value) {
    if (offset > data.size() || (data.size() - offset) < 4U) {
        throw std::out_of_range("write_le32 out of bounds");
    }
    data[offset] = static_cast<u8>(value & 0xFFU);
    data[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
}

void patch_u16_and_recompute_header_crc(std::vector<u8>& rom, std::size_t offset, u16 value) {
    write_le16(rom, offset, value);
    recompute_header_crc(rom);
}

void patch_u32_and_recompute_header_crc(std::vector<u8>& rom, std::size_t offset, u32 value) {
    write_le32(rom, offset, value);
    recompute_header_crc(rom);
}

std::vector<u32> make_idle_words() {
    return {0xEAFFFFFEU};
}

NdsRomSpec make_base_rom_spec(std::vector<u32> arm9_words, std::vector<u32> arm7_words) {
    NdsRomSpec spec{};
    spec.arm9_rom_offset = kNdsDefaultHeaderSize;
    spec.arm9_entry_addr = kArm9DefaultBase;
    spec.arm9_ram_addr = kArm9DefaultBase;
    spec.arm9_code = words_to_le_bytes(arm9_words);

    const u32 arm9_end =
        spec.arm9_rom_offset + static_cast<u32>(spec.arm9_code.size());
    const u32 aligned_arm9_end = (arm9_end + 0x1FFU) & ~0x1FFU;
    spec.arm7_rom_offset = std::max<u32>(kNdsSecureAreaEnd, aligned_arm9_end);
    spec.arm7_entry_addr = kArm7DefaultBase;
    spec.arm7_ram_addr = kArm7DefaultBase;
    spec.arm7_code = words_to_le_bytes(arm7_words);
    return spec;
}

void boot_rom_and_step(EmulatorE2eHarness& harness, const NdsRomSpec& spec, TickCount ticks) {
    const std::vector<u8> rom = build_valid_nds_rom(spec);
    boot_bytes_and_step(harness, rom, ticks);
}

void boot_bytes_and_step(EmulatorE2eHarness& harness,
                         std::span<const u8> rom_bytes,
                         TickCount ticks) {
    const Status load_status = harness.load_rom_bytes(rom_bytes);
    ASSERT_TRUE(load_status.is_ok()) << load_status.error().message;
    const Status boot_status = harness.direct_boot();
    ASSERT_TRUE(boot_status.is_ok()) << boot_status.error().message;
    harness.step_ticks(ticks);
}

}  // namespace elmasds::e2e::m3
