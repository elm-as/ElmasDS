#pragma once

#include <filesystem>

namespace elmasds::frontend {

/**
 * @brief Runs a 3-scenario SDL3+OpenGL performance audit on `rom_path` and prints metrics.
 */
[[nodiscard]] int run_profile_audit(const std::filesystem::path& rom_path);

}  // namespace elmasds::frontend
