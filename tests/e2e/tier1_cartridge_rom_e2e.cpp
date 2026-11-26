#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr TickCount kDefaultSteps = 40'000ULL;
constexpr u32 kDoneMagic = 0xD04EC002U;
constexpr u32 kArm7DoneSlot = 29U;
constexpr u32 kSyncSlot = 30U;
constexpr u32 kArm9DoneSlot = 31U;
constexpr Address kDstBuf = 0x02200000U;
constexpr Address kDstBuf2 = 0x02210000U;

struct Dma0Cfg {
    Address sad = 0U;
    Address dad = 0U;
    u16 cnt_l = 0U;
    u16 cnt_h = 0U;
};

struct IrqStubSpec {
    Address irq_slot = kArm9IrqSlot;
    u32 irq_mask = 0U;
    u32 capture_slot = 0U;
};

void emit_dma0_start(ArmProgramEmitter& em, const Dma0Cfg& cfg) {
    em.emit_write32_abs(kRegDma0Sad, cfg.sad);
    em.emit_write32_abs(kRegDma0Dad, cfg.dad);
    em.emit_write16_abs(kRegDma0CntL, cfg.cnt_l);
    em.emit_write16_abs(kRegDma0CntH, cfg.cnt_h);
}

void emit_irq_capture_stub(ArmProgramEmitter& em, const IrqStubSpec& spec) {
    const Address main_pc = em.base_address() + 128U;
    em.emit_b(main_pc);
    const Address handler_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(em, spec.capture_slot, ArmReg::R0);
    em.emit_write32_abs(kRegIf, spec.irq_mask);
    em.emit_bx(ArmReg::Lr);
    while (em.current_pc() < main_pc) {
        em.emit_nop();
    }
    em.emit_irq_trampoline_install(spec.irq_slot, handler_pc);
    em.emit_enable_irq(ArmReg::R0);
    em.emit_write32_abs(kRegIe, spec.irq_mask);
    em.emit_write32_abs(kRegIme, 1U);
}

void emit_wait_sync(ArmProgramEmitter& em, u32 expected) {
    const Address loop_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, result_slot_addr(kSyncSlot));
    em.emit_cmp_imm(ArmReg::R0, expected);
    em.emit_b(loop_pc, ArmCond::Ne);
}

[[nodiscard]] std::vector<u32> make_pattern_words(u32 base_val, u32 count) {
    std::vector<u32> words(count);
    for (u32 i = 0U; i < count; ++i) {
        words[i] = base_val + i;
    }
    return words;
}

void boot_and_verify_arm9(EmulatorE2eHarness& harness, const NdsRomSpec& spec) {
    boot_rom_and_step(harness, spec, kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm9DoneSlot)), kDoneMagic);
}

TEST(Tier1CartridgeRomE2e, F25RomCmd8ByteWriteAndReadback) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0x820000B7U, 0x33221100U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCmdLo);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCmdHi);
    emit_store_result(arm9, 1U, ArmReg::R0);

    for (u32 i = 0U; i < 8U; ++i) {
        arm9.emit_write8_abs(kRegRomCmdLo + i, static_cast<u8>(0x11U * (i + 1U)));
    }
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCmdLo);
    emit_store_result(arm9, 2U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCmdHi);
    emit_store_result(arm9, 3U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x820000B7U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x33221100U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x44332211U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)), 0x88776655U);
}

TEST(Tier1CartridgeRomE2e, F25RomctrlDrqBit23And4Byte0xB8Read) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0xB8U, 0U);
    emit_start_romctrl(arm9, 7U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.min_total_size = 2U * 1024U * 1024U;

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_NE(harness.read_main_ram32(result_slot_addr(0U)) & kRomCtrlBusyBit, 0U);
    EXPECT_NE(harness.read_main_ram32(result_slot_addr(0U)) & kRomCtrlDrqBit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)) & (kRomCtrlBusyBit | kRomCtrlDrqBit),
              0U);
}

TEST(Tier1CartridgeRomE2e, F25Romctrl512ByteBlockCpuPolling) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xCAFE0000U, 128U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & (kRomCtrlBusyBit | kRomCtrlDrqBit),
              0U);
    for (u32 i = 0U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xCAFE0000U + i);
    }
}

TEST(Tier1CartridgeRomE2e, F25Arm9SingleShotCartridgeDmaAndIrq8) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_irq_capture_stub(arm9, IrqStubSpec{kArm9IrqSlot, kIrqDma0Mask, 0U});
    const u16 dma_ctrl = static_cast<u16>(
        kDmaEnable | kDmaIrqEnable | kDmaWidth32 | kDmaSrcFixed | kDma9TimingCartridge);
    emit_dma0_start(arm9, Dma0Cfg{kRegRomData, kDstBuf, 128U, dma_ctrl});
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 1U);
    emit_read16_abs(arm9, ArmReg::R0, kRegDma0CntH);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xD9A00000U, 128U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kIrqDma0Mask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)) & kDmaEnable, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)) & kRomCtrlBusyBit, 0U);
    for (u32 i = 0U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xD9A00000U + i);
    }
}

TEST(Tier1CartridgeRomE2e, F25Arm7CartridgeRepeatDmaAndIrq19) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write16_abs(kRegExMemCnt, kExMemSlot1Arm7Bit);
    emit_store_const_result(arm9, kSyncSlot, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    ArmProgramEmitter arm7(kArm7DefaultBase);
    emit_wait_sync(arm7, 1U);
    emit_irq_capture_stub(arm7, IrqStubSpec{kArm7IrqSlot, kIrqCartridgeMask, 0U});
    arm7.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiEnable | kAuxSpiIrq);
    const u16 dma_ctrl = static_cast<u16>(
        kDmaEnable | kDmaRepeat | kDmaWidth32 | kDmaSrcFixed | kDma7TimingCartridge);
    emit_dma0_start(arm7, Dma0Cfg{kRegRomData, kDstBuf, 1U, dma_ctrl});
    emit_rom_cmd_0xb7(arm7, 0x10000U);
    emit_start_romctrl(arm7, 1U);
    arm7.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm7, 1U, ArmReg::R0);
    emit_store_const_result(arm7, kArm7DoneSlot, kDoneMagic);
    emit_halt_loop(arm7);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), arm7.words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xD7A00000U, 128U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kArm7DoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kIrqCartridgeMask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)) & kRomCtrlBusyBit, 0U);
    for (u32 i = 0U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xD7A00000U + i);
    }
}

TEST(Tier1CartridgeRomE2e, F26Command0x9FDummyReturnsAllOnes) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0x9FU, 0U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    for (u32 i = 0U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xFFFFFFFFU);
    }
}

TEST(Tier1CartridgeRomE2e, F26Command0x90And0xB8ReturnChipId) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0x90U, 0U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    emit_rom_cmd_words(arm9, 0xB8U, 0U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf2, 128U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.min_total_size = 2U * 1024U * 1024U;

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    for (u32 i = 0U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0x000001C2U);
        EXPECT_EQ(harness.read_main_ram32(kDstBuf2 + (i * 4U)), 0x000001C2U);
    }
}

TEST(Tier1CartridgeRomE2e, F26Command0x00ReadsHeaderFromOffsetZero) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0x00U, 0U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.title = "CARTROM00TST";
    spec.gamecode = "CT00";
    spec.makercode = "EL";

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x00U), 0x54524143U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x04U), 0x304D4F52U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x08U), 0x54535430U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x0CU), 0x30305443U);
    EXPECT_EQ(harness.read_main_ram16(kDstBuf + 0x15CU), kExpectedLogoCrc16);
}

TEST(Tier1CartridgeRomE2e, F26Command0xB7ReadsExtraRomPayloadAt0x10000) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xA1B20000U, 16U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    for (u32 i = 0U; i < 16U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xA1B20000U + i);
    }
}

TEST(Tier1CartridgeRomE2e, F26Command0xB7ReadsBlockSizes1KiBAnd2KiB) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 2U);
    emit_read_romdata_words(arm9, kDstBuf, 256U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 0U, ArmReg::R0);

    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 3U);
    emit_read_romdata_words(arm9, kDstBuf2, 512U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 1U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xB7200000U, 512U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kRomCtrlBusyBit, 0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)) & kRomCtrlBusyBit, 0U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0xB7200000U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0x3FCU), 0xB72000FFU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf2 + 0U), 0xB7200000U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf2 + 0x7FCU), 0xB72001FFU);
}

}  // namespace
}  // namespace elmasds::e2e::m3
