#pragma once

#include "cpu/arm_architecture/coprocessor_interface.hpp"
#include "primitives/fixed_width_types.hpp"

#include <optional>

namespace elmasds::cpu {

/**
 * @brief Decoded configuration for an ARM946E-S Tightly-Coupled Memory (ITCM/DTCM) region.
 */
struct TcmRegionConfig {
    bool enabled = false;
    bool load_mode = false;
    Address base_address = 0U;
    u64 virtual_size_bytes = 0ULL;
};

/**
 * @brief ARM946E-S CP15 System Control Coprocessor managing IDs, HIVECS, WFI, and TCM regions.
 *
 * Responsibility:
 *   Implements privileged CP15 (`p15`, `op1 == 0`) MCR/MRC transfers for identification,
 *   cache/TCM size reporting, System Control Register (`c1`) flags (`HIVECS`, ITCM/DTCM enable
 *   and load mode), `Wait for Interrupt` (`c7, c0, 4`) + cache-maintenance no-ops, and
 *   DTCM/ITCM region base/size registers (`c9, c1, 0/1`).
 * Dependencies:
 *   `CoprocessorInterface`, `fixed_width_types.hpp`, `bit_operations.hpp`.
 * Hardware notes:
 *   - User-mode accesses (`!transfer.privileged`), non-CP15 transfers, `op1 != 0`, or
 *     unsupported register indices return `false` / `std::nullopt` to trap to `Undefined`.
 *   - Control Register (`c1`) keeps ARM946E-S SBO bits `[6:3]` (`0x78`) fixed at `1`.
 */
class Cp15SystemControl final : public CoprocessorInterface {
public:
    static constexpr u32 kMainId = 0x41059461U;
    static constexpr u32 kCacheType = 0x0F0D2112U;
    static constexpr u32 kTcmSize = 0x00140180U;

    static constexpr u32 kControlFixedOneMask = 0x00000078U;
    static constexpr u32 kControlWritableMask = 0x000FF085U;
    static constexpr u32 kTcmRegionWriteMask = 0xFFFFF03EU;

    static constexpr u32 kBitHivects = 13U;
    static constexpr u32 kBitDtcmEnable = 16U;
    static constexpr u32 kBitDtcmLoadMode = 17U;
    static constexpr u32 kBitItcmEnable = 18U;
    static constexpr u32 kBitItcmLoadMode = 19U;

    Cp15SystemControl() noexcept = default;

    /**
     * @brief Resets CP15 registers and clears pending WFI requests.
     * @param high_vectors Initial `HIVECS` bit 13 state (`0xFFFF0000` vectors when `true`).
     */
    void reset(bool high_vectors = false) noexcept;

    /**
     * @brief Handles a CP15 MCR write transfer.
     * @return True on accepted transfer; false to trap to `CpuException::Undefined`.
     */
    bool write_register(const CoprocessorTransfer& transfer, u32 value) override;

    /**
     * @brief Handles a CP15 MRC read transfer.
     * @return Register value on accepted transfer; `std::nullopt` to trap to `Undefined`.
     */
    [[nodiscard]] std::optional<u32> read_register(const CoprocessorTransfer& transfer) override;

    /**
     * @brief Returns true when CP15 C1 bit 13 (`HIVECS`) routes vectors to `0xFFFF0000`.
     */
    [[nodiscard]] bool high_vectors() const noexcept;

    /**
     * @brief Decodes the live DTCM region configuration from CP15 `c1` and `c9, c1, 0`.
     */
    [[nodiscard]] TcmRegionConfig dtcm_config() const noexcept;

    /**
     * @brief Decodes the live ITCM region configuration from CP15 `c1` and `c9, c1, 1`.
     */
    [[nodiscard]] TcmRegionConfig itcm_config() const noexcept;

    /**
     * @brief Returns and clears any pending `Wait for Interrupt` request from `MCR c7, c0, 4`.
     */
    [[nodiscard]] bool consume_wfi_request() noexcept;

    [[nodiscard]] u32 control_register() const noexcept { return control_register_; }
    [[nodiscard]] u32 dtcm_register() const noexcept { return dtcm_register_; }
    [[nodiscard]] u32 itcm_register() const noexcept { return itcm_register_; }
    void restore_registers(u32 control_reg, u32 dtcm_reg, u32 itcm_reg) noexcept {
        control_register_ = (control_reg & kControlWritableMask) | kControlFixedOneMask;
        dtcm_register_ = dtcm_reg & kTcmRegionWriteMask;
        itcm_register_ = itcm_reg & kTcmRegionWriteMask;
    }

private:
    u32 control_register_ = kControlFixedOneMask;
    u32 dtcm_register_ = 0U;
    u32 itcm_register_ = 0U;
    bool wfi_requested_ = false;
};

}  // namespace elmasds::cpu
