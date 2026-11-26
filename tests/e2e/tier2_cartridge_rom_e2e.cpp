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
constexpr u32 kDoneMagic = 0xD04EC004U;
constexpr u32 kArm9DoneSlot = 31U;
constexpr Address kDstBuf = 0x02200000U;

void emit_arm9_irq19_stub(ArmProgramEmitter& em, u32 capture_slot) {
    const Address main_pc = em.base_address() + 128U;
    em.emit_b(main_pc);
    const Address handler_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(em, capture_slot, ArmReg::R0);
    em.emit_write32_abs(kRegIf, kIrqCartridgeMask);
    em.emit_bx(ArmReg::Lr);
    while (em.current_pc() < main_pc) {
        em.emit_nop();
    }
    em.emit_irq_trampoline_install(kArm9IrqSlot, handler_pc);
    em.emit_enable_irq(ArmReg::R0);
    em.emit_write32_abs(kRegIe, kIrqCartridgeMask);
    em.emit_write32_abs(kRegIme, 1U);
}

void emit_0xb7_4byte_read(ArmProgramEmitter& em, u32 rom_addr, u32 slot) {
    emit_rom_cmd_0xb7(em, rom_addr);
    emit_start_romctrl(em, 7U);
    emit_wait_rom_drq(em, ArmReg::R0);
    em.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(em, slot, ArmReg::R0);
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

TEST(Tier2CartridgeRomE2e, F25RomctrlBlockCodeZeroCompletesImmediately) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_arm9_irq19_stub(arm9, 0U);
    emit_store_const_result(arm9, 0U, 0U);
    arm9.emit_write16_abs(kRegAuxSpiCnt, kAuxSpiEnable | kAuxSpiIrq);
    emit_rom_cmd_words(arm9, 0xB8U, 0U);
    emit_start_romctrl(arm9, 0U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 1U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), kIrqCartridgeMask);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)) & (kRomCtrlBusyBit | kRomCtrlDrqBit),
              0U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0xFFFFFFFFU);
}

TEST(Tier2CartridgeRomE2e, F25RomctrlBit23DrqIsReadOnly) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write32_abs(kRegRomCtrl, kRomCtrlDrqBit);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kRomCtrlDrqBit, 0U);
}

TEST(Tier2CartridgeRomE2e, F25OverReadingRomdataPastBlockEndReturnsLastRomWord) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0xB8U, 0U);
    emit_start_romctrl(arm9, 7U);
    emit_wait_rom_drq(arm9, ArmReg::R0);
    for (u32 slot = 0U; slot < 3U; ++slot) {
        arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
        emit_store_result(arm9, slot, ArmReg::R0);
    }
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 3U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.min_total_size = 2U * 1024U * 1024U;

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(2U)), 0x000001C2U);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(3U)) & kRomCtrlBusyBit, 0U);
}

TEST(Tier2CartridgeRomE2e, F25RomctrlMaxBlockSize16KiBCode6ViaDma) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write32_abs(kRegDma0Sad, kRegRomData);
    arm9.emit_write32_abs(kRegDma0Dad, kDstBuf);
    arm9.emit_write16_abs(kRegDma0CntL, 4096U);
    const u16 dma_ctrl =
        static_cast<u16>(kDmaEnable | kDmaWidth32 | kDmaSrcFixed | kDma9TimingCartridge);
    arm9.emit_write16_abs(kRegDma0CntH, dma_ctrl);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 6U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomCtrl);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(make_pattern_words(0xB6000000U, 4096U));

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)) & kRomCtrlBusyBit, 0U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0xB6000000U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + (2048U * 4U)), 0xB6000800U);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + (4095U * 4U)), 0xB6000FFFU);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + (4096U * 4U)), 0U);
}

TEST(Tier2CartridgeRomE2e, F25Romseed0And1ReadZeroAndWritesIgnoredWithoutCorrupting0xB7) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    arm9.emit_write32_abs(kRegRomSeed0Lo, 0x12345678U);
    arm9.emit_write32_abs(kRegRomSeed1Lo, 0x9ABCDEF0U);
    arm9.emit_write16_abs(kRegRomSeed0Hi, 0x55AAU);
    arm9.emit_write16_abs(kRegRomSeed1Hi, 0xAA55U);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomSeed0Lo);
    emit_store_result(arm9, 0U, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomSeed1Lo);
    emit_store_result(arm9, 1U, ArmReg::R0);
    emit_read16_abs(arm9, ArmReg::R0, kRegRomSeed0Hi);
    emit_store_result(arm9, 2U, ArmReg::R0);
    emit_read16_abs(arm9, ArmReg::R0, kRegRomSeed1Hi);
    emit_store_result(arm9, 3U, ArmReg::R0);
    emit_0xb7_4byte_read(arm9, 0x10000U, 4U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(std::array<u32, 1>{0x5EEDBEEFU});

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    for (u32 slot = 0U; slot < 4U; ++slot) {
        EXPECT_EQ(harness.read_main_ram32(result_slot_addr(slot)), 0U) << "slot=" << slot;
    }
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(4U)), 0x5EEDBEEFU);
}

TEST(Tier2CartridgeRomE2e, F26Command0xB7Below0x8000RedirectsTo0x8000Alias) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_0xb7_4byte_read(arm9, 0x00000000U, 0U);
    emit_0xb7_4byte_read(arm9, 0x000001FCU, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x81FCU;
    spec.extra_rom_payload = words_to_le_bytes(std::array<u32, 1>{0xBEEF81FCU});

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xEAFFFFFEU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xBEEF81FCU);
}

TEST(Tier2CartridgeRomE2e, F26Command0xB7ExactBoundary0x7FFCVs0x8000) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_0xb7_4byte_read(arm9, 0x00007FFCU, 0U);
    emit_0xb7_4byte_read(arm9, 0x00008000U, 1U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x81FCU;
    spec.extra_rom_payload = words_to_le_bytes(std::array<u32, 1>{0xBEEF81FCU});

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(0U)), 0xBEEF81FCU);
    EXPECT_EQ(harness.read_main_ram32(result_slot_addr(1U)), 0xEAFFFFFEU);
}

TEST(Tier2CartridgeRomE2e, F26Command0xB7OutOfBoundsReturnsValidWordThenAllOnes) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_0xb7(arm9, 0x10000U);
    emit_start_romctrl(arm9, 1U);
    emit_read_romdata_words(arm9, kDstBuf, 128U);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    NdsRomSpec spec = make_base_rom_spec(arm9.words(), make_idle_words());
    spec.extra_rom_offset = 0x10000U;
    spec.extra_rom_payload = words_to_le_bytes(std::array<u32, 1>{0xABCD1234U});

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, spec);
    EXPECT_EQ(harness.read_main_ram32(kDstBuf + 0U), 0xABCD1234U);
    for (u32 i = 1U; i < 128U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(kDstBuf + (i * 4U)), 0xFFFFFFFFU) << "word=" << i;
    }
}

TEST(Tier2CartridgeRomE2e, F26UnsupportedAndKey1OpcodesReturnAllOnes) {
    constexpr std::array<u8, 4> kOpcodes{0x3CU, 0x40U, 0x12U, 0xAAU};
    ArmProgramEmitter arm9(kArm9DefaultBase);
    for (u32 i = 0U; i < kOpcodes.size(); ++i) {
        emit_rom_cmd_words(arm9, static_cast<u32>(kOpcodes[i]), 0U);
        emit_start_romctrl(arm9, 7U);
        emit_wait_rom_drq(arm9, ArmReg::R0);
        arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
        emit_store_result(arm9, i, ArmReg::R0);
    }
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness harness;
    boot_and_verify_arm9(harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    for (u32 i = 0U; i < kOpcodes.size(); ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_slot_addr(i)), 0xFFFFFFFFU);
    }
}

TEST(Tier2CartridgeRomE2e, F26ChipIdFloorsAt1MiBAndReflects4MiB) {
    ArmProgramEmitter arm9(kArm9DefaultBase);
    emit_rom_cmd_words(arm9, 0xB8U, 0U);
    emit_start_romctrl(arm9, 7U);
    emit_wait_rom_drq(arm9, ArmReg::R0);
    arm9.emit_read32_abs(ArmReg::R0, kRegRomData);
    emit_store_result(arm9, 0U, ArmReg::R0);
    emit_store_const_result(arm9, kArm9DoneSlot, kDoneMagic);
    emit_halt_loop(arm9);

    EmulatorE2eHarness small_harness;
    boot_and_verify_arm9(small_harness, make_base_rom_spec(arm9.words(), make_idle_words()));
    EXPECT_EQ(small_harness.read_main_ram32(result_slot_addr(0U)), 0x000000C2U);

    NdsRomSpec four_mib = make_base_rom_spec(arm9.words(), make_idle_words());
    four_mib.min_total_size = 4U * 1024U * 1024U;
    EmulatorE2eHarness four_mib_harness;
    boot_and_verify_arm9(four_mib_harness, four_mib);
    EXPECT_EQ(four_mib_harness.read_main_ram32(result_slot_addr(0U)), 0x000003C2U);
}

}  // namespace
}  // namespace elmasds::e2e::m3
