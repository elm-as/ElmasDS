#pragma once

#include "e2e/arm_program_emitter.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace elmasds::e2e::m2 {

// Default guest entry points, result RAM, and HLE BIOS IRQ slots after direct_boot()
inline constexpr Address kArm9DefaultEntry = 0x02000000U;
inline constexpr Address kArm7DefaultEntry = 0x03800000U;
inline constexpr Address kResultRamBase = 0x02100000U;
inline constexpr Address kArm9IrqSlot = 0x03003FFCU;
inline constexpr Address kArm7IrqSlot = 0x0380FFFCU;
inline constexpr Address kArm9BiosIrqFlags = 0x03003FF8U;
inline constexpr Address kArm7BiosIrqFlags = 0x0380FFF8U;
// ARM9 0x03000000..0x03003FFF hits DTCM after direct_boot(); Shared WRAM starts at 0x03004000
inline constexpr Address kSharedWramArm9Base = 0x03004000U;

// Interrupt & system control MMIO addresses
inline constexpr Address kRegIme = 0x04000208U;
inline constexpr Address kRegIe = 0x04000210U;
inline constexpr Address kRegIf = 0x04000214U;
inline constexpr Address kRegPostflg = 0x04000300U;
inline constexpr Address kRegHaltcnt = 0x04000301U;
inline constexpr Address kRegWramcnt = 0x04000247U;
inline constexpr Address kRegArm7Wramstat = 0x04000241U;
inline constexpr Address kRegExmemcnt = 0x04000204U;

// Timer & DMA MMIO base addresses
inline constexpr Address kRegTimerBase = 0x04000100U;
inline constexpr Address kRegDmaBase = 0x040000B0U;
inline constexpr Address kRegDmaFillBase = 0x040000E0U;

// IPC MMIO addresses
inline constexpr Address kRegIpcSync = 0x04000180U;
inline constexpr Address kRegIpcFifoCnt = 0x04000184U;
inline constexpr Address kRegIpcFifoSend = 0x04000188U;
inline constexpr Address kRegIpcFifoRecv = 0x04100000U;

// ARM7 SPI & RTC MMIO addresses
inline constexpr Address kRegSpiCnt = 0x040001C0U;
inline constexpr Address kRegSpiData = 0x040001C2U;
inline constexpr Address kRegRtc = 0x04000138U;

// IRQ source bitmasks (IE / IF)
inline constexpr u32 kIrqVBlank = 1U << 0U;
inline constexpr u32 kIrqHBlank = 1U << 1U;
inline constexpr u32 kIrqVCount = 1U << 2U;
inline constexpr u32 kIrqRtc = 1U << 7U;
inline constexpr u32 kIrqIpcSync = 1U << 16U;
inline constexpr u32 kIrqIpcSendEmpty = 1U << 17U;
inline constexpr u32 kIrqIpcRecvNotEmpty = 1U << 18U;
inline constexpr u32 kIrqCartTransfer = 1U << 19U;
inline constexpr u32 kIrqGxFifo = 1U << 21U;
inline constexpr u32 kIrqSpi = 1U << 23U;

// HALTCNT commands (ARM7 0x04000301)
inline constexpr u8 kHaltcntHalt = 0x80U;
inline constexpr u8 kHaltcntSleep = 0xC0U;

// Timer TMnCNT_H control flags
inline constexpr u16 kTimerPrescale1 = 0U;
inline constexpr u16 kTimerPrescale64 = 1U;
inline constexpr u16 kTimerPrescale256 = 2U;
inline constexpr u16 kTimerPrescale1024 = 3U;
inline constexpr u16 kTimerCascade = 1U << 2U;
inline constexpr u16 kTimerIrqEnable = 1U << 6U;
inline constexpr u16 kTimerStart = 1U << 7U;

// DMA DMAxCNT_H control flags
inline constexpr u16 kDmaDestInc = 0U << 5U;
inline constexpr u16 kDmaDestDec = 1U << 5U;
inline constexpr u16 kDmaDestFixed = 2U << 5U;
inline constexpr u16 kDmaDestReload = 3U << 5U;
inline constexpr u16 kDmaSrcInc = 0U << 7U;
inline constexpr u16 kDmaSrcDec = 1U << 7U;
inline constexpr u16 kDmaSrcFixed = 2U << 7U;
inline constexpr u16 kDmaRepeat = 1U << 9U;
inline constexpr u16 kDmaWidth32 = 1U << 10U;
inline constexpr u16 kDmaIrqEnable = 1U << 14U;
inline constexpr u16 kDmaEnable = 1U << 15U;

// IPC control flags
inline constexpr u16 kIpcSyncIrqRemote = 1U << 13U;
inline constexpr u16 kIpcSyncIrqEnable = 1U << 14U;
inline constexpr u16 kIpcFifoSendEmptyIrq = 1U << 2U;
inline constexpr u16 kIpcFifoSendClear = 1U << 3U;
inline constexpr u16 kIpcFifoRecvNotEmptyIrq = 1U << 10U;
inline constexpr u16 kIpcFifoError = 1U << 14U;
inline constexpr u16 kIpcFifoEnable = 1U << 15U;

// SPI SPICNT control flags (ARM7)
inline constexpr u16 kSpiDevicePowerman = 0U << 8U;
inline constexpr u16 kSpiDeviceFirmware = 1U << 8U;
inline constexpr u16 kSpiDeviceTouchscreen = 2U << 8U;
inline constexpr u16 kSpiDeviceNone = 3U << 8U;
inline constexpr u16 kSpiCsHold = 1U << 11U;
inline constexpr u16 kSpiIrqEnable = 1U << 14U;
inline constexpr u16 kSpiEnable = 1U << 15U;

// RTC register bitmasks (ARM7 0x04000138)
inline constexpr u8 kRtcSio = 1U << 0U;
inline constexpr u8 kRtcSck = 1U << 1U;
inline constexpr u8 kRtcCs = 1U << 2U;
inline constexpr u8 kRtcSioWrite = 1U << 4U;

[[nodiscard]] constexpr Address result_addr(u32 slot) noexcept {
    return kResultRamBase + (slot * 4U);
}

[[nodiscard]] constexpr Address timer_cnt_l(u32 channel) noexcept {
    return kRegTimerBase + (channel * 4U);
}

[[nodiscard]] constexpr Address timer_cnt_h(u32 channel) noexcept {
    return kRegTimerBase + (channel * 4U) + 2U;
}

[[nodiscard]] constexpr u32 timer_irq_bit(u32 channel) noexcept {
    return 1U << (3U + channel);
}

[[nodiscard]] constexpr Address dma_sad(u32 channel) noexcept {
    return kRegDmaBase + (channel * 12U);
}

[[nodiscard]] constexpr Address dma_dad(u32 channel) noexcept {
    return kRegDmaBase + (channel * 12U) + 4U;
}

[[nodiscard]] constexpr Address dma_cnt_l(u32 channel) noexcept {
    return kRegDmaBase + (channel * 12U) + 8U;
}

[[nodiscard]] constexpr Address dma_cnt_h(u32 channel) noexcept {
    return kRegDmaBase + (channel * 12U) + 10U;
}

[[nodiscard]] constexpr Address dma_fill(u32 channel) noexcept {
    return kRegDmaFillBase + (channel * 4U);
}

[[nodiscard]] constexpr u32 dma_irq_bit(u32 channel) noexcept {
    return 1U << (8U + channel);
}

/**
 * @brief Guest ARM9/ARM7 code payloads and entry addresses for dual-CPU boot.
 */
struct DualProgramSpec {
    std::vector<u32> arm9_code = {0xEAFFFFFEU};
    std::vector<u32> arm7_code = {0xEAFFFFFEU};
    Address arm9_entry = kArm9DefaultEntry;
    Address arm7_entry = kArm7DefaultEntry;
};

/**
 * @brief Emits a minimal infinite branch-self loop (`B .`) at `base`.
 */
[[nodiscard]] inline std::vector<u32> make_idle_program(Address base = kArm7DefaultEntry) {
    ArmProgramEmitter emitter(base);
    emitter.emit_branch_self();
    return emitter.words();
}

/**
 * @brief Builds a valid synthetic `.nds` ROM from `spec`, loads it, and runs `direct_boot()`.
 */
inline void boot_dual_program(EmulatorE2eHarness& harness, const DualProgramSpec& spec) {
    NdsRomSpec rom_spec{};
    rom_spec.arm9_entry_addr = spec.arm9_entry;
    rom_spec.arm9_ram_addr = spec.arm9_entry;
    const std::vector<u32> arm9_words =
        spec.arm9_code.empty() ? make_idle_program(spec.arm9_entry) : spec.arm9_code;
    rom_spec.arm9_code = words_to_le_bytes(arm9_words);

    rom_spec.arm7_entry_addr = spec.arm7_entry;
    rom_spec.arm7_ram_addr = spec.arm7_entry;
    const std::vector<u32> arm7_words =
        spec.arm7_code.empty() ? make_idle_program(spec.arm7_entry) : spec.arm7_code;
    rom_spec.arm7_code = words_to_le_bytes(arm7_words);

    const std::vector<u8> rom = build_valid_nds_rom(rom_spec);
    const Status load_status = harness.load_rom_bytes(rom);
    ASSERT_TRUE(load_status.is_ok()) << load_status.error().message;
    const Status boot_status = harness.direct_boot();
    ASSERT_TRUE(boot_status.is_ok()) << boot_status.error().message;
}

/**
 * @brief Stores `value_reg` into `result_addr(slot)`.
 * @note Clobbers `R12` as the address scratch register (or `R11` if `value_reg == ArmReg::R12`).
 */
inline void emit_store_result(ArmProgramEmitter& emitter, u32 slot, ArmReg value_reg) {
    const ArmReg addr_reg = (value_reg == ArmReg::R12) ? ArmReg::R11 : ArmReg::R12;
    emitter.emit_mov_imm(addr_reg, result_addr(slot));
    emitter.emit_str32(MemOffsetOp{value_reg, addr_reg, 0});
}

/**
 * @brief Stores immediate `value` into `result_addr(slot)`. Clobbers `R0` and `R1`.
 */
inline void emit_store_const_result(ArmProgramEmitter& emitter, u32 slot, u32 value) {
    emitter.emit_write32_abs(result_addr(slot), value, ScratchRegs{ArmReg::R0, ArmReg::R1});
}

/**
 * @brief Installs `handler_addr` into the HLE BIOS IRQ vector slot and clears `CPSR.I`.
 * @note Clobbers `R0` and `R1`.
 */
inline void emit_install_irq_trampoline(ArmProgramEmitter& emitter,
                                        Address slot_addr,
                                        Address handler_addr) {
    emitter.emit_irq_trampoline_install(slot_addr, handler_addr);
    emitter.emit_enable_irq(ArmReg::R0);
}

/**
 * @brief Returns from a user IRQ handler invoked by the HLE BIOS `BXNE R1` trampoline.
 * @note The HLE BIOS trampoline already saves `{R0-R3, R12, LR}` and executes `SUBS PC, LR, #4`
 *       upon return; calling `emit_irq_return()` here would corrupt control flow.
 */
inline void emit_hle_irq_return(ArmProgramEmitter& emitter) {
    emitter.emit_bx(ArmReg::Lr);
}

/**
 * @brief Acknowledges `irq_mask` bits in `IF` (`kRegIf`, W1C). Clobbers `R0` and `R1`.
 */
inline void emit_ack_irq(ArmProgramEmitter& emitter, u32 irq_mask) {
    emitter.emit_write32_abs(kRegIf, irq_mask);
}

/**
 * @brief Sets `IE` to `ie_mask` and enables `IME = 1`. Clobbers `R0` and `R1`.
 */
inline void emit_enable_interrupts(ArmProgramEmitter& emitter, u32 ie_mask) {
    emitter.emit_write32_abs(kRegIe, ie_mask);
    emitter.emit_write32_abs(kRegIme, 1U);
}

/**
 * @brief Reads a 16-bit value at `addr` into `rd` (`LDRH rd, [rd]`).
 */
inline void emit_read16_abs(ArmProgramEmitter& emitter, ArmReg rd, Address addr) {
    emitter.emit_mov_imm(rd, addr);
    emitter.emit_ldr16(MemOffsetOp{rd, rd, 0});
}

/**
 * @brief Reads an 8-bit value at `addr` into `rd` (`LDRB rd, [rd]`).
 */
inline void emit_read8_abs(ArmProgramEmitter& emitter, ArmReg rd, Address addr) {
    emitter.emit_mov_imm(rd, addr);
    emitter.emit_ldr8(MemOffsetOp{rd, rd, 0});
}

/**
 * @brief Executes one synchronous ARM7 SPI byte transfer and loads `SPIDATA` into `R0`.
 * @note Clobbers `R0` and `R1`.
 */
inline void emit_spi_byte_xfer(ArmProgramEmitter& emitter, u16 spicnt_val, u8 tx_byte) {
    emitter.emit_write16_abs(kRegSpiCnt, spicnt_val);
    emitter.emit_write8_abs(kRegSpiData, tx_byte);
    emit_read8_abs(emitter, ArmReg::R0, kRegSpiData);
}

/**
 * @brief Emits `count` NOP instructions (`MOV R0, R0`).
 */
inline void emit_delay_nop(ArmProgramEmitter& emitter, u32 count) {
    for (u32 i = 0U; i < count; ++i) {
        emitter.emit_nop();
    }
}

}  // namespace elmasds::e2e::m2
