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
constexpr u32 kDoneMagic = 0xD04E4001U;
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

// F20: RTC (0x04000138) — Tier 2 Boundary/Edge/Negative (5 tests)

TEST(Tier2RtcE2e, Status1MaskAndResetBitClearAllControlRegisters) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 1> status1_hi{0xFEU};
    const std::array<u8, 1> status2_val{0xFFU};
    const std::array<u8, 3> alarm1_val{0x11U, 0x22U, 0x33U};
    const std::array<u8, 1> free_val{0xAAU};
    const std::array<u8, 1> reset_val{0x01U};
    emit_rtc_write(em, RtcWriteReq{0x60U, status1_hi});
    emit_rtc_read(em, RtcReadReq{0x61U, 1U, 0U});
    emit_rtc_write(em, RtcWriteReq{0x62U, status2_val});
    emit_rtc_write(em, RtcWriteReq{0x68U, alarm1_val});
    emit_rtc_write(em, RtcWriteReq{0x6EU, free_val});
    emit_rtc_write(em, RtcWriteReq{0x60U, reset_val});
    emit_rtc_read(em, RtcReadReq{0x61U, 1U, 1U});
    emit_rtc_read(em, RtcReadReq{0x63U, 1U, 2U});
    emit_rtc_read(em, RtcReadReq{0x69U, 3U, 3U});
    emit_rtc_read(em, RtcReadReq{0x6FU, 1U, 6U});
    emit_finish(em);
    run_arm7(harness, em);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x0EU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x02U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(3U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(5U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(6U)), 0x00U);
}

TEST(Tier2RtcE2e, DateTimeAndTimeMaskHourUpperTwoBits) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    const std::array<u8, 7> dt_val{0x26U, 0x05U, 0x10U, 0x02U, 0xD2U, 0x11U, 0x22U};
    const std::array<u8, 3> time_val{0xE3U, 0x44U, 0x55U};
    emit_rtc_write(em, RtcWriteReq{0x64U, dt_val});
    emit_rtc_read(em, RtcReadReq{0x65U, 7U, 0U});
    emit_rtc_write(em, RtcWriteReq{0x66U, time_val});
    emit_rtc_read(em, RtcReadReq{0x67U, 3U, 7U});
    emit_finish(em);
    run_arm7(harness, em);
    EXPECT_EQ(harness.read_main_ram32(result_addr(4U)), 0x12U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(7U)), 0x23U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(8U)), 0x44U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(9U)), 0x55U);
}

TEST(Tier2RtcE2e, CsLowAbortsPartialWriteAndResetsCommandPhase) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em(kArm7DefaultEntry);
    emit_rtc_send_cmd(em, 0x6EU);
    for (u32 b = 0U; b < 4U; ++b) {
        const u8 lo = static_cast<u8>(kRtcCs | kRtcSioWrite | 1U);
        em.emit_write8_abs(kRegRtc, lo);
        em.emit_write8_abs(kRegRtc, static_cast<u8>(lo | kRtcSck));
    }
    em.emit_write8_abs(kRegRtc, 0x00U);
    emit_rtc_read(em, RtcReadReq{0x6FU, 1U, 0U});

    em.emit_write8_abs(kRegRtc, kRtcCs | kRtcSioWrite);
    for (u32 i = 0U; i < 4U; ++i) {
        const u8 lo = static_cast<u8>(kRtcCs | kRtcSioWrite | 1U);
        em.emit_write8_abs(kRegRtc, lo);
        em.emit_write8_abs(kRegRtc, static_cast<u8>(lo | kRtcSck));
    }
    em.emit_write8_abs(kRegRtc, 0x00U);
    emit_rtc_read(em, RtcReadReq{0x61U, 1U, 1U});
    emit_finish(em);
    run_arm7(harness, em);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x02U);
}

TEST(Tier2RtcE2e, InvalidCommandIgnoredAndArm9AccessIsolated) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em9(kArm9DefaultEntry);
    em9.emit_write8_abs(kRegRtc, 0x15U);
    emit_store_const_result(em9, 10U, 1U);
    em9.emit_branch_self();

    ArmProgramEmitter em7(kArm7DefaultEntry);
    emit_delay_nop(em7, 64U);
    emit_rtc_read(em7, RtcReadReq{0x51U, 1U, 0U});
    emit_rtc_read(em7, RtcReadReq{0x61U, 1U, 1U});
    emit_finish(em7);

    boot_dual_program(harness, DualProgramSpec{em9.words(), em7.words()});
    harness.step_ticks(kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_addr(10U)), 1U);
    ASSERT_EQ(harness.read_main_ram32(result_addr(kDoneSlot)), kDoneMagic);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x02U);
}

TEST(Tier2RtcE2e, DirectBootResetsFreeRegisterAndBit7MasksIoReg) {
    EmulatorE2eHarness harness;
    ArmProgramEmitter em_set(kArm7DefaultEntry);
    const std::array<u8, 1> free_val{0x77U};
    emit_rtc_write(em_set, RtcWriteReq{0x6EU, free_val});
    emit_rtc_read(em_set, RtcReadReq{0x6FU, 1U, 0U});
    emit_finish(em_set);
    run_arm7(harness, em_set);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x77U);

    ArmProgramEmitter em_chk(kArm7DefaultEntry);
    emit_rtc_read(em_chk, RtcReadReq{0x6FU, 1U, 0U});
    em_chk.emit_write16_abs(kRegRtc, 0x00FFU);
    emit_read16_abs(em_chk, ArmReg::R0, kRegRtc);
    emit_store_result(em_chk, 1U, ArmReg::R0);
    em_chk.emit_write32_abs(kRegRtc, 0x00000080U);
    emit_read32_abs(em_chk, ArmReg::R0, kRegRtc);
    emit_store_result(em_chk, 2U, ArmReg::R0);
    emit_finish(em_chk);
    run_arm7(harness, em_chk);
    EXPECT_EQ(harness.read_main_ram32(result_addr(0U)), 0x00U);
    EXPECT_EQ(harness.read_main_ram32(result_addr(1U)), 0x7FU);
    EXPECT_EQ(harness.read_main_ram32(result_addr(2U)), 0x00U);
}

}  // namespace
}  // namespace elmasds::e2e::m2
