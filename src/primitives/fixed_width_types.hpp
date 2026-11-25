#pragma once

#include <cstdint>

namespace elmasds {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using Address = std::uint32_t;
using TickCount = std::uint64_t;

/// Native width of each Nintendo DS LCD panel in pixels.
inline constexpr u32 kScreenWidth = 256;

/// Native height of a single Nintendo DS LCD panel in pixels.
inline constexpr u32 kScreenHeight = 192;

/// Combined native height of both stacked Nintendo DS panels (excluding gap).
inline constexpr u32 kDualScreenHeight = kScreenHeight * 2;

/// Master scheduler clock: ARM946E-S clock frequency (67.027964 MHz).
inline constexpr u64 kArm9ClockHz = 67'027'964ULL;

/// ARM7TDMI clock frequency (33.513982 MHz, half of ARM9 clock).
inline constexpr u64 kArm7ClockHz = 33'513'982ULL;

/// Number of master scheduler ticks per ARM9 cycle.
inline constexpr TickCount kArm9TicksPerCycle = 1;

/// Number of master scheduler ticks per ARM7 cycle.
inline constexpr TickCount kArm7TicksPerCycle = 2;

}  // namespace elmasds
