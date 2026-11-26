#include "e2e/e2e_m3_guest.hpp"

#include <array>

namespace elmasds::e2e::m3 {
namespace {

constexpr u32 kArmTstRomctrlDrqBase = 0xE3100880U;  // TST Rn, #0x00800000
constexpr u32 kArmMrcCp15C1Base = 0xEE110F10U;      // MRC p15, 0, Rd, c1, c0, 0
constexpr u32 kArmMrcCp15DtcmBase = 0xEE190F11U;    // MRC p15, 0, Rd, c9, c1, 0
constexpr u32 kArmMrcCp15ItcmBase = 0xEE190F31U;    // MRC p15, 0, Rd, c9, c1, 1

void emit_store_r0_byte_abs(ArmProgramEmitter& emit, Address addr) {
    emit.emit_mov_imm(ArmReg::R1, addr);
    emit.emit_str8(MemOffsetOp{ArmReg::R0, ArmReg::R1, 0});
}

}  // namespace

void emit_read16_abs(ArmProgramEmitter& emit, ArmReg dst, Address addr) {
    emit.emit_mov_imm(dst, addr);
    emit.emit_ldr16(MemOffsetOp{dst, dst, 0});
}

void emit_read8_abs(ArmProgramEmitter& emit, ArmReg dst, Address addr) {
    emit.emit_mov_imm(dst, addr);
    emit.emit_ldr8(MemOffsetOp{dst, dst, 0});
}

void emit_store_result(ArmProgramEmitter& emit, u32 slot, ArmReg value_reg) {
    const ArmReg addr_reg = (value_reg == ArmReg::R12) ? ArmReg::R11 : ArmReg::R12;
    emit.emit_mov_imm(addr_reg, result_slot_addr(slot));
    emit.emit_str32(MemOffsetOp{value_reg, addr_reg, 0});
}

void emit_store_const_result(ArmProgramEmitter& emit, u32 slot, u32 value) {
    emit.emit_write32_abs(result_slot_addr(slot), value, ScratchRegs{ArmReg::R0, ArmReg::R1});
}

void emit_rom_cmd_words(ArmProgramEmitter& emit, u32 cmd_lo, u32 cmd_hi) {
    emit.emit_write32_abs(kRegRomCmdLo, cmd_lo);
    emit.emit_write32_abs(kRegRomCmdHi, cmd_hi);
}

void emit_rom_cmd_0xb7(ArmProgramEmitter& emit, u32 rom_addr) {
    emit_rom_cmd_words(emit, make_rom_cmd_0xb7_lo(rom_addr), make_rom_cmd_0xb7_hi(rom_addr));
}

void emit_start_romctrl(ArmProgramEmitter& emit, u32 block_code, u32 flags) {
    emit.emit_write32_abs(kRegRomCtrl, make_romctrl_cmd_word(block_code, flags));
}

void emit_poll_romctrl_drq_bounded(ArmProgramEmitter& emit, ArmReg scratch, u32 max_iters) {
    if (max_iters == 0U) {
        return;
    }
    const ArmReg cnt_reg = (scratch == ArmReg::R12) ? ArmReg::R11 : ArmReg::R12;
    emit.emit_mov_imm(cnt_reg, max_iters);

    const Address loop_pc = emit.current_pc();
    emit.emit_read32_abs(scratch, kRegRomCtrl);
    emit.emit_word(kArmTstRomctrlDrqBase | (static_cast<u32>(scratch) << 16U));
    emit.emit_b(emit.current_pc() + 12U, ArmCond::Ne);
    emit.emit_sub_imm(RegImmOp{cnt_reg, cnt_reg, 1U, true, ArmCond::Al});
    emit.emit_b(loop_pc, ArmCond::Ne);
}

void emit_wait_rom_drq(ArmProgramEmitter& emit, ArmReg scratch) {
    emit_poll_romctrl_drq_bounded(emit, scratch, 64U);
}

void emit_read_romdata_words(ArmProgramEmitter& emit, u32 dst_ram, u32 word_count) {
    if (word_count == 0U) {
        return;
    }
    emit.emit_mov_imm(ArmReg::R1, dst_ram);
    emit.emit_mov_imm(ArmReg::R2, word_count);

    const Address loop_pc = emit.current_pc();
    emit_poll_romctrl_drq_bounded(emit, ArmReg::R0, 64U);
    emit.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit.emit_str32(MemOffsetOp{ArmReg::R0, ArmReg::R1, 0});
    emit.emit_add_imm(RegImmOp{ArmReg::R1, ArmReg::R1, 4U, false, ArmCond::Al});
    emit.emit_sub_imm(RegImmOp{ArmReg::R2, ArmReg::R2, 1U, true, ArmCond::Al});
    emit.emit_b(loop_pc, ArmCond::Ne);
}

void emit_auxspi_tx(ArmProgramEmitter& emit, u16 auxspicnt, u8 tx_byte) {
    emit.emit_write16_abs(kRegAuxSpiCnt, auxspicnt);
    emit.emit_write8_abs(kRegAuxSpiData, tx_byte);
    emit_read8_abs(emit, ArmReg::R0, kRegAuxSpiData);
}

void emit_auxspi_seq(ArmProgramEmitter& emit, const AuxSpiSeqSpec& spec) {
    const std::size_t count = spec.tx_bytes.size();
    for (std::size_t i = 0U; i < count; ++i) {
        const bool is_last = (i + 1U) == count;
        const bool hold = !is_last || !spec.release_on_last;
        const u16 cnt = hold ? static_cast<u16>(spec.base_cnt | kAuxSpiHold)
                             : static_cast<u16>(spec.base_cnt & ~kAuxSpiHold);
        emit_auxspi_tx(emit, cnt, spec.tx_bytes[i]);
        if (spec.rx_ram_addr != 0U && (spec.store_all_rx || is_last)) {
            const Address dst = spec.store_all_rx
                                    ? (spec.rx_ram_addr + static_cast<Address>(i))
                                    : spec.rx_ram_addr;
            emit_store_r0_byte_abs(emit, dst);
        }
    }
}

void emit_spi_firmware_byte(ArmProgramEmitter& emit, u16 spicnt, u8 tx_byte) {
    emit.emit_write16_abs(kRegSpiCnt, spicnt);
    emit.emit_write8_abs(kRegSpiData, tx_byte);
    emit_read8_abs(emit, ArmReg::R0, kRegSpiData);
}

void emit_spi_firmware_read(ArmProgramEmitter& emit, const SpiFirmwareReadSpec& spec) {
    if (spec.byte_count == 0U) {
        return;
    }
    const u16 hold_cnt = kSpiEnable | kSpiDeviceFirmware | kSpiCsHold;
    const u16 release_cnt = kSpiEnable | kSpiDeviceFirmware;
    const std::array<u8, 4U> header = {
        0x03U,
        static_cast<u8>((spec.fw_offset >> 16U) & 0xFFU),
        static_cast<u8>((spec.fw_offset >> 8U) & 0xFFU),
        static_cast<u8>(spec.fw_offset & 0xFFU),
    };
    for (const u8 byte : header) {
        emit_spi_firmware_byte(emit, hold_cnt, byte);
    }
    for (u32 i = 0U; i < spec.byte_count; ++i) {
        const bool is_last = (i + 1U) == spec.byte_count;
        emit_spi_firmware_byte(emit, is_last ? release_cnt : hold_cnt, 0x00U);
        emit_store_r0_byte_abs(emit, spec.dst_ram + i);
    }
}

void emit_halt_loop(ArmProgramEmitter& emit) {
    emit.emit_branch_self();
    emit.finalize_literal_pool();
}

void emit_cp15_read_c1(ArmProgramEmitter& emit, ArmReg dst) {
    emit.emit_word(kArmMrcCp15C1Base | (static_cast<u32>(dst) << 12U));
}

void emit_cp15_read_dtcm(ArmProgramEmitter& emit, ArmReg dst) {
    emit.emit_word(kArmMrcCp15DtcmBase | (static_cast<u32>(dst) << 12U));
}

void emit_cp15_read_itcm(ArmProgramEmitter& emit, ArmReg dst) {
    emit.emit_word(kArmMrcCp15ItcmBase | (static_cast<u32>(dst) << 12U));
}

}  // namespace elmasds::e2e::m3
