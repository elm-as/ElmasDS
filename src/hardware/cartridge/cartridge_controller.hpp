#pragma once

#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"
#include "storage/rom_loader/nds_rom.hpp"
#include "storage/save_manager/save_memory.hpp"

#include <array>
#include <cstddef>
#include <optional>

namespace elmasds::hardware::cartridge {

inline constexpr Address kCartMmioStart = 0x040001A0U;
inline constexpr Address kCartMmioEnd = 0x040001BFU;
inline constexpr Address kRegAuxSpiCnt = 0x040001A0U;
inline constexpr Address kRegAuxSpiData = 0x040001A2U;
inline constexpr Address kRegRomCtrl = 0x040001A4U;
inline constexpr Address kRegRomCmdStart = 0x040001A8U;
inline constexpr Address kRegRomData = 0x04100010U;
inline constexpr Address kRegRomDataEnd = 0x04100013U;
inline constexpr Address kRegExMemCnt = 0x04000204U;
inline constexpr Address kRegExMemCntEnd = 0x04000205U;

/**
 * @brief Wiring configuration for `CartridgeController`.
 */
struct CartridgeConfig {
    interrupts::InterruptController* arm9_irq = nullptr;
    interrupts::InterruptController* arm7_irq = nullptr;
    dma::DmaController* arm9_dma = nullptr;
    dma::DmaController* arm7_dma = nullptr;
};

class CartridgeController;

/**
 * @brief Per-CPU MMIO view enforcing `EXMEMCNT` slot ownership for `CartridgeController`.
 */
class CartridgeEndpointView final : public memory::MmioDevice {
public:
    CartridgeEndpointView(CartridgeController& controller, interrupts::CpuKind cpu) noexcept;

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    CartridgeController& controller_;
    interrupts::CpuKind cpu_;
};

/**
 * @brief Nintendo DS Slot-1 Cartridge & Auxiliary SPI controller.
 *
 * Responsibility:
 *   Implements `EXMEMCNT`/`EXMEMSTAT` slot arbitration, `AUXSPICNT`/`AUXSPIDATA` save-memory SPI
 *   routing, `ROMCTRL` + 8-byte command dispatch (`0x00`, `0x90`, `0xB8`, `0xB7`, `0x9F`),
 *   `ROMDATA` word streaming, cartridge DMA triggering, and `IrqSource::CartridgeTransfer`.
 */
class CartridgeController final {
public:
    explicit CartridgeController(const CartridgeConfig& config = {}) noexcept;

    CartridgeController(const CartridgeController&) = delete;
    CartridgeController& operator=(const CartridgeController&) = delete;
    CartridgeController(CartridgeController&&) = delete;
    CartridgeController& operator=(CartridgeController&&) = delete;

    void reset() noexcept;

    void insert_rom(storage::rom_loader::NdsRom rom);
    void eject_rom() noexcept;

    [[nodiscard]] bool has_rom() const noexcept { return rom_.has_value(); }
    [[nodiscard]] const storage::rom_loader::NdsRom* rom() const noexcept {
        return rom_.has_value() ? &(*rom_) : nullptr;
    }

    [[nodiscard]] storage::save_manager::SaveMemory& save_memory() noexcept { return save_memory_; }
    [[nodiscard]] const storage::save_manager::SaveMemory& save_memory() const noexcept {
        return save_memory_;
    }

    [[nodiscard]] interrupts::CpuKind slot_owner() const noexcept;
    [[nodiscard]] u16 exmemcnt() const noexcept { return exmemcnt_; }

    [[nodiscard]] memory::MmioDevice& arm9_mmio() noexcept { return arm9_view_; }
    [[nodiscard]] memory::MmioDevice& arm7_mmio() noexcept { return arm7_view_; }

    [[nodiscard]] u32 read_register_32(interrupts::CpuKind cpu, Address aligned_addr);
    void write_register_32(interrupts::CpuKind cpu, Address aligned_addr, u32 value);
    void write_register_16(interrupts::CpuKind cpu, Address aligned_addr, u16 value);
    void write_register_8(interrupts::CpuKind cpu, Address address, u8 value);

private:
    enum class TransferKind : u8 {
        None = 0,
        RomData,
        ChipId,
        Dummy,
    };

    [[nodiscard]] bool has_slot_access(interrupts::CpuKind cpu) const noexcept;
    void write_aux_spi_cnt(u16 value);
    void write_aux_spi_data(u8 value);
    void start_rom_command(interrupts::CpuKind cpu);
    [[nodiscard]] u32 read_rom_data_word(interrupts::CpuKind cpu);
    void finish_transfer(interrupts::CpuKind cpu) noexcept;
    [[nodiscard]] static u32 decode_block_bytes(u32 romctrl) noexcept;

    CartridgeConfig config_;
    std::optional<storage::rom_loader::NdsRom> rom_;
    storage::save_manager::SaveMemory save_memory_;
    CartridgeEndpointView arm9_view_;
    CartridgeEndpointView arm7_view_;

    u16 exmemcnt_ = 0U;
    u16 aux_spi_cnt_ = 0U;
    u8 aux_spi_rx_ = 0U;
    u32 rom_ctrl_ = 0U;
    std::array<u8, 8U> command_{};

    TransferKind transfer_kind_ = TransferKind::None;
    u32 transfer_rom_offset_ = 0U;
    u32 transfer_words_remaining_ = 0U;
    u32 last_rom_word_ = 0xFFFFFFFFU;
};

}  // namespace elmasds::hardware::cartridge

