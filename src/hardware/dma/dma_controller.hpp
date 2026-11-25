#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"
#include "hardware/dma/dma_channel.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::hardware::dma {

/**
 * @brief Per-CPU 4-channel Nintendo DS DMA controller (`0x040000B0..0x040000DF` / `0x040000EF`).
 *
 * Responsibility:
 *   Implements `DMA0..DMA3` source, destination, word-count, and control registers, ARM9
 *   `DMA0..3FILL` registers (`0x040000E0..0x040000EF`), `0->1` Enable shadow latches, 16/32-bit
 *   bus transfers, `Inc`/`Dec`/`Fixed`/`IncReload` addressing modes, `Src=3` prohibited no-op,
 *   priority `DMA0 > DMA1 > DMA2 > DMA3`, Repeat, HBlank pause during VBlank, and `IrqSource::Dma0..3`.
 */
class DmaController final : public memory::MmioDevice {
public:
    /**
     * @brief Constructs a DMA controller for `cpu` bound to `bus` and `irq`.
     */
    DmaController(interrupts::CpuKind cpu, cpu::CpuBusInterface& bus,
                  interrupts::InterruptController& irq) noexcept;

    DmaController(const DmaController&) = delete;
    DmaController& operator=(const DmaController&) = delete;
    DmaController(DmaController&&) = delete;
    DmaController& operator=(DmaController&&) = delete;

    /**
     * @brief Zeroes all channel registers, shadow latches, ARM9 fill registers, and VBlank state.
     */
    void reset() noexcept;

    /**
     * @brief Triggers all enabled channels matching `source` in priority order (`0 > 1 > 2 > 3`).
     */
    void trigger(DmaTrigger source);

    /**
     * @brief Convenience trigger for `DmaTrigger::VBlank`.
     */
    void trigger_vblank();

    /**
     * @brief Convenience trigger for `DmaTrigger::HBlank` (paused when VBlank is active).
     */
    void trigger_hblank();

    /**
     * @brief Convenience trigger for `DmaTrigger::CartridgeSlot1`.
     */
    void trigger_cartridge();

    /**
     * @brief Convenience trigger for `DmaTrigger::GeometryFifo`.
     */
    void trigger_gxfifo();

    /**
     * @brief Sets whether VBlank is active (suppresses `HBlank` DMA transfers while true).
     */
    void set_vblank_active(bool active) noexcept;

    /**
     * @brief Returns true if `channel` (`0..3`) currently has its `Enable` bit set.
     */
    [[nodiscard]] bool is_channel_active(u8 channel) const noexcept;

    /**
     * @brief Returns read-only state of channel `index` (`0..3`, clamped to `0` if out of range).
     */
    [[nodiscard]] const DmaChannel& channel(std::size_t index) const noexcept;

    /**
     * @brief Returns ARM9 `DMAxFILL` value for `index` (`0..3`, or `0` if out of range / ARM7).
     */
    [[nodiscard]] u32 fill_register(std::size_t index) const noexcept;

    /**
     * @brief Returns the owning processor core (`CpuKind::Arm9` or `CpuKind::Arm7`).
     */
    [[nodiscard]] interrupts::CpuKind cpu() const noexcept;

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

private:
    [[nodiscard]] u32 read_channel_word(std::size_t index, Address reg_offset) const noexcept;
    void write_channel_byte(std::size_t index, Address reg_offset, u8 value);
    void write_channel_halfword(std::size_t index, Address reg_offset, u16 value);
    void write_channel_word(std::size_t index, Address reg_offset, u32 value);
    void write_control(std::size_t index, u16 raw_value);
    void latch_channel(std::size_t index) noexcept;
    void execute_transfer(std::size_t index);
    void transfer_units(std::size_t index);
    void finish_transfer(std::size_t index, bool raise_irq) noexcept;

    [[nodiscard]] static interrupts::IrqSource channel_irq_source(std::size_t index) noexcept;

    interrupts::CpuKind cpu_;
    cpu::CpuBusInterface& bus_;
    interrupts::InterruptController& irq_;
    std::array<DmaChannel, kDmaChannelCount> channels_{};
    std::array<u32, kDmaChannelCount> fill_registers_{};
    bool vblank_active_ = false;
};

}  // namespace elmasds::hardware::dma
