#pragma once

#include "hardware/spi/spi_device.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace elmasds::storage::save_manager {

/**
 * @brief Supported Nintendo DS cartridge save memory types and capacities.
 */
enum class SaveType : u8 {
    None = 0,
    Eeprom512B,
    Eeprom8KB,
    Eeprom64KB,
    Flash256KB,
    Flash512KB,
    Flash1MB,
    Sram32KB,
};

/**
 * @brief Returns the byte capacity of `type` (`0` for `SaveType::None`).
 */
[[nodiscard]] constexpr std::size_t save_type_size_bytes(SaveType type) noexcept {
    switch (type) {
        case SaveType::Eeprom512B:
            return 512U;
        case SaveType::Eeprom8KB:
            return 8U * 1024U;
        case SaveType::Eeprom64KB:
            return 64U * 1024U;
        case SaveType::Flash256KB:
            return 256U * 1024U;
        case SaveType::Flash512KB:
            return 512U * 1024U;
        case SaveType::Flash1MB:
            return 1024U * 1024U;
        case SaveType::Sram32KB:
            return 32U * 1024U;
        case SaveType::None:
        default:
            return 0U;
    }
}

/**
 * @brief Infers a `SaveType` from an existing `.sav` file's byte size (`None` if unrecognized).
 */
[[nodiscard]] constexpr SaveType detect_save_type_from_size(std::size_t size_bytes) noexcept {
    switch (size_bytes) {
        case 512U:
            return SaveType::Eeprom512B;
        case 8U * 1024U:
            return SaveType::Eeprom8KB;
        case 32U * 1024U:
            return SaveType::Sram32KB;
        case 64U * 1024U:
            return SaveType::Eeprom64KB;
        case 256U * 1024U:
            return SaveType::Flash256KB;
        case 512U * 1024U:
            return SaveType::Flash512KB;
        case 1024U * 1024U:
            return SaveType::Flash1MB;
        default:
            return SaveType::None;
    }
}

/**
 * @brief Cartridge save memory supporting EEPROM, Flash, and SRAM with deferred disk flush.
 *
 * Responsibility:
 *   Implements the cartridge auxiliary SPI protocol (`AUXSPIDATA`) for EEPROM (512B/8KB/64KB),
 *   Flash (256KB/512KB/1MB), and direct SRAM (32KB), tracks dirty writes, and persists changes
 *   to disk on explicit `flush_if_dirty()` calls.
 */
class SaveMemory final : public hardware::spi::SpiDevice {
public:
    explicit SaveMemory(SaveType type = SaveType::None);

    void reset() override;
    [[nodiscard]] u8 transfer(u8 tx_byte, bool hold_chip_select) override;
    void release_chip_select() override;

    /**
     * @brief Configures in-memory save storage for `type` (filled with `0xFF`), clearing dirty bit.
     */
    void configure(SaveType type);

    /**
     * @brief Attaches a backing `.sav` file, loading existing data and auto-detecting capacity.
     */
    [[nodiscard]] Status attach_file(const std::filesystem::path& path,
                                     SaveType type_hint = SaveType::None);

    /**
     * @brief Flushes modified save data to the attached file if dirty; no-op when clean.
     */
    [[nodiscard]] Status flush_if_dirty();

    [[nodiscard]] u8 read_direct(std::size_t offset) const noexcept;
    void write_direct(std::size_t offset, u8 value) noexcept;

    [[nodiscard]] SaveType type() const noexcept { return type_; }
    [[nodiscard]] bool is_dirty() const noexcept { return dirty_; }
    [[nodiscard]] std::span<const u8> data() const noexcept { return storage_; }
    [[nodiscard]] const std::filesystem::path& file_path() const noexcept { return file_path_; }

private:
    enum class State : u8 {
        Idle = 0,
        IrReadId,
        ReadStatus,
        WriteStatus,
        ReadJedecId,
        ReadAddress,
        FastReadAddress,
        FastReadDummy,
        ReadData,
        WriteAddress,
        WriteData,
        PageEraseAddress,
        SubsectorEraseAddress,
        SectorEraseAddress,
    };

    [[nodiscard]] u8 address_byte_count() const noexcept;
    [[nodiscard]] u8 handle_command(u8 command) noexcept;
    void erase_region(u32 base_mask, std::size_t region_size) noexcept;

    SaveType type_ = SaveType::None;
    std::vector<u8> storage_;
    std::filesystem::path file_path_;
    bool dirty_ = false;

    State state_ = State::Idle;
    u32 address_ = 0U;
    u8 address_bytes_received_ = 0U;
    u8 jedec_index_ = 0U;
    bool write_enabled_ = false;
};

}  // namespace elmasds::storage::save_manager

