#include "memory/memory_bus/arm9_memory_bus.hpp"

#include <optional>

namespace elmasds::memory {
namespace {

[[nodiscard]] std::optional<VramBankId> vram_bank_for_mmio(Address address) noexcept {
    switch (address) {
    case 0x04000240U:
        return VramBankId::A;
    case 0x04000241U:
        return VramBankId::B;
    case 0x04000242U:
        return VramBankId::C;
    case 0x04000243U:
        return VramBankId::D;
    case 0x04000244U:
        return VramBankId::E;
    case 0x04000245U:
        return VramBankId::F;
    case 0x04000246U:
        return VramBankId::G;
    case 0x04000248U:
        return VramBankId::H;
    case 0x04000249U:
        return VramBankId::I;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool is_bank_control_range(Address address, Address width) noexcept {
    return (address + width) > 0x04000240U && address <= 0x04000249U;
}

}  // namespace

Arm9MemoryBus::Arm9MemoryBus(MemorySystem& system) noexcept : system_(system) {}

bool Arm9MemoryBus::try_read_bank_control_8(Address address, u8& out_value) const noexcept {
    if (address == 0x04000247U) {
        out_value = system_.shared_wram().wram_cnt();
        return true;
    }
    if (const auto bank = vram_bank_for_mmio(address); bank.has_value()) {
        out_value = system_.vram().read_vram_cnt(*bank);
        return true;
    }
    return false;
}

bool Arm9MemoryBus::try_write_bank_control_8(Address address, u8 value) noexcept {
    if (address == 0x04000247U) {
        system_.shared_wram().set_wram_cnt(value);
        return true;
    }
    if (const auto bank = vram_bank_for_mmio(address); bank.has_value()) {
        system_.vram().write_vram_cnt(*bank, value);
        return true;
    }
    return false;
}

u8 Arm9MemoryBus::read_mmio_8(Address address) const {
    u8 control_val = 0U;
    if (try_read_bank_control_8(address, control_val)) {
        return control_val;
    }
    return system_.arm9_mmio().read8(address);
}

u16 Arm9MemoryBus::read_mmio_16(Address address) const {
    if (is_bank_control_range(address, 2U)) {
        const u16 lo = read_mmio_8(address);
        const u16 hi = read_mmio_8(address + 1U);
        return static_cast<u16>(lo | (hi << 8U));
    }
    return system_.arm9_mmio().read16(address);
}

u32 Arm9MemoryBus::read_mmio_32(Address address) const {
    if (is_bank_control_range(address, 4U)) {
        const u32 lo = read_mmio_16(address);
        const u32 hi = read_mmio_16(address + 2U);
        return lo | (hi << 16U);
    }
    return system_.arm9_mmio().read32(address);
}

void Arm9MemoryBus::write_mmio_8(Address address, u8 value) {
    if (!try_write_bank_control_8(address, value)) {
        system_.arm9_mmio().write8(address, value);
    }
}

void Arm9MemoryBus::write_mmio_16(Address address, u16 value) {
    if (is_bank_control_range(address, 2U)) {
        write_mmio_8(address, static_cast<u8>(value & 0xFFU));
        write_mmio_8(address + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
        return;
    }
    system_.arm9_mmio().write16(address, value);
}

void Arm9MemoryBus::write_mmio_32(Address address, u32 value) {
    if (is_bank_control_range(address, 4U)) {
        write_mmio_16(address, static_cast<u16>(value & 0xFFFFU));
        write_mmio_16(address + 2U, static_cast<u16>((value >> 16U) & 0xFFFFU));
        return;
    }
    system_.arm9_mmio().write32(address, value);
}

u8 Arm9MemoryBus::read8(Address address) {
    if (address >= 0xFFFF0000U) {
        accumulated_waitstates_ += 1U;
        return system_.bios9().read8(address - 0xFFFF0000U);
    }

    switch (address >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 8U;
        return system_.main_ram().read8(address);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return system_.shared_wram().read_arm9_8(address);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_8(address);
    case 0x05U:
        accumulated_waitstates_ += 1U;
        return system_.palette().read8(address);
    case 0x06U:
        accumulated_waitstates_ += 1U;
        return system_.vram().read_arm9_8(address);
    case 0x07U:
        accumulated_waitstates_ += 1U;
        return system_.oam().read8(address);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

u16 Arm9MemoryBus::read16(Address address) {
    const Address aligned = address & ~1U;
    if (aligned >= 0xFFFF0000U) {
        accumulated_waitstates_ += 1U;
        return system_.bios9().read16(aligned - 0xFFFF0000U);
    }

    switch (aligned >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 8U;
        return system_.main_ram().read16(aligned);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return system_.shared_wram().read_arm9_16(aligned);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_16(aligned);
    case 0x05U:
        accumulated_waitstates_ += 1U;
        return system_.palette().read16(aligned);
    case 0x06U:
        accumulated_waitstates_ += 1U;
        return system_.vram().read_arm9_16(aligned);
    case 0x07U:
        accumulated_waitstates_ += 1U;
        return system_.oam().read16(aligned);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

u32 Arm9MemoryBus::read32(Address address) {
    const Address aligned = address & ~3U;
    if (aligned >= 0xFFFF0000U) {
        accumulated_waitstates_ += 1U;
        return system_.bios9().read32(aligned - 0xFFFF0000U);
    }

    switch (aligned >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 9U;
        return system_.main_ram().read32(aligned);
    case 0x03U:
        accumulated_waitstates_ += 1U;
        return system_.shared_wram().read_arm9_32(aligned);
    case 0x04U:
        accumulated_waitstates_ += 1U;
        return read_mmio_32(aligned);
    case 0x05U:
        accumulated_waitstates_ += 2U;
        return system_.palette().read32(aligned);
    case 0x06U:
        accumulated_waitstates_ += 2U;
        return system_.vram().read_arm9_32(aligned);
    case 0x07U:
        accumulated_waitstates_ += 2U;
        return system_.oam().read32(aligned);
    default:
        accumulated_waitstates_ += 1U;
        return 0U;
    }
}

void Arm9MemoryBus::write8(Address address, u8 value) {
    switch (address >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 8U;
        system_.main_ram().write8(address, value);
        break;
    case 0x03U:
        accumulated_waitstates_ += 1U;
        system_.shared_wram().write_arm9_8(address, value);
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        write_mmio_8(address, value);
        break;
    case 0x05U:
        accumulated_waitstates_ += 1U;
        system_.palette().write8(address, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 1U;
        system_.vram().write_arm9_8(address, value);
        break;
    case 0x07U:
        accumulated_waitstates_ += 1U;
        system_.oam().write8(address, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

void Arm9MemoryBus::write16(Address address, u16 value) {
    const Address aligned = address & ~1U;
    switch (aligned >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 8U;
        system_.main_ram().write16(aligned, value);
        break;
    case 0x03U:
        accumulated_waitstates_ += 1U;
        system_.shared_wram().write_arm9_16(aligned, value);
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        write_mmio_16(aligned, value);
        break;
    case 0x05U:
        accumulated_waitstates_ += 1U;
        system_.palette().write16(aligned, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 1U;
        system_.vram().write_arm9_16(aligned, value);
        break;
    case 0x07U:
        accumulated_waitstates_ += 1U;
        system_.oam().write16(aligned, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

void Arm9MemoryBus::write32(Address address, u32 value) {
    const Address aligned = address & ~3U;
    switch (aligned >> 24U) {
    case 0x02U:
        accumulated_waitstates_ += 9U;
        system_.main_ram().write32(aligned, value);
        break;
    case 0x03U:
        accumulated_waitstates_ += 1U;
        system_.shared_wram().write_arm9_32(aligned, value);
        break;
    case 0x04U:
        accumulated_waitstates_ += 1U;
        write_mmio_32(aligned, value);
        break;
    case 0x05U:
        accumulated_waitstates_ += 2U;
        system_.palette().write32(aligned, value);
        break;
    case 0x06U:
        accumulated_waitstates_ += 2U;
        system_.vram().write_arm9_32(aligned, value);
        break;
    case 0x07U:
        accumulated_waitstates_ += 2U;
        system_.oam().write32(aligned, value);
        break;
    default:
        accumulated_waitstates_ += 1U;
        break;
    }
}

}  // namespace elmasds::memory
