#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::gpu {

inline constexpr Address kDisp3dCntStart = 0x04000060U;
inline constexpr Address kDisp3dCntEnd = 0x04000063U;
inline constexpr Address kGxMmioStart = 0x04000320U;
inline constexpr Address kGxMmioEnd = 0x040006A3U;
inline constexpr Address kRegGxFifoStart = 0x04000400U;
inline constexpr Address kRegGxFifoEnd = 0x0400043FU;
inline constexpr Address kRegGxCmdPortStart = 0x04000440U;
inline constexpr Address kRegGxCmdPortEnd = 0x040005DFU;
inline constexpr Address kRegGxStat = 0x04000600U;
inline constexpr Address kRegRamCount = 0x04000604U;
inline constexpr Address kRegClipMtxResultStart = 0x04000640U;
inline constexpr Address kRegClipMtxResultEnd = 0x0400067FU;
inline constexpr Address kRegVecMtxResultStart = 0x04000680U;
inline constexpr Address kRegVecMtxResultEnd = 0x040006A3U;

/// Fixed-point 1.19.12 unit (`1.0 == 0x1000 == 4096`).
inline constexpr i32 kFx12One = 0x1000;

/**
 * @brief 4x4 matrix of signed 1.19.12 fixed-point values stored in row-major order.
 */
struct Matrix4x4 {
    std::array<i32, 16U> m{};

    [[nodiscard]] static constexpr Matrix4x4 identity() noexcept {
        return Matrix4x4{{
            kFx12One, 0,        0,        0,
            0,        kFx12One, 0,        0,
            0,        0,        kFx12One, 0,
            0,        0,        0,        kFx12One,
        }};
    }
};

/**
 * @brief Multiplies two 1.19.12 fixed-point 4x4 matrices (`lhs * rhs` in row-vector convention).
 */
[[nodiscard]] inline Matrix4x4 multiply_matrix(const Matrix4x4& lhs,
                                               const Matrix4x4& rhs) noexcept {
    Matrix4x4 out{};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t col = 0U; col < 4U; ++col) {
            i64 sum = 0;
            for (std::size_t k = 0U; k < 4U; ++k) {
                sum += static_cast<i64>(lhs.m[row * 4U + k]) *
                       static_cast<i64>(rhs.m[k * 4U + col]);
            }
            out.m[row * 4U + col] = static_cast<i32>(sum >> 12);
        }
    }
    return out;
}

/**
 * @brief Primitive topology passed to `BEGIN_VTXS` (`0x40`).
 */
enum class PrimitiveType : u8 {
    Triangles = 0,
    Quads = 1,
    TriangleStrip = 2,
    QuadStrip = 3,
};

/**
 * @brief Transformed clip-space & screen-space vertex emitted by the geometry engine.
 */
struct Vertex3D {
    i32 clip_x = 0;
    i32 clip_y = 0;
    i32 clip_z = 0;
    i32 clip_w = kFx12One;
    i32 screen_x = 0;
    i32 screen_y = 0;
    i32 depth_z = 0;
    u8 r = 31U;
    u8 g = 31U;
    u8 b = 31U;
    i16 tex_s = 0;
    i16 tex_t = 0;
};

/**
 * @brief Submitted triangle or quad stored in the active 3D polygon RAM buffer.
 */
struct Polygon3D {
    std::array<Vertex3D, 4U> vertices{};
    u8 vertex_count = 3U;
    u8 alpha = 31U;
    u8 poly_id = 0U;
    bool back_surface = true;
    bool front_surface = true;
    bool depth_equal = false;
    u32 teximage_param = 0U;
    u16 pltt_base = 0U;
};

/**
 * @brief Viewport rectangle configured via GX command `VIEWPORT` (`0x60`).
 */
struct Viewport3D {
    u8 x1 = 0U;
    u8 y1 = 0U;
    u8 x2 = 255U;
    u8 y2 = 191U;
};

}  // namespace elmasds::gpu
