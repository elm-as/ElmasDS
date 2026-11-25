#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::gpu {

inline constexpr Address kDispStatMmioStart = 0x04000004U;
inline constexpr Address kDispStatMmioEnd = 0x04000007U;
inline constexpr Address kRegDispStat = 0x04000004U;
inline constexpr Address kRegVCount = 0x04000006U;

inline constexpr Address kPowCnt1MmioStart = 0x04000304U;
inline constexpr Address kPowCnt1MmioEnd = 0x04000307U;

/**
 * @brief Per-CPU `DISPSTAT` (`0x04000004`) and shared `VCOUNT` (`0x04000006`) MMIO device.
 */
class DisplayStatusMmio final : public memory::MmioDevice {
public:
    explicit DisplayStatusMmio(const u16& shared_vcount) noexcept;

    void reset() noexcept;
    void set_vblank(bool active) noexcept;
    void set_hblank(bool active) noexcept;
    void update_vcount_match() noexcept;

    [[nodiscard]] u16 dispstat() const noexcept { return dispstat_; }
    [[nodiscard]] u16 lyc() const noexcept;
    [[nodiscard]] bool is_vblank_irq_enabled() const noexcept { return (dispstat_ & 0x08U) != 0U; }
    [[nodiscard]] bool is_hblank_irq_enabled() const noexcept { return (dispstat_ & 0x10U) != 0U; }
    [[nodiscard]] bool is_vcount_irq_enabled() const noexcept { return (dispstat_ & 0x20U) != 0U; }
    [[nodiscard]] bool is_vcount_matching() const noexcept { return (dispstat_ & 0x04U) != 0U; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    const u16& shared_vcount_;
    u16 dispstat_ = 0U;
};

/**
 * @brief ARM9 `POWCNT1` (`0x04000304`) power & display-swap control register.
 */
class PowCnt1Mmio final : public memory::MmioDevice {
public:
    PowCnt1Mmio() noexcept = default;

    void reset() noexcept { value_ = 0x820FU; }
    void set_value(u16 value) noexcept { value_ = static_cast<u16>(value & 0x820FU); }

    [[nodiscard]] u16 value() const noexcept { return value_; }
    [[nodiscard]] bool lcd_enabled() const noexcept { return (value_ & 0x0001U) != 0U; }
    [[nodiscard]] bool engine_a_enabled() const noexcept { return (value_ & 0x0002U) != 0U; }
    [[nodiscard]] bool engine_b_enabled() const noexcept { return (value_ & 0x0200U) != 0U; }
    [[nodiscard]] bool display_swap() const noexcept { return (value_ & 0x8000U) != 0U; }

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    u16 value_ = 0x820FU;
};

}  // namespace elmasds::gpu
