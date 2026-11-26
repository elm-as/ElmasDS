#include "hardware/math/math_coprocessor.hpp"

#include <limits>

namespace elmasds::hardware::math {

namespace {

[[nodiscard]] u32 integer_sqrt_u64(u64 value) noexcept {
    u64 root = 0ULL;
    u64 bit = 1ULL << 62U;
    while (bit > value) {
        bit >>= 2U;
    }
    while (bit != 0ULL) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1U) + bit;
        } else {
            root >>= 1U;
        }
        bit >>= 2U;
    }
    return static_cast<u32>(root);
}

[[nodiscard]] u64 replace_u32_half(u64 current, bool high_word, u32 value) noexcept {
    if (high_word) {
        return (current & 0x00000000FFFFFFFFULL) | (static_cast<u64>(value) << 32U);
    }
    return (current & 0xFFFFFFFF00000000ULL) | static_cast<u64>(value);
}

}  // namespace

void MathCoprocessor::reset() noexcept {
    divcnt_ = 0U;
    div_numer_ = 0ULL;
    div_denom_ = 0ULL;
    div_result_ = 0;
    divrem_result_ = 0;
    sqrtcnt_ = 0U;
    sqrt_param_ = 0ULL;
    sqrt_result_ = 0U;
}

u8 MathCoprocessor::read8(Address address) noexcept {
    const u32 word = read32(address & ~3U);
    return static_cast<u8>((word >> ((address & 3U) * 8U)) & 0xFFU);
}

u16 MathCoprocessor::read16(Address address) noexcept {
    const Address aligned = address & ~1U;
    const u32 word = read32(aligned & ~3U);
    return static_cast<u16>((word >> ((aligned & 2U) * 8U)) & 0xFFFFU);
}

u32 MathCoprocessor::read32(Address address) noexcept {
    const Address aligned = address & ~3U;
    switch (aligned) {
    case kRegDivCnt:
        return static_cast<u32>(divcnt_);
    case kRegDivNumer:
        return static_cast<u32>(div_numer_ & 0xFFFFFFFFULL);
    case kRegDivNumer + 4U:
        return static_cast<u32>(div_numer_ >> 32U);
    case kRegDivDenom:
        return static_cast<u32>(div_denom_ & 0xFFFFFFFFULL);
    case kRegDivDenom + 4U:
        return static_cast<u32>(div_denom_ >> 32U);
    case kRegDivResult:
        return static_cast<u32>(static_cast<u64>(div_result_) & 0xFFFFFFFFULL);
    case kRegDivResult + 4U:
        return static_cast<u32>(static_cast<u64>(div_result_) >> 32U);
    case kRegDivRemResult:
        return static_cast<u32>(static_cast<u64>(divrem_result_) & 0xFFFFFFFFULL);
    case kRegDivRemResult + 4U:
        return static_cast<u32>(static_cast<u64>(divrem_result_) >> 32U);
    case kRegSqrtCnt:
        return static_cast<u32>(sqrtcnt_);
    case kRegSqrtResult:
        return sqrt_result_;
    case kRegSqrtParam:
        return static_cast<u32>(sqrt_param_ & 0xFFFFFFFFULL);
    case kRegSqrtParam + 4U:
        return static_cast<u32>(sqrt_param_ >> 32U);
    default:
        return 0U;
    }
}

void MathCoprocessor::write8(Address address, u8 value) noexcept {
    const Address aligned = address & ~3U;
    const u32 shift = (address & 3U) * 8U;
    const u32 merged = (read32(aligned) & ~(0xFFU << shift)) | (static_cast<u32>(value) << shift);
    write32(aligned, merged);
}

void MathCoprocessor::write16(Address address, u16 value) noexcept {
    const Address aligned = address & ~1U;
    if (aligned == kRegDivCnt) {
        divcnt_ = static_cast<u16>((divcnt_ & 0x4000U) | (value & 0x0003U));
        recompute_division();
        return;
    }
    if (aligned == kRegSqrtCnt) {
        sqrtcnt_ = value & 0x0001U;
        recompute_sqrt();
        return;
    }
    const Address word_addr = aligned & ~3U;
    const u32 shift = (aligned & 2U) * 8U;
    const u32 merged =
        (read32(word_addr) & ~(0xFFFFU << shift)) | (static_cast<u32>(value) << shift);
    write32(word_addr, merged);
}

void MathCoprocessor::write32(Address address, u32 value) noexcept {
    const Address aligned = address & ~3U;
    switch (aligned) {
    case kRegDivCnt:
        divcnt_ = static_cast<u16>((divcnt_ & 0x4000U) | (value & 0x0003U));
        recompute_division();
        break;
    case kRegDivNumer:
    case kRegDivNumer + 4U:
        div_numer_ = replace_u32_half(div_numer_, aligned != kRegDivNumer, value);
        recompute_division();
        break;
    case kRegDivDenom:
    case kRegDivDenom + 4U:
        div_denom_ = replace_u32_half(div_denom_, aligned != kRegDivDenom, value);
        recompute_division();
        break;
    case kRegSqrtCnt:
        sqrtcnt_ = static_cast<u16>(value & 0x0001U);
        recompute_sqrt();
        break;
    case kRegSqrtParam:
    case kRegSqrtParam + 4U:
        sqrt_param_ = replace_u32_half(sqrt_param_, aligned != kRegSqrtParam, value);
        recompute_sqrt();
        break;
    default:
        break;
    }
}

void MathCoprocessor::recompute_division() noexcept {
    const u16 mode = divcnt_ & 0x0003U;
    const i64 numer = (mode == 0U)
                          ? static_cast<i64>(static_cast<i32>(div_numer_ & 0xFFFFFFFFULL))
                          : static_cast<i64>(div_numer_);
    const i64 denom = (mode <= 1U)
                          ? static_cast<i64>(static_cast<i32>(div_denom_ & 0xFFFFFFFFULL))
                          : static_cast<i64>(div_denom_);

    if (denom == 0) {
        divcnt_ |= 0x4000U;
        div_result_ = (numer < 0) ? 1 : -1;
        divrem_result_ = numer;
        return;
    }

    divcnt_ &= static_cast<u16>(~0x4000U);
    constexpr i64 kI32Min = std::numeric_limits<i32>::min();
    constexpr i64 kI64Min = std::numeric_limits<i64>::min();
    if (mode == 0U && numer == kI32Min && denom == -1) {
        div_result_ = kI32Min;
        divrem_result_ = 0;
        return;
    }
    if (mode != 0U && numer == kI64Min && denom == -1) {
        div_result_ = kI64Min;
        divrem_result_ = 0;
        return;
    }
    div_result_ = numer / denom;
    divrem_result_ = numer % denom;
}

void MathCoprocessor::recompute_sqrt() noexcept {
    const u64 input = ((sqrtcnt_ & 1U) == 0U) ? (sqrt_param_ & 0xFFFFFFFFULL) : sqrt_param_;
    sqrt_result_ = integer_sqrt_u64(input);
}

}  // namespace elmasds::hardware::math
