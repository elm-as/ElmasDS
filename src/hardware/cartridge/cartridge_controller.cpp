#include "hardware/cartridge/cartridge_controller.hpp"

#include <utility>

namespace elmasds::hardware::cartridge {

CartridgeEndpointView::CartridgeEndpointView(CartridgeController& controller,
                                             interrupts::CpuKind cpu) noexcept
    : controller_(controller), cpu_(cpu) {}

u8 CartridgeEndpointView::read8(Address address) {
    const u32 word = controller_.read_register_32(cpu_, address & ~3U);
    const u32 shift = (address & 3U) * 8U;
    return static_cast<u8>((word >> shift) & 0xFFU);
}

u16 CartridgeEndpointView::read16(Address address) {
    const u32 word = controller_.read_register_32(cpu_, address & ~3U);
    const u32 shift = (address & 2U) * 8U;
    return static_cast<u16>((word >> shift) & 0xFFFFU);
}

u32 CartridgeEndpointView::read32(Address address) {
    return controller_.read_register_32(cpu_, address & ~3U);
}

void CartridgeEndpointView::write8(Address address, u8 value) {
    controller_.write_register_8(cpu_, address, value);
}

void CartridgeEndpointView::write16(Address address, u16 value) {
    controller_.write_register_16(cpu_, address & ~1U, value);
}

void CartridgeEndpointView::write32(Address address, u32 value) {
    controller_.write_register_32(cpu_, address & ~3U, value);
}

CartridgeController::CartridgeController(const CartridgeConfig& config) noexcept
    : config_(config),
      arm9_view_(*this, interrupts::CpuKind::Arm9),
      arm7_view_(*this, interrupts::CpuKind::Arm7) {}

void CartridgeController::reset() noexcept {
    exmemcnt_ = 0U;
    aux_spi_cnt_ = 0U;
    aux_spi_rx_ = 0U;
    rom_ctrl_ = 0U;
    command_.fill(u8{0});
    transfer_kind_ = TransferKind::None;
    transfer_rom_offset_ = 0U;
    transfer_words_remaining_ = 0U;
    last_rom_word_ = 0xFFFFFFFFU;
    save_memory_.reset();
}

void CartridgeController::insert_rom(storage::rom_loader::NdsRom rom) {
    rom_.emplace(std::move(rom));
    reset();
}

void CartridgeController::eject_rom() noexcept {
    rom_.reset();
    reset();
}

interrupts::CpuKind CartridgeController::slot_owner() const noexcept {
    return ((exmemcnt_ & (1U << 11U)) != 0U) ? interrupts::CpuKind::Arm7
                                            : interrupts::CpuKind::Arm9;
}

bool CartridgeController::has_slot_access(interrupts::CpuKind cpu) const noexcept {
    return slot_owner() == cpu;
}

u32 CartridgeController::read_register_32(interrupts::CpuKind cpu, Address aligned_addr) {
    if (aligned_addr == kRegExMemCnt) {
        return static_cast<u32>(exmemcnt_);
    }

    if (!has_slot_access(cpu)) {
        return 0U;
    }

    if (aligned_addr == kRegAuxSpiCnt) {
        return static_cast<u32>(aux_spi_cnt_) | (static_cast<u32>(aux_spi_rx_) << 16U);
    }
    if (aligned_addr == kRegRomCtrl) {
        return rom_ctrl_;
    }
    if (aligned_addr == kRegRomCmdStart) {
        return static_cast<u32>(command_[0U]) | (static_cast<u32>(command_[1U]) << 8U) |
               (static_cast<u32>(command_[2U]) << 16U) | (static_cast<u32>(command_[3U]) << 24U);
    }
    if (aligned_addr == kRegRomCmdStart + 4U) {
        return static_cast<u32>(command_[4U]) | (static_cast<u32>(command_[5U]) << 8U) |
               (static_cast<u32>(command_[6U]) << 16U) | (static_cast<u32>(command_[7U]) << 24U);
    }
    if (aligned_addr == kRegRomData) {
        return read_rom_data_word(cpu);
    }
    return 0U;
}

void CartridgeController::write_register_8(interrupts::CpuKind cpu, Address address, u8 value) {
    if (address >= kRegExMemCnt && address <= kRegExMemCntEnd) {
        if (cpu != interrupts::CpuKind::Arm9) {
            return;
        }
        const bool high = (address & 1U) != 0U;
        exmemcnt_ = high ? static_cast<u16>((exmemcnt_ & 0x00FFU) | (value << 8U))
                         : static_cast<u16>((exmemcnt_ & 0xFF00U) | value);
        return;
    }

    if (!has_slot_access(cpu)) {
        return;
    }

    if (address >= kRegRomCmdStart && address < kRegRomCmdStart + 8U) {
        command_[address - kRegRomCmdStart] = value;
        return;
    }
    if (address == kRegAuxSpiData) {
        write_aux_spi_data(value);
        return;
    }
    if (address == kRegAuxSpiCnt || address == kRegAuxSpiCnt + 1U) {
        const bool high = (address & 1U) != 0U;
        const u16 merged = high ? static_cast<u16>((aux_spi_cnt_ & 0x00FFU) | (value << 8U))
                                : static_cast<u16>((aux_spi_cnt_ & 0xFF00U) | value);
        write_aux_spi_cnt(merged);
    }
}

void CartridgeController::write_register_16(interrupts::CpuKind cpu, Address aligned_addr,
                                            u16 value) {
    if (aligned_addr == kRegExMemCnt) {
        if (cpu == interrupts::CpuKind::Arm9) {
            exmemcnt_ = value;
        }
        return;
    }

    if (!has_slot_access(cpu)) {
        return;
    }

    if (aligned_addr == kRegAuxSpiCnt) {
        write_aux_spi_cnt(value);
        return;
    }
    if (aligned_addr == kRegAuxSpiData) {
        write_aux_spi_data(static_cast<u8>(value & 0xFFU));
        return;
    }
    if (aligned_addr >= kRegRomCmdStart && aligned_addr < kRegRomCmdStart + 8U) {
        const std::size_t idx = aligned_addr - kRegRomCmdStart;
        command_[idx] = static_cast<u8>(value & 0xFFU);
        command_[idx + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        return;
    }
    if (aligned_addr == kRegRomCtrl) {
        const u32 merged = (rom_ctrl_ & 0xFFFF0000U) | value;
        write_register_32(cpu, kRegRomCtrl, merged);
        return;
    }
    if (aligned_addr == kRegRomCtrl + 2U) {
        const u32 merged = (rom_ctrl_ & 0x0000FFFFU) | (static_cast<u32>(value) << 16U);
        write_register_32(cpu, kRegRomCtrl, merged);
    }
}

void CartridgeController::write_register_32(interrupts::CpuKind cpu, Address aligned_addr,
                                            u32 value) {
    if (aligned_addr == kRegExMemCnt) {
        if (cpu == interrupts::CpuKind::Arm9) {
            exmemcnt_ = static_cast<u16>(value & 0xFFFFU);
        }
        return;
    }

    if (!has_slot_access(cpu)) {
        return;
    }

    if (aligned_addr == kRegAuxSpiCnt) {
        write_aux_spi_cnt(static_cast<u16>(value & 0xFFFFU));
        write_aux_spi_data(static_cast<u8>((value >> 16U) & 0xFFU));
        return;
    }
    if (aligned_addr == kRegRomCmdStart) {
        command_[0U] = static_cast<u8>(value & 0xFFU);
        command_[1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        command_[2U] = static_cast<u8>((value >> 16U) & 0xFFU);
        command_[3U] = static_cast<u8>((value >> 24U) & 0xFFU);
        return;
    }
    if (aligned_addr == kRegRomCmdStart + 4U) {
        command_[4U] = static_cast<u8>(value & 0xFFU);
        command_[5U] = static_cast<u8>((value >> 8U) & 0xFFU);
        command_[6U] = static_cast<u8>((value >> 16U) & 0xFFU);
        command_[7U] = static_cast<u8>((value >> 24U) & 0xFFU);
        return;
    }
    if (aligned_addr == kRegRomCtrl) {
        // Bit 23 (data-word ready) is read-only.
        rom_ctrl_ = (value & ~(1U << 23U)) | (rom_ctrl_ & (1U << 23U));
        if ((value & (1U << 31U)) != 0U) {
            start_rom_command(cpu);
        }
    }
}

void CartridgeController::write_aux_spi_cnt(u16 value) {
    const bool was_spi = (aux_spi_cnt_ & (1U << 15U)) != 0U && (aux_spi_cnt_ & (1U << 13U)) != 0U;
    const bool now_spi = (value & (1U << 15U)) != 0U && (value & (1U << 13U)) != 0U;
    if (was_spi && !now_spi) {
        save_memory_.release_chip_select();
    }
    aux_spi_cnt_ = static_cast<u16>(value & 0xE043U);
}

void CartridgeController::write_aux_spi_data(u8 value) {
    const bool slot_enabled = (aux_spi_cnt_ & (1U << 15U)) != 0U;
    const bool spi_mode = (aux_spi_cnt_ & (1U << 13U)) != 0U;
    if (!slot_enabled || !spi_mode) {
        aux_spi_rx_ = 0xFFU;
        return;
    }
    const bool hold_cs = (aux_spi_cnt_ & (1U << 6U)) != 0U;
    aux_spi_rx_ = save_memory_.transfer(value, hold_cs);
}

void CartridgeController::start_rom_command(interrupts::CpuKind cpu) {
    const u32 block_bytes = decode_block_bytes(rom_ctrl_);
    transfer_words_remaining_ = block_bytes / 4U;

    const u8 opcode = command_[0U];
    if (!rom_.has_value() || opcode == 0x9FU) {
        transfer_kind_ = TransferKind::Dummy;
    } else if (opcode == 0x90U || opcode == 0xB8U) {
        transfer_kind_ = TransferKind::ChipId;
    } else if (opcode == 0x00U) {
        transfer_kind_ = TransferKind::RomData;
        transfer_rom_offset_ = 0U;
    } else if (opcode == 0xB7U) {
        transfer_kind_ = TransferKind::RomData;
        u32 addr = (static_cast<u32>(command_[1U]) << 24U) |
                   (static_cast<u32>(command_[2U]) << 16U) |
                   (static_cast<u32>(command_[3U]) << 8U) | static_cast<u32>(command_[4U]);
        if (addr < 0x8000U) {
            addr = 0x8000U + (addr & 0x1FFU);
        }
        transfer_rom_offset_ = addr;
    } else {
        transfer_kind_ = TransferKind::Dummy;
    }

    if (transfer_words_remaining_ == 0U) {
        finish_transfer(cpu);
        return;
    }

    rom_ctrl_ |= (1U << 23U);  // Mark data word ready.
    dma::DmaController* dma_ctrl =
        (cpu == interrupts::CpuKind::Arm9) ? config_.arm9_dma : config_.arm7_dma;
    if (dma_ctrl != nullptr) {
        dma_ctrl->trigger_cartridge();
    }
}

u32 CartridgeController::read_rom_data_word(interrupts::CpuKind cpu) {
    if (transfer_words_remaining_ == 0U) {
        return last_rom_word_;
    }

    u32 word = 0xFFFFFFFFU;
    if (transfer_kind_ == TransferKind::ChipId && rom_.has_value()) {
        word = rom_->chip_id();
    } else if (transfer_kind_ == TransferKind::RomData && rom_.has_value()) {
        word = rom_->read32(transfer_rom_offset_);
        transfer_rom_offset_ += 4U;
    }

    last_rom_word_ = word;
    --transfer_words_remaining_;
    if (transfer_words_remaining_ == 0U) {
        finish_transfer(cpu);
    }
    return word;
}

void CartridgeController::finish_transfer(interrupts::CpuKind cpu) noexcept {
    rom_ctrl_ &= ~((1U << 31U) | (1U << 23U));
    transfer_kind_ = TransferKind::None;

    if ((aux_spi_cnt_ & (1U << 14U)) == 0U) {
        return;
    }
    interrupts::InterruptController* irq =
        (cpu == interrupts::CpuKind::Arm9) ? config_.arm9_irq : config_.arm7_irq;
    if (irq != nullptr) {
        irq->request_irq(interrupts::IrqSource::CartridgeTransfer);
    }
}

u32 CartridgeController::decode_block_bytes(u32 romctrl) noexcept {
    const u32 code = (romctrl >> 24U) & 0x07U;
    if (code == 0U) {
        return 0U;
    }
    if (code == 7U) {
        return 4U;
    }
    return 0x100U << code;
}

}  // namespace elmasds::hardware::cartridge
