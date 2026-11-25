#pragma once

#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elmasds::e2e {

inline constexpr std::size_t kBios9Size = 4U * 1024U;
inline constexpr std::size_t kBios7Size = 16U * 1024U;
inline constexpr u32 kFirmwareSize128K = 128U * 1024U;
inline constexpr u32 kFirmwareSize256K = 256U * 1024U;
inline constexpr u32 kFirmwareSize512K = 512U * 1024U;

struct BiosSpec {
    std::optional<Address> irq_pointer_addr = std::nullopt;
    Address fallback_irq_pointer_addr = 0x027FFFC0U;
    std::optional<Address> reset_pc = std::nullopt;
    bool include_irq_trampoline = true;
    /// Note: BIOS7 SWI halt uses R0/R1 as scratch per NDS SWI ABI.
    bool include_swi_halt = false;
};

struct FirmwareSpec {
    u32 size_bytes = kFirmwareSize256K;
    std::array<u8, 6> mac_address{0x00U, 0x09U, 0xBFU, 0x12U, 0x34U, 0x56U};
    u8 console_type = 0xFFU;
    std::u16string nickname = u"Elmas";
    std::u16string message = u"ElmasDS";
    u8 favorite_color = 0U;
    u8 birthday_month = 1U;
    u8 birthday_day = 1U;
    u8 language = 1U;
    bool corrupt_wifi_crc = false;
    bool corrupt_user_settings_crc = false;
};

enum class SyntheticSaveKind : u8 {
    Sram32K = 0,
    Eeprom512B,
    Eeprom8K,
    Eeprom64K,
    Eeprom128K,
    Flash256K,
    Flash512K,
    Flash1M,
};

struct SaveImageSpec {
    SyntheticSaveKind kind = SyntheticSaveKind::Eeprom8K;
    u8 fill_byte = 0xFFU;
    std::vector<u8> initial_patch;
    u32 patch_offset = 0U;
};

/**
 * @brief RAII temporary directory under `std::filesystem::temp_directory_path()`.
 */
class ScopedTempDir {
public:
    ScopedTempDir();
    ~ScopedTempDir();

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;

    ScopedTempDir(ScopedTempDir&& other) noexcept;
    ScopedTempDir& operator=(ScopedTempDir&& other) noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::filesystem::path write_file(std::string_view filename,
                                                   std::span<const u8> data) const;
    [[nodiscard]] std::vector<u8> read_file(std::string_view filename) const;

private:
    std::filesystem::path dir_path_;
};

[[nodiscard]] std::vector<u8> build_synthetic_bios9(const BiosSpec& spec = {});

[[nodiscard]] std::vector<u8> build_synthetic_bios7(const BiosSpec& spec = {});

[[nodiscard]] std::vector<u8> build_synthetic_firmware(const FirmwareSpec& spec = {});

[[nodiscard]] std::size_t save_image_byte_size(SyntheticSaveKind kind) noexcept;

[[nodiscard]] std::vector<u8> build_save_image(const SaveImageSpec& spec = {});

void write_save_file(const std::filesystem::path& file_path, std::span<const u8> data);

[[nodiscard]] std::vector<u8> read_save_file(const std::filesystem::path& file_path);

}  // namespace elmasds::e2e
