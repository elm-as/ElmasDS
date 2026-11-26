#include "cpu/arm9/tcm_bus_router.hpp"

#include <algorithm>
#include <cstring>
#include <span>

namespace elmasds::cpu {
namespace {

struct TcmReadView {
    const TcmRegionConfig& config;
    std::span<const u8> storage;
};

struct TcmWriteView {
    const TcmRegionConfig& config;
    std::span<u8> storage;
};

[[nodiscard]] inline bool contains_range(const TcmRegionConfig& region,
                                         Address address,
                                         u64 access_size) noexcept {
    return region.enabled && (static_cast<u64>(address) >= region.base_address) &&
           ((static_cast<u64>(address) + access_size) <= region.end_address);
}

template <typename T>
[[nodiscard]] inline T read_tcm_le(std::span<const u8> storage, u64 offset) noexcept {
    const u64 mask = static_cast<u64>(storage.size() - 1U);
    const std::size_t masked = static_cast<std::size_t>(offset & mask);
    if (masked + sizeof(T) <= storage.size()) {
        T val{};
        std::memcpy(&val, storage.data() + masked, sizeof(T));
        return val;
    }
    u32 assembled = 0U;
    for (u64 i = 0U; i < sizeof(T); ++i) {
        const auto index = static_cast<std::size_t>((offset + i) & mask);
        assembled |= static_cast<u32>(storage[index]) << (i * 8U);
    }
    return static_cast<T>(assembled);
}

template <typename T>
inline void write_tcm_le(std::span<u8> storage, u64 offset, T value) noexcept {
    const u64 mask = static_cast<u64>(storage.size() - 1U);
    const std::size_t masked = static_cast<std::size_t>(offset & mask);
    if (masked + sizeof(T) <= storage.size()) {
        std::memcpy(storage.data() + masked, &value, sizeof(T));
        return;
    }
    const u32 raw = static_cast<u32>(value);
    for (u64 i = 0U; i < sizeof(T); ++i) {
        const auto index = static_cast<std::size_t>((offset + i) & mask);
        storage[index] = static_cast<u8>((raw >> (i * 8U)) & 0xFFU);
    }
}

template <typename T>
[[nodiscard]] inline T read_main_ram_le(const u8* ram, Address address) noexcept {
    constexpr Address kMask = static_cast<Address>(0x3FFFFFU & ~(sizeof(T) - 1U));
    T val{};
    std::memcpy(&val, ram + (address & kMask), sizeof(T));
    return val;
}

template <typename T>
inline void write_main_ram_le(u8* ram, Address address, T value) noexcept {
    constexpr Address kMask = static_cast<Address>(0x3FFFFFU & ~(sizeof(T) - 1U));
    std::memcpy(ram + (address & kMask), &value, sizeof(T));
}

template <typename T>
[[nodiscard]] bool try_read_tcm(const TcmReadView& view, Address address, T& out) noexcept {
    if (view.config.load_mode || !contains_range(view.config, address, sizeof(T))) {
        return false;
    }
    const u64 offset = static_cast<u64>(address) - static_cast<u64>(view.config.base_address);
    out = read_tcm_le<T>(view.storage, offset);
    return true;
}

template <typename T>
[[nodiscard]] bool try_write_tcm(const TcmWriteView& view, Address address, T value) noexcept {
    if (!contains_range(view.config, address, sizeof(T))) {
        return false;
    }
    const u64 offset = static_cast<u64>(address) - static_cast<u64>(view.config.base_address);
    write_tcm_le<T>(view.storage, offset, value);
    return true;
}

}  // namespace

TcmBusRouter::TcmBusRouter(CpuBusInterface& external_bus, const Cp15SystemControl& cp15) noexcept
    : external_bus_(external_bus), cp15_(cp15) {}

void TcmBusRouter::reset() noexcept {
    itcm_.fill(0U);
    dtcm_.fill(0U);
}

u8 TcmBusRouter::read8(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    u8 value = 0U;
    if (try_read_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_read_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return value;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        return read_main_ram_le<u8>(main_ram_, address);
    }
    return external_bus_.read8(address);
}

u16 TcmBusRouter::read16(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    u16 value = 0U;
    if (try_read_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_read_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return value;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        return read_main_ram_le<u16>(main_ram_, address);
    }
    return external_bus_.read16(address);
}

u32 TcmBusRouter::read32(Address address) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    u32 value = 0U;
    if (try_read_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_read_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return value;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        return read_main_ram_le<u32>(main_ram_, address);
    }
    return external_bus_.read32(address);
}

u16 TcmBusRouter::fetch16(Address address) {
    primitives::ScopedBusSample bus_sample(profiler_);
    u16 value = 0U;
    if (try_read_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return value;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        return read_main_ram_le<u16>(main_ram_, address);
    }
    return external_bus_.fetch16(address);
}

u32 TcmBusRouter::fetch32(Address address) {
    primitives::ScopedBusSample bus_sample(profiler_);
    u32 value = 0U;
    if (try_read_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return value;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        return read_main_ram_le<u32>(main_ram_, address);
    }
    return external_bus_.fetch32(address);
}

void TcmBusRouter::write8(Address address, u8 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    if (try_write_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_write_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        write_main_ram_le<u8>(main_ram_, address, value);
        return;
    }
    external_bus_.write8(address, value);
}

void TcmBusRouter::write16(Address address, u16 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    if (try_write_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_write_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        write_main_ram_le<u16>(main_ram_, address, value);
        return;
    }
    external_bus_.write16(address, value);
}

void TcmBusRouter::write32(Address address, u32 value) {
    primitives::ScopedBusSample bus_sample((address >> 24U) != 0x04U ? profiler_ : nullptr);
    if (try_write_tcm({cp15_.dtcm_config(), dtcm_}, address, value) ||
        try_write_tcm({cp15_.itcm_config(), itcm_}, address, value)) {
        return;
    }
    if (main_ram_ != nullptr && (address >> 24U) == 0x02U) {
        write_main_ram_le<u32>(main_ram_, address, value);
        return;
    }
    external_bus_.write32(address, value);
}

}  // namespace elmasds::cpu
