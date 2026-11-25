#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"

#include <unordered_map>

namespace elmasds::cpu::test {

/**
 * @brief Sparse byte-addressable mock bus for isolated CPU instruction tests.
 */
class MockMemoryBus final : public CpuBusInterface {
public:
    u8 read8(Address address) override {
        const auto it = bytes_.find(address);
        return it != bytes_.end() ? it->second : 0U;
    }

    u16 read16(Address address) override {
        return static_cast<u16>(read8(address)) |
               (static_cast<u16>(read8(address + 1U)) << 8U);
    }

    u32 read32(Address address) override {
        return static_cast<u32>(read8(address)) |
               (static_cast<u32>(read8(address + 1U)) << 8U) |
               (static_cast<u32>(read8(address + 2U)) << 16U) |
               (static_cast<u32>(read8(address + 3U)) << 24U);
    }

    void write8(Address address, u8 value) override { bytes_[address] = value; }

    void write16(Address address, u16 value) override {
        write8(address, static_cast<u8>(value & 0xFFU));
        write8(address + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
    }

    void write32(Address address, u32 value) override {
        write8(address, static_cast<u8>(value & 0xFFU));
        write8(address + 1U, static_cast<u8>((value >> 8U) & 0xFFU));
        write8(address + 2U, static_cast<u8>((value >> 16U) & 0xFFU));
        write8(address + 3U, static_cast<u8>((value >> 24U) & 0xFFU));
    }

private:
    std::unordered_map<Address, u8> bytes_;
};

}  // namespace elmasds::cpu::test
