#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m2_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace elmasds::e2e::m2 {
namespace {

constexpr u32 kDoneSlot = 31U;
constexpr u32 kDoneMagic = 0xD04E3001U;
constexpr TickCount kDefaultSteps = 120'000ULL;

struct RtcReadReq {
    u8 cmd = 0x61U;
    u32 count = 1U;
    u32 start_slot = 0U;
};

struct RtcWriteReq {
    u8 cmd = 0x60U;
    std::span<const u8> data;
};

void emit_finish(ArmProgramEmitter& em) {
    emit_store_const_result(em, kDoneSlot, kDoneMagic);
    em.emit_branch_self();
}

void run_arm7(EmulatorE2eHarness& harness, const ArmProgramEmitter& em) {
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
}

void emit_rtc_send_cmd(ArmProgramEmitter& em, u8 cmd) {
    em.emit_write8_abs(kRegRtc, 0x00U);
    em.emit_write8_abs(kRegRtc, kRtcCs | kRtcSioWrite);
    for (u32 i = 0U; i < 8U; ++i) {
        const u8 bit = static_cast<u8>((cmd >> (7U - i)) & 1U);
        const u8 lo = static_cast<u8>(kRtcCs | kRtcSioWrite | bit);
        const u8 hi = static_cast<u8>(lo | kRtcSck);
        em.emit_write8_abs(kRegRtc, lo);
        em.emit_write8_abs(kRegRtc, hi);
    }
}

void emit_rtc_read_byte(ArmProgramEmitter& em, u32 slot) {
    em.emit_mov_imm(ArmReg::R4, 0U);
    for (u32 b = 0U; b < 8U; ++b) {
        em.emit_write8_abs(kRegRtc, kRtcCs);
        emit_read8_abs(em, ArmReg::R0, kRegRtc);
        em.emit_word(0xE2000001U);
        em.emit_word(0xE1844000U | (b << 7U));
        em.emit_write8_abs(kRegRtc, kRtcCs | kRtcSck);
    }
    emit_store_result(em, slot, ArmReg::R4);
}

void emit_rtc_write_byte(ArmProgramEmitter& em, u8 value) {
    for (u32 b = 0U; b < 8U; ++b) {
        const u8 bit = static_cast<u8>((value >> b) & 1U);
        const u8 lo = static_cast<u8>(kRtcCs | kRtcSioWrite | bit);
        const u8 hi = static_cast<u8>(lo | kRtcSck);
        em.emit_write8_abs(kRegRtc, lo);
        em.emit_write8_abs(kRegRtc, hi);
    }
}

void emit_rtc_read(ArmProgramEmitter& em, const RtcReadReq& req) {
    emit_rtc_send_cmd(em, req.cmd);
    for (u32 i = 0U; i < req.count; ++i) {
        emit_rtc_read_byte(em, req.start_slot + i);
    }
    em.emit_write8_abs(kRegRtc, 0x00U);
}

void emit_rtc_write(ArmProgramEmitter& em, const RtcWriteReq& req) {
    emit_rtc_send_cmd(em, req.cmd);
    for (const u8 byte : req.data) {
        emit_rtc_write_byte(em, byte);
    }
    em.emit_write8_abs(kRegRtc, 0x00U);
}

// F20: RTC (0x04000138) — Tier 1 Happy Path (5 tests)

TEST(Tier1RtcE2e, Status1DefaultAndWritableBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 1> status1_val{0x0EU};
    emit_rtc_read(em, RtcReadReq{0x61U, 1U, 0U});
    emit_rtc_write(em, RtcWriteReq{0x60U, status1_val});
    emit_rtc_read(em, RtcReadReq{0x61U, 1U, 1U});
    emit_finish(em);
    run_arm7(harness, em);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x02U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x0EU);
}

TEST(Tier1RtcE2e, DateTimeReadReflectsSetRtcDateTimeBcd) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    emit_rtc_read(em, RtcReadReq{0x65U, 7U, 0U});
    emit_finish(em);
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.set_rtc_datetime(RtcDateTime{26U, 11U, 25U, 3U, 14U, 37U, 52U});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x26U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x11U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x25U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x03U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x14U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)), 0x37U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(6U)), 0x52U);
}

TEST(Tier1RtcE2e, DateTimeAndTimeWriteReadRoundTrip) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 7> dt_bytes{0x25U, 0x12U, 0x31U, 0x04U, 0x23U, 0x59U, 0x58U};
    const std::array<u8, 3> time_bytes{0x09U, 0x15U, 0x30U};
    emit_rtc_write(em, RtcWriteReq{0x64U, dt_bytes});
    emit_rtc_read(em, RtcReadReq{0x65U, 7U, 0U});
    emit_rtc_read(em, RtcReadReq{0x67U, 3U, 7U});
    emit_rtc_write(em, RtcWriteReq{0x66U, time_bytes});
    emit_rtc_read(em, RtcReadReq{0x67U, 3U, 10U});
    emit_finish(em);
    run_arm7(harness, em);
    for (u32 i = 0U; i < 7U; ++i) {
        EXPECT_EQ(harness.read_main_ram32(result_addr(i)), dt_bytes[i]);
    }
    EXPECT_EQ(harness.read_main_ram32(result_addr(7U)), 0x23U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(8U)), 0x59U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(9U)), 0x58U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(10U)), 0x09U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(11U)), 0x15U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(12U)), 0x30U);
}

TEST(Tier1RtcE2e, Status2Alarm1Alarm2ClockAdjustAndFreeRegisters) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 1> status2_val{0xA5U};
    const std::array<u8, 3> alarm1_val{0x12U, 0x30U, 0x01U};
    const std::array<u8, 3> alarm2_val{0x07U, 0x45U, 0x02U};
    const std::array<u8, 1> adj_val{0x5AU};
    const std::array<u8, 1> free_val{0xC3U};
    emit_rtc_write(em, RtcWriteReq{0x62U, status2_val});
    emit_rtc_write(em, RtcWriteReq{0x68U, alarm1_val});
    emit_rtc_write(em, RtcWriteReq{0x6AU, alarm2_val});
    emit_rtc_write(em, RtcWriteReq{0x6CU, adj_val});
    emit_rtc_write(em, RtcWriteReq{0x6EU, free_val});
    emit_rtc_read(em, RtcReadReq{0x63U, 1U, 0U});
    emit_rtc_read(em, RtcReadReq{0x69U, 3U, 1U});
    emit_rtc_read(em, RtcReadReq{0x6BU, 3U, 4U});
    emit_rtc_read(em, RtcReadReq{0x6DU, 1U, 7U});
    emit_rtc_read(em, RtcReadReq{0x6FU, 1U, 8U});
    emit_finish(em);
    run_arm7(harness, em);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0xA5U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x12U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x30U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x01U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x07U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)), 0x45U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(6U)), 0x02U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(7U)), 0x5AU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(8U)), 0xC3U);
}

TEST(Tier1RtcE2e, BitReversedLsbFirstCommandFormat) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 1> free_val{0x55U};
    emit_rtc_write(em, RtcWriteReq{0x76U, free_val});
    emit_rtc_read(em, RtcReadReq{0xF6U, 1U, 0U});
    emit_rtc_read(em, RtcReadReq{0xA6U, 7U, 1U});
    emit_finish(em);
    boot_dual_program(harness, DualProgramSpec{{}, em.words()});
    harness.set_rtc_datetime(RtcDateTime{24U, 8U, 19U, 1U, 6U, 42U, 17U});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x55U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x24U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x08U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x19U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x01U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)), 0x06U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(6U)), 0x42U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(7U)), 0x17U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
