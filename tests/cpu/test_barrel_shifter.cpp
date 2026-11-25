#include "cpu/arm_architecture/barrel_shifter.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(BarrelShifterTest, ImmediateShiftsHandleSpecialZeroEncodings) {
    // LSL #0 preserves carry_in.
    const auto lsl0 = shift_by_immediate(ShiftRequest{0x12345678U, 0, ShiftType::Lsl, true});
    EXPECT_EQ(lsl0.value, 0x12345678U);
    EXPECT_TRUE(lsl0.carry_out);

    // LSR #0 encodes LSR #32.
    const auto lsr32 = shift_by_immediate(ShiftRequest{0x80000001U, 0, ShiftType::Lsr, false});
    EXPECT_EQ(lsr32.value, 0U);
    EXPECT_TRUE(lsr32.carry_out);

    // ASR #0 encodes ASR #32.
    const auto asr32_neg = shift_by_immediate(ShiftRequest{0x80000000U, 0, ShiftType::Asr, false});
    EXPECT_EQ(asr32_neg.value, 0xFFFFFFFFU);
    EXPECT_TRUE(asr32_neg.carry_out);

    // ROR #0 encodes RRX (rotate right through carry).
    const auto rrx = shift_by_immediate(ShiftRequest{0x00000003U, 0, ShiftType::Ror, true});
    EXPECT_EQ(rrx.value, 0x80000001U);
    EXPECT_TRUE(rrx.carry_out);
}

TEST(BarrelShifterTest, RegisterShiftsHandleZeroAndLargeAmounts) {
    // Register shift by 0 preserves value and carry_in for all shift types (including ROR).
    const auto ror0 = shift_by_register(ShiftRequest{0x00000001U, 0, ShiftType::Ror, true});
    EXPECT_EQ(ror0.value, 0x00000001U);
    EXPECT_TRUE(ror0.carry_out);

    // LSL by 32 yields 0 with bit 0 in carry.
    const auto lsl32 = shift_by_register(ShiftRequest{0x00000001U, 32, ShiftType::Lsl, false});
    EXPECT_EQ(lsl32.value, 0U);
    EXPECT_TRUE(lsl32.carry_out);

    // LSL by 33 yields 0 with carry cleared.
    const auto lsl33 = shift_by_register(ShiftRequest{0xFFFFFFFFU, 33, ShiftType::Lsl, true});
    EXPECT_EQ(lsl33.value, 0U);
    EXPECT_FALSE(lsl33.carry_out);

    // ROR by 32 preserves value and sets carry to bit 31.
    const auto ror32 = shift_by_register(ShiftRequest{0x80000001U, 32, ShiftType::Ror, false});
    EXPECT_EQ(ror32.value, 0x80000001U);
    EXPECT_TRUE(ror32.carry_out);
}

TEST(BarrelShifterTest, DecodesRotatedImmediatesWithCarryRules) {
    const auto unrotated = decode_rotated_immediate(0xFFU, 0, true);
    EXPECT_EQ(unrotated.value, 0xFFU);
    EXPECT_TRUE(unrotated.carry_out);

    // imm8=0x01, rotate4=1 => ROR by 2 => 0x40000000, carry_out=bit31=0.
    const auto rot2 = decode_rotated_immediate(0x01U, 1, true);
    EXPECT_EQ(rot2.value, 0x40000000U);
    EXPECT_FALSE(rot2.carry_out);

    // imm8=0x02, rotate4=1 => ROR by 2 => 0x80000000, carry_out=bit31=1.
    const auto rot2_msb = decode_rotated_immediate(0x02U, 1, false);
    EXPECT_EQ(rot2_msb.value, 0x80000000U);
    EXPECT_TRUE(rot2_msb.carry_out);
}

}  // namespace
}  // namespace elmasds::cpu
