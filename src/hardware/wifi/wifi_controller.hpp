#pragma once

#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>

namespace elmasds::hardware::wifi {

inline constexpr Address kWifiMmioStart = 0x04800000U;
inline constexpr Address kWifiMmioEnd = 0x0480FFFFU;

/**
 * @brief Stubbed Nintendo DS ARM7 Wi-Fi MMIO & 8 KiB RAM controller (`0x04800000..0x0480FFFF`).
 *
 * Responds to hardware ID (`W_ID`), power state handshake (`W_POWERSTATE`/`W_POWERFORCE`),
 * baseband SPI register transfers (`W_BB_CNT`/`W_BB_WRITE`/`W_BB_READ`), busy flags, LFSR PRNG
 * (`W_RANDOM`), and 8 KiB Wi-Fi RAM so commercial SDK wireless startup completes cleanly.
 */
class WifiController final : public memory::MmioDevice {
public:
    WifiController() noexcept { reset(); }

    void reset() noexcept;

    [[nodiscard]] u8 read8(Address address) noexcept override;
    [[nodiscard]] u16 read16(Address address) noexcept override;
    [[nodiscard]] u32 read32(Address address) noexcept override;

    void write8(Address address, u8 value) noexcept override;
    void write16(Address address, u16 value) noexcept override;
    void write32(Address address, u32 value) noexcept override;

private:
    [[nodiscard]] u16 read_reg16(Address offset) noexcept;
    void write_reg16(Address offset, u16 value) noexcept;

    std::array<u8, 0x2000U> wifi_ram_{};
    std::array<u16, 0x800U> regs_{};
    std::array<u8, 0x100U> bb_regs_{};
    u16 random_lfsr_ = 0x02A5U;
};

}  // namespace elmasds::hardware::wifi
