#pragma once

#include "primitives/fixed_width_types.hpp"

#include <span>

namespace elmasds::memory {

/**
 * @brief Reads an 8-bit value from a masked little-endian memory buffer.
 */
[[nodiscard]] inline u8 read_buffer8(std::span<const u8> buffer, Address offset) noexcept {
    return buffer[offset % buffer.size()];
}

/**
 * @brief Reads a 16-bit little-endian value from a masked memory buffer.
 */
[[nodiscard]] inline u16 read_buffer16(std::span<const u8> buffer, Address offset) noexcept {
    const Address base = (offset & ~1U) % buffer.size();
    return static_cast<u16>(buffer[base]) | (static_cast<u16>(buffer[base + 1U]) << 8U);
}

/**
 * @brief Reads a 32-bit little-endian value from a masked memory buffer.
 */
[[nodiscard]] inline u32 read_buffer32(std::span<const u8> buffer, Address offset) noexcept {
    const Address base = (offset & ~3U) % buffer.size();
    return static_cast<u32>(buffer[base]) | (static_cast<u32>(buffer[base + 1U]) << 8U) |
           (static_cast<u32>(buffer[base + 2U]) << 16U) |
           (static_cast<u32>(buffer[base + 3U]) << 24U);
}

/**
 * @brief Writes an 8-bit value into a masked little-endian memory buffer.
 */
inline void write_buffer8(std::span<u8> buffer, Address offset, u8 value) noexcept {
    buffer[offset % buffer.size()] = value;
}

/**
 * @brief Writes a 16-bit little-endian value into a masked memory buffer.
 */
inline void write_buffer16(std::span<u8> buffer, Address offset, u16 value) noexcept {
    const Address base = (offset & ~1U) % buffer.size();
    buffer[base] = static_cast<u8>(value & 0xFFU);
    buffer[base + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

/**
 * @brief Writes a 32-bit little-endian value into a masked memory buffer.
 */
inline void write_buffer32(std::span<u8> buffer, Address offset, u32 value) noexcept {
    const Address base = (offset & ~3U) % buffer.size();
    buffer[base] = static_cast<u8>(value & 0xFFU);
    buffer[base + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    buffer[base + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
    buffer[base + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
}

}  // namespace elmasds::memory
