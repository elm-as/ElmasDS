#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <span>
#include <vector>

namespace elmasds::e2e::m2 {
namespace {

constexpr u32 kDoneSlot = 31U;
constexpr u32 kDoneMagic = 0xD04E2001U;
constexpr TickCount kDefaultSteps = 80'000ULL;
constexpr u16 kFwHold = kSpiEnable | kSpiCsHold | kSpiDeviceFirmware;
constexpr u16 kFwRel = kSpiEnable | kSpiDeviceFirmware;
constexpr u16 kPmHold = kSpiEnable | kSpiCsHold | kSpiDevicePowerman;
constexpr u16 kPmRel = kSpiEnable | kSpiDevicePowerman;
constexpr u16 kTsHold = kSpiEnable | kSpiCsHold | kSpiDeviceTouchscreen;
constexpr u16 kTsRel = kSpiEnable | kSpiDeviceTouchscreen;

struct SpiStep {
    u16 spicnt = 0U;
    u8 tx = 0U;
    u32 slot = 0U;
};

struct SpiRxSeq {
    u16 hold_cnt = 0U;
    u32 start_slot = 0U;
    u32 count = 0U;
};

struct ProgramExpectCase {
    void (*build)(ArmProgramEmitter&) = nullptr;
    std::vector<u32> expected;
    u32 start_slot = 0U;
};

struct TouchResetCase {
    TouchInputState input{};
    bool set_before_boot = false;
    u16 expected_y = 0U;
    u16 expected_x = 0U;
};

void emit_spi_to_slot(ArmProgramEmitter& em, const SpiStep& step) {
    emit_spi_byte_xfer(em, step.spicnt, step.tx);
    emit_store_result(em, step.slot, ArmReg::R0);
}

void emit_spi_tx_seq(ArmProgramEmitter& em, u16 hold_cnt, std::initializer_list<u8> bytes) {
    for (const u8 b : bytes) {
        emit_spi_byte_xfer(em, hold_cnt, b);
    }
}

void emit_spi_rx_seq(ArmProgramEmitter& em, const SpiRxSeq& seq) {
    const u16 rel_cnt = static_cast<u16>(seq.hold_cnt & ~kSpiCsHold);
    for (u32 i = 0U; i < seq.count; ++i) {
        const u16 cnt = (i + 1U == seq.count) ? rel_cnt : seq.hold_cnt;
        emit_spi_to_slot(em, SpiStep{cnt, 0x00U, seq.start_slot + i});
    }
}

template <typename BuildFn>
void run_arm7_fn(EmulatorE2eHarness& harness, BuildFn&& build, TouchInputState touch = {}) {
    ArmProgramEmitter em(kArm7DefaultEntry);
    build(em);
    emit_store_const_result(em, kDoneSlot, kDoneMagic);
    em.emit_branch_self();
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.set_touch_state(touch);
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
}

void expect_slots(const EmulatorE2eHarness& harness, u32 start, std::span<const u32> vals) {
    for (u32 i = 0U; i < vals.size(); ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(start + i)), vals[i]) << "slot=" << i;
    }
}

void emit_flash_read(ArmProgramEmitter& em, Address addr, u32 count) {
    const u8 a2 = static_cast<u8>(addr >> 16U);
    const u8 a1 = static_cast<u8>(addr >> 8U);
    emit_spi_tx_seq(em, kFwHold, {0x03U, a2, a1, static_cast<u8>(addr)});
    emit_spi_rx_seq(em, SpiRxSeq{kFwHold, 0U, count});
}

void emit_flash_write(ArmProgramEmitter& em, u8 cmd, std::span<const u8> addr_and_data) {
    emit_spi_byte_xfer(em, kFwHold, cmd);
    for (std::size_t i = 0U; i < addr_and_data.size(); ++i) {
        const u16 cnt = (i + 1U == addr_and_data.size()) ? kFwRel : kFwHold;
        emit_spi_byte_xfer(em, cnt, addr_and_data[i]);
    }
}

void emit_pmic_rw(ArmProgramEmitter& em, u8 reg_idx, bool is_read) {
    emit_spi_byte_xfer(em, kPmHold, static_cast<u8>(0x80U | reg_idx));
    if (is_read) {
        emit_spi_to_slot(em, SpiStep{kPmRel, 0x00U, 0U});
    }
}

void emit_pmic_write(ArmProgramEmitter& em, u8 reg_idx, u8 val) {
    emit_spi_byte_xfer(em, kPmHold, reg_idx);
    emit_spi_byte_xfer(em, kPmRel, val);
}

void emit_pmic_read(ArmProgramEmitter& em, u8 reg_idx, u32 slot) {
    emit_spi_byte_xfer(em, kPmHold, static_cast<u8>(0x80U | reg_idx));
    emit_spi_to_slot(em, SpiStep{kPmRel, 0x00U, slot});
}

[[nodiscard]] u16 decode_tsc12(const EmulatorE2eHarness& harness, u32 hi_slot) {
    const u32 hi = harness.read_main_ram32(result_addr(hi_slot)) & 0x7FU;
    const u32 lo = (harness.read_main_ram32(result_addr(hi_slot + 1U)) >> 3U) & 0x1FU;
    return static_cast<u16>((hi << 5U) | lo);
}

// F16: SPICNT & SPIDATA — Tier 2 Boundary/Edge/Negative (5 test cases)

TEST(Tier2SpiControllerE2e, DisabledTransferClearsDataAndSuppressesIrq) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_to_slot(em, SpiStep{kSpiEnable | kSpiDeviceNone, 0x55U, 0U});
        emit_spi_to_slot(em, SpiStep{kSpiIrqEnable | kSpiDeviceNone, 0x9FU, 1U});
        emit_read32_abs(em, ArmReg::R0, kRegIf);
        emit_store_result(em, 2U, ArmReg::R0);
    });
    expect_slots(harness, 0U, std::array<u32, 2>{0xFFU, 0x00U});
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)) & kIrqSpi, 0U);
}

class Tier2SpiCsDropTest : public ::testing::TestWithParam<u16> {};
TEST_P(Tier2SpiCsDropTest, ClearingEnableOrSwitchingDeviceReleasesCs) {
    EmulatorE2eHarness harness;
    const u16 drop_cnt = GetParam();
    run_arm7_fn(harness, [drop_cnt](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kFwHold, 0x9FU);
        emit_spi_to_slot(em, SpiStep{kFwHold, 0x00U, 0U});
        em.emit_write16_abs(kRegSpiCnt, drop_cnt);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 1U});
    });
    expect_slots(harness, 0U, std::array<u32, 2>{0x20U, 0x00U});
}
INSTANTIATE_TEST_SUITE_P(CsDropTriggers, Tier2SpiCsDropTest,
                         ::testing::Values(kSpiCsHold | kSpiDeviceFirmware, kPmHold));

TEST(Tier2SpiControllerE2e, Arm9MmioAccessDoesNotTouchArm7Spi) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em9(kArm9DefaultEntry);
    em9.emit_write16_abs(kRegSpiCnt, 0xCF03U);
    em9.emit_write8_abs(kRegSpiData, 0x9FU);
    emit_store_const_result(em9, 10U, 1U);
    em9.emit_branch_self();
    ArmProgramEmitter em7(kArm7DefaultEntry);
    emit_delay_nop(em7, 64U);
    emit_read16_abs(em7, ArmReg::R0, kRegSpiCnt);
    emit_store_result(em7, 0U, ArmReg::R0);
    emit_read8_abs(em7, ArmReg::R0, kRegSpiData);
    emit_store_result(em7, 1U, ArmReg::R0);
    emit_store_const_result(em7, kDoneSlot, kDoneMagic);
    em7.emit_branch_self();
    boot_dual_program(harness, DualProgramSpec{em9.words(), em7.words()});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    expect_slots(harness, 0U, std::array<u32, 2>{0x0000U, 0x00U});
}

TEST(Tier2SpiControllerE2e, ReservedBitsMaskedAnd16BitModeWithoutIrqBit) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        em.emit_write16_abs(kRegSpiCnt, 0xFFFFU);
        emit_read16_abs(em, ArmReg::R0, kRegSpiCnt);
        emit_store_result(em, 0U, ArmReg::R0);
        em.emit_write16_abs(kRegSpiCnt, kPmHold | (1U << 10U));
        em.emit_write16_abs(kRegSpiData, 0xAA80U);
        em.emit_write16_abs(kRegSpiCnt, kPmRel | (1U << 10U));
        em.emit_write16_abs(kRegSpiData, 0xBB00U);
        emit_read16_abs(em, ArmReg::R0, kRegSpiData);
        emit_store_result(em, 1U, ArmReg::R0);
        emit_read32_abs(em, ArmReg::R0, kRegIf);
        emit_store_result(em, 2U, ArmReg::R0);
    });
    expect_slots(harness, 0U, std::array<u32, 2>{0xCF03U, 0x0DU});
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)) & kIrqSpi, 0U);
}

// F17: Firmware Flash — Tier 2 Boundary/Edge/Negative (5 test cases)

void build_fw_write_blocked(ArmProgramEmitter& em) {
    emit_flash_write(em, 0x02U, std::array<u8, 5>{0x00U, 0x10U, 0x00U, 0x42U, 0x43U});
    emit_spi_tx_seq(em, kFwRel, {0x06U, 0x04U});
    emit_flash_write(em, 0x0AU, std::array<u8, 5>{0x00U, 0x10U, 0x02U, 0x44U, 0x45U});
    emit_flash_read(em, 0x001000U, 4U);
}

void build_fw_fast_read(ArmProgramEmitter& em) {
    emit_spi_tx_seq(em, kFwHold, {0x0BU, 0x00U});
    emit_spi_rx_seq(em, SpiRxSeq{kFwHold, 0U, 12U});
}

void build_fw_unknown_and_abort(ArmProgramEmitter& em) {
    emit_spi_byte_xfer(em, kFwHold, 0xAAU);
    emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 0U});
    emit_spi_byte_xfer(em, kFwRel, 0x06U);
    emit_spi_tx_seq(em, kFwHold, {0x02U, 0x00U});
    emit_spi_byte_xfer(em, kFwRel, 0x10U);
    emit_spi_byte_xfer(em, kFwHold, 0x05U);
    emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 1U});
}

class Tier2Firmware256KEdgeTest : public ::testing::TestWithParam<ProgramExpectCase> {};
TEST_P(Tier2Firmware256KEdgeTest, ExercisesNegativeAndQuirkCases) {
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(FirmwareSpec{})).is_ok());
    run_arm7_fn(harness, GetParam().build);
    expect_slots(harness, GetParam().start_slot, GetParam().expected);
}
INSTANTIATE_TEST_SUITE_P(
    Fw256KEdges, Tier2Firmware256KEdgeTest,
    ::testing::Values(ProgramExpectCase{build_fw_write_blocked, {0xFFU, 0xFFU, 0xFFU, 0xFFU}},
                      ProgramExpectCase{build_fw_fast_read, {'E', 'L', 'M', 'S'}, 8U},
                      ProgramExpectCase{build_fw_unknown_and_abort, {0x00U, 0x02U}}));

TEST(Tier2FirmwareFlashE2e, ReadAndWriteWrapAt128KImageBoundary) {
    EmulatorE2eHarness harness;
    FirmwareSpec spec{};
    spec.size_bytes = kFirmwareSize128K;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(spec)).is_ok());
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kFwRel, 0x06U);
        emit_flash_write(em, 0x02U, std::array<u8, 5>{0x01U, 0xFFU, 0xFFU, 0x51U, 0x52U});
        emit_flash_read(em, 0x01FFFEU, 4U);
    });
    expect_slots(harness, 0U, std::array<u32, 4>{0xFFU, 0x51U, 0x52U, 0xFFU});
}

TEST(Tier2FirmwareFlashE2e, DirectBootResetsWriteEnableLatch) {
    EmulatorE2eHarness harness;
    auto check_status = [&](bool send_wren) {
        run_arm7_fn(harness, [send_wren](ArmProgramEmitter& em) {
            if (send_wren) {
                emit_spi_byte_xfer(em, kFwRel, 0x06U);
            }
            emit_spi_byte_xfer(em, kFwHold, 0x05U);
            emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 0U});
        });
    };
    check_status(true);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x02U);
    check_status(false);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00U);
}

// F18: Powerman PMIC — Tier 2 Boundary/Edge/Negative (7 test cases)

class Tier2PowermanOutOfRangeTest : public ::testing::TestWithParam<u8> {};
TEST_P(Tier2PowermanOutOfRangeTest, OutOfRangeRegIgnoresWriteAndReadsZero) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_pmic_write(em, GetParam(), 0xEEU);
        emit_pmic_read(em, GetParam(), 0U);
        emit_pmic_read(em, 0U, 1U);
    });
    expect_slots(harness, 0U, std::array<u32, 2>{0x00U, 0x0DU});
}
INSTANTIATE_TEST_SUITE_P(Regs5To7, Tier2PowermanOutOfRangeTest, ::testing::Values(5U, 6U, 7U));

void build_pmic_cs_abort(ArmProgramEmitter& em) {
    emit_spi_byte_xfer(em, kPmRel, 0x01U);
    emit_pmic_read(em, 0U, 0U);
    emit_pmic_read(em, 1U, 1U);
}
void build_pmic_chained(ArmProgramEmitter& em) {
    emit_spi_tx_seq(em, kPmHold, {0x02U, 0x3CU, 0x82U});
    emit_spi_to_slot(em, SpiStep{kPmRel, 0x00U, 0U});
}
void build_pmic_index_mask(ArmProgramEmitter& em) {
    emit_pmic_write(em, 0x08U, 0x42U);
    emit_pmic_read(em, 0x08U, 0U);
    emit_pmic_read(em, 0x00U, 1U);
}

class Tier2PowermanEdgeTest : public ::testing::TestWithParam<ProgramExpectCase> {};
TEST_P(Tier2PowermanEdgeTest, CsAbortChainedTransferAndIndexMasking) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, GetParam().build);
    expect_slots(harness, GetParam().start_slot, GetParam().expected);
}
INSTANTIATE_TEST_SUITE_P(
    PowermanProtocolEdges, Tier2PowermanEdgeTest,
    ::testing::Values(ProgramExpectCase{build_pmic_cs_abort, {0x0DU, 0x00U}},
                      ProgramExpectCase{build_pmic_chained, {0x3CU}},
                      ProgramExpectCase{build_pmic_index_mask, {0x42U, 0x42U}}));

TEST(Tier2PowermanE2e, DirectBootResetsModifiedRegisters) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_pmic_write(em, 0U, 0x99U);
        emit_pmic_read(em, 0U, 0U);
    });
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x99U);
    run_arm7_fn(harness, [](ArmProgramEmitter& em) { emit_pmic_rw(em, 0U, true); });
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x0DU);
}

// F19: Touchscreen ADC TSC2046 — Tier 2 Boundary/Edge/Negative (6 test cases)

class Tier2TouchEdgeTest : public ::testing::TestWithParam<TouchResetCase> {};
TEST_P(Tier2TouchEdgeTest, MasksCoordinatesAndResetsOnDirectBoot) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    for (u32 idx = 0U; idx < 2U; ++idx) {
        emit_spi_byte_xfer(em, kTsHold, idx == 0U ? 0x90U : 0xD0U);
        emit_spi_rx_seq(em, SpiRxSeq{kTsHold, idx * 2U, 2U});
    }
    emit_store_const_result(em, kDoneSlot, kDoneMagic);
    em.emit_branch_self();
    if (GetParam().set_before_boot) {
        harness.set_touch_state(GetParam().input);
    }
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    if (!GetParam().set_before_boot) {
        harness.set_touch_state(GetParam().input);
    }
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(decode_tsc12(harness, 0U), GetParam().expected_y);
    EXPECT_EQ(decode_tsc12(harness, 2U), GetParam().expected_x);
}
INSTANTIATE_TEST_SUITE_P(
    CoordAndResetEdges, Tier2TouchEdgeTest,
    ::testing::Values(TouchResetCase{{0xFFFFU, 0xF123U, true}, false, 0x0123U, 0x0FFFU},
                      TouchResetCase{{0x0000U, 0x0000U, true}, false, 0x0000U, 0x0000U},
                      TouchResetCase{{0x0222U, 0x0333U, true}, true, 0x0FFFU, 0x0000U}));

TEST(Tier2TouchscreenE2e, NonStartByteWithoutBit7IgnoredInIdle) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_to_slot(em, SpiStep{kTsHold, 0x10U, 0U});
        emit_spi_rx_seq(em, SpiRxSeq{kTsHold, 1U, 2U});
    });
    expect_slots(harness, 0U, std::array<u32, 3>{0x00U, 0x00U, 0x00U});
}

TEST(Tier2TouchscreenE2e, CsReleaseDiscardsPendingSamplePhase) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kTsHold, 0x90U);
        emit_spi_to_slot(em, SpiStep{kTsRel, 0x00U, 0U});
        emit_spi_to_slot(em, SpiStep{kTsRel, 0x00U, 1U});
        emit_spi_byte_xfer(em, kTsRel, 0x90U);
        emit_spi_to_slot(em, SpiStep{kTsRel, 0x00U, 2U});
    });
    EXPECT_NE(harness.read_main_ram32(result_addr(0U)), 0x00U);
    expect_slots(harness, 1U, std::array<u32, 2>{0x00U, 0x00U});
}

TEST(Tier2TouchscreenE2e, PipelinedCommandOverlapLatchesNextChannel) {
    EmulatorE2eHarness harness;
    auto build = [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kTsHold, 0x90U);
        emit_spi_to_slot(em, SpiStep{kTsHold, 0xD0U, 0U});
        emit_spi_rx_seq(em, SpiRxSeq{kTsHold, 1U, 2U});
    };
    run_arm7_fn(harness, build, TouchInputState{0x0456U, 0x0ABCU, true});
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), (0x0ABCU >> 5U) & 0x7FU);
    EXPECT_EQ(decode_tsc12(harness, 1U), 0x0456U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
