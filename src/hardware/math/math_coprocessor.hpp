#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::hardware::math {

inline constexpr Address kMathMmioStart = 0x04000280U;
inline constexpr Address kMathMmioEnd = 0x040002BFU;
inline constexpr Address kRegDivCnt = 0x04000280U;
inline constexpr Address kRegDivNumer = 0x04000290U;
inline constexpr Address kRegDivDenom = 0x04000298U;
inline constexpr Address kRegDivResult = 0x040002A0U;
inline constexpr Address kRegDivRemResult = 0x040002A8U;
inline constexpr Address kRegSqrtCnt = 0x040002B0U;
inline constexpr Address kRegSqrtResult = 0x040002B4U;
inline constexpr Address kRegSqrtParam = 0x040002B8U;

/**
 * @brief ARM9 hardware integer division and square-root coprocessor (`0x04000280..0x040002BF`).
 *
 * Responsibility:
 *   Implements `DIVCNT`, `DIV_NUMER`, `DIV_DENOM`, `DIV_RESULT`, `DIVREM_RESULT`, `SQRTCNT`,
 *   `SQRT_RESULT`, and `SQRT_PARAM` with synchronous calculation on parameter/control writes,
 *   including 32/32, 64/32, and 64/64 signed division, overflow/divide-by-zero handling, and
 *   exact 32-bit/64-bit unsigned integer square root.
 */
class MathCoprocessor final : public memory::MmioDevice {
public:
    MathCoprocessor() noexcept = default;

    void reset() noexcept;

    [[nodiscard]] u8 read8(Address address) noexcept override;
    [[nodiscard]] u16 read16(Address address) noexcept override;
    [[nodiscard]] u32 read32(Address address) noexcept override;

    void write8(Address address, u8 value) noexcept override;
    void write16(Address address, u16 value) noexcept override;
    void write32(Address address, u32 value) noexcept override;

    [[nodiscard]] u16 divcnt() const noexcept { return divcnt_; }
    [[nodiscard]] i64 div_result() const noexcept { return div_result_; }
    [[nodiscard]] i64 divrem_result() const noexcept { return divrem_result_; }
    [[nodiscard]] u16 sqrtcnt() const noexcept { return sqrtcnt_; }
    [[nodiscard]] u32 sqrt_result() const noexcept { return sqrt_result_; }

private:
    void recompute_division() noexcept;
    void recompute_sqrt() noexcept;

    u16 divcnt_ = 0U;
    u64 div_numer_ = 0ULL;
    u64 div_denom_ = 0ULL;
    i64 div_result_ = 0;
    i64 divrem_result_ = 0;

    u16 sqrtcnt_ = 0U;
    u64 sqrt_param_ = 0ULL;
    u32 sqrt_result_ = 0U;
};

}  // namespace elmasds::hardware::math
