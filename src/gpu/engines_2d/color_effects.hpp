#pragma once

#include "primitives/fixed_width_types.hpp"

#include <algorithm>
#include <span>

namespace elmasds::gpu {

/**
 * @brief Converts a 15-bit Nintendo DS `BGR555` color into `0xFFRRGGBB` (`ARGB8888`).
 */
[[nodiscard]] constexpr u32 bgr555_to_argb8888(u16 color) noexcept {
    const u32 r5 = color & 0x1FU;
    const u32 g5 = (color >> 5U) & 0x1FU;
    const u32 b5 = (color >> 10U) & 0x1FU;

    const u32 r8 = (r5 << 3U) | (r5 >> 2U);
    const u32 g8 = (g5 << 3U) | (g5 >> 2U);
    const u32 b8 = (b5 << 3U) | (b5 >> 2U);
    return 0xFF000000U | (r8 << 16U) | (g8 << 8U) | b8;
}

/**
 * @brief Alpha-blends two `ARGB8888` pixels using NDS 0..16 weights `eva` and `evb`.
 */
[[nodiscard]] constexpr u32 blend_argb8888(u32 top, u32 bottom, u32 eva, u32 evb) noexcept {
    const u32 wa = std::min(eva, 16U);
    const u32 wb = std::min(evb, 16U);

    const u32 r = std::min(255U, (((top >> 16U) & 0xFFU) * wa + ((bottom >> 16U) & 0xFFU) * wb) / 16U);
    const u32 g = std::min(255U, (((top >> 8U) & 0xFFU) * wa + ((bottom >> 8U) & 0xFFU) * wb) / 16U);
    const u32 b = std::min(255U, ((top & 0xFFU) * wa + (bottom & 0xFFU) * wb) / 16U);
    return 0xFF000000U | (r << 16U) | (g << 8U) | b;
}

/**
 * @brief Applies NDS brightness increase (`mode == 1`) or decrease (`mode == 2`) with weight `evy` (0..16).
 */
[[nodiscard]] constexpr u32 fade_argb8888(u32 color, u32 mode, u32 evy) noexcept {
    const u32 y = std::min(evy, 16U);
    u32 r = (color >> 16U) & 0xFFU;
    u32 g = (color >> 8U) & 0xFFU;
    u32 b = color & 0xFFU;

    if (mode == 1U) {
        r += ((255U - r) * y) / 16U;
        g += ((255U - g) * y) / 16U;
        b += ((255U - b) * y) / 16U;
    } else if (mode == 2U) {
        r -= (r * y) / 16U;
        g -= (g * y) / 16U;
        b -= (b * y) / 16U;
    }
    return 0xFF000000U | (r << 16U) | (g << 8U) | b;
}

/**
 * @brief Applies per-engine `MASTER_BRIGHT` register to a 256-pixel scanline in place.
 */
inline void apply_master_brightness(std::span<u32> scanline, u16 master_bright) noexcept {
    const u32 mode = (master_bright >> 14U) & 0x03U;
    if (mode != 1U && mode != 2U) {
        return;
    }
    const u32 factor = std::min<u32>(master_bright & 0x1FU, 16U);
    for (u32& pixel : scanline) {
        pixel = fade_argb8888(pixel, mode, factor);
    }
}

}  // namespace elmasds::gpu
