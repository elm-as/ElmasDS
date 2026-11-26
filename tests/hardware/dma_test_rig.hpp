#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"

#include <unordered_map>
#include <vector>

namespace elmasds::hardware::dma {

class RecordingBus final : public cpu::CpuBusInterface {
public:
    [[nodiscard]] u8 read8(Address address) override { return read_byte(address); }

    [[nodiscard]] u16 read16(Address address) override {
        const Address aligned = address & ~1U;
        const u16 lo = read_byte(aligned);
        const u16 hi = read_byte(aligned + 1U);
        return static_cast<u16>(lo | (hi << 8U));
    }

    [[nodiscard]] u32 read32(Address address) override {
        const Address aligned = address & ~3U;
        const u32 lo = read16(aligned);
        const u32 hi = read16(aligned + 2U);
        return lo | (hi << 16U);
    }

    void write8(Address address, u8 value) override {
        write_log.push_back(address);
        storage[address] = value;
    }

    void write16(Address address, u16 value) override {
        const Address aligned = address & ~1U;
        write_log.push_back(aligned);
        storage[aligned] = static_cast<u8>(value & 0xFFU);
        storage[aligned + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    }

    void write32(Address address, u32 value) override {
        const Address aligned = address & ~3U;
        write_log.push_back(aligned);
        storage[aligned] = static_cast<u8>(value & 0xFFU);
        storage[aligned + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        storage[aligned + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
        storage[aligned + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
    }

    std::unordered_map<Address, u8> storage;
    std::vector<Address> write_log;

private:
    [[nodiscard]] u8 read_byte(Address address) const {
        const auto it = storage.find(address);
        return (it == storage.end()) ? 0U : it->second;
    }
};

struct DmaHarness {
    RecordingBus bus;
    interrupts::InterruptController irq;
    DmaController dma;

    explicit DmaHarness(interrupts::CpuKind cpu = interrupts::CpuKind::Arm9)
        : irq(cpu), dma(cpu, bus, irq) {}
};

}  // namespace elmasds::hardware::dma
