#pragma once

#include "primitives/fixed_width_types.hpp"

#include <bit>
#include <cassert>

namespace elmasds::bits {

/**
 * @brief Extracts a contiguous bitfield `[lsb, lsb + width - 1]` from a 32-bit word.
 * @param value Source 32-bit unsigned value.
 * @param lsb Zero-based index of the least significant bit (0..31).
 * @param width Number of bits to extract (1..32, with `lsb + width <= 32`).
 * @return Right-aligned extracted bits.
 */
[[nodiscard]] constexpr u32 extract_bits(u32 value, u32 lsb, u32 width) noexcept {
    assert(width > 0 && width <= 32);
    assert(lsb < 32 && (lsb + width) <= 32);
    if (width == 32) {
        return value;
    }
    const u32 mask = (1U << width) - 1U;
    return (value >> lsb) & mask;
}

/**
 * @brief Tests whether a single bit is set in a 32-bit word.
 * @param value Source 32-bit unsigned value.
 * @param bit_index Zero-based bit index (0..31).
 * @return True if bit `bit_index` equals 1.
 */
[[nodiscard]] constexpr bool test_bit(u32 value, u32 bit_index) noexcept {
    assert(bit_index < 32);
    return ((value >> bit_index) & 1U) != 0U;
}

/**
 * @brief Returns a copy of `value` with bit `bit_index` set to `enabled`.
 * @param value Source 32-bit unsigned value.
 * @param bit_index Zero-based bit index (0..31).
 * @param enabled Target state for the bit.
 * @return Updated 32-bit value without mutating inputs.
 */
[[nodiscard]] constexpr u32 write_bit(u32 value, u32 bit_index, bool enabled) noexcept {
    assert(bit_index < 32);
    const u32 mask = 1U << bit_index;
    return enabled ? (value | mask) : (value & ~mask);
}

/**
 * @brief Sign-extends a `width`-bit two's complement value into a signed 32-bit integer.
 * @param value Raw bit pattern in the lower `width` bits.
 * @param width Bit width of the signed value (1..32).
 * @return Sign-extended 32-bit signed integer.
 */
[[nodiscard]] constexpr i32 sign_extend(u32 value, u32 width) noexcept {
    assert(width > 0 && width <= 32);
    if (width == 32) {
        return static_cast<i32>(value);
    }
    const u32 shift = 32U - width;
    return static_cast<i32>(value << shift) >> static_cast<i32>(shift);
}

/**
 * @brief Performs a 32-bit bitwise right rotation.
 * @param value Input 32-bit word.
 * @param amount Rotation amount in bits (normalized modulo 32).
 * @return Rotated 32-bit word.
 */
[[nodiscard]] constexpr u32 rotate_right_32(u32 value, u32 amount) noexcept {
    return std::rotr(value, static_cast<int>(amount & 31U));
}

/**
 * @brief Counts leading zero bits in a 32-bit word (ARM CLZ semantics).
 * @param value Input 32-bit word.
 * @return Number of leading zeros (`32` when `value == 0`).
 */
[[nodiscard]] constexpr u32 count_leading_zeros_32(u32 value) noexcept {
    return static_cast<u32>(std::countl_zero(value));
}

}  // namespace elmasds::bits
