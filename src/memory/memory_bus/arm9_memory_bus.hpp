#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::memory {

/**
 * @brief ARM946E-S view of the Nintendo DS memory map implementing `cpu::CpuBusInterface`.
 *
 * Responsibility:
 *   Routes ARM9 data and instruction accesses across Main RAM (`0x02`), Shared WRAM (`0x03`),
 *   MMIO + `VRAMCNT`/`WRAMCNT` (`0x04`), Palette (`0x05`), VRAM (`0x06`), OAM (`0x07`),
 *   and ARM9 BIOS (`0xFFFF0000`), while accumulating per-region waitstate cycles.
 */
class Arm9MemoryBus final : public cpu::CpuBusInterface {
public:
    explicit Arm9MemoryBus(MemorySystem& system) noexcept;

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

    [[nodiscard]] TickCount accumulated_waitstates() const noexcept {
        return accumulated_waitstates_;
    }
    void reset_waitstates() noexcept { accumulated_waitstates_ = 0U; }

private:
    [[nodiscard]] u8 read_mmio_8(Address address) const;
    [[nodiscard]] u16 read_mmio_16(Address address) const;
    [[nodiscard]] u32 read_mmio_32(Address address) const;

    void write_mmio_8(Address address, u8 value);
    void write_mmio_16(Address address, u16 value);
    void write_mmio_32(Address address, u32 value);
    [[nodiscard]] bool try_write_bank_control_8(Address address, u8 value) noexcept;
    [[nodiscard]] bool try_read_bank_control_8(Address address, u8& out_value) const noexcept;

    MemorySystem& system_;
    TickCount accumulated_waitstates_ = 0U;
};

}  // namespace elmasds::memory
