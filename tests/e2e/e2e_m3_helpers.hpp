#pragma once

#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace elmasds::e2e::m3 {

// Slot-1 ownership & Cartridge MMIO constants
inline constexpr Address kRegExMemCnt = 0x04000204U;
inline constexpr Address kRegExMemCntHigh = 0x04000205U;
inline constexpr u16 kExMemSlot1Arm7Bit = 0x0800U;

inline constexpr Address kRegAuxSpiCnt = 0x040001A0U;
inline constexpr Address kRegAuxSpiCntHigh = 0x040001A1U;
inline constexpr Address kRegAuxSpiData = 0x040001A2U;
inline constexpr u16 kAuxSpiEnable = 0x8000U;
inline constexpr u16 kAuxSpiIrq = 0x4000U;
inline constexpr u16 kAuxSpiMode = 0x2000U;
inline constexpr u16 kAuxSpiHold = 0x0040U;
inline constexpr u16 kAuxSpiMask = 0xE043U;

inline constexpr Address kRegRomCtrl = 0x040001A4U;
inline constexpr Address kRegRomCmdLo = 0x040001A8U;
inline constexpr Address kRegRomCmdHi = 0x040001ACU;
inline constexpr Address kRegRomSeed0Lo = 0x040001B0U;
inline constexpr Address kRegRomSeed1Lo = 0x040001B4U;
inline constexpr Address kRegRomSeed0Hi = 0x040001B8U;
inline constexpr Address kRegRomSeed1Hi = 0x040001BAU;
inline constexpr Address kRegRomData = 0x04100010U;
inline constexpr u32 kRomCtrlBusyBit = 1U << 31U;
inline constexpr u32 kRomCtrlDrqBit = 1U << 23U;

// ARM7 SPI Firmware / PMIC / Touch MMIO constants
inline constexpr Address kRegSpiCnt = 0x040001C0U;
inline constexpr Address kRegSpiData = 0x040001C2U;
inline constexpr u16 kSpiEnable = 0x8000U;
inline constexpr u16 kSpiIrq = 0x4000U;
inline constexpr u16 kSpiCsHold = 0x0800U;
inline constexpr u16 kSpiDeviceFirmware = 0x0100U;

// System control, Interrupt & DMA0 MMIO constants
inline constexpr Address kRegWramCnt = 0x04000247U;
inline constexpr Address kRegPostFlg = 0x04000300U;
inline constexpr Address kRegIme = 0x04000208U;
inline constexpr Address kRegIe = 0x04000210U;
inline constexpr Address kRegIf = 0x04000214U;
inline constexpr u32 kIrqCartridgeMask = 1U << 19U;
inline constexpr u32 kIrqDma0Mask = 1U << 8U;

inline constexpr Address kRegDma0Sad = 0x040000B0U;
inline constexpr Address kRegDma0Dad = 0x040000B4U;
inline constexpr Address kRegDma0CntL = 0x040000B8U;
inline constexpr Address kRegDma0CntH = 0x040000BAU;
inline constexpr u16 kDmaEnable = 0x8000U;
inline constexpr u16 kDmaIrqEnable = 0x4000U;
inline constexpr u16 kDmaWidth32 = 0x0400U;
inline constexpr u16 kDmaRepeat = 0x0200U;
inline constexpr u16 kDmaSrcFixed = 0x0100U;
inline constexpr u16 kDma9TimingCartridge = 5U << 11U;
inline constexpr u16 kDma7TimingCartridge = 2U << 12U;

// Post-boot Main RAM, WRAM & default guest entry constants
inline constexpr Address kRamHeaderBase = 0x027FFE00U;
inline constexpr Address kRamPostBiosBase = 0x027FF800U;
inline constexpr Address kRamFwUserSettings = 0x027FFC80U;
inline constexpr Address kSharedWramBase = 0x03000000U;
inline constexpr Address kArm9SharedWramBase = 0x03004000U;
inline constexpr Address kArm9DefaultBase = 0x02000000U;
inline constexpr Address kArm7DefaultBase = 0x03800000U;
inline constexpr Address kResultBase = 0x02100000U;
inline constexpr Address kArm9IrqSlot = 0x03003FFCU;
inline constexpr Address kArm7IrqSlot = 0x0380FFFCU;

/**
 * @brief Save type hints matching `SaveMemory::SaveType` ordinals for `attach_save()`.
 * @note Differs in ordering from `SyntheticSaveKind`; pass these values to `attach_save()`.
 */
enum class M3SaveTypeHint : u32 {
    None = 0U,
    Eeprom512B = 1U,
    Eeprom8KB = 2U,
    Eeprom64KB = 3U,
    Flash256KB = 4U,
    Flash512KB = 5U,
    Flash1MB = 6U,
    Sram32KB = 7U,
};

/**
 * @brief Returns the Main RAM result address for 32-bit observation `slot`.
 */
[[nodiscard]] constexpr Address result_slot_addr(u32 slot) noexcept {
    return kResultBase + (slot * 4U);
}

/**
 * @brief Builds a `ROMCTRL` command start word with Start/Busy bit and 3-bit block size code.
 */
[[nodiscard]] constexpr u32 make_romctrl_cmd_word(u32 block_code, u32 flags = 0U) noexcept {
    return kRomCtrlBusyBit | ((block_code & 7U) << 24U) | flags;
}

/**
 * @brief Returns the lower 32-bit `ROMCMD` word (`0x040001A8`) for a `0xB7` read at `rom_addr`.
 */
[[nodiscard]] constexpr u32 make_rom_cmd_0xb7_lo(u32 rom_addr) noexcept {
    return 0xB7U | (((rom_addr >> 24U) & 0xFFU) << 8U) | (((rom_addr >> 16U) & 0xFFU) << 16U) |
           (((rom_addr >> 8U) & 0xFFU) << 24U);
}

/**
 * @brief Returns the upper 32-bit `ROMCMD` word (`0x040001AC`) for a `0xB7` read at `rom_addr`.
 */
[[nodiscard]] constexpr u32 make_rom_cmd_0xb7_hi(u32 rom_addr) noexcept {
    return rom_addr & 0xFFU;
}

/**
 * @brief Reads a little-endian 16-bit integer from `data` at `offset`.
 */
[[nodiscard]] u16 read_le16(std::span<const u8> data, std::size_t offset = 0U);

/**
 * @brief Reads a little-endian 32-bit integer from `data` at `offset`.
 */
[[nodiscard]] u32 read_le32(std::span<const u8> data, std::size_t offset = 0U);

/**
 * @brief Writes a little-endian 16-bit `value` into `data` at `offset`.
 */
void write_le16(std::span<u8> data, std::size_t offset, u16 value);

/**
 * @brief Writes a little-endian 32-bit `value` into `data` at `offset`.
 */
void write_le32(std::span<u8> data, std::size_t offset, u32 value);

/**
 * @brief Patches a 16-bit header/ROM field at `offset` and recomputes header CRC-16 (`0x15E`).
 */
void patch_u16_and_recompute_header_crc(std::vector<u8>& rom, std::size_t offset, u16 value);

/**
 * @brief Patches a 32-bit header/ROM field at `offset` and recomputes header CRC-16 (`0x15E`).
 */
void patch_u32_and_recompute_header_crc(std::vector<u8>& rom, std::size_t offset, u32 value);

/**
 * @brief Returns a single-instruction infinite loop (`B .`) word vector.
 */
[[nodiscard]] std::vector<u32> make_idle_words();

/**
 * @brief Creates a valid `NdsRomSpec` with default entry addresses and encoded ARM9/ARM7 words.
 */
[[nodiscard]] NdsRomSpec make_base_rom_spec(std::vector<u32> arm9_words,
                                            std::vector<u32> arm7_words);

/**
 * @brief Builds a synthetic `.nds` ROM from `spec`, boots it via `direct_boot()`, and steps.
 */
void boot_rom_and_step(EmulatorE2eHarness& harness, const NdsRomSpec& spec, TickCount ticks);

/**
 * @brief Loads `rom_bytes`, boots via `direct_boot()`, and runs for `ticks` master cycles.
 */
void boot_bytes_and_step(EmulatorE2eHarness& harness,
                         std::span<const u8> rom_bytes,
                         TickCount ticks);

}  // namespace elmasds::e2e::m3
