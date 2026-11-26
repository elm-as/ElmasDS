#pragma once

#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <filesystem>
#include <span>

namespace elmasds::frontend {

/**
 * @brief Saves stacked top (`256x192`) and bottom (`256x192`) `ARGB8888` framebuffers as a `256x384` BMP.
 */
[[nodiscard]] Status save_dual_screen_bmp(const std::filesystem::path& path,
                                          std::span<const u32> top_pixels,
                                          std::span<const u32> bottom_pixels);

}  // namespace elmasds::frontend
