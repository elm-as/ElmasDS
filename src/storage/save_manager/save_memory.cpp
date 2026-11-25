#include "storage/save_manager/save_memory.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace elmasds::storage::save_manager {

SaveMemory::SaveMemory(SaveType type) {
    configure(type);
}

void SaveMemory::configure(SaveType type) {
    type_ = type;
    const std::size_t size = save_type_size_bytes(type);
    storage_.assign(size, u8{0xFF});
    dirty_ = false;
    reset();
}

void SaveMemory::reset() {
    state_ = State::Idle;
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;
    write_enabled_ = false;
}

Status SaveMemory::attach_file(const std::filesystem::path& path, SaveType type_hint) {
    if (path.empty()) {
        return Status::err(ErrorCode::InvalidArgument, "Save file path must not be empty.");
    }

    file_path_ = path;
    std::error_code fs_err;
    if (std::filesystem::exists(path, fs_err)) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            return Status::err(ErrorCode::IoFailure, "Failed to open save file: " + path.string());
        }
        const std::streamsize raw_size = file.tellg();
        if (raw_size < 0) {
            return Status::err(ErrorCode::IoFailure, "Failed to read save file size.");
        }

        const std::size_t file_size = static_cast<std::size_t>(raw_size);
        SaveType resolved = (type_hint != SaveType::None) ? type_hint
                                                          : detect_save_type_from_size(file_size);
        if (resolved == SaveType::None) {
            resolved = SaveType::Flash256KB;
        }
        configure(resolved);

        file.seekg(0, std::ios::beg);
        const std::size_t copy_len = std::min(file_size, storage_.size());
        if (copy_len > 0U &&
            !file.read(reinterpret_cast<char*>(storage_.data()),
                       static_cast<std::streamsize>(copy_len))) {
            return Status::err(ErrorCode::IoFailure, "Failed to read save file contents.");
        }
        dirty_ = false;
        return Status::ok();
    }

    const SaveType resolved = (type_hint != SaveType::None) ? type_hint : SaveType::Flash256KB;
    configure(resolved);
    return Status::ok();
}

Status SaveMemory::flush_if_dirty() {
    if (!dirty_ || file_path_.empty() || storage_.empty()) {
        return Status::ok();
    }

    std::ofstream file(file_path_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return Status::err(ErrorCode::IoFailure,
                           "Failed to open save file for writing: " + file_path_.string());
    }

    file.write(reinterpret_cast<const char*>(storage_.data()),
               static_cast<std::streamsize>(storage_.size()));
    if (!file.good()) {
        return Status::err(ErrorCode::IoFailure, "Failed to write save file data.");
    }

    dirty_ = false;
    return Status::ok();
}

u8 SaveMemory::read_direct(std::size_t offset) const noexcept {
    if (storage_.empty()) {
        return 0xFFU;
    }
    return storage_[offset % storage_.size()];
}

void SaveMemory::write_direct(std::size_t offset, u8 value) noexcept {
    if (storage_.empty()) {
        return;
    }
    storage_[offset % storage_.size()] = value;
    dirty_ = true;
}

u8 SaveMemory::transfer(u8 tx_byte, bool hold_chip_select) {
    if (type_ == SaveType::None || storage_.empty()) {
        return 0xFFU;
    }

    u8 response = 0xFFU;
    switch (state_) {
        case State::Idle:
            response = handle_command(tx_byte);
            break;
        case State::ReadStatus:
            response = write_enabled_ ? 0x02U : 0x00U;
            break;
        case State::ReadJedecId: {
            static constexpr std::array<u8, 3U> kJedec = {0x20U, 0x40U, 0x13U};
            response = (jedec_index_ < kJedec.size()) ? kJedec[jedec_index_++] : 0x00U;
            break;
        }
        case State::ReadAddress:
        case State::WriteAddress:
        case State::PageEraseAddress:
            address_ = (address_ << 8U) | tx_byte;
            ++address_bytes_received_;
            if (address_bytes_received_ >= address_byte_count()) {
                if (state_ == State::ReadAddress) {
                    state_ = State::ReadData;
                } else if (state_ == State::WriteAddress) {
                    state_ = State::WriteData;
                } else {
                    erase_page(address_);
                    state_ = State::Idle;
                }
            }
            response = 0U;
            break;
        case State::ReadData:
            response = storage_[address_ % storage_.size()];
            ++address_;
            break;
        case State::WriteData:
            if (write_enabled_) {
                storage_[address_ % storage_.size()] = tx_byte;
                dirty_ = true;
            }
            ++address_;
            response = 0U;
            break;
    }

    if (!hold_chip_select) {
        release_chip_select();
    }
    return response;
}

void SaveMemory::release_chip_select() {
    if (state_ == State::WriteData) {
        write_enabled_ = false;
    }
    state_ = State::Idle;
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;
}

u8 SaveMemory::address_byte_count() const noexcept {
    switch (type_) {
        case SaveType::Eeprom512B:
            return 1U;
        case SaveType::Eeprom8KB:
        case SaveType::Eeprom64KB:
            return 2U;
        default:
            return 3U;
    }
}

u8 SaveMemory::handle_command(u8 command) noexcept {
    address_ = 0U;
    address_bytes_received_ = 0U;
    jedec_index_ = 0U;

    // 512-byte EEPROM encodes address bit 8 in opcode bit 3 (0x0B read / 0x0A write).
    if (type_ == SaveType::Eeprom512B && (command == 0x0BU || command == 0x0AU)) {
        address_ = 1U;
        state_ = (command == 0x0BU) ? State::ReadAddress : State::WriteAddress;
        return 0U;
    }

    switch (command) {
        case 0x03U:  // Read Data
            state_ = State::ReadAddress;
            return 0U;
        case 0x02U:  // Page Program / EEPROM Write
        case 0x0AU:  // Flash Page Write
            state_ = State::WriteAddress;
            return 0U;
        case 0x05U:  // Read Status Register
            state_ = State::ReadStatus;
            return 0U;
        case 0x06U:  // Write Enable
            write_enabled_ = true;
            return 0U;
        case 0x04U:  // Write Disable
            write_enabled_ = false;
            return 0U;
        case 0x9FU:  // Read JEDEC ID
            state_ = State::ReadJedecId;
            return 0U;
        case 0xDBU:  // Page Erase (256 bytes)
            state_ = State::PageEraseAddress;
            return 0U;
        default:
            state_ = State::Idle;
            return 0U;
    }
}

void SaveMemory::erase_page(u32 address) noexcept {
    if (!write_enabled_ || storage_.empty()) {
        return;
    }
    const std::size_t base = (address & ~0xFFU) % storage_.size();
    const std::size_t end = std::min(base + 256U, storage_.size());
    std::fill(storage_.begin() + static_cast<std::ptrdiff_t>(base),
              storage_.begin() + static_cast<std::ptrdiff_t>(end), u8{0xFF});
    dirty_ = true;
    write_enabled_ = false;
}

}  // namespace elmasds::storage::save_manager

