#pragma once

#include <filesystem>

namespace elmasds::frontend {

/**
 * @brief Executes `frames` headless frames on `rom_path` and prints CPU/GPU/Cartridge state.
 */
[[nodiscard]] int run_diag_frames(const std::filesystem::path& rom_path, int frames);

}  // namespace elmasds::frontend
