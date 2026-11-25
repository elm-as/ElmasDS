#pragma once

#include "memory/arm7_wram/arm7_wram.hpp"
#include "memory/bios/bios_memory.hpp"
#include "memory/main_ram/main_ram.hpp"
#include "memory/mmio/mmio_router.hpp"
#include "memory/oam/oam_ram.hpp"
#include "memory/palette/palette_ram.hpp"
#include "memory/shared_wram/shared_wram.hpp"
#include "memory/vram/vram_controller.hpp"

namespace elmasds::memory {

/**
 * @brief Owns all physical Nintendo DS memory regions and per-CPU MMIO dispatch routers.
 *
 * Responsibility:
 *   Centralizes Main RAM, Shared WRAM, ARM7 WRAM, VRAM, Palette RAM, OAM, ARM9/ARM7 BIOS,
 *   and MMIO routers without any global mutable state.
 */
class MemorySystem {
public:
    MemorySystem();

    void reset() noexcept;

    [[nodiscard]] MainRam& main_ram() noexcept { return main_ram_; }
    [[nodiscard]] const MainRam& main_ram() const noexcept { return main_ram_; }

    [[nodiscard]] SharedWram& shared_wram() noexcept { return shared_wram_; }
    [[nodiscard]] const SharedWram& shared_wram() const noexcept { return shared_wram_; }

    [[nodiscard]] Arm7Wram& arm7_wram() noexcept { return arm7_wram_; }
    [[nodiscard]] const Arm7Wram& arm7_wram() const noexcept { return arm7_wram_; }

    [[nodiscard]] VramController& vram() noexcept { return vram_; }
    [[nodiscard]] const VramController& vram() const noexcept { return vram_; }

    [[nodiscard]] PaletteRam& palette() noexcept { return palette_; }
    [[nodiscard]] const PaletteRam& palette() const noexcept { return palette_; }

    [[nodiscard]] OamRam& oam() noexcept { return oam_; }
    [[nodiscard]] const OamRam& oam() const noexcept { return oam_; }

    [[nodiscard]] BiosMemory& bios9() noexcept { return bios9_; }
    [[nodiscard]] const BiosMemory& bios9() const noexcept { return bios9_; }

    [[nodiscard]] BiosMemory& bios7() noexcept { return bios7_; }
    [[nodiscard]] const BiosMemory& bios7() const noexcept { return bios7_; }

    [[nodiscard]] MmioRouter& arm9_mmio() noexcept { return arm9_mmio_; }
    [[nodiscard]] const MmioRouter& arm9_mmio() const noexcept { return arm9_mmio_; }

    [[nodiscard]] MmioRouter& arm7_mmio() noexcept { return arm7_mmio_; }
    [[nodiscard]] const MmioRouter& arm7_mmio() const noexcept { return arm7_mmio_; }

private:
    MainRam main_ram_;
    SharedWram shared_wram_;
    Arm7Wram arm7_wram_;
    VramController vram_;
    PaletteRam palette_;
    OamRam oam_;
    BiosMemory bios9_;
    BiosMemory bios7_;
    MmioRouter arm9_mmio_;
    MmioRouter arm7_mmio_;
};

}  // namespace elmasds::memory
