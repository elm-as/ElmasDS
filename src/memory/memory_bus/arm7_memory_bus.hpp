#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::memory {

/**
 * @brief ARM7TDMI view of the Nintendo DS memory map implementing `cpu::CpuBusInterface`.
 *
 * Responsibility:
 *   Routes ARM7 accesses across protected ARM7 BIOS (`0x00000000-0x00003FFF`),
 *   Main RAM (`0x02`), Shared WRAM / ARM7-WRAM fallback (`0x03000000-0x037FFFFF`),
 *   private ARM7 WRAM (`0x03800000-0x03FFFFFF`), MMIO + `VRAMSTAT`/`WRAMSTAT` (`0x04`),
 *   and ARM7 VRAM-WRAM (`0x06`).
 */
class Arm7MemoryBus final : public cpu::CpuBusInterface {
public:
    explicit Arm7MemoryBus(MemorySystem& system) noexcept;

    [[nodiscard]] u8 read8(Address address) override;
    [[nodiscard]] u16 read16(Address address) override;
    [[nodiscard]] u32 read32(Address address) override;

    [[nodiscard]] u16 fetch16(Address address) override;
    [[nodiscard]] u32 fetch32(Address address) override;

    void write8(Address address, u8 value) override;
    void write16(Address address, u16 value) override;
    void write32(Address address, u32 value) override;

    void set_current_pc(Address pc) noexcept { current_pc_ = pc; }
    [[nodiscard]] Address current_pc() const noexcept { return current_pc_; }

    [[nodiscard]] TickCount accumulated_waitstates() const noexcept {
        return accumulated_waitstates_;
    }
    void reset_waitstates() noexcept { accumulated_waitstates_ = 0U; }

private:
    [[nodiscard]] bool use_shared_wram(Address address) const noexcept;
    [[nodiscard]] u8 read_mmio_8(Address address) const;
    [[nodiscard]] u16 read_mmio_16(Address address) const;
    [[nodiscard]] u32 read_mmio_32(Address address) const;

    MemorySystem& system_;
    Address current_pc_ = 0U;
    TickCount accumulated_waitstates_ = 0U;
};

}  // namespace elmasds::memory
