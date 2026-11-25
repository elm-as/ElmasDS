#pragma once

#include "hardware/spi/spi_devices.hpp"
#include "memory/bios/bios_memory.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace elmasds::storage::firmware {

inline constexpr std::size_t kArm9BiosSize = 4U * 1024U;
inline constexpr std::size_t kArm7BiosSize = 16U * 1024U;
inline constexpr std::size_t kMinFirmwareSize = 128U * 1024U;

/**
 * @brief Reads a binary image from disk, optionally validating an exact byte size.
 */
[[nodiscard]] Result<std::vector<u8>> load_binary_file(const std::filesystem::path& path,
                                                       std::size_t expected_size = 0U);

/**
 * @brief Validates and loads a 4 KiB ARM9 BIOS image into `bios9`.
 */
[[nodiscard]] Status load_bios9_image(memory::BiosMemory& bios9, std::span<const u8> bytes);

/**
 * @brief Validates and loads a 16 KiB ARM7 BIOS image into `bios7`.
 */
[[nodiscard]] Status load_bios7_image(memory::BiosMemory& bios7, std::span<const u8> bytes);

/**
 * @brief Validates and loads an external Nintendo DS firmware flash image.
 */
[[nodiscard]] Status load_firmware_image(hardware::spi::FirmwareFlashDevice& firmware,
                                         std::span<const u8> bytes);

/**
 * @brief Synthesizes a clean non-proprietary 256 KiB firmware image with default user settings.
 */
[[nodiscard]] std::vector<u8> create_default_firmware_image();

}  // namespace elmasds::storage::firmware

