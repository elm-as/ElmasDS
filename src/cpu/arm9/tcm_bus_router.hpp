#pragma once

#include "cpu/arm9/cp15_system_control.hpp"
#include "cpu/arm_architecture/bus_interface.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/performance_profiler.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace elmasds::cpu {

/**
 * @brief Bus router intercepting ARM946E-S accesses for internal 32 KiB ITCM and 16 KiB DTCM.
 *
 * Responsibility:
 *   Owns physical ITCM (32 KiB) and DTCM (16 KiB) backing arrays and routes data reads/writes
 *   and instruction fetches according to live CP15 region configuration and load-mode bits.
 * Dependencies:
 *   `CpuBusInterface`, `Cp15SystemControl`, `fixed_width_types.hpp`.
 * Hardware notes:
 *   - Data writes: DTCM (if enabled) -> ITCM (if enabled) -> external bus
 *     (load mode permits writes).
 *   - Data reads: DTCM (if enabled & !load_mode) -> ITCM (if enabled & !load_mode) -> external bus.
 *   - Instruction fetches (`fetch16`/`fetch32`): ITCM (if enabled & !load_mode) -> external bus.
 *     DTCM never serves instruction fetches.
 */
class TcmBusRouter final : public CpuBusInterface {
public:
    static constexpr std::size_t kItcmPhysicalSize = 32768U;
    static constexpr std::size_t kDtcmPhysicalSize = 16384U;

    /**
     * @brief Constructs a TCM router forwarding non-TCM accesses to `external_bus`.
     */
    TcmBusRouter(CpuBusInterface& external_bus, const Cp15SystemControl& cp15) noexcept;

    TcmBusRouter(const TcmBusRouter&) = delete;
    TcmBusRouter& operator=(const TcmBusRouter&) = delete;
    TcmBusRouter(TcmBusRouter&&) = delete;
    TcmBusRouter& operator=(TcmBusRouter&&) = delete;

    /**
     * @brief Zero-fills both physical ITCM and DTCM backing stores.
     */
    void reset() noexcept;

    /**
     * @brief Registers a direct pointer to the 4 MiB Main RAM backing buffer for fast `0x02xxxxxx` accesses.
     */
    void set_main_ram_fast_path(u8* main_ram_4mb) noexcept { main_ram_ = main_ram_4mb; }
    void set_profiler(primitives::PerformanceProfiler* profiler) noexcept { profiler_ = profiler; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    [[nodiscard]] u16 fetch16(Address address) override;
    [[nodiscard]] u32 fetch32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

    [[nodiscard]] std::span<const u8> itcm_data() const noexcept { return itcm_; }
    [[nodiscard]] std::span<u8> mutable_itcm_data() noexcept { return itcm_; }
    [[nodiscard]] std::span<const u8> dtcm_data() const noexcept { return dtcm_; }
    [[nodiscard]] std::span<u8> mutable_dtcm_data() noexcept { return dtcm_; }

private:
    CpuBusInterface& external_bus_;
    const Cp15SystemControl& cp15_;
    u8* main_ram_ = nullptr;
    primitives::PerformanceProfiler* profiler_ = nullptr;
    std::array<u8, kItcmPhysicalSize> itcm_{};
    std::array<u8, kDtcmPhysicalSize> dtcm_{};
};

}  // namespace elmasds::cpu
