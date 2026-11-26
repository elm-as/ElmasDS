#pragma once

#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "primitives/fixed_width_types.hpp"

#include <span>

namespace elmasds::e2e::m3 {

/**
 * @brief Configuration for `emit_auxspi_seq()` multi-byte AUXSPI transfers.
 */
struct AuxSpiSeqSpec {
    u16 base_cnt = kAuxSpiEnable | kAuxSpiMode;
    std::span<const u8> tx_bytes;
    Address rx_ram_addr = 0U;
    bool release_on_last = true;
    bool store_all_rx = true;
};

/**
 * @brief Configuration for `emit_spi_firmware_read()` ARM7 SPI firmware reads.
 */
struct SpiFirmwareReadSpec {
    Address dst_ram = kResultBase;
    u32 fw_offset = 0U;
    u32 byte_count = 0U;
};

/**
 * @brief Reads a 16-bit value at `addr` into `dst` (`LDRH dst, [dst]`).
 */
void emit_read16_abs(ArmProgramEmitter& emit, ArmReg dst, Address addr);

/**
 * @brief Reads an 8-bit value at `addr` into `dst` (`LDRB dst, [dst]`).
 */
void emit_read8_abs(ArmProgramEmitter& emit, ArmReg dst, Address addr);

/**
 * @brief Stores `value_reg` into Main RAM `result_slot_addr(slot)` (clobbers `R12` or `R11`).
 */
void emit_store_result(ArmProgramEmitter& emit, u32 slot, ArmReg value_reg);

/**
 * @brief Stores immediate `value` into Main RAM `result_slot_addr(slot)` (clobbers `R0`, `R1`).
 */
void emit_store_const_result(ArmProgramEmitter& emit, u32 slot, u32 value);

/**
 * @brief Writes 8-byte cartridge command words to `ROMCMD` (`0x040001A8..0x040001AF`).
 * @note Clobbers `R0` and `R1`.
 */
void emit_rom_cmd_words(ArmProgramEmitter& emit, u32 cmd_lo, u32 cmd_hi);

/**
 * @brief Writes a `0xB7` main ROM read command for `rom_addr` to `ROMCMD` (clobbers `R0`, `R1`).
 */
void emit_rom_cmd_0xb7(ArmProgramEmitter& emit, u32 rom_addr);

/**
 * @brief Starts a `ROMCTRL` transfer with Start/Busy bit, `block_code`, and optional `flags`.
 * @note Clobbers `R0` and `R1`.
 */
void emit_start_romctrl(ArmProgramEmitter& emit, u32 block_code, u32 flags = 0U);

/**
 * @brief Polls `ROMCTRL` bit 23 (DRQ) up to `max_iters` times (clobbers `scratch` and `R12`/`R11`).
 */
void emit_poll_romctrl_drq_bounded(ArmProgramEmitter& emit,
                                   ArmReg scratch,
                                   u32 max_iters = 64U);

/**
 * @brief Bounded convenience wrapper polling `ROMCTRL` DRQ (clobbers `scratch` and `R12`/`R11`).
 */
void emit_wait_rom_drq(ArmProgramEmitter& emit, ArmReg scratch);

/**
 * @brief Polls DRQ and reads `word_count` words from `ROMDATA` into `dst_ram`.
 * @note Clobbers `R0`, `R1`, `R2`, and `R12`.
 */
void emit_read_romdata_words(ArmProgramEmitter& emit, u32 dst_ram, u32 word_count);

/**
 * @brief Configures `AUXSPICNT`, clocks `tx_byte` via `AUXSPIDATA`, and loads RX byte into `R0`.
 * @note Clobbers `R0` and `R1`.
 */
void emit_auxspi_tx(ArmProgramEmitter& emit, u16 auxspicnt, u8 tx_byte);

/**
 * @brief Clocks a sequence of AUXSPI bytes and optionally stores RX bytes to `spec.rx_ram_addr`.
 * @note Clobbers `R0` and `R1`.
 */
void emit_auxspi_seq(ArmProgramEmitter& emit, const AuxSpiSeqSpec& spec);

/**
 * @brief Configures `SPICNT`, clocks `tx_byte` via `SPIDATA`, and loads RX byte into `R0`.
 * @note Clobbers `R0` and `R1`.
 */
void emit_spi_firmware_byte(ArmProgramEmitter& emit, u16 spicnt, u8 tx_byte);

/**
 * @brief Reads `spec.byte_count` firmware bytes from `spec.fw_offset` into `spec.dst_ram`.
 * @note Clobbers `R0` and `R1`.
 */
void emit_spi_firmware_read(ArmProgramEmitter& emit, const SpiFirmwareReadSpec& spec);

/**
 * @brief Emits an infinite branch-self halt (`B .`) and flushes the pending literal pool.
 */
void emit_halt_loop(ArmProgramEmitter& emit);

/**
 * @brief Emits `MRC p15, 0, dst, c1, c0, 0` (CP15 Control Register) on ARM9.
 */
void emit_cp15_read_c1(ArmProgramEmitter& emit, ArmReg dst);

/**
 * @brief Emits `MRC p15, 0, dst, c9, c1, 0` (CP15 DTCM Region Register) on ARM9.
 */
void emit_cp15_read_dtcm(ArmProgramEmitter& emit, ArmReg dst);

/**
 * @brief Emits `MRC p15, 0, dst, c9, c1, 1` (CP15 ITCM Region Register) on ARM9.
 */
void emit_cp15_read_itcm(ArmProgramEmitter& emit, ArmReg dst);

}  // namespace elmasds::e2e::m3
