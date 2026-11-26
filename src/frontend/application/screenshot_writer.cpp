#include "frontend/application/screenshot_writer.hpp"

#include <array>
#include <fstream>
#include <vector>

namespace elmasds::frontend {
namespace {

constexpr std::size_t kScreenPixels = static_cast<std::size_t>(kScreenWidth) * kScreenHeight;

void write_le16(std::array<u8, 54U>& dst, std::size_t off, u16 v) noexcept {
    dst[off] = static_cast<u8>(v & 0xFFU);
    dst[off + 1U] = static_cast<u8>((v >> 8U) & 0xFFU);
}

void write_le32(std::array<u8, 54U>& dst, std::size_t off, u32 v) noexcept {
    dst[off] = static_cast<u8>(v & 0xFFU);
    dst[off + 1U] = static_cast<u8>((v >> 8U) & 0xFFU);
    dst[off + 2U] = static_cast<u8>((v >> 16U) & 0xFFU);
    dst[off + 3U] = static_cast<u8>((v >> 24U) & 0xFFU);
}

void append_bgr_screen(std::vector<u8>& out, std::span<const u32> pixels) {
    for (std::size_t i = 0U; i < kScreenPixels; ++i) {
        const u32 px = pixels[i];
        out.push_back(static_cast<u8>(px & 0xFFU));
        out.push_back(static_cast<u8>((px >> 8U) & 0xFFU));
        out.push_back(static_cast<u8>((px >> 16U) & 0xFFU));
    }
}

}  // namespace

Status save_dual_screen_bmp(const std::filesystem::path& path,
                            std::span<const u32> top_pixels,
                            std::span<const u32> bottom_pixels) {
    if (path.empty() || top_pixels.size() < kScreenPixels ||
        bottom_pixels.size() < kScreenPixels) {
        return Status::err(ErrorCode::InvalidArgument, "Invalid screenshot path or framebuffer size.");
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    constexpr u32 kWidth = kScreenWidth;
    constexpr u32 kHeight = kDualScreenHeight;
    constexpr u32 kPixelBytes = kWidth * kHeight * 3U;
    std::array<u8, 54U> header{};
    header[0U] = 'B';
    header[1U] = 'M';
    write_le32(header, 2U, 54U + kPixelBytes);
    write_le32(header, 10U, 54U);
    write_le32(header, 14U, 40U);
    write_le32(header, 18U, kWidth);
    write_le32(header, 22U, static_cast<u32>(-static_cast<i32>(kHeight)));
    write_le16(header, 26U, 1U);
    write_le16(header, 28U, 24U);
    write_le32(header, 34U, kPixelBytes);

    std::vector<u8> bgr_data;
    bgr_data.reserve(kPixelBytes);
    append_bgr_screen(bgr_data, top_pixels);
    append_bgr_screen(bgr_data, bottom_pixels);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Status::err(ErrorCode::IoFailure, "Failed to open screenshot file: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    out.write(reinterpret_cast<const char*>(bgr_data.data()), static_cast<std::streamsize>(bgr_data.size()));
    if (!out.good()) {
        return Status::err(ErrorCode::IoFailure, "Failed to write screenshot BMP data.");
    }
    return Status::ok();
}

}  // namespace elmasds::frontend
