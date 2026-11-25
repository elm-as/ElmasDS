#include "cpu/arm9/cp15_system_control.hpp"

#include "primitives/bit_operations.hpp"

#include <utility>

namespace elmasds::cpu {
namespace {

constexpr u32 kMinTcmSizeField = 3U;
constexpr u32 kMaxTcmSizeField = 23U;
constexpr u64 kBaseTcmVirtualUnit = 512ULL;

[[nodiscard]] bool is_valid_cp15_access(const CoprocessorTransfer& transfer) noexcept {
    return transfer.coprocessor == 15U && transfer.privileged && transfer.opcode_1 == 0U;
}

[[nodiscard]] std::optional<u32> read_cr0_register(u8 crm, u8 opcode_2) noexcept {
    if (crm != 0U) {
        return std::nullopt;
    }
    switch (opcode_2) {
    case 0U:
        return Cp15SystemControl::kMainId;
    case 1U:
        return Cp15SystemControl::kCacheType;
    case 2U:
        return Cp15SystemControl::kTcmSize;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool is_valid_cache_maintenance_op(u8 crm, u8 opcode_2) noexcept {
    switch (crm) {
    case 5U:
        return opcode_2 <= 2U || opcode_2 == 4U;
    case 6U:
        return opcode_2 <= 2U;
    case 7U:
        return opcode_2 == 0U;
    case 10U:
        return opcode_2 == 1U || opcode_2 == 2U || opcode_2 == 4U;
    case 14U:
        return opcode_2 == 1U || opcode_2 == 2U;
    default:
        return false;
    }
}

[[nodiscard]] TcmRegionConfig decode_tcm_region(u32 reg, bool enabled, bool load_mode) noexcept {
    const u32 size_field = bits::extract_bits(reg, 1U, 5U);
    const u64 virtual_size = (size_field >= kMinTcmSizeField && size_field <= kMaxTcmSizeField)
                                 ? (kBaseTcmVirtualUnit << size_field)
                                 : 0ULL;
    return TcmRegionConfig{
        .enabled = enabled,
        .load_mode = load_mode,
        .base_address = reg & 0xFFFFF000U,
        .virtual_size_bytes = virtual_size,
    };
}

}  // namespace

void Cp15SystemControl::reset(bool high_vectors) noexcept {
    control_register_ =
        bits::write_bit(kControlFixedOneMask, kBitHivects, high_vectors);
    dtcm_register_ = 0U;
    itcm_register_ = 0U;
    wfi_requested_ = false;
}

bool Cp15SystemControl::write_register(const CoprocessorTransfer& transfer, u32 value) {
    if (!is_valid_cp15_access(transfer)) {
        return false;
    }

    switch (transfer.crn) {
    case 1U:
        if (transfer.crm != 0U || transfer.opcode_2 != 0U) {
            return false;
        }
        control_register_ = (value & kControlWritableMask) | kControlFixedOneMask;
        return true;

    case 7U:
        if (transfer.crm == 0U && transfer.opcode_2 == 4U) {
            wfi_requested_ = true;
            return true;
        }
        return is_valid_cache_maintenance_op(transfer.crm, transfer.opcode_2);

    case 9U:
        if (transfer.crm != 1U || transfer.opcode_2 > 1U) {
            return false;
        }
        if (transfer.opcode_2 == 0U) {
            dtcm_register_ = value & kTcmRegionWriteMask;
        } else {
            itcm_register_ = value & kTcmRegionWriteMask;
        }
        return true;

    default:
        return false;
    }
}

std::optional<u32> Cp15SystemControl::read_register(const CoprocessorTransfer& transfer) {
    if (!is_valid_cp15_access(transfer)) {
        return std::nullopt;
    }

    switch (transfer.crn) {
    case 0U:
        return read_cr0_register(transfer.crm, transfer.opcode_2);
    case 1U:
        if (transfer.crm != 0U || transfer.opcode_2 != 0U) {
            return std::nullopt;
        }
        return control_register_;
    case 9U:
        if (transfer.crm != 1U || transfer.opcode_2 > 1U) {
            return std::nullopt;
        }
        return (transfer.opcode_2 == 0U) ? dtcm_register_ : itcm_register_;
    default:
        return std::nullopt;
    }
}

bool Cp15SystemControl::high_vectors() const noexcept {
    return bits::test_bit(control_register_, kBitHivects);
}

TcmRegionConfig Cp15SystemControl::dtcm_config() const noexcept {
    return decode_tcm_region(dtcm_register_,
                             bits::test_bit(control_register_, kBitDtcmEnable),
                             bits::test_bit(control_register_, kBitDtcmLoadMode));
}

TcmRegionConfig Cp15SystemControl::itcm_config() const noexcept {
    return decode_tcm_region(itcm_register_,
                             bits::test_bit(control_register_, kBitItcmEnable),
                             bits::test_bit(control_register_, kBitItcmLoadMode));
}

bool Cp15SystemControl::consume_wfi_request() noexcept {
    return std::exchange(wfi_requested_, false);
}

}  // namespace elmasds::cpu
