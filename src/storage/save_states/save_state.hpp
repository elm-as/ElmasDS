#pragma once

#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace elmasds::core {
class Emulator;
}

namespace elmasds::storage::save_states {

/// 8-byte ASCII magic signature `"ELMASDSS"` identifying an ElmasDS save state file.
inline constexpr std::array<u8, 8U> kSaveStateMagic = {
    'E', 'L', 'M', 'A', 'S', 'D', 'S', 'S',
};

/// Current binary save state format version.
inline constexpr u32 kSaveStateFormatVersion = 1U;

/**
 * @brief Serializes and restores complete deterministic snapshots of an `Emulator` instance.
 *
 * Responsibility:
 *   Produces and validates versioned binary save state snapshots (`.ess`) containing the
 *   format magic, version number, loaded ROM identity (`game_code` and `header_crc16`),
 *   ARM946E-S and ARM7TDMI register banks, CP15 and ITCM/DTCM state, Main RAM, Shared WRAM,
 *   ARM7 WRAM, 9 VRAM banks + `VRAMCNT`, Palette RAM, OAM, Interrupts, Keypad, Audio, and
 *   GPU registers. Rejects corrupted headers, unsupported format versions, or mismatched ROMs.
 */
class SaveStateManager final {
public:
    /**
     * @brief Captures the complete state of `emulator` into a binary byte vector.
     */
    [[nodiscard]] static Result<std::vector<u8>> serialize(const core::Emulator& emulator);

    /**
     * @brief Restores `emulator` from a serialized save-state byte span.
     */
    [[nodiscard]] static Status deserialize(core::Emulator& emulator,
                                            std::span<const u8> state_bytes);

    /**
     * @brief Saves the current state of `emulator` to `file_path`.
     */
    [[nodiscard]] static Status save_to_file(const core::Emulator& emulator,
                                             const std::filesystem::path& file_path);

    /**
     * @brief Loads and restores `emulator` state from `file_path`.
     */
    [[nodiscard]] static Status load_from_file(core::Emulator& emulator,
                                               const std::filesystem::path& file_path);
};

}  // namespace elmasds::storage::save_states
