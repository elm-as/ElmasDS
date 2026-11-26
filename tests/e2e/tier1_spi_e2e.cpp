#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <span>
#include <utility>

namespace elmasds::e2e::m2 {
namespace {

constexpr u32 kDoneSlot = 31U;
constexpr u32 kDoneMagic = 0xD04E1001U;
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

struct FlashReadReq {
    Address addr = 0U;
    u32 count = 0U;
    u32 start_slot = 0U;
};

struct FlashWriteReq {
    u8 cmd = 0x02U;
    Address addr = 0U;
    std::span<const u8> data;
};

struct PmicOp {
    u8 reg_idx = 0U;
    bool is_read = true;
    u8 val_or_slot = 0U;
};

void emit_spi_to_slot(ArmProgramEmitter& em, const SpiStep& step) {
    emit_spi_byte_xfer(em, step.spicnt, step.tx);
    emit_store_result(em, step.slot, ArmReg::R0);
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

void expect_slots(const EmulatorE2eHarness& harness, u32 start, std::initializer_list<u32> vals) {
    u32 idx = 0U;
    for (const u32 v : vals) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(start + idx)), v) << "slot=" << idx;
        ++idx;
    }
}

void emit_flash_read(ArmProgramEmitter& em, const FlashReadReq& req) {
    emit_spi_byte_xfer(em, kFwHold, 0x03U);
    emit_spi_byte_xfer(em, kFwHold, static_cast<u8>((req.addr >> 16U) & 0xFFU));
    emit_spi_byte_xfer(em, kFwHold, static_cast<u8>((req.addr >> 8U) & 0xFFU));
    emit_spi_byte_xfer(em, kFwHold, static_cast<u8>(req.addr & 0xFFU));
    for (u32 i = 0U; i < req.count; ++i) {
        const u16 cnt = (i + 1U == req.count) ? kFwRel : kFwHold;
        emit_spi_to_slot(em, SpiStep{cnt, 0x00U, req.start_slot + i});
    }
}

void emit_flash_write(ArmProgramEmitter& em, const FlashWriteReq& req) {
    emit_spi_byte_xfer(em, kFwRel, 0x06U);
    emit_spi_byte_xfer(em, kFwHold, req.cmd);
    emit_spi_byte_xfer(em, kFwHold, static_cast<u8>((req.addr >> 16U) & 0xFFU));
    emit_spi_byte_xfer(em, kFwHold, static_cast<u8>((req.addr >> 8U) & 0xFFU));
    const u16 addr_cnt = req.data.empty() ? kFwRel : kFwHold;
    emit_spi_byte_xfer(em, addr_cnt, static_cast<u8>(req.addr));
    for (std::size_t i = 0U; i < req.data.size(); ++i) {
        emit_spi_byte_xfer(em, (i + 1U == req.data.size()) ? kFwRel : kFwHold, req.data[i]);
    }
}

void emit_pmic_rw(ArmProgramEmitter& em, const PmicOp& op) {
    const u8 cmd = op.is_read ? static_cast<u8>(0x80U | op.reg_idx) : op.reg_idx;
    emit_spi_byte_xfer(em, kPmHold, cmd);
    if (op.is_read) {
        emit_spi_to_slot(em, SpiStep{kPmRel, 0x00U, op.val_or_slot});
    } else {
        emit_spi_byte_xfer(em, kPmRel, op.val_or_slot);
    }
}

void emit_touch_ch(ArmProgramEmitter& em, u8 channel, u32 hi_slot) {
    emit_spi_byte_xfer(em, kTsHold, static_cast<u8>(0x80U | ((channel & 0x07U) << 4U)));
    emit_spi_to_slot(em, SpiStep{kTsHold, 0x00U, hi_slot});
    emit_spi_to_slot(em, SpiStep{kTsRel, 0x00U, hi_slot + 1U});
}

[[nodiscard]] u16 decode_tsc12(const EmulatorE2eHarness& harness, u32 hi_slot) {
    const u32 hi = harness.read_main_ram32(result_addr(hi_slot)) & 0x7FU;
    const u32 lo = (harness.read_main_ram32(result_addr(hi_slot + 1U)) >> 3U) & 0x1FU;
    return static_cast<u16>((hi << 5U) | lo);
}

// F16: SPICNT & SPIDATA — Tier 1 (7 test cases)

class Tier1SpiBaudTest : public ::testing::TestWithParam<u16> {};
TEST_P(Tier1SpiBaudTest, BaudRateAndCntReadback) {
    EmulatorE2eHarness harness;
    const u16 val = static_cast<u16>(kSpiEnable | kSpiCsHold | (1U << 10U) | GetParam());
    run_arm7_fn(harness, [val](ArmProgramEmitter& em) {
        em.emit_write16_abs(kRegSpiCnt, val);
        emit_read16_abs(em, ArmReg::R0, kRegSpiCnt);
        emit_store_result(em, 0U, ArmReg::R0);
    });
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), val);
}
INSTANTIATE_TEST_SUITE_P(Baud0To3, Tier1SpiBaudTest, ::testing::Values(0U, 1U, 2U, 3U));

TEST(Tier1SpiControllerE2e, Device3NoneReturns0xFF) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_to_slot(em, SpiStep{kSpiEnable | kSpiDeviceNone, 0x55U, 0U});
        emit_spi_to_slot(em, SpiStep{kSpiEnable | kSpiDeviceNone, 0x00U, 1U});
    });
    expect_slots(harness, 0U, {0xFFU, 0xFFU});
}

TEST(Tier1SpiControllerE2e, CsHoldMaintainsAndClearReleases) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kFwHold, 0x9FU);
        emit_spi_to_slot(em, SpiStep{kFwHold, 0x00U, 0U});
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 1U});
        emit_spi_byte_xfer(em, kFwRel, 0x9FU);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 2U});
    });
    expect_slots(harness, 0U, {0x20U, 0x40U, 0x00U});
}

TEST(Tier1SpiControllerE2e, SpiIrq23FiresWhenIrqAndEnableSet) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    emit_install_irq_trampoline(em, kArm7IrqSlot, kArm7DefaultEntry + 0x200U);
    emit_enable_interrupts(em, kIrqSpi);
    emit_spi_to_slot(em, SpiStep{kSpiEnable | kSpiIrqEnable | kSpiDeviceNone, 0xA5U, 0U});
    emit_delay_nop(em, 16U);
    emit_read32_abs(em, ArmReg::R0, kRegIf);
    emit_store_result(em, 3U, ArmReg::R0);
    emit_store_const_result(em, kDoneSlot, kDoneMagic);
    em.emit_branch_self();
    while (em.word_count() < 128U) {
        em.emit_nop();
    }
    em.emit_read32_abs(ArmReg::R0, kRegIf);
    emit_store_result(em, 1U, ArmReg::R0);
    emit_store_const_result(em, 2U, 1U);
    emit_ack_irq(em, kIrqSpi);
    emit_hle_irq_return(em);
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0xFFU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)) & kIrqSpi, kIrqSpi);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 1U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)) & kIrqSpi, 0U);
}

// F17: Firmware Flash — Tier 1 (6 test cases)

TEST(Tier1FirmwareFlashE2e, JedecIdReturns20401200) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kFwHold, 0x9FU);
        for (u32 i = 0U; i < 4U; ++i) {
            emit_spi_to_slot(em, SpiStep{(i == 3U) ? kFwRel : kFwHold, 0x00U, i});
        }
    });
    expect_slots(harness, 0U, {0x20U, 0x40U, 0x12U, 0x00U});
}

TEST(Tier1FirmwareFlashE2e, WrenAndWrdiToggleStatusWelBit) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_spi_byte_xfer(em, kFwHold, 0x05U);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 0U});
        emit_spi_byte_xfer(em, kFwRel, 0x06U);
        emit_spi_byte_xfer(em, kFwHold, 0x05U);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 1U});
        emit_spi_byte_xfer(em, kFwRel, 0x04U);
        emit_spi_byte_xfer(em, kFwHold, 0x05U);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 2U});
    });
    expect_slots(harness, 0U, {0x00U, 0x02U, 0x00U});
}

TEST(Tier1FirmwareFlashE2e, PageProgramAndPageWritePersistAndClearWren) {
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(FirmwareSpec{})).is_ok());
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_flash_write(em, FlashWriteReq{0x02U, 0x001000U, std::array<u8, 2>{0xABU, 0xCDU}});
        emit_spi_byte_xfer(em, kFwHold, 0x05U);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 0U});
        emit_flash_write(em, FlashWriteReq{0x0AU, 0x001002U, std::array<u8, 2>{0xEFU, 0x12U}});
        emit_flash_read(em, FlashReadReq{0x001000U, 4U, 1U});
    });
    expect_slots(harness, 0U, {0x00U, 0xABU, 0xCDU, 0xEFU, 0x12U});
}

class Tier1FirmwareSizeTest : public ::testing::TestWithParam<u32> {};

TEST_P(Tier1FirmwareSizeTest, ReadsSyntheticFirmwareHeaderAndUserSlot) {
    const u32 size = GetParam();
    EmulatorE2eHarness harness;
    FirmwareSpec spec{};
    spec.size_bytes = size;
    ASSERT_TRUE(harness.load_firmware(build_synthetic_firmware(spec)).is_ok());
    run_arm7_fn(harness, [size](ArmProgramEmitter& em) {
        emit_flash_read(em, FlashReadReq{0x000008U, 4U, 0U});
        emit_flash_read(em, FlashReadReq{0x000014U, 2U, 4U});
        emit_flash_read(em, FlashReadReq{size - 0x200U, 2U, 6U});
    });
    expect_slots(harness, 0U,
                 {'E', 'L', 'M', 'S', 0x17U, (size / kFirmwareSize128K) << 4U, 0x05U, 0x00U});
}

INSTANTIATE_TEST_SUITE_P(
    Sizes128KTo512K, Tier1FirmwareSizeTest,
    ::testing::Values(kFirmwareSize128K, kFirmwareSize256K, kFirmwareSize512K));

// F18: Powerman PMIC — Tier 1 (7 test cases)

class Tier1PowermanRegTest : public ::testing::TestWithParam<u8> {};
TEST_P(Tier1PowermanRegTest, DefaultAndWriteReadRoundTrip) {
    const u8 reg = GetParam();
    const u32 expected_default = (reg == 0U) ? 0x0DU : 0x00U;
    const u8 new_val = static_cast<u8>(0x30U + reg);
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [&](ArmProgramEmitter& em) {
        emit_pmic_rw(em, PmicOp{reg, true, 0U});
        emit_pmic_rw(em, PmicOp{reg, false, new_val});
        emit_pmic_rw(em, PmicOp{reg, true, 1U});
    });
    expect_slots(harness, 0U, {expected_default, new_val});
}
INSTANTIATE_TEST_SUITE_P(Regs0To4, Tier1PowermanRegTest, ::testing::Values(0U, 1U, 2U, 3U, 4U));

TEST(Tier1PowermanE2e, RegistersPersistAcrossOtherSpiDeviceAccess) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_pmic_rw(em, PmicOp{2U, false, 0x77U});
        emit_spi_byte_xfer(em, kFwHold, 0x9FU);
        emit_spi_to_slot(em, SpiStep{kFwRel, 0x00U, 0U});
        emit_pmic_rw(em, PmicOp{2U, true, 1U});
    });
    expect_slots(harness, 0U, {0x20U, 0x77U});
}

TEST(Tier1PowermanE2e, CsReleaseStartsFreshCommandOnNextTransfer) {
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [](ArmProgramEmitter& em) {
        emit_pmic_rw(em, PmicOp{1U, false, 0xA5U});
        emit_pmic_rw(em, PmicOp{1U, true, 0U});
        emit_pmic_rw(em, PmicOp{0U, true, 1U});
    });
    expect_slots(harness, 0U, {0xA5U, 0x0DU});
}

// F19: Touchscreen ADC TSC2046 — Tier 1 (9 test cases)

struct TouchXyCase {
    TouchInputState input{};
    u16 expected_y = 0U;
    u16 expected_x = 0U;
};

class Tier1TouchXyTest : public ::testing::TestWithParam<TouchXyCase> {};

TEST_P(Tier1TouchXyTest, ReadsYAndXForPressedAndReleasedStates) {
    EmulatorE2eHarness harness;
    auto build = [](ArmProgramEmitter& em) {
        emit_touch_ch(em, 1U, 0U);
        emit_touch_ch(em, 5U, 2U);
    };
    run_arm7_fn(harness, build, GetParam().input);
    EXPECT_EQ(decode_tsc12(harness, 0U), GetParam().expected_y);
    EXPECT_EQ(decode_tsc12(harness, 2U), GetParam().expected_x);
}

INSTANTIATE_TEST_SUITE_P(
    PressedAndReleased, Tier1TouchXyTest,
    ::testing::Values(TouchXyCase{{0x0456U, 0x0ABCU, true}, 0x0ABCU, 0x0456U},
                      TouchXyCase{{0x0123U, 0x0456U, false}, 0x0FFFU, 0x0000U}));

TEST(Tier1TouchscreenE2e, DynamicTouchStateUpdateBetweenSamples) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    emit_touch_ch(em, 1U, 0U);
    emit_store_const_result(em, 10U, 1U);
    const Address poll_pc = em.current_pc();
    em.emit_read32_abs(ArmReg::R0, result_addr(11U));
    em.emit_cmp_imm(ArmReg::R0, 0U);
    em.emit_b(poll_pc, ArmCond::Eq);
    emit_touch_ch(em, 1U, 2U);
    emit_touch_ch(em, 5U, 4U);
    emit_store_const_result(em, kDoneSlot, kDoneMagic);
    em.emit_branch_self();
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.set_touch_state(TouchInputState{0x0100U, 0x0200U, false});
    harness.step_ticks(30'000ULL);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    harness.set_touch_state(TouchInputState{0x0321U, 0x0654U, true});
    harness.write_main_ram32(result_addr(11U), 1U);
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(decode_tsc12(harness, 0U), 0x0FFFU);
    EXPECT_EQ(decode_tsc12(harness, 2U), 0x0654U);
    EXPECT_EQ(decode_tsc12(harness, 4U), 0x0321U);
}

class Tier1TouchChannelTest : public ::testing::TestWithParam<std::pair<u8, u16>> {};
TEST_P(Tier1TouchChannelTest, TempAndAuxChannelsReturnExpectedSample) {
    const auto [channel, expected] = GetParam();
    EmulatorE2eHarness harness;
    run_arm7_fn(harness, [channel](ArmProgramEmitter& em) { emit_touch_ch(em, channel, 0U); });
    EXPECT_EQ(decode_tsc12(harness, 0U), expected);
}
INSTANTIATE_TEST_SUITE_P(
    TempAndAuxChannels, Tier1TouchChannelTest,
    ::testing::Values(std::pair<u8, u16>{0U, 0x0700U}, std::pair<u8, u16>{2U, 0x0800U},
                      std::pair<u8, u16>{3U, 0x0800U}, std::pair<u8, u16>{4U, 0x0800U},
                      std::pair<u8, u16>{6U, 0x0800U}, std::pair<u8, u16>{7U, 0x0800U}));

}  // namespace
}  // namespace elmasds::e2e::m2
