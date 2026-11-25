#pragma once

#include "primitives/fixed_width_types.hpp"

namespace elmasds::hardware::spi {

/**
 * @brief Interface implemented by peripherals attached to the ARM7 SPI bus (`SPICNT`/`SPIDATA`).
 */
class SpiDevice {
public:
    virtual ~SpiDevice() = default;

    /**
     * @brief Resets internal transfer state and latches.
     */
    virtual void reset() = 0;

    /**
     * @brief Exchanges one byte over SPI and returns the response byte.
     * @param tx_byte Outgoing byte written by ARM7.
     * @param hold_chip_select True when `SPICNT` bit 11 keeps chip-select asserted after this byte.
     */
    [[nodiscard]] virtual u8 transfer(u8 tx_byte, bool hold_chip_select) = 0;

    /**
     * @brief Signals chip-select deassertion, terminating any active multi-byte command.
     */
    virtual void release_chip_select() = 0;
};

}  // namespace elmasds::hardware::spi

